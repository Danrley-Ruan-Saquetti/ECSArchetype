#include "oly/ecs/diagnostics.hpp"

#include "oly/ecs/world.hpp"

namespace oly::ecs {
  bool checkInvariants(const World& world, std::string* why) {
    return world.checkInvariants(why);
  }

  uint64_t signatureLookupCount(const World& world) {
    return world.signatureLookups_;
  }

  uint32_t archetypeCount(const World& world) {
    return static_cast<uint32_t>(world.archetypes_.size());
  }

  WorldStats stats(const World& world) {
    WorldStats worldStats{};
    worldStats.archetypeCount = archetypeCount(world);
    worldStats.signatureLookups = world.signatureLookups_;

    for (const auto& owned : world.archetypes_) {
      const detail::Archetype& archetype = *owned;
      size_t rowBytes = sizeof(Entity);
      for (uint16_t i = 0; i < archetype.table.columnCount(); ++i) {
        rowBytes += archetype.table.column(i).info->size;
      }

      ArchetypeStats archetypeStats{};
      archetypeStats.id = archetype.id;
      archetypeStats.signature = archetype.signature;
      archetypeStats.count = archetype.table.count();
      archetypeStats.capacity = archetype.table.capacity();
      archetypeStats.edgeCount = static_cast<uint32_t>(archetype.addEdges.size() + archetype.removeEdges.size());
      archetypeStats.columnBytes = size_t(archetypeStats.capacity) * rowBytes;
      archetypeStats.idleBytes = size_t(archetypeStats.capacity - archetypeStats.count) * rowBytes;
      archetypeStats.signatureBytes = (archetype.signature.capacity() + archetype.columnTypes.capacity()) * sizeof(ComponentId);

      archetypeStats.edgeBytes = archetype.addEdges.capacityBytes() + archetype.removeEdges.capacityBytes();
      for (const auto& [componentId, edge] : archetype.addEdges) {
        archetypeStats.edgeBytes += edge.sourceToDestination.capacity() * sizeof(int16_t);
      }
      for (const auto& [componentId, edge] : archetype.removeEdges) {
        archetypeStats.edgeBytes += edge.sourceToDestination.capacity() * sizeof(int16_t);
      }

      worldStats.liveEntities += archetypeStats.count;
      const uint32_t bucket = archetypeStats.count == 0 ? 0 : 1 + (31 - static_cast<uint32_t>(__builtin_clz(archetypeStats.count)));
      ++worldStats.histogram[bucket];
      worldStats.archetypes.push_back(std::move(archetypeStats));
    }

    return worldStats;
  }
}
