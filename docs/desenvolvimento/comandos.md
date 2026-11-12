# Comandos

Não há toolchain C++ no host Windows: tudo roda em container via Docker Compose. Os serviços estão descritos em [../arquitetura/build-e-testes.md](../arquitetura/build-e-testes.md#toolchain).

```sh
DC="docker compose run --rm dev"     # gcc:14 + cmake + perf
WC="docker compose run --rm wasm"    # emsdk + Clang + node + Python
```

## C++ nativo

```sh
$DC make test                       # configura + compila + ctest no preset debug-asan (ASan+UBSan, asserts ligados)
$DC make test T='^ecs\.'            # um módulo (T é regex passada a `ctest -R`)
$DC make test T='ecs.unit_world'    # uma suíte
$DC make test T='contract_death'    # todos os casos de morte
$DC make build PRESET=release       # compila testes + benchmarks em Release, asserts desligados
$DC ./build/debug-asan/tests/ecs_unit_world -tc="nome do caso"   # um TEST_CASE doctest específico
```

- Build em `build/<preset>/`. Executáveis de teste em `build/<preset>/tests/<módulo>_<suíte>`; de benchmark em `build/<preset>/bench/ecs_bench_*`.
- Fuzz (`fuzz_vs_model`, partes de `command_buffer`): semente e número de operações por `OLY_FUZZ_SEED` e `OLY_FUZZ_OPS` (`engine/runtime/ecs/tests/support/model.hpp`). A semente usada é impressa no stderr para reproduzir uma falha.

## Benchmarks do ECS

```sh
$DC make build PRESET=release
$DC ./build/release/bench/ecs_bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=docs/benchmarks/ecs/<arquivo>.json
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/<arquivo>.json --name <bench> --baseline <bench> --max <razão>
```

- Executáveis: `ecs_bench_iterate`, `ecs_bench_transitions`, `ecs_bench_fragmented`, `ecs_bench_flush`.
- `tools/perf_fragmented.sh` roda `perf stat` no serviço `perf` (privilegiado).
- Compare sempre razões da mesma execução; o ambiente WSL2 é ruidoso ([../arquitetura/desempenho.md](../arquitetura/desempenho.md)).

## WASM, TypeScript e jogos

```sh
$WC make ts                 # npm ci (se preciso) + tsc -b em todos os workspaces
$WC make games-test         # ts + ctest no preset wasm-games (ECS em WASM, bridge, testes TS)
$WC make game G=arena       # monta build/wasm-games-release/web/arena/ (index.html, main.js do esbuild, arena_game.mjs/.wasm)
$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'   # Release: só as validações TS seguram os erros
$WC node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/<arquivo>.json --commit $(git rev-parse --short HEAD)
docker compose run --rm --service-ports web make serve G=arena   # http://localhost:8000
```

Cuidados:

- Os presets `wasm-games*` exigem `make ts` antes de configurar (o codegen e o esquema saem do `dist/` dos pacotes).
- `make game` e `make serve` só aceitam presets `wasm-games*`. Sem `PRESET` na linha de comando ou no ambiente, usam `wasm-games-release`.
- `make serve` precisa de stdin aberto (terminal interativo): o esbuild encerra quando o stdin fecha.
