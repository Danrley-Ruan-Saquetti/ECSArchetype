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
// 1 se ok; 0 com o motivo truncado em `message` (NUL no fim). Com capacity 0, `message` não é escrita.
OLY_API int32_t olyWorldCheckInvariants(OlyWorld* world, char* message, uint32_t capacity);

OLY_API uint32_t olySchemaHash();
OLY_API uint32_t olySchemaComponentCount();
// schemaIndex < olySchemaComponentCount(), sem verificação.
OLY_API uint32_t olySchemaComponentId(uint32_t schemaIndex);
OLY_API uint32_t olyComponentSize(uint32_t componentId); // 0 para tag
OLY_API uint32_t olyComponentAlignment(uint32_t componentId);
OLY_API uint32_t olySchemaEventCount();
// eventIndex < olySchemaEventCount(), sem verificação. O índice é o do evento no esquema, que é também o seu
// tipo de evento na Engine (olyEngineEmit, olyEngineAdd*Handler).
OLY_API uint32_t olySchemaEventSize(uint32_t eventIndex);
OLY_API uint32_t olySchemaEventAlignment(uint32_t eventIndex);

// Mudanças estruturais (spawn, destroy, add, remove) exigem nenhuma iteração aberta; destroy, add e remove
// também exigem entidade viva (OLY_ASSERT). Qualquer mudança estrutural invalida os ponteiros de
// olyComponentGet; só (index, generation) é handle estável.
OLY_API void olyEntitySpawn(OlyWorld* world, OlyEntity* out);
OLY_API void olyEntityDestroy(OlyWorld* world, uint32_t index, uint32_t generation);
OLY_API int32_t olyEntityIsAlive(OlyWorld* world, uint32_t index, uint32_t generation);

// Move o valor de `value` (olyComponentSize(componentId) bytes); se a entidade já tem o componente, substitui.
// Para tag, `value` é ignorado e pode ser nullptr.
OLY_API void olyComponentAdd(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
OLY_API void olyComponentRemove(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
OLY_API int32_t olyComponentHas(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
// nullptr se a entidade está morta, o componente ausente ou é tag.
OLY_API void* olyComponentGet(OlyWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);

// componentIds = read | write | with | without, concatenados. A query pertence ao World (não há destroy) e é
// cacheada pela spec: a mesma combinação devolve o mesmo ponteiro. Em read/write, componente desconhecido,
// repetido ou tag é erro de contrato (OLY_ASSERT).
OLY_API OlyQuery* olyQueryCreate(OlyWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount);
// Inicia a iteração. *outBlockCount recebe o nº de blocos (tabelas não vazias) e *outDescriptors aponta para
// blocos × (2 + readCount + writeCount) uintptr_t: [count, endereço das entidades, endereço de cada coluna
// (read, depois write)...]. Os descritores valem até o olyQueryEnd daquela iteração, mesmo com iterações
// aninhadas. Toda olyQueryBegin precisa de um olyQueryEnd; até lá, mudança estrutural no World é erro de contrato.
OLY_API OlyIteration* olyQueryBegin(OlyWorld* world, OlyQuery* query, uint32_t* outBlockCount, const uintptr_t** outDescriptors);
// Encerra a iteração mais interna; outra ordem é erro de contrato (OLY_ASSERT).
OLY_API void olyQueryEnd(OlyWorld* world, OlyIteration* iteration);
}
