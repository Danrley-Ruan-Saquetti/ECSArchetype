# 12. Performance

[← Testing your game](11-testing.md) · [Guide index](README.md) · Next: [Troubleshooting →](13-troubleshooting.md)

Oly is built around one cost model: **data in WASM memory is cheap to reach from C++, and cheap from TS only in batches**. Each call from TS into WASM (or back) has a fixed cost. Pay it per system, per table or per event batch — never per entity.

The measured numbers are in [performance](../architecture/performance.md) and [benchmarks](../benchmarks/README.md). This page turns them into rules.

## The rules

### 1. Iterate with `eachBlock`, not with `get` per entity

```ts
// slow: two boundary crossings and one object per entity
for (const enemy of enemies) {
  const position = world.get(enemy, Position);
  world.set(enemy, Position, { x: position.x + 1 });
}

// fast: one crossing per table, no objects
world.query({ write: [Position], with: [Enemy] }).eachBlock((block) => {
  const position = block.write(Position);
  for (let row = 0; row < block.count; ++row) {
    position.x.set(row, position.x.get(row) + 1);
  }
});
```

`get`, `set`, `single` and `trySingle` are for **one** entity: the player, the boss, the camera.

### 2. Move per-entity work to C++

A TS system costs at least one crossing per call and runs in the JS engine. A C++ system runs next to the data, can be inlined and vectorized by the compiler. Movement, collision, AI and weapons belong in C++ once the entity count grows ([C++ gameplay](10-cpp-gameplay.md)).

### 3. In hot TS loops, use `raw()`

`field.raw()` gives the typed array, base and stride. One array access per element, no method call:

```ts
const x = block.write(Position).x.raw();
for (let row = 0; row < block.count; ++row) {
  x.array[x.base + row * x.stride] += 1;
}
```

Take raw arrays inside the block and do not call anything that can allocate in WASM (spawn, add, emit) while you use them: if WASM memory grows, the old array is detached.

### 4. Handle events in batches

Handlers already receive a whole batch per call. Do the loop inside the handler. Emitting thousands of events is fine; the queue is a byte buffer.

### 5. Avoid archetype churn

Every `add` or `remove` moves the entity to another table: it copies its components. Doing it every step to many entities costs more than a field write.

| Need | Cheap | Expensive if it changes every step |
|---|---|---|
| a state that toggles often (`active`, `ready`) | a `u8` field | a tag added and removed |
| a state that changes rarely (`Frozen`, `Invulnerable` for 1 s) | a tag (queries can filter it for free) | — |

For example, a `Dash` component keeps `ready` and `active` as `u8` fields, because they flip on every use; `Invulnerable` can be a tag, because it changes once per hit.

The first move to a new combination of components creates an archetype and a cached edge. After that, the same move costs O(components) with no allocation.

### 6. Split components by who reads them

A system that reads `Position` loads only the `Position` column into the cache. If `Position` also carried `health` and `name`, every movement step would drag them along. Small components read together go together; data read by different systems goes apart.

### 7. Reuse queries — or don't worry about it

`world.query(spec)` is cached by its spec. Calling it inside a system every step is a map lookup, not a new query. Still, in very hot TS code you can keep the `Query` object in a variable.

### 8. Watch the fragmentation

Many small tables (one entity per combination) make `eachBlock` call your function many times with `count` 1. Prefer a few fixed combinations per kind of entity. In C++, `oly::ecs::stats(world)` shows the archetype count and a histogram of table sizes.

### 9. Keep render cheap

Render runs once per display frame, which may be 144 times per second. Draw with batched queries and avoid allocating objects per entity there.

## Measuring

- Compare against a baseline in the **same run**: the environment is noisy (WSL2). The engine's benchmarks report ratios, not absolute times.
- In the browser, use the Chromium profiler on the running game.
- For the engine's own benchmarks and the WASM boundary benchmark, see [commands](../development/commands.md#ecs-benchmarks).

## Debug vs. release

| Build | Asserts | Use |
|---|---|---|
| `wasm-games` | on | development: C++ contract violations stop with a message |
| `wasm-games-release` | off | shipping and measuring |

In release, the TS layer still validates everything that goes through it and throws `ContractError`. A violation inside your **C++** code is not checked and is undefined behaviour. Run your tests in the debug preset.

## Next

[13. Troubleshooting →](13-troubleshooting.md): what each error means.
