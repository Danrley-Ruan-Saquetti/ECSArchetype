#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define OLY_API EMSCRIPTEN_KEEPALIVE
#else
#define OLY_API
#endif

struct OlyWorld;
struct OlyQuery; // opaco: é um oly::ecs::QueryById*
struct OlyIteration; // opaco: ocupa um slot da pilha de iterações do OlyWorld

extern "C" {
struct OlyEntity {
  uint32_t index;
  uint32_t generation;
};

OLY_API OlyWorld* olyWorldCreate();
OLY_API void olyWorldDestroy(OlyWorld* world);
// 1 se ok; 0 com o motivo truncado em `message` (NUL no fim).
OLY_API int32_t olyWorldCheckInvariants(OlyWorld* world, char* message, uint32_t capacity);

OLY_API uint32_t olySchemaHash();
OLY_API uint32_t olySchemaComponentCount();
OLY_API uint32_t olySchemaComponentId(uint32_t schemaIndex);
OLY_API uint32_t olyComponentSize(uint32_t componentId); // 0 para tag
OLY_API uint32_t olyComponentAlignment(uint32_t componentId);

OLY_API void olyEntitySpawn(OlyWorld* world, OlyEntity* out);
OLY_API void olyEntityDestroy(OlyWorld* world, uint32_t index, uint32_t generation);
OLY_API int32_t olyEntityIsAlive(OlyWorld* world, uint32_t index, uint32_t generation);

OLY_API void olyComponentAdd(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
OLY_API void olyComponentRemove(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
OLY_API int32_t olyComponentHas(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
// nullptr se a entidade está morta, o componente ausente ou é tag.
OLY_API void* olyComponentGet(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);

// componentIds = read | write | with | without, concatenados.
OLY_API OlyQuery* olyQueryCreate(OlyWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount);
// Inicia a iteração. *outBlockCount recebe o nº de blocos (tabelas não vazias) e *outDescriptors aponta para
// blocos × (2 + readCount + writeCount) uintptr_t: [count, endereço das entidades, endereço de cada coluna...].
// Os descritores valem até o olyQueryEnd daquela iteração, mesmo com iterações aninhadas.
OLY_API OlyIteration* olyQueryBegin(OlyWorld* world, OlyQuery* query, uint32_t* outBlockCount, const uintptr_t** outDescriptors);
// Encerra a iteração mais interna; outra ordem é erro de contrato.
OLY_API void olyQueryEnd(OlyWorld* world, OlyIteration* iteration);
}
