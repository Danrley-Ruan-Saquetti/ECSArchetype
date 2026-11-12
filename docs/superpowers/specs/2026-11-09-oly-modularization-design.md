# Oly — modularização e revisão das APIs públicas — design

Data: 2026-11-09. Base: branch `develop` depois da fase 1 da engine (`docs/superpowers/plans/2026-11-09-engine-phase1-bridge.md`).
Specs que continuam valendo: `2026-10-16-ecs-archetype-spec.md` (requisitos `R-*`, invariantes `I-*`, metas `NF-*`) e
`2026-11-09-game-engine-design.md` (a engine e as fases 2–7).

Marcação: **[decisão do usuário]** foi decidido na conversa; **[decisão do autor]** foi escolha minha durante o design,
que o usuário pode revisar; o resto é consequência direta das duas.

## 1. Intenção

A engine passa a se chamar **Oly** **[decisão do usuário]**. O nome não tem significado e vale para a engine inteira:
núcleo C++, API C, pacotes TS e build.

Hoje a árvore não mostra a arquitetura. O ECS ocupa a raiz (`include/`, `src/`, `tests/`, `bench/`) como se fosse o
projeto todo, e a engine é uma pasta irmã (`engine/`) com outra convenção: `tests/` em C++ ao lado de `test/` em TS,
gerador, fixture e pacote TS no mesmo nível. As camadas existem no código, mas valem só por disciplina: qualquer
arquivo inclui qualquer header, e a superfície pública de cada camada expõe detalhes internos.

O objetivo é deixar o projeto organizado como uma engine grande (no espírito de Unreal, O3DE e Bevy). Os critérios
do usuário são **clareza, coesão, design de código, bons contratos e uma API coesa** **[decisão do usuário]**:

- cada módulo é uma unidade autocontida (API pública, implementação privada, testes e benchmarks próprios) que
  declara de quais módulos depende;
- **o build garante as fronteiras**: um módulo só enxerga os headers públicos dos módulos que declarou como
  dependência;
- as três superfícies públicas (ECS em C++, API C, pacotes TS) expõem só o que quem usa precisa, com um vocabulário
  único;
- as fases 2–7 do spec da engine (loop, eventos, input, schedule, estados, jogo) entram como módulos novos sem
  mexer nos existentes.

## 2. Objetivos, não-objetivos, anti-objetivos

**Objetivos**
- Árvore nova (§3), com histórico preservado (`git mv`).
- Build por módulo, com dependências declaradas e fronteiras verificadas (§4).
- O nome Oly aplicado em namespaces, includes, alvos, opções, API C, pacotes e erros (§5).
- Os nove pontos do levantamento de API resolvidos (§6–§8), com um glossário único (§5.2).
- O pacote TS dividido em `@oly/core` (não exige navegador) e `@oly/client` (camada fina do navegador) (§8)
  **[decisão do usuário]**.

**Não-objetivos**
- Funcionalidade nova. Loop, fases, eventos, input, schedule e estados continuam nas fases 2–6.
- Mudar comportamento, layout dos dados, regras de validade ou o caminho quente do ECS.
- Instalação e exportação como pacote CMake (`install`/`find_package`) e publicação dos pacotes npm.
- Renomear o diretório do repositório ou o repositório remoto **[decisão do autor]**.
- Reescrever specs e planos antigos. Eles são registro histórico e continuam citando os caminhos antigos
  **[decisão do autor]**.

**Anti-objetivos** (falha mesmo que funcione)
- Regredir o ECS: NF-P1 (≤ 1,10), NF-P2 (≤ 1,25) e NF-P5 (`/1000` ≤ 0,5) continuam atendidas, e todos os testes
  passam em todos os presets.
- Fronteira que vale só por convenção quando o build pode garanti-la.
- Ocultar detalhes do ECS de um jeito que tire inlining do caminho quente (pimpl, virtual ou indireção em
  `Query<Ts...>::each`/`eachBlock`, `World::get`).
- Aliases ou shims de compatibilidade com os nomes antigos. Não há usuário externo: a troca é limpa
  **[decisão do autor]**.
- Conceitos de jogo dentro de `engine/` (regra que já existe).

## 3. Árvore

```
<raiz>/
  CMakeLists.txt  CMakePresets.json  Makefile
  Dockerfile  Dockerfile.wasm  docker-compose.yaml  .clang-format  .gitignore  CLAUDE.md
  package.json  package-lock.json  tsconfig.base.json      # workspaces npm (§8.1)
  cmake/
    OlyOptions.cmake        # opções OLY_*, asserts, sanitizers
    OlyWarnings.cmake       # oly_warnings(<alvo>)
    OlyWasm.cmake           # oly_wasm_exceptions, oly_wasm_node_env
    OlyModule.cmake         # oly_add_module, oly_add_test, oly_add_bench, oly_add_contract_death
    OlyGame.cmake           # oly_add_game (gerador + link do módulo .mjs)
    Dependencies.cmake      # doctest, Google Benchmark (FetchContent)
    wasm/node_env.js        # hoje em tools/wasm/
  engine/
    runtime/                # C++, um módulo por pasta
      core/
        include/oly/core/   assert.hpp  align.hpp  small_map.hpp  type_name.hpp
        src/                assert.cpp
        tests/
      ecs/
        include/oly/ecs/    world.hpp  entity.hpp  component.hpp  query.hpp  query_by_id.hpp
                            command_buffer.hpp  diagnostics.hpp
                            detail/  table.hpp  archetype.hpp  entity_store.hpp  query_cache.hpp
                                     command_plan.hpp  ...                       (§6.1)
        src/
        tests/              suítes doctest, contract_death, fuzz, support/
        bench/              iterate, fragmentation, flush, transitions, common.hpp
      bridge/
        include/oly/bridge/ c_api.hpp  world.hpp  schema_registry.hpp
        src/
        tests/
    packages/               # TS, um pacote npm por pasta
      core/                 # @oly/core
        src/  tests/unit/
      client/               # @oly/client
        src/  tests/
    tools/
      codegen/              # @oly/codegen: esquema TS → C++
        src/  tests/
    testing/                # @oly/testing (privado): integração WASM da engine
      fixture/              # jogo mínimo de teste: schema.ts, systems.cpp (único lugar com nomes de jogo)
      tests/wasm/           # hoje engine/test/wasm/
      bench/                # boundary.ts (benchmark da fronteira)
  games/                    # vazio até a fase 7 (jogo de validação)
  tools/                    # scripts de medição: bench_ratio.py, bench_ladder.py, perf_fragmented.sh, ...
  docs/
    superpowers/specs/  superpowers/plans/
    benchmarks/             # hoje bench/results/: README.md, ecs/, wasm/, bridge/ (§9)
```

Decisões da árvore:

- **`engine/runtime/` (C++) e `engine/packages/` (TS) separados por linguagem**, porque cada lado tem o próprio
  sistema de build e de dependências (CMake e npm workspaces) **[decisão do autor]**.
- **Módulo C++ = `include/oly/<módulo>/` + `src/` + `tests/` + `bench/`.** O caminho de include
  `oly/<módulo>/...` repete o nome do módulo de propósito: o include já diz de onde vem o header.
- **`core` (C++)** recebe o que hoje é `include/ecs/detail/` e não é específico do ECS: `assert`, `align`,
  `small_map`, `type_name` **[decisão do autor]**. `type_info` continua no `ecs` (é o registro de componentes).
  O builder pode mover mais coisa para o `core` se ela for genérica de verdade.
- **`engine/testing/`** é o pacote privado `@oly/testing` com o jogo fixture (esquema + `systems.cpp`, montado com
  `oly_add_game`), os testes de integração WASM e o benchmark da fronteira. Eles dependem do fixture, e o fixture
  depende de `@oly/core`. Colocá-los dentro de `@oly/core` criaria um ciclo nas project references e poria nomes
  de jogo no pacote. Por isso o `core` fica só com os testes unitários, e a regra "nomes de jogo só no fixture"
  passa a valer para `engine/testing/fixture/` **[decisão do autor]**. `games/` fica reservado para o jogo de
  validação da fase 7.
- **Benchmarks:** o código vai para dentro de cada módulo (`engine/runtime/ecs/bench/`,
  `engine/testing/bench/`) e os resultados registrados vão para `docs/benchmarks/`. São documentação de
  medição, não código **[decisão do autor]**.
- **`tools/`** fica só com os scripts de medição e análise. O que é peça do build (`node_env.js`) vai para `cmake/`.

## 4. Build e fronteiras

### 4.1 Módulos C++

`oly_add_module(NAME <nome> DEPENDS <módulos...> [HEADER_ONLY])` cria o alvo `oly_<nome>` com alias `oly::<nome>`,
compila `src/*.cpp`, publica `include/` e liga **só** os módulos de `DEPENDS`. Dependências da fase:

```
oly::core  ←  oly::ecs  ←  oly::bridge  ←  jogos (oly_add_game)
```

Assim a regra "o ECS não conhece a engine" passa a ser garantida pelo build: `oly::ecs` não liga `oly::bridge`, então
`#include "oly/bridge/..."` dentro do ECS não compila. Na fase 2 em diante, módulos novos (`app`, `events`, `input`,
`schedule`, `state` ou como o plano da fase os chamar) entram em `engine/runtime/<nome>/` com o próprio
`oly_add_module`.

As dependências são ligadas como `PUBLIC` (os headers do `ecs` incluem o `core`, e os do `bridge` expõem o
`World`), então o CMake as torna visíveis transitivamente. Mesmo assim, **cada módulo e cada jogo declara em
`DEPENDS` todo módulo que inclui diretamente**, e o `oly.lint.boundaries` (§4.2) reprova `#include "oly/<m>/..."`
num módulo que não declarou `<m>` **[decisão do autor]**. Um jogo, por exemplo, declara `ecs` e `bridge`.

Se um módulo C++ precisar ser `OBJECT` (como `engine_core` hoje, para os `EMSCRIPTEN_KEEPALIVE` não serem
descartados no link), o `oly_add_module` aceita isso por opção. Hoje isso vale para o `bridge`.

### 4.2 Detalhes internos (`detail/`)

Os templates do caminho quente (`World::spawn<Ts...>`, `Query<Ts...>::each`, `World::get<T>`) precisam das
definições de `Table` e `Archetype` no ponto de uso. Esconder esses tipos fisicamente (pimpl, interface virtual)
violaria um anti-objetivo (§2). Por isso:

- os tipos internos ficam em `include/oly/ecs/detail/` e no namespace `oly::ecs::detail`;
- **uma verificação no ctest** (`oly.lint.boundaries`, script em `tools/` ou CMake script) falha se algum arquivo
  fora de `engine/runtime/ecs/` incluir `oly/ecs/detail/`. Os testes e benchmarks do próprio módulo podem incluir.
  A mesma verificação cobre `oly/core` e `oly/bridge`, se eles tiverem `detail/`, e o TS (§8.3).

Ou seja: o compilador vê os detalhes, mas o build reprova quem os usa de fora.

### 4.3 Opções, presets, Makefile e nomes de teste

- Opções `ECS_*` passam a `OLY_*` (`OLY_BUILD_TESTS`, `OLY_BUILD_BENCH`, `OLY_SANITIZE`, `OLY_WERROR`,
  `OLY_VEC_REPORT`, `OLY_ENABLE_ASSERTS`), e `ENGINE_BUILD_FIXTURE` passa a `OLY_BUILD_GAMES`.
- Presets: `debug-asan`, `release`, `relwithdebinfo`, as variantes do estudo de custo WASM e `wasm-debug` mantêm
  os nomes. `wasm-engine` e `wasm-engine-release` passam a `wasm-games` e `wasm-games-release`, porque o que os
  distingue é compilar os jogos **[decisão do autor]**.
- Makefile: `engine-ts` passa a `ts` (npm ci se preciso + build de todos os workspaces) e `engine-test` passa a
  `games-test` (`ts` + ctest em `wasm-games`). `configure`/`build`/`test`/`bench` continuam como estão.
- Testes no ctest com prefixo de módulo: C++ como `<módulo>.<suíte>` (`ecs.unit_world`,
  `ecs.contract_death.<caso>` ou equivalente, `bridge.c_api`, `core.<suíte>`) e TS como `ts.<pacote>.<suíte>`
  (`ts.core.unit`, `ts.client.unit`, `ts.codegen.unit`, `ts.testing.wasm`), além de `oly.lint.boundaries`. Assim
  `make test T='^ecs\.'` roda um módulo, e o `core` C++ não se confunde com o `@oly/core` **[decisão do autor]**.
- Os presets do estudo de custo WASM (`native-clang*`, `wasm-scalar`, `wasm-simd`) continuam compilando e passando
  nos testes, e `tools/run_ladder.sh`, `bench_ladder.py` e `extract_codegen.sh` são atualizados para os nomes novos
  dos alvos. Eles fazem parte da regressão zero **[decisão do autor]**. Os executáveis de benchmark ganham o prefixo do módulo
  (`ecs_bench_iterate` etc.), e os scripts de `tools/` que os chamam são atualizados.

## 5. Nome e vocabulário

### 5.1 Onde o nome Oly aparece **[decisão do usuário, mapeamento do autor]**

| Lugar | Antes | Depois |
|---|---|---|
| Namespace C++ | `ecs::`, `engine::` | `oly::ecs`, `oly::bridge`, `oly::core` |
| Includes | `"ecs/world.hpp"` | `"oly/ecs/world.hpp"` |
| Alvos CMake | `ecs`, `engine_core` | `oly::ecs`, `oly::bridge`, `oly::core` |
| Funções CMake | `ecs_add_test`, `engine_add_game` | `oly_add_module`, `oly_add_test`, `oly_add_game` |
| Assert | `ECS_ASSERT`, `ECS_ENABLE_ASSERTS` | `OLY_ASSERT`, `OLY_ENABLE_ASSERTS` (em `oly::core`) |
| API C | `engine*`, `ENGINE_API` | `oly*`, `OLY_API`; tipos `OlyWorld`, `OlyQuery`, `OlyIteration`, `OlyEntity` |
| Pacotes npm | `engine` | `@oly/core`, `@oly/client`, `@oly/codegen` |
| Erros TS | `EngineContractError` | `ContractError` **[decisão do usuário]**; `SchemaMismatchError` mantém o nome |
| Runtime TS | `EngineRuntime`, `loadEngine`, `EngineModule` | `Runtime`, `loadRuntime`, `OlyModule` (interno) **[decisão do autor]** |

O prefixo `oly` aparece **só na API C** e no que é interno à ponte. Motivo: símbolos `extern "C"` vivem num espaço
global único do módulo WASM, junto com a libc, o runtime do Emscripten e os exports C dos sistemas do jogo, e
`namespace` não muda o nome do símbolo. Como só a `Bridge` chama esses exports, o prefixo não chega a quem usa a
engine. As APIs C++ e TS não repetem `oly` nos nomes.

### 5.2 Glossário (o mesmo verbo para o mesmo conceito em C++, API C e TS)

| Conceito | C++ (`oly::ecs`) | API C | TS (`@oly/core`) |
|---|---|---|---|
| criar entidade | `World::spawn()`, `spawn(components...)`; `CommandBuffer::spawn()` | `olyEntitySpawn` | `World.spawn(types, values?)` |
| destruir entidade | `destroy` | `olyEntityDestroy` | `destroy` |
| entidade viva | `isAlive` | `olyEntityIsAlive` | `isAlive` |
| adicionar / remover / ter | `add`, `remove`, `has` | `olyComponentAdd`, `olyComponentRemove`, `olyComponentHas` | `add`, `remove`, `has` |
| ler componente presente (contrato: existe) | `get` → `T&` | — | `get` → cópia |
| ler se existir | `tryGet` → `T*` ou nulo | `olyComponentGet` → endereço ou 0 | — |
| escrita parcial de campos | — (já é `get(e).x = ...`) | — | `set` |
| query | `query<Ts...>(With, Without)` | `olyQueryCreate` | `query(spec)` |
| iterar | `each`, `eachBlock` | `olyQueryBegin` / `olyQueryEnd` | `eachBlock` |
| variante por ID em tempo de execução | sufixo **`ById`**: `addById`, `removeById`, `hasById`, `getById`, `tryGetById`, `queryById` | (a API C é toda por ID) | — |

`create` deixa de existir como verbo de entidade. "Raw" deixa de existir.

## 6. ECS em C++ (`oly::ecs`)

### 6.1 Headers públicos

| Header | Conteúdo |
|---|---|
| `world.hpp` | `World` (spawn/destroy/isAlive/isIterating, add/remove/has/get/tryGet, as variantes `ById`, `commands`, `flush`, `query`, `queryById`) |
| `entity.hpp` | `Entity`, `NullEntity`, `EntityHash` |
| `component.hpp` | `ComponentId`, `componentId<T>()`, `TypeInfo`, `typeInfo`, `registeredTypeCount` (hoje em `type_info.hpp`) |
| `query.hpp` | `Query<Ts...>`, `With`, `Without` |
| `query_by_id.hpp` | `QueryById`, `QueryByIdSpec`, `BlockById`, `QueryIteration` (§6.3) |
| `command_buffer.hpp` | `CommandBuffer`: `spawn`, `destroy`, `add`, `remove`, `addById`, `removeById`, `commandCount`, `empty`, `clear` |
| `diagnostics.hpp` | `checkInvariants(const World&, std::string* why)`, `stats(const World&)`, `WorldStats`, `ArchetypeStats`, `signatureLookupCount(const World&)` |

Vão para `detail/` (namespace `oly::ecs::detail`): `Table`, `Archetype`, `Signature`, `Edge`, `EntityStore`,
`EntityRecord`, `QueryCacheBase` (com `onArchetypeCreated`/`initialize`/`matches`/`isMatched`), `MatchedTable`,
`IterationGuard`, `signatureMatches`, `CommandOperation`, `CommandView`, `CommandPlan`, `EntityPlan`, `NetEffect`,
`NetComponentOperation`. Também saem da superfície pública de `CommandBuffer` os métodos `plan()`, `netEffect()`,
`forEach()` e `store()`: o `World` os usa por `friend` ou por um acessor em `detail`.

### 6.2 Diagnóstico e ganchos de teste

- `stats`, `signatureLookupCount` e `checkInvariants` saem da classe `World` e passam a funções livres em
  `diagnostics.hpp`, com acesso por `friend`. `checkInvariants` continua compilado em todos os builds (o `bridge`
  e os testes usam).
- `flushSequential` (oráculo sem coalescing, só usado em teste) e `EntityStore::setGenerationForTest` saem da API.
  Ficam acessíveis só aos testes do módulo, por um acessor `detail::WorldTestAccess`/`detail::EntityStoreTestAccess`
  declarado em `detail/` e usado em `engine/runtime/ecs/tests/` **[decisão do autor]**. O builder escolhe a forma
  exata (`friend struct` ou outra), desde que a classe pública não tenha nada "para teste".
- `CommandBuffer::forEach` é usado hoje por `tests/command_buffer.cpp`. Passa pelo mesmo acessor de teste.

### 6.3 API por ID

- `World::rawQuery(RawQuerySpec)` passa a `World::queryById(QueryByIdSpec)`, com a mesma semântica (cache pela
  spec; `read` antes de `write`; tag em `read`/`write`, tipo repetido e ID desconhecido continuam sendo erro de
  contrato).
- **O bloco não expõe `Table`.** `QueryById::eachBlock(callback)` chama `callback(const BlockById& block)`, em que
  `BlockById` tem `count()`, `entities()` (`std::span<const Entity>`) e `column(i)` (`void*` da coluna do i-ésimo
  componente, na ordem read seguida de write).
- **O par manual `beginIteration`/`endIteration` deixa de ser público.** No lugar entra
  `QueryIteration QueryById::iterate()`: um objeto RAII, só movível, que marca o `World` como iterando enquanto
  existe, expõe os próprios blocos (contagem, endereço das entidades, endereço das colunas) e encerra a iteração
  no destrutor. Aninhar é possível: cada iteração tem os seus blocos.
- **Sem alocação por iteração em regime:** os buffers de blocos são reaproveitados, um por nível de aninhamento
  da `QueryById` (pool que cresce até a maior profundidade vista e não encolhe), em vez de um vetor novo a cada
  `iterate()` **[decisão do autor]**. Hoje o buffer do world é reutilizado, e a fronteira não pode passar a
  alocar a cada `eachBlock` do TS.
- `addRaw`/`removeRaw`/`hasRaw`/`getRaw` passam a `addById`/`removeById`/`hasById`/`tryGetById`. Entra também
  `getById`, com o mesmo contrato do `get<T>` (assert se a entidade está morta ou o componente ausente).
  `CommandBuffer::addRaw` passa a `addById`. As APIs template continuam com o caminho próprio, sem delegar para as
  `ById` no caminho quente (mesma decisão e mesmo motivo de hoje).

### 6.4 Restante

- `World::create` passa a `World::spawn`, nas duas formas.
- Mensagens de assert seguem os verbos novos (`"spawn durante iteração..."`, `"queryById ... tag"`). Os casos de
  `contract_death` exigem as substrings novas.
- `Query<Ts...>::writes` (reservado para o scheduler, R-QY-1) continua.

## 7. API C (`oly::bridge`)

### 7.1 Nomes: `oly<Objeto><Verbo>`

```
olyWorldCreate  olyWorldDestroy  olyWorldCheckInvariants
olySchemaHash  olySchemaComponentCount  olySchemaComponentId
olyComponentSize  olyComponentAlignment                               // por ComponentId (tipo)
olyEntitySpawn  olyEntityDestroy  olyEntityIsAlive
olyComponentAdd  olyComponentRemove  olyComponentHas  olyComponentGet // na entidade
olyQueryCreate  olyQueryBegin  olyQueryEnd
```

Assinaturas, tipos de retorno e convenções (`int32_t` 0/1 para booleano, saída por ponteiro, `(index, generation)`
na entrada) seguem as atuais. O builder pode corrigir inconsistências que encontrar, desde que sejam registradas no
plano.

### 7.2 Iteração com buffer próprio

`olyQueryBegin(world, query, outBlockCount, outDescriptors)` devolve um `OlyIteration*` (um `QueryIteration` do
§6.3) com os seus descritores (mesmo formato de hoje: `count`, endereço das entidades, endereço de cada coluna).
`olyQueryEnd(world, iteration)` o libera. Os descritores de uma iteração valem até o `olyQueryEnd` dela, então um
`eachBlock` aninhado não sobrescreve o externo, e o TS não precisa mais copiar o buffer (pode continuar copiando
se medir melhor; é escolha do builder).

### 7.3 `OlyWorld` opaco

`world_handle.hpp` (que expunha `blockDescriptors` aos jogos) deixa de existir. O tipo `OlyWorld` é opaco na API C,
e os sistemas C++ dos jogos obtêm o `World` por `oly::bridge::worldOf(OlyWorld*) -> oly::ecs::World&` em
`oly/bridge/world.hpp`. O contrato de link do esquema continua: `oly::bridge::schemaHash()` e
`oly::bridge::schemaComponentIds()` são definidas pelo código gerado do jogo (ou à mão nos testes do `bridge`).

## 8. Pacotes TS

### 8.1 Workspaces

`package.json` na raiz com `workspaces: ["engine/packages/*", "engine/tools/*", "engine/testing", "games/*"]`,
um único `package-lock.json` e `tsconfig.base.json` (`strict`, ES2022, `NodeNext`). Cada pacote tem o próprio
`package.json` e `tsconfig.json` com **project references**: `@oly/client`, `@oly/codegen` e `@oly/testing`
referenciam `@oly/core`, e o `core` não referencia ninguém. Build com `tsc -b` na raiz.

A saída de cada pacote vai para **`dist/` dentro do próprio pacote** (gitignored), não para `build/ts/`. O Node não
aceita alvo de `exports` fora do pacote, e a resolução por nome (`@oly/core`, `@oly/core/internal`) pelos
workspaces exige isso **[decisão do autor]**. Os caminhos que o CMake usa (bin do `oly-codegen`, `schema.js` do
fixture, `node --test`) passam a apontar para esses `dist/`, e `module-package.json` deixa de existir. As versões continuam fixadas e exatas
(TypeScript 5.6.3, `@types/node` 20.17.6), sem bundler e com `node:test`.

### 8.2 `@oly/core`: a engine no lado TS, sem navegador **[decisão do usuário]**

Contém tudo o que existe hoje em `engine/ts/`. Não usa DOM, `window`, `requestAnimationFrame`, `fetch` nem eventos
de teclado e mouse, e roda em node, no navegador e em worker. O que depende do ambiente entra por injeção: hoje, a
fábrica do módulo WASM em `loadRuntime(factory, schema)`; nas fases seguintes, relógio e fonte de input.
`tsconfig` do core sem `lib: ["DOM"]`, para o compilador reprovar o uso de API de navegador.

**Superfície pública** (entrada `"."` do `exports`):
- esquema: `component`, `tag`, `defineSchema`, `Schema`, `ComponentType`, `TagType`, `SchemaType`, `Fields`,
  `ValueOf`, `ScalarType`, `ComponentLayout`/`FieldLayout` (como dados de leitura em `type.layout`);
- runtime: `loadRuntime`, `Runtime` (`schema`, `createWorld()`), `OlyModuleFactory`;
- mundo: `World` (`spawn`, `destroy`, `isAlive`, `has`, `add`, `remove`, `get`, `set`, `query`, `checkInvariants`,
  `dispose`, `handle`), `SpawnValues`;
- query: `Query` (`eachBlock`), `QuerySpec`, `Block` (`count`, `entity`, `read`, `write`), `ReadView`,
  `WriteView`, `ReadField`, `WriteField`, `RawField`;
- entidade: `Entity`, `NullEntity`, `entityIndex`, `entityGeneration`, `formatEntity`;
- erros: `ContractError`, `SchemaMismatchError`.

**Interno** (entrada `"./internal"`, só para `@oly/client`, `@oly/codegen`, `@oly/testing` e os testes do core): `Bridge`, `Heap`,
`readScalar`/`writeScalar`, `makeView`, `computeLayout`, `scalarSize`/`scalarAlignment`, `fnv1a32`,
`canonicalSchema`, `schemaHash`, `packEntity`, `MaxEntityGeneration`, `OlyModule` (exports `_oly*`), `Runtime.bridge`
e `Runtime.module`.

**Membros que deixam de ser públicos:** `World.iterationDepth` (hoje campo mutável), `World.assertUsable`,
`Block.invalidate`, `Query.writes`, `Query.positionOf`. O mecanismo (`#private`, funções do módulo, `@internal` com
`stripInternal`) fica a critério do builder. O requisito é que o `.d.ts` público de `@oly/core` não mostre nenhum
deles.

### 8.3 `@oly/client`: camada fina do navegador **[decisão do usuário]**

Depende só da superfície pública de `@oly/core` (e de `./internal` se for inevitável, justificado no plano). Não
acrescenta regra de engine nem de jogo: só implementa, com APIs do navegador, as portas que o core recebe por
injeção. Quem usa só o `core` funciona igual, sem o `client`.

Nesta rodada ele nasce com o mínimo: uma fábrica que carrega o módulo Emscripten de um jogo a partir de uma URL
(o `.mjs` e o `.wasm` ao lado) e devolve um `OlyModuleFactory` para `loadRuntime`. Testes em node com o import
substituído. O `tsconfig` do client inclui `lib: ["DOM"]`. Loop por rAF (fase 2) e input (fase 4) chegam nas
respectivas fases.

### 8.4 `@oly/codegen`

O gerador (`emit_cpp`, `main`) sai do pacote TS da engine e vira ferramenta de build, com bin `oly-codegen
--schema <schema.js> --out <dir>`. Usa `@oly/core` (esquema) e `@oly/core/internal` (layout, hash canônico).
`oly_add_game` o chama. A saída gerada passa a usar os nomes novos (`oly/ecs/entity.hpp`, `oly::ecs::Entity`,
`oly::bridge::schemaHash`).

### 8.5 Verificação das fronteiras no TS

`oly.lint.boundaries` (§4.2) também reprova: import de `@oly/core/internal` fora de `engine/` (jogos não usam o
interno); import de `@oly/client` por `@oly/core`; caminhos relativos que atravessem pacotes (`../../core/src/...`)
em vez do nome do pacote. Um teste de superfície confere o `.d.ts` público de `@oly/core` contra uma lista
versionada de exports, para que adicionar algo à API pública seja uma mudança explícita.

## 9. Benchmarks e documentação

- `bench/results/` passa para `docs/benchmarks/`, com subpastas `ecs/` (os `m*`/`e1_*` atuais), `wasm/` (estudo de
  custo) e `bridge/` (hoje `engine/`, benchmark da fronteira). O `README.md` de cada pasta é atualizado com os
  caminhos e comandos novos. Os JSON antigos não são editados.
- **Reexecução obrigatória** depois da mudança, registrada como `r1_*` em `docs/benchmarks/ecs/`, com o mesmo
  protocolo e o mesmo `tools/bench_ratio.py`: NF-P1, NF-P2 e NF-P5 dentro das metas. Regressão confirmada (repetida
  por causa do ruído do WSL2) bloqueia a entrega. Também o benchmark da fronteira (`boundary`), registrado em
  `docs/benchmarks/bridge/` para comparar com a fase 1 (sem meta numérica).
- `CLAUDE.md` reescrito para a árvore, os comandos, os módulos, as fronteiras e o glossário novos.
- `.claude/skills/add-test-case`, `.claude/skills/bench-compare`, `.claude/agents/perf-reviewer.md` e
  `.claude/agents/spec-conformance-reviewer.md` são atualizados para os caminhos, alvos e nomes novos.
- Specs e planos antigos não mudam. O plano desta modularização registra a tabela "caminho antigo → novo" para
  quem ler os documentos antigos.

## 10. Critérios de pronto

1. A árvore do §3 existe e os arquivos movidos preservam histórico (`git mv`).
2. `$DC make test` (debug-asan), `$DC make test PRESET=release`, `$WC make test PRESET=wasm-debug`,
   `$WC make games-test`, o Release de `wasm-games-release` e os presets do estudo de custo estão verdes, com
   todos os casos de `contract_death`.
3. `oly.lint.boundaries` e o teste de superfície do TS rodam no ctest e passam; um include proibido introduzido à
   mão os faz falhar (verificado uma vez e desfeito).
4. Nenhum nome antigo sobrevive no código, no build ou nos documentos vivos: `ECS_ASSERT`, `ecs::` sem `oly::`,
   `engine*` na API C, `Raw`, `create` como verbo de entidade, `EngineContractError`, `loadEngine`. Busca no
   repositório, excluindo specs/planos antigos e `docs/benchmarks/` histórico.
5. NF-P1, NF-P2 e NF-P5 reexecutadas e dentro das metas; benchmark da fronteira registrado.
6. `CLAUDE.md`, skills e agentes do projeto atualizados.

## 11. Fica a critério do builder

- A ordem das mudanças e o recorte dos commits. Sugestão: (1) mover a árvore e o build sem renomear nada, verde;
  (2) aplicar o nome Oly (namespaces, includes, alvos, opções); (3) superfície do ECS (§6); (4) API C (§7);
  (5) divisão e superfície do TS (§8); (6) benchmarks e documentação (§9). Cada passo termina verde.
- O nome do branch (sugestão: `refactor/oly-modulos` a partir de `develop`).
- Como implementar `oly.lint.boundaries` (script Python com `-I`, script CMake ou shell), desde que rode no ctest
  dos presets nativos e falhe com mensagem que aponte arquivo e linha.
- A forma dos acessores de teste (§6.2) e o mecanismo de esconder membros no TS (§8.2).
- Mover para o `core` (C++) mais utilidades realmente genéricas.
- Pequenos ajustes de nome não cobertos pelo glossário, seguindo o mesmo padrão e registrados no plano.
