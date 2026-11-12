# 9. States

[← Schedule](08-schedule.md) · [Guide index](README.md) · Next: [C++ gameplay →](10-cpp-gameplay.md)

States split the game into screens or modes: `menu`, `playing`, `gameOver`. Each state can turn systems and handlers on or off, run code on entry and exit, and — the best part — **clean up after itself**.

## Declare and start

```ts
// schema.ts
export const menu = state("menu");
export const playing = state("playing");
export const gameOver = state("gameOver");
```

The engine starts with **no state** (`engine.state === null`). Request the first one before the first frame:

```ts
engine.requestState(menu);
```

States are flat: one active state at a time, no nesting, no stack. At most 32.

## Change state

```ts
context.requestState(playing);   // from a system, handler, task or hook
engine.requestState(gameOver);   // from outside
```

A request does not change the state at once. The change happens at **step 5** of the fixed step, after the phases and events. If several requests arrive in one step, the **last one wins**. State changes happen even while paused.

Requesting the state that is already active is a full change too: it runs `onExit`, the clean-up and `onEnter` again. Use it to restart a round.

Read the current state with `engine.state` or `context.state`. It returns the same object you declared, so compare with `===`:

```ts
if (context.state === playing) {
  // ...
}
```

## Run only in some states

```ts
engine.addSystem(Phase.UpdateBefore, "startOnConfirm", startOnConfirm).inState(menu, gameOver);
engine.addNativeSystem(Phase.Update, "chasePlayer").inState(playing);
engine.on(PlayerDied, "endRound", endRound).inState(playing);
```

- No `inState`: runs in every state, and also with no state.
- With `inState`: runs only in the listed states. With no active state, it does not run.
- It works in every phase, Render included.

## Enter and exit hooks

```ts
engine.onEnter(playing, "startRound", (context) => {
  context.spawn([Player, Position, Health], { Position: { x: 480, y: 270 }, Health: { value: 3 } });
  context.schedule.every(5, (task) => spawnWave(task));
});

engine.onExit(playing, "saveBest", (context) => {
  best = Math.max(best, score.score);
});
```

A hook receives a `StateContext`, with direct structural changes and two extra members:

| Member | Value |
|---|---|
| `fromState` | the state being left (`null` on the first change) |
| `toState` | the state being entered |

The change runs in this order:

1. `onExit` hooks of the old state, in registration order;
2. **scope clean-up** (below);
3. the state becomes the new one;
4. `onEnter` hooks of the new state;
5. events emitted by the hooks are dispatched.

A `requestState` made inside a hook takes effect in the next step.

## Scope: automatic clean-up

While a state is active, **everything created through a context is scoped to that state**:

- entities created with `context.spawn` (in systems, handlers, tasks and hooks);
- tasks created with `schedule.after` / `schedule.every`.

When the state changes, the engine destroys the scoped entities and cancels the scoped tasks. So "play again" needs **no reset code**: leaving `playing` removes the player, the enemies, the bullets and the wave timer.

| Created by | Scoped? |
|---|---|
| `context.spawn(...)` with an active state | yes |
| `schedule.after/every` with an active state | yes |
| `engine.world.spawn(...)` (direct on the world) | **never** |
| anything with no active state | no |

What `onExit` creates is cleaned in the same change. What `onEnter` creates belongs to the new state.

### Keep something across states: `persist`

```ts
engine.onEnter(playing, "startRound", (context) => {
  const camera = context.spawn([Camera, Position]);
  context.persist(camera);                     // survives every state change

  const music = context.schedule.every(30, (task) => changeTrack());
  context.schedule.persist(music);
});
```

In a system, `persist` is deferred to the end of the phase, like `remove`. In hooks, handlers and tasks it is direct.

For things that exist from the start (a camera, a background), create them with `engine.world.spawn` before the first frame: they are never scoped.

## Complete pattern: menu → playing → game over

A typical `src/game.ts`, reduced to the state logic.

```ts
export function installGame(engine: Engine): { kills: number } {
  const score = { kills: 0 };

  engine.actions.bind(confirm, "Enter");
  engine.actions.bind(pause, "Escape");

  engine.addSystem(Phase.UpdateBefore, "startOnConfirm", (context) => {
    if (context.actions.button(confirm).pressed) {
      context.requestState(playing);
    }
  }).inState(menu, gameOver);

  engine.addSystem(Phase.UpdateBefore, "pauseOnPress", (context) => {
    if (context.actions.button(pause).pressed) {
      context.pause();
    }
  }).inState(playing);
  engine.addSystem(Phase.UpdateBefore, "resumeOnPress", (context) => {
    if (context.actions.button(pause).pressed) {
      context.resume();
    }
  }).whilePaused().inState(playing);

  engine.addNativeSystem(Phase.Update, "integrate").inState(playing);
  engine.addNativeSystem(Phase.UpdateAfter, "collide").inState(playing);

  engine.on(PlayerDied, "endRound", (context) => context.requestState(gameOver));

  engine.onEnter(playing, "startRound", (context) => {
    score.kills = 0;
    context.spawn([Player, Position, Health], { Position: { x: 480, y: 270 }, Health: { value: 3 } });
    context.schedule.every(5, (task) => spawnWave(task));
  });
  // The pause belongs to the whole engine and only resumeOnPress (limited to playing) lifts it:
  // leaving playing while paused would freeze the next state.
  engine.onExit(playing, "resumeOnExit", (context) => context.resume());

  engine.requestState(menu);

  return score;
}
```

Notice:

- No code destroys the enemies or cancels the wave timer: the scope does it.
- The pause is global. `resumeOnExit` makes sure a pause never leaks into the next state.
- The render system has no `inState`: it draws every state and picks the overlay with `context.state`.

## C++

```cpp
#include "oly/app/state_context.hpp"

OLY_STATE_HOOK(spawnWalls) {
  context.spawn(Wall{}, Position{0, 0});
}

OLY_SYSTEM(checkVictory) {
  bool enemyLeft = false;
  context.query<const Health>(oly::ecs::With<Enemy>{}).each([&enemyLeft](const Health&) { enemyLeft = true; });

  if (!enemyLeft) {
    context.requestState(states::gameOver);
  }
}
```

```ts
engine.addNativeOnEnter(playing, "spawnWalls");
engine.addNativeOnExit(playing, "spawnWalls");   // the same hook can serve both; context.entering() tells which
```

`states::playing` and the other ids come from the generated header. In a C++-only game, declare them with `engine.declareState("playing")` ([C++ gameplay](10-cpp-gameplay.md#a-c-only-game)).

## Next

[10. C++ gameplay →](10-cpp-gameplay.md): move the per-entity work to C++.
