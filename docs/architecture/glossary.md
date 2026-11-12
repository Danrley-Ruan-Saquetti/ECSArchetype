# Glossary

The same verb stands for the same concept in C++, in the C API and in TS.

| Concept | C++ (`oly::ecs`, `oly::app`, `oly::input`) | C API | TS (`@oly/core`) |
|---|---|---|---|
| create entity | `World::spawn()`, `spawn(components...)`, `CommandBuffer::spawn()` | `olyEntitySpawn`, `olyDirectSpawn` (TS handler, task and hook, with the state scope) | `World.spawn` |
| destroy / alive | `destroy`, `isAlive` | `olyEntityDestroy`, `olyEntityIsAlive` | `destroy`, `isAlive` |
| add / remove / has | `add`, `remove`, `has` | `olyComponentAdd/Remove/Has` | `add`, `remove`, `has` |
| read (exists by contract) / if it exists | `get` -> `T&` / `tryGet` -> `T*` | `olyComponentGet` | `get` -> copy |
| query / iterate | `query<Ts...>`, `each`, `eachBlock` | `olyQueryCreate`, `olyQueryBegin/End` | `query`, `eachBlock` |
| read the only row of a query (exactly one / at most one) | `Query::single()` -> `tuple<Entity, Ts&...>` / `trySingle()` -> `optional` | (`olyQueryBegin/End`) | `query.single()` / `trySingle()` -> `SingleRow` (copy) |
| game (fixed) step / hidden tab | `Engine::frame(nowSeconds)`, `setHidden` | `olyEngineFrame`, `olyEngineSetHidden` | `Engine.frame`, `setHidden` |
| pause / resume | `Engine::pause`, `resume`, `paused`; `SystemContext::pause/resume` | `olyEnginePause/Resume/IsPaused` | `pause`, `resume`, `paused` |
| register system | `Engine::addSystem(phase, name, function)`, `OLY_SYSTEM(name)` (by name) | `olyEngineAddNativeSystem` (by name), `olyEngineAddScriptSystem` | `addSystem`, `addNativeSystem` |
| system runs while paused | `Engine::setWhilePaused` | `olyEngineSetWhilePaused` | `SystemRegistration.whilePaused()` |
| deferred structural change (inside a system) | `SystemContext::spawn/destroy/add/remove` | `olySystemSpawn/Destroy/Add/Remove` | `context.spawn/destroy/add/remove` |
| declare event (order = dispatch order) | `Engine::declareEvent<T>(name)` | (schema events, when the `OlyEngine` is created) | `event(name, fields)` in the schema |
| emit event | `Engine::emit`, `SystemContext/HandlerContext::emit` | `olyEngineEmit` | `context.emit` |
| declare action | `ActionMap::declareButton/declareAxis2d/declarePointer` | (schema actions, when the `OlyEngine` is created) | `action(name, kind)` in the schema |
| bind action to physical button | `ActionMap::bind`, `clearBindings`, `isBound` | `olyEngineBindButton/BindAxis2d`, `olyEngineClearBindings`, `olyEngineIsBound` | `engine.actions.bind/clearBindings/isBound` |
| deliver host input | `buttonDown`, `buttonUp`, `pointerMove`, `releaseAll` | `olyEngineButtonDown/Up`, `olyEnginePointerMove`, `olyEngineReleaseAll` | `engine.actions.buttonDown/buttonUp/pointerMove/releaseAll` |
| read action in the step | `ActionMap::button/axis2d/pointer` (`context.actions()`) | `olyEngineActionSnapshot` (read without crossing) | `context.actions.button/axis2d/pointer` |
| register handler (batch per round) | `Engine::on<T>(name, function)` | `olyEngineAddScriptHandler` | `Engine.on` |
| handler by name | `OLY_HANDLER(name, EventType)` | `olyEngineAddNativeHandler` | `addNativeHandler` |
| schedule task (game clock) | `Scheduler::after/every` (`engine.schedule()`, `context.schedule()`) | `olyEngineScheduleScriptTask` | `schedule.after/every` |
| cancel / query task | `Scheduler::cancel/isPending` | `olyEngineCancelTask`, `olyEngineIsTaskPending` | `schedule.cancel/isPending` |
| declare state (order = StateId) | `Engine::declareState(name)` | (schema states, when the `OlyEngine` is created) | `state(name)` in the schema |
| request / read state | `Engine::requestState`, `state`; `EngineContext::requestState/state` | `olyEngineRequestState`, `olyEngineState` | `engine.requestState`, `engine.state`, `context.requestState/state` |
| state hook | `Engine::onEnter/onExit(state, name, function)`, `OLY_STATE_HOOK(name)` (by name) | `olyEngineAddNativeStateHook` (by name), `olyEngineAddScriptStateHook` | `onEnter`, `onExit`, `addNativeOnEnter/addNativeOnExit` |
| restrict to states | `Engine::setSystemStates/setHandlerStates` (and `*StateMask`) | `olyEngineSetSystemStates/SetHandlerStates` | `registration.inState(...states)` |
| persist (remove the state scope) | `SystemContext/DirectContext::persist`, `Scheduler::persist` | `olyContextPersist`, `olyEnginePersistTask` | `context.persist`, `schedule.persist` |
| variant by ID | `ById` suffix: `addById`, `removeById`, `hasById`, `getById`, `tryGetById`, `queryById` | (all by ID) | — |

## Naming rules

- The `oly` prefix appears only in the C API (`oly*`, `OlyWorld`, `OLY_API`) and in what is internal to the bridge (`OlyModule`). The C++ and TS APIs do not repeat `oly` in names. The reason is in [modules-and-boundaries.md](modules-and-boundaries.md#vocabulary-rules).
- `create` and "Raw" do not exist as names. An entity is born through `spawn`; the variant by `ComponentId` uses the `ById` suffix.
- There are no aliases or compatibility `using` declarations for old names.
