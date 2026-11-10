#pragma once

#include <cstddef>
#include <cstdint>

namespace oly::ecs {
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
}
