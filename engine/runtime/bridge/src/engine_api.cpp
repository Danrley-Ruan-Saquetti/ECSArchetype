#include "oly/bridge/engine_api.hpp"

#include "handles.hpp"
#include "oly/app/system.hpp"
#include "oly/bridge/engine.hpp"
#include "oly/core/assert.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

EM_JS(void, olyHostInvokeScript, (OlyEngine * engine, uint32_t systemId), { Module["olyInvokeScript"](engine, systemId); });
#endif

namespace oly::bridge {
  namespace {
    void defaultInvoker([[maybe_unused]] OlyEngine* engine, [[maybe_unused]] uint32_t systemId) {
#if defined(__EMSCRIPTEN__)
      olyHostInvokeScript(engine, systemId);
#else
      OLY_ASSERT(false, "sistema de script chamado sem invocador instalado (setScriptInvoker) systemId=%u", systemId);
#endif
    }

    ScriptInvoker g_invoker = &defaultInvoker;

    // O sistema de script é, para o núcleo, uma função C++ que chama o invocador uma vez.
    void runScriptSystem(oly::app::SystemContext& context) {
      OlyEngine* engine = static_cast<OlyEngine*>(context.userData());

      engine->currentContext = &context;
      g_invoker(engine, context.systemId());
      engine->currentContext = nullptr;
    }

    oly::app::SystemContext& currentContext(OlyEngine* engine, const char* operation) {
      OLY_ASSERT(engine->currentContext != nullptr, "%s: chamado fora de sistema", operation);
      return *engine->currentContext;
    }

    oly::app::Phase phaseOf(uint32_t phase, const char* operation) {
      OLY_ASSERT(phase < oly::app::PhaseCount, "%s: phase=%u fora do intervalo (0..%u)", operation, phase, oly::app::PhaseCount - 1);
      return static_cast<oly::app::Phase>(phase);
    }

    oly::ecs::Entity entityOf(uint32_t index, uint32_t generation) {
      return oly::ecs::Entity{index, generation};
    }
  }

  oly::app::Engine& engineOf(OlyEngine* engine) noexcept {
    return engine->engine;
  }

  ScriptInvoker setScriptInvoker(ScriptInvoker invoker) noexcept {
    const ScriptInvoker previous = g_invoker;
    g_invoker = invoker;
    return previous;
  }
}

extern "C" {
OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame) {
  return new OlyEngine(oly::app::EngineConfig{fixedHz, maxStepsPerFrame});
}

void olyEngineDestroy(OlyEngine* engine) {
  delete engine;
}

OlyWorld* olyEngineWorld(OlyEngine* engine) {
  return &engine->world;
}

void olyEngineFrame(OlyEngine* engine, double nowSeconds) {
  // Depois de uma exceção JS o ponteiro ficou apontando para uma pilha que já não existe.
  engine->currentContext = nullptr;
  engine->engine.frame(nowSeconds);
}

void olyEngineSetHidden(OlyEngine* engine, int32_t hidden) {
  engine->engine.setHidden(hidden != 0);
}

void olyEnginePause(OlyEngine* engine) {
  engine->engine.pause();
}

void olyEngineResume(OlyEngine* engine) {
  engine->engine.resume();
}

int32_t olyEngineIsPaused(OlyEngine* engine) {
  return engine->engine.paused() ? 1 : 0;
}

double olyEngineTime(OlyEngine* engine) {
  return engine->engine.time();
}

double olyEngineFixedDeltaTime(OlyEngine* engine) {
  return engine->engine.fixedDeltaTime();
}

double olyEngineInterpolation(OlyEngine* engine) {
  return engine->engine.interpolation();
}

uint32_t olyEngineAddNativeSystem(OlyEngine* engine, uint32_t phase, const char* name) {
  const oly::app::Phase checkedPhase = oly::bridge::phaseOf(phase, "olyEngineAddNativeSystem");
  const oly::app::SystemFunction function = oly::app::findNamedSystem(name);
  if (function == nullptr) {
    return OLY_INVALID_SYSTEM;
  }

  return engine->engine.addSystem(checkedPhase, name, function);
}

uint32_t olyEngineAddScriptSystem(OlyEngine* engine, uint32_t phase, const char* name) {
  const oly::app::Phase checkedPhase = oly::bridge::phaseOf(phase, "olyEngineAddScriptSystem");

  return engine->engine.addSystem(checkedPhase, name, &oly::bridge::runScriptSystem, engine);
}

void olyEngineSetWhilePaused(OlyEngine* engine, uint32_t systemId, int32_t whilePaused) {
  engine->engine.setWhilePaused(systemId, whilePaused != 0);
}

void olySystemSpawn(OlyEngine* engine, OlyEntity* out) {
  const oly::ecs::Entity entity = oly::bridge::currentContext(engine, "olySystemSpawn").spawn();
  out->index = entity.index;
  out->generation = entity.generation;
}

void olySystemDestroy(OlyEngine* engine, uint32_t index, uint32_t generation) {
  oly::bridge::currentContext(engine, "olySystemDestroy").destroy(oly::bridge::entityOf(index, generation));
}

void olySystemAdd(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId, void* value) {
  oly::bridge::currentContext(engine, "olySystemAdd").addById(oly::bridge::entityOf(index, generation), componentId, value);
}

void olySystemRemove(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId) {
  oly::bridge::currentContext(engine, "olySystemRemove").removeById(oly::bridge::entityOf(index, generation), componentId);
}
}
