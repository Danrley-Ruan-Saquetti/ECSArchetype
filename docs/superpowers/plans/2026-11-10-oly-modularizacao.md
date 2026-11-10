# Oly — modularização e revisão das APIs públicas — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reorganizar o repositório na engine **Oly** (módulos C++ `core`/`ecs`/`bridge` e pacotes TS `@oly/core`/`@oly/client`/`@oly/codegen`/`@oly/testing`), com fronteiras garantidas pelo build e superfícies públicas enxutas, sem mudar comportamento nem desempenho.

**Architecture:** Cada módulo C++ vira `engine/runtime/<m>/{include/oly/<m>,src,tests,bench}` criado por `oly_add_module`, que liga só os `DEPENDS`; um script (`oly.lint.boundaries`) reprova includes não declarados, `detail/` usado de fora e imports TS proibidos. O lado TS vira npm workspaces com project references e saída em `dist/` por pacote. As renomeações seguem o glossário do spec (§5.2) e não deixam aliases.

**Tech Stack:** C++20 (gcc 14, clang/Emscripten 3.1.74), CMake ≥ 3.25 com presets, doctest 2.4.11, Google Benchmark 1.8.3, TypeScript 5.6.3, `@types/node` 20.17.6, `node:test`, Python 3 (scripts em `tools/`), Docker Compose (`dev`, `wasm`, `perf`).

**Spec:** `docs/superpowers/specs/2026-11-09-oly-modularizacao-design.md` (e, para requisitos do ECS, `2026-10-16-ecs-archetype-spec.md`).

## Global Constraints

- Branch `refactor/oly-modulos` a partir de `develop`. Cada task termina verde e com commit.
- Arquivos existentes mudam de lugar só com `git mv` (histórico preservado).
- Sem funcionalidade nova, sem mudar layout de dados, regras de validade ou o caminho quente.
- Anti-objetivo: nada de pimpl, `virtual` ou indireção em `Query<Ts...>::each`/`eachBlock` e `World::get`; as APIs template não delegam para as `ById`.
- Sem aliases, shims ou `using` de compatibilidade com nomes antigos (inclusive `namespace ecs = oly::ecs;`).
- Metas que continuam valendo: NF-P1 ≤ 1,10; NF-P2 ≤ 1,25; NF-P5 (`/1000`) ≤ 0,5.
- O prefixo `oly` aparece só na API C (`oly*`, `OlyWorld`, `OLY_API`) e no que é interno à ponte (`OlyModule`, `_oly*`). APIs C++ e TS não repetem `oly` nos nomes.
- Nomes de jogo só em `engine/testing/fixture/`. Conceitos de jogo nunca em `engine/`.
- Versões TS exatas: `typescript` 5.6.3, `@types/node` 20.17.6; sem bundler; testes com `node:test`.
- Commits, comentários e mensagens de assert em pt-BR; estilo do `CLAUDE.md` (chaves sempre, nomes completos, dados no topo da classe, um bloco `namespace` por arquivo).
- Specs e planos antigos e os JSON/TXT históricos de benchmark não são editados.
- Comandos: `DC="docker compose run --rm dev"`, `WC="docker compose run --rm wasm"`.

## Review Focus

1. **`eachBlock` aninhado pela API C e pelo TS** (mesma query e queries diferentes): os descritores do laço externo continuam válidos depois que o interno começa e termina. Teste na Task 6 (`bridge.c_api`) e na Task 8 (TS).
2. **Exceção dentro do callback de iteração** (C++ `QueryById::eachBlock`, TS `Query.eachBlock`): a iteração é encerrada e o `World` volta a aceitar mudança estrutural. Testes nas Tasks 5 e 8.
3. **`QueryIteration` movida**: o objeto movido não encerra a iteração de novo (sem decremento duplo) e o destino encerra uma vez. Teste na Task 5.
4. **Alocação em regime na iteração por ID**: o segundo `iterate()` na mesma profundidade não aloca. Teste na Task 5 (com `AllocCounter`).
5. **Memória do WASM crescendo durante `eachBlock`** (query nova criada dentro do callback, que aloca no WASM): o bloco externo continua lendo os valores certos. Teste na Task 8.

## Tabela "caminho antigo → novo" (§9 do spec)

| Antes | Depois |
|---|---|
| `include/ecs/detail/{assert,align,small_map,type_name}.hpp`, `src/assert.cpp` | `engine/runtime/core/include/oly/core/…`, `engine/runtime/core/src/assert.cpp` |
| `include/ecs/*.hpp`, `src/*.cpp` | `engine/runtime/ecs/include/oly/ecs/…` (internos em `detail/`, Task 4), `engine/runtime/ecs/src/` |
| `include/ecs/type_info.hpp` / `raw_query.hpp` | `oly/ecs/component.hpp` / `oly/ecs/query_by_id.hpp` |
| `tests/**` (menos `support/doctest_main.cpp`) | `engine/runtime/ecs/tests/**` (`raw_query.cpp` → `query_by_id.cpp`) |
| `tests/support/doctest_main.cpp` | `engine/runtime/core/tests/support/doctest_main.cpp` |
| `bench/*.cpp`, `bench/common.hpp` | `engine/runtime/ecs/bench/` |
| `engine/include/engine/*.hpp`, `engine/src/c_api.cpp`, `engine/tests/*` | `engine/runtime/bridge/{include/oly/bridge,src,tests}/` (`world_handle.hpp` some, entra `world.hpp`) |
| `engine/ts/*` | `engine/packages/core/src/*` |
| `engine/generator/*` | `engine/tools/codegen/src/*` |
| `engine/test/unit/{entity,layout,schema}.test.ts` | `engine/packages/core/tests/unit/` |
| `engine/test/unit/generator.test.ts` | `engine/tools/codegen/tests/generator.test.ts` |
| `engine/fixture/*`, `engine/test/wasm/*`, `engine/bench/boundary.ts` | `engine/testing/{fixture,tests/wasm,bench}/` |
| `engine/package.json`, `engine/package-lock.json`, `engine/tsconfig.json`, `engine/module-package.json` | `package.json`, `package-lock.json`, `tsconfig.base.json`, `tsconfig.json` na raiz + um `package.json`/`tsconfig.json` por pacote; `module-package.json` some |
| `tools/wasm/node_env.js` | `cmake/wasm/node_env.js` |
| `bench/results/**` | `docs/benchmarks/**` (`engine/` → `bridge/`, `m*`/`e1_*` → `ecs/`) |
| `build/ts/**` | `<pacote>/dist/**` (e `dist-tests/**` para testes) |
| ctest `unit_world`, `contract_death.<c>`, `engine_c_api`, `engine_ts_unit`, `engine_ts_wasm` | `ecs.unit_world`, `ecs.contract_death.<c>`, `bridge.c_api`, `ts.core.unit` + `ts.codegen.unit`, `ts.testing.wasm` |
| executáveis `bench_iterate` etc. | `ecs_bench_iterate`, `ecs_bench_transitions`, `ecs_bench_fragmented`, `ecs_bench_flush` |

Ajustes de nome não cobertos pelo glossário (registrados aqui, §11 do spec): `World::archetypeCount` → função livre `archetypeCount(const World&)` em `diagnostics.hpp`; `ECS_FUZZ_SEED`/`ECS_FUZZ_OPS` → `OLY_FUZZ_SEED`/`OLY_FUZZ_OPS`; `ECS_LADDER_EXTRA_ARGS` → `OLY_LADDER_EXTRA_ARGS`; `ENGINE_FIXTURE_MODULE` → `OLY_FIXTURE_MODULE`; caso de morte `raw_query_end_without_begin` deixa de existir (o par manual sai da API) e entra `query_by_id_iteration_out_of_order`; os rótulos `ts-engine`/`ts-engine-raw` do benchmark da fronteira ficam, para comparar com a fase 1. `RawField`/`ReadField.raw()` do TS ficam (§8.2 os lista como públicos): o "Raw deixa de existir" do glossário vale para as variantes por ID.

---

### Task 1: Árvore C++ e build por módulo

**Files:**
- Move (`git mv`): ver tabela acima, linhas C++ e `tools/wasm/node_env.js`. `tests/CMakeLists.txt` → `engine/runtime/ecs/tests/CMakeLists.txt`; `bench/CMakeLists.txt` → `engine/runtime/ecs/bench/CMakeLists.txt`; `engine/tests/*` → `engine/runtime/bridge/tests/`.
- Create: `cmake/OlyOptions.cmake`, `cmake/OlyWarnings.cmake`, `cmake/OlyWasm.cmake`, `cmake/OlyModule.cmake`, `cmake/OlyGame.cmake`, `cmake/Dependencies.cmake`, `engine/runtime/CMakeLists.txt`, `engine/runtime/{core,ecs,bridge}/CMakeLists.txt`, `engine/runtime/core/tests/{CMakeLists.txt,unit_small_map.cpp}`.
- Modify: `CMakeLists.txt`, `engine/CMakeLists.txt`, `CMakePresets.json`, `Makefile`, todos os `#include` dos arquivos movidos, `engine/generator/emit_cpp.ts` (só os dois includes emitidos).

**Interfaces:**
- Produces (CMake, usadas por todas as tasks seguintes):
  - `oly_add_module(NAME <m> [DEPENDS <m...>] [OBJECT | HEADER_ONLY])` → alvo `oly_<m>`, alias `oly::<m>`; compila `src/*.cpp` (glob `CONFIGURE_DEPENDS`), `include/` como `PUBLIC`, `DEPENDS` ligados `PUBLIC` como `oly::<dep>`; acrescenta `<m>|<diretório relativo à raiz>|<deps separados por ;>` à propriedade global `OLY_MODULES`.
  - `oly_add_test(MODULE <m> NAME <suíte> SOURCES <arqs...> [SUPPORT <dirs...>])` → executável `<m>_<suíte>` em `${CMAKE_BINARY_DIR}/tests/`, ligado a `oly::<m>` e doctest, com `engine/runtime/core/tests/support/doctest_main.cpp`, `<módulo>/tests/support` e os `SUPPORT` como include; ctest `<m>.<suíte>`; aplica `oly_warnings`, `oly_wasm_exceptions`, `oly_wasm_node_env`.
  - `oly_add_contract_death(MODULE <m> SOURCE <arq> CASES <casos...>)` → só quando os asserts estão ligados; executável `<m>_contract_death`; ctest `<m>.contract_death.<caso>`.
  - `oly_add_bench(MODULE <m> NAME <n> SOURCES <arqs...>)` → executável `<m>_bench_<n>` em `${CMAKE_BINARY_DIR}/bench/` (mesmas flags de hoje: `-Wno-deprecated-declarations` no Emscripten, relatório de vetorização com `OLY_VEC_REPORT`).
  - `oly_add_game(<alvo> SCHEMA_JS <js> SOURCES <cpp...> DEPENDS <m...>)` → como o `engine_add_game` atual; liga só `oly::<m>` dos `DEPENDS`; registra `<alvo>|<diretório das fontes>|<deps>` na propriedade global `OLY_GAMES`; saída `.mjs` em `${CMAKE_BINARY_DIR}/games/`.
  - `oly_warnings(<alvo>)`, `oly_wasm_exceptions(<alvo>)`, `oly_wasm_node_env(<alvo>)`.
  - Opções: `OLY_BUILD_TESTS`, `OLY_BUILD_BENCH`, `OLY_SANITIZE`, `OLY_WERROR`, `OLY_VEC_REPORT`, `OLY_ENABLE_ASSERTS`, `OLY_BUILD_GAMES`. Até a Task 3, `oly::core` publica a definição `ECS_ENABLE_ASSERTS=<valor de OLY_ENABLE_ASSERTS resolvido>` (o macro só muda de nome na Task 3).
  - Presets: `wasm-engine`/`wasm-engine-release` → `wasm-games`/`wasm-games-release`; os demais mantêm o nome. Make: `ts`, `games-test` (no lugar de `engine-ts`, `engine-test`).

Decisões: `project(Oly VERSION 1.0 LANGUAGES CXX)`. `engine/CMakeLists.txt` continua como agregador da engine: `add_subdirectory(runtime)` e, com `OLY_BUILD_GAMES`, o jogo fixture e os testes TS (nesta task ainda com os caminhos `build/ts/...` de hoje; a Task 7 os move para `engine/testing/CMakeLists.txt`). `engine/runtime/CMakeLists.txt` adiciona `core`, `ecs`, `bridge` nessa ordem. Dependências: `core` sem deps; `ecs DEPENDS core`; `bridge DEPENDS ecs core OBJECT`; `fixture_game DEPENDS ecs bridge`. Os testes do `bridge` usam `SUPPORT engine/runtime/ecs/tests/support` (componentes `A`, `B`, `Tag`). O `core` ganha uma suíte mínima para ter ciclo de teste próprio.

- [ ] **Step 1: Criar o branch**

Run: `git switch -c refactor/oly-modulos develop`

- [ ] **Step 2: Escrever a suíte do `core`** (`engine/runtime/core/tests/unit_small_map.cpp`)

```cpp
TEST_CASE("SmallMap insere, encontra e mantém a ordem das chaves")   // find de chave ausente == nullptr; find(k) devolve o valor inserido
TEST_CASE("alignUp arredonda para o próximo múltiplo")               // alignUp(0,64)==0, alignUp(1,64)==64, alignUp(64,64)==64, alignUp(65,8)==72
```
Use a API atual de `SmallMap` (a que `world.hpp` usa: `find`, iteração `for (auto& [k, v] : map)`) e de `alignUp`.

- [ ] **Step 3: Mover os arquivos com `git mv`** segundo a tabela e trocar os includes: `"ecs/detail/X.hpp"` (X ∈ assert, align, small_map, type_name) → `"oly/core/X.hpp"`; `"ecs/X.hpp"` → `"oly/ecs/X.hpp"`; `"engine/X.hpp"` → `"oly/bridge/X.hpp"`. Em `emit_cpp.ts`, os includes emitidos passam a `"oly/ecs/entity.hpp"` e `"oly/bridge/schema_registry.hpp"`.

- [ ] **Step 4: Escrever `cmake/*.cmake`, os `CMakeLists.txt` dos módulos e a raiz** com as interfaces acima. A raiz faz só: opções/presets (`include(OlyOptions)` …), `include(Dependencies)` (doctest com `OLY_BUILD_TESTS`, benchmark com `OLY_BUILD_BENCH`), `enable_testing()`, `add_subdirectory(engine)`. Registros de teste: `ecs` — `unit_table`, `unit_type_info`, `unit_entity`, `unit_archetype`, `unit_world` (+ `support/alloc_counter.cpp`), `invariants`, `fuzz_vs_model`, `nontrivial`, `queries`, `command_buffer`, `raw_query`; `contract_death` com os 10 casos atuais; `bridge` — `c_api` (`c_api.cpp`, `test_schema_registry.cpp`); `core` — `unit_small_map`. Benchmarks `ecs`: `iterate`, `transitions`, `fragmented` (fonte `fragmentation.cpp`), `flush`. Testes TS: `ts.core.unit` (hoje `build/ts/test/unit/`) e `ts.testing.wasm` (hoje `build/ts/test/wasm/`, com `ENGINE_FIXTURE_MODULE=$<TARGET_FILE:fixture_game>`).

- [ ] **Step 5: Atualizar `CMakePresets.json` e `Makefile`**: `ECS_*` → `OLY_*`, `ENGINE_BUILD_FIXTURE` → `OLY_BUILD_GAMES`, presets `wasm-games`/`wasm-games-release` (configure, build e test). Make: `ts` (por enquanto `cd engine && npm ci se preciso && npm run build`) e `games-test: ts` + `$(MAKE) test PRESET=wasm-games`.

- [ ] **Step 6: Verificar os nomes no ctest**

Run: `$DC sh -c 'cmake --preset debug-asan >/dev/null && ctest --preset debug-asan -N'`
Expected: lista `core.unit_small_map`, `ecs.unit_world`, `ecs.contract_death.add_during_each`, …, `bridge.c_api`; nenhum nome sem prefixo de módulo.

- [ ] **Step 7: Rodar tudo**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`
Expected: todos verdes, mesmo número de casos de `contract_death` de antes (10).

- [ ] **Step 8: Conferir os presets do estudo de custo e o relwithdebinfo**

Run: `$DC make build PRESET=relwithdebinfo`; `$WC make test PRESET=native-clang`; `$WC make test PRESET=wasm-simd`
Expected: compila; testes verdes; `build/release/bench/ecs_bench_iterate` existe após o build do release.

- [ ] **Step 9: Commit**

```bash
git add -A
git commit -m "refactor: árvore por módulos (core, ecs, bridge) e build com oly_add_module"
```

---

### Task 2: `oly.lint.boundaries` (regras C++)

**Files:**
- Create: `tools/check_boundaries.py`, `tools/test_check_boundaries.py`
- Modify: `CMakeLists.txt` (manifesto + registro no ctest)

**Interfaces:**
- Consumes: propriedades globais `OLY_MODULES` e `OLY_GAMES` (Task 1).
- Produces: `${CMAKE_BINARY_DIR}/oly_boundaries.json` no formato `{"modules": [{"name": "ecs", "directory": "engine/runtime/ecs", "depends": ["core"]}, …], "games": [{"name": "fixture_game", "directory": "engine/fixture", "depends": ["ecs", "bridge"]}]}` (diretórios relativos à raiz), escrito no fim da configuração. CLI: `python3 -I tools/check_boundaries.py --root <repo> --manifest <json>`; sai 0 sem violações; sai 1 imprimindo uma linha `<arquivo relativo>:<linha>: <motivo>` por violação. A Task 8 acrescenta regras TS a este script.

Regras C++ (arquivos `.hpp`/`.cpp` fora de `build/` e `node_modules/`; include = `#include "oly/<x>/…"`):
- **R1** arquivo dentro do diretório de um módulo ou jogo `<m>` inclui `oly/<x>/` com `x ≠ m` e `x ∉ depends(m)` → motivo `"<m> inclui oly/<x> sem declarar <x> em DEPENDS"`.
- **R2** arquivo fora de `engine/runtime/<x>/` inclui `oly/<x>/detail/` → motivo `"oly/<x>/detail é interno ao módulo <x>"`.

- [ ] **Step 1: Escrever os testes do script** (`tools/test_check_boundaries.py`, `unittest`, mesmo esquema de import de `tools/test_bench_ladder.py`; cada teste monta uma árvore num diretório temporário e um manifesto)

```python
def test_include_declarado_passa(self)            # ecs inclui oly/core, ecs depends [core] → código 0, saída vazia
def test_include_nao_declarado_falha(self)        # bridge inclui oly/core com depends [ecs] → 1; saída contém "engine/runtime/bridge/src/x.cpp:3:" e "sem declarar core"
def test_detail_de_fora_falha(self)               # bridge inclui oly/ecs/detail/table.hpp → 1; "oly/ecs/detail é interno ao módulo ecs"
def test_detail_nos_testes_do_modulo_passa(self)  # engine/runtime/ecs/tests/t.cpp inclui oly/ecs/detail/table.hpp → 0
def test_jogo_usa_os_depends_do_jogo(self)        # jogo com depends [ecs, bridge] incluindo oly/core → 1
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: FAIL/ERROR (`check_boundaries` não existe).

- [ ] **Step 3: Implementar `tools/check_boundaries.py`** com a CLI e as regras acima (expressão regular por linha; o módulo dono de um arquivo é o de maior prefixo de diretório).

- [ ] **Step 4: Rodar os testes do script**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: OK (5 testes).

- [ ] **Step 5: Manifesto e ctest.** No fim da raiz, montar o JSON a partir de `OLY_MODULES`/`OLY_GAMES` e `file(WRITE …)`. Fora do Emscripten e com `find_package(Python3 COMPONENTS Interpreter)`, registrar `oly.lint.boundaries` (`${Python3_EXECUTABLE} -I tools/check_boundaries.py --root ${PROJECT_SOURCE_DIR} --manifest ${CMAKE_BINARY_DIR}/oly_boundaries.json`) e `oly.lint.boundaries_selftest` (o arquivo de testes do Step 1).

- [ ] **Step 6: Verificar no repositório e uma violação à mão**

Run: `$DC make test T='^oly\.'`
Expected: 2 testes, verdes.
Depois, acrescentar `#include "oly/bridge/c_api.hpp"` em `engine/runtime/ecs/src/world.cpp` e rodar o script direto (o build quebraria antes do ctest, porque o `ecs` não enxerga os headers do `bridge`): `$DC sh -c 'cmake --preset debug-asan >/dev/null && python3 -I tools/check_boundaries.py --root . --manifest build/debug-asan/oly_boundaries.json'` → código 1 com `engine/runtime/ecs/src/world.cpp:<linha>: ecs inclui oly/bridge sem declarar bridge em DEPENDS`. Desfazer.

- [ ] **Step 7: Commit**

```bash
git add tools/check_boundaries.py tools/test_check_boundaries.py CMakeLists.txt
git commit -m "build: oly.lint.boundaries verifica DEPENDS e uso de detail/ no ctest"
```

---

### Task 3: Nome Oly no C++ (namespaces, assert, opções)

**Files:**
- Modify: todos os `.hpp`/`.cpp` de `engine/runtime/**`, `engine/fixture/systems.cpp`, `engine/generator/emit_cpp.ts`, `engine/test/unit/generator.test.ts`, `engine/runtime/ecs/tests/support/model.hpp`, `cmake/OlyOptions.cmake`/`OlyModule.cmake`

**Interfaces:**
- Produces:
  - `namespace oly::core`: `AssertHandler`, `setAssertHandler`, `detail::assertFail`, `SmallMap`, `alignUp`, `alignedAllocate`, `alignedFree`, `typeName<T>()` (sai de `detail`, é utilitário público do core). Macros `OLY_ASSERT(cond, fmt, ...)` e `OLY_ENABLE_ASSERTS` (definição pública de `oly::core`).
  - `namespace oly::ecs` para tudo do ECS (o `ecs::detail` atual vira `oly::ecs::detail`); dentro dele, o core é citado como `core::SmallMap` etc.
  - `namespace oly::bridge`: `schemaHash()`, `schemaComponentIds()` (tipo `std::span<const oly::ecs::ComponentId>`).
  - Código gerado: `oly::ecs::Entity` nos campos `entity`, registro em `namespace oly::bridge { … }` com `oly::ecs::componentId<T>()`; comentário de topo `// Gerado por oly-codegen: não edite.`
  - Fuzz: `OLY_FUZZ_SEED`, `OLY_FUZZ_OPS`.

- [ ] **Step 1: Atualizar o teste do gerador** (`generator.test.ts`): as expressões passam a exigir `oly::ecs::Entity owner;`, `oly::ecs::componentId<P>\(\),\s*oly::ecs::componentId<T>\(\)` e `namespace oly::bridge`.

- [ ] **Step 2: Ver falhar**

Run: `$WC sh -c 'make ts && node --test build/ts/test/unit/'`
Expected: FAIL nos testes de struct e de registro.

- [ ] **Step 3: Renomear namespaces e macros** no C++, no `emit_cpp.ts` e no CMake (`OLY_ENABLE_ASSERTS=<0|1>` como definição pública de `oly::core`). Testes e benchmarks usam o nome qualificado completo (`oly::ecs::World`), sem alias de namespace.

- [ ] **Step 4: Rodar tudo**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`
Expected: verdes.

- [ ] **Step 5: Varredura**

Run: `git grep -nE '\bECS_[A-Z_]+|(^|[^:])\becs::|namespace (ecs|engine)\b|\bengine::' -- engine cmake CMakeLists.txt CMakePresets.json Makefile tools`
Expected: só ocorrências da API C `engine*`/`ENGINE_API`/`Engine*` (Task 6) e de `tools/run_ladder.sh` (`ECS_LADDER_EXTRA_ARGS`, Task 10); nenhuma em C++ fora da API C.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: namespaces oly::core, oly::ecs, oly::bridge e OLY_ASSERT"
```

---

### Task 4: Superfície pública do ECS (`detail/`, `component.hpp`, `diagnostics.hpp`, acessores de teste)

**Files:**
- Move (`git mv`): `oly/ecs/{table,archetype}.hpp` → `oly/ecs/detail/`; `oly/ecs/type_info.hpp` → `oly/ecs/component.hpp`; `src/type_info.cpp` → `src/component.cpp`.
- Create: `oly/ecs/diagnostics.hpp`, `src/diagnostics.cpp`, `oly/ecs/detail/{entity_store,query_cache,command_plan,test_access}.hpp`, `engine/runtime/ecs/tests/surface.cpp`
- Modify: `world.hpp`, `entity.hpp`, `query.hpp`, `command_buffer.hpp`, `raw_query.hpp`, `world.cpp`, `command_buffer.cpp`, testes que usam os ganchos (`command_buffer.cpp`, `unit_entity.cpp`, `unit_world.cpp`), `bench/{iterate,transitions}.cpp`, `bridge/src/c_api.cpp`

**Interfaces:**
- Produces (headers públicos; tudo o mais em `oly::ecs::detail`):
  - `entity.hpp`: `Entity`, `NullEntity`, `EntityHash` (sem `EntityRecord`/`EntityStore`, que vão para `detail/entity_store.hpp`).
  - `component.hpp`: `ComponentId`, `componentId<T>()`, `TypeInfo`, `typeInfo(ComponentId)`, `registeredTypeCount()`.
  - `query.hpp`: `With`, `Without`, `Query<Ts...>` com públicos só `each`, `eachBlock`, `matchedCount`, `writes`. `QueryCacheBase`, `MatchedTable`, `IterationGuard`, `signatureMatches` em `detail/query_cache.hpp`; `initialize`/`matches`/`isMatched`/`onArchetypeCreated` privados com `friend class World` (as sobrescritas virtuais continuam chamáveis pelo ponteiro de base).
  - `command_buffer.hpp`: `CommandBuffer` com públicos `spawn`, `destroy`, `add<T>`, `remove<T>`, `addRaw`, `removeRaw` (renomeados na Task 5), `commandCount`, `empty`, `clear`, move/destrutor. Construtor por `EntityStore&`, `plan`, `netEffect`, `forEach`, `store` privados (`friend class World`, `friend struct detail::CommandBufferTestAccess`). `CommandOperation`, `CommandView`, `NetComponentOperation`, `EntityPlan`, `NetEffect`, `CommandPlan` em `detail/command_plan.hpp`.
  - `diagnostics.hpp`: `struct ArchetypeStats` (campo `signature` como `std::vector<ComponentId>`), `struct WorldStats`, `bool checkInvariants(const World&, std::string* why = nullptr)`, `WorldStats stats(const World&)`, `uint64_t signatureLookupCount(const World&)`, `uint32_t archetypeCount(const World&)`. `World` declara essas funções `friend`; `World::checkInvariants`, `stats`, `signatureLookupCount`, `archetypeCount` deixam de existir.
  - `detail/test_access.hpp`: `struct WorldTestAccess { static void flushSequential(World&, CommandBuffer&); };`, `struct CommandBufferTestAccess { template <class F> static void forEach(const CommandBuffer&, F&& callback); };`, `struct EntityStoreTestAccess { static void setGeneration(EntityStore&, uint32_t index, uint32_t generation) noexcept; };`. `World::flushSequential` e `EntityStore::setGenerationForTest` deixam de ser membros públicos.

- [ ] **Step 1: Escrever o teste de superfície** (`engine/runtime/ecs/tests/surface.cpp`, registrado como `ecs.surface`). Conceitos-sonda por template (o acesso privado é falha de substituição só dentro de template):

```cpp
template <class W> concept HasMemberStats = requires(W& world) { world.stats(); };
template <class W> concept HasMemberCheckInvariants = requires(W& world) { world.checkInvariants(nullptr); };
template <class W, class C> concept HasFlushSequential = requires(W& world, C& commands) { world.flushSequential(commands); };
template <class C> concept HasPlan = requires(C& commands) { commands.plan(); };
template <class C> concept HasForEach = requires(C& commands) { commands.forEach([](auto&) {}); };
template <class C> concept HasStore = requires(C& commands) { commands.store(); };
template <class Q> concept HasOnArchetypeCreated = requires(Q& query, oly::ecs::detail::Archetype& archetype) { query.onArchetypeCreated(archetype); };

TEST_CASE("World e CommandBuffer não expõem diagnóstico nem ganchos de teste") {
  static_assert(!HasMemberStats<oly::ecs::World>);
  static_assert(!HasMemberCheckInvariants<oly::ecs::World>);
  static_assert(!HasFlushSequential<oly::ecs::World, oly::ecs::CommandBuffer>);
  static_assert(!HasPlan<oly::ecs::CommandBuffer> && !HasForEach<oly::ecs::CommandBuffer> && !HasStore<oly::ecs::CommandBuffer>);
  static_assert(!HasOnArchetypeCreated<oly::ecs::Query<int>>);
  static_assert(!std::is_constructible_v<oly::ecs::CommandBuffer, oly::ecs::detail::EntityStore&>);
}
TEST_CASE("diagnóstico por funções livres") {   // world.spawn… ainda é create nesta task
  // cria uma entidade com A; CHECK(oly::ecs::checkInvariants(world)); CHECK(oly::ecs::archetypeCount(world) == 2);
  // CHECK(oly::ecs::stats(world).liveEntities == 1); CHECK(oly::ecs::signatureLookupCount(world) > 0)
}
```
(Use um componente de `support/components.hpp` em vez de `int` se `Query<int>` não for instanciável.)

- [ ] **Step 2: Ver falhar**

Run: `$DC make test T='ecs\.surface'`
Expected: falha de compilação (`oly/ecs/diagnostics.hpp` não existe) ou `static_assert` falhando.

- [ ] **Step 3: Implementar a divisão** conforme as interfaces. `diagnostics.cpp` recebe `stats` e a contagem; o corpo de `checkInvariants` e seus auxiliares (`checkArchetypes` … `checkEdges`) podem continuar em `world.cpp` como membros privados chamados pela função livre amiga. Testes, benchmarks e a API C passam a usar as funções livres e os acessores de `detail/test_access.hpp`.

- [ ] **Step 4: Rodar tudo e conferir as fronteiras**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`
Expected: verdes, inclusive `oly.lint.boundaries`. O `c_api.cpp` não pode incluir `oly/ecs/detail/` diretamente; até a Task 5 ele chega a `Table` só pelo `raw_query.hpp` (a Task 5 elimina esse uso com `QueryIteration`).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: superfície do ECS com detail/, component.hpp e diagnostics.hpp"
```

---

### Task 5: Vocabulário do ECS e API por ID (`spawn`, `ById`, `QueryById`, `QueryIteration`)

**Files:**
- Move (`git mv`): `oly/ecs/raw_query.hpp` → `oly/ecs/query_by_id.hpp`; `tests/raw_query.cpp` → `tests/query_by_id.cpp`
- Modify: `world.hpp`, `world.cpp`, `command_buffer.hpp/.cpp`, `tests/**` (`create` → `spawn`), `tests/contract_death.cpp`, `tests/support/model.hpp`, `bench/*.cpp`, `bridge/src/c_api.cpp`, `engine/fixture/systems.cpp`, `engine/runtime/ecs/tests/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `World::spawn()`, `template <class... Ts> Entity World::spawn(Ts&&...)`; `World::create` deixa de existir. `CommandBuffer::spawn()` fica.
  - `void World::addById(Entity, ComponentId, void* value)`, `void removeById(Entity, ComponentId)`, `bool hasById(Entity, ComponentId) const noexcept`, `void* tryGetById(Entity, ComponentId) noexcept` (nulo se morta, ausente ou tag), `void* getById(Entity, ComponentId)` (assert `"getById e=%u:%u type=%.*s"` se morta ou ausente; tag também é erro). `CommandBuffer::addById`/`removeById`. As versões template continuam com o caminho próprio.
  - `query_by_id.hpp`:
    ```cpp
    struct QueryByIdSpec { std::vector<ComponentId> read{}, write{}, with{}, without{}; };
    class BlockById {   // valor leve, vale enquanto a iteração existir
      uint32_t count() const noexcept;
      std::span<const Entity> entities() const noexcept;
      void* column(size_t index) const noexcept;   // index na ordem read seguida de write
    };
    class QueryIteration {   // só movível; ++iterating ao nascer, -- no destrutor (uma vez)
      uint32_t blockCount() const noexcept;
      BlockById block(uint32_t index) const noexcept;
      std::span<const uintptr_t> descriptors() const noexcept;   // blockCount × (2 + components().size()): [count, entidades, colunas...]
    };
    class QueryById {
      template <class F> void eachBlock(F&& callback);   // callback(const BlockById&), só tabelas não vazias
      QueryIteration iterate();
      std::span<const ComponentId> components() const noexcept;
      size_t matchedCount() const noexcept;
    };
    QueryById& World::queryById(const QueryByIdSpec&);   // cacheada pela spec
    ```
    `beginIteration`/`endIteration`/`iterationDepth`/`matchedTables` deixam de ser públicos; `RawMatchedTable` vira `detail::MatchedTableById`. Os descritores de `iterate()` vêm de um pool da `QueryById` (um `std::vector<uintptr_t>` por nível de aninhamento; cresce até a maior profundidade vista e não encolhe). Encerrar uma `QueryIteration` que não é a mais interna da sua `QueryById` é erro de contrato: `OLY_ASSERT(…, "iterate: iteração encerrada fora de ordem (nível=%u, profundidade=%u)", …)`. `eachBlock` usa `iterate()` por dentro.
  - Mensagens de assert com os verbos novos: `"spawn durante iteração=%u"`, `"queryById: tag em read/write type=%.*s"`, `"queryById: componente repetido …"`, `"queryById: componente desconhecido id=%u"` (mantendo `e=`/tipo onde já havia).
  - `contract_death` (`ecs`): `add_during_each`, `get_dead_entity`, `flush_during_each`, `command_after_destroy`, `destroy_during_each_block`, `query_by_id_tag_in_read` {`queryById`, `tag`}, `query_by_id_repeated_component` {`queryById`, `repetido`}, `query_by_id_unknown_component` {`queryById`, `desconhecido`}, `query_by_id_iteration_out_of_order` {`iterate`, `fora de ordem`}, `add_during_query_by_id_iteration` {`add`}, `get_by_id_missing` {`getById`, `e=`}, `spawn_during_each` {`spawn`, `iteração`}.

- [ ] **Step 1: Reescrever `tests/query_by_id.cpp`** (casos atuais adaptados ao `BlockById` + os novos)

```cpp
TEST_CASE("eachBlock entrega count, entidades e colunas na ordem read, write")   // valores escritos via column(1) aparecem em get<B>
TEST_CASE("iterate marca o World como iterando até o destrutor")                // isIterating() true dentro do escopo, false depois
TEST_CASE("iterate aninhado mantém os blocos de cada nível")                    // outer.descriptors() igual antes e depois de um inner na mesma query
TEST_CASE("QueryIteration movida encerra a iteração uma vez só")                 // auto b = std::move(a); após destruir os dois, isIterating()==false e checkInvariants ok
TEST_CASE("exceção no callback de eachBlock encerra a iteração")                 // CHECK_THROWS(query.eachBlock(lança)); depois isIterating()==false e spawn funciona
TEST_CASE("iterate em regime não aloca")                                        // primeiro iterate() aquece; AllocCounter: segundo iterate() na mesma profundidade == 0 alocações
TEST_CASE("getById e tryGetById")                                               // tryGetById de ausente/tag/morta == nullptr; getById devolve o mesmo endereço de get<A>
```
Registrar com `SOURCES query_by_id.cpp support/alloc_counter.cpp` (o teste de alocação só roda onde `AllocCounter` funciona hoje — use o mesmo guarda de `unit_world.cpp`).

- [ ] **Step 2: Escrever/renomear os casos de `contract_death`** da lista acima (função + nome no `CASES`).

- [ ] **Step 3: Ver falhar**

Run: `$DC make test T='ecs\.(query_by_id|contract_death)'`
Expected: falha de compilação (`QueryById`, `iterate`, `getById` inexistentes).

- [ ] **Step 4: Implementar** as interfaces; trocar `create` → `spawn` em todo o repositório C++ (testes, benchmarks, `model.hpp`); `c_api.cpp` passa a usar `queryById`/`iterate`/`*ById` mantendo os nomes C `engine*` (renomeados na Task 6), guardando a `QueryIteration` ativa da query numa pilha dentro do `EngineWorld` (a Task 6 formaliza isso como `OlyIteration`).

- [ ] **Step 5: Rodar tudo**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`
Expected: verdes; `ctest -N` mostra 12 casos `ecs.contract_death.*`.

- [ ] **Step 6: Revisão de desempenho do diff** — despachar o agente `perf-reviewer` sobre `git diff HEAD` (foco: `World::spawn<Ts...>`, `get<T>`, `Query::each` sem mudança de código gerado; pool sem alocação). Corrigir o que ele confirmar.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: spawn, variantes ById e QueryById com QueryIteration RAII"
```

---

### Task 6: API C `oly*` e `OlyWorld` opaco

**Files:**
- Create: `engine/runtime/bridge/include/oly/bridge/world.hpp`, `engine/runtime/bridge/tests/contract_death.cpp`
- Delete (`git rm`): `engine/runtime/bridge/include/oly/bridge/world_handle.hpp`
- Modify: `oly/bridge/c_api.hpp`, `src/c_api.cpp`, `tests/c_api.cpp`, `tests/test_schema_registry.cpp`, `engine/fixture/systems.cpp`, `engine/ts/module.ts`, `engine/ts/bridge.ts`, `engine/ts/query.ts`, `engine/ts/runtime.ts`, `engine/test/wasm/fixture.ts`, `engine/bench/boundary.ts`, `engine/runtime/bridge/CMakeLists.txt`

**Interfaces:**
- Produces (C, `extern "C"`, `OLY_API` = `EMSCRIPTEN_KEEPALIVE` no Emscripten):
  ```
  struct OlyWorld; struct OlyQuery; struct OlyIteration;     // opacos
  struct OlyEntity { uint32_t index; uint32_t generation; };
  OlyWorld* olyWorldCreate();  void olyWorldDestroy(OlyWorld*);
  int32_t olyWorldCheckInvariants(OlyWorld*, char* message, uint32_t capacity);
  uint32_t olySchemaHash();  uint32_t olySchemaComponentCount();  uint32_t olySchemaComponentId(uint32_t schemaIndex);
  uint32_t olyComponentSize(uint32_t componentId);  uint32_t olyComponentAlignment(uint32_t componentId);
  void olyEntitySpawn(OlyWorld*, OlyEntity* out);
  void olyEntityDestroy(OlyWorld*, uint32_t index, uint32_t generation);
  int32_t olyEntityIsAlive(OlyWorld*, uint32_t index, uint32_t generation);
  void olyComponentAdd(OlyWorld*, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
  void olyComponentRemove(OlyWorld*, uint32_t index, uint32_t generation, uint32_t componentId);
  int32_t olyComponentHas(OlyWorld*, uint32_t index, uint32_t generation, uint32_t componentId);
  void* olyComponentGet(OlyWorld*, uint32_t index, uint32_t generation, uint32_t componentId);   // tryGetById
  OlyQuery* olyQueryCreate(OlyWorld*, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount);
  OlyIteration* olyQueryBegin(OlyWorld*, OlyQuery*, uint32_t* outBlockCount, const uintptr_t** outDescriptors);
  void olyQueryEnd(OlyWorld*, OlyIteration*);
  ```
  `OlyWorld` é definido só em `c_api.cpp`: o `World` e uma pilha de slots de iteração (`std::vector<std::unique_ptr<…>>` com `std::optional<QueryIteration>`; cresce até a maior profundidade e não encolhe, ponteiros estáveis). `olyQueryBegin` ocupa o próximo slot e devolve o próprio slot como `OlyIteration*`; `olyQueryEnd` exige que seja o slot do topo: `OLY_ASSERT(…, "olyQueryEnd: iteração encerrada fora de ordem")`. Os descritores valem até o `olyQueryEnd` daquela iteração.
- Produces (C++): `namespace oly::bridge { oly::ecs::World& worldOf(OlyWorld* world) noexcept; }` em `oly/bridge/world.hpp`.
- Produces (TS, ainda em `engine/ts/`): `OlyModule` (antes `EngineModule`) com `_olyWorldCreate` … `_olyQueryBegin(world, query, outBlockCount, outDescriptors): number`, `_olyQueryEnd(world, iteration)`; `OlyModuleFactory`. `Bridge`: `spawn(world)` (antes `create`), `queryCreate(…)` (antes `query`), `queryBegin(world, query): { iteration: number; blockCount: number; descriptors: number }`, `queryEnd(world, iteration)`. Copiar ou não os descritores em `Query.eachBlock` fica a critério (medir na Task 10 se mudar).

- [ ] **Step 1: Atualizar `tests/c_api.cpp`** para os nomes novos e acrescentar:

```cpp
TEST_CASE("olyQueryBegin aninhado preserva os descritores do externo")   // outer = olyQueryBegin(q); copia outer descritores; inner = olyQueryBegin(q) e olyQueryEnd(inner); memcmp(outer, cópia) == 0; olyQueryEnd(outer)
TEST_CASE("worldOf dá acesso ao World do handle")                        // olyEntitySpawn + oly::bridge::worldOf(world).isAlive(entity)
TEST_CASE("iteração repetida reaproveita o slot")                        // dois pares begin/end seguidos devolvem o mesmo OlyIteration*
```
E `tests/contract_death.cpp` do bridge com o caso `query_end_out_of_order` {`olyQueryEnd`, `fora de ordem`} (begin A, begin B, end A), registrado com `oly_add_contract_death(MODULE bridge …)`.

- [ ] **Step 2: Ver falhar**

Run: `$DC make test T='^bridge\.'`
Expected: falha de compilação (`olyWorldCreate` inexistente).

- [ ] **Step 3: Implementar** a API C, `worldOf`, `systems.cpp` do fixture (`OLY_API void fixtureMove(OlyWorld* world, float deltaTime)` com `oly::bridge::worldOf(world)`), e o lado TS (`module.ts`, `bridge.ts`, `query.ts`, `runtime.ts`, `fixture.ts`, `boundary.ts`).

- [ ] **Step 4: Rodar tudo**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`
Expected: verdes.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: API C oly* com OlyWorld opaco e iteração com buffer próprio"
```

---

### Task 7: Workspaces npm e divisão em pacotes (`@oly/core`, `@oly/codegen`, `@oly/testing`)

**Files:**
- Move (`git mv`): linhas TS da tabela.
- Create: `package.json`, `tsconfig.base.json`, `tsconfig.json` (solução, só `references`) na raiz; `engine/packages/core/{package.json,tsconfig.json,src/internal.ts,src/platform.d.ts,tests/tsconfig.json}`; `engine/tools/codegen/{package.json,tsconfig.json,tests/tsconfig.json}`; `engine/testing/{package.json,tsconfig.json,CMakeLists.txt}`
- Delete (`git rm`): `engine/package.json`, `engine/package-lock.json`, `engine/tsconfig.json`, `engine/module-package.json`
- Modify: `Makefile`, `engine/CMakeLists.txt`, `cmake/OlyGame.cmake`, `.gitignore`, imports de todos os `.ts` movidos

**Interfaces:**
- Produces:
  - Raiz: `package.json` (`"private": true`, `"workspaces": ["engine/packages/*", "engine/tools/*", "engine/testing", "games/*"]`, `devDependencies` `typescript` 5.6.3 e `@types/node` 20.17.6, script `"build": "tsc -b"`); `package-lock.json` único; `tsconfig.base.json` (`strict`, `target` ES2022, `module`/`moduleResolution` NodeNext, `composite`, `declaration`, `skipLibCheck`); `tsconfig.json` de solução (`"files": []`) com `references` a `engine/packages/core`, `engine/packages/core/tests`, `engine/tools/codegen`, `engine/tools/codegen/tests`, `engine/testing` (a Task 9 acrescenta o client), para o `tsc -b` da raiz.
  - `@oly/core` (`engine/packages/core`): `"type": "module"`, `"exports": { ".": { "types": "./dist/index.d.ts", "default": "./dist/index.js" }, "./internal": { "types": "./dist/internal.d.ts", "default": "./dist/internal.js" } }`. `tsconfig.json`: `rootDir` `src`, `outDir` `dist`, `lib: ["ES2022"]`, `types: []` (sem DOM e sem node: o compilador reprova API de ambiente). `src/platform.d.ts` declara só o mínimo de `TextDecoder` usado em `bridge.ts` (existe em node, navegador e worker). `src/internal.ts` reexporta: `Bridge`, `Heap`, `readScalar`, `writeScalar`, `makeView`, `computeLayout`, `scalarSize`, `scalarAlignment`, `fnv1a32`, `canonicalSchema`, `schemaHash`, `packEntity`, `MaxEntityGeneration`, tipo `OlyModule`. Nesta task `src/index.ts` continua `export *` (a Task 8 o restringe). Testes: `tests/tsconfig.json` (`references` ao core, `types: ["node"]`, `outDir` `../dist-tests`), importando `@oly/core` e `@oly/core/internal` pelo nome.
  - `@oly/codegen` (`engine/tools/codegen`): `"bin": { "oly-codegen": "./dist/main.js" }`, `references` ao core, `types: ["node"]`; `emit_cpp.ts` importa tipos de `@oly/core`. Teste em `tests/generator.test.ts` → `dist-tests/`.
  - `@oly/testing` (`engine/testing`, `"private": true`): `rootDir` `.`, `outDir` `dist`, `include` `["fixture", "tests", "bench"]`, `references` ao core, `types: ["node"]`. `fixture.ts` lê `OLY_FIXTURE_MODULE`.
  - CMake: `oly_add_game` chama `${NODE} ${PROJECT_SOURCE_DIR}/engine/tools/codegen/dist/main.js` (erro `"rode 'make ts' antes de configurar com OLY_BUILD_GAMES=ON"` se não existir); `engine/testing/CMakeLists.txt` declara `oly_add_game(fixture_game SCHEMA_JS ${…}/engine/testing/dist/fixture/schema.js SOURCES fixture/systems.cpp DEPENDS ecs bridge)` e `ts.testing.wasm` (`node --test engine/testing/dist/tests/wasm/`, com `OLY_FIXTURE_MODULE=$<TARGET_FILE:fixture_game>`); `engine/CMakeLists.txt` registra `ts.core.unit` (`engine/packages/core/dist-tests/unit/`) e `ts.codegen.unit` (`engine/tools/codegen/dist-tests/`) com `OLY_BUILD_GAMES` e faz `add_subdirectory(testing)`.
  - `Makefile`: `ts: ([ -d node_modules ] || $(NPM) ci --no-audit --no-fund) && $(NPM) run build` na raiz.
  - `.gitignore`: `dist`, `dist-tests`, `*.tsbuildinfo`.
  - `games/` vazio no git (sem `.gitkeep`); o glob `games/*` do workspace não casa nada até a fase 7.

- [ ] **Step 1: Mover os arquivos e criar os `package.json`/`tsconfig`.** Imports entre pacotes passam a usar o nome (`@oly/core`, `@oly/core/internal`); dentro de um pacote, caminhos relativos.

- [ ] **Step 2: Gerar o lock e compilar**

Run: `$WC sh -c 'rm -rf node_modules engine/node_modules && npm install --no-audit --no-fund && npm run build'`
Expected: `package-lock.json` novo na raiz; `engine/packages/core/dist/index.js`, `dist/internal.js`, `engine/tools/codegen/dist/main.js`, `engine/testing/dist/fixture/schema.js` existem; nenhum erro de `tsc` (em particular nenhum uso de DOM ou node em `core/src`).

- [ ] **Step 3: Conferir que o core rejeita API de ambiente** — acrescentar temporariamente `window.alert("x")` em `core/src/runtime.ts`; `npm run build` deve falhar com "Cannot find name 'window'". Desfazer.

- [ ] **Step 4: Rodar tudo**

Run: `$WC make games-test`, `$DC make test`
Expected: verdes, com `ts.core.unit`, `ts.codegen.unit`, `ts.testing.wasm` e `oly.lint.boundaries`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: workspaces npm com @oly/core, @oly/codegen e @oly/testing"
```

---

### Task 8: Superfície e nomes de `@oly/core`, regras TS do lint

**Files:**
- Create: `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/tests/wasm/iteration.test.ts`
- Modify: `engine/packages/core/src/{index,internal,errors,runtime,world,query,module}.ts`, usos em `engine/testing/**`, `engine/tools/codegen/**`, `tools/check_boundaries.py`, `tools/test_check_boundaries.py`

**Interfaces:**
- Produces:
  - `ContractError` (antes `EngineContractError`, `name` = `"ContractError"`), `SchemaMismatchError`.
  - `Runtime` (antes `EngineRuntime`) com públicos só `schema` e `createWorld()`; `loadRuntime(factory: OlyModuleFactory, schema: Schema): Promise<Runtime>` (antes `loadEngine`).
  - Em `@oly/core/internal`, além da lista da Task 7: `bridgeOf(runtime: Runtime): Bridge`, `moduleOf(runtime: Runtime): OlyModule`, `componentIdOf(runtime: Runtime, type: SchemaType): number`.
  - Fora do `.d.ts` público: `World.iterationDepth`, `World.assertUsable`, `Block.invalidate`, `Query.writes`, `Query.positionOf`, `Runtime.bridge`, `Runtime.module`, `Runtime.componentId` (mecanismo livre: `#private`, funções de módulo não reexportadas pelo `index.ts`, ou `WeakMap`). Não use `private` do TS: ele aparece no `.d.ts` e o teste de superfície o reprova.
  - `src/index.ts` com exports nomeados, exatamente a lista de `api/public-exports.txt` (uma por linha, ordem alfabética): `Block`, `ComponentLayout`, `ComponentType`, `ContractError`, `Entity`, `FieldLayout`, `Fields`, `NullEntity`, `OlyModuleFactory`, `Query`, `QuerySpec`, `RawField`, `ReadField`, `ReadView`, `Runtime`, `ScalarType`, `Schema`, `SchemaMismatchError`, `SchemaType`, `SpawnValues`, `TagType`, `ValueOf`, `World`, `WriteField`, `WriteView`, `component`, `defineSchema`, `entityGeneration`, `entityIndex`, `formatEntity`, `loadRuntime`, `tag`.
  - Regras TS do `check_boundaries.py` (arquivos `.ts` fora de `node_modules/`, `dist/`, `dist-tests/`, `build/`):
    - **R3** import de `@oly/core/internal` fora de `engine/` → `"@oly/core/internal só pode ser usado dentro de engine/"`.
    - **R4** import de `@oly/client` em `engine/packages/core/` → `"@oly/core não pode depender de @oly/client"`.
    - **R5** import relativo que resolve para fora do pacote do arquivo (pacote = diretório mais próximo com `package.json`) → `"import relativo atravessa pacotes; use o nome do pacote"`.

- [ ] **Step 1: Escrever `surface.test.ts`** com a API do compilador TypeScript (`typescript` já é dependência da raiz): cria um `Program` sobre `engine/packages/core/dist/index.d.ts`, pega `checker.getExportsOfModule` e as propriedades das classes exportadas.

```ts
test("exports públicos batem com api/public-exports.txt")   // nomes ordenados === linhas do arquivo
test("World expõe só a API do glossário")   // propriedades === [add, checkInvariants, destroy, dispose, get, handle, has, isAlive, query, remove, set, spawn]
test("Query expõe só eachBlock")
test("Block expõe só count, entity, read, write")
test("Runtime expõe só schema e createWorld")
test("ContractError tem name ContractError")   // new ContractError("x").name === "ContractError"
```

- [ ] **Step 2: Escrever os testes WASM do Review Focus** (`engine/testing/tests/wasm/iteration.test.ts`)

```ts
test("exceção no callback de eachBlock encerra a iteração")   // assert.throws(() => query.eachBlock(() => { throw new Error("x") })); depois world.spawn([Position]) funciona e world.checkInvariants() === null
test("eachBlock aninhado em outra query mantém o bloco externo")   // externo lê Position.x antes e depois de um eachBlock interno de [Velocity]; valores iguais aos gravados
test("query criada dentro de eachBlock (memória pode crescer) não corrompe o bloco externo")   // dentro do callback: world.query com read de todos os componentes e um eachBlock; depois block.read(Position).x.get(row) ainda bate com o valor gravado para block.entity(row)
```

- [ ] **Step 3: Escrever os testes das regras TS** em `tools/test_check_boundaries.py`

```python
def test_internal_fora_de_engine_falha(self)       # games/x/src/a.ts importa "@oly/core/internal" → 1, motivo R3 com arquivo:linha
def test_internal_dentro_de_engine_passa(self)     # engine/testing/tests/a.ts importa "@oly/core/internal" → 0
def test_core_importando_client_falha(self)        # engine/packages/core/src/a.ts importa "@oly/client" → 1
def test_relativo_entre_pacotes_falha(self)        # engine/packages/client/src/a.ts importa "../../core/src/world.js" → 1
```

- [ ] **Step 4: Ver falhar**

Run: `$WC make games-test`; `$DC python3 -I tools/test_check_boundaries.py`
Expected: `ts.core.unit` falha (exports a mais, membros internos visíveis, `ContractError` inexistente); 4 testes novos do script falham.

- [ ] **Step 5: Implementar** os nomes, o `index.ts` restrito, os acessores de `internal.ts`, os membros escondidos e as regras R3–R5. Atualizar `engine/testing/**` (`loadRuntime`, `moduleOf(runtime)` para os exports do fixture, `ContractError`) e `boundary.ts`.

- [ ] **Step 6: Rodar tudo**

Run: `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`, `$DC make test`
Expected: verdes.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: superfície pública de @oly/core e regras TS de fronteira"
```

---

### Task 9: `@oly/client`

**Files:**
- Create: `engine/packages/client/{package.json,tsconfig.json,src/index.ts,src/module_factory.ts,tests/tsconfig.json,tests/module_factory.test.ts}`
- Modify: `tsconfig.json` (raiz, nova referência), `engine/CMakeLists.txt` (`ts.client.unit`), `package-lock.json`

**Interfaces:**
- Consumes: `OlyModuleFactory` de `@oly/core` (superfície pública; o client não importa `./internal`).
- Produces: `moduleFactoryFromUrl(url: string | URL, importModule?: (href: string) => Promise<unknown>): OlyModuleFactory`. Resolve `url` para `href` absoluto (contra `globalThis.location?.href` quando relativa — com `lib: ["DOM"]`), importa o `.mjs` com `importModule` (padrão: `import(href)`), exige `default` função (senão lança `Error` cuja mensagem contém o `href` e `"não exporta uma fábrica Emscripten"`) e chama `default({ locateFile: (path: string) => new URL(path, href).href })`, para o `.wasm` ser buscado ao lado do `.mjs`. `tsconfig` do client: `lib: ["ES2022", "DOM"]`, `references` ao core.

- [ ] **Step 1: Escrever os testes** (`node:test`, com `importModule` falso)

```ts
test("importa o .mjs pela URL absoluta")              // importModule recebe "https://x/games/demo.mjs"
test("locateFile resolve o .wasm ao lado do .mjs")    // a fábrica falsa recebe moduleArg; moduleArg.locateFile("demo.wasm") === "https://x/games/demo.wasm"
test("devolve o módulo criado pela fábrica")          // await factory() === objeto devolvido pelo default falso
test("default ausente é erro com a URL")              // rejects com mensagem contendo "https://x/games/demo.mjs" e "não exporta uma fábrica Emscripten"
```

- [ ] **Step 2: Ver falhar**

Run: `$WC sh -c 'npm install --no-audit --no-fund && npm run build'`
Expected: erro de compilação (`moduleFactoryFromUrl` inexistente).

- [ ] **Step 3: Implementar** `module_factory.ts` e `index.ts` (exporta só `moduleFactoryFromUrl`).

- [ ] **Step 4: Rodar**

Run: `$WC make games-test`, `$DC make test T='^oly\.'`
Expected: verdes, com `ts.client.unit`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: @oly/client com fábrica do módulo Emscripten por URL"
```

---

### Task 10: Benchmarks em `docs/benchmarks/` e reexecução das metas

**Files:**
- Move (`git mv`): `bench/results/README.md` → `docs/benchmarks/README.md`; `bench/results/{m*,e1_*}` → `docs/benchmarks/ecs/`; `bench/results/wasm/**` → `docs/benchmarks/wasm/**`; `bench/results/engine/**` → `docs/benchmarks/bridge/**`
- Create: `docs/benchmarks/ecs/r1_{iterate,fragmented,flush}.json`, `docs/benchmarks/bridge/boundary_oly.json`
- Modify: `docs/benchmarks/README.md`, `docs/benchmarks/bridge/README.md` (e o README de `wasm/`, se houver), `tools/run_ladder.sh` (`docs/benchmarks/wasm/runs/$bench`, `OLY_LADDER_EXTRA_ARGS`, nomes `ecs_bench_*`), `tools/perf_fragmented.sh` (uso com `ecs_bench_fragmented`), `tools/extract_codegen.sh` e `tools/bench_ladder.py` se citarem caminhos ou nomes antigos

- [ ] **Step 1: Mover e atualizar caminhos e comandos** nos READMEs vivos (sem editar JSON/TXT antigos).

- [ ] **Step 2: Compilar e medir** (repetir até 3 vezes se uma razão sair da meta, por causa do ruído do WSL2)

```sh
$DC make build PRESET=release
P="--benchmark_repetitions=10 --benchmark_report_aggregates_only=true"
$DC ./build/release/bench/ecs_bench_iterate $P --benchmark_out=docs/benchmarks/ecs/r1_iterate.json
$DC ./build/release/bench/ecs_bench_fragmented $P --benchmark_out=docs/benchmarks/ecs/r1_fragmented.json
$DC ./build/release/bench/ecs_bench_flush $P --benchmark_out=docs/benchmarks/ecs/r1_flush.json
```

- [ ] **Step 3: Conferir as metas**

```sh
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/r1_iterate.json --name BM_IterateSingle_ECS/1000000 --baseline BM_IterateSingle_SoA/1000000 --max 1.10
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/r1_fragmented.json --name BM_IterateFragmented/64 --baseline BM_IterateSingle_SoA/1000000 --max 1.25
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/r1_flush.json --name BM_FlushCoalesced/1000 --baseline BM_DirectAdds/1000 --max 0.5
```
Expected: as três passam. Regressão confirmada nas repetições bloqueia a entrega: parar e investigar com `superpowers:systematic-debugging`.

- [ ] **Step 4: Benchmark da fronteira**

```sh
$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'
$WC node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/boundary_oly.json --commit $(git rev-parse --short HEAD)
```
Expected: JSON gravado. Registrar no `bridge/README.md` uma tabela comparando com `boundary_fase1.json` (sem meta numérica).

- [ ] **Step 5: Registrar** as razões `r1_*` na tabela e no texto de `docs/benchmarks/README.md` (mesmo formato da linha dos `e1_*`).

- [ ] **Step 6: Conferir a escada** (só a execução curta, sem registrar resultado)

Run: `OLY_LADDER_EXTRA_ARGS=--benchmark_min_time=0.01s tools/run_ladder.sh 1 ecs_bench_iterate V0`
Expected: grava `docs/benchmarks/wasm/runs/ecs_bench_iterate/…`; apagar o arquivo depois.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "bench: resultados em docs/benchmarks e reexecução r1 das metas NF-P1, NF-P2 e NF-P5"
```

---

### Task 11: Documentação viva, varredura de nomes e verificação final

**Files:**
- Modify: `CLAUDE.md`, `.claude/skills/add-test-case/SKILL.md`, `.claude/skills/bench-compare/SKILL.md`, `.claude/agents/perf-reviewer.md`, `.claude/agents/spec-conformance-reviewer.md`

- [ ] **Step 1: Reescrever `CLAUDE.md`** para: árvore (§3 do spec), comandos (`make test T='^ecs\.'`, `make ts`, `make games-test`, presets `wasm-games*`, executáveis `build/<preset>/tests/ecs_<suíte>` e `bench/ecs_bench_*`, `docs/benchmarks/`), módulos e `DEPENDS`, fronteiras (`oly.lint.boundaries`, `detail/`, `@oly/core/internal`), glossário (§5.2), `OLY_ASSERT`/`OLY_ENABLE_ASSERTS`, `oly_add_test`/`oly_add_contract_death`, acessores de teste, `checkInvariants(world)` livre, regras da ponte com `ContractError`/`loadRuntime`. Manter as seções de estilo e corretude.

- [ ] **Step 2: Atualizar skills e agentes** para os caminhos, alvos (`oly_add_test`, `ecs.contract_death.<caso>`, `ecs_bench_*`) e nomes novos (`QueryById` no lugar de `raw_query`/`RawQuery`, `OLY_ASSERT`).

- [ ] **Step 3: Varredura do critério 4**

Run:
```sh
git grep -nE 'ECS_ASSERT|ECS_ENABLE_ASSERTS|ENGINE_API|\bengine[A-Z][A-Za-z]*\(|Engine(World|Query|Entity|Module|Runtime|ContractError)|loadEngine|\b(add|remove|has|get)Raw\b|RawQuery|rawQuery|\.create\(|\bcreate\(\)|(^|[^:])\becs::|include "ecs/|include "engine/|bench/results|build/ts|engine-ts|engine-test|wasm-engine' -- . ':!docs/superpowers/specs' ':!docs/superpowers/plans' ':!docs/benchmarks/**/*.json' ':!docs/benchmarks/**/*.txt'
```
Expected: nenhuma ocorrência, ou só ocorrências justificadas (ex.: `World.create` de outra API que não seja entidade, `createWorld`) anotadas no commit. Corrigir o resto.

- [ ] **Step 4: Matriz completa do critério 2**

Run: `$DC make test`, `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release'`, `$WC make test PRESET=native-clang`, `$WC make test PRESET=native-clang-32`, `$WC make test PRESET=native-clang-32-scalar`, `$WC make test PRESET=native-clang-avx2`, `$WC make test PRESET=wasm-scalar`, `$WC make test PRESET=wasm-simd`
Expected: todos verdes; `debug-asan` com 12 `ecs.contract_death.*` e 1 `bridge.contract_death.*`.

- [ ] **Step 5: Revisão de conformidade** — despachar `spec-conformance-reviewer` sobre `git diff develop...HEAD` (foco: `checkInvariants` ainda cobre as caches de `QueryById`, guarda de iteração no `iterate()`). Corrigir o que for confirmado.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "docs: CLAUDE.md, skills e agentes para a árvore e os nomes da Oly"
```
