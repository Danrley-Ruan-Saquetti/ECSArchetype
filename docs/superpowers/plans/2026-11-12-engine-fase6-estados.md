# Engine — Fase 6 (Estados) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Estados de jogo planos no núcleo C++ (`declareState`, pedido, troca no passo 5 com `onExit` → limpeza → `onEnter`, `inState` em sistemas e handlers, escopo de entidades e tarefas com `persist`), declarados no esquema e expostos na API C e no `@oly/core`.

**Architecture:** A `Engine` guarda os estados declarados, o estado atual, o pedido pendente e os ganchos de cada estado; a troca roda em `applyStateTransition()` (passo 5, pausado ou não) e termina com um novo despacho de eventos. O escopo é a tag interna `detail::StateScoped`: os contextos a põem no `spawn` quando há estado ativo, e a limpeza destrói as entidades com a tag por uma `QueryById` criada uma vez. As tarefas ganham um campo `scoped` no slot da `TaskQueue`, e a limpeza as cancela com descarte. O `bridge` declara os estados do esquema na criação, registra ganchos TS com a chamada de volta única (`OLY_SCRIPT_STATE_HOOK`) e expõe `olyDirectSpawn`/`olyContextPersist`, que escolhem o contexto atual. O `@oly/core` só valida uso e repassa.

**Tech Stack:** C++20, GCC 14 (nativo), Emscripten 3.1.74 + node 20.18 (serviço `wasm`), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (fase 6 da §5; §4.7 inteira, §4.3 para a ordem do passo e a pausa, §4.4 para eventos emitidos em ganchos, §4.6 para o escopo e o descarte de tarefas, §6, §7 "Estados"). `§N` abaixo são desse spec.

## Global Constraints

- Branch `feat/engine-fase6-estados` a partir de `develop`. Commits, comentários, mensagens de assert e de erro em pt-BR; commits terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain só em Docker: `DC="docker compose run --rm dev"` (nativo, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). Presets `wasm-games*` exigem `make ts` antes.
- `-Wall -Wextra -Wpedantic -Werror` em todo alvo C++; TS `strict` (§3).
- Camadas: `core ← input ← app ← bridge ← jogos` e `core ← ecs ← app`. Nenhum módulo novo; o ECS e o `input` não mudam. A tag fica em `engine/runtime/app/include/oly/app/detail/` (§4.7); o `bridge` não a inclui.
- Engine completa em C++ (§2): pedido, troca, ordem dos ganchos, escopo, limpeza, `inState` e recuperação vivem no `app`. O TS só valida uso, guarda closures e repassa.
- Fronteira: uma chamada por gancho TS por troca; `requestState`, `inState`, `persist` e registro cruzam uma vez por chamada. Nada por entidade.
- `StateId` denso na ordem de declaração; `NoState = 0xFFFFFFFF`; no máximo 32 estados (máscara `uint32_t`) (§4.7).
- Nada de conceito de jogo em `engine/runtime` nem em `engine/packages`; nomes de jogo só em `engine/testing/fixture/` e nos testes.
- Sem benchmark nesta fase (§7).
- Glossário (`CLAUDE.md`): mesmo verbo nas três camadas — `requestState`, `state`, `onEnter`/`onExit`, `persist`, `inState` (C++ `setSystemStates`/`setHandlerStates`). `oly` só na API C.
- Estilo `CLAUDE.md` e skills `linhas-em-branco`/`comentarios-de-codigo`: chaves em todo bloco, nomes completos, dados no topo da classe, um `namespace` por arquivo, includes do projeto com aspas depois da biblioteca padrão. Não formatar à mão (hook `clang-format`). Comentário só para razão, restrição externa ou contrato não óbvio; nada de narração da edição; testes em Arrange/Act/Assert.

### Decisões que o spec deixa em aberto (fixadas aqui)

- **Arquivos.** `oly/app/state.hpp`: tipos sem contexto (`StateId`, `StateMask`, `StateHookId`, `NoState`, `MaxStates`, `StateHookFunction`, registro por nome e `OLY_STATE_HOOK`). `oly/app/state_context.hpp`: `StateContext`. `oly/app/detail/state_scoped.hpp`: `struct StateScoped {};`. Os métodos de estado da `Engine` ficam em `src/engine_states.cpp`, para o `engine.cpp` continuar sendo o loop. A separação repete `task.hpp`/`task_context.hpp`.
- **Pedido pendente.** `requestedState_ == NoState` significa "sem pedido": pedir `NoState` é erro de contrato (fora do intervalo), então não há ambiguidade.
- **`inState` vale em todas as fases, inclusive `render`.** Só o `whilePaused` é restrito ao gameplay. Teste: `runsInState(mask) = mask == 0 || (state_ != NoState && (mask >> state_) & 1)`.
- **Máscara válida.** `stateCount() == 32 || (mask >> stateCount()) == 0` (deslocar por 32 é comportamento indefinido). `setSystemStates(system, {ids...})` confere cada id e monta a máscara; `setSystemStateMask` confere a máscara. O mesmo para handlers, que ganham `handlerLocations_` (`[HandlerId] → (tipo, posição)`, como `systemLocations_`).
- **Spawn com escopo.** `EngineContext` ganha `protected: bool spawnsScoped() const noexcept` (em `context.cpp`: `engine_->state() != NoState`). `DirectContext::spawn(Ts&&...)` chama `world_->spawn(components..., detail::StateScoped{})` ou `world_->spawn(components...)`, uma transição só. `SystemContext::spawn()` sai do header para `system.cpp` (`commands_->spawn()` + `commands_->add(entity, detail::StateScoped{})`); o template `spawn(Ts&&...)` chama `spawn()` e depois os `add`, e o flush faz uma transição só (R-CB-3).
- **`persist`.** `SystemContext::persist(entity)` = `commands_->remove<detail::StateScoped>(entity)` (mesmo contrato do `remove` adiado); `DirectContext::persist(entity)` = `world_->remove<detail::StateScoped>(entity)` (entidade morta dispara o assert do `remove`, que nomeia `StateScoped`). Os dois ficam em `.cpp`, então `system.hpp` não inclui `detail/`.
- **Limpeza.** `QueryById* scopedQuery_` criada na primeira limpeza com `QueryByIdSpec{.with = {componentId<detail::StateScoped>()}}` (criá-la no construtor mudaria a contagem de queries de testes existentes); `eachBlock` junta `block.entities()` em `scopedEntities_` (vetor reaproveitado, `clear()` antes); destrói depois da iteração. Em seguida `scheduler_.queue_.cancelScoped()`.
- **Tarefas.** `TaskQueue::push(..., const TaskPayload& payload, bool scoped = false)`: o padrão mantém os testes da fase 5. `Scheduler::schedule` passa `engine_.state() != NoState`. `TaskQueue::persist(task)` zera `scoped` e devolve `false` para tarefa não pendente ou `TaskHandle{}`. `cancelScoped()` repete "cancelar todo slot vivo com escopo" até uma passada não cancelar nada, como `discardAll`.
- **Limitação documentada.** Duas `Engine`s sobre o mesmo `World` dividem a tag: a troca de uma limpa as entidades com escopo da outra. Fica no comentário do construtor da `Engine`; a ponte cria um `World` por `OlyEngine`.
- **`StateContext::entering()`.** Verdadeiro no `onEnter`. A ponte usa para saber o lado da troca, e um gancho nativo registrado nos dois momentos também.
- **API C, `OLY_SCRIPT_STATE_HOOK`.** `olyHostInvokeScript(engine, 4, hookId, NULL, outro)`, onde `outro` é o estado do outro lado da troca (`fromState` num `onEnter`, `toState` num `onExit`); o lado do gancho é o `olyEngineState` no momento da chamada. O §4.7 escreve `eventCount` 0; o §8 deixa o detalhe da API C com o construtor, e assim o TS monta `fromState`/`toState` sem export novo.
- **Contexto atual na ponte.** `OlyEngine` ganha `oly::app::DirectContext* directContext = nullptr` ao lado do `currentContext` (sistema). `runScriptHandler`, `runScriptTask` e `runScriptStateHook` salvam o anterior, apontam para o seu contexto e restauram; `olyEngineFrame` zera os dois (exceção JS). `olyContextPersist` usa o direto, senão o de sistema, senão `OLY_ASSERT`.
- **TS.** O handle se chama `State` (§4.7) e o nome segue o padrão das ações (camelCase, fora das palavras reservadas do C++). Forma canônica do hash ganha `#nome;` por estado, depois das ações. Mais de 32 estados é `ContractError` em `defineSchema` (o gerador recebe o esquema pronto). `onEnter`/`onExit`/`addNativeOnEnter`/`addNativeOnExit` devolvem `void`. `persist` fica em `SystemContext` (sem conferir vida, como `remove`) e em `DirectContext` (`ContractError` para entidade morta). A máscara sai de `stateMaskOf(indices)` com `>>> 0`, porque `1 << 31` é negativo em JS.
- **Spawn direto do TS.** `world.ts` ganha a função interna `spawnWith(world, types, values, create: () => Entity)`, com as validações de hoje; `World.spawn` passa `() => bridge.spawn(handle)` e `DirectContext.spawn` passa `() => bridge.directSpawn(engineHandle)`.
- **Fora do escopo:** estados aninhados ou pilha, dar escopo a algo persistente, benchmark.

## Review Focus

1. **Evento emitido em `onExit` citando uma entidade com escopo**: é despachado no item 6, depois da limpeza; o handler vê `isAlive` falso e nada dispara assert (Task 3).
2. **O que `onExit` cria (entidade e tarefa) é limpo na mesma troca; o que `onEnter` cria fica com o escopo novo** e só sai na troca seguinte (Task 3).
3. **Sistema que cria uma entidade e chama `persist` nela no passo da troca**: o `add` e o `remove` da tag se anulam no flush e a entidade sobrevive à limpeza do passo 5 (Task 3).
4. **`inState` com o estado de índice 31 pelo TS**: a máscara é `0x80000000`, não um número negativo que a API C leria errado (Task 6).
5. **Pedido de troca com a aba oculta**: nenhum passo roda; ao voltar, o primeiro `frame` só registra o tempo e a troca acontece no passo 5 do passo seguinte (Task 2).

---

### Task 1: Escopo na `TaskQueue`

**Files:**
- Modify: `engine/runtime/app/include/oly/app/detail/task_queue.hpp`, `engine/runtime/app/src/task_queue.cpp`, `engine/runtime/app/tests/task_queue.cpp`

**Interfaces:**
- Produces (`oly::app::detail::TaskQueue`): `Slot` ganha `bool scoped = false`; `TaskHandle push(double due, double interval, TaskFunction function, TaskDiscardFunction discard, const TaskPayload& payload, bool scoped = false)`; `bool persist(TaskHandle task)` (contrato de slot de `matches`, operação `"persist"`); `void cancelScoped()` (descarte uma vez por tarefa, inclusive as com escopo agendadas por descartes).

- [ ] **Step 1: Branch** `git switch -c feat/engine-fase6-estados`.

- [ ] **Step 2: Testes que falham** em `tests/task_queue.cpp` (mesmo contador de descartes da fase 5):
```cpp
TEST_CASE("push sem escopo por padrão: cancelScoped não mexe") { /* push sem scoped; cancelScoped */ CHECK(queue.isPending(handle)); CHECK(descartes == 0); }
TEST_CASE("cancelScoped cancela só as com escopo, com descarte uma vez") { /* 2 com escopo, 1 sem */ CHECK(descartes == 2); CHECK(queue.pendingCount() == 1); CHECK(queue.isPending(semEscopo)); }
TEST_CASE("cancelScoped repete para tarefas com escopo agendadas por descartes") { /* o descarte da 1ª faz push com escopo de outra com descarte */ CHECK(descartes == 2); CHECK(queue.pendingCount() == 0); }
TEST_CASE("persist tira o escopo") { /* push com escopo; persist; cancelScoped */ CHECK(queue.persist(handle)); CHECK(queue.isPending(handle)); }
TEST_CASE("persist de tarefa não pendente ou de TaskHandle{} devolve false") { CHECK(!queue.persist(cancelada)); CHECK(!queue.persist(TaskHandle{})); }
TEST_CASE("cancelScoped não pega entradas antigas de um slot reusado sem escopo") { /* push com escopo A; cancel A; push sem escopo B no mesmo slot; cancelScoped */ CHECK(queue.isPending(b)); }
```

- [ ] **Step 3: Ver falhar** — `$DC make test T='app.task_queue'` → erro de compilação (`persist`/`cancelScoped` não existem).

- [ ] **Step 4: Implementar.** `push` grava `slot.scoped`; `release` não precisa zerar (o `push` seguinte sobrescreve). `cancelScoped` usa `cancel(TaskHandle{index, generation})` para que o descarte rode depois de liberar o slot.

- [ ] **Step 5: Ver passar** — `$DC make test T='^app\.'` e `$WC make test PRESET=wasm-debug T='app.task_queue'`.

- [ ] **Step 6: Commit** `feat: escopo de tarefas na fila do app com persist e cancelamento`.

---

### Task 2: Estados na `Engine` — declaração, pedido, troca, ganchos e `inState`

**Files:**
- Create: `engine/runtime/app/include/oly/app/{state.hpp,state_context.hpp}`, `engine/runtime/app/src/{state.cpp,engine_states.cpp}`, `engine/runtime/app/tests/states.cpp`
- Modify: `engine/runtime/app/include/oly/app/{engine.hpp,context.hpp}`, `engine/runtime/app/src/{engine.cpp,context.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp,recovery.cpp}`

**Interfaces:**
- Produces (`oly::app`, `state.hpp`): `using StateId = uint32_t; using StateMask = uint32_t; using StateHookId = uint32_t;` `inline constexpr StateId NoState = 0xFFFFFFFFu;` `inline constexpr uint32_t MaxStates = 32;` `class StateContext;` `using StateHookFunction = void (*)(StateContext& context);` `StateHookFunction findNamedStateHook(std::string_view name) noexcept;` (`nullptr` se não existe) `struct NamedStateHookRegistration { NamedStateHookRegistration(std::string_view name, StateHookFunction function); };` e `OLY_STATE_HOOK(name)`, que define `void name(oly::app::StateContext& context)` e o registra, como `OLY_SYSTEM`.
- Produces (`state_context.hpp`): `class StateContext : public DirectContext` com construtor privado (`friend class Engine`) `(Engine&, ecs::World&, StateHookId hookId, std::string_view name, void* userData, StateId fromState, StateId toState, bool entering)` e `hookId()`, `name()`, `userData()`, `fromState()`, `toState()`, `entering()`, todos `const noexcept`.
- Produces (`Engine`): `StateId declareState(std::string_view name)`, `uint32_t stateCount() const noexcept`, `void requestState(StateId state)`, `StateId state() const noexcept`, `StateHookId onEnter(StateId state, std::string_view name, StateHookFunction function, void* userData = nullptr)`, `StateHookId onExit(...)` (mesma assinatura; `StateHookId` conta entrada e saída juntos), `void setSystemStates(SystemId system, std::initializer_list<StateId> states)`, `void setSystemStateMask(SystemId system, StateMask mask)`, `void setHandlerStates(HandlerId handler, std::initializer_list<StateId> states)`, `void setHandlerStateMask(HandlerId handler, StateMask mask)`. `SystemEntry` e `HandlerEntry` ganham `StateMask states = 0`. `applyStateTransition()` sai do header (vazia) para `engine_states.cpp`.
- Produces (`EngineContext`): `void requestState(StateId state)`, `StateId state() const noexcept`.

Algoritmo de `applyStateTransition()` (§4.7, "Transição"):
```
se requestedState_ == NoState: nada
target = requestedState_; requestedState_ = NoState       // pedido feito em gancho vale no passo seguinte
from = state_
roda onExit de from (se from != NoState), na ordem de registro, com StateContext(from, target, entering=false)
limpeza do escopo (Task 3; nesta task, um ponto vazio)
state_ = target
roda onEnter de target, na ordem de registro, com StateContext(from, target, entering=true)
dispatchEvents()                                          // mesmas rodadas do passo, mesmo limite
```
`recoverFromInterruptedFrame` faz `requestedState_ = NoState`. Em `runPhase` e `dispatchEvents`, sistema ou handler com `!runsInState(entry.states)` não roda.

Contratos (`OLY_ASSERT`):

| Operação | Mensagem |
|---|---|
| `declareState` durante frame / nome repetido / 33º estado | `"declareState: estado \"%s\" declarado durante frame"`, `"declareState: estado \"%s\" já declarado"`, `"declareState: estado \"%s\" passa do máximo de 32"` |
| `requestState`, `onEnter`, `onExit`, `setSystemStates`, `setHandlerStates` com id fora | `"%s: state=%u fora do intervalo (%u estados)"` |
| `onEnter`/`onExit` durante frame | `"%s: gancho \"%s\" registrado durante frame"` |
| `set*States`/`set*StateMask` durante frame | `"%s: restrição de estados durante frame"` |
| máscara com bit além de `stateCount()` | `"%s: máscara 0x%x com estado não declarado (%u estados)"`, com `%s` = `setSystemStates`/`setHandlerStates` também nas variantes `*StateMask` |

- [ ] **Step 1: Testes que falham** em `tests/states.cpp` (`oly_add_test(MODULE app NAME states SOURCES states.cpp SUPPORT engine/runtime/ecs/tests/support)`). Fixture como `schedule.cpp`: `frame(0)` só registra; `step()` avança 1/60. Ganchos gravam `"exit:<nome>"`/`"enter:<nome>"` com `fromState`/`toState` num `Log` apontado por `userData`; sistemas usam `recordName`. `CHECK_INVARIANTS(world)` no fim de cada caso.
```cpp
TEST_CASE("estado inicial entra no passo 5 do primeiro passo; sistemas inState só rodam a partir do seguinte") { /* requestState(menu) antes de frame; sistema inState(menu); 1 passo */ CHECK(log.calls == {"enter:menu"}); CHECK(from == NoState); /* +1 passo */ CHECK(log.count("menuSystem") == 1); }
TEST_CASE("state() só muda no passo 5") { /* em playing; sistema pede gameOver e grava state() */ CHECK(estadoVistoNoSistema == playing); CHECK(engine.state() == gameOver); }
TEST_CASE("onExit antes de onEnter, cada um na ordem de registro, com fromState e toState") { CHECK(log.calls == {"exit:a1", "exit:a2", "enter:b1", "enter:b2"}); }
TEST_CASE("dois pedidos no mesmo passo: vale o último") { }
TEST_CASE("reentrada roda onExit e onEnter do mesmo estado") { CHECK(log.calls == {"exit:playing", "enter:playing"}); }
TEST_CASE("pedido feito num gancho vale no passo seguinte") { /* onEnter(b) pede c */ CHECK(engine.state() == b); /* +1 passo */ CHECK(engine.state() == c); }
TEST_CASE("troca em passo pausado acontece e não mexe na pausa") { CHECK(engine.state() == b); CHECK(engine.paused()); }
TEST_CASE("inState combina com whilePaused") { /* sistema whilePaused + inState(menu): roda só pausado e em menu */ }
TEST_CASE("inState vale na fase render") { }
TEST_CASE("lista vazia tira a restrição") { }
TEST_CASE("handler com inState: confere o estado em cada rodada e no despacho do item 6 vale o novo") { /* onEnter(playing) emite Ping; handler inState(playing) */ CHECK(handlerTimes == {tempoDoPasso}); }
TEST_CASE("evento emitido em gancho é despachado no mesmo passo e conta no limite de rodadas") { /* maxEventRounds=2; 1 rodada no passo 4 + 1 no item 6 */ CHECK(log.count("handler") == 2); }
TEST_CASE("pedido com a aba oculta espera o primeiro passo depois de voltar") { /* setHidden(true); requestState; frames; setHidden(false); frame só registra */ CHECK(engine.state() == NoState); /* 1 passo */ CHECK(engine.state() == menu); }
TEST_CASE("requestState e state pelos contextos de sistema, handler, tarefa e gancho") { }
TEST_CASE("OLY_STATE_HOOK registra por nome") { CHECK(findNamedStateHook("namedEnter") == &namedEnter); CHECK(findNamedStateHook("nenhum") == nullptr); }
```
`recovery.cpp` (só nativo): `"a recuperação descarta o pedido pendente"` (sistema pede `b` e lança; `frame` seguinte; passos; `state()` continua `a`); `"exceção em onExit: o estado não muda"`; `"exceção em onEnter: o estado já é o novo e os ganchos seguintes não rodam"`.

`contract_death.cpp` + `CASES`:

| Caso | Ação | Substrings |
|---|---|---|
| `declare_state_during_frame` | sistema chama `declareState` | `declareState`, `durante frame` |
| `declare_state_twice` | `declareState("a")` duas vezes | `declareState`, `já declarado` |
| `too_many_states` | 33 `declareState` | `declareState`, `máximo` |
| `request_state_out_of_range` | `requestState(5)` com 2 estados | `requestState`, `fora do intervalo` |
| `on_enter_during_frame` | sistema chama `onEnter` | `onEnter`, `durante frame` |
| `system_states_during_frame` | sistema chama `setSystemStates` | `setSystemStates`, `durante frame` |
| `system_state_mask_undeclared` | `setSystemStateMask(id, 0b100)` com 2 estados | `setSystemStates`, `não declarado` |

- [ ] **Step 2: Ver falhar** — `$DC make test T='^app\.'` → erro de compilação (`oly/app/state_context.hpp` não existe).

- [ ] **Step 3: Implementar.** Dados no bloco de dados da `Engine`, depois de `scheduler_`: `struct StateHookEntry { StateHookFunction function; void* userData; std::string name; StateHookId id; };`, `struct StateEntry { std::string name; std::vector<StateHookEntry> enterHooks; std::vector<StateHookEntry> exitHooks; };`, `std::vector<StateEntry> states_`, `StateHookId stateHookCount_ = 0`, `StateId state_ = NoState`, `StateId requestedState_ = NoState`. Registro por nome em `src/state.cpp`, como `src/system.cpp`. Atualizar os comentários que ficaram falsos: o das etapas vazias em `engine.hpp` (não sobra nenhuma) e o de `frame` sobre recuperação (o pedido pendente também é descartado). Docs públicos de `requestState` (vale dentro e fora de `frame`, último vence, troca no passo 5), `onEnter`/`onExit` (ordem, o que podem fazer) e `set*States` (lista vazia tira a restrição; vale em todas as fases).

- [ ] **Step 4: Ver passar** — `$DC make test` (inclui `oly.lint.boundaries`) e `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: estados na Engine com troca no passo 5, ganchos e inState`.

---

### Task 3: Escopo de entidades e tarefas, `persist` e limpeza

**Files:**
- Create: `engine/runtime/app/include/oly/app/detail/state_scoped.hpp`, `engine/runtime/app/tests/state_scope.cpp`
- Modify: `engine/runtime/app/include/oly/app/{engine.hpp,context.hpp,system.hpp,scheduler.hpp}`, `engine/runtime/app/src/{engine_states.cpp,context.cpp,system.cpp,scheduler.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp,recovery.cpp}`

**Interfaces:**
- Consumes: Task 1 (`push(..., scoped)`, `persist`, `cancelScoped`), Task 2 (`state()`, `applyStateTransition`).
- Produces: `oly::app::detail::StateScoped`; `void SystemContext::persist(ecs::Entity entity)`; `void DirectContext::persist(ecs::Entity entity)`; `bool Scheduler::persist(TaskHandle task)` (`false` se não está pendente); `DirectContext::spawn()`/`spawn(Ts&&...)` e `SystemContext::spawn()`/`spawn(Ts&&...)` com escopo conforme "Decisões". Na `Engine`: `ecs::QueryById* scopedQuery_ = nullptr`, `std::vector<ecs::Entity> scopedEntities_`, `void clearStateScope()` chamada no ponto da limpeza de `applyStateTransition`. Docs do construtor da `Engine` com a limitação de duas `Engine`s num `World`.

- [ ] **Step 1: Testes que falham** em `tests/state_scope.cpp` (`oly_add_test(MODULE app NAME state_scope SOURCES state_scope.cpp SUPPORT engine/runtime/ecs/tests/support)`; o teste do próprio módulo pode incluir `oly/app/detail/state_scoped.hpp`). `CHECK_INVARIANTS(world)` no fim de cada caso.
```cpp
TEST_CASE("entidades criadas por sistema, handler, tarefa e gancho com estado ativo são destruídas na troca") { CHECK(!world.isAlive(doSistema)); CHECK(!world.isAlive(doHandler)); CHECK(!world.isAlive(daTarefa)); CHECK(!world.isAlive(doGancho)); }
TEST_CASE("spawn direto no World é persistente") { CHECK(world.isAlive(setup)); }
TEST_CASE("sem estado ativo nenhuma entidade recebe a tag") { CHECK(!world.has<StateScoped>(entidade)); }
TEST_CASE("spawn com componentes num contexto direto entra com a tag numa transição só") { CHECK(world.has<StateScoped>(entidade)); CHECK(world.has<Position>(entidade)); }
TEST_CASE("o que onExit cria é limpo na mesma troca; o que onEnter cria fica até a troca seguinte") { /* entidade e tarefa de cada gancho */ CHECK(!world.isAlive(doExit)); CHECK(world.isAlive(doEnter)); CHECK(descartesDoExit == 1); }
TEST_CASE("persist num sistema no passo da troca: add e remove da tag se anulam e a entidade sobrevive") { CHECK(world.isAlive(entidade)); CHECK(!world.has<StateScoped>(entidade)); }
TEST_CASE("persist num contexto direto tira a tag; sem a tag não faz nada") { }
TEST_CASE("tarefas com escopo são canceladas com descarte uma vez; persist mantém; não pendente devolve false") { CHECK(descartes == 1); CHECK(engine.schedule().isPending(persistida)); CHECK(!engine.schedule().persist(terminada)); }
TEST_CASE("tarefa agendada fora de frame com estado ativo tem escopo") { }
TEST_CASE("descarte que agenda durante a limpeza também é cancelado") { CHECK(descartes == 2); CHECK(engine.schedule().pendingCount() == 0); }
TEST_CASE("evento emitido em onExit sobre entidade com escopo chega com a entidade já destruída") { CHECK(handlerViuMorta); }
TEST_CASE("playing → gameOver → playing sem sobras no World") { /* onEnter(playing) cria 3 entidades e um every; troca 3 vezes */ CHECK(entidadesVivas == 3 + persistentes); CHECK(engine.schedule().pendingCount() == 1); }
```
`recovery.cpp`: `"exceção em onExit não limpa o escopo"` (entidade com escopo continua viva depois da recuperação).

`contract_death.cpp` + `CASES`: `persist_dead_entity` (tarefa chama `context.persist` numa entidade destruída; substrings `remove`, `StateScoped`).

- [ ] **Step 2: Ver falhar** — `$DC make test T='^app\.'` → erro de compilação (`persist` e `detail/state_scoped.hpp` não existem).

- [ ] **Step 3: Implementar** conforme "Decisões" (spawn com escopo, `persist`, `Scheduler::schedule` com `scoped`, limpeza com `QueryById` e vetor reaproveitado, depois `cancelScoped`). O comentário do `Scheduler` ganha o escopo e o `persist`.

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: escopo de entidades e tarefas por estado com persist e limpeza na troca`.

---

### Task 4: Estados no esquema, no gerador e na criação da `OlyEngine`

**Files:**
- Modify: `engine/packages/core/src/{schema.ts,schema_hash.ts,runtime.ts,module.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/{schema.test.ts,surface.test.ts}`, `engine/tools/codegen/src/emit_cpp.ts`, `engine/tools/codegen/tests/generator.test.ts`, `engine/runtime/bridge/include/oly/bridge/{schema_registry.hpp,c_api.hpp}`, `engine/runtime/bridge/src/{c_api.cpp,engine_api.cpp}`, `engine/runtime/bridge/tests/{test_schema_registry.cpp,c_api.cpp,engine_api.cpp}`

**Interfaces:**
- Consumes: Task 2 (`Engine::declareState`, `stateCount`, `StateId`).
- Produces (`@oly/core`): `interface State<Name extends string = string> { readonly kind: "state"; readonly name: Name; readonly schemaIndex: number }`; `state<const Name extends string>(name: Name): State<Name>` (`ContractError` para nome fora de `/^[a-z][A-Za-z0-9]*$/` ou palavra reservada do C++); `Schema.states: readonly State[]`; `defineSchema` aceita `State`, dá `schemaIndex` na ordem e lança `ContractError` `"defineSchema: 33 estados; o máximo é 32"`; `canonicalSchema`/`schemaHash` ganham `states` (`#nome;`). Exports públicos: `state`, `State`.
- Produces (`Runtime`, interno): `stateIndexOf(runtime, state: State): number` (`ContractError` `"state=<nome>: estado não pertence ao esquema deste runtime"`), `stateAt(runtime, index: number): State | null` (`null` para `0xFFFFFFFF`). `loadRuntime` compara `_olySchemaStateCount()` com `schema.states.length` e a mensagem do `SchemaMismatchError` cita os estados.
- Produces (codegen): header com `#include "oly/app/state.hpp"` e `namespace states { inline constexpr oly::app::StateId menu{0}; }` quando há estados; registro com `schemaStates()`.
- Produces (`bridge`): `struct SchemaState { std::string_view name; };` `std::span<const SchemaState> schemaStates();` (na ordem do esquema; o índice é o `StateId`); `OLY_API uint32_t olySchemaStateCount();`; `olyEngineCreate` declara os estados depois das ações, com o mesmo assert de ordem.

- [ ] **Step 1: Testes que falham.**
  - `schema.test.ts`: `state("menu")` com `schemaIndex` na ordem entre os estados; nome repetido com componente ou ação é `ContractError`; `state("Menu")` e `state("class")` são `ContractError`; 33 estados é `ContractError` com "máximo"; o hash muda quando um estado é acrescentado ou renomeado.
  - `generator.test.ts`: header com `inline constexpr oly::app::StateId menu{0};` e `playing{1}` e o include de `oly/app/state.hpp`; header sem estados não inclui `oly/app/state`; registro com `{"menu"}` antes de `{"playing"}`; esquema sem estados gera `schemaStates() {\s*return {};`.
  - `surface.test.ts`: exports públicos com `state` e `State`.
  - `test_schema_registry.cpp` ganha `schemaStates()` com `"bridgeMenu"`, `"bridgePlaying"`; `c_api.cpp`: `CHECK(olySchemaStateCount() == 2)`; `engine_api.cpp`: `TEST_CASE("olyEngineCreate declara os estados do esquema na ordem")` → `CHECK(engineOf(engine).stateCount() == 2)`.

- [ ] **Step 2: Ver falhar** — `$DC make test T='^bridge\.'` → erro de compilação (`schemaStates` não existe); `$WC make games-test` → erro de tipos (`state` não existe).

- [ ] **Step 3: Implementar.** No codegen, `emitStates` ao lado de `emitActions` e `emitStatesRegistry` ao lado de `emitActionsRegistry`. Atualizar o comentário da forma canônica em `schema_hash.ts` e o do `olyEngineCreate` em `engine_api.hpp` (declara eventos, ações e estados).

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make games-test`.

- [ ] **Step 5: Commit** `feat: estados no esquema, no gerador e na criação da OlyEngine`.

---

### Task 5: API C de estados

**Files:**
- Modify: `engine/runtime/bridge/include/oly/bridge/{engine_api.hpp,engine.hpp}`, `engine/runtime/bridge/src/{engine_api.cpp,handles.hpp}`, `engine/runtime/bridge/tests/{engine_api.cpp,contract_death.cpp,CMakeLists.txt}`

**Interfaces:**
- Consumes: Task 2 (requestState, ganchos, máscaras, `StateContext::entering`), Task 3 (`DirectContext::spawn`/`persist`, `SystemContext::persist`, `Scheduler::persist`), Task 4 (estados na criação).
- Produces (API C, `extern "C"`, `OLY_API`): `OLY_NO_STATE`, `OLY_INVALID_STATE_HOOK`, `OLY_SCRIPT_STATE_HOOK 4u`, `OLY_STATE_ENTER 0u`, `OLY_STATE_EXIT 1u` e as funções do §4.7 com as assinaturas de lá (`olyEngineRequestState`, `olyEngineState`, `olyEngineSetSystemStates`, `olyEngineSetHandlerStates`, `olyEngineAddNativeStateHook`, `olyEngineAddScriptStateHook`, `olyDirectSpawn`, `olyContextPersist`, `olyEnginePersistTask`). `OlyEngine` ganha `directContext` ("Decisões"). No `bridge` (anônimo): `runScriptStateHook(StateContext&)` → `g_invoker(engine, OLY_SCRIPT_STATE_HOOK, context.hookId(), nullptr, context.entering() ? context.fromState() : context.toState())`. O comentário do `ScriptInvoker` em `engine.hpp` ganha o quinto `kind`.
- Contratos novos: `when` fora de `{0, 1}` (`"%s: when=%u inválido"`); `olyDirectSpawn` fora de handler, tarefa ou gancho de script (`"olyDirectSpawn: chamado fora de handler, tarefa ou gancho"`); `olyContextPersist` sem contexto (`"olyContextPersist: chamado fora de sistema, handler, tarefa ou gancho"`). Os demais vêm do `app`.

- [ ] **Step 1: Testes que falham** em `tests/engine_api.cpp` (o `recordingInvoker` grava `kind`, `id` e `eventCount`; o teste define `OLY_STATE_HOOK(bridgeNamedEnter)`):
```cpp
TEST_CASE("olyEngineState começa em OLY_NO_STATE e muda no passo 5") { }
TEST_CASE("gancho de script: OLY_SCRIPT_STATE_HOOK com o hookId e o outro estado") { /* onEnter(playing) e onExit(menu); menu → playing */ CHECK(g_kinds == {OLY_SCRIPT_STATE_HOOK, OLY_SCRIPT_STATE_HOOK}); CHECK(g_eventCounts == {playing, menu}); }
TEST_CASE("gancho nativo por nome e OLY_INVALID_STATE_HOOK para nome desconhecido") { }
TEST_CASE("máscara de estados em sistema e handler; 0 tira a restrição") { }
TEST_CASE("olyDirectSpawn num handler de script tem escopo e some na troca") { /* o invocador chama olyDirectSpawn quando kind == OLY_SCRIPT_HANDLER */ CHECK(!olyEntityIsAlive(world, criada)); }
TEST_CASE("olyContextPersist em sistema de script (adiado) e em handler de script (direto)") { CHECK(olyEntityIsAlive(world, persistidaNoSistema)); CHECK(olyEntityIsAlive(world, persistidaNoHandler)); }
TEST_CASE("olyEnginePersistTask mantém a tarefa de script na troca e devolve 0 para tarefa não pendente") { }
```
`contract_death.cpp` + `CASES`: `direct_spawn_outside_context` (`olyDirectSpawn` fora de frame; `olyDirectSpawn`, `fora`), `context_persist_outside_context` (`olyContextPersist` fora de frame; `olyContextPersist`, `fora`), `state_hook_invalid_when` (`olyEngineAddScriptStateHook(engine, 2, 0, "x")`; `when`, `inválido`), `request_state_out_of_range_c` (`olyEngineRequestState(engine, 9)`; `requestState`, `fora do intervalo`).

- [ ] **Step 2: Ver falhar** — `$DC make test T='^bridge\.'` → erro de compilação.

- [ ] **Step 3: Implementar** as funções como repasse à `Engine` (máscara por `setSystemStateMask`/`setHandlerStateMask`; ganchos de script com `userData = engine`; ganchos nativos por `findNamedStateHook`).

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: estados na API C com ganchos de script, spawn direto e persist`.

---

### Task 6: Estados no `@oly/core`, teste WASM e documentação

**Files:**
- Create: `engine/packages/core/src/{state_context.ts,state_mask.ts}`, `engine/packages/core/tests/unit/state_mask.test.ts`, `engine/testing/tests/wasm/states.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,context.ts,direct_context.ts,system_context.ts,scheduler.ts,world.ts,bridge.ts,module.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/{schema.ts,systems.cpp}`, `engine/testing/tests/wasm/fixture.ts`, `CLAUDE.md`

**Interfaces:**
- Consumes: API C da Task 5; `State`, `stateIndexOf`, `stateAt` da Task 4.
- Produces (`@oly/core`):
  - `state_mask.ts` (interno): `stateMaskOf(indices: readonly number[]): number` → OR de `1 << index`, terminado em `>>> 0`.
  - `Engine`: `requestState(state: State): void` (vale dentro e fora de `frame`); `get state(): State | null`; `onEnter(state: State, name: string, hook: StateHookFunction): void`; `onExit(...)`; `addNativeOnEnter(state: State, name: string): void`; `addNativeOnExit(...)` (`ContractError` se não há `OLY_STATE_HOOK` com o nome). `SystemRegistration` e `HandlerRegistration` ganham `inState(...states: State[]): this` (lista vazia tira a restrição). Registro e `inState` lançam `ContractError` com a `Engine` liberada, durante `frame` e depois de um `frame` interrompido, como `addSystem`. O despacho de `olyInvokeScript` trata `kind` 4: `sync()`, monta `fromState`/`toState` pelo lado do gancho e pelo `eventCount`, ativa o contexto, destrava a estrutura do `World` e, no `finally`, desativa e trava.
  - `EngineContext`: `requestState(state: State): void`, `get state(): State | null`.
  - `SystemContext.persist(entity: Entity): void` (adiado, sem conferir vida); `DirectContext.persist(entity: Entity): void` (`ContractError` `"persist entity=<e>: entidade morta"`); `DirectContext.spawn` passa por `spawnWith` com `bridge.directSpawn`.
  - `state_context.ts`: `type StateHookFunction = (context: StateContext) => void;` `class StateContext extends DirectContext` com `get fromState(): State | null` e `get toState(): State | null`; `ContextKind { label: "hook", className: "StateContext", callNoun: "gancho" }`.
  - `Scheduler.persist(handle: TaskHandle): boolean` (mesmas validações de `cancel`).
  - `Bridge`: `engineRequestState(engine, state)`, `engineState(engine): number`, `engineSetSystemStates(engine, systemId, mask)`, `engineSetHandlerStates(engine, handlerId, mask)`, `engineAddNativeStateHook(engine, when, state, name): number` e `engineAddScriptStateHook(...)` (terminam em `sync()`), `directSpawn(engine): Entity` (saída em `scratch + OutputOffset`, termina em `sync()`), `contextPersist(engine, entity)` (termina em `sync()`), `enginePersistTask(engine, slot, generation): boolean`. `OlyModule` ganha os exports correspondentes.
  - `public-exports.txt`: + `StateContext`, `StateHookFunction`.
- Fixture: `schema.ts` ganha `state("menu")`, `state("playing")`, `state("gameOver")` (exportados também por `fixture.ts`); `systems.cpp` ganha `OLY_STATE_HOOK(appendEight)` (`query<Trace>().each`: `value * 10 + 8`).

- [ ] **Step 1: Testes que falham.**
  - `state_mask.test.ts`: `stateMaskOf([]) === 0`; `stateMaskOf([0, 2]) === 0b101`; `stateMaskOf([31]) === 0x80000000`.
  - `engine/testing/tests/wasm/states.test.ts` (helpers `withEngine` e `stepper` como em `schedule.test.ts`; `checkInvariants() === null` no fim de cada caso):
```ts
test("requestState, engine.state e context.state; vale o último pedido", ...)
test("ganchos C++ e TS intercalados na ordem de registro", ...)                 // onEnter(playing): TS 1, addNativeOnEnter appendEight, TS 2 → Trace 182
test("fromState e toState no onExit e no onEnter; null para o estado vazio", ...)
test("inState pelo TS em sistema nativo, sistema TS e handler", ...)
test("spawn direto do TS em gancho e handler tem escopo; World.spawn fora de contexto é persistente", ...)
test("persist em sistema TS, em contexto direto e de tarefa", ...)
test("mapa de closures vazio depois da troca", ...)                            // every e after com escopo pendentes; troca → scriptTaskCount === 0
test("exceção em onExit TS: estado não muda e a limpeza não roda; o frame seguinte se recupera", ...)
test("exceção em onEnter TS: estado novo e os ganchos seguintes não rodam", ...)
test("memória cresce dentro do gancho TS e o contexto continua válido", ...)   // como o teste do handler em events.test.ts
test("StateContext fora da chamada do gancho é ContractError", ...)
test("erros: estado de outro esquema, onEnter e inState durante frame e após frame interrompido, persist de entidade morta, addNativeOnEnter desconhecido", ...)
```
  - `surface.test.ts`: `Engine` ganha `requestState`, `state`, `onEnter`, `onExit`, `addNativeOnEnter`, `addNativeOnExit`; `SystemContext`, `HandlerContext`, `TaskContext` e `StateContext` ganham `requestState`, `state`, `persist`; `StateContext` tem `fromState` e `toState`; `Scheduler` ganha `persist`.

- [ ] **Step 2: Ver falhar** — `$WC make games-test` → erro de tipos no `tsc` (`StateContext` não existe).

- [ ] **Step 3: Implementar.** A `Engine` guarda `Map<number, { function, context: StateContext, entering: boolean }>` por `hookId`; o registro de estados usa `stateIndexOf`; `engine.state` e `context.state` usam `stateAt(engineState(...))`.

- [ ] **Step 4: `CLAUDE.md`** — seção nova curta "Engine: estados" (declaração e ordem, pedido com último vencendo, troca no passo 5 pausado ou não com `onExit` → limpeza → `onEnter` → despacho, pedido em gancho vale no passo seguinte, `inState` por máscara em todas as fases, escopo pela tag `detail::StateScoped` e pelo campo da tarefa, spawn direto no `World` persistente, `persist`, recuperação descarta o pedido, duas `Engine`s num `World` dividem a tag); na ponte, `OLY_SCRIPT_STATE_HOOK` na descrição de `olyHostInvokeScript` e `olyDirectSpawn`/`olyContextPersist`; linhas do glossário para declarar/pedir/ler estado, gancho de estado, restringir a estados e persistir (e `olyDirectSpawn` na linha de criar entidade); `StateContext` e `State` na lista do `@oly/core`; o resumo do `app` na árvore ganha "estados".

- [ ] **Step 5: Verificação final** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (sem asserts: só as validações TS seguram os erros): tudo verde.

- [ ] **Step 6: Commit** `feat: estados no @oly/core com ganchos TS, inState e documentação`.
