# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Oly: engine de jogos em C++20 e TypeScript, construída sobre um ECS por arquétipos single-thread (projeto de estudo e benchmark). A fonte da verdade do ECS é `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md` (requisitos `R-*`, invariantes `I-1..I-11`, metas `NF-*`); a da modularização é `docs/superpowers/specs/2026-11-09-oly-modularizacao-design.md`. Planos de implementação por tarefas ficam em `docs/superpowers/plans/`. Commits, comentários e mensagens de assert são em pt-BR.

## Árvore

```
cmake/            OlyOptions, OlyWarnings, OlyWasm, OlyModule (oly_add_module/test/bench/contract_death), OlyGame, Dependencies
engine/runtime/   C++, um módulo por pasta: <m>/{include/oly/<m>, src, tests, bench}
  core/           assert, align, small_map, type_name           (sem dependências)
  ecs/            o ECS                                         (DEPENDS core)
  app/            Engine: loop, fases, sistemas, pausa          (DEPENDS ecs core)
  bridge/         API C (oly*), OlyWorld, registro do esquema   (DEPENDS app ecs core, módulo OBJECT)
engine/packages/  TS, um pacote npm por pasta: core (@oly/core), client (@oly/client)
engine/tools/     codegen (@oly/codegen: esquema TS -> C++)
engine/testing/   @oly/testing (privado): fixture (único lugar com nomes de jogo), tests/wasm, bench/{boundary,loop}.ts
games/            vazio até a fase 7
tools/            scripts de medição e de lint (bench_ratio.py, bench_ladder.py, run_ladder.sh, check_boundaries.py, ...)
docs/benchmarks/  resultados registrados: ecs/, bridge/, wasm/
```

## Build e testes

Não há toolchain C++ no host Windows: tudo roda no container (`gcc:14` + cmake + perf) via Docker Compose.

```sh
DC="docker compose run --rm dev"
$DC make test                       # configura + compila + ctest no preset debug-asan (ASan+UBSan, asserts ligados)
$DC make test T='^ecs\.'            # um módulo (T é regex passada a `ctest -R`)
$DC make test T='ecs.unit_world'    # uma suíte
$DC make test T='contract_death'    # todos os casos de morte (ecs e bridge)
$DC make build PRESET=release       # compila testes + benchmarks em Release, asserts desligados
$DC ./build/debug-asan/tests/ecs_unit_world -tc="nome do caso"   # um TEST_CASE doctest específico
```

- Presets (`CMakePresets.json`): `debug-asan` (padrão), `release` (testes + bench, `OLY_ENABLE_ASSERTS=0`), `relwithdebinfo` (só bench, para `perf`), as variantes do estudo de custo WASM (`native-clang*`, `wasm-scalar`, `wasm-simd`), `wasm-debug`, `wasm-games` e `wasm-games-release`. Build em `build/<preset>/`; executáveis de teste em `build/<preset>/tests/<módulo>_<suíte>`, de benchmark em `build/<preset>/bench/ecs_bench_*`.
- Opções: `OLY_BUILD_TESTS`, `OLY_BUILD_BENCH`, `OLY_SANITIZE`, `OLY_WERROR`, `OLY_VEC_REPORT`, `OLY_ENABLE_ASSERTS`, `OLY_BUILD_GAMES`.
- Nomes no ctest levam o prefixo do módulo: `core.*`, `ecs.*`, `app.*`, `bridge.*`, `ts.<pacote>.<suíte>` (`ts.core.unit`, `ts.codegen.unit`, `ts.client.unit`, `ts.testing.wasm`) e `oly.lint.boundaries`.
- `-Wall -Wextra -Wpedantic -Werror` estão ligados em todos os nossos alvos (`oly_warnings`); aviso quebra o build.
- Cada suíte é um executável separado registrado com `oly_add_test(MODULE <m> NAME <suíte> SOURCES ... [SUPPORT <dirs>])` no `tests/CMakeLists.txt` do módulo; arquivo novo precisa ser adicionado lá. Fontes de `src/*.cpp` entram por glob.
- `ecs_contract_death` (e `bridge_contract_death`) só existem com asserts ligados e são registrados com `oly_add_contract_death(MODULE <m> SOURCE <arq> CASES ...)`: cada caso é um argumento de linha de comando, o handler de assert confere substrings da mensagem e sai com 0. Novo caso = função no arquivo + nome na lista `CASES`.
- Fuzz (`fuzz_vs_model`, partes de `command_buffer`): semente e nº de operações via `OLY_FUZZ_SEED` / `OLY_FUZZ_OPS` (`engine/runtime/ecs/tests/support/model.hpp`); a semente usada é impressa no stderr para reproduzir falhas.
- Formatação: `.clang-format` (base Google, indentação 2, `NamespaceIndentation: All`, sem limite de coluna).

## Benchmarks

```sh
$DC make build PRESET=release
$DC ./build/release/bench/ecs_bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=docs/benchmarks/ecs/<arquivo>.json
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/<arquivo>.json --name <bench> --baseline <bench> --max <razão>
```

Executáveis: `ecs_bench_iterate`, `ecs_bench_transitions`, `ecs_bench_fragmented`, `ecs_bench_flush`. Metas de desempenho são **razões** contra uma linha de base medida na mesma execução (o ambiente WSL2 é ruidoso), nunca números absolutos. `tools/perf_fragmented.sh` roda `perf stat` (serviço `perf` do compose, privilegiado). Resultados registrados e situação de cada meta em `docs/benchmarks/README.md` — NF-P5 não é atendida a N = 100000 e fica no limite do ruído a N = 1000.

## Módulos e fronteiras

`oly_add_module(NAME <m> [DEPENDS <m...>] [OBJECT | HEADER_ONLY])` cria `oly_<m>` (alias `oly::<m>`) e liga só os `DEPENDS`. Dependências: `core` ← `ecs` ← `app` ← `bridge` ← jogos (`oly_add_game`). Cada módulo e jogo declara em `DEPENDS` todo módulo que inclui diretamente.

O `oly.lint.boundaries` (`tools/check_boundaries.py`, roda no ctest) reprova:
- `#include "oly/<x>/..."` num módulo ou jogo que não declarou `<x>` em `DEPENDS`;
- `oly/<x>/detail/` incluído de fora de `engine/runtime/<x>/` (testes e benchmarks do próprio módulo podem);
- `@oly/core/internal` importado fora de `engine/`, `@oly/client` importado em `@oly/core` e import relativo que atravessa pacotes.

Nada em `engine/runtime/ecs` conhece o `bridge`. Nomes de jogo só em `engine/testing/fixture/`; conceitos de jogo nunca em `engine/`. Sem aliases ou `using` de compatibilidade com nomes antigos.

## Glossário (o mesmo verbo em C++, API C e TS)

| Conceito | C++ (`oly::ecs`) | API C | TS (`@oly/core`) |
|---|---|---|---|
| criar entidade | `World::spawn()`, `spawn(components...)`, `CommandBuffer::spawn()` | `olyEntitySpawn` | `World.spawn` |
| destruir / viva | `destroy`, `isAlive` | `olyEntityDestroy`, `olyEntityIsAlive` | `destroy`, `isAlive` |
| adicionar / remover / ter | `add`, `remove`, `has` | `olyComponentAdd/Remove/Has` | `add`, `remove`, `has` |
| ler (existe por contrato) / se existir | `get` -> `T&` / `tryGet` -> `T*` | `olyComponentGet` | `get` -> cópia |
| query / iterar | `query<Ts...>`, `each`, `eachBlock` | `olyQueryCreate`, `olyQueryBegin/End` | `query`, `eachBlock` |
| passo de jogo (fixo) / aba oculta | `Engine::frame(nowSeconds)`, `setHidden` | `olyEngineFrame`, `olyEngineSetHidden` | `Engine.frame`, `setHidden` |
| pausar / retomar | `Engine::pause`, `resume`, `paused`; `SystemContext::pause/resume` | `olyEnginePause/Resume/IsPaused` | `pause`, `resume`, `paused` |
| registrar sistema | `Engine::addSystem(phase, name, função)`, `OLY_SYSTEM(name)` (por nome) | `olyEngineAddNativeSystem` (por nome), `olyEngineAddScriptSystem` | `addSystem`, `addNativeSystem` |
| sistema roda pausado | `Engine::setWhilePaused` | `olyEngineSetWhilePaused` | `SystemRegistration.whilePaused()` |
| mudança estrutural adiada (dentro de sistema) | `SystemContext::spawn/destroy/add/remove` | `olySystemSpawn/Destroy/Add/Remove` | `context.spawn/destroy/add/remove` |
| variante por ID | sufixo `ById`: `addById`, `removeById`, `hasById`, `getById`, `tryGetById`, `queryById` | (toda por ID) | — |

O prefixo `oly` aparece só na API C (`oly*`, `OlyWorld`, `OLY_API`) e no que é interno à ponte (`OlyModule`). APIs C++ e TS não repetem `oly` nos nomes. `create` e "Raw" não existem mais como nomes.

## Arquitetura do ECS (`engine/runtime/ecs`)

Camadas estritas, cada uma só usa as de baixo: `component → entity → detail/table → detail/archetype → query / command_buffer → world`. Os headers públicos são `world`, `entity`, `component`, `query`, `query_by_id`, `command_buffer` e `diagnostics`; tudo interno fica em `oly/ecs/detail/` (`Table`, `Archetype`, `EntityStore`, cache de query, plano de comandos, `test_access`). Os templates do caminho quente precisam ver `detail/`, então a proteção é o lint, não pimpl.

- **`component`**: `componentId<T>()` dá IDs densos por processo; `TypeInfo` guarda size/align, se é trivial (caminho `memcpy`) ou tag (tamanho 0, sem coluna) e ponteiros de move/destroy.
- **`detail/table`**: SoA com um único `count`/`capacity` para todas as colunas, colunas alinhadas a 64 B. Não conhece entidades vivas nem records. `pushUninit` deixa uma linha morta que o chamador precisa construir; `fillHole(r)` faz swap-remove e devolve a entidade deslocada.
- **`detail/archetype`**: dono de uma `Table`, com `signature` (inclui tags) e `columnTypes` (só componentes com dados) — confundir os dois é a fonte clássica de coluna errada. Arestas `±ComponentId` memoizadas trazem o mapeamento de colunas `sourceToDestination` pronto. Arquétipos nunca são removidos; ponteiros são estáveis.
- **`world`**: único dono dos `EntityRecord` e único que chama `fillHole`, então a correção do record da entidade deslocada vive num só lugar. Mantém `bySignature_` e `componentIndex_[componentId]`, atualizados só em `findOrCreate`, que também registra o arquétipo novo nas queries existentes. Templates em `world.hpp` são finos e delegam para funções não-template por `ComponentId` (`addById`, `removeById` etc.) em `world.cpp`; o caminho quente (`get<T>`, `Query::each`) não delega.
- **`query`**: `Query<Ts...>` cacheada e de vida controlada pelo `World`, com matching incremental. `each`/`eachBlock` incrementam `iterating_` (via `IterationGuard`); qualquer mudança estrutural direta com `iterating_ > 0` é erro de contrato (`OLY_ASSERT`). Mudanças durante iteração passam por `CommandBuffer`.
- **`query_by_id`**: `World::queryById(QueryByIdSpec)` devolve a `QueryById` (type-erased por `ComponentId`, cacheada pela spec). `eachBlock(callback)` entrega `BlockById`; `iterate()` devolve uma `QueryIteration` RAII (só movível, encerra a iteração uma vez) com `descriptors()` de um pool por nível de aninhamento; encerrar fora de ordem é erro de contrato. As APIs template **não** delegam para as `ById`, porque isso poria indireção no caminho quente e mexeria em NF-P1/P2.
- **`command_buffer`**: `spawn()` reserva o índice na hora (record com `archetype == nullptr`); `flush` agrupa por entidade, resolve conflitos (R-CB-4) e faz **uma única transição por entidade** até o arquétipo final.
- **`diagnostics`**: funções livres `checkInvariants(const World&, std::string*)`, `stats`, `signatureLookupCount`, `archetypeCount` (amigas do `World`). Ganchos de teste (`flushSequential`, `setGeneration`, `CommandBuffer::forEach`) ficam em `detail/test_access.hpp` (`WorldTestAccess`, `EntityStoreTestAccess`, `CommandBufferTestAccess`).

Validade: qualquer mudança estrutural invalida ponteiros/spans de qualquer entidade; só `Entity` é handle estável.

## Engine: loop e sistemas (`engine/runtime/app`)

`oly::app::Engine` guarda o relógio de passo fixo, os sistemas de cada fase (`UpdateBefore`, `Update`, `UpdateAfter`, `Render`), a pausa e a aba oculta. Toda regra do loop vive no C++; o TS só valida o uso e repassa. Um sistema é um ponteiro de função mais `userData`, então o `app` não conhece TS: o `bridge` registra um sistema TS como uma função C++ que chama a única função JS importada (`olyHostInvokeScript`), uma vez por sistema por passo, nunca por entidade. Mudanças estruturais feitas pelos sistemas (`context.spawn/destroy/add/remove`) ficam num `CommandBuffer` por `Engine` que não aparece na API e é aplicado ao fim de cada fase. Se um sistema TS lança, o `frame` seguinte descarta os comandos pendentes e se comporta como o primeiro frame (recuperação); o que as fases anteriores já aplicaram fica.

## Engine: ponte C e pacotes TS

`bridge` expõe a API C `extern "C"` (`engine/runtime/bridge/src/c_api.cpp`): `OlyWorld` é opaco (guarda o `World` e uma pilha de slots de iteração; `oly::bridge::worldOf(OlyWorld*)` dá o `World` aos sistemas do jogo), `olyQueryBegin` devolve uma `OlyIteration*` com buffer próprio de descritores e `olyQueryEnd` precisa encerrar o slot do topo. Os pacotes TS são npm workspaces na raiz (`package.json`, `tsconfig.base.json`, project references; saída em `<pacote>/dist` e `dist-tests`):

- `@oly/core`: `loadRuntime(factory, schema)` -> `Runtime` (`schema`, `createWorld()`), `World`, `Query`, `Block`, `ContractError`, `SchemaMismatchError`, `defineSchema`. Sem DOM nem node (`types: []`). A lista pública exata é `engine/packages/core/api/public-exports.txt`, conferida por `surface.test.ts`. `@oly/core/internal` (`bridgeOf`, `moduleOf`, `componentIdOf`, `Bridge`...) é só para `engine/`.
- `@oly/client`: `moduleFactoryFromUrl(url)` e `runInBrowser(engine)` (rAF + `visibilitychange`), camada fina do navegador (usa DOM).
- `@oly/codegen`: gera o C++ do esquema (`oly-codegen`); `static_assert` + hash conferido por `loadRuntime` (`SchemaMismatchError` se o WASM estiver desatualizado).
- `@oly/testing` (privado): fixture, testes de integração WASM e benchmark da fronteira.

```sh
WC="docker compose run --rm wasm"
$WC make ts                 # npm ci (se preciso) + tsc -b em todos os workspaces
$WC make games-test         # ts + ctest no preset wasm-games (ECS em WASM, bridge.c_api, testes TS)
$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'   # Release: só as validações TS seguram os erros
$WC node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/<arquivo>.json --commit $(git rev-parse --short HEAD)
```

Os presets `wasm-games*` exigem `make ts` antes de configurar. Regras da ponte: views (`Block.read`/`write`) só valem dentro do callback de `eachBlock`; só `Entity` é handle estável. Toda chamada que pode alocar no WASM passa pela `Bridge`, que faz `sync()` (recria as views do heap se a memória cresceu); ninguém fora dela chama exports. O TS valida o uso antes de chamar a API C (erro = `ContractError`), para nunca chegar ao `OLY_ASSERT`.

## Corretude

- `checkInvariants(const World&, std::string* why)` (função livre de `diagnostics.hpp`) verifica as invariantes da seção 6 da spec; os testes a chamam após cada operação. Ao mudar a lógica estrutural, mantenha-a cobrindo o que mudou (inclusive as caches de `QueryById`, que entram em `checkQueries`).
- `fuzz_vs_model` (`engine/runtime/ecs/tests/`) compara o `World` com um modelo trivial (`engine/runtime/ecs/tests/support/model.hpp`); valores derivados do `Entity` fazem trocas de linha aparecerem como valor errado.
- `LifeCounter` (em `engine/runtime/ecs/tests/support/components.hpp`) conta construções − destruições para pegar moved-from não destruído e double destroy (I-2).
- `OLY_ASSERT(cond, fmt, ...)` (`oly/core/assert.hpp`) some com `OLY_ENABLE_ASSERTS=0`; mensagens devem incluir operação, `Entity` e nome do tipo.

## Estilo do código

- Chaves em todo `if`/`for`/`while`/`else`, mesmo com uma linha só.
- Nomes completos, sem abreviações (`entity`, `componentId`, `sourceToDestination`, não `e`, `cid`, `srcToDst`).
- Tipos em `PascalCase`, funções e variáveis em `camelCase`, membros privados com sufixo `_`.
- Comentários só para o porquê ou para contratos não óbvios; nada que repita o código. Se um comentário explica o quê, prefira um nome ou uma função extraída.
- Linhas em branco separam ideias (blocos lógicos), não linhas soltas.
- Um bloco `namespace` por arquivo, sem reabrir o mesmo namespace mais abaixo.
- Em `class`/`struct`, os atributos (membros de dados, inclusive privados e estáticos) ficam no topo, antes de construtores e métodos. Numa `class` isso significa abrir com uma seção `private:` só de dados (e dos tipos aninhados que eles usam), depois `public:` e, por fim, `private:` com os métodos auxiliares. Mover blocos de membros não pode mudar a ordem relativa entre eles (ordem de inicialização, `-Wreorder`).
- `#include` dos headers do projeto com aspas (`"oly/ecs/..."`), depois dos headers da biblioteca padrão.
