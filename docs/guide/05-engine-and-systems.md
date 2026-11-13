# 5. Engine, loop and systems

[← Queries](04-queries.md) · [Guide index](README.md) · Next: [Events →](06-events.md)

The `Engine` runs your game: a fixed-step clock, phases of systems, pause, events, input, schedule and states. It owns one `World`.

## Create the engine

```ts
const runtime = await loadRuntime(factory, schema);
const engine = runtime.createEngine({
  fixedHz: 60,            // gameplay steps per second (default 60)
  maxStepsPerFrame: 5,    // catch-up limit per frame (default 5)
  maxEventRounds: 16,     // event round limit per step (default 16)
});

// ... register systems, handlers, hooks

engine.dispose();          // also disposes engine.world
```

## The loop

Something outside the engine calls `engine.frame(nowSeconds)` once per display frame. In the browser, `runInBrowser(engine)` does it with `requestAnimationFrame` and also calls `engine.setHidden` when the tab is hidden.

```ts
import { runInBrowser } from "@oly/web-host";

const loop = runInBrowser(engine);
// loop.stop() to end it
```

Each `frame` runs as many **fixed steps** as the elapsed time allows (at most `maxStepsPerFrame`), then **one render**:

```
frame(now)
├── fixed step (repeated)
│   1. input capture
│   2. due scheduled tasks              (skipped while paused)
│   3. UpdateBefore → Update → UpdateAfter   (deferred changes applied after each phase)
│   4. event dispatch, in rounds
│   5. pending state change
└── Render (once)
```

Why a fixed step: gameplay runs with the same `deltaTime` every time, at any display rate (60 Hz, 144 Hz, a slow laptop). Results are reproducible and tests can step the game exactly.

If the game falls far behind (a debugger pause, a slow device), the steps above `maxStepsPerFrame` are dropped instead of piling up. This prevents the "spiral of death".

## Phases

| Phase | Use it for |
|---|---|
| `Phase.UpdateBefore` | reading input, steering, decisions |
| `Phase.Update` | movement, AI, shooting, most gameplay |
| `Phase.UpdateAfter` | collision and physics, after everything moved |
| `Phase.Render` | drawing; runs once per frame, not per step |

Inside a phase, systems run in **registration order**. There is no dependency graph: the order you write is the order that runs. C++ and TS systems mix freely in one phase.

## Register systems

```ts
import { Phase } from "@oly/core";

engine.addSystem(Phase.Update, "spin", (context) => {
  context.query({ write: [Rotation] }).eachBlock((block) => {
    const rotation = block.write(Rotation);
    for (let row = 0; row < block.count; ++row) {
      rotation.angle.set(row, rotation.angle.get(row) + Math.PI * context.fixedDeltaTime);
    }
  });
});

engine.addNativeSystem(Phase.Update, "integrate");   // a C++ OLY_SYSTEM(integrate)
```

The name appears in error messages; use a clear one.

Registration returns an object with two options. Both can be chained:

```ts
engine.addSystem(Phase.UpdateBefore, "resumeOnPress", resumeOnPress).whilePaused().inState(playing);
```

| Option | Effect |
|---|---|
| `.whilePaused()` | the system runs **only** while the game is paused (not in Render) |
| `.inState(...states)` | the system runs only in those states ([States](09-states.md)) |

Systems cannot be registered during `frame` (from inside another system). Register everything at start-up.

## The system context

A system receives a `SystemContext`. It is valid only during that call.

| Member | What it does |
|---|---|
| `time` | game time at the start of this step, in seconds (stops while paused) |
| `fixedDeltaTime` | seconds per step (`1 / fixedHz`) |
| `interpolation` | in Render: how far (0..1) the real time is between the last step and the next |
| `paused`, `pause()`, `resume()` | game pause |
| `query(spec)`, `get`, `set`, `has`, `isAlive` | read and write component values |
| `spawn`, `destroy`, `add`, `remove`, `persist` | **deferred** structural changes |
| `emit(Event, values)` | send an event ([Events](06-events.md)) |
| `actions` | input ([Input](07-input.md)) |
| `schedule` | timers ([Schedule](08-schedule.md)) |
| `state`, `requestState(state)` | game state ([States](09-states.md)) |
| `phase`, `name` | where the system is registered |

## Structural changes in systems

Inside a system, `spawn`, `destroy`, `add` and `remove` are **recorded** and applied at the **end of the phase**. This makes them safe inside `eachBlock`.

```ts
engine.addSystem(Phase.Update, "explodeDead", (context) => {
  context.query({ read: [Health, Position], with: [Enemy] }).eachBlock((block) => {
    const health = block.read(Health);
    const position = block.read(Position);
    for (let row = 0; row < block.count; ++row) {
      if (health.value.get(row) > 0) {
        continue;
      }

      context.destroy(block.entity(row));
      context.spawn([Explosion, Position], { Position: { x: position.x.get(row), y: position.y.get(row) } });
    }
  });
});
```

What this means for the next systems:

- A system later **in the same phase** still sees the destroyed enemy and does not see the explosion.
- A system in the **next phase** sees the result.
- `context.spawn` returns the new handle at once. You can `add` to it or store it in another component in the same step, but `isAlive` is `false` until the phase ends.

`context.set` is **not** structural: it writes the value immediately.

Use the phase boundary on purpose. For example, destroy out-of-bounds bullets in `Update`; a `collide` system in `UpdateAfter` then never sees them.

## Time and interpolation

`time` counts only completed, unpaused steps: `steps × fixedDeltaTime`. There is no accumulated floating-point sum, so it does not drift.

Render runs between steps. To draw smooth motion at high refresh rates, blend the previous and current positions with `interpolation`:

```ts
const PreviousPosition = component("PreviousPosition", { x: "f32", y: "f32" });

// first system of UpdateBefore: remember where everything was
engine.addSystem(Phase.UpdateBefore, "rememberPosition", (context) => {
  context.query({ read: [Position], write: [PreviousPosition] }).eachBlock((block) => {
    const position = block.read(Position);
    const previous = block.write(PreviousPosition);
    for (let row = 0; row < block.count; ++row) {
      previous.x.set(row, position.x.get(row));
      previous.y.set(row, position.y.get(row));
    }
  });
});

engine.addSystem(Phase.Render, "draw", (context) => {
  const alpha = context.interpolation;

  context.query({ read: [Position, PreviousPosition] }).eachBlock((block) => {
    const position = block.read(Position);
    const previous = block.read(PreviousPosition);
    for (let row = 0; row < block.count; ++row) {
      const x = previous.x.get(row) + (position.x.get(row) - previous.x.get(row)) * alpha;
      const y = previous.y.get(row) + (position.y.get(row) - previous.y.get(row)) * alpha;
      drawer.circle(x, y, 8);
    }
  });
});
```

## Pause

```ts
engine.pause();
engine.resume();
engine.paused;        // true or false
```

The pause takes effect from the **next** step. While paused:

| Keeps running | Stops |
|---|---|
| input capture | the clock (`time`) |
| systems marked `.whilePaused()` | all other gameplay systems |
| event dispatch, state changes | scheduled tasks |
| Render | |

A paused game still needs a way to resume. Register a system that runs only while paused:

```ts
function pauseOnPress(context: SystemContext): void {
  if (context.actions.button(pause).pressed) {
    context.pause();
  }
}

function resumeOnPress(context: SystemContext): void {
  if (context.actions.button(pause).pressed) {
    context.resume();
  }
}

engine.addSystem(Phase.UpdateBefore, "pauseOnPress", pauseOnPress);
engine.addSystem(Phase.UpdateBefore, "resumeOnPress", resumeOnPress).whilePaused();
```

A `.whilePaused()` system runs **only** while paused. That is why the two systems above never both fire on the same key press.

In Render, read `context.paused` to draw a "Paused" overlay.

## Hidden tab

When the tab is hidden, `runInBrowser` calls `engine.setHidden(true)`. No step runs, every held button is released and, on return, the lost time is discarded: the game continues where it stopped, with no jump. This is independent of the game pause.

## Driving the loop yourself

You can call `frame` from anything: a test, a worker, a server.

```ts
const engine = runtime.createEngine({ fixedHz: 60 });
engine.frame(0);                 // the first frame only records the time

for (let step = 1; step <= 600; ++step) {
  engine.frame(step / 60);       // exactly one step per call: 10 seconds of game
}
```

`frame` refuses a time that goes backwards or is not finite.

## When a system throws

An exception in a TS system leaves `frame` and goes to your code (or to the browser console with `runInBrowser`, which keeps the loop alive). The next `frame` recovers: it discards the pending deferred changes, events and state request of the interrupted step and restarts the clock. Everything applied before the exception stays.

Until that next `frame`, registering systems, handlers or hooks throws `ContractError`.

## Next

[6. Events →](06-events.md): let systems react to what other systems did.
