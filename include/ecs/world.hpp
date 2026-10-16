#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <ecs/archetype.hpp>
#include <ecs/detail/assert.hpp>
#include <ecs/entity.hpp>
#include <ecs/table.hpp>
#include <ecs/type_info.hpp>

namespace ecs {

namespace detail {
template <class...>
inline constexpr bool distinctTypes = true;
template <class T, class... Rest>
inline constexpr bool distinctTypes<T, Rest...> = (!(std::is_same_v<T, Rest> || ...)) && distinctTypes<Rest...>;
}  // namespace detail

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
    bool isAlive(Entity e) const noexcept { return store_.isAlive(e); }

    template <class T>
    void add(Entity e, T&& value);  // ou atribui, se já tiver T
    template <class T>
    void remove(Entity e);
    template <class T>
    bool has(Entity e) const noexcept;
    template <class T>
    T& get(Entity e);
    template <class T>
    T* tryGet(Entity e) noexcept;

    // Núcleo não template.
    void addRaw(Entity e, ComponentId cid, void* value);  // move de `value`; ignorado para tags
    void removeRaw(Entity e, ComponentId cid);
    void* getRaw(Entity e, ComponentId cid) noexcept {
        if (!store_.isAlive(e)) return nullptr;
        const EntityRecord& rec = store_.record(e.index);
        if (cid >= componentIndex_.size()) return nullptr;
        const uint16_t* col = componentIndex_[cid].find(rec.arch->id);
        if (!col || *col == NoColumn) return nullptr;
        return rec.arch->table.column(*col).at(rec.row);
    }
    uint64_t signatureLookupCount() const noexcept { return sigLookups_; }

    // Verifica as invariantes I-1, I-3..I-7, I-10, I-11 (compilado em todos os builds).
    // Em falha escreve em *why o ID e o objeto (ex.: "I-3 e=4:1") e devolve false.
    bool checkInvariants(std::string* why = nullptr) const;

    bool hasRaw(Entity e, ComponentId cid) const noexcept {
        if (!store_.isAlive(e) || cid >= componentIndex_.size()) return false;
        return componentIndex_[cid].find(store_.record(e.index).arch->id) != nullptr;
    }

private:
    Archetype* findOrCreate(Signature sig);
    uint32_t moveEntity(Entity e, EntityRecord& rec, Archetype& dst, const Edge& edge);
    const Edge& edgeFor(Archetype& src, ComponentId cid, bool adding);
    Entity place(Archetype& a);  // aloca entidade e linha morta em `a`

    EntityStore store_;
    std::vector<std::unique_ptr<Archetype>> archetypes_;
    std::unordered_map<Signature, Archetype*, SigHash> bySignature_;
    std::vector<SmallMap<ArchetypeId, uint16_t>> componentIndex_;  // [cid]: arquétipo → coluna (NoColumn p/ tag)
    uint32_t iterating_ = 0;
    mutable uint64_t sigLookups_ = 0;
};

template <class... Ts>
Entity World::create(Ts&&... comps) {
    static_assert(detail::distinctTypes<std::remove_cvref_t<Ts>...>, "create<Ts...>: tipos repetidos");
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

}  // namespace ecs
