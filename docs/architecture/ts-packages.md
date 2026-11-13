# TypeScript packages

npm workspaces at the root, a single `package-lock.json`, `tsconfig.base.json` (`strict`, ES2022, `NodeNext`) and project references. Each package compiles to its own `dist/` (tests in `dist-tests/`). Pinned versions: TypeScript 5.6.3, `@types/node` 20.17.6 and esbuild 0.24.2. Tests with `node:test`.

## `@oly/core`

The engine on the TS side (`engine/environments/web/clients/typescript/core/`), with no DOM and no node (`types: []`): it runs in node, in the browser and in a worker. What depends on the environment comes in by injection (the WASM module factory, the time, the input).

- **Schema**: `defineSchema`, `component`, `tag`, `event`, `action`, `state`.
- **Runtime**: `loadRuntime(factory, schema)` → `Runtime` (`schema`, `createWorld()`).
- **World**: `World`, `Query` (`eachBlock`, `single`, `trySingle`), `Block` (`read`, `write`), `Entity`.
- **Engine**: `Engine`, `SystemContext`, `HandlerContext`, `TaskContext`, `StateContext`, `EventBatch`, `Scheduler`, `ActionMap`.
- **Errors**: `ContractError`, `SchemaMismatchError`.
- **Internal** (`@oly/core/internal`, only for `engine/`): `Bridge`, `bridgeOf`, `moduleOf`, `componentIdOf`, `Heap`, layout and hash.

The exact public list is `engine/environments/web/clients/typescript/core/api/public-exports.txt` ([modules-and-boundaries.md](modules-and-boundaries.md#how-the-boundaries-are-enforced)).

Implementation rules:

- **Only the `Bridge` calls WASM exports.** Every call that may allocate goes through it, and it does `sync()`: if memory grew, the old `ArrayBuffer` was detached and the heap views are recreated.
- **Copy-free views.** `Block.read/write` and `EventBatch.read` return fields that read the heap through the column address and the struct stride. They are only valid inside the callback.
- **TS validates before calling C++.** Invalid usage throws `ContractError` with operation, entity and type. With asserts disabled in Release, reaching the `OLY_ASSERT` would be undefined behaviour.
- **`Entity` is a `number`**: `generation × 2³² + index`, with generation up to `0x1FFFFF` to fit in 53 bits.
- **`World` locked during `frame`.** A direct structural change in a TS system is a `ContractError`; the system uses `context.spawn/destroy/add/remove`.
- One-off reads (`get`, `single`) cross the boundary a constant number of times per call and return copies.

## `@oly/client`

Thin browser layer (`engine/packages/client/`). It implements with DOM APIs the ports that `core` receives by injection and has no engine rule:

- `moduleFactoryFromUrl(url)`: loads a game's Emscripten `.mjs`.
- `runInBrowser(engine)`: loop via `requestAnimationFrame` and `visibilitychange` → `setHidden`.
- `attachBrowserInput(engine, canvas)`: keyboard and `blur` on `window`, `pointerdown` on the canvas, `pointerup`/`pointermove` on `window`, position converted to canvas pixels. `preventDefault` only for buttons bound to some action.

Loop and input are kept separate because they change for different reasons.

## `@oly/codegen` and `@oly/testing`

- `@oly/codegen` (`engine/tools/codegen/`): the `oly-codegen --schema <schema.js> --out <dir>` bin, called by `oly_add_game` ([schema-and-codegen.md](schema-and-codegen.md)).
- `@oly/testing` (`engine/testing/`, private): the `fixture` game (`engine/testing/fixture/`, the only place in the engine with game names), the integration tests against the real WASM (`tests/wasm/`) and the boundary, loop and event benchmarks (`bench/`). It stays outside `@oly/core` so as not to create a cycle in the project references or put game names in the package.
