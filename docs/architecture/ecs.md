# `ecs`: the archetype ECS

Code in `engine/runtime/ecs/`. Spec: `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md`.

## Layers

```
component → entity → detail/table → detail/archetype → query / command_buffer → world
```

Each layer uses only the ones below it. Public headers: `world`, `entity`, `component`, `query`, `query_by_id`, `command_buffer` and `diagnostics`. Everything else lives in `detail/`: `Table`, `Archetype`, `EntityStore`, query cache, command plan and the test accessors.

| Layer | Responsibility |
|---|---|
| `component` | `componentId<T>()` gives dense per-process IDs. `TypeInfo` holds size, alignment, whether the type is trivial (`memcpy` path) or a tag (size 0, no column) and move and destroy pointers. |
| `entity` | `Entity` = 32-bit index + 32-bit generation. `EntityStore` holds the records and the free index list. It sits below the `World` so the `CommandBuffer` can reserve indices without knowing the `World`. |
| `detail/table` | SoA storage: a single `count` and `capacity` for all columns, each column aligned to 64 B, ×2 growth. It does not know about alive entities, records or queries. `pushUninit` leaves a dead row; `fillHole(r)` does a swap-remove and returns the displaced entity. |
| `detail/archetype` | Owns a `Table`. Holds `signature` (with the tags) and `columnTypes` (only components with data); mixing the two up is the classic source of a wrong column. Memoized `±ComponentId` edges carry the precomputed `sourceToDestination` column mapping. Archetypes are never removed, so pointers are stable. |
| `query` | `Query<Ts...>` cached by the `World`, with incremental matching. `With<...>` and `Without<...>` filters are part of the cache key. `each`, `eachBlock`, `single`, `trySingle`. |
| `query_by_id` | `QueryById`: the same query, type-erased through `ComponentId` lists (`read`, `write`, `with`, `without`). It is what `capi` and TS use. |
| `command_buffer` | Records deferred structural changes. `spawn()` reserves the index immediately. `flush` groups by entity and makes a single transition per entity to the final archetype. |
| `world` | Sole owner of the records and the only one that calls `fillHole`, so fixing the record of the displaced entity lives in one place. |
| `diagnostics` | Free functions: `checkInvariants`, `stats`, `signatureLookupCount`, `archetypeCount`. |

## `World` indices

- `records_` (via `EntityStore`): `Entity.index → (archetype, row, generation)`.
- `bySignature_`: signature → archetype. Only consulted on the first transition to an archetype.
- `componentIndex_[componentId]`: archetypes that have the component and its column. `has`, `get` and query creation use this index, without scanning every archetype.

The two indices only change in `findOrCreate`, which also registers the new archetype in the existing queries.

## Operation costs

| Operation | Cost |
|---|---|
| `get<T>(entity)` | 3 indirections: record, `componentIndex_`, column |
| `add`/`remove` with cached edge | O(k) (k = components of the entity), zero allocations and zero hash lookups |
| first transition to an archetype | O(k) + signature hash |
| `query<Ts...>()` | O(archetypes with the rarest component) the first time; O(1) after |
| `flush` | O(Σk), one transition per affected entity |

## Templates and `ById`

The templates in `world.hpp` are thin and delegate to non-template functions by `ComponentId` in `world.cpp` (`addById`, `removeById` etc.), which reduces compile time and binary size. The hot path (`get<T>`, `Query::each`, `eachBlock`) does **not** delegate: it stays inline, because an indirection there would affect the NF-P1 and NF-P2 targets ([performance.md](performance.md)).

## Iteration and structural changes

- `each` and `eachBlock` increment `iterating_` (through an `IterationGuard`). A direct structural change with `iterating_ > 0` is a contract violation.
- Changes during iteration go through the `CommandBuffer`.
- `QueryById::iterate()` returns a move-only RAII `QueryIteration`, with block descriptors from a pool per nesting level. There is no per-iteration allocation after warm-up.

Validity rules:

- Any structural change invalidates pointers, references and spans of **any** entity.
- Only the `Entity` is a stable handle. To keep a relationship, store the `Entity` and call `tryGet` when using it.

## `CommandBuffer`

- The buffer is its own block aligned to 64 B. When it grows, it relocates each payload through the `TypeInfo`, because copying the bytes of a non-trivial type (e.g. `std::string` with SSO) would be undefined behaviour.
- Conflict rules (R-CB-4): `add` followed by `remove` of the same type cancel out; a repeated `add` keeps the last value; `destroy` discards the entity's earlier commands; a command after `destroy` is a contract violation.
- A direct `destroy` of an entity with pending commands is resolved by the generation: flush sees the stale handle and ignores the commands.

## Correctness

- Invariants I-1 to I-11, the fuzz against a model and `LifeCounter` are in [build-and-tests.md](build-and-tests.md#tests).
- When changing structural logic, keep `checkInvariants` covering what changed, including the `QueryById` caches (which go into `checkQueries`).
- Test-only hooks (`flushSequential`, `setGeneration`, `CommandBuffer::forEach`) live in `detail/test_access.hpp` (`WorldTestAccess`, `EntityStoreTestAccess`, `CommandBufferTestAccess`). The public classes have nothing "for testing".
