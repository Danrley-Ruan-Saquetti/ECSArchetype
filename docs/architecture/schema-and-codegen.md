# Schema, codegen and each game's WASM module

## The schema

The game declares everything in a `schema.ts`:

```ts
component("Position", { x: "f32", y: "f32" })
tag("Enemy")
event("EnemyDied", { entity: "entity", x: "f32", y: "f32" })
action("move", "axis2d")
state("playing")
```

- Scalar types: `f32 f64 i8 u8 i16 u16 i32 u32 entity`. There are no 64-bit integers (they would require `BigInt`).
- Fields in declaration order, natural alignment. A component becomes a trivial struct; the column stores structs (SoA across components, AoS inside the component).
- The layout is computed by `@oly/core` itself (`computeLayout`). The generator uses the same function. There is no generated TS file that could diverge.

## The build flow

```
schema.ts ──tsc──► schema.js ──oly-codegen──► C++ header (structs, ids, static_assert, hash)
                                                     │
the game's C++ systems (OLY_SYSTEM, OLY_HANDLER) ────┤
oly::capi + app + input + ecs + core + oly::web_host ┴──► emcc ──► <game>_game.mjs + .wasm
```

- `oly_add_game` (`cmake/OlyGame.cmake`) puts it all together. The WASM is compiled **per game**.
- The generated code has `static_assert`s on `sizeof`, `alignof` and `offsetof`, and `actions::` and `states::` namespaces with the ids. A wrong name in a native system is a compile error.
- **Schema hash**: 32-bit FNV-1a of a canonical form (components, tags, events, actions and states). `loadRuntime` checks the hash and the sizes and throws `SchemaMismatchError` if the WASM was generated with another schema.
- Link: `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web`, growable memory. The output goes to `build/<preset>/games/<game>_game.{mjs,wasm}`.

See also: [bridge.md](bridge.md) (how the `OlyEngine` declares the schema on creation) and [ts-packages.md](ts-packages.md#olycodegen-and-olytesting) (the generator package).
