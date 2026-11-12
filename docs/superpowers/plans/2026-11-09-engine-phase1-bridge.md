# Engine — Phase 1 (Bridge) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** WASM build of the engine with a C API, a TS package with a single schema + C++ generator, and copy-free TS access (`spawn`/`destroy`/`get`/`set`/`eachBlock` with views), with the boundary benchmark recorded.

**Architecture:** The ECS gains a type-erased query by `ComponentId` (`RawQuery`) and `CommandBuffer::addRaw`. Above the `world`, a thin C++ core (`engine/`) exports an `extern "C"` C API over an `EngineWorld`. The schema is a TypeScript file; the same TS code that computes the layout serves the runtime (descriptors) and the generator (C++ structs with `static_assert`), and a hash of the schema compiled into the WASM detects a stale WASM. TS iterates by block: one `engineQueryBegin` call returns the descriptors of every table, and fields are read with a stride over views of the WASM heap.

**Tech Stack:** C++20, GCC 14 (native), Emscripten 3.1.74 + node 20.18 (`wasm` image), CMake ≥ 3.25 with v6 presets, doctest 2.4.11, Google Benchmark 1.8.3, TypeScript 5.6.3 (`tsc`, `strict`), `@types/node` 20.17.6, `node:test` test runner.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (phase 1 of §5). The `§N` sections below are from that spec. Requirements `R-*`, invariants `I-*` and targets `NF-*` are from `2026-10-16-ecs-archetype-spec.md` and still apply.

## Global Constraints

- Branch `feat/engine-bridge` (not `master`). Commits, comments, assert and error messages in English; commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain only in Docker: native in the `dev` service (`gcc:14`), WASM and everything node/TS in the `wasm` service (`emscripten/emsdk:3.1.74`, node 20.18). The host does not need node (§3).
- `-Wall -Wextra -Wpedantic -Werror` on every C++ target (including generated code and engine tests); TypeScript `strict` (§3).
- Layers: the engine sits above the `world` and only uses the ECS public API; nothing in `include/ecs/` or `src/` includes `engine/` (§3).
- Reusable engine: nothing in `engine/include`, `engine/src`, `engine/ts`, `engine/generator` knows game concepts; game names only in `engine/fixture/` (§2).
- Boundary: never cross WASM↔JS per entity on a hot path; `eachBlock` crosses once per call to get every block (§2, §4.2).
- `extern "C"` C API exported with `EMSCRIPTEN_KEEPALIVE`; no embind (§4.2).
- Column layout unchanged: a column of structs (AoS inside the component); field = typed array with a stride (§4.2).
- Validity: views only inside the `eachBlock` callback; only `Entity` is a stable handle (§3, §4.2).
- Zero regression in the ECS: presets, tests, `checkInvariants`, and the ratios NF-P1 (≤ 1.10), NF-P2 (≤ 1.25), NF-P5 (`/1000` ≤ 0.5) stay as in `bench/results/README.md` (§2).
- `CLAUDE.md` style: braces on every `if`/`for`, full names, `PascalCase`/`camelCase`, private members with `_`, data at the top of the class, one `namespace` per file. In TS: the same full names, `camelCase`, ESM imports with the `.js` extension.

### How to run commands

```sh
DC="docker compose run --rm dev"     # native GCC 14
WC="docker compose run --rm wasm"    # Emscripten + node + npm
$WC make engine-ts                   # npm ci (if needed) + tsc → build/ts/   (Task 4)
$WC make engine-test                 # engine-ts + ctest on the wasm-engine preset (Task 5)
```

In the host's Git Bash, prefix with `MSYS_NO_PATHCONV=1` when the command has absolute paths.

### Decisions the spec leaves open (fixed here)

- **Directories:** everything of the engine in `engine/`: `include/engine/` + `src/` (C++ core), `ts/` (TS package), `generator/` (generator), `fixture/` (minimal test game: schema + C++ exports), `test/unit/` and `test/wasm/` (TS tests), `tests/` (C++ doctest), `bench/`. `package.json`/`tsconfig.json` in `engine/`; `tsc` compiles everything to `build/ts/` (gitignored).
- **Schema in TS, no generated TS:** `component()`/`tag()` compute the layout on the spot with `computeLayout`, and the generator uses the same function to emit the C++. So the "TS descriptor" (§4.1) is the schema object itself; there is no generated TS file that could diverge.
- **Schema hash:** 32-bit FNV-1a of the canonical form (`Name{field:type,...};Tag;`), emitted in the generated C++ (`engineSchemaHash`) and checked by `loadEngine`. Together with size/alignment per component, it catches a WASM compiled with another schema.
- **Scalar types:** `f32 f64 i8 u8 i16 u16 i32 u32 entity` (no 64-bit integers: they would require `BigInt`). `entity` is `ecs::Entity` (8 B, alignment 4). Fields in declaration order, natural alignment.
- **Events (`event(...)`) are left for phase 3**, reusing the same layout; phase 1 only has `component` and `tag`.
- **TS storage per `Entity` for data outside the schema (§4.1) is left for phase 2:** cleaning it up follows destruction, which there becomes deferred by the phase's `CommandBuffer`; doing it now would be redone.
- **`Entity` in TS:** `number` = `generation * 2^32 + index`; `generation ≤ MaxEntityGeneration = 0x1FFFFF` (2²¹−1, keeps the number within 53 bits). Above that `packEntity` throws `EngineContractError` (≈ 2 million reuses of the same slot).
- **Heap and memory growth:** fields read `heap.f32[...]` etc. through a shared `Heap` object; every `Bridge` call that may allocate in the WASM calls `sync()`, which recreates the views if `module.HEAPU8.buffer` changed. Nobody outside the `Bridge` calls exports.
- **Usage errors in TS never reach `ECS_ASSERT`:** `World`/`Query` validate before calling the C API (in Release the asserts vanish and it would be UB). Error = `EngineContractError` with the operation, the `Entity` (`index:generation`) and the type name.
- **Structural change inside `eachBlock` in TS:** `EngineContractError` in phase 1 (the per-phase `CommandBuffer` comes in phase 2).
- **`RawQuery` does not replace `Query<Ts...>`:** the template APIs keep their own path (inline, no indirection), because delegating would affect NF-P1/P2. `CommandBuffer::add<T>` also keeps constructing in place. "When it makes sense" (§3) = no case in this phase; recorded in `CLAUDE.md`.
- **TS tooling:** only `typescript@5.6.3` and `@types/node@20.17.6` (exact devDependencies, `package-lock.json` versioned). No bundler in phase 1 (node only). Test runner `node --test <directory>`.
- **New presets:** `wasm-engine` (Debug, asserts on, `ENGINE_BUILD_FIXTURE=ON`) and `wasm-engine-release` (Release, `ECS_ENABLE_ASSERTS=0`, `ENGINE_BUILD_FIXTURE=ON`). The existing presets do not turn on the fixture and do not need node.
- **Game module link:** `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web -sEXPORTED_FUNCTIONS=_malloc,_free -sEXPORTED_RUNTIME_METHODS=HEAPU8 -sEXIT_RUNTIME=0`, suffix `.mjs`. `engine_core` is an `OBJECT` library (an unreferenced member of a `.a` would be discarded and take the `EMSCRIPTEN_KEEPALIVE`s with it).

## Review Focus

1. **Wrong usage in TS** (dead entity, absent component, `write` of a read-only component, tag in `read`, `spawn` inside `eachBlock`): a readable `EngineContractError` in TS, and the WASM stays usable — never an `abort` of the module nor UB in Release (Tasks 6 and 7).
2. **Exception thrown inside the `eachBlock` callback:** propagates to the caller, the iteration is ended (`engineQueryEnd`) and the `World` accepts structural changes again with a clean `checkInvariants` (Task 7).
3. **WASM memory growth with live views** (allocation inside the callback): following reads and writes land in the right memory and the C++ sees the values (Task 7).
4. **WASM stale relative to `schema.ts`** (a field changed, WASM not recompiled): `loadEngine` rejects with `SchemaMismatchError` citing both hashes, instead of reading garbage (Task 6).
5. **Nested `eachBlock` over the same query** (e.g. enemy × enemy pairs): both loops see the right blocks; the outer one's descriptors are not overwritten by the inner one (Task 7).

---

### Task 1: `RawQuery` in the ECS (query by `ComponentId`)

**Files:**
- Create: `include/ecs/raw_query.hpp`
- Modify: `include/ecs/query.hpp` (extract `detail::signatureMatches`), `include/ecs/world.hpp`, `src/world.cpp`
- Test: `tests/raw_query.cpp` (new, register in `tests/CMakeLists.txt`), `tests/contract_death.cpp` + `foreach` list

**Interfaces:**
- Produces:
  - `bool ecs::detail::signatureMatches(const Signature& signature, std::span<const ComponentId> with, std::span<const ComponentId> without)` — used by `Query<Ts...>::matches` and `RawQuery::matches` (same behaviour as today).
  - `struct ecs::RawQuerySpec { std::vector<ComponentId> read, write, with, without; };`
  - `struct ecs::RawMatchedTable { Archetype* archetype; std::vector<uint16_t> columns; };` — `columns[i]` is the column of `components()[i]`.
  - `class ecs::RawQuery : public QueryCacheBase` with `std::span<const ComponentId> components() const noexcept` (= `read` followed by `write`, in the given order), `size_t matchedCount() const noexcept`, `std::span<const RawMatchedTable> matchedTables() const noexcept`, `template <class F> void eachBlock(F&& callback)` with `callback(Table& table, std::span<const uint16_t> columns)` once per non-empty table, `void beginIteration() noexcept`, `void endIteration() noexcept`, `uint32_t iterationDepth() const noexcept`.
  - `RawQuery& ecs::World::rawQuery(const RawQuerySpec& spec)` — cached by spec.

- [ ] **Step 1: Create the branch** `git switch -c feat/engine-bridge`.

- [ ] **Step 2: Failing tests** in `tests/raw_query.cpp` (doctest, components `A`, `B`, `Tag` from `tests/support/components.hpp`; `checkInvariants` after each structural operation):

```cpp
TEST_CASE("rawQuery delivers columns in read-then-write order") {
  // 10 entities A{i}, B{i, 2*i}; spec {read={id<A>}, write={id<B>}}
  // eachBlock: A* a = table.column(columns[0]).data; B* b = ...columns[1]; sum a[r].value == 45; b[r].first += 100
  // afterwards: world.get<B>(e).first == i + 100 for all of them
}
TEST_CASE("rawQuery is cached by spec") { CHECK(&world.rawQuery(spec) == &world.rawQuery(spec)); /* read {A,B} vs {B,A}: different instances */ }
TEST_CASE("rawQuery and Query<Ts...> with the same signature are distinct instances") { /* query<const A, B>() and rawQuery{read={A}, write={B}}: different addresses, both iterate */ }
TEST_CASE("rawQuery follows archetypes created later") { /* query first; create A,B,Tag; matchedCount() == 1; eachBlock sees the entity */ }
TEST_CASE("rawQuery filters by with and without") { /* 3 with Tag, 2 without: with={Tag} → 3 rows; without={Tag} → 2 */ }
TEST_CASE("rawQuery skips empty tables") { /* create and destroy: zero callbacks */ }
TEST_CASE("beginIteration and endIteration mark the World as iterating") {
  // begin: world.isIterating() == true, iterationDepth() == 1; begin again: 2; end, end: false; checkInvariants ok
}
```

And in `tests/contract_death.cpp` (add the names to the `foreach` list):

| Case | Action | Required substrings |
|---|---|---|
| `raw_query_tag_in_read` | `rawQuery({read={id<Tag>}})` | `rawQuery`, `tag` |
| `raw_query_repeated_component` | `read={id<A>}, write={id<A>}` | `rawQuery`, `repeated` |
| `raw_query_unknown_component` | `read={registeredTypeCount() + 5}` | `rawQuery`, `unknown` |
| `raw_query_end_without_begin` | `endIteration()` directly | `endIteration` |
| `add_during_raw_iteration` | `beginIteration()` and `world.add(entity, B{})` | `add` |

- [ ] **Step 3: Watch it fail** — `$DC make test T='raw_query|contract_death'` → compile error (`raw_query.hpp` does not exist).

- [ ] **Step 4: Implement.** `RawQuery` in `include/ecs/raw_query.hpp` (same layer as `query.hpp`). `World::rawQuery` validates with `ECS_ASSERT` (messages from the table above, with the type name when there is one), builds `with` = read ∪ write ∪ with, sorted and without duplicates, and stores it in its own map `rawQueryCache_` (key `QueryKey`, read as const) — not in `queryCache_`, whose `static_cast<Query<Ts...>&>` would confuse the types. Registers in `queries_` like the others (receives `onArchetypeCreated` and goes into `checkQueries`, I-8). `beginIteration` increments `*iterating_` and `iterationDepth_`; `endIteration` requires `iterationDepth_ > 0`. `eachBlock` uses `IterationGuard`.

- [ ] **Step 5: Watch it pass** — `$DC make test` (all suites, `debug-asan`) and `$WC make test PRESET=wasm-debug`. Expected: all green, including the 5 new `contract_death` cases.

- [ ] **Step 6: Commit** `feat: RawQuery, ECS query by ComponentId`.

---

### Task 2: `CommandBuffer::addRaw` and no regression in the ECS benchmarks

**Files:**
- Modify: `include/ecs/command_buffer.hpp`, `src/command_buffer.cpp`, `bench/iterate.cpp`
- Test: `tests/command_buffer.cpp`
- Create: `bench/results/e1_iterate.json`, `bench/results/e1_fragmented.json`, `bench/results/e1_flush.json`; Modify: `bench/results/README.md`

**Interfaces:**
- Consumes: `World::rawQuery`, `RawQuery::eachBlock` (Task 1).
- Produces: `void ecs::CommandBuffer::addRaw(Entity entity, ComponentId componentId, void* value)` — moves from `value` (memcpy if trivial, `moveConstruct` otherwise; the caller still owns the moved-from object); `value` ignored for tags.

- [ ] **Step 1: Failing tests** in `tests/command_buffer.cpp`:

```cpp
TEST_CASE("addRaw records a trivial component by ID") { /* spawn; A value{7}; addRaw(e, id<A>, &value); flush → get<A>(e).value == 7 */ }
TEST_CASE("addRaw of a tag ignores the value") { /* addRaw(e, id<Tag>, nullptr); flush → has<Tag>(e) */ }
TEST_CASE("addRaw moves a non-trivial type") {
  // { LifeCounter source{9}; buffer.addRaw(e, id<LifeCounter>, &source); } flush → get<LifeCounter>(e).value == 9
  // destroy(e) and buffer destroyed → LifeCounter::alive == 0
}
TEST_CASE("addRaw coalesces like add<T> in the flush") {
  // two worlds with the same sequence (spawn, addRaw A, removeRaw A, addRaw B / add<A>, remove<A>, add<B>)
  // same final state: has<A> false, get<B> equal, checkInvariants ok on both
}
```

- [ ] **Step 2: Watch it fail** — `$DC make test T=command_buffer` → `addRaw` is not a member.

- [ ] **Step 3: Implement** `addRaw` with `pushRecord` + `TypeInfo` (`size`, `alignment`, `trivial`). `add<T>` does not change (constructs in place, no extra copy).

- [ ] **Step 4: Watch it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug`.

- [ ] **Step 5: `BM_IterateSingle_RawQuery` benchmark** in `bench/iterate.cpp`, same as `BM_IterateSingle_ECS` but with `rawQuery({.read = {id<Velocity>}, .write = {id<Position>}})` and `eachBlock` turning `columns` into pointers (`columns[0]` is `Velocity`, `columns[1]` is `Position`: read before write); same `Arg`s. Measures the cost of the type-erased query in C++.

- [ ] **Step 6: Rerun and check the targets**

```sh
$DC make build PRESET=release
$DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/e1_iterate.json
$DC ./build/release/bench/bench_fragmented --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/e1_fragmented.json
$DC ./build/release/bench/bench_flush --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/e1_flush.json
$DC python3 -I tools/bench_ratio.py --json bench/results/e1_iterate.json --name BM_IterateSingle_ECS/1000000 --baseline BM_IterateSingle_SoA/1000000 --max 1.10
$DC python3 -I tools/bench_ratio.py --json bench/results/e1_fragmented.json --name BM_IterateFragmented/64 --baseline BM_IterateSingle_SoA/1000000 --max 1.25
$DC python3 -I tools/bench_ratio.py --json bench/results/e1_flush.json --name BM_FlushCoalesced/1000 --baseline BM_DirectAdds/1000 --max 0.5
```

Expected: exit 0 on all three (`bench_fragmented` has its own `BM_IterateSingle_SoA/1000000`, as in `m3_fragmented.json`). `BM_FlushCoalesced/100000` is recorded and compared with the ~0.85 already documented. If a ratio gets worse, repeat the run before concluding (noisy environment); a confirmed regression blocks the task.

- [ ] **Step 7: Record** in `bench/results/README.md`: `e1_*.json` rows in the table ("rerun after `RawQuery`/`addRaw`, engine phase 1"), the ratios obtained and the ratio `BM_IterateSingle_RawQuery/N` / `BM_IterateSingle_ECS/N` per N.

- [ ] **Step 8: Commit** `feat: CommandBuffer::addRaw and rerun of the ECS benchmarks`.

---

### Task 3: Engine C++ core — C API

**Files:**
- Create: `engine/CMakeLists.txt`, `engine/include/engine/c_api.hpp`, `engine/include/engine/world_handle.hpp`, `engine/include/engine/schema_registry.hpp`, `engine/src/c_api.cpp`
- Create: `engine/tests/c_api.cpp`, `engine/tests/test_schema_registry.cpp`
- Modify: `CMakeLists.txt` (root: `add_subdirectory(engine)` after the tests block; option `ENGINE_BUILD_FIXTURE` OFF)

**Interfaces:**
- Consumes: `World::rawQuery`, `RawQuery::{matchedTables, beginIteration, endIteration}` (Task 1); `World::{create, destroy, isAlive, addRaw, removeRaw, hasRaw, getRaw, checkInvariants}`.
- Produces (`engine/include/engine/c_api.hpp`, everything inside `extern "C"`, each function with `ENGINE_API` = `EMSCRIPTEN_KEEPALIVE` under `__EMSCRIPTEN__`, empty otherwise):

```cpp
struct EngineWorld;                                   // defined in world_handle.hpp
struct EngineQuery;                                   // opaque: it is an ecs::RawQuery*
struct EngineEntity { uint32_t index; uint32_t generation; };   // static_assert: same layout as ecs::Entity
EngineWorld* engineWorldCreate();
void engineWorldDestroy(EngineWorld* world);
uint32_t engineSchemaHash();
uint32_t engineSchemaComponentCount();
uint32_t engineSchemaComponentId(uint32_t schemaIndex);
uint32_t engineComponentSize(uint32_t componentId);      // 0 for a tag
uint32_t engineComponentAlignment(uint32_t componentId);
void engineCreate(EngineWorld* world, EngineEntity* out);
void engineDestroy(EngineWorld* world, uint32_t index, uint32_t generation);
int32_t engineIsAlive(EngineWorld* world, uint32_t index, uint32_t generation);
void engineAdd(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
void engineRemove(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
int32_t engineHas(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
void* engineGet(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);   // nullptr if dead, absent or a tag
// componentIds = read | write | with | without, concatenated
EngineQuery* engineQuery(EngineWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount);
// Starts the iteration and returns the number of blocks (non-empty tables). *outDescriptors points to
// blocks × (2 + readCount + writeCount) uintptr_t: [count, address of the entities, address of each column...].
// Valid until the next call that iterates or changes the World.
uint32_t engineQueryBegin(EngineWorld* world, EngineQuery* query, const uintptr_t** outDescriptors);
void engineQueryEnd(EngineWorld* world, EngineQuery* query);
int32_t engineCheckInvariants(EngineWorld* world, char* message, uint32_t capacity);   // 1 ok; 0 and the reason truncated with NUL
```

- `engine/include/engine/world_handle.hpp`: `struct EngineWorld { ecs::World world; std::vector<uintptr_t> blockDescriptors; };` (used by the games' C++ exports).
- `engine/include/engine/schema_registry.hpp`: `namespace engine { uint32_t schemaHash() noexcept; std::span<const ecs::ComponentId> schemaComponentIds(); }` — **defined by the game's generated code** (Task 5) or, in the tests, by `test_schema_registry.cpp`.
- CMake: `engine_core` (`add_library(... OBJECT engine/src/*.cpp)`, include `engine/include` PUBLIC, link `ecs` PUBLIC, `ecs_warnings`); `engine_add_test(<name> <sources...>)` same as `ecs_add_test` but linking `engine_core` and with the `tests/support` include.

- [ ] **Step 1: Failing tests** in `engine/tests/c_api.cpp`, with `test_schema_registry.cpp` registering `A`, `B`, `Tag` (in that order) and `schemaHash() == 0x1234u`:

```cpp
TEST_CASE("schema exposed by the C API") {
  CHECK(engineSchemaHash() == 0x1234u); CHECK(engineSchemaComponentCount() == 3);
  CHECK(engineSchemaComponentId(1) == ecs::componentId<B>());
  CHECK(engineComponentSize(ecs::componentId<A>()) == 8); CHECK(engineComponentAlignment(ecs::componentId<A>()) == 8);
  CHECK(engineComponentSize(ecs::componentId<Tag>()) == 0);
}
TEST_CASE("create, add, get, has, remove and destroy by ID") {
  // engineCreate → out; A value{5}; engineAdd(..., id<A>, &value) → static_cast<A*>(engineGet(...))->value == 5
  // engineHas == 1; engineRemove → engineHas == 0 and engineGet == nullptr; engineDestroy → engineIsAlive == 0
  // engineCheckInvariants(world, buffer, 256) == 1 after each step
}
TEST_CASE("engineQueryBegin delivers descriptors per block") {
  // 3 entities {A{1..3}, B} and 2 entities {A{10,20}, B, Tag}; ids = {A | B}, readCount 1, writeCount 1
  // begin → 2 blocks; for each: descriptor[0] = count, [1] = Entity*, [2] = A*, [3] = B*
  // sum of the A values == 36; descriptor entities isAlive; world.isIterating() true until engineQueryEnd, false afterwards
}
TEST_CASE("engineQuery is cached") { /* same ids/counts → same pointer; with {Tag} → another one */ }
TEST_CASE("engineQueryBegin nested on the same query") {
  // outer begin; copy the descriptors; inner begin → same number of blocks; inner end; outer end; isIterating false
}
```

- [ ] **Step 2: Watch it fail** — `$DC make test T=engine` → `engine/` is not in the build.

- [ ] **Step 3: Implement** `engine/src/c_api.cpp`. The exports convert `(index, generation)` into `ecs::Entity` and delegate to the `World` (contract validation stays with the ECS `ECS_ASSERT`; TS validates first, Task 6). `engineQueryBegin` calls `beginIteration` and fills `blockDescriptors` from `matchedTables()`, skipping `count == 0`.

- [ ] **Step 4: Watch it pass** — `$DC make test` (includes `engine_c_api` in `debug-asan`) and `$WC make test PRESET=wasm-debug` (the same test in WASM via node).

- [ ] **Step 5: Commit** `feat: engine C++ core with a C API over the World`.

---

### Task 4: TS tooling and schema (DSL, layout, hash, `Entity`)

**Files:**
- Create: `engine/package.json`, `engine/package-lock.json`, `engine/tsconfig.json`
- Create: `engine/ts/schema.ts`, `engine/ts/layout.ts`, `engine/ts/schema_hash.ts`, `engine/ts/entity.ts`, `engine/ts/errors.ts`, `engine/ts/index.ts`
- Test: `engine/test/unit/layout.test.ts`, `engine/test/unit/schema.test.ts`, `engine/test/unit/entity.test.ts`
- Modify: `Makefile` (`engine-ts` target), `.gitignore` (`node_modules`)

**Interfaces:**
- Produces:
  - `layout.ts`: `type ScalarType = "f32" | "f64" | "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "entity"`; `interface FieldLayout { name: string; type: ScalarType; offset: number }`; `interface ComponentLayout { size: number; alignment: number; fields: readonly FieldLayout[] }`; `scalarSize(type)`, `scalarAlignment(type)` (`entity`: 8 and 4); `computeLayout(fields: Fields): ComponentLayout`.
  - `schema.ts`: `type Fields = Readonly<Record<string, ScalarType>>`; `type ValueOf<F extends Fields> = { [K in keyof F]: number }`; `interface ComponentType<Name extends string = string, F extends Fields = Fields> { readonly kind: "component"; readonly name: Name; readonly fields: F; readonly layout: ComponentLayout; readonly schemaIndex: number }`; `interface TagType<Name extends string = string> { readonly kind: "tag"; readonly name: Name; readonly schemaIndex: number }`; `type SchemaType = ComponentType | TagType`; `component(name, fields)`, `tag(name)` (with `const` type parameters, `schemaIndex` = −1 until it joins a schema); `interface Schema { readonly types: readonly SchemaType[]; readonly hash: number }`; `defineSchema(types: readonly SchemaType[]): Schema`.
  - `schema_hash.ts`: `fnv1a32(text: string): number` (unsigned), `canonicalSchema(types): string`, `schemaHash(types): number`.
  - `entity.ts`: `type Entity = number`; `MaxEntityGeneration = 0x1FFFFF`; `packEntity(index, generation): Entity`; `entityIndex(entity)`; `entityGeneration(entity)`; `NullEntity` (= `packEntity(0xFFFFFFFF, 0)`); `formatEntity(entity): string` (`"index:generation"`).
  - `errors.ts`: `class EngineContractError extends Error`, `class SchemaMismatchError extends Error`.
  - `package.json`: `"type": "module"`, scripts `build` (`tsc -p .`), `test:unit` (`node --test ../build/ts/test/unit/`), `test:wasm` (`node --test ../build/ts/test/wasm/`). `tsconfig.json`: `strict`, `target ES2022`, `module`/`moduleResolution` `NodeNext`, `rootDir "."`, `outDir "../build/ts"`, `include` `ts`, `generator`, `fixture`, `test`, `bench`.
  - `Makefile`: `engine-ts` = `cd engine && ([ -d node_modules ] || npm ci --no-audit --no-fund) && npm run build`.

- [ ] **Step 1: Tooling.** `$WC sh -c 'node --version && npm --version'` → v20.18.x and an npm. If `npm` is not on the `PATH`, use `$(dirname $(command -v node))/npm` in the Makefile target. Create `package.json`/`tsconfig.json`, `$WC sh -c 'cd engine && npm install --save-exact --save-dev typescript@5.6.3 @types/node@20.17.6'` (generates the lock).

- [ ] **Step 2: Failing tests** (`node:test` + `node:assert/strict`):

```ts
// layout.test.ts
test("Position f32,f32", () => assert.deepEqual(computeLayout({ x: "f32", y: "f32" }), { size: 8, alignment: 4, fields: [{ name: "x", type: "f32", offset: 0 }, { name: "y", type: "f32", offset: 4 }] }));
test("padding before f32", () => offsets({ a: "u8", b: "f32" }) === [0, 4], size 8);
test("f64 aligns the struct to 8", () => offsets({ a: "f32", b: "f64" }) === [0, 8], size 16, alignment 8);
test("u8,u16,u8", () => offsets === [0, 2, 4], size 6, alignment 2);
test("entity has 8 bytes and alignment 4", () => offsets({ owner: "entity", flag: "u8" }) === [0, 8], size 12, alignment 4);

// schema.test.ts
test("component without fields is an error", () => assert.throws(() => component("Empty", {}), /tag/));
test("invalid names", () => { /* component("1abc", ...), component("Ok", { class: "f32" }), component("Ok", { _x: "f32" }) throw */ });
test("defineSchema assigns schemaIndex in order and rejects a repeated name", ...);
test("type already used in another schema is an error", ...);
test("fnv1a32", () => { assert.equal(fnv1a32(""), 0x811c9dc5); assert.equal(fnv1a32("a"), 0xe40c292c); });
test("canonical form", () => assert.equal(canonicalSchema([component("Position", { x: "f32", y: "f32" }), tag("Player")]), "Position{x:f32,y:f32};Player;"));
test("hash changes with order and with field type", ...);   // schema.hash === schemaHash(schema.types)

// entity.test.ts
test("packs and unpacks", () => { const e = packEntity(5, 3); assert.equal(e, 3 * 2 ** 32 + 5); assert.equal(entityIndex(e), 5); assert.equal(entityGeneration(e), 3); });
test("maximum generation fits in a safe integer", () => assert.ok(Number.isSafeInteger(packEntity(0xFFFFFFFF, MaxEntityGeneration))));
test("generation above the maximum is an error", () => assert.throws(() => packEntity(0, MaxEntityGeneration + 1), EngineContractError));
test("NullEntity", () => { assert.equal(entityIndex(NullEntity), 0xFFFFFFFF); assert.equal(entityGeneration(NullEntity), 0); });
```

Valid names: component/tag `^[A-Z][A-Za-z0-9]*$`; field `^[a-z][A-Za-z0-9]*$` and outside a list of C++ reserved words (`class`, `struct`, `int`, `float`, `double`, `char`, `long`, `short`, `unsigned`, `signed`, `bool`, `void`, `auto`, `return`, `operator`, `template`, `namespace`, `new`, `delete`, `this`, `default`, `case`, `switch`, `register`, `union`, `enum`, `const`, `static`, `virtual`, `public`, `private`, `protected`, `friend`, `using`, `typename`, `do`, `if`, `else`, `for`, `while`, `goto`, `break`, `continue`, `try`, `catch`, `throw`, `true`, `false`, `nullptr`, `sizeof`, `alignof`, `export`, `extern`, `inline`, `mutable`, `volatile`, `explicit`, `concept`, `requires`, `and`, `or`, `not`, `xor`).

- [ ] **Step 3: Watch it fail** — `$WC sh -c 'make engine-ts && cd engine && npm run test:unit'` → compile/import errors.

- [ ] **Step 4: Implement** the modules. `computeLayout`: offset = align(current offset, field alignment) in declaration order; final size aligned to the largest alignment. `defineSchema` freezes the objects and assigns `schemaIndex` (error if the type already has `schemaIndex ≥ 0`). `index.ts` re-exports the public API.

- [ ] **Step 5: Watch it pass** — `$WC sh -c 'make engine-ts && cd engine && npm run test:unit'` → all `ok`.

- [ ] **Step 6: Commit** `feat: engine TS package with schema, layout, hash and Entity`.

---

### Task 5: C++ generator, `engine_add_game`, presets and the fixture game

**Files:**
- Create: `engine/generator/emit_cpp.ts`, `engine/generator/main.ts`
- Create: `engine/fixture/schema.ts`, `engine/fixture/systems.cpp`
- Test: `engine/test/unit/generator.test.ts`
- Modify: `engine/CMakeLists.txt` (`engine_add_game`, fixture, TS tests in ctest), `CMakePresets.json`, `Makefile` (`engine-test` target)

**Interfaces:**
- Consumes: `computeLayout`, `Schema`, `schemaHash` (Task 4); `engine_core`, `schema_registry.hpp`, `world_handle.hpp` (Task 3).
- Produces:
  - `emitHeader(schema: Schema): string` — `#pragma once`, `<cstddef>`, `<cstdint>`, `"ecs/entity.hpp"`; one `struct` per type in the global namespace, fields in order (`f32→float`, `f64→double`, `i8→int8_t`, `u8→uint8_t`, `i16→int16_t`, `u16→uint16_t`, `i32→int32_t`, `u32→uint32_t`, `entity→ecs::Entity`); tag = `struct Name {};`; for each component `static_assert(sizeof(T) == S && alignof(T) == A, "schema: layout of T diverges from TS")` and `static_assert(offsetof(T, field) == O, ...)`.
  - `emitRegistry(schema: Schema): string` — includes the generated header and `engine/schema_registry.hpp`; defines `engine::schemaHash()` (literal `0x...u`) and `engine::schemaComponentIds()` (static array with `ecs::componentId<T>()` in schema order).
  - CLI: `node build/ts/generator/main.js --schema <schema.js> --out <dir>` — imports the module, uses the `export default` (a `Schema`), writes `<dir>/schema.generated.hpp` and `<dir>/schema_registry.generated.cpp`; exit 1 with a message if the default is not a `Schema`.
  - CMake `engine_add_game(<target> SCHEMA_JS <compiled schema.js> SOURCES <cpp...>)`: `add_custom_command` with the generator (OUTPUT the two files, DEPENDS the schema JS and `main.js`), executable with `engine_core`, include of the generated directory, link flags from the Decisions, `SUFFIX ".mjs"`. At configure time, if `build/ts/generator/main.js` does not exist: `FATAL_ERROR "run 'make engine-ts' before configuring with ENGINE_BUILD_FIXTURE=ON"`.
  - Fixture (`engine/fixture/schema.ts`, `export default defineSchema([...])` and named exports): `Position {x: f32, y: f32}`, `Velocity {x: f32, y: f32}`, `Mixed {a: u8, b: f64, c: i16, owner: entity}`, tags `Player`, `Enemy`.
  - Fixture (`engine/fixture/systems.cpp`, `extern "C"` + `EMSCRIPTEN_KEEPALIVE`): `void fixtureMove(EngineWorld* world, float deltaTime)` (`query<Position, const Velocity>().each`, `position += velocity * deltaTime`); `double fixtureSumPositionX(EngineWorld* world)`; `double fixtureMixedChecksum(EngineWorld* world, uint32_t index, uint32_t generation)` = `a + b + c + owner.index + owner.generation` of the entity's `Mixed`.
  - Target `fixture_game` → `build/<preset>/engine/fixture_game.mjs` + `.wasm`. ctest (only with the fixture): `engine_ts_unit` (`node --test build/ts/test/unit/`). `engine_ts_wasm` comes in Task 6, together with the first tests that use the module.
  - Presets `wasm-engine` and `wasm-engine-release` (configure/build/test of the same name; same toolchain as `wasm-debug`; see Decisions). `Makefile`: `engine-test: engine-ts` → `$(MAKE) test PRESET=wasm-engine`.

- [ ] **Step 1: Failing test** `engine/test/unit/generator.test.ts`:

```ts
const P = component("P", { a: "u8", b: "f64", owner: "entity" }); const T = tag("T"); const schema = defineSchema([P, T]);
test("struct in declaration order", () => assert.match(emitHeader(schema), /struct P \{\s*uint8_t a;\s*double b;\s*ecs::Entity owner;\s*\};/));
test("size and offset static_asserts", () => { const h = emitHeader(schema); assert.match(h, /sizeof\(P\) == 24 && alignof\(P\) == 8/); assert.match(h, /offsetof\(P, b\) == 8/); assert.match(h, /offsetof\(P, owner\) == 16/); });
test("tag becomes an empty struct", () => assert.match(emitHeader(schema), /struct T \{\};/));
test("registry with hash and ids in order", () => { const r = emitRegistry(schema); assert.ok(r.includes(`0x${schema.hash.toString(16)}u`)); assert.match(r, /ecs::componentId<P>\(\),\s*ecs::componentId<T>\(\)/); });
```

- [ ] **Step 2: Watch it fail** — `$WC sh -c 'make engine-ts && cd engine && npm run test:unit'` → `emit_cpp.js` does not exist.

- [ ] **Step 3: Implement** `emit_cpp.ts` and `main.ts`; watch Step 1 pass.

- [ ] **Step 4: CMake, presets, fixture.** Implement per Interfaces. If the link complains about a missing `main`, add `--no-entry` to the game flags.

- [ ] **Step 5: Check the fixture build**

Run: `$WC sh -c 'make engine-ts && cmake --preset wasm-engine && cmake --build --preset wasm-engine -j && ls build/wasm-engine/engine/ && node -e "import(\"/app/build/wasm-engine/engine/fixture_game.mjs\").then(m => m.default()).then(mod => console.log(typeof mod._engineSchemaHash, mod._engineSchemaHash().toString(16), mod.HEAPU8.length))"'`
Expected: `fixture_game.mjs` and `fixture_game.wasm` listed; `function <hash> <bytes>` with the hash equal to the fixture's `schemaHash` (check with `node -e` importing `build/ts/fixture/schema.js`). Also proves that `-Werror` accepts the generated code. Test by hand that a `static_assert` catches a divergence: temporarily edit an expected `offsetof` in the generated header and watch the build fail with the message "schema: layout of ... diverges from TS"; undo.

- [ ] **Step 6: Watch the ctest pass** — `$WC make engine-test` (ECS in WASM, `engine_c_api`, `engine_ts_unit`). And `$DC make test` stays green (the fixture is not in the native presets).

- [ ] **Step 7: Commit** `feat: C++ schema generator, engine_add_game and fixture game`.

---

### Task 6: TS bridge — `loadEngine` and `World` (spawn/destroy/get/set)

**Files:**
- Create: `engine/ts/module.ts`, `engine/ts/bridge.ts`, `engine/ts/world.ts`, `engine/ts/runtime.ts`
- Create: `engine/test/wasm/fixture.ts` (helper), `engine/test/wasm/load.test.ts`, `engine/test/wasm/world.test.ts`
- Modify: `engine/ts/index.ts`, `engine/CMakeLists.txt` (ctest `engine_ts_wasm`: `node --test build/ts/test/wasm/` with `ENVIRONMENT ENGINE_FIXTURE_MODULE=$<TARGET_FILE:fixture_game>`)

**Interfaces:**
- Consumes: C API (Task 3, exports with the `_` prefix); `Schema`, `ComponentType`, `ValueOf`, `packEntity`/`entityIndex`/`entityGeneration`, errors (Task 4); fixture module (Task 5).
- Produces:
  - `module.ts`: `interface EngineModule` (`HEAPU8: Uint8Array`, `_malloc`, `_free` and each export from Task 3 typed with `number` for pointers/handles); `type EngineModuleFactory = () => Promise<EngineModule>`.
  - `bridge.ts`: `class Heap { buffer: ArrayBuffer; u8; i8; u16; i16; u32; i32; f32; f64 }` (views over the whole buffer) with `refresh(buffer)`; `class Bridge` with `readonly heap: Heap`, `readonly module: EngineModule`, `allocate(bytes): number`, `free(pointer): void`, `sync(): void`, and one method per export (`create(world): Entity`, `destroy`, `isAlive`, `add(world, entity, componentId, valuePointer)`, `remove`, `has`, `get(...): number` (address or 0), `query(world, ids, counts...)`, `queryBegin(world, query): { blockCount: number; descriptors: number }`, `queryEnd`, `checkInvariants(world): string | null`). The methods that may allocate call `sync()` before returning. The `Bridge` keeps a scratch area (`_malloc` once) for outputs (`EngineEntity`, descriptor pointer), for a component's value (the schema's largest `layout.size`) and for ID lists (grows on demand).
  - `runtime.ts`: `loadEngine(factory: EngineModuleFactory, schema: Schema): Promise<EngineRuntime>` — checks `engineSchemaHash() === schema.hash`, `engineSchemaComponentCount() === schema.types.length` and, per type, size (`0` for a tag) and alignment; otherwise `SchemaMismatchError` with both hashes in hex and the first diverging type. `class EngineRuntime { readonly schema; readonly bridge: Bridge; readonly module: EngineModule; componentId(type: SchemaType): number; createWorld(): World }`.
  - `world.ts`: `class World` with `readonly handle: number` and:
    - `spawn<const Ts extends readonly SchemaType[]>(types: Ts, values?: SpawnValues<Ts>): Entity`, where `SpawnValues<Ts> = { [C in Extract<Ts[number], ComponentType> as C["name"]]?: Partial<ValueOf<C["fields"]>> }`; omitted fields = 0.
    - `destroy(entity)`, `isAlive(entity): boolean`, `has(entity, type): boolean`.
    - `add(entity, type, value?)` — full value (omitted = 0); on a component already present, assigns (ECS semantics). `remove(entity, type)`.
    - `get<F>(entity, type: ComponentType<string, F>): ValueOf<F>` (copy); `set<F>(entity, type, value: Partial<ValueOf<F>>)` (only the given fields, directly in the column).
    - `query(spec)` (Task 7), `checkInvariants(): string | null`, `dispose()`.
    - Validations before calling the C++: entity alive (`destroy`/`add`/`remove`/`get`/`set`), component present (`get`/`set`), type belongs to the runtime's schema, unknown key/field in `values`, `World` already released, structural change with an iteration in progress (the `World`'s `iterationDepth` counter, used by Task 7). Messages: `"<operation> e=<index:generation> type=<Name>: <reason>"`.
  - `engine/test/wasm/fixture.ts`: `loadFixture(): Promise<{ runtime: EngineRuntime; fixture: FixtureExports }>` reading `process.env.ENGINE_FIXTURE_MODULE` (clear error if absent), `import(pathToFileURL(...).href)`; `FixtureExports` = `fixtureMove(world, deltaTime)`, `fixtureSumPositionX(world)`, `fixtureMixedChecksum(world, index, generation)`.

- [ ] **Step 1: Failing tests** (`checkInvariants()` must be `null` at the end of each test):

```ts
// load.test.ts
test("loads with the fixture schema", async () => { const { runtime } = await loadFixture(); assert.ok(runtime.createWorld()); });
test("diverging schema is rejected", async () => {
  const OtherPosition = component("Position", { x: "f64", y: "f32" }); /* + the same other types, recreated */
  await assert.rejects(loadEngine(factory, defineSchema([...])), (error) => error instanceof SchemaMismatchError && /0x[0-9a-f]+/.test(error.message));
});

// world.test.ts
test("spawn with values and get", () => { const e = world.spawn([Player, Position, Velocity], { Position: { x: 1.5, y: -2 } }); assert.deepEqual(world.get(e, Position), { x: 1.5, y: -2 }); assert.deepEqual(world.get(e, Velocity), { x: 0, y: 0 }); assert.ok(world.has(e, Player)); });
test("partial set", () => { world.set(e, Position, { y: 7 }); assert.deepEqual(world.get(e, Position), { x: 1.5, y: 7 }); });
test("add of a tag, add that assigns, remove", ...);
test("destroy and slot reuse", () => { world.destroy(e); assert.equal(world.isAlive(e), false); const reused = world.spawn([Position]); assert.equal(entityIndex(reused), entityIndex(e)); assert.equal(entityGeneration(reused), entityGeneration(e) + 1); });
test("Mixed matches the C++", () => {
  const other = world.spawn([Player]); const e = world.spawn([Mixed], { Mixed: { a: 200, b: 1.5, c: -3, owner: other } });
  assert.deepEqual(world.get(e, Mixed), { a: 200, b: 1.5, c: -3, owner: other });
  assert.equal(fixture.fixtureMixedChecksum(world.handle, entityIndex(e), entityGeneration(e)), 200 + 1.5 - 3 + entityIndex(other) + entityGeneration(other));
});
test("value written by TS is seen by the C++", () => { /* 3 entities Position x = 1, 2, 3 → fixtureSumPositionX == 6 */ });
test("contract errors", () => {
  // get/set/destroy/add/remove of a dead entity → EngineContractError with "e=" and the operation name
  // get of an absent component → message with "type=Velocity"; spawn with values { Velocity: ... } without Velocity in types → error
  // after all of it: world.spawn([Position]) works and checkInvariants() === null (the module did not abort)
});
test("dispose", () => { world.dispose(); assert.throws(() => world.spawn([Position]), EngineContractError); });
```

- [ ] **Step 2: Watch it fail** — `$WC make engine-test T=engine_ts_wasm` → missing imports.

- [ ] **Step 3: Implement.** `spawn` = `create` + one `add` per type (value assembled in the scratch area from the layout; `entity` written as two `u32`). Field reads/writes through `Heap` (`f32[address >> 2]` etc.). Component → `ComponentId` via an array indexed by `schemaIndex`.

- [ ] **Step 4: Watch it pass** — `$WC make engine-test` → all green, on `wasm-engine` (asserts on: no `ECS_ASSERT` fired). And `$WC sh -c 'make engine-ts && make test PRESET=wasm-engine-release T=engine_ts'` (Release: the TS validations alone hold the errors).

- [ ] **Step 5: Commit** `feat: TS bridge with loadEngine and World (spawn, destroy, get, set)`.

---

### Task 7: Queries in TS — `eachBlock` and strided views

**Files:**
- Create: `engine/ts/fields.ts`, `engine/ts/query.ts`
- Modify: `engine/ts/world.ts` (`query`), `engine/ts/index.ts`
- Test: `engine/test/wasm/query.test.ts`

**Interfaces:**
- Consumes: `Bridge.query`/`queryBegin`/`queryEnd`/`allocate`/`free`, `Heap`, `World.iterationDepth` (Task 6); `ComponentLayout` (Task 4).
- Produces:
  - `fields.ts`: `interface RawField { readonly array: Float32Array | Float64Array | Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array; readonly base: number; readonly stride: number }` (element = `array[base + row * stride]`; for `entity`, `array` is `u32`, `base` points to the `index` and `base + 1` to the `generation`); `interface ReadField { get(row: number): number; raw(): RawField }`; `interface WriteField extends ReadField { set(row: number, value: number): void }`; `type ReadView<F extends Fields> = { readonly [K in keyof F]: ReadField }`; `type WriteView<F extends Fields> = { readonly [K in keyof F]: WriteField }`; `makeView(heap: Heap, layout: ComponentLayout, columnAddress: number): WriteView<Fields>`. One class per scalar type (`F32Field`, `F64Field`, `I8Field`, `U8Field`, `I16Field`, `U16Field`, `I32Field`, `U32Field`, `EntityField`) so that each `get`/`set` stays monomorphic; each one reads the array from the `Heap` on every access (follows memory growth). `raw()` returns the current array, valid until the next call that allocates in the WASM.
  - `query.ts`: `interface QuerySpec { read?: readonly ComponentType[]; write?: readonly ComponentType[]; with?: readonly SchemaType[]; without?: readonly SchemaType[] }`; `class Query { eachBlock(callback: (block: Block) => void): void }`; `class Block { readonly count: number; entity(row: number): Entity; read<F extends Fields>(type: ComponentType<string, F>): ReadView<F>; write<F extends Fields>(type: ComponentType<string, F>): WriteView<F> }`.
  - `World.query(spec: QuerySpec): Query` — memoized in TS by a canonical key (`schemaIndex` of read, write, with, without, preserving the read/write order); validates: tag in `read`/`write`, type repeated between `read` and `write`, type from another schema → `EngineContractError`.
  - `eachBlock` semantics: `queryBegin`, copy of all descriptors into local JS arrays (the nested one may overwrite the C++ buffer), `iterationDepth++`, one `Block` per descriptor; `finally`: invalidates the current block, `iterationDepth--`, `queryEnd`. `Block.read`/`write`/`entity` on an invalidated block → `EngineContractError("block used outside the eachBlock callback")`; `write` of a type only in `read` → error; type outside the query → error.

- [ ] **Step 1: Failing tests** in `query.test.ts` (each test ends with `checkInvariants() === null`):

```ts
test("movement in TS and in C++ give the same result", () => {
  // 1000 entities: Position {x: i, y: 0}, Velocity {x: 1, y: 2}; the even ones also with Enemy (2 archetypes)
  // TS: query({ read: [Velocity], write: [Position] }).eachBlock: position.x.set(r, position.x.get(r) + velocity.x.get(r) * 0.5), same for y
  // C++: fixture.fixtureMove(world.handle, 0.5)
  // for every i: world.get(e_i, Position) deepEqual { x: i + 1, y: 2 }; fixtureSumPositionX == Σ (i + 1)
});
test("blocks, counts and filters", () => { /* 2 blocks adding up to 1000; with [Enemy] → 500; without [Enemy] → 500 */ });
test("entity(row) identifies the row", () => { /* world.get(block.entity(r), Position).x === position.x.get(r) for every row */ });
test("query created before the archetypes sees them", ...);
test("emptied table produces no block", ...);
test("same spec returns the same Query", () => assert.equal(world.query(spec), world.query({ ...spec })));
test("f64, u8, i16 and entity fields through the view", () => { /* Mixed: read view get(r) of each field === world.get(e, Mixed) */ });
test("raw() reads the same as get()", () => { /* const { array, base, stride } = position.x.raw(); array[base + r * stride] === position.x.get(r) */ });
test("spec and access errors", () => {
  // query({ read: [Player] }) and query({ read: [Position], write: [Position] }) → EngineContractError
  // inside the callback: block.write(Velocity) with Velocity only in read → error; block.read(Mixed) outside the query → error
});
test("structural change inside eachBlock is an error and the World stays usable", () => {
  // spawn, destroy, add, remove inside the callback → EngineContractError (assert.throws inside the callback)
  // after the eachBlock: world.spawn([Position]) works
});
test("exception in the callback ends the iteration", () => {
  assert.throws(() => query.eachBlock(() => { throw new Error("game failure"); }), /game failure/);
  // world.spawn([Position]) works; checkInvariants() === null (I-11: not left iterating)
});
test("block used outside the callback", () => { let saved!: Block; query.eachBlock((b) => { saved = b; }); assert.throws(() => saved.read(Position), EngineContractError); });
test("nested eachBlock on the same query", () => {
  // 10 entities in one archetype: outer × inner visit 100 pairs; in each pair, position.x.get of both is the expected value
});
test("memory growth during eachBlock", () => {
  // inside the callback: const before = runtime.module.HEAPU8.buffer; const pointer = runtime.bridge.allocate(64 * 1024 * 1024);
  // assert.notEqual(runtime.module.HEAPU8.buffer, before); position.x.set(r, 42) for every row; runtime.bridge.free(pointer)
  // afterwards: fixtureSumPositionX == 42 * N
});
```

- [ ] **Step 2: Watch it fail** — `$WC make engine-test T=engine_ts_wasm` → `query` does not exist.

- [ ] **Step 3: Implement** `fields.ts`, `query.ts`, `World.query`. Base of a field = (column address + `offset`) / element size; stride = `layout.size` / element size (always integers, §4.2: the `Mixed` test covers `f64` and `u8`).

- [ ] **Step 4: Watch it pass** — `$WC make engine-test` and `$WC sh -c 'make test PRESET=wasm-engine-release T=engine_ts'`. This is the phase's "done when" criterion (§5): a TS test in node creates entities, iterates with views and matches the C++.

- [ ] **Step 5: Commit** `feat: TS queries with eachBlock and strided views over the WASM heap`.

---

### Task 8: Boundary benchmark, results and documentation

**Files:**
- Create: `engine/bench/boundary.ts`, `bench/results/engine/boundary_phase1.json`, `bench/results/engine/README.md`
- Modify: `bench/results/README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: `loadEngine`, `World`, `Query`, `RawField`, fixture (`fixtureMove`, `fixtureSumPositionX`) (Tasks 5–7).
- Produces: `node build/ts/bench/boundary.js --module <fixture_game.mjs> --out <json> [--sizes 1000,10000,100000,1000000] [--rounds 10]`.

- [ ] **Step 1: Implement the benchmark** (§7). Same run, four variants of `position += velocity * deltaTime` (`deltaTime = 1/60`) over N entities `Position {0, 0}`, `Velocity {1, 2}` in a single archetype:
  1. `cpp` — `fixtureMove(world, deltaTime)`;
  2. `ts-engine` — `eachBlock` + `get`/`set` (the normal usage);
  3. `ts-engine-raw` — `eachBlock` + `raw()` (separates the accessor cost from the stride cost);
  4. `ts-manual` — four own `Float32Array`s (`x`, `y`, `vx`, `vy`), no engine (baseline).

  Protocol: per N, a warm-up of 50 iterations per variant; then `rounds` interleaved rounds, with the variant order rotating every round; each measurement = `ceil(4_000_000 / N)` iterations timed with `performance.now()`, recorded in ns/entity. Median, minimum and maximum per variant; ratios `ts-engine/ts-manual`, `ts-engine/cpp`, `ts-engine-raw/ts-manual` per N, computed over the medians. At the end, check that each variant accumulated `x ≈ 1 · deltaTime · total iterations` (relative tolerance 1e-3) and abort with exit 1 if not — a wrong variant's number does not go into the result. JSON: `meta` (date, commit from `git rev-parse` if available via the `--commit` argument, `process.versions.node`, `process.versions.v8`, sizes, rounds, iterations per N), `results` (N, variant, ns/entity per round, median, min, max), `ratios`. On stdout, the table of ratios.

- [ ] **Step 2: Smoke test**

Run: `$WC sh -c 'make engine-ts && cmake --preset wasm-engine-release && cmake --build --preset wasm-engine-release -j && node build/ts/bench/boundary.js --module build/wasm-engine-release/engine/fixture_game.mjs --out /tmp/boundary.json --sizes 1000 --rounds 2'`
Expected: a table with one row (N = 1000), exit 0.

- [ ] **Step 3: Measure** with the four sizes and 10 rounds, `--commit $(git rev-parse --short HEAD)` and `--out bench/results/engine/boundary_phase1.json`.

- [ ] **Step 4: `bench/results/engine/README.md`:** machine and versions (Emscripten 3.1.74, node and V8 from the JSON), protocol, table per N with the working-set bytes (`N × (8 + 8 + 8)` B for the engine) and the classification against L1d 32 KiB / L2 512 KiB / L3 16 MiB (fits in cache or not), the three ratios with the spread between rounds, and a short reading of what the numbers say about the SoA-per-field decision (§4.2) — no numeric target in this phase (§7). A row in `bench/results/README.md` pointing to `engine/README.md`.

- [ ] **Step 5: `CLAUDE.md`:** an engine section with the commands (`WC`, `make engine-ts`, `make engine-test`, presets `wasm-engine`/`wasm-engine-release`, the benchmark command), the `engine/` layer above the `world`, `RawQuery`/`addRaw` in the ECS description (and that the template APIs do not delegate to them, with the why), and the bridge rules (views only inside the callback; every call that allocates goes through the `Bridge` and does `sync`).

- [ ] **Step 6: Final check**

Run: `$DC make test && $DC make test PRESET=release && $WC make test PRESET=wasm-debug && $WC make engine-test && $WC sh -c 'make test PRESET=wasm-engine-release'`
Expected: all green.

- [ ] **Step 7: Commit** `bench: engine WASM↔TS boundary benchmark (phase 1)`.
