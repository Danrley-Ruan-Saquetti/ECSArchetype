#pragma once
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

#include <ecs/detail/assert.hpp>

namespace ecs {

// Mapa como vetor ordenado por chave: bom para poucos elementos e percursos em ordem.
template <class K, class V>
class SmallMap {
public:
    using value_type = std::pair<K, V>;

    V* find(K k) noexcept {
        auto it = lower(k);
        return (it != items_.end() && it->first == k) ? &it->second : nullptr;
    }
    const V* find(K k) const noexcept {
        auto it = lower(k);
        return (it != items_.end() && it->first == k) ? &it->second : nullptr;
    }

    // Pode invalidar referências anteriores.
    V& insert(K k, V v) {
        auto it = lower(k);
        ECS_ASSERT(it == items_.end() || it->first != k, "SmallMap::insert chave repetida");
        it = items_.insert(it, value_type(k, std::move(v)));
        return it->second;
    }

    size_t size() const noexcept { return items_.size(); }
    auto begin() noexcept { return items_.begin(); }
    auto end() noexcept { return items_.end(); }
    auto begin() const noexcept { return items_.begin(); }
    auto end() const noexcept { return items_.end(); }
    size_t capacityBytes() const noexcept { return items_.capacity() * sizeof(value_type); }

private:
    auto lower(K k) noexcept {
        return std::lower_bound(items_.begin(), items_.end(), k, [](const value_type& p, K key) { return p.first < key; });
    }
    auto lower(K k) const noexcept {
        return std::lower_bound(items_.begin(), items_.end(), k, [](const value_type& p, K key) { return p.first < key; });
    }
    std::vector<value_type> items_;
};

}  // namespace ecs
