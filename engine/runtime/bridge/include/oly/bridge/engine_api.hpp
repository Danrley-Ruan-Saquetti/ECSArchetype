#pragma once

#include <cstdint>

#include "oly/bridge/c_api.hpp"

struct OlyEngine;

#define OLY_INVALID_SYSTEM 0xFFFFFFFFu

extern "C" {
// fixedHz e maxStepsPerFrame precisam ser positivos (OLY_ASSERT).
OLY_API OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame);
OLY_API void olyEngineDestroy(OlyEngine* engine);
// Pertence à engine: vale até olyEngineDestroy e nunca vai para olyWorldDestroy.
OLY_API OlyWorld* olyEngineWorld(OlyEngine* engine);

// Contrato do loop, da pausa e da aba oculta: o de oly::app::Engine (oly/app/engine.hpp).
OLY_API void olyEngineFrame(OlyEngine* engine, double nowSeconds);
OLY_API void olyEngineSetHidden(OlyEngine* engine, int32_t hidden);
OLY_API void olyEnginePause(OlyEngine* engine);
OLY_API void olyEngineResume(OlyEngine* engine);
OLY_API int32_t olyEngineIsPaused(OlyEngine* engine);
OLY_API double olyEngineTime(OlyEngine* engine);
OLY_API double olyEngineFixedDeltaTime(OlyEngine* engine);
OLY_API double olyEngineInterpolation(OlyEngine* engine);

// `phase` é um oly::app::Phase (fora do intervalo, OLY_ASSERT). Registrar durante olyEngineFrame é erro de contrato.
// OLY_INVALID_SYSTEM se não existe sistema nativo com esse nome (registrado por OLY_SYSTEM).
OLY_API uint32_t olyEngineAddNativeSystem(OlyEngine* engine, uint32_t phase, const char* name);
// O sistema é chamado pelo invocador de scripts (olyHostInvokeScript no WASM) com o id devolvido aqui.
OLY_API uint32_t olyEngineAddScriptSystem(OlyEngine* engine, uint32_t phase, const char* name);
OLY_API void olyEngineSetWhilePaused(OlyEngine* engine, uint32_t systemId, int32_t whilePaused);

// Só valem dentro de um sistema de script (fora dele, OLY_ASSERT). Mudanças adiadas até o fim da fase.
OLY_API void olySystemSpawn(OlyEngine* engine, OlyEntity* out);
OLY_API void olySystemDestroy(OlyEngine* engine, uint32_t index, uint32_t generation);
OLY_API void olySystemAdd(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
OLY_API void olySystemRemove(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId);
}
