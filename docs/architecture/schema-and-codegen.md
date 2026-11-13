# Schema, oly-schema and each game's WASM module

## The `.oly` schema

The game declares everything in a `<game>.oly` file, the single source of the data layout for every environment and language:

```
# game.oly
component Position { x: f32, y: f32 }
tag Enemy
event EnemyDied { entity: entity, x: f32, y: f32 }
action move: axis2d
state playing
```

- Scalar types: `f32 f64 i8 u8 i16 u16 i32 u32 entity`. There are no 64-bit integers (they would require `BigInt`).
- Fields in declaration order, natural alignment. A component becomes a trivial struct; the column stores structs (SoA across components, AoS inside the component).
- The grammar, the naming rules, the limits and the test corpus are in `engine/schema/format/README.md`.

## `oly-schema`

`oly-schema` (`engine/schema/codegen/`) is a C++ executable: `oly-schema <file.oly> --emit cpp=<dir> --emit ts=<file>`.

- It parses, validates, lays out and hashes the schema once. Every client gets the same layout and the same hash, because no client computes them.
- An emitter turns the schema into the code of one language. The C++ emitter is part of the core (C++ is the engine language, not a client). A client's emitter lives in its environment folder and registers itself into `oly-schema` when linked; the TS emitter is `engine/environments/web/clients/typescript/codegen/`. Adding a language is one folder and one `add_subdirectory`; the generator core does not change.
- Errors report `<file>:<line>:<column>: <message>` and the build fails without writing any file.

## The build flow

```
<game>.oly ──► oly-schema ─┬─ cpp ──► schema.generated.hpp + schema_registry.generated.cpp
                           │            │
                           │   the game's C++ systems (OLY_SYSTEM, OLY_HANDLER) ──┤
                           │   oly::capi + app + input + ecs + core + oly::web_host ┴──► emcc ──► <game>_game.mjs + .wasm
                           └─ ts ───► web/schema.generated.ts (imports @oly/core/generated)
```

- `oly_add_game` (`cmake/OlyGame.cmake`) runs `oly-schema` on the game's `.oly` and puts it all together. The WASM is compiled **per game**.
- The generated C++ has `static_assert`s on `sizeof`, `alignof` and `offsetof` ("diverges from oly-schema"), and `actions::` and `states::` namespaces with the ids. A wrong name in a native system is a compile error.
- **Schema hash**: 32-bit FNV-1a of a canonical form (components, tags, events, actions and states). The form did not change when the schema moved to `.oly`, so the hash of an existing schema is the same. `loadRuntime` checks the hash and the sizes and throws `SchemaMismatchError` if the WASM was generated with another schema.
- **Where `oly-schema` is built**: native presets build it with the tree. A WASM build cannot run a tool it compiles to WASM, so the `wasm` image builds it for the machine with the `schema-tool` preset (`make schema-tool`, which `make ts` runs first). `oly_add_game` runs the executable named by the cache variable `OLY_SCHEMA_TOOL` (default `build/schema-tool/bin/oly-schema`).
- `make ts` generates `games/<game>/web/schema.generated.ts` from every game's `.oly` before `tsc`. `*.generated.ts` is never committed and never edited.
- Link: `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web`, growable memory. The output goes to `build/<preset>/games/<game>_game.{mjs,wasm}`.

See also: [bridge.md](bridge.md) (how the `OlyEngine` declares the schema on creation) and [ts-packages.md](ts-packages.md) (the TS side: `@oly/core/generated`).
