# 2. Schema

[← Getting started](01-getting-started.md) · [Guide index](README.md) · Next: [World and entities →](03-world-and-entities.md)

The schema is the **single source** of your game's data. You write it once, in TypeScript. The build generates the matching C++ from it, so TS and C++ always agree on the memory layout.

## The five kinds of entries

| Function | Declares | Example |
|---|---|---|
| `component(Name, fields)` | data attached to an entity | `component("Health", { value: "i32" })` |
| `tag(Name)` | a marker with no data | `tag("Enemy")` |
| `event(Name, fields)` | a message sent between systems | `event("EnemyDied", { entity: "entity", x: "f32", y: "f32" })` |
| `action(name, kind)` | an input action (`"button"`, `"axis2d"`, `"pointer"`) | `action("shoot", "button")` |
| `state(name)` | a game state (menu, playing...) | `state("playing")` |

```ts
import { action, component, defineSchema, event, state, tag } from "@oly/core";

export const Position = component("Position", { x: "f32", y: "f32" });
export const Health = component("Health", { value: "i32" });
export const Weapon = component("Weapon", { interval: "f32", cooldown: "f32" });
export const Target = component("Target", { entity: "entity" });
export const Enemy = tag("Enemy");
export const Frozen = tag("Frozen");

export const EnemyDied = event("EnemyDied", { entity: "entity", x: "f32", y: "f32" });

export const move = action("move", "axis2d");
export const aim = action("aim", "pointer");
export const shoot = action("shoot", "button");

export const menu = state("menu");
export const playing = state("playing");

export default defineSchema([
  Position, Health, Weapon, Target, Enemy, Frozen,
  EnemyDied,
  move, aim, shoot,
  menu, playing,
]);
```

Export every entry. You pass these objects to every API call: `world.get(entity, Health)`, `context.emit(EnemyDied, ...)`, `actions.button(shoot)`. There are no string lookups at run time.

## Field types

| Type | C++ | Range / use |
|---|---|---|
| `f32` | `float` | positions, speeds, timers |
| `f64` | `double` | values that need precision (accumulated time, money) |
| `i8` `u8` | `int8_t` `uint8_t` | small counters, booleans (`0`/`1`) |
| `i16` `u16` | `int16_t` `uint16_t` | medium counters |
| `i32` `u32` | `int32_t` `uint32_t` | health, score, ids |
| `entity` | `oly::ecs::Entity` | a reference to another entity |

There are no 64-bit integers (they would need `BigInt` in JS) and no strings. Keep text, sprites and other assets on the TS side, indexed by a number in a component.

Booleans are a `u8` field with `0` or `1`:

```ts
export const Dash = component("Dash", { speed: "f32", duration: "f32", ready: "u8", active: "u8" });
```

## Naming rules

The names become C++ identifiers, so the schema checks them and throws `ContractError` when they break a rule.

| Entry | Rule | Good | Bad |
|---|---|---|---|
| component, tag, event | `PascalCase` | `EnemyDied` | `enemy_died` |
| field | `camelCase`, not a C++ keyword | `cooldown` | `Cooldown`, `class`, `int` |
| action, state | `camelCase`, not a C++ keyword | `gameOver` | `GameOver`, `default` |

Other rules:

- A component needs at least one field. With no data, use `tag`.
- An event needs at least one field.
- A name can appear only once in a schema (components, tags and events share one namespace).
- An entry belongs to one schema only. Define the schema once and import it.
- At most 32 states.

## Order matters

`defineSchema` gives each entry an index inside its group, in the order you pass them:

- **Events**: the index is the dispatch order. Earlier events are handled first in each round ([Events](06-events.md#order)).
- **Actions** and **states**: the index is the id used in C++ (`actions::shoot`, `states::playing`).

Changing the order, adding or removing an entry changes the **schema hash**. The WASM must be rebuilt.

## Memory layout

Each component becomes a plain C++ struct with natural alignment, in field order:

```ts
const Mixed = component("Mixed", { a: "u8", b: "f64", c: "i16" });
```

```cpp
// Generated: do not edit.
struct Mixed {
  uint8_t a;   // offset 0
  double b;    // offset 8 (7 bytes of padding before it)
  int16_t c;   // offset 16
};
static_assert(sizeof(Mixed) == 24 && alignof(Mixed) == 8, "schema: layout of Mixed diverges from TS");
```

Tip: order fields from the largest to the smallest type to avoid padding. `{ b: "f64", c: "i16", a: "u8" }` takes 16 bytes instead of 24.

Each archetype stores one column per component (SoA across components). Inside a column, each element is the whole struct (AoS inside the component). So split data that is read by different systems into different components: a system that reads only `Position` does not pull `Health` into the cache.

## What the build generates

From the schema above, `schema.generated.hpp` contains:

```cpp
struct Position { float x; float y; };
struct Health { int32_t value; };
struct Target { oly::ecs::Entity entity; };
struct Enemy {};
struct EnemyDied { oly::ecs::Entity entity; float x; float y; };

namespace actions {
  inline constexpr oly::input::Axis2dAction move{0};
  inline constexpr oly::input::PointerAction aim{1};
  inline constexpr oly::input::ButtonAction shoot{2};
}

namespace states {
  inline constexpr oly::app::StateId menu{0};
  inline constexpr oly::app::StateId playing{1};
}
```

A typo such as `actions::shoto` in C++ is a compile error.

## The schema check at start-up

```ts
import { loadRuntime, SchemaMismatchError } from "@oly/core";

try {
  const runtime = await loadRuntime(factory, schema);
} catch (error) {
  if (error instanceof SchemaMismatchError) {
    console.error("rebuild the WASM:", error.message);
  }
  throw error;
}
```

`loadRuntime` compares the hash, the number of entries and the size and alignment of each type. The message names the first type that differs.

## Using the schema in several places

The schema module has no side effects beyond creating objects. Import it from the game, from the shell and from tests. Do not call `defineSchema` twice with the same entries: an entry that already belongs to a schema is refused.

## Next

[3. World and entities →](03-world-and-entities.md): create entities from these types.
