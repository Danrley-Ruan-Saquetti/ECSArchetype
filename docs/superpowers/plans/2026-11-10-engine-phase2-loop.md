# Engine — Phase 2 (Loop and phases) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Engine loop in the C++ core (`frame`, `setHidden`, fixed step, render, pause, `.whilePaused()`), C++ systems (by function and by name) and TS systems (single callback) in the same phases, structural changes deferred by the phase's `CommandBuffer`, browser host in `@oly/client`, and the §7 measurements.

**Architecture:** A new C++ module, `app` (`engine/runtime/app/`, above `ecs`), holds the class `oly::app::Engine`: it keeps a reference to the `World`, the systems per phase, the clock, the fixed-step accumulator, the pause and a `CommandBuffer` applied at the end of each phase. `app` does not know TS exists: a system is a function pointer plus `userData`. `bridge` now depends on `app` and exposes the engine in the C API (`OlyEngine`, `olyEngine*`, `olySystem*`); to the core, a TS system is a C++ function in `bridge` that calls the single imported JS function (`olyHostInvokeScript` → `Module.olyInvokeScript`). In TS, `Runtime.createEngine` returns a thin `Engine` that forwards everything to the C API and dispatches the callback by id. `@oly/client` gains `runInBrowser` (rAF + `visibilitychange`).

**Tech Stack:** C++20, GCC 14 (native), Emscripten 3.1.74 + node 20.18 (`wasm` service), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (phase 2 of §5; all of §4.3, §6, §7 "Loop cost"). `§N` below refers to that spec.

## Global Constraints

- Branch `feat/engine-phase2-loop` from `develop`. Commits, comments, assert and error messages in English; commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain only in Docker: `DC="docker compose run --rm dev"` (native, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). Presets `wasm-games*` require `make ts` first.
- `-Wall -Wextra -Wpedantic -Werror` on every C++ target; TS `strict` (§3).
- Layers: `core ← ecs ← app ← bridge ← games`. `app` uses only the public ECS API; the ECS does not change in this phase (if it does, rerun `ecs_bench_*` and check NF-P1/P2/P5 as in `docs/benchmarks/README.md`).
- Complete engine in C++ (§2, anti-goal): every loop rule (steps, pause, hidden tab, order, `whilePaused`, recovery) lives in `oly::app::Engine`. TS only validates usage and forwards.
- No game concept in `engine/runtime` or in `engine/packages`; game names only in `engine/testing/fixture/`.
- Boundary: one C++→JS call per TS system per step; never per entity (§2).
- The `CommandBuffer` does not appear under that name in the engine API (§4.5): systems use `context.spawn/destroy/add/remove`, which are deferred.
- Glossary (`CLAUDE.md`): the same name in all three layers — `Engine` (C++ `oly::app::Engine`, C `OlyEngine`/`olyEngine*`, TS `Engine`), `frame`, `setHidden`, `pause`, `resume`, `addSystem`, `addNativeSystem`, `whilePaused`. `oly` only in the C API.
- `CLAUDE.md` style: braces on every block, full names, data at the top of the class, one `namespace` per file, project includes with quotes after the standard library. Do not format by hand (`clang-format` hook).

### Decisions the spec leaves open (fixed here)

- **Names:** class `oly::app::Engine` (not `App`), to match the TS `Engine` and the C API `OlyEngine`. Config `EngineConfig { double fixedHz = 60.0; uint32_t maxStepsPerFrame = 5; }` (C++ and TS).
- **`Engine` does not own the `World`:** `Engine(ecs::World& world, EngineConfig config = {})`. The C API `OlyEngine` holds an `OlyWorld` and the `Engine` over it; `olyEngineWorld` returns that `OlyWorld*`, and TS reuses `World`/`Query` over it.
- **One `CommandBuffer` per `Engine`**, applied (`world.flush`) at the end of each phase, including `render`. It is equivalent to one per phase and avoids four buffers.
- **Clock:** `time()` = number of completed non-paused steps × `fixedDeltaTime` (in `double`, no running sum). During a step, `time()` is the time at the start of the step.
- **Accumulator tolerance:** runs a step while `accumulator >= fixedDeltaTime * (1 - 1e-9)`; after subtracting, a negative value becomes 0. This way `frame(k/60)` runs exactly k steps.
- **Excess:** after `maxStepsPerFrame` steps, if one more step still fits, the accumulator keeps only the fraction (`fmod(accumulator, fixedDeltaTime)`).
- **First `frame`** (and the first one after `setHidden(false)` or after a recovery) only records the time: it runs no step and no `render`.
- **`paused()`** returns the most recent request; the effect (`pausedThisStep_`) is applied at the start of each fixed step.
- **`.whilePaused()` on a `render` system** is a contract error (`OLY_ASSERT` in C++, `ContractError` in TS).
- **Registering a system during a `frame`** is a contract error. So is `frame` with a time lower than the previous one.
- **Steps 1, 2, 4, 5 of §4.3** become empty private functions of `Engine` (`captureActions`, `runDueSchedule`, `dispatchEvents`, `applyStateTransition`), called in the spec's order; `runDueSchedule` is only called when not paused.
- **Exception in a TS system:** JS unwinds the WASM without destructors and `inFrame_` stays `true`. The next `frame` sees this and **recovers**: it discards the pending commands (`commands_ = world.commands()`, which frees the reserved indices), zeroes the accumulator and behaves as a first frame. What the systems already applied (completed phases, written values) stays. In TS, `Engine.frame` uses `try/finally` to unlock the `World` and invalidate contexts.
- **TS entry point in C++:** `EM_JS` function `olyHostInvokeScript(OlyEngine*, uint32_t systemId)` that calls `Module["olyInvokeScript"](engine, systemId)`. The `Runtime` installs `module.olyInvokeScript` once and dispatches to the TS `Engine` by handle (several engines per module). In native, `oly::bridge::setScriptInvoker` swaps the invoker (tests); the native default fires `OLY_ASSERT`.
- **TS `SystemContext`:** one object per system, valid only during that system's call (outside it, `ContractError`). Before calling the system, the TS `Engine` does `bridge.sync()` (an earlier C++ system may have grown the memory).
- **Direct structural change on the TS `World` during `frame`** is `ContractError` ("use context.spawn/destroy/add/remove"). `Engine.world.dispose()` is `ContractError` (the `World` belongs to the `Engine`).
- **Out of scope for this phase:** the TS per-`Entity` storage (§4.1), which phase 1 deferred to here. Cleaning it up needs to know about destructions done by C++, and that only makes sense together with events or states; it moves to phase 6 (state scope) or 7. `single(...)` and `inState` also move to phases 6–7.
- **Browser host:** `runInBrowser(target, environment?)` requests the next rAF **before** calling `frame`; an exception in `frame` propagates to the browser (shows up in the console) and the loop continues.

## Review Focus

1. **Clock at exact multiples of `1/60`** (`frame(k/60)` for k = 0…120): exactly 120 steps, none lost or duplicated by floating-point error (Task 1).
2. **WASM memory grows in a C++ system before a TS system** (flush of many `spawn`): the next TS system reads and writes the right memory (Task 4).
3. **Exception inside `eachBlock` in a TS system:** reaches the caller of `frame`; the `World` accepts structural changes afterwards, `checkInvariants` clean, the deferred entity of the interrupted phase does not exist, and the following frames run steps again (Task 5).
4. **Tab comes back after a long time / `frame` with a big jump** (10 s): at most `maxStepsPerFrame` steps, excess discarded, no spiral (Task 1).
5. **`pause()` called by a system mid-step:** the following systems in the same step still run; the next step is already paused (Task 2).

---

### Task 1: `app` module — phases, fixed step, render and deferred changes

**Files:**
- Create: `engine/runtime/app/CMakeLists.txt`, `engine/runtime/app/include/oly/app/{phase.hpp,system.hpp,engine.hpp}`, `engine/runtime/app/src/{engine.cpp,system.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,support/log.hpp,loop.cpp,systems.cpp}`
- Modify: `engine/runtime/CMakeLists.txt` (`add_subdirectory(app)` between `ecs` and `bridge`)

**Interfaces:**
- Produces (namespace `oly::app`):
  - `phase.hpp`: `enum class Phase : uint8_t { UpdateBefore, Update, UpdateAfter, Render };` `inline constexpr uint32_t PhaseCount = 4;` `constexpr bool isGameplayPhase(Phase phase) noexcept;`
  - `system.hpp`: `using SystemId = uint32_t;` `class SystemContext;` `using SystemFunction = void (*)(SystemContext& context);`
    `SystemContext`: `Phase phase() const`, `SystemId systemId() const`, `std::string_view name() const`, `void* userData() const`, `float fixedDeltaTime() const`, `double time() const`, `float interpolation() const` (fraction of the step, the same for the whole frame), `void pause()`, `void resume()`, `bool paused() const`; reads: `template <class... Ts, class... Filters> ecs::Query<Ts...>& query(Filters...)`, `get<T>(Entity)`, `tryGet<T>(Entity)`, `has<T>(Entity)`, `isAlive(Entity)`; deferred: `Entity spawn()`, `template <class... Ts> Entity spawn(Ts&&... components)`, `destroy(Entity)`, `template <class T> add(Entity, T&&)`, `template <class T> remove(Entity)`, `addById(Entity, ComponentId, void* value)`, `removeById(Entity, ComponentId)`.
    Registration by name: `SystemFunction findNamedSystem(std::string_view name) noexcept` (nullptr if it does not exist); `struct NamedSystemRegistration { NamedSystemRegistration(std::string_view name, SystemFunction function); };` and the macro `OLY_SYSTEM(name)` (declares `void name(oly::app::SystemContext& context)` and a static registration with the name `#name`).
  - `engine.hpp`: `struct EngineConfig { double fixedHz = 60.0; uint32_t maxStepsPerFrame = 5; };` `class Engine` with `Engine(ecs::World& world, EngineConfig config = {})`, `ecs::World& world()`, `SystemId addSystem(Phase phase, std::string_view name, SystemFunction function, void* userData = nullptr)`, `void setWhilePaused(SystemId system, bool whilePaused)`, `void frame(double nowSeconds)`, `void setHidden(bool hidden)`, `void pause() noexcept`, `void resume() noexcept`, `bool paused() const noexcept`, `double time() const noexcept`, `double fixedDeltaTime() const noexcept`, `double interpolation() const noexcept`, `bool inFrame() const noexcept`.

- [ ] **Step 1: Branch** `git switch -c feat/engine-phase2-loop`.

- [ ] **Step 2: Failing tests.** `oly_add_module(NAME app DEPENDS ecs core)`; `tests/CMakeLists.txt` with `oly_add_test(MODULE app NAME loop SOURCES loop.cpp SUPPORT engine/runtime/ecs/tests/support)` and the same for `systems`. `support/log.hpp`: `struct Log { std::vector<std::string> calls; std::vector<float> interpolations; std::vector<double> times; };` and `void recordName(SystemContext& context)` (pushes `context.name()` into the `Log*` from `userData`; in `Render` it also pushes `interpolation()`; in the others, `time()`). Every test calls `checkInvariants(world)` at the end.

`loop.cpp`:
```cpp
TEST_CASE("first frame only records the time") { /* Update "u", Render "r"; frame(10.0) */ CHECK(log.calls.empty()); /* frame(10.0 + 1.0/60) */ CHECK(log.calls == {"u","r"}); }
TEST_CASE("frame at exact multiples of 1/60 runs one step per frame") { /* frame(k / 60.0), k = 0..120 */ CHECK(count("u") == 120); CHECK(count("r") == 120); }
TEST_CASE("fractions accumulate across frames") { /* frame(0); frame(0.5/60) */ CHECK(count("u") == 0); CHECK(count("r") == 1); /* frame(1.0/60) */ CHECK(count("u") == 1); }
TEST_CASE("steps-per-frame limit discards the excess") { /* maxStepsPerFrame = 5; frame(0); frame(10.0) */ CHECK(count("u") == 5); /* frame(10.0 + 1.0/60) */ CHECK(count("u") == 6); }
TEST_CASE("render receives the interpolation") { /* frame(0); frame(1.5/60) */ CHECK(log.interpolations.back() == doctest::Approx(0.5).epsilon(1e-6)); }
TEST_CASE("time is the start of the step and fixedDeltaTime comes from fixedHz") { /* fixedHz = 50; frame(0); frame(0.06) */ CHECK(log.times == {0.0, 0.02, 0.04}); /* context: fixedDeltaTime() == 0.02f */ CHECK(engine.time() == doctest::Approx(0.06)); }
```

`systems.cpp`:
```cpp
TEST_CASE("phases in order and, within a phase, registration order") { /* registers Render r, Update u1, UpdateBefore b, UpdateAfter a, Update u2; one step */ CHECK(log.calls == {"b","u1","u2","a","r"}); }
OLY_SYSTEM(appTestNamed) { recordName(context); }
TEST_CASE("OLY_SYSTEM registers by name") { CHECK(findNamedSystem("appTestNamed") != nullptr); CHECK(findNamedSystem("doesNotExist") == nullptr); }
TEST_CASE("deferred spawn: alive only in the next phase") { /* Update: e = context.spawn(Position{1, 2}); CHECK(!context.isAlive(e)); UpdateAfter: CHECK(context.isAlive(e)); CHECK(context.get<Position>(e).x == 1) */ }
TEST_CASE("render commands applied at the end of the frame") { /* Render: e = context.spawn(); after the frame */ CHECK(world.isAlive(e)); }
TEST_CASE("deferred destroy and add in the same phase resolve like the CommandBuffer") { /* Update: add<Velocity>(e) and destroy(e) */ CHECK(!world.isAlive(e)); }
```
`Position`/`Velocity` come from the ECS support `components.hpp` (or are defined in the test, if not there).

- [ ] **Step 3: See it fail** — `$DC make test T='^app\.'` → compile error (headers do not exist).

- [ ] **Step 4: Implement.** `Engine` holds `std::array<std::vector<SystemEntry>, PhaseCount>` with `SystemEntry { SystemFunction function; void* userData; std::string name; SystemId id; bool whilePaused; }` and an index `SystemId → (phase, position)`. `frame` follows the "Decisions" section (tolerance, excess, first frame). One fixed step: applies the requested pause; `captureActions()`; `runDueSchedule()` if not paused; `UpdateBefore`, `Update`, `UpdateAfter` (each runs the systems with `whilePaused == paused` and then `world.flush(commands_)`); counts the step on the clock if not paused; `dispatchEvents()`; `applyStateTransition()`. After the steps, `interpolation_ = accumulator / dt` and the `Render` phase (all systems, then flush). `SystemContext` is built on the stack for each call. `findNamedSystem` uses a static `std::vector<std::pair<std::string, SystemFunction>>` inside a function (safe static initialization order). In this task, `pause`/`setHidden` may exist only as state; their tests belong to Task 2.

- [ ] **Step 5: See it pass** — `$DC make test` (everything, including `oly.lint.boundaries`) and `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 6: Commit** `feat: app module with fixed-step loop, phases and deferred changes`.

---

### Task 2: Pause, hidden tab, `.whilePaused()` and recovery

**Files:**
- Modify: `engine/runtime/app/src/engine.cpp`, `engine/runtime/app/tests/CMakeLists.txt`
- Create: `engine/runtime/app/tests/{pause.cpp,recovery.cpp,contract_death.cpp}`

**Interfaces:**
- Consumes: everything from Task 1.
- Produces: behaviour of `pause`/`resume`/`setWhilePaused`/`setHidden` and the recovery from an interrupted frame, described in "Decisions".

- [ ] **Step 1: Failing tests.** `pause.cpp` (Update `u`, Update `p` with `setWhilePaused(p, true)`, Render `r`, one step per frame):
```cpp
TEST_CASE("paused: only whilePaused runs, clock freezes, render continues") { /* pause(); 2 frames */ CHECK(log.calls == {"p","r","p","r"}); CHECK(engine.time() == 0.0); /* resume(); 1 frame */ CHECK(last == {"u","r"}); }
TEST_CASE("pause requested by a system applies on the next step") { /* Update "pauser" calls context.pause(); UpdateAfter "a"; 1 step */ CHECK(calls == {"pauser","a"}); CHECK(engine.paused()); /* 1 step */ CHECK(none of "pauser","a"); }
TEST_CASE("whilePaused system resumes through the context") { /* p calls context.resume(); pause(); 2 steps */ CHECK(calls == {"p","u"}); }
TEST_CASE("hidden tab ignores frames and the first one after only records the time") { /* frame(0); setHidden(true); frame(1.0) */ CHECK(empty); /* setHidden(false); frame(2.0) */ CHECK(empty); /* frame(2.0 + 1.0/60) */ CHECK(calls == {"u","r"}); }
TEST_CASE("hidden tab does not undo the game pause") { /* pause(); setHidden(true); setHidden(false) */ CHECK(engine.paused()); }
```
`recovery.cpp` (native only: `#if !defined(__EMSCRIPTEN__)`, because the C++ exception crosses the `Engine` compiled without `-fwasm-exceptions`; the real case, a JS exception, is tested in Task 5):
```cpp
TEST_CASE("frame interrupted by an exception recovers") {
  // Update: e = context.spawn(); throws std::runtime_error on the 1st call
  // frame(0); CHECK_THROWS(engine.frame(1.0/60)); CHECK(!world.isAlive(e)); checkInvariants
  // frame(2.0/60) runs no step; frame(3.0/60) runs 1 step; checkInvariants
}
```
`contract_death.cpp` (same protocol as `engine/runtime/bridge/tests/contract_death.cpp`), registered with `oly_add_contract_death(MODULE app SOURCE contract_death.cpp CASES ...)`:

| Case | Action | Substrings |
|---|---|---|
| `add_system_during_frame` | a system calls `engine.addSystem` (engine via `userData`) | `addSystem`, `during frame` |
| `while_paused_render` | `setWhilePaused` on a `Render` system | `whilePaused`, `render` |
| `frame_time_backwards` | `frame(1.0)` and then `frame(0.5)` | `frame`, `time` |

- [ ] **Step 2: See it fail** — `$DC make test T='^app\.'` → failures in `pause`/`recovery`, death cases "assert did not fire".

- [ ] **Step 3: Implement** the effective pause per step, `setHidden` (while hidden, `frame` returns immediately; on show, the next frame only records the time), the recovery at the start of `frame` when `inFrame_` is already `true`, and the three `OLY_ASSERT` with the system name when there is one.

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: pause, hidden tab, whilePaused and recovery from interrupted frame`.

---

### Task 3: Engine in the C API (`OlyEngine`) and callback for scripts

**Files:**
- Create: `engine/runtime/bridge/include/oly/bridge/{engine_api.hpp,engine.hpp}`, `engine/runtime/bridge/src/{handles.hpp,engine_api.cpp}`, `engine/runtime/bridge/tests/engine_api.cpp`
- Modify: `engine/runtime/bridge/CMakeLists.txt` (`DEPENDS app ecs core`), `engine/runtime/bridge/src/c_api.cpp` (move `struct OlyWorld`/`OlyIteration` to `src/handles.hpp`), `engine/runtime/bridge/tests/{CMakeLists.txt,contract_death.cpp}`

**Interfaces:**
- Consumes: `oly::app::Engine`, `SystemContext`, `findNamedSystem`, `Phase`, `PhaseCount`.
- Produces:
  - `engine_api.hpp` (`extern "C"`, `OLY_API`): `struct OlyEngine;` `#define OLY_INVALID_SYSTEM 0xFFFFFFFFu`;
    `OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame)`, `void olyEngineDestroy(OlyEngine*)`, `OlyWorld* olyEngineWorld(OlyEngine*)`,
    `void olyEngineFrame(OlyEngine*, double nowSeconds)`, `void olyEngineSetHidden(OlyEngine*, int32_t hidden)`, `void olyEnginePause(OlyEngine*)`, `void olyEngineResume(OlyEngine*)`, `int32_t olyEngineIsPaused(OlyEngine*)`,
    `double olyEngineTime(OlyEngine*)`, `double olyEngineFixedDeltaTime(OlyEngine*)`, `double olyEngineInterpolation(OlyEngine*)`,
    `uint32_t olyEngineAddNativeSystem(OlyEngine*, uint32_t phase, const char* name)` (`OLY_INVALID_SYSTEM` if the name does not exist), `uint32_t olyEngineAddScriptSystem(OlyEngine*, uint32_t phase, const char* name)`, `void olyEngineSetWhilePaused(OlyEngine*, uint32_t systemId, int32_t whilePaused)`,
    `void olySystemSpawn(OlyEngine*, OlyEntity* out)`, `void olySystemDestroy(OlyEngine*, uint32_t index, uint32_t generation)`, `void olySystemAdd(OlyEngine*, uint32_t index, uint32_t generation, uint32_t componentId, void* value)`, `void olySystemRemove(OlyEngine*, uint32_t index, uint32_t generation, uint32_t componentId)`.
  - `engine.hpp` (C++): `oly::app::Engine& engineOf(OlyEngine*) noexcept;` `using ScriptInvoker = void (*)(OlyEngine* engine, uint32_t systemId);` `ScriptInvoker setScriptInvoker(ScriptInvoker invoker) noexcept;` (returns the previous one).
  - JS import (Emscripten only): `EM_JS(void, olyHostInvokeScript, (OlyEngine* engine, uint32_t systemId), { Module["olyInvokeScript"](engine, systemId); })` — default invoker in WASM.

- [ ] **Step 1: Failing tests** in `tests/engine_api.cpp` (native and `wasm-debug`; register with `oly_add_test(MODULE bridge NAME engine_api SOURCES engine_api.cpp test_schema_registry.cpp SUPPORT engine/runtime/ecs/tests/support)`; a test invoker records `(engine, systemId)` in a static vector and can run an action):
```cpp
TEST_CASE("script system is called by the invoker, in order with the native ones") { /* script "s" Update; OLY_SYSTEM(bridgeTestNative) records "n"; order s, n, s → one step */ CHECK(recorded == {"s","n","s"}); CHECK(ids match the ones returned by olyEngineAddScriptSystem); }
TEST_CASE("unknown native name returns OLY_INVALID_SYSTEM") { CHECK(olyEngineAddNativeSystem(engine, 1, "doesNotExist") == OLY_INVALID_SYSTEM); }
TEST_CASE("olySystemSpawn and olySystemAdd inside the script are applied at the end of the phase") { /* invoker: olySystemSpawn + olySystemAdd(A{7}); after the frame */ CHECK(olyEntityIsAlive(...)); CHECK(get A == 7); invariants }
TEST_CASE("two engines in the same process: the invoker receives the right engine") { /* frame only on the 2nd */ CHECK(every record with engine == second); }
TEST_CASE("olyEngineWorld works with the existing World API") { /* olyEntitySpawn(olyEngineWorld(engine)) */ CHECK(olyEntityIsAlive(...)); }
```
`contract_death.cpp`: cases `system_spawn_outside_system` (`olySystemSpawn` without a frame; substrings `olySystemSpawn`, `outside system`) and `engine_phase_out_of_range` (`olyEngineAddScriptSystem(engine, 4, "x")`; `phase`).

- [ ] **Step 2: See it fail** — `$DC make test T='^bridge\.'` → compile error.

- [ ] **Step 3: Implement.** `struct OlyEngine { OlyWorld world; oly::app::Engine engine; oly::app::SystemContext* currentContext = nullptr; std::vector<std::unique_ptr<ScriptSystem>> scripts; };` (in this order: `engine` references `world.world`). A script system is registered with a bridge function and `userData` = `OlyEngine*`; it stores `currentContext`, calls the invoker with `context.systemId()` and clears `currentContext`. `olyEngineFrame` zeroes `currentContext` before calling `frame` (after a JS exception it was left pointing at the old stack). `olySystem*` require `currentContext != nullptr` and delegate to the context's `spawn`/`destroy`/`addById`/`removeById`. `phase < PhaseCount` is an `OLY_ASSERT`.

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: Engine in the C API with native systems by name and callback for scripts`.

---

### Task 4: `Engine` and `SystemContext` in `@oly/core`

**Files:**
- Create: `engine/packages/core/src/{engine.ts,system_context.ts}`, `engine/testing/tests/wasm/engine.test.ts`
- Modify: `engine/packages/core/src/{module.ts,bridge.ts,runtime.ts,world.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/{schema.ts,systems.cpp}`, `engine/testing/tests/wasm/fixture.ts`, `engine/testing/CMakeLists.txt` (`DEPENDS ecs app bridge`)

**Interfaces:**
- Consumes: the C API from Task 3.
- Produces (`@oly/core`):
  - `export const Phase = { UpdateBefore: 0, Update: 1, UpdateAfter: 2, Render: 3 } as const; export type Phase = (typeof Phase)[keyof typeof Phase];`
  - `interface EngineConfig { fixedHz?: number; maxStepsPerFrame?: number }`; `Runtime.createEngine(config?: EngineConfig): Engine`.
  - `class Engine`: `readonly world: World`, `frame(nowSeconds: number): void`, `setHidden(hidden: boolean): void`, `pause(): void`, `resume(): void`, `get paused(): boolean`, `get time(): number`, `get fixedDeltaTime(): number`, `addSystem(phase: Phase, name: string, system: SystemFunction): SystemRegistration`, `addNativeSystem(phase: Phase, name: string): SystemRegistration`, `dispose(): void`.
  - `type SystemFunction = (context: SystemContext) => void`; `interface SystemRegistration { readonly id: number; whilePaused(): SystemRegistration }`.
  - `class SystemContext`: `readonly phase: Phase`, `readonly name: string`, `get fixedDeltaTime()`, `get time()`, `get interpolation()`, `get paused()`, `pause()`, `resume()`, `query(spec)`, `get`, `set`, `has`, `isAlive` (delegate to the engine's `World`), and deferred `spawn(types, values?) → Entity`, `destroy(entity)`, `add(entity, type, value?)`, `remove(entity, type)` (same validations and messages as `World.spawn/add`).
  - `OlyModule`: the `_olyEngine*`/`_olySystem*` exports and `olyInvokeScript?: (engine: number, systemId: number) => void`.
  - Internal (`world.ts`, not exported in `internal.ts`): `setStructureLocked(world, locked: boolean)`, and `World` created by the engine with `owned = false`.
- Fixture: `Trace = component("Trace", { value: "f64" })` in the schema; in `systems.cpp`, `OLY_SYSTEM(noop) {}`, `OLY_SYSTEM(move)` (the `move` from §4.8, with `context.fixedDeltaTime()`), `OLY_SYSTEM(appendNine)` (`trace.value = trace.value * 10 + 9` on every entity with `Trace`) and `OLY_SYSTEM(spawnBatch)` (`context.spawn(Position{1, 0})` 400 000 times; large enough for the heap to grow).

- [ ] **Step 1: Failing tests** in `engine/testing/tests/wasm/engine.test.ts` (`fixedHz: 60`; helper `step(engine)` that calls `frame` with the next multiple of `1/60`; `checkInvariants() === null` at the end of each test):
```ts
test("TS and C++ systems in phase and registration order", ...)   // Trace {0}; UpdateBefore TS ×10+1, Update TS ×10+2, Update native appendNine, Update TS ×10+3; 1 step → trace.value === 1293
test("frame(k/60) for k = 0..60 runs 60 steps", ...)
test("context.spawn is deferred and C++ sees the value", ...)           // Update: e = context.spawn([Position], {Position:{x:5}}); isAlive(e) === false; UpdateAfter: context.get(e, Position).x === 5; then fixtureSumPositionX(world.handle) === 5
test("memory grows in a C++ system before a TS system", ...) // Update native spawnBatch; UpdateAfter TS sums x via eachBlock → 400000; HEAPU8.buffer changed during the frame
test("direct structural change on the World during frame is ContractError", ...) // inside the system: engine.world.spawn throws ContractError with "context.spawn"; outside the frame it works
test("context used outside the call is ContractError", ...)
test("addNativeSystem with unknown name is ContractError with the name", ...)
test("addSystem during frame is ContractError", ...)
test("two engines in the same module receive only their own systems", ...)
test("engine.world.dispose() is ContractError; frame after engine.dispose() is ContractError", ...)
```
Update `surface.test.ts`: `Runtime` = `["createEngine", "createWorld", "schema"]`; new test with the members of `Engine` and `SystemContext` exactly as in Produces; `public-exports.txt` gains `Engine`, `EngineConfig`, `Phase`, `SystemContext`, `SystemFunction`, `SystemRegistration`.

- [ ] **Step 2: See it fail** — `$WC make games-test` → type error in `tsc` (`createEngine` does not exist).

- [ ] **Step 3: Implement.** `Bridge` gains one method per new export (the ones that may allocate — `engineCreate`, `engineFrame`, `addNativeSystem`, `addScriptSystem`, `systemSpawn`, `systemAdd` — end in `sync()`) and `#writeString(text)` that writes UTF-8 + NUL into a growing scratch buffer. The `Runtime` installs `module.olyInvokeScript` in the constructor and keeps a `Map<number, Engine>`; the call goes to the `Engine`'s internal `invoke(systemId)`, which does `bridge.sync()`, activates the system's `SystemContext` (array indexed by `systemId`), calls the function and deactivates it in `finally`. `Engine.frame` validates (`Number.isFinite`, time does not go back, not reentrant, not disposed), locks the `World` and calls `olyEngineFrame` inside `try/finally`. `addSystem`/`addNativeSystem` inside `frame` are `ContractError`.

- [ ] **Step 4: See it pass** — `$WC make games-test` (includes `ts.core.unit`, `ts.testing.wasm`, `bridge.*`, `app.*`) and `$DC make test`.

- [ ] **Step 5: Commit** `feat: Engine and SystemContext in @oly/core with TS systems called by the core`.

---

### Task 5: Pause, hidden tab and exceptions seen from TS

**Files:**
- Create: `engine/testing/tests/wasm/engine_pause.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,system_context.ts}` (only if the tests find gaps)

**Interfaces:**
- Consumes: `Engine`, `SystemContext`, `SystemRegistration.whilePaused()` from Task 4.

- [ ] **Step 1: Tests** (some may already pass; the ones that fail guide Step 3):
```ts
test("paused: only whilePaused runs, time freezes, render continues", ...)
test("whilePaused system calls context.resume() and the next step runs the gameplay", ...)
test("whilePaused() on a Render system is ContractError", ...)
test("setHidden(true) ignores frames; after setHidden(false) the 1st frame only records the time", ...)
test("exception inside eachBlock in a TS system reaches the caller of frame and the next frame recovers", ...)
  // Update: e = context.spawn([Position]); query.eachBlock(() => { throw new Error("boom") }) only on the 1st call
  // assert.throws(() => step(engine), /boom/); engine.world.checkInvariants() === null; engine.world.isAlive(e) === false
  // engine.world.spawn([Position]) works (World unlocked); next frame: 0 steps; the following one: 1 step
test("frame called from inside a system is ContractError", ...)
test("frame with a time lower than the previous one or NaN is ContractError", ...)
```

- [ ] **Step 2: Run** — `$WC make games-test T='^ts\.testing'` and note what fails.

- [ ] **Step 3: Fix** what failed, always in the right place: loop rule in C++ (`engine.cpp`), validation and unlocking in TS.

- [ ] **Step 4: See it pass** — `$WC make games-test` and, in Release, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (no asserts, only the TS validations catch the errors).

- [ ] **Step 5: Commit** `test: Engine pause, hidden tab and exception recovery seen from TS`.

---

### Task 6: Browser host in `@oly/client`

**Files:**
- Create: `engine/packages/client/src/browser_loop.ts`, `engine/packages/client/tests/browser_loop.test.ts`
- Modify: `engine/packages/client/src/index.ts`

**Interfaces:**
- Produces: `interface FrameTarget { frame(nowSeconds: number): void; setHidden(hidden: boolean): void }`; `interface BrowserEnvironment { requestAnimationFrame(callback: (timestampMilliseconds: number) => void): number; cancelAnimationFrame(handle: number): void; readonly document: { readonly hidden: boolean; addEventListener(type: "visibilitychange", listener: () => void): void; removeEventListener(type: "visibilitychange", listener: () => void): void } }`; `interface BrowserLoop { stop(): void }`; `function runInBrowser(target: FrameTarget, environment: BrowserEnvironment = globalThis-based): BrowserLoop`. The `@oly/core` `Engine` satisfies `FrameTarget` by structural typing.

- [ ] **Step 1: Failing tests** (`node:test`, fake environment that stores the rAF callbacks and the listener):
```ts
test("each rAF calls frame with the timestamp in seconds", ...)        // rAF(1500) → frame(1.5)
test("visibilitychange forwards setHidden with document.hidden", ...)
test("document already hidden at start calls setHidden(true)", ...)
test("stop cancels the pending rAF and removes the listener", ...)
test("exception in frame does not stop the loop: the next rAF was already requested", ...)
```

- [ ] **Step 2: See it fail** — `$WC make games-test T='^ts\.client'` → type error (`runInBrowser` does not exist).

- [ ] **Step 3: Implement** `runInBrowser`: requests the next rAF before calling `frame(timestamp / 1000)`.

- [ ] **Step 4: See it pass** — `$WC make games-test`.

- [ ] **Step 5: Commit** `feat: runInBrowser in @oly/client with requestAnimationFrame and visibilitychange`.

---

### Task 7: Loop cost (§7), results and documentation

**Files:**
- Create: `engine/testing/bench/common.ts`, `engine/testing/bench/loop.ts`, `docs/benchmarks/bridge/loop_phase2.json` (generated)
- Modify: `engine/testing/bench/boundary.ts` (uses `common.ts`), `docs/benchmarks/bridge/README.md`, `docs/benchmarks/README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: `Runtime.createEngine`, `Engine.addSystem/addNativeSystem/frame`, fixture systems `noop` and `move`, `fixtureMove`.
- Produces: `common.ts` with `parseOptions`, `loadBenchRuntime(modulePath)`, `median`, `rotated`, `expectedPositionX` (moved from `boundary.ts`, without changing its result).

- [ ] **Step 1: `loop.ts`** — same protocol as `boundary.ts` (warm-up, interleaved rounds with rotating order, median), with one step per `frame` (`frame(k * fixedDeltaTime)`), in ns/step:
  1. K ∈ {1, 4, 16, 64} empty systems: `cpp` (K × `addNativeSystem("noop")`), `ts` (K × empty TS system) and `zero` (no system, fixed cost of `frame`); ratios `ts/cpp` and cost per callback `(ts − cpp) / K`.
  2. K ∈ {4, 16, 64}, K/2 + K/2 in the same phase: `alternating` (`C,T,C,T…`) and `grouped` (`C…C,T…T`); ratio `alternating/grouped`.
  3. Boundary inside the loop, N ∈ {1000, 10000, 100000, 1000000}, ns/entity: `cpp-loop` (native `move`), `ts-loop` (TS system with `eachBlock` + views) and `ts-manual` (the same as `boundary.ts`); ratios `ts-loop/ts-manual` and `ts-loop/cpp-loop`. Check the final `x` like `boundary.ts` does (mismatch → exit 1).
  JSON output `{ meta, emptySystems, interleaving, boundaryInLoop }` with `meta` equal to that of `boundary.ts`.

- [ ] **Step 2: Verify the refactored `boundary.ts` still runs** — `$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'` and `$WC node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs --out /tmp/b.json --sizes 1000 --rounds 2` → ratio table printed, exit 0.

- [ ] **Step 3: Measure** — `$WC node engine/testing/dist/bench/loop.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/loop_phase2.json --commit $(git rev-parse --short HEAD)` → exit 0 and three tables printed.

- [ ] **Step 4: Record** — section "Loop cost — phase 2" in `docs/benchmarks/bridge/README.md` (command, environment, protocol, tables, a few lines of reading: cost per callback, whether interleaving costs more than the number of calls explains, and whether grouping TS systems is worth it) and one line in `docs/benchmarks/README.md`. No numeric target (§7).

- [ ] **Step 5: `CLAUDE.md`** — tree with `app/`; dependencies `core ← ecs ← app ← bridge ← games`; short paragraph about the `Engine` (loop in the core, per-phase `CommandBuffer` hidden behind `context.spawn/...`, single callback, recovery); glossary lines for `frame`, `setHidden`, `pause`/`resume`, `addSystem`/`addNativeSystem`/`OLY_SYSTEM`, `whilePaused`, and the deferred operations (`SystemContext::spawn` / `olySystemSpawn` / `context.spawn`); `app.*` in the list of ctest prefixes; `runInBrowser` in the `@oly/client` line.

- [ ] **Step 6: Final verification** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`: all green.

- [ ] **Step 7: Commit** `bench: phase 2 loop cost and Engine documentation`.
