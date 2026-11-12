# Engine — Fase 1 (Ponte) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build WASM da engine com API C, pacote TS com esquema único + gerador C++, e acesso TS sem cópia (`spawn`/`destroy`/`get`/`set`/`eachBlock` com views), com o benchmark da fronteira registrado.

**Architecture:** O ECS ganha uma query type-erased por `ComponentId` (`RawQuery`) e `CommandBuffer::addRaw`. Acima do `world`, um núcleo C++ fino (`engine/`) exporta uma API C `extern "C"` sobre um `EngineWorld`. O esquema é um arquivo TypeScript; o mesmo código TS que calcula o layout serve ao runtime (descritores) e ao gerador (structs C++ com `static_assert`), e um hash do esquema compilado no WASM detecta WASM desatualizado. O TS itera por bloco: uma chamada `engineQueryBegin` devolve os descritores de todas as tabelas, e os campos são lidos com passo sobre views do heap do WASM.

**Tech Stack:** C++20, GCC 14 (nativo), Emscripten 3.1.74 + node 20.18 (imagem `wasm`), CMake ≥ 3.25 com presets v6, doctest 2.4.11, Google Benchmark 1.8.3, TypeScript 5.6.3 (`tsc`, `strict`), `@types/node` 20.17.6, test runner `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (fase 1 da §5). Seções `§N` abaixo são desse spec. Requisitos `R-*`, invariantes `I-*` e metas `NF-*` são de `2026-10-16-ecs-archetype-spec.md` e continuam valendo.

## Global Constraints

- Branch `feat/engine-ponte` (não `master`). Commits, comentários, mensagens de assert e de erro em pt-BR; commits terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain só em Docker: nativo no serviço `dev` (`gcc:14`), WASM e tudo de node/TS no serviço `wasm` (`emscripten/emsdk:3.1.74`, node 20.18). O host não precisa de node (§3).
- `-Wall -Wextra -Wpedantic -Werror` em todos os alvos C++ (inclusive código gerado e testes da engine); TypeScript `strict` (§3).
- Camadas: a engine fica acima do `world` e só usa a API pública do ECS; nada em `include/ecs/` ou `src/` inclui `engine/` (§3).
- Engine reaproveitável: nada em `engine/include`, `engine/src`, `engine/ts`, `engine/generator` conhece conceitos de jogo; nomes de jogo só em `engine/fixture/` (§2).
- Fronteira: nunca cruzar WASM↔JS por entidade em caminho quente; `eachBlock` cruza uma vez por chamada para obter todos os blocos (§2, §4.2).
- API C `extern "C"` exportada com `EMSCRIPTEN_KEEPALIVE`; sem embind (§4.2).
- Layout das colunas inalterado: coluna de structs (AoS dentro do componente); campo = typed array com passo (§4.2).
- Validade: views só dentro do callback de `eachBlock`; só `Entity` é handle estável (§3, §4.2).
- Regressão zero no ECS: presets, testes, `checkInvariants`, e as razões NF-P1 (≤ 1,10), NF-P2 (≤ 1,25), NF-P5 (`/1000` ≤ 0,5) continuam como em `bench/results/README.md` (§2).
- Estilo `CLAUDE.md`: chaves em todo `if`/`for`, nomes completos, `PascalCase`/`camelCase`, membros privados com `_`, dados no topo da classe, um `namespace` por arquivo. No TS: mesmos nomes completos, `camelCase`, imports ESM com extensão `.js`.

### Como rodar comandos

```sh
DC="docker compose run --rm dev"     # GCC 14 nativo
WC="docker compose run --rm wasm"    # Emscripten + node + npm
$WC make engine-ts                   # npm ci (se preciso) + tsc → build/ts/   (Task 4)
$WC make engine-test                 # engine-ts + ctest no preset wasm-engine (Task 5)
```

No Git Bash do host, prefixar com `MSYS_NO_PATHCONV=1` quando houver caminhos absolutos no comando.

### Decisões que o spec deixa em aberto (fixadas aqui)

- **Diretórios:** tudo da engine em `engine/`: `include/engine/` + `src/` (núcleo C++), `ts/` (pacote TS), `generator/` (gerador), `fixture/` (jogo mínimo de teste: esquema + exports C++), `test/unit/` e `test/wasm/` (testes TS), `tests/` (doctest C++), `bench/`. `package.json`/`tsconfig.json` em `engine/`; `tsc` compila tudo para `build/ts/` (gitignored).
- **Esquema em TS, sem TS gerado:** `component()`/`tag()` calculam o layout na hora com `computeLayout`, e o gerador usa a mesma função para emitir o C++. Assim o "descritor TS" (§4.1) é o próprio objeto do esquema; não existe arquivo TS gerado que possa divergir.
- **Hash do esquema:** FNV-1a 32 bits da forma canônica (`Nome{campo:tipo,...};Tag;`), emitido no C++ gerado (`engineSchemaHash`) e conferido por `loadEngine`. Junto com tamanho/alinhamento por componente, pega WASM compilado com outro esquema.
- **Tipos escalares:** `f32 f64 i8 u8 i16 u16 i32 u32 entity` (sem 64 bits inteiros: exigiriam `BigInt`). `entity` é `ecs::Entity` (8 B, alinhamento 4). Campos na ordem de declaração, alinhamento natural.
- **Eventos (`event(...)`) ficam para a fase 3**, reaproveitando o mesmo layout; a fase 1 só tem `component` e `tag`.
- **Armazenamento TS por `Entity` para dados fora do esquema (§4.1) fica para a fase 2:** a limpeza dele acompanha a destruição, que lá passa a ser adiada pelo `CommandBuffer` da fase; fazê-lo agora seria refeito.
- **`Entity` no TS:** `number` = `generation * 2^32 + index`; `generation ≤ MaxEntityGeneration = 0x1FFFFF` (2²¹−1, mantém o número em 53 bits). Acima disso `packEntity` lança `EngineContractError` (≈ 2 milhões de reusos do mesmo slot).
- **Heap e crescimento de memória:** os campos leem `heap.f32[...]` etc. por um objeto `Heap` compartilhado; toda chamada da `Bridge` que pode alocar no WASM chama `sync()`, que recria as views se `module.HEAPU8.buffer` mudou. Ninguém fora da `Bridge` chama exports.
- **Erros de uso no TS nunca chegam ao `ECS_ASSERT`:** `World`/`Query` validam antes de chamar a API C (no Release os asserts somem e seria UB). Erro = `EngineContractError` com operação, `Entity` (`index:generation`) e nome do tipo.
- **Mudança estrutural dentro de `eachBlock` no TS:** `EngineContractError` na fase 1 (o `CommandBuffer` por fase vem na fase 2).
- **`RawQuery` não substitui `Query<Ts...>`:** as APIs template continuam com o caminho próprio (inline, sem indireção), porque delegar mexeria em NF-P1/P2. `CommandBuffer::add<T>` também continua construindo no lugar. "Quando fizer sentido" (§3) = nenhum caso nesta fase; registrado no `CLAUDE.md`.
- **Ferramentas TS:** só `typescript@5.6.3` e `@types/node@20.17.6` (devDependencies exatas, `package-lock.json` versionado). Sem bundler na fase 1 (só node). Test runner `node --test <diretório>`.
- **Presets novos:** `wasm-engine` (Debug, asserts ligados, `ENGINE_BUILD_FIXTURE=ON`) e `wasm-engine-release` (Release, `ECS_ENABLE_ASSERTS=0`, `ENGINE_BUILD_FIXTURE=ON`). Os presets existentes não ligam o fixture e não precisam de node.
- **Link do módulo do jogo:** `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web -sEXPORTED_FUNCTIONS=_malloc,_free -sEXPORTED_RUNTIME_METHODS=HEAPU8 -sEXIT_RUNTIME=0`, sufixo `.mjs`. `engine_core` é biblioteca `OBJECT` (membro não referenciado de `.a` seria descartado e levaria junto os `EMSCRIPTEN_KEEPALIVE`).

## Review Focus

1. **Uso errado no TS** (entidade morta, componente ausente, `write` de componente só lido, tag em `read`, `spawn` dentro de `eachBlock`): `EngineContractError` legível no TS, e o WASM continua usável — nunca `abort` do módulo nem UB no Release (Tasks 6 e 7).
2. **Exceção lançada dentro do callback de `eachBlock`:** propaga para quem chamou, a iteração é encerrada (`engineQueryEnd`) e o `World` volta a aceitar mudanças estruturais com `checkInvariants` limpo (Task 7).
3. **Crescimento da memória do WASM com views vivas** (alocação dentro do callback): leituras e escritas seguintes caem na memória certa e o C++ vê os valores (Task 7).
4. **WASM desatualizado em relação ao `schema.ts`** (campo mudou, WASM não recompilado): `loadEngine` rejeita com `SchemaMismatchError` citando os dois hashes, em vez de ler lixo (Task 6).
5. **`eachBlock` aninhado sobre a mesma query** (ex.: pares inimigo × inimigo): os dois laços veem os blocos certos; os descritores do externo não são sobrescritos pelo interno (Task 7).

---

### Task 1: `RawQuery` no ECS (query por `ComponentId`)

**Files:**
- Create: `include/ecs/raw_query.hpp`
- Modify: `include/ecs/query.hpp` (extrair `detail::signatureMatches`), `include/ecs/world.hpp`, `src/world.cpp`
- Test: `tests/raw_query.cpp` (novo, registrar em `tests/CMakeLists.txt`), `tests/contract_death.cpp` + lista `foreach`

**Interfaces:**
- Produces:
  - `bool ecs::detail::signatureMatches(const Signature& signature, std::span<const ComponentId> with, std::span<const ComponentId> without)` — usada por `Query<Ts...>::matches` e `RawQuery::matches` (mesmo comportamento atual).
  - `struct ecs::RawQuerySpec { std::vector<ComponentId> read, write, with, without; };`
  - `struct ecs::RawMatchedTable { Archetype* archetype; std::vector<uint16_t> columns; };` — `columns[i]` é a coluna de `components()[i]`.
  - `class ecs::RawQuery : public QueryCacheBase` com `std::span<const ComponentId> components() const noexcept` (= `read` seguido de `write`, na ordem dada), `size_t matchedCount() const noexcept`, `std::span<const RawMatchedTable> matchedTables() const noexcept`, `template <class F> void eachBlock(F&& callback)` com `callback(Table& table, std::span<const uint16_t> columns)` uma vez por tabela não vazia, `void beginIteration() noexcept`, `void endIteration() noexcept`, `uint32_t iterationDepth() const noexcept`.
  - `RawQuery& ecs::World::rawQuery(const RawQuerySpec& spec)` — cacheada por spec.

- [ ] **Step 1: Criar o branch** `git switch -c feat/engine-ponte`.

- [ ] **Step 2: Testes que falham** em `tests/raw_query.cpp` (doctest, componentes `A`, `B`, `Tag` de `tests/support/components.hpp`; `checkInvariants` depois de cada operação estrutural):

```cpp
TEST_CASE("rawQuery entrega colunas na ordem read seguida de write") {
  // 10 entidades A{i}, B{i, 2*i}; spec {read={id<A>}, write={id<B>}}
  // eachBlock: A* a = table.column(columns[0]).data; B* b = ...columns[1]; soma a[r].value == 45; b[r].first += 100
  // depois: world.get<B>(e).first == i + 100 para todas
}
TEST_CASE("rawQuery é cacheada pela spec") { CHECK(&world.rawQuery(spec) == &world.rawQuery(spec)); /* read {A,B} vs {B,A}: instâncias diferentes */ }
TEST_CASE("rawQuery e Query<Ts...> com a mesma assinatura são instâncias distintas") { /* query<const A, B>() e rawQuery{read={A}, write={B}}: endereços diferentes, ambas iteram */ }
TEST_CASE("rawQuery acompanha arquétipos criados depois") { /* query antes; create A,B,Tag; matchedCount() == 1; eachBlock vê a entidade */ }
TEST_CASE("rawQuery filtra por with e without") { /* 3 com Tag, 2 sem: with={Tag} → 3 linhas; without={Tag} → 2 */ }
TEST_CASE("rawQuery pula tabelas vazias") { /* create e destroy: zero callbacks */ }
TEST_CASE("beginIteration e endIteration marcam o World como iterando") {
  // begin: world.isIterating() == true, iterationDepth() == 1; begin de novo: 2; end, end: false; checkInvariants ok
}
```

E em `tests/contract_death.cpp` (adicionar os nomes à lista `foreach`):

| Caso | Ação | Substrings exigidas |
|---|---|---|
| `raw_query_tag_in_read` | `rawQuery({read={id<Tag>}})` | `rawQuery`, `tag` |
| `raw_query_repeated_component` | `read={id<A>}, write={id<A>}` | `rawQuery`, `repetido` |
| `raw_query_unknown_component` | `read={registeredTypeCount() + 5}` | `rawQuery`, `desconhecido` |
| `raw_query_end_without_begin` | `endIteration()` direto | `endIteration` |
| `add_during_raw_iteration` | `beginIteration()` e `world.add(entity, B{})` | `add` |

- [ ] **Step 3: Ver falhar** — `$DC make test T='raw_query|contract_death'` → erro de compilação (`raw_query.hpp` não existe).

- [ ] **Step 4: Implementar.** `RawQuery` em `include/ecs/raw_query.hpp` (mesma camada de `query.hpp`). `World::rawQuery` valida com `ECS_ASSERT` (mensagens da tabela acima, com o nome do tipo quando houver), monta `with` = read ∪ write ∪ with ordenado e sem repetição, e guarda num mapa próprio `rawQueryCache_` (chave `QueryKey`, read como const) — não em `queryCache_`, cujo `static_cast<Query<Ts...>&>` confundiria os tipos. Registra em `queries_` como as demais (recebe `onArchetypeCreated` e entra em `checkQueries`, I-8). `beginIteration` incrementa `*iterating_` e `iterationDepth_`; `endIteration` exige `iterationDepth_ > 0`. `eachBlock` usa `IterationGuard`.

- [ ] **Step 5: Ver passar** — `$DC make test` (todas as suítes, `debug-asan`) e `$WC make test PRESET=wasm-debug`. Expected: tudo verde, incluindo os 5 casos novos de `contract_death`.

- [ ] **Step 6: Commit** `feat: RawQuery, query do ECS por ComponentId`.

---

### Task 2: `CommandBuffer::addRaw` e não-regressão dos benchmarks do ECS

**Files:**
- Modify: `include/ecs/command_buffer.hpp`, `src/command_buffer.cpp`, `bench/iterate.cpp`
- Test: `tests/command_buffer.cpp`
- Create: `bench/results/e1_iterate.json`, `bench/results/e1_fragmented.json`, `bench/results/e1_flush.json`; Modify: `bench/results/README.md`

**Interfaces:**
- Consumes: `World::rawQuery`, `RawQuery::eachBlock` (Task 1).
- Produces: `void ecs::CommandBuffer::addRaw(Entity entity, ComponentId componentId, void* value)` — move de `value` (memcpy se trivial, `moveConstruct` se não; quem chamou continua dono do moved-from); `value` ignorado para tags.

- [ ] **Step 1: Testes que falham** em `tests/command_buffer.cpp`:

```cpp
TEST_CASE("addRaw grava componente trivial por ID") { /* spawn; A value{7}; addRaw(e, id<A>, &value); flush → get<A>(e).value == 7 */ }
TEST_CASE("addRaw de tag ignora o valor") { /* addRaw(e, id<Tag>, nullptr); flush → has<Tag>(e) */ }
TEST_CASE("addRaw move tipo não trivial") {
  // { LifeCounter source{9}; buffer.addRaw(e, id<LifeCounter>, &source); } flush → get<LifeCounter>(e).value == 9
  // destroy(e) e buffer destruído → LifeCounter::alive == 0
}
TEST_CASE("addRaw coalesce igual a add<T> no flush") {
  // dois worlds com a mesma sequência (spawn, addRaw A, removeRaw A, addRaw B / add<A>, remove<A>, add<B>)
  // estado final igual: has<A> false, get<B> igual, checkInvariants ok nos dois
}
```

- [ ] **Step 2: Ver falhar** — `$DC make test T=command_buffer` → `addRaw` não é membro.

- [ ] **Step 3: Implementar** `addRaw` com `pushRecord` + `TypeInfo` (`size`, `alignment`, `trivial`). `add<T>` não muda (constrói no lugar, sem cópia extra).

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug`.

- [ ] **Step 5: Benchmark `BM_IterateSingle_RawQuery`** em `bench/iterate.cpp`, igual a `BM_IterateSingle_ECS` mas com `rawQuery({.read = {id<Velocity>}, .write = {id<Position>}})` e `eachBlock` convertendo `columns` em ponteiros (`columns[0]` é `Velocity`, `columns[1]` é `Position`: read antes de write); mesmos `Arg`s. Mede o custo da query type-erased no C++.

- [ ] **Step 6: Reexecutar e conferir as metas**

```sh
$DC make build PRESET=release
$DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/e1_iterate.json
$DC ./build/release/bench/bench_fragmented --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/e1_fragmented.json
$DC ./build/release/bench/bench_flush --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/e1_flush.json
$DC python3 -I tools/bench_ratio.py --json bench/results/e1_iterate.json --name BM_IterateSingle_ECS/1000000 --baseline BM_IterateSingle_SoA/1000000 --max 1.10
$DC python3 -I tools/bench_ratio.py --json bench/results/e1_fragmented.json --name BM_IterateFragmented/64 --baseline BM_IterateSingle_SoA/1000000 --max 1.25
$DC python3 -I tools/bench_ratio.py --json bench/results/e1_flush.json --name BM_FlushCoalesced/1000 --baseline BM_DirectAdds/1000 --max 0.5
```

Expected: exit 0 nos três (`bench_fragmented` tem a própria `BM_IterateSingle_SoA/1000000`, como no `m3_fragmented.json`). `BM_FlushCoalesced/100000` fica registrado e comparado com o ~0,85 já documentado. Se alguma razão piorar, repetir a execução antes de concluir (ambiente ruidoso); regressão confirmada bloqueia a task.

- [ ] **Step 7: Registrar** em `bench/results/README.md`: linhas `e1_*.json` na tabela ("reexecução após `RawQuery`/`addRaw`, engine fase 1"), as razões obtidas e a razão `BM_IterateSingle_RawQuery/N` / `BM_IterateSingle_ECS/N` por N.

- [ ] **Step 8: Commit** `feat: CommandBuffer::addRaw e reexecução dos benchmarks do ECS`.

---

### Task 3: Núcleo C++ da engine — API C

**Files:**
- Create: `engine/CMakeLists.txt`, `engine/include/engine/c_api.hpp`, `engine/include/engine/world_handle.hpp`, `engine/include/engine/schema_registry.hpp`, `engine/src/c_api.cpp`
- Create: `engine/tests/c_api.cpp`, `engine/tests/test_schema_registry.cpp`
- Modify: `CMakeLists.txt` (raiz: `add_subdirectory(engine)` depois do bloco de testes; opção `ENGINE_BUILD_FIXTURE` OFF)

**Interfaces:**
- Consumes: `World::rawQuery`, `RawQuery::{matchedTables, beginIteration, endIteration}` (Task 1); `World::{create, destroy, isAlive, addRaw, removeRaw, hasRaw, getRaw, checkInvariants}`.
- Produces (`engine/include/engine/c_api.hpp`, tudo dentro de `extern "C"`, cada função com `ENGINE_API` = `EMSCRIPTEN_KEEPALIVE` sob `__EMSCRIPTEN__`, vazio fora):

```cpp
struct EngineWorld;                                   // definido em world_handle.hpp
struct EngineQuery;                                   // opaco: é um ecs::RawQuery*
struct EngineEntity { uint32_t index; uint32_t generation; };   // static_assert: mesmo layout de ecs::Entity
EngineWorld* engineWorldCreate();
void engineWorldDestroy(EngineWorld* world);
uint32_t engineSchemaHash();
uint32_t engineSchemaComponentCount();
uint32_t engineSchemaComponentId(uint32_t schemaIndex);
uint32_t engineComponentSize(uint32_t componentId);      // 0 para tag
uint32_t engineComponentAlignment(uint32_t componentId);
void engineCreate(EngineWorld* world, EngineEntity* out);
void engineDestroy(EngineWorld* world, uint32_t index, uint32_t generation);
int32_t engineIsAlive(EngineWorld* world, uint32_t index, uint32_t generation);
void engineAdd(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
void engineRemove(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
int32_t engineHas(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);
void* engineGet(EngineWorld* world, uint32_t index, uint32_t generation, uint32_t componentId);   // nullptr se morta, ausente ou tag
// componentIds = read | write | with | without, concatenados
EngineQuery* engineQuery(EngineWorld* world, const uint32_t* componentIds, uint32_t readCount, uint32_t writeCount, uint32_t withCount, uint32_t withoutCount);
// Inicia a iteração e devolve o nº de blocos (tabelas não vazias). *outDescriptors aponta para
// blocos × (2 + readCount + writeCount) uintptr_t: [count, endereço das entidades, endereço de cada coluna...].
// Válido até a próxima chamada que itere ou altere o World.
uint32_t engineQueryBegin(EngineWorld* world, EngineQuery* query, const uintptr_t** outDescriptors);
void engineQueryEnd(EngineWorld* world, EngineQuery* query);
int32_t engineCheckInvariants(EngineWorld* world, char* message, uint32_t capacity);   // 1 ok; 0 e motivo truncado com NUL
```

- `engine/include/engine/world_handle.hpp`: `struct EngineWorld { ecs::World world; std::vector<uintptr_t> blockDescriptors; };` (usado pelos exports C++ dos jogos).
- `engine/include/engine/schema_registry.hpp`: `namespace engine { uint32_t schemaHash() noexcept; std::span<const ecs::ComponentId> schemaComponentIds(); }` — **definido pelo código gerado do jogo** (Task 5) ou, nos testes, por `test_schema_registry.cpp`.
- CMake: `engine_core` (`add_library(... OBJECT engine/src/*.cpp)`, include `engine/include` PUBLIC, link `ecs` PUBLIC, `ecs_warnings`); `engine_add_test(<nome> <fontes...>)` igual a `ecs_add_test` mas linkando `engine_core` e com include de `tests/support`.

- [ ] **Step 1: Testes que falham** em `engine/tests/c_api.cpp`, com `test_schema_registry.cpp` registrando `A`, `B`, `Tag` (nessa ordem) e `schemaHash() == 0x1234u`:

```cpp
TEST_CASE("schema exposto pela API C") {
  CHECK(engineSchemaHash() == 0x1234u); CHECK(engineSchemaComponentCount() == 3);
  CHECK(engineSchemaComponentId(1) == ecs::componentId<B>());
  CHECK(engineComponentSize(ecs::componentId<A>()) == 8); CHECK(engineComponentAlignment(ecs::componentId<A>()) == 8);
  CHECK(engineComponentSize(ecs::componentId<Tag>()) == 0);
}
TEST_CASE("create, add, get, has, remove e destroy por ID") {
  // engineCreate → out; A value{5}; engineAdd(..., id<A>, &value) → static_cast<A*>(engineGet(...))->value == 5
  // engineHas == 1; engineRemove → engineHas == 0 e engineGet == nullptr; engineDestroy → engineIsAlive == 0
  // engineCheckInvariants(world, buffer, 256) == 1 após cada passo
}
TEST_CASE("engineQueryBegin entrega descritores por bloco") {
  // 3 entidades {A{1..3}, B} e 2 entidades {A{10,20}, B, Tag}; ids = {A | B}, readCount 1, writeCount 1
  // begin → 2 blocos; para cada: descriptor[0] = count, [1] = Entity*, [2] = A*, [3] = B*
  // soma dos A == 36; entidades do descritor isAlive; world.isIterating() true até engineQueryEnd, depois false
}
TEST_CASE("engineQuery é cacheada") { /* mesmos ids/contagens → mesmo ponteiro; with {Tag} → outro */ }
TEST_CASE("engineQueryBegin aninhado na mesma query") {
  // begin externo; copia os descritores; begin interno → mesmo nº de blocos; end interno; end externo; isIterating false
}
```

- [ ] **Step 2: Ver falhar** — `$DC make test T=engine` → `engine/` não existe no build.

- [ ] **Step 3: Implementar** `engine/src/c_api.cpp`. Os exports convertem `(index, generation)` em `ecs::Entity` e delegam ao `World` (a validação de contrato fica com o `ECS_ASSERT` do ECS; o TS valida antes, Task 6). `engineQueryBegin` chama `beginIteration` e preenche `blockDescriptors` a partir de `matchedTables()`, pulando `count == 0`.

- [ ] **Step 4: Ver passar** — `$DC make test` (inclui `engine_c_api` em `debug-asan`) e `$WC make test PRESET=wasm-debug` (o mesmo teste em WASM via node).

- [ ] **Step 5: Commit** `feat: núcleo C++ da engine com API C sobre o World`.

---

### Task 4: Ferramentas TS e esquema (DSL, layout, hash, `Entity`)

**Files:**
- Create: `engine/package.json`, `engine/package-lock.json`, `engine/tsconfig.json`
- Create: `engine/ts/schema.ts`, `engine/ts/layout.ts`, `engine/ts/schema_hash.ts`, `engine/ts/entity.ts`, `engine/ts/errors.ts`, `engine/ts/index.ts`
- Test: `engine/test/unit/layout.test.ts`, `engine/test/unit/schema.test.ts`, `engine/test/unit/entity.test.ts`
- Modify: `Makefile` (alvo `engine-ts`), `.gitignore` (`node_modules`)

**Interfaces:**
- Produces:
  - `layout.ts`: `type ScalarType = "f32" | "f64" | "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "entity"`; `interface FieldLayout { name: string; type: ScalarType; offset: number }`; `interface ComponentLayout { size: number; alignment: number; fields: readonly FieldLayout[] }`; `scalarSize(type)`, `scalarAlignment(type)` (`entity`: 8 e 4); `computeLayout(fields: Fields): ComponentLayout`.
  - `schema.ts`: `type Fields = Readonly<Record<string, ScalarType>>`; `type ValueOf<F extends Fields> = { [K in keyof F]: number }`; `interface ComponentType<Name extends string = string, F extends Fields = Fields> { readonly kind: "component"; readonly name: Name; readonly fields: F; readonly layout: ComponentLayout; readonly schemaIndex: number }`; `interface TagType<Name extends string = string> { readonly kind: "tag"; readonly name: Name; readonly schemaIndex: number }`; `type SchemaType = ComponentType | TagType`; `component(name, fields)`, `tag(name)` (com `const` type parameters, `schemaIndex` = −1 até entrar num esquema); `interface Schema { readonly types: readonly SchemaType[]; readonly hash: number }`; `defineSchema(types: readonly SchemaType[]): Schema`.
  - `schema_hash.ts`: `fnv1a32(text: string): number` (sem sinal), `canonicalSchema(types): string`, `schemaHash(types): number`.
  - `entity.ts`: `type Entity = number`; `MaxEntityGeneration = 0x1FFFFF`; `packEntity(index, generation): Entity`; `entityIndex(entity)`; `entityGeneration(entity)`; `NullEntity` (= `packEntity(0xFFFFFFFF, 0)`); `formatEntity(entity): string` (`"index:generation"`).
  - `errors.ts`: `class EngineContractError extends Error`, `class SchemaMismatchError extends Error`.
  - `package.json`: `"type": "module"`, scripts `build` (`tsc -p .`), `test:unit` (`node --test ../build/ts/test/unit/`), `test:wasm` (`node --test ../build/ts/test/wasm/`). `tsconfig.json`: `strict`, `target ES2022`, `module`/`moduleResolution` `NodeNext`, `rootDir "."`, `outDir "../build/ts"`, `include` `ts`, `generator`, `fixture`, `test`, `bench`.
  - `Makefile`: `engine-ts` = `cd engine && ([ -d node_modules ] || npm ci --no-audit --no-fund) && npm run build`.

- [ ] **Step 1: Ferramentas.** `$WC sh -c 'node --version && npm --version'` → v20.18.x e um npm. Se `npm` não estiver no `PATH`, usar `$(dirname $(command -v node))/npm` no alvo do Makefile. Criar `package.json`/`tsconfig.json`, `$WC sh -c 'cd engine && npm install --save-exact --save-dev typescript@5.6.3 @types/node@20.17.6'` (gera o lock).

- [ ] **Step 2: Testes que falham** (`node:test` + `node:assert/strict`):

```ts
// layout.test.ts
test("Position f32,f32", () => assert.deepEqual(computeLayout({ x: "f32", y: "f32" }), { size: 8, alignment: 4, fields: [{ name: "x", type: "f32", offset: 0 }, { name: "y", type: "f32", offset: 4 }] }));
test("padding antes de f32", () => offsets({ a: "u8", b: "f32" }) === [0, 4], size 8);
test("f64 alinha o struct a 8", () => offsets({ a: "f32", b: "f64" }) === [0, 8], size 16, alignment 8);
test("u8,u16,u8", () => offsets === [0, 2, 4], size 6, alignment 2);
test("entity tem 8 bytes e alinhamento 4", () => offsets({ owner: "entity", flag: "u8" }) === [0, 8], size 12, alignment 4);

// schema.test.ts
test("component sem campos é erro", () => assert.throws(() => component("Empty", {}), /tag/));
test("nomes inválidos", () => { /* component("1abc", ...), component("Ok", { class: "f32" }), component("Ok", { _x: "f32" }) lançam */ });
test("defineSchema atribui schemaIndex na ordem e rejeita nome repetido", ...);
test("tipo já usado em outro esquema é erro", ...);
test("fnv1a32", () => { assert.equal(fnv1a32(""), 0x811c9dc5); assert.equal(fnv1a32("a"), 0xe40c292c); });
test("forma canônica", () => assert.equal(canonicalSchema([component("Position", { x: "f32", y: "f32" }), tag("Player")]), "Position{x:f32,y:f32};Player;"));
test("hash muda com ordem e com tipo de campo", ...);   // schema.hash === schemaHash(schema.types)

// entity.test.ts
test("empacota e desempacota", () => { const e = packEntity(5, 3); assert.equal(e, 3 * 2 ** 32 + 5); assert.equal(entityIndex(e), 5); assert.equal(entityGeneration(e), 3); });
test("geração máxima cabe em inteiro seguro", () => assert.ok(Number.isSafeInteger(packEntity(0xFFFFFFFF, MaxEntityGeneration))));
test("geração acima do máximo é erro", () => assert.throws(() => packEntity(0, MaxEntityGeneration + 1), EngineContractError));
test("NullEntity", () => { assert.equal(entityIndex(NullEntity), 0xFFFFFFFF); assert.equal(entityGeneration(NullEntity), 0); });
```

Nomes válidos: componente/tag `^[A-Z][A-Za-z0-9]*$`; campo `^[a-z][A-Za-z0-9]*$` e fora de uma lista de palavras reservadas do C++ (`class`, `struct`, `int`, `float`, `double`, `char`, `long`, `short`, `unsigned`, `signed`, `bool`, `void`, `auto`, `return`, `operator`, `template`, `namespace`, `new`, `delete`, `this`, `default`, `case`, `switch`, `register`, `union`, `enum`, `const`, `static`, `virtual`, `public`, `private`, `protected`, `friend`, `using`, `typename`, `do`, `if`, `else`, `for`, `while`, `goto`, `break`, `continue`, `try`, `catch`, `throw`, `true`, `false`, `nullptr`, `sizeof`, `alignof`, `export`, `extern`, `inline`, `mutable`, `volatile`, `explicit`, `concept`, `requires`, `and`, `or`, `not`, `xor`).

- [ ] **Step 3: Ver falhar** — `$WC sh -c 'make engine-ts && cd engine && npm run test:unit'` → erros de compilação/import.

- [ ] **Step 4: Implementar** os módulos. `computeLayout`: offset = alinhar(offset atual, alinhamento do campo) na ordem de declaração; tamanho final alinhado ao maior alinhamento. `defineSchema` congela os objetos e atribui `schemaIndex` (erro se o tipo já tem `schemaIndex ≥ 0`). `index.ts` reexporta a API pública.

- [ ] **Step 5: Ver passar** — `$WC sh -c 'make engine-ts && cd engine && npm run test:unit'` → todos `ok`.

- [ ] **Step 6: Commit** `feat: pacote TS da engine com esquema, layout, hash e Entity`.

---

### Task 5: Gerador C++, `engine_add_game`, presets e o jogo fixture

**Files:**
- Create: `engine/generator/emit_cpp.ts`, `engine/generator/main.ts`
- Create: `engine/fixture/schema.ts`, `engine/fixture/systems.cpp`
- Test: `engine/test/unit/generator.test.ts`
- Modify: `engine/CMakeLists.txt` (`engine_add_game`, fixture, testes TS no ctest), `CMakePresets.json`, `Makefile` (alvo `engine-test`)

**Interfaces:**
- Consumes: `computeLayout`, `Schema`, `schemaHash` (Task 4); `engine_core`, `schema_registry.hpp`, `world_handle.hpp` (Task 3).
- Produces:
  - `emitHeader(schema: Schema): string` — `#pragma once`, `<cstddef>`, `<cstdint>`, `"ecs/entity.hpp"`; um `struct` por tipo no namespace global, campos na ordem (`f32→float`, `f64→double`, `i8→int8_t`, `u8→uint8_t`, `i16→int16_t`, `u16→uint16_t`, `i32→int32_t`, `u32→uint32_t`, `entity→ecs::Entity`); tag = `struct Nome {};`; para cada componente `static_assert(sizeof(T) == S && alignof(T) == A, "esquema: layout de T diverge do TS")` e `static_assert(offsetof(T, campo) == O, ...)`.
  - `emitRegistry(schema: Schema): string` — inclui o header gerado e `engine/schema_registry.hpp`; define `engine::schemaHash()` (literal `0x...u`) e `engine::schemaComponentIds()` (array estático com `ecs::componentId<T>()` na ordem do esquema).
  - CLI: `node build/ts/generator/main.js --schema <schema.js> --out <dir>` — importa o módulo, usa o `export default` (um `Schema`), grava `<dir>/schema.generated.hpp` e `<dir>/schema_registry.generated.cpp`; exit 1 com mensagem se o default não for um `Schema`.
  - CMake `engine_add_game(<alvo> SCHEMA_JS <schema.js compilado> SOURCES <cpp...>)`: `add_custom_command` com o gerador (OUTPUT os dois arquivos, DEPENDS o schema JS e `main.js`), executável com `engine_core`, include do diretório gerado, link flags das Decisões, `SUFFIX ".mjs"`. Na configuração, se `build/ts/generator/main.js` não existir: `FATAL_ERROR "rode 'make engine-ts' antes de configurar com ENGINE_BUILD_FIXTURE=ON"`.
  - Fixture (`engine/fixture/schema.ts`, `export default defineSchema([...])` e exports nomeados): `Position {x: f32, y: f32}`, `Velocity {x: f32, y: f32}`, `Mixed {a: u8, b: f64, c: i16, owner: entity}`, tags `Player`, `Enemy`.
  - Fixture (`engine/fixture/systems.cpp`, `extern "C"` + `EMSCRIPTEN_KEEPALIVE`): `void fixtureMove(EngineWorld* world, float deltaTime)` (`query<Position, const Velocity>().each`, `position += velocity * deltaTime`); `double fixtureSumPositionX(EngineWorld* world)`; `double fixtureMixedChecksum(EngineWorld* world, uint32_t index, uint32_t generation)` = `a + b + c + owner.index + owner.generation` do `Mixed` da entidade.
  - Alvo `fixture_game` → `build/<preset>/engine/fixture_game.mjs` + `.wasm`. ctest (só com o fixture): `engine_ts_unit` (`node --test build/ts/test/unit/`). O `engine_ts_wasm` entra na Task 6, junto com os primeiros testes que usam o módulo.
  - Presets `wasm-engine` e `wasm-engine-release` (configure/build/test de mesmo nome; mesmo toolchain do `wasm-debug`; ver Decisões). `Makefile`: `engine-test: engine-ts` → `$(MAKE) test PRESET=wasm-engine`.

- [ ] **Step 1: Teste que falha** `engine/test/unit/generator.test.ts`:

```ts
const P = component("P", { a: "u8", b: "f64", owner: "entity" }); const T = tag("T"); const schema = defineSchema([P, T]);
test("struct na ordem de declaração", () => assert.match(emitHeader(schema), /struct P \{\s*uint8_t a;\s*double b;\s*ecs::Entity owner;\s*\};/));
test("static_asserts de tamanho e offsets", () => { const h = emitHeader(schema); assert.match(h, /sizeof\(P\) == 24 && alignof\(P\) == 8/); assert.match(h, /offsetof\(P, b\) == 8/); assert.match(h, /offsetof\(P, owner\) == 16/); });
test("tag vira struct vazio", () => assert.match(emitHeader(schema), /struct T \{\};/));
test("registro com hash e ids na ordem", () => { const r = emitRegistry(schema); assert.ok(r.includes(`0x${schema.hash.toString(16)}u`)); assert.match(r, /ecs::componentId<P>\(\),\s*ecs::componentId<T>\(\)/); });
```

- [ ] **Step 2: Ver falhar** — `$WC sh -c 'make engine-ts && cd engine && npm run test:unit'` → `emit_cpp.js` não existe.

- [ ] **Step 3: Implementar** `emit_cpp.ts` e `main.ts`; ver passar o Step 1.

- [ ] **Step 4: CMake, presets, fixture.** Implementar conforme Interfaces. Se o link reclamar de `main` ausente, acrescentar `--no-entry` às flags do jogo.

- [ ] **Step 5: Verificar o build do fixture**

Run: `$WC sh -c 'make engine-ts && cmake --preset wasm-engine && cmake --build --preset wasm-engine -j && ls build/wasm-engine/engine/ && node -e "import(\"/app/build/wasm-engine/engine/fixture_game.mjs\").then(m => m.default()).then(mod => console.log(typeof mod._engineSchemaHash, mod._engineSchemaHash().toString(16), mod.HEAPU8.length))"'`
Expected: `fixture_game.mjs` e `fixture_game.wasm` listados; `function <hash> <bytes>` com o hash igual a `schemaHash` do fixture (conferir com `node -e` importando `build/ts/fixture/schema.js`). Prova também que `-Werror` aceita o código gerado. Testar à mão que um `static_assert` pega divergência: editar temporariamente um `offsetof` esperado no header gerado e ver o build falhar com a mensagem "esquema: layout de ... diverge do TS"; desfazer.

- [ ] **Step 6: Ver passar o ctest** — `$WC make engine-test` (ECS em WASM, `engine_c_api`, `engine_ts_unit`). E `$DC make test` continua verde (o fixture não entra nos presets nativos).

- [ ] **Step 7: Commit** `feat: gerador C++ do esquema, engine_add_game e jogo fixture`.

---

### Task 6: Ponte TS — `loadEngine` e `World` (spawn/destroy/get/set)

**Files:**
- Create: `engine/ts/module.ts`, `engine/ts/bridge.ts`, `engine/ts/world.ts`, `engine/ts/runtime.ts`
- Create: `engine/test/wasm/fixture.ts` (helper), `engine/test/wasm/load.test.ts`, `engine/test/wasm/world.test.ts`
- Modify: `engine/ts/index.ts`, `engine/CMakeLists.txt` (ctest `engine_ts_wasm`: `node --test build/ts/test/wasm/` com `ENVIRONMENT ENGINE_FIXTURE_MODULE=$<TARGET_FILE:fixture_game>`)

**Interfaces:**
- Consumes: API C (Task 3, exports com prefixo `_`); `Schema`, `ComponentType`, `ValueOf`, `packEntity`/`entityIndex`/`entityGeneration`, erros (Task 4); módulo do fixture (Task 5).
- Produces:
  - `module.ts`: `interface EngineModule` (`HEAPU8: Uint8Array`, `_malloc`, `_free` e cada export da Task 3 tipado com `number` para ponteiros/handles); `type EngineModuleFactory = () => Promise<EngineModule>`.
  - `bridge.ts`: `class Heap { buffer: ArrayBuffer; u8; i8; u16; i16; u32; i32; f32; f64 }` (views sobre o buffer inteiro) com `refresh(buffer)`; `class Bridge` com `readonly heap: Heap`, `readonly module: EngineModule`, `allocate(bytes): number`, `free(pointer): void`, `sync(): void`, e um método por export (`create(world): Entity`, `destroy`, `isAlive`, `add(world, entity, componentId, valuePointer)`, `remove`, `has`, `get(...): number` (endereço ou 0), `query(world, ids, counts...)`, `queryBegin(world, query): { blockCount: number; descriptors: number }`, `queryEnd`, `checkInvariants(world): string | null`). Os métodos que podem alocar chamam `sync()` antes de retornar. A `Bridge` mantém uma área de rascunho (`_malloc` uma vez) para saídas (`EngineEntity`, ponteiro de descritores), para o valor de um componente (maior `layout.size` do esquema) e para listas de IDs (cresce sob demanda).
  - `runtime.ts`: `loadEngine(factory: EngineModuleFactory, schema: Schema): Promise<EngineRuntime>` — confere `engineSchemaHash() === schema.hash`, `engineSchemaComponentCount() === schema.types.length` e, por tipo, tamanho (`0` para tag) e alinhamento; senão `SchemaMismatchError` com os dois hashes em hex e o primeiro tipo divergente. `class EngineRuntime { readonly schema; readonly bridge: Bridge; readonly module: EngineModule; componentId(type: SchemaType): number; createWorld(): World }`.
  - `world.ts`: `class World` com `readonly handle: number` e:
    - `spawn<const Ts extends readonly SchemaType[]>(types: Ts, values?: SpawnValues<Ts>): Entity`, onde `SpawnValues<Ts> = { [C in Extract<Ts[number], ComponentType> as C["name"]]?: Partial<ValueOf<C["fields"]>> }`; campos omitidos = 0.
    - `destroy(entity)`, `isAlive(entity): boolean`, `has(entity, type): boolean`.
    - `add(entity, type, value?)` — valor completo (omitidos = 0); em componente já presente, atribui (semântica do ECS). `remove(entity, type)`.
    - `get<F>(entity, type: ComponentType<string, F>): ValueOf<F>` (cópia); `set<F>(entity, type, value: Partial<ValueOf<F>>)` (só os campos dados, direto na coluna).
    - `query(spec)` (Task 7), `checkInvariants(): string | null`, `dispose()`.
    - Validações antes de chamar o C++: entidade viva (`destroy`/`add`/`remove`/`get`/`set`), componente presente (`get`/`set`), tipo pertence ao esquema do runtime, chave/campo desconhecido em `values`, `World` já liberado, mudança estrutural com iteração em curso (contador `iterationDepth` do `World`, usado pela Task 7). Mensagens: `"<operação> e=<index:generation> type=<Nome>: <motivo>"`.
  - `engine/test/wasm/fixture.ts`: `loadFixture(): Promise<{ runtime: EngineRuntime; fixture: FixtureExports }>` lendo `process.env.ENGINE_FIXTURE_MODULE` (erro claro se ausente), `import(pathToFileURL(...).href)`; `FixtureExports` = `fixtureMove(world, deltaTime)`, `fixtureSumPositionX(world)`, `fixtureMixedChecksum(world, index, generation)`.

- [ ] **Step 1: Testes que falham** (`checkInvariants()` deve ser `null` ao fim de cada teste):

```ts
// load.test.ts
test("carrega com o esquema do fixture", async () => { const { runtime } = await loadFixture(); assert.ok(runtime.createWorld()); });
test("esquema divergente é rejeitado", async () => {
  const OtherPosition = component("Position", { x: "f64", y: "f32" }); /* + mesmos demais tipos, recriados */
  await assert.rejects(loadEngine(factory, defineSchema([...])), (error) => error instanceof SchemaMismatchError && /0x[0-9a-f]+/.test(error.message));
});

// world.test.ts
test("spawn com valores e get", () => { const e = world.spawn([Player, Position, Velocity], { Position: { x: 1.5, y: -2 } }); assert.deepEqual(world.get(e, Position), { x: 1.5, y: -2 }); assert.deepEqual(world.get(e, Velocity), { x: 0, y: 0 }); assert.ok(world.has(e, Player)); });
test("set parcial", () => { world.set(e, Position, { y: 7 }); assert.deepEqual(world.get(e, Position), { x: 1.5, y: 7 }); });
test("add de tag, add que atribui, remove", ...);
test("destroy e reuso do slot", () => { world.destroy(e); assert.equal(world.isAlive(e), false); const reused = world.spawn([Position]); assert.equal(entityIndex(reused), entityIndex(e)); assert.equal(entityGeneration(reused), entityGeneration(e) + 1); });
test("Mixed bate com o C++", () => {
  const other = world.spawn([Player]); const e = world.spawn([Mixed], { Mixed: { a: 200, b: 1.5, c: -3, owner: other } });
  assert.deepEqual(world.get(e, Mixed), { a: 200, b: 1.5, c: -3, owner: other });
  assert.equal(fixture.fixtureMixedChecksum(world.handle, entityIndex(e), entityGeneration(e)), 200 + 1.5 - 3 + entityIndex(other) + entityGeneration(other));
});
test("valor escrito pelo TS é visto pelo C++", () => { /* 3 entidades Position x = 1, 2, 3 → fixtureSumPositionX == 6 */ });
test("erros de contrato", () => {
  // get/set/destroy/add/remove de entidade morta → EngineContractError com "e=" e o nome da operação
  // get de componente ausente → mensagem com "type=Velocity"; spawn com values { Velocity: ... } sem Velocity em types → erro
  // depois de tudo: world.spawn([Position]) funciona e checkInvariants() === null (o módulo não abortou)
});
test("dispose", () => { world.dispose(); assert.throws(() => world.spawn([Position]), EngineContractError); });
```

- [ ] **Step 2: Ver falhar** — `$WC make engine-test T=engine_ts_wasm` → imports inexistentes.

- [ ] **Step 3: Implementar.** `spawn` = `create` + um `add` por tipo (valor montado na área de rascunho a partir do layout; `entity` escrito como dois `u32`). Leitura/escrita de campos por `Heap` (`f32[address >> 2]` etc.). Componente → `ComponentId` por array indexado por `schemaIndex`.

- [ ] **Step 4: Ver passar** — `$WC make engine-test` → tudo verde, em `wasm-engine` (asserts ligados: nenhum `ECS_ASSERT` disparou). E `$WC sh -c 'make engine-ts && make test PRESET=wasm-engine-release T=engine_ts'` (Release: as validações TS sozinhas seguram os erros).

- [ ] **Step 5: Commit** `feat: ponte TS com loadEngine e World (spawn, destroy, get, set)`.

---

### Task 7: Queries no TS — `eachBlock` e views com passo

**Files:**
- Create: `engine/ts/fields.ts`, `engine/ts/query.ts`
- Modify: `engine/ts/world.ts` (`query`), `engine/ts/index.ts`
- Test: `engine/test/wasm/query.test.ts`

**Interfaces:**
- Consumes: `Bridge.query`/`queryBegin`/`queryEnd`/`allocate`/`free`, `Heap`, `World.iterationDepth` (Task 6); `ComponentLayout` (Task 4).
- Produces:
  - `fields.ts`: `interface RawField { readonly array: Float32Array | Float64Array | Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array; readonly base: number; readonly stride: number }` (elemento = `array[base + row * stride]`; para `entity`, `array` é `u32`, `base` aponta para o `index` e `base + 1` para a `generation`); `interface ReadField { get(row: number): number; raw(): RawField }`; `interface WriteField extends ReadField { set(row: number, value: number): void }`; `type ReadView<F extends Fields> = { readonly [K in keyof F]: ReadField }`; `type WriteView<F extends Fields> = { readonly [K in keyof F]: WriteField }`; `makeView(heap: Heap, layout: ComponentLayout, columnAddress: number): WriteView<Fields>`. Uma classe por tipo escalar (`F32Field`, `F64Field`, `I8Field`, `U8Field`, `I16Field`, `U16Field`, `I32Field`, `U32Field`, `EntityField`) para que cada `get`/`set` fique monomórfico; cada uma lê o array do `Heap` a cada acesso (segue o crescimento da memória). `raw()` devolve o array atual, válido até a próxima chamada que aloque no WASM.
  - `query.ts`: `interface QuerySpec { read?: readonly ComponentType[]; write?: readonly ComponentType[]; with?: readonly SchemaType[]; without?: readonly SchemaType[] }`; `class Query { eachBlock(callback: (block: Block) => void): void }`; `class Block { readonly count: number; entity(row: number): Entity; read<F extends Fields>(type: ComponentType<string, F>): ReadView<F>; write<F extends Fields>(type: ComponentType<string, F>): WriteView<F> }`.
  - `World.query(spec: QuerySpec): Query` — memoizada no TS por chave canônica (`schemaIndex` de read, write, with, without, preservando a ordem de read/write); valida: tag em `read`/`write`, tipo repetido entre `read` e `write`, tipo de outro esquema → `EngineContractError`.
  - Semântica de `eachBlock`: `queryBegin`, cópia de todos os descritores para arrays JS locais (o aninhado pode sobrescrever o buffer C++), `iterationDepth++`, um `Block` por descritor; `finally`: invalida o bloco corrente, `iterationDepth--`, `queryEnd`. `Block.read`/`write`/`entity` em bloco invalidado → `EngineContractError("bloco usado fora do callback de eachBlock")`; `write` de tipo só em `read` → erro; tipo fora da query → erro.

- [ ] **Step 1: Testes que falham** em `query.test.ts` (cada teste termina com `checkInvariants() === null`):

```ts
test("movimento no TS e no C++ dão o mesmo resultado", () => {
  // 1000 entidades: Position {x: i, y: 0}, Velocity {x: 1, y: 2}; as pares também com Enemy (2 arquétipos)
  // TS: query({ read: [Velocity], write: [Position] }).eachBlock: position.x.set(r, position.x.get(r) + velocity.x.get(r) * 0.5), idem y
  // C++: fixture.fixtureMove(world.handle, 0.5)
  // para todo i: world.get(e_i, Position) deepEqual { x: i + 1, y: 2 }; fixtureSumPositionX == Σ (i + 1)
});
test("blocos, contagens e filtros", () => { /* 2 blocos somando 1000; with [Enemy] → 500; without [Enemy] → 500 */ });
test("entity(row) identifica a linha", () => { /* world.get(block.entity(r), Position).x === position.x.get(r) para toda linha */ });
test("query criada antes dos arquétipos os enxerga", ...);
test("tabela esvaziada não gera bloco", ...);
test("mesma spec devolve a mesma Query", () => assert.equal(world.query(spec), world.query({ ...spec })));
test("campos f64, u8, i16 e entity pela view", () => { /* Mixed: read view get(r) de cada campo === world.get(e, Mixed) */ });
test("raw() lê o mesmo que get()", () => { /* const { array, base, stride } = position.x.raw(); array[base + r * stride] === position.x.get(r) */ });
test("erros de spec e de acesso", () => {
  // query({ read: [Player] }) e query({ read: [Position], write: [Position] }) → EngineContractError
  // dentro do callback: block.write(Velocity) com Velocity só em read → erro; block.read(Mixed) fora da query → erro
});
test("mudança estrutural dentro de eachBlock é erro e o World segue usável", () => {
  // spawn, destroy, add, remove dentro do callback → EngineContractError (assert.throws dentro do callback)
  // depois do eachBlock: world.spawn([Position]) funciona
});
test("exceção no callback encerra a iteração", () => {
  assert.throws(() => query.eachBlock(() => { throw new Error("falha do jogo"); }), /falha do jogo/);
  // world.spawn([Position]) funciona; checkInvariants() === null (I-11: não ficou iterando)
});
test("bloco usado fora do callback", () => { let saved!: Block; query.eachBlock((b) => { saved = b; }); assert.throws(() => saved.read(Position), EngineContractError); });
test("eachBlock aninhado na mesma query", () => {
  // 10 entidades num arquétipo: externo × interno visitam 100 pares; em cada par, position.x.get de ambos é o valor esperado
});
test("crescimento da memória durante eachBlock", () => {
  // dentro do callback: const before = runtime.module.HEAPU8.buffer; const pointer = runtime.bridge.allocate(64 * 1024 * 1024);
  // assert.notEqual(runtime.module.HEAPU8.buffer, before); position.x.set(r, 42) para toda linha; runtime.bridge.free(pointer)
  // depois: fixtureSumPositionX == 42 * N
});
```

- [ ] **Step 2: Ver falhar** — `$WC make engine-test T=engine_ts_wasm` → `query` não existe.

- [ ] **Step 3: Implementar** `fields.ts`, `query.ts`, `World.query`. Base de um campo = (endereço da coluna + `offset`) / tamanho do elemento; passo = `layout.size` / tamanho do elemento (sempre inteiros, §4.2: o teste do `Mixed` cobre `f64` e `u8`).

- [ ] **Step 4: Ver passar** — `$WC make engine-test` e `$WC sh -c 'make test PRESET=wasm-engine-release T=engine_ts'`. Este é o critério "pronto quando" da fase (§5): teste TS em node cria entidades, itera com views e bate com o C++.

- [ ] **Step 5: Commit** `feat: queries no TS com eachBlock e views com passo sobre o heap do WASM`.

---

### Task 8: Benchmark da fronteira, resultados e documentação

**Files:**
- Create: `engine/bench/boundary.ts`, `bench/results/engine/boundary_phase1.json`, `bench/results/engine/README.md`
- Modify: `bench/results/README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: `loadEngine`, `World`, `Query`, `RawField`, fixture (`fixtureMove`, `fixtureSumPositionX`) (Tasks 5–7).
- Produces: `node build/ts/bench/boundary.js --module <fixture_game.mjs> --out <json> [--sizes 1000,10000,100000,1000000] [--rounds 10]`.

- [ ] **Step 1: Implementar o benchmark** (§7). Mesma execução, quatro variantes de `position += velocity * deltaTime` (`deltaTime = 1/60`) sobre N entidades `Position {0, 0}`, `Velocity {1, 2}` num único arquétipo:
  1. `cpp` — `fixtureMove(world, deltaTime)`;
  2. `ts-engine` — `eachBlock` + `get`/`set` (o uso normal);
  3. `ts-engine-raw` — `eachBlock` + `raw()` (separa o custo do acessor do custo do passo);
  4. `ts-manual` — quatro `Float32Array` próprios (`x`, `y`, `vx`, `vy`), sem engine (linha de base).

  Protocolo: por N, aquecimento de 50 iterações por variante; depois `rounds` rodadas intercaladas, com a ordem das variantes rodando a cada rodada; cada medida = `ceil(4_000_000 / N)` iterações cronometradas com `performance.now()`, registrada em ns/entidade. Mediana, mínimo e máximo por variante; razões `ts-engine/ts-manual`, `ts-engine/cpp`, `ts-engine-raw/ts-manual` por N, calculadas sobre as medianas. Ao final, conferir que cada variante acumulou `x ≈ 1 · deltaTime · iterações totais` (tolerância relativa 1e-3) e abortar com exit 1 se não — número de variante errada não entra no resultado. JSON: `meta` (data, commit de `git rev-parse` se disponível via argumento `--commit`, `process.versions.node`, `process.versions.v8`, tamanhos, rodadas, iterações por N), `results` (N, variante, ns/entidade por rodada, mediana, mín, máx), `ratios`. No stdout, a tabela das razões.

- [ ] **Step 2: Fumaça**

Run: `$WC sh -c 'make engine-ts && cmake --preset wasm-engine-release && cmake --build --preset wasm-engine-release -j && node build/ts/bench/boundary.js --module build/wasm-engine-release/engine/fixture_game.mjs --out /tmp/boundary.json --sizes 1000 --rounds 2'`
Expected: tabela com uma linha (N = 1000), exit 0.

- [ ] **Step 3: Medir** com os quatro tamanhos e 10 rodadas, `--commit $(git rev-parse --short HEAD)` e `--out bench/results/engine/boundary_phase1.json`.

- [ ] **Step 4: `bench/results/engine/README.md`:** máquina e versões (Emscripten 3.1.74, node e V8 do JSON), protocolo, tabela por N com bytes do conjunto de trabalho (`N × (8 + 8 + 8)` B para a engine) e a classificação contra L1d 32 KiB / L2 512 KiB / L3 16 MiB (cabe ou não em cache), as três razões com a dispersão entre rodadas, e uma leitura curta do que os números dizem sobre a decisão SoA-por-campo (§4.2) — sem meta numérica nesta fase (§7). Linha em `bench/results/README.md` apontando para `engine/README.md`.

- [ ] **Step 5: `CLAUDE.md`:** seção da engine com os comandos (`WC`, `make engine-ts`, `make engine-test`, presets `wasm-engine`/`wasm-engine-release`, comando do benchmark), a camada `engine/` acima do `world`, `RawQuery`/`addRaw` na descrição do ECS (e que as APIs template não delegam para elas, com o porquê), e as regras da ponte (views só dentro do callback; toda chamada que aloca passa pela `Bridge` e faz `sync`).

- [ ] **Step 6: Verificação final**

Run: `$DC make test && $DC make test PRESET=release && $WC make test PRESET=wasm-debug && $WC make engine-test && $WC sh -c 'make test PRESET=wasm-engine-release'`
Expected: tudo verde.

- [ ] **Step 7: Commit** `bench: benchmark da fronteira WASM↔TS da engine (fase 1)`.
