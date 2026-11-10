#pragma once

#include <cstdint>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#define ENGINE_API EMSCRIPTEN_KEEPALIVE
#else
#define ENGINE_API
#endif

struct EngineWorld;
struct EngineQuery; // opaco: é um ecs::RawQuery*

extern "C" {
struct EngineEntity {
  uint32_t index;
  uint32_t generation;
};

ENGINE_API EngineWorld* engineWorldCreate();
ENGINE_API void engineWorldDestroy(EngineWorld* world);

ENGINE_API uint32_t engineSchemaHash();
ENGINE_API uint32_t engineSchemaComponentCount();
ENGINE_API uint32_t engineSchemaComponentId(uint32_t schemaIndex);
ENGINE_API uint32_t engineComponentSize(uint32_t componentId); // 0 para tag
ENGINE_API uint32_t engineComponentAlignment(uint32_t componentId);

ENGINE_API void engineCreate(EngineWorld* world, EngineEntity* out);
ENGINE_API void engineDestroy(EngineWorld* world, uint32_t index, uint32_t generation);
ENGINE_API int32_t engineIsAlive(EngineWorld* world, uint32_t index, uint32_t generation);
ENGINE_API void engineAdd(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
ENGINE_API void engineRemove(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
ENGINE_API int32_t engineHas(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
// nullptr se a entidade está morta, o componente ausente ou é tag.
ENGINE_API void* engineGet(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);

// componentIds = read | write | with | without, concatenados.
ENGINE_API EngineQuery* engineQuery(EngineWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount);
// Inicia a iteração e devolve o nº de blocos (tabelas não vazias). *outDescriptors aponta para
// blocos × (2 + readCount + writeCount) uintptr_t: [count, endereço das entidades, endereço de cada coluna...].
// Válido até a próxima chamada que itere ou altere o World.
ENGINE_API uint32_t engineQueryBegin(EngineWorld* world, EngineQuery* query, const uintptr_t** outDescriptors);
ENGINE_API void engineQueryEnd(EngineWorld* world, EngineQuery* query);

// 1 se ok; 0 com o motivo truncado em `message` (NUL no fim).
ENGINE_API int32_t engineCheckInvariants(EngineWorld* world, char* message, uint32_t capacity);
}
