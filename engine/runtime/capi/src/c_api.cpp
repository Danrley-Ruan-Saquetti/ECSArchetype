#include "oly/capi/c_api.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "handles.hpp"
#include "oly/capi/schema_registry.hpp"
#include "oly/capi/world.hpp"
#include "oly/core/assert.hpp"
#include "oly/ecs/diagnostics.hpp"
#include "oly/ecs/entity.hpp"
#include "oly/ecs/query_by_id.hpp"

static_assert(sizeof(OlyEntity) == sizeof(oly::ecs::Entity) && alignof(OlyEntity) == alignof(oly::ecs::Entity),
              "OlyEntity must have the same layout as oly::ecs::Entity");

namespace oly::capi {
  namespace {
    oly::ecs::Entity entityOf(uint32_t index, uint32_t generation) {
      return oly::ecs::Entity{index, generation};
    }

    oly::ecs::QueryById& queryOf(OlyQuery* query) {
      return *reinterpret_cast<oly::ecs::QueryById*>(query);
    }
  }

  oly::ecs::World& worldOf(OlyWorld* world) noexcept {
    return world->world;
  }
}

extern "C" {
OlyWorld* olyWorldCreate() {
  return new OlyWorld();
}

void olyWorldDestroy(OlyWorld* world) {
  delete world;
}

int32_t olyWorldCheckInvariants(OlyWorld* world, char* message, uint32_t capacity) {
  std::string why;
  const bool ok = oly::ecs::checkInvariants(world->world, &why);
  if (!ok && capacity > 0) {
    const size_t length = std::min<size_t>(why.size(), capacity - 1);
    std::memcpy(message, why.data(), length);
    message[length] = '\0';
  }

  return ok ? 1 : 0;
}

uint32_t olySchemaHash() {
  return oly::capi::schemaHash();
}

uint32_t olySchemaComponentCount() {
  return static_cast<uint32_t>(oly::capi::schemaComponentIds().size());
}

uint32_t olySchemaComponentId(uint32_t schemaIndex) {
  return oly::capi::schemaComponentIds()[schemaIndex];
}

uint32_t olyComponentSize(uint32_t componentId) {
  return oly::ecs::typeInfo(componentId).size;
}

uint32_t olyComponentAlignment(uint32_t componentId) {
  return oly::ecs::typeInfo(componentId).alignment;
}

uint32_t olySchemaEventCount() {
  return static_cast<uint32_t>(oly::capi::schemaEvents().size());
}

uint32_t olySchemaEventSize(uint32_t eventIndex) {
  return oly::capi::schemaEvents()[eventIndex].size;
}

uint32_t olySchemaEventAlignment(uint32_t eventIndex) {
  return oly::capi::schemaEvents()[eventIndex].alignment;
}

static_assert(OLY_ACTION_BUTTON == static_cast<uint32_t>(oly::input::ActionKind::Button) && OLY_ACTION_AXIS2D == static_cast<uint32_t>(oly::input::ActionKind::Axis2d) && OLY_ACTION_POINTER == static_cast<uint32_t>(oly::input::ActionKind::Pointer),
              "OLY_ACTION_* must match oly::input::ActionKind");

uint32_t olySchemaActionCount() {
  return static_cast<uint32_t>(oly::capi::schemaActions().size());
}

uint32_t olySchemaActionKind(uint32_t actionIndex) {
  return static_cast<uint32_t>(oly::capi::schemaActions()[actionIndex].kind);
}

uint32_t olySchemaStateCount() {
  return static_cast<uint32_t>(oly::capi::schemaStates().size());
}

void olyEntitySpawn(OlyWorld* world, OlyEntity* out) {
  const oly::ecs::Entity entity = world->world.spawn();
  out->index = entity.index;
  out->generation = entity.generation;
}

void olyEntityDestroy(OlyWorld* world, uint32_t index, uint32_t generation) {
  world->world.destroy(oly::capi::entityOf(index, generation));
}

int32_t olyEntityIsAlive(OlyWorld* world, uint32_t index, uint32_t generation) {
  return world->world.isAlive(oly::capi::entityOf(index, generation)) ? 1 : 0;
}

void olyComponentAdd(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId, void* value) {
  world->world.addById(oly::capi::entityOf(index, generation), componentId, value);
}

void olyComponentRemove(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  world->world.removeById(oly::capi::entityOf(index, generation), componentId);
}

int32_t olyComponentHas(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  return world->world.hasById(oly::capi::entityOf(index, generation), componentId) ? 1 : 0;
}

void* olyComponentGet(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId) {
  return world->world.tryGetById(oly::capi::entityOf(index, generation), componentId);
}

OlyQuery* olyQueryCreate(OlyWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount) {
  oly::ecs::QueryByIdSpec spec;
  const uint32_t* cursor = componentIds;
  spec.read.assign(cursor, cursor + readCount);
  cursor += readCount;
  spec.write.assign(cursor, cursor + writeCount);
  cursor += writeCount;
  spec.with.assign(cursor, cursor + withCount);
  cursor += withCount;
  spec.without.assign(cursor, cursor + withoutCount);

  return reinterpret_cast<OlyQuery*>(&world->world.queryById(spec));
}

OlyIteration* olyQueryBegin(OlyWorld* world, OlyQuery* query, uint32_t* outBlockCount, const uintptr_t** outDescriptors) {
  if (world->activeIterations == world->iterationSlots.size()) {
    world->iterationSlots.push_back(std::make_unique<OlyIteration>());
  }

  OlyIteration* slot = world->iterationSlots[world->activeIterations].get();
  slot->iteration.emplace(oly::capi::queryOf(query).iterate());
  ++world->activeIterations;

  *outBlockCount = slot->iteration->blockCount();
  *outDescriptors = slot->iteration->descriptors().data();
  return slot;
}

void olyQueryEnd(OlyWorld* world, OlyIteration* iteration) {
  OLY_ASSERT(world->activeIterations > 0 && world->iterationSlots[world->activeIterations - 1].get() == iteration,
             "olyQueryEnd: iteration ended out of order");

  iteration->iteration.reset();
  --world->activeIterations;
}
}
