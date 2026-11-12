#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "oly/ecs/component.hpp"

namespace oly::ecs {
  class World;

  struct ArchetypeStats {
    uint32_t id;
    std::vector<ComponentId> signature;
    uint32_t count, capacity, edgeCount;
    size_t columnBytes; // capacity × Σ size of the columns + capacity × sizeof(Entity)
    size_t idleBytes; // (capacity − count) × (Σ size + sizeof(Entity))
    size_t signatureBytes; // capacity() of signature + columnTypes, in bytes
    size_t edgeBytes; // capacityBytes() of addEdges/removeEdges + sourceToDestination
  };

  struct WorldStats {
    uint32_t archetypeCount;
    uint32_t liveEntities;
    std::vector<ArchetypeStats> archetypes; // by id
    std::array<uint32_t, 33> histogram; // [0] = empty archetypes; [i] = count in [2^(i-1), 2^i)
    uint64_t signatureLookups;
  };

  // Checks invariants I-1 and I-3..I-11 (compiled in every build); I-2 depends on the
  // tests' LifeCounter.
  // On failure writes the ID and the object to *why (e.g. "I-3 e=4:1") and returns false.
  bool checkInvariants(const World& world, std::string* why = nullptr);
  WorldStats stats(const World& world);
  uint64_t signatureLookupCount(const World& world);
  uint32_t archetypeCount(const World& world);
}
