# 3. World and entities

[← Schema](02-schema.md) · [Guide index](README.md) · Next: [Queries →](04-queries.md)

The `World` holds all entities and their components. An **entity** is only an id. What it *is* comes from the components it has.

## Where the World comes from

| Situation | How you get a World | Who disposes it |
|---|---|---|
| a game with a loop | `runtime.createEngine()` → `engine.world` | `engine.dispose()` |
| ECS only, no loop (tools, tests, benchmarks) | `runtime.createWorld()` | you: `world.dispose()` |
| inside a system, handler, task or hook | use the `context` methods | — |

```ts
const runtime = await loadRuntime(factory, schema);

const world = runtime.createWorld();
// ... use it
world.dispose();
```

## Create entities

`spawn` takes the list of types and, optionally, the initial values. Missing fields start at `0`.

```ts
const player = world.spawn([Player, Position, Velocity, Health], {
  Position: { x: 100, y: 50 },
  Health: { value: 3 },
});

const marker = world.spawn([]);                  // an empty entity
const rock = world.spawn([Position]);           // Position = { x: 0, y: 0 }
```

Tags go in the list and take no values. The entity is born directly in its final archetype, with no intermediate moves.

C++:

```cpp
oly::ecs::World world;
oly::ecs::Entity player = world.spawn(Player{}, Position{100, 50}, Velocity{0, 0}, Health{3});
oly::ecs::Entity marker = world.spawn();
```

## Read and write values

```ts
const health = world.get(player, Health);        // a copy: { value: 3 }
world.set(player, Health, { value: health.value - 1 });
world.set(player, Position, { x: 0 });           // partial: y is kept
```

`get` returns a **copy**. Changing it does not change the world. Use `set` to write.

`get` and `set` throw `ContractError` if the entity is dead or does not have the component. When the component may be missing, check first:

```ts
if (world.has(player, Health)) {
  world.set(player, Health, { value: 10 });
}
```

C++ returns references instead of copies:

```cpp
Health& health = world.get<Health>(player);       // asserts if missing
health.value -= 1;

if (Shield* shield = world.tryGet<Shield>(player)) {   // nullptr if missing or dead
  shield->charge = 0;
}
```

## Add and remove components

Adding or removing a component **moves** the entity to another archetype. This is a structural change.

```ts
world.add(enemy, Frozen);                        // a tag
world.add(enemy, Slow, { factor: 0.5 });         // a component with a value
world.add(enemy, Slow, { factor: 0.25 });        // already has it: the value is replaced
world.remove(enemy, Frozen);
world.remove(enemy, Frozen);                     // no effect: it does not have it
```

```cpp
world.add(enemy, Frozen{});
world.add(enemy, Slow{0.5f});
world.remove<Frozen>(enemy);
```

## Destroy and check

```ts
world.destroy(enemy);
world.isAlive(enemy);   // false
world.destroy(enemy);   // ContractError: dead entity
```

## Entity handles

An `Entity` is an index plus a **generation**. When an entity dies, its index is reused later with a new generation. An old handle never points to the new entity: `isAlive` returns `false` for it.

In TS, an `Entity` is a plain `number`. You can store it in components (`entity` fields), in `Map`s and `Set`s, and compare it with `===`.

```ts
import { formatEntity, NullEntity } from "@oly/core";

console.log(formatEntity(player));   // "12:0" (index:generation)

let target = NullEntity;             // "no entity"
```

### Keep references between entities

Store the other entity's handle in a component and check it each time you use it.

```ts
const Follower = component("Follower", { leader: "entity", distance: "f32" });

// later, in a system
const leader = follower.leader.get(row);
if (!context.isAlive(leader)) {
  context.destroy(block.entity(row));     // the leader died: remove the follower too
}
```

```cpp
if (const Position* leaderPosition = context.tryGet<Position>(follower.leader)) {
  // the leader is alive and still has a Position
}
```

## What stays valid

Only the `Entity` handle is stable. **Any** structural change (spawn, destroy, add, remove) can move rows in memory, so:

- in C++, references and pointers from `get`, `tryGet` and queries become invalid after a structural change;
- in TS, `get` returns copies, so there is nothing to invalidate; views from queries are only valid inside their callback ([Queries](04-queries.md)).

## Structural changes and the loop

Inside the loop the rules depend on **where** your code runs:

| Code | `spawn`/`destroy`/`add`/`remove` | Why |
|---|---|---|
| a system (`SystemContext`) | **deferred** to the end of the phase | a system may be iterating a query |
| a handler, task or state hook | **direct** | they run outside any iteration |
| `engine.world` during `frame` | **refused** (`ContractError`) | use the context instead |
| any code inside `eachBlock` | direct changes **refused** | the iteration would see moved rows |

Details in [Engine, loop and systems](05-engine-and-systems.md#structural-changes-in-systems).

## Without the engine: `CommandBuffer` (C++)

With a bare C++ `World`, record changes during an iteration and apply them after it:

```cpp
oly::ecs::CommandBuffer commands = world.commands();

world.query<const Health>().each([&](oly::ecs::Entity entity, const Health& health) {
  if (health.value <= 0) {
    commands.destroy(entity);
  }
});

world.flush(commands);   // one transition per entity, buffer left empty
```

The `CommandBuffer` merges commands per entity: `add` then `remove` of the same type cancel out, a repeated `add` keeps the last value, and `destroy` drops the earlier commands of that entity. `spawn()` returns the handle at once, so you can add components to it in the same buffer:

```cpp
oly::ecs::Entity bullet = commands.spawn();
commands.add(bullet, Position{10, 10});
commands.add(bullet, Bullet{});
```

The engine uses one `CommandBuffer` per phase for you, so in game code you rarely need your own.

## Debugging the world

```ts
const problem = world.checkInvariants();   // null when the world is consistent
if (problem !== null) {
  throw new Error(problem);
}
```

```cpp
#include "oly/ecs/diagnostics.hpp"

std::string why;
if (!oly::ecs::checkInvariants(world, &why)) {
  // why explains the broken invariant
}
oly::ecs::WorldStats statistics = oly::ecs::stats(world);   // archetypes, live entities, memory per archetype
```

Call `checkInvariants` in tests after each step. It catches corruption at the step that caused it ([Testing your game](11-testing.md)).

## Next

[4. Queries →](04-queries.md): visit many entities at once.
