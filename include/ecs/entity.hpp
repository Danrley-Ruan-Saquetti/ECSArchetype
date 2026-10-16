#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace ecs {

struct Archetype;

struct Entity {
    uint32_t index;
    uint32_t generation;
    friend bool operator==(Entity, Entity) = default;
};
inline constexpr Entity NullEntity{UINT32_MAX, 0};

struct EntityHash {
    size_t operator()(Entity e) const noexcept {
        uint64_t x = (uint64_t(e.index) << 32) | e.generation;
        x += 0x9e3779b97f4a7c15ull;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        return static_cast<size_t>(x ^ (x >> 31));
    }
};

struct EntityRecord {
    Archetype* arch;      // nullptr = morto ou reservado (pendente)
    uint32_t row;
    uint32_t generation;  // geração atual do slot
};
static_assert(sizeof(EntityRecord) == 16, "NF-M1: 16 bytes por EntityRecord");

// Dono dos records e da freeList. Fica aqui (e não no World) para que o CommandBuffer,
// uma camada abaixo do World, possa reservar índices (R-CB-1) sem conhecê-lo.
class EntityStore {
public:
    // Pega do fim da freeList (LIFO) ou cria um slot novo. Deixa arch = nullptr (reservado, R-EN-3).
    Entity allocate() {
        uint32_t idx;
        if (!freeList_.empty()) {
            idx = freeList_.back();
            freeList_.pop_back();
        } else {
            idx = static_cast<uint32_t>(records_.size());
            records_.push_back(EntityRecord{nullptr, 0, 0});
        }
        records_[idx].arch = nullptr;
        return Entity{idx, records_[idx].generation};
    }

    // Invalida o handle (geração + 1). Slot com geração UINT32_MAX é aposentado (R-EN-1).
    void release(uint32_t index) noexcept {
        EntityRecord& r = records_[index];
        r.arch = nullptr;
        ++r.generation;
        if (r.generation != UINT32_MAX) freeList_.push_back(index);
    }

    bool isAlive(Entity e) const noexcept {
        return e.index < records_.size() && records_[e.index].generation == e.generation &&
               records_[e.index].arch != nullptr;
    }
    bool isReserved(Entity e) const noexcept {
        return e.index < records_.size() && records_[e.index].generation == e.generation &&
               records_[e.index].arch == nullptr;
    }

    EntityRecord& record(uint32_t index) noexcept { return records_[index]; }
    const EntityRecord& record(uint32_t index) const noexcept { return records_[index]; }

    uint32_t slotCount() const noexcept { return static_cast<uint32_t>(records_.size()); }
    std::span<const uint32_t> freeList() const noexcept { return freeList_; }

    void setGenerationForTest(uint32_t index, uint32_t gen) noexcept { records_[index].generation = gen; }

private:
    std::vector<EntityRecord> records_;
    std::vector<uint32_t> freeList_;
};

}  // namespace ecs
