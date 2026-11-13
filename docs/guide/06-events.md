# 6. Events

[← Engine, loop and systems](05-engine-and-systems.md) · [Guide index](README.md) · Next: [Input →](07-input.md)

An event is a small message: "this enemy died here", "the player picked up this coin". The system that sees it happen **emits** it. Any number of **handlers** react to it later in the same step. The emitter does not know who listens.

Use events when one thing must trigger several independent reactions: an enemy death drops a coin, adds score, plays a sound and destroys the enemy.

## Declare, emit, handle

```
# game.oly
event EnemyDied { entity: entity, x: f32, y: f32 }
```

```ts
// a system emits
if (health.value.get(row) <= 0) {
  context.emit(EnemyDied, { entity: block.entity(row), x: position.x.get(row), y: position.y.get(row) });
}
```

```ts
// a handler reacts
engine.on(EnemyDied, "countKill", (context, batch) => {
  score.kills += batch.count;
});
```

Missing fields in `emit` are `0`.

## Handlers receive batches

A handler is called **once per batch**, not once per event. The batch holds every event of that type emitted since the last round, in emission order.

```ts
engine.on(EnemyDied, "spawnCorpses", (context, batch) => {
  const events = batch.read();

  for (let index = 0; index < batch.count; ++index) {
    context.spawn([Corpse, Position], { Position: { x: events.x.get(index), y: events.y.get(index) } });
  }
});
```

`batch.read()` returns views over the event queue in WASM memory, with no copy. Like query views, they are valid only during the handler call.

## Handlers change the world directly

Handlers run **after** the phases, outside any iteration. So their `spawn`, `destroy`, `add` and `remove` happen **at once** (a `HandlerContext` is a direct context).

Because of that, the entity named in an event may already be dead: another handler may have destroyed it, or two bullets may have reported the same death. Always check:

```ts
engine.on(CoinCollected, "collectCoin", (context, batch) => {
  const coins = batch.read().coin;

  for (let index = 0; index < batch.count; ++index) {
    const coin = coins.get(index);
    if (context.isAlive(coin)) {
      context.destroy(coin);
      score.coins += 1;
    }
  }
});
```

## The engine does not remove duplicates

If two bullets hit the same enemy in one step, `EnemyDied` arrives twice. Decide in the handler what a duplicate means:

```ts
engine.on(EnemyDied, "countKill", (_context, batch) => {
  const enemies = batch.read().entity;
  const counted = new Set<Entity>();

  for (let index = 0; index < batch.count; ++index) {
    const enemy = enemies.get(index);
    if (!counted.has(enemy)) {
      counted.add(enemy);
      score.kills += 1;
    }
  }
});
```

## Order

- **Between types**: the order of the events in the schema. Every round handles the first event type, then the second, and so on.
- **Between handlers of one type**: registration order. C++ and TS handlers mix.

### Put the destroying handler last

Register the handlers that **read** the entity before the one that **destroys** it:

```ts
engine.addNativeHandler(EnemyDied, "dropCoin");                          // reads the position
engine.on(EnemyDied, "countKill", (_context, batch) => countKill(score, batch));
engine.addNativeHandler(EnemyDied, "destroyDead");                       // destroys: last
```

## Rounds: events that cause events

What a handler emits goes to the **next round of the same step**. Rounds repeat until no event is pending.

```ts
engine.on(EnemyDied, "chainExplosion", (context, batch) => {
  // nearby enemies take damage; some of them die and emit EnemyDied again (next round)
});
```

The number of rounds per step is limited by `maxEventRounds` (default 16). Reaching it means an event loop (A emits B, B emits A): a contract error in debug builds. Raise the limit only if your chain is legitimately deep.

## When events are dispatched

| Emitted from | Dispatched |
|---|---|
| a system (any gameplay phase) | at step 4 of the same step, after all phases |
| a handler | next round, same step |
| a task or a state hook | in the rounds of the same step |
| Render or outside `frame` | in the next step |

An event nobody handles is discarded at the end of the step.

## Restrict handlers to states

```ts
engine.on(PlayerDied, "endRound", (context) => context.requestState(gameOver)).inState(playing);
```

With an active state that is not in the list, the handler is skipped.

## C++ handlers

Declare the handler in C++ with `OLY_HANDLER(name, EventType)` and register it from TS by name. `events` is a `std::span<const EventType>`.

```cpp
#include "oly/app/handler.hpp"
#include "schema.generated.hpp"

OLY_HANDLER(destroyDead, EnemyDied) {
  for (const EnemyDied& died : events) {
    if (context.isAlive(died.entity)) {
      context.destroy(died.entity);
    }
  }
}
```

```ts
engine.addNativeHandler(EnemyDied, "destroyDead");
```

Registering a name that does not exist, or a handler declared for a different event type, throws `ContractError`.

C++ systems emit with the generated struct:

```cpp
context.emit(EnemyDied{enemy, position.x, position.y});
```

## Events vs. other tools

| You want | Use |
|---|---|
| several reactions to one fact, now | an event |
| something to happen after N seconds | the [schedule](08-schedule.md) |
| to keep data on an entity across steps | a component |
| to mark entities for another system to process | a tag (`add(entity, Hit)`), then a query `with: [Hit]` |
| to change screens | a [state](09-states.md) |

## Next

[7. Input →](07-input.md): turn keys and mouse into actions.
