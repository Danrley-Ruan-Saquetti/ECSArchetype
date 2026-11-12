# 8. Schedule

[← Input](07-input.md) · [Guide index](README.md) · Next: [States →](09-states.md)

The scheduler runs code **later** or **repeatedly**, on the **game clock**. Game time stops when the game pauses, so timers stop too. You never need to count frames or compare `Date.now()`.

## `after` and `every`

```ts
// once, 2 seconds from now
engine.schedule.after(2, (task) => {
  task.spawn([Enemy, Position], { Position: { x: 0, y: 0 } });
});

// every 5 seconds; the first run is 5 seconds from now
engine.schedule.every(5, (task) => {
  spawnWave(task);
});
```

The same scheduler is available as `engine.schedule` and as `context.schedule` in every system, handler, task and hook.

| Call | Runs | Rule |
|---|---|---|
| `after(delay, callback)` | once, at `time + delay` | `delay ≥ 0` and finite |
| `every(interval, callback)` | at `time + interval`, then every `interval` | `interval > 0` and finite |

Due tasks run at **step 2** of the fixed step (after input capture, before the phases), in due-time order. `every` does not drift: the 10th run is at exactly `10 × interval`, and a task fires at most once per step.

## The task context

The callback receives a `TaskContext`. Tasks run outside any iteration, so structural changes are **direct**, like in handlers.

| Member | What it is |
|---|---|
| `handle` | the `TaskHandle` of this task |
| `lastRun` | `true` on the run of an `after` (it will not run again) |
| `spawn`, `destroy`, `add`, `remove`, `persist` | direct changes |
| `query`, `get`, `set`, `has`, `isAlive`, `emit`, `actions`, `schedule`, `state`, `requestState`, `time`... | as in systems |

## Cancel and check

```ts
const handle = engine.schedule.every(1, (task) => {
  countdown -= 1;
});

engine.schedule.isPending(handle);   // true while it may still run
engine.schedule.cancel(handle);      // true if it was pending; false if it already ended or was cancelled
```

A task can cancel itself:

```ts
let remaining = 3;

engine.schedule.every(1, (task) => {
  remaining -= 1;
  if (remaining === 0) {
    task.schedule.cancel(task.handle);
  }
});
```

## Captured values: closures in TS

In TS, the callback is a closure. Capture what the task needs:

```ts
function scheduleRespawn(context: HandlerContext, x: number, y: number): void {
  context.schedule.after(3, (task) => {
    task.spawn([Enemy, Position, Health], { Position: { x, y }, Health: { value: 1 } });
  });
}
```

If the task refers to an entity, check that it is still alive when the task runs:

```ts
function stunFor(context: SystemContext, enemy: Entity, seconds: number): void {
  context.add(enemy, Stunned);
  context.schedule.after(seconds, (task) => {
    if (task.isAlive(enemy)) {
      task.remove(enemy, Stunned);
    }
  });
}
```

## Pause, states and recovery

- **Pause**: tasks do not run and the clock does not move. A 2-second `after` scheduled before a 10-second pause still fires 2 game seconds later.
- **States**: a task created while a state is active is **scoped** to it. A state change cancels it. This is what makes "play again" safe: the old wave timer does not keep spawning in the menu. Use `schedule.persist(handle)` to keep a task across state changes. See [States](09-states.md#scope-automatic-clean-up).
- **Created during step 2**: a task scheduled by another task runs from the next step on. `after(0)` inside a task is not a loop.
- **Exceptions**: scheduled tasks survive an interrupted frame.

## C++ tasks and payloads

C++ tasks are plain functions. Data goes in a **payload**: a trivially copyable struct of up to 32 bytes, copied into the task.

```cpp
#include "oly/app/task_context.hpp"

struct PlayerTarget {
  oly::ecs::Entity player;
};

void endInvulnerability(oly::app::TaskContext& context) {
  const oly::ecs::Entity player = context.payload<PlayerTarget>().player;
  if (context.isAlive(player)) {
    context.remove<Invulnerable>(player);
  }
}

OLY_SYSTEM(hitPlayer) {
  // ...
  context.add(player, Invulnerable{});
  context.schedule().after(1.0, endInvulnerability, PlayerTarget{player});
}
```

`payload<P>()` checks the type: asking for a different type than the one scheduled is a contract error.

### Discard callbacks

If the payload owns something (an index into your own pool, for example), pass a **discard** function. It runs once if the task leaves the queue without its last run: cancelled, cleared by a state change, or the engine destroyed.

```cpp
void releaseSlot(void* payload) {
  const SlotPayload* slot = static_cast<const SlotPayload*>(payload);
  pool.release(slot->index);
}

engine.schedule().after(5.0, useSlot, SlotPayload{index}, releaseSlot);
```

On the last run of an `after`, the discard does **not** run; release the resource inside the task when `context.lastRun()` is true.

## Next

[9. States →](09-states.md): menus, rounds and game over with automatic clean-up.
