# 1. Getting started

[Guide index](README.md) · Next: [Schema →](02-schema.md)

This page takes you from a clean clone to your own game running in the browser.

## What you need

Only **Docker** with Docker Compose. The images bring GCC, CMake, Emscripten, Node and Python. There is no compiler on the host.

```sh
WC="docker compose run --rm wasm"   # WASM, TypeScript and games
DC="docker compose run --rm dev"    # native C++ (tests and benchmarks)
```

## Run an example game

Each folder in `games/` is a game. `G` is the folder name.

```sh
$WC make games-test                                                 # builds everything and runs the tests of every game
docker compose run --rm --service-ports web make serve G=<game>     # serves http://localhost:8000
```

Open `http://localhost:8000`. The game's `README.md` lists its controls.

> `make serve` needs an interactive terminal: esbuild stops when stdin closes.

## How a game is built

You write the schema and the gameplay. The build turns them into one WASM module per game.

```
games/<game>/schema.ts ──tsc──► schema.js ──oly-codegen──► schema.generated.hpp (C++ structs, ids)
games/<game>/native/*.cpp  (your C++ systems, optional) ──┐
engine (ecs + app + input + capi) ────────────────────────┴──emcc──► <game>_game.mjs + .wasm
games/<game>/src/*.ts  (your TS systems and the shell) ──tsc + esbuild──► main.js
```

At start-up, `loadRuntime` checks that the WASM was built from the same schema as the TS code. If you change the schema and forget to rebuild, you get a `SchemaMismatchError` instead of corrupted data.

## Create your own game

The steps below create `games/hello`. Copy the files, then run the build.

### 1. Folder layout

```
games/hello/
  package.json
  tsconfig.json            DOM-free code: schema and game logic
  tsconfig.browser.json    only src/main.ts, with the DOM
  CMakeLists.txt
  schema.ts
  native/hello.cpp         optional C++ systems
  src/game.ts              installGame(engine): registers everything, no DOM
  src/main.ts              browser shell
  web/index.html
```

Keeping `game.ts` free of the DOM is what lets you [test the game in Node](11-testing.md). The two `tsconfig` files enforce it: using `document` outside `main.ts` is a compile error.

### 2. `package.json`

```json
{
  "name": "hello",
  "private": true,
  "version": "0.1.0",
  "type": "module",
  "dependencies": { "@oly/core": "*", "@oly/web-host": "*" }
}
```

### 3. `tsconfig.json` and `tsconfig.browser.json`

```json
{
  "extends": "../../tsconfig.base.json",
  "compilerOptions": { "rootDir": ".", "outDir": "dist", "lib": ["ES2022"], "types": [] },
  "include": ["schema.ts", "src"],
  "exclude": ["src/main.ts"],
  "references": [{ "path": "../../engine/environments/web/clients/typescript/core" }]
}
```

```json
{
  "extends": "../../tsconfig.base.json",
  "compilerOptions": { "rootDir": ".", "outDir": "dist", "lib": ["ES2022", "DOM"], "types": [] },
  "include": ["src/main.ts"],
  "references": [
    { "path": "./tsconfig.json" },
    { "path": "../../engine/environments/web/clients/typescript/core" },
    { "path": "../../engine/environments/web/host/js" }
  ]
}
```

### 4. `CMakeLists.txt`

```cmake
file(GLOB nativeSources CONFIGURE_DEPENDS native/*.cpp)

oly_add_game(hello_game
  SCHEMA_JS ${CMAKE_CURRENT_SOURCE_DIR}/dist/schema.js
  SOURCES ${nativeSources}
  DEPENDS ecs app capi)
```

The target name must be `<folder>_game`: `make game G=hello` looks for `hello_game.mjs`.

### 5. `schema.ts`

```ts
import { action, component, defineSchema, tag } from "@oly/core";

export const Position = component("Position", { x: "f32", y: "f32" });
export const Velocity = component("Velocity", { x: "f32", y: "f32" });
export const Player = tag("Player");
export const move = action("move", "axis2d");

export default defineSchema([Position, Velocity, Player, move]);
```

### 6. `native/hello.cpp` (optional)

C++ is the right place for code that touches every entity each step. This system moves everything that has a velocity.

```cpp
#include "oly/app/system.hpp"
#include "oly/ecs/query.hpp"
#include "schema.generated.hpp"

OLY_SYSTEM(integrate) {
  const float deltaTime = context.fixedDeltaTime();

  context.query<Position, const Velocity>().each([deltaTime](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}
```

`schema.generated.hpp` is created by the build from `schema.ts`. It has the `Position`, `Velocity` and `Player` structs and the `actions::move` handle.

### 7. `src/game.ts`

```ts
import { Phase } from "@oly/core";
import type { Engine } from "@oly/core";

import { Player, Position, Velocity, move } from "../schema.js";

const PlayerSpeed = 200;

export interface Drawer {
  clear(): void;
  circle(x: number, y: number, radius: number): void;
}

export function installGame(engine: Engine, drawer: Drawer): void {
  engine.actions.bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });
  engine.world.spawn([Player, Position, Velocity], { Position: { x: 320, y: 180 } });

  engine.addSystem(Phase.UpdateBefore, "steer", (context) => {
    const direction = context.actions.axis2d(move);

    context.query({ write: [Velocity], with: [Player] }).eachBlock((block) => {
      const velocity = block.write(Velocity);
      for (let row = 0; row < block.count; ++row) {
        velocity.x.set(row, direction.x * PlayerSpeed);
        velocity.y.set(row, direction.y * PlayerSpeed);
      }
    });
  });
  engine.addNativeSystem(Phase.Update, "integrate");

  engine.addSystem(Phase.Render, "draw", (context) => {
    drawer.clear();
    context.query({ read: [Position] }).eachBlock((block) => {
      const position = block.read(Position);
      for (let row = 0; row < block.count; ++row) {
        drawer.circle(position.x.get(row), position.y.get(row), 10);
      }
    });
  });
}
```

### 8. `src/main.ts`

```ts
import { loadRuntime } from "@oly/core";
import { attachBrowserInput, moduleFactoryFromUrl, runInBrowser } from "@oly/web-host";

import schema from "../schema.js";
import { installGame } from "./game.js";

const Width = 640;
const Height = 360;

async function boot(): Promise<void> {
  const canvas = document.getElementById("game");
  if (!(canvas instanceof HTMLCanvasElement)) {
    throw new Error('canvas with id "game" not found');
  }

  canvas.width = Width;
  canvas.height = Height;
  const context2d = canvas.getContext("2d")!;

  const runtime = await loadRuntime(moduleFactoryFromUrl("./hello_game.mjs"), schema);
  const engine = runtime.createEngine({ fixedHz: 60 });
  installGame(engine, {
    clear: () => context2d.clearRect(0, 0, Width, Height),
    circle: (x, y, radius) => {
      context2d.beginPath();
      context2d.arc(x, y, radius, 0, Math.PI * 2);
      context2d.fill();
    },
  });
  attachBrowserInput(engine, canvas);
  runInBrowser(engine);
}

boot().catch((error: unknown) => {
  console.error(error);
});
```

### 9. `web/index.html`

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <title>Hello Oly</title>
  </head>
  <body>
    <canvas id="game"></canvas>
    <script type="module" src="./main.js"></script>
  </body>
</html>
```

### 10. Register the game

Add one line to `games/CMakeLists.txt`:

```cmake
add_subdirectory(hello)
```

Add the two TS projects to the `references` of the root `tsconfig.json`:

```json
{ "path": "games/hello" },
{ "path": "games/hello/tsconfig.browser.json" }
```

`games/*` is already an npm workspace. Run `$WC npm install` once, so `package-lock.json` includes the new package.

### 11. Build and run

```sh
$WC make game G=hello                                            # builds build/wasm-games-release/web/hello/
docker compose run --rm --service-ports web make serve G=hello   # http://localhost:8000
```

## Three ways to split the work

| Style | When it fits | What you write |
|---|---|---|
| **TS only** | prototypes, small games, few entities | `schema.ts` and TS systems. The `native/` folder can be empty: the WASM still holds the engine and your structs. |
| **TS + C++** (recommended) | real games | per-entity work (movement, collision, AI) in C++; render, HUD, waves, score and menus in TS. |
| **C++ only** | native tools, benchmarks, a future native host | an `oly::app::Engine` over an `oly::ecs::World`, with no TS at all. See [C++ gameplay](10-cpp-gameplay.md#a-c-only-game). |

## Next

[2. Schema →](02-schema.md): every kind of data you can declare.
