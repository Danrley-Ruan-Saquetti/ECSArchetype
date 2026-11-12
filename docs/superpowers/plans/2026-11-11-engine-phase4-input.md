# Engine — Phase 4 (Input actions) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Input actions in the C++ core (`input` module with `ActionMap`, HID physical buttons, edge queue and capture at step 1), wired into the `Engine`, declared in the schema, exposed in the C API, read by the contexts in C++ and TS without crossing the boundary, and delivered by the browser with `attachBrowserInput`.

**Architecture:** A new module `oly::input` (`DEPENDS core`) holds declarations, bindings, physical state, edge queue and the snapshot (`ActionState[]`, 12 B per action). It knows neither the ECS nor the `Engine`. `app` now depends on `input`: the `Engine` holds an `ActionMap`, calls `capture()` at step 1 of every fixed step, `releaseAll()` in `setHidden(true)`, and locks declarations during `frame`. The `bridge` declares the schema's actions when the `OlyEngine` is created and exposes bindings, host input and the snapshot address. In TS, `action()` enters the schema, `ActionMap` (the same object in `engine.actions` and `context.actions`) validates and forwards, and reads the snapshot through the `Bridge` views. `@oly/client` gains `attachBrowserInput`.

**Tech Stack:** C++20, GCC 14 (native), Emscripten 3.1.74 + node 20.18 (`wasm` service), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (phase 4 of §5; all of §4.5, §4.3 for the step order and the host, §4.1 for the schema, §6, §7 "Input actions"). `§N` below refer to that spec.

## Global Constraints

- Branch `feat/engine-phase4-input` from `develop`. Commits, comments, assert and error messages in English; commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain only in Docker: `DC="docker compose run --rm dev"` (native, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). `wasm-games*` presets require `make ts` first.
- `-Wall -Wextra -Wpedantic -Werror` on every C++ target; TS `strict` (§3).
- Layers: `core ← input ← app ← bridge ← games` and `core ← ecs ← app`. `input` includes nothing from `oly/ecs` or `oly/app` (`oly.lint.boundaries` fails it). The ECS does not change in this phase.
- Full engine in C++ (§2): edges, auto-repeat, capture, axis, pointer, rebinding and release live in `oly::input::ActionMap`. TS only validates usage, translates W3C names into usages and forwards.
- Boundary: host input crosses once per input event; capture does not cross; reading an action in TS does not cross (snapshot views).
- No game concepts in `engine/runtime` or in `engine/packages`; game names only in `engine/testing/fixture/` and in the tests.
- No benchmark in this phase (§7).
- Glossary (`CLAUDE.md`): same verb in all three layers — `declareButton`/`declareAxis2d`/`declarePointer` (C++; in TS, `action(name, kind)` in the schema), `bind` (C `olyEngineBindButton`/`olyEngineBindAxis2d`), `clearBindings`, `isBound`, `buttonDown`, `buttonUp`, `pointerMove`, `releaseAll`, `button`/`axis2d`/`pointer` (reading). `oly` only in the C API.
- `CLAUDE.md` style: braces on every block, full names, data at the top of the class, one `namespace` per file, project includes with quotes after the standard library. Do not format by hand (`clang-format` hook). Comments only for a reason, an external constraint or a non-obvious contract.

### Decisions the spec leaves open (settled here)

- **`PhysicalButton`** is `struct PhysicalButton { uint32_t usage; }` with implicit `constexpr` constructors from `Key` and `MouseButton`, an `explicit` one from `uint32_t` (C API) and a defaulted `operator==`. An `enum class` does not convert on its own; the struct is what makes "implicitly converted into `PhysicalButton`" (§4.5) compile, and `Axis2dBinding{Key::W, Key::S, Key::A, Key::D}` work.
- **Table keys** (C++ `Key` and TS `buttonUsages`, page `0x07`): `KeyA`–`KeyZ` (0x04–0x1D), `Digit1`–`Digit9`, `Digit0` (0x1E–0x27), `Enter`, `Escape`, `Backspace`, `Tab`, `Space` (0x28–0x2C), `Minus`, `Equal`, `BracketLeft`, `BracketRight`, `Backslash` (0x2D–0x31), `Semicolon`, `Quote`, `Backquote`, `Comma`, `Period`, `Slash`, `CapsLock` (0x33–0x39), `F1`–`F12` (0x3A–0x45), `PrintScreen`, `ScrollLock`, `Pause`, `Insert`, `Home`, `PageUp`, `Delete`, `End`, `PageDown`, `ArrowRight`, `ArrowLeft`, `ArrowDown`, `ArrowUp` (0x46–0x52), `NumLock`, `NumpadDivide`, `NumpadMultiply`, `NumpadSubtract`, `NumpadAdd`, `NumpadEnter`, `Numpad1`–`Numpad9`, `Numpad0`, `NumpadDecimal` (0x53–0x63), `IntlBackslash`, `ContextMenu` (0x64–0x65), `ControlLeft`, `ShiftLeft`, `AltLeft`, `MetaLeft`, `ControlRight`, `ShiftRight`, `AltRight`, `MetaRight` (0xE0–0xE7). Mouse (page `0x09`): `MouseLeft` 1, `MouseRight` 2, `MouseMiddle` 3. The C++ name is the W3C name without the `Key` prefix of the letters (`Key::W`, `Key::Digit1`, `Key::ArrowUp`); `MouseButton::Left/Right/Middle`.
- **Table samples** (the same in the C++ and TS tests): `KeyA` 0x0007'0004, `KeyW` 0x0007'001A, `KeyZ` 0x0007'001D, `Digit1` 0x0007'001E, `Digit0` 0x0007'0027, `Enter` 0x0007'0028, `Escape` 0x0007'0029, `Space` 0x0007'002C, `F1` 0x0007'003A, `F12` 0x0007'0045, `ArrowRight` 0x0007'004F, `ArrowLeft` 0x0007'0050, `ArrowDown` 0x0007'0051, `ArrowUp` 0x0007'0052, `ControlLeft` 0x0007'00E0, `ShiftLeft` 0x0007'00E1, `AltLeft` 0x0007'00E2, `MetaLeft` 0x0007'00E3, `ShiftRight` 0x0007'00E5, `MouseLeft` 0x0009'0001, `MouseRight` 0x0009'0002, `MouseMiddle` 0x0009'0003.
- **Physical state in two stages.** `hostPressed_` changes at the time of the host call and decides what enters the queue: `buttonDown` of a button already in `hostPressed_` is ignored (auto-repeat), and so is `buttonUp` of a button not in `hostPressed_` (a keyup arriving after a `releaseAll` from `blur`). `capturedPressed_` is the state the capture replays; at the end of every capture, the two are equal. Both are `std::vector<PhysicalButton>` with linear search: few buttons are pressed at the same time.
- **Capture** (determines `pressed`/`released` with several bindings; the rest comes out of the tests):
  ```
  for each button action: held = some bound button is in capturedPressed_; pressed = released = false
  for each edge in the queue, in order: apply the edge to capturedPressed_;
    for each button action bound to that button: now = some bound button in capturedPressed_;
      if !held && now: pressed = true;  if held && !now: released = true;  held = now
  clear the queue; axes and pointers come from the final state
  ```
  Recomputing `held` from scratch from `capturedPressed_` is what makes rebinding take effect on the next capture without phantom edges: binding `Space` to an action with `Space` already pressed gives `held` without `pressed`; removing the binding gives `held` false without `released`.
- **Bindings:** binding the same button (or the same `Axis2dBinding`) to the same action again does not duplicate. A button can be bound to several actions; all of them react. Axis with several bindings: each direction is the OR across the bindings (W or ↑ = up). `clearBindings` on a pointer action does nothing.
- **Snapshot:** `ActionState { float x; float y; uint32_t flags; }`, bits `ActionPressed = 1u << 0`, `ActionHeld = 1u << 1`, `ActionReleased = 1u << 2`, `ActionValid = 1u << 3`. Button: `x = y = 0`. Axis: `flags = 0`. Pointer: last position and `ActionValid` after the first `pointerMove`; all pointer actions get the same source. Before the first capture, everything is zero. `snapshot()` is valid until the next declaration; in `OlyEngine` all actions are declared at creation, so the C API pointer is stable until `olyEngineDestroy`.
- **Declaration during `frame`:** `ActionMap` has `setDeclarationsLocked(bool)`; the `Engine` locks it when entering `frame` (together with `inFrame_ = true`) and unlocks it on exit and in `recoverFromInterruptedFrame`. The assert message says "during frame".
- **`ActionMap::declare(ActionKind, name)`** is public (the `bridge` uses it with the kind coming from the schema); `declareButton/declareAxis2d/declarePointer` delegate to it and return the typed handle.
- **Non-finite pointer:** `pointerMove` with non-finite `x` or `y` is an `OLY_ASSERT` in C++ and a `ContractError` in TS.
- **Action name in the schema:** camelCase (`/^[a-z][A-Za-z0-9]*$/`) and not a C++ reserved word (the same list as for fields), so that `actions::<name>` compiles. The `actions` namespace is only generated if there are actions; the generated header only includes `oly/input/action_map.hpp` in that case.
- **Hash:** today's canonical form followed by `@name:kind;` per action. A schema without actions keeps the current hash.
- **Action kind in the C API:** `OLY_ACTION_BUTTON 0u`, `OLY_ACTION_AXIS2D 1u`, `OLY_ACTION_POINTER 2u` = `static_cast<uint32_t>(ActionKind)`.
- **TS:** a single `ActionMap` per `Engine`, exposed as `engine.actions` and `context.actions` (the same object, as in C++). The snapshot address is read once when the `Engine` is created.
- **`attachBrowserInput`:** `pointerdown` sends `pointerMove` before `buttonDown` (the click already goes out with the right aim); `contextmenu` on the canvas gets `preventDefault` if `MouseRight` is bound (the right-button menu comes from that event, not from `pointerdown`); `PointerEvent.button` 0 → `MouseLeft`, 1 → `MouseMiddle`, 2 → `MouseRight`, others ignored; a canvas with `rect.width` or `rect.height` 0 ignores the pointer event.
- **Out of scope:** gamepad, pointer with binding, `Engine.on`/states restricted to actions, benchmark.

## Review Focus

1. **Canvas with size 0 on screen** (`display: none`, freshly opened tab) receiving `pointermove`: the conversion divides by zero; the expected behaviour is to ignore the event, with no exception in the listener and no `NaN` in the snapshot (Task 6).
2. **Real `keyup` arriving after the `releaseAll` from `blur`** (the player holds W, switches window, releases W out there and comes back): only one `released`, no new edge and no assert (Task 1).
3. **Same button in two actions and repeated binding** (`Space` in `jump` and in `confirm`; `bind(jump, Space)` twice): both actions react; the repeated one does not produce two `pressed` nor prevent the `released` (Task 1).
4. **Rebinding with the button already pressed** (key menu): neither a phantom `pressed` nor a phantom `released` on the next capture; `held` reflects the new binding (Task 1, and in TS in Task 5).
5. **WASM memory grows before a TS system reads actions** (a native system that creates many entities first): reading the snapshot is still correct after `sync()` (Task 5).

---

### Task 1: `input` module — physical buttons, `ActionMap` and capture

**Files:**
- Create: `engine/runtime/input/CMakeLists.txt`, `engine/runtime/input/include/oly/input/{physical_button.hpp,action_map.hpp}`, `engine/runtime/input/src/action_map.cpp`, `engine/runtime/input/tests/{CMakeLists.txt,physical_button.cpp,action_map.cpp,contract_death.cpp}`
- Modify: `engine/runtime/CMakeLists.txt` (`add_subdirectory(input)` after `core`, before `ecs`)

**Interfaces:**
- Consumes: `OLY_ASSERT` (`oly/core/assert.hpp`).
- Produces (namespace `oly::input`):
  - `physical_button.hpp`: `enum class Key : uint32_t` and `enum class MouseButton : uint32_t` with the table from "Decisions"; `struct PhysicalButton` as in "Decisions".
  - `action_map.hpp`: `using ActionId = uint32_t;` `enum class ActionKind : uint8_t { Button, Axis2d, Pointer };` `struct ButtonAction { ActionId id; };` `struct Axis2dAction { ActionId id; };` `struct PointerAction { ActionId id; };` `struct ButtonState { bool pressed; bool held; bool released; };` `struct Vector2 { float x; float y; };` `struct PointerState { float x; float y; bool valid; };` `struct Axis2dBinding { PhysicalButton up, down, left, right; };` `struct ActionState { float x; float y; uint32_t flags; };` (`static_assert` of 12 B and offsets 0/4/8) and the constants `ActionPressed/ActionHeld/ActionReleased/ActionValid`.
  - `class ActionMap` with the methods from §4.5 (`declareButton`, `declareAxis2d`, `declarePointer`, both `bind`, `clearBindings`, `isBound`, `buttonDown`, `buttonUp`, `pointerMove`, `releaseAll`, `capture`, `button`, `axis2d`, `pointer`, `snapshot`) plus `ActionId declare(ActionKind kind, std::string_view name)`, `uint32_t actionCount() const noexcept`, `ActionKind kind(ActionId action) const`, `std::string_view name(ActionId action) const`, `void setDeclarationsLocked(bool locked) noexcept`.
  - Contracts (`OLY_ASSERT`, message with the operation and the action name when there is one): repeated name (`already declared`), declaring while locked (`during frame`), `ActionId` out of range (`out of range`), handle of another kind (`kind`), usage `0` (`usage 0`), non-finite pointer (`non-finite`).

- [ ] **Step 1: Branch** `git switch -c feat/engine-phase4-input`.

- [ ] **Step 2: Failing tests.** `tests/CMakeLists.txt`: `oly_add_test(MODULE input NAME unit SOURCES physical_button.cpp action_map.cpp)` and `oly_add_contract_death(MODULE input SOURCE contract_death.cpp CASES ...)`. The module's `CMakeLists.txt`: `oly_add_module(NAME input DEPENDS core)` + `add_subdirectory(tests)` under `OLY_BUILD_TESTS`, like `app`. No `Engine` or `World`: each case declares, binds, delivers input and calls `capture()`.
```cpp
TEST_CASE("HID table samples") { /* each sample from "Decisions" */ CHECK(PhysicalButton(Key::W).usage == 0x0007'001Au); CHECK(PhysicalButton(MouseButton::Left).usage == 0x0009'0001u); }
TEST_CASE("pressed on the edge step, held while pressed, released on release") { /* down; capture → {1,1,0}; capture → {0,1,0}; up; capture → {0,0,1}; capture → {0,0,0} */ }
TEST_CASE("a tap shorter than a step is not lost") { /* down, up; capture */ CHECK(state == ButtonState{true, false, true}); /* capture */ CHECK(state == ButtonState{false, false, false}); }
TEST_CASE("empty queue keeps the state") { /* down; capture; capture; capture */ CHECK(held && !pressed); }
TEST_CASE("auto-repeat is ignored") { /* down, down, down; capture */ CHECK(pressed); /* up; capture */ CHECK(released && !held); }
TEST_CASE("two keys on the same action: releasing one does not produce released") { /* bind Space and ShiftLeft; down Space, down Shift; capture; up Space; capture */ CHECK(!released && held); /* up Shift; capture */ CHECK(released); }
TEST_CASE("same button on two actions: both react") { CHECK(jump.pressed && confirm.pressed); }
TEST_CASE("repeated binding does not duplicate") { /* bind(jump, Space) 2×; down, up; capture */ CHECK(state == ButtonState{true, false, true}); }
TEST_CASE("releaseAll releases everything and the late keyup is ignored") { /* down W; capture; releaseAll; up W; capture */ CHECK(released); /* capture */ CHECK(!released && !held); }
TEST_CASE("axis: WASD, cancellation and normalized diagonal") { /* W → (0,-1); W+S → (0,0); W+D → (0.70710677f, -0.70710677f) with doctest::Approx */ }
TEST_CASE("axis with two bindings: each direction is OR") { /* WASD and arrows; A and ArrowRight → (0,0); W and ArrowUp → (0,-1) */ }
TEST_CASE("pointer: invalid until the first pointerMove, then the last position") { /* capture → valid false; move(1,2), move(3,4); capture → {3,4,true} */ }
TEST_CASE("rebinding with the button pressed produces no edges") { /* down Space without binding; capture; bind(jump, Space); capture */ CHECK(state == ButtonState{false, true, false}); /* clearBindings; capture */ CHECK(state == ButtonState{false, false, false}); }
TEST_CASE("rebinding only takes effect from the next capture") { /* bind and down before the 1st capture; button() before capture */ CHECK(!pressed); }
TEST_CASE("isBound works for button and axis") { CHECK(isBound(Key::Space)); CHECK(isBound(Key::D)); CHECK(!isBound(Key::Q)); }
TEST_CASE("snapshot: one ActionState per action in declaration order with the bits") { /* pressed button and valid pointer */ CHECK(snapshot[0].flags == (ActionPressed | ActionHeld)); CHECK(snapshot[2].flags == ActionValid); }
```
`contract_death.cpp` (same protocol as `app`):

| Case | Action | Substrings |
|---|---|---|
| `declare_duplicate_name` | `declareButton("jump")` twice | `declare`, `jump`, `already declared` |
| `declare_while_locked` | `setDeclarationsLocked(true)`; `declareButton("jump")` | `declare`, `during frame` |
| `bind_wrong_kind` | axis `ActionId` used as `ButtonAction` in `bind` | `bind`, `kind` |
| `action_out_of_range` | `clearBindings(7)` with 1 action | `clearBindings`, `out of range` |
| `button_usage_zero` | `buttonDown(PhysicalButton(0u))` | `buttonDown`, `usage 0` |
| `pointer_not_finite` | `pointerMove(NAN, 0)` | `pointerMove`, `non-finite` |

- [ ] **Step 3: See it fail** — `$DC make test T='^input\.'` → configuration/compilation error (`oly/input/action_map.hpp` does not exist).

- [ ] **Step 4: Implement** `ActionMap` according to "Decisions" (edge queue `std::vector<Edge>` with `Edge { PhysicalButton button; bool down; }`, per-action bindings in vectors, capture by the algorithm in "Decisions"). The diagonal uses `1/√2` as a constant.

- [ ] **Step 5: See it pass** — `$DC make test` (includes `oly.lint.boundaries`, which now sees the `input` module) and `$WC make test PRESET=wasm-debug T='^input\.'`.

- [ ] **Step 6: Commit** `feat: input module with ActionMap, HID buttons and per-step capture`.

---

### Task 2: `ActionMap` in the `Engine` — capture at step 1, hidden tab and context

**Files:**
- Create: `engine/runtime/app/tests/actions.cpp`
- Modify: `engine/runtime/app/CMakeLists.txt` (`DEPENDS input ecs core`), `engine/runtime/app/include/oly/app/{engine.hpp,context.hpp}`, `engine/runtime/app/src/{engine.cpp,context.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp}`

**Interfaces:**
- Consumes: `oly::input::ActionMap` from Task 1.
- Produces: `input::ActionMap& Engine::actions() noexcept` (and the `const` version); `input::ActionMap& EngineContext::actions() const noexcept`. `captureActions()` is no longer empty; `setHidden(true)` calls `releaseAll()`; declarations locked during `frame`.

- [ ] **Step 1: Failing tests** in `tests/actions.cpp` (`oly_add_test(MODULE app NAME actions SOURCES actions.cpp SUPPORT engine/runtime/ecs/tests/support)`). Fixture like `pause.cpp` (`frame(0)` only records the time; `step()` advances 1/60). Systems write `context.actions().button(jump)` into a vector via `userData`. `CHECK_INVARIANTS(world)` at the end.
```cpp
TEST_CASE("one capture per step, also while paused") { /* pause(); whilePaused system; down Space; step → pressed; step → !pressed && held */ }
TEST_CASE("edges only on the first step of the frame") { /* down, up; frame that runs 3 steps */ CHECK(states == {{1,0,1},{0,0,0},{0,0,0}}); }
TEST_CASE("a frame without a step keeps the edge for the next step") { /* down, up; frame advancing < 1 step; then frame that completes the step */ CHECK(pressed on the only step); }
TEST_CASE("leaving pause through an action in a whilePaused system") { /* whilePaused system: if button(togglePause).pressed, context.resume(); pause(); down; step; up */ CHECK(!engine.paused()); /* next step runs the normal system */ }
TEST_CASE("setHidden(true) releases the buttons") { /* down Space; step; setHidden(true); setHidden(false); frame(time only); step */ CHECK(released && !held); }
TEST_CASE("a system rebinds and the change takes effect on the following capture") { /* system in Update: bind(jump, Key::Q) on the 1st step; Q pressed before */ CHECK(step 1: !held); CHECK(step 2: held && !pressed); }
TEST_CASE("a handler reads the actions through the same context") { /* Ping event emitted in Update; handler records button(jump).pressed */ CHECK(pressed in the handler of the edge step); }
```
`contract_death.cpp`: case `declare_action_during_frame` (a system calls `context.actions().declareButton("late")`; substrings `declareButton`, `during frame`), in the `CASES` list.

- [ ] **Step 2: See it fail** — `$DC make test T='^app\.'` → compilation error (`actions()` does not exist).

- [ ] **Step 3: Implement.** Member `input::ActionMap actions_` in the `Engine` (in the data block, next to the event ones). `frame`: `actions_.setDeclarationsLocked(true)` when setting `inFrame_` and `false` when clearing it; `recoverFromInterruptedFrame` also unlocks. Update the comment on the empty stages (only `runDueSchedule` and `applyStateTransition` remain) and the one on `setHidden` (it now releases the buttons).

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: action capture in the Engine fixed step and release on hidden tab`.

---

### Task 3: `action` in the schema, the generator and the WASM check

**Files:**
- Modify: `engine/packages/core/src/{schema.ts,schema_hash.ts,runtime.ts,module.ts,index.ts,internal.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/{schema.test.ts,surface.test.ts}`, `engine/tools/codegen/src/emit_cpp.ts`, `engine/tools/codegen/tests/generator.test.ts`, `engine/runtime/bridge/CMakeLists.txt` (`DEPENDS app input ecs core`), `engine/runtime/bridge/include/oly/bridge/{schema_registry.hpp,c_api.hpp}`, `engine/runtime/bridge/src/c_api.cpp`, `engine/runtime/bridge/tests/{test_schema_registry.cpp,c_api.cpp}`, `engine/testing/tests/wasm/load.test.ts`

**Interfaces:**
- Produces:
  - TS (`@oly/core`): `type ActionKind = "button" | "axis2d" | "pointer";` `interface ActionType<Name extends string = string, Kind extends ActionKind = ActionKind> { readonly kind: "action"; readonly actionKind: Kind; readonly name: Name; readonly schemaIndex: number }`; `type ButtonAction = ActionType<string, "button">`, `Axis2dAction`, `PointerAction` likewise; `function action<const Name extends string, const Kind extends ActionKind>(name: Name, kind: Kind): ActionType<Name, Kind>` (`ContractError` for a name outside the pattern in "Decisions" or an unknown `kind`); `Schema` gains `readonly actions: readonly ActionType[]`; `defineSchema` accepts `SchemaType | EventType | ActionType`.
  - Internal TS: `actionIndexOf(runtime: Runtime, action: ActionType): number` (in `runtime.ts`, exported by `internal.ts`; `ContractError` "action does not belong to this runtime's schema" like `eventIndexOf`).
  - C++ (`oly/bridge/schema_registry.hpp`): `struct SchemaAction { std::string_view name; oly::input::ActionKind kind; };` `std::span<const SchemaAction> schemaActions();` (defined by the generated code).
  - C API: `#define OLY_ACTION_BUTTON 0u`, `OLY_ACTION_AXIS2D 1u`, `OLY_ACTION_POINTER 2u`; `uint32_t olySchemaActionCount()`, `uint32_t olySchemaActionKind(uint32_t actionIndex)` (index unchecked, like `olySchemaEventSize`). `static_assert` that the three values match `ActionKind`.
  - Generator: in `schema.generated.hpp`, if there are actions, `#include "oly/input/action_map.hpp"` and `namespace actions { inline constexpr oly::input::ButtonAction fire{0}; ... }` (handle type from the `kind`, value = `schemaIndex`); in `schema_registry.generated.cpp`, `schemaActions()` with `{"fire", oly::input::ActionKind::Button}` per action, in order, or `return {};`.

- [ ] **Step 1: Failing tests.**
  - `schema.test.ts`: `action("Fire", "button")`, `action("for", "button")` and `action("fire", "trigger" as never)` throw `ContractError`; `defineSchema([A, fire, Hit, move])` gives `actions` = `[fire, move]` with `schemaIndex` 0 and 1, without touching `types`/`events`; a name repeated between an action and a component/event throws `ContractError`; the existing hash tests pass unchanged (schema without actions); adding an action changes the hash, and so does changing an action's `kind`.
  - `generator.test.ts`: a schema header with actions contains `#include "oly/input/action_map.hpp"`, `inline constexpr oly::input::ButtonAction fire{0};` and `inline constexpr oly::input::Axis2dAction move{1};`; a header without actions does not contain `oly/input`; the registry contains `{"fire", oly::input::ActionKind::Button}` before `{"move", oly::input::ActionKind::Axis2d}`.
  - `c_api.cpp` (bridge): `olySchemaActionCount() == 3`, `olySchemaActionKind(0) == OLY_ACTION_BUTTON`, `(1) == OLY_ACTION_AXIS2D`, `(2) == OLY_ACTION_POINTER` (`test_schema_registry.cpp` now defines `schemaActions()` with `bridgeJump`, `bridgeMove`, `bridgeAim`, in that order).
  - `load.test.ts`: a TS schema with one more action than the WASM → `SchemaMismatchError` with "actions in WASM"; same number of actions with a different `kind` → `SchemaMismatchError` with the action name (build it like the event mismatch cases).
  - `surface.test.ts` / `public-exports.txt`: `action`, `ActionKind`, `ActionType`, `ButtonAction`, `Axis2dAction`, `PointerAction`.

- [ ] **Step 2: See it fail** — `$WC make ts` → type error (`action` does not exist); `$DC make test T='^bridge\.'` → compilation error (`schemaActions`).

- [ ] **Step 3: Implement.** `canonicalSchema(types, events, actions = [])` appends `@name:kind;`. `loadRuntime` checks `_olySchemaActionCount() === schema.actions.length` and the kind of each action (`["button","axis2d","pointer"].indexOf(actionKind)`); the `SchemaMismatchError` message gains "actions in WASM: n, in TS: m" and the first mismatch can be an action. `OlyModule` gains the two exports.

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make games-test` (the fixture has no actions yet: generated `schemaActions()` is empty).

- [ ] **Step 5: Commit** `feat: action in the schema, the generator and the WASM check`.

---

### Task 4: Actions in the C API — schema declaration, bindings, host input and snapshot

**Files:**
- Modify: `engine/runtime/bridge/include/oly/bridge/engine_api.hpp`, `engine/runtime/bridge/src/engine_api.cpp`, `engine/runtime/bridge/tests/{engine_api.cpp,contract_death.cpp,CMakeLists.txt}`

**Interfaces:**
- Consumes: Task 1 (`ActionMap::declare/kind/actionCount`, `ActionState`, flag constants), Task 2 (`Engine::actions()`), Task 3 (`schemaActions()`).
- Produces (C API, `extern "C"`, `OLY_API`): the nine functions from §4.5 (`olyEngineBindButton`, `olyEngineBindAxis2d`, `olyEngineClearBindings`, `olyEngineIsBound`, `olyEngineButtonDown`, `olyEngineButtonUp`, `olyEnginePointerMove`, `olyEngineReleaseAll`, `olyEngineActionSnapshot`) with exactly those signatures; `struct OlyActionState { float x; float y; uint32_t flags; };` and `#define OLY_ACTION_PRESSED 1u`, `OLY_ACTION_HELD 2u`, `OLY_ACTION_RELEASED 4u`, `OLY_ACTION_VALID 8u`, with `static_assert` of size, offsets and values against `oly::input::ActionState` and the constants. `olyEngineCreate` declares, after the events, `schemaActions()` in order (`OLY_ASSERT` that the returned id is the index). `olyEngineBindButton`/`BindAxis2d` check `action < actionCount()` and the kind (`OLY_ASSERT` with the function name, `action` and the expected kind); `olyEngineClearBindings` only checks the range.

- [ ] **Step 1: Failing tests** in `tests/engine_api.cpp` (no script: a native system `OLY_SYSTEM(bridgeTestReadJump)` writes `context.actions().button(ButtonAction{0})` into a global):
```cpp
TEST_CASE("schema actions are declared at creation, in order") { CHECK(engineOf(engine).actions().actionCount() == 3); CHECK(name(0) == "bridgeJump"); CHECK(kind(2) == ActionKind::Pointer); }
TEST_CASE("bind, press and read through the snapshot") { /* BindButton(0, Space); ButtonDown(Space); 1 step */ CHECK(olyEngineActionSnapshot(engine)[0].flags == (OLY_ACTION_PRESSED | OLY_ACTION_HELD)); CHECK(native system saw pressed); }
TEST_CASE("axis and pointer through the C API") { /* BindAxis2d(1, W,S,A,D); ButtonDown(D); PointerMove(5, 6); 1 step */ CHECK(snapshot[1].x == 1.0f); CHECK(snapshot[2] == {5, 6, OLY_ACTION_VALID}); }
TEST_CASE("isBound, clearBindings and releaseAll") { CHECK(olyEngineIsBound(engine, Space) == 1); /* ClearBindings(0) */ CHECK(olyEngineIsBound(engine, Space) == 0); /* rebind, down, step, ReleaseAll, step */ CHECK(released); }
TEST_CASE("the snapshot address does not change between frames") { CHECK(olyEngineActionSnapshot(engine) == address read before 3 steps with input); }
```
`contract_death.cpp` + `CASES`: `bind_button_wrong_kind` (`olyEngineBindButton(engine, 1, Space)`; substrings `olyEngineBindButton`, `kind`), `action_out_of_range` (`olyEngineClearBindings(engine, 9)`; `olyEngineClearBindings`, `out of range`).

- [ ] **Step 2: See it fail** — `$DC make test T='^bridge\.'` → compilation error.

- [ ] **Step 3: Implement** the functions as forwarding to `engine->engine.actions()`, with an explicit `PhysicalButton(button)` and `reinterpret_cast<const OlyActionState*>(snapshot().data())`.

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: actions in the C API with bindings, host input and stable snapshot`.

---

### Task 5: Actions in `@oly/core` — `ActionMap`, W3C → HID table and reading in the contexts

**Files:**
- Create: `engine/packages/core/src/{buttons.ts,action_map.ts}`, `engine/packages/core/tests/unit/buttons.test.ts`, `engine/testing/tests/wasm/actions.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,context.ts,bridge.ts,module.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/{schema.ts,systems.cpp}`, `engine/testing/tests/wasm/fixture.ts`

**Interfaces:**
- Consumes: C API from Task 4; `ActionType`, `actionIndexOf` from Task 3.
- Produces (`@oly/core`):
  - `buttons.ts`: `const buttonUsages` (W3C name → usage, the table from "Decisions", `as const`); `type ButtonName = keyof typeof buttonUsages;` `function isButtonName(name: string): name is ButtonName`.
  - `action_map.ts`: `interface ButtonState { readonly pressed: boolean; readonly held: boolean; readonly released: boolean }`, `interface Vector2 { readonly x: number; readonly y: number }`, `interface PointerState { readonly x: number; readonly y: number; readonly valid: boolean }`, `interface Axis2dBinding { readonly up: ButtonName; readonly down: ButtonName; readonly left: ButtonName; readonly right: ButtonName }`; `class ActionMap` with `button(action: ButtonAction): ButtonState`, `axis2d(action: Axis2dAction): Vector2`, `pointer(action: PointerAction): PointerState`, `bind(action: ButtonAction, button: ButtonName): void`, `bind(action: Axis2dAction, binding: Axis2dBinding): void`, `clearBindings(action: ActionType): void`, `isBound(button: ButtonName): boolean`, `buttonDown(button: ButtonName): void`, `buttonUp(button: ButtonName): void`, `pointerMove(x: number, y: number): void`, `releaseAll(): void`. Internal constructor `(runtime, bridge, engineHandle, snapshotAddress, assertUsable: (operation: string) => void)`.
  - Errors (`ContractError`, before calling the C API): button name outside the table; action from another schema; `actionKind` different from the method (untyped JS); non-finite `pointerMove`; released `Engine`.
  - `Engine.actions: ActionMap` (getter; `ContractError` with the `Engine` released); `EngineContext.actions: ActionMap` (getter; `ContractError` outside the call). Both return the same object.
  - `Bridge`: `engineBindButton`, `engineBindAxis2d`, `engineClearBindings`, `engineIsBound`, `engineButtonDown`, `engineButtonUp`, `enginePointerMove`, `engineReleaseAll`, `engineActionSnapshot`; those that can allocate (binding, host input) end with `sync()`. Reading: `bridge.heap.f32`/`u32` at `snapshotAddress + 12 * actionIndex`, without calling an export.
  - `OlyModule`: the nine `_olyEngine*` from Task 4.
  - `public-exports.txt`: + `ActionMap`, `Axis2dBinding`, `ButtonName`, `ButtonState`, `PointerState`, `Vector2`, `isButtonName`.
- Fixture: `fire = action("fire", "button")`, `move = action("move", "axis2d")`, `aim = action("aim", "pointer")`, `togglePause = action("togglePause", "button")`, in that order, at the end of `defineSchema`. In `systems.cpp`: `OLY_SYSTEM(countFirePresses)` adds 1 to a static counter when `context.actions().button(actions::fire).pressed`; export `uint32_t fixtureTakeFirePressCount()` (returns and resets it).

- [ ] **Step 1: Failing tests.**
  - `buttons.test.ts`: each sample from "Decisions" (`buttonUsages.KeyW === 0x0007_001a`, ..., `MouseMiddle === 0x0009_0003`); `isButtonName("KeyW")` true, `isButtonName("w")` and `isButtonName("Unidentified")` false.
  - `engine/testing/tests/wasm/actions.test.ts` (helpers `withEngine` and `stepper` as in `engine_pause.test.ts`; `checkInvariants() === null` at the end):
```ts
test("before the first capture everything is false and the pointer is invalid", ...)          // read outside frame right after createEngine
test("a TS system reads pressed, held and released", ...)                                       // bind(fire, "Space"); buttonDown; step; step; buttonUp; step
test("a tap shorter than a step shows up in a single step", ...)                                // down+up before step → {true,false,true}; next step → all false
test("C++ and TS systems see the same snapshot in the same step", ...)                          // addNativeSystem("countFirePresses") and TS system; 1 tap → fixtureTakeFirePressCount() === 1 and TS saw pressed
test("WASD axis with normalized diagonal and pointer", ...)                                     // W+D → x ≈ 0.7071, y ≈ -0.7071 (f32); pointerMove(10, 20) → {10, 20, true}
test("leaving pause through togglePause in a whilePaused system", ...)
test("rebinding from inside a system takes effect on the next capture, without phantom edges", ...) // KeyQ already pressed
test("memory grows before the TS system and reading is still correct", ...)                     // addNativeSystem("spawnBatch") before the TS system that reads fire; buffer changes; pressed read correctly
test("isBound and clearBindings", ...)
test("errors: button outside the table, action from another schema, wrong kind, non-finite pointer, released Engine", ...) // ContractError in each one
test("context.actions outside the system call is a ContractError", ...)
```
  - `surface.test.ts`: `ActionMap` members exactly as in Produces; `Engine` and `SystemContext`/`HandlerContext` gain `actions`.

- [ ] **Step 2: See it fail** — `$WC make games-test` → type error in `tsc` (`ActionMap` does not exist).

- [ ] **Step 3: Implement.** The `Engine` creates the `ActionMap` in the constructor (`bridge.engineActionSnapshot(handle)` once) and passes it to the contexts it creates. `button()` builds a new object on every read (§4.5: few per step). `bind`/`clearBindings` may be called during `frame` (rebinding from inside a system).

- [ ] **Step 4: See it pass** — `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (no asserts: only the TS validations catch the errors) and `$DC make test`.

- [ ] **Step 5: Commit** `feat: actions in @oly/core with snapshot reads and W3C to HID table`.

---

### Task 6: `attachBrowserInput` in `@oly/client` and documentation

**Files:**
- Create: `engine/packages/client/src/browser_input.ts`, `engine/packages/client/tests/browser_input.test.ts`
- Modify: `engine/packages/client/src/index.ts`, `CLAUDE.md`

**Interfaces:**
- Consumes: `ActionMap`, `ButtonName`, `isButtonName` from `@oly/core` (public).
- Produces (`@oly/client`):
  - `interface InputTarget { readonly actions: Pick<ActionMap, "buttonDown" | "buttonUp" | "pointerMove" | "releaseAll" | "isBound"> }` (the `Engine` satisfies it).
  - `interface InputCanvas { readonly width: number; readonly height: number; getBoundingClientRect(): { left: number; top: number; width: number; height: number }; addEventListener`/`removeEventListener` for `"pointerdown"` and `"contextmenu"` }` and `interface InputEnvironment { readonly window: { addEventListener`/`removeEventListener` for `"keydown"`, `"keyup"`, `"blur"`, `"pointerup"` and `"pointermove"` } }`. The listeners receive only the minimal shapes used (`KeyInput { code; preventDefault() }`, `PointerInput { button; clientX; clientY; preventDefault() }`), without `any`, so the test can build fake events without a DOM.
  - `function attachBrowserInput(target: InputTarget, canvas: InputCanvas, environment?: InputEnvironment): BrowserInput`; `interface BrowserInput { detach(): void }` (`detach` removes all listeners; calling it again does nothing).

- [ ] **Step 1: Failing tests** in `browser_input.test.ts`, with a fake window and canvas that store listeners and a target that records calls (style of `browser_loop.test.ts`):
```ts
test("keydown/keyup become buttonDown/buttonUp through the W3C code", ...)      // code "KeyW"
test("a code outside the table is ignored", ...)                                 // "Unidentified" → no call
test("preventDefault only for a bound button", ...)                              // isBound("Space") true → prevented; "F5" → not
test("blur on the window calls releaseAll", ...)
test("pointerdown on the canvas sends pointerMove and then buttonDown", ...)     // rect {left 10, top 20, width 200, height 100}, canvas 400×200, client (60, 70) → (100, 100); button 0 → "MouseLeft"
test("pointerup and pointermove come from the window", ...)                      // releasing outside the canvas arrives as buttonUp
test("mouse button 1 is MouseMiddle, 2 is MouseRight, 3 is ignored", ...)
test("contextmenu on the canvas is only prevented with MouseRight bound", ...)
test("a canvas with size 0 ignores pointer events", ...)                         // rect width 0 → no call to pointerMove
test("detach removes the listeners and is idempotent", ...)
```

- [ ] **Step 2: See it fail** — `$WC make ts` → type error (`attachBrowserInput` does not exist).

- [ ] **Step 3: Implement.** Default environment from `globalThis` (wrapped functions, as in `browser_loop.ts`). Conversion: `x = (clientX − rect.left) · canvas.width / rect.width`, likewise for `y` with the height. Export `attachBrowserInput`, `BrowserInput`, `InputCanvas`, `InputEnvironment`, `InputTarget` in `index.ts`.

- [ ] **Step 4: `CLAUDE.md`** — tree: `input` module (`DEPENDS core`) and the chain `core ← input ← app`; a short new section "Engine: input actions" (`ActionMap` without ECS or `Engine`, HID `PhysicalButton`, edge queue with auto-repeat ignored, capture at step 1 also while paused, edges for one step, `releaseAll` in `setHidden(true)`, 12 B snapshot per action read without crossing the boundary); in the bridge, the schema actions declared when the `OlyEngine` is created; glossary rows for declaring an action, binding, delivering input and reading; `action`, `ActionMap` and `isButtonName` in the `@oly/core` list; `attachBrowserInput` in the `@oly/client` one; `input.*` in the ctest names.

- [ ] **Step 5: Final verification** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`: all green.

- [ ] **Step 6: Commit** `feat: attachBrowserInput in @oly/client and action documentation`.
