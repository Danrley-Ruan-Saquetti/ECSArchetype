#include "oly/bridge/c_api.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "oly/ecs/entity.hpp"
#include "oly/ecs/raw_query.hpp"
#include "oly/bridge/schema_registry.hpp"
#include "oly/bridge/world_handle.hpp"

static_assert(sizeof(EngineEntity) == sizeof(ecs::Entity) && alignof(EngineEntity) == alignof(ecs::Entity),
              "EngineEntity deve ter o mesmo layout de ecs::Entity");

namespace {
  ecs::Entity entityOf(uint32_t index, uint32_t generation) {
    return ecs::Entity{index, generation};
  }

  ecs::RawQuery& queryOf(EngineQuery* query) {
    return *reinterpret_cast<ecs::RawQuery*>(query);
  }
}

extern "C" {
EngineWorld* engineWorldCreate() {
  return new EngineWorld();
}

void engineWorldDestroy(EngineWorld* world) {
  delete world;
}

uint32_t engineSchemaHash() {
  return engine::schemaHash();
}

uint32_t engineSchemaComponentCount() {
  return static_cast<uint32_t>(engine::schemaComponentIds().size());
}

uint32_t engineSchemaComponentId(uint32_t schemaIndex) {
  return engine::schemaComponentIds()[schemaIndex];
}

uint32_t engineComponentSize(uint32_t componentId) {
  return ecs::typeInfo(componentId).size;
}

uint32_t engineComponentAlignment(uint32_t componentId) {
  return ecs::typeInfo(componentId).alignment;
}

void engineCreate(EngineWorld* world, EngineEntity* out) {
  const ecs::Entity entity = world->world.create();
  out->index = entity.index;
  out->generation = entity.generation;
}

void engineDestroy(EngineWorld* world, uint32_t index, uint32_t generation) {
  world->world.destroy(entityOf(index, generation));
}

int32_t engineIsAlive(EngineWorld* world, uint32_t index, uint32_t generation) {
  return world->world.isAlive(entityOf(index, generation)) ? 1 : 0;
}

void engineAdd(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId, void* value) {
  world->world.addRaw(entityOf(index, generation), componentId, value);
}

void engineRemove(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  world->world.removeRaw(entityOf(index, generation), componentId);
}

int32_t engineHas(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  return world->world.hasRaw(entityOf(index, generation), componentId) ? 1 : 0;
}

void* engineGet(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  return world->world.getRaw(entityOf(index, generation), componentId);
}

EngineQuery* engineQuery(EngineWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount) {
  ecs::RawQuerySpec spec;
  const uint32_t* cursor = componentIds;
  spec.read.assign(cursor, cursor + readCount);
  cursor += readCount;
  spec.write.assign(cursor, cursor + writeCount);
  cursor += writeCount;
  spec.with.assign(cursor, cursor + withCount);
  cursor += withCount;
  spec.without.assign(cursor, cursor + withoutCount);

  return reinterpret_cast<EngineQuery*>(&world->world.rawQuery(spec));
}

uint32_t engineQueryBegin(EngineWorld* world, EngineQuery* query, const uintptr_t** outDescriptors) {
  ecs::RawQuery& rawQuery = queryOf(query);
  rawQuery.beginIteration();

  std::vector<uintptr_t>& descriptors = world->blockDescriptors;
  descriptors.clear();

  uint32_t blockCount = 0;
  for (const ecs::RawMatchedTable& matched : rawQuery.matchedTables()) {
    ecs::Table& table = matched.archetype->table;
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

  *outDescriptors = descriptors.data();
  return blockCount;
}

void engineQueryEnd(EngineWorld*, EngineQuery* query) {
  queryOf(query).endIteration();
}

int32_t engineCheckInvariants(EngineWorld* world, char* message, uint32_t capacity) {
  std::string why;
  const bool ok = world->world.checkInvariants(&why);
  if (!ok && capacity > 0) {
    const size_t length = std::min<size_t>(why.size(), capacity - 1);
    std::memcpy(message, why.data(), length);
    message[length] = '\0';
  }

  return ok ? 1 : 0;
}
}
