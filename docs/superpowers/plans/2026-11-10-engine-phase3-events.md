# Engine — Phase 3 (Events) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Typed events in the C++ core (declaration on the `Engine`, queues, emission, batch handlers, type-by-type dispatch with cascade and round limit), exposed in the C API and in `@oly/core`, with `event(...)` in the schema and in the generator, and the §7 event measurement.

**Architecture:** `app` gains the whole event system: `Engine::declareEvent<T>()` gives a dense `EventTypeId` (declaration order = dispatch order), `detail::EventQueues` keeps per type a `pending` queue and a `dispatching` queue (raw bytes, struct layout), and step 4 (`dispatchEvents`) runs rounds: it moves everything pending to `dispatching`, calls each handler of the type once with the whole batch, type by type, until nothing is left or `maxEventRounds` is exceeded. C++ handlers receive a `HandlerContext` with direct structural changes. `bridge` declares the schema events when the `OlyEngine` is created (in schema order) and registers TS handlers as a C++ function that calls the same imported JS function as systems, now with `kind`, `id`, address and size of the batch. In TS, `event()` enters the schema, `Engine.on`/`addNativeHandler` register handlers and `EventBatch` gives views over the queue in WASM memory, with the same accessors as blocks.

**Tech Stack:** C++20, GCC 14 (native), Emscripten 3.1.74 + node 20.18 (`wasm` service), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (phase 3 of §5; all of §4.4, §4.1 for `event`, §4.3 for the step order, §6, §7 "Event cost"). `§N` below refers to that spec.

## Global Constraints

- Branch `feat/engine-phase3-events` from `develop`. Commits, comments, assert and error messages in English; commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain only in Docker: `DC="docker compose run --rm dev"` (native, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). Presets `wasm-games*` require `make ts` first.
- `-Wall -Wextra -Wpedantic -Werror` on every C++ target; TS `strict` (§3).
- Layers: `core ← ecs ← app ← bridge ← games`. The ECS does not change in this phase (if it does, rerun `ecs_bench_*` and check NF-P1/P2/P5 as in `docs/benchmarks/README.md`).
- Complete engine in C++ (§2): declaration, queues, order, cascade, limit, discard and recovery live in `oly::app::Engine`. TS only validates usage, writes values and forwards.
- Boundary: C++ systems emit without crossing the boundary; a TS handler is called once per batch; emitting from TS crosses once per event (§4.4). Never one call per event during dispatch.
- No game concept in `engine/runtime` or in `engine/packages`; game names only in `engine/testing/fixture/` and in tests.
- Glossary (`CLAUDE.md`): the same verb in all three layers — `declareEvent` (C++), `emit` (C++ `emit`, C `olyEngineEmit`, TS `context.emit`), `on` (C++ `Engine::on`, C `olyEngineAddScriptHandler`, TS `Engine.on`), `addNativeHandler` (C `olyEngineAddNativeHandler`, TS `addNativeHandler`, C++ `OLY_HANDLER`). `oly` only in the C API.
- `CLAUDE.md` style: braces on every block, full names, data at the top of the class, one `namespace` per file, project includes with quotes after the standard library. Do not format by hand (`clang-format` hook).

### Decisions the spec leaves open (fixed here)

- **Type identity in C++:** `EventKey eventKey<T>()` is dense per process (like `componentId`, but separate: events are not components). Each `Engine` maps `EventKey → EventTypeId`; the `EventTypeId` is the position in that `Engine`'s declaration order. `declareEvent<T>(name = oly::core::typeName<T>())` requires `std::is_trivially_copyable_v<T>` (`static_assert`) and `alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__` (`static_assert`): the queue is a `std::vector<std::byte>`.
- **Name in messages:** the name passed to `declareEvent`. The generated code passes the schema name (`"Ping"`), so the C++ message matches the name the TS game knows.
- **Queues:** per type, `pending` (receives emissions) and `dispatching` (the round's batch). At the start of the round, every type with a non-empty `pending` swaps (`swap`) the two; a type's batch does not change or move while its handlers run, because emissions go to `pending`. `clear()` keeps the capacity: after warm-up, dispatch does not allocate.
- **Round counter per step:** `eventRoundsThisStep_` is zeroed at the start of the fixed step and counts all rounds of the step (phase 6 will call `dispatchEvents` again after `onEnter`/`onExit`, §4.4).
- **Limit exceeded:** `OLY_ASSERT(false, "dispatchEvents: limit of %u event rounds exceeded; pending: %s", ...)` with the names of the types still pending separated by `, `. With asserts off, it discards all queues and continues the step (defined behaviour, no infinite loop). TS does not detect the overflow beforehand (it would need to replay the dispatch, which is engine logic): the assert applies.
- **Clock in handlers:** handlers see the same `time()` as the step's systems (start of the step). For that, `++steps_` moves from before `dispatchEvents` to the end of `runFixedStep`.
- **Emitting outside gameplay phases:** emission in `Render`, outside `frame` (`Engine::emit` directly) or with the tab hidden stays in `pending` and is dispatched in step 4 of the next fixed step that runs.
- **C++ contexts:** `EngineContext` (new, `oly/app/context.hpp`) gathers what `SystemContext` and `HandlerContext` have in common: `fixedDeltaTime`, `time`, `pause`, `resume`, `paused`, `query`, `get`, `tryGet`, `has`, `isAlive`, `emit<T>`, `emitById`. `SystemContext` (deferred + `interpolation`) and `HandlerContext` (direct + batch) derive from it.
- **C++ handler by function:** `HandlerFunction = void (*)(HandlerContext& context)`; the batch comes from `context.events<T>()` (`std::span<const T>`; `OLY_ASSERT` if `T` is not the handler's type). `OLY_HANDLER(name, EventType)` defines `void name(HandlerContext& context, std::span<const EventType> events)` and registers, by name, an untyped adapter that calls `name(context, context.events<EventType>())`.
- **Single callback:** `olyHostInvokeScript(OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount)`, with `kind` `OLY_SCRIPT_SYSTEM` (0; `events` null, `eventCount` 0) or `OLY_SCRIPT_HANDLER` (1). `ScriptInvoker` and `Module.olyInvokeScript` change to the same signature.
- **Schema events in `OlyEngine`:** the constructor declares, in schema order, all events from `oly::bridge::schemaEvents()`; this way the event's index in the TS schema is the `EventTypeId` (`OLY_ASSERT` if not). A C++-only game declares by hand and does not use the schema.
- **`olyEngineAddNativeHandler`:** returns `OLY_INVALID_HANDLER` (`0xFFFFFFFFu`) if the name does not exist and `OLY_HANDLER_TYPE_MISMATCH` (`0xFFFFFFFEu`) if the named handler is of another type (or of a type not declared in this engine); TS turns both into `ContractError` with the names.
- **TS handler makes direct structural changes:** during a TS handler call, the TS `Engine` unlocks the `World` (`setStructureLocked(world, false)`) and locks it again in `finally`. `HandlerContext.spawn/destroy/add/remove` delegate to the `World` (same validations and messages).
- **Batch views and memory growth (§4.4):** no copy. `EventBatch.read()` uses `makeView` from `fields.ts`, whose fields read `bridge.heap.<type>` on every access; every call that may allocate goes through the `Bridge`, which does `sync()`. The batch address does not change (the `dispatching` queue is not touched during the round), only the `ArrayBuffer`. Outside the handler call, `EventBatch.read()` throws `ContractError`.
- **`event()` requires at least one field** (`ContractError`, like `component`); events without data are not part of this phase.
- **Schema:** `defineSchema` accepts types and events mixed; `schema.types` still holds only components and tags (their index is the `ComponentId`'s), `schema.events` has the events in declaration order and `EventType.schemaIndex` is the position in `schema.events`. Names are unique across types and events. Canonical form for the hash: the current one followed by `!Name{field:type,...};` per event — a schema without events keeps today's hash.
- **Recovery (§4.4):** the `frame` after an exception discards both queues of all types together with the pending commands (including events emitted between the exception and that `frame`). What earlier handlers already did stays.
- **Out of scope for this phase:** handlers restricted to a state (`inState`, phase 6), `Engine.emit` in TS (only contexts emit), batch emission from TS (only if §7 shows a relevant cost).

## Review Focus

1. **Handler that emits many events of its own type while walking the batch** (the `pending` queue reallocates): the batch's `std::span` stays valid and the same size; the new ones arrive in the next round (Task 1, test under ASan).
2. **Entity referenced in the event already destroyed** by an earlier handler, by a repeated event in the same batch or by an earlier round: `destroyDead` with `isAlive` does not fire an assert and `checkInvariants` stays clean (Task 2).
3. **WASM memory grows inside a TS handler** while reading the batch: values and `entity` read after the growth are still correct (Task 5).
4. **Exception in a TS handler with a cascade in progress** (it already emitted another type): the emitted event is not dispatched in the next `frame`, `checkInvariants` clean and new events work again (Task 5).
5. **Events without a handler or emitted outside a step** (before the first `frame`, in `Render`): the ones without a handler vanish at the end of the step (a handler registered later does not see them); the ones emitted outside a step are dispatched in the next step, neither lost nor duplicated (Tasks 1 and 2).

---

### Task 1: Events in `app` — declaration, emission, batch handlers and dispatch in rounds

**Files:**
- Create: `engine/runtime/app/include/oly/app/{event.hpp,context.hpp,handler.hpp,detail/event_queues.hpp}`, `engine/runtime/app/src/{context.cpp,handler.cpp,event.cpp,event_queues.cpp}`, `engine/runtime/app/tests/events.cpp`
- Modify: `engine/runtime/app/include/oly/app/{system.hpp,engine.hpp}`, `engine/runtime/app/src/{system.cpp,engine.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp}`

**Interfaces:**
- Consumes: `Engine`, `SystemContext`, `Phase` from phase 2; `oly::core::typeName<T>()`.
- Produces (namespace `oly::app`):
  - `event.hpp`: `using EventTypeId = uint32_t; using HandlerId = uint32_t; using EventKey = uint32_t; inline constexpr EventTypeId InvalidEventType = 0xFFFFFFFFu;` `template <class T> EventKey eventKey() noexcept;` (dense per process; counter in `detail::nextEventKey()` in `event.cpp`).
  - `detail/event_queues.hpp`: `class EventQueues` with `EventTypeId declare(EventKey key, std::string_view name, uint32_t size)`, `EventTypeId typeOf(EventKey key) const noexcept` (`InvalidEventType` if not declared), `uint32_t typeCount() const noexcept`, `std::string_view name(EventTypeId type) const`, `void push(EventTypeId type, const void* event)`, `bool beginRound()` (swaps `pending`/`dispatching` of the types with pending events; `false` if nothing is pending), `const std::byte* batchData(EventTypeId type) const`, `uint32_t batchCount(EventTypeId type) const`, `void endRound()` (clears `dispatching`), `void clear()` (both queues of every type), `std::string pendingNames() const`.
  - `context.hpp`: `class EngineContext` with the members listed in "Decisions" (`template <class T> void emit(const T& event)`, `void emitById(EventTypeId type, const void* event)`; both `OLY_ASSERT` for an undeclared type, with the C++ type name or the id).
  - `system.hpp`: `SystemContext : public EngineContext` (keeps `phase`, `systemId`, `name`, `userData`, `interpolation` and the deferred ones).
  - `handler.hpp`: `class HandlerContext : public EngineContext` with `EventTypeId eventType() const`, `HandlerId handlerId() const`, `std::string_view name() const`, `void* userData() const`, `template <class T> std::span<const T> events() const`, `uint32_t eventCount() const`, `const void* eventData() const`, and direct `Entity spawn()`, `template <class... Ts> Entity spawn(Ts&&...)`, `destroy(Entity)`, `template <class T> add(Entity, T&&)`, `template <class T> remove(Entity)`, `addById(Entity, ComponentId, void* value)`, `removeById(Entity, ComponentId)`. `using HandlerFunction = void (*)(HandlerContext& context);` `struct NamedHandler { HandlerFunction function; EventKey eventKey; };` `const NamedHandler* findNamedHandler(std::string_view name) noexcept;` `struct NamedHandlerRegistration { NamedHandlerRegistration(std::string_view name, HandlerFunction function, EventKey eventKey); };` macro `OLY_HANDLER(name, EventType)`.
  - `engine.hpp`: `EngineConfig` gains `uint32_t maxEventRounds = 16;` (positive, `OLY_ASSERT`). `Engine` gains `template <class T> EventTypeId declareEvent(std::string_view name = oly::core::typeName<T>())`, `EventTypeId eventTypeOf(EventKey key) const noexcept`, `uint32_t eventTypeCount() const noexcept`, `template <class T> HandlerId on(std::string_view name, HandlerFunction function, void* userData = nullptr)`, `HandlerId onById(EventTypeId type, std::string_view name, HandlerFunction function, void* userData = nullptr)`, `template <class T> void emit(const T& event)`, `void emitById(EventTypeId type, const void* event)`. `HandlerId` is dense per `Engine`, in registration order.

- [ ] **Step 1: Branch** `git switch -c feat/engine-phase3-events`.

- [ ] **Step 2: Failing tests** in `tests/events.cpp` (`oly_add_test(MODULE app NAME events SOURCES events.cpp SUPPORT engine/runtime/ecs/tests/support)`). Test events: `struct Ping { uint32_t value; }; struct Echo { uint32_t depth; };`. Handlers record into `Log` (from `support/log.hpp`) via `userData`. One step per `frame` (helper `step(engine)`), `checkInvariants(world)` at the end of each case.
```cpp
TEST_CASE("handler receives the whole batch, in emission order") { /* Update emits Ping{1},{2},{3}; handler records the values */ CHECK(values == {1,2,3}); CHECK(handler calls == 1); }
TEST_CASE("handlers of the same type in registration order") { /* on<Ping> "h1", "h2", "h3" */ CHECK(log.calls == {"h1","h2","h3"}); }
TEST_CASE("types dispatched in declaration order, not emission order") { /* declares Ping, then Echo; Update emits Echo and then Ping */ CHECK(log.calls == {"ping","echo"}); }
TEST_CASE("event emitted during the round goes to the next round") { /* Ping handler emits Echo and Ping{99}; records (type, count, round) */ CHECK(1st round: Ping count 1); CHECK(2nd round: Ping{99} and Echo); }
TEST_CASE("batch does not change when the handler emits many of its own type") { /* handler of Ping{0}: reads span, emits 10000 Ping{1} (only in the 1st round), rereads span */ CHECK(span.size() == 1); CHECK(span[0].value == 0); }
TEST_CASE("cascade ends in the same step when the queues empty") { /* Echo{depth}: if depth < 5 emits Echo{depth+1} */ CHECK(depths == {0,1,2,3,4,5}); CHECK(all in the same frame); }
TEST_CASE("handler makes a direct structural change") { /* handler: e = context.spawn(Position{1,2}) */ CHECK(context.isAlive(e) still inside the handler); CHECK(world.get<Position>(e).x == 1); }
TEST_CASE("event without a handler is discarded at the end of the step") { /* emits Ping without a handler; step; registers handler; step without emitting */ CHECK(handler not called); }
TEST_CASE("OLY_HANDLER registers by name with the type") { CHECK(findNamedHandler("appTestPingHandler")->eventKey == eventKey<Ping>()); CHECK(findNamedHandler("doesNotExist") == nullptr); }
TEST_CASE("configurable round limit: exactly maxEventRounds rounds is allowed") { /* maxEventRounds = 3; Echo cascade of 3 rounds */ CHECK(passes without assert); }
```
`contract_death.cpp` gains the cases (same protocol; `CASES` in `CMakeLists.txt`):

| Case | Action | Substrings |
|---|---|---|
| `emit_undeclared_event` | `engine.emit(Ping{})` without `declareEvent` | `emit`, `not declared` |
| `on_undeclared_event` | `engine.on<Ping>("h", ...)` without `declareEvent` | `on`, `not declared` |
| `declare_event_twice` | `declareEvent<Ping>()` twice | `declareEvent`, `already declared` |
| `on_during_frame` | a system calls `engine.on<Ping>` | `on`, `during frame` |
| `declare_event_during_frame` | a system calls `engine.declareEvent<Echo>()` | `declareEvent`, `during frame` |
| `event_round_limit` | handler of `Echo` (declared with name `"Echo"`) always emits `Echo` | `dispatchEvents`, `rounds`, `Echo` |
| `events_wrong_type` | handler of `Ping` calls `context.events<Echo>()` | `events`, `type` |

- [ ] **Step 3: See it fail** — `$DC make test T='^app\.'` → compile error (`event.hpp` does not exist).

- [ ] **Step 4: Implement.** `EventQueues` as in "Decisions" (per type: `name`, `size`, `pending`, `dispatching` as `std::vector<std::byte>`; `typeOf` through a `std::vector<EventTypeId>` indexed by `EventKey`). In `Engine`: `std::vector<std::vector<HandlerEntry>> handlers_` per `EventTypeId` with `HandlerEntry { HandlerFunction function; void* userData; std::string name; HandlerId id; }`; `HandlerContext` built on the stack per call. `dispatchEvents()`:
```
while queues_.beginRound():
  if eventRoundsThisStep_ == maxEventRounds: OLY_ASSERT(false, ...pendingNames...); queues_.clear(); return
  ++eventRoundsThisStep_
  for each type in declaration order with batchCount > 0: each handler of the type, in order, receives the batch
  queues_.endRound()
```
(`pendingNames` in the message lists the types of the batch that overflowed — call it before any clearing). Move `fixedDeltaTime`/`time`/`pause`/`resume`/`paused` from `SystemContext` to `EngineContext` (`context.cpp`). `declareEvent`, `on`/`onById` with `OLY_ASSERT(!inFrame_, ...)` and the type/handler name in the message.

- [ ] **Step 5: See it pass** — `$DC make test` (everything, including the phase 2 tests and `oly.lint.boundaries`) and `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 6: Commit** `feat: events in app with batch handlers and dispatch in rounds`.

---

### Task 2: Events in the fixed step — reference flow, pause, render, clock and recovery

**Files:**
- Create: `engine/runtime/app/tests/events_loop.cpp`
- Modify: `engine/runtime/app/src/engine.cpp`, `engine/runtime/app/include/oly/app/engine.hpp` (comments on the empty steps and on `frame`), `engine/runtime/app/tests/{CMakeLists.txt,recovery.cpp}`

**Interfaces:**
- Consumes: everything from Task 1.
- Produces: behaviour described in "Decisions" for the clock in handlers, emission outside a step and recovery (queues cleared in `recoverFromInterruptedFrame`).

- [ ] **Step 1: Failing tests** in `tests/events_loop.cpp` (register in `CMakeLists.txt`). The reference flow uses only names local to the test: `struct Health { float value; }; struct Enemy {}; struct Coin {}; struct EnemyDied { oly::ecs::Entity entity; float x, y; }; struct CoinDropped { oly::ecs::Entity coin; };` and `Position` from the support.
```cpp
TEST_CASE("reference flow: damage, coin, score and destruction at the end of dispatch") {
  // UpdateAfter "damage": Health -= 10 on every Enemy; if <= 0 emits EnemyDied{entity, position.x, position.y} (does not destroy)
  // on<EnemyDied>: "dropCoin" (spawn Position{x,y} + Coin, emits CoinDropped{coin}), "addScore" (CHECK(context.isAlive(entity));
  //   reads its Position; ++score), "destroyDead" (destroy if isAlive), registered last; on<CoinDropped> "countCoin"
  // enemy with Health{10} at (3, 4); 1 step
  CHECK(!world.isAlive(enemy)); CHECK(score == 1); CHECK(coin at (3,4) with Coin); CHECK(countCoin called in the 2nd round); checkInvariants
}
TEST_CASE("repeated event: two deaths of the same enemy in the same batch") { /* damage emits EnemyDied twice */ CHECK(batch of 2); CHECK(destroyDead does not fire an assert); CHECK(2 coins); checkInvariants }
TEST_CASE("entity destroyed in an earlier round arrives dead in the next round") { /* handler of A destroys and emits B{entity}; handler of B */ CHECK(!context.isAlive(entity)); }
TEST_CASE("handlers run in a paused step") { /* whilePaused system emits Ping; pause(); 1 step */ CHECK(handler called); CHECK(engine.time() == 0.0); }
TEST_CASE("event emitted in Render is dispatched in the next step") { /* Render emits Ping in the 1st frame */ CHECK(handler not called in the 1st frame); /* 2nd frame */ CHECK(handler called once, before that frame's render); }
TEST_CASE("event emitted outside frame goes to the next step") { /* engine.emit(Ping{7}) before the 1st frame; frame(0) only records the time */ CHECK(not called); /* frame(1/60) */ CHECK(called with 7, once); /* frame(2/60) */ CHECK(still once); }
TEST_CASE("handler sees the time() of the start of the step, same as the systems") { /* system and handler record time() over 2 steps */ CHECK(handler times == system times == {0, 1/60}); }
```
`recovery.cpp` (native only, as today) gains:
```cpp
TEST_CASE("exception in a handler discards pending events on the next frame") {
  // on<Ping> "thrower": emits Echo and, on the 1st call, throws std::runtime_error; on<Echo> "echo" counts
  // step with Ping → CHECK_THROWS; next frame (only records the time); step without emitting → CHECK(echo == 0)
  // step with a new Ping → CHECK(echo == 1); checkInvariants
}
```

- [ ] **Step 2: See it fail** — `$DC make test T='^app\.'` → the handler clock (`time` one step ahead) and the recovery (old Echo dispatched) fail.

- [ ] **Step 3: Implement.** In `runFixedStep`: zero `eventRoundsThisStep_` at the start and move `++steps_` to after `applyStateTransition()`. In `recoverFromInterruptedFrame`: `queues_.clear()`. Update the `frame` comment in `engine.hpp` (the interrupted frame also discards events) and the one on the empty steps (only `captureActions`, `runDueSchedule`, `applyStateTransition` remain).

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: events in the fixed step with step clock and recovery from interrupted handler`.

---

### Task 3: `event` in the schema, in the generator and in the WASM check

**Files:**
- Modify: `engine/packages/core/src/{schema.ts,schema_hash.ts,runtime.ts,module.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/{schema.test.ts,surface.test.ts}`, `engine/tools/codegen/src/emit_cpp.ts`, `engine/tools/codegen/tests/generator.test.ts`, `engine/runtime/bridge/include/oly/bridge/{schema_registry.hpp,c_api.hpp}`, `engine/runtime/bridge/src/c_api.cpp`, `engine/runtime/bridge/tests/{test_schema_registry.cpp,c_api.cpp}`, `engine/testing/tests/wasm/load.test.ts`
- Create: `engine/runtime/bridge/tests/test_events.hpp`

**Interfaces:**
- Produces:
  - TS (`@oly/core`): `interface EventType<Name extends string = string, F extends Fields = Fields> { readonly kind: "event"; readonly name: Name; readonly fields: F; readonly layout: ComponentLayout; readonly schemaIndex: number }`; `function event<const Name extends string, const F extends Fields>(name: Name, fields: F): EventType<Name, F>` (same validations as `component`); `Schema` gains `readonly events: readonly EventType[]`; `defineSchema(types: readonly (SchemaType | EventType)[]): Schema`.
  - Internal TS: `eventIndexOf(runtime: Runtime, type: EventType): number` (in `runtime.ts`, exported by `internal.ts`; `ContractError` "type does not belong to this runtime's schema" like `componentIdOf`).
  - C++ (`oly/bridge/schema_registry.hpp`): `struct SchemaEvent { std::string_view name; uint32_t size; uint32_t alignment; void (*declare)(oly::app::Engine& engine); };` `std::span<const SchemaEvent> schemaEvents();` (defined by the generated code).
  - C API: `uint32_t olySchemaEventCount()`, `uint32_t olySchemaEventSize(uint32_t eventIndex)`, `uint32_t olySchemaEventAlignment(uint32_t eventIndex)` (index not checked, like `olySchemaComponentId`).
  - Generator: struct + layout `static_assert` for each event (same format as components) in `schema.generated.hpp`; in `schema_registry.generated.cpp`, `#include "oly/app/engine.hpp"` and `schemaEvents()` with one entry per event: `{"Name", size, alignment, [](oly::app::Engine& engine) { engine.declareEvent<Name>("Name"); }}`.
  - `test_events.hpp` (bridge): `struct BridgeTestEvent { uint32_t value; float amount; };` and `struct BridgeOtherEvent { uint32_t value; };` (the latter outside the test registry).

- [ ] **Step 1: Failing tests.**
  - `schema.test.ts`: `event("Hit", {})` throws `ContractError`; `defineSchema([A, Hit, T, Boom])` gives `types` = `[A, T]` and `events` = `[Hit, Boom]` with `Hit.schemaIndex === 0`, `Boom.schemaIndex === 1`, `A.schemaIndex === 0`, `T.schemaIndex === 1`; a name repeated between component and event throws `ContractError`; the hash of a schema without events equals `fnv1a32` of today's canonical form (literal value computed before the change); adding an event changes the hash.
  - `generator.test.ts`: `struct Hit { uint32_t target; float amount; };` with layout `static_assert`; the registry contains `#include "oly/app/engine.hpp"` and `engine.declareEvent<Hit>("Hit")` before `declareEvent<Boom>("Boom")`.
  - `c_api.cpp`: `olySchemaEventCount() == 1`, `olySchemaEventSize(0) == sizeof(BridgeTestEvent)`, `olySchemaEventAlignment(0) == alignof(BridgeTestEvent)` (`test_schema_registry.cpp` now defines `schemaEvents()` with `BridgeTestEvent`).
  - `load.test.ts`: TS schema with one more event than the WASM → `SchemaMismatchError` whose message contains "events in WASM"; same number of events with a different size → `SchemaMismatchError` with the event name (build the schemas like the mismatch cases already in the file).
  - `surface.test.ts` / `public-exports.txt`: `event`, `EventType`.

- [ ] **Step 2: See it fail** — `$WC make ts` → type error (`event` does not exist); `$DC make test T='^bridge\.'` → compile error (`schemaEvents`).

- [ ] **Step 3: Implement.** `canonicalSchema` also receives the events and appends `!Name{field:type,...};` per event. `loadRuntime` checks `_olySchemaEventCount() === schema.events.length` and the size/alignment of each event; the `SchemaMismatchError` message gains "events in WASM: n, in TS: m" and the first mismatch may be an event. `OlyModule` gains the three exports.

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make games-test` (the fixture has no events yet: generated `schemaEvents()` is empty).

- [ ] **Step 5: Commit** `feat: event in the schema, in the generator and in the WASM check`.

---

### Task 4: Events in the C API — emission, native handlers by name and script handlers

**Files:**
- Modify: `engine/runtime/bridge/include/oly/bridge/{engine_api.hpp,engine.hpp}`, `engine/runtime/bridge/src/{engine_api.cpp,handles.hpp}`, `engine/runtime/bridge/tests/{engine_api.cpp,contract_death.cpp,CMakeLists.txt}`

**Interfaces:**
- Consumes: Task 1 (`declareEvent`, `onById`, `emitById`, `eventTypeOf`, `eventTypeCount`, `HandlerContext::eventData/eventCount/handlerId`, `findNamedHandler`), Task 3 (`schemaEvents()`).
- Produces (C API, `extern "C"`, `OLY_API`):
  - `#define OLY_INVALID_HANDLER 0xFFFFFFFFu`, `#define OLY_HANDLER_TYPE_MISMATCH 0xFFFFFFFEu`, `#define OLY_SCRIPT_SYSTEM 0u`, `#define OLY_SCRIPT_HANDLER 1u`.
  - `OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame, uint32_t maxEventRounds)` (declares `schemaEvents()` in order).
  - `void olyEngineEmit(OlyEngine*, uint32_t eventType, const void* value)` (`eventType < eventTypeCount()`, `OLY_ASSERT` with `olyEngineEmit` and `eventType`; copies `size` bytes; valid inside and outside `frame`).
  - `uint32_t olyEngineAddNativeHandler(OlyEngine*, uint32_t eventType, const char* name)`, `uint32_t olyEngineAddScriptHandler(OlyEngine*, uint32_t eventType, const char* name)` (`eventType` out of range is `OLY_ASSERT`).
  - C++ (`engine.hpp`): `using ScriptInvoker = void (*)(OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount);`
  - `EM_JS(void, olyHostInvokeScript, (OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount), { Module["olyInvokeScript"](engine, kind, id, events, eventCount); })`.

- [ ] **Step 1: Failing tests** in `tests/engine_api.cpp` (the test invoker now records `(engine, kind, id, eventCount)` and the `value`s read from `events`; `OLY_HANDLER(bridgeTestNativeHandler, BridgeTestEvent)` records `"n"` and the values):
```cpp
TEST_CASE("schema events are declared at creation, in order") { CHECK(engineOf(engine).eventTypeOf(eventKey<BridgeTestEvent>()) == 0); CHECK(engineOf(engine).eventTypeCount() == 1); }
TEST_CASE("script handler receives kind, id and the batch") { /* olyEngineAddScriptHandler; olyEngineEmit 2× outside frame; 1 step */ CHECK(kind == OLY_SCRIPT_HANDLER); CHECK(id == returned); CHECK(eventCount == 2); CHECK(values == {1, 2}); }
TEST_CASE("script and native-by-name handlers in registration order") { /* script, native, script */ CHECK(recorded == {"s","n","s"}); }
TEST_CASE("script system still receives OLY_SCRIPT_SYSTEM") { CHECK(kind == OLY_SCRIPT_SYSTEM); CHECK(eventCount == 0); }
TEST_CASE("unknown handler name and handler of another type") { CHECK(olyEngineAddNativeHandler(engine, 0, "doesNotExist") == OLY_INVALID_HANDLER); /* OLY_HANDLER(bridgeTestOtherHandler, BridgeOtherEvent) */ CHECK(olyEngineAddNativeHandler(engine, 0, "bridgeTestOtherHandler") == OLY_HANDLER_TYPE_MISMATCH); }
TEST_CASE("olyEngineEmit inside a script system is dispatched in the same step") { /* the system's invoker calls olyEngineEmit */ CHECK(handler called in the same frame); }
```
Update the existing `olyEngineCreate` calls (two arguments → three, `maxEventRounds = 16`). `contract_death.cpp`: case `engine_emit_out_of_range` (`olyEngineEmit(engine, 5, &value)`; substrings `olyEngineEmit`, `eventType`).

- [ ] **Step 2: See it fail** — `$DC make test T='^bridge\.'` → compile error.

- [ ] **Step 3: Implement.** In the `OlyEngine` constructor, after creating the `engine`, call `schemaEvent.declare(engine)` for each event and `OLY_ASSERT` that the returned id is the index. Script handler: a `bridge` function registered with `onById(type, name, runScriptHandler, engine)` that calls the invoker with `OLY_SCRIPT_HANDLER`, `context.handlerId()`, `context.eventData()`, `context.eventCount()`. `olyEngineAddNativeHandler` resolves `findNamedHandler(name)` and compares `engine.eventTypeOf(named->eventKey)` with `eventType`.

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: events in the C API with native handlers by name and script handlers by batch`.

---

### Task 5: Events in `@oly/core` — `on`, `addNativeHandler`, `HandlerContext`, `EventBatch` and `emit`

**Files:**
- Create: `engine/packages/core/src/{context.ts,handler_context.ts,event_batch.ts}`, `engine/testing/tests/wasm/events.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,system_context.ts,runtime.ts,bridge.ts,module.ts,values.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/{schema.ts,systems.cpp}`, `engine/testing/tests/wasm/fixture.ts`

**Interfaces:**
- Consumes: C API from Task 4; `EventType`, `eventIndexOf` from Task 3; `makeView`, `ReadView` from `fields.ts`.
- Produces (`@oly/core`):
  - `EngineConfig` gains `maxEventRounds?: number` (positive integer, default 16; otherwise `ContractError` in `createEngine`).
  - `Engine.on<F extends Fields>(type: EventType<string, F>, name: string, handler: HandlerFunction<F>): HandlerRegistration`; `Engine.addNativeHandler(type: EventType, name: string): HandlerRegistration` (`ContractError` with the name for `OLY_INVALID_HANDLER` and, for `OLY_HANDLER_TYPE_MISMATCH`, with the handler and event names). Both: `ContractError` during `frame`, with a type from another schema or with the `Engine` disposed.
  - `interface HandlerRegistration { readonly id: number }`; `type HandlerFunction<F extends Fields = Fields> = (context: HandlerContext, batch: EventBatch<F>) => void`.
  - `class EventBatch<F extends Fields = Fields>`: `readonly type: EventType`, `readonly count: number`, `read(): ReadView<F>` (`ContractError` outside the handler call).
  - `class HandlerContext`: `readonly eventType: EventType`, `readonly name: string`, the common members and the direct `spawn`, `destroy`, `add`, `remove` (delegate to the `World`).
  - Common to both contexts (base `EngineContext` in `context.ts`, exported from the file but not from `index.ts`): `fixedDeltaTime`, `time`, `paused`, `pause()`, `resume()`, `query`, `get`, `set`, `has`, `isAlive`, `emit<F extends Fields>(type: EventType<string, F>, value?: Partial<ValueOf<F>>): void` (missing fields = 0; unknown field or type from another schema = `ContractError`). `SystemContext` keeps `phase`, `name`, `interpolation` and the deferred ones.
  - `values.ts`: `writeComponentValue` becomes `writeValue(bridge, layout: ComponentLayout | null, value)` (null = tag); the `Bridge` scratch area covers the largest of components and events.
  - `OlyModule`: `_olyEngineCreate(fixedHz, maxStepsPerFrame, maxEventRounds)`, `_olyEngineEmit(engine, eventType, value)`, `_olyEngineAddNativeHandler(engine, eventType, name)`, `_olyEngineAddScriptHandler(engine, eventType, name)`, `olyInvokeScript?: (engine: number, kind: number, id: number, events: number, eventCount: number) => void`.
  - `public-exports.txt`: + `EventBatch`, `HandlerContext`, `HandlerFunction`, `HandlerRegistration`.
- Fixture: `Ping = event("Ping", { target: "entity", value: "f32" })` and `Echo = event("Echo", { depth: "u32" })`, in that order, at the end of `defineSchema`. In `systems.cpp`: `OLY_HANDLER(appendNineOnPing, Ping)` (for each event, `trace(target).value = value * 10 + 9`), `OLY_HANDLER(sumPing, Ping)` (sums `value` into a static `double`), `OLY_SYSTEM(emitPings)` (emits `fixturePingCount` `Ping{NullEntity, 1}`), exports `fixtureSetPingCount(uint32_t)` and `double fixtureTakePingSum()` (returns and zeroes).

- [ ] **Step 1: Failing tests** in `engine/testing/tests/wasm/events.test.ts` (same helpers as `engine.test.ts`: `withEngine`, `stepper`; `checkInvariants() === null` at the end of each test):
```ts
test("TS and C++ Ping handlers in registration order", ...)        // Trace{0}; Update emits Ping{target}; on h1 ×10+1, addNativeHandler appendNineOnPing, on h2 ×10+2; 1 step → 192
test("types dispatched in schema order, not emission order", ...) // emits Echo and then Ping → ["Ping","Echo"]
test("batch: count and values through the views, including entity", ...)      // 3 Pings with value 1,2,3 → count 3, read().value.get(i), read().target.get(0) === entity
test("event emitted by a TS handler goes to the next round of the same step", ...) // Ping handler emits Echo{1} and Ping; current batch stays count 1; Echo arrives in the same step()
test("TS handler destroys directly; repeated event does not break", ...)   // 2 Pings with the same target; handler destroys if isAlive → isAlive false, no error
test("event emitted in Render is dispatched in the next step", ...)
test("memory grows inside the TS handler and the batch stays readable", ...)
  // Ping{target: e, value: 42}; in the handler: buffer = moduleOf(runtime).HEAPU8.buffer; context.spawn([Position, Velocity, Mixed, Trace])
  // until the buffer changes (at most 1 000 000 times; assert that it changed); then batch.read().value.get(0) === 42 and target.get(0) === e
test("exception in a TS handler reaches the caller of frame and the next frame discards pending events", ...)
  // Ping handler emits Echo and throws only the 1st time; assert.throws(step, /boom/); engine.world.checkInvariants() === null
  // next frame: 0 steps; the one after: Echo not dispatched; new Ping → Echo dispatched
test("direct structural change on the World inside the handler is allowed; inside the system it is still forbidden", ...)
test("emit with unknown field, with an event from another schema or outside the call is ContractError", ...)
test("addNativeHandler with unknown name or of another type is ContractError with the names", ...) // ("doesNotExist"); (Echo, "appendNineOnPing")
test("on during frame is ContractError; batch.read() and context after the handler are ContractError", ...)
test("event without a handler is discarded at the end of the step", ...)
```
`surface.test.ts`: new members of `Engine` (`on`, `addNativeHandler`), `SystemContext` (`emit`), `HandlerContext` and `EventBatch` exactly as in Produces.

- [ ] **Step 2: See it fail** — `$WC make games-test` → type error in `tsc` (`on` does not exist).

- [ ] **Step 3: Implement.** `Bridge`: `engineCreate(fixedHz, maxStepsPerFrame, maxEventRounds)`, `engineEmit(engine, eventType, valuePointer)`, `addNativeHandler`, `addScriptHandler` (the ones that allocate end in `sync()`). `Runtime` forwards `olyInvokeScript(engine, kind, id, events, eventCount)` to the `Engine`'s `invokeSystem` or `invokeHandler`. `Engine#invokeHandler(id, address, count)`: `bridge.sync()`, creates the `EventBatch` (address, `count`, the type's layout), unlocks the `World`, activates the `HandlerContext`, calls, and in `finally` invalidates the batch, deactivates the context and locks the `World` again.

- [ ] **Step 4: See it pass** — `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (no asserts: only the TS validations catch the errors) and `$DC make test`.

- [ ] **Step 5: Commit** `feat: events in @oly/core with TS batch handlers and emit in the contexts`.

---

### Task 6: Event cost (§7), results and documentation

**Files:**
- Create: `engine/testing/bench/events.ts`, `docs/benchmarks/bridge/events_phase3.json` (generated)
- Modify: `docs/benchmarks/bridge/README.md`, `docs/benchmarks/README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: `common.ts` (`parseOptions`, `loadBenchRuntime`, `median`, `rotated`), `Engine.on/addNativeHandler/addSystem/addNativeSystem/frame`, fixture `emitPings`, `sumPing`, `fixtureSetPingCount`, `fixtureTakePingSum`.

- [ ] **Step 1: `events.ts`** — same protocol as `loop.ts` (warm-up, interleaved rounds with rotating order, median, one step per `frame`), N ∈ {16, 256, 4096} `Ping` events per step, in ns/event:
  1. Handler: emission by native `emitPings` and `none` (no handler), `cpp` (`addNativeHandler(Ping, "sumPing")`) and `ts` (TS handler that sums `value` via `batch.read()`); ratio `ts/cpp` and cost per batch `(ts − cpp)` in ns/step. Check the sum (`fixtureTakePingSum()` or the TS sum) = N × steps; mismatch → exit 1.
  2. Emission: `cpp` (`emitPings`) versus `ts` (TS system with N × `context.emit(Ping, { value: 1 })`), both with `sumPing`; ratio `ts/cpp`.
  JSON output `{ meta, handler, emission }` with `meta` equal to that of `loop.ts`.

- [ ] **Step 2: Measure** — `$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'` and `$WC node engine/testing/dist/bench/events.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/events_phase3.json --commit $(git rev-parse --short HEAD)` → exit 0 and two tables printed.

- [ ] **Step 3: Record** — section "Event cost — phase 3" in `docs/benchmarks/bridge/README.md` (command, environment, protocol, tables, a few lines of reading: cost of the callback per batch, cost per event read through the views, and whether batch emission from TS is worth it) and one line in `docs/benchmarks/README.md`. No numeric target (§7).

- [ ] **Step 4: `CLAUDE.md`** — in the `app` section, a short paragraph about events (declaration on the `Engine` = dispatch order, `pending`/`dispatching` queues, rounds with the `maxEventRounds` limit, batch handlers with direct changes, recovery discards events); in the bridge section, the callback with `kind` and the schema events declared when the `OlyEngine` is created; glossary lines for declaring an event, emitting, registering a handler and handler by name; `event` in the `@oly/core` list.

- [ ] **Step 5: Final verification** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`: all green.

- [ ] **Step 6: Commit** `bench: phase 3 event cost and documentation`.
