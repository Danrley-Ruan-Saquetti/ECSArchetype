# Engine — Fase 3 (Eventos) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eventos tipados no núcleo C++ (declaração na `Engine`, filas, emissão, handlers por lote, despacho tipo a tipo com cascata e limite de rodadas), expostos na API C e no `@oly/core`, com `event(...)` no esquema e no gerador, e a medição de eventos do §7.

**Architecture:** O `app` ganha os eventos inteiros: `Engine::declareEvent<T>()` dá um `EventTypeId` denso (ordem de declaração = ordem de despacho), `detail::EventQueues` guarda por tipo uma fila `pending` e uma `dispatching` (bytes crus, layout do struct), e o passo 4 (`dispatchEvents`) faz rodadas: move tudo que está pendente para `dispatching`, chama cada handler do tipo uma vez com o lote inteiro, tipo a tipo, até não sobrar nada ou estourar `maxEventRounds`. Handlers C++ recebem um `HandlerContext` com mudanças estruturais diretas. O `bridge` declara os eventos do esquema na criação da `OlyEngine` (na ordem do esquema) e registra handlers TS como uma função C++ que chama a mesma função JS importada dos sistemas, agora com `kind`, `id`, endereço e tamanho do lote. No TS, `event()` entra no esquema, `Engine.on`/`addNativeHandler` registram handlers e `EventBatch` dá views sobre a fila na memória do WASM, com os mesmos acessores dos blocos.

**Tech Stack:** C++20, GCC 14 (nativo), Emscripten 3.1.74 + node 20.18 (serviço `wasm`), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (fase 3 da §5; §4.4 inteira, §4.1 para `event`, §4.3 para a ordem do passo, §6, §7 "Custo dos eventos"). `§N` abaixo são desse spec.

## Global Constraints

- Branch `feat/engine-phase3-events` a partir de `develop`. Commits, comentários, mensagens de assert e de erro em pt-BR; commits terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain só em Docker: `DC="docker compose run --rm dev"` (nativo, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). Presets `wasm-games*` exigem `make ts` antes.
- `-Wall -Wextra -Wpedantic -Werror` em todo alvo C++; TS `strict` (§3).
- Camadas: `core ← ecs ← app ← bridge ← jogos`. O ECS não muda nesta fase (se mudar, reexecutar `ecs_bench_*` e conferir NF-P1/P2/P5 como em `docs/benchmarks/README.md`).
- Engine completa em C++ (§2): declaração, filas, ordem, cascata, limite, descarte e recuperação vivem em `oly::app::Engine`. O TS só valida uso, escreve valores e repassa.
- Fronteira: sistemas C++ emitem sem cruzar a fronteira; um handler TS é chamado uma vez por lote; emitir do TS cruza uma vez por evento (§4.4). Nunca uma chamada por evento no despacho.
- Nada de conceito de jogo em `engine/runtime` nem em `engine/packages`; nomes de jogo só em `engine/testing/fixture/` e nos testes.
- Glossário (`CLAUDE.md`): mesmo verbo nas três camadas — `declareEvent` (C++), `emit` (C++ `emit`, C `olyEngineEmit`, TS `context.emit`), `on` (C++ `Engine::on`, C `olyEngineAddScriptHandler`, TS `Engine.on`), `addNativeHandler` (C `olyEngineAddNativeHandler`, TS `addNativeHandler`, C++ `OLY_HANDLER`). `oly` só na API C.
- Estilo `CLAUDE.md`: chaves em todo bloco, nomes completos, dados no topo da classe, um `namespace` por arquivo, includes do projeto com aspas depois da biblioteca padrão. Não formatar à mão (hook `clang-format`).

### Decisões que o spec deixa em aberto (fixadas aqui)

- **Identidade do tipo em C++:** `EventKey eventKey<T>()` é denso por processo (como `componentId`, mas separado: eventos não são componentes). Cada `Engine` mapeia `EventKey → EventTypeId`; o `EventTypeId` é a posição na ordem de declaração daquela `Engine`. `declareEvent<T>(name = oly::core::typeName<T>())` exige `std::is_trivially_copyable_v<T>` (`static_assert`) e `alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__` (`static_assert`): a fila é um `std::vector<std::byte>`.
- **Nome nas mensagens:** o nome passado a `declareEvent`. O código gerado passa o nome do esquema (`"Ping"`), para a mensagem do C++ bater com o nome que o jogo TS conhece.
- **Filas:** por tipo, `pending` (recebe emissões) e `dispatching` (lote da rodada). No início da rodada, todo tipo com `pending` não vazio troca (`swap`) as duas; o lote de um tipo não muda nem se move enquanto seus handlers rodam, porque emissões vão para `pending`. `clear()` mantém a capacidade: depois do aquecimento, o despacho não aloca.
- **Contador de rodadas por passo:** `eventRoundsThisStep_` é zerado no início do passo fixo e conta todas as rodadas do passo (a fase 6 vai chamar `dispatchEvents` de novo depois de `onEnter`/`onExit`, §4.4).
- **Estouro do limite:** `OLY_ASSERT(false, "dispatchEvents: limite de %u rodadas de eventos estourado; pendentes: %s", ...)` com os nomes dos tipos ainda pendentes separados por `, `. Com asserts desligados, descarta todas as filas e segue o passo (comportamento definido, sem laço infinito). O TS não detecta o estouro antes (precisaria repetir o despacho, que é lógica de engine): vale o assert.
- **Relógio nos handlers:** handlers veem o mesmo `time()` dos sistemas do passo (início do passo). Para isso, o `++steps_` sai de antes do `dispatchEvents` e vai para o fim de `runFixedStep`.
- **Emitir fora das fases de gameplay:** emissão em `Render`, fora de `frame` (`Engine::emit` direto) ou com a aba oculta fica em `pending` e é despachada no passo 4 do próximo passo fixo que rodar.
- **Contextos C++:** `EngineContext` (novo, `oly/app/context.hpp`) junta o que `SystemContext` e `HandlerContext` têm em comum: `fixedDeltaTime`, `time`, `pause`, `resume`, `paused`, `query`, `get`, `tryGet`, `has`, `isAlive`, `emit<T>`, `emitById`. `SystemContext` (adiadas + `interpolation`) e `HandlerContext` (diretas + lote) derivam dele.
- **Handler C++ por função:** `HandlerFunction = void (*)(HandlerContext& context)`; o lote sai de `context.events<T>()` (`std::span<const T>`; `OLY_ASSERT` se `T` não for o tipo do handler). `OLY_HANDLER(name, EventType)` define `void name(HandlerContext& context, std::span<const EventType> events)` e registra, por nome, um adaptador sem tipo que chama `name(context, context.events<EventType>())`.
- **Chamada de volta única:** `olyHostInvokeScript(OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount)`, com `kind` `OLY_SCRIPT_SYSTEM` (0; `events` nulo, `eventCount` 0) ou `OLY_SCRIPT_HANDLER` (1). `ScriptInvoker` e `Module.olyInvokeScript` mudam para a mesma assinatura.
- **Eventos do esquema na `OlyEngine`:** o construtor declara, na ordem do esquema, todos os eventos de `oly::bridge::schemaEvents()`; assim o índice do evento no esquema TS é o `EventTypeId` (`OLY_ASSERT` se não for). Jogo só em C++ declara à mão e não usa o esquema.
- **`olyEngineAddNativeHandler`:** devolve `OLY_INVALID_HANDLER` (`0xFFFFFFFFu`) se o nome não existe e `OLY_HANDLER_TYPE_MISMATCH` (`0xFFFFFFFEu`) se o handler nomeado é de outro tipo (ou de um tipo não declarado nesta engine); o TS transforma os dois em `ContractError` com os nomes.
- **Handler TS faz mudança estrutural direta:** durante a chamada de um handler TS, a `Engine` TS destrava o `World` (`setStructureLocked(world, false)`) e trava de novo em `finally`. `HandlerContext.spawn/destroy/add/remove` delegam ao `World` (mesmas validações e mensagens).
- **Views do lote e crescimento da memória (§4.4):** sem cópia. `EventBatch.read()` usa `makeView` de `fields.ts`, cujos campos leem `bridge.heap.<tipo>` a cada acesso; toda chamada que pode alocar passa pela `Bridge`, que faz `sync()`. O endereço do lote não muda (a fila `dispatching` não é tocada durante a rodada), só o `ArrayBuffer`. Fora da chamada do handler, `EventBatch.read()` lança `ContractError`.
- **`event()` exige ao menos um campo** (`ContractError`, como `component`); evento sem dados não entra nesta fase.
- **Esquema:** `defineSchema` aceita tipos e eventos misturados; `schema.types` continua só com componentes e tags (o índice deles é o do `ComponentId`), `schema.events` tem os eventos na ordem de declaração e `EventType.schemaIndex` é a posição em `schema.events`. Nomes são únicos entre tipos e eventos. Forma canônica do hash: a atual seguida de `!Nome{campo:tipo,...};` por evento — esquema sem eventos mantém o hash de hoje.
- **Recuperação (§4.4):** o `frame` seguinte a uma exceção descarta as duas filas de todos os tipos junto com os comandos pendentes (inclusive eventos emitidos entre a exceção e esse `frame`). O que handlers anteriores já fizeram fica.
- **Fora do escopo desta fase:** handlers restritos a estado (`inState`, fase 6), `Engine.emit` no TS (só os contextos emitem), emissão em lote a partir do TS (só se o §7 mostrar custo relevante).

## Review Focus

1. **Handler que emite muitos eventos do próprio tipo enquanto percorre o lote** (a fila `pending` realoca): o `std::span` do lote continua válido e com o mesmo tamanho; os novos chegam na rodada seguinte (Task 1, teste sob ASan).
2. **Entidade citada no evento já destruída** por um handler anterior, por um evento repetido do mesmo lote ou por uma rodada anterior: `destroyDead` com `isAlive` não dispara assert e `checkInvariants` fica limpo (Task 2).
3. **Memória do WASM cresce dentro de um handler TS** no meio da leitura do lote: valores e `entity` lidos depois do crescimento continuam certos (Task 5).
4. **Exceção num handler TS com cascata em andamento** (ele já emitiu outro tipo): o evento emitido não é despachado no `frame` seguinte, `checkInvariants` limpo e eventos novos voltam a funcionar (Task 5).
5. **Eventos sem handler ou emitidos fora de passo** (antes do primeiro `frame`, em `Render`): os sem handler somem ao fim do passo (um handler registrado depois não os vê); os emitidos fora de passo são despachados no próximo passo, nem perdidos nem duplicados (Tasks 1 e 2).

---

### Task 1: Eventos no `app` — declaração, emissão, handlers por lote e despacho em rodadas

**Files:**
- Create: `engine/runtime/app/include/oly/app/{event.hpp,context.hpp,handler.hpp,detail/event_queues.hpp}`, `engine/runtime/app/src/{context.cpp,handler.cpp,event.cpp,event_queues.cpp}`, `engine/runtime/app/tests/events.cpp`
- Modify: `engine/runtime/app/include/oly/app/{system.hpp,engine.hpp}`, `engine/runtime/app/src/{system.cpp,engine.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp}`

**Interfaces:**
- Consumes: `Engine`, `SystemContext`, `Phase` da fase 2; `oly::core::typeName<T>()`.
- Produces (namespace `oly::app`):
  - `event.hpp`: `using EventTypeId = uint32_t; using HandlerId = uint32_t; using EventKey = uint32_t; inline constexpr EventTypeId InvalidEventType = 0xFFFFFFFFu;` `template <class T> EventKey eventKey() noexcept;` (denso por processo; contador em `detail::nextEventKey()` em `event.cpp`).
  - `detail/event_queues.hpp`: `class EventQueues` com `EventTypeId declare(EventKey key, std::string_view name, uint32_t size)`, `EventTypeId typeOf(EventKey key) const noexcept` (`InvalidEventType` se não declarado), `uint32_t typeCount() const noexcept`, `std::string_view name(EventTypeId type) const`, `void push(EventTypeId type, const void* event)`, `bool beginRound()` (troca `pending`/`dispatching` dos tipos com pendência; `false` se nada pendente), `const std::byte* batchData(EventTypeId type) const`, `uint32_t batchCount(EventTypeId type) const`, `void endRound()` (limpa `dispatching`), `void clear()` (as duas filas de todos os tipos), `std::string pendingNames() const`.
  - `context.hpp`: `class EngineContext` com os membros listados em "Decisões" (`template <class T> void emit(const T& event)`, `void emitById(EventTypeId type, const void* event)`; ambos `OLY_ASSERT` para tipo não declarado, com o nome do tipo C++ ou o id).
  - `system.hpp`: `SystemContext : public EngineContext` (mantém `phase`, `systemId`, `name`, `userData`, `interpolation` e as adiadas).
  - `handler.hpp`: `class HandlerContext : public EngineContext` com `EventTypeId eventType() const`, `HandlerId handlerId() const`, `std::string_view name() const`, `void* userData() const`, `template <class T> std::span<const T> events() const`, `uint32_t eventCount() const`, `const void* eventData() const`, e diretas `Entity spawn()`, `template <class... Ts> Entity spawn(Ts&&...)`, `destroy(Entity)`, `template <class T> add(Entity, T&&)`, `template <class T> remove(Entity)`, `addById(Entity, ComponentId, void* value)`, `removeById(Entity, ComponentId)`. `using HandlerFunction = void (*)(HandlerContext& context);` `struct NamedHandler { HandlerFunction function; EventKey eventKey; };` `const NamedHandler* findNamedHandler(std::string_view name) noexcept;` `struct NamedHandlerRegistration { NamedHandlerRegistration(std::string_view name, HandlerFunction function, EventKey eventKey); };` macro `OLY_HANDLER(name, EventType)`.
  - `engine.hpp`: `EngineConfig` ganha `uint32_t maxEventRounds = 16;` (positivo, `OLY_ASSERT`). `Engine` ganha `template <class T> EventTypeId declareEvent(std::string_view name = oly::core::typeName<T>())`, `EventTypeId eventTypeOf(EventKey key) const noexcept`, `uint32_t eventTypeCount() const noexcept`, `template <class T> HandlerId on(std::string_view name, HandlerFunction function, void* userData = nullptr)`, `HandlerId onById(EventTypeId type, std::string_view name, HandlerFunction function, void* userData = nullptr)`, `template <class T> void emit(const T& event)`, `void emitById(EventTypeId type, const void* event)`. `HandlerId` é denso por `Engine`, na ordem de registro.

- [ ] **Step 1: Branch** `git switch -c feat/engine-phase3-events`.

- [ ] **Step 2: Testes que falham** em `tests/events.cpp` (`oly_add_test(MODULE app NAME events SOURCES events.cpp SUPPORT engine/runtime/ecs/tests/support)`). Eventos do teste: `struct Ping { uint32_t value; }; struct Echo { uint32_t depth; };`. Handlers gravam em `Log` (de `support/log.hpp`) via `userData`. Um passo por `frame` (helper `step(engine)`), `checkInvariants(world)` no fim de cada caso.
```cpp
TEST_CASE("handler recebe o lote inteiro, na ordem de emissão") { /* Update emite Ping{1},{2},{3}; handler grava os valores */ CHECK(values == {1,2,3}); CHECK(chamadas do handler == 1); }
TEST_CASE("handlers do mesmo tipo na ordem de registro") { /* on<Ping> "h1", "h2", "h3" */ CHECK(log.calls == {"h1","h2","h3"}); }
TEST_CASE("tipos despachados na ordem de declaração, não na de emissão") { /* declara Ping, depois Echo; Update emite Echo e depois Ping */ CHECK(log.calls == {"ping","echo"}); }
TEST_CASE("evento emitido durante a rodada vai para a rodada seguinte") { /* handler de Ping emite Echo e Ping{99}; registra (tipo, count, rodada) */ CHECK(1ª rodada: Ping count 1); CHECK(2ª rodada: Ping{99} e Echo); }
TEST_CASE("lote não muda quando o handler emite muitos do próprio tipo") { /* handler de Ping{0}: lê span, emite 10000 Ping{1} (só na 1ª rodada), relê span */ CHECK(span.size() == 1); CHECK(span[0].value == 0); }
TEST_CASE("cascata termina no mesmo passo quando as filas esvaziam") { /* Echo{depth}: se depth < 5 emite Echo{depth+1} */ CHECK(depths == {0,1,2,3,4,5}); CHECK(tudo no mesmo frame); }
TEST_CASE("handler faz mudança estrutural direta") { /* handler: e = context.spawn(Position{1,2}) */ CHECK(context.isAlive(e) ainda dentro do handler); CHECK(world.get<Position>(e).x == 1); }
TEST_CASE("evento sem handler é descartado no fim do passo") { /* emite Ping sem handler; passo; registra handler; passo sem emitir */ CHECK(handler não chamado); }
TEST_CASE("OLY_HANDLER registra por nome com o tipo") { CHECK(findNamedHandler("appTestPingHandler")->eventKey == eventKey<Ping>()); CHECK(findNamedHandler("naoExiste") == nullptr); }
TEST_CASE("limite de rodadas configurável: exatamente maxEventRounds rodadas é permitido") { /* maxEventRounds = 3; cascata Echo de 3 rodadas */ CHECK(passa sem assert); }
```
`contract_death.cpp` ganha os casos (mesmo protocolo; `CASES` no `CMakeLists.txt`):

| Caso | Ação | Substrings |
|---|---|---|
| `emit_undeclared_event` | `engine.emit(Ping{})` sem `declareEvent` | `emit`, `não declarado` |
| `on_undeclared_event` | `engine.on<Ping>("h", ...)` sem `declareEvent` | `on`, `não declarado` |
| `declare_event_twice` | `declareEvent<Ping>()` duas vezes | `declareEvent`, `já declarado` |
| `on_during_frame` | sistema chama `engine.on<Ping>` | `on`, `durante frame` |
| `declare_event_during_frame` | sistema chama `engine.declareEvent<Echo>()` | `declareEvent`, `durante frame` |
| `event_round_limit` | handler de `Echo` (declarado com nome `"Echo"`) sempre emite `Echo` | `dispatchEvents`, `rodadas`, `Echo` |
| `events_wrong_type` | handler de `Ping` chama `context.events<Echo>()` | `events`, `tipo` |

- [ ] **Step 3: Ver falhar** — `$DC make test T='^app\.'` → erro de compilação (`event.hpp` não existe).

- [ ] **Step 4: Implementar.** `EventQueues` conforme "Decisões" (por tipo: `name`, `size`, `pending`, `dispatching` como `std::vector<std::byte>`; `typeOf` por um `std::vector<EventTypeId>` indexado por `EventKey`). Na `Engine`: `std::vector<std::vector<HandlerEntry>> handlers_` por `EventTypeId` com `HandlerEntry { HandlerFunction function; void* userData; std::string name; HandlerId id; }`; `HandlerContext` montado na pilha por chamada. `dispatchEvents()`:
```
enquanto queues_.beginRound():
  se eventRoundsThisStep_ == maxEventRounds: OLY_ASSERT(false, ...pendingNames...); queues_.clear(); retorna
  ++eventRoundsThisStep_
  para cada tipo em ordem de declaração com batchCount > 0: cada handler do tipo, em ordem, recebe o lote
  queues_.endRound()
```
(`pendingNames` na mensagem lista os tipos do lote que estourou — chame-a antes de qualquer limpeza). Mover `fixedDeltaTime`/`time`/`pause`/`resume`/`paused` de `SystemContext` para `EngineContext` (`context.cpp`). `declareEvent`, `on`/`onById` com `OLY_ASSERT(!inFrame_, ...)` e nome do tipo/handler na mensagem.

- [ ] **Step 5: Ver passar** — `$DC make test` (tudo, incluindo os testes da fase 2 e `oly.lint.boundaries`) e `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 6: Commit** `feat: eventos no app com handlers por lote e despacho em rodadas`.

---

### Task 2: Eventos no passo fixo — fluxo de referência, pausa, render, relógio e recuperação

**Files:**
- Create: `engine/runtime/app/tests/events_loop.cpp`
- Modify: `engine/runtime/app/src/engine.cpp`, `engine/runtime/app/include/oly/app/engine.hpp` (comentários das etapas vazias e de `frame`), `engine/runtime/app/tests/{CMakeLists.txt,recovery.cpp}`

**Interfaces:**
- Consumes: tudo da Task 1.
- Produces: comportamento descrito em "Decisões" para relógio nos handlers, emissão fora de passo e recuperação (filas limpas em `recoverFromInterruptedFrame`).

- [ ] **Step 1: Testes que falham** em `tests/events_loop.cpp` (registrar no `CMakeLists.txt`). O fluxo de referência usa só nomes locais do teste: `struct Health { float value; }; struct Enemy {}; struct Coin {}; struct EnemyDied { oly::ecs::Entity entity; float x, y; }; struct CoinDropped { oly::ecs::Entity coin; };` e `Position` do suporte.
```cpp
TEST_CASE("fluxo de referência: dano, moeda, pontos e destruição no fim do despacho") {
  // UpdateAfter "damage": Health -= 10 em todo Enemy; se <= 0 emite EnemyDied{entity, position.x, position.y} (não destrói)
  // on<EnemyDied>: "dropCoin" (spawn Position{x,y} + Coin, emite CoinDropped{coin}), "addScore" (CHECK(context.isAlive(entity));
  //   lê Position dele; ++score), "destroyDead" (destroy se isAlive), registrado por último; on<CoinDropped> "countCoin"
  // inimigo com Health{10} em (3, 4); 1 passo
  CHECK(!world.isAlive(enemy)); CHECK(score == 1); CHECK(moeda em (3,4) com Coin); CHECK(countCoin chamado na 2ª rodada); checkInvariants
}
TEST_CASE("evento repetido: duas mortes do mesmo inimigo no mesmo lote") { /* damage emite EnemyDied duas vezes */ CHECK(lote de 2); CHECK(destroyDead não dispara assert); CHECK(2 moedas); checkInvariants }
TEST_CASE("entidade destruída numa rodada anterior chega morta à rodada seguinte") { /* handler de A destrói e emite B{entity}; handler de B */ CHECK(!context.isAlive(entity)); }
TEST_CASE("handlers rodam em passo pausado") { /* sistema whilePaused emite Ping; pause(); 1 passo */ CHECK(handler chamado); CHECK(engine.time() == 0.0); }
TEST_CASE("evento emitido em Render é despachado no passo seguinte") { /* Render emite Ping no 1º frame */ CHECK(handler não chamado no 1º frame); /* 2º frame */ CHECK(handler chamado 1 vez, antes do render desse frame); }
TEST_CASE("evento emitido fora de frame vai para o próximo passo") { /* engine.emit(Ping{7}) antes do 1º frame; frame(0) só registra o tempo */ CHECK(não chamado); /* frame(1/60) */ CHECK(chamado com 7, uma vez); /* frame(2/60) */ CHECK(continua 1 vez); }
TEST_CASE("handler vê o time() do início do passo, igual aos sistemas") { /* sistema e handler gravam time() em 2 passos */ CHECK(times do handler == times do sistema == {0, 1/60}); }
```
`recovery.cpp` (só fora do Emscripten, como hoje) ganha:
```cpp
TEST_CASE("exceção num handler descarta os eventos pendentes no frame seguinte") {
  // on<Ping> "thrower": emite Echo e, na 1ª chamada, lança std::runtime_error; on<Echo> "echo" conta
  // passo com Ping → CHECK_THROWS; frame seguinte (só registra o tempo); passo sem emitir → CHECK(echo == 0)
  // passo com novo Ping → CHECK(echo == 1); checkInvariants
}
```

- [ ] **Step 2: Ver falhar** — `$DC make test T='^app\.'` → falham o relógio do handler (`time` adiantado um passo) e a recuperação (Echo antigo despachado).

- [ ] **Step 3: Implementar.** Em `runFixedStep`: zerar `eventRoundsThisStep_` no início e mover o `++steps_` para depois de `applyStateTransition()`. Em `recoverFromInterruptedFrame`: `queues_.clear()`. Atualizar o comentário de `frame` em `engine.hpp` (o frame interrompido também descarta eventos) e o das etapas vazias (só restam `captureActions`, `runDueSchedule`, `applyStateTransition`).

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: eventos no passo fixo com relógio do passo e recuperação de handler interrompido`.

---

### Task 3: `event` no esquema, no gerador e na verificação do WASM

**Files:**
- Modify: `engine/packages/core/src/{schema.ts,schema_hash.ts,runtime.ts,module.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/{schema.test.ts,surface.test.ts}`, `engine/tools/codegen/src/emit_cpp.ts`, `engine/tools/codegen/tests/generator.test.ts`, `engine/runtime/bridge/include/oly/bridge/{schema_registry.hpp,c_api.hpp}`, `engine/runtime/bridge/src/c_api.cpp`, `engine/runtime/bridge/tests/{test_schema_registry.cpp,c_api.cpp}`, `engine/testing/tests/wasm/load.test.ts`
- Create: `engine/runtime/bridge/tests/test_events.hpp`

**Interfaces:**
- Produces:
  - TS (`@oly/core`): `interface EventType<Name extends string = string, F extends Fields = Fields> { readonly kind: "event"; readonly name: Name; readonly fields: F; readonly layout: ComponentLayout; readonly schemaIndex: number }`; `function event<const Name extends string, const F extends Fields>(name: Name, fields: F): EventType<Name, F>` (mesmas validações de `component`); `Schema` ganha `readonly events: readonly EventType[]`; `defineSchema(types: readonly (SchemaType | EventType)[]): Schema`.
  - TS interno: `eventIndexOf(runtime: Runtime, type: EventType): number` (em `runtime.ts`, exportado por `internal.ts`; `ContractError` "tipo não pertence ao esquema deste runtime" como `componentIdOf`).
  - C++ (`oly/bridge/schema_registry.hpp`): `struct SchemaEvent { std::string_view name; uint32_t size; uint32_t alignment; void (*declare)(oly::app::Engine& engine); };` `std::span<const SchemaEvent> schemaEvents();` (definida pelo código gerado).
  - API C: `uint32_t olySchemaEventCount()`, `uint32_t olySchemaEventSize(uint32_t eventIndex)`, `uint32_t olySchemaEventAlignment(uint32_t eventIndex)` (índice sem verificação, como `olySchemaComponentId`).
  - Gerador: struct + `static_assert` de layout para cada evento (mesmo formato dos componentes) em `schema.generated.hpp`; em `schema_registry.generated.cpp`, `#include "oly/app/engine.hpp"` e `schemaEvents()` com uma entrada por evento: `{"Nome", tamanho, alinhamento, [](oly::app::Engine& engine) { engine.declareEvent<Nome>("Nome"); }}`.
  - `test_events.hpp` (bridge): `struct BridgeTestEvent { uint32_t value; float amount; };` e `struct BridgeOtherEvent { uint32_t value; };` (este último fora do registro de teste).

- [ ] **Step 1: Testes que falham.**
  - `schema.test.ts`: `event("Hit", {})` lança `ContractError`; `defineSchema([A, Hit, T, Boom])` dá `types` = `[A, T]` e `events` = `[Hit, Boom]` com `Hit.schemaIndex === 0`, `Boom.schemaIndex === 1`, `A.schemaIndex === 0`, `T.schemaIndex === 1`; nome repetido entre componente e evento lança `ContractError`; hash de um esquema sem eventos é igual a `fnv1a32` da forma canônica de hoje (valor literal calculado antes da mudança); acrescentar um evento muda o hash.
  - `generator.test.ts`: `struct Hit { uint32_t target; float amount; };` com `static_assert` de layout; registro contém `#include "oly/app/engine.hpp"` e `engine.declareEvent<Hit>("Hit")` antes de `declareEvent<Boom>("Boom")`.
  - `c_api.cpp`: `olySchemaEventCount() == 1`, `olySchemaEventSize(0) == sizeof(BridgeTestEvent)`, `olySchemaEventAlignment(0) == alignof(BridgeTestEvent)` (`test_schema_registry.cpp` passa a definir `schemaEvents()` com `BridgeTestEvent`).
  - `load.test.ts`: esquema TS com um evento a mais que o WASM → `SchemaMismatchError` cuja mensagem tem "eventos no WASM"; mesmo nº de eventos com tamanho diferente → `SchemaMismatchError` com o nome do evento (montar os esquemas como os casos de divergência já existentes no arquivo).
  - `surface.test.ts` / `public-exports.txt`: `event`, `EventType`.

- [ ] **Step 2: Ver falhar** — `$WC make ts` → erro de tipos (`event` não existe); `$DC make test T='^bridge\.'` → erro de compilação (`schemaEvents`).

- [ ] **Step 3: Implementar.** `canonicalSchema` recebe também os eventos e acrescenta `!Nome{campo:tipo,...};` por evento. `loadRuntime` confere `_olySchemaEventCount() === schema.events.length` e tamanho/alinhamento de cada evento; a mensagem de `SchemaMismatchError` ganha "eventos no WASM: n, no TS: m" e o primeiro divergente pode ser evento. `OlyModule` ganha os três exports.

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make games-test` (o fixture ainda não tem eventos: `schemaEvents()` gerado vazio).

- [ ] **Step 5: Commit** `feat: event no esquema, no gerador e na verificação do WASM`.

---

### Task 4: Eventos na API C — emissão, handlers nativos por nome e handlers de script

**Files:**
- Modify: `engine/runtime/bridge/include/oly/bridge/{engine_api.hpp,engine.hpp}`, `engine/runtime/bridge/src/{engine_api.cpp,handles.hpp}`, `engine/runtime/bridge/tests/{engine_api.cpp,contract_death.cpp,CMakeLists.txt}`

**Interfaces:**
- Consumes: Task 1 (`declareEvent`, `onById`, `emitById`, `eventTypeOf`, `eventTypeCount`, `HandlerContext::eventData/eventCount/handlerId`, `findNamedHandler`), Task 3 (`schemaEvents()`).
- Produces (API C, `extern "C"`, `OLY_API`):
  - `#define OLY_INVALID_HANDLER 0xFFFFFFFFu`, `#define OLY_HANDLER_TYPE_MISMATCH 0xFFFFFFFEu`, `#define OLY_SCRIPT_SYSTEM 0u`, `#define OLY_SCRIPT_HANDLER 1u`.
  - `OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame, uint32_t maxEventRounds)` (declara `schemaEvents()` na ordem).
  - `void olyEngineEmit(OlyEngine*, uint32_t eventType, const void* value)` (`eventType < eventTypeCount()`, `OLY_ASSERT` com `olyEngineEmit` e `eventType`; copia `size` bytes; vale dentro e fora de `frame`).
  - `uint32_t olyEngineAddNativeHandler(OlyEngine*, uint32_t eventType, const char* name)`, `uint32_t olyEngineAddScriptHandler(OlyEngine*, uint32_t eventType, const char* name)` (`eventType` fora do intervalo é `OLY_ASSERT`).
  - C++ (`engine.hpp`): `using ScriptInvoker = void (*)(OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount);`
  - `EM_JS(void, olyHostInvokeScript, (OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount), { Module["olyInvokeScript"](engine, kind, id, events, eventCount); })`.

- [ ] **Step 1: Testes que falham** em `tests/engine_api.cpp` (o invocador de teste passa a gravar `(engine, kind, id, eventCount)` e os `value` lidos de `events`; `OLY_HANDLER(bridgeTestNativeHandler, BridgeTestEvent)` grava `"n"` e os valores):
```cpp
TEST_CASE("eventos do esquema são declarados na criação, na ordem") { CHECK(engineOf(engine).eventTypeOf(eventKey<BridgeTestEvent>()) == 0); CHECK(engineOf(engine).eventTypeCount() == 1); }
TEST_CASE("handler de script recebe kind, id e o lote") { /* olyEngineAddScriptHandler; olyEngineEmit 2× fora de frame; 1 passo */ CHECK(kind == OLY_SCRIPT_HANDLER); CHECK(id == devolvido); CHECK(eventCount == 2); CHECK(values == {1, 2}); }
TEST_CASE("handlers de script e nativo por nome na ordem de registro") { /* script, nativo, script */ CHECK(gravado == {"s","n","s"}); }
TEST_CASE("sistema de script continua recebendo OLY_SCRIPT_SYSTEM") { CHECK(kind == OLY_SCRIPT_SYSTEM); CHECK(eventCount == 0); }
TEST_CASE("nome de handler desconhecido e handler de outro tipo") { CHECK(olyEngineAddNativeHandler(engine, 0, "naoExiste") == OLY_INVALID_HANDLER); /* OLY_HANDLER(bridgeTestOtherHandler, BridgeOtherEvent) */ CHECK(olyEngineAddNativeHandler(engine, 0, "bridgeTestOtherHandler") == OLY_HANDLER_TYPE_MISMATCH); }
TEST_CASE("olyEngineEmit dentro de um sistema de script é despachado no mesmo passo") { /* invocador do sistema chama olyEngineEmit */ CHECK(handler chamado no mesmo frame); }
```
Atualizar as chamadas existentes de `olyEngineCreate` (dois argumentos → três, `maxEventRounds = 16`). `contract_death.cpp`: caso `engine_emit_out_of_range` (`olyEngineEmit(engine, 5, &value)`; substrings `olyEngineEmit`, `eventType`).

- [ ] **Step 2: Ver falhar** — `$DC make test T='^bridge\.'` → erro de compilação.

- [ ] **Step 3: Implementar.** No construtor de `OlyEngine`, depois de criar a `engine`, chamar `schemaEvent.declare(engine)` para cada evento e `OLY_ASSERT` que o id devolvido é o índice. Handler de script: função do `bridge` registrada com `onById(type, name, runScriptHandler, engine)` que chama o invocador com `OLY_SCRIPT_HANDLER`, `context.handlerId()`, `context.eventData()`, `context.eventCount()`. `olyEngineAddNativeHandler` resolve `findNamedHandler(name)` e compara `engine.eventTypeOf(named->eventKey)` com `eventType`.

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: eventos na API C com handlers nativos por nome e de script por lote`.

---

### Task 5: Eventos no `@oly/core` — `on`, `addNativeHandler`, `HandlerContext`, `EventBatch` e `emit`

**Files:**
- Create: `engine/packages/core/src/{context.ts,handler_context.ts,event_batch.ts}`, `engine/testing/tests/wasm/events.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,system_context.ts,runtime.ts,bridge.ts,module.ts,values.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/{schema.ts,systems.cpp}`, `engine/testing/tests/wasm/fixture.ts`

**Interfaces:**
- Consumes: API C da Task 4; `EventType`, `eventIndexOf` da Task 3; `makeView`, `ReadView` de `fields.ts`.
- Produces (`@oly/core`):
  - `EngineConfig` ganha `maxEventRounds?: number` (inteiro positivo, padrão 16; senão `ContractError` em `createEngine`).
  - `Engine.on<F extends Fields>(type: EventType<string, F>, name: string, handler: HandlerFunction<F>): HandlerRegistration`; `Engine.addNativeHandler(type: EventType, name: string): HandlerRegistration` (`ContractError` com o nome para `OLY_INVALID_HANDLER` e, para `OLY_HANDLER_TYPE_MISMATCH`, com o nome do handler e do evento). Os dois: `ContractError` durante `frame`, com tipo de outro esquema ou com a `Engine` liberada.
  - `interface HandlerRegistration { readonly id: number }`; `type HandlerFunction<F extends Fields = Fields> = (context: HandlerContext, batch: EventBatch<F>) => void`.
  - `class EventBatch<F extends Fields = Fields>`: `readonly type: EventType`, `readonly count: number`, `read(): ReadView<F>` (`ContractError` fora da chamada do handler).
  - `class HandlerContext`: `readonly eventType: EventType`, `readonly name: string`, os membros comuns e as diretas `spawn`, `destroy`, `add`, `remove` (delegam ao `World`).
  - Comum aos dois contextos (base `EngineContext` em `context.ts`, exportada do arquivo mas não do `index.ts`): `fixedDeltaTime`, `time`, `paused`, `pause()`, `resume()`, `query`, `get`, `set`, `has`, `isAlive`, `emit<F extends Fields>(type: EventType<string, F>, value?: Partial<ValueOf<F>>): void` (campos ausentes = 0; campo desconhecido ou tipo de outro esquema = `ContractError`). `SystemContext` mantém `phase`, `name`, `interpolation` e as adiadas.
  - `values.ts`: `writeComponentValue` vira `writeValue(bridge, layout: ComponentLayout | null, value)` (null = tag); a área de rascunho da `Bridge` cobre o maior entre componentes e eventos.
  - `OlyModule`: `_olyEngineCreate(fixedHz, maxStepsPerFrame, maxEventRounds)`, `_olyEngineEmit(engine, eventType, value)`, `_olyEngineAddNativeHandler(engine, eventType, name)`, `_olyEngineAddScriptHandler(engine, eventType, name)`, `olyInvokeScript?: (engine: number, kind: number, id: number, events: number, eventCount: number) => void`.
  - `public-exports.txt`: + `EventBatch`, `HandlerContext`, `HandlerFunction`, `HandlerRegistration`.
- Fixture: `Ping = event("Ping", { target: "entity", value: "f32" })` e `Echo = event("Echo", { depth: "u32" })`, nessa ordem, no fim de `defineSchema`. Em `systems.cpp`: `OLY_HANDLER(appendNineOnPing, Ping)` (para cada evento, `trace(target).value = value * 10 + 9`), `OLY_HANDLER(sumPing, Ping)` (soma `value` num `double` estático), `OLY_SYSTEM(emitPings)` (emite `fixturePingCount` `Ping{NullEntity, 1}`), exports `fixtureSetPingCount(uint32_t)` e `double fixtureTakePingSum()` (devolve e zera).

- [ ] **Step 1: Testes que falham** em `engine/testing/tests/wasm/events.test.ts` (mesmos helpers de `engine.test.ts`: `withEngine`, `stepper`; `checkInvariants() === null` no fim de cada teste):
```ts
test("handlers TS e C++ de Ping na ordem de registro", ...)        // Trace{0}; Update emite Ping{target}; on h1 ×10+1, addNativeHandler appendNineOnPing, on h2 ×10+2; 1 passo → 192
test("tipos despachados na ordem do esquema, não na de emissão", ...) // emite Echo e depois Ping → ["Ping","Echo"]
test("lote: count e valores pelas views, inclusive entity", ...)      // 3 Pings com value 1,2,3 → count 3, read().value.get(i), read().target.get(0) === entity
test("evento emitido por handler TS vai para a rodada seguinte do mesmo passo", ...) // handler de Ping emite Echo{1} e Ping; lote atual continua count 1; Echo chega no mesmo step()
test("handler TS destrói direto; evento repetido não quebra", ...)   // 2 Pings do mesmo target; handler destrói se isAlive → isAlive false, sem erro
test("evento emitido em Render é despachado no passo seguinte", ...)
test("memória cresce dentro do handler TS e o lote continua legível", ...)
  // Ping{target: e, value: 42}; no handler: buffer = moduleOf(runtime).HEAPU8.buffer; context.spawn([Position, Velocity, Mixed, Trace])
  // até o buffer mudar (no máximo 1 000 000 vezes; assert que mudou); depois batch.read().value.get(0) === 42 e target.get(0) === e
test("exceção num handler TS chega a quem chamou frame e o frame seguinte descarta os eventos pendentes", ...)
  // handler de Ping emite Echo e lança só na 1ª vez; assert.throws(step, /boom/); engine.world.checkInvariants() === null
  // frame seguinte: 0 passos; o outro: Echo não despachado; novo Ping → Echo despachado
test("mudança estrutural direta no World dentro do handler é permitida; dentro do sistema continua proibida", ...)
test("emit com campo desconhecido, com evento de outro esquema ou fora da chamada é ContractError", ...)
test("addNativeHandler com nome desconhecido ou de outro tipo é ContractError com os nomes", ...) // ("naoExiste"); (Echo, "appendNineOnPing")
test("on durante frame é ContractError; batch.read() e context depois do handler são ContractError", ...)
test("evento sem handler é descartado no fim do passo", ...)
```
`surface.test.ts`: membros novos de `Engine` (`on`, `addNativeHandler`), `SystemContext` (`emit`), `HandlerContext` e `EventBatch` exatamente como em Produces.

- [ ] **Step 2: Ver falhar** — `$WC make games-test` → erro de tipos no `tsc` (`on` não existe).

- [ ] **Step 3: Implementar.** `Bridge`: `engineCreate(fixedHz, maxStepsPerFrame, maxEventRounds)`, `engineEmit(engine, eventType, valuePointer)`, `addNativeHandler`, `addScriptHandler` (os que alocam terminam em `sync()`). `Runtime` repassa `olyInvokeScript(engine, kind, id, events, eventCount)` para `invokeSystem` ou `invokeHandler` da `Engine`. `Engine#invokeHandler(id, address, count)`: `bridge.sync()`, cria o `EventBatch` (endereço, `count`, layout do tipo), destrava o `World`, ativa o `HandlerContext`, chama, e em `finally` invalida o lote, desativa o contexto e trava o `World` de novo.

- [ ] **Step 4: Ver passar** — `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (sem asserts: só as validações TS seguram os erros) e `$DC make test`.

- [ ] **Step 5: Commit** `feat: eventos no @oly/core com handlers TS por lote e emit nos contextos`.

---

### Task 6: Custo dos eventos (§7), resultados e documentação

**Files:**
- Create: `engine/testing/bench/events.ts`, `docs/benchmarks/bridge/events_phase3.json` (gerado)
- Modify: `docs/benchmarks/bridge/README.md`, `docs/benchmarks/README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: `common.ts` (`parseOptions`, `loadBenchRuntime`, `median`, `rotated`), `Engine.on/addNativeHandler/addSystem/addNativeSystem/frame`, fixture `emitPings`, `sumPing`, `fixtureSetPingCount`, `fixtureTakePingSum`.

- [ ] **Step 1: `events.ts`** — mesmo protocolo de `loop.ts` (aquecimento, rodadas intercaladas com ordem girando, mediana, um passo por `frame`), N ∈ {16, 256, 4096} eventos `Ping` por passo, em ns/evento:
  1. Handler: emissão por `emitPings` nativo e `none` (sem handler), `cpp` (`addNativeHandler(Ping, "sumPing")`) e `ts` (handler TS que soma `value` por `batch.read()`); razão `ts/cpp` e custo por lote `(ts − cpp)` em ns/passo. Conferir a soma (`fixtureTakePingSum()` ou a soma TS) = N × passos; divergência → exit 1.
  2. Emissão: `cpp` (`emitPings`) contra `ts` (sistema TS com N × `context.emit(Ping, { value: 1 })`), os dois com `sumPing`; razão `ts/cpp`.
  Saída JSON `{ meta, handler, emission }` com `meta` igual ao de `loop.ts`.

- [ ] **Step 2: Medir** — `$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'` e `$WC node engine/testing/dist/bench/events.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/events_phase3.json --commit $(git rev-parse --short HEAD)` → exit 0 e duas tabelas impressas.

- [ ] **Step 3: Registrar** — seção "Custo dos eventos — fase 3" em `docs/benchmarks/bridge/README.md` (comando, ambiente, protocolo, tabelas, leitura em poucas linhas: custo da chamada de volta por lote, custo por evento lido pelas views, e se vale emitir em lote a partir do TS) e uma linha em `docs/benchmarks/README.md`. Sem meta numérica (§7).

- [ ] **Step 4: `CLAUDE.md`** — na seção do `app`, um parágrafo curto sobre eventos (declaração na `Engine` = ordem de despacho, filas `pending`/`dispatching`, rodadas com limite `maxEventRounds`, handlers por lote com mudanças diretas, recuperação descarta eventos); na da ponte, a chamada de volta com `kind` e os eventos do esquema declarados na criação da `OlyEngine`; linhas do glossário para declarar evento, emitir, registrar handler e handler por nome; `event` na lista do `@oly/core`.

- [ ] **Step 5: Verificação final** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`: tudo verde.

- [ ] **Step 6: Commit** `bench: custo dos eventos da fase 3 e documentação`.
