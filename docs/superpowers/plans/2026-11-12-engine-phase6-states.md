# Engine — Phase 6 (States) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Flat game states in the C++ core (`declareState`, request, transition at step 5 with `onExit` → cleanup → `onEnter`, `inState` on systems and handlers, scope of entities and tasks with `persist`), declared in the schema and exposed in the C API and in `@oly/core`.

**Architecture:** The `Engine` holds the declared states, the current state, the pending request and the hooks of each state; the transition runs in `applyStateTransition()` (step 5, paused or not) and ends with a new event dispatch. The scope is the internal tag `detail::StateScoped`: the contexts add it on `spawn` when a state is active, and the cleanup destroys the entities with the tag through a `QueryById` created once. Tasks gain a `scoped` field in the `TaskQueue` slot, and the cleanup cancels them with discard. The `bridge` declares the schema's states at creation, registers TS hooks through the single callback (`OLY_SCRIPT_STATE_HOOK`) and exposes `olyDirectSpawn`/`olyContextPersist`, which pick the current context. `@oly/core` only validates usage and forwards.

**Tech Stack:** C++20, GCC 14 (native), Emscripten 3.1.74 + node 20.18 (`wasm` service), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (phase 6 of §5; all of §4.7, §4.3 for the step order and pause, §4.4 for events emitted in hooks, §4.6 for task scope and discard, §6, §7 "States"). `§N` below refer to that spec.

## Global Constraints

- Branch `feat/engine-phase6-states` from `develop`. Commits, comments, assert and error messages in English; commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain only in Docker: `DC="docker compose run --rm dev"` (native, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). `wasm-games*` presets require `make ts` first.
- `-Wall -Wextra -Wpedantic -Werror` on every C++ target; TS `strict` (§3).
- Layers: `core ← input ← app ← bridge ← games` and `core ← ecs ← app`. No new module; the ECS and `input` do not change. The tag goes in `engine/runtime/app/include/oly/app/detail/` (§4.7); the `bridge` does not include it.
- Full engine in C++ (§2): request, transition, hook order, scope, cleanup, `inState` and recovery live in `app`. TS only validates usage, holds closures and forwards.
- Boundary: one call per TS hook per transition; `requestState`, `inState`, `persist` and registration cross once per call. Nothing per entity.
- `StateId` dense in declaration order; `NoState = 0xFFFFFFFF`; at most 32 states (`uint32_t` mask) (§4.7).
- No game concepts in `engine/runtime` or in `engine/packages`; game names only in `engine/testing/fixture/` and in the tests.
- No benchmark in this phase (§7).
- Glossary (`CLAUDE.md`): same verb in all three layers — `requestState`, `state`, `onEnter`/`onExit`, `persist`, `inState` (C++ `setSystemStates`/`setHandlerStates`). `oly` only in the C API.
- `CLAUDE.md` style and the `blank-lines`/`code-comments` skills: braces on every block, full names, data at the top of the class, one `namespace` per file, project includes with quotes after the standard library. Do not format by hand (`clang-format` hook). Comments only for a reason, an external constraint or a non-obvious contract; no narration of the edit; tests in Arrange/Act/Assert.

### Decisions the spec leaves open (settled here)

- **Files.** `oly/app/state.hpp`: types without context (`StateId`, `StateMask`, `StateHookId`, `NoState`, `MaxStates`, `StateHookFunction`, registration by name and `OLY_STATE_HOOK`). `oly/app/state_context.hpp`: `StateContext`. `oly/app/detail/state_scoped.hpp`: `struct StateScoped {};`. The `Engine`'s state methods go in `src/engine_states.cpp`, so that `engine.cpp` stays the loop. The split mirrors `task.hpp`/`task_context.hpp`.
- **Pending request.** `requestedState_ == NoState` means "no request": requesting `NoState` is a contract violation (out of range), so there is no ambiguity.
- **`inState` applies in every phase, including `render`.** Only `whilePaused` is restricted to gameplay. Test: `runsInState(mask) = mask == 0 || (state_ != NoState && (mask >> state_) & 1)`.
- **Valid mask.** `stateCount() == 32 || (mask >> stateCount()) == 0` (shifting by 32 is undefined behaviour). `setSystemStates(system, {ids...})` checks each id and builds the mask; `setSystemStateMask` checks the mask. The same for handlers, which gain `handlerLocations_` (`[HandlerId] → (type, position)`, like `systemLocations_`).
- **Scoped spawn.** `EngineContext` gains `protected: bool spawnsScoped() const noexcept` (in `context.cpp`: `engine_->state() != NoState`). `DirectContext::spawn(Ts&&...)` calls `world_->spawn(components..., detail::StateScoped{})` or `world_->spawn(components...)`, a single transition. `SystemContext::spawn()` moves out of the header into `system.cpp` (`commands_->spawn()` + `commands_->add(entity, detail::StateScoped{})`); the `spawn(Ts&&...)` template calls `spawn()` and then the `add`s, and the flush makes a single transition (R-CB-3).
- **`persist`.** `SystemContext::persist(entity)` = `commands_->remove<detail::StateScoped>(entity)` (same contract as the deferred `remove`); `DirectContext::persist(entity)` = `world_->remove<detail::StateScoped>(entity)` (a dead entity triggers the `remove` assert, which names `StateScoped`). Both live in `.cpp`, so `system.hpp` does not include `detail/`.
- **Cleanup.** `QueryById* scopedQuery_` created on the first cleanup with `QueryByIdSpec{.with = {componentId<detail::StateScoped>()}}` (creating it in the constructor would change the query count of existing tests); `eachBlock` gathers `block.entities()` into `scopedEntities_` (reused vector, `clear()` first); destroys after the iteration. Then `scheduler_.queue_.cancelScoped()`.
- **Tasks.** `TaskQueue::push(..., const TaskPayload& payload, bool scoped = false)`: the default keeps the phase 5 tests. `Scheduler::schedule` passes `engine_.state() != NoState`. `TaskQueue::persist(task)` clears `scoped` and returns `false` for a non-pending task or `TaskHandle{}`. `cancelScoped()` repeats "cancel every live scoped slot" until a pass cancels nothing, like `discardAll`.
- **Documented limitation.** Two `Engine`s over the same `World` share the tag: a transition in one cleans up the other's scoped entities. It goes in the comment on the `Engine` constructor; the bridge creates one `World` per `OlyEngine`.
- **`StateContext::entering()`.** True in `onEnter`. The bridge uses it to know the side of the transition, and so does a native hook registered for both moments.
- **C API, `OLY_SCRIPT_STATE_HOOK`.** `olyHostInvokeScript(engine, 4, hookId, NULL, other)`, where `other` is the state on the other side of the transition (`fromState` in an `onEnter`, `toState` in an `onExit`); the hook's side is the `olyEngineState` at the time of the call. §4.7 writes `eventCount` 0; §8 leaves the C API detail to the implementer, and this way TS builds `fromState`/`toState` without a new export.
- **Current context in the bridge.** `OlyEngine` gains `oly::app::DirectContext* directContext = nullptr` next to `currentContext` (system). `runScriptHandler`, `runScriptTask` and `runScriptStateHook` save the previous one, point to their own context and restore; `olyEngineFrame` clears both (JS exception). `olyContextPersist` uses the direct one, otherwise the system one, otherwise `OLY_ASSERT`.
- **TS.** The handle is called `State` (§4.7) and the name follows the action pattern (camelCase, not a C++ reserved word). The canonical hash form gains `#name;` per state, after the actions. More than 32 states is a `ContractError` in `defineSchema` (the generator receives the finished schema). `onEnter`/`onExit`/`addNativeOnEnter`/`addNativeOnExit` return `void`. `persist` lives in `SystemContext` (without checking liveness, like `remove`) and in `DirectContext` (`ContractError` for a dead entity). The mask comes from `stateMaskOf(indices)` with `>>> 0`, because `1 << 31` is negative in JS.
- **Direct spawn from TS.** `world.ts` gains the internal function `spawnWith(world, types, values, create: () => Entity)`, with today's validations; `World.spawn` passes `() => bridge.spawn(handle)` and `DirectContext.spawn` passes `() => bridge.directSpawn(engineHandle)`.
- **Out of scope:** nested states or a stack, giving scope to something persistent, benchmark.

## Review Focus

1. **Event emitted in `onExit` referring to a scoped entity**: it is dispatched in item 6, after the cleanup; the handler sees `isAlive` false and nothing triggers an assert (Task 3).
2. **What `onExit` creates (entity and task) is cleaned up in the same transition; what `onEnter` creates gets the new scope** and only goes away on the following transition (Task 3).
3. **A system that creates an entity and calls `persist` on it in the transition step**: the tag's `add` and `remove` cancel out in the flush and the entity survives the step 5 cleanup (Task 3).
4. **`inState` with the state at index 31 from TS**: the mask is `0x80000000`, not a negative number that the C API would read wrongly (Task 6).
5. **Transition request with the tab hidden**: no step runs; on return, the first `frame` only records the time and the transition happens at step 5 of the following step (Task 2).

---

### Task 1: Scope in the `TaskQueue`

**Files:**
- Modify: `engine/runtime/app/include/oly/app/detail/task_queue.hpp`, `engine/runtime/app/src/task_queue.cpp`, `engine/runtime/app/tests/task_queue.cpp`

**Interfaces:**
- Produces (`oly::app::detail::TaskQueue`): `Slot` gains `bool scoped = false`; `TaskHandle push(double due, double interval, TaskFunction function, TaskDiscardFunction discard, const TaskPayload& payload, bool scoped = false)`; `bool persist(TaskHandle task)` (slot contract from `matches`, operation `"persist"`); `void cancelScoped()` (discard once per task, including scoped ones scheduled by discards).

- [ ] **Step 1: Branch** `git switch -c feat/engine-phase6-states`.

- [ ] **Step 2: Failing tests** in `tests/task_queue.cpp` (same discard counter as phase 5):
```cpp
TEST_CASE("push unscoped by default: cancelScoped does not touch it") { /* push without scoped; cancelScoped */ CHECK(queue.isPending(handle)); CHECK(discards == 0); }
TEST_CASE("cancelScoped cancels only the scoped ones, with discard once") { /* 2 scoped, 1 unscoped */ CHECK(discards == 2); CHECK(queue.pendingCount() == 1); CHECK(queue.isPending(unscoped)); }
TEST_CASE("cancelScoped repeats for scoped tasks scheduled by discards") { /* the 1st one's discard pushes another scoped one with a discard */ CHECK(discards == 2); CHECK(queue.pendingCount() == 0); }
TEST_CASE("persist removes the scope") { /* scoped push; persist; cancelScoped */ CHECK(queue.persist(handle)); CHECK(queue.isPending(handle)); }
TEST_CASE("persist of a non-pending task or of TaskHandle{} returns false") { CHECK(!queue.persist(cancelled)); CHECK(!queue.persist(TaskHandle{})); }
TEST_CASE("cancelScoped does not catch old entries of a slot reused unscoped") { /* scoped push A; cancel A; unscoped push B in the same slot; cancelScoped */ CHECK(queue.isPending(b)); }
```

- [ ] **Step 3: See it fail** — `$DC make test T='app.task_queue'` → compilation error (`persist`/`cancelScoped` do not exist).

- [ ] **Step 4: Implement.** `push` writes `slot.scoped`; `release` does not need to clear it (the next `push` overwrites it). `cancelScoped` uses `cancel(TaskHandle{index, generation})` so that the discard runs after the slot is freed.

- [ ] **Step 5: See it pass** — `$DC make test T='^app\.'` and `$WC make test PRESET=wasm-debug T='app.task_queue'`.

- [ ] **Step 6: Commit** `feat: task scope in the app queue with persist and cancellation`.

---

### Task 2: States in the `Engine` — declaration, request, transition, hooks and `inState`

**Files:**
- Create: `engine/runtime/app/include/oly/app/{state.hpp,state_context.hpp}`, `engine/runtime/app/src/{state.cpp,engine_states.cpp}`, `engine/runtime/app/tests/states.cpp`
- Modify: `engine/runtime/app/include/oly/app/{engine.hpp,context.hpp}`, `engine/runtime/app/src/{engine.cpp,context.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp,recovery.cpp}`

**Interfaces:**
- Produces (`oly::app`, `state.hpp`): `using StateId = uint32_t; using StateMask = uint32_t; using StateHookId = uint32_t;` `inline constexpr StateId NoState = 0xFFFFFFFFu;` `inline constexpr uint32_t MaxStates = 32;` `class StateContext;` `using StateHookFunction = void (*)(StateContext& context);` `StateHookFunction findNamedStateHook(std::string_view name) noexcept;` (`nullptr` if it does not exist) `struct NamedStateHookRegistration { NamedStateHookRegistration(std::string_view name, StateHookFunction function); };` and `OLY_STATE_HOOK(name)`, which defines `void name(oly::app::StateContext& context)` and registers it, like `OLY_SYSTEM`.
- Produces (`state_context.hpp`): `class StateContext : public DirectContext` with a private constructor (`friend class Engine`) `(Engine&, ecs::World&, StateHookId hookId, std::string_view name, void* userData, StateId fromState, StateId toState, bool entering)` and `hookId()`, `name()`, `userData()`, `fromState()`, `toState()`, `entering()`, all `const noexcept`.
- Produces (`Engine`): `StateId declareState(std::string_view name)`, `uint32_t stateCount() const noexcept`, `void requestState(StateId state)`, `StateId state() const noexcept`, `StateHookId onEnter(StateId state, std::string_view name, StateHookFunction function, void* userData = nullptr)`, `StateHookId onExit(...)` (same signature; `StateHookId` counts enter and exit together), `void setSystemStates(SystemId system, std::initializer_list<StateId> states)`, `void setSystemStateMask(SystemId system, StateMask mask)`, `void setHandlerStates(HandlerId handler, std::initializer_list<StateId> states)`, `void setHandlerStateMask(HandlerId handler, StateMask mask)`. `SystemEntry` and `HandlerEntry` gain `StateMask states = 0`. `applyStateTransition()` moves out of the header (empty) into `engine_states.cpp`.
- Produces (`EngineContext`): `void requestState(StateId state)`, `StateId state() const noexcept`.

`applyStateTransition()` algorithm (§4.7, "Transition"):
```
if requestedState_ == NoState: nothing
target = requestedState_; requestedState_ = NoState       // a request made in a hook applies on the following step
from = state_
run onExit of from (if from != NoState), in registration order, with StateContext(from, target, entering=false)
scope cleanup (Task 3; in this task, an empty spot)
state_ = target
run onEnter of target, in registration order, with StateContext(from, target, entering=true)
dispatchEvents()                                          // same rounds as the step, same limit
```
`recoverFromInterruptedFrame` does `requestedState_ = NoState`. In `runPhase` and `dispatchEvents`, a system or handler with `!runsInState(entry.states)` does not run.

Contracts (`OLY_ASSERT`):

| Operation | Message |
|---|---|
| `declareState` during frame / repeated name / 33rd state | `"declareState: state \"%s\" declared during frame"`, `"declareState: state \"%s\" already declared"`, `"declareState: state \"%s\" exceeds the maximum of 32"` |
| `requestState`, `onEnter`, `onExit`, `setSystemStates`, `setHandlerStates` with an id out of range | `"%s: state=%u out of range (%u states)"` |
| `onEnter`/`onExit` during frame | `"%s: hook \"%s\" registered during frame"` |
| `set*States`/`set*StateMask` during frame | `"%s: state restriction during frame"` |
| mask with a bit beyond `stateCount()` | `"%s: mask 0x%x with undeclared state (%u states)"`, with `%s` = `setSystemStates`/`setHandlerStates` also in the `*StateMask` variants |

- [ ] **Step 1: Failing tests** in `tests/states.cpp` (`oly_add_test(MODULE app NAME states SOURCES states.cpp SUPPORT engine/runtime/ecs/tests/support)`). Fixture like `schedule.cpp`: `frame(0)` only records; `step()` advances 1/60. Hooks record `"exit:<name>"`/`"enter:<name>"` with `fromState`/`toState` into a `Log` pointed to by `userData`; systems use `recordName`. `CHECK_INVARIANTS(world)` at the end of each case.
```cpp
TEST_CASE("the initial state is entered at step 5 of the first step; inState systems only run from the next one") { /* requestState(menu) before frame; inState(menu) system; 1 step */ CHECK(log.calls == {"enter:menu"}); CHECK(from == NoState); /* +1 step */ CHECK(log.count("menuSystem") == 1); }
TEST_CASE("state() only changes at step 5") { /* in playing; a system requests gameOver and records state() */ CHECK(stateSeenInSystem == playing); CHECK(engine.state() == gameOver); }
TEST_CASE("onExit before onEnter, each in registration order, with fromState and toState") { CHECK(log.calls == {"exit:a1", "exit:a2", "enter:b1", "enter:b2"}); }
TEST_CASE("two requests in the same step: the last one wins") { }
TEST_CASE("re-entry runs onExit and onEnter of the same state") { CHECK(log.calls == {"exit:playing", "enter:playing"}); }
TEST_CASE("a request made in a hook applies on the following step") { /* onEnter(b) requests c */ CHECK(engine.state() == b); /* +1 step */ CHECK(engine.state() == c); }
TEST_CASE("a transition in a paused step happens and does not touch the pause") { CHECK(engine.state() == b); CHECK(engine.paused()); }
TEST_CASE("inState combines with whilePaused") { /* whilePaused + inState(menu) system: runs only while paused and in menu */ }
TEST_CASE("inState applies in the render phase") { }
TEST_CASE("an empty list removes the restriction") { }
TEST_CASE("handler with inState: checks the state in each round and in the item 6 dispatch the new one applies") { /* onEnter(playing) emits Ping; inState(playing) handler */ CHECK(handlerTimes == {stepTime}); }
TEST_CASE("an event emitted in a hook is dispatched in the same step and counts toward the round limit") { /* maxEventRounds=2; 1 round at step 4 + 1 in item 6 */ CHECK(log.count("handler") == 2); }
TEST_CASE("a request with the tab hidden waits for the first step after returning") { /* setHidden(true); requestState; frames; setHidden(false); frame only records */ CHECK(engine.state() == NoState); /* 1 step */ CHECK(engine.state() == menu); }
TEST_CASE("requestState and state through the system, handler, task and hook contexts") { }
TEST_CASE("OLY_STATE_HOOK registers by name") { CHECK(findNamedStateHook("namedEnter") == &namedEnter); CHECK(findNamedStateHook("none") == nullptr); }
```
`recovery.cpp` (native only): `"recovery discards the pending request"` (a system requests `b` and throws; next `frame`; steps; `state()` is still `a`); `"exception in onExit: the state does not change"`; `"exception in onEnter: the state is already the new one and the following hooks do not run"`.

`contract_death.cpp` + `CASES`:

| Case | Action | Substrings |
|---|---|---|
| `declare_state_during_frame` | a system calls `declareState` | `declareState`, `during frame` |
| `declare_state_twice` | `declareState("a")` twice | `declareState`, `already declared` |
| `too_many_states` | 33 `declareState` | `declareState`, `maximum` |
| `request_state_out_of_range` | `requestState(5)` with 2 states | `requestState`, `out of range` |
| `on_enter_during_frame` | a system calls `onEnter` | `onEnter`, `during frame` |
| `system_states_during_frame` | a system calls `setSystemStates` | `setSystemStates`, `during frame` |
| `system_state_mask_undeclared` | `setSystemStateMask(id, 0b100)` with 2 states | `setSystemStates`, `undeclared` |

- [ ] **Step 2: See it fail** — `$DC make test T='^app\.'` → compilation error (`oly/app/state_context.hpp` does not exist).

- [ ] **Step 3: Implement.** Data in the `Engine`'s data block, after `scheduler_`: `struct StateHookEntry { StateHookFunction function; void* userData; std::string name; StateHookId id; };`, `struct StateEntry { std::string name; std::vector<StateHookEntry> enterHooks; std::vector<StateHookEntry> exitHooks; };`, `std::vector<StateEntry> states_`, `StateHookId stateHookCount_ = 0`, `StateId state_ = NoState`, `StateId requestedState_ = NoState`. Registration by name in `src/state.cpp`, like `src/system.cpp`. Update the comments that became false: the one on the empty stages in `engine.hpp` (none remain) and the one on `frame` about recovery (the pending request is also discarded). Public docs for `requestState` (works inside and outside `frame`, last one wins, transition at step 5), `onEnter`/`onExit` (order, what they can do) and `set*States` (an empty list removes the restriction; applies in every phase).

- [ ] **Step 4: See it pass** — `$DC make test` (includes `oly.lint.boundaries`) and `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: states in the Engine with transition at step 5, hooks and inState`.

---

### Task 3: Scope of entities and tasks, `persist` and cleanup

**Files:**
- Create: `engine/runtime/app/include/oly/app/detail/state_scoped.hpp`, `engine/runtime/app/tests/state_scope.cpp`
- Modify: `engine/runtime/app/include/oly/app/{engine.hpp,context.hpp,system.hpp,scheduler.hpp}`, `engine/runtime/app/src/{engine_states.cpp,context.cpp,system.cpp,scheduler.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp,recovery.cpp}`

**Interfaces:**
- Consumes: Task 1 (`push(..., scoped)`, `persist`, `cancelScoped`), Task 2 (`state()`, `applyStateTransition`).
- Produces: `oly::app::detail::StateScoped`; `void SystemContext::persist(ecs::Entity entity)`; `void DirectContext::persist(ecs::Entity entity)`; `bool Scheduler::persist(TaskHandle task)` (`false` if not pending); `DirectContext::spawn()`/`spawn(Ts&&...)` and `SystemContext::spawn()`/`spawn(Ts&&...)` with scope according to "Decisions". In the `Engine`: `ecs::QueryById* scopedQuery_ = nullptr`, `std::vector<ecs::Entity> scopedEntities_`, `void clearStateScope()` called at the cleanup spot of `applyStateTransition`. Docs on the `Engine` constructor with the limitation of two `Engine`s on one `World`.

- [ ] **Step 1: Failing tests** in `tests/state_scope.cpp` (`oly_add_test(MODULE app NAME state_scope SOURCES state_scope.cpp SUPPORT engine/runtime/ecs/tests/support)`; the module's own test can include `oly/app/detail/state_scoped.hpp`). `CHECK_INVARIANTS(world)` at the end of each case.
```cpp
TEST_CASE("entities created by system, handler, task and hook with an active state are destroyed on transition") { CHECK(!world.isAlive(fromSystem)); CHECK(!world.isAlive(fromHandler)); CHECK(!world.isAlive(fromTask)); CHECK(!world.isAlive(fromHook)); }
TEST_CASE("a direct spawn on the World is persistent") { CHECK(world.isAlive(setup)); }
TEST_CASE("without an active state no entity gets the tag") { CHECK(!world.has<StateScoped>(entity)); }
TEST_CASE("spawn with components in a direct context gets the tag in a single transition") { CHECK(world.has<StateScoped>(entity)); CHECK(world.has<Position>(entity)); }
TEST_CASE("what onExit creates is cleaned up in the same transition; what onEnter creates stays until the following transition") { /* entity and task from each hook */ CHECK(!world.isAlive(fromExit)); CHECK(world.isAlive(fromEnter)); CHECK(exitDiscards == 1); }
TEST_CASE("persist in a system in the transition step: the tag's add and remove cancel out and the entity survives") { CHECK(world.isAlive(entity)); CHECK(!world.has<StateScoped>(entity)); }
TEST_CASE("persist in a direct context removes the tag; without the tag it does nothing") { }
TEST_CASE("scoped tasks are cancelled with discard once; persist keeps them; not pending returns false") { CHECK(discards == 1); CHECK(engine.schedule().isPending(persisted)); CHECK(!engine.schedule().persist(finished)); }
TEST_CASE("a task scheduled outside frame with an active state is scoped") { }
TEST_CASE("a discard that schedules during cleanup is also cancelled") { CHECK(discards == 2); CHECK(engine.schedule().pendingCount() == 0); }
TEST_CASE("an event emitted in onExit about a scoped entity arrives with the entity already destroyed") { CHECK(handlerSawDead); }
TEST_CASE("playing → gameOver → playing with no leftovers in the World") { /* onEnter(playing) creates 3 entities and an every; transition 3 times */ CHECK(aliveEntities == 3 + persistent); CHECK(engine.schedule().pendingCount() == 1); }
```
`recovery.cpp`: `"exception in onExit does not clean up the scope"` (a scoped entity stays alive after recovery).

`contract_death.cpp` + `CASES`: `persist_dead_entity` (a task calls `context.persist` on a destroyed entity; substrings `remove`, `StateScoped`).

- [ ] **Step 2: See it fail** — `$DC make test T='^app\.'` → compilation error (`persist` and `detail/state_scoped.hpp` do not exist).

- [ ] **Step 3: Implement** according to "Decisions" (scoped spawn, `persist`, `Scheduler::schedule` with `scoped`, cleanup with `QueryById` and a reused vector, then `cancelScoped`). The `Scheduler` comment gains the scope and `persist`.

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: per-state scope of entities and tasks with persist and cleanup on transition`.

---

### Task 4: States in the schema, the generator and the `OlyEngine` creation

**Files:**
- Modify: `engine/packages/core/src/{schema.ts,schema_hash.ts,runtime.ts,module.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/{schema.test.ts,surface.test.ts}`, `engine/tools/codegen/src/emit_cpp.ts`, `engine/tools/codegen/tests/generator.test.ts`, `engine/runtime/bridge/include/oly/bridge/{schema_registry.hpp,c_api.hpp}`, `engine/runtime/bridge/src/{c_api.cpp,engine_api.cpp}`, `engine/runtime/bridge/tests/{test_schema_registry.cpp,c_api.cpp,engine_api.cpp}`

**Interfaces:**
- Consumes: Task 2 (`Engine::declareState`, `stateCount`, `StateId`).
- Produces (`@oly/core`): `interface State<Name extends string = string> { readonly kind: "state"; readonly name: Name; readonly schemaIndex: number }`; `state<const Name extends string>(name: Name): State<Name>` (`ContractError` for a name outside `/^[a-z][A-Za-z0-9]*$/` or a C++ reserved word); `Schema.states: readonly State[]`; `defineSchema` accepts `State`, assigns `schemaIndex` in order and throws `ContractError` `"defineSchema: 33 states; the maximum is 32"`; `canonicalSchema`/`schemaHash` gain `states` (`#name;`). Public exports: `state`, `State`.
- Produces (`Runtime`, internal): `stateIndexOf(runtime, state: State): number` (`ContractError` `"state=<name>: state does not belong to this runtime's schema"`), `stateAt(runtime, index: number): State | null` (`null` for `0xFFFFFFFF`). `loadRuntime` compares `_olySchemaStateCount()` with `schema.states.length` and the `SchemaMismatchError` message mentions the states.
- Produces (codegen): header with `#include "oly/app/state.hpp"` and `namespace states { inline constexpr oly::app::StateId menu{0}; }` when there are states; registry with `schemaStates()`.
- Produces (`bridge`): `struct SchemaState { std::string_view name; };` `std::span<const SchemaState> schemaStates();` (in schema order; the index is the `StateId`); `OLY_API uint32_t olySchemaStateCount();`; `olyEngineCreate` declares the states after the actions, with the same order assert.

- [ ] **Step 1: Failing tests.**
  - `schema.test.ts`: `state("menu")` with `schemaIndex` in order among the states; a name repeated with a component or action is a `ContractError`; `state("Menu")` and `state("class")` are `ContractError`; 33 states is a `ContractError` with "maximum"; the hash changes when a state is added or renamed.
  - `generator.test.ts`: header with `inline constexpr oly::app::StateId menu{0};` and `playing{1}` and the include of `oly/app/state.hpp`; a header without states does not include `oly/app/state`; registry with `{"menu"}` before `{"playing"}`; a schema without states generates `schemaStates() {\s*return {};`.
  - `surface.test.ts`: public exports with `state` and `State`.
  - `test_schema_registry.cpp` gains `schemaStates()` with `"bridgeMenu"`, `"bridgePlaying"`; `c_api.cpp`: `CHECK(olySchemaStateCount() == 2)`; `engine_api.cpp`: `TEST_CASE("olyEngineCreate declares the schema states in order")` → `CHECK(engineOf(engine).stateCount() == 2)`.

- [ ] **Step 2: See it fail** — `$DC make test T='^bridge\.'` → compilation error (`schemaStates` does not exist); `$WC make games-test` → type error (`state` does not exist).

- [ ] **Step 3: Implement.** In the codegen, `emitStates` next to `emitActions` and `emitStatesRegistry` next to `emitActionsRegistry`. Update the comment on the canonical form in `schema_hash.ts` and the one on `olyEngineCreate` in `engine_api.hpp` (declares events, actions and states).

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make games-test`.

- [ ] **Step 5: Commit** `feat: states in the schema, the generator and the OlyEngine creation`.

---

### Task 5: State C API

**Files:**
- Modify: `engine/runtime/bridge/include/oly/bridge/{engine_api.hpp,engine.hpp}`, `engine/runtime/bridge/src/{engine_api.cpp,handles.hpp}`, `engine/runtime/bridge/tests/{engine_api.cpp,contract_death.cpp,CMakeLists.txt}`

**Interfaces:**
- Consumes: Task 2 (requestState, hooks, masks, `StateContext::entering`), Task 3 (`DirectContext::spawn`/`persist`, `SystemContext::persist`, `Scheduler::persist`), Task 4 (states at creation).
- Produces (C API, `extern "C"`, `OLY_API`): `OLY_NO_STATE`, `OLY_INVALID_STATE_HOOK`, `OLY_SCRIPT_STATE_HOOK 4u`, `OLY_STATE_ENTER 0u`, `OLY_STATE_EXIT 1u` and the functions from §4.7 with the signatures from there (`olyEngineRequestState`, `olyEngineState`, `olyEngineSetSystemStates`, `olyEngineSetHandlerStates`, `olyEngineAddNativeStateHook`, `olyEngineAddScriptStateHook`, `olyDirectSpawn`, `olyContextPersist`, `olyEnginePersistTask`). `OlyEngine` gains `directContext` ("Decisions"). In the `bridge` (anonymous): `runScriptStateHook(StateContext&)` → `g_invoker(engine, OLY_SCRIPT_STATE_HOOK, context.hookId(), nullptr, context.entering() ? context.fromState() : context.toState())`. The `ScriptInvoker` comment in `engine.hpp` gains the fifth `kind`.
- New contracts: `when` outside `{0, 1}` (`"%s: invalid when=%u"`); `olyDirectSpawn` outside a script handler, task or hook (`"olyDirectSpawn: called outside a handler, task or hook"`); `olyContextPersist` without a context (`"olyContextPersist: called outside a system, handler, task or hook"`). The rest come from `app`.

- [ ] **Step 1: Failing tests** in `tests/engine_api.cpp` (the `recordingInvoker` records `kind`, `id` and `eventCount`; the test defines `OLY_STATE_HOOK(bridgeNamedEnter)`):
```cpp
TEST_CASE("olyEngineState starts at OLY_NO_STATE and changes at step 5") { }
TEST_CASE("script hook: OLY_SCRIPT_STATE_HOOK with the hookId and the other state") { /* onEnter(playing) and onExit(menu); menu → playing */ CHECK(g_kinds == {OLY_SCRIPT_STATE_HOOK, OLY_SCRIPT_STATE_HOOK}); CHECK(g_eventCounts == {playing, menu}); }
TEST_CASE("native hook by name and OLY_INVALID_STATE_HOOK for an unknown name") { }
TEST_CASE("state mask on system and handler; 0 removes the restriction") { }
TEST_CASE("olyDirectSpawn in a script handler is scoped and goes away on transition") { /* the invoker calls olyDirectSpawn when kind == OLY_SCRIPT_HANDLER */ CHECK(!olyEntityIsAlive(world, created)); }
TEST_CASE("olyContextPersist in a script system (deferred) and in a script handler (direct)") { CHECK(olyEntityIsAlive(world, persistedInSystem)); CHECK(olyEntityIsAlive(world, persistedInHandler)); }
TEST_CASE("olyEnginePersistTask keeps the script task across the transition and returns 0 for a non-pending task") { }
```
`contract_death.cpp` + `CASES`: `direct_spawn_outside_context` (`olyDirectSpawn` outside frame; `olyDirectSpawn`, `outside`), `context_persist_outside_context` (`olyContextPersist` outside frame; `olyContextPersist`, `outside`), `state_hook_invalid_when` (`olyEngineAddScriptStateHook(engine, 2, 0, "x")`; `when`, `invalid`), `request_state_out_of_range_c` (`olyEngineRequestState(engine, 9)`; `requestState`, `out of range`).

- [ ] **Step 2: See it fail** — `$DC make test T='^bridge\.'` → compilation error.

- [ ] **Step 3: Implement** the functions as forwarding to the `Engine` (mask through `setSystemStateMask`/`setHandlerStateMask`; script hooks with `userData = engine`; native hooks through `findNamedStateHook`).

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: states in the C API with script hooks, direct spawn and persist`.

---

### Task 6: States in `@oly/core`, WASM test and documentation

**Files:**
- Create: `engine/packages/core/src/{state_context.ts,state_mask.ts}`, `engine/packages/core/tests/unit/state_mask.test.ts`, `engine/testing/tests/wasm/states.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,context.ts,direct_context.ts,system_context.ts,scheduler.ts,world.ts,bridge.ts,module.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/{schema.ts,systems.cpp}`, `engine/testing/tests/wasm/fixture.ts`, `CLAUDE.md`

**Interfaces:**
- Consumes: C API from Task 5; `State`, `stateIndexOf`, `stateAt` from Task 4.
- Produces (`@oly/core`):
  - `state_mask.ts` (internal): `stateMaskOf(indices: readonly number[]): number` → OR of `1 << index`, ending in `>>> 0`.
  - `Engine`: `requestState(state: State): void` (works inside and outside `frame`); `get state(): State | null`; `onEnter(state: State, name: string, hook: StateHookFunction): void`; `onExit(...)`; `addNativeOnEnter(state: State, name: string): void`; `addNativeOnExit(...)` (`ContractError` if there is no `OLY_STATE_HOOK` with that name). `SystemRegistration` and `HandlerRegistration` gain `inState(...states: State[]): this` (an empty list removes the restriction). Registration and `inState` throw `ContractError` with the `Engine` released, during `frame` and after an interrupted `frame`, like `addSystem`. The `olyInvokeScript` dispatch handles `kind` 4: `sync()`, builds `fromState`/`toState` from the hook's side and the `eventCount`, activates the context, unlocks the `World` structure and, in the `finally`, deactivates and locks.
  - `EngineContext`: `requestState(state: State): void`, `get state(): State | null`.
  - `SystemContext.persist(entity: Entity): void` (deferred, without checking liveness); `DirectContext.persist(entity: Entity): void` (`ContractError` `"persist entity=<e>: dead entity"`); `DirectContext.spawn` goes through `spawnWith` with `bridge.directSpawn`.
  - `state_context.ts`: `type StateHookFunction = (context: StateContext) => void;` `class StateContext extends DirectContext` with `get fromState(): State | null` and `get toState(): State | null`; `ContextKind { label: "hook", className: "StateContext", callNoun: "hook" }`.
  - `Scheduler.persist(handle: TaskHandle): boolean` (same validations as `cancel`).
  - `Bridge`: `engineRequestState(engine, state)`, `engineState(engine): number`, `engineSetSystemStates(engine, systemId, mask)`, `engineSetHandlerStates(engine, handlerId, mask)`, `engineAddNativeStateHook(engine, when, state, name): number` and `engineAddScriptStateHook(...)` (end with `sync()`), `directSpawn(engine): Entity` (output at `scratch + OutputOffset`, ends with `sync()`), `contextPersist(engine, entity)` (ends with `sync()`), `enginePersistTask(engine, slot, generation): boolean`. `OlyModule` gains the corresponding exports.
  - `public-exports.txt`: + `StateContext`, `StateHookFunction`.
- Fixture: `schema.ts` gains `state("menu")`, `state("playing")`, `state("gameOver")` (also exported by `fixture.ts`); `systems.cpp` gains `OLY_STATE_HOOK(appendEight)` (`query<Trace>().each`: `value * 10 + 8`).

- [ ] **Step 1: Failing tests.**
  - `state_mask.test.ts`: `stateMaskOf([]) === 0`; `stateMaskOf([0, 2]) === 0b101`; `stateMaskOf([31]) === 0x80000000`.
  - `engine/testing/tests/wasm/states.test.ts` (helpers `withEngine` and `stepper` as in `schedule.test.ts`; `checkInvariants() === null` at the end of each case):
```ts
test("requestState, engine.state and context.state; the last request wins", ...)
test("C++ and TS hooks interleaved in registration order", ...)                   // onEnter(playing): TS 1, addNativeOnEnter appendEight, TS 2 → Trace 182
test("fromState and toState in onExit and onEnter; null for the empty state", ...)
test("inState from TS on a native system, a TS system and a handler", ...)
test("direct spawn from TS in a hook and a handler is scoped; World.spawn outside a context is persistent", ...)
test("persist in a TS system, in a direct context and in a task context", ...)
test("closure map empty after the transition", ...)                              // pending scoped every and after; transition → scriptTaskCount === 0
test("exception in a TS onExit: the state does not change and the cleanup does not run; the next frame recovers", ...)
test("exception in a TS onEnter: new state and the following hooks do not run", ...)
test("memory grows inside the TS hook and the context stays valid", ...)       // like the handler test in events.test.ts
test("StateContext outside the hook call is a ContractError", ...)
test("errors: state from another schema, onEnter and inState during frame and after an interrupted frame, persist of a dead entity, unknown addNativeOnEnter", ...)
```
  - `surface.test.ts`: `Engine` gains `requestState`, `state`, `onEnter`, `onExit`, `addNativeOnEnter`, `addNativeOnExit`; `SystemContext`, `HandlerContext`, `TaskContext` and `StateContext` gain `requestState`, `state`, `persist`; `StateContext` has `fromState` and `toState`; `Scheduler` gains `persist`.

- [ ] **Step 2: See it fail** — `$WC make games-test` → type error in `tsc` (`StateContext` does not exist).

- [ ] **Step 3: Implement.** The `Engine` keeps `Map<number, { function, context: StateContext, entering: boolean }>` by `hookId`; state registration uses `stateIndexOf`; `engine.state` and `context.state` use `stateAt(engineState(...))`.

- [ ] **Step 4: `CLAUDE.md`** — a short new section "Engine: states" (declaration and order, request with the last one winning, transition at step 5 paused or not with `onExit` → cleanup → `onEnter` → dispatch, a request in a hook applies on the following step, `inState` by mask in every phase, scope through the `detail::StateScoped` tag and the task field, direct spawn on the `World` is persistent, `persist`, recovery discards the request, two `Engine`s on one `World` share the tag); in the bridge, `OLY_SCRIPT_STATE_HOOK` in the description of `olyHostInvokeScript` and `olyDirectSpawn`/`olyContextPersist`; glossary rows for declaring/requesting/reading a state, state hook, restricting to states and persisting (and `olyDirectSpawn` in the create-entity row); `StateContext` and `State` in the `@oly/core` list; the `app` summary in the tree gains "states".

- [ ] **Step 5: Final verification** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (no asserts: only the TS validations catch the errors): all green.

- [ ] **Step 6: Commit** `feat: states in @oly/core with TS hooks, inState and documentation`.
