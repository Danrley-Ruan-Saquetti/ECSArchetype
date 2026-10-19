# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

ECS por arquétipos, single-thread, em C++20 — projeto de estudo e benchmark. A fonte da verdade do design é `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md` (requisitos `R-*`, invariantes `I-1..I-11`, metas `NF-*`); o plano de implementação por tarefas está em `docs/superpowers/plans/`. Commits, comentários e mensagens de assert são em pt-BR.

## Build e testes

Não há toolchain C++ no host Windows: tudo roda no container (`gcc:14` + cmake + perf) via Docker Compose.

```sh
DC="docker compose run --rm dev"
$DC make test                      # configura + compila + ctest no preset debug-asan (ASan+UBSan, asserts ligados)
$DC make test T=unit_world         # um teste só (T é regex passada a `ctest -R`)
$DC make test T='contract_death'   # todos os casos de morte
$DC make build PRESET=release      # compila testes + benchmarks em Release, asserts desligados
$DC ./build/debug-asan/tests/unit_world -tc="nome do caso"   # um TEST_CASE doctest específico
```

- Presets (`CMakePresets.json`): `debug-asan` (padrão), `release` (testes + bench, `ECS_ENABLE_ASSERTS=0`), `relwithdebinfo` (só bench, para `perf`). Build em `build/<preset>/`.
- `-Wall -Wextra -Wpedantic -Werror` estão ligados em todos os nossos alvos; aviso quebra o build.
- Cada suíte em `tests/` é um executável separado registrado com `ecs_add_test(...)` em `tests/CMakeLists.txt`; arquivo novo precisa ser adicionado lá. Fontes de `src/*.cpp` entram por glob.
- `contract_death` é um executável à parte (só existe com asserts ligados): cada caso é um argumento de linha de comando, o handler de assert confere substrings da mensagem e sai com 0. Novo caso = função no arquivo + nome na lista `foreach` do CMake.
- Fuzz (`fuzz_vs_model`, partes de `command_buffer`): semente e nº de operações via `ECS_FUZZ_SEED` / `ECS_FUZZ_OPS` (`tests/support/model.hpp`); a semente usada é impressa no stderr para reproduzir falhas.
- Formatação: `.clang-format` (base Google, indentação 2, `NamespaceIndentation: All`, sem limite de coluna).

## Benchmarks

```sh
$DC make build PRESET=release
$DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/<arquivo>.json
$DC python3 -I tools/bench_ratio.py --json <arquivo>.json --name <bench> --baseline <bench> --max <razão>
```

Metas de desempenho são **razões** contra uma linha de base medida na mesma execução (o ambiente WSL2 é ruidoso), nunca números absolutos. `tools/perf_fragmented.sh` roda `perf stat` (serviço `perf` do compose, privilegiado). Resultados registrados e situação de cada meta em `bench/results/README.md` — NF-P5 não é atendida a N = 100000.

## Arquitetura

Camadas estritas, cada módulo só usa os de baixo: `type_info → entity → table → archetype → query / command_buffer → world`.

- **`type_info`**: `componentId<T>()` dá IDs densos por processo; `TypeInfo` guarda size/align, se é trivial (caminho `memcpy`) ou tag (tamanho 0, sem coluna) e ponteiros de move/destroy.
- **`table`**: SoA com um único `count`/`capacity` para todas as colunas, colunas alinhadas a 64 B. Não conhece entidades vivas nem records. `pushUninit` deixa uma linha morta que o chamador precisa construir; `fillHole(r)` faz swap-remove e devolve a entidade deslocada.
- **`archetype`**: dono de uma `Table`, com `signature` (inclui tags) e `columnTypes` (só componentes com dados) — confundir os dois é a fonte clássica de coluna errada. Arestas `±ComponentId` memoizadas trazem o mapeamento de colunas `sourceToDestination` pronto. Arquétipos nunca são removidos; ponteiros são estáveis.
- **`world`**: único dono dos `EntityRecord` e único que chama `fillHole`, então a correção do record da entidade deslocada vive num só lugar. Mantém `bySignature_` e `componentIndex_[cid]` (arquétipos que contêm `cid` + índice de coluna), atualizados só em `findOrCreate`, que também registra o arquétipo novo nas queries existentes. Templates em `world.hpp` são finos e delegam para funções não-template por `ComponentId` (`addRaw`, `removeRaw` etc.) em `world.cpp`.
- **`query`**: `Query<Ts...>` cacheada e de vida controlada pelo `World`, com matching incremental. `each`/`eachBlock` incrementam `iterating_` (via `IterationGuard`); qualquer mudança estrutural direta com `iterating_ > 0` é erro de contrato (`ECS_ASSERT`). Mudanças durante iteração passam por `CommandBuffer`.
- **`command_buffer`**: `spawn()` reserva o índice na hora (record com `archetype == nullptr`); `flush` agrupa por entidade, resolve conflitos (R-CB-4) e faz **uma única transição por entidade** até o arquétipo final.

Validade: qualquer mudança estrutural invalida ponteiros/spans de qualquer entidade; só `Entity` é handle estável.

## Corretude

- `World::checkInvariants(std::string* why)` verifica as invariantes da seção 6 da spec; os testes a chamam após cada operação. Ao mudar a lógica estrutural, mantenha-a cobrindo o que mudou.
- `fuzz_vs_model` compara o `World` com um modelo trivial (`tests/support/model.hpp`); valores derivados do `Entity` fazem trocas de linha aparecerem como valor errado.
- `LifeCounter` (em `tests/support/components.hpp`) conta construções − destruições para pegar moved-from não destruído e double destroy (I-2).
- `ECS_ASSERT(cond, fmt, ...)` (`include/ecs/detail/assert.hpp`) some com `ECS_ENABLE_ASSERTS=0`; mensagens devem incluir operação, `Entity` e nome do tipo.

## Estilo do código

- Chaves em todo `if`/`for`/`while`/`else`, mesmo com uma linha só.
- Nomes completos, sem abreviações (`entity`, `componentId`, `sourceToDestination`, não `e`, `cid`, `srcToDst`).
- Tipos em `PascalCase`, funções e variáveis em `camelCase`, membros privados com sufixo `_`.
- Comentários só para o porquê ou para contratos não óbvios; nada que repita o código. Se um comentário explica o quê, prefira um nome ou uma função extraída.
- Linhas em branco separam ideias (blocos lógicos), não linhas soltas.
- Um bloco `namespace` por arquivo, sem reabrir o mesmo namespace mais abaixo.
- `#include` dos headers do projeto com aspas (`"ecs/..."`), depois dos headers da biblioteca padrão.
