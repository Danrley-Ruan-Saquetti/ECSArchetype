# Game engine on top of the ECS — design

Date: 2026-11-09. Evolves the package described in `2026-10-16-ecs-archetype-spec.md` (requirements `R-*`, invariants
`I-*` and targets `NF-*` still hold for the ECS) and reuses the WASM build infrastructure from
`2026-11-06-wasm-cost-study-design.md`.

Revision of 2026-11-10 (before phase 2): the engine becomes complete in C++, with TypeScript as a thin layer;
the loop moves out of TS and into C++ (§1, §2, §4, §4.3, §5, §7, §8). Names follow the modularization
(`2026-11-09-oly-modularization-design.md`): `oly` prefix, `OLY_ASSERT`, modules in `engine/runtime/<m>/`.

Revision of 2026-11-10 (before phase 3): §4.4 detailed for events — handlers receive batches per type,
dispatch type by type within each round of the cascade, reference flow for an enemy's death, and the game (not the
engine) decides when to destroy the entity. Phase 3 measurement in §7.

Revision of 2026-11-11 (before phase 4): §4.5 detailed for input actions — the host pushes raw events,
physical buttons are USB HID usages, actions are declared in the schema, own `input` module, no platform
interface (§4.3). Phase 4 tests in §5 and absence of a benchmark justified in §7.

Revision of 2026-11-11 (before phase 5): §4.6 detailed for the schedule — what gets scheduled is now called a
**task** (the name "action" went to input, §4.5), payload copied into C++ tasks, no native task by name
(TS reaches C++ through an event), discard that tells TS to release the closure. Phase 5 tests in §5 and absence of a
benchmark justified in §7.

Revision of 2026-11-12 (before phase 6): §4.7 detailed for states — states declared in the schema, entry into the
initial state by the same rule as any change, scope through an internal tag in the `World`, persistence requested after
creation (`persist`), direct spawn from TS going through the engine. Indicative usage (§4.8), phase 6 tests in §5 and absence
of a benchmark justified in §7.

Revision of 2026-11-12 (before phase 7): §4.9 details the validation game — `games/arena/`, C++/TS split of the
gameplay, DOM-free game (`installGame`) separate from the browser shell, esbuild for serving, `single` in C++ and TS
(promised in §4.2), rule for engine gaps discovered by the game and the phase list. Phase 7 tests in §5 and
absence of a benchmark justified in §7.

Convention of this document: whatever is not marked was decided by the project's author. Items marked
**[design decision]** were choices made while writing the document, without explicit confirmation; they may be revised
in the plan of the corresponding phase without going back to the author, as long as they respect the goals and anti-goals.

## 1. Intent

Turn the archetype ECS into a **reusable game engine for 2D games in the browser**, with:

- a **C++ core** (the current ECS plus the engine infrastructure), compiled to WASM. **The core is the whole
  engine**: loop, phases, fixed step, pause, events, actions, schedule and states live in it;
- a **TypeScript complement**, which is how the game uses the engine in the browser. It is a **thin layer**: it loads the
  WASM, forwards the browser's time and signals to the core, registers TS systems and gives copy-free access to
  components. It contains no engine logic missing from C++.

Reason (from the author): someone who wants to write a game in C++ only, and in the future run it in an environment other than the browser,
must not find the engine incomplete because part of it was written in TypeScript.

A system is a system, regardless of language: the game can write each system in C++ or in TypeScript, and the
two kinds coexist in the same phases. This makes the **WASM↔JS boundary a central piece of the design**, not a detail:
TS systems access components through copy-free views over the WASM memory.

The project remains a study project too: abstraction and boundary costs are measured, in the same
style as the ECS targets (ratios against a baseline from the same run).

### 1.1 Validation game

A top-down 2D shooter, in the browser:

- the player has a weapon with infinite ammo, aims with the mouse and can **dash** (with recharge);
- coins appear across the map and the player collects them;
- enemies chase the player to attack them; the player flees and kills them;
- lifecycle sequence: menu → playing → game over → play again.

The game is a **validation client** of the engine: if something it needs can only be done with a hack outside the
engine (outside the mechanisms of systems, events, actions, schedule and states), the engine is incomplete. Render,
collision and audio for the game are written in the game itself (§2). The game design is in §4.9.

## 2. Goals, non-goals, anti-goals

**Goals**
- Reusable engine: nothing in the core or in the TS package knows concepts of the validation game (enemy, coin, dash).
- Complete infrastructure for: systems in fixed phases, safe structural changes, events, input actions,
  `schedule` of tasks over time, application lifecycle and game states.
- Systems in C++ and in TypeScript with the same usage model.
- Engine complete in C++: a C++-only game uses every mechanism (loop, phases, pause and, in the following phases,
  events, actions, schedule and states) through the C++ API, without depending on anything written in TS. What is specific to the environment
  (time, frame, visibility and, later, input) enters the core through the **host** (§4.3), which is replaceable.
- **Copy-free** TS access to components, per archetype block.
- The validation game playable in the browser, end to end.
- Boundary cost measured: the same system written in TS (via the engine) and in C++, and against a hand-written TS baseline.

**Non-goals (for now)**
- Render, collision/physics, audio, asset loading, camera, UI. They stay with the game.
- Threads, Web Workers, `SharedArrayBuffer`.
- Network/multiplayer, editor, hot reload, serialization/save.
- Running outside the browser as a product (a native build exists only to test the C++ core, §6). The engine is
  **ready** for it (complete core and replaceable host), but no real native host (window, input, SDL etc.)
  is delivered now.
- Guaranteed support for multiple browsers: the validation target is a Chromium-based browser.
- Gamepad (actions are designed to accept it later; keyboard and mouse are enough now).

**Anti-goals** (failure even if it works)
- Crossing the WASM↔JS boundary **per entity** on hot paths (system iteration, event emission by
  C++ systems). The boundary is crossed per block, per batch or per system.
- A component's layout diverging between C++ and TS. There is a single source (the schema, §4.1).
- Regressing the ECS: presets, tests, invariants and numbers recorded in `docs/benchmarks/README.md` still hold.
- Concepts of the validation game leaking into the engine.
- Engine logic (deciding when to run steps, phases, pause, events, schedule, states) written only in TS. TS
  may have API conveniences, never behaviour that a C++-only game would not have.
- An engine that only works with the validation game (APIs shaped for a single case).

## 3. Constraints

- Toolchain only in Docker, as today. The WASM path uses the Emscripten image already pinned by the cost study
  (`emscripten/emsdk:3.1.74`, node 20.18). JS/TS tools (TypeScript, bundler, test runner) also run in the
  container; the host does not need node.
- `-Wall -Wextra -Wpedantic -Werror` on every C++ target; TypeScript in `strict` mode.
- Project style and language (`CLAUDE.md`): commits, comments and messages in English; full names; strict layers.
- Layers: the engine sits **above** `world` and only uses the ECS's public API. The ECS does not get to know the engine.
- **[design decision]** The ECS's public API **may be extended** with operations by `ComponentId` at
  runtime, which TS requires (TS queries are built at runtime): type-erased query by ID lists
  (`read`/`write`/`with`/`without`) with access to the column pointer and `count` per table, and `add`/`remove` by ID in the
  `CommandBuffer` (the `World` already has `addRaw`/`removeRaw`). The existing template APIs start delegating to these
  where it makes sense. This extension is the first task of phase 1 and comes with tests, `checkInvariants` and a
  rerun of the ECS benchmarks to prove nothing regressed.
- The ECS's validity contracts also hold in TS: any structural change invalidates pointers and spans;
  only `Entity` is a stable handle.

## 4. Architecture

```
 game (TS and/or C++)
 ─────────────────────────────────────────────────────────
 TS packages (thin layer)         │  C++ engine core (WASM or native)
  boot (loads the WASM)           │   loop: frame(now) → fixed steps + render
  browser host: rAF,              │   phases, pause, CommandBuffer per phase
   visibilitychange, input        │   events, schedule, states (phases 3–6)
  views over columns              │   exported C API (extern "C")
  TS systems/handlers             │   C++ systems/handlers
   (called back by C++)           │
 ─────────────────────────────────────────────────────────
 ECS (component → entity → table → archetype → query/command_buffer → world)
```

### 4.1 Single schema for components and events

- Components and events are declared in a **schema** of scalar fields (`f32`, `f64`, `i32`, `u32`, `u8`, `entity`
  etc.) and tags (components without data). Indicative example:
  `component("Position", { x: "f32", y: "f32" })`, `tag("Enemy")`, `event("EnemyDied", { entity: "entity", x: "f32", y: "f32" })`.
- A generator produces, from the schema, the C++ header (trivial structs) and the TS descriptors (name, size,
  alignment, fields with type and offset). It also checks, at build time, that the C++ `sizeof`/`alignof`/`offsetof`
  match the TS descriptor (e.g. generated `static_assert`s).
- **[design decision]** The schema is written in TypeScript (or JSON read by a node script); the exact format and the
  generator's language are up to the implementer.
- Consequence: the WASM is compiled **per game** (engine + the game's schema + the game's C++ systems). The engine is
  distributed as a C++ library + TS package + generator, and the game's build puts them together.
- Components that do not fit the schema (strings, JS objects, image references) live in TS-side storage
  indexed by `Entity`, outside the `World`. **[design decision]** This storage is simple (map per type) and cannot be
  queried by queries; its cleanup follows the entity's destruction.

### 4.2 Boundary and copy-free access

- The core exposes a **C API** (`extern "C"`, exported by Emscripten); no embind. **[design decision]**
- A TS system declares a query (`read`, `write`, `with`, `without`) and iterates with `eachBlock`: one callback per
  non-empty table, receiving `count` and typed views over the columns in the WASM linear memory. The boundary is
  crossed once per block (getting column pointers and `count`), never per entity.
- **Column layout: [design decision, important]** the current `Table` stores each component as a column of
  structs (`Position` = `x0 y0 x1 y1 …`), that is, SoA across components and AoS within the component. The design
  **keeps this layout**. A field's view is a typed array over the whole column with a stride
  (`x[row] = column[row * stride + fieldOffset]`, in elements of the field's type); generated code hides the stride
  behind accessors. Splitting fields into their own columns (SoA per field) would change the `Table` and the C++ query API,
  and is left as future study, motivated by the boundary benchmarks.
- Alignment: since structs are trivial and naturally aligned, every scalar field has an offset that is a multiple of its
  size and the struct size is a multiple of the alignment, so the strided typed array is always valid.
- **View validity:** only inside the `eachBlock` callback. WASM memory growth (`ALLOW_MEMORY_GROWTH`)
  detaches the `ArrayBuffer`, and structural changes move rows; that is why views are never kept across calls.
  In a debug build, the engine detects use of a view outside the callback when it is cheap to do so.
- `Entity` in TS: **[design decision]** represented as a `number` (index + generation packed safely
  within 53 bits, or two fields); the choice is up to the implementer as long as comparing and storing is cheap.
- One-off access (`get(entity, Component)`, `single(...)`) exists for cases like "the player's position"; it crosses the
  boundary per call and must not be used in a per-entity loop. `single` only arrives in phase 7 (§4.9).

### 4.3 Loop and phases

**Who drives the loop.** The loop belongs to the **C++ core**. A new C++ module (suggested name `app`, in
`engine/runtime/app/`, above `ecs`; **[design decision]** the name) holds the `World`, the phases and the order of
systems, the game clock, the fixed-step accumulator, the pause state and the `CommandBuffer` per phase.

- **The host drives time; the core decides the rest.** The host is the one that knows the environment. It calls `frame(now)` once
  per frame (time in seconds, monotonic) and signals when the game becomes hidden/visible (`setHidden`). The core
  accumulates time, runs the fixed steps that fit and the `render` phase. Hosts:
  - browser: the `@oly/client` package, with `requestAnimationFrame` and `visibilitychange`, calling the C API;
  - tests: a fake clock, both in native C++ (doctest) and in TS on node (through the WASM's C API);
  - other environments: left for later (§2); the engine must not depend on anything from the browser.
  In phase 2, `frame` and `setHidden` are enough and there is **no** platform interface. In phase 4 input also arrives
  through host calls (raw events pushed, §4.5), so there is still no platform interface.
  **[design decision]**
- **C++ systems** are functions registered in the core and called directly, without a boundary. A C++-only game registers them
  through the C++ API (`addSystem(phase, name, function)`). So that TS can place them in phase order, the game's C++ systems
  can also be registered by **name** with a macro (e.g. `OLY_SYSTEM(move)`), and TS adds them by that
  name (`addNativeSystem`).
- **TS systems** are registered by TS, which receives an id from the core. At run time, the core calls back into TS
  through **a single JS function imported** by the WASM module, which picks the system by id (no `addFunction` nor a growing function
  table). **[design decision]** The boundary is crossed once per TS system per step, which the
  anti-goal allows (per system, not per entity). The same mechanism serves, in the following phases, for event
  handlers, schedule callbacks and state hooks written in TS.
- Callback contract: while a TS system runs, the core does not keep pointers into the `World`
  or the heap across the call, because TS may allocate in the WASM (memory growth) and change structure through the
  `CommandBuffer`. Exceptions thrown by a TS system propagate to whoever called `frame` and leave the engine in a
  defined state (documented in the plan), without corrupting the `World`.

**Application lifecycle** (controlled by the engine, the same for any game):
- `boot`: in the browser, TS loads the WASM; the core creates the `World` and registers the schema. A C++-only game creates the
  engine directly.
- `run`: the host starts calling `frame(now)`. Gameplay at a **fixed step** (**[design decision]** 60 Hz by default,
  configurable), accumulating real time between calls, with a limit of steps per frame to avoid a spiral of
  lag (the excess is discarded). Render once per `frame`, receiving the interpolation factor between steps.
- `pause`/`resume`: automatic when the tab becomes hidden (`visibilitychange`) and available to the game
  (`engine.pause()`/`engine.resume()`, also through the `context`). While paused:
  - the game clock and the `schedule` freeze, and gameplay systems do not run;
  - fixed steps keep happening only to **capture actions** (§4.5) and run systems registered with
    `.whilePaused()` (e.g. a system that reads the `pause` action and calls `resume`, or the pause menu), followed by
    event dispatch and the pending state transition;
  - `render` continues.
  Pause due to a hidden tab is independent from the game's pause: the tab comes back and the game resumes on its own, unless the game
  has also paused. While the tab is hidden no step runs (the browser stops `requestAnimationFrame`; the core,
  notified by `setHidden`, ignores `frame`), and the accumulated time is discarded on return. This rule lives in the core;
  the host only forwards the signal.
- `shutdown`: releases the `World` and the engine's resources.

**Loop details** (**[design decision]**, revisable in the phase 2 plan):
- `pause()`/`resume()` requested in the middle of a step take effect from the next fixed step.
- The `render` phase runs paused or not; `.whilePaused()` only applies to gameplay phases.
- While paused, the game clock freezes, but `.whilePaused()` systems receive the normal `fixedDeltaTime`.
- Inside systems (C++ and TS), the context only offers structural changes through the phase's `CommandBuffer`; direct
  structural access to the `World` stays outside the loop. The `CommandBuffer` is also applied at the end of the `render` phase.
- The first `frame`, and the first one after the hidden tab, only record the time: the accumulator starts at zero.
- Dependencies: `app` sits above `ecs`; `bridge` starts depending on `app` to expose the loop in the C API. There can be
  more than one engine per WASM instance; the single callback receives which engine called.
- Exception in a TS system: C++ is compiled without exceptions, so a JS error crosses the WASM without running destructors. The
  engine marks the step as interrupted, the next `frame` recovers, and this has a test.

**Fixed phases.** Each system registers in exactly one phase. The gameplay phases are `updateBefore`, `update`,
`updateAfter`. Order of a fixed step (**[design decision]** the order of the items that are not gameplay phases):

1. capture of input actions (§4.5)
2. firing of due scheduled tasks (§4.6)
3. `updateBefore` → `update` → `updateAfter`
4. event dispatch (§4.4)
5. pending state transition, if any (§4.7)

And, per `requestAnimationFrame` frame: the `render` phase (only once, after the frame's fixed steps).

- There is no physics phase: the game's collision and physics register in `updateAfter`. **[design decision]**
- Within a phase, the order is the registration order. **[design decision]** There is no dependency graph.
- C++ and TS systems mix freely in the same phase, in registration order; the core walks the phase and calls
  each one (C++ directly, TS through the callback).
- **Structural changes inside systems** (`spawn`, `destroy`, `add`, `remove`) are deferred through the ECS's
  `CommandBuffer`, applied **at the end of each phase**. C++ and TS systems write to the **same** `CommandBuffer` of the phase; TS
  reaches it through the C API (operations by `ComponentId`). **[design decision]** **[design decision]** `spawn` returns the `Entity` immediately (the ECS already
  reserves the index). Changing component values is direct.
- A system can be restricted to states (`inState(playing)`, §4.7); outside them it does not run.
- A system registered with `.whilePaused()` runs **only** while the engine is paused by the game; the others, only
  while it is not. The two restrictions (state and pause) combine.

### 4.4 Events

Phase goal (from the author): events written in C++ as much as possible. TS comes in only where the browser is needed
(e.g. audio). A C++-only game uses events entirely through the C++ API.

**Reference flow** (accepted by the author as the model; names belong to the game, not the engine):

1. A C++ damage system, in `updateAfter`, reduces health. When it reaches zero, it emits
   `EnemyDied { entity, x, y }` and does **not** destroy the entity.
2. In step 4, the `EnemyDied` handlers run in registration order, each receiving the step's batch of deaths:
   `dropCoin` (C++, creates the coin at `x, y`, may emit another event, which becomes a new round of the cascade),
   `addScore` (C++), `playDeathSound` (TS, the only one in TS because audio belongs to the browser) and `destroyDead` (C++,
   registered last, destroys the entity).
3. While the handlers run, the enemy is still alive and any handler can read its components. At the end of the
   dispatch, it no longer exists.

Consequences for the engine:
- **The game is the one that destroys.** The engine guarantees handler order; "destroy at the end" means registering the handler that
  destroys last. There is no automatic destruction associated with an event.
- **The engine does not remove repeated events.** Two bullets that kill the same enemy in the same step generate two
  `EnemyDied` in the same batch; the game's handlers deal with it (e.g. `destroyDead` checks `isAlive`). This holds
  for any handler that changes structure: an entity named in an event may already have been destroyed by an earlier handler,
  by an earlier batch or by an earlier round.

Rules:
- Event types are declared in the schema (§4.1) with `event(name, fields)`, the same scalar types as
  components (including `entity`); the generator produces the C++ struct and the TS descriptor, as for components. Events are not
  components: they have their own id and do not enter the `World`. Each type has a queue.
- **Declaration of event types in the `Engine`, [design decision]:** the registry of event types (dense id,
  size, alignment and name of each queue) lives in `app`, not in the generated code. A type must be declared in the
  `Engine` before being emitted or receiving handlers (something like `engine.declareEvent<EnemyDied>()`; using an undeclared
  type is a contract error), and the **declaration order is the dispatch order**. A C++-only game declares the
  types by hand, without schema or codegen; on the TS path, `bridge` declares the schema's events in schema order.
  This way the dispatch order is deterministic and controlled by the game on both paths.
- Systems (C++ or TS) and handlers **emit**; nobody reacts at emission time.
- **A handler receives a batch:** a handler is registered for a type and is called **once per batch**, with all the
  pending events of that type in emission order (C++: something like `std::span<const EnemyDied>`; TS: views over the
  queue, with the same generated accessors as blocks). **[design decision]** The handler walks the batch; there is no
  call per event, not even in C++.
- **Dispatch order, [design decision]:** each round takes a snapshot of all non-empty queues and dispatches
  **type by type**, in the declaration order in the `Engine` (on the TS path, schema order); within a type, each handler in registration order receives the
  whole batch. Events emitted during the round go to the next round (they do not enter the batch in progress, even of the
  same type). This way the batch a handler is reading never changes or moves while it runs. The global emission order
  across different types is **not** preserved; this is documented.
- Dispatch happens outside query iteration, so **handlers make structural changes directly** (without a
  `CommandBuffer`), in C++ and in TS. Handlers can start queries and iterate them; inside that iteration the usual ECS rules
  apply.
- **C++ handlers** register through the C++ API (`engine.on<EnemyDied>(name, function)` or equivalent) and, so that TS
  can fit them into the order, by name with a macro (e.g. `OLY_HANDLER(dropCoin, EnemyDied)`), which TS adds
  with `addNativeHandler`, like `OLY_SYSTEM`/`addNativeSystem`. **TS handlers** use the same single callback as
  TS systems (§4.3), once per batch. C++ and TS handlers of the same type mix in registration order.
- **Pause, [design decision]:** handlers run on every fixed step, paused or not; there is no `.whilePaused()` for
  handlers. While paused, only `.whilePaused()` systems run, so only they (and their handlers, in cascade) produce events.
- **Cascade within the same step:** events emitted during dispatch are dispatched right after, round after round,
  until the queues empty. There is a **round limit** (configurable in `EngineConfig`; **[design decision]**
  default 16); exceeding it is a contract error (`OLY_ASSERT` in C++, `ContractError` in TS before reaching the
  assert, if TS can detect it; otherwise the assert), with a message that names the event type(s) still pending.
- The queues live in WASM memory with the schema's layout, so that C++ systems emit without crossing the boundary and
  TS handlers receive a batch (views over the queue), not one call per event. **[design decision]**
- **Emission from TS, [design decision]:** may cross the boundary once per event (`context.emit(EnemyDied,
  {...})`); emitting from TS is not a hot path of the validation game. If the benchmark (§7) shows a relevant cost, the
  implementer can accumulate in TS and send in a batch, without changing the API.
- **Event views in TS and memory growth:** a TS handler makes direct structural changes, which may
  allocate in the WASM and detach the `ArrayBuffer`. The batch's views must not become silently invalid in the middle of the
  handler: the implementer chooses how (e.g. accessors that go through the `Bridge` and are rebuilt after `sync()`, or copying
  the batch to JS before calling the handler), and the choice has a test that forces memory growth inside a
  handler. **[design decision]** the requirement; the mechanism is up to the implementer.
- Handler context: the same kind of access as `SystemContext` (time, pause, `get`/`tryGet`/`has`/`isAlive`,
  queries, `emit`), plus **direct** structural changes in place of the deferred ones. **[design decision]** The names
  (`HandlerContext` or another) are up to the implementer.
- **Exception in a TS handler:** same rule as TS systems (§4.3): the step is marked as interrupted, and the next
  `frame` discards pending events together with pending commands and recovers. What earlier handlers already
  did stays.
- The step's events are discarded after dispatch; they do not outlive the step.
- Handler registration follows the rules of system registration: it cannot happen during `frame` (`OLY_ASSERT`).
- **Events emitted outside the gameplay phases** (**[design decision]**):
  - in scheduled tasks (step 2): they enter the queue and are dispatched in step 4 of the same fixed step;
  - in `onExit`/`onEnter` (step 5): after the transition, if there are pending events, the engine runs a new dispatch
    in the same fixed step, with the same cascade rule; the rounds count toward the same step limit;
  - in the `render` phase: they stay in the queue and are dispatched in step 4 of the next fixed step (render is not gameplay, but
    silently losing an event would be worse).
- The scope cleanup of a transition (§4.7) does not emit events; the engine has no implicit destruction events.

### 4.5 Input actions ("commands")

- The game declares **actions** and binds them to inputs: button (`dash` → `Space`), 2D axis (`move` → WASD), pointer
  (`aim` → mouse position in canvas coordinates). The bindings table can be swapped without touching systems.
- Systems query actions, never keys: `pressed`, `held`, `released` for buttons; a vector for axes; a position for
  the pointer.
- The state is captured **once per fixed step** and is the same for C++ and TS systems (a snapshot written in the WASM
  memory). A press shorter than a step must not be lost: `pressed` from an event between two steps shows up in the
  next step.
- Converting the pointer position to world coordinates belongs to the game (there is no camera in the engine).
- The ECS's `CommandBuffer` remains an internal mechanism and is not exposed under that name by the engine.

**Module.** Actions live in their own module, `input` (`engine/runtime/input/`, `DEPENDS core`, namespace
`oly::input`), which knows neither the ECS nor the `Engine`. `app` starts depending on `input`. This way the edge rules
are testable without a loop or a `World`, and the boundary lint prevents input from depending on the ECS. The `ActionMap` is not
a platform interface: the core never asks the host anything.

**Physical buttons.** A key or a mouse button is a `PhysicalButton`: a `uint32_t` in the USB HID extended usage
format, `(page << 16) | usage`. Keyboard is page `0x07` (`Key::W = 0x0007'001A`, `Key::Space =
0x0007'002C`), mouse is page `0x09` (`MouseButton::Left = 0x0009'0001`, `Right = 0x0009'0002`, `Middle =
0x0009'0003`). `Key` and `MouseButton` are `enum class : uint32_t` with these values, implicitly converted to
`PhysicalButton`. The HID values are the same as SDL scancodes, so a future native host needs no table.
The identity is the key's **physical position** (the W3C `KeyboardEvent.code`, not `key`): WASD stays in the same place on
any keyboard layout.

**Host input (raw events pushed).** The host calls, at any time (inside or outside `frame`):
- `buttonDown(button)` and `buttonUp(button)`: they enter an edge queue, in arrival order. A `buttonDown` of an
  already pressed button (keyboard auto-repeat) is ignored by the core; the host filters nothing.
- `pointerMove(x, y)`: does not enter the queue; only the last position matters. Coordinates in canvas pixels, with `y`
  growing downward; converting from DOM coordinates belongs to the host.
- `releaseAll()`: releases all pressed buttons (generates the release edges). The host calls it on the window's `blur`,
  because `keyup` does not arrive when focus leaves; the `Engine` calls it in `setHidden(true)`.
The boundary is crossed once per input event (dozens per second), off the hot path.
**[design decision]** The edge queue has no limit: it only grows with human events and every step empties it.

**Capture (step 1, on every fixed step, paused or not).** The core consumes the queue in order and builds the snapshot:
- **Button.** A button action has zero or more bindings (e.g. `Space` and `ShiftLeft`). `held` is true if some
  bound button is pressed at the end of the capture; `pressed` is true if the action went from released to pressed
  at some point in the queue; `released`, the opposite. A press within one step gives `pressed` and `released` true
  with `held` false, and that is how it is not lost. Two keys on the same action: releasing one while the other is
  pressed does not generate `released`.
- **Edges last for one step.** In a `frame` with 3 steps, only the first sees the edges received before it; in a
  `frame` with 0 steps, the queue waits for the next step.
- **2D axis.** Each binding has 4 buttons (`up`, `down`, `left`, `right`); there can be several (WASD and arrows). The value
  comes from the pressed state at the end of the capture: `x = right − left`, `y = down − up` (**[design decision]** `y` grows
  downward, as in the canvas, so that `move` and `aim` use the same system), opposite keys cancel out and the diagonal is
  normalized to length 1. An axis has no edges.
- **Pointer.** The last received position and `valid`, false until the first `pointerMove`. For now the pointer has no
  binding: the source is the host's single pointer.
- **Bindings swappable at any time**, including from inside a system (key configuration menu). The swap
  takes effect from the next capture, and the action's state is recomputed from the pressed physical buttons.

**C++ API** (normative names for the plan; signature details are up to it):

```cpp
struct ButtonAction  { ActionId id; };   // typed ids: reading an axis as a button does not compile
struct Axis2dAction  { ActionId id; };
struct PointerAction { ActionId id; };

struct ButtonState   { bool pressed; bool held; bool released; };
struct Vector2       { float x; float y; };
struct PointerState  { float x; float y; bool valid; };
struct Axis2dBinding { PhysicalButton up, down, left, right; };

class ActionMap {
 public:
  ButtonAction  declareButton(std::string_view name);   // dense ActionId, in declaration order
  Axis2dAction  declareAxis2d(std::string_view name);
  PointerAction declarePointer(std::string_view name);

  void bind(ButtonAction action, PhysicalButton button);
  void bind(Axis2dAction action, Axis2dBinding binding);
  void clearBindings(ActionId action);
  bool isBound(PhysicalButton button) const;

  void buttonDown(PhysicalButton button);
  void buttonUp(PhysicalButton button);
  void pointerMove(float x, float y);
  void releaseAll();

  void capture();

  ButtonState  button(ButtonAction action) const;
  Vector2      axis2d(Axis2dAction action) const;
  PointerState pointer(PointerAction action) const;
  std::span<const ActionState> snapshot() const;   // for the bridge
};
```

- The `Engine` holds an `ActionMap`, exposed in `engine.actions()`; the host and the game declare, bind and deliver input
  through the same object. `captureActions()` (step 1) calls `capture()`; `setHidden(true)` calls `releaseAll()`.
  `EngineContext` gains `actions()`, so systems and handlers read and rebind through the same path
  (`context.actions().button(actions::dash).pressed`). **[design decision]** No duplicate readers in the context
  (`context.button(...)`), so the context's surface does not repeat that of `ActionMap`.
- Declaring an action during `frame` is a contract error, like declaring an event. Binding, delivering input and reading are valid at any
  time.
- A C++-only game keeps the return value of `declareButton` and binds with `engine.actions().bind(dash, Key::Space)`.

**Schema and codegen.** Actions are declared in the schema, like events: `action("dash", "button")`, with kind
`"button"`, `"axis2d"` or `"pointer"`, goes to `schema.actions` with `schemaIndex` in order. The name is unique across types,
events and actions and must be a valid C++ identifier. The schema hash includes the actions (name and kind), so an
outdated WASM gives `SchemaMismatchError`. The codegen generates `namespace actions { inline constexpr
oly::input::ButtonAction dash{0}; }` (the namespace avoids collision with component names) and a `schemaActions()`
like `schemaEvents()`; `olyEngineCreate` declares the schema's actions in schema order, so the index in the schema
is the `ActionId` on both sides and a wrong name in a native system is a compile error. **Bindings** do not enter the
schema: they are runtime data.

**Snapshot and C API.** `ActionState` is `{ float x; float y; uint32_t flags; }` (12 bytes, one per action, in
`ActionId` order), with the `pressed`, `held`, `released` and `valid` bits in `flags`; the layout is checked by `static_assert`.

```c
void olyEngineBindButton(OlyEngine*, uint32_t action, uint32_t button);
void olyEngineBindAxis2d(OlyEngine*, uint32_t action, uint32_t up, uint32_t down, uint32_t left, uint32_t right);
void olyEngineClearBindings(OlyEngine*, uint32_t action);
int32_t olyEngineIsBound(OlyEngine*, uint32_t button);
void olyEngineButtonDown(OlyEngine*, uint32_t button);
void olyEngineButtonUp(OlyEngine*, uint32_t button);
void olyEnginePointerMove(OlyEngine*, float x, float y);
void olyEngineReleaseAll(OlyEngine*);
const OlyActionState* olyEngineActionSnapshot(OlyEngine*);   // stable until olyEngineDestroy
```

TS reads the snapshot through `Bridge` views (rebuilt after `sync()`), so reading an action does not cross the boundary.

**`@oly/core`.**
- `action(name, kind)` returns a handle typed by kind (`ButtonAction`, `Axis2dAction`, `PointerAction`):
  `context.actions.axis2d(dash)` is a type error.
- Reading in systems and handlers, with the C++ shape: `context.actions.button(dash)` → `{ pressed, held, released }`,
  `axis2d(move)` → `{ x, y }`, `pointer(aim)` → `{ x, y, valid }`. Each read creates a small object; there are few
  per step.
- Bindings: `engine.actions.bind(dash, "Space")`, `bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right:
  "KeyD" })`, `clearBindings(action)`. The names are those of the W3C `KeyboardEvent.code`, plus `"MouseLeft"`,
  `"MouseRight"` and `"MouseMiddle"`, typed as a literal union (a wrong name is a compile error). The W3C name →
  HID usage table lives in `@oly/core` (data only, no DOM).
- Host side: `engine.actions.buttonDown(name)`, `buttonUp(name)`, `pointerMove(x, y)`, `releaseAll()`.

**`@oly/client`.** `attachBrowserInput(engine, canvas, environment?)` → `{ detach() }`, separate from `runInBrowser`
(loop and input change for different reasons):
- keyboard and `blur` on the `window` (`blur` → `releaseAll`); `pointerdown` on the canvas (a click outside it does not shoot);
  `pointerup` and `pointermove` on the `window` (releasing outside the canvas also arrives);
- position converted to canvas pixels with `getBoundingClientRect` and the ratio `canvas.width / rect.width` (likewise
  for the height);
- codes outside the table are ignored;
- `preventDefault` only for buttons bound to some action (`olyEngineIsBound`): `Space` does not scroll the page and the right
  button does not open the menu, but `F5` and the browser shortcuts keep working.

**Contract errors** (`OLY_ASSERT` in C++; in TS, `ContractError` before reaching the assert): declaring an action during
`frame` or with a repeated name; `ActionId` out of range or of the wrong kind in the C API (in typed C++, it does not compile);
button with usage `0`; in TS, a button name outside the table and an action handle from another schema.

**HID table on both sides.** The C++ enum and the TS table copy the same standard; a test on each side checks the same
samples (letters, digits, arrows, `Space`, modifiers, mouse) against the standard's values.

### 4.6 Schedule

- Scheduling of **tasks** on the **game clock** (the `Engine`'s `time()`, sum of the non-paused fixed steps):
  `after(seconds, callback)` and `every(seconds, callback)`, with a handle to cancel. The name "action" belongs to input
  (§4.5); what gets scheduled is a task.
- Callbacks run in step 2 of the loop, outside iteration, and can make structural changes directly and emit events.
- Callbacks in TS and in C++. TS schedules only TS closures and C++ only C++ functions: there is no native task by name
  (like `OLY_SYSTEM`), because TS would have to write the C++ payload. A TS task that needs C++ code emits an
  event with a C++ handler, dispatched in step 4 of the same fixed step.
- Examples from the game: enemy wave every 5 s; dash available again after 0.8 s; new coin every 2 s.

**Time rules**
- A task stores due time (seconds, `double`), interval (0 for `after`), creation sequence, callback,
  payload and generation.
- **Due time = `time()` + delay**, computed when scheduling. Inside a step, `time()` is the start of the step; outside
  `frame`, the start of the next step. The delay must be finite and ≥ 0; the `every` interval, finite and > 0.
- **Firing in step 2**, only in a non-paused step: all tasks with due time ≤ `time()` run (with the same
  tolerance as the fixed-step accumulator), in due-time order and, on a tie, creation order.
- **Tasks created during step 2 only run from the next step on**, even if already due: an `after(0)` inside
  a task does not become a loop. An `after(0)` scheduled outside `frame` runs in step 2 of the next step; scheduled by
  a system, in the step after that system's.
- **`every`:** the first firing is at now + interval. After each firing, the next due time is
  due time + interval (no drift); if it is still ≤ now, it jumps to the first multiple > now. This way each task
  fires at most once per step, and an interval shorter than the fixed step becomes "once per step".
- **The task is rescheduled (`every`) or removed from the queue (`after`) before the callback.** An exception in the callback leaves the
  queue consistent, and an `every` can cancel itself inside the callback.
- **Cancel:** the handle carries the generation. Cancelling a task already finished or already cancelled does nothing and returns
  `false` (including an `after` cancelling itself in the callback, which has already left the queue). A task cancelled in the same
  step, before running, does not run.
- **Pause and hidden tab:** the clock stops, so nothing becomes due. Steps discarded by the per-frame limit do not advance the
  clock, so they do not generate accumulated delay.
- **Exception recovery (§4.3):** tasks survive, like the `World`; pending commands and events are still
  discarded.

**Structure, [design decision]:** the queue lives in `engine/runtime/app/detail/` (like the event queues), with no
new module. It is a min-heap ordered by (due time, sequence) over slots with a generation, with lazy cancellation
(the cancelled task is marked and leaves when it reaches the top). With dozens of tasks, a heap and a linear scan cost
little (unmeasured hypothesis); the heap delivers the order ready-made. Rejected alternatives: a module of its own like `input` (the
pure part is just a priority queue; context, direct changes and bridge belong to `app`) and tasks as ECS
entities (they would show up in the game's queries and the engine would use the `World` for internal state, even though scope cleanup in
phase 6 would come for free).

**C++ API** (normative names for the plan; signature details are up to it):

```cpp
struct TaskHandle { uint32_t slot = InvalidTaskSlot; uint32_t generation = 0; };
using TaskFunction = void (*)(TaskContext& context);
using TaskDiscardFunction = void (*)(void* payload);

class Scheduler {
 public:
  template <class P>
  TaskHandle after(double delaySeconds, TaskFunction function, const P& payload, TaskDiscardFunction discard = nullptr);
  TaskHandle after(double delaySeconds, TaskFunction function);
  template <class P>
  TaskHandle every(double intervalSeconds, TaskFunction function, const P& payload, TaskDiscardFunction discard = nullptr);
  TaskHandle every(double intervalSeconds, TaskFunction function);

  bool cancel(TaskHandle task);
  bool isPending(TaskHandle task) const;
  uint32_t pendingCount() const;
};

class TaskContext : public DirectContext {
 public:
  TaskHandle handle() const;
  bool lastRun() const;   // true on the firing of an after: the task does not run again
  template <class P>
  const P& payload() const;
};
```

- **Payload:** copied into the task; it must be trivially copyable, up to 32 bytes and with alignment up to 8
  (`static_assert`, **[design decision]** the limits). The task stores the type; `payload<P>()` with another type is a contract
  error, like `events<T>()`. A pointer is also a payload, so the "function + `userData`" model of systems and
  handlers fits in it.
- **Where to schedule:** `engine.schedule()`, inside and outside `frame`, and `context.schedule()` in systems, handlers and
  tasks; it is the same `Scheduler`, like `actions()`. Scheduling is not a structural change, so it applies directly also inside
  systems and in the `render` phase.
- **`DirectContext`, [design decision]:** direct structural changes move out of `HandlerContext` into a base
  between `EngineContext` and the two contexts that use them (`HandlerContext` and `TaskContext`), instead of being repeated.
- **Discard:** `TaskDiscardFunction` (optional) runs **exactly once** when the task leaves the queue **without** its last
  execution: cancellation, self-cancellation of an `every`, end of scope (phase 6) and destruction of the `Engine`. It receives
  only the payload. On the firing of an `after`, the callback itself is the last call (`lastRun()`), and it is the one that releases
  whatever is in the payload. The discard does not run after the last callback because a JS exception unwinds the WASM without
  executing what comes after, and the TS closure would be stuck.
- Usage:

```cpp
struct DashTarget {
  ecs::Entity player;
};

void dashReady(TaskContext& context) {
  const ecs::Entity player = context.payload<DashTarget>().player;
  if (context.isAlive(player)) {
    context.get<Dash>(player).ready = 1;
  }
}

context.schedule().after(0.8, dashReady, DashTarget{player});
```

**Bridge and C API.** A TS task is an ordinary C++ task, registered by `bridge`: the function calls
`olyHostInvokeScript` with a new `kind`, the payload holds the script id and the discard notifies TS. `app` still does not
know TS.

```c
#define OLY_SCRIPT_TASK 2u           // olyHostInvokeScript(engine, kind, scriptId, NULL, lastRun)
#define OLY_SCRIPT_TASK_DISCARD 3u   // olyHostInvokeScript(engine, kind, scriptId, NULL, 0)

typedef struct OlyTaskHandle {
  uint32_t slot;
  uint32_t generation;
} OlyTaskHandle;

// intervalSeconds == 0 schedules an after; > 0, an every. Valid inside and outside olyEngineFrame.
OLY_API void olyEngineScheduleScriptTask(OlyEngine* engine, double delaySeconds, double intervalSeconds, uint32_t scriptId, OlyTaskHandle* out);
OLY_API int32_t olyEngineCancelTask(OlyEngine* engine, uint32_t slot, uint32_t generation);
OLY_API int32_t olyEngineIsTaskPending(OlyEngine* engine, uint32_t slot, uint32_t generation);
```

- TS picks the `scriptId` (its own counter); the core returns it in the callbacks. The boundary is crossed once
  per firing and once per discard, off the hot path.

**`@oly/core`.**

```ts
const handle = context.schedule.after(0.8, (task) => {
  if (task.isAlive(player)) {
    task.set(player, Dash, { ready: 1 });
  }
});
engine.schedule.every(5, spawnWave);
context.schedule.cancel(handle);
context.schedule.isPending(handle);
```

- `schedule` exists on the `Engine` and on `SystemContext`, `HandlerContext` and `TaskContext`; it is the same object.
- `TaskContext` has the access of `HandlerContext` without the batch (direct structural changes, `emit`, `schedule`), plus
  `handle` and `lastRun`. It is only valid during the call; outside it, `ContractError`.
- `TaskHandle` is an opaque value that is cheap to compare; the representation is up to the implementer, like that of `Entity`.
  `cancel` returns `false` for an already finished task.
- **Closure lifetime:** TS keeps `scriptId → closure` and deletes the entry in the `finally` of the last execution or on
  receiving `OLY_SCRIPT_TASK_DISCARD`. The map ends empty on every path (firing, cancellation,
  self-cancellation, exception, `dispose`), and this has a test.

**Contract errors** (`OLY_ASSERT` in C++; in TS, `ContractError` before reaching the assert): negative or non-finite
delay; interval ≤ 0 or non-finite; null function; `payload<P>()` with a type different from the scheduled one; handle with `slot`
out of range (it did not come from this `Scheduler`; an old generation is not an error, `cancel` returns `false`); in TS, a callback
that is not a function, `TaskContext` used outside the call and a handle from another `Engine`. Scheduling and cancelling are valid at any
time, including during `frame`.

**Preparation for phase 6.** Phase 5 delivers cancellation by the core and the discard that notifies TS, which
cancellation by scope will use. The scope field in the task and "cancel everything in the scope" come in phase 6, when
states exist.

### 4.7 Game states

- The game declares a flat set of states (e.g. `menu`, `playing`, `gameOver`). There are no nested states nor a
  stack. **[design decision]**
- Hooks `onEnter(state)` and `onExit(state)`; they can make structural changes and use the `schedule`.
- Requesting a transition (`context.requestState(gameOver)`) does not switch immediately: the switch happens in step 5 of the loop
  (`onExit` of the old one, scope cleanup, `onEnter` of the new one). Two requests in the same step: the last one wins.
  **[design decision]**
- **Scope:** entities and scheduled tasks created while a state is active (including in `onEnter`) belong to
  it by default and are destroyed/cancelled when leaving it. Creating something persistent requires asking explicitly.
  **[design decision]** This way "play again" (`gameOver → playing`) starts clean with no cleanup code in the game.
- Systems and handlers can be restricted to states.

**Declaration**
- `StateId` is a dense `uint32_t`, in declaration order; `NoState = 0xFFFFFFFF`. The engine starts in `NoState`.
- A C++-only game declares with `engine.declareState(name)`. On the TS path, states are declared in the schema
  (`state("menu")`), and `olyEngineCreate` declares them in schema order, after events and actions; the index in the
  schema is the `StateId` on both sides.
- Declaring during `frame` or with a repeated name is a contract error. **[design decision]** At most 32 states,
  so that the `inState` restriction is a `uint32_t` mask.

**Request and current state**
- `requestState(state)` exists on the `Engine` and in every context (system, handler, task, hook). It is valid inside and
  outside `frame`; the last request made before step 5 wins.
- `state()` returns the current state, which only changes in step 5.
- **Initial state, [design decision]:** there is no dedicated concept. The game requests the first state before running
  (`engine.requestState(menu)`), and the entry happens in step 5 of the first fixed step, by the same rule as any change.
  In that first step only the systems without `inState` run. Rejected alternative: entering at the start of the first step,
  which would create a second rule just to save one step.
- **Reentry, [design decision]:** requesting the current state runs its `onExit`, cleanup and `onEnter`, which serves
  to restart the level.
- **[design decision]** The switch does not touch the pause. A game that wants to leave the pause when entering a state calls
  `resume()` in `onEnter`.

**Transition (step 5, on every fixed step, paused or not)**
1. Consumes the pending request. Without a request, step 5 does nothing.
2. Runs the `onExit` hooks of the current state, in registration order. The state is still active, so what these hooks create
   has scope and is cleaned in the next item.
3. Cleans the scope (below).
4. The current state becomes the new one.
5. Runs the `onEnter` hooks of the new state, in registration order; what they create has the new state's scope. On the first
   `onEnter`, the origin state is `NoState`.
6. If there are pending events, runs a new dispatch (§4.4), which counts toward the same round limit of the step.

**[design decision]** A request made in a hook or in the dispatch of item 6 takes effect in step 5 of the next fixed step:
there is no loop of switches within a step.

**Hooks**
- C++: `engine.onEnter(state, name, function, userData)` and `engine.onExit(...)`, with
  `StateHookFunction = void (*)(StateContext&)`. By name, with the macro `OLY_STATE_HOOK(name)`, which TS adds with
  `addNativeOnEnter(state, name)` and `addNativeOnExit(state, name)`, like `OLY_SYSTEM`/`addNativeSystem`.
- TS hooks use the single callback (§4.3) with a new `kind`, once per hook. C++ and TS hooks of the same
  state and the same moment (entry or exit) mix in registration order.
- Registering a hook during `frame` is a contract error.
- **`StateContext`, [design decision]:** derived from `DirectContext` (§4.6): direct structural changes, `emit`,
  `schedule` and `requestState`, plus `fromState()` and `toState()`. Only valid during the call.

**Scope**
- **Mechanism, [design decision]:** an internal tag, `StateScoped` (in `engine/runtime/app/detail/`, with no data nor
  column). Since only one state is active at a time, every entity with the tag belongs to the current state, and one tag is enough.
  No game needs to mention it; it only shows up in queries that filter by it. Accepted cost: twin archetypes (with and
  without the tag) when the game creates the same combination with and without scope. Rejected alternative: a list of entities in the
  `Engine`, which grows with every spawn (every bullet), needs compaction and would make `persist` search the list or keep
  an extra set.
- **Scoped entities:** those created by `spawn` from any context while `state() != NoState`. In
  `SystemContext`, the spawn and the tag's `add` enter the `CommandBuffer` and become a single transition at flush; in
  `DirectContext`, the tag goes into `world.spawn(components..., StateScoped{})` itself. With no active state, and therefore
  in a game that does not use states, no entity receives the tag.
- **Direct spawn in the `World`** (outside any context, e.g. `engine.world().spawn()` in setup) is always
  persistent: the `World` sits below the engine, which does not see that spawn.
- **Direct spawn in TS:** the `DirectContext` of `@oly/core` used to create entities through the `World` (`olyEntitySpawn`), without
  going through the engine. It now uses `olyDirectSpawn`, and C++ is the one that decides the tag. Direct `add`, `remove` and `destroy`
  still go through the `World`.
- **Scoped tasks:** those scheduled by the `Scheduler` (on the `Engine` or in a context) while `state() != NoState`.
  The task queue's slot (§4.6) gains the scope field.
- **Persist, after creating:** `context.persist(entity)` removes the tag. In a system, it enters the `CommandBuffer`, and the
  `add` and `remove` of the tag for the same spawn cancel out at flush (R-CB-4); in a direct context, it is a direct `remove`.
  Entity without the tag: does nothing (the ECS's `remove` is a no-op for an absent component). Dead entity: contract
  error, the same as `remove`. For tasks, `schedule().persist(handle)` returns `false` if the task is not
  pending, like `cancel`. There is no inverse (giving scope to something persistent), for lack of a use case.
- **Cleanup (item 3), [design decision]:** destroys the entities with the tag through a `QueryById` with
  `with: [StateScoped]`, created once; the entities are gathered in a reused vector and destroyed after the
  iteration, in O(scoped entities). Then it cancels the scoped tasks, and the discard of each one runs once (the
  TS releases the closure). A task scheduled by a discard during cleanup still sees the old state, so it has scope
  and is also cancelled: cleanup repeats until none is left, like the destruction of the `Engine`. Cleanup does not emit
  events (§4.4).

**`inState`**
- Systems and handlers can be restricted to one or more states. Without a restriction, they run in any state, including
  `NoState`. The restriction combines with `.whilePaused()` (§4.3).
- The handler checks the current state in each round; in the dispatch of item 6, the new state applies.
- C++: `engine.setSystemStates(system, {states...})` and `engine.setHandlerStates(handler, {states...})`; the empty list
  removes the restriction. TS: `.inState(...states)` on `SystemRegistration` and `HandlerRegistration`. During `frame`, a contract
  error.

**Exception and destruction, [design decision]**
- Recovery (§4.3) discards the pending request, together with the pending commands and events.
- An exception in a TS hook stops the switch where it was: in an `onExit`, the state does not change and cleanup does not run; in an
  `onEnter`, the state is already the new one and the following hooks do not run. What was already done stays.
- Destroying the `Engine` discards the tasks (§4.6). The entities stay in the `World`, which does not belong to the `Engine`.

**Schema and codegen.** `state("menu")` goes to `schema.states`, with `schemaIndex` in order. The name is unique across
types, events, actions and states and must be a valid C++ identifier. The schema hash includes the states, so an
outdated WASM gives `SchemaMismatchError`. The codegen generates `namespace states { inline constexpr oly::app::StateId
menu{0}; }` and a `schemaStates()`, like `schemaActions()`; a wrong name in a native system is a compile error.
More than 32 states is a generator error.

**C API**

```c
#define OLY_NO_STATE 0xFFFFFFFFu
#define OLY_INVALID_STATE_HOOK 0xFFFFFFFFu
#define OLY_SCRIPT_STATE_HOOK 4u   // olyHostInvokeScript(engine, kind, hookId, NULL, 0)
#define OLY_STATE_ENTER 0u
#define OLY_STATE_EXIT 1u

OLY_API void olyEngineRequestState(OlyEngine* engine, uint32_t state);
OLY_API uint32_t olyEngineState(OlyEngine* engine);
// stateMask 0 removes the restriction.
OLY_API void olyEngineSetSystemStates(OlyEngine* engine, uint32_t systemId, uint32_t stateMask);
OLY_API void olyEngineSetHandlerStates(OlyEngine* engine, uint32_t handlerId, uint32_t stateMask);
// `when` is OLY_STATE_ENTER or OLY_STATE_EXIT. OLY_INVALID_STATE_HOOK if there is no native hook with that name.
OLY_API uint32_t olyEngineAddNativeStateHook(OlyEngine* engine, uint32_t when, uint32_t state, const char* name);
OLY_API uint32_t olyEngineAddScriptStateHook(OlyEngine* engine, uint32_t when, uint32_t state, const char* name);
// Only inside a TS handler, task or hook; applies the current state's scope.
OLY_API void olyDirectSpawn(OlyEngine* engine, OlyEntity* out);
// Inside a TS system, deferred; in a TS direct context, direct. The bridge picks by the current context.
OLY_API void olyContextPersist(OlyEngine* engine, uint32_t index, uint32_t generation);
OLY_API int32_t olyEnginePersistTask(OlyEngine* engine, uint32_t slot, uint32_t generation);
```

**`@oly/core`.**
- `state(name)` in the schema returns a `State` handle.
- `engine.requestState(state)` and `engine.state` (`State | null`, `null` for `NoState`); `context.requestState`,
  `context.state` and `context.persist(entity)` in every context; `schedule.persist(handle)`.
- `engine.onEnter(state, name, hook)`, `engine.onExit(...)`, `addNativeOnEnter(state, name)` and
  `addNativeOnExit(state, name)`.
- `StateContext` with `fromState` and `toState` (`null` for `NoState`). Only valid during the call; outside it,
  `ContractError`.

**Contract errors** (`OLY_ASSERT` in C++; in TS, `ContractError` before reaching the assert): declaring a state during
`frame`, with a repeated name or beyond 32; `StateId` out of range (request, hook, `inState`); registering a hook or
`inState` during `frame`; `persist` of a dead entity; `olyDirectSpawn` outside a direct context; in TS, a state
handle from another schema and `StateContext` used outside the call.

### 4.8 Indicative usage

Sketch accepted as a usage baseline (indicative names, not normative):

```ts
const engine = await Engine.boot({ wasm: "game.wasm", fixedHz: 60 });
// menu, playing and gameOver come from the schema: state("menu"), ...
// move, aim, shoot and dash come from the schema: action("move", "axis2d"), action("aim", "pointer"), ...
engine.actions.bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });
engine.actions.bind(shoot, "MouseLeft");
engine.actions.bind(dash, "Space");
engine.requestState(menu);
engine.onEnter(playing, "startRound", (context) => {
  context.spawn([Player, Position, Velocity, Dash], { Dash: { ready: 1, speed: 600 } });
  context.schedule.every(5.0, spawnWave);
});
engine.addSystem(Phase.Update, "chasePlayer", chasePlayerSystem).inState(playing);
engine.addNativeSystem(Phase.Update, "move").inState(playing);
engine.on(EnemyDied, dropCoin);
engine.run();

function chasePlayerSystem(context: SystemContext) {
  const player = context.single(Position, { with: [Player] });
  context.query({ read: [Position], write: [Velocity], with: [Enemy] }).eachBlock((block) => {
    const position = block.read(Position);
    const velocity = block.write(Velocity);
    for (let row = 0; row < block.count; row++) {
      // generated accessors hide the column stride (§4.2)
    }
  });
}
```

```cpp
OLY_SYSTEM(move) {
  const float deltaTime = context.fixedDeltaTime();
  context.query<Position, const Velocity>().each([&](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}
```

### 4.9 Validation game (phase 7)

Phase goal (from the author): the game from §1.1 playable in the browser, using both languages in the same phases. When
the game needs something generic that the engine lacks, the gap is fixed in the engine during the phase, not worked around.

**Author's decisions**
- **Mixed game:** C++ does the per-entity gameplay (control, shooting, chasing, movement, collision); TS does render,
  HUD, key bindings, state hooks and the waves. This way the boundary is used as §2 prescribes: per system, per
  block and per batch, never per entity.
- **Generic engine gaps are fixed during the phase** (rule below).
- **esbuild** bundles the game's TS for the browser and serves it.
- **Fixed arena the size of the canvas**, no camera: world coordinates = canvas pixels, which is what
  `attachBrowserInput` already delivers for the pointer.
- **DOM-free game separate from the browser shell**, with a smoke test on node against the real WASM.

**Structure**

```
games/arena/
  CMakeLists.txt        oly_add_game(arena_game SCHEMA_JS … SOURCES native/*.cpp DEPENDS ecs app bridge) + ts.arena.smoke
  package.json          private package "arena", depends on @oly/core and @oly/client
  tsconfig.json         project reference, like the engine packages
  schema.ts             components, tags, events, actions and states
  native/               C++ systems and handlers (OLY_SYSTEM, OLY_HANDLER)
  src/game.ts           installGame(engine, options): registers everything, no DOM
  src/render.ts         render system that draws through a Drawer
  src/main.ts           browser shell: canvas, Drawer over Canvas 2D, runInBrowser, attachBrowserInput
  tests/smoke.test.ts   script on node against the WASM (ts.arena.smoke)
  web/index.html
  README.md             how to run, manual checklist and the phase list
```

- `games/CMakeLists.txt` enters the build under `OLY_BUILD_GAMES`, next to `engine/testing`. The WASM comes out in
  `build/<preset>/games/arena_game.mjs` + `.wasm`, in the `wasm-games` and `wasm-games-release` presets.
- `installGame(engine, { drawer, random })` returns a read-only object with the score (`kills`, `coins`, `score`),
  which the HUD and the test use. `Drawer` is the minimal drawing interface (clear, circle, rectangle, text): the
  `main.ts` passes one over `CanvasRenderingContext2D`, the test passes one that does not draw. `random: () => number` is
  `Math.random` in the browser and a fixed sequence in the test.

**Serving in the browser, [design decision] the names:**
- `esbuild` comes in as a root `devDependency`, with an exact version. It bundles `games/arena/dist/src/main.js` (output of
  `tsc -b`, which has already checked the types) into a single `main.js`. Emscripten's `.mjs` does not enter the bundle: it is loaded at
  runtime by `moduleFactoryFromUrl("./arena_game.mjs")`.
- `make game G=arena PRESET=wasm-games-release` assembles `build/<preset>/web/arena/` with `index.html`, `main.js`,
  `arena_game.mjs` and `arena_game.wasm`. The folder is static and can be published as is.
- `make serve G=arena` runs the esbuild server on that folder. `web` service in `docker-compose.yaml` (image from
  `Dockerfile.wasm`, port `8000:8000`): `docker compose run --rm --service-ports web make serve G=arena`.

**Schema** (game names, only in `games/arena/`)
- Components: `Position {x, y}`, `Velocity {x, y}`, `Collider {radius}`, `Health {value: i32}`, `Chaser {speed}`,
  `Weapon {interval, cooldown}`, `Dash {speed, duration, recharge, ready: u8, active: u8}`.
- Tags: `Player`, `Enemy`, `Bullet`, `Coin`, `Invulnerable`.
- Events: `EnemyDied {entity, x, y}`, `CoinCollected {coin: entity}`, `PlayerDied {entity}`.
- Actions: `move` (axis2d, WASD), `aim` (pointer), `shoot` (MouseLeft), `dash` (Space), `pause` (Escape), `confirm`
  (Enter and MouseLeft).
- States: `menu`, `playing`, `gameOver`.

**Fixed step** (C = C++ registered by name, T = TS; registration order = table order):

| Phase | System | States | What it does |
|---|---|---|---|
| updateBefore | T `startOnConfirm` | menu, gameOver | `confirm` → `requestState(playing)` |
| updateBefore | T `pauseOnPress`; T `resumeOnPress` (`whilePaused`) | playing | `pause` toggles the pause |
| updateBefore | C `steerPlayer` | playing | `move` → velocity; `dash` ready → impulse, and schedules in C++ (`Entity` payload) the end of the dash and the recharge |
| update | C `fireWeapon` | playing | `shoot` held + weapon cooldown → bullet in the direction of `aim` |
| update | C `chasePlayer` | playing | enemies point their velocity at the player |
| update | C `integrate` | playing | position += velocity × step |
| update | C `confine` | playing | keeps the player inside the arena and destroys bullets outside it. **[design decision]** It stays in `update`, not in `updateAfter`: destroys are applied at the end of the phase, so `collide` only sees bullets inside the arena and the player already confined. A bullet that leaves the arena hits nothing, not even an enemy on the edge, and only one system decides to destroy it for that reason (two `destroy` of the same entity in the same `CommandBuffer` are a contract error, R-CB-4) |
| updateAfter | C `collide` | playing | bullet×enemy: damage, bullet destroyed, `EnemyDied` with health ≤ 0. Enemy×player: without `Invulnerable`, damage, tag and a C++ task that removes it after 1 s; health ≤ 0 → `PlayerDied`. Player×coin: `CoinCollected` |

**Handlers** (step 4), following the reference flow of §4.4:
- `EnemyDied`: C `dropCoin` (chance of a coin at `x, y`) → T `countKill` (score) → C `destroyDead` (last,
  checks `isAlive`: two bullets in the same step generate two deaths).
- `CoinCollected`: T `collectCoin` (score and direct `destroy` from TS, checking `isAlive`).
- `PlayerDied`: T `endRound` (stores the final score and requests `gameOver`).

**Hooks and tasks**
- The game requests `menu` before running (§4.7, initial state).
- `onEnter(playing)` (T): resets the score, creates the player and schedules `every` for the waves (enemies at the edges, more each
  wave) and for coins scattered across the arena. Everything has `playing` scope: the switch to `gameOver` cleans entities and tasks
  with no cleanup code, and "play again" starts clean.
- `onExit(playing)` (T): `resume()`. The pause applies to the whole engine and only `resumeOnPress` (restricted to `playing`)
  lifts it; without the hook, any exit from a paused `playing` (Esc on the step of death, `requestState` from the host) would lock the
  next state.

**Render** (`render` phase, T): player, enemies, bullets and coins via `eachBlock` (one query per tag), HUD (score,
health, dash ready) and the menu, pause and game over text. **[design decision]** No interpolation: draws the position
from the last step.

**Done in the game because it belongs to the game:** render, collision (brute-force pairs, because there are dozens to a few hundred
entities), score, waves and randomness (C++ uses a local generator with a fixed seed). **[design decision]** The
game's numbers (speeds, radii, intervals, health 3) are unmeasured initial values, tuned by playing.

**`single` (already known gap), [design decision] the names and the shape of the return value are up to the plan:**
- §4.2 promises `single`, which does not exist. The game needs it in `chasePlayer` and `fireWeapon` (player position, in
  C++) and in the HUD (player health and dash, in TS).
- C++: `Query<Ts...>::single()` returns the `Entity` and the components of the only row; zero or more than one row is a contract
  error (`OLY_ASSERT`, with a case in `contract_death`). `trySingle()` returns empty when there is no row, because
  between `gameOver` and the next `playing` the player does not exist; more than one row is still an error.
- TS: `query.single()` and `query.trySingle()` with the same rule (`ContractError` before the assert). They cross the boundary
  a constant number of times per call (`olyQueryBegin`/`End`), not per entity.
- The `CLAUDE.md` glossary gains the `single` row.

**Rule for gaps discovered during the phase**
1. The piece is generic if another game would also use it. If it belongs to the game (render, collision, numbers), it stays in the game.
2. A generic piece becomes an engine task in the same phase: C++ first (a C++-only game must have it), then C API and
   TS, with tests in the module, death cases for new contracts and `CLAUDE.md`/spec updated.
3. Every gap goes into the phase list.

**The phase list** lives in `games/arena/README.md`, in three groups: **missing from the engine (added)** — the piece, why
the game needed it and the commit; **done in the game because it belongs to the game** — confirms the non-goals of §2; **worked around** —
something done outside the engine without fixing it. The goal is an empty last group; each item that stays in it says why the fix
was left for later.

**Boundary lint in games, [design decision]:** `oly.lint.boundaries` is only registered outside Emscripten, and
games are only configured in the `wasm-games*` presets; that is why a game's `DEPENDS` (including the fixture's) is never
checked. The lint is now also registered in the `wasm-games*` presets (the image has Python). The plan first checks
why it was left out of Emscripten.

## 5. Implementation phases

Each phase gets its own plan in `docs/superpowers/plans/` when it starts. Each phase ends with tests passing
(native and WASM where applicable) and does not regress the earlier ones nor the ECS.

| # | Phase | Delivers | Done when |
|---|------|---------|---------------|
| 1 | **Bridge** | WASM build of the engine with C API; TS package; schema + generator; spawn/destroy/get/set and `eachBlock` with views from TS | a TS test on node creates entities, iterates with views and the result matches the C++ side; boundary benchmark (§7) recorded |
| 2 | **Loop and phases** | C++ module with the loop (`frame`, `setHidden`, fixed step, render, pause), registration of C++ systems (by function and by name) and TS systems (callback) in the phases, `CommandBuffer` per phase shared by both languages; browser host in `@oly/client` (rAF + `visibilitychange`); loop steps 1, 2, 4 and 5 as empty slots | a native C++ test runs the whole loop without TS, with a fake clock; systems from both languages run in the expected order (TS test on node against the WASM); pause, hidden tab and `.whilePaused()` tested; §7 measurements recorded |
| 3 | **Events** | `event` in the schema and the generator; typed queues in WASM memory; emission on both sides; C++ handlers (by function and by name) and TS handlers per batch; type-by-type dispatch with cascade and limit | native C++ test of the reference flow (§4.4) without TS, ending with `checkInvariants` and the enemy destroyed; tests of order (type, registration, rounds), cascade, limit overflow (death case in C++), duplicate event, emission in the `render` phase, mixed C++ and TS handlers (TS test on node against the WASM), memory growth inside a TS handler and exception recovery; §7 measurement recorded |
| 4 | **Input actions** | `input` module (`ActionMap`, HID physical buttons, edge queue, capture in step 1); `action` in the schema and the generator; C API for bindings, host input and snapshot; reading through the contexts in C++ and TS; `attachBrowserInput` in `@oly/client` | `input.unit` without `Engine` covering edges, a press shorter than a step, multiple bindings, auto-repeat, `releaseAll`, axis (cancellation and diagonal) and rebinding; in `app`, one capture per step (including paused), edges only on the frame's first step and leaving the pause through an action in a `.whilePaused()` system; new death cases; TS test on node against the WASM with a TS system reading actions, short press and leaving the pause; HID table samples equal on both sides; `attachBrowserInput` tested with a fake environment |
| 5 | **Schedule** | task queue in `app` (`Scheduler`, `TaskContext`, `DirectContext`); `after`/`every`/cancel on the game clock, freezing on pause; payload and discard; C API for TS tasks; `schedule` on the `Engine` and in the `@oly/core` contexts | in `app`, native C++ with a fake clock and without TS: firing step (delay outside the step, `after(0)` inside and outside `frame`), order by due time and creation, `every` without drift and with an interval shorter than the step, task created in step 2 waiting for the next one, cancel before/during/after, pause, hidden tab, discarded steps, discard exactly once and never after `lastRun`, payload, direct changes with `checkInvariants`, `emit` in a task dispatched in the same step, tasks surviving recovery; new death cases; `bridge.c_api`; TS test on node against the WASM with interleaved C++ and TS tasks, exception and recovery, memory growth inside a TS task and an empty closure map on every path |
| 6 | **States** | `declareState`, request and transition in step 5, `onEnter`/`onExit` (by function and by name) with `StateContext`, `inState` on systems and handlers, scope through the `StateScoped` tag and on tasks, `persist`; `state` in the schema and the generator; C API for states, `olyDirectSpawn` and `olyContextPersist`; states in the `@oly/core` contexts | in `app`, native C++ with a fake clock and without TS: entry into the initial state, order `onExit` → cleanup → `onEnter`, last request wins, reentry, request in a hook taking effect in the next step, switch in a paused step, `inState` with `whilePaused`, scope of entities (system, handler, task, hook; direct spawn in the `World` persistent) and of tasks with discard exactly once, `persist` of both, discard that schedules during cleanup, event emitted in a hook dispatched in the same step, recovery (request discarded, exception in `onExit` and in `onEnter`) and `playing → gameOver → playing` with no leftovers in the `World` (`checkInvariants`); new death cases; `ts.codegen.unit`; `bridge.c_api`; TS test on node against the WASM with interleaved C++ and TS hooks, `inState` from TS, scoped direct spawn from TS, `persist`, empty closure map after the switch, exception in a TS hook and memory growth inside a TS hook |
| 7 | **Validation game** | the shooter in `games/arena/` (§4.9), with canvas render and simple collision done in the game; `single` in C++ and TS and the other generic gaps the game reveals; esbuild, `make game`/`make serve` and the `web` service; boundary lint in the `wasm-games*` presets | `ts.arena.smoke` on node against the WASM, with fake clock and input and `checkInvariants` at each stage: entry into `menu`, `confirm` → `playing` with one player, enemy killed by a shot with no leftover (duplicate deaths in the same step), coin collected, dash with end and recharge, pause freezing the world, death → `gameOver` with no scoped entities, and play again with the score reset; `single` tests and death cases on both sides; manual checklist in Chromium done; phase list in `games/arena/README.md` with the "worked around" group empty or justified; `$DC make test`, `$WC make games-test` and the `ts.*` in `wasm-games-release` green |

States (6) depends on the schedule (5) because of the scope of scheduled tasks. Events, actions and schedule (3–5) depend
only on 2 among themselves; the order above is the recommended one.

## 6. Tests and correctness

- C++ engine core: doctest tests in the current style, running on the native preset (`debug-asan`) and in WASM via node.
  New contract errors (cascade overflow, direct structural change during iteration etc.) get cases in
  `contract_death`.
- `World::checkInvariants` is still called in tests after engine operations that change structure.
- TS package: tests on node in the container (test runner at the implementer's discretion), against the real WASM, not a mock.
- Loop, schedule and states are testable without a browser: injectable clock and input source.
- The validation game is verified manually in the browser; no automated browser test is required.

## 7. Measurement

- Boundary benchmark (phase 1, repeated when the loop exists): a movement system over N entities in three
  versions, in the same run:
  1. C++ (native system);
  2. TS via the engine (`eachBlock` + strided views);
  3. hand-written TS (its own typed arrays, no engine) as the baseline.
  Report the ratios (2)/(3) and (2)/(1) per size, separating those that fit in cache. **[design decision]** No numeric
  target at this stage: the numbers are recorded and guide the decision on SoA per field (§4.2).
- Loop cost (phase 2), with K empty systems and 0 entities, in the same run:
  1. fixed cost of a step with K C++ systems (baseline) and with K TS systems: measures the C++→JS callback;
  2. K/2 C++ and K/2 TS systems **alternated** (`C,T,C,T…`) versus **grouped** (`C…C,T…T`) in the same phase: measures whether
     interleaving languages costs more than the number of callbacks explains;
  3. the boundary benchmark (above) repeated with the systems running inside the loop.
  No numeric target: the numbers say how much having the loop in C++ costs and guide whether grouping TS systems is worth it.
- Event cost (phase 3, **[design decision]**), in the same run, for N events of one type per step:
  1. a C++ handler versus a TS handler (cost of the callback per batch and of reading through the views);
  2. emission of N events by a C++ system versus by a TS system (cost of emission across the boundary).
  No numeric target: the numbers say whether emitting in batches from TS is worth it.
- Input actions (phase 4, **[design decision]**): no benchmark. Input is off the hot path: the boundary is
  crossed per input event (dozens per second), capture runs once per step over few actions and reading in
  TS is done through views, without crossing the boundary. Measuring would only make sense if a game had hundreds of actions.
- Schedule (phase 5, **[design decision]**): no benchmark. A game has dozens of tasks, the boundary is crossed once
  per firing or discard of a TS task and nothing is done per entity. The heap vs linear scan choice (§4.6) would only
  be measured if a game reached thousands of tasks.
- States (phase 6, **[design decision]**): no benchmark. A state switch is rare (screen change), the cost per
  step is one mask test per system and per handler, and the twin archetypes of the `StateScoped` tag only show up
  when the game creates the same combination with and without scope. Measuring would only make sense if a game showed relevant
  fragmentation (the ECS's `stats()`).
- Validation game (phase 7, **[design decision]**): no benchmark. The game has dozens to a few hundred
  entities, and the costs of the boundary, the loop and events were already measured in phases 1–3. If the game stutters in the
  browser, the investigation uses the Chromium profiler and the finding goes into the phase list (§4.9).
- Noisy environment (WSL2/Docker): only ratios from the same run, as in the ECS.
- Results versioned in `docs/benchmarks/` with an entry in the corresponding `README.md`.

## 8. What is left to the implementer

- Directory structure of the C++ engine core, of the TS package and of the example; package name.
- TS tools (bundler, dev server, test runner) and the Docker image/service that runs them.
- Schema format and generator language (§4.1).
- Shape of the imported JS function for the callback (`--js-library`, `EM_JS` or equivalent) and of the C++ system
  registration API, as long as the loop stays in the core (§4.3).
- Representation of `Entity` in TS (§4.2).
- Details of the exported C API and of the TS API, as long as usage stays close to the sketch (§4.8).
- Global vs per-type order in event dispatch (§4.4).
- Render and collision of the validation game (2D canvas is enough).
