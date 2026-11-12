# Archetype ECS v1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the `ecs` library (C++20, single-thread, archetype-based) with tests, verifiable invariants and benchmarks that prove the NF-P1…NF-P5 targets, closing the spec's M0…M5 milestones.

**Architecture:** Static library `ecs` in four layers (`type_info → entity → table → archetype → query / command_buffer → world`). Thin templates in headers delegate to non-template functions in `src/*.cpp` that take a `ComponentId`. The `World` is the sole owner of the records and the only one that calls `fillHole`. Tests with doctest (one executable per suite), benchmarks with Google Benchmark; everything builds and runs inside the repository's Docker container.

**Tech Stack:** C++20, GCC 14 (image `gcc:14`), CMake ≥ 3.25 with presets, doctest 2.4.11, Google Benchmark 1.8.3 (both via `FetchContent`), Python 3 (only to compare benchmark JSON), `perf` (M5).

**Spec:** `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md` (copy of `~/Downloads/spec-ecs-archetype.md`). The `R-*`, `I-*` and `NF-*` IDs cited below are from that spec.

## Global Constraints

- C++20 (`std::span`, concepts, `std::bit_cast`); target compiler GCC 13+ (we use GCC 14); Clang 17+ desirable; MSVC does not block.
- Namespace `ecs`; headers in `include/ecs/`; types `PascalCase`, functions `camelCase`.
- A single `World` per thread, no concurrent access.
- Components: `static_assert(std::is_nothrow_move_constructible_v<T>)` and `static_assert(alignof(T) <= 64)` (R-TI-3).
- No exceptions on the hot path. Contract violation → `ECS_ASSERT` in debug, UB in release. `ECS_ENABLE_ASSERTS=0` removes the asserts (NF-S4).
- An `ECS_ASSERT` message includes the operation, the `Entity` (as `index:generation`) and the type name when there is one (NF-S2).
- The library has no external dependencies; doctest and Google Benchmark only in `tests/` and `bench/`, via `FetchContent` (NF-B3).
- CMake 3.25+, presets `debug-asan`, `release`, `relwithdebinfo` (NF-B1). Our targets build with `-Wall -Wextra -Wpedantic -Werror` (NF-B2); third-party dependencies do not get these flags.
- All test code runs clean under ASan + UBSan in the `debug-asan` preset (NF-S1).
- Per-entity overhead: 16 bytes of `EntityRecord` + 8 bytes of `Entity` in the table; no per-entity allocation (NF-M1). `static_assert(sizeof(EntityRecord) == 16)`.
- Columns aligned to `max(info->align, 64)` (R-TB-3 / NF-M3). Growth ×2 with a minimum of 64 rows (R-TB-2).
- Performance targets are ratios against a baseline measured on the same machine, median of ≥ 10 repetitions, results in JSON (`--benchmark_out`) in `bench/results/`.
- Commits end with the line `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Work on the `feat/ecs-v1` branch (not on `master`).

### How to run commands

Every build/test command runs in the container. Shorthand used in the plan:

```bash
DC="docker compose run --rm dev"
$DC make test PRESET=debug-asan T=unit_table   # configures, builds and runs ctest -R unit_table
```

A specific doctest case: `$DC ./build/debug-asan/tests/unit_table -tc="<case name>"`.

### Decisions the spec leaves open (fixed here)

- **Question 1 (`create<Ts...>`)**: accepts forwarding; lvalues are copied (`std::remove_cvref_t<Ts>(std::forward<Ts>(c))`). `static_assert` of distinct types (NF-S3).
- **Question 2 (query identity)**: by type. Filters are type parameters at the call: `world.query<Ts...>(ecs::With<Us...>{}, ecs::Without<Xs...>{})`. The spec shows `Query& without(ComponentId...)` as a mutating method; that would break the cache (two calls with different filters would return the same instance), so the filters go into the cache key. `Query` has no `with`/`without` methods.
- **Question 3 (direct destroy with pending commands)**: the flush detects the stale handle by its generation and ignores that entity's commands (cheapest option).
- **Question 4 (`get<Tag>`)**: compile error.
- **`add<T>` over an existing `T`** also requires `iterating_ == 0` (assert), even though it is not structural; R-API-3 forbids it and the death test "add during each" depends on it.
- **`CommandBuffer` buffer**: not a `std::vector<std::byte>`. Growing a `vector<byte>` copies the bytes of non-trivial payloads (e.g. `std::string` with SSO points to itself) — UB. The buffer is its own 64-aligned block that, when growing, walks the records and relocates each payload with `TypeInfo` (memcpy if trivial, `moveConstruct`+`destroy` otherwise). The record layout is the spec's.
- **`Archetype::mask`** (optional in the spec, R-AR-4): not implemented in v1; matching uses `std::includes`.
- **`Table::shrinkToFit`** (optional, R-TB-6): not implemented.
- **Records and freeList** live in an `EntityStore` class in `entity.hpp`, so that `CommandBuffer` (a layer below the `World`) can reserve indices (R-CB-1) without knowing the `World`.
- **"No memcpy" benchmark**: instead of a flag in the library, uses a `NonTrivial<T>` wrapper with a user-defined move constructor and destructor (same bytes, non-trivial path).
- **Death test**: doctest has no death tests. `contract_death` is its own executable: it installs an assert handler that prints and calls `std::_Exit(0)`; if the case ends without an assert, it returns 1. Each case is a separate CTest test (`contract_death <case>`). Only built with asserts on.

## Review Focus

1. **Non-trivial payload surviving `CommandBuffer` growth** — record an `add<std::string>` with a long string and then hundreds of commands that force reallocation; after the flush the string has its original value and ASan does not complain. Test in Task 15.
2. **`add` with a value that points into the `World` itself** — `w.add<B>(e, w.get<B>(other))` where `other` lives in the destination archetype and the destination table reallocates in `pushUninit`. Expected: `e` gets `other`'s value, no use-after-free. Spec mitigation: build the temporary before the structural change. Test in Task 7.
3. **Removing the last row / the moved entity is the entity itself** — `destroy`/`remove` on the entity in the last row (including the only one in the archetype): `fillHole` returns `nullopt`, no record is wrongly touched. Test in Task 7.
4. **Stale handles** — old handle after the index is recycled: `isAlive` false, `tryGet` returns `nullptr`, buffer commands for it are ignored in the flush; a slot with generation `UINT32_MAX` is never reused. Tests in Tasks 4 and 16.
5. **Exception thrown by the `each` lambda** — `iterating_` goes back to 0 (RAII guard) and following structural changes work (I-11). Test in Task 13.

---

## File map

```
CMakeLists.txt  CMakePresets.json  Makefile  Dockerfile  docker-compose.yaml
include/ecs/
  detail/assert.hpp      ECS_ASSERT, setAssertHandler
  detail/type_name.hpp   typeName<T>() via __PRETTY_FUNCTION__
  detail/small_map.hpp   SmallMap<K,V> (sorted vector)
  detail/align.hpp       alignUp, alignedAlloc/alignedFree
  type_info.hpp          ComponentId, TypeInfo, componentId<T>(), typeInfo()
  entity.hpp             Entity, NullEntity, EntityRecord, EntityStore
  table.hpp              Column, Table, relocateValue, destroyValue
  archetype.hpp          ArchetypeId, NoColumn, Signature, SigHash, Edge, Archetype, buildEdge
  query.hpp              With, Without, QueryCacheBase, MatchedTable, Query<Ts...>
  command_buffer.hpp     CommandBuffer, CommandOp, NetComponentOp, EntityPlan
  world.hpp              World, WorldStats, ArchetypeStats
src/ assert.cpp type_info.cpp table.cpp archetype.cpp world.cpp command_buffer.cpp
tests/
  CMakeLists.txt
  support/ doctest_main.cpp components.hpp (LifeCounter and test types) alloc_counter.cpp model.hpp
  unit_type_info.cpp unit_entity.cpp unit_table.cpp unit_archetype.cpp unit_world.cpp
  invariants.cpp fuzz_vs_model.cpp nontrivial.cpp queries.cpp command_buffer.cpp contract_death.cpp
bench/ CMakeLists.txt common.hpp iterate.cpp transitions.cpp fragmentation.cpp flush.cpp results/
tools/ bench_ratio.py perf_fragmented.sh
```

`src/main.cpp` is removed (the library has no executable).

---

## M0 · Infrastructure

### Task 1: Build, presets, container and `ECS_ASSERT`

**Files:**

- Modify: `CMakeLists.txt` (rewrite), `Dockerfile`, `docker-compose.yaml`, `Makefile`, `.gitignore`
- Delete: `src/main.cpp`
- Create: `CMakePresets.json`, `include/ecs/detail/assert.hpp`, `src/assert.cpp`, `tests/CMakeLists.txt`, `tests/support/doctest_main.cpp`, `tests/unit_table.cpp`, `bench/CMakeLists.txt`

**Interfaces:**

- Produces:
    - CMake target `ecs` (STATIC, `target_include_directories(ecs PUBLIC include)`, `cxx_std_20`).
    - CMake function `ecs_add_test(<name> <sources...>)`: creates executable `<name>` in `build/<preset>/tests/`, links `ecs`, `doctest::doctest` and `tests/support/doctest_main.cpp`, registers `add_test(NAME <name> ...)`.
    - CMake function `ecs_add_bench(<name> <sources...>)`: executable in `build/<preset>/bench/`, links `ecs` and `benchmark::benchmark`.
    - Options: `ECS_BUILD_TESTS`, `ECS_BUILD_BENCH`, `ECS_SANITIZE` (adds `-fsanitize=address,undefined -fno-omit-frame-pointer` to compile and link), `ECS_WERROR`, `ECS_ENABLE_ASSERTS` (string; empty = `0` if `NDEBUG`, otherwise `1`; passed as `target_compile_definitions(ecs PUBLIC ECS_ENABLE_ASSERTS=...)`).
    - `ECS_ASSERT(cond, fmt, ...)` (printf-style; expands to nothing when `ECS_ENABLE_ASSERTS == 0`, without evaluating `cond`).
    - `namespace ecs { using AssertHandler = void(*)(const char* message) noexcept; AssertHandler setAssertHandler(AssertHandler) noexcept; }` — returns the previous one. Default handler: writes the message to `stderr` and calls `std::abort()`.
    - `[[noreturn]] void ecs::detail::assertFail(const char* expr, const char* file, int line, const char* fmt, ...) noexcept` — formats `"ECS_ASSERT failed: <expr> | <msg> (<file>:<line>)"` into a 512-byte buffer and calls the handler; if the handler returns, `std::abort()`.
    - Makefile: `PRESET ?= debug-asan`, `T ?= .`; targets `configure` (`cmake --preset $(PRESET)`), `build`, `test` (`ctest --preset $(PRESET) -R '$(T)' --output-on-failure`), `bench` (forces `PRESET=release`).

- [ ] **Step 1: Create the branch**

```bash
git checkout -b feat/ecs-v1
```

- [ ] **Step 2: Container**

`Dockerfile`: `FROM gcc:14`; install `cmake git python3 linux-perf` (`--no-install-recommends`). `docker-compose.yaml`: keep `build`/`run` and add a `dev` service (same volumes, `command: bash`) and a `perf` service equal to `dev` with `privileged: true`.

Run: `docker compose build dev && $DC cmake --version`
Expected: `cmake version 3.25` or higher. If lower, install Kitware's CMake in the Dockerfile before moving on.

- [ ] **Step 3: Write the smoke test**

```cpp
// tests/unit_table.cpp
#include <doctest/doctest.h>

#include "ecs/detail/assert.hpp"

TEST_CASE("smoke: asserts on in debug") {
#ifdef NDEBUG
    CHECK(ECS_ENABLE_ASSERTS == 0);
#else
    CHECK(ECS_ENABLE_ASSERTS == 1);
#endif
    ECS_ASSERT(1 + 1 == 2, "op=%s", "smoke");
}
```

- [ ] **Step 4: Run and watch it fail**

Run: `$DC make test PRESET=debug-asan T=unit_table`
Expected: FAIL at configuration (preset or target missing).

- [ ] **Step 5: Implement CMake, presets, Makefile, `assert.hpp`/`assert.cpp`**

Presets (all with `binaryDir: build/${presetName}`, `CMAKE_CXX_STANDARD 20`, `ECS_WERROR ON`, and a `testPreset` of the same name with `output.outputOnFailure`):

| preset           | `CMAKE_BUILD_TYPE` | `ECS_SANITIZE` | `ECS_BUILD_TESTS` | `ECS_BUILD_BENCH` | `ECS_ENABLE_ASSERTS` |
| ---------------- | ------------------ | -------------- | ----------------- | ----------------- | -------------------- |
| `debug-asan`     | Debug              | ON             | ON                | OFF               | (default → 1)        |
| `release`        | Release            | OFF            | ON                | ON                | 0                    |
| `relwithdebinfo` | RelWithDebInfo     | OFF            | OFF               | ON                | 0                    |

doctest via `FetchContent` (tag `v2.4.11`), Google Benchmark (tag `v1.8.3`, with `BENCHMARK_ENABLE_TESTING OFF`, `BENCHMARK_ENABLE_GTEST_TESTS OFF`). `.gitignore` keeps `build`.

- [ ] **Step 6: Run and watch it pass**

Run: `$DC make test PRESET=debug-asan T=unit_table && $DC make test PRESET=release T=unit_table`
Expected: `100% tests passed` on both.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "build: CMake presets, dev container and ECS_ASSERT"
```

---

### Task 2: SoA baseline and benchmark comparator

**Files:**

- Create: `bench/common.hpp`, `bench/iterate.cpp`, `tools/bench_ratio.py`, `bench/results/m0_iterate_single_soa.json`
- Modify: `bench/CMakeLists.txt`

**Interfaces:**

- Produces:
    - `bench/common.hpp`: `struct float3 { float x, y, z; };` `struct Position { float3 v; };` `struct Velocity { float3 v; };` `inline constexpr float kDt = 1.0f / 60.0f;`
    - Benchmark `BM_IterateSingle_SoA` with `Arg` in {1000, 10000, 100000, 1000000}, `state.counters["per_entity"]` = `benchmark::Counter(n, kIsIterationInvariantRate | kInvert)` (time per entity). Executable `bench_iterate`.
    - `tools/bench_ratio.py --json <file> --name <benchmark> --baseline <benchmark> --max <ratio>`: reads the `<name>_median` aggregate (`real_time`) of each, prints `ratio=<x>` and exits with 0 if `ratio <= max`, 1 otherwise, 2 if a name does not exist. Always run with `python3 -I`.

- [ ] **Step 1: Write the SoA benchmark** — two `std::vector<float3>` of size `n`, loop `pos[i] += vel[i] * kDt` component by component, `benchmark::DoNotOptimize(pos.data())`.

- [ ] **Step 2: Run and save JSON**

Run: `$DC make build PRESET=release && $DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m0_iterate_single_soa.json`
Expected: 4 `_median` lines for `BM_IterateSingle_SoA/<n>`.

- [ ] **Step 3: Check the comparator against its own JSON**

Run: `$DC python3 -I tools/bench_ratio.py --json bench/results/m0_iterate_single_soa.json --name BM_IterateSingle_SoA/1000000 --baseline BM_IterateSingle_SoA/1000000 --max 1.0; echo $?`
Expected: `ratio=1.000`, exit 0. With `--max 0.5`: exit 1. With `--name nonexistent`: exit 2.

- [ ] **Step 4: Commit** — `git add bench tools && git commit -m "bench: iterate_single SoA baseline (M0)"`

**Gate M0:** presets, FetchContent and SoA baseline in JSON — closed by Tasks 1–2.

---

## M1 · Core

### Task 3: `type_info`

**Files:**

- Create: `include/ecs/type_info.hpp`, `include/ecs/detail/type_name.hpp`, `src/type_info.cpp`, `tests/unit_type_info.cpp`, `tests/support/components.hpp`
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt` (`ecs` sources)

**Interfaces:**

- Produces:
    - `using ComponentId = uint32_t;` `struct TypeInfo` exactly as in section 4.1 of the spec.
    - `template<class T> ComponentId componentId();` — `using U = std::remove_cvref_t<T>`; R-TI-3 `static_assert`s; `static const ComponentId id = detail::registerType(detail::makeTypeInfo<U>()); return id;` (R-TI-4). For tags (`std::is_empty_v<U>`): `size = 0`, `tag = true`, `trivial = true`.
    - `const TypeInfo& typeInfo(ComponentId);` `uint32_t registeredTypeCount() noexcept;`
    - `ComponentId detail::registerType(const TypeInfo&);` — registry in a `std::deque<TypeInfo>` inside a function-local `static` (stable references).
    - `template<class T> constexpr std::string_view detail::typeName();` — extracted from `__PRETTY_FUNCTION__` (GCC/Clang) / `__FUNCSIG__` (MSVC), e.g. `"Position"`.
    - `tests/support/components.hpp`: `struct A { uint64_t v; }; struct B { uint64_t v, w; }; struct Tag {}; struct alignas(64) Wide { float f[16]; };` and `struct LifeCounter { static inline int64_t alive = 0; uint64_t v = 0; /* ++ in every ctor, -- in the dtor, move noexcept */ };`

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("componentId is stable, dense and ignores cvref") {
    auto a = ecs::componentId<A>();
    CHECK(ecs::componentId<A>() == a);
    CHECK(ecs::componentId<const A&>() == a);
    auto b = ecs::componentId<B>();
    CHECK(b != a);
    CHECK(std::max(a, b) < ecs::registeredTypeCount());
}
TEST_CASE("TypeInfo of trivial, non-trivial and tag") {
    auto& ia = ecs::typeInfo(ecs::componentId<A>());
    CHECK(ia.size == sizeof(A)); CHECK(ia.trivial); CHECK_FALSE(ia.tag);
    CHECK(ia.moveConstruct == nullptr); CHECK(ia.destroy == nullptr);
    auto& is = ecs::typeInfo(ecs::componentId<std::string>());
    CHECK_FALSE(is.trivial); CHECK(is.moveConstruct != nullptr); CHECK(is.destroy != nullptr);
    auto& it = ecs::typeInfo(ecs::componentId<Tag>());
    CHECK(it.tag); CHECK(it.size == 0);
    CHECK(ecs::typeInfo(ecs::componentId<Wide>()).align == 64);
    CHECK(ia.name == "A");
}
```

- [ ] **Step 2: Run and watch it fail** — `$DC make test T=unit_type_info` → FAIL (header missing).
- [ ] **Step 3: Implement** per Interfaces.
- [ ] **Step 4: Run and watch it pass** — `$DC make test T=unit_type_info` → PASS.
- [ ] **Step 5: Commit** — `feat(type_info): componentId<T> and TypeInfo`

---

### Task 4: `entity` and `EntityStore`

**Files:**

- Create: `include/ecs/entity.hpp`, `tests/unit_entity.cpp`

**Interfaces:**

- Consumes: nothing beyond `<cstdint>`; `struct Archetype;` forward-declared.
- Produces:
    - `Entity`, `NullEntity`, `EntityRecord` exactly as in section 4.2; `static_assert(sizeof(EntityRecord) == 16)`; `struct EntityHash` (for `unordered_map<Entity, …>` in the tests).
    - `class EntityStore` (header-only):
        - `Entity allocate();` — takes from the end of the `freeList` (LIFO) or creates a new slot with generation 0; leaves `arch = nullptr` (reserved state, R-EN-3); returns `{index, generation}`.
        - `void release(uint32_t index) noexcept;` — `arch = nullptr`, `++generation`; if `generation == UINT32_MAX` the slot is retired (does not go back to the `freeList`), otherwise `freeList.push_back(index)` (R-EN-1).
        - `bool isAlive(Entity) const noexcept;` — formula from R-EN-2.
        - `bool isReserved(Entity) const noexcept;` — valid index, equal generation and `arch == nullptr`.
        - `EntityRecord& record(uint32_t index) noexcept; const EntityRecord& record(uint32_t) const noexcept;`
        - `uint32_t slotCount() const noexcept; std::span<const uint32_t> freeList() const noexcept;`
        - `void setGenerationForTest(uint32_t index, uint32_t gen) noexcept;`

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("recycling increments the generation and invalidates the old handle") {
    ecs::EntityStore s; ecs::Archetype* fake = reinterpret_cast<ecs::Archetype*>(0x10);
    auto e = s.allocate();
    CHECK(s.isReserved(e)); CHECK_FALSE(s.isAlive(e));
    s.record(e.index).arch = fake;
    CHECK(s.isAlive(e));
    s.release(e.index);
    CHECK_FALSE(s.isAlive(e));
    auto e2 = s.allocate();
    CHECK(e2.index == e.index); CHECK(e2.generation == e.generation + 1);
    s.record(e2.index).arch = fake;
    CHECK_FALSE(s.isAlive(e)); CHECK(s.isAlive(e2));
}
TEST_CASE("slot with generation UINT32_MAX is retired") {
    ecs::EntityStore s;
    auto e = s.allocate();
    s.setGenerationForTest(e.index, UINT32_MAX - 1);
    s.release(e.index);
    CHECK(s.freeList().empty());
    CHECK(s.allocate().index != e.index);
}
TEST_CASE("NullEntity is never alive") { ecs::EntityStore s; CHECK_FALSE(s.isAlive(ecs::NullEntity)); }
```

- [ ] **Step 2: Watch it fail** — `$DC make test T=unit_entity` → FAIL.
- [ ] **Step 3: Implement** per Interfaces.
- [ ] **Step 4: Watch it pass** — PASS.
- [ ] **Step 5: Commit** — `feat(entity): Entity, EntityRecord and generational EntityStore`

---

### Task 5: `Table`

**Files:**

- Create: `include/ecs/table.hpp`, `include/ecs/detail/align.hpp`, `src/table.cpp`
- Modify: `tests/unit_table.cpp`

**Interfaces:**

- Consumes: `TypeInfo`, `typeInfo()`, `Entity`.
- Produces:
    - `Column` and `Table` with the exact public API of section 4.3, plus `uint32_t capacity() const noexcept;` and `std::span<const ComponentId> columnTypes() const noexcept;`.
    - `void relocateValue(const TypeInfo&, void* dst, void* src) noexcept;` — `memcpy` if `trivial`, otherwise `moveConstruct` + `destroy`.
    - `void destroyValue(const TypeInfo&, void* p) noexcept;` — no-op if `trivial`.
    - `detail/align.hpp`: `constexpr size_t alignUp(size_t v, size_t a) noexcept;` `std::byte* alignedAlloc(size_t bytes, size_t align);` `void alignedFree(std::byte*, size_t align) noexcept;` (via `::operator new(bytes, std::align_val_t)`).
- Pinned rules: growth `newCap = max(64, capacity*2)`; `reserve` also does `entities_.reserve(newCap)` (required for NF-P4); trivial = one `memcpy` per column, non-trivial = `moveConstruct`+`destroy` per element (R-TB-2); `fillHole` per R-TB-5 (uses `relocateValue` from the last row to `r`, `entities_[r] = entities_.back()`, `pop_back`, returns the moved entity or `nullopt` if `r` was the last); the constructor does an `ECS_ASSERT` that no type is a tag.

- [ ] **Step 1: Failing tests** (replace the smoke test; each step checks I-1 and I-2)

```cpp
static void construct(ecs::Table& t, uint32_t row, uint64_t v);  // placement-new A{v} in col 0 and LifeCounter{v} in col 1
static void checkI1I2(const ecs::Table& t) {
    CHECK(t.entities().size() == t.count());
    CHECK(t.capacity() >= t.count());
    CHECK(LifeCounter::alive == t.count());
}
TEST_CASE("push, destroyRow, fillHole with swap and reallocation") {
    LifeCounter::alive = 0;
    {
        ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<LifeCounter>()};
        std::sort(std::begin(ids), std::end(ids));
        ecs::Table t(ids);
        for (uint32_t i = 0; i < 200; ++i) { auto r = t.pushUninit({i, 0}); construct(t, r, i); checkI1I2(t); }
        CHECK(t.capacity() == 256);                       // 64 → 128 → 256
        t.destroyRow(10);
        auto moved = t.fillHole(10);
        REQUIRE(moved); CHECK(moved->index == 199);
        CHECK(t.entities()[10].index == 199);
        CHECK(valueA(t, 10) == 199);                      // the value followed the entity
        checkI1I2(t);
        t.destroyRow(t.count() - 1);
        CHECK_FALSE(t.fillHole(t.count() - 1));           // last row: nothing moved
        checkI1I2(t);
    }
    CHECK(LifeCounter::alive == 0);                       // the destructor destroys live rows
}
TEST_CASE("columns aligned to 64, including alignas(64)") {
    ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<Wide>()};
    std::sort(std::begin(ids), std::end(ids));
    ecs::Table t(ids); t.pushUninit({0, 0});
    for (uint16_t c = 0; c < t.columnCount(); ++c)
        CHECK(reinterpret_cast<uintptr_t>(t.column(c).data) % 64 == 0);
}
TEST_CASE("std::string survives reallocation") { /* 100 long strings (>32 chars), check all of them after growing */ }
```

(Order the target column by the index returned by `std::lower_bound` on `columnTypes()`; the `construct`/`valueA` helpers do that.)

- [ ] **Step 2: Watch it fail** — `$DC make test T=unit_table` → FAIL.
- [ ] **Step 3: Implement** `table.hpp`/`table.cpp`/`align.hpp`.
- [ ] **Step 4: Watch it pass** — PASS, no ASan report.
- [ ] **Step 5: Commit** — `feat(table): SoA table with joint growth and fillHole`

---

### Task 6: `Archetype`, `Signature`, `SmallMap` and `buildEdge`

**Files:**

- Create: `include/ecs/archetype.hpp`, `include/ecs/detail/small_map.hpp`, `src/archetype.cpp`, `tests/unit_archetype.cpp`

**Interfaces:**

- Consumes: `Table`, `TypeInfo`.
- Produces:
    - `using ArchetypeId = uint32_t; using Signature = std::vector<ComponentId>; inline constexpr uint16_t NoColumn = UINT16_MAX;`
    - `struct SigHash { size_t operator()(const Signature&) const noexcept; };` — FNV-1a 64 over the IDs.
    - `template<class K, class V> class SmallMap` — sorted vector of `std::pair<K,V>`: `V* find(K) noexcept`, `const V* find(K) const noexcept`, `V& insert(K, V)` (`ECS_ASSERT` that the key is absent; may invalidate references), `size()`, `begin()/end()`, `capacityBytes()`.
    - `Edge` as in section 4.4. `Archetype` as in section 4.4 **without** `mask`, with constructor `Archetype(ArchetypeId, Signature)` that derives `columnTypes` (non-tags) and builds `table(columnTypes)`.
    - `Edge buildEdge(const Archetype& src, Archetype& dst, ComponentId added);` — O(k) merge of the two `columnTypes` (R-AR-2); `addedColumn` = column of `added` in `dst` if `added` has data and is not in `src`, otherwise `-1`. For removal or an arbitrary jump, pass `added = NoComponent` where `inline constexpr ComponentId NoComponent = UINT32_MAX;`.
    - `uint16_t columnOf(const Archetype&, ComponentId) noexcept;` — `NoColumn` if absent or a tag (binary search in `columnTypes`).

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("columnTypes excludes tags; sorted signature includes them") {
    ecs::Signature sig{ecs::componentId<A>(), ecs::componentId<Tag>(), ecs::componentId<B>()};
    std::sort(sig.begin(), sig.end());
    ecs::Archetype a(1, sig);
    CHECK(a.signature == sig);
    CHECK(a.columnTypes.size() == 2);
    CHECK(a.table.columnCount() == 2);
    CHECK(ecs::columnOf(a, ecs::componentId<Tag>()) == ecs::NoColumn);
}
TEST_CASE("buildEdge maps columns by merge") {
    // src = {A, B}, dst = {A, B, C(data)}: srcToDst == {0,1} in columnTypes order, addedColumn == column of C
    // src = {A, B}, dst = {B}: srcToDst[col of A] == -1
}
TEST_CASE("SmallMap keeps order and finds") { /* insert 5,1,3; begin()->first == 1; find(3) != nullptr; find(2) == nullptr */ }
TEST_CASE("SigHash differs for different signatures") { CHECK(ecs::SigHash{}({1,2}) != ecs::SigHash{}({2,1,0})); }
```

(In the `buildEdge` test, declare a local `struct C { uint32_t v; };` and write the expected values from `columnOf`.)

- [ ] **Step 2: Watch it fail** — `$DC make test T=unit_archetype` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Watch it pass.**
- [ ] **Step 5: Commit** — `feat(archetype): Archetype, SmallMap and buildEdge by merge`

---

### Task 7: Minimal `World` (no cached edges)

**Files:**

- Create: `include/ecs/world.hpp`, `src/world.cpp`, `tests/unit_world.cpp`

**Interfaces:**

- Consumes: Tasks 3–6.
- Produces (public API from section 5, except `query`, `commands`, `flush`, `stats`, `checkInvariants`, which come later):
    - `World(); ~World();` non-copyable. The constructor creates the root archetype `id == 0` (R-AR-3).
    - `Entity create(); template<class... Ts> Entity create(Ts&&...); void destroy(Entity); bool isAlive(Entity) const noexcept;`
    - `template<class T> void add(Entity, T&&); template<class T> void remove(Entity); template<class T> bool has(Entity) const noexcept; template<class T> T& get(Entity); template<class T> T* tryGet(Entity) noexcept;`
    - Non-template core (public, used by `flushSequential` and tests): `void addRaw(Entity, ComponentId, void* value);` (moves from `value`; if it already has the component: `destroyValue` + `moveConstruct`/`memcpy` in place; for a tag `value` is ignored), `void removeRaw(Entity, ComponentId);`, `void* getRaw(Entity, ComponentId) noexcept;` (`nullptr` if absent/dead), `bool hasRaw(Entity, ComponentId) const noexcept;`
    - Private members with fixed names (used by Tasks 8, 10, 16, 19): `Archetype* findOrCreate(Signature sig);` (lookup in `bySignature_`; if it creates: `archetypes_.push_back`, inserts into `bySignature_`, updates `componentIndex_[cid].insert(id, columnOf(...))` for every `cid` in the signature — R-WI-1 — and, from Task 12 on, notifies `queries_`), `uint32_t moveEntity(Entity, EntityRecord&, Archetype& dst, const Edge&);` and the members from 4.5 with `records_`/`freeList_` replaced by `EntityStore store_;`.
    - `moveEntity` (pinned): `dstRow = dst.table.pushUninit(e)`; for each source column `c`: if `srcToDst[c] >= 0` → `relocateValue` to `dst.column(srcToDst[c]).at(dstRow)`, otherwise `destroyValue`; `src.table.fillHole(srcRow)` and, if it returns a moved entity `d`, `store_.record(d.index).row = srcRow`; updates `rec = {&dst, dstRow, rec.generation}`; returns `dstRow`. The `edge.addedColumn` column is left for the caller to construct.
    - `add<T>` (pinned): `using U = remove_cvref_t<T>`; `ECS_ASSERT(isAlive(e) && iterating_ == 0, "add e=%u:%u type=%s", …)`; for a non-tag, build `U tmp(std::forward<T>(value))` **before** any structural change and call `addRaw(e, cid, &tmp)`.
    - `get<T>`: `static_assert(!std::is_empty_v<U>)` (question 4); cost per R-WI-2; `ECS_ASSERT(isAlive(e) && has, "get e=%u:%u type=%s", …)`.
    - `create<Ts...>`: `static_assert` of distinct types; sorted signature; `findOrCreate` once; `pushUninit`; placement-new of each `T` into its column (born directly in the final archetype).
    - `destroy`: `destroyRow` + `fillHole` + update of the moved entity + `store_.release`.
    - In M1, transitions use `buildEdge(src, *findOrCreate(sig ± cid), cid)` on every call (no cache).

- [ ] **Step 1: Failing tests** — one case per row of the table in section 5, plus items 2 and 3 of the Review Focus:

```cpp
TEST_CASE("create() is born alive in the root archetype") { ecs::World w; auto e = w.create(); CHECK(w.isAlive(e)); CHECK_FALSE(w.has<A>(e)); }
TEST_CASE("create<Ts...> is born in the final archetype with values") {
    ecs::World w; auto e = w.create(A{1}, B{2, 3}, Tag{});
    CHECK(w.get<A>(e).v == 1); CHECK(w.get<B>(e).w == 3); CHECK(w.has<Tag>(e));
}
TEST_CASE("add over an existing T assigns without changing archetype") {
    ecs::World w; auto e = w.create(A{1}); w.add(e, A{7}); CHECK(w.get<A>(e).v == 7);
}
TEST_CASE("remove of an absent T is a no-op") { ecs::World w; auto e = w.create(A{1}); w.remove<B>(e); CHECK(w.get<A>(e).v == 1); }
TEST_CASE("destroy updates the record of the moved entity") {
    ecs::World w; auto e0 = w.create(A{0}); auto e1 = w.create(A{1}); auto e2 = w.create(A{2});
    w.destroy(e0);
    CHECK_FALSE(w.isAlive(e0)); CHECK(w.get<A>(e2).v == 2); CHECK(w.get<A>(e1).v == 1);
    CHECK(w.tryGet<A>(e0) == nullptr);
}
TEST_CASE("remove/destroy of the last and of the only row") {   // Review Focus 3
    ecs::World w; auto a = w.create(A{1}); auto b = w.create(A{2});
    w.remove<A>(b); CHECK(w.get<A>(a).v == 1); CHECK_FALSE(w.has<A>(b));
    w.destroy(a);   CHECK(w.isAlive(b));
}
TEST_CASE("add with a value read from another entity of the destination table that reallocates") {   // Review Focus 2
    ecs::World w;
    std::vector<ecs::Entity> full; for (uint64_t i = 0; i < 64; ++i) full.push_back(w.create(A{i}, B{i, i}));  // capacity 64 full
    auto e = w.create(A{999});
    w.add(e, w.get<B>(full[5]));                  // destination {A,B} reallocates in pushUninit
    CHECK(w.get<B>(e).v == 5); CHECK(w.get<B>(full[5]).w == 5);
}
TEST_CASE("tryGet on a dead entity and without T returns nullptr") { /* … */ }
TEST_CASE("tags: add/has/remove without a column") { /* add(e, Tag{}) → has; remove<Tag> → !has; data columns intact */ }
```

- [ ] **Step 2: Watch it fail** — `$DC make test T=unit_world` → FAIL.
- [ ] **Step 3: Implement** `world.hpp`/`world.cpp` per Interfaces.
- [ ] **Step 4: Watch it pass** — PASS under ASan/UBSan.
- [ ] **Step 5: Commit** — `feat(world): create/destroy/add/remove/get with findOrCreate`

---

### Task 8: `checkInvariants()` (I-1…I-7, I-10) and the `invariants` suite

**Files:**

- Modify: `include/ecs/world.hpp`, `src/world.cpp`
- Create: `tests/invariants.cpp`

**Interfaces:**

- Produces: `bool World::checkInvariants(std::string* why = nullptr) const;` — compiled in every build (spec: "debug only" refers to usage; release tests also call it). On failure writes the invariant ID and the object into `*why` (e.g. `"I-3 e=4:1"`) and returns `false`. This task covers I-1, I-3, I-4, I-5, I-6, I-7, I-10 and I-11 (`iterating_ == 0`). I-2 is checked by the tests via `LifeCounter`. Tasks 10 and 12 extend it with I-9 and I-8.
- Test helper in `tests/support/components.hpp`: `#define CHECK_INV(w) do { std::string why_; CHECK_MESSAGE((w).checkInvariants(&why_), why_); } while (0)`.

- [ ] **Step 1: Failing tests** — deterministic sequences with `CHECK_INV` after each operation:

```cpp
TEST_CASE("create/add/remove/destroy sequence keeps the invariants") {
    ecs::World w; CHECK_INV(w);
    std::vector<ecs::Entity> es;
    for (uint64_t i = 0; i < 100; ++i) { es.push_back(w.create()); CHECK_INV(w); }
    for (uint64_t i = 0; i < 100; i += 2) { w.add(es[i], A{i}); CHECK_INV(w); }
    for (uint64_t i = 0; i < 100; i += 3) { w.add(es[i], Tag{}); CHECK_INV(w); }
    for (uint64_t i = 0; i < 100; i += 4) { w.remove<A>(es[i]); CHECK_INV(w); }
    for (uint64_t i = 0; i < 100; i += 5) { w.destroy(es[i]); CHECK_INV(w); }
    for (int i = 0; i < 20; ++i) { w.create(A{1}, LifeCounter{}); CHECK_INV(w); }   // reuses freeList slots
}
TEST_CASE("I-2: LifeCounter matches live rows") { /* create/remove/destroy with LifeCounter; CHECK(LifeCounter::alive == entities with LifeCounter); 0 after ~World */ }
```

- [ ] **Step 2: Watch it fail** — `$DC make test T=invariants` → FAIL (method missing).
- [ ] **Step 3: Implement.** I-7 compares `componentIndex_[c]` with the set of archetypes whose signature contains `c` and the stored value with `columnOf`. I-6: `bySignature_.size() == archetypes_.size()` and each `archetypes_[i]` is found by its own signature, with `id == i`.
- [ ] **Step 4: Watch it pass.**
- [ ] **Step 5: Commit** — `feat(world): checkInvariants I-1..I-7, I-10, I-11`

---

### Task 9: `fuzz_vs_model` and `nontrivial`

**Files:**

- Create: `tests/support/model.hpp`, `tests/fuzz_vs_model.cpp`, `tests/nontrivial.cpp`

**Interfaces:**

- Produces (`model.hpp`, reused in Tasks 15–16):
    - `struct FS { std::string s; };` (non-trivial, value always > 32 chars to get out of SSO).
    - Fuzz universe: `A`, `B`, `FS`, `Tag`, `LifeCounter`. `using Model = std::unordered_map<ecs::Entity, std::map<ecs::ComponentId, uint64_t>, ecs::EntityHash>;` (a tag stores 0).
    - `uint64_t makeValue(ecs::Entity, uint64_t counter);` — mixes `index`, `generation` and the counter (splitmix64), so that a row swap shows up as a wrong value.
    - `void addToWorld(ecs::World&, ecs::Entity, ecs::ComponentId, uint64_t)`, `bool readFromWorld(const ecs::World&, ecs::Entity, ecs::ComponentId, uint64_t& out)` — dispatch by `cid` to the 5 types.
    - `void requireEqual(ecs::World&, const Model&)` — for each entity in the model: alive, and for each of the 5 types `has` == present in the model and an equal value.
    - `uint64_t fuzzSeed()` (env `ECS_FUZZ_SEED`, default `0xEC5`) and `uint64_t fuzzOps()` (env `ECS_FUZZ_OPS`, default `1'000'000`).

- [ ] **Step 1: Write the fuzz**

Loop of `fuzzOps()` operations with `std::mt19937_64(seed)`; `MESSAGE("seed=" << seed)` at the start. Weights: create 20 %, destroy 10 %, add 30 %, remove 20 %, get 20 % (get compares `readFromWorld` with the model). If alive > 4096, destroy goes up to 40 %. Every 1000 operations `requireEqual(w, model)`; at the end `CHECK_INV(w)` and `CHECK(LifeCounter::alive == <LifeCounter entries in the model>)`; after destroying the `World`, `LifeCounter::alive == 0`. Also keep destroyed handles and, in ~1 % of operations, check `!w.isAlive(old)` and `w.tryGet<A>(old) == nullptr` (Review Focus 4).

- [ ] **Step 2: Write `nontrivial`**

Cases with `std::string`, `std::unique_ptr<int>`, `std::vector<int>` and `LifeCounter`: create 300 entities (forces reallocations), move between archetypes with add/remove of `A`, destroy half, check values and `LifeCounter::alive` == expected count; end of scope → `LifeCounter::alive == 0`.

- [ ] **Step 3: Run**

Run: `$DC make test PRESET=debug-asan T='fuzz_vs_model|nontrivial'`
Expected: PASS, `seed=` printed, no ASan/UBSan report. If something fails, debug with superpowers:systematic-debugging before touching the test.

- [ ] **Step 4: Commit** — `test: fuzz against model (1e6 ops) and nontrivial suite`

**Gate M1:** core, invariants I-1…I-7/I-10, fuzz 1e6 and nontrivial clean under ASan/UBSan.

---

## M2 · Edges and index

### Task 10: Cached edges, I-9 and NF-P4

**Files:**

- Modify: `include/ecs/world.hpp`, `src/world.cpp`, `tests/unit_world.cpp`, `tests/invariants.cpp`
- Create: `tests/support/alloc_counter.cpp`, `tests/support/alloc_counter.hpp`
- Modify: `tests/CMakeLists.txt` (link `alloc_counter.cpp` only into `unit_world`)

**Interfaces:**

- Produces:
    - Private `const Edge& World::edgeFor(Archetype& src, ComponentId cid, bool adding);` — looks in `src.addEdges`/`src.removeEdges`; if missing, `findOrCreate(src.signature ± cid)` + `buildEdge` and inserts (R-AR-2). `add`/`remove` now use this.
    - `uint64_t World::signatureLookupCount() const noexcept;` — incremented on each lookup in `bySignature_`.
    - `checkInvariants` covers I-9: for every edge `a --±c--> b`, `b.signature == a.signature ± c` and `srcToDst[i]` points to the column of `b` with the same `ComponentId` (or `-1` iff absent in `b`); `addedColumn` correct.
    - `alloc_counter.hpp`: `size_t allocationCount() noexcept;` — counter incremented by global replacements of every form of `operator new` (including `align_val_t`).

- [ ] **Step 1: Failing NF-P4 test**

```cpp
TEST_CASE("NF-P4: add/remove with a cached edge neither allocates nor queries the hash") {
    struct C1 { uint64_t v; }; struct C2 { uint64_t v; }; struct C3 { uint64_t v; }; struct C4 { uint64_t v; };
    ecs::World w;
    auto e = w.create(C1{1}, C2{2}, C3{3});
    w.add(e, C4{4}); w.remove<C4>(e);           // warms up edges and capacity (64 rows)
    auto allocs = allocationCount(); auto lookups = w.signatureLookupCount();
    for (int i = 0; i < 1000; ++i) { w.add(e, C4{uint64_t(i)}); w.remove<C4>(e); }
    CHECK(allocationCount() == allocs);
    CHECK(w.signatureLookupCount() == lookups);
    CHECK(w.get<C3>(e).v == 3);
}
```

In `invariants.cpp`, `CHECK_INV` now covers I-9 in every sequence.

- [ ] **Step 2: Watch it fail** — `$DC make test T=unit_world` → FAIL (lookups grow on every add).
- [ ] **Step 3: Implement** `edgeFor`, the counter and I-9.
- [ ] **Step 4: Watch it pass** — `$DC make test T='unit_world|invariants|fuzz_vs_model'` → PASS.
- [ ] **Step 5: Commit** — `feat(world): memoized edges, I-9 and NF-P4`

---

### Task 11: `transition` and `transition_cold` benchmarks

**Files:**

- Create: `bench/transitions.cpp`, `bench/results/m2_transitions.json`
- Modify: `bench/common.hpp`, `bench/CMakeLists.txt`

**Interfaces:**

- Produces:
    - `template<class T> struct NonTrivial { T v; NonTrivial(T x) noexcept; NonTrivial(NonTrivial&&) noexcept; NonTrivial& operator=(NonTrivial&&) noexcept; ~NonTrivial() {} };` in `common.hpp` — same bytes, non-`memcpy` path.
    - `template<int I> struct Comp { uint64_t v; };`
    - `BM_Transition<Trivial|NonTrivial>/k` with `k ∈ {2,4,8,16}`: entities with `k` components `Comp<0..k-1>` (or `NonTrivial<…>`), loop `add<Comp<31>>` + `remove<Comp<31>>`; reports ns/operation, `archetypes` and `entities_per_archetype` as counters.
    - `BM_TransitionCold`: each iteration creates a new `World` (outside the timing) and measures the first transition to a never-seen archetype.
    - `BM_RandomGet` (NF-P6, no target) and baseline `BM_RandomGet_VectorStruct`: 1M entities with `Comp<0..3>`, `get<Comp<2>>` over a fixed random permutation vs `std::vector<struct{4×uint64}>` indexed by the same permutation.
    - `BM_CreateDestroyChurn`: 100k entities with 4 components; per iteration destroys a random 10 % and creates 10 % new ones; ns/operation.

- [ ] **Step 1: Write the benchmarks.**
- [ ] **Step 2: Run and save**

Run: `$DC make build PRESET=release && $DC ./build/release/bench/bench_transitions --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m2_transitions.json`
Expected: medians for the 8 hot variants, the cold one, `random_get` (with baseline) and the churn; trivial ≤ non-trivial for the same `k` (if not, investigate the `memcpy` path in `relocateValue`).

- [ ] **Step 3: Commit** — `bench: transition and transition_cold (M2)`

**Gate M2:** edges + I-9 (Task 10), `componentIndex_` with `get`/`tryGet`/`has` (already in Task 7), transitions measured and NF-P4 (Tasks 10–11). `random_get` and `create_destroy` (section 8) are also measured here.

---

## M3 · Queries

### Task 12: `Query<Ts...>` — cache, incremental matching and I-8

**Files:**

- Create: `include/ecs/query.hpp`, `tests/queries.cpp`
- Modify: `include/ecs/world.hpp`, `src/world.cpp`

**Interfaces:**

- Consumes: `Archetype`, `SmallMap`, `componentIndex_`.
- Produces:
    - `template<class... Us> struct With {}; template<class... Us> struct Without {};`
    - `class QueryCacheBase { public: virtual ~QueryCacheBase(); virtual void onArchetypeCreated(Archetype&) = 0; virtual bool matches(const Archetype&) const = 0; virtual bool isMatched(const Archetype*) const = 0; protected: uint32_t* iterating_; };`
    - `MatchedTable<Ts...>` as in section 4.6.
    - `Query<Ts...>`: `static_assert` that no `std::remove_const_t<T>` is a tag (R-QY-2/NF-S3); `static constexpr std::array<bool, sizeof...(Ts)> writes` = `!std::is_const_v<T>` (R-QY-1). Constructor `Query(uint32_t* iterating, std::vector<ComponentId> with, std::vector<ComponentId> without)` (`with` = ids of `Ts` ∪ `With`, sorted). `void initialize(const std::vector<std::unique_ptr<Archetype>>& all, const SmallMap<ArchetypeId, uint16_t>* smallest);` — with `smallest != nullptr` walks only it (R-QY-3), otherwise all. `size_t matchedCount() const noexcept;`
    - `matches(a)`: `std::includes(a.signature, with_)` and empty intersection with `without_`. `MatchedTable::cols` columns via `columnOf` on match (R-QY-4).
    - `World::query`: `template<class... Ts, class... Filters> Query<Ts...>& query(Filters...);` — key `struct QueryKey { std::vector<uint64_t> ts /* (cid<<1)|isConst */; Signature with, without; auto operator<=>(const QueryKey&) const = default; };` in `std::map<QueryKey, std::unique_ptr<QueryCacheBase>> queryCache_`; `queries_` holds the pointers; `c*` = the component of `with` with the smallest `componentIndex_[c].size()`. `findOrCreate` calls `q->onArchetypeCreated(a)` for each query.
    - `checkInvariants` covers I-8.

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("same instance for the same combination; filters change the key") {
    ecs::World w;
    auto& q1 = w.query<A, const B>(); auto& q2 = w.query<A, const B>();
    CHECK(&q1 == &q2);
    CHECK(static_cast<void*>(&w.query<A, const B>(ecs::Without<Tag>{})) != static_cast<void*>(&q1));
    CHECK(static_cast<void*>(&w.query<A, B>()) != static_cast<void*>(&q1));   // constness is part of the key
}
TEST_CASE("archetypes created before and after the query; with/without of a tag") {
    ecs::World w; w.create(A{1}); w.create(A{2}, Tag{});
    auto& all = w.query<A>(); auto& tagged = w.query<A>(ecs::With<Tag>{}); auto& untagged = w.query<A>(ecs::Without<Tag>{});
    w.create(A{3}, B{0, 0});  w.create(A{4}, B{0, 0}, Tag{});      // new archetypes afterwards
    CHECK(all.matchedCount() == 4); CHECK(tagged.matchedCount() == 2); CHECK(untagged.matchedCount() == 2);
    CHECK_INV(w);   // I-8
}
TEST_CASE("query created before any archetype with its types exists") { /* matchedCount() == 0, then create → 1 */ }
```

- [ ] **Step 2: Watch it fail** — `$DC make test T=queries` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Watch it pass** — `$DC make test T='queries|invariants'` → PASS.
- [ ] **Step 5: Commit** — `feat(query): Query<Ts...> with cache and incremental matching (I-8)`

---

### Task 13: `eachBlock`, `each` and iterating mode

**Files:**

- Modify: `include/ecs/query.hpp`, `include/ecs/world.hpp`, `tests/queries.cpp`

**Interfaces:**

- Produces:
    - `template<class F> void Query::eachBlock(F&& f);` — for each `MatchedTable` with `count > 0`: `f(std::span<Ts>(ptr_i, count)...)`; const `Ts` → `std::span<const T>`.
    - `template<class F> void Query::each(F&& f);` — if `std::is_invocable_v<F, Entity, Ts&...>` calls `f(entity, comps...)`, otherwise `f(comps...)`; column pointers resolved before the inner loop.
    - RAII guard `detail::IterationGuard` (increments `*iterating_` in the constructor, decrements in the destructor) used by both (R-QY-5, Review Focus 5).
    - `World::isIterating() const noexcept` (`iterating_ > 0`) for tests.
    - `ECS_ASSERT(iterating_ == 0, "<op> e=%u:%u during iteration", …)` in `create`, `destroy`, `add`, `remove` (death tests in Task 17).

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("each visits exactly the model's entities and allows writing") {
    ecs::World w; std::set<uint32_t> expected;
    for (uint64_t i = 0; i < 300; ++i) { auto e = (i % 3) ? w.create(A{i}, B{i, 0}) : w.create(A{i}); if (i % 3) expected.insert(e.index); }
    w.create(B{0, 0});                                      // archetype without A
    auto empty = w.create(A{0}, B{0, 0}, Tag{}); w.destroy(empty);   // empty archetype is skipped
    std::set<uint32_t> seen;
    w.query<A, const B>().each([&](ecs::Entity e, A& a, const B&) { seen.insert(e.index); a.v += 1; });
    CHECK(seen == expected);
}
TEST_CASE("eachBlock delivers contiguous spans with count == table size") { /* sum of span.size() == entities with A and B */ }
TEST_CASE("iterating_ nests and goes back to zero, including on exception") {   // Review Focus 5
    ecs::World w; w.create(A{1});
    w.query<A>().each([&](A&) { CHECK(w.isIterating()); w.query<A>().each([&](A&) { CHECK(w.isIterating()); }); });
    CHECK_FALSE(w.isIterating());
    CHECK_THROWS(w.query<A>().each([](A&) { throw 42; }));
    CHECK_FALSE(w.isIterating());
    w.create(A{2}); CHECK_INV(w);
}
```

The compile-fail of `w.query<Tag>()` does not go into CTest; note in the commit that it was checked manually (`static_assert` fires).

- [ ] **Step 2: Watch it fail** — `$DC make test T=queries` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Watch it pass.**
- [ ] **Step 5: Commit** — `feat(query): each/eachBlock and iterating mode`

---

### Task 14: NF-P1, NF-P2, NF-P3 benchmarks

**Files:**

- Modify: `bench/iterate.cpp`, `bench/CMakeLists.txt`, `CMakeLists.txt`
- Create: `bench/fragmentation.cpp`, `bench/results/m3_iterate.json`, `bench/results/m3_fragmented.json`, `bench/results/m3_vectorization.txt`

**Interfaces:**

- Produces:
    - `BM_IterateSingle_ECS/n` in `bench_iterate` (same `n` as SoA): `world.query<Position, const Velocity>().eachBlock(...)` with the same loop body as SoA.
    - `bench_fragmented`: `BM_IterateFragmented/A` with `A ∈ {1, 8, 64, 512, 4096}`, 1M entities distributed uniformly; each entity's archetype is `Position, Velocity` + the subset of tags `Frag<0..11>` given by the bits of `i % A`. Counters `archetypes` and `entities_per_archetype`. Also `BM_IterateSingle_SoA/1000000` as the baseline in the same binary.
    - CMake option `ECS_VEC_REPORT` (OFF by default): adds `-fopt-info-vec-optimized` (GCC) / `-Rpass=loop-vectorize` (Clang) to the bench targets.

- [ ] **Step 1: Write the benchmarks.**
- [ ] **Step 2: Measure and check NF-P1/NF-P2**

```bash
$DC make build PRESET=release
$DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m3_iterate.json
$DC python3 -I tools/bench_ratio.py --json bench/results/m3_iterate.json --name BM_IterateSingle_ECS/1000000 --baseline BM_IterateSingle_SoA/1000000 --max 1.10
$DC ./build/release/bench/bench_fragmented --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m3_fragmented.json
$DC python3 -I tools/bench_ratio.py --json bench/results/m3_fragmented.json --name BM_IterateFragmented/64 --baseline BM_IterateSingle_SoA/1000000 --max 1.25
```

Expected: both `exit 0`. If not, profile before changing code (do not loosen the target).

- [ ] **Step 3: Check NF-P3**

Run: `$DC sh -c "cmake --preset release -DECS_VEC_REPORT=ON && cmake --build --preset release --target bench_iterate 2>&1 | grep -E 'iterate.cpp:[0-9]+:[0-9]+: optimized: loop vectorized' | tee bench/results/m3_vectorization.txt"`
Expected: at least one line pointing to the inner loop of the `eachBlock` lambda. If it does not vectorize, copy the span pointers into local `__restrict` variables inside the benchmark lambda and repeat; note the change in the commit. Then reconfigure with `-DECS_VEC_REPORT=OFF`.

- [ ] **Step 4: Commit** — `bench: iterate ECS vs SoA and fragmentation (NF-P1..P3)`

**Gate M3:** Tasks 12–14.

---

## M4 · Command buffer

### Task 15: `CommandBuffer` (recording, safe growth, discard) and sequential flush

**Files:**

- Create: `include/ecs/command_buffer.hpp`, `src/command_buffer.cpp`, `tests/command_buffer.cpp`
- Modify: `include/ecs/world.hpp`, `src/world.cpp`, `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `EntityStore`, `TypeInfo`, `relocateValue`, `destroyValue`, `alignedAlloc`.
- Produces:
    - `enum class CommandOp : uint8_t { Spawn, Destroy, Add, Remove };`
    - Record: 24-byte header `[op:u8][pad:7][entity:u64][cid:u32][size:u32]`; payload at `alignUp(offset + 24, max(8, align))`; next record at `alignUp(end of payload, 8)`. Block aligned to 64; growth ×2 (minimum 1 KiB) walking the records and using `relocateValue` per payload.
    - `class CommandBuffer` (move-only):
        - `explicit CommandBuffer(EntityStore&)`; `~CommandBuffer()` destroys non-trivial payloads and returns to the `EntityStore` (`release`) the reserved indices not yet materialized (R-CB-2).
        - `Entity spawn();` — immediate `store.allocate()` (R-CB-1).
        - `void destroy(Entity); template<class T> void add(Entity, T&& v); template<class T> void remove(Entity);` (`add` move-constructs `U` into the payload).
        - `struct CommandView { CommandOp op; Entity e; ComponentId cid; void* payload; };` `template<class F> void forEach(F&&);` in recording order.
        - `size_t commandCount() const noexcept; bool empty() const noexcept; const EntityStore* store() const noexcept;`
        - `void clear() noexcept;` — destroys every non-trivial payload (including moved-from ones), resets the buffer and forgets the spawns (already materialized or released by the flush).
    - `World::commands()` → `CommandBuffer{store_}`.
    - `void World::flushSequential(CommandBuffer&);` — oracle without coalescing: applies each command in order via `create`-into-the-reserved-slot (Spawn: places the reserved entity in the root archetype), `destroy`, `addRaw` (with `moveConstruct` from the payload), `removeRaw`; commands for handles that are neither alive nor reserved are ignored; then `cb.clear()`. Asserts `iterating_ == 0` and `cb.store() == &store_`.

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("spawn referenced in the same buffer") {
    ecs::World w; auto cb = w.commands();
    auto e = cb.spawn(); CHECK_FALSE(w.isAlive(e));
    cb.add(e, A{5}); cb.add(e, FS{std::string(40, 'x')});
    w.flushSequential(cb);
    CHECK(w.get<A>(e).v == 5); CHECK(w.get<FS>(e).s == std::string(40, 'x')); CHECK_INV(w);
}
TEST_CASE("non-trivial payload survives buffer growth") {   // Review Focus 1
    ecs::World w; auto e = w.create(); auto cb = w.commands();
    std::string big = "payload-that-does-not-fit-in-sso-0123456789";
    cb.add(e, FS{big});
    for (int i = 0; i < 500; ++i) cb.add(w.create(), A{uint64_t(i)});   // forces several reallocations
    w.flushSequential(cb);
    CHECK(w.get<FS>(e).s == big);
}
TEST_CASE("alignas(64) payload stays aligned") { /* add Wide; in forEach, uintptr_t(payload) % 64 == 0 */ }
TEST_CASE("buffer discarded without flush does not leak") {
    LifeCounter::alive = 0;
    { ecs::World w; { auto cb = w.commands(); auto s = cb.spawn(); cb.add(s, LifeCounter{}); cb.add(s, FS{std::string(64, 'y')}); }
      CHECK(LifeCounter::alive == 0); CHECK(w.create().index == 0); }   // reserved index went back to the freeList (generation +1)
}
TEST_CASE("sequential fuzz == applying directly") {
    // Random batches (fixed seed) of 1..64 commands over spawn/destroy/add/remove of the model.hpp types,
    // applied by flushSequential on the World and one by one on the Model; requireEqual after each batch; 20k commands in total.
}
```

- [ ] **Step 2: Watch it fail** — `$DC make test T=command_buffer` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Watch it pass** under ASan/UBSan.
- [ ] **Step 5: Commit** — `feat(command_buffer): recording, safe growth and sequential flush`

---

### Task 16: Coalesced flush (R-CB-3, R-CB-4)

**Files:**

- Modify: `include/ecs/command_buffer.hpp`, `src/command_buffer.cpp`, `include/ecs/world.hpp`, `src/world.cpp`, `tests/command_buffer.cpp`

**Interfaces:**

- Produces:
    - Pass 1 (in `command_buffer.cpp`): `struct NetComponentOp { ComponentId cid; bool present; void* payload; };` `struct EntityPlan { Entity e; bool spawned; bool destroyed; std::vector<NetComponentOp> ops; /* sorted by cid */ };` `std::vector<EntityPlan> CommandBuffer::plan() const;` — groups by entity preserving order (stable sort of `(entity.index, seq)`); per `cid` the last command wins: Add → `present = true, payload` of the last Add; Remove → `present = false`. `Destroy` clears `ops` and marks `destroyed`; commands after `Destroy` fire `ECS_ASSERT(false, "command after destroy e=%u:%u")` and, without asserts, are ignored (R-CB-4).
    - Pass 2: `void World::flush(CommandBuffer&);` — for each plan: discards it if the handle is neither alive nor reserved (question 3); `destroyed` → `destroy(e)` if alive, `store_.release` if reserved; otherwise final signature = current signature (empty if reserved) with `ops` applied, `dst = findOrCreate(final)`, **one** transition via the jump edge `jumpEdge(src, dst)` (or a direct `pushUninit` if reserved), and for each `present` with a payload: if the column already existed in the source, `destroyValue` + `moveConstruct`/`memcpy`; otherwise constructs in the new column. At the end `cb.clear()` (R-CB-3).
    - Private `const Edge& World::jumpEdge(Archetype& src, Archetype& dst);` — cache `std::unordered_map<uint64_t, Edge> jumpEdges_` keyed by `(uint64_t(src.id) << 32) | dst.id`; I-9 checks the `srcToDst` consistency of these edges too.
    - `ECS_ASSERT(iterating_ == 0, "flush during iteration")` (R-CB-5).

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("R-CB-4: add followed by remove leaves nothing") { /* add A, remove A → !has<A>, entity in the original archetype */ }
TEST_CASE("R-CB-4: repeated add keeps the last value") { /* add A{1}, add A{2} → get<A>.v == 2 */ }
TEST_CASE("R-CB-4: destroy discards earlier commands; spawn+destroy releases the index") { /* … */ }
TEST_CASE("R-CB-3: a single transition per entity") {
    ecs::World w; auto e = w.create(); auto cb = w.commands();
    cb.add(e, A{1}); cb.add(e, B{2, 3}); cb.add(e, Tag{});
    auto before = w.archetypeCount();
    w.flush(cb);
    CHECK(w.archetypeCount() == before + 1);   // only {A,B,Tag}; no intermediate one
}
TEST_CASE("commands for a directly destroyed handle are ignored") {   // Review Focus 4 / question 3
    ecs::World w; auto e = w.create(A{1}); auto cb = w.commands(); cb.add(e, B{0, 0});
    w.destroy(e); auto reused = w.create(A{9});        // same index, new generation
    w.flush(cb);
    CHECK_FALSE(w.has<B>(reused)); CHECK(w.get<A>(reused).v == 9);
}
TEST_CASE("fuzz: coalesced flush == flushSequential") {
    // Two Worlds fed by the same seed; logical handles mapped in std::vector<std::pair<Entity,Entity>>;
    // each batch recorded into cbA (world1) and cbB (world2) with the same logical commands (no commands after destroy),
    // world1.flush(cbA), world2.flushSequential(cbB); compare components per logical entity and CHECK_INV on both.
}
```

Add `uint32_t World::archetypeCount() const noexcept` in this task (used here and in `stats()`).

- [ ] **Step 2: Watch it fail** — `$DC make test T=command_buffer` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Watch it pass** — `$DC make test T='command_buffer|invariants|fuzz'` → PASS.
- [ ] **Step 5: Commit** — `feat(command_buffer): coalesced flush with one transition per entity`

---

### Task 17: `contract_death` and I-11

**Files:**

- Create: `tests/contract_death.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

- Consumes: `setAssertHandler`, `World`, `Query::each`, `CommandBuffer`.
- Produces: executable `contract_death` (no doctest), built only when `ECS_ENABLE_ASSERTS` resolves to 1. `main(argc, argv)` installs a handler that prints the message to `stderr`, checks that it contains the substrings required by the case (otherwise `std::_Exit(2)`) and calls `std::_Exit(0)`; runs the case named in `argv[1]`; if it returns, prints "assert did not fire" and returns 1. One `add_test(NAME contract_death.<case> COMMAND contract_death <case>)` per case:

| case                        | action                                                                    | required substrings     |
| --------------------------- | ------------------------------------------------------------------------- | ----------------------- |
| `add_during_each`           | `w.add(e, B{})` inside `query<A>().each`                                  | `add`, `e=`             |
| `get_dead_entity`           | `w.destroy(e); w.get<Position>(e)` (local `struct Position { float x; }`) | `get`, `e=`, `Position` |
| `flush_during_each`         | `w.flush(cb)` inside `each`                                               | `flush`                 |
| `command_after_destroy`     | `cb.destroy(e); cb.add(e, A{}); w.flush(cb)`                              | `after destroy`         |
| `destroy_during_each_block` | `w.destroy(e)` inside `eachBlock`                                         | `destroy`               |

- [ ] **Step 1: Write the executable and register the cases.**
- [ ] **Step 2: Run** — `$DC make test PRESET=debug-asan T=contract_death` → 5/5 PASS. Sanity check: temporarily replace a case with valid code and watch the test fail with "assert did not fire"; undo.
- [ ] **Step 3: Run the full suite** — `$DC make test PRESET=debug-asan` and `$DC make test PRESET=release` → 100 % (in release, `contract_death` does not exist).
- [ ] **Step 4: Commit** — `test: contract_death for iterating mode and flush`

---

### Task 18: `flush_coalesced` benchmark (NF-P5)

**Files:**

- Create: `bench/flush.cpp`, `bench/results/m4_flush.json`
- Modify: `bench/CMakeLists.txt`

**Interfaces:**

- Produces: `bench_flush` with `BM_FlushCoalesced/N` and `BM_DirectAdds/N`, `N ∈ {1000, 100000}`: `N` entities created in the root archetype (outside the timing, `PauseTiming`), 3 adds (`Comp<0>`, `Comp<1>`, `Comp<2>`) per entity; the first records into a `CommandBuffer` and does the `flush` inside the timing, the second does `3N` direct `add`s. Both warm up the edges/archetypes first. Reports ns/entity.

- [ ] **Step 1: Write.**
- [ ] **Step 2: Measure and check**

```bash
$DC make build PRESET=release
$DC ./build/release/bench/bench_flush --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m4_flush.json
$DC python3 -I tools/bench_ratio.py --json bench/results/m4_flush.json --name BM_FlushCoalesced/100000 --baseline BM_DirectAdds/100000 --max 0.5
$DC python3 -I tools/bench_ratio.py --json bench/results/m4_flush.json --name BM_FlushCoalesced/1000 --baseline BM_DirectAdds/1000 --max 0.5
```

Expected: `exit 0` on both (≥ 2× faster).

- [ ] **Step 3: Commit** — `bench: coalesced flush vs direct adds (NF-P5)`

**Gate M4:** Tasks 15–18.

---

## M5 · Instrumentation

### Task 19: `stats()`

**Files:**

- Modify: `include/ecs/world.hpp`, `src/world.cpp`, `tests/unit_world.cpp`

**Interfaces:**

- Produces:

```cpp
struct ArchetypeStats {
    ArchetypeId id; Signature signature;
    uint32_t count, capacity, edgeCount;
    size_t columnBytes;     // capacity × Σ column size + capacity × sizeof(Entity)
    size_t idleBytes;       // (capacity − count) × (Σ size + sizeof(Entity))
    size_t signatureBytes;  // capacity() of signature + columnTypes, in bytes
    size_t edgeBytes;       // capacityBytes() of addEdges/removeEdges + srcToDst
};
struct WorldStats {
    uint32_t archetypeCount; uint32_t liveEntities;
    std::vector<ArchetypeStats> archetypes;       // by id
    std::array<uint32_t, 33> histogram;           // [0] = empty archetypes; [i] = count in [2^(i-1), 2^i)
    uint64_t signatureLookups;
};
WorldStats World::stats() const;
```

- [ ] **Step 1: Failing test**

```cpp
TEST_CASE("stats: counts, histogram and idle memory") {
    ecs::World w;
    for (uint64_t i = 0; i < 5; ++i) w.create(A{i});      // {A}: 5
    w.create(A{0}, B{0, 0});                              // {A,B}: 1
    auto s = w.stats();
    CHECK(s.archetypeCount == 3);                         // root, {A}, {A,B}
    CHECK(s.liveEntities == 6);
    CHECK(s.histogram[0] == 1);                           // empty root
    CHECK(s.histogram[1] == 1);                           // count 1
    CHECK(s.histogram[3] == 1);                           // count 5 ∈ [4, 8)
    auto& a = *std::find_if(s.archetypes.begin(), s.archetypes.end(),
                            [](auto& x) { return x.signature == ecs::Signature{ecs::componentId<A>()}; });
    CHECK(a.capacity == 64);
    CHECK(a.idleBytes == (64 - 5) * (sizeof(A) + sizeof(ecs::Entity)));
}
```

- [ ] **Step 2: Watch it fail** — `$DC make test T=unit_world` → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Watch it pass.**
- [ ] **Step 5: Commit** — `feat(world): stats() with histogram and idle memory (NF-M2)`

---

### Task 20: `iterate_fragmented` under `perf stat`

**Files:**

- Create: `tools/perf_fragmented.sh`, `bench/results/m5_perf_fragmented.txt`, `bench/results/README.md`

**Interfaces:**

- Consumes: `bench_fragmented` (Task 14), preset `relwithdebinfo`, `perf` service (privileged).
- Produces: `tools/perf_fragmented.sh <bench_bin> <out>` — for each `A ∈ {1, 8, 64, 512, 4096}` runs `perf stat -x, -e cycles,instructions,cache-misses,L1-dcache-load-misses <bin> --benchmark_filter='BM_IterateFragmented/A$' --benchmark_min_time=2s` and writes a CSV line `A,ns_per_entity,cycles,instructions,cache_misses_per_entity,l1d_misses_per_entity` (events divided by `iterations × 1M`). The `README.md` in `bench/results/` lists each JSON/TXT, the commit and the machine it was measured on, and the curve of ns and misses per entity as a function of `A`.

- [ ] **Step 1: Write the script** (POSIX sh, `set -eu`).
- [ ] **Step 2: Run**

Run: `docker compose run --rm perf sh -c "make build PRESET=relwithdebinfo && sh tools/perf_fragmented.sh build/relwithdebinfo/bench/bench_fragmented bench/results/m5_perf_fragmented.txt"`
Expected: 5 CSV lines with numeric counters. If `perf` does not work on the Docker Desktop/WSL2 kernel (`<not supported>` or a version error), **stop and report to the user** with the exact output; do not invent numbers.

- [ ] **Step 3: Write `bench/results/README.md`** with the generated table.
- [ ] **Step 4: Commit** — `bench: perf stat of iterate_fragmented and results record (M5)`

**Gate M5:** Tasks 19–20. End of v1: run `$DC make test PRESET=debug-asan` and `$DC make test PRESET=release` one last time and attach the output to the PR.

