# `bridge`: the C API

`bridge` (`engine/runtime/bridge/`) exposes the `extern "C"` C API (`c_api.cpp`, `engine_api.cpp`) and is the only place in the core that knows about the TS side.

| Group | Functions |
|---|---|
| World and schema | `olyWorldCreate/Destroy/CheckInvariants`, `olySchemaHash`, `olySchemaComponentCount/ComponentId`, `olySchemaEvent*`, `olySchemaAction*`, `olySchemaStateCount`, `olyComponentSize/Alignment` |
| Entities and components | `olyEntitySpawn/Destroy/IsAlive`, `olyComponentAdd/Remove/Has/Get` |
| Queries | `olyQueryCreate`, `olyQueryBegin`, `olyQueryEnd` |
| Engine and loop | `olyEngineCreate/Destroy/World`, `olyEngineFrame/SetHidden`, `olyEnginePause/Resume/IsPaused`, `olyEngineTime/FixedDeltaTime/Interpolation` |
| Systems | `olyEngineAddNativeSystem` (by name), `olyEngineAddScriptSystem`, `olyEngineSetWhilePaused`, `olySystemSpawn/Destroy/Add/Remove` |
| Events | `olyEngineEmit`, `olyEngineAddNativeHandler`, `olyEngineAddScriptHandler` |
| Input | `olyEngineBindButton/BindAxis2d/ClearBindings/IsBound`, `olyEngineButtonDown/Up`, `olyEnginePointerMove`, `olyEngineReleaseAll`, `olyEngineActionSnapshot` |
| Schedule | `olyEngineScheduleScriptTask`, `olyEngineCancelTask`, `olyEngineIsTaskPending`, `olyEnginePersistTask` |
| States | `olyEngineRequestState`, `olyEngineState`, `olyEngineSetSystemStates/SetHandlerStates`, `olyEngineAddNativeStateHook/AddScriptStateHook`, `olyDirectSpawn`, `olyContextPersist` |

## Design points

- **`OlyWorld` is opaque.** It holds the `World` and a stack of iteration slots. The game's C++ systems reach the `World` through `oly::bridge::worldOf(OlyWorld*)`.
- **`olyQueryBegin` returns an `OlyIteration*`** with its own descriptor buffer (`count`, address of the entities, address of each column). A nested `eachBlock` does not overwrite the outer one. `olyQueryEnd` must close the top slot.
- **`OlyEngine`** holds an `OlyWorld` and the `Engine` on top of it (one `World` per `OlyEngine`). On creation, it declares the schema's events, actions and states in schema order, so the index in the TS schema is the id in C++.
- **A single imported JS function**, `olyHostInvokeScript(engine, kind, id, events, eventCount)`, serves all TS code called from C++. There is no `addFunction` and no growing function table. The `engine` parameter allows several engines per WASM module. `kind` says what to call and the meaning of the other parameters:

  | `kind` | `id` | `events` / `eventCount` |
  |---|---|---|
  | `OLY_SCRIPT_SYSTEM` | system | null / 0; one call per system per step |
  | `OLY_SCRIPT_HANDLER` | handler | address of the queue in the heap / batch size; one call per batch |
  | `OLY_SCRIPT_TASK` | `scriptId` | null / 1 on the last run, otherwise 0 |
  | `OLY_SCRIPT_TASK_DISCARD` | `scriptId` | null / 0 |
  | `OLY_SCRIPT_STATE_HOOK` | `hookId` | null / the state on the other side of the change (the source in an `onEnter`, the destination in an `onExit`) |
- **Native names are resolved at creation.** `OLY_SYSTEM`, `OLY_HANDLER(name, EventType)` and `OLY_STATE_HOOK` register functions by name. An unknown name returns `OLY_INVALID_HANDLER`; a wrong event type, `OLY_HANDLER_TYPE_MISMATCH`.
- **Current context.** `olyDirectSpawn` and `olyContextPersist` use the context TS is in: direct in a handler, task or hook; deferred in a system.
- In the native build, `oly::bridge::setScriptInvoker` swaps the invoker for tests.

The TS side that consumes this API is in [ts-packages.md](ts-packages.md). The link contract with the game's generated code is in [schema-and-codegen.md](schema-and-codegen.md).
