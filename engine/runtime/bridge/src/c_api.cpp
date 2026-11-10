#include "oly/bridge/c_api.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "oly/core/assert.hpp"
#include "oly/ecs/entity.hpp"
#include "oly/ecs/query_by_id.hpp"
#include "oly/bridge/schema_registry.hpp"
#include "oly/bridge/world_handle.hpp"

static_assert(sizeof(EngineEntity) == sizeof(oly::ecs::Entity) && alignof(EngineEntity) == alignof(oly::ecs::Entity),
              "EngineEntity deve ter o mesmo layout de oly::ecs::Entity");

namespace {
  oly::ecs::Entity entityOf(uint32_t index, uint32_t generation) {
    return oly::ecs::Entity{index, generation};
  }

  oly::ecs::QueryById& queryOf(EngineQuery* query) {
    return *reinterpret_cast<oly::ecs::QueryById*>(query);
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
  return oly::bridge::schemaHash();
}

uint32_t engineSchemaComponentCount() {
  return static_cast<uint32_t>(oly::bridge::schemaComponentIds().size());
}

uint32_t engineSchemaComponentId(uint32_t schemaIndex) {
  return oly::bridge::schemaComponentIds()[schemaIndex];
}

uint32_t engineComponentSize(uint32_t componentId) {
  return oly::ecs::typeInfo(componentId).size;
}

uint32_t engineComponentAlignment(uint32_t componentId) {
  return oly::ecs::typeInfo(componentId).alignment;
}

void engineCreate(EngineWorld* world, EngineEntity* out) {
  const oly::ecs::Entity entity = world->world.spawn();
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
  world->world.addById(entityOf(index, generation), componentId, value);
}

void engineRemove(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  world->world.removeById(entityOf(index, generation), componentId);
}

int32_t engineHas(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  return world->world.hasById(entityOf(index, generation), componentId) ? 1 : 0;
}

void* engineGet(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  return world->world.tryGetById(entityOf(index, generation), componentId);
}

EngineQuery* engineQuery(EngineWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount) {
  oly::ecs::QueryByIdSpec spec;
  const uint32_t* cursor = componentIds;
  spec.read.assign(cursor, cursor + readCount);
  cursor += readCount;
  spec.write.assign(cursor, cursor + writeCount);
  cursor += writeCount;
  spec.with.assign(cursor, cursor + withCount);
  cursor += withCount;
  spec.without.assign(cursor, cursor + withoutCount);

  return reinterpret_cast<EngineQuery*>(&world->world.queryById(spec));
}

uint32_t engineQueryBegin(EngineWorld* world, EngineQuery* query, const uintptr_t** outDescriptors) {
  world->activeIterations.push_back(queryOf(query).iterate());

  const oly::ecs::QueryIteration& iteration = world->activeIterations.back();
  *outDescriptors = iteration.descriptors().data();
  return iteration.blockCount();
}

void engineQueryEnd(EngineWorld* world, EngineQuery*) {
  OLY_ASSERT(!world->activeIterations.empty(), "engineQueryEnd sem engineQueryBegin correspondente");
  world->activeIterations.pop_back();
}

int32_t engineCheckInvariants(EngineWorld* world, char* message, uint32_t capacity) {
  std::string why;
  const bool ok = oly::ecs::checkInvariants(world->world, &why);
  if (!ok && capacity > 0) {
    const size_t length = std::min<size_t>(why.size(), capacity - 1);
    std::memcpy(message, why.data(), length);
    message[length] = '\0';
  }

  return ok ? 1 : 0;
}
}
