#include "oly/ecs/detail/table.hpp"

#include <algorithm>

#include "oly/core/align.hpp"
#include "oly/core/assert.hpp"

namespace oly::ecs::detail {
  namespace {
    constexpr size_t kCacheLineBytes = 64;
    constexpr uint32_t kInitialRowCapacity = 64;

    // R-TB-3: alinhada pelo menos à linha de cache, para favorecer a vetorização.
    size_t columnAlignment(const TypeInfo& info) {
      return std::max<size_t>(info.alignment, kCacheLineBytes);
    }
  }

  Table::Table(std::span<const ComponentId> columnTypes)
    : columnTypes_(columnTypes.begin(), columnTypes.end()) {
    columns_.reserve(columnTypes.size());
    for (ComponentId id : columnTypes) {
      const TypeInfo& info = typeInfo(id);
      OLY_ASSERT(!info.tag, "Table::Table tipo=%.*s é tag", int(info.name.size()), info.name.data());
      columns_.push_back(Column{&info, nullptr});
    }
  }

  void Table::releaseAll() noexcept {
    for (Column& column : columns_) {
      if (!column.data) {
        continue;
      }

      if (!column.info->trivial) {
        for (uint32_t row = 0; row < count_; ++row) {
          column.info->destroy(column.at(row));
        }
      }

      core::alignedFree(column.data, columnAlignment(*column.info));
      column.data = nullptr;
    }

    count_ = capacity_ = 0;
    entities_.clear();
  }

  Table::~Table() {
    releaseAll();
  }

  Table::Table(Table&& other) noexcept
    : columns_(std::move(other.columns_)),
      columnTypes_(std::move(other.columnTypes_)),
      entities_(std::move(other.entities_)),
      count_(other.count_),
      capacity_(other.capacity_) {
    other.columns_.clear();
    other.entities_.clear();
    other.count_ = other.capacity_ = 0;
  }

  Table& Table::operator=(Table&& other) noexcept {
    if (this != &other) {
      releaseAll();

      columns_ = std::move(other.columns_);
      columnTypes_ = std::move(other.columnTypes_);
      entities_ = std::move(other.entities_);
      count_ = other.count_;
      capacity_ = other.capacity_;

      other.columns_.clear();
      other.entities_.clear();
      other.count_ = other.capacity_ = 0;
    }

    return *this;
  }

  void Table::reserve(uint32_t newCapacity) {
    entities_.reserve(newCapacity); // NF-P4: push sem alocar até a capacidade

    // Aloca tudo antes de mover qualquer coisa: se uma alocação lançar, nada foi tocado.
    std::vector<std::byte*> fresh(columns_.size(), nullptr);
    try {
      for (size_t i = 0; i < columns_.size(); ++i) {
        fresh[i] = core::alignedAllocate(size_t(newCapacity) * columns_[i].info->size, columnAlignment(*columns_[i].info));
      }
    } catch (...) {
      for (size_t i = 0; i < columns_.size(); ++i) {
        if (fresh[i]) {
          core::alignedFree(fresh[i], columnAlignment(*columns_[i].info));
        }
      }
      throw;
    }

    for (size_t i = 0; i < columns_.size(); ++i) {
      Column& column = columns_[i];
      if (column.data) {
        if (column.info->trivial) {
          if (count_ > 0) {
            std::memcpy(fresh[i], column.data, size_t(count_) * column.info->size);
          }
        } else {
          for (uint32_t row = 0; row < count_; ++row) {
            std::byte* destination = fresh[i] + size_t(row) * column.info->size;
            column.info->moveConstruct(destination, column.at(row));
            column.info->destroy(column.at(row));
          }
        }

        core::alignedFree(column.data, columnAlignment(*column.info));
      }

      column.data = fresh[i];
    }

    capacity_ = newCapacity;
  }

  uint32_t Table::pushUninitialized(Entity entity) {
    if (count_ == capacity_) {
      reserve(std::max<uint32_t>(kInitialRowCapacity, capacity_ * 2));
    }

    entities_.push_back(entity);
    return count_++;
  }

  void Table::destroyRow(uint32_t row) noexcept {
    for (Column& column : columns_) {
      destroyValue(*column.info, column.at(row));
    }
  }

  std::optional<Entity> Table::fillHole(uint32_t row) noexcept {
    const uint32_t last = count_ - 1;
    --count_;
    if (row == last) {
      entities_.pop_back();
      return std::nullopt;
    }

    for (Column& column : columns_) {
      relocateValue(*column.info, column.at(row), column.at(last));
    }

    entities_[row] = entities_.back();
    entities_.pop_back();
    return entities_[row];
  }
}
