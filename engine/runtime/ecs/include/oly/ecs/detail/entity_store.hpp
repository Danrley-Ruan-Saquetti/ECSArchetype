#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "oly/ecs/entity.hpp"

namespace oly::ecs::detail {
  struct Archetype;

  struct EntityRecord {
    Archetype* archetype; // nullptr = morto ou reservado (pendente)
    uint32_t row;
    uint32_t generation;
  };
  static_assert(sizeof(EntityRecord) == sizeof(void*) + 8, "NF-M1: um ponteiro + 8 bytes por EntityRecord");

  // Dono dos records e da freeList. Fica aqui (e não no World) para que o CommandBuffer,
  // uma camada abaixo do World, possa reservar índices (R-CB-1) sem conhecê-lo.
  class EntityStore {
   private:
    friend struct EntityStoreTestAccess;

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
  };
}
