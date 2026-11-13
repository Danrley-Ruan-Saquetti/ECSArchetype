# Oly architecture

This folder describes Oly as it is today: what each part does, how the parts connect and why the structure is this one. It does not tell the history of the project. The detailed requirements live in the specs ([Reference documents](#reference-documents)).

## Overview

Oly is a 2D game engine for the browser, written in C++20 and TypeScript. It is a study and benchmark project: every data representation decision has a measured cost.

The engine has three pieces:

- **C++ core**: the archetype ECS and all engine logic (fixed-step loop, phases, pause, events, input actions, schedule and states). It compiles to x86-64 (native tests) and to WebAssembly (games).
- **Bridge**: a C API (`oly*`) exported by the WASM module.
- **Thin TypeScript layer**: loads the WASM, forwards the browser's time and input to the core, registers TS systems and gives copy-free access to components.

Three rules guide the design:

1. **The engine is complete in C++.** A C++-only game uses every mechanism without depending on anything written in TS. TS has API conveniences, never engine behaviour.
2. **The WASM↔JS boundary is crossed per block, per batch or per system, never per entity** on hot paths.
3. **There is a single source for the data layout**: the game's `.oly` file. `oly-schema`, a C++ generator, produces the C++ and each client's code from it.

```
 game (TS and/or C++)                        games/<game>/
 ───────────────────────────────────────────────────────────────────────
 TS packages (thin layer)          │  C++ core (WASM or native)
   @oly/web-host: rAF, visibility, │    app:    Engine (loop, phases, pause,
                  keyboard, mouse │            events, schedule, states)
   @oly/core:     schema, World,   │    input:  ActionMap
                  Engine, contexts │    capi: C API oly*, script invoker
                  copy-free views  │    the game's C++ systems and handlers
 ──────────────── C API (extern "C") + per-engine script invoker ─────────────
 ECS: component → entity → table → archetype → query / command_buffer → world
 core: assert, align, small_map, type_name
```

## Index

| File | Contents |
|---|---|
| [repository.md](repository.md) | folder map and the shape of a module |
| [modules-and-boundaries.md](modules-and-boundaries.md) | dependency graph, how boundaries are enforced, vocabulary rules |
| [core.md](core.md) | base utilities (`OLY_ASSERT` and others) |
| [ecs.md](ecs.md) | the archetype ECS: layers, indices, costs, iteration, `CommandBuffer` |
| [input.md](input.md) | input actions and HID physical buttons |
| [app.md](app.md) | the `Engine`: loop, systems, pause, events, schedule, states, recovery |
| [bridge.md](bridge.md) | the C API and the per-engine script invoker |
| [schema-and-codegen.md](schema-and-codegen.md) | the `.oly` schema, `oly-schema` and each game's WASM module |
| [ts-packages.md](ts-packages.md) | `@oly/core`, `@oly/web-host`, `@oly/testing` |
| [games.md](games.md) | the validation game `games/arena` |
| [build-and-tests.md](build-and-tests.md) | Docker, presets, tests and code quality |
| [performance.md](performance.md) | targets, current status and boundary costs |
| [decisions.md](decisions.md) | main architectural decisions, with reason and accepted cost |
| [limits.md](limits.md) | known limitations and what is out of scope |
| [glossary.md](glossary.md) | the same verb in C++, C API and TS |

To make a game with the engine: [../guide/README.md](../guide/README.md) (user guide with examples and recipes). How to write and maintain it: [../development/guide-style.md](../development/guide-style.md).

To work on the code: [../development/commands.md](../development/commands.md) (build, tests, benchmarks, games) and [../development/code-style.md](../development/code-style.md).

## Reference documents

The specs are the source of truth. The plans record, phase by phase, how each part was implemented and the decisions the specs left open.

| Document | Contents |
|---|---|
| `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md` | ECS: requirements `R-*`, invariants `I-1..I-11`, targets `NF-*` |
| `docs/superpowers/specs/2026-11-06-wasm-cost-study-design.md` | native × WASM cost study |
| `docs/superpowers/specs/2026-11-09-game-engine-design.md` | the engine: loop, events, input, schedule, states, validation game |
| `docs/superpowers/specs/2026-11-09-oly-modularization-design.md` | modules, boundaries, names and public surfaces |
| `docs/superpowers/plans/` | implementation plans per phase |
| `CLAUDE.md` | index for Claude Code and rules that always apply |
| `docs/benchmarks/` | results and status of the targets |
