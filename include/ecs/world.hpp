#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <ecs/archetype.hpp>
#include <ecs/command_buffer.hpp>
#include <ecs/detail/assert.hpp>
#include <ecs/entity.hpp>
#include <ecs/query.hpp>
#include <ecs/table.hpp>
#include <ecs/type_info.hpp>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ecs {

  namespace detail {
    template <class...>
    inline constexpr bool distinctTypes = true;
    template <class T, class... Rest>
    inline constexpr bool distinctTypes<T, Rest...> = (!(std::is_same_v<T, Rest> || ...)) && distinctTypes<Rest...>;
  } // namespace detail

  struct ArchetypeStats {
    ArchetypeId id;
    Signature signature;
    uint32_t count, capacity, edgeCount;
    size_t columnBytes; // capacity × Σ size das colunas + capacity × sizeof(Entity)
    size_t idleBytes; // (capacity − count) × (Σ size + sizeof(Entity))
    size_t signatureBytes; // capacity() de signature + columnTypes, em bytes
    size_t edgeBytes; // capacityBytes() de addEdges/removeEdges + srcToDst
  };

  struct WorldStats {
    uint32_t archetypeCount;
    uint32_t liveEntities;
    std::vector<ArchetypeStats> archetypes; // por id
    std::array<uint32_t, 33> histogram; // [0] = arquétipos vazios; [i] = count em [2^(i-1), 2^i)
    uint64_t signatureLookups;
  };

  class World {
   public:
    World();
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Entity create();
    // Nasce direto no arquétipo final. Lvalues são copiados.
    template <class... Ts>
    Entity create(Ts&&... comps);
    void destroy(Entity e);
    bool isAlive(Entity e) const noexcept {
      return store_.isAlive(e);
    }
    bool isIterating() const noexcept {
      return iterating_ > 0;
    }

    template <class T>
    void add(Entity e, T&& value); // ou atribui, se já tiver T
    template <class T>
    void remove(Entity e);
    template <class T>
    bool has(Entity e) const noexcept;
    template <class T>
    T& get(Entity e);
    template <class T>
    T* tryGet(Entity e) noexcept;

    // Núcleo não template.
    void addRaw(Entity e, ComponentId cid, void* value); // move de `value`; ignorado para tags
    void removeRaw(Entity e, ComponentId cid);

    CommandBuffer commands() {
      return CommandBuffer(store_);
    }
    // Oráculo sem coalescing: aplica cada comando na ordem de gravação e limpa o buffer.
    void flushSequential(CommandBuffer& cb);
    // Duas passadas (R-CB-3): agrupa por entidade e faz uma única transição para o arquétipo final.
    void flush(CommandBuffer& cb);

    // Cacheada por (Ts..., filtros): mesma instância para a mesma combinação.
    template <class... Ts, class... Filters>
    Query<Ts...>& query(Filters... filters);
    void* getRaw(Entity e, ComponentId cid) noexcept {
      if (!store_.isAlive(e))
        return nullptr;
      const EntityRecord& rec = store_.record(e.index);
      if (cid >= componentIndex_.size())
        return nullptr;
      const uint16_t* col = componentIndex_[cid].find(rec.arch->id);
      if (!col || *col == NoColumn)
        return nullptr;
      return rec.arch->table.column(*col).at(rec.row);
    }
    uint64_t signatureLookupCount() const noexcept {
      return sigLookups_;
    }
    uint32_t archetypeCount() const noexcept {
      return static_cast<uint32_t>(archetypes_.size());
    }
    WorldStats stats() const;

    // Verifica as invariantes I-1, I-3..I-7, I-10, I-11 (compilado em todos os builds).
    // Em falha escreve em *why o ID e o objeto (ex.: "I-3 e=4:1") e devolve false.
    bool checkInvariants(std::string* why = nullptr) const;

    bool hasRaw(Entity e, ComponentId cid) const noexcept {
      if (!store_.isAlive(e) || cid >= componentIndex_.size())
        return false;
      return componentIndex_[cid].find(store_.record(e.index).arch->id) != nullptr;
    }

   private:
    Archetype* findOrCreate(Signature sig);
    uint32_t moveEntity(Entity e, EntityRecord& rec, Archetype& dst, const Edge& edge);
    struct QueryKey {
      std::vector<uint64_t> ts; // (cid << 1) | isConst, na ordem de Ts
      Signature with, without;
      auto operator<=>(const QueryKey&) const = default;
    };
    QueryCacheBase* registerQuery(QueryKey key, std::unique_ptr<QueryCacheBase> q);
    const SmallMap<ArchetypeId, uint16_t>* smallestIndex(const Signature& with) const;

    const Edge& edgeFor(Archetype& src, ComponentId cid, bool adding);
    const Edge& jumpEdge(Archetype& src, Archetype& dst);
    void materialize(Entity e); // põe uma entidade reservada no arquétipo raiz
    Entity place(Archetype& a); // aloca entidade e linha morta em `a`

    EntityStore store_;
    std::vector<std::unique_ptr<Archetype>> archetypes_;
    std::unordered_map<Signature, Archetype*, SigHash> bySignature_;
    std::vector<SmallMap<ArchetypeId, uint16_t>> componentIndex_; // [cid]: arquétipo → coluna (NoColumn p/ tag)
    uint32_t iterating_ = 0;
    mutable uint64_t sigLookups_ = 0;
    std::map<QueryKey, std::unique_ptr<QueryCacheBase>> queryCache_;
    std::vector<QueryCacheBase*> queries_;
    std::unordered_map<uint64_t, Edge> jumpEdges_; // (src.id << 32) | dst.id
  };

  template <class... Ts>
  Entity World::create(Ts&&... comps) {
    static_assert(detail::distinctTypes<std::remove_cvref_t<Ts>...>, "create<Ts...>: tipos repetidos");
    ECS_ASSERT(iterating_ == 0, "create durante iteração=%u", iterating_);
    Signature sig{componentId<Ts>()...};
    std::sort(sig.begin(), sig.end());
    Archetype* a = findOrCreate(std::move(sig));
    Entity e = place(*a);
    const uint32_t row = store_.record(e.index).row;
    (
      [&] {
        using U = std::remove_cvref_t<Ts>;
        if constexpr (!std::is_empty_v<U>) {
          void* p = a->table.column(columnOf(*a, componentId<U>())).at(row);
          ::new (p) U(std::forward<Ts>(comps));
        }
      }(),
      ...);
    return e;
  }

  template <class T>
  void World::add(Entity e, T&& value) {
    using U = std::remove_cvref_t<T>;
    const ComponentId cid = componentId<U>();
    ECS_ASSERT(isAlive(e) && iterating_ == 0, "add e=%u:%u type=%.*s", e.index, e.generation,
               int(typeInfo(cid).name.size()), typeInfo(cid).name.data());
    if constexpr (std::is_empty_v<U>) {
      addRaw(e, cid, nullptr);
    } else {
      // Temporário construído ANTES de qualquer mudança estrutural: `value` pode apontar
      // para dentro do próprio World (ex.: get<B>(outra)).
      U tmp(std::forward<T>(value));
      addRaw(e, cid, &tmp);
    }
  }

  template <class T>
  void World::remove(Entity e) {
    const ComponentId cid = componentId<T>();
    ECS_ASSERT(isAlive(e), "remove e=%u:%u type=%.*s", e.index, e.generation, int(typeInfo(cid).name.size()),
               typeInfo(cid).name.data());
    removeRaw(e, cid);
  }

  template <class T>
  bool World::has(Entity e) const noexcept {
    return hasRaw(e, componentId<T>());
  }

  template <class T>
  T& World::get(Entity e) {
    using U = std::remove_cvref_t<T>;
    static_assert(!std::is_empty_v<U>, "get<Tag> não é permitido");
    const ComponentId cid = componentId<U>();
    ECS_ASSERT(isAlive(e) && hasRaw(e, cid), "get e=%u:%u type=%.*s", e.index, e.generation,
               int(typeInfo(cid).name.size()), typeInfo(cid).name.data());
    return *static_cast<U*>(getRaw(e, cid));
  }

  template <class T>
  T* World::tryGet(Entity e) noexcept {
    using U = std::remove_cvref_t<T>;
    static_assert(!std::is_empty_v<U>, "tryGet<Tag> não é permitido");
    return static_cast<U*>(getRaw(e, componentId<U>()));
  }

  namespace detail {
    template <class... Us>
    void addFilter(std::vector<ComponentId>& with, std::vector<ComponentId>&, With<Us...>) {
      (with.push_back(componentId<Us>()), ...);
    }
    template <class... Us>
    void addFilter(std::vector<ComponentId>&, std::vector<ComponentId>& without, Without<Us...>) {
      (without.push_back(componentId<Us>()), ...);
    }
    inline void sortUnique(std::vector<ComponentId>& v) {
      std::sort(v.begin(), v.end());
      v.erase(std::unique(v.begin(), v.end()), v.end());
    }
  } // namespace detail

  template <class... Ts, class... Filters>
  Query<Ts...>& World::query(Filters... filters) {
    using Q = Query<Ts...>;
    Signature with{componentId<std::remove_const_t<Ts>>()...}, without;
    (detail::addFilter(with, without, filters), ...);
    detail::sortUnique(with);
    detail::sortUnique(without);
    QueryKey key{std::vector<uint64_t>{(uint64_t(componentId<std::remove_const_t<Ts>>()) << 1 | uint64_t(std::is_const_v<Ts>))...},
                 with, without};
    if (auto it = queryCache_.find(key); it != queryCache_.end())
      return static_cast<Q&>(*it->second);
    auto q = std::make_unique<Q>(&iterating_, with, without);
    q->initialize(archetypes_, smallestIndex(with));
    Q& ref = *q;
    registerQuery(std::move(key), std::move(q));
    return ref;
  }

} // namespace ecs
