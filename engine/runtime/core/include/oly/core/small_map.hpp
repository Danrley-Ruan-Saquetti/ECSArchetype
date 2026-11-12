#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include "oly/core/assert.hpp"

namespace oly::core {
  // Map as a vector sorted by key: good for few elements and in-order traversal.
  template <class K, class V>
  class SmallMap {
   public:
    using value_type = std::pair<K, V>;

   private:
    std::vector<value_type> items_;

   public:
    V* find(K key) noexcept {
      auto it = lowerBound(key);
      return (it != items_.end() && it->first == key) ? &it->second : nullptr;
    }
    const V* find(K key) const noexcept {
      auto it = lowerBound(key);
      return (it != items_.end() && it->first == key) ? &it->second : nullptr;
    }

    // May invalidate earlier references.
    V& insert(K key, V value) {
      auto it = lowerBound(key);
      OLY_ASSERT(it == items_.end() || it->first != key, "SmallMap::insert duplicate key");

      it = items_.insert(it, value_type(key, std::move(value)));
      return it->second;
    }

    size_t size() const noexcept {
      return items_.size();
    }
    auto begin() noexcept {
      return items_.begin();
    }
    auto end() noexcept {
      return items_.end();
    }
    auto begin() const noexcept {
      return items_.begin();
    }
    auto end() const noexcept {
      return items_.end();
    }
    size_t capacityBytes() const noexcept {
      return items_.capacity() * sizeof(value_type);
    }

   private:
    auto lowerBound(K key) noexcept {
      return std::lower_bound(items_.begin(), items_.end(), key, [](const value_type& item, K target) { return item.first < target; });
    }
    auto lowerBound(K key) const noexcept {
      return std::lower_bound(items_.begin(), items_.end(), key, [](const value_type& item, K target) { return item.first < target; });
    }
  };
}
