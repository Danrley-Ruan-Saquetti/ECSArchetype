# Repository map

```
CMakeLists.txt  CMakePresets.json  Makefile         C++ build and shortcuts
Dockerfile  Dockerfile.wasm  docker-compose.yaml    toolchains (gcc:14 and emsdk 3.1.74)
package.json  tsconfig.base.json  tsconfig.json     npm workspaces and project references
cmake/            build functions: OlyModule, OlyGame, OlyOptions, OlyWarnings, OlyWasm, Dependencies
engine/
  runtime/        C++, one module per folder: <m>/{include/oly/<m>, src, tests, bench}
    core/         dependency-free utilities
    ecs/          the archetype ECS
    input/        input actions (ActionMap)
    app/          the Engine: loop, systems, events, schedule, states
    capi/         C API oly*, OlyWorld, OlyEngine, schema registration
  environments/   one folder per target environment
    web/clients/typescript/core/  @oly/core: the engine on the TS side, no DOM
    web/host/js/                  @oly/web-host: browser layer
  tools/codegen/  @oly/codegen: TS schema -> C++ header
  testing/        @oly/testing (private): fixture game, WASM tests, boundary benchmarks
games/
  arena/          validation game (C++ in native/, TS in src/, smoke test in tests/)
tools/            measurement and lint scripts (Python and shell)
docs/
  architecture/   this documentation
  development/    commands and code style
  benchmarks/     recorded results: ecs/, bridge/, wasm/
  superpowers/    specs (source of truth) and implementation plans per phase
.claude/          Claude Code skills, agents and formatting hook
```

## Shape of a C++ module

Every C++ module follows the same shape:

```
engine/runtime/<m>/
  CMakeLists.txt         oly_add_module(NAME <m> DEPENDS ...)
  include/oly/<m>/       public headers
  include/oly/<m>/detail/  internal types (namespace oly::<m>::detail)
  src/                   implementation (comes in by glob)
  tests/                 doctest suites and death cases
  bench/                 benchmarks (only in ecs)
```

The include path repeats the module name (`#include "oly/ecs/world.hpp"`), so the include already says where the header comes from.

See also: [modules-and-boundaries.md](modules-and-boundaries.md).
