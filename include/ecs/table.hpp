#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include "ecs/entity.hpp"
#include "ecs/type_info.hpp"

namespace ecs {
  // `destination` não pode estar construído; `source` termina destruído.
  inline void relocateValue(const TypeInfo& info, void* destination, void* source) noexcept {
    if (info.trivial) {
      std::memcpy(destination, source, info.size);
    } else {
      info.moveConstruct(destination, source);
      info.destroy(source);
    }
  }
  inline void destroyValue(const TypeInfo& info, void* value) noexcept {
    if (!info.trivial) {
      info.destroy(value);
    }
  }

  struct Column {
    const TypeInfo* info;
    std::byte* data = nullptr;

    void* at(uint32_t row) noexcept {
      return data + size_t(row) * info->size;
    }
    const void* at(uint32_t row) const noexcept {
      return data + size_t(row) * info->size;
    }
  };

  class Table {
   private:
    std::vector<Column> columns_;
    std::vector<ComponentId> columnTypes_;
    std::vector<Entity> entities_;
    uint32_t count_ = 0, capacity_ = 0;

   public:
    explicit Table(std::span<const ComponentId> columnTypes);
    ~Table();
    Table(Table&&) noexcept;
    Table& operator=(Table&&) noexcept;
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;

    uint32_t count() const noexcept {
      return count_;
    }
    uint32_t capacity() const noexcept {
      return capacity_;
    }

    uint32_t pushUninitialized(Entity entity); // linha nova MORTA no fim; pode realocar
    void destroyRow(uint32_t row) noexcept; // linha viva → morta
    std::optional<Entity> fillHole(uint32_t row) noexcept; // tapa linha morta com a última

    std::span<const Entity> entities() const noexcept {
      return entities_;
    }
    Column& column(uint16_t index) noexcept {
      return columns_[index];
    }
    const Column& column(uint16_t index) const noexcept {
      return columns_[index];
    }
    uint16_t columnCount() const noexcept {
      return static_cast<uint16_t>(columns_.size());
    }
    std::span<const ComponentId> columnTypes() const noexcept {
      return columnTypes_;
    }

   private:
    void reserve(uint32_t newCapacity);
    void releaseAll() noexcept;
  };
}
