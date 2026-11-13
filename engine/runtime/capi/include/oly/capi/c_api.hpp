#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define OLY_API EMSCRIPTEN_KEEPALIVE
#else
#define OLY_API
#endif

struct OlyWorld;
struct OlyQuery; // opaque: it is an oly::ecs::QueryById*
struct OlyIteration; // opaque: occupies a slot of the OlyWorld iteration stack

extern "C" {
struct OlyEntity {
  uint32_t index;
  uint32_t generation;
};

OLY_API OlyWorld* olyWorldCreate();
OLY_API void olyWorldDestroy(OlyWorld* world);
// 1 if ok; 0 with the reason truncated into `message` (NUL-terminated). With capacity 0, `message` is not written.
OLY_API int32_t olyWorldCheckInvariants(OlyWorld* world, char* message, uint32_t capacity);

OLY_API uint32_t olySchemaHash();
OLY_API uint32_t olySchemaComponentCount();
// schemaIndex < olySchemaComponentCount(), unchecked.
OLY_API uint32_t olySchemaComponentId(uint32_t schemaIndex);
OLY_API uint32_t olyComponentSize(uint32_t componentId); // 0 for tag
OLY_API uint32_t olyComponentAlignment(uint32_t componentId);
OLY_API uint32_t olySchemaEventCount();
// eventIndex < olySchemaEventCount(), unchecked. The index is the event's index in the schema, which is also its
// event type in the Engine (olyEngineEmit, olyEngineAdd*Handler).
OLY_API uint32_t olySchemaEventSize(uint32_t eventIndex);
OLY_API uint32_t olySchemaEventAlignment(uint32_t eventIndex);

#define OLY_ACTION_BUTTON 0u
#define OLY_ACTION_AXIS2D 1u
#define OLY_ACTION_POINTER 2u
OLY_API uint32_t olySchemaActionCount();
// actionIndex < olySchemaActionCount(), unchecked. The index is the action's index in the schema, which is also its
// ActionId in the Engine.
OLY_API uint32_t olySchemaActionKind(uint32_t actionIndex);
// The state's index in the schema is its StateId in the Engine.
OLY_API uint32_t olySchemaStateCount();

// Structural changes (spawn, destroy, add, remove) require no open iteration; destroy, add and remove
// also require an alive entity (OLY_ASSERT). Any structural change invalidates the pointers from
// olyComponentGet; only (index, generation) is a stable handle.
OLY_API void olyEntitySpawn(OlyWorld* world, OlyEntity* out);
OLY_API void olyEntityDestroy(OlyWorld* world, uint32_t index, uint32_t generation);
OLY_API int32_t olyEntityIsAlive(OlyWorld* world, uint32_t index, uint32_t generation);

// Moves the value from `value` (olyComponentSize(componentId) bytes); if the entity already has the component, replaces it.
// For a tag, `value` is ignored and may be nullptr.
OLY_API void olyComponentAdd(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
OLY_API void olyComponentRemove(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
OLY_API int32_t olyComponentHas(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
// nullptr if the entity is dead, the component is absent or is a tag.
OLY_API void* olyComponentGet(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);

// componentIds = read | write | with | without, concatenated. The query belongs to the World (there is no destroy) and is
// cached by spec: the same combination returns the same pointer. In read/write, an unknown,
// repeated or tag component is a contract violation (OLY_ASSERT).
OLY_API OlyQuery* olyQueryCreate(OlyWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount);
// Starts the iteration. *outBlockCount receives the number of blocks (non-empty tables) and *outDescriptors points to
// blocks × (2 + readCount + writeCount) uintptr_t: [count, address of the entities, address of each column
// (read, then write)...]. The descriptors stay valid until the olyQueryEnd of that iteration, even with nested
// iterations. Every olyQueryBegin needs an olyQueryEnd; until then, a structural change in the World is a contract violation.
OLY_API OlyIteration* olyQueryBegin(OlyWorld* world, OlyQuery* query, uint32_t* outBlockCount, const uintptr_t** outDescriptors);
// Ends the innermost iteration; any other order is a contract violation (OLY_ASSERT).
OLY_API void olyQueryEnd(OlyWorld* world, OlyIteration* iteration);
}
