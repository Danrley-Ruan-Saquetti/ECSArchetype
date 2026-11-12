#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "oly/ecs/component.hpp"
#include "oly/ecs/detail/archetype.hpp"

namespace oly::ecs::detail {
  class QueryCacheBase {
   protected:
    uint32_t* iterating_;

   public:
    explicit QueryCacheBase(uint32_t* iterating)
      : iterating_(iterating) {}
    virtual ~QueryCacheBase() = default;

    virtual void onArchetypeCreated(Archetype& archetype) = 0;
    virtual bool matches(const Archetype& archetype) const = 0;
    virtual bool isMatched(const Archetype* archetype) const = 0;
  };

  // `signature`, `with` and `without` must be sorted.
  inline bool signatureMatches(const Signature& signature, std::span<const ComponentId> with, std::span<const ComponentId> without) {
    if (!std::includes(signature.begin(), signature.end(), with.begin(), with.end())) {
      return false;
    }
    for (ComponentId componentId : without) {
      if (std::binary_search(signature.begin(), signature.end(), componentId)) {
        return false;
      }
    }

    return true;
  }

  struct IterationGuard {
    uint32_t* counter_;

    explicit IterationGuard(uint32_t* counter) noexcept
      : counter_(counter) {
      ++*counter_;
    }
    ~IterationGuard() {
      --*counter_;
    }
    IterationGuard(const IterationGuard&) = delete;
    IterationGuard& operator=(const IterationGuard&) = delete;
  };

  template <class... Ts>
  struct MatchedTable {
    Archetype* archetype;
    std::array<uint16_t, sizeof...(Ts)> columns;
  };

  struct MatchedTableById {
    Archetype* archetype;
    std::vector<uint16_t> columns; // columns[i] is the column of components()[i]
  };
}
