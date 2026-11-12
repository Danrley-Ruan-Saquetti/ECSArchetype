# 4. Queries

[← World and entities](03-world-and-entities.md) · [Guide index](README.md) · Next: [Engine, loop and systems →](05-engine-and-systems.md)

A query visits every entity that has a given set of components. It is the main way to run gameplay over many entities.

## The query spec

```ts
world.query({
  read: [Velocity],          // components you read
  write: [Position],         // components you read and write
  with: [Enemy],             // must have (data not needed: tags or components)
  without: [Frozen],         // must not have
});
```

| Key | Gives you | Accepts tags? |
|---|---|---|
| `read` | read-only views | no |
| `write` | read-write views | no |
| `with` | only filtering | yes |
| `without` | only filtering | yes |

All keys are optional. `world.query({ with: [Enemy] })` visits every enemy without reading any data.

Queries are **cached**. Calling `world.query` with the same spec returns the same `Query` object, and the engine keeps its list of matching tables up to date as new archetypes appear. Calling it inside a system every step is cheap.

## Iterate with `eachBlock`

Entities with the same components live together in a **table** (one per archetype). `eachBlock` calls your function once per table, with all its rows.

```ts
world.query({ read: [Velocity], write: [Position] }).eachBlock((block) => {
  const velocity = block.read(Velocity);
  const position = block.write(Position);

  for (let row = 0; row < block.count; ++row) {
    position.x.set(row, position.x.get(row) + velocity.x.get(row));
    position.y.set(row, position.y.get(row) + velocity.y.get(row));
  }
});
```

| Block member | What it is |
|---|---|
| `block.count` | number of rows in this table |
| `block.entity(row)` | the entity of that row |
| `block.read(Type)` | view with `field.get(row)` |
| `block.write(Type)` | view with `field.get(row)` and `field.set(row, value)`; only for types in `write` |

The views read and write the WASM memory directly. There is **no copy** and no boundary crossing per entity.

### Rules inside `eachBlock`

- The block and its views are valid **only inside the callback**. Do not keep them for later.
- A direct structural change on the world (`world.spawn/destroy/add/remove`) throws `ContractError`. Inside a system, use `context.spawn/destroy/add/remove`: they are deferred and safe. Outside the loop, collect the entities first and change them after the loop.

```ts
const dead: Entity[] = [];

world.query({ read: [Health] }).eachBlock((block) => {
  const health = block.read(Health);
  for (let row = 0; row < block.count; ++row) {
    if (health.value.get(row) <= 0) {
      dead.push(block.entity(row));
    }
  }
});

for (const entity of dead) {
  world.destroy(entity);
}
```

## Exactly one: `single` and `trySingle`

Many games have one player, one camera or one boss. `single` reads it without a loop.

```ts
const player = world.query({ read: [Position, Health], with: [Player] }).single();

player.entity;                  // the Entity
player.get(Position);           // a copy: { x, y }
player.get(Health).value;
```

| Method | 0 rows | 1 row | 2+ rows |
|---|---|---|---|
| `single()` | `ContractError` | the row | `ContractError` |
| `trySingle()` | `null` | the row | `ContractError` |

The row is a **copy** taken at the moment of the call. To change it, write through the world or the context: `context.set(player.entity, Health, { value: 0 })`.

Use `trySingle` where the entity may not exist yet, for example in a HUD drawn while in the menu:

```ts
const player = context.query({ read: [Health], with: [Player] }).trySingle();
if (player === null) {
  return;
}

drawer.text(16, 16, `Health ${player.get(Health).value}`);
```

## Common query patterns

### Count entities

```ts
function count(world: World, spec: QuerySpec): number {
  let total = 0;
  world.query(spec).eachBlock((block) => {
    total += block.count;
  });

  return total;
}

count(world, { with: [Enemy] });
count(world, { with: [Enemy], without: [Frozen] });
```

### Find the nearest entity

```ts
function nearestEnemy(context: SystemContext, x: number, y: number): Entity | null {
  let nearest: Entity | null = null;
  let nearestDistance = Infinity;

  context.query({ read: [Position], with: [Enemy] }).eachBlock((block) => {
    const position = block.read(Position);
    for (let row = 0; row < block.count; ++row) {
      const deltaX = position.x.get(row) - x;
      const deltaY = position.y.get(row) - y;
      const distance = deltaX * deltaX + deltaY * deltaY;
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearest = block.entity(row);
      }
    }
  });

  return nearest;
}
```

### Two queries together (pairs)

Read the small set into local values first, then iterate the large set. Here, enemies chase the player:

```ts
const target = context.query({ read: [Position], with: [Player] }).single().get(Position);

context.query({ read: [Position, Chaser], write: [Velocity], with: [Enemy] }).eachBlock((block) => {
  const position = block.read(Position);
  const chaser = block.read(Chaser);
  const velocity = block.write(Velocity);
  for (let row = 0; row < block.count; ++row) {
    const deltaX = target.x - position.x.get(row);
    const deltaY = target.y - position.y.get(row);
    const length = Math.hypot(deltaX, deltaY) || 1;
    velocity.x.set(row, (deltaX / length) * chaser.speed.get(row));
    velocity.y.set(row, (deltaY / length) * chaser.speed.get(row));
  }
});
```

For "each against each" (bullets × enemies), prefer C++: see [C++ gameplay](10-cpp-gameplay.md#nested-queries).

### Several shapes with one function

Pass the filter as a parameter. One function draws coins, bullets, enemies and the player:

```ts
type Filter = Pick<QuerySpec, "with" | "without">;

function drawCircles(context: SystemContext, drawer: Drawer, filter: Filter, color: string): void {
  context.query({ read: [Position, Collider], ...filter }).eachBlock((block) => {
    const position = block.read(Position);
    const collider = block.read(Collider);
    for (let row = 0; row < block.count; ++row) {
      drawer.circle(position.x.get(row), position.y.get(row), collider.radius.get(row), color);
    }
  });
}

drawCircles(context, drawer, { with: [Enemy] }, "red");
drawCircles(context, drawer, { with: [Player], without: [Invulnerable] }, "blue");
```

## Raw arrays: the fastest TS path

`field.raw()` returns the typed array under a field, plus the base index and stride. Use it in tight loops to avoid one method call per element.

```ts
world.query({ read: [Velocity], write: [Position] }).eachBlock((block) => {
  const positionX = block.write(Position).x.raw();
  const velocityX = block.read(Velocity).x.raw();

  for (let row = 0; row < block.count; ++row) {
    positionX.array[positionX.base + row * positionX.stride] += velocityX.array[velocityX.base + row * velocityX.stride];
  }
});
```

A raw array is valid only until the next call that can allocate in the WASM (any spawn, add or engine call can grow the memory). Take it inside the block and use it right away. For `entity` fields, the array is `u32`: `base` points to the index and `base + 1` to the generation.

## C++ queries

C++ queries take the component types as template parameters. `const` means read-only.

```cpp
world.query<Position, const Velocity>(oly::ecs::With<Enemy>{}, oly::ecs::Without<Frozen>{})
    .each([](Position& position, const Velocity& velocity) {
      position.x += velocity.x;
      position.y += velocity.y;
    });

// with the entity
world.query<const Health>().each([](oly::ecs::Entity entity, const Health& health) {
  // ...
});

// one call per table, with spans: good for SIMD-friendly loops
world.query<Position, const Velocity>().eachBlock([](std::span<Position> positions, std::span<const Velocity> velocities) {
  for (size_t row = 0; row < positions.size(); ++row) {
    positions[row].x += velocities[row].x;
  }
});

// exactly one
auto [player, position, health] = world.query<const Position, Health>(oly::ecs::With<Player>{}).single();
health.value -= 1;

// zero or one
if (auto row = world.query<const Position>(oly::ecs::With<Boss>{}).trySingle()) {
  const Position& bossPosition = std::get<1>(*row);
}
```

C++ `single` returns **references** into the table, valid until the next structural change.

## Next

[5. Engine, loop and systems →](05-engine-and-systems.md): run queries every step inside systems.
