# 13. Troubleshooting

[← Performance](12-performance.md) · [Guide index](README.md) · References: [Recipes](recipes.md) · [Cheat sheet](cheat-sheet.md)

Oly checks how you use it and fails **at the wrong call**, with a message that names the operation, the entity (`index:generation`) and the type. This page explains the messages you will meet most.

## The two error classes (TS)

| Error | Meaning | What to do |
|---|---|---|
| `ContractError` | you called the API in a way it does not allow | read the message: it says the operation and why |
| `SchemaMismatchError` | the WASM was built from a different schema | rebuild: `./spur ts` then the game build |

In C++, the same violations stop on `OLY_ASSERT` in debug builds (`wasm-games`, `debug-asan`). In release builds they are **not checked**. Develop and test with asserts on: `PRESET=wasm-games ./spur game <game>`.

## Common messages

| Message contains | Cause | Fix |
|---|---|---|
| `dead entity` | the entity was destroyed (maybe by another handler, or its state ended) | check `isAlive` before using a stored handle |
| `entity does not have the component` | `get`/`set` on a component the entity lacks | check with `has`, or add the component at spawn |
| `direct structural change during frame; use context.spawn/destroy/add/remove` | you used `engine.world.spawn/destroy/add/remove` inside a system | use the `context` methods |
| `structural change during eachBlock` | a direct change inside a query loop (handler, task, hook or outside the loop) | collect entities in an array and change them after the loop |
| `block used outside the eachBlock callback` | you kept a `Block` and used it later | read what you need inside the callback |
| `EventBatch used outside the handler call` | you kept a batch view | copy the values inside the handler |
| `SystemContext used outside the system call` (or handler/task/hook) | a closure kept the context and ran later | use the context passed to the new call (a task gets its own `TaskContext`) |
| `query with N rows; expected 1` | `single()` found 0 or several entities | use `trySingle()` where 0 is valid; check you did not spawn two players |
| `a tag cannot go in read or write; use with/without` | `read: [Enemy]` with a tag | move it to `with` |
| `type is not in the query's write` | `block.write(T)` for a type in `read` | put the type in `write` |
| `registration during frame` | `addSystem`/`on`/`onEnter` called from inside a system or handler | register everything at start-up |
| `the last frame was interrupted by an exception; call frame before registering` | a system threw; the engine has not recovered yet | call `frame` once, then register |
| `frame is not reentrant` | `engine.frame` called from inside a system | never call `frame` from game code |
| `time=... less than the previous frame's` | the time passed to `frame` went backwards | use a monotonic clock (`performance.now()`, rAF timestamps) |
| `no native system with that name (declare it with OLY_SYSTEM)` | typo, or the `.cpp` is not in `native/`, or the WASM is old | check the name and rebuild |
| `the native handler is for another event type` | `addNativeHandler(EventA, "x")` for an `OLY_HANDLER(x, EventB)` | pass the matching event |
| `action of kind button, expected axis2d` | `axis2d(shoot)` on a button action | read with the method of the action's kind |
| `button outside the W3C table` | a key name that does not exist (`"W"` instead of `"KeyW"`) | use `KeyboardEvent.code` names; check with `isButtonName` |
| `does not belong to this runtime's schema` | an entry from another generated schema module | import the entries from the game's `schema.generated.js` |
| `invalid type name` / `invalid field` | schema naming rules | `PascalCase` types, `camelCase` fields, no C++ keywords |
| `<game>.oly:<line>:<column>: ...` when building | a schema rule is broken | fix that line; the rules are in [Schema](02-schema.md#naming-rules) |
| `Engine already disposed` / `World already disposed` | use after `dispose()` | keep the engine alive while the game runs |
| `World belongs to the Engine; use engine.dispose()` | `engine.world.dispose()` | dispose the engine instead |

## "It does nothing" — silent problems

| Symptom | Likely cause |
|---|---|
| a system never runs | it has `.inState(...)` and no state is active (did you call `requestState` before the first frame?) or it is `.whilePaused()` and the game is not paused |
| nothing runs after pausing | no `.whilePaused()` system calls `resume()` |
| the next state is frozen | the game was paused when the state changed; resume in an `onExit` hook |
| a spawned entity is not found in the same phase | system changes are deferred to the end of the phase |
| entities disappear on state change | they were created through a context while a state was active: they are scoped. Use `persist`, or create them with `engine.world.spawn` before the first frame |
| a timer stopped firing | it was scoped to a state that ended, or the game is paused (game clock stopped) |
| a handler sees the same entity twice | two emitters reported it; deduplicate in the handler |
| events from Render are late | events emitted in Render are dispatched in the next step |
| a key does not respond | it is not bound, or another element has focus; check `engine.actions.isBound("KeyX")` |
| `pressed` never true in a test | input is captured at the start of a step: deliver the input, then call `frame` |
| the game jumps after alt-tab | not using `runInBrowser` (or not calling `setHidden`) |
| the right-click menu opens | `MouseRight` is not bound; prevent `contextmenu` on the canvas yourself |

## Rebuild checklist

After changing the schema:

1. `./spur ts` — builds `oly-schema`, generates the TS schemas and compiles the TS;
2. build the game (`./spur game <game>` or `./spur games-test`) — regenerates `schema.generated.hpp` and the WASM;
3. reload the page (or rerun the tests).

## What Oly does not do

Out of scope by design: rendering, physics, audio, camera, UI, assets, networking, serialization, editor, threads and gamepad. Render and collision belong to the game: draw in a `Render` system ([Engine, loop and systems](05-engine-and-systems.md#phases)) and test overlaps in `UpdateAfter` ([Recipes](recipes.md#pickups)). More in [limits](../architecture/limits.md).

ECS v1 also has no fixed-size chunks, no change detection, no enable/disable flag per component and no sparse storage.
