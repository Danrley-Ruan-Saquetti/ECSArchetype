#include "oly/bridge/engine_api.hpp"

#include "handles.hpp"
#include "oly/app/system.hpp"
#include "oly/app/handler.hpp"
#include "oly/bridge/engine.hpp"
#include "oly/bridge/schema_registry.hpp"
#include "oly/core/assert.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// Module.olyInvokeScript é instalado pelo Runtime de @oly/core (runtime.ts) antes de qualquer frame.
EM_JS(void, olyHostInvokeScript, (OlyEngine * engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount), { Module["olyInvokeScript"](engine, kind, id, events, eventCount); });
#endif

namespace oly::bridge {
  namespace {
    void defaultInvoker([[maybe_unused]] OlyEngine* engine, [[maybe_unused]] uint32_t kind, [[maybe_unused]] uint32_t id, [[maybe_unused]] const void* events, [[maybe_unused]] uint32_t eventCount) {
#if defined(__EMSCRIPTEN__)
      olyHostInvokeScript(engine, kind, id, events, eventCount);
#else
      OLY_ASSERT(false, "script chamado sem invocador instalado (setScriptInvoker) kind=%u id=%u", kind, id);
#endif
    }

    ScriptInvoker g_invoker = &defaultInvoker;

    void runScriptSystem(oly::app::SystemContext& context) {
      OlyEngine* engine = static_cast<OlyEngine*>(context.userData());

      engine->currentContext = &context;
      g_invoker(engine, OLY_SCRIPT_SYSTEM, context.systemId(), nullptr, 0);
      engine->currentContext = nullptr;
    }

    void runScriptHandler(oly::app::HandlerContext& context) {
      OlyEngine* engine = static_cast<OlyEngine*>(context.userData());

      g_invoker(engine, OLY_SCRIPT_HANDLER, context.handlerId(), context.eventData(), context.eventCount());
    }

    uint32_t checkedEventType(OlyEngine* engine, uint32_t eventType, [[maybe_unused]] const char* operation) {
      OLY_ASSERT(eventType < engine->engine.eventTypeCount(), "%s: eventType=%u fora do intervalo (0..%u)", operation, eventType, engine->engine.eventTypeCount() - 1);
      return eventType;
    }

    oly::app::SystemContext& currentContext(OlyEngine* engine, [[maybe_unused]] const char* operation) {
      OLY_ASSERT(engine->currentContext != nullptr, "%s: chamado fora de sistema", operation);
      return *engine->currentContext;
    }

    oly::app::Phase phaseOf(uint32_t phase, [[maybe_unused]] const char* operation) {
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
OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame, uint32_t maxEventRounds) {
  OlyEngine* engine = new OlyEngine(oly::app::EngineConfig{fixedHz, maxStepsPerFrame, maxEventRounds});

  for (const oly::bridge::SchemaEvent& schemaEvent : oly::bridge::schemaEvents()) {
    schemaEvent.declare(engine->engine);
  }

  return engine;
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

void olyEngineEmit(OlyEngine* engine, uint32_t eventType, const void* value) {
  engine->engine.emitById(oly::bridge::checkedEventType(engine, eventType, "olyEngineEmit"), value);
}

uint32_t olyEngineAddNativeHandler(OlyEngine* engine, uint32_t eventType, const char* name) {
  const uint32_t checkedType = oly::bridge::checkedEventType(engine, eventType, "olyEngineAddNativeHandler");
  const oly::app::NamedHandler* named = oly::app::findNamedHandler(name);
  if (named == nullptr) {
    return OLY_INVALID_HANDLER;
  }
  if (engine->engine.eventTypeOf(named->eventKey) != checkedType) {
    return OLY_HANDLER_TYPE_MISMATCH;
  }

  return engine->engine.onById(checkedType, name, named->function);
}

uint32_t olyEngineAddScriptHandler(OlyEngine* engine, uint32_t eventType, const char* name) {
  const uint32_t checkedType = oly::bridge::checkedEventType(engine, eventType, "olyEngineAddScriptHandler");

  return engine->engine.onById(checkedType, name, &oly::bridge::runScriptHandler, engine);
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
