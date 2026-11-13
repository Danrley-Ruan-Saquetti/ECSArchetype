#include "oly/capi/engine_api.hpp"

#include <cstddef>

#include "handles.hpp"
#include "oly/app/handler.hpp"
#include "oly/app/state_context.hpp"
#include "oly/app/system.hpp"
#include "oly/app/task_context.hpp"
#include "oly/capi/engine.hpp"
#include "oly/capi/schema_registry.hpp"
#include "oly/core/assert.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// Module.olyInvokeScript is installed by the @oly/core Runtime (runtime.ts) before any frame.
EM_JS(void, olyHostInvokeScript, (OlyEngine * engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount), { Module["olyInvokeScript"](engine, kind, id, events, eventCount); });
#endif

static_assert(sizeof(OlyActionState) == sizeof(oly::input::ActionState) && offsetof(OlyActionState, x) == offsetof(oly::input::ActionState, x) && offsetof(OlyActionState, y) == offsetof(oly::input::ActionState, y) && offsetof(OlyActionState, flags) == offsetof(oly::input::ActionState, flags), "OlyActionState must have the layout of oly::input::ActionState");
static_assert(OLY_ACTION_PRESSED == oly::input::ActionPressed && OLY_ACTION_HELD == oly::input::ActionHeld && OLY_ACTION_RELEASED == oly::input::ActionReleased && OLY_ACTION_VALID == oly::input::ActionValid, "OLY_ACTION_* must match oly::input::Action*");

namespace oly::capi {
  namespace {
    void defaultInvoker([[maybe_unused]] OlyEngine* engine, [[maybe_unused]] uint32_t kind, [[maybe_unused]] uint32_t id, [[maybe_unused]] const void* events, [[maybe_unused]] uint32_t eventCount) {
#if defined(__EMSCRIPTEN__)
      olyHostInvokeScript(engine, kind, id, events, eventCount);
#else
      OLY_ASSERT(false, "script called with no invoker installed (setScriptInvoker) kind=%u id=%u", kind, id);
#endif
    }

    ScriptInvoker g_invoker = &defaultInvoker;

    void runScriptSystem(oly::app::SystemContext& context) {
      OlyEngine* engine = static_cast<OlyEngine*>(context.userData());

      engine->currentContext = &context;
      g_invoker(engine, OLY_SCRIPT_SYSTEM, context.systemId(), nullptr, 0);
      engine->currentContext = nullptr;
    }

    // Holds the current direct context for olyDirectSpawn and olyContextPersist and restores the previous one on exit.
    class DirectContextScope {
     private:
      OlyEngine* engine_;
      oly::app::DirectContext* previous_;

     public:
      DirectContextScope(OlyEngine* engine, oly::app::DirectContext& context) noexcept
        : engine_(engine),
          previous_(engine->directContext) {
        engine->directContext = &context;
      }
      DirectContextScope(const DirectContextScope&) = delete;
      DirectContextScope& operator=(const DirectContextScope&) = delete;
      ~DirectContextScope() {
        engine_->directContext = previous_;
      }
    };

    void runScriptHandler(oly::app::HandlerContext& context) {
      OlyEngine* engine = static_cast<OlyEngine*>(context.userData());

      DirectContextScope scope(engine, context);
      g_invoker(engine, OLY_SCRIPT_HANDLER, context.handlerId(), context.eventData(), context.eventCount());
    }

    void runScriptStateHook(oly::app::StateContext& context) {
      OlyEngine* engine = static_cast<OlyEngine*>(context.userData());
      const oly::app::StateId otherSide = context.entering() ? context.fromState() : context.toState();

      DirectContextScope scope(engine, context);
      g_invoker(engine, OLY_SCRIPT_STATE_HOOK, context.hookId(), nullptr, otherSide);
    }

    struct ScriptTaskPayload {
      OlyEngine* engine;
      uint32_t scriptId;
    };

    void runScriptTask(oly::app::TaskContext& context) {
      const ScriptTaskPayload& payload = context.payload<ScriptTaskPayload>();

      DirectContextScope scope(payload.engine, context);
      g_invoker(payload.engine, OLY_SCRIPT_TASK, payload.scriptId, nullptr, context.lastRun() ? 1 : 0);
    }

    void discardScriptTask(void* payload) {
      const ScriptTaskPayload& task = *static_cast<ScriptTaskPayload*>(payload);

      g_invoker(task.engine, OLY_SCRIPT_TASK_DISCARD, task.scriptId, nullptr, 0);
    }

    uint32_t checkedEventType([[maybe_unused]] OlyEngine* engine, uint32_t eventType, [[maybe_unused]] const char* operation) {
      OLY_ASSERT(eventType < engine->engine.eventTypeCount(), "%s: eventType=%u out of range (0..%u)", operation, eventType, engine->engine.eventTypeCount() - 1);
      return eventType;
    }

    oly::app::SystemContext& currentContext(OlyEngine* engine, [[maybe_unused]] const char* operation) {
      OLY_ASSERT(engine->currentContext != nullptr, "%s: called outside a system", operation);
      return *engine->currentContext;
    }

    void checkedAction([[maybe_unused]] const oly::input::ActionMap& actions, [[maybe_unused]] uint32_t action, [[maybe_unused]] oly::input::ActionKind expected, [[maybe_unused]] const char* operation) {
      OLY_ASSERT(action < actions.actionCount(), "%s: action=%u out of range (%u declared)", operation, action, actions.actionCount());
      OLY_ASSERT(actions.kind(action) == expected, "%s: action=%u (%.*s) has a kind other than the expected one (%u)", operation, action, static_cast<int>(actions.name(action).size()), actions.name(action).data(), static_cast<unsigned>(expected));
    }

    oly::app::Phase phaseOf(uint32_t phase, [[maybe_unused]] const char* operation) {
      OLY_ASSERT(phase < oly::app::PhaseCount, "%s: phase=%u out of range (0..%u)", operation, phase, oly::app::PhaseCount - 1);
      return static_cast<oly::app::Phase>(phase);
    }

    uint32_t checkedWhen([[maybe_unused]] uint32_t when, [[maybe_unused]] const char* operation) {
      OLY_ASSERT(when == OLY_STATE_ENTER || when == OLY_STATE_EXIT, "%s: invalid when=%u", operation, when);
      return when;
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

  for (const oly::capi::SchemaEvent& schemaEvent : oly::capi::schemaEvents()) {
    schemaEvent.declare(engine->engine);
  }

  for (const oly::capi::SchemaAction& schemaAction : oly::capi::schemaActions()) {
    [[maybe_unused]] const oly::input::ActionId id = engine->engine.actions().declare(schemaAction.kind, schemaAction.name);
    OLY_ASSERT(id == engine->engine.actions().actionCount() - 1, "olyEngineCreate: action \"%.*s\" out of order", static_cast<int>(schemaAction.name.size()), schemaAction.name.data());
  }

  for (const oly::capi::SchemaState& schemaState : oly::capi::schemaStates()) {
    [[maybe_unused]] const oly::app::StateId id = engine->engine.declareState(schemaState.name);
    OLY_ASSERT(id == engine->engine.stateCount() - 1, "olyEngineCreate: state \"%.*s\" out of order", static_cast<int>(schemaState.name.size()), schemaState.name.data());
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
  // After a JS exception the pointer was left pointing to a stack that no longer exists.
  engine->currentContext = nullptr;
  engine->directContext = nullptr;
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
  const oly::app::Phase checkedPhase = oly::capi::phaseOf(phase, "olyEngineAddNativeSystem");
  const oly::app::SystemFunction function = oly::app::findNamedSystem(name);
  if (function == nullptr) {
    return OLY_INVALID_SYSTEM;
  }

  return engine->engine.addSystem(checkedPhase, name, function);
}

uint32_t olyEngineAddScriptSystem(OlyEngine* engine, uint32_t phase, const char* name) {
  const oly::app::Phase checkedPhase = oly::capi::phaseOf(phase, "olyEngineAddScriptSystem");
  return engine->engine.addSystem(checkedPhase, name, &oly::capi::runScriptSystem, engine);
}

void olyEngineSetWhilePaused(OlyEngine* engine, uint32_t systemId, int32_t whilePaused) {
  engine->engine.setWhilePaused(systemId, whilePaused != 0);
}

void olyEngineEmit(OlyEngine* engine, uint32_t eventType, const void* value) {
  engine->engine.emitById(oly::capi::checkedEventType(engine, eventType, "olyEngineEmit"), value);
}

uint32_t olyEngineAddNativeHandler(OlyEngine* engine, uint32_t eventType, const char* name) {
  const uint32_t checkedType = oly::capi::checkedEventType(engine, eventType, "olyEngineAddNativeHandler");
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
  const uint32_t checkedType = oly::capi::checkedEventType(engine, eventType, "olyEngineAddScriptHandler");

  return engine->engine.onById(checkedType, name, &oly::capi::runScriptHandler, engine);
}

void olyEngineScheduleScriptTask(OlyEngine* engine, double delaySeconds, double intervalSeconds, uint32_t scriptId, OlyTaskHandle* out) {
  const oly::app::TaskHandle task = engine->engine.schedule().schedule(delaySeconds, intervalSeconds, &oly::capi::runScriptTask, oly::app::TaskPayload::of(oly::capi::ScriptTaskPayload{engine, scriptId}), &oly::capi::discardScriptTask);

  out->slot = task.slot;
  out->generation = task.generation;
}

int32_t olyEngineCancelTask(OlyEngine* engine, uint32_t slot, uint32_t generation) {
  return engine->engine.schedule().cancel(oly::app::TaskHandle{slot, generation}) ? 1 : 0;
}

int32_t olyEngineIsTaskPending(OlyEngine* engine, uint32_t slot, uint32_t generation) {
  return engine->engine.schedule().isPending(oly::app::TaskHandle{slot, generation}) ? 1 : 0;
}

int32_t olyEnginePersistTask(OlyEngine* engine, uint32_t slot, uint32_t generation) {
  return engine->engine.schedule().persist(oly::app::TaskHandle{slot, generation}) ? 1 : 0;
}

void olyEngineRequestState(OlyEngine* engine, uint32_t state) {
  engine->engine.requestState(state);
}

uint32_t olyEngineState(OlyEngine* engine) {
  return engine->engine.state();
}

void olyEngineSetSystemStates(OlyEngine* engine, uint32_t systemId, uint32_t stateMask) {
  engine->engine.setSystemStateMask(systemId, stateMask);
}

void olyEngineSetHandlerStates(OlyEngine* engine, uint32_t handlerId, uint32_t stateMask) {
  engine->engine.setHandlerStateMask(handlerId, stateMask);
}

uint32_t olyEngineAddNativeStateHook(OlyEngine* engine, uint32_t when, uint32_t state, const char* name) {
  const uint32_t hookWhen = oly::capi::checkedWhen(when, "olyEngineAddNativeStateHook");
  const oly::app::StateHookFunction function = oly::app::findNamedStateHook(name);
  if (function == nullptr) {
    return OLY_INVALID_STATE_HOOK;
  }

  return hookWhen == OLY_STATE_ENTER ? engine->engine.onEnter(state, name, function) : engine->engine.onExit(state, name, function);
}

uint32_t olyEngineAddScriptStateHook(OlyEngine* engine, uint32_t when, uint32_t state, const char* name) {
  const uint32_t hookWhen = oly::capi::checkedWhen(when, "olyEngineAddScriptStateHook");

  return hookWhen == OLY_STATE_ENTER ? engine->engine.onEnter(state, name, &oly::capi::runScriptStateHook, engine) : engine->engine.onExit(state, name, &oly::capi::runScriptStateHook, engine);
}

void olyEngineBindButton(OlyEngine* engine, uint32_t action, uint32_t button) {
  oly::input::ActionMap& actions = engine->engine.actions();
  oly::capi::checkedAction(actions, action, oly::input::ActionKind::Button, "olyEngineBindButton");

  actions.bind(oly::input::ButtonAction{action}, oly::input::PhysicalButton(button));
}

void olyEngineBindAxis2d(OlyEngine* engine, uint32_t action, uint32_t up, uint32_t down, uint32_t left, uint32_t right) {
  oly::input::ActionMap& actions = engine->engine.actions();
  oly::capi::checkedAction(actions, action, oly::input::ActionKind::Axis2d, "olyEngineBindAxis2d");

  actions.bind(oly::input::Axis2dAction{action}, oly::input::Axis2dBinding{oly::input::PhysicalButton(up), oly::input::PhysicalButton(down), oly::input::PhysicalButton(left), oly::input::PhysicalButton(right)});
}

void olyEngineClearBindings(OlyEngine* engine, uint32_t action) {
  oly::input::ActionMap& actions = engine->engine.actions();
  OLY_ASSERT(action < actions.actionCount(), "olyEngineClearBindings: action=%u out of range (%u declared)", action, actions.actionCount());

  actions.clearBindings(action);
}

int32_t olyEngineIsBound(OlyEngine* engine, uint32_t button) {
  return engine->engine.actions().isBound(oly::input::PhysicalButton(button)) ? 1 : 0;
}

void olyEngineButtonDown(OlyEngine* engine, uint32_t button) {
  engine->engine.actions().buttonDown(oly::input::PhysicalButton(button));
}

void olyEngineButtonUp(OlyEngine* engine, uint32_t button) {
  engine->engine.actions().buttonUp(oly::input::PhysicalButton(button));
}

void olyEnginePointerMove(OlyEngine* engine, float x, float y) {
  engine->engine.actions().pointerMove(x, y);
}

void olyEngineReleaseAll(OlyEngine* engine) {
  engine->engine.actions().releaseAll();
}

const OlyActionState* olyEngineActionSnapshot(OlyEngine* engine) {
  return reinterpret_cast<const OlyActionState*>(engine->engine.actions().snapshot().data());
}

void olySystemSpawn(OlyEngine* engine, OlyEntity* out) {
  const oly::ecs::Entity entity = oly::capi::currentContext(engine, "olySystemSpawn").spawn();
  out->index = entity.index;
  out->generation = entity.generation;
}

void olySystemDestroy(OlyEngine* engine, uint32_t index, uint32_t generation) {
  oly::capi::currentContext(engine, "olySystemDestroy").destroy(oly::capi::entityOf(index, generation));
}

void olySystemAdd(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId, void* value) {
  oly::capi::currentContext(engine, "olySystemAdd").addById(oly::capi::entityOf(index, generation), componentId, value);
}

void olySystemRemove(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId) {
  oly::capi::currentContext(engine, "olySystemRemove").removeById(oly::capi::entityOf(index, generation), componentId);
}

void olyDirectSpawn(OlyEngine* engine, OlyEntity* out) {
  OLY_ASSERT(engine->directContext != nullptr, "olyDirectSpawn: called outside a handler, task or hook");

  const oly::ecs::Entity entity = engine->directContext->spawn();
  out->index = entity.index;
  out->generation = entity.generation;
}

void olyContextPersist(OlyEngine* engine, uint32_t index, uint32_t generation) {
  OLY_ASSERT(engine->directContext != nullptr || engine->currentContext != nullptr, "olyContextPersist: called outside a system, handler, task or hook");

  const oly::ecs::Entity entity = oly::capi::entityOf(index, generation);
  if (engine->directContext != nullptr) {
    engine->directContext->persist(entity);
  } else {
    engine->currentContext->persist(entity);
  }
}
}
