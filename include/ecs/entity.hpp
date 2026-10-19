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
    size_t operator()(Entity entity) const noexcept {
      uint64_t hash = (uint64_t(entity.index) << 32) | entity.generation;
      hash += 0x9e3779b97f4a7c15ull;
      hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ull;
      hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebull;
      return static_cast<size_t>(hash ^ (hash >> 31));
    }
  };

  struct EntityRecord {
    Archetype* archetype; // nullptr = morto ou reservado (pendente)
    uint32_t row;
    uint32_t generation;
  };
  static_assert(sizeof(EntityRecord) == 16, "NF-M1: 16 bytes por EntityRecord");

  // Dono dos records e da freeList. Fica aqui (e não no World) para que o CommandBuffer,
  // uma camada abaixo do World, possa reservar índices (R-CB-1) sem conhecê-lo.
  class EntityStore {
   private:
    std::vector<EntityRecord> records_;
    std::vector<uint32_t> freeList_;

   public:
    // Pega do fim da freeList (LIFO) ou cria um slot novo. Deixa archetype = nullptr (reservado, R-EN-3).
    Entity allocate() {
      uint32_t index;
      if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
      } else {
        index = static_cast<uint32_t>(records_.size());
        records_.push_back(EntityRecord{nullptr, 0, 0});
      }

      records_[index].archetype = nullptr;
      return Entity{index, records_[index].generation};
    }

    // Invalida o handle (geração + 1). Slot com geração UINT32_MAX é aposentado (R-EN-1).
    void release(uint32_t index) noexcept {
      EntityRecord& record = records_[index];
      record.archetype = nullptr;
      ++record.generation;

      if (record.generation != UINT32_MAX) {
        freeList_.push_back(index);
      }
    }

    bool isAlive(Entity entity) const noexcept {
      return entity.index < records_.size() && records_[entity.index].generation == entity.generation && records_[entity.index].archetype != nullptr;
    }
    bool isReserved(Entity entity) const noexcept {
      return entity.index < records_.size() && records_[entity.index].generation == entity.generation && records_[entity.index].archetype == nullptr;
    }

    EntityRecord& record(uint32_t index) noexcept {
      return records_[index];
    }
    const EntityRecord& record(uint32_t index) const noexcept {
      return records_[index];
    }

    uint32_t slotCount() const noexcept {
      return static_cast<uint32_t>(records_.size());
    }
    std::span<const uint32_t> freeList() const noexcept {
      return freeList_;
    }

    void setGenerationForTest(uint32_t index, uint32_t generation) noexcept {
      records_[index].generation = generation;
    }
  };
}
