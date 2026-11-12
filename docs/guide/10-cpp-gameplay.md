# 10. C++ gameplay

[← States](09-states.md) · [Guide index](README.md) · Next: [Testing your game →](11-testing.md)

C++ runs inside the WASM module, next to the ECS data. A C++ system touches components with plain references, with no boundary crossing per entity. Put the per-entity, per-step work here: movement, AI, collision, weapons.

## Where the code goes

Every `.cpp` in `games/<game>/native/` is compiled into the game's WASM. Include the engine headers you use and the generated schema:

```cpp
#include "oly/app/handler.hpp"        // OLY_HANDLER
#include "oly/app/state_context.hpp"  // OLY_STATE_HOOK (StateContext)
#include "oly/app/system.hpp"         // OLY_SYSTEM
#include "oly/app/task_context.hpp"   // TaskContext, payloads
#include "oly/ecs/query.hpp"          // With, Without
#include "schema.generated.hpp"       // your components, events, actions:: and states::
```

## The three macros

| Macro | Defines | Register from TS with |
|---|---|---|
| `OLY_SYSTEM(name)` | `void name(oly::app::SystemContext& context)` | `engine.addNativeSystem(phase, "name")` |
| `OLY_HANDLER(name, Event)` | `void name(HandlerContext& context, std::span<const Event> events)` | `engine.addNativeHandler(Event, "name")` |
| `OLY_STATE_HOOK(name)` | `void name(oly::app::StateContext& context)` | `engine.addNativeOnEnter(state, "name")` / `addNativeOnExit` |

The macros register the function by name when the module loads. TS looks the name up once, at registration. A wrong name throws `ContractError` right there, not in the middle of the game.

```cpp
OLY_SYSTEM(integrate) {
  const float deltaTime = context.fixedDeltaTime();

  context.query<Position, const Velocity>().each([deltaTime](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}
```

```ts
engine.addNativeSystem(Phase.Update, "integrate").inState(playing);
```

## The context in C++

The C++ contexts have the same members as the TS ones, with C++ types.

| Need | Call |
|---|---|
| query | `context.query<Position, const Velocity>(oly::ecs::With<Enemy>{})` |
| read / write one component | `context.get<Health>(entity)` (reference), `context.tryGet<Health>(entity)` (pointer or `nullptr`) |
| check | `context.has<Frozen>(entity)`, `context.isAlive(entity)` |
| create | `context.spawn(Bullet{}, Position{x, y}, Velocity{vx, vy})` |
| change structure | `context.add(entity, Frozen{})`, `context.remove<Frozen>(entity)`, `context.destroy(entity)` |
| events | `context.emit(EnemyDied{entity, x, y})` |
| input | `context.actions().button(actions::shoot).held` |
| time | `context.fixedDeltaTime()`, `context.time()` |
| schedule | `context.schedule().after(seconds, function, payload)` |
| state | `context.state() == states::playing`, `context.requestState(states::gameOver)` |
| pause | `context.pause()`, `context.resume()`, `context.paused()` |

In a `SystemContext`, structural changes are deferred to the end of the phase. In handlers, tasks and hooks they are direct. The rules are the same as in TS ([Engine, loop and systems](05-engine-and-systems.md#structural-changes-in-systems)).

## Examples

### Steer the player from input

```cpp
OLY_SYSTEM(steerPlayer) {
  auto [player, velocity] = context.query<Velocity>(oly::ecs::With<Player>{}).single();
  const oly::input::Vector2 direction = context.actions().axis2d(actions::move);

  velocity = Velocity{direction.x * 220.0f, direction.y * 220.0f};
}
```

### Shoot with a cooldown

```cpp
OLY_SYSTEM(fireWeapon) {
  auto [player, position, weapon] = context.query<const Position, Weapon>(oly::ecs::With<Player>{}).single();
  weapon.cooldown = std::max(0.0f, weapon.cooldown - context.fixedDeltaTime());
  if (!context.actions().button(actions::shoot).held || weapon.cooldown > 0.0f) {
    return;
  }

  const oly::input::PointerState aim = context.actions().pointer(actions::aim);
  if (!aim.valid) {
    return;
  }

  const float deltaX = aim.x - position.x;
  const float deltaY = aim.y - position.y;
  const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
  if (length == 0.0f) {
    return;
  }

  weapon.cooldown = weapon.interval;
  context.spawn(Bullet{}, Position{position.x, position.y}, Velocity{deltaX / length * 600.0f, deltaY / length * 600.0f});
}
```

### Nested queries

For "each bullet against each enemy", take the inner query reference once, then nest the loops. Changes go through the context, so they are safe inside both loops.

```cpp
OLY_SYSTEM(hitEnemies) {
  auto& enemies = context.query<const Position, const Collider, Health>(oly::ecs::With<Enemy>{});

  context.query<const Position, const Collider>(oly::ecs::With<Bullet>{}).each([&](oly::ecs::Entity bullet, const Position& bulletPosition, const Collider& bulletCollider) {
    bool hit = false;
    enemies.each([&](oly::ecs::Entity enemy, const Position& enemyPosition, const Collider& enemyCollider, Health& health) {
      if (hit || !touches(bulletPosition, bulletCollider, enemyPosition, enemyCollider)) {
        return;
      }

      hit = true;
      health.value -= 1;
      context.destroy(bullet);
      if (health.value <= 0) {
        context.emit(EnemyDied{enemy, enemyPosition.x, enemyPosition.y});
      }
    });
  });
}
```

This is a brute-force O(bullets × enemies) check. It is fine for hundreds of entities. For thousands, add a spatial grid in C++.

### Read one value from a single entity, then iterate

`single` returns references that are valid until the next structural change. Copy the value when you only need it for reading:

```cpp
OLY_SYSTEM(chasePlayer) {
  const Position target = std::get<1>(context.query<const Position>(oly::ecs::With<Player>{}).single());

  context.query<const Position, const Chaser, Velocity>(oly::ecs::With<Enemy>{}).each([target](const Position& position, const Chaser& chaser, Velocity& velocity) {
    // steer toward target
  });
}
```

### A timer with a payload

```cpp
struct PlayerTarget {
  oly::ecs::Entity player;
};

void endDash(oly::app::TaskContext& context) {
  const oly::ecs::Entity player = context.payload<PlayerTarget>().player;
  if (context.isAlive(player)) {
    context.get<Dash>(player).active = 0;
  }
}

// inside a system
context.schedule().after(dash.duration, endDash, PlayerTarget{player});
```

More in [Schedule](08-schedule.md#c-tasks-and-payloads).

## Randomness and shared constants

- **Deterministic randomness**: use `std::minstd_rand` with a fixed seed. Its sequence is the same in every standard library; the `std::*_distribution` classes are not, so use `generator() % 100` or your own mapping.
- **Shared constants**: values used by both languages (world size, bullet speed) live in a C++ header (`native/constants.hpp`) and a TS module (`src/constants.ts`). Keep them side by side and say in each file that the other one must change with it.

## How to split work between TS and C++

| Put it in C++ | Put it in TS |
|---|---|
| runs for every entity, every step | runs once per step, or rarely |
| movement, collision, AI, weapons | render, HUD, menus |
| tight math | score, waves, game rules that change often |
| | bindings, state hooks that talk to the page |

Both can live in the same phase, in the order you register them.

## A C++-only game

Without TS, you create the `World` and the `Engine` yourself and declare events, actions and states in C++. Systems are function pointers with an optional `userData` pointer.

```cpp
#include "oly/app/engine.hpp"
#include "oly/app/system.hpp"
#include "oly/ecs/world.hpp"

struct Position { float x, y; };
struct Velocity { float x, y; };
struct Hit { oly::ecs::Entity target; };

struct Game {
  oly::input::ButtonAction jump;
  uint32_t hits = 0;
};

void integrate(oly::app::SystemContext& context) {
  const float deltaTime = context.fixedDeltaTime();

  context.query<Position, const Velocity>().each([deltaTime](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}

void jumpOnPress(oly::app::SystemContext& context) {
  const Game& game = *static_cast<const Game*>(context.userData());
  if (context.actions().button(game.jump).pressed) {
    // ...
  }
}

void countHits(oly::app::HandlerContext& context) {
  static_cast<Game*>(context.userData())->hits += context.eventCount();
}

int main() {
  oly::ecs::World world;
  oly::app::Engine engine(world, oly::app::EngineConfig{.fixedHz = 60.0});
  Game game;

  engine.declareEvent<Hit>("Hit");
  const oly::app::StateId playing = engine.declareState("playing");
  game.jump = engine.actions().declareButton("jump");
  engine.actions().bind(game.jump, oly::input::Key::Space);

  engine.addSystem(oly::app::Phase::Update, "integrate", integrate);
  const oly::app::SystemId jumpSystem = engine.addSystem(oly::app::Phase::UpdateBefore, "jumpOnPress", jumpOnPress, &game);
  engine.setSystemStates(jumpSystem, {playing});
  engine.on<Hit>("countHits", countHits, &game);

  world.spawn(Position{0, 0}, Velocity{1, 0});
  engine.requestState(playing);

  for (int step = 0; step <= 600; ++step) {
    engine.frame(step / 60.0);   // your host calls frame, setHidden and the input functions
  }
}
```

Inside a handler registered with `on<T>`, read the batch with `context.events<T>()`, which returns a `std::span<const T>`. The macros (`OLY_SYSTEM`...) are only needed when TS registers by name.

Today there is no native window or renderer: a C++-only host is a test, a tool or a benchmark. The engine is ready for one: a host only calls `frame`, `setHidden` and the input functions (`buttonDown`, `buttonUp`, `pointerMove`, `releaseAll`).

## Advanced: your own exported C functions

A game can export extra `extern "C"` functions with `OLY_API`. From an `OlyWorld*` (the C API handle), `oly::bridge::worldOf(world)` gives the `oly::ecs::World&` behind it, and `oly::bridge::engineOf(engine)` gives the `Engine&`. The engine's test fixture (`engine/testing/fixture/systems.cpp`) shows this. Calling such functions from TS needs the module internals (`@oly/core/internal`), which are reserved for the engine; in game code, prefer `OLY_SYSTEM`, `OLY_HANDLER` and `OLY_STATE_HOOK`.

## Next

[11. Testing your game →](11-testing.md): run all of this in Node, step by step.
