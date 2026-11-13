#pragma once

#include <cstdint>

#include "oly/capi/c_api.hpp"

struct OlyEngine;

#define OLY_INVALID_SYSTEM 0xFFFFFFFFu
#define OLY_INVALID_HANDLER 0xFFFFFFFFu
#define OLY_HANDLER_TYPE_MISMATCH 0xFFFFFFFEu

// `kind` of a script call: what the invoker runs and the meaning of `id`, `events` and `eventCount` (OlyScriptInvoker).
#define OLY_SCRIPT_SYSTEM 0u
#define OLY_SCRIPT_HANDLER 1u
#define OLY_SCRIPT_TASK 2u
#define OLY_SCRIPT_TASK_DISCARD 3u
#define OLY_SCRIPT_STATE_HOOK 4u

#define OLY_NO_STATE 0xFFFFFFFFu
#define OLY_INVALID_STATE_HOOK 0xFFFFFFFFu
#define OLY_STATE_ENTER 0u
#define OLY_STATE_EXIT 1u

struct OlyTaskHandle {
  uint32_t slot;
  uint32_t generation;
};

// Mirror of oly::input::ActionState (12 B per action): JS reads the snapshot straight from the heap.
struct OlyActionState {
  float x;
  float y;
  uint32_t flags;
};

#define OLY_ACTION_PRESSED 1u
#define OLY_ACTION_HELD 2u
#define OLY_ACTION_RELEASED 4u
#define OLY_ACTION_VALID 8u

extern "C" {
// Runs the script code of one engine. `kind` is OLY_SCRIPT_SYSTEM (`id` is the system; `events` null and `eventCount`
// 0), OLY_SCRIPT_HANDLER (`id` is the handler; `events` is the batch, with the event struct's layout), OLY_SCRIPT_TASK
// (`id` is the task's scriptId; `events` null and `eventCount` 1 on the last run, 0 on the others),
// OLY_SCRIPT_TASK_DISCARD (`id` is the scriptId; `events` null and `eventCount` 0) or OLY_SCRIPT_STATE_HOOK (`id` is
// the hookId; `events` null and `eventCount` is the state on the other side of the switch). `userData` is the pointer
// given to olyEngineSetScriptInvoker.
typedef void (*OlyScriptInvoker)(void* userData, OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount);

// fixedHz, maxStepsPerFrame and maxEventRounds must be positive (OLY_ASSERT). Declares, in schema order, all
// the events from oly::capi::schemaEvents(): the event's index in the schema is its event type (eventType).
// After the events and the actions, declares the states from oly::capi::schemaStates(): the state's index in the schema is
// its StateId.
OLY_API OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame, uint32_t maxEventRounds);
OLY_API void olyEngineDestroy(OlyEngine* engine);
// Owned by the engine: valid until olyEngineDestroy and never passed to olyWorldDestroy.
OLY_API OlyWorld* olyEngineWorld(OlyEngine* engine);
// `invoker` is not null (OLY_ASSERT). Valid inside and outside olyEngineFrame: the next script call uses it. Before
// the first call, a script call is a contract violation (OLY_ASSERT with kind and id). olyEngineDestroy calls the invoker with OLY_SCRIPT_TASK_DISCARD for each pending script task,
// so `userData` must stay valid until then.
OLY_API void olyEngineSetScriptInvoker(OlyEngine* engine, OlyScriptInvoker invoker, void* userData);

// Contract of the loop, pause and hidden tab: that of oly::app::Engine (oly/app/engine.hpp).
OLY_API void olyEngineFrame(OlyEngine* engine, double nowSeconds);
OLY_API void olyEngineSetHidden(OlyEngine* engine, int32_t hidden);
OLY_API void olyEnginePause(OlyEngine* engine);
OLY_API void olyEngineResume(OlyEngine* engine);
OLY_API int32_t olyEngineIsPaused(OlyEngine* engine);
OLY_API double olyEngineTime(OlyEngine* engine);
OLY_API double olyEngineFixedDeltaTime(OlyEngine* engine);
OLY_API double olyEngineInterpolation(OlyEngine* engine);

// `phase` is an oly::app::Phase (out of range, OLY_ASSERT). Registering during olyEngineFrame is a contract violation.
// OLY_INVALID_SYSTEM if there is no native system with that name (registered by OLY_SYSTEM).
OLY_API uint32_t olyEngineAddNativeSystem(OlyEngine* engine, uint32_t phase, const char* name);
// The system is called by the engine's script invoker with the id returned here.
OLY_API uint32_t olyEngineAddScriptSystem(OlyEngine* engine, uint32_t phase, const char* name);
OLY_API void olyEngineSetWhilePaused(OlyEngine* engine, uint32_t systemId, int32_t whilePaused);

// eventType < olySchemaEventCount() (OLY_ASSERT). Copies the event (the type's size) into the queue; valid inside and outside
// olyEngineFrame. Emitted events are dispatched in step 4 of the fixed step, batched by type.
OLY_API void olyEngineEmit(OlyEngine* engine, uint32_t eventType, const void* value);
// OLY_INVALID_HANDLER if there is no native handler with that name (registered by OLY_HANDLER) and
// OLY_HANDLER_TYPE_MISMATCH if the named handler is for another event type. Registering during olyEngineFrame is an error.
OLY_API uint32_t olyEngineAddNativeHandler(OlyEngine* engine, uint32_t eventType, const char* name);
// The handler is called by the script invoker once per batch, with the id returned here.
OLY_API uint32_t olyEngineAddScriptHandler(OlyEngine* engine, uint32_t eventType, const char* name);

// Script task on the game clock. intervalSeconds 0 schedules an after; greater than 0, an every with the first
// firing at delaySeconds. Valid inside and outside olyEngineFrame. The invoker receives OLY_SCRIPT_TASK on each firing
// (eventCount is 1 on the last run of an after) and OLY_SCRIPT_TASK_DISCARD, once, when the task leaves the queue without
// its last run (cancellation or olyEngineDestroy). The Scheduler asserts apply (invalid delay and interval).
OLY_API void olyEngineScheduleScriptTask(OlyEngine* engine, double delaySeconds, double intervalSeconds, uint32_t scriptId, OlyTaskHandle* out);
// 1 if the task was pending; 0 if it already finished, was canceled or the handle belongs to an older task of the slot.
OLY_API int32_t olyEngineCancelTask(OlyEngine* engine, uint32_t slot, uint32_t generation);
OLY_API int32_t olyEngineIsTaskPending(OlyEngine* engine, uint32_t slot, uint32_t generation);

// States: olyEngineCreate declares the schema's states, so the index in the schema is the StateId. The request is valid inside and outside
// olyEngineFrame (the last one wins) and the switch happens in step 5. A state out of range is a contract violation (OLY_ASSERT).
OLY_API void olyEngineRequestState(OlyEngine* engine, uint32_t state);
// OLY_NO_STATE until the first switch.
OLY_API uint32_t olyEngineState(OlyEngine* engine);
// One bit per state; stateMask 0 removes the restriction. A mask with the bit of an undeclared state is a contract violation.
OLY_API void olyEngineSetSystemStates(OlyEngine* engine, uint32_t systemId, uint32_t stateMask);
OLY_API void olyEngineSetHandlerStates(OlyEngine* engine, uint32_t handlerId, uint32_t stateMask);
// `when` is OLY_STATE_ENTER or OLY_STATE_EXIT (any other value is a contract violation). The native one returns OLY_INVALID_STATE_HOOK if
// there is no hook with that name (OLY_STATE_HOOK). The script one calls the engine's invoker with (OLY_SCRIPT_STATE_HOOK,
// hookId, NULL, other), where `other` is the state on the other side of the switch: the source one in an OLY_STATE_ENTER and the destination one
// in an OLY_STATE_EXIT. The hook's side is olyEngineState at the time of the call.
OLY_API uint32_t olyEngineAddNativeStateHook(OlyEngine* engine, uint32_t when, uint32_t state, const char* name);
OLY_API uint32_t olyEngineAddScriptStateHook(OlyEngine* engine, uint32_t when, uint32_t state, const char* name);
// Only inside a script handler, task or hook; applies the current state's scope.
OLY_API void olyDirectSpawn(OlyEngine* engine, OlyEntity* out);
// Inside a script system it is deferred; in a script handler, task or hook it is direct (the entity must be alive).
OLY_API void olyContextPersist(OlyEngine* engine, uint32_t index, uint32_t generation);
// 1 if the task was pending and lost its scope; 0 if it is not pending.
OLY_API int32_t olyEnginePersistTask(OlyEngine* engine, uint32_t slot, uint32_t generation);

// Actions: olyEngineCreate declares, after the events, all those from oly::capi::schemaActions(); the action's index in the
// schema is its ActionId (action). `button` is a HID usage (page in the high 16 bits). Binding checks the range
// and the kind of the action (OLY_ASSERT); clearing checks only the range. Valid inside and outside olyEngineFrame.
OLY_API void olyEngineBindButton(OlyEngine* engine, uint32_t action, uint32_t button);
OLY_API void olyEngineBindAxis2d(OlyEngine* engine, uint32_t action, uint32_t up, uint32_t down, uint32_t left, uint32_t right);
OLY_API void olyEngineClearBindings(OlyEngine* engine, uint32_t action);
OLY_API int32_t olyEngineIsBound(OlyEngine* engine, uint32_t button);
// Host input: enters the queue and only becomes action state at the capture of the next step.
OLY_API void olyEngineButtonDown(OlyEngine* engine, uint32_t button);
OLY_API void olyEngineButtonUp(OlyEngine* engine, uint32_t button);
OLY_API void olyEnginePointerMove(OlyEngine* engine, float x, float y);
OLY_API void olyEngineReleaseAll(OlyEngine* engine);
// One OlyActionState per action, in schema order; stable until olyEngineDestroy because all actions are
// declared at creation.
OLY_API const OlyActionState* olyEngineActionSnapshot(OlyEngine* engine);

// Only valid inside a script system (outside it, OLY_ASSERT). Changes deferred until the end of the phase.
OLY_API void olySystemSpawn(OlyEngine* engine, OlyEntity* out);
OLY_API void olySystemDestroy(OlyEngine* engine, uint32_t index, uint32_t generation);
OLY_API void olySystemAdd(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
OLY_API void olySystemRemove(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId);
}
