#pragma once

#include <cstddef>
#include <cstdint>
#include <ecs/detail/small_map.hpp>
#include <ecs/table.hpp>
#include <ecs/type_info.hpp>
#include <vector>

namespace ecs {

  using ArchetypeId = uint32_t;
  using Signature = std::vector<ComponentId>;
  inline constexpr uint16_t NoColumn = UINT16_MAX;
  inline constexpr ComponentId NoComponent = UINT32_MAX;

  // FNV-1a 64 sobre os IDs.
  struct SigHash {
    size_t operator()(const Signature& s) const noexcept {
      uint64_t h = 1469598103934665603ull;
      for (ComponentId id : s)
        for (int i = 0; i < 4; ++i) {
          h ^= (id >> (8 * i)) & 0xffu;
          h *= 1099511628211ull;
        }
      return static_cast<size_t>(h);
    }
  };

  struct Archetype;

  struct Edge {
    Archetype* target = nullptr;
    std::vector<int16_t> srcToDst; // por coluna da origem: coluna no destino, ou -1
    int16_t addedColumn = -1; // add de componente com dados: coluna no destino
  };

  struct Archetype {
    ArchetypeId id;
    Signature signature; // ordenada, inclui tags
    std::vector<ComponentId> columnTypes; // ordenada, só dados
    Table table;
    SmallMap<ComponentId, Edge> addEdges, removeEdges;

    Archetype(ArchetypeId id, Signature sig);
  };

  // Coluna de `c` em `a`, ou NoColumn se ausente ou tag.
  uint16_t columnOf(const Archetype& a, ComponentId c) noexcept;

  // Merge O(k) das columnTypes de src e dst (R-AR-2). `added` = componente recém-adicionado
  // (NoComponent para remoção ou salto arbitrário).
  Edge buildEdge(const Archetype& src, Archetype& dst, ComponentId added);

} // namespace ecs
