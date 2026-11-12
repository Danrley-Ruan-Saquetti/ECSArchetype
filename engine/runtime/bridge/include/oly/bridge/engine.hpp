#pragma once

#include <cstdint>

#include "oly/app/engine.hpp"
#include "oly/bridge/engine_api.hpp"

namespace oly::bridge {
  // Access to the Engine behind the C API's opaque handle, for games written in C++.
  oly::app::Engine& engineOf(OlyEngine* engine) noexcept;

  // `kind` is OLY_SCRIPT_SYSTEM (`id` is the system; `events` null and `eventCount` 0), OLY_SCRIPT_HANDLER (`id` is the
  // handler; `events` is the batch, with the event struct's layout), OLY_SCRIPT_TASK (`id` is the task's scriptId;
  // `events` null and `eventCount` 1 on the last run, 0 on the others), OLY_SCRIPT_TASK_DISCARD (`id` is the scriptId;
  // `events` null and `eventCount` 0) or OLY_SCRIPT_STATE_HOOK (`id` is the hookId; `events` null and `eventCount` is the state on the other side
  // of the switch).
  using ScriptInvoker = void (*)(OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount);

  // Replaces whoever runs the script systems and returns the previous one. In WASM the default calls JS; in native, it fires an assert.
  ScriptInvoker setScriptInvoker(ScriptInvoker invoker) noexcept;
}
