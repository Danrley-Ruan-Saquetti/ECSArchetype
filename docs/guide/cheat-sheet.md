# Cheat sheet

[Guide index](README.md) · [Recipes](recipes.md) · [Glossary (C++ / C API / TS)](../architecture/glossary.md)

One page with every public name. TS unless marked C++.

## Start-up

```ts
import { loadRuntime, Phase } from "@oly/core";
import { attachBrowserInput, moduleFactoryFromUrl, runInBrowser } from "@oly/web-host";

const runtime = await loadRuntime(moduleFactoryFromUrl("./game_game.mjs"), schema);   // SchemaMismatchError
const engine = runtime.createEngine({ fixedHz: 60, maxStepsPerFrame: 5, maxEventRounds: 16 });
const world = runtime.createWorld();          // a World without engine; world.dispose()
attachBrowserInput(engine, canvas);           // .detach()
runInBrowser(engine);                         // .stop()
engine.dispose();
```

## Schema

| Call | Notes |
|---|---|
| `component("Name", { field: "f32" })` | types: `f32 f64 i8 u8 i16 u16 i32 u32 entity` |
| `tag("Name")` | no data |
| `event("Name", { field: "u32" })` | at least one field; order = dispatch order |
| `action("name", "button" \| "axis2d" \| "pointer")` | |
| `state("name")` | at most 32 |
| `defineSchema([...])` | `export default` it |

## World (and contexts)

| Call | Returns / effect |
|---|---|
| `spawn([Types...], { Type: { field: value } })` | `Entity` |
| `destroy(entity)` | |
| `isAlive(entity)` | `boolean` |
| `has(entity, Type)` | `boolean` |
| `add(entity, Type, values?)` | replaces the value if present |
| `remove(entity, Type)` | no effect if absent |
| `get(entity, Type)` | a **copy** `{ field: number }` |
| `set(entity, Type, partialValues)` | immediate, not structural |
| `query(spec)` | cached `Query` |
| `checkInvariants()` | `string \| null` (World only) |

Helpers: `NullEntity`, `formatEntity(entity)`, `entityIndex(entity)`, `entityGeneration(entity)`.

## Queries

```ts
const query = context.query({ read: [A], write: [B], with: [TagOrType], without: [TagOrType] });

query.eachBlock((block) => {
  block.count;
  block.entity(row);
  block.read(A).field.get(row);
  block.write(B).field.set(row, value);
  block.read(A).field.raw();             // { array, base, stride }
});

query.single();      // SingleRow: .entity, .get(Type) — ContractError unless exactly 1
query.trySingle();   // SingleRow | null — ContractError if 2+
```

## Engine

| Member | Notes |
|---|---|
| `frame(nowSeconds)` | not reentrant, time cannot go back |
| `setHidden(boolean)` | `runInBrowser` calls it |
| `pause()`, `resume()`, `paused` | effect from the next step |
| `time`, `fixedDeltaTime` | seconds |
| `world`, `actions`, `schedule` | |
| `addSystem(phase, name, fn)` | → `.whilePaused()`, `.inState(...states)` |
| `addNativeSystem(phase, name)` | `OLY_SYSTEM(name)` in C++ |
| `on(Event, name, (context, batch) => {})` | → `.inState(...)` |
| `addNativeHandler(Event, name)` | `OLY_HANDLER(name, Event)` in C++ |
| `requestState(state)`, `state` | `state` is `null` until the first change |
| `onEnter(state, name, fn)`, `onExit(state, name, fn)` | |
| `addNativeOnEnter(state, name)`, `addNativeOnExit(state, name)` | `OLY_STATE_HOOK(name)` in C++ |

Phases: `Phase.UpdateBefore`, `Phase.Update`, `Phase.UpdateAfter`, `Phase.Render`.

Fixed step: capture → tasks → UpdateBefore → Update → UpdateAfter → events → state change. Render once per frame.

## Contexts

| | System | Handler | Task | State hook |
|---|---|---|---|---|
| structural changes | deferred to end of phase | direct | direct | direct |
| extra members | `phase`, `name`, `interpolation` | `eventType`, `name` (+ the batch argument) | `handle`, `lastRun` | `fromState`, `toState` |

All contexts: `time`, `fixedDeltaTime`, `paused`, `pause()`, `resume()`, `state`, `requestState()`, `actions`, `schedule`, `query`, `get`, `set`, `has`, `isAlive`, `emit`, `spawn`, `destroy`, `add`, `remove`, `persist`.

## Events

```ts
context.emit(Event, { field: value });                     // missing fields = 0
engine.on(Event, "name", (context, batch) => {
  const view = batch.read();
  for (let index = 0; index < batch.count; ++index) {
    view.field.get(index);
  }
});
```

## Input

```ts
engine.actions.bind(buttonAction, "KeyW");
engine.actions.bind(axisAction, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });
engine.actions.clearBindings(action);
engine.actions.isBound("KeyW");

context.actions.button(action);    // { pressed, held, released }
context.actions.axis2d(action);    // { x, y }, y down, diagonal normalized
context.actions.pointer(action);   // { x, y, valid }, canvas pixels

engine.actions.buttonDown("Space"); engine.actions.buttonUp("Space");
engine.actions.pointerMove(x, y); engine.actions.releaseAll();
```

## Schedule

```ts
const handle = context.schedule.after(seconds, (task) => {});
context.schedule.every(seconds, (task) => {});
context.schedule.cancel(handle);      // boolean
context.schedule.isPending(handle);   // boolean
context.schedule.persist(handle);     // survives state changes
```

## Scope

| Created with | Scoped to the active state? |
|---|---|
| `context.spawn` | yes |
| `schedule.after/every` | yes |
| `engine.world.spawn` | no |

`context.persist(entity)` and `schedule.persist(handle)` remove the scope.

## C++ quick reference

```cpp
OLY_SYSTEM(name) { /* context: oly::app::SystemContext& */ }
OLY_HANDLER(name, Event) { /* context, events: std::span<const Event> */ }
OLY_STATE_HOOK(name) { /* context: oly::app::StateContext& */ }

context.query<A, const B>(oly::ecs::With<Tag>{}, oly::ecs::Without<Other>{}).each([](A& a, const B& b) {});
context.query<A>().each([](oly::ecs::Entity entity, A& a) {});
context.query<A, const B>().eachBlock([](std::span<A> as, std::span<const B> bs) {});
auto [entity, a] = context.query<A>(oly::ecs::With<Player>{}).single();
auto row = context.query<A>().trySingle();               // std::optional<std::tuple<Entity, A&>>

context.get<A>(entity);  context.tryGet<A>(entity);  context.has<A>(entity);  context.isAlive(entity);
context.spawn(A{...}, Tag{});  context.add(entity, A{...});  context.remove<A>(entity);  context.destroy(entity);
context.emit(Event{...});
context.actions().button(actions::shoot).pressed;
context.schedule().after(seconds, function, Payload{...});   // payload: trivially copyable, ≤ 32 B
context.requestState(states::playing);
context.fixedDeltaTime();  context.time();  context.paused();
```

## Errors

`ContractError` (wrong use) · `SchemaMismatchError` (rebuild the WASM) · `OLY_ASSERT` in C++ debug builds. See [Troubleshooting](13-troubleshooting.md).
