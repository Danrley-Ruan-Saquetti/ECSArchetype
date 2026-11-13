# 11. Testing your game

[← C++ gameplay](10-cpp-gameplay.md) · [Guide index](README.md) · Next: [Performance →](12-performance.md)

Because the engine takes time and input from outside, you can run the **real game**, with the **real WASM**, in Node: you choose the time, you press the keys, you read the world. Tests are exact and fast — no browser, no waiting.

## What makes a game testable

1. **`installGame(engine, options)` has no DOM.** It registers systems, handlers and hooks only. The browser shell (`main.ts`) is the only file that touches `document`.
2. **Everything the outside world provides is injected**: the drawer, the random source, anything that would read the clock.

```ts
export interface GameOptions {
  readonly drawer: Drawer;
  readonly random: () => number;
}

export function installGame(engine: Engine, options: GameOptions): Score {
  // ...
}
```

In the browser you pass a Canvas drawer and `Math.random`. In tests, a recording drawer and a fixed sequence.

## A test harness

A small helper keeps tests short. Put it in `tests/harness.ts`:

```ts
import { pathToFileURL } from "node:url";

import { loadRuntime } from "@oly/core";
import type { ButtonName, Engine, OlyModuleFactory, Runtime, SchemaType } from "@oly/core";

import schema from "../schema.generated.js";
import { installGame } from "../src/game.js";

const StepsPerSecond = 60;

function moduleFactory(): OlyModuleFactory {
  const modulePath = process.env.OLY_GAME_MODULE!;

  return async () => {
    const loaded: { default: OlyModuleFactory } = await import(pathToFileURL(modulePath).href);

    return loaded.default();
  };
}

export function loadGame(): Promise<Runtime> {
  return loadRuntime(moduleFactory(), schema);
}

export interface GameTest {
  readonly engine: Engine;
  step(count?: number): void;
  tap(button: ButtonName): void;
  count(...types: SchemaType[]): number;
}

export function withGame(runtime: Runtime, body: (game: GameTest) => void): void {
  const engine = runtime.createEngine({ fixedHz: StepsPerSecond });
  installGame(engine, { drawer: new RecordingDrawer(), random: sequence([0.5, 0.25, 0.75]) });
  engine.frame(0);
  let stepNumber = 0;

  const game: GameTest = {
    engine,
    step(count = 1) {
      for (let index = 0; index < count; ++index) {
        ++stepNumber;
        engine.frame(stepNumber / StepsPerSecond);   // exactly one fixed step

        const broken = engine.world.checkInvariants();
        if (broken !== null) {
          throw new Error(`invariant broken at step ${stepNumber}: ${broken}`);
        }
      }
    },
    tap(button) {
      engine.actions.buttonDown(button);
      engine.actions.buttonUp(button);
      game.step();
    },
    count(...types) {
      let total = 0;
      engine.world.query({ with: types }).eachBlock((block) => {
        total += block.count;
      });

      return total;
    },
  };

  try {
    body(game);
  } finally {
    engine.dispose();
  }
}
```

`RecordingDrawer` and `sequence` are defined [further down](#check-what-was-drawn).

Key ideas:

- `engine.frame(stepNumber / 60)` with `fixedHz: 60` runs **exactly one** step per call.
- `checkInvariants()` after every step catches ECS corruption at the step that caused it.
- `dispose()` in `finally` frees the WASM memory even when an assertion fails.

## Writing tests

Tests use `node:test` and `node:assert`, like the engine's own tests.

```ts
import assert from "node:assert/strict";
import { test } from "node:test";

import { Collider, Enemy, Health, Player, Position, menu, playing } from "../schema.generated.js";
import { loadGame, withGame } from "./harness.js";

const runtime = await loadGame();

test("the game starts in the menu", () => {
  withGame(runtime, (game) => {
    game.step();

    assert.equal(game.engine.state, menu);
    assert.equal(game.count(Player), 0);
  });
});

test("Enter starts a round with one player at full health", () => {
  withGame(runtime, (game) => {
    game.step();

    game.tap("Enter");
    game.step();

    assert.equal(game.engine.state, playing);
    const player = game.engine.world.query({ read: [Health], with: [Player] }).single();
    assert.equal(player.get(Health).value, 3);
  });
});

test("a wave arrives after 5 seconds", () => {
  withGame(runtime, (game) => {
    game.step();
    game.tap("Enter");

    game.step(5 * 60);

    assert.ok(game.count(Enemy) > 0);
  });
});
```

### Set up a scene directly

Between frames, `engine.world` accepts direct changes. Use it to place entities exactly where the test needs them:

```ts
test("touching an enemy costs one health", () => {
  withGame(runtime, (game) => {
    game.step();
    game.tap("Enter");
    game.step();

    const player = game.engine.world.query({ read: [Position], with: [Player] }).single();
    const { x, y } = player.get(Position);
    game.engine.world.spawn([Enemy, Position, Collider, Health], {
      Position: { x, y },
      Collider: { radius: 12 },
      Health: { value: 1 },
    });

    game.step();

    assert.equal(game.engine.world.get(player.entity, Health).value, 2);
  });
});
```

Entities created with `engine.world.spawn` are never scoped to a state, so they survive a state change. Destroy them yourself if a later part of the test changes state.

### Simulate input

| You want | Do |
|---|---|
| a short press | `buttonDown` + `buttonUp`, then one step: the step sees `pressed` and `released` |
| holding | `buttonDown`, step N times, `buttonUp` |
| aim | `engine.actions.pointerMove(x, y)` before the step |
| alt-tab | `engine.actions.releaseAll()` or `engine.setHidden(true)` |

```ts
game.engine.actions.pointerMove(800, 270);
game.engine.actions.buttonDown("MouseLeft");
game.step(30);                                  // half a second of shooting
game.engine.actions.buttonUp("MouseLeft");
```

### Check what was drawn

A recording drawer keeps the calls of the last frame:

```ts
class RecordingDrawer implements Drawer {
  readonly texts: string[] = [];

  clear(): void {
    this.texts.length = 0;
  }
  circle(): void {}
  rect(): void {}
  text(_x: number, _y: number, content: string): void {
    this.texts.push(content);
  }
}

assert.ok(drawer.texts.some((text) => text.startsWith("Game over")));
```

### Deterministic randomness

Pass a function that cycles over fixed values:

```ts
function sequence(values: readonly number[]): () => number {
  let index = 0;

  return () => values[index++ % values.length]!;
}
```

## Register the test with ctest

Add to the game's `CMakeLists.txt`, next to `oly_add_game`:

```cmake
if(OLY_BUILD_TESTS)
  add_test(NAME ts.hello.smoke COMMAND ${OLY_NODE} --test ${CMAKE_CURRENT_SOURCE_DIR}/web/dist/tests/)
  set_tests_properties(ts.hello.smoke PROPERTIES ENVIRONMENT OLY_GAME_MODULE=$<TARGET_FILE:hello_game>)
endif()
```

The test TS project must emit to `web/dist/tests/`, so its relative imports (`../schema.generated.js`, `../src/game.js`) resolve at run time. `web/tests/tsconfig.json`:

```json
{
  "extends": "../../../../tsconfig.base.json",
  "compilerOptions": {
    "rootDir": "..",
    "outDir": "../dist",
    "tsBuildInfoFile": "../dist-tests/tsconfig.tsbuildinfo",
    "types": ["node"]
  },
  "include": ["**/*.ts"],
  "references": [{ "path": ".." }, { "path": "../../../../engine/environments/web/clients/typescript/core" }]
}
```

Add `{ "path": "games/<game>/web/tests" }` to the `references` of the root `tsconfig.json`.

Run:

```sh
$WC make games-test                      # everything
$WC make test PRESET=wasm-games T='^ts\.hello'   # only your game, after a full build
```

## Testing C++ systems alone

The engine's own C++ tests use doctest with a bare `World` and `Engine` (see `engine/runtime/app/tests/`). For game code, testing through the WASM in Node is usually enough and covers TS and C++ together. When you add a test inside the engine, follow the `add-test-case` skill described in [build and tests](../architecture/build-and-tests.md).

## Next

[12. Performance →](12-performance.md): keep the hot path fast.
