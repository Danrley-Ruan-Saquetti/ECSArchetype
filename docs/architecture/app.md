# `app`: the `Engine`

Code in `engine/runtime/app/`. Spec: `docs/superpowers/specs/2026-11-09-game-engine-design.md`.

`oly::app::Engine` holds the fixed-step clock, the systems of each phase, the pause, the hidden tab, the event queues, the `Scheduler`, the states, an `ActionMap` ([input.md](input.md)) and a `CommandBuffer`. It does **not** own the `World`: it receives `World&` in the constructor.

`EngineConfig`: `fixedHz = 60`, `maxStepsPerFrame = 5`, `maxEventRounds = 16`.

## The loop

The host calls `frame(nowSeconds)` once per frame and `setHidden(bool)` when the tab changes visibility. The core decides the rest.

```
frame(now):
  if the previous frame was interrupted by an exception → recover (see "Exception recovery")
  if the tab is hidden → return
  first frame (or first after hidden/recovery) → only record the time
  accumulator += now − previous
  while a step fits and steps < maxStepsPerFrame:
    fixed step
  excess beyond the limit → discarded (avoids the spiral of death)
  render (once), with interpolation = accumulator / step

fixed step:
  1. action capture                          (paused or not)
  2. due scheduled tasks                     (only when not paused)
  3. updateBefore → update → updateAfter     (CommandBuffer applied at the end of each phase)
  4. event dispatch, in rounds
  5. pending state change                    (paused or not)
  clock advances (only when not paused)
```

- `time()` = completed unpaused steps × `fixedDeltaTime`. Inside a step, it is the start of the step; outside `frame` and in the `render` phase, it is the start of the next step. There is no accumulated `double` sum.
- There is no physics phase. The game's collision and physics go in `updateAfter`.
- Inside a phase, the order is the registration order. There is no dependency graph between systems.

## Systems and contexts

A system is a function pointer plus `userData`. That way `app` does not know about TS: `bridge` registers a TS system as an ordinary C++ function ([bridge.md](bridge.md)).

Registration: `addSystem(phase, name, function)` or, by name, the `OLY_SYSTEM(name)` macro, which TS adds with `addNativeSystem`. C++ and TS systems mix in the same phase, in registration order.

Context hierarchy:

```
EngineContext      time, pause, query/get/has/isAlive, emit, actions, schedule, state, requestState
├── SystemContext  deferred changes (CommandBuffer), interpolation, deferred persist
└── DirectContext  direct changes to the World, direct persist
    ├── HandlerContext  + event batch
    ├── TaskContext     + handle, lastRun, payload<P>()
    └── StateContext    + fromState, toState
```

Systems do not make direct structural changes, because they may be inside an iteration. Handlers, tasks and hooks run outside iteration, so they change the `World` directly.

## Pause and hidden tab

- `pause()`/`resume()` take effect from the next fixed step.
- Paused: the clock and the schedule freeze; only systems marked `whilePaused` run. Capture, event dispatch, state change and `render` continue.
- Hidden tab: no step runs, `releaseAll()` is called and the accumulated time is discarded on return. The hidden tab is independent of the game's pause.

## Events

- `declareEvent<T>(name)` gives the `EventTypeId` in declaration order, which is the dispatch order. `T` is trivially copyable.
- Each type has two byte queues: `pending` (receives `emit`, at any time) and `dispatching` (the round's batch).
- **Round**: swaps the queues of every type with pending events and calls each handler of the type once with the whole batch, type by type. What handlers emit goes to the next round of the same step. The batch a handler reads never changes or moves while it runs.
- Rounds continue until the queues are empty or `maxEventRounds` is exceeded (`OLY_ASSERT`; without asserts, it discards everything and the step continues).
- The engine does not deduplicate events or destroy anything on its own. The game registers the destroying handler last and checks `isAlive`.
- An event with no handler disappears at the end of the step. An event emitted in `render` or outside `frame` is dispatched in the next step.

## Schedule

- `Scheduler::after/every/cancel/isPending`, on the game clock. Due time = `time()` + delay.
- The queue (`detail::TaskQueue`) is a min-heap by (due time, sequence) over slots with generation and lazy cancellation. It does not know the `Engine`.
- Due tasks run in step 2, only when not paused. A task created during step 2 only runs from the next step on, so `after(0)` inside a task does not become a loop.
- `every` does not drift and fires at most once per step.
- The payload is copied (trivially copyable, up to 32 B, alignment up to 8). `payload<P>()` checks the type.
- The optional discard runs exactly once when the task leaves the queue without its last run (cancellation, state change, destruction of the `Engine`). It never runs after the last firing of an `after` (`lastRun()`): a JS exception unwinds the WASM without running what comes after, and the TS closure would stay stuck.

## States

- Flat states, no nesting and no stack. `declareState` gives the `StateId` in declaration order; at most 32, so the restriction is a `uint32_t` mask. The engine starts in `NoState`.
- `requestState` is valid at any time; the last request before step 5 wins. There is no special initial state: the game requests the first state before running.
- **Change (step 5)**: `onExit` of the leaving state → scope cleanup → new state → `onEnter` of the entering one → new event dispatch, which counts toward the step's round limit. A request made in a hook takes effect in the next step.
- **`inState`**: systems and handlers can be restricted by mask, in every phase. With no active state, anything with a restriction does not run.
- **Scope**: with an active state, every context `spawn` adds the internal tag `detail::StateScoped`, and every scheduled task is marked as `scoped`. The change destroys the entities with the tag (through a `QueryById` created once) and cancels the scoped tasks. That way "play again" starts clean with no cleanup code in the game. A direct `World::spawn` is always persistent. `persist` removes the scope (deferred in `SystemContext`, direct in the other contexts). What `onExit` creates is cleaned up in the same change; what `onEnter` creates stays until the next change.
- Accepted cost of tag-based scope: twin archetypes (with and without the tag) when the game creates the same combination in both modes.

## Exception recovery

A TS system that throws unwinds the WASM without running destructors. The next `frame` sees `inFrame_` still set and recovers:

- discards the pending commands, the event queues and the state request;
- returns to the heap the tasks created during the interrupted step 2;
- resets the frame clock (the next `frame` only records the time).

What the earlier phases already applied stays. Scheduled tasks survive. An exception in an `onExit` leaves the state and the scope as they were; in an `onEnter`, the state is already the new one.
