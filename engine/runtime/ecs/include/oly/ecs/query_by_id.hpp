#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "oly/core/assert.hpp"
#include "oly/core/small_map.hpp"
#include "oly/ecs/component.hpp"
#include "oly/ecs/detail/archetype.hpp"
#include "oly/ecs/detail/query_cache.hpp"
#include "oly/ecs/entity.hpp"

namespace oly::ecs {
  class QueryById;
  class World;

  // `read` and `write`: registered components, with data and without repetition (OLY_ASSERT); the order of both,
  // in that sequence, defines the columns of BlockById. `with` and `without` only filter and accept tags.
  struct QueryByIdSpec {
    std::vector<ComponentId> read{}, write{}, with{}, without{};
  };

  // Lightweight value over a block's descriptor; valid while the originating QueryIteration exists.
  class BlockById {
   private:
    const uintptr_t* descriptor_;

   public:
    explicit BlockById(const uintptr_t* descriptor) noexcept
      : descriptor_(descriptor) {}

    uint32_t count() const noexcept {
      return static_cast<uint32_t>(descriptor_[0]);
    }
    std::span<const Entity> entities() const noexcept {
      return {reinterpret_cast<const Entity*>(descriptor_[1]), count()};
    }
    // `index` in read-then-write order.
    void* column(size_t index) const noexcept {
      return reinterpret_cast<void*>(descriptor_[2 + index]);
    }
  };

  // Marks the World as iterating until destroyed. Move-only; the moved-from object does not end it again.
  // Nested iterations must end in reverse order of opening (OLY_ASSERT).
  class QueryIteration {
   private:
    friend class QueryById;

    QueryById* query_;
    const uintptr_t* descriptors_;
    uint32_t blockCount_;
    uint32_t stride_;
    uint32_t level_;

   public:
    ~QueryIteration();
    QueryIteration(QueryIteration&& other) noexcept
      : query_(std::exchange(other.query_, nullptr)),
        descriptors_(other.descriptors_),
        blockCount_(other.blockCount_),
        stride_(other.stride_),
        level_(other.level_) {}
    QueryIteration& operator=(QueryIteration&&) = delete;
    QueryIteration(const QueryIteration&) = delete;
    QueryIteration& operator=(const QueryIteration&) = delete;

    uint32_t blockCount() const noexcept {
      return blockCount_;
    }
    BlockById block(uint32_t index) const noexcept {
      return BlockById(descriptors_ + size_t(index) * stride_);
    }
    // blockCount × (2 + components().size()): [count, entities, columns...] per block.
    std::span<const uintptr_t> descriptors() const noexcept {
      return {descriptors_, size_t(blockCount_) * stride_};
    }

   private:
    QueryIteration(QueryById* query, const uintptr_t* descriptors, uint32_t blockCount, uint32_t stride, uint32_t level) noexcept
      : query_(query),
        descriptors_(descriptors),
        blockCount_(blockCount),
        stride_(stride),
        level_(level) {}
  };

  // Query by ComponentId, without compile-time types: serves layers that only know IDs.
  // Owned by the World and valid while it exists.
  class QueryById : public detail::QueryCacheBase {
   private:
    friend class World;
    friend class QueryIteration;

    std::vector<ComponentId> with_, without_, components_;
    std::vector<detail::MatchedTableById> matched_;
    std::vector<std::vector<uintptr_t>> descriptorPool_; // one buffer per nesting level; grows and never shrinks
    uint32_t depth_ = 0;

   public:
    // `with` = read ∪ write ∪ with, sorted and without repetition; `components` = read followed by write.
    QueryById(uint32_t* iterating, std::vector<ComponentId> components, std::vector<ComponentId> with, std::vector<ComponentId> without)
      : QueryCacheBase(iterating),
        with_(std::move(with)),
        without_(std::move(without)),
        components_(std::move(components)) {}

    std::span<const ComponentId> components() const noexcept {
      return components_;
    }
    size_t matchedCount() const noexcept {
      return matched_.size();
    }

    // Only non-empty tables. The iteration ends even if the callback throws.
    template <class F>
    void eachBlock(F&& callback) {
      QueryIteration current = iterate();
      for (uint32_t index = 0; index < current.blockCount(); ++index) {
        callback(static_cast<const BlockById&>(current.block(index)));
      }
    }

    QueryIteration iterate() {
      ++*iterating_;
      const uint32_t level = depth_++;
      if (level >= descriptorPool_.size()) {
        descriptorPool_.emplace_back();
      }

      std::vector<uintptr_t>& descriptors = descriptorPool_[level];
      descriptors.clear();

      uint32_t blockCount = 0;
      for (const detail::MatchedTableById& matched : matched_) {
        detail::Table& table = matched.archetype->table;
        if (table.count() == 0) {
          continue;
        }

        descriptors.push_back(table.count());
        descriptors.push_back(reinterpret_cast<uintptr_t>(table.entities().data()));
        for (uint16_t column : matched.columns) {
          descriptors.push_back(reinterpret_cast<uintptr_t>(table.column(column).data));
        }
        ++blockCount;
      }

      return QueryIteration(this, descriptors.data(), blockCount, static_cast<uint32_t>(2 + components_.size()), level);
    }

   private:
    void initialize(const std::vector<std::unique_ptr<detail::Archetype>>& all, const core::SmallMap<detail::ArchetypeId, uint16_t>* smallest) {
      if (smallest) {
        for (const auto& [archetypeId, column] : *smallest) {
          onArchetypeCreated(*all[archetypeId]);
        }
      } else {
        for (const auto& archetype : all) {
          onArchetypeCreated(*archetype);
        }
      }
    }

    bool matches(const detail::Archetype& archetype) const override {
      return detail::signatureMatches(archetype.signature, with_, without_);
    }

    bool isMatched(const detail::Archetype* archetype) const override {
      return std::any_of(matched_.begin(), matched_.end(), [archetype](const detail::MatchedTableById& matched) { return matched.archetype == archetype; });
    }

    void onArchetypeCreated(detail::Archetype& archetype) override {
      if (!matches(archetype)) {
        return;
      }

      detail::MatchedTableById matched{&archetype, {}};
      matched.columns.reserve(components_.size());
      for (ComponentId componentId : components_) {
        matched.columns.push_back(detail::columnOf(archetype, componentId));
      }

      matched_.push_back(std::move(matched));
    }

    void endIteration([[maybe_unused]] uint32_t level) noexcept {
      OLY_ASSERT(depth_ > 0 && level == depth_ - 1, "iterate: iteration ended out of order (level=%u, depth=%u)", level, depth_);
      --*iterating_;
      --depth_;
    }
  };

  inline QueryIteration::~QueryIteration() {
    if (query_) {
      query_->endIteration(level_);
    }
  }
}
