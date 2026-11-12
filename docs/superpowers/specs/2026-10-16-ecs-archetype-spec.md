# Specification — Archetype ECS in C++

Oct 16, 2026 · @Danrley

## 1. Goal, assumptions and scope

v1 delivers a single-threaded archetype ECS in C++20. Its batch iteration must stay close to hand-written SoA, and its structural changes must have measured, predictable cost. The project is for study and benchmarking, not production, and the architecture must allow chunks, parallelism and change detection without changing the systems' API.

### Assumptions

- C++20 standard (`std::span`, concepts, `std::bit_cast`). Target compilers: GCC 13+ and Clang 17+; MSVC is desirable but not blocking.
- A single `World` per thread and no concurrent access in v1.
- Components are types with a `noexcept` move constructor. This is checked at compile time.
- Component IDs are valid only within the process. There is no support for plugins or DLLs.
- Exceptions are not used on the hot path. Contract violations trigger `assert` in debug and are undefined behaviour in release.

### In v1

| Item | Module |
| --- | --- |
| Runtime type IDs, with `TypeInfo` and a `memcpy` path for trivial types | `type_info` |
| Generational handles and index recycling | `entity` |
| SoA table with columns that grow together | `table` |
| Archetypes, with signature and transition graph | `archetype` |
| `create`, `destroy`, `add`, `remove`, `get`, `tryGet`, `has` and `isAlive` | `world` |
| Cached queries with `with`, `without` and per-block and per-entity iteration | `query` |
| Command buffer with reserved IDs and coalescing | `command_buffer` |
| "Iterating" mode, which triggers `assert` on a direct structural change | `world` |
| Instrumentation: number of archetypes, entities per archetype and histogram | `world` |

### Out of v1

- Multithreading and a system scheduler.
- Fixed-size chunks.
- Version-based change detection.
- Enableable components (bitset).
- Hybrid sparse-set storage.
- Entity relationships, observers and events.
- Serialization, reflection and editor.
- `ComponentId` stability across runs and across DLLs.

Each out-of-scope item has a planned extension point in section 10.

## 2. Glossary and conventions

| Term | Definition in this specification |
| --- | --- |
| Entity | Opaque 64-bit handle: 32-bit index plus 32-bit generation. Holds no data. |
| Component | Type `T` registered via `componentId<T>()`. It can have data (`sizeof(T) > 0` and not empty) or be a tag (`std::is_empty_v<T>`). |
| `ComponentId` | Dense `uint32_t` integer, assigned in order of first use. |
| Signature | Sorted `std::vector<ComponentId>` without duplicates. Includes tags. |
| `columnTypes` | Sorted subset of the signature that contains only the components with data. |
| Archetype | Owner of a `Table`. Has a unique signature within the `World`. |
| Table | SoA storage: a single `count` and `capacity` and N byte columns. |
| Alive row | Row `r < count` whose columns hold constructed objects. |
| Dead row | Memory range with no constructed object, not even a moved-from one. |
| Block | Contiguous range of rows handed to a system as spans. In v1, it is a whole table. |
| Structural change | Any operation that changes `count` or signature: `create`, `destroy`, `add` and `remove`. |
| Edge | Memoized transition `archetype × (±ComponentId) → archetype`, with the column mapping embedded. |

### Conventions

- Namespace `ecs`, headers in `include/ecs/`.
- Types in `PascalCase` and functions in `camelCase`.
- Complexities are expressed in terms of: `k` = number of components of the entity, `A` = number of archetypes, `Q` = number of queries, `n` = number of entities and `m` = number of entities in an archetype.
- "Debug" means `NDEBUG` not defined. In that mode, all invariants of section 6 can be checked on demand.

## 3. Architecture

The library has seven modules in four layers, and each layer depends only on the ones below it. The `Table` does not know about alive entities, queries or records, which allows replacing it with chunks without touching the upper layers.

&#91;embedded content: layered architecture · 7 modules\]

The arrows hold between layers: a module may use any module of a lower layer, never of a higher one. The `World` is the only one that knows the records and the only one that calls `fillHole`, so updating the record of the displaced entity happens in a single place.

### Directory layout

```
ecs/
  CMakeLists.txt  CMakePresets.json
  include/ecs/
    type_info.hpp      componentId<T>(), TypeInfo
    entity.hpp         Entity, EntityRecord
    table.hpp          Table, Column
    archetype.hpp      Archetype, Edge, Signature, SigHash
    query.hpp          Query<Ts...>, MatchedTable
    command_buffer.hpp CommandBuffer
    world.hpp          World (inline templates)
    detail/            SmallMap, assert, alignment
  src/
    table.cpp  archetype.cpp  world.cpp  command_buffer.cpp
  tests/
    unit_table.cpp  unit_world.cpp  invariants.cpp
    fuzz_vs_model.cpp  nontrivial.cpp  queries.cpp
    command_buffer.cpp  contract_death.cpp
  bench/
    iterate.cpp  transitions.cpp  fragmentation.cpp  flush.cpp
```

The non-template part (`Table` reallocation, `findOrCreate`, edge construction and flush) lives in `.cpp`. The templates (`add<T>`, `get<T>`, `Query<Ts...>`) are thin and delegate to non-template functions that take a `ComponentId`, which reduces compile time and binary size.

## 4. Module specification

Each module knows only the ones to its left in the chain `type_info → entity → table → archetype → query / command_buffer → world`. The `Table` does not know what an alive entity, a query or a record is.

### 4.1 `type_info`

```cpp
using ComponentId = uint32_t;

struct TypeInfo {
    uint32_t size;            // 0 for tags
    uint32_t align;
    bool     trivial;         // is_trivially_copyable_v && is_trivially_destructible_v
    bool     tag;             // is_empty_v
    void (*moveConstruct)(void* dst, void* src) noexcept;  // nullptr if trivial
    void (*destroy)(void* p) noexcept;                     // nullptr if trivial
    std::string_view name;    // for diagnostics
};

template<class T> ComponentId componentId();
const TypeInfo& typeInfo(ComponentId);
```

- **R-TI-1.** `componentId<T>()` returns the same value on every call in the process, and IDs are dense starting at 0.
- **R-TI-2.** `T` goes through `std::remove_cvref_t` before registration, so `componentId<const T&>() == componentId<T>()`.
- **R-TI-3.** `static_assert(std::is_nothrow_move_constructible_v<T>)` and `static_assert(alignof(T) <= 64)`.
- **R-TI-4.** Registration sits behind a function with a local `static`. v1 does not require thread safety, but registering inside a `static` avoids initialization-order problems.

### 4.2 `entity`

```cpp
struct Entity {
    uint32_t index;
    uint32_t generation;
    friend bool operator==(Entity, Entity) = default;
};
inline constexpr Entity NullEntity{UINT32_MAX, 0};

struct EntityRecord {
    Archetype* arch;        // nullptr = dead or reserved (pending)
    uint32_t   row;
    uint32_t   generation;  // current generation of the slot
};
```

- **R-EN-1.** On destroy, `generation` is incremented and the index goes to the `freeList`. When the generation reaches `UINT32_MAX`, the slot is retired and never reused.
- **R-EN-2.** `isAlive(e)` is equivalent to `e.index < records.size() && records[e.index].generation == e.generation && records[e.index].arch != nullptr`.
- **R-EN-3.** An index reserved by a command buffer has `arch == nullptr` and its final generation already set. It is not considered alive until the flush.

### 4.3 `table`

```cpp
struct Column {
    const TypeInfo* info;
    std::byte*      data = nullptr;
    void* at(uint32_t r) noexcept { return data + size_t(r) * info->size; }
};

class Table {
public:
    explicit Table(std::span<const ComponentId> columnTypes);
    ~Table();                                    // destroys the alive rows and frees
    Table(Table&&) noexcept; Table& operator=(Table&&) noexcept;
    Table(const Table&) = delete;

    uint32_t count() const noexcept;
    uint32_t pushUninit(Entity e);               // new DEAD row at the end; may reallocate
    void     destroyRow(uint32_t r) noexcept;    // alive row → dead
    std::optional<Entity> fillHole(uint32_t r) noexcept; // fills dead row with the last one

    std::span<const Entity> entities() const noexcept;
    Column& column(uint16_t c) noexcept;
    uint16_t columnCount() const noexcept;
private:
    void reserve(uint32_t newCap);               // reallocates ALL columns together
    std::vector<Column> columns_;
    std::vector<Entity> entities_;               // size() == count
    uint32_t count_ = 0, capacity_ = 0;
};
```

- **R-TB-1.** There is a single `count` and a single `capacity` per table. No column keeps its own count.
- **R-TB-2.** Growth is geometric (×2, minimum of 64 rows). On reallocation, trivial types are copied with a single `memcpy` per column; the others use `moveConstruct` followed by `destroy` per element.
- **R-TB-3.** Each column is allocated with `std::align_val_t(max(info->align, 64))`, aligned to the cache line to favour vectorization.
- **R-TB-4.** `pushUninit` leaves the new row dead. The caller must construct all columns before any other operation on the table.
- **R-TB-5.** `fillHole(r)` requires row `r` to be dead. It decrements `count`; if `r` was not the last row, it relocates the last row to `r` and returns the entity that was displaced.
- **R-TB-6.** The table does not shrink automatically. `shrinkToFit()` remains an optional API.

### 4.4 `archetype`

```cpp
struct Edge {
    Archetype* target = nullptr;
    std::vector<int16_t> srcToDst;  // per source column: column in the destination, or -1
    int16_t addedColumn = -1;       // add of a component with data: column in the destination
};

struct Archetype {
    ArchetypeId              id;           // stable index in World::archetypes_
    std::vector<ComponentId> signature;    // sorted, includes tags
    std::vector<ComponentId> columnTypes;  // sorted, data only
    Table                    table;
    SmallMap<ComponentId, Edge> addEdges, removeEdges;
    std::bitset<128>         mask;         // fast filter (optional)
};
```

- **R-AR-1.** Archetypes live in `std::vector<std::unique_ptr<Archetype>>`, so the pointer is stable for the whole life of the `World`. In v1, archetypes are never removed.
- **R-AR-2.** An `Edge` is created on the first transition. `srcToDst` is computed by merging the two sorted `columnTypes`, in O(k).
- **R-AR-3.** The root archetype (empty signature) is created in the `World` constructor, with `id == 0`.
- **R-AR-4.** `mask` is used only while the largest `ComponentId` is below 128. Above that, the test falls back to `std::includes`.

### 4.5 `World` indices

```cpp
std::vector<EntityRecord>                         records_;
std::vector<uint32_t>                             freeList_;
std::vector<std::unique_ptr<Archetype>>           archetypes_;
std::unordered_map<Signature, Archetype*, SigHash> bySignature_;
std::vector<SmallMap<ArchetypeId, uint16_t>>      componentIndex_;  // [cid]: archetype → column (NoColumn for tag)
std::vector<QueryCacheBase*>                      queries_;
uint32_t                                          iterating_ = 0;
```

- **R-WI-1.** `componentIndex_[cid]` lists every archetype that contains `cid`. It is updated in `findOrCreate` and does not need to be updated by any other operation.
- **R-WI-2.** `get<T>(e)` costs one access to `records_[e.index]`, one lookup in `componentIndex_[cid]` and one access to `column.at(row)`.

### 4.6 `query`

```cpp
template<class... Ts>
struct MatchedTable {
    Archetype* arch;
    std::array<uint16_t, sizeof...(Ts)> cols;
};

template<class... Ts>
class Query : QueryCacheBase {
    std::vector<ComponentId> with_, without_;     // sorted
    std::vector<MatchedTable<Ts...>> matched_;
public:
    Query& without(ComponentId...);
    template<class F> void eachBlock(F&& f);      // f(std::span<Ts>...)
    template<class F> void each(F&& f);           // f(Ts&...) or f(Entity, Ts&...)
    void onArchetypeCreated(Archetype&) override; // incremental matching
};
```

- **R-QY-1.** `Ts` may contain `const T`. In that case, the span handed over is `std::span<const T>`. Constness is kept as read/write metadata, for future use by the scheduler.
- **R-QY-2.** A tag in `Ts` is a compile error. Tags may only appear in the filter (`with<Tag>()` and `without<Tag>()`).
- **R-QY-3.** On creation, the query walks `componentIndex_[c*]`, where `c*` is the component of `with_` with the fewest archetypes. It does not scan `archetypes_`.
- **R-QY-4.** `eachBlock` and `each` skip archetypes with `count == 0`. Column indices are resolved at match time; the inner loop does no lookup.
- **R-QY-5.** While an `each` or `eachBlock` is in progress, `World::iterating_` is greater than zero (nesting allowed).
- **R-QY-6.** The query's lifetime is controlled by the `World`, which returns a stable reference. The RAII version (unregistration in the destructor) is left for after v1.

### 4.7 `command_buffer`

The buffer is a `std::vector<std::byte>` with records aligned to 8 bytes:

```
[op:u8][pad][entity:u64][cid:u32][size:u32][payload: size bytes, aligned]
op ∈ { Spawn, Destroy, Add, Remove }
```

- **R-CB-1.** `spawn()` reserves an index immediately, per R-EN-3, and returns an `Entity` that can be used in later commands of the same buffer.
- **R-CB-2.** `add<T>(e, v)` move-constructs `T` inside the payload. If the buffer is discarded without a flush, the non-trivial payloads are destroyed.
- **R-CB-3.** The flush runs in two passes. The first groups the commands by entity, preserving order, and computes each entity's final signature. The second performs **a single transition per entity**, straight to the final archetype.
- **R-CB-4.** Conflict semantics, applied in recording order: `add` followed by `remove` of the same `T` leaves nothing; repeated `add` keeps the last value; `destroy` discards the entity's earlier commands; commands after `destroy` are ignored (with `assert` in debug).
- **R-CB-5.** Calling `flush` with `iterating_ > 0` is a contract error.

## 5. Public `World` API

Every direct structural change requires `iterating_ == 0`. During an iteration, changes go through a `CommandBuffer`.

```cpp
class World {
public:
    Entity create();
    template<class... Ts> Entity create(Ts&&... comps);   // born directly in the final archetype
    void   destroy(Entity e);
    bool   isAlive(Entity e) const noexcept;

    template<class T> void      add(Entity e, T&& value);    // or assigns, if it already has T
    template<class T> void      remove(Entity e);
    template<class T> bool      has(Entity e) const noexcept;
    template<class T> T&        get(Entity e);
    template<class T> T*        tryGet(Entity e) noexcept;

    template<class... Ts> Query<Ts...>& query();             // cached by (Ts..., filters)
    CommandBuffer commands();
    void          flush(CommandBuffer&);

    WorldStats stats() const;                                // section 4.5 / M5
    bool       checkInvariants() const;                      // section 6, debug only
};
```

| Operation | Precondition | Postcondition | Cost |
| --- | --- | --- | --- |
| `create()` | — | Alive entity in the root archetype, with current generation | Amortized O(1) |
| `create<Ts...>(…)` | Distinct `Ts` | Alive entity in archetype `{Ts…}`, without passing through the intermediate ones | O(\|Ts\|) + signature hash the 1st time |
| `destroy(e)` | `isAlive(e)` | Components destroyed, generation incremented, index in the `freeList`, record of the displaced entity updated | O(k) |
| `add<T>(e, v)` | `isAlive(e)` | If it did not have `T`: moves to archetype `sig ∪ {T}`. If it did: `get<T>(e) = std::move(v)`, no structural change | O(k) with cached edge; O(k + hash) the 1st time |
| `remove<T>(e)` | `isAlive(e)` | If it had `T`: destroys `T` and moves to `sig \ {T}`. If it did not: no-op | Same as `add` |
| `has<T>(e)` | `isAlive(e)` | — | O(1), via `componentIndex_` |
| `get<T>(e)` | `isAlive(e) && has<T>(e)`; `T` is not a tag | Reference valid until the next structural change on any entity | O(1), 3 indirections |
| `tryGet<T>(e)` | — | `nullptr` if the entity is dead or lacks `T` | O(1) |
| `query<Ts...>()` | — | Same instance for the same combination | O(A\_c\*) the 1st time; O(1) afterwards |
| `flush(cb)` | `iterating_ == 0` | One transition per affected entity | O(Σk) |

### Reference validity rules

- **R-API-1.** Pointers and references obtained from `get`, `tryGet` or `eachBlock` spans are invalidated by **any** structural change in the `World`, even on another entity, because the table may reallocate or swap.
- **R-API-2.** `Entity` is the only stable handle. To keep a relationship, store the `Entity` and call `tryGet` when using it.
- **R-API-3.** Writing to components inside `each` is allowed; adding or removing components is not.

## 6. System invariants

The invariants below must hold **between** any two public calls. `checkInvariants()` checks all of them, and the tests call it after every operation.

| ID | Invariant | Catches which bug |
| --- | --- | --- |
| I-1 | For every table, `entities().size() == count()`, and each column has capacity for at least `count()` rows | Count diverging between columns |
| I-2 | Rows `< count` are alive and rows `>= count` are dead. Checked with a lifetime-counter component (constructions − destructions == alive) | Moved-from not destroyed, double destroy |
| I-3 | For every alive `e`, `records[e.index].arch->table.entities()[records[e.index].row] == e` | Record of the entity displaced by the swap not updated |
| I-4 | For every archetype `a` and every row `r`, `records[a.entities[r].index]` points to `(a, r)` | The same bug, seen from the table side |
| I-5 | `signature` and `columnTypes` are sorted and without duplicates, and `columnTypes ⊆ signature`, containing exactly the non-tags | Wrong column indexed because of a tag |
| I-6 | `bySignature_` is bijective with `archetypes_` | Duplicate archetype |
| I-7 | `componentIndex_[c]` contains `a` if and only if `c ∈ a.signature`, and the stored column index is correct | `get` reading the wrong column |
| I-8 | For every query `q` and every archetype `a`, `a ∈ q.matched` if and only if `q.matches(a)` | New archetype not registered in the queries |
| I-9 | Every edge `a --±c--> b` satisfies `b.signature == a.signature ± c`, and its `srcToDst` is consistent | Stale edge or wrong mapping |
| I-10 | Indices in the `freeList` have `arch == nullptr`, and there are no repeated indices | Double reuse of a slot |
| I-11 | Outside `each`, `iterating_ == 0` | Counter leaked by early return or exception |

I-2 requires instrumentation: in debug, a `LifeCounter` test component increments and decrements a global counter in its constructor and destructor. The total must match the number of alive rows that contain that component.

## 7. Non-functional requirements

Performance targets are relative to a baseline measured on the same machine, not absolute numbers. Each target is checked by a benchmark from section 8.

### Performance

| ID | Requirement | Target |
| --- | --- | --- |
| NF-P1 | Iteration `Position += Velocity * dt` over 1M entities in a single archetype | At most 1.10× the time of manual SoA (two `std::vector<float3>`) |
| NF-P2 | The same, spread over 64 archetypes of \~15k entities | At most 1.25× manual SoA |
| NF-P3 | The inner loop of `eachBlock` with arithmetic types is vectorized | Confirmed by `-fopt-info-vec` (GCC) or `-Rpass=loop-vectorize` (Clang) |
| NF-P4 | `add`/`remove` with a cached edge, entity with 4 trivial components | Zero allocations (with reserved capacity) and zero hash lookups |
| NF-P5 | Flush of N coalesced adds (3 per entity) | At least 2× faster than 3N direct `add` calls |
| NF-P6 | `get<T>(e)` with random access | Measured and reported; no target in v1 |

### Memory

- **NF-M1.** Per-entity overhead: one pointer + 8 bytes per `EntityRecord` (16 B on 64-bit, 12 B on 32-bit) plus 8 bytes of `Entity` in the table. There is no per-entity allocation.
- **NF-M2.** Per-archetype overhead must be reported by `stats()`: signature, edges, columns and idle capacity.
- **NF-M3.** Columns are aligned to 64 bytes (R-TB-3).

### Safety and diagnostics

- **NF-S1.** All test code runs clean under ASan and UBSan.
- **NF-S2.** In debug, the preconditions of section 5 are checked by `ECS_ASSERT`, with a message that includes the operation, the `Entity` and the type name.
- **NF-S3.** Usage errors detectable at compile time become `static_assert`: tag in a query's `Ts`, type without `noexcept` move and repeated types in `create<Ts...>`.
- **NF-S4.** `ECS_ASSERT` can be removed in release by macro (`ECS_ENABLE_ASSERTS=0`).

### Build

- **NF-B1.** CMake 3.25+, with presets `debug-asan`, `release` and `relwithdebinfo` (the latter for profiling).
- **NF-B2.** Warnings with `-Wall -Wextra -Wpedantic -Werror` in CI.
- **NF-B3.** External dependencies only in tests and benchmarks (doctest and Google Benchmark, via `FetchContent`). The library has no dependencies.

## 8. Testing and benchmark strategy

The main test is a fuzz that compares the `World` with a trivially correct reference model. It catches bugs of the right value on the wrong entity, which the structural invariants alone do not catch.

### Tests

| Suite | What it does | Criterion |
| --- | --- | --- |
| `unit_table` | `pushUninit`, `destroyRow`, `fillHole` and reallocation, with trivial and non-trivial types | I-1 and I-2 after each step |
| `unit_world` | One case per row of the section 5 table, including `add` over an existing `T` and `remove` of an absent `T` | Exact postconditions |
| `invariants` | Short, deterministic sequences calling `checkInvariants()` at every operation | All invariants of section 6 |
| `fuzz_vs_model` | 10⁶ random operations (create, destroy, add, remove, get), with a fixed, reported seed. The model is `std::unordered_map<Entity, std::map<ComponentId, Value>>` | State equality every 1,000 operations and `checkInvariants()` at the end |
| `nontrivial` | Components `std::string`, `std::unique_ptr<int>`, `std::vector<int>` and `LifeCounter` | Clean under ASan and UBSan; lifetime counter reaches zero at the end |
| `queries` | Archetypes created before and after the query, `without`, tags in the filter and empty archetypes | I-8; count of visited entities equal to the model's |
| `command_buffer` | Coalescing, R-CB-4 conflicts, `spawn` referenced in the same buffer and buffer discarded without flush | State equal to applying the commands one by one; no leak |
| `contract_death` | `add` during `each`, `get` on a dead entity, `flush` during iteration | The `assert` fires (death test in debug) |

In `fuzz_vs_model`, component values must be derived from the `Entity` and a counter, so that a row swap shows up as a wrong value.

### Benchmarks

| Benchmark | Variables | Metric | Baseline |
| --- | --- | --- | --- |
| `iterate_single` | n ∈ {10³, 10⁴, 10⁵, 10⁶} | ns/entity | Manual SoA with two `std::vector` |
| `iterate_fragmented` | 1M entities in A ∈ {1, 8, 64, 512, 4096} archetypes | ns/entity | `iterate_single` with the same n |
| `transition` | k ∈ {2, 4, 8, 16}; trivial and non-trivial components | ns/operation | Same operation without the `memcpy` path |
| `transition_cold` | First transition to new archetypes | ns/operation | `transition` |
| `flush_coalesced` | N ∈ {10³, 10⁵}, 3 adds per entity | ns/entity | 3N direct `add` calls |
| `random_get` | `get<T>` access in random order | ns/access | Indexed `std::vector<struct>` |
| `create_destroy` | Churn of 10% of entities per frame | ns/operation | — |

Each benchmark reports median and deviation over at least 10 repetitions, plus entities per archetype and number of archetypes. Results are saved as JSON (`--benchmark_out`) for comparison across commits.

### Profiling

The hot benchmarks run under `perf stat`, recording cycles, instructions, `cache-misses` and `L1-dcache-load-misses`. In `iterate_fragmented`, the metric of interest is misses per entity as a function of A.

## 9. Milestones and acceptance criteria

v1 closes in six sequential milestones, and none starts before the previous one's gate passes.

&#91;embedded content: v1 milestones · 6 steps, each with its gate\]

The criteria below are each milestone's gate. The IDs refer to sections 6 and 7.

**M0 · Infrastructure**

- [ ] CMake presets `debug-asan`, `release` and `relwithdebinfo` (NF-B1)
- [ ] doctest and Google Benchmark via `FetchContent` (NF-B3)
- [ ] SoA baseline of `iterate_single` measured and saved as JSON

**M1 · Core**

- [ ] `type_info`, `entity`, `table` and a minimal `World`, with `add`/`remove` via `findOrCreate` and no edges
- [ ] `checkInvariants()` covers I-1 to I-7 and I-10
- [ ] `fuzz_vs_model` with 10⁶ operations passes
- [ ] `nontrivial` suite clean under ASan and UBSan (NF-S1)

**M2 · Edges and index**

- [ ] `Edge` with `srcToDst`; I-9 checked
- [ ] `componentIndex_` with `get`, `tryGet` and `has`
- [ ] `transition` and `transition_cold` measured, with and without `memcpy`; NF-P4 met

**M3 · Queries**

- [ ] `Query<Ts...>` with incremental matching; I-8 checked
- [ ] `eachBlock` and `each`; `static_assert` for a tag in `Ts`
- [ ] NF-P1, NF-P2 and NF-P3 met

**M4 · Command buffer**

- [ ] `CommandBuffer` without coalescing, validated in the fuzz
- [ ] Coalescing (R-CB-3) and conflicts (R-CB-4), compared with the version without coalescing
- [ ] Iterating mode, I-11 and the `contract_death` suite
- [ ] NF-P5 met

**M5 · Instrumentation**

- [ ] `stats()` with number of archetypes, entities per archetype, histogram and idle memory (NF-M2)
- [ ] `iterate_fragmented` under `perf stat`, with the curve of ns and misses per entity recorded

## 10. Evolutions, risks and open questions

Chunks are the next evolution after v1, because `eachBlock` already isolates systems from the storage format.

### Extension points

| Evolution | Where it goes | What changes | What does **not** change |
| --- | --- | --- | --- |
| 16 KB chunks | `Table` becomes a list of `Chunk`; `EntityRecord.row` comes to encode `(chunk, row)` | `Table`, `fillHole` and the `Query` block iterator | Systems' signature (`eachBlock` with spans) |
| Parallel iteration | `Query::eachBlockParallel(pool, f)`, with one job per block | `Query` and a read/write flag per `Ts` | Storage |
| Change detection | `uint32_t` version per column and per block, incremented on mutable access | `Column`/`Chunk` and a `changed<T>()` filter in the query | Transitions |
| Enableable components | Bitset per block and per marked type | `Table` and masked iteration | Signature and archetypes |
| Hybrid storage | `TypeInfo::storage = Sparse`; the type stays out of the archetype signature | `World::add/remove`, `componentIndex_` and mixed queries | Existing tables |
| IDs stable across DLLs | `componentId<T>()` comes to use a hash of the type name, mapped to a dense index | Only `type_info` | Everything else |

### Risks

| Risk | Signal | Mitigation |
| --- | --- | --- |
| Fragmentation from tags and states | `stats()` shows many archetypes with few entities | `iterate_fragmented` benchmark from M5 on; steer frequent states toward fields or bitsets |
| Overhead of `componentIndex_` as `SmallMap` with many archetypes | Slow `has` and `get` in `random_get` | Switch to a dense `[cid][archId]` vector when `A × C` fits in memory |
| Exceptions in copy constructors used by `add(const T&)` | Inconsistent state after `throw` | Construct the temporary value **before** any structural change |
| Coalescing complexity (R-CB-3) | Bugs in the command buffer fuzz | Implement first without coalescing and compare both versions in the fuzz |
| Edge explosion in `SmallMap` | Lots of memory per archetype in worlds with many types | Measure in M5; a global edge map is the alternative |

### Open questions

- Should `create<Ts...>()` require all `Ts` to be movable, or accept `const T&` with a copy?
- Should the query be identified by type (`query<Ts...>()` cached per template) or by a runtime builder? The specification assumes the first model; the second is needed for scripts.
- Direct `destroy` of an entity that has commands in a pending buffer: does the flush detect the stale handle by generation and ignore the commands, or does the `World` cancel the pending ones at `destroy` time? The specification does not yet cover the case; the first option is the cheapest.
- `get<T>` for a tag: compile error (as specified) or return a static `T&`?
