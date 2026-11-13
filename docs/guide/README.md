# Oly user guide

This guide shows how to **make games with Oly**: what you write, where it goes and what the engine does with it. Every page has short examples you can copy, in TypeScript and, when it matters, in C++.

If you want to know *why* the engine is built this way, read [the architecture docs](../architecture/README.md). This guide only covers *how to use it*.

## What you can build

Oly is a 2D engine for the browser (and Node). With it you can:

- keep thousands of entities in an archetype ECS and iterate over them without copies;
- write gameplay in **TypeScript**, in **C++** (compiled to WebAssembly) or mix both in the same phase;
- run a **fixed-step loop** with pause, hidden-tab handling and render interpolation;
- send **events** between systems, in batches;
- map the keyboard and mouse to **actions** (button, 2D axis, pointer) and rebind them at run time;
- **schedule** work on the game clock (`after`, `every`) that pauses with the game;
- organize the game in **states** (menu → playing → game over) that clean up after themselves;
- **test the game in Node** with a fake clock and fake input.

Oly does **not** draw, play audio or do physics. You draw with whatever you want (Canvas 2D, WebGL, a test recorder) inside a render system. See [Limits](13-troubleshooting.md#what-oly-does-not-do).

## Learning path

Read in order the first time. Each page builds on the one before.

| # | Page | You learn to |
|---|---|---|
| 1 | [Getting started](01-getting-started.md) | build the engine, run the example game and create your own game folder |
| 2 | [Schema](02-schema.md) | declare components, tags, events, actions and states |
| 3 | [World and entities](03-world-and-entities.md) | create, change, read and destroy entities |
| 4 | [Queries](04-queries.md) | iterate over entities fast, filter them, find "the only one" |
| 5 | [Engine, loop and systems](05-engine-and-systems.md) | run the game loop, register systems, pause, render |
| 6 | [Events](06-events.md) | let systems talk without knowing each other |
| 7 | [Input](07-input.md) | bind keys and mouse to actions and read them |
| 8 | [Schedule](08-schedule.md) | run code later or every N seconds on the game clock |
| 9 | [States](09-states.md) | build menu, playing and game over screens that reset cleanly |
| 10 | [C++ gameplay](10-cpp-gameplay.md) | write systems, handlers and hooks in C++, or a C++-only game |
| 11 | [Testing your game](11-testing.md) | run the game in Node with fake time and input |
| 12 | [Performance](12-performance.md) | keep the hot path fast across the WASM boundary |
| 13 | [Troubleshooting](13-troubleshooting.md) | understand each error and fix the common mistakes |

Then use these as references:

| Page | Use it when |
|---|---|
| [Recipes](recipes.md) | you want a ready pattern: projectile, cooldown, spawner, pickup, health, timer, nearest target, key rebinding and more |
| [Cheat sheet](cheat-sheet.md) | you remember the idea but not the exact name or signature |
| [Glossary](../architecture/glossary.md) | you need the same operation in C++, C API and TS |

## The big picture in one example

A complete game is three things: a **schema** (the data), **systems** (the behaviour) and a **shell** (browser or test) that drives the loop.

```
# hello.oly — the data, shared by TS and C++
component Position { x: f32, y: f32 }
component Velocity { x: f32, y: f32 }
tag Player
action move: axis2d
```

```ts
// src/game.ts — the behaviour, with no DOM
import { Phase } from "@oly/core";
import type { Engine } from "@oly/core";

import { Player, Position, Velocity, move } from "../schema.generated.js";

const PlayerSpeed = 200;

export function installGame(engine: Engine): void {
  engine.actions.bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });
  engine.world.spawn([Player, Position, Velocity], { Position: { x: 100, y: 100 } });

  engine.addSystem(Phase.Update, "steer", (context) => {
    const direction = context.actions.axis2d(move);

    context.query({ write: [Velocity], with: [Player] }).eachBlock((block) => {
      const velocity = block.write(Velocity);
      for (let row = 0; row < block.count; ++row) {
        velocity.x.set(row, direction.x * PlayerSpeed);
        velocity.y.set(row, direction.y * PlayerSpeed);
      }
    });
  });

  engine.addSystem(Phase.Update, "integrate", (context) => {
    const deltaTime = context.fixedDeltaTime;

    context.query({ read: [Velocity], write: [Position] }).eachBlock((block) => {
      const velocity = block.read(Velocity);
      const position = block.write(Position);
      for (let row = 0; row < block.count; ++row) {
        position.x.set(row, position.x.get(row) + velocity.x.get(row) * deltaTime);
        position.y.set(row, position.y.get(row) + velocity.y.get(row) * deltaTime);
      }
    });
  });
}
```

```ts
// src/main.ts — the browser shell
import { loadRuntime } from "@oly/core";
import { attachBrowserInput, moduleFactoryFromUrl, runInBrowser } from "@oly/web-host";

import schema from "../schema.generated.js";
import { installGame } from "./game.js";

const canvas = document.getElementById("game") as HTMLCanvasElement;
const runtime = await loadRuntime(moduleFactoryFromUrl("./hello_game.mjs"), schema);
const engine = runtime.createEngine({ fixedHz: 60 });

installGame(engine);
attachBrowserInput(engine, canvas);
runInBrowser(engine);
```

## Example games

Complete example games live in [`games/`](../../games/), one folder per game. Each one has its own `README.md` that says what it shows and how to run it. After this guide, they are the best reference: real code that uses every part of the engine together.

All example games follow the same layout ([Getting started](01-getting-started.md#create-your-own-game)), so once you know one, you can read any of them.

## Conventions used in this guide

- `TS` blocks use `@oly/core` and `@oly/web-host`. `C++` blocks use the `oly::ecs`, `oly::app` and `oly::input` namespaces.
- Commands run inside Docker. `$WC` means `docker compose run --rm wasm` and `$DC` means `docker compose run --rm dev` ([commands](../development/commands.md)).
- "Contract error" means the engine refuses the call: TS throws `ContractError`; C++ stops on `OLY_ASSERT` (in debug builds).
