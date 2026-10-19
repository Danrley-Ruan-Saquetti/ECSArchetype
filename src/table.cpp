#include "ecs/table.hpp"

#include <algorithm>

#include "ecs/detail/align.hpp"
#include "ecs/detail/assert.hpp"

namespace ecs {
  namespace {
    size_t columnAlign(const TypeInfo& ti) {
      return std::max<size_t>(ti.align, 64);
    }
  } // namespace

  Table::Table(std::span<const ComponentId> columnTypes)
    : columnTypes_(columnTypes.begin(), columnTypes.end()) {
    columns_.reserve(columnTypes.size());
    for (ComponentId id : columnTypes) {
      const TypeInfo& ti = typeInfo(id);
      ECS_ASSERT(!ti.tag, "Table::Table tipo=%.*s é tag", int(ti.name.size()), ti.name.data());
      columns_.push_back(Column{&ti, nullptr});
    }
  }

  void Table::releaseAll() noexcept {
    for (Column& c : columns_) {
      if (!c.data)
        continue;

      if (!c.info->trivial)
        for (uint32_t r = 0; r < count_; ++r)
          c.info->destroy(c.at(r));

      alignedFree(c.data, columnAlign(*c.info));
      c.data = nullptr;
    }

    count_ = capacity_ = 0;
    entities_.clear();
  }

  Table::~Table() {
    releaseAll();
  }

  Table::Table(Table&& o) noexcept
    : columns_(std::move(o.columns_)),
      columnTypes_(std::move(o.columnTypes_)),
      entities_(std::move(o.entities_)),
      count_(o.count_),
      capacity_(o.capacity_) {
    o.columns_.clear();
    o.entities_.clear();
    o.count_ = o.capacity_ = 0;
  }

  Table& Table::operator=(Table&& o) noexcept {
    if (this != &o) {
      releaseAll();

      columns_ = std::move(o.columns_);
      columnTypes_ = std::move(o.columnTypes_);
      entities_ = std::move(o.entities_);
      count_ = o.count_;
      capacity_ = o.capacity_;

      o.columns_.clear();
      o.entities_.clear();
      o.count_ = o.capacity_ = 0;
    }

    return *this;
  }

  void Table::reserve(uint32_t newCap) {
    entities_.reserve(newCap); // NF-P4: push sem alocar até a capacidade

    // Aloca tudo antes de mover qualquer coisa: se uma alocação lançar, nada foi tocado.
    std::vector<std::byte*> fresh(columns_.size(), nullptr);
    try {
      for (size_t i = 0; i < columns_.size(); ++i)
        fresh[i] = alignedAlloc(size_t(newCap) * columns_[i].info->size, columnAlign(*columns_[i].info));
    } catch (...) {
      for (size_t i = 0; i < columns_.size(); ++i)
        if (fresh[i])
          alignedFree(fresh[i], columnAlign(*columns_[i].info));
      throw;
    }

    for (size_t i = 0; i < columns_.size(); ++i) {
      Column& c = columns_[i];
      if (c.data) {
        if (c.info->trivial) {
          if (count_ > 0)
            std::memcpy(fresh[i], c.data, size_t(count_) * c.info->size);
        } else {
          for (uint32_t r = 0; r < count_; ++r) {
            std::byte* dst = fresh[i] + size_t(r) * c.info->size;
            c.info->moveConstruct(dst, c.at(r));
            c.info->destroy(c.at(r));
          }
        }

        alignedFree(c.data, columnAlign(*c.info));
      }

      c.data = fresh[i];
    }

    capacity_ = newCap;
  }

  uint32_t Table::pushUninit(Entity e) {
    if (count_ == capacity_)
      reserve(std::max<uint32_t>(64, capacity_ * 2));

    entities_.push_back(e);
    return count_++;
  }

  void Table::destroyRow(uint32_t r) noexcept {
    for (Column& c : columns_)
      destroyValue(*c.info, c.at(r));
  }

  std::optional<Entity> Table::fillHole(uint32_t r) noexcept {
    const uint32_t last = count_ - 1;
    --count_;
    if (r == last) {
      entities_.pop_back();
      return std::nullopt;
    }

    for (Column& c : columns_)
      relocateValue(*c.info, c.at(r), c.at(last));

    entities_[r] = entities_.back();
    entities_.pop_back();
    return entities_[r];
  }
} // namespace ecs
