# Commands

There is no C++ toolchain on the Windows host: everything runs in a container via Docker Compose, driven by `./spur <task>` from Git Bash. The host needs Git Bash and Docker. The services are described in [../architecture/build-and-tests.md](../architecture/build-and-tests.md#toolchain).

`./spur --list` shows the tasks, and `./spur --describe <task>` the help of one. Each task runs in the container its preset needs (`dev`: gcc:14 + cmake + perf; `wasm`: emsdk + Clang + node + Python); typed inside the wrong container, it exits with 64.

- Run `docker compose build` once after pulling the change that introduced `./spur`: images built before it lack `OLY_TOOLCHAIN` and stop with exit 69.
- `-x` and `-n` given on the host show only the dispatch to the container. To trace a task, run it inside: `./spur dev ./spur -x test`.

## Native C++

```sh
./spur test                       # configure + build + ctest in the debug-asan preset (ASan+UBSan, asserts enabled)
./spur test -R '^ecs\.'           # one module (the arguments go to ctest)
./spur test -R 'ecs.unit_world'   # one suite
./spur test -R 'contract_death'   # all death cases
PRESET=release ./spur build       # builds tests + benchmarks in Release, asserts disabled
./spur dev ./build/debug-asan/tests/ecs_unit_world -tc="case name"   # one specific doctest TEST_CASE
./spur dev                        # a shell in the dev container
```

- Build in `build/<preset>/`. Test executables in `build/<preset>/tests/<module>_<suite>`; benchmark executables in `build/<preset>/bench/ecs_bench_*`.
- Fuzz (`fuzz_vs_model`, parts of `command_buffer`): seed and number of operations via `OLY_FUZZ_SEED` and `OLY_FUZZ_OPS` (`engine/runtime/ecs/tests/support/model.hpp`). The seed used is printed to stderr to reproduce a failure.

## ECS benchmarks

```sh
PRESET=release ./spur build
./spur dev ./build/release/bench/ecs_bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=docs/benchmarks/ecs/<file>.json
./spur dev python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/<file>.json --name <bench> --baseline <bench> --max <ratio>
```

- Executables: `ecs_bench_iterate`, `ecs_bench_transitions`, `ecs_bench_fragmented`, `ecs_bench_flush`.
- `tools/perf_fragmented.sh` runs `perf stat` in the `perf` service (privileged).
- Always compare ratios from the same run; the WSL2 environment is noisy ([../architecture/performance.md](../architecture/performance.md)).

## WASM, TypeScript and games

```sh
./spur schema-tool        # builds oly-schema for the machine (preset schema-tool)
./spur ts                 # builds oly-schema, generates every TS schema and compiles the TS
./spur games-test         # ts + ctest in the wasm-games preset (ECS in WASM, capi, TS tests)
./spur game arena         # assembles build/wasm-games-release/web/arena/ (index.html, esbuild's main.js, arena_game.mjs/.wasm)
PRESET=wasm-games-release ./spur games-test -R '^ts\.'   # Release: only the TS validations catch the errors
./spur wasm node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/<file>.json --commit $(git rev-parse --short HEAD)
./spur serve arena        # http://localhost:8000
```

Caveats:

- The `wasm-games*` presets require `./spur ts` (or `./spur schema-tool`) before configuring: `oly_add_game` runs `build/schema-tool/bin/oly-schema`.
- `./spur games-test`, `game` and `serve` only accept `wasm-games*` presets. Without `PRESET`, `games-test` uses `wasm-games` and `game` and `serve` use `wasm-games-release`.
- `./spur serve` needs an open stdin (interactive terminal): esbuild exits when stdin closes.
