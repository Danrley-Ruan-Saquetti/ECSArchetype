# Oly — C++ first, multi-environment engine — design

Date: 2026-11-13. Base: `master` at `5877a28`.
Specs that remain valid: `2026-10-16-ecs-archetype-spec.md`, `2026-11-09-game-engine-design.md`.
Spec partly replaced: `2026-11-09-oly-modularization-design.md` (tree, schema source, TS surface; see §9).

Marking: **[user decision]** was decided in the conversation; **[author decision]** was my choice during the design,
which the user may review; the rest is a direct consequence of the two.

## 1. Intent

Oly must be a product that games can be built with in several programming languages, at least the common ones for
games **[user decision]**. The goal is the product, not measurement **[user decision]**. The idea is ambitious on
purpose: the user wants Oly to go beyond what usual engines offer.

The philosophy is **C++ first** **[user decision]**: the engine is C++, and each client is an entry point written
the way its language is used, not a port of another client. Clients are grouped by **environment**, not by language,
and the user guide `docs/guide/` follows the same structure **[user decision]**.

Today the engine logic is already C++ (rule 1 of `docs/architecture/README.md`), but the project is still centered on
TypeScript in three places:

1. **The schema source is TS.** `schema.ts` is the single source; `@oly/codegen` (TS, run by Node) derives the C++
   header from it. A C++-only game still needs a TS file and Node.
2. **The callback is JS.** `bridge` calls script code through `olyHostInvokeScript`, an `EM_JS` import
   (`engine/runtime/bridge/src/engine_api.cpp`). Outside WASM, only a process-wide test hook
   (`oly::bridge::setScriptInvoker`) exists.
3. **The only host and the only client are the browser and TS.** There is no native host (`limits.md`), and the guide
   teaches TS first, with C++ as an extra page (`10-cpp-gameplay.md`).

What must exist at the end of this project **[user decision]**: the architectural separation of the project (core
versus environments and clients) and an engine neutralized so that it is decoupled from WASM.

## 2. Goals, non-goals, anti-goals

**Goals**

- The tree separates the C++ core, the schema tooling and the environments (§3) **[user decision]**.
- The schema is a neutral `.oly` file, read by a generator written in C++ that emits code for C++ and for each client
  (§4) **[user decision: option A]**.
- The core has no dependency on JS or Emscripten except the export attribute of `OLY_API` (§5).
- A second environment, `native`, exists with a minimal host, so that the neutrality is proven by two real consumers (§6)
  **[author decision]**.
- The `web` environment and its TS client keep every feature they have today; `games/arena` keeps running in the
  browser and its smoke test keeps passing.
- `docs/guide/` is reorganized into concepts (C++) and environments (§7) **[user decision]**, and the architecture
  docs, the guide style and `CLAUDE.md` describe the new structure.

**Non-goals**

- Clients in other languages (C#, Rust, Lua, ...). This project prepares the ground; each new client is its own
  project, written in its language's idioms **[user decision]**.
- A `embedded` environment (a native host that embeds an interpreter). It appears in the tree only as the planned shape.
- Render, audio and physics in the engine. The native host gives a window and input; the game draws, as on the web.
- Mobile and consoles.
- Benchmarks of each client's boundary (the goal is the product **[user decision]**). The existing benchmarks must keep
  running.

**Anti-goals** (these make the project a failure even if it works)

- A client that is a mechanical translation of another (the TS API transliterated to C#). Each client follows its
  language.
- Engine behaviour implemented in a client. Rule 1 stays: a client has API conveniences, never engine rules.
- C++ treated as one client among others. C++ is the engine language and runs in every environment (§3.1).
- A schema change that has to be written twice (once in `.oly`, once by hand in some client).
- The `web` environment losing a feature or getting slower on the hot path because of the neutralization.

## 3. Structure

### 3.1 The model

- **Core**: the C++ engine (`core`, `ecs`, `input`, `app`) and its C API. It knows no environment.
- **Environment**: how the core is loaded and who drives the loop. Each environment has one **host** and zero or more
  **clients**.
  - `web`: the core compiled to WASM, inside a browser or Node. Host in JS. Client: TypeScript.
  - `native`: the core compiled for the machine, in a native process. Host in C++ (window, loop, input). Future
    clients: C#, Rust and others, through a shared library and the C API.
  - `embedded` (future): a native host that embeds an interpreter (Lua, Python).
- **Host**: loads the core, drives `frame`, forwards time, visibility and input. It has no engine rules.
- **Client**: the API of one language over the C API, with that language's idioms.
- **C++ is not a client.** A game's C++ systems, handlers and hooks are compiled together with the core in every
  environment: into the WASM on `web`, into the executable or library on `native` **[user decision]**.
- **A C++ game and a C++ host use `oly::app::Engine` directly**, never the C API **[author decision]**. The C API
  exists for clients in other languages. Consequence for the generator: the schema declaration that today lives inside
  `olyEngineCreate` (events, actions and states in schema order) becomes a generated function that declares the schema
  into an `oly::app::Engine` (§6.3). `capi` calls it on engine creation, and a native game's `main` calls it too. The
  native host depends on `app`, not on `capi`.

### 3.2 Tree

```
engine/
  runtime/                 C++ core, one module per folder (shape unchanged)
    core/  ecs/  input/  app/
    capi/                  C API oly* (today bridge/), no JS
  schema/
    format/                docs and test corpus of the .oly language
    codegen/               oly-schema: parser, validation, layout, hash, emitter interface, C++ emitter
  environments/
    web/
      host/
        native/            C++ compiled into the WASM: Emscripten link options, the EM_JS script invoker and
                           the exported function that installs it
        js/                @oly/web-host (today @oly/client): rAF loop, visibility, keyboard and mouse
      clients/
        typescript/
          core/            @oly/core (today engine/packages/core)
          codegen/         the TS emitter, compiled into oly-schema
    native/
      host/                native host module (SDL3): window, loop, keyboard and mouse
      clients/             empty in this project; future c#/, rust/ ...
  testing/                 fixture, WASM tests, benchmarks (fixture.oly replaces the fixture's schema.ts)
  tools/                   (codegen/ leaves; see §4)
games/
  <game>/
    <game>.oly             the schema, one source for every environment
    native/                the game's C++ (runs in every environment)
    web/                   the browser shell and TS gameplay, when the game has them
    desktop/               the native shell, when the game has one
```

`bridge` becomes `capi` **[author decision]**: after the change it is no longer a bridge to JS but the C surface
every client uses. Following the glossary rule, there is no alias: namespace `oly::capi`, target `oly::capi`,
include `oly/capi/...`.

`engine/environments/web/clients/typescript/` keeps the npm package name `@oly/core` **[author decision]**: it is the
name a TS user imports, and the folder already says the language.

`@oly/client` becomes `@oly/web-host` **[author decision]**. `runInBrowser`, `attachBrowserInput` and
`moduleFactoryFromUrl` are host work by the definition in §3.1 (they drive `frame` and forward time and input), and
the old name would clash with "client" in the new model. A future second web client (plain JS, for example) reuses the
same host. Following the glossary rule, there is no alias package.

### 3.3 Boundaries

`tools/check_boundaries.py` gains these rules:

- nothing in `engine/runtime/` includes or imports anything from `engine/environments/`;
- one environment does not include another;
- `__EMSCRIPTEN__` and `EM_*` macros appear only in `engine/environments/web/` and in `OLY_API`
  (`engine/runtime/capi/include/oly/capi/c_api.hpp`, `c_api.h` from phase 4 on, §6.5); tests that skip a case on WASM (today
  `engine/runtime/app/tests/recovery.cpp`) are exempt;
- the existing rules keep their meaning with the new paths.

## 4. The schema

### 4.1 The `.oly` language **[user decision: option A]**

```
# arena.oly
component Position { x: f32, y: f32 }
component Health { value: i32 }
tag Enemy
event EnemyDied { entity: entity, x: f32, y: f32 }
action move: axis2d
action shoot: button
state playing
```

- Five declarations: `component`, `tag`, `event`, `action`, `state`. The same rules as `schema.ts` today: name
  patterns, C++ reserved words, components and events need at least one field, at most 32 states, no repeated name
  across all declarations.
- Scalar types: `f32 f64 i8 u8 i16 u16 i32 u32 entity`. Action kinds: `button axis2d pointer`.
- Declaration order inside each group is the id order, as today.
- Comments start with `#` and run to the end of the line. Whitespace and newlines are free. One file per game.
- Errors report file, line and column, and the build fails.

The exact grammar (separators, trailing commas, identifiers) is left to the builder within these rules, and is written
down in `engine/schema/format/`.

### 4.2 The generator `oly-schema` **[author decision]**

```
<game>.oly ──► oly-schema (C++): parse, validate, layout, hash  (once)
                 ├── C++ emitter ──► schema.generated.hpp + schema_registry.generated.cpp  (same content as today)
                 └── TS emitter  ──► schema.generated.ts
```

- Written in C++20, in `engine/schema/codegen/`, with the project's code style. Native executable; it runs at build
  time in both Docker images.
- The layout (size, alignment, field offsets) and the hash are computed once, here. **The hash keeps today's canonical
  form and FNV-1a**, so that the hash of an existing schema does not change; a test compares the generator's hash with
  the value `@oly/core` computes today for `arena` and the fixture before the TS code is removed.
- The generated C++ keeps its `static_assert`s on `sizeof`, `alignof` and `offsetof`: the compiler still checks the
  generator's layout.
- **Emitters are a C++ interface compiled into `oly-schema`.** The central generator owns parse, validation, layout and
  hash; each client brings its emitter in its own folder (`environments/web/clients/typescript/codegen/`). Adding a
  language does not touch the generator core. Command line: `oly-schema <file.oly> --emit cpp=<dir> --emit ts=<file>`.

### 4.3 What changes for the TS client

The generated file has the shape TS games already use, so game code changes only the import path:

```ts
// schema.generated.ts: generated, do not edit
export const Position = componentType("Position", { x: "f32", y: "f32" }, 0, { size: 8, alignment: 4, offsets: { x: 0, y: 4 } });
export const move = actionType("move", "axis2d", 0);
export default schemaOf([Position, move], 0x3f2a91c4);
```

```ts
import { Position, move } from "../schema.generated.js";
```

- `component`, `tag`, `event`, `action`, `state`, `defineSchema` and `computeLayout` leave the public API of
  `@oly/core`. The TS validation of names moves to the C++ parser.
- Generated code imports its constructors from a subpath `@oly/core/generated` **[author decision]**. The boundary lint
  allows that subpath only in `*.generated.ts`, so hand-written game code cannot declare a schema.
- Types stay literal: `query({ write: [Position] })` and `block.write(Position)` keep the field names typed as today.
- `loadRuntime` keeps the hash and size check against the WASM, and `SchemaMismatchError` keeps its meaning.
- WASM tests that today build schemas on the fly (`load.test.ts` mismatches, foreign actions and states in
  `actions.test.ts` and `states.test.ts`) get one small `.oly` file per case under `engine/testing/`, generated to TS
  like any game schema **[author decision]**. No lint exception and no coverage lost.
- `schema.generated.ts` is generated on every build and never committed **[author decision]**: a committed copy would
  need a drift check, which is the "written twice" anti-goal in another form. Build order changes: `oly-schema` is
  built first, then generates each game's TS, then `tsc` runs. The `Makefile` targets (`ts`, `game`, `games-test`)
  and the npm workspaces follow that order in both Docker images.
- `oly_add_game` takes `SCHEMA <file.oly>` instead of `SCHEMA_JS`. A C++-only game no longer needs Node.

`@oly/codegen` (`engine/tools/codegen/`) is removed after `arena` and the fixture switch to `.oly`.

## 5. The neutral C API

- **Per-engine script invoker** **[author decision]**:

  ```c
  typedef void (*OlyScriptInvoker)(void* userData, OlyEngine* engine, uint32_t kind, uint32_t id,
                                   const void* events, uint32_t eventCount);
  OLY_API void olyEngineSetScriptInvoker(OlyEngine* engine, OlyScriptInvoker invoker, void* userData);
  ```

  `userData` lets a managed client pass its own handle (for example a C# `GCHandle`). The `kind` contract
  (`OLY_SCRIPT_SYSTEM`, `_HANDLER`, `_TASK`, `_TASK_DISCARD`, `_STATE_HOOK`) does not change.
- Calling a script with no invoker installed stays a contract error (`OLY_ASSERT`, with kind and id).
- The `EM_JS` import moves to the web host's C++ part (`environments/web/host/native/`), compiled into every web
  game's WASM. It exports one function that installs, on a given engine, an invoker that calls
  `Module["olyInvokeScript"]`; the TS client calls it on engine creation **[author decision]**. No `addFunction`, so no
  table growth and no change to the link options. Whether this costs a function-pointer call more than today on the web
  hot path is checked with the existing `bridge` benchmarks; the target is no measurable regression.
- `oly::bridge::setScriptInvoker` (process-wide test hook) is removed; tests use `olyEngineSetScriptInvoker`.
- `OLY_API` is the only Emscripten-aware line in the core: `EMSCRIPTEN_KEEPALIVE` on WASM, default visibility
  (or `__declspec(dllexport)` on Windows) on native.
- Pointers in the C API (column addresses, event queues) are addresses in the core's memory. On `web` the client reads
  them through the WASM heap; on `native` they are process pointers. The C API does not change for that.

## 6. The `native` environment

Revised on 2026-11-13, before phase 4, with the base at `a96068a` (phases 1–3 done). Two sentences of the first version
are replaced: "`setHidden` on minimize and focus loss" (§6.2, Visibility and focus) and "a hook to draw after each
`frame`" (§6.2, Drawing).

### 6.1 Approaches for the host

- **A. The host is a library the game calls** **[user decision]**. The game's `desktop/main.cpp` owns `main`: it
  creates the `World` and the `Engine`, declares the schema, registers its systems and calls the host's loop. This is
  the shape of `runInBrowser(engine)` on `web`, and a test runs a fixed number of frames by passing an option, with no
  other entry point.
- **B. The host is a framework that owns `main`** and calls functions the game implements (`setup(Engine&)`,
  `draw(...)`). Rejected: less repeated code in each game, but `main` is hidden, the call order is implicit, and the
  headless test needs a second entry point.

### 6.2 Host

A C++ module in `engine/environments/native/host/`, on **SDL3** **[author decision]**. It depends on `app` and SDL3,
never on `capi`. It has no engine rules (§3.1). The shape, with names left to the builder (§10):

```cpp
oly::native_host::Window window({.title = "Example", .width = 960, .height = 540});
engine.addSystem(oly::app::Phase::Render, "draw", draw, window.renderer());

return oly::native_host::run(engine, window, {.maxFrames = 0});
```

- **Loop.** Each iteration reads the pending SDL events and turns them into input calls, clears the renderer, calls
  `engine.frame` with a monotonic time in seconds, then presents. It ends on the window's quit event or after
  `maxFrames` frames (0 = no limit), and `run` returns the process exit code.
- **Drawing** **[user decision]**: the game draws in a system of the `Render` phase and gets the SDL renderer through
  that system's `userData`; there is no separate draw hook. It is how `arena` draws on `web`
  (`games/arena/web/src/game.ts`), and drawing keeps the engine's rules for `Render`: state restriction, nothing runs
  while hidden, registration order. The game also gets the `SDL_Window` and `SDL_Renderer` from `Window`. The engine
  draws nothing.
- **Visibility and focus** **[user decision]**: window minimized, hidden or occluded → `setHidden(true)`; restored or
  exposed → `setHidden(false)`; focus lost → `releaseAll` only. The browser does the same: only a hidden tab calls
  `setHidden`, and `blur` only releases the buttons. Hiding on focus loss would freeze the game whenever the player
  clicks another window.
- **Keyboard** **[author decision]**: `oly::input::Key` values are HID usages (page `0x07` in the high 16 bits), and
  SDL3 scancodes are the usages of that same page, so the button is `0x0007'0000 | scancode`, with no table. A test
  checks the assumption for every key `Key` declares. Only scancodes that `Key` declares are forwarded, as the browser
  host forwards only known button names.
- **Mouse**: left, right and middle map to `oly::input::MouseButton`. A press counts only inside the window and a
  release counts anywhere, as on `web`. The pointer position is in the renderer's logical coordinates, not in screen
  pixels, as the browser host reports canvas pixels.

### 6.3 Schema declaration

- The C++ emitter of `oly-schema` emits a function that declares the schema into an `oly::app::Engine`: events,
  actions and states in schema order, with the same order checks `olyEngineCreate` makes today. The phase 3 plan left
  it to this phase, which brings its first consumer.
- `olyEngineCreate` calls it instead of walking the tables itself; a native game's `main` calls it after creating the
  `Engine`. A native game and the native host do not link `capi`. Whatever `capi` still needs from the generated tables
  for its own checks stays available to it.

### 6.4 Game build

- `oly_add_game` takes `ENVIRONMENT web|native`. On `web` it produces `<game>_game.{mjs,wasm}` as today. On `native` it
  produces an executable from the game's C++ (`native/`), its shell (`desktop/`), the core and the native host, and
  generates only the C++ schema.
- SDL3 enters through `cmake/Dependencies.cmake` with FetchContent and a pinned tag, as the other dependencies do, and
  only when the native host is built (a CMake option). WASM builds never fetch it.
- The shared library for future clients is out of scope.

### 6.5 Proof of the C API outside WASM

The proof is a real C ABI, the one a C#, Rust or Lua client consumes **[user decision]**:

- The public C API headers become valid C. Today they are C++ only (`<cstdint>`, `extern "C" {` without a
  `__cplusplus` guard, `OlyWorld*` and the other handles used without `struct` or `typedef`). They use `<stdint.h>`,
  guard `extern "C"`, and declare a `typedef` for every opaque handle and struct. The project enables `LANGUAGES C`.
- They are renamed from `.hpp` to `.h` (`oly/capi/c_api.h`, `oly/capi/engine_api.h`) **[author decision]**: a C client
  reads the extension as the language of the file. Following the glossary rule, there is no alias header; every
  include moves.
- The test is a `.c` file that includes only those headers and drives an engine only through `oly*` functions and
  `olyEngineSetScriptInvoker`, playing the role of a foreign client (spawn, query, systems, events, tasks, state hooks).
  It runs on Linux and under Wine.
- This is a public-API change, so the guide and `capi.md` change in the same phase. The existing C++ suites
  `capi.engine_api` and `capi.c_api` stay.

### 6.6 Example game

One C++-only game in `games/`, with a `desktop/` shell, that runs on `native` **[author decision]**. Its theme and name
are left to the builder; it follows the example-game layout and has its own `README.md`.

### 6.7 Platforms: Linux and Windows **[user decision]**

Both are built in Docker, since the host machine has no C++ compiler.

- **Linux is playable** **[user decision]**: the executable opens a real window on a Linux desktop. The `dev` image
  gains the X11, Wayland, xkbcommon and EGL/GL development packages, and SDL3 is built with those video backends. The
  native host tests, the C API test and the example game also run headless in ctest, with SDL's `dummy` video driver
  and `maxFrames`. The real window is checked by hand (WSLg on the Windows host); ctest cannot check it.
- **Windows** **[author decision]**: a separate Docker image with mingw-w64 and Wine, and its own compose service, so
  the `dev` image does not carry a second toolchain. A cross-compile preset uses a toolchain file that sets
  `CMAKE_CROSSCOMPILING_EMULATOR=wine`, so ctest runs the same Windows `.exe` tests under Wine with no extra script.
  This exercises the Windows branch of `OLY_API` (`__declspec(dllexport)`). Rejected: a link-only check, which does not
  prove the `.exe` starts.
- The example game's `.exe` runs with a double click on a Windows machine that has nothing installed: SDL3 and the
  MinGW runtime are linked statically or shipped next to it (left to the builder).

### 6.8 Docs in this phase

The native host and `ENVIRONMENT` in `oly_add_game` are public API, so phase 4 documents them in the guide as it is
then (the `CLAUDE.md` rule); phase 5 moves that text into `environments/native/getting-started.md`.

## 7. The user guide

```
docs/guide/
  README.md               the map: core, environments, how to choose, learning path per environment
  concepts/               valid in every environment; examples in C++
    schema.md  world-and-entities.md  queries.md  engine-and-systems.md
    events.md  input.md  schedule.md  states.md  performance.md
  environments/
    web/
      getting-started.md  host, WASM build, first game in the browser
      typescript.md       the TS client: the equivalent of each concept
      testing.md          Node with a fake clock and fake input
      boundary.md         the WASM↔JS boundary cost and how to keep the hot path fast
    native/
      getting-started.md  host, build, first desktop game
  recipes.md              recipes in C++, with a link to each client's version where it differs
  cheat-sheet.md          C++, C API and per-client names
  troubleshooting.md
```

- Every concept page teaches in C++, the engine language. `10-cpp-gameplay.md` disappears: its content is spread over
  `concepts/` and the environment pages.
- `docs/development/guide-style.md` changes with it: the reader is a game developer in any supported language, examples
  are C++ first, each client's page shows the same concept in its language, and the files-and-navigation rules
  describe the new folders. The game-agnostic rule stays.
- The numbered learning path is replaced by per-environment paths listed in `README.md` **[author decision]**: with
  concepts shared and environments separate, a single global number order no longer fits.

## 8. Order of work **[author decision]**

Each phase ends with every existing test passing and `arena` running on `web`.

1. **Move.** New tree with `git mv`; `bridge` → `capi`; `@oly/core` under `environments/web/clients/typescript/`;
   `@oly/client` → `@oly/web-host` under `environments/web/host/js/`;
   build, lint, npm workspaces and docs paths updated. No behaviour change.
2. **Neutral callback.** `olyEngineSetScriptInvoker`, `EM_JS` moved to the web host, test hook removed, new boundary
   lint rules. Benchmark check of §5.
3. **Schema.** `.oly` language, `oly-schema` with C++ and TS emitters, hash parity test, `arena` and the fixture switched,
   TS schema API and `@oly/codegen` removed.
4. **Native.** SDL3 host, `oly_add_game` per environment, C API test as a foreign client, the C++-only example game,
   Linux build with headless tests and the Windows cross-compile preset.
5. **Docs.** Guide reorganized (§7), guide style, architecture docs (README rules and diagram, `decisions.md`,
   `repository.md`, `modules-and-boundaries.md`, `schema-and-codegen.md`, `bridge.md` → `capi.md`, `ts-packages.md`,
   `limits.md`, `glossary.md`), `CLAUDE.md`, `docs/development/commands.md`.

Docs that describe a public API changed in phases 1–4 are updated in the same phase (the `CLAUDE.md` rule); phase 5 is
the reorganization of the guide itself.

## 9. Decisions this design replaces

| Old decision (`decisions.md`) | New decision |
|---|---|
| TS schema as the single source + codegen + hash | `.oly` as the single source + C++ generator + hash; each client gets generated code |
| A single imported JS function, with `kind` | A per-engine invoker set through the C API; the web host installs the JS one |
| `@oly/core` without DOM, `@oly/client` with DOM | Same split: `@oly/core` is the TS client (no DOM), `@oly/web-host` is the web host (DOM) |
| Rule 3 of the architecture README | "There is a single source for the data layout: the game's `.oly` file" |

## 10. Left to the builder

- The exact `.oly` grammar within §4.1, and the parser technique.
- The shape of the emitter interface and how client emitters are registered in the build.
- How `oly-schema` is built in each Docker image (a host compiler in the `wasm` image may need to be added).
- The SDL3 tag and the name of the CMake option that turns the native host on.
- The native host's C++ API names, following the glossary and code style.
- How the Windows `.exe` carries SDL3 and the MinGW runtime (static link or files next to it), and which test suites
  run under Wine besides the native host, the C API test and the example game.
- The example native game's theme and name.
- Plan granularity inside each phase.
