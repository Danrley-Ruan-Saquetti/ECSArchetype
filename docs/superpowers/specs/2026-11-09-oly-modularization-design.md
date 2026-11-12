# Oly — modularization and review of the public APIs — design

Date: 2026-11-09. Base: branch `develop` after engine phase 1 (`docs/superpowers/plans/2026-11-09-engine-phase1-bridge.md`).
Specs that remain valid: `2026-10-16-ecs-archetype-spec.md` (requirements `R-*`, invariants `I-*`, targets `NF-*`) and
`2026-11-09-game-engine-design.md` (the engine and phases 2–7).

Marking: **[user decision]** was decided in the conversation; **[author decision]** was my choice during the design,
which the user may review; the rest is a direct consequence of the two.

## 1. Intent

The engine is now called **Oly** **[user decision]**. The name has no meaning and applies to the whole engine:
C++ core, C API, TS packages and build.

Today the tree does not show the architecture. The ECS occupies the root (`include/`, `src/`, `tests/`, `bench/`) as if it were the
whole project, and the engine is a sibling folder (`engine/`) with another convention: C++ `tests/` next to TS `test/`,
generator, fixture and TS package at the same level. The layers exist in the code, but hold only by discipline: any
file includes any header, and each layer's public surface exposes internal details.

The goal is to organize the project like a large engine (in the spirit of Unreal, O3DE and Bevy). The user's
criteria are **clarity, cohesion, code design, good contracts and a cohesive API** **[user decision]**:

- each module is a self-contained unit (public API, private implementation, its own tests and benchmarks) that
  declares which modules it depends on;
- **the build enforces the boundaries**: a module only sees the public headers of the modules it declared as a
  dependency;
- the three public surfaces (C++ ECS, C API, TS packages) expose only what users need, with a single
  vocabulary;
- phases 2–7 of the engine spec (loop, events, input, schedule, states, game) come in as new modules without
  touching the existing ones.

## 2. Goals, non-goals, anti-goals

**Goals**
- New tree (§3), with history preserved (`git mv`).
- Per-module build, with declared dependencies and checked boundaries (§4).
- The Oly name applied to namespaces, includes, targets, options, C API, packages and errors (§5).
- The nine points of the API survey resolved (§6–§8), with a single glossary (§5.2).
- The TS package split into `@oly/core` (does not require a browser) and `@oly/client` (thin browser layer) (§8)
  **[user decision]**.

**Non-goals**
- New functionality. Loop, phases, events, input, schedule and states remain in phases 2–6.
- Changing behaviour, data layout, validity rules or the ECS hot path.
- Installing and exporting as a CMake package (`install`/`find_package`) and publishing the npm packages.
- Renaming the repository directory or the remote repository **[author decision]**.
- Rewriting old specs and plans. They are a historical record and keep citing the old paths
  **[author decision]**.

**Anti-goals** (failure even if it works)
- Regressing the ECS: NF-P1 (≤ 1.10), NF-P2 (≤ 1.25) and NF-P5 (`/1000` ≤ 0.5) remain met, and all tests
  pass in all presets.
- A boundary that holds only by convention when the build can enforce it.
- Hiding ECS details in a way that removes inlining from the hot path (pimpl, virtual or indirection in
  `Query<Ts...>::each`/`eachBlock`, `World::get`).
- Compatibility aliases or shims with the old names. There is no external user: the switch is clean
  **[author decision]**.
- Game concepts inside `engine/` (a rule that already exists).

## 3. Tree

```
<root>/
  CMakeLists.txt  CMakePresets.json  Makefile
  Dockerfile  Dockerfile.wasm  docker-compose.yaml  .clang-format  .gitignore  CLAUDE.md
  package.json  package-lock.json  tsconfig.base.json      # npm workspaces (§8.1)
  cmake/
    OlyOptions.cmake        # OLY_* options, asserts, sanitizers
    OlyWarnings.cmake       # oly_warnings(<target>)
    OlyWasm.cmake           # oly_wasm_exceptions, oly_wasm_node_env
    OlyModule.cmake         # oly_add_module, oly_add_test, oly_add_bench, oly_add_contract_death
    OlyGame.cmake           # oly_add_game (generator + link of the .mjs module)
    Dependencies.cmake      # doctest, Google Benchmark (FetchContent)
    wasm/node_env.js        # today in tools/wasm/
  engine/
    runtime/                # C++, one module per folder
      core/
        include/oly/core/   assert.hpp  align.hpp  small_map.hpp  type_name.hpp
        src/                assert.cpp
        tests/
      ecs/
        include/oly/ecs/    world.hpp  entity.hpp  component.hpp  query.hpp  query_by_id.hpp
                            command_buffer.hpp  diagnostics.hpp
                            detail/  table.hpp  archetype.hpp  entity_store.hpp  query_cache.hpp
                                     command_plan.hpp  ...                       (§6.1)
        src/
        tests/              doctest suites, contract_death, fuzz, support/
        bench/              iterate, fragmentation, flush, transitions, common.hpp
      bridge/
        include/oly/bridge/ c_api.hpp  world.hpp  schema_registry.hpp
        src/
        tests/
    packages/               # TS, one npm package per folder
      core/                 # @oly/core
        src/  tests/unit/
      client/               # @oly/client
        src/  tests/
    tools/
      codegen/              # @oly/codegen: TS schema → C++
        src/  tests/
    testing/                # @oly/testing (private): engine WASM integration
      fixture/              # minimal test game: schema.ts, systems.cpp (only place with game names)
      tests/wasm/           # today engine/test/wasm/
      bench/                # boundary.ts (boundary benchmark)
  games/                    # empty until phase 7 (validation game)
  tools/                    # measurement scripts: bench_ratio.py, bench_ladder.py, perf_fragmented.sh, ...
  docs/
    superpowers/specs/  superpowers/plans/
    benchmarks/             # today bench/results/: README.md, ecs/, wasm/, bridge/ (§9)
```

Tree decisions:

- **`engine/runtime/` (C++) and `engine/packages/` (TS) separated by language**, because each side has its own
  build and dependency system (CMake and npm workspaces) **[author decision]**.
- **C++ module = `include/oly/<module>/` + `src/` + `tests/` + `bench/`.** The include path
  `oly/<module>/...` repeats the module name on purpose: the include already says where the header comes from.
- **`core` (C++)** receives what is today `include/ecs/detail/` and is not ECS-specific: `assert`, `align`,
  `small_map`, `type_name` **[author decision]**. `type_info` stays in `ecs` (it is the component registry).
  The builder may move more things to `core` if they are truly generic.
- **`engine/testing/`** is the private package `@oly/testing` with the fixture game (schema + `systems.cpp`, assembled with
  `oly_add_game`), the WASM integration tests and the boundary benchmark. They depend on the fixture, and the fixture
  depends on `@oly/core`. Putting them inside `@oly/core` would create a cycle in the project references and put game
  names in the package. So `core` keeps only the unit tests, and the rule "game names only in the fixture"
  now applies to `engine/testing/fixture/` **[author decision]**. `games/` stays reserved for the phase 7
  validation game.
- **Benchmarks:** the code goes inside each module (`engine/runtime/ecs/bench/`,
  `engine/testing/bench/`) and the recorded results go to `docs/benchmarks/`. They are measurement
  documentation, not code **[author decision]**.
- **`tools/`** keeps only the measurement and analysis scripts. What is part of the build (`node_env.js`) goes to `cmake/`.

## 4. Build and boundaries

### 4.1 C++ modules

`oly_add_module(NAME <name> DEPENDS <modules...> [HEADER_ONLY])` creates the target `oly_<name>` with alias `oly::<name>`,
compiles `src/*.cpp`, publishes `include/` and links **only** the modules in `DEPENDS`. Dependencies of the phase:

```
oly::core  ←  oly::ecs  ←  oly::bridge  ←  games (oly_add_game)
```

So the rule "the ECS does not know the engine" becomes enforced by the build: `oly::ecs` does not link `oly::bridge`, so
`#include "oly/bridge/..."` inside the ECS does not compile. From phase 2 on, new modules (`app`, `events`, `input`,
`schedule`, `state` or whatever the phase's plan calls them) go into `engine/runtime/<name>/` with their own
`oly_add_module`.

Dependencies are linked as `PUBLIC` (the `ecs` headers include `core`, and the `bridge` headers expose the
`World`), so CMake makes them visible transitively. Even so, **each module and each game declares in
`DEPENDS` every module it includes directly**, and `oly.lint.boundaries` (§4.2) rejects `#include "oly/<m>/..."`
in a module that did not declare `<m>` **[author decision]**. A game, for example, declares `ecs` and `bridge`.

If a C++ module needs to be `OBJECT` (like `engine_core` today, so that the `EMSCRIPTEN_KEEPALIVE` symbols are not
discarded at link time), `oly_add_module` accepts that as an option. Today this applies to `bridge`.

### 4.2 Internal details (`detail/`)

The hot-path templates (`World::spawn<Ts...>`, `Query<Ts...>::each`, `World::get<T>`) need the
definitions of `Table` and `Archetype` at the point of use. Hiding these types physically (pimpl, virtual interface)
would violate an anti-goal (§2). Therefore:

- the internal types live in `include/oly/ecs/detail/` and in the namespace `oly::ecs::detail`;
- **a check in ctest** (`oly.lint.boundaries`, a script in `tools/` or a CMake script) fails if any file
  outside `engine/runtime/ecs/` includes `oly/ecs/detail/`. The module's own tests and benchmarks may include it.
  The same check covers `oly/core` and `oly/bridge`, if they have `detail/`, and the TS (§8.3).

In other words: the compiler sees the details, but the build rejects anyone who uses them from outside.

### 4.3 Options, presets, Makefile and test names

- `ECS_*` options become `OLY_*` (`OLY_BUILD_TESTS`, `OLY_BUILD_BENCH`, `OLY_SANITIZE`, `OLY_WERROR`,
  `OLY_VEC_REPORT`, `OLY_ENABLE_ASSERTS`), and `ENGINE_BUILD_FIXTURE` becomes `OLY_BUILD_GAMES`.
- Presets: `debug-asan`, `release`, `relwithdebinfo`, the WASM cost study variants and `wasm-debug` keep
  their names. `wasm-engine` and `wasm-engine-release` become `wasm-games` and `wasm-games-release`, because what
  distinguishes them is compiling the games **[author decision]**.
- Makefile: `engine-ts` becomes `ts` (npm ci if needed + build of all workspaces) and `engine-test` becomes
  `games-test` (`ts` + ctest on `wasm-games`). `configure`/`build`/`test`/`bench` stay as they are.
- Tests in ctest with a module prefix: C++ as `<module>.<suite>` (`ecs.unit_world`,
  `ecs.contract_death.<case>` or equivalent, `bridge.c_api`, `core.<suite>`) and TS as `ts.<package>.<suite>`
  (`ts.core.unit`, `ts.client.unit`, `ts.codegen.unit`, `ts.testing.wasm`), plus `oly.lint.boundaries`. So
  `make test T='^ecs\.'` runs one module, and the C++ `core` is not confused with `@oly/core` **[author decision]**.
- The WASM cost study presets (`native-clang*`, `wasm-scalar`, `wasm-simd`) keep compiling and passing
  the tests, and `tools/run_ladder.sh`, `bench_ladder.py` and `extract_codegen.sh` are updated to the new target
  names. They are part of the zero-regression requirement **[author decision]**. The benchmark executables get the module prefix
  (`ecs_bench_iterate` etc.), and the `tools/` scripts that call them are updated.

## 5. Name and vocabulary

### 5.1 Where the Oly name appears **[user decision, author mapping]**

| Place | Before | After |
|---|---|---|
| C++ namespace | `ecs::`, `engine::` | `oly::ecs`, `oly::bridge`, `oly::core` |
| Includes | `"ecs/world.hpp"` | `"oly/ecs/world.hpp"` |
| CMake targets | `ecs`, `engine_core` | `oly::ecs`, `oly::bridge`, `oly::core` |
| CMake functions | `ecs_add_test`, `engine_add_game` | `oly_add_module`, `oly_add_test`, `oly_add_game` |
| Assert | `ECS_ASSERT`, `ECS_ENABLE_ASSERTS` | `OLY_ASSERT`, `OLY_ENABLE_ASSERTS` (in `oly::core`) |
| C API | `engine*`, `ENGINE_API` | `oly*`, `OLY_API`; types `OlyWorld`, `OlyQuery`, `OlyIteration`, `OlyEntity` |
| npm packages | `engine` | `@oly/core`, `@oly/client`, `@oly/codegen` |
| TS errors | `EngineContractError` | `ContractError` **[user decision]**; `SchemaMismatchError` keeps its name |
| TS runtime | `EngineRuntime`, `loadEngine`, `EngineModule` | `Runtime`, `loadRuntime`, `OlyModule` (internal) **[author decision]** |

The `oly` prefix appears **only in the C API** and in what is internal to the bridge. Reason: `extern "C"` symbols live in a single
global space of the WASM module, together with libc, the Emscripten runtime and the C exports of the game's systems, and
`namespace` does not change the symbol name. Since only the `Bridge` calls these exports, the prefix never reaches engine
users. The C++ and TS APIs do not repeat `oly` in names.

### 5.2 Glossary (the same verb for the same concept in C++, C API and TS)

| Concept | C++ (`oly::ecs`) | C API | TS (`@oly/core`) |
|---|---|---|---|
| create entity | `World::spawn()`, `spawn(components...)`; `CommandBuffer::spawn()` | `olyEntitySpawn` | `World.spawn(types, values?)` |
| destroy entity | `destroy` | `olyEntityDestroy` | `destroy` |
| alive entity | `isAlive` | `olyEntityIsAlive` | `isAlive` |
| add / remove / has | `add`, `remove`, `has` | `olyComponentAdd`, `olyComponentRemove`, `olyComponentHas` | `add`, `remove`, `has` |
| read a present component (contract: it exists) | `get` → `T&` | — | `get` → copy |
| read if it exists | `tryGet` → `T*` or null | `olyComponentGet` → address or 0 | — |
| partial field write | — (already `get(e).x = ...`) | — | `set` |
| query | `query<Ts...>(With, Without)` | `olyQueryCreate` | `query(spec)` |
| iterate | `each`, `eachBlock` | `olyQueryBegin` / `olyQueryEnd` | `eachBlock` |
| runtime ID variant | suffix **`ById`**: `addById`, `removeById`, `hasById`, `getById`, `tryGetById`, `queryById` | (the C API is entirely by ID) | — |

`create` no longer exists as an entity verb. "Raw" no longer exists.

## 6. C++ ECS (`oly::ecs`)

### 6.1 Public headers

| Header | Contents |
|---|---|
| `world.hpp` | `World` (spawn/destroy/isAlive/isIterating, add/remove/has/get/tryGet, the `ById` variants, `commands`, `flush`, `query`, `queryById`) |
| `entity.hpp` | `Entity`, `NullEntity`, `EntityHash` |
| `component.hpp` | `ComponentId`, `componentId<T>()`, `TypeInfo`, `typeInfo`, `registeredTypeCount` (today in `type_info.hpp`) |
| `query.hpp` | `Query<Ts...>`, `With`, `Without` |
| `query_by_id.hpp` | `QueryById`, `QueryByIdSpec`, `BlockById`, `QueryIteration` (§6.3) |
| `command_buffer.hpp` | `CommandBuffer`: `spawn`, `destroy`, `add`, `remove`, `addById`, `removeById`, `commandCount`, `empty`, `clear` |
| `diagnostics.hpp` | `checkInvariants(const World&, std::string* why)`, `stats(const World&)`, `WorldStats`, `ArchetypeStats`, `signatureLookupCount(const World&)` |

These go to `detail/` (namespace `oly::ecs::detail`): `Table`, `Archetype`, `Signature`, `Edge`, `EntityStore`,
`EntityRecord`, `QueryCacheBase` (with `onArchetypeCreated`/`initialize`/`matches`/`isMatched`), `MatchedTable`,
`IterationGuard`, `signatureMatches`, `CommandOperation`, `CommandView`, `CommandPlan`, `EntityPlan`, `NetEffect`,
`NetComponentOperation`. The methods `plan()`, `netEffect()`, `forEach()` and `store()` also leave the public surface of
`CommandBuffer`: the `World` uses them through `friend` or through an accessor in `detail`.

### 6.2 Diagnostics and test hooks

- `stats`, `signatureLookupCount` and `checkInvariants` leave the `World` class and become free functions in
  `diagnostics.hpp`, with access through `friend`. `checkInvariants` stays compiled in every build (`bridge`
  and the tests use it).
- `flushSequential` (oracle without coalescing, used only in tests) and `EntityStore::setGenerationForTest` leave the API.
  They stay accessible only to the module's tests, through a `detail::WorldTestAccess`/`detail::EntityStoreTestAccess` accessor
  declared in `detail/` and used in `engine/runtime/ecs/tests/` **[author decision]**. The builder chooses the exact
  form (`friend struct` or another), as long as the public class has nothing "for testing".
- `CommandBuffer::forEach` is used today by `tests/command_buffer.cpp`. It goes through the same test accessor.

### 6.3 By-ID API

- `World::rawQuery(RawQuerySpec)` becomes `World::queryById(QueryByIdSpec)`, with the same semantics (cache by
  spec; `read` before `write`; tag in `read`/`write`, repeated type and unknown ID remain contract
  errors).
- **The block does not expose `Table`.** `QueryById::eachBlock(callback)` calls `callback(const BlockById& block)`, where
  `BlockById` has `count()`, `entities()` (`std::span<const Entity>`) and `column(i)` (`void*` of the column of the i-th
  component, in read-then-write order).
- **The manual `beginIteration`/`endIteration` pair is no longer public.** In its place comes
  `QueryIteration QueryById::iterate()`: a move-only RAII object that marks the `World` as iterating while it
  exists, exposes its own blocks (count, entity address, column addresses) and ends the iteration
  in its destructor. Nesting is possible: each iteration has its own blocks.
- **No per-iteration allocation in steady state:** the block buffers are reused, one per nesting level
  of the `QueryById` (a pool that grows to the deepest level seen and does not shrink), instead of a new vector on every
  `iterate()` **[author decision]**. Today the world's buffer is reused, and the boundary must not start
  allocating on every TS `eachBlock`.
- `addRaw`/`removeRaw`/`hasRaw`/`getRaw` become `addById`/`removeById`/`hasById`/`tryGetById`. `getById` is also added,
  with the same contract as `get<T>` (assert if the entity is dead or the component absent).
  `CommandBuffer::addRaw` becomes `addById`. The template APIs keep their own path, without delegating to the
  `ById` ones on the hot path (same decision and same reason as today).

### 6.4 Remaining

- `World::create` becomes `World::spawn`, in both forms.
- Assert messages follow the new verbs (`"spawn during iteration..."`, `"queryById ... tag"`). The
  `contract_death` cases require the new substrings.
- `Query<Ts...>::writes` (reserved for the scheduler, R-QY-1) stays.

## 7. C API (`oly::bridge`)

### 7.1 Names: `oly<Object><Verb>`

```
olyWorldCreate  olyWorldDestroy  olyWorldCheckInvariants
olySchemaHash  olySchemaComponentCount  olySchemaComponentId
olyComponentSize  olyComponentAlignment                               // per ComponentId (type)
olyEntitySpawn  olyEntityDestroy  olyEntityIsAlive
olyComponentAdd  olyComponentRemove  olyComponentHas  olyComponentGet // on the entity
olyQueryCreate  olyQueryBegin  olyQueryEnd
```

Signatures, return types and conventions (`int32_t` 0/1 for boolean, output through pointer, `(index, generation)`
on input) follow the current ones. The builder may fix inconsistencies it finds, as long as they are recorded in the
plan.

### 7.2 Iteration with its own buffer

`olyQueryBegin(world, query, outBlockCount, outDescriptors)` returns an `OlyIteration*` (a `QueryIteration` from
§6.3) with its descriptors (same format as today: `count`, entity address, address of each column).
`olyQueryEnd(world, iteration)` releases it. The descriptors of an iteration are valid until its `olyQueryEnd`, so a
nested `eachBlock` does not overwrite the outer one, and the TS no longer needs to copy the buffer (it may keep copying
if that measures better; it is the builder's choice).

### 7.3 Opaque `OlyWorld`

`world_handle.hpp` (which exposed `blockDescriptors` to games) no longer exists. The `OlyWorld` type is opaque in the C API,
and the games' C++ systems obtain the `World` through `oly::bridge::worldOf(OlyWorld*) -> oly::ecs::World&` in
`oly/bridge/world.hpp`. The schema link contract remains: `oly::bridge::schemaHash()` and
`oly::bridge::schemaComponentIds()` are defined by the game's generated code (or by hand in the `bridge` tests).

## 8. TS packages

### 8.1 Workspaces

`package.json` at the root with `workspaces: ["engine/packages/*", "engine/tools/*", "engine/testing", "games/*"]`,
a single `package-lock.json` and `tsconfig.base.json` (`strict`, ES2022, `NodeNext`). Each package has its own
`package.json` and `tsconfig.json` with **project references**: `@oly/client`, `@oly/codegen` and `@oly/testing`
reference `@oly/core`, and `core` references nobody. Build with `tsc -b` at the root.

Each package's output goes to **`dist/` inside the package itself** (gitignored), not to `build/ts/`. Node does not
accept an `exports` target outside the package, and resolution by name (`@oly/core`, `@oly/core/internal`) through the
workspaces requires this **[author decision]**. The paths CMake uses (the `oly-codegen` bin, the fixture's `schema.js`,
`node --test`) now point to these `dist/` folders, and `module-package.json` no longer exists. Versions stay pinned and exact
(TypeScript 5.6.3, `@types/node` 20.17.6), with no bundler and with `node:test`.

### 8.2 `@oly/core`: the engine on the TS side, without a browser **[user decision]**

Contains everything that exists today in `engine/ts/`. It does not use DOM, `window`, `requestAnimationFrame`, `fetch` or keyboard and mouse
events, and runs in node, in the browser and in a worker. What depends on the environment comes in through injection: today, the
WASM module factory in `loadRuntime(factory, schema)`; in the following phases, clock and input source.
The core's `tsconfig` has no `lib: ["DOM"]`, so the compiler rejects use of browser APIs.

**Public surface** (`"."` entry of `exports`):
- schema: `component`, `tag`, `defineSchema`, `Schema`, `ComponentType`, `TagType`, `SchemaType`, `Fields`,
  `ValueOf`, `ScalarType`, `ComponentLayout`/`FieldLayout` (as read-only data in `type.layout`);
- runtime: `loadRuntime`, `Runtime` (`schema`, `createWorld()`), `OlyModuleFactory`;
- world: `World` (`spawn`, `destroy`, `isAlive`, `has`, `add`, `remove`, `get`, `set`, `query`, `checkInvariants`,
  `dispose`, `handle`), `SpawnValues`;
- query: `Query` (`eachBlock`), `QuerySpec`, `Block` (`count`, `entity`, `read`, `write`), `ReadView`,
  `WriteView`, `ReadField`, `WriteField`, `RawField`;
- entity: `Entity`, `NullEntity`, `entityIndex`, `entityGeneration`, `formatEntity`;
- errors: `ContractError`, `SchemaMismatchError`.

**Internal** (`"./internal"` entry, only for `@oly/client`, `@oly/codegen`, `@oly/testing` and the core's tests): `Bridge`, `Heap`,
`readScalar`/`writeScalar`, `makeView`, `computeLayout`, `scalarSize`/`scalarAlignment`, `fnv1a32`,
`canonicalSchema`, `schemaHash`, `packEntity`, `MaxEntityGeneration`, `OlyModule` (`_oly*` exports), `Runtime.bridge`
and `Runtime.module`.

**Members that stop being public:** `World.iterationDepth` (today a mutable field), `World.assertUsable`,
`Block.invalidate`, `Query.writes`, `Query.positionOf`. The mechanism (`#private`, module functions, `@internal` with
`stripInternal`) is left to the builder. The requirement is that the public `.d.ts` of `@oly/core` shows none
of them.

### 8.3 `@oly/client`: thin browser layer **[user decision]**

Depends only on the public surface of `@oly/core` (and on `./internal` if unavoidable, justified in the plan). It does not
add engine or game rules: it only implements, with browser APIs, the ports the core receives through
injection. Whoever uses only `core` works the same, without `client`.

In this round it starts with the minimum: a factory that loads a game's Emscripten module from a URL
(the `.mjs` and the `.wasm` next to it) and returns an `OlyModuleFactory` for `loadRuntime`. Tests in node with the import
replaced. The client's `tsconfig` includes `lib: ["DOM"]`. The rAF loop (phase 2) and input (phase 4) arrive in their
respective phases.

### 8.4 `@oly/codegen`

The generator (`emit_cpp`, `main`) leaves the engine's TS package and becomes a build tool, with bin `oly-codegen
--schema <schema.js> --out <dir>`. It uses `@oly/core` (schema) and `@oly/core/internal` (layout, canonical hash).
`oly_add_game` calls it. The generated output now uses the new names (`oly/ecs/entity.hpp`, `oly::ecs::Entity`,
`oly::bridge::schemaHash`).

### 8.5 Boundary checking in TS

`oly.lint.boundaries` (§4.2) also rejects: import of `@oly/core/internal` outside `engine/` (games do not use the
internal entry); import of `@oly/client` by `@oly/core`; relative paths that cross packages (`../../core/src/...`)
instead of the package name. A surface test checks the public `.d.ts` of `@oly/core` against a
versioned list of exports, so that adding something to the public API is an explicit change.

## 9. Benchmarks and documentation

- `bench/results/` moves to `docs/benchmarks/`, with subfolders `ecs/` (the current `m*`/`e1_*`), `wasm/` (cost
  study) and `bridge/` (today `engine/`, boundary benchmark). Each folder's `README.md` is updated with the
  new paths and commands. The old JSON files are not edited.
- **Mandatory re-run** after the change, recorded as `r1_*` in `docs/benchmarks/ecs/`, with the same
  protocol and the same `tools/bench_ratio.py`: NF-P1, NF-P2 and NF-P5 within targets. A confirmed regression (repeated
  because of WSL2 noise) blocks delivery. Also the boundary benchmark (`boundary`), recorded in
  `docs/benchmarks/bridge/` for comparison with phase 1 (no numeric target).
- `CLAUDE.md` rewritten for the new tree, commands, modules, boundaries and glossary.
- `.claude/skills/add-test-case`, `.claude/skills/bench-compare`, `.claude/agents/perf-reviewer.md` and
  `.claude/agents/spec-conformance-reviewer.md` are updated to the new paths, targets and names.
- Old specs and plans do not change. The plan for this modularization records the "old path → new" table
  for whoever reads the old documents.

## 10. Done criteria

1. The tree of §3 exists and the moved files preserve history (`git mv`).
2. `$DC make test` (debug-asan), `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`,
   `$WC make games-test`, the Release of `wasm-games-release` and the cost study presets are green, with
   all `contract_death` cases.
3. `oly.lint.boundaries` and the TS surface test run in ctest and pass; a forbidden include introduced by
   hand makes them fail (checked once and reverted).
4. No old name survives in the code, the build or the living documents: `ECS_ASSERT`, `ecs::` without `oly::`,
   `engine*` in the C API, `Raw`, `create` as an entity verb, `EngineContractError`, `loadEngine`. Search the
   repository, excluding old specs/plans and historical `docs/benchmarks/`.
5. NF-P1, NF-P2 and NF-P5 re-run and within targets; boundary benchmark recorded.
6. `CLAUDE.md`, the project's skills and agents updated.

## 11. Left to the builder's discretion

- The order of the changes and how commits are cut. Suggestion: (1) move the tree and the build without renaming anything, green;
  (2) apply the Oly name (namespaces, includes, targets, options); (3) ECS surface (§6); (4) C API (§7);
  (5) TS split and surface (§8); (6) benchmarks and documentation (§9). Each step ends green.
- The branch name (suggestion: `refactor/oly-modules` from `develop`).
- How to implement `oly.lint.boundaries` (Python script with `-I`, CMake script or shell), as long as it runs in the ctest
  of the native presets and fails with a message that points to file and line.
- The form of the test accessors (§6.2) and the mechanism for hiding members in TS (§8.2).
- Moving more truly generic utilities to `core` (C++).
- Small naming adjustments not covered by the glossary, following the same pattern and recorded in the plan.
