#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ecs/archetype.hpp"
#include "ecs/command_buffer.hpp"
#include "ecs/detail/assert.hpp"
#include "ecs/entity.hpp"
#include "ecs/query.hpp"
#include "ecs/table.hpp"
#include "ecs/type_info.hpp"

namespace ecs {
  namespace detail {
    template <class...>
    inline constexpr bool distinctTypes = true;
    template <class T, class... Rest>
    inline constexpr bool distinctTypes<T, Rest...> = (!(std::is_same_v<T, Rest> || ...)) && distinctTypes<Rest...>;
  }

  struct ArchetypeStats {
    ArchetypeId id;
    Signature signature;
    uint32_t count, capacity, edgeCount;
    size_t columnBytes; // capacity × Σ size das colunas + capacity × sizeof(Entity)
    size_t idleBytes; // (capacity − count) × (Σ size + sizeof(Entity))
    size_t signatureBytes; // capacity() de signature + columnTypes, em bytes
    size_t edgeBytes; // capacityBytes() de addEdges/removeEdges + sourceToDestination
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
    Entity create(Ts&&... components);
    void destroy(Entity entity);
    bool isAlive(Entity entity) const noexcept {
      return store_.isAlive(entity);
    }
    bool isIterating() const noexcept {
      return iterating_ > 0;
    }

    template <class T>
    void add(Entity entity, T&& value); // ou atribui, se já tiver T
    template <class T>
    void remove(Entity entity);
    template <class T>
    bool has(Entity entity) const noexcept;
    template <class T>
    T& get(Entity entity);
    template <class T>
    T* tryGet(Entity entity) noexcept;

    void addRaw(Entity entity, ComponentId componentId, void* value); // move de `value`; ignorado para tags
    void removeRaw(Entity entity, ComponentId componentId);

    CommandBuffer commands() {
      return CommandBuffer(store_);
    }
    // Oráculo sem coalescing: aplica cada comando na ordem de gravação e limpa o buffer.
    void flushSequential(CommandBuffer& commandBuffer);
    // Duas passadas (R-CB-3): agrupa por entidade e faz uma única transição para o arquétipo final.
    void flush(CommandBuffer& commandBuffer);

    // Cacheada por (Ts..., filtros): mesma instância para a mesma combinação.
    template <class... Ts, class... Filters>
    Query<Ts...>& query(Filters... filters);
    void* getRaw(Entity entity, ComponentId componentId) noexcept {
      if (!store_.isAlive(entity)) {
        return nullptr;
      }

      const EntityRecord& record = store_.record(entity.index);
      if (componentId >= componentIndex_.size()) {
        return nullptr;
      }

      const uint16_t* column = componentIndex_[componentId].find(record.archetype->id);
      if (!column || *column == NoColumn) {
        return nullptr;
      }

      return record.archetype->table.column(*column).at(record.row);
    }

    uint64_t signatureLookupCount() const noexcept {
      return signatureLookups_;
    }
    uint32_t archetypeCount() const noexcept {
      return static_cast<uint32_t>(archetypes_.size());
    }
    WorldStats stats() const;

    // Verifica as invariantes I-1, I-3..I-7, I-10, I-11 (compilado em todos os builds).
    // Em falha escreve em *why o ID e o objeto (ex.: "I-3 e=4:1") e devolve false.
    bool checkInvariants(std::string* why = nullptr) const;

    bool hasRaw(Entity entity, ComponentId componentId) const noexcept {
      if (!store_.isAlive(entity) || componentId >= componentIndex_.size()) {
        return false;
      }

      return componentIndex_[componentId].find(store_.record(entity.index).archetype->id) != nullptr;
    }

   private:
    Archetype* findOrCreate(Signature signature);
    uint32_t moveEntity(Entity entity, EntityRecord& record, Archetype& destination, const Edge& edge);

    struct QueryKey {
      std::vector<uint64_t> components; // (componentId << 1) | isConst, na ordem de Ts
      Signature with, without;

      auto operator<=>(const QueryKey&) const = default;
    };
    QueryCacheBase* registerQuery(QueryKey key, std::unique_ptr<QueryCacheBase> query);
    const SmallMap<ArchetypeId, uint16_t>* smallestIndex(const Signature& with) const;

    const Edge& edgeFor(Archetype& source, ComponentId componentId, bool adding);
    const Edge& jumpEdge(Archetype& source, Archetype& destination);
    void materialize(Entity reserved);
    Entity place(Archetype& archetype); // a linha nasce morta: o chamador constrói as colunas
    Archetype& root() noexcept {
      return *archetypes_[0];
    }

    bool checkArchetypes(std::string* why) const;
    bool checkRows(const Archetype& archetype, std::string* why) const;
    bool checkRecords(std::string* why) const;
    bool checkFreeList(std::string* why) const;
    bool checkComponentIndex(std::string* why) const;
    bool checkQueries(std::string* why) const;
    bool checkEdges(std::string* why) const;

    EntityStore store_;
    std::vector<std::unique_ptr<Archetype>> archetypes_;
    std::unordered_map<Signature, Archetype*, SignatureHash> bySignature_;
    std::vector<SmallMap<ArchetypeId, uint16_t>> componentIndex_; // [componentId]: arquétipo → coluna (NoColumn p/ tag)
    uint32_t iterating_ = 0;
    mutable uint64_t signatureLookups_ = 0;
    std::map<QueryKey, std::unique_ptr<QueryCacheBase>> queryCache_;
    std::vector<QueryCacheBase*> queries_;
    std::unordered_map<uint64_t, Edge> jumpEdges_; // (source.id << 32) | destination.id
  };

  template <class... Ts>
  Entity World::create(Ts&&... components) {
    static_assert(detail::distinctTypes<std::remove_cvref_t<Ts>...>, "create<Ts...>: tipos repetidos");
    ECS_ASSERT(iterating_ == 0, "create durante iteração=%u", iterating_);

    Signature signature{componentId<Ts>()...};
    std::sort(signature.begin(), signature.end());
    Archetype* archetype = findOrCreate(std::move(signature));
    Entity entity = place(*archetype);

    const uint32_t row = store_.record(entity.index).row;
    (
      [&] {
        using U = std::remove_cvref_t<Ts>;
        if constexpr (!std::is_empty_v<U>) {
          void* slot = archetype->table.column(columnOf(*archetype, componentId<U>())).at(row);
          ::new (slot) U(std::forward<Ts>(components));
        }
      }(),
      ...);

    return entity;
  }

  template <class T>
  void World::add(Entity entity, T&& value) {
    using U = std::remove_cvref_t<T>;
    const ComponentId componentId = ecs::componentId<U>();
    ECS_ASSERT(isAlive(entity) && iterating_ == 0, "add e=%u:%u type=%.*s", entity.index, entity.generation,
               int(typeInfo(componentId).name.size()), typeInfo(componentId).name.data());

    if constexpr (std::is_empty_v<U>) {
      addRaw(entity, componentId, nullptr);
    } else {
      // Temporário construído ANTES de qualquer mudança estrutural: `value` pode apontar
      // para dentro do próprio World (ex.: get<B>(outra)).
      U temporary(std::forward<T>(value));
      addRaw(entity, componentId, &temporary);
    }
  }

  template <class T>
  void World::remove(Entity entity) {
    const ComponentId componentId = ecs::componentId<T>();
    ECS_ASSERT(isAlive(entity), "remove e=%u:%u type=%.*s", entity.index, entity.generation, int(typeInfo(componentId).name.size()),
               typeInfo(componentId).name.data());

    removeRaw(entity, componentId);
  }

  template <class T>
  bool World::has(Entity entity) const noexcept {
    return hasRaw(entity, componentId<T>());
  }

  template <class T>
  T& World::get(Entity entity) {
    using U = std::remove_cvref_t<T>;
    static_assert(!std::is_empty_v<U>, "get<Tag> não é permitido");
    const ComponentId componentId = ecs::componentId<U>();
    ECS_ASSERT(isAlive(entity) && hasRaw(entity, componentId), "get e=%u:%u type=%.*s", entity.index, entity.generation,
               int(typeInfo(componentId).name.size()), typeInfo(componentId).name.data());

    return *static_cast<U*>(getRaw(entity, componentId));
  }

  template <class T>
  T* World::tryGet(Entity entity) noexcept {
    using U = std::remove_cvref_t<T>;
    static_assert(!std::is_empty_v<U>, "tryGet<Tag> não é permitido");
    return static_cast<U*>(getRaw(entity, componentId<U>()));
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
    inline void sortUnique(std::vector<ComponentId>& ids) {
      std::sort(ids.begin(), ids.end());
      ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    }
  }

  template <class... Ts, class... Filters>
  Query<Ts...>& World::query(Filters... filters) {
    using Q = Query<Ts...>;

    Signature with{componentId<std::remove_const_t<Ts>>()...}, without;
    (detail::addFilter(with, without, filters), ...);
    detail::sortUnique(with);
    detail::sortUnique(without);

    QueryKey key{std::vector<uint64_t>{(uint64_t(componentId<std::remove_const_t<Ts>>()) << 1 | uint64_t(std::is_const_v<Ts>))...},
                 with, without};
    if (auto it = queryCache_.find(key); it != queryCache_.end()) {
      return static_cast<Q&>(*it->second);
    }

    auto created = std::make_unique<Q>(&iterating_, with, without);
    created->initialize(archetypes_, smallestIndex(with));
    Q& reference = *created;
    registerQuery(std::move(key), std::move(created));
    return reference;
  }
}
