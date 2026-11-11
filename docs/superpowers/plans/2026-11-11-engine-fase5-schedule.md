# Engine — Fase 5 (Schedule) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tarefas agendadas no relógio do jogo (`after`/`every`/cancelar), disparadas no passo 2 do passo fixo pelo núcleo C++, com payload copiado e descarte, expostas na API C para tarefas TS e usadas pelo `@oly/core` em `engine.schedule` e `context.schedule`.

**Architecture:** Uma fila `detail::TaskQueue` (min-heap por `(vencimento, sequência)` sobre slots com geração, cancelamento preguiçoso) vive no `app` e não conhece a `Engine`. A classe pública `Scheduler` calcula o vencimento a partir de `Engine::time()` e delega para a fila; a `Engine` a guarda, roda as tarefas vencidas em `runDueSchedule()` (passo 2, só em passo não pausado) com um `TaskContext`, e as tarefas sobrevivem à recuperação de exceção. As mudanças estruturais diretas saem do `HandlerContext` para `DirectContext`, base do `HandlerContext` e do `TaskContext`. O `bridge` registra uma tarefa TS como tarefa C++ comum cujo payload guarda `OlyEngine*` e o `scriptId`; o disparo e o descarte chamam `olyHostInvokeScript` com `kind` novo. No TS, `Scheduler` guarda `scriptId → closure` e apaga a entrada na última execução ou no descarte.

**Tech Stack:** C++20, GCC 14 (nativo), Emscripten 3.1.74 + node 20.18 (serviço `wasm`), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (fase 5 da §5; §4.6 inteira, §4.3 para a ordem do passo, a pausa e a recuperação, §4.4 para eventos emitidos em tarefa, §6, §7 "Schedule"). `§N` abaixo são desse spec.

## Global Constraints

- Branch `feat/engine-fase5-schedule` a partir de `develop`. Commits, comentários, mensagens de assert e de erro em pt-BR; commits terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain só em Docker: `DC="docker compose run --rm dev"` (nativo, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). Presets `wasm-games*` exigem `make ts` antes.
- `-Wall -Wextra -Wpedantic -Werror` em todo alvo C++; TS `strict` (§3).
- Camadas: `core ← input ← app ← bridge ← jogos` e `core ← ecs ← app`. Nenhum módulo novo: a fila fica em `engine/runtime/app/include/oly/app/detail/` (§4.6). O ECS e o `input` não mudam.
- Engine completa em C++ (§2): vencimento, ordem, `every` sem deriva, pausa, cancelamento, descarte e recuperação vivem no `app`. O TS só valida uso, guarda closures e repassa.
- Fronteira: uma chamada por disparo e uma por descarte de tarefa TS; agendar, cancelar e consultar cruzam uma vez por chamada. Nada por entidade.
- Relógio: `time()` da `Engine` (soma dos passos fixos não pausados). Dentro de um passo, `time()` é o início do passo; fora de `frame` (e na fase `render`), o início do próximo passo.
- Payload: trivialmente copiável, `sizeof ≤ 32`, `alignof ≤ 8` (`static_assert`) (§4.6).
- Nada de conceito de jogo em `engine/runtime` nem em `engine/packages`; nomes de jogo só em `engine/testing/fixture/` e nos testes.
- Sem benchmark nesta fase (§7).
- Glossário (`CLAUDE.md`): mesmo verbo nas três camadas — `after`, `every`, `cancel`, `isPending` (C++ `Scheduler`; C `olyEngineScheduleScriptTask`, `olyEngineCancelTask`, `olyEngineIsTaskPending`; TS `schedule.after/every/cancel/isPending`). `oly` só na API C.
- Estilo `CLAUDE.md`: chaves em todo bloco, nomes completos, dados no topo da classe, um `namespace` por arquivo, includes do projeto com aspas depois da biblioteca padrão. Não formatar à mão (hook `clang-format`). Comentário só para razão, restrição externa ou contrato não óbvio; nada de narração da edição.

### Decisões que o spec deixa em aberto (fixadas aqui)

- **Arquivos.** `oly/app/task.hpp`: tipos básicos sem contexto (`TaskHandle`, `InvalidTaskSlot`, `TaskFunction`, `TaskDiscardFunction`, `PayloadKey`, `payloadKey<P>()`, `TaskPayload`, limites). `oly/app/detail/task_queue.hpp`: a fila. `oly/app/scheduler.hpp`: `Scheduler`. `oly/app/task_context.hpp`: `TaskContext`. `DirectContext` fica em `context.hpp`, ao lado do `EngineContext`. A separação `task.hpp`/`task_context.hpp` repete a de `event.hpp`/`handler.hpp` e evita o ciclo `context.hpp → scheduler.hpp → TaskContext → context.hpp`.
- **Tipo do payload.** `PayloadKey` é denso por processo, como `EventKey` (`detail::nextPayloadKey()` em `src/task.cpp`); `NoPayloadKey = 0xFFFFFFFFu` para tarefa sem payload. O `TaskPayload` guarda também `typeName<P>()` para a mensagem de erro.
  ```cpp
  struct TaskPayload {
    alignas(MaxTaskPayloadAlignment) std::byte bytes[MaxTaskPayloadSize];
    PayloadKey key = NoPayloadKey;
    std::string_view typeName;

    template <class P>
    static TaskPayload of(const P& value);   // static_asserts de §4.6; memcpy para bytes
  };
  ```
- **Base type-erased.** `TaskHandle Scheduler::schedule(double delaySeconds, double intervalSeconds, TaskFunction function, const TaskPayload& payload = {}, TaskDiscardFunction discard = nullptr)`: `intervalSeconds == 0` é `after`; `> 0` é `every` com o primeiro disparo em `time() + delaySeconds`. Os templates `after`/`every` são finos e delegam (`every(interval, ...)` passa `delaySeconds = interval`). É o que a API C chama; o TS sempre passa `delay = interval` para `every`.
- **Handle padrão.** `cancel(TaskHandle{})` e `isPending(TaskHandle{})` devolvem `false` sem assert: o handle padrão significa "nenhuma tarefa", para o jogo poder escrever `schedule.cancel(dashTimer)` sem testar antes. Só `slot` fora do intervalo **e** diferente de `InvalidTaskSlot` é erro de contrato.
- **Slots e geração.** Cada slot guarda função, descarte, `TaskPayload`, vencimento, intervalo, sequência, geração e `live`. O handle captura a geração do slot; liberar o slot (cancelamento, último disparo de `after`, descarte) incrementa a geração e devolve o slot à lista livre. A entrada do heap é `{due, sequence, slot, generation}`; entrada com geração diferente da do slot, ou slot morto, é descartada quando chega ao topo (cancelamento preguiçoso).
- **Tarefas criadas durante o passo 2.** `beginRun()` liga um modo em que `push` reserva o slot na hora (handle válido, `isPending` verdadeiro) mas guarda a entrada do heap em `deferred_`; `endRun()` e `recoverFromInterruptedRun()` as passam para o heap. Uma fronteira por sequência no heap não serve: com a tolerância, uma tarefa nova de vencimento `agora` ficaria no topo à frente de uma antiga vencida em `(agora, agora + tolerância]` e a bloquearia.
- **`popDue(limit)`** (o `every` volta ao heap e o `after` sai antes de o callback rodar, §4.6):
  ```
  repete:
    heap vazio → nada
    topo morto ou de geração antiga → descarta a entrada, continua
    topo.due > limit → nada
    tira o topo; copia função, handle e payload para DueTask
    after: lastRun = true; libera o slot (sem chamar o descarte)
    every: next = due + interval
           se next ≤ limit: next = due + (floor((limit − due) / interval) + 1) · interval
           se ainda next ≤ limit (arredondamento): next = limit + interval
           volta ao heap com a mesma sequência; lastRun = false
    devolve DueTask
  ```
  `limit = time() + fixedDeltaTime · StepTolerance`, a mesma tolerância do acumulador (`StepTolerance` sai do namespace anônimo de `engine.cpp` para um `constexpr` privado da `Engine`).
- **Descarte.** `cancel` copia o payload para uma variável local, libera o slot e só depois chama `discard(local.bytes)`: o descarte pode agendar ou cancelar outras tarefas sem pegar a fila no meio. `discardAll()` repete "cancelar todo slot vivo" até `pendingCount() == 0` (cobre descarte que agenda) e limpa heap e `deferred_`; roda no destrutor da `TaskQueue`, então na destruição da `Engine`.
- **`StepTolerance` e `time()` no `Scheduler`.** O `Scheduler` guarda `Engine&` (construído com `*this` na `Engine`) e lê `engine.time()` em `schedule.cpp`; o header só declara `class Engine;`. A `Engine` é `friend` do `Scheduler` para chegar à `TaskQueue` em `runDueSchedule()`.
- **Contratos (`OLY_ASSERT`).** `schedule`: `"schedule: atraso=%f negativo ou não finito"`, `"schedule: intervalo=%f negativo ou não finito"`, `"schedule: função nula"`. `every`: `"every: intervalo=%f deve ser positivo e finito"` (no template, antes de delegar). `cancel`/`isPending`: `"%s: slot=%u fora do intervalo (%u slots)"`. `TaskContext::payload<P>()`: `"payload: tipo \"%s\" diferente do agendado \"%s\""` (agendado sem payload aparece como `"(nenhum)"`).
- **API C.** `OLY_SCRIPT_TASK 2u` e `OLY_SCRIPT_TASK_DISCARD 3u` usam a mesma `olyHostInvokeScript`/`ScriptInvoker`: `events` nulo e `eventCount` = `lastRun` (0 ou 1) no disparo, 0 no descarte. Payload da tarefa TS: `struct ScriptTaskPayload { OlyEngine* engine; uint32_t scriptId; };`. `olyEngineScheduleScriptTask` repassa a `Scheduler::schedule` sem validação própria; os asserts do `Scheduler` valem.
- **Ordem na destruição da `OlyEngine` (TS).** `olyEngineDestroy` descarta as tarefas pendentes e chama o JS com `OLY_SCRIPT_TASK_DISCARD` antes de voltar. Por isso `Engine.dispose()` no TS só tira a engine do mapa do `Runtime` (`onDispose`) **depois** de `engineDestroy`, como hoje, e o tratamento do descarte não passa por `#assertUsable`.
- **`TaskHandle` no TS.** Objeto congelado, sem campos visíveis, tipado como `interface TaskHandle { readonly [taskHandleBrand]: true }` (`declare const taskHandleBrand: unique symbol`). O `Scheduler` guarda `WeakMap<TaskHandle, { slot, generation }>`: comparar é `===`, e handle que não está no mapa é "de outra `Engine`".
- **Contextos no TS.** `DirectContext` (novo `direct_context.ts`, sem export no `index.ts`, como `EngineContext`) recebe `spawn`/`destroy`/`add`/`remove` diretos do `HandlerContext`. `EngineContext` ganha `schedule` (getter). `TaskContext extends DirectContext` com `handle` e `lastRun` (getters), `ContextKind { label: "task", className: "TaskContext", callNoun: "tarefa" }`. Um `TaskContext` por tarefa, criado ao agendar; `lastRun` é atualizado a cada disparo.
- **Interleaving C++/TS no teste WASM.** Tarefas C++ só são agendadas por C++ (§4.6). O fixture ganha `fixtureScheduleDigit(OlyEngine*, double delaySeconds, uint32_t index, uint32_t generation, uint32_t digit)`, que agenda `after(delay, appendDigitTask, AppendDigit{entity, digit})`; a tarefa faz `trace.value = trace.value * 10 + digit`, o mesmo truque de ordem do `appendNineOnPing`. Para o teste chegar ao `OlyEngine*`, `@oly/core/internal` ganha `engineHandleOf(engine: Engine): number`.
- **Fora do escopo:** escopo por estado (fase 6), tarefa nativa por nome, `pendingCount` no TS, benchmark.

## Review Focus

1. **Handle antigo depois do reuso do slot** (o `every` do dash se cancela, uma tarefa nova ocupa o mesmo slot, e o jogo cancela de novo pelo handle velho): `cancel` devolve `false` e a tarefa nova continua pendente; `TaskHandle{}` também devolve `false` sem assert (Task 1, Task 2).
2. **`every` com intervalo muito menor que o passo** (`every(1e-9)`) ou depois de muitos passos pausados: dispara uma vez por passo, sem laço longo nem disparo repetido no mesmo passo (Task 1).
3. **Descarte reentrante** (o descarte de uma tarefa agenda ou cancela outra, inclusive durante a destruição da `Engine`): cada tarefa é descartada uma vez e a fila termina vazia (Task 1).
4. **A mesma closure TS agendada duas vezes**: cancelar um handle não afeta o outro; o mapa é por `scriptId`, não pela função (Task 4).
5. **`every` TS que lança**: a exceção chega a quem chamou `frame`, o `frame` seguinte se recupera, a tarefa continua pendente e roda de novo, e a closure não é apagada (Task 4).

---

### Task 1: `detail::TaskQueue` — heap com slots, geração e descarte

**Files:**
- Create: `engine/runtime/app/include/oly/app/task.hpp`, `engine/runtime/app/include/oly/app/detail/task_queue.hpp`, `engine/runtime/app/src/task.cpp`, `engine/runtime/app/src/task_queue.cpp`, `engine/runtime/app/tests/task_queue.cpp`
- Modify: `engine/runtime/app/tests/CMakeLists.txt`

**Interfaces:**
- Produces (`oly::app`, `task.hpp`):
  - `class TaskContext;` (declaração); `using TaskFunction = void (*)(TaskContext& context);` `using TaskDiscardFunction = void (*)(void* payload);`
  - `inline constexpr uint32_t InvalidTaskSlot = 0xFFFFFFFFu;` `struct TaskHandle { uint32_t slot = InvalidTaskSlot; uint32_t generation = 0; bool operator==(const TaskHandle&) const = default; };`
  - `using PayloadKey = uint32_t;` `inline constexpr PayloadKey NoPayloadKey = 0xFFFFFFFFu;` `namespace detail { PayloadKey nextPayloadKey() noexcept; }` `template <class P> PayloadKey payloadKey() noexcept;` (estático local, como `eventKey`).
  - `inline constexpr std::size_t MaxTaskPayloadSize = 32;` `inline constexpr std::size_t MaxTaskPayloadAlignment = 8;` `struct TaskPayload` como em "Decisões", com `static_assert` de trivialmente copiável, tamanho e alinhamento em `of<P>` (mensagens em pt-BR com "payload").
- Produces (`oly::app::detail`, `task_queue.hpp`):
  - `struct DueTask { TaskFunction function; TaskHandle handle; bool lastRun; TaskPayload payload; };`
  - `class TaskQueue` com `~TaskQueue()` (chama `discardAll`), não copiável; `TaskHandle push(double due, double interval, TaskFunction function, TaskDiscardFunction discard, const TaskPayload& payload)`, `bool cancel(TaskHandle task)`, `bool isPending(TaskHandle task) const`, `uint32_t pendingCount() const noexcept`, `void beginRun() noexcept`, `std::optional<DueTask> popDue(double limit)`, `void endRun()`, `void recoverFromInterruptedRun()`, `void discardAll()`.
  - Contrato: `cancel`/`isPending` com `slot` fora do intervalo e diferente de `InvalidTaskSlot` (`OLY_ASSERT`, mensagem de "Decisões"). A fila não valida atraso nem função; isso é do `Scheduler` (Task 2).

- [ ] **Step 1: Branch** `git switch -c feat/engine-fase5-schedule`.

- [ ] **Step 2: Testes que falham** em `tests/task_queue.cpp` (`oly_add_test(MODULE app NAME task_queue SOURCES task_queue.cpp)`). Sem `Engine` nem `World`: os callbacks não são chamados, só a ordem e o estado dos `DueTask` são conferidos. Um descarte de teste incrementa um contador global e grava o payload recebido.
```cpp
TEST_CASE("popDue devolve na ordem de vencimento e, no empate, de criação") { /* push 2.0 "a", 1.0 "b", 1.0 "c"; popDue(5) ×3 */ CHECK(ordem == {b, c, a}); }
TEST_CASE("popDue respeita o limite") { /* push 1.0; popDue(0.5) */ CHECK(!result); CHECK(popDue(1.0)); }
TEST_CASE("after sai da fila antes de ser devolvido") { CHECK(task->lastRun); CHECK(!queue.isPending(task->handle)); CHECK(queue.pendingCount() == 0); CHECK(descartes == 0); }
TEST_CASE("every volta ao heap sem deriva") { /* push(due 0.1, interval 0.1); popDue(0.1) → next 0.2; popDue(0.25) → next 0.3 */ CHECK(!task->lastRun); CHECK(queue.isPending(task->handle)); }
TEST_CASE("every atrasado pula para o primeiro múltiplo depois do limite") { /* due 0.1, interval 0.1; popDue(0.55) devolve 1; popDue(0.55) */ CHECK(!segundo); CHECK(popDue(0.6)); }
TEST_CASE("every com intervalo minúsculo dispara uma vez por chamada de popDue") { /* due 1e-9, interval 1e-9; popDue(1.0/60) */ CHECK(popDue(1.0/60).has_value()); CHECK(!popDue(1.0/60).has_value()); }
TEST_CASE("cancelada não sai em popDue e cancelar de novo devolve false") { CHECK(queue.cancel(handle)); CHECK(!queue.cancel(handle)); CHECK(!popDue(10)); CHECK(descartes == 1); }
TEST_CASE("handle antigo não cancela a tarefa que reusou o slot") { /* push A; cancel A; push B (mesmo slot) */ CHECK(b.slot == a.slot); CHECK(!queue.cancel(a)); CHECK(queue.isPending(b)); }
TEST_CASE("handle padrão: cancel e isPending devolvem false") { CHECK(!queue.cancel(TaskHandle{})); CHECK(!queue.isPending(TaskHandle{})); }
TEST_CASE("push durante a execução só entra no heap no endRun") { /* beginRun; push due 0; */ CHECK(queue.isPending(nova)); CHECK(!popDue(1)); /* endRun */ CHECK(popDue(1)); }
TEST_CASE("recoverFromInterruptedRun guarda as tarefas criadas antes da exceção") { /* beginRun; push; sem endRun; recoverFromInterruptedRun */ CHECK(popDue(1)); }
TEST_CASE("descarte recebe a cópia do payload e roda uma vez") { /* payload {7}; cancel */ CHECK(payloadRecebido == 7); CHECK(descartes == 1); }
TEST_CASE("discardAll descarta cada pendente uma vez, inclusive o agendado por um descarte") { /* 2 tarefas; o descarte da 1ª faz push de uma 3ª com descarte */ CHECK(descartes == 3); CHECK(queue.pendingCount() == 0); }
TEST_CASE("destrutor descarta o que estiver pendente") { /* escopo com 2 tarefas */ CHECK(descartes == 2); }
TEST_CASE("payloadKey distingue tipos e TaskPayload::of copia os bytes") { CHECK(payloadKey<A>() != payloadKey<B>()); CHECK(payload.key == payloadKey<A>()); }
```

- [ ] **Step 3: Ver falhar** — `$DC make test T='app.task_queue'` → erro de compilação (`oly/app/detail/task_queue.hpp` não existe).

- [ ] **Step 4: Implementar** conforme "Decisões" (`std::vector<HeapEntry>` com `std::push_heap`/`std::pop_heap` e comparação "maior" em `(due, sequence)`; slots em `std::vector`; lista livre em `std::vector<uint32_t>`; sequência `uint64_t` crescente).

- [ ] **Step 5: Ver passar** — `$DC make test T='^app\.'` e `$WC make test PRESET=wasm-debug T='app.task_queue'`.

- [ ] **Step 6: Commit** `feat: fila de tarefas do app com heap, geração e descarte`.

---

### Task 2: `Scheduler`, `TaskContext` e `DirectContext` na `Engine`

**Files:**
- Create: `engine/runtime/app/include/oly/app/{scheduler.hpp,task_context.hpp}`, `engine/runtime/app/src/scheduler.cpp`, `engine/runtime/app/tests/schedule.cpp`
- Modify: `engine/runtime/app/include/oly/app/{engine.hpp,context.hpp,handler.hpp}`, `engine/runtime/app/src/{engine.cpp,context.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp,recovery.cpp}`

**Interfaces:**
- Consumes: Task 1 (`TaskQueue`, `TaskHandle`, `TaskPayload`, `payloadKey`).
- Produces:
  - `class Scheduler` com a API do §4.6 (`after`/`every` com e sem payload, `cancel`, `isPending`, `pendingCount`) mais `TaskHandle schedule(double delaySeconds, double intervalSeconds, TaskFunction function, const TaskPayload& payload = {}, TaskDiscardFunction discard = nullptr)`. Construtor `explicit Scheduler(Engine& engine)`, não copiável.
  - `class DirectContext : public EngineContext` (em `context.hpp`) com `spawn()`, `spawn(Ts&&...)`, `destroy`, `add<T>`, `remove<T>`, `addById`, `removeById`, todos diretos no `World` (movidos do `HandlerContext`, que passa a herdar de `DirectContext` sem mudar de comportamento).
  - `EngineContext::schedule() const noexcept -> Scheduler&`; `context.hpp` inclui `scheduler.hpp`.
  - `class TaskContext : public DirectContext` com `TaskHandle handle() const noexcept`, `bool lastRun() const noexcept`, `template <class P> const P& payload() const` (contrato de "Decisões"). Construtor privado, `friend class Engine`.
  - `Engine::schedule() noexcept -> Scheduler&` (e `const`). `runDueSchedule()` deixa de ser vazia; `recoverFromInterruptedFrame` chama `recoverFromInterruptedRun()` e **não** limpa as tarefas.

- [ ] **Step 1: Testes que falham** em `tests/schedule.cpp` (`oly_add_test(MODULE app NAME schedule SOURCES schedule.cpp SUPPORT engine/runtime/ecs/tests/support)`). Fixture como `pause.cpp` (`frame(0)` só registra o tempo; `step()` avança 1/60). As tarefas gravam `context.time()` e um rótulo do payload num `Log` apontado pelo payload (`struct Mark { Log* log; char label; }`). `CHECK_INVARIANTS(world)` no fim de cada caso. Tempos comparados com `doctest::Approx` (`time()` é `passos · (1/60)`, que pode diferir de `k/60.0` no último bit).
```cpp
TEST_CASE("after agendado fora de frame dispara no passo cujo início alcança o vencimento") { /* after(2/60) em t=0; 3 passos */ CHECK(log.times.size() == 1); CHECK(log.times[0] == Approx(2.0/60)); }
TEST_CASE("after(0) fora de frame roda no passo 2 do próximo passo") { /* after(0); 1 passo */ CHECK(log.calls == {"tarefa", "sistema"}); }
TEST_CASE("after(0) dentro de uma tarefa só roda no passo seguinte") { CHECK(log.times == {0.0, 1.0/60}); }
TEST_CASE("after(0) agendado por um sistema roda no passo seguinte ao dele") { }
TEST_CASE("after(0) agendado na fase render roda no próximo passo") { }
TEST_CASE("mesmo passo: ordem de vencimento e, no empate, de criação") { /* after(1.5/60) a, after(1.2/60) b, after(1.2/60) c; 3 passos */ CHECK(labels == "bca"); }
TEST_CASE("every sem deriva") { /* every(0.025); 60 passos */ CHECK(disparos == 39); CHECK(times[k] == Approx(ceil(1.5·(k+1)) / 60)); }
TEST_CASE("every com intervalo menor que o passo vira uma vez por passo") { /* every(0.004); 10 passos */ CHECK(disparos == 9); }
TEST_CASE("cancelar antes, durante e depois") { /* antes: cancel fora de frame → não roda; durante: A cancela B vencida no mesmo passo → B não roda; depois: cancel de after já disparado → false */ }
TEST_CASE("every cancela a si mesma dentro do callback") { CHECK(disparos == 1); CHECK(descartes == 1); CHECK(!engine.schedule().isPending(handle)); }
TEST_CASE("after: lastRun verdadeiro, cancelar a si mesmo devolve false e o descarte nunca roda depois") { CHECK(lastRun); CHECK(!cancelou); CHECK(descartes == 0); }
TEST_CASE("pausado nada vence e o relógio fica parado") { /* after(1/60); pause; 5 passos */ CHECK(disparos == 0); /* resume; 2 passos */ CHECK(disparos == 1); }
TEST_CASE("aba oculta não dispara e não acumula") { /* after(2/60); setHidden(true); frames; setHidden(false); frame só registra; 2 passos */ CHECK(disparos == 0); /* 1 passo */ CHECK(disparos == 1); }
TEST_CASE("passos descartados não avançam o relógio") { /* maxStepsPerFrame=2; after(3/60); frame(0); frame(1.0) */ CHECK(disparos == 0); CHECK(engine.time() == 2.0/60); }
TEST_CASE("payload é copiado ao agendar") { /* Mark local alterado depois do after */ CHECK(label == valor original); }
TEST_CASE("tarefa faz mudanças estruturais diretas") { /* spawn Position, add Velocity, destroy outra */ CHECK(world.isAlive(nova)); CHECK_INVARIANTS(world); }
TEST_CASE("evento emitido numa tarefa é despachado no mesmo passo") { /* handler grava time() */ CHECK(handlerTime == tarefaTime); }
TEST_CASE("context.schedule() é o mesmo Scheduler da Engine") { CHECK(&context.schedule() == &engine.schedule()); }
TEST_CASE("destruir a Engine descarta as tarefas pendentes uma vez") { CHECK(descartes == 2); }
TEST_CASE("cancel e isPending com TaskHandle{} devolvem false") { }
```
`recovery.cpp` (só nativo, como os casos existentes): `TEST_CASE("tarefas sobrevivem à recuperação, inclusive as criadas antes da exceção")` — um `every(1/60)` agenda um `after(0)` e lança no primeiro disparo; `frame` seguinte recupera; os dois estão pendentes e rodam nos passos seguintes; `CHECK_INVARIANTS`.

`contract_death.cpp` + `CASES`:

| Caso | Ação | Substrings |
|---|---|---|
| `schedule_negative_delay` | `engine.schedule().after(-1.0, task)` | `schedule`, `atraso` |
| `schedule_not_finite_delay` | `after(NAN, task)` | `schedule`, `atraso` |
| `every_zero_interval` | `every(0.0, task)` | `every`, `intervalo` |
| `schedule_null_function` | `after(1.0, nullptr)` | `schedule`, `função nula` |
| `task_payload_wrong_type` | tarefa com payload `Mark` lê `payload<Other>()` | `payload`, `Other` |
| `cancel_slot_out_of_range` | `cancel(TaskHandle{7, 0})` sem tarefas | `cancel`, `fora do intervalo` |

- [ ] **Step 2: Ver falhar** — `$DC make test T='^app\.'` → erro de compilação (`schedule()` e `oly/app/task_context.hpp` não existem).

- [ ] **Step 3: Implementar.** Membro `Scheduler scheduler_{*this}` na `Engine` (bloco de dados, depois de `actions_`). `runDueSchedule()`: `beginRun()`, laço `popDue(limit)` criando `TaskContext` com o `DueTask` e chamando a função, `endRun()`. Atualizar os comentários que ficaram falsos: o das etapas vazias em `engine.hpp` (só resta `applyStateTransition`) e o de `frame` sobre recuperação (as tarefas ficam). Docs de `Scheduler::schedule`/`after`/`every`/`cancel` cobrem: quando vence, que pausa congela, que vale dentro e fora de `frame`, que o descarte roda uma vez e nunca depois do último disparo, que `cancel` devolve `false` para tarefa terminada ou handle padrão.

- [ ] **Step 4: Ver passar** — `$DC make test` (inclui `oly.lint.boundaries`) e `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: Scheduler com after/every no relógio do jogo e TaskContext`.

---

### Task 3: Tarefas de script na API C

**Files:**
- Modify: `engine/runtime/bridge/include/oly/bridge/{engine_api.hpp,engine.hpp}`, `engine/runtime/bridge/src/engine_api.cpp`, `engine/runtime/bridge/tests/{engine_api.cpp,contract_death.cpp,CMakeLists.txt}`

**Interfaces:**
- Consumes: Task 2 (`Engine::schedule()`, `Scheduler::schedule/cancel/isPending`, `TaskContext::payload/lastRun`).
- Produces (API C, `extern "C"`, `OLY_API`): `OLY_SCRIPT_TASK 2u`, `OLY_SCRIPT_TASK_DISCARD 3u`, `typedef struct OlyTaskHandle { uint32_t slot; uint32_t generation; } OlyTaskHandle;`, `olyEngineScheduleScriptTask`, `olyEngineCancelTask`, `olyEngineIsTaskPending` com as assinaturas do §4.6. No `bridge` (anônimo): `ScriptTaskPayload`, `runScriptTask(TaskContext&)` → `g_invoker(engine, OLY_SCRIPT_TASK, scriptId, nullptr, lastRun ? 1 : 0)`, `discardScriptTask(void*)` → `g_invoker(engine, OLY_SCRIPT_TASK_DISCARD, scriptId, nullptr, 0)`. O comentário do `ScriptInvoker` em `engine.hpp` passa a descrever os quatro `kind`.

- [ ] **Step 1: Testes que falham** em `tests/engine_api.cpp` (o `recordingInvoker` já grava `kind`, `id` e `eventCount`):
```cpp
TEST_CASE("tarefa de script after: OLY_SCRIPT_TASK com lastRun 1, sem descarte") { /* schedule(0, 0, 42); 1 passo */ CHECK(g_kinds == {OLY_SCRIPT_TASK}); CHECK(g_invocations == {42}); CHECK(g_eventCounts == {1}); CHECK(olyEngineIsTaskPending(...) == 0); }
TEST_CASE("tarefa de script every: lastRun 0 e cancelar descarta uma vez") { /* schedule(1/60, 1/60, 7); 3 passos; Cancel → 1; Cancel → 0 */ CHECK(contagem de OLY_SCRIPT_TASK_DISCARD com id 7 == 1); }
TEST_CASE("olyEngineDestroy descarta as tarefas de script pendentes") { /* 2 tarefas; destroy */ CHECK(descartes com ids {1, 2}); }
TEST_CASE("tarefas de script e nativas na ordem de vencimento") { /* nativa via engineOf(engine).schedule().after(1/60) grava em g_order; script after(0) e after(2/60); 3 passos */ CHECK(g_order == {script, nativa, script}); }
```
`contract_death.cpp` + `CASES`: `schedule_script_task_negative_interval` (`olyEngineScheduleScriptTask(engine, 0, -1, 1, &out)`; substrings `schedule`, `intervalo`).

- [ ] **Step 2: Ver falhar** — `$DC make test T='^bridge\.'` → erro de compilação.

- [ ] **Step 3: Implementar** as três funções como repasse a `engine->engine.schedule()` (`TaskPayload::of(ScriptTaskPayload{engine, scriptId})`, descarte `discardScriptTask`).

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: tarefas de script na API C com disparo e descarte pelo invocador`.

---

### Task 4: `schedule` no `@oly/core`, teste WASM e documentação

**Files:**
- Create: `engine/packages/core/src/{scheduler.ts,task_context.ts,direct_context.ts}`, `engine/testing/tests/wasm/schedule.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,context.ts,handler_context.ts,system_context.ts,bridge.ts,module.ts,index.ts,internal.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/systems.cpp`, `engine/testing/tests/wasm/fixture.ts`, `CLAUDE.md`

**Interfaces:**
- Consumes: API C da Task 3.
- Produces (`@oly/core`):
  - `scheduler.ts`: `type TaskFunction = (task: TaskContext) => void;` `interface TaskHandle` (marca de "Decisões"); `class Scheduler` com `after(delaySeconds: number, callback: TaskFunction): TaskHandle`, `every(intervalSeconds: number, callback: TaskFunction): TaskHandle`, `cancel(handle: TaskHandle): boolean`, `isPending(handle: TaskHandle): boolean`. Construtor interno `(runtime, bridge, engineHandle, world, actions, assertUsable: (operation: string) => void)`. Funções de módulo (fora do `index.ts`): `invokeScriptTask(scheduler, scriptId, lastRun: boolean)`, `discardScriptTask(scheduler, scriptId)`, `scriptTaskCount(scheduler): number`.
  - Erros (`ContractError`, antes de chamar a API C): atraso negativo ou não finito; intervalo ≤ 0 ou não finito; callback que não é função; handle que não está no `WeakMap` ("handle de outra Engine"); `Engine` liberada. Cancelar um handle já terminado devolve `false`.
  - `direct_context.ts`: `class DirectContext extends EngineContext` com `spawn`/`destroy`/`add`/`remove` (movidos de `handler_context.ts`); `HandlerContext extends DirectContext`.
  - `task_context.ts`: `class TaskContext extends DirectContext` com `get handle(): TaskHandle` e `get lastRun(): boolean`.
  - `EngineContext.schedule: Scheduler` (getter; `ContractError` fora da chamada); `Engine.schedule: Scheduler` (getter; `ContractError` com a `Engine` liberada). Os dois devolvem o mesmo objeto. Os construtores dos contextos recebem o `Scheduler` depois de `actions`.
  - `Engine`: o despacho de `olyInvokeScript` passa a tratar `kind` 2 (`invokeScriptTask(scheduler, id, eventCount !== 0)`) e 3 (`discardScriptTask`). A chamada da tarefa faz `bridge.sync()`, ativa o contexto e destrava a estrutura do `World` como em `#invokeHandler`; no `finally`, apaga a closure se `lastRun`, desativa o contexto e trava de novo.
  - `Bridge`: `engineScheduleScriptTask(engine, delaySeconds, intervalSeconds, scriptId): { slot: number; generation: number }` (saída no `scratch + OutputOffset`, como `systemSpawn`), `engineCancelTask(engine, slot, generation): boolean`, `engineIsTaskPending(engine, slot, generation): boolean`; agendar e cancelar terminam em `sync()` (o descarte chama o JS).
  - `OlyModule`: `_olyEngineScheduleScriptTask`, `_olyEngineCancelTask`, `_olyEngineIsTaskPending`.
  - `internal.ts`: + `engineHandleOf(engine: Engine): number`, `scriptTaskCount`.
  - `public-exports.txt`: + `Scheduler`, `TaskContext`, `TaskFunction`, `TaskHandle`.
- Fixture (`systems.cpp`): `struct AppendDigit { oly::ecs::Entity target; uint32_t digit; };`, tarefa `appendDigitTask` e export `fixtureScheduleDigit` de "Decisões"; `fixture.ts` expõe `fixtureScheduleDigit(engine: number, delaySeconds: number, index: number, generation: number, digit: number)`.

- [ ] **Step 1: Testes que falham.**
  - `engine/testing/tests/wasm/schedule.test.ts` (helpers `withEngine` e `stepper` como em `events.test.ts`; `checkInvariants() === null` no fim de cada caso):
```ts
test("after e every do engine.schedule disparam no passo do vencimento", ...)       // time lido no callback: after(2/60) → [2/60]; every(0.025) em 60 passos → 39
test("tarefas C++ e TS intercaladas por vencimento e criação", ...)                // fixtureScheduleDigit(0, d1), TS after(0) d2, fixtureScheduleDigit(1/60, d3), TS after(1/60) d4 → Trace 1234 após 2 passos
test("tarefa TS faz mudanças diretas e o evento emitido é despachado no mesmo passo", ...) // spawn + emit(Ping); handler TS vê o Ping com o mesmo context.time
test("cancel, isPending e every que cancela a si mesma", ...)
test("a mesma closure agendada duas vezes: cancelar um handle não afeta o outro", ...)
test("exceção em after TS chega a quem chamou frame; o frame seguinte se recupera e a closure some", ...)
test("every TS que lança continua pendente e roda de novo depois da recuperação", ...)
test("memória cresce dentro da tarefa TS e o contexto continua válido", ...)       // como o teste do handler em events.test.ts: context.spawn até o buffer mudar, depois context.get/set certos
test("mapa de closures termina vazio em todos os caminhos", ...)                   // disparo de after, cancel, auto-cancelamento, exceção e dispose com every pendente: scriptTaskCount(scheduler) === 0
test("context.schedule e engine.schedule são o mesmo objeto", ...)
test("erros: atraso negativo ou NaN, intervalo 0, callback que não é função, handle de outra Engine, Engine liberada", ...)
test("TaskContext fora da chamada da tarefa é ContractError", ...)
```
  - `surface.test.ts`: membros de `Scheduler` e `TaskContext` exatamente como em Produces; `Engine`, `SystemContext`, `HandlerContext` e `TaskContext` ganham `schedule`; `HandlerContext` mantém `spawn`/`destroy`/`add`/`remove`.

- [ ] **Step 2: Ver falhar** — `$WC make games-test` → erro de tipos no `tsc` (`Scheduler` não existe).

- [ ] **Step 3: Implementar.** O `Scheduler` usa um contador próprio de `scriptId` e `Map<number, { callback, context, handle }>`. `after(d, cb)` chama `engineScheduleScriptTask(handle, d, 0, id)`; `every(i, cb)` passa `(i, i, id)`. A `Engine` cria o `Scheduler` no construtor, depois do `ActionMap`, e o repassa aos contextos.

- [ ] **Step 4: `CLAUDE.md`** — seção nova curta "Engine: schedule" (fila no `app/detail`, relógio do jogo, disparo no passo 2 só sem pausa, criadas no passo 2 esperam o passo seguinte, `every` sem deriva, payload copiado ≤ 32 B, descarte uma vez e nunca depois de `lastRun`, tarefas sobrevivem à recuperação, `DirectContext` base de handler e tarefa); na ponte, `OLY_SCRIPT_TASK`/`OLY_SCRIPT_TASK_DISCARD` na descrição de `olyHostInvokeScript`; linhas do glossário para agendar e para cancelar/consultar; `Scheduler` e `TaskContext` na lista do `@oly/core`; o resumo do `app` na árvore ganha "schedule".

- [ ] **Step 5: Verificação final** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (sem asserts: só as validações TS seguram os erros): tudo verde.

- [ ] **Step 6: Commit** `feat: schedule no @oly/core com tarefas TS e documentação`.
