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
    size_t columnBytes; // capacity × Σ size das colunas + capacity × sizeof(Entity)
    size_t idleBytes; // (capacity − count) × (Σ size + sizeof(Entity))
    size_t signatureBytes; // capacity() de signature + columnTypes, em bytes
    size_t edgeBytes; // capacityBytes() de addEdges/removeEdges + sourceToDestination
  };

  struct WorldStats {
    uint32_t archetypeCount;
    uint32_t liveEntities;
    std::vector<ArchetypeStats> archetypes; // por id
    std::array<uint32_t, 33> histogram; // [0] = arquétipos vazios; [i] = count em [2^(i-1), 2^i)
    uint64_t signatureLookups;
  };

  // Verifica as invariantes I-1 e I-3..I-11 (compilado em todos os builds); I-2 depende do
  // LifeCounter dos testes.
  // Em falha escreve em *why o ID e o objeto (ex.: "I-3 e=4:1") e devolve false.
  bool checkInvariants(const World& world, std::string* why = nullptr);
  WorldStats stats(const World& world);
  uint64_t signatureLookupCount(const World& world);
  uint32_t archetypeCount(const World& world);
}
