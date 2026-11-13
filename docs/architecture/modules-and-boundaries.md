# Modules and boundaries

## Dependency graph

```mermaid
graph BT
  ecs --> core
  input --> core
  app --> ecs
  app --> input
  capi --> app
  games["games (oly_add_game)"] --> capi
  codegen["@oly/codegen"] --> tscore["@oly/core"]
  client["@oly/client"] --> tscore
  testing["@oly/testing"] --> tscore
  arena["games/arena (TS)"] --> tscore
  arena --> client
```

| Module | CMake target | `DEPENDS` | Type |
|---|---|---|---|
| `core` | `oly::core` | — | library |
| `ecs` | `oly::ecs` | `core` | library |
| `input` | `oly::input` | `core` | library |
| `app` | `oly::app` | `input ecs core` | library |
| `capi` | `oly::capi` | `app input ecs core` | `OBJECT` |

`capi` is `OBJECT` because, in a static library, the linker would discard the unreferenced members and take the `EMSCRIPTEN_KEEPALIVE` functions with them.

## How the boundaries are enforced

The boundaries do not hold by convention alone. Three mechanisms enforce them:

1. **The build.** `oly_add_module(NAME <m> DEPENDS <...>)` links only the declared modules. `ecs` does not link `capi`, so an include of `capi` inside the ECS does not compile.
2. **The `oly.lint.boundaries` lint** (`tools/check_boundaries.py`, runs in ctest, with a self-test in `oly.lint.boundaries_selftest`). It rejects:
   - `#include "oly/<x>/..."` in a module or game that did not declare `<x>` in `DEPENDS` (dependencies are `PUBLIC` and transitive, so only the lint catches the undeclared direct include);
   - `oly/<x>/detail/` included outside `engine/runtime/<x>/` (the module's own tests and benchmarks may);
   - `@oly/core/internal` imported outside `engine/`;
   - `@oly/client` imported in `@oly/core`;
   - a relative import that crosses packages.
3. **The TS surface test** (`ts.core.unit`, `surface.test.ts`). It checks the public exports of `@oly/core` against the versioned list `engine/packages/core/api/public-exports.txt`. Adding something to the public API is an explicit change in that file.

Why `detail/` and a lint, and not pimpl: the hot-path templates (`Query<Ts...>::each`, `World::get<T>`, `World::spawn<Ts...>`) need to see `Table` and `Archetype` to inline. Hiding these types behind pimpl or a virtual interface would put indirection in the iteration loop. The compiler sees the details; the build rejects whoever uses them from outside.

## Vocabulary rules

- Game concepts (enemy, coin, dash) never enter `engine/`. Game names only appear in `engine/testing/fixture/` and in `games/`.
- The same verb holds across the three surfaces (C++, C API, TS): `spawn`, `destroy`, `add`, `remove`, `has`, `get`, `query`, `eachBlock`. The full glossary is in [glossary.md](glossary.md).
- The `oly` prefix appears only in the C API (`oly*`, `OlyWorld`, `OLY_API`) and in what is internal to `capi`. `extern "C"` symbols live in a single global namespace of the WASM module, together with libc and the game's exports; the prefix avoids collisions. The C++ (`oly::ecs::World`) and TS (`World`) APIs do not repeat `oly`.
- The `ById` suffix marks the variant by `ComponentId` at run time: `addById`, `removeById`, `hasById`, `getById`, `tryGetById`, `queryById`.
