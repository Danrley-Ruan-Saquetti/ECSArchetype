#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ecs/detail/small_map.hpp"
#include "ecs/table.hpp"
#include "ecs/type_info.hpp"

namespace ecs {
  using ArchetypeId = uint32_t;
  using Signature = std::vector<ComponentId>;

  inline constexpr uint16_t NoColumn = UINT16_MAX;
  inline constexpr ComponentId NoComponent = UINT32_MAX;

  struct SignatureHash {
    static constexpr uint64_t kFnv1aOffsetBasis = 1469598103934665603ull;
    static constexpr uint64_t kFnv1aPrime = 1099511628211ull;

    size_t operator()(const Signature& signature) const noexcept {
      uint64_t hash = kFnv1aOffsetBasis;
      for (ComponentId id : signature)
        for (int i = 0; i < 4; ++i) {
          hash ^= (id >> (8 * i)) & 0xffu;
          hash *= kFnv1aPrime;
        }

      return static_cast<size_t>(hash);
    }
  };

  struct Archetype;

  struct Edge {
    Archetype* target = nullptr;
    std::vector<int16_t> sourceToDestination; // por coluna da origem: coluna no destino, ou -1
    int16_t addedColumn = -1; // add de componente com dados: coluna no destino
  };

  struct Archetype {
    ArchetypeId id;
    Signature signature; // ordenada, inclui tags
    std::vector<ComponentId> columnTypes; // ordenada, só dados
    Table table;
    SmallMap<ComponentId, Edge> addEdges, removeEdges;

    Archetype(ArchetypeId id, Signature signature);
  };

  // Coluna de `componentId` em `archetype`, ou NoColumn se ausente ou tag.
  uint16_t columnOf(const Archetype& archetype, ComponentId componentId) noexcept;

  // `added` = componente recém-adicionado; NoComponent para remoção ou salto arbitrário.
  Edge buildEdge(const Archetype& source, Archetype& destination, ComponentId added);
}
