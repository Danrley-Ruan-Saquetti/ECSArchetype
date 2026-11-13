# Commands

There is no C++ toolchain on the Windows host: everything runs in a container via Docker Compose. The services are described in [../architecture/build-and-tests.md](../architecture/build-and-tests.md#toolchain).

```sh
DC="docker compose run --rm dev"     # gcc:14 + cmake + perf
WC="docker compose run --rm wasm"    # emsdk + Clang + node + Python
```

## Native C++

```sh
$DC make test                       # configure + build + ctest in the debug-asan preset (ASan+UBSan, asserts enabled)
$DC make test T='^ecs\.'            # one module (T is a regex passed to `ctest -R`)
$DC make test T='ecs.unit_world'    # one suite
$DC make test T='contract_death'    # all death cases
$DC make build PRESET=release       # builds tests + benchmarks in Release, asserts disabled
$DC ./build/debug-asan/tests/ecs_unit_world -tc="case name"   # one specific doctest TEST_CASE
```

- Build in `build/<preset>/`. Test executables in `build/<preset>/tests/<module>_<suite>`; benchmark executables in `build/<preset>/bench/ecs_bench_*`.
- Fuzz (`fuzz_vs_model`, parts of `command_buffer`): seed and number of operations via `OLY_FUZZ_SEED` and `OLY_FUZZ_OPS` (`engine/runtime/ecs/tests/support/model.hpp`). The seed used is printed to stderr to reproduce a failure.

## ECS benchmarks

```sh
$DC make build PRESET=release
$DC ./build/release/bench/ecs_bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=docs/benchmarks/ecs/<file>.json
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/<file>.json --name <bench> --baseline <bench> --max <ratio>
```

- Executables: `ecs_bench_iterate`, `ecs_bench_transitions`, `ecs_bench_fragmented`, `ecs_bench_flush`.
- `tools/perf_fragmented.sh` runs `perf stat` in the `perf` service (privileged).
- Always compare ratios from the same run; the WSL2 environment is noisy ([../architecture/performance.md](../architecture/performance.md)).

## WASM, TypeScript and games

```sh
$WC make ts                 # npm ci (if needed) + tsc -b in every workspace
$WC make games-test         # ts + ctest in the wasm-games preset (ECS in WASM, capi, TS tests)
$WC make game G=arena       # assembles build/wasm-games-release/web/arena/ (index.html, esbuild's main.js, arena_game.mjs/.wasm)
$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'   # Release: only the TS validations catch the errors
$WC node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/<file>.json --commit $(git rev-parse --short HEAD)
docker compose run --rm --service-ports web make serve G=arena   # http://localhost:8000
```

Caveats:

- The `wasm-games*` presets require `make ts` before configuring (codegen and the schema come from the packages' `dist/`).
- `make game` and `make serve` only accept `wasm-games*` presets. Without `PRESET` on the command line or in the environment, they use `wasm-games-release`.
- `make serve` needs an open stdin (interactive terminal): esbuild exits when stdin closes.
