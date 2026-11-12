# Oly — modularization and review of the public APIs — implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganize the repository into the **Oly** engine (C++ modules `core`/`ecs`/`bridge` and TS packages `@oly/core`/`@oly/client`/`@oly/codegen`/`@oly/testing`), with boundaries enforced by the build and lean public surfaces, without changing behaviour or performance.

**Architecture:** Each C++ module becomes `engine/runtime/<m>/{include/oly/<m>,src,tests,bench}` created by `oly_add_module`, which links only the `DEPENDS`; a script (`oly.lint.boundaries`) rejects undeclared includes, `detail/` used from outside and forbidden TS imports. The TS side becomes npm workspaces with project references and output in `dist/` per package. Renames follow the spec's glossary (§5.2) and leave no aliases.

**Tech Stack:** C++20 (gcc 14, clang/Emscripten 3.1.74), CMake ≥ 3.25 with presets, doctest 2.4.11, Google Benchmark 1.8.3, TypeScript 5.6.3, `@types/node` 20.17.6, `node:test`, Python 3 (scripts in `tools/`), Docker Compose (`dev`, `wasm`, `perf`).

**Spec:** `docs/superpowers/specs/2026-11-09-oly-modularization-design.md` (and, for ECS requirements, `2026-10-16-ecs-archetype-spec.md`).

## Global Constraints

- Branch `refactor/oly-modules` from `develop`. Each task ends green and with a commit.
- Existing files change place only with `git mv` (history preserved).
- No new functionality, no change to data layout, validity rules or the hot path.
- Anti-goal: no pimpl, `virtual` or indirection in `Query<Ts...>::each`/`eachBlock` and `World::get`; the template APIs do not delegate to the `ById` ones.
- No aliases, shims or compatibility `using` with old names (including `namespace ecs = oly::ecs;`).
- Targets that still apply: NF-P1 ≤ 1.10; NF-P2 ≤ 1.25; NF-P5 (`/1000`) ≤ 0.5.
- The `oly` prefix appears only in the C API (`oly*`, `OlyWorld`, `OLY_API`) and in what is internal to the bridge (`OlyModule`, `_oly*`). C++ and TS APIs do not repeat `oly` in names.
- Game names only in `engine/testing/fixture/`. Game concepts never in `engine/`.
- Exact TS versions: `typescript` 5.6.3, `@types/node` 20.17.6; no bundler; tests with `node:test`.
- Commits, comments and assert messages in English; `CLAUDE.md` style (braces always, full names, data at the top of the class, one `namespace` block per file).
- Old specs and plans and the historical benchmark JSON/TXT files are not edited.
- Commands: `DC="docker compose run --rm dev"`, `WC="docker compose run --rm wasm"`.

## Review Focus

1. **Nested `eachBlock` through the C API and TS** (same query and different queries): the outer loop's descriptors stay valid after the inner one starts and ends. Test in Task 6 (`bridge.c_api`) and in Task 8 (TS).
2. **Exception inside the iteration callback** (C++ `QueryById::eachBlock`, TS `Query.eachBlock`): the iteration is ended and the `World` accepts structural changes again. Tests in Tasks 5 and 8.
3. **Moved `QueryIteration`**: the moved-from object does not end the iteration again (no double decrement) and the destination ends it once. Test in Task 5.
4. **Steady-state allocation in iteration by ID**: the second `iterate()` at the same depth does not allocate. Test in Task 5 (with `AllocCounter`).
5. **WASM memory growing during `eachBlock`** (new query created inside the callback, which allocates in WASM): the outer block keeps reading the right values. Test in Task 8.

## "Old path → new" table (§9 of the spec)

| Before | After |
|---|---|
| `include/ecs/detail/{assert,align,small_map,type_name}.hpp`, `src/assert.cpp` | `engine/runtime/core/include/oly/core/…`, `engine/runtime/core/src/assert.cpp` |
| `include/ecs/*.hpp`, `src/*.cpp` | `engine/runtime/ecs/include/oly/ecs/…` (internals in `detail/`, Task 4), `engine/runtime/ecs/src/` |
| `include/ecs/type_info.hpp` / `raw_query.hpp` | `oly/ecs/component.hpp` / `oly/ecs/query_by_id.hpp` |
| `tests/**` (except `support/doctest_main.cpp`) | `engine/runtime/ecs/tests/**` (`raw_query.cpp` → `query_by_id.cpp`) |
| `tests/support/doctest_main.cpp` | `engine/runtime/core/tests/support/doctest_main.cpp` |
| `bench/*.cpp`, `bench/common.hpp` | `engine/runtime/ecs/bench/` |
| `engine/include/engine/*.hpp`, `engine/src/c_api.cpp`, `engine/tests/*` | `engine/runtime/bridge/{include/oly/bridge,src,tests}/` (`world_handle.hpp` goes away, `world.hpp` comes in) |
| `engine/ts/*` | `engine/packages/core/src/*` |
| `engine/generator/*` | `engine/tools/codegen/src/*` |
| `engine/test/unit/{entity,layout,schema}.test.ts` | `engine/packages/core/tests/unit/` |
| `engine/test/unit/generator.test.ts` | `engine/tools/codegen/tests/generator.test.ts` |
| `engine/fixture/*`, `engine/test/wasm/*`, `engine/bench/boundary.ts` | `engine/testing/{fixture,tests/wasm,bench}/` |
| `engine/package.json`, `engine/package-lock.json`, `engine/tsconfig.json`, `engine/module-package.json` | `package.json`, `package-lock.json`, `tsconfig.base.json`, `tsconfig.json` at the root + one `package.json`/`tsconfig.json` per package; `module-package.json` goes away |
| `tools/wasm/node_env.js` | `cmake/wasm/node_env.js` |
| `bench/results/**` | `docs/benchmarks/**` (`engine/` → `bridge/`, `m*`/`e1_*` → `ecs/`) |
| `build/ts/**` | `<package>/dist/**` (and `dist-tests/**` for tests) |
| ctest `unit_world`, `contract_death.<c>`, `engine_c_api`, `engine_ts_unit`, `engine_ts_wasm` | `ecs.unit_world`, `ecs.contract_death.<c>`, `bridge.c_api`, `ts.core.unit` + `ts.codegen.unit`, `ts.testing.wasm` |
| executables `bench_iterate` etc. | `ecs_bench_iterate`, `ecs_bench_transitions`, `ecs_bench_fragmented`, `ecs_bench_flush` |

Name adjustments not covered by the glossary (recorded here, §11 of the spec): `World::archetypeCount` → free function `archetypeCount(const World&)` in `diagnostics.hpp`; `ECS_FUZZ_SEED`/`ECS_FUZZ_OPS` → `OLY_FUZZ_SEED`/`OLY_FUZZ_OPS`; `ECS_LADDER_EXTRA_ARGS` → `OLY_LADDER_EXTRA_ARGS`; `ENGINE_FIXTURE_MODULE` → `OLY_FIXTURE_MODULE`; death case `raw_query_end_without_begin` ceases to exist (the manual pair leaves the API) and `query_by_id_iteration_out_of_order` comes in; the labels `ts-engine`/`ts-engine-raw` of the boundary benchmark stay, to compare with phase 1. TS `RawField`/`ReadField.raw()` stay (§8.2 lists them as public): the glossary's "Raw ceases to exist" applies to the by-ID variants.

---

### Task 1: C++ tree and per-module build

**Files:**
- Move (`git mv`): see the table above, C++ rows and `tools/wasm/node_env.js`. `tests/CMakeLists.txt` → `engine/runtime/ecs/tests/CMakeLists.txt`; `bench/CMakeLists.txt` → `engine/runtime/ecs/bench/CMakeLists.txt`; `engine/tests/*` → `engine/runtime/bridge/tests/`.
- Create: `cmake/OlyOptions.cmake`, `cmake/OlyWarnings.cmake`, `cmake/OlyWasm.cmake`, `cmake/OlyModule.cmake`, `cmake/OlyGame.cmake`, `cmake/Dependencies.cmake`, `engine/runtime/CMakeLists.txt`, `engine/runtime/{core,ecs,bridge}/CMakeLists.txt`, `engine/runtime/core/tests/{CMakeLists.txt,unit_small_map.cpp}`.
- Modify: `CMakeLists.txt`, `engine/CMakeLists.txt`, `CMakePresets.json`, `Makefile`, every `#include` of the moved files, `engine/generator/emit_cpp.ts` (only the two emitted includes).

**Interfaces:**
- Produces (CMake, used by all following tasks):
  - `oly_add_module(NAME <m> [DEPENDS <m...>] [OBJECT | HEADER_ONLY])` → target `oly_<m>`, alias `oly::<m>`; compiles `src/*.cpp` (glob `CONFIGURE_DEPENDS`), `include/` as `PUBLIC`, `DEPENDS` linked `PUBLIC` as `oly::<dep>`; appends `<m>|<directory relative to the root>|<deps separated by ;>` to the global property `OLY_MODULES`.
  - `oly_add_test(MODULE <m> NAME <suite> SOURCES <files...> [SUPPORT <dirs...>])` → executable `<m>_<suite>` in `${CMAKE_BINARY_DIR}/tests/`, linked to `oly::<m>` and doctest, with `engine/runtime/core/tests/support/doctest_main.cpp`, `<module>/tests/support` and the `SUPPORT` dirs as include; ctest `<m>.<suite>`; applies `oly_warnings`, `oly_wasm_exceptions`, `oly_wasm_node_env`.
  - `oly_add_contract_death(MODULE <m> SOURCE <file> CASES <cases...>)` → only when asserts are on; executable `<m>_contract_death`; ctest `<m>.contract_death.<case>`.
  - `oly_add_bench(MODULE <m> NAME <n> SOURCES <files...>)` → executable `<m>_bench_<n>` in `${CMAKE_BINARY_DIR}/bench/` (same flags as today: `-Wno-deprecated-declarations` in Emscripten, vectorization report with `OLY_VEC_REPORT`).
  - `oly_add_game(<target> SCHEMA_JS <js> SOURCES <cpp...> DEPENDS <m...>)` → like the current `engine_add_game`; links only `oly::<m>` of the `DEPENDS`; registers `<target>|<sources directory>|<deps>` in the global property `OLY_GAMES`; `.mjs` output in `${CMAKE_BINARY_DIR}/games/`.
  - `oly_warnings(<target>)`, `oly_wasm_exceptions(<target>)`, `oly_wasm_node_env(<target>)`.
  - Options: `OLY_BUILD_TESTS`, `OLY_BUILD_BENCH`, `OLY_SANITIZE`, `OLY_WERROR`, `OLY_VEC_REPORT`, `OLY_ENABLE_ASSERTS`, `OLY_BUILD_GAMES`. Until Task 3, `oly::core` publishes the definition `ECS_ENABLE_ASSERTS=<resolved value of OLY_ENABLE_ASSERTS>` (the macro only changes name in Task 3).
  - Presets: `wasm-engine`/`wasm-engine-release` → `wasm-games`/`wasm-games-release`; the others keep their names. Make: `ts`, `games-test` (replacing `engine-ts`, `engine-test`).

Decisions: `project(Oly VERSION 1.0 LANGUAGES CXX)`. `engine/CMakeLists.txt` remains the engine aggregator: `add_subdirectory(runtime)` and, with `OLY_BUILD_GAMES`, the fixture game and the TS tests (in this task still with today's `build/ts/...` paths; Task 7 moves them to `engine/testing/CMakeLists.txt`). `engine/runtime/CMakeLists.txt` adds `core`, `ecs`, `bridge` in that order. Dependencies: `core` without deps; `ecs DEPENDS core`; `bridge DEPENDS ecs core OBJECT`; `fixture_game DEPENDS ecs bridge`. The `bridge` tests use `SUPPORT engine/runtime/ecs/tests/support` (components `A`, `B`, `Tag`). `core` gains a minimal suite to have its own test cycle.

- [ ] **Step 1: Create the branch**

Run: `git switch -c refactor/oly-modules develop`

- [ ] **Step 2: Write the `core` suite** (`engine/runtime/core/tests/unit_small_map.cpp`)

```cpp
TEST_CASE("SmallMap inserts, finds and keeps the key order")   // find of a missing key == nullptr; find(k) returns the inserted value
TEST_CASE("alignUp rounds to the next multiple")               // alignUp(0,64)==0, alignUp(1,64)==64, alignUp(64,64)==64, alignUp(65,8)==72
```
Use the current API of `SmallMap` (the one `world.hpp` uses: `find`, iteration `for (auto& [k, v] : map)`) and of `alignUp`.

- [ ] **Step 3: Move the files with `git mv`** following the table and replace the includes: `"ecs/detail/X.hpp"` (X ∈ assert, align, small_map, type_name) → `"oly/core/X.hpp"`; `"ecs/X.hpp"` → `"oly/ecs/X.hpp"`; `"engine/X.hpp"` → `"oly/bridge/X.hpp"`. In `emit_cpp.ts`, the emitted includes become `"oly/ecs/entity.hpp"` and `"oly/bridge/schema_registry.hpp"`.

- [ ] **Step 4: Write `cmake/*.cmake`, the modules' `CMakeLists.txt` and the root** with the interfaces above. The root only does: options/presets (`include(OlyOptions)` …), `include(Dependencies)` (doctest with `OLY_BUILD_TESTS`, benchmark with `OLY_BUILD_BENCH`), `enable_testing()`, `add_subdirectory(engine)`. Test registrations: `ecs` — `unit_table`, `unit_type_info`, `unit_entity`, `unit_archetype`, `unit_world` (+ `support/alloc_counter.cpp`), `invariants`, `fuzz_vs_model`, `nontrivial`, `queries`, `command_buffer`, `raw_query`; `contract_death` with the current 10 cases; `bridge` — `c_api` (`c_api.cpp`, `test_schema_registry.cpp`); `core` — `unit_small_map`. `ecs` benchmarks: `iterate`, `transitions`, `fragmented` (source `fragmentation.cpp`), `flush`. TS tests: `ts.core.unit` (today `build/ts/test/unit/`) and `ts.testing.wasm` (today `build/ts/test/wasm/`, with `ENGINE_FIXTURE_MODULE=$<TARGET_FILE:fixture_game>`).

- [ ] **Step 5: Update `CMakePresets.json` and `Makefile`**: `ECS_*` → `OLY_*`, `ENGINE_BUILD_FIXTURE` → `OLY_BUILD_GAMES`, presets `wasm-games`/`wasm-games-release` (configure, build and test). Make: `ts` (for now `cd engine && npm ci if needed && npm run build`) and `games-test: ts` + `$(MAKE) test PRESET=wasm-games`.

- [ ] **Step 6: Check the names in ctest**

Run: `$DC sh -c 'cmake --preset debug-asan >/dev/null && ctest --preset debug-asan -N'`
Expected: lists `core.unit_small_map`, `ecs.unit_world`, `ecs.contract_death.add_during_each`, …, `bridge.c_api`; no name without a module prefix.

- [ ] **Step 7: Run everything**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`
Expected: all green, same number of `contract_death` cases as before (10).

- [ ] **Step 8: Check the cost-study presets and relwithdebinfo**

Run: `$DC make build PRESET=relwithdebinfo`; `$WC make test PRESET=native-clang`; `$WC make test PRESET=wasm-simd`
Expected: compiles; tests green; `build/release/bench/ecs_bench_iterate` exists after the release build.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "refactor: per-module tree (core, ecs, bridge) and build with oly_add_module"
```

---

### Task 2: `oly.lint.boundaries` (C++ rules)

**Files:**
- Create: `tools/check_boundaries.py`, `tools/test_check_boundaries.py`
- Modify: `CMakeLists.txt` (manifest + ctest registration)

**Interfaces:**
- Consumes: global properties `OLY_MODULES` and `OLY_GAMES` (Task 1).
- Produces: `${CMAKE_BINARY_DIR}/oly_boundaries.json` in the format `{"modules": [{"name": "ecs", "directory": "engine/runtime/ecs", "depends": ["core"]}, …], "games": [{"name": "fixture_game", "directory": "engine/fixture", "depends": ["ecs", "bridge"]}]}` (directories relative to the root), written at the end of configuration. CLI: `python3 -I tools/check_boundaries.py --root <repo> --manifest <json>`; exits 0 with no violations; exits 1 printing one line `<relative file>:<line>: <reason>` per violation. Task 8 adds TS rules to this script.

C++ rules (`.hpp`/`.cpp` files outside `build/` and `node_modules/`; include = `#include "oly/<x>/…"`):
- **R1** a file inside the directory of a module or game `<m>` includes `oly/<x>/` with `x ≠ m` and `x ∉ depends(m)` → reason `"<m> includes oly/<x> without declaring <x> in DEPENDS"`.
- **R2** a file outside `engine/runtime/<x>/` includes `oly/<x>/detail/` → reason `"oly/<x>/detail is internal to module <x>"`.

- [ ] **Step 1: Write the script's tests** (`tools/test_check_boundaries.py`, `unittest`, same import scheme as `tools/test_bench_ladder.py`; each test builds a tree in a temporary directory and a manifest)

```python
def test_declared_include_passes(self)            # ecs includes oly/core, ecs depends [core] → code 0, empty output
def test_undeclared_include_fails(self)        # bridge includes oly/core with depends [ecs] → 1; output contains "engine/runtime/bridge/src/x.cpp:3:" and "without declaring core"
def test_detail_from_outside_fails(self)               # bridge includes oly/ecs/detail/table.hpp → 1; "oly/ecs/detail is internal to module ecs"
def test_detail_in_module_tests_passes(self)  # engine/runtime/ecs/tests/t.cpp includes oly/ecs/detail/table.hpp → 0
def test_game_uses_game_depends(self)        # game with depends [ecs, bridge] including oly/core → 1
```

- [ ] **Step 2: Run and see it fail**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: FAIL/ERROR (`check_boundaries` does not exist).

- [ ] **Step 3: Implement `tools/check_boundaries.py`** with the CLI and the rules above (regular expression per line; a file's owning module is the one with the longest directory prefix).

- [ ] **Step 4: Run the script's tests**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: OK (5 tests).

- [ ] **Step 5: Manifest and ctest.** At the end of the root, build the JSON from `OLY_MODULES`/`OLY_GAMES` and `file(WRITE …)`. Outside Emscripten and with `find_package(Python3 COMPONENTS Interpreter)`, register `oly.lint.boundaries` (`${Python3_EXECUTABLE} -I tools/check_boundaries.py --root ${PROJECT_SOURCE_DIR} --manifest ${CMAKE_BINARY_DIR}/oly_boundaries.json`) and `oly.lint.boundaries_selftest` (the test file from Step 1).

- [ ] **Step 6: Check on the repository and a violation by hand**

Run: `$DC make test T='^oly\.'`
Expected: 2 tests, green.
Then add `#include "oly/bridge/c_api.hpp"` to `engine/runtime/ecs/src/world.cpp` and run the script directly (the build would break before ctest, because `ecs` does not see the `bridge` headers): `$DC sh -c 'cmake --preset debug-asan >/dev/null && python3 -I tools/check_boundaries.py --root . --manifest build/debug-asan/oly_boundaries.json'` → code 1 with `engine/runtime/ecs/src/world.cpp:<line>: ecs includes oly/bridge without declaring bridge in DEPENDS`. Undo.

- [ ] **Step 7: Commit**

```bash
git add tools/check_boundaries.py tools/test_check_boundaries.py CMakeLists.txt
git commit -m "build: oly.lint.boundaries checks DEPENDS and detail/ usage in ctest"
```

---

### Task 3: Oly name in C++ (namespaces, assert, options)

**Files:**
- Modify: every `.hpp`/`.cpp` in `engine/runtime/**`, `engine/fixture/systems.cpp`, `engine/generator/emit_cpp.ts`, `engine/test/unit/generator.test.ts`, `engine/runtime/ecs/tests/support/model.hpp`, `cmake/OlyOptions.cmake`/`OlyModule.cmake`

**Interfaces:**
- Produces:
  - `namespace oly::core`: `AssertHandler`, `setAssertHandler`, `detail::assertFail`, `SmallMap`, `alignUp`, `alignedAllocate`, `alignedFree`, `typeName<T>()` (leaves `detail`, it is a public core utility). Macros `OLY_ASSERT(cond, fmt, ...)` and `OLY_ENABLE_ASSERTS` (public definition of `oly::core`).
  - `namespace oly::ecs` for everything in the ECS (the current `ecs::detail` becomes `oly::ecs::detail`); inside it, core is referred to as `core::SmallMap` etc.
  - `namespace oly::bridge`: `schemaHash()`, `schemaComponentIds()` (type `std::span<const oly::ecs::ComponentId>`).
  - Generated code: `oly::ecs::Entity` in `entity` fields, registry in `namespace oly::bridge { … }` with `oly::ecs::componentId<T>()`; top comment `// Generated by oly-codegen: do not edit.`
  - Fuzz: `OLY_FUZZ_SEED`, `OLY_FUZZ_OPS`.

- [ ] **Step 1: Update the generator test** (`generator.test.ts`): the expressions now require `oly::ecs::Entity owner;`, `oly::ecs::componentId<P>\(\),\s*oly::ecs::componentId<T>\(\)` and `namespace oly::bridge`.

- [ ] **Step 2: See it fail**

Run: `$WC sh -c 'make ts && node --test build/ts/test/unit/'`
Expected: FAIL in the struct and registry tests.

- [ ] **Step 3: Rename namespaces and macros** in C++, in `emit_cpp.ts` and in CMake (`OLY_ENABLE_ASSERTS=<0|1>` as a public definition of `oly::core`). Tests and benchmarks use the fully qualified name (`oly::ecs::World`), without a namespace alias.

- [ ] **Step 4: Run everything**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`
Expected: green.

- [ ] **Step 5: Sweep**

Run: `git grep -nE '\bECS_[A-Z_]+|(^|[^:])\becs::|namespace (ecs|engine)\b|\bengine::' -- engine cmake CMakeLists.txt CMakePresets.json Makefile tools`
Expected: only occurrences in the C API `engine*`/`ENGINE_API`/`Engine*` (Task 6) and in `tools/run_ladder.sh` (`ECS_LADDER_EXTRA_ARGS`, Task 10); none in C++ outside the C API.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: namespaces oly::core, oly::ecs, oly::bridge and OLY_ASSERT"
```

---

### Task 4: ECS public surface (`detail/`, `component.hpp`, `diagnostics.hpp`, test accessors)

**Files:**
- Move (`git mv`): `oly/ecs/{table,archetype}.hpp` → `oly/ecs/detail/`; `oly/ecs/type_info.hpp` → `oly/ecs/component.hpp`; `src/type_info.cpp` → `src/component.cpp`.
- Create: `oly/ecs/diagnostics.hpp`, `src/diagnostics.cpp`, `oly/ecs/detail/{entity_store,query_cache,command_plan,test_access}.hpp`, `engine/runtime/ecs/tests/surface.cpp`
- Modify: `world.hpp`, `entity.hpp`, `query.hpp`, `command_buffer.hpp`, `raw_query.hpp`, `world.cpp`, `command_buffer.cpp`, tests that use the hooks (`command_buffer.cpp`, `unit_entity.cpp`, `unit_world.cpp`), `bench/{iterate,transitions}.cpp`, `bridge/src/c_api.cpp`

**Interfaces:**
- Produces (public headers; everything else in `oly::ecs::detail`):
  - `entity.hpp`: `Entity`, `NullEntity`, `EntityHash` (without `EntityRecord`/`EntityStore`, which go to `detail/entity_store.hpp`).
  - `component.hpp`: `ComponentId`, `componentId<T>()`, `TypeInfo`, `typeInfo(ComponentId)`, `registeredTypeCount()`.
  - `query.hpp`: `With`, `Without`, `Query<Ts...>` with only `each`, `eachBlock`, `matchedCount`, `writes` public. `QueryCacheBase`, `MatchedTable`, `IterationGuard`, `signatureMatches` in `detail/query_cache.hpp`; `initialize`/`matches`/`isMatched`/`onArchetypeCreated` private with `friend class World` (the virtual overrides remain callable through the base pointer).
  - `command_buffer.hpp`: `CommandBuffer` with public `spawn`, `destroy`, `add<T>`, `remove<T>`, `addRaw`, `removeRaw` (renamed in Task 5), `commandCount`, `empty`, `clear`, move/destructor. Constructor from `EntityStore&`, `plan`, `netEffect`, `forEach`, `store` private (`friend class World`, `friend struct detail::CommandBufferTestAccess`). `CommandOperation`, `CommandView`, `NetComponentOperation`, `EntityPlan`, `NetEffect`, `CommandPlan` in `detail/command_plan.hpp`.
  - `diagnostics.hpp`: `struct ArchetypeStats` (field `signature` as `std::vector<ComponentId>`), `struct WorldStats`, `bool checkInvariants(const World&, std::string* why = nullptr)`, `WorldStats stats(const World&)`, `uint64_t signatureLookupCount(const World&)`, `uint32_t archetypeCount(const World&)`. `World` declares these functions `friend`; `World::checkInvariants`, `stats`, `signatureLookupCount`, `archetypeCount` cease to exist.
  - `detail/test_access.hpp`: `struct WorldTestAccess { static void flushSequential(World&, CommandBuffer&); };`, `struct CommandBufferTestAccess { template <class F> static void forEach(const CommandBuffer&, F&& callback); };`, `struct EntityStoreTestAccess { static void setGeneration(EntityStore&, uint32_t index, uint32_t generation) noexcept; };`. `World::flushSequential` and `EntityStore::setGenerationForTest` stop being public members.

- [ ] **Step 1: Write the surface test** (`engine/runtime/ecs/tests/surface.cpp`, registered as `ecs.surface`). Probe concepts via template (private access is a substitution failure only inside a template):

```cpp
template <class W> concept HasMemberStats = requires(W& world) { world.stats(); };
template <class W> concept HasMemberCheckInvariants = requires(W& world) { world.checkInvariants(nullptr); };
template <class W, class C> concept HasFlushSequential = requires(W& world, C& commands) { world.flushSequential(commands); };
template <class C> concept HasPlan = requires(C& commands) { commands.plan(); };
template <class C> concept HasForEach = requires(C& commands) { commands.forEach([](auto&) {}); };
template <class C> concept HasStore = requires(C& commands) { commands.store(); };
template <class Q> concept HasOnArchetypeCreated = requires(Q& query, oly::ecs::detail::Archetype& archetype) { query.onArchetypeCreated(archetype); };

TEST_CASE("World and CommandBuffer expose neither diagnostics nor test hooks") {
  static_assert(!HasMemberStats<oly::ecs::World>);
  static_assert(!HasMemberCheckInvariants<oly::ecs::World>);
  static_assert(!HasFlushSequential<oly::ecs::World, oly::ecs::CommandBuffer>);
  static_assert(!HasPlan<oly::ecs::CommandBuffer> && !HasForEach<oly::ecs::CommandBuffer> && !HasStore<oly::ecs::CommandBuffer>);
  static_assert(!HasOnArchetypeCreated<oly::ecs::Query<int>>);
  static_assert(!std::is_constructible_v<oly::ecs::CommandBuffer, oly::ecs::detail::EntityStore&>);
}
TEST_CASE("diagnostics through free functions") {   // world.spawn… is still create in this task
  // creates an entity with A; CHECK(oly::ecs::checkInvariants(world)); CHECK(oly::ecs::archetypeCount(world) == 2);
  // CHECK(oly::ecs::stats(world).liveEntities == 1); CHECK(oly::ecs::signatureLookupCount(world) > 0)
}
```
(Use a component from `support/components.hpp` instead of `int` if `Query<int>` cannot be instantiated.)

- [ ] **Step 2: See it fail**

Run: `$DC make test T='ecs\.surface'`
Expected: compile failure (`oly/ecs/diagnostics.hpp` does not exist) or a failing `static_assert`.

- [ ] **Step 3: Implement the split** according to the interfaces. `diagnostics.cpp` receives `stats` and the count; the body of `checkInvariants` and its helpers (`checkArchetypes` … `checkEdges`) may stay in `world.cpp` as private members called by the friend free function. Tests, benchmarks and the C API now use the free functions and the accessors from `detail/test_access.hpp`.

- [ ] **Step 4: Run everything and check the boundaries**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`
Expected: green, including `oly.lint.boundaries`. `c_api.cpp` must not include `oly/ecs/detail/` directly; until Task 5 it reaches `Table` only through `raw_query.hpp` (Task 5 removes that use with `QueryIteration`).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: ECS surface with detail/, component.hpp and diagnostics.hpp"
```

---

### Task 5: ECS vocabulary and by-ID API (`spawn`, `ById`, `QueryById`, `QueryIteration`)

**Files:**
- Move (`git mv`): `oly/ecs/raw_query.hpp` → `oly/ecs/query_by_id.hpp`; `tests/raw_query.cpp` → `tests/query_by_id.cpp`
- Modify: `world.hpp`, `world.cpp`, `command_buffer.hpp/.cpp`, `tests/**` (`create` → `spawn`), `tests/contract_death.cpp`, `tests/support/model.hpp`, `bench/*.cpp`, `bridge/src/c_api.cpp`, `engine/fixture/systems.cpp`, `engine/runtime/ecs/tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `World::spawn()`, `template <class... Ts> Entity World::spawn(Ts&&...)`; `World::create` ceases to exist. `CommandBuffer::spawn()` stays.
  - `void World::addById(Entity, ComponentId, void* value)`, `void removeById(Entity, ComponentId)`, `bool hasById(Entity, ComponentId) const noexcept`, `void* tryGetById(Entity, ComponentId) noexcept` (null if dead, missing or tag), `void* getById(Entity, ComponentId)` (assert `"getById e=%u:%u type=%.*s"` if dead or missing; a tag is also an error). `CommandBuffer::addById`/`removeById`. The template versions keep their own path.
  - `query_by_id.hpp`:
    ```cpp
    struct QueryByIdSpec { std::vector<ComponentId> read{}, write{}, with{}, without{}; };
    class BlockById {   // lightweight value, valid while the iteration exists
      uint32_t count() const noexcept;
      std::span<const Entity> entities() const noexcept;
      void* column(size_t index) const noexcept;   // index in read order followed by write
    };
    class QueryIteration {   // move-only; ++iterating on creation, -- in the destructor (once)
      uint32_t blockCount() const noexcept;
      BlockById block(uint32_t index) const noexcept;
      std::span<const uintptr_t> descriptors() const noexcept;   // blockCount × (2 + components().size()): [count, entities, columns...]
    };
    class QueryById {
      template <class F> void eachBlock(F&& callback);   // callback(const BlockById&), only non-empty tables
      QueryIteration iterate();
      std::span<const ComponentId> components() const noexcept;
      size_t matchedCount() const noexcept;
    };
    QueryById& World::queryById(const QueryByIdSpec&);   // cached by spec
    ```
    `beginIteration`/`endIteration`/`iterationDepth`/`matchedTables` stop being public; `RawMatchedTable` becomes `detail::MatchedTableById`. The descriptors of `iterate()` come from a pool in the `QueryById` (one `std::vector<uintptr_t>` per nesting level; grows to the largest depth seen and does not shrink). Ending a `QueryIteration` that is not the innermost of its `QueryById` is a contract error: `OLY_ASSERT(…, "iterate: iteration ended out of order (level=%u, depth=%u)", …)`. `eachBlock` uses `iterate()` internally.
  - Assert messages with the new verbs: `"spawn during iteration=%u"`, `"queryById: tag in read/write type=%.*s"`, `"queryById: repeated component …"`, `"queryById: unknown component id=%u"` (keeping `e=`/type where they already were).
  - `contract_death` (`ecs`): `add_during_each`, `get_dead_entity`, `flush_during_each`, `command_after_destroy`, `destroy_during_each_block`, `query_by_id_tag_in_read` {`queryById`, `tag`}, `query_by_id_repeated_component` {`queryById`, `repeated`}, `query_by_id_unknown_component` {`queryById`, `unknown`}, `query_by_id_iteration_out_of_order` {`iterate`, `out of order`}, `add_during_query_by_id_iteration` {`add`}, `get_by_id_missing` {`getById`, `e=`}, `spawn_during_each` {`spawn`, `iteration`}.

- [ ] **Step 1: Rewrite `tests/query_by_id.cpp`** (current cases adapted to `BlockById` + the new ones)

```cpp
TEST_CASE("eachBlock delivers count, entities and columns in read, write order")   // values written via column(1) show up in get<B>
TEST_CASE("iterate marks the World as iterating until the destructor")                // isIterating() true inside the scope, false after
TEST_CASE("nested iterate keeps the blocks of each level")                    // outer.descriptors() equal before and after an inner one on the same query
TEST_CASE("moved QueryIteration ends the iteration only once")                 // auto b = std::move(a); after destroying both, isIterating()==false and checkInvariants ok
TEST_CASE("exception in the eachBlock callback ends the iteration")                 // CHECK_THROWS(query.eachBlock(throws)); then isIterating()==false and spawn works
TEST_CASE("steady-state iterate does not allocate")                                        // first iterate() warms up; AllocCounter: second iterate() at the same depth == 0 allocations
TEST_CASE("getById and tryGetById")                                               // tryGetById of missing/tag/dead == nullptr; getById returns the same address as get<A>
```
Register with `SOURCES query_by_id.cpp support/alloc_counter.cpp` (the allocation test only runs where `AllocCounter` works today — use the same guard as `unit_world.cpp`).

- [ ] **Step 2: Write/rename the `contract_death` cases** from the list above (function + name in `CASES`).

- [ ] **Step 3: See it fail**

Run: `$DC make test T='ecs\.(query_by_id|contract_death)'`
Expected: compile failure (`QueryById`, `iterate`, `getById` do not exist).

- [ ] **Step 4: Implement** the interfaces; replace `create` → `spawn` across the whole C++ repository (tests, benchmarks, `model.hpp`); `c_api.cpp` now uses `queryById`/`iterate`/`*ById` while keeping the C names `engine*` (renamed in Task 6), storing the query's active `QueryIteration` in a stack inside `EngineWorld` (Task 6 formalizes this as `OlyIteration`).

- [ ] **Step 5: Run everything**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`
Expected: green; `ctest -N` shows 12 `ecs.contract_death.*` cases.

- [ ] **Step 6: Performance review of the diff** — dispatch the `perf-reviewer` agent over `git diff HEAD` (focus: `World::spawn<Ts...>`, `get<T>`, `Query::each` with no change in generated code; pool without allocation). Fix what it confirms.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: spawn, ById variants and QueryById with RAII QueryIteration"
```

---

### Task 6: C API `oly*` and opaque `OlyWorld`

**Files:**
- Create: `engine/runtime/bridge/include/oly/bridge/world.hpp`, `engine/runtime/bridge/tests/contract_death.cpp`
- Delete (`git rm`): `engine/runtime/bridge/include/oly/bridge/world_handle.hpp`
- Modify: `oly/bridge/c_api.hpp`, `src/c_api.cpp`, `tests/c_api.cpp`, `tests/test_schema_registry.cpp`, `engine/fixture/systems.cpp`, `engine/ts/module.ts`, `engine/ts/bridge.ts`, `engine/ts/query.ts`, `engine/ts/runtime.ts`, `engine/test/wasm/fixture.ts`, `engine/bench/boundary.ts`, `engine/runtime/bridge/CMakeLists.txt`

**Interfaces:**
- Produces (C, `extern "C"`, `OLY_API` = `EMSCRIPTEN_KEEPALIVE` in Emscripten):
  ```
  struct OlyWorld; struct OlyQuery; struct OlyIteration;     // opaque
  struct OlyEntity { uint32_t index; uint32_t generation; };
  OlyWorld* olyWorldCreate();  void olyWorldDestroy(OlyWorld*);
  int32_t olyWorldCheckInvariants(OlyWorld*, char* message, uint32_t capacity);
  uint32_t olySchemaHash();  uint32_t olySchemaComponentCount();  uint32_t olySchemaComponentId(uint32_t schemaIndex);
  uint32_t olyComponentSize(uint32_t componentId);  uint32_t olyComponentAlignment(uint32_t componentId);
  void olyEntitySpawn(OlyWorld*, OlyEntity* out);
  void olyEntityDestroy(OlyWorld*, uint32_t index, uint32_t generation);
  int32_t olyEntityIsAlive(OlyWorld*, uint32_t index, uint32_t generation);
  void olyComponentAdd(OlyWorld*, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
  void olyComponentRemove(OlyWorld*, uint32_t index, uint32_t generation, uint32_t componentId);
  int32_t olyComponentHas(OlyWorld*, uint32_t index, uint32_t generation, uint32_t componentId);
  void* olyComponentGet(OlyWorld*, uint32_t index, uint32_t generation, uint32_t componentId);   // tryGetById
  OlyQuery* olyQueryCreate(OlyWorld*, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount);
  OlyIteration* olyQueryBegin(OlyWorld*, OlyQuery*, uint32_t* outBlockCount, const uintptr_t** outDescriptors);
  void olyQueryEnd(OlyWorld*, OlyIteration*);
  ```
  `OlyWorld` is defined only in `c_api.cpp`: the `World` and a stack of iteration slots (`std::vector<std::unique_ptr<…>>` with `std::optional<QueryIteration>`; grows to the largest depth and does not shrink, stable pointers). `olyQueryBegin` takes the next slot and returns the slot itself as `OlyIteration*`; `olyQueryEnd` requires it to be the top slot: `OLY_ASSERT(…, "olyQueryEnd: iteration ended out of order")`. The descriptors are valid until that iteration's `olyQueryEnd`.
- Produces (C++): `namespace oly::bridge { oly::ecs::World& worldOf(OlyWorld* world) noexcept; }` in `oly/bridge/world.hpp`.
- Produces (TS, still in `engine/ts/`): `OlyModule` (formerly `EngineModule`) with `_olyWorldCreate` … `_olyQueryBegin(world, query, outBlockCount, outDescriptors): number`, `_olyQueryEnd(world, iteration)`; `OlyModuleFactory`. `Bridge`: `spawn(world)` (formerly `create`), `queryCreate(…)` (formerly `query`), `queryBegin(world, query): { iteration: number; blockCount: number; descriptors: number }`, `queryEnd(world, iteration)`. Whether or not to copy the descriptors in `Query.eachBlock` is up to the implementer (measure in Task 10 if it changes).

- [ ] **Step 1: Update `tests/c_api.cpp`** to the new names and add:

```cpp
TEST_CASE("nested olyQueryBegin preserves the outer descriptors")   // outer = olyQueryBegin(q); copy outer descriptors; inner = olyQueryBegin(q) and olyQueryEnd(inner); memcmp(outer, copy) == 0; olyQueryEnd(outer)
TEST_CASE("worldOf gives access to the handle's World")                        // olyEntitySpawn + oly::bridge::worldOf(world).isAlive(entity)
TEST_CASE("repeated iteration reuses the slot")                        // two begin/end pairs in a row return the same OlyIteration*
```
And the bridge's `tests/contract_death.cpp` with the case `query_end_out_of_order` {`olyQueryEnd`, `out of order`} (begin A, begin B, end A), registered with `oly_add_contract_death(MODULE bridge …)`.

- [ ] **Step 2: See it fail**

Run: `$DC make test T='^bridge\.'`
Expected: compile failure (`olyWorldCreate` does not exist).

- [ ] **Step 3: Implement** the C API, `worldOf`, the fixture's `systems.cpp` (`OLY_API void fixtureMove(OlyWorld* world, float deltaTime)` with `oly::bridge::worldOf(world)`), and the TS side (`module.ts`, `bridge.ts`, `query.ts`, `runtime.ts`, `fixture.ts`, `boundary.ts`).

- [ ] **Step 4: Run everything**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`
Expected: green.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: C API oly* with opaque OlyWorld and iteration with its own buffer"
```

---

### Task 7: npm workspaces and split into packages (`@oly/core`, `@oly/codegen`, `@oly/testing`)

**Files:**
- Move (`git mv`): the table's TS rows.
- Create: `package.json`, `tsconfig.base.json`, `tsconfig.json` (solution, only `references`) at the root; `engine/packages/core/{package.json,tsconfig.json,src/internal.ts,src/platform.d.ts,tests/tsconfig.json}`; `engine/tools/codegen/{package.json,tsconfig.json,tests/tsconfig.json}`; `engine/testing/{package.json,tsconfig.json,CMakeLists.txt}`
- Delete (`git rm`): `engine/package.json`, `engine/package-lock.json`, `engine/tsconfig.json`, `engine/module-package.json`
- Modify: `Makefile`, `engine/CMakeLists.txt`, `cmake/OlyGame.cmake`, `.gitignore`, imports of every moved `.ts`

**Interfaces:**
- Produces:
  - Root: `package.json` (`"private": true`, `"workspaces": ["engine/packages/*", "engine/tools/*", "engine/testing", "games/*"]`, `devDependencies` `typescript` 5.6.3 and `@types/node` 20.17.6, script `"build": "tsc -b"`); a single `package-lock.json`; `tsconfig.base.json` (`strict`, `target` ES2022, `module`/`moduleResolution` NodeNext, `composite`, `declaration`, `skipLibCheck`); solution `tsconfig.json` (`"files": []`) with `references` to `engine/packages/core`, `engine/packages/core/tests`, `engine/tools/codegen`, `engine/tools/codegen/tests`, `engine/testing` (Task 9 adds the client), for the root `tsc -b`.
  - `@oly/core` (`engine/packages/core`): `"type": "module"`, `"exports": { ".": { "types": "./dist/index.d.ts", "default": "./dist/index.js" }, "./internal": { "types": "./dist/internal.d.ts", "default": "./dist/internal.js" } }`. `tsconfig.json`: `rootDir` `src`, `outDir` `dist`, `lib: ["ES2022"]`, `types: []` (no DOM and no node: the compiler rejects environment APIs). `src/platform.d.ts` declares only the minimum of `TextDecoder` used in `bridge.ts` (it exists in node, browser and worker). `src/internal.ts` re-exports: `Bridge`, `Heap`, `readScalar`, `writeScalar`, `makeView`, `computeLayout`, `scalarSize`, `scalarAlignment`, `fnv1a32`, `canonicalSchema`, `schemaHash`, `packEntity`, `MaxEntityGeneration`, type `OlyModule`. In this task `src/index.ts` stays `export *` (Task 8 restricts it). Tests: `tests/tsconfig.json` (`references` to core, `types: ["node"]`, `outDir` `../dist-tests`), importing `@oly/core` and `@oly/core/internal` by name.
  - `@oly/codegen` (`engine/tools/codegen`): `"bin": { "oly-codegen": "./dist/main.js" }`, `references` to core, `types: ["node"]`; `emit_cpp.ts` imports types from `@oly/core`. Test in `tests/generator.test.ts` → `dist-tests/`.
  - `@oly/testing` (`engine/testing`, `"private": true`): `rootDir` `.`, `outDir` `dist`, `include` `["fixture", "tests", "bench"]`, `references` to core, `types: ["node"]`. `fixture.ts` reads `OLY_FIXTURE_MODULE`.
  - CMake: `oly_add_game` calls `${NODE} ${PROJECT_SOURCE_DIR}/engine/tools/codegen/dist/main.js` (error `"run 'make ts' before configuring with OLY_BUILD_GAMES=ON"` if it does not exist); `engine/testing/CMakeLists.txt` declares `oly_add_game(fixture_game SCHEMA_JS ${…}/engine/testing/dist/fixture/schema.js SOURCES fixture/systems.cpp DEPENDS ecs bridge)` and `ts.testing.wasm` (`node --test engine/testing/dist/tests/wasm/`, with `OLY_FIXTURE_MODULE=$<TARGET_FILE:fixture_game>`); `engine/CMakeLists.txt` registers `ts.core.unit` (`engine/packages/core/dist-tests/unit/`) and `ts.codegen.unit` (`engine/tools/codegen/dist-tests/`) with `OLY_BUILD_GAMES` and does `add_subdirectory(testing)`.
  - `Makefile`: `ts: ([ -d node_modules ] || $(NPM) ci --no-audit --no-fund) && $(NPM) run build` at the root.
  - `.gitignore`: `dist`, `dist-tests`, `*.tsbuildinfo`.
  - `games/` empty in git (no `.gitkeep`); the workspace glob `games/*` matches nothing until phase 7.

- [ ] **Step 1: Move the files and create the `package.json`/`tsconfig`.** Imports between packages now use the name (`@oly/core`, `@oly/core/internal`); within a package, relative paths.

- [ ] **Step 2: Generate the lock and compile**

Run: `$WC sh -c 'rm -rf node_modules engine/node_modules && npm install --no-audit --no-fund && npm run build'`
Expected: new `package-lock.json` at the root; `engine/packages/core/dist/index.js`, `dist/internal.js`, `engine/tools/codegen/dist/main.js`, `engine/testing/dist/fixture/schema.js` exist; no `tsc` error (in particular no DOM or node usage in `core/src`).

- [ ] **Step 3: Check that core rejects environment APIs** — temporarily add `window.alert("x")` to `core/src/runtime.ts`; `npm run build` must fail with "Cannot find name 'window'". Undo.

- [ ] **Step 4: Run everything**

Run: `$WC make games-test`, `$DC make test`
Expected: green, with `ts.core.unit`, `ts.codegen.unit`, `ts.testing.wasm` and `oly.lint.boundaries`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: npm workspaces with @oly/core, @oly/codegen and @oly/testing"
```

---

### Task 8: `@oly/core` surface and names, TS lint rules

**Files:**
- Create: `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/tests/wasm/iteration.test.ts`
- Modify: `engine/packages/core/src/{index,internal,errors,runtime,world,query,module}.ts`, uses in `engine/testing/**`, `engine/tools/codegen/**`, `tools/check_boundaries.py`, `tools/test_check_boundaries.py`

**Interfaces:**
- Produces:
  - `ContractError` (formerly `EngineContractError`, `name` = `"ContractError"`), `SchemaMismatchError`.
  - `Runtime` (formerly `EngineRuntime`) with only `schema` and `createWorld()` public; `loadRuntime(factory: OlyModuleFactory, schema: Schema): Promise<Runtime>` (formerly `loadEngine`).
  - In `@oly/core/internal`, besides the list from Task 7: `bridgeOf(runtime: Runtime): Bridge`, `moduleOf(runtime: Runtime): OlyModule`, `componentIdOf(runtime: Runtime, type: SchemaType): number`.
  - Out of the public `.d.ts`: `World.iterationDepth`, `World.assertUsable`, `Block.invalidate`, `Query.writes`, `Query.positionOf`, `Runtime.bridge`, `Runtime.module`, `Runtime.componentId` (any mechanism: `#private`, module functions not re-exported by `index.ts`, or `WeakMap`). Do not use TS `private`: it shows up in the `.d.ts` and the surface test rejects it.
  - `src/index.ts` with named exports, exactly the list in `api/public-exports.txt` (one per line, alphabetical order): `Block`, `ComponentLayout`, `ComponentType`, `ContractError`, `Entity`, `FieldLayout`, `Fields`, `NullEntity`, `OlyModuleFactory`, `Query`, `QuerySpec`, `RawField`, `ReadField`, `ReadView`, `Runtime`, `ScalarType`, `Schema`, `SchemaMismatchError`, `SchemaType`, `SpawnValues`, `TagType`, `ValueOf`, `World`, `WriteField`, `WriteView`, `component`, `defineSchema`, `entityGeneration`, `entityIndex`, `formatEntity`, `loadRuntime`, `tag`.
  - TS rules in `check_boundaries.py` (`.ts` files outside `node_modules/`, `dist/`, `dist-tests/`, `build/`):
    - **R3** import of `@oly/core/internal` outside `engine/` → `"@oly/core/internal can only be used inside engine/"`.
    - **R4** import of `@oly/client` in `engine/packages/core/` → `"@oly/core cannot depend on @oly/client"`.
    - **R5** relative import that resolves outside the file's package (package = closest directory with a `package.json`) → `"relative import crosses packages; use the package name"`.

- [ ] **Step 1: Write `surface.test.ts`** with the TypeScript compiler API (`typescript` is already a root dependency): it creates a `Program` over `engine/packages/core/dist/index.d.ts`, takes `checker.getExportsOfModule` and the properties of the exported classes.

```ts
test("public exports match api/public-exports.txt")   // sorted names === lines of the file
test("World exposes only the glossary API")   // properties === [add, checkInvariants, destroy, dispose, get, handle, has, isAlive, query, remove, set, spawn]
test("Query exposes only eachBlock")
test("Block exposes only count, entity, read, write")
test("Runtime exposes only schema and createWorld")
test("ContractError has name ContractError")   // new ContractError("x").name === "ContractError"
```

- [ ] **Step 2: Write the Review Focus WASM tests** (`engine/testing/tests/wasm/iteration.test.ts`)

```ts
test("exception in the eachBlock callback ends the iteration")   // assert.throws(() => query.eachBlock(() => { throw new Error("x") })); then world.spawn([Position]) works and world.checkInvariants() === null
test("eachBlock nested in another query keeps the outer block")   // outer reads Position.x before and after an inner eachBlock over [Velocity]; values equal to the written ones
test("query created inside eachBlock (memory may grow) does not corrupt the outer block")   // inside the callback: world.query reading all components and an eachBlock; then block.read(Position).x.get(row) still matches the value written for block.entity(row)
```

- [ ] **Step 3: Write the TS rule tests** in `tools/test_check_boundaries.py`

```python
def test_internal_outside_engine_fails(self)       # games/x/src/a.ts imports "@oly/core/internal" → 1, reason R3 with file:line
def test_internal_inside_engine_passes(self)     # engine/testing/tests/a.ts imports "@oly/core/internal" → 0
def test_core_importing_client_fails(self)        # engine/packages/core/src/a.ts imports "@oly/client" → 1
def test_relative_across_packages_fails(self)        # engine/packages/client/src/a.ts imports "../../core/src/world.js" → 1
```

- [ ] **Step 4: See it fail**

Run: `$WC make games-test`; `$DC python3 -I tools/test_check_boundaries.py`
Expected: `ts.core.unit` fails (extra exports, internal members visible, `ContractError` does not exist); the script's 4 new tests fail.

- [ ] **Step 5: Implement** the names, the restricted `index.ts`, the `internal.ts` accessors, the hidden members and rules R3–R5. Update `engine/testing/**` (`loadRuntime`, `moduleOf(runtime)` for the fixture exports, `ContractError`) and `boundary.ts`.

- [ ] **Step 6: Run everything**

Run: `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`, `$DC make test`
Expected: green.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: @oly/core public surface and TS boundary rules"
```

---

### Task 9: `@oly/client`

**Files:**
- Create: `engine/packages/client/{package.json,tsconfig.json,src/index.ts,src/module_factory.ts,tests/tsconfig.json,tests/module_factory.test.ts}`
- Modify: `tsconfig.json` (root, new reference), `engine/CMakeLists.txt` (`ts.client.unit`), `package-lock.json`

**Interfaces:**
- Consumes: `OlyModuleFactory` from `@oly/core` (public surface; the client does not import `./internal`).
- Produces: `moduleFactoryFromUrl(url: string | URL, importModule?: (href: string) => Promise<unknown>): OlyModuleFactory`. Resolves `url` to an absolute `href` (against `globalThis.location?.href` when relative — with `lib: ["DOM"]`), imports the `.mjs` with `importModule` (default: `import(href)`), requires `default` to be a function (otherwise throws an `Error` whose message contains the `href` and `"does not export an Emscripten factory"`) and calls `default({ locateFile: (path: string) => new URL(path, href).href })`, so the `.wasm` is fetched next to the `.mjs`. Client `tsconfig`: `lib: ["ES2022", "DOM"]`, `references` to core.

- [ ] **Step 1: Write the tests** (`node:test`, with a fake `importModule`)

```ts
test("imports the .mjs by absolute URL")              // importModule receives "https://x/games/demo.mjs"
test("locateFile resolves the .wasm next to the .mjs")    // the fake factory receives moduleArg; moduleArg.locateFile("demo.wasm") === "https://x/games/demo.wasm"
test("returns the module created by the factory")          // await factory() === object returned by the fake default
test("missing default is an error with the URL")              // rejects with a message containing "https://x/games/demo.mjs" and "does not export an Emscripten factory"
```

- [ ] **Step 2: See it fail**

Run: `$WC sh -c 'npm install --no-audit --no-fund && npm run build'`
Expected: compile error (`moduleFactoryFromUrl` does not exist).

- [ ] **Step 3: Implement** `module_factory.ts` and `index.ts` (exports only `moduleFactoryFromUrl`).

- [ ] **Step 4: Run**

Run: `$WC make games-test`, `$DC make test T='^oly\.'`
Expected: green, with `ts.client.unit`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: @oly/client with Emscripten module factory by URL"
```

---

### Task 10: Benchmarks in `docs/benchmarks/` and rerun of the targets

**Files:**
- Move (`git mv`): `bench/results/README.md` → `docs/benchmarks/README.md`; `bench/results/{m*,e1_*}` → `docs/benchmarks/ecs/`; `bench/results/wasm/**` → `docs/benchmarks/wasm/**`; `bench/results/engine/**` → `docs/benchmarks/bridge/**`
- Create: `docs/benchmarks/ecs/r1_{iterate,fragmented,flush}.json`, `docs/benchmarks/bridge/boundary_oly.json`
- Modify: `docs/benchmarks/README.md`, `docs/benchmarks/bridge/README.md` (and the `wasm/` README, if any), `tools/run_ladder.sh` (`docs/benchmarks/wasm/runs/$bench`, `OLY_LADDER_EXTRA_ARGS`, names `ecs_bench_*`), `tools/perf_fragmented.sh` (usage with `ecs_bench_fragmented`), `tools/extract_codegen.sh` and `tools/bench_ladder.py` if they mention old paths or names

- [ ] **Step 1: Move and update paths and commands** in the living READMEs (without editing old JSON/TXT).

- [ ] **Step 2: Compile and measure** (repeat up to 3 times if a ratio misses the target, because of WSL2 noise)

```sh
$DC make build PRESET=release
P="--benchmark_repetitions=10 --benchmark_report_aggregates_only=true"
$DC ./build/release/bench/ecs_bench_iterate $P --benchmark_out=docs/benchmarks/ecs/r1_iterate.json
$DC ./build/release/bench/ecs_bench_fragmented $P --benchmark_out=docs/benchmarks/ecs/r1_fragmented.json
$DC ./build/release/bench/ecs_bench_flush $P --benchmark_out=docs/benchmarks/ecs/r1_flush.json
```

- [ ] **Step 3: Check the targets**

```sh
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/r1_iterate.json --name BM_IterateSingle_ECS/1000000 --baseline BM_IterateSingle_SoA/1000000 --max 1.10
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/r1_fragmented.json --name BM_IterateFragmented/64 --baseline BM_IterateSingle_SoA/1000000 --max 1.25
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/r1_flush.json --name BM_FlushCoalesced/1000 --baseline BM_DirectAdds/1000 --max 0.5
```
Expected: all three pass. A regression confirmed across repetitions blocks delivery: stop and investigate with `superpowers:systematic-debugging`.

- [ ] **Step 4: Boundary benchmark**

```sh
$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'
$WC node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/boundary_oly.json --commit $(git rev-parse --short HEAD)
```
Expected: JSON written. Record in `bridge/README.md` a table comparing with `boundary_phase1.json` (no numeric target).

- [ ] **Step 5: Record** the `r1_*` ratios in the table and text of `docs/benchmarks/README.md` (same format as the `e1_*` row).

- [ ] **Step 6: Check the ladder** (short run only, without recording results)

Run: `OLY_LADDER_EXTRA_ARGS=--benchmark_min_time=0.01s tools/run_ladder.sh 1 ecs_bench_iterate V0`
Expected: writes `docs/benchmarks/wasm/runs/ecs_bench_iterate/…`; delete the file afterwards.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "bench: results in docs/benchmarks and r1 rerun of the NF-P1, NF-P2 and NF-P5 targets"
```

---

### Task 11: Living documentation, name sweep and final verification

**Files:**
- Modify: `CLAUDE.md`, `.claude/skills/add-test-case/SKILL.md`, `.claude/skills/bench-compare/SKILL.md`, `.claude/agents/perf-reviewer.md`, `.claude/agents/spec-conformance-reviewer.md`

- [ ] **Step 1: Rewrite `CLAUDE.md`** to cover: tree (§3 of the spec), commands (`make test T='^ecs\.'`, `make ts`, `make games-test`, presets `wasm-games*`, executables `build/<preset>/tests/ecs_<suite>` and `bench/ecs_bench_*`, `docs/benchmarks/`), modules and `DEPENDS`, boundaries (`oly.lint.boundaries`, `detail/`, `@oly/core/internal`), glossary (§5.2), `OLY_ASSERT`/`OLY_ENABLE_ASSERTS`, `oly_add_test`/`oly_add_contract_death`, test accessors, free `checkInvariants(world)`, bridge rules with `ContractError`/`loadRuntime`. Keep the style and correctness sections.

- [ ] **Step 2: Update skills and agents** to the new paths, targets (`oly_add_test`, `ecs.contract_death.<case>`, `ecs_bench_*`) and names (`QueryById` instead of `raw_query`/`RawQuery`, `OLY_ASSERT`).

- [ ] **Step 3: Criterion 4 sweep**

Run:
```sh
git grep -nE 'ECS_ASSERT|ECS_ENABLE_ASSERTS|ENGINE_API|\bengine[A-Z][A-Za-z]*\(|Engine(World|Query|Entity|Module|Runtime|ContractError)|loadEngine|\b(add|remove|has|get)Raw\b|RawQuery|rawQuery|\.create\(|\bcreate\(\)|(^|[^:])\becs::|include "ecs/|include "engine/|bench/results|build/ts|engine-ts|engine-test|wasm-engine' -- . ':!docs/superpowers/specs' ':!docs/superpowers/plans' ':!docs/benchmarks/**/*.json' ':!docs/benchmarks/**/*.txt'
```
Expected: no occurrences, or only justified occurrences (e.g. `World.create` of some API other than entities, `createWorld`) noted in the commit. Fix the rest.

- [ ] **Step 4: Criterion 2 full matrix**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release'`, `$WC make test PRESET=native-clang`, `$WC make test PRESET=native-clang-32`, `$WC make test PRESET=native-clang-32-scalar`, `$WC make test PRESET=native-clang-avx2`, `$WC make test PRESET=wasm-scalar`, `$WC make test PRESET=wasm-simd`
Expected: all green; `debug-asan` with 12 `ecs.contract_death.*` and 1 `bridge.contract_death.*`.

- [ ] **Step 5: Conformance review** — dispatch `spec-conformance-reviewer` over `git diff develop...HEAD` (focus: `checkInvariants` still covers the `QueryById` caches, iteration guard in `iterate()`). Fix what is confirmed.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "docs: CLAUDE.md, skills and agents for the Oly tree and names"
```
