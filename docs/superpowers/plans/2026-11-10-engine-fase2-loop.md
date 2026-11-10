# Engine — Fase 2 (Loop e fases) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Loop da engine no núcleo C++ (`frame`, `setHidden`, passo fixo, render, pausa, `.whilePaused()`), sistemas C++ (por função e por nome) e TS (chamada de volta única) nas mesmas fases, mudanças estruturais adiadas pelo `CommandBuffer` da fase, host do navegador em `@oly/client` e as medições do §7.

**Architecture:** Um módulo C++ novo, `app` (`engine/runtime/app/`, acima de `ecs`), tem a classe `oly::app::Engine`: guarda uma referência ao `World`, os sistemas por fase, o relógio, o acumulador do passo fixo, a pausa e um `CommandBuffer` aplicado ao fim de cada fase. O `app` não sabe que TS existe: um sistema é um ponteiro de função mais `userData`. O `bridge` passa a depender de `app` e expõe a engine na API C (`OlyEngine`, `olyEngine*`, `olySystem*`); um sistema TS é, para o núcleo, uma função C++ do `bridge` que chama a única função JS importada (`olyHostInvokeScript` → `Module.olyInvokeScript`). No TS, `Runtime.createEngine` devolve uma `Engine` fina que repassa tudo à API C e despacha a chamada de volta por id. `@oly/client` ganha `runInBrowser` (rAF + `visibilitychange`).

**Tech Stack:** C++20, GCC 14 (nativo), Emscripten 3.1.74 + node 20.18 (serviço `wasm`), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (fase 2 da §5; §4.3 inteira, §6, §7 "Custo do loop"). `§N` abaixo são desse spec.

## Global Constraints

- Branch `feat/engine-fase2-loop` a partir de `develop`. Commits, comentários, mensagens de assert e de erro em pt-BR; commits terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain só em Docker: `DC="docker compose run --rm dev"` (nativo, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). Presets `wasm-games*` exigem `make ts` antes.
- `-Wall -Wextra -Wpedantic -Werror` em todo alvo C++; TS `strict` (§3).
- Camadas: `core ← ecs ← app ← bridge ← jogos`. O `app` só usa a API pública do ECS; o ECS não muda nesta fase (se mudar, reexecutar `ecs_bench_*` e conferir NF-P1/P2/P5 como em `docs/benchmarks/README.md`).
- Engine completa em C++ (§2, anti-objetivo): toda regra do loop (passos, pausa, aba oculta, ordem, `whilePaused`, recuperação) vive em `oly::app::Engine`. O TS só valida uso e repassa.
- Nada de conceito de jogo em `engine/runtime` nem em `engine/packages`; nomes de jogo só em `engine/testing/fixture/`.
- Fronteira: uma chamada C++→JS por sistema TS por passo; nunca por entidade (§2).
- O `CommandBuffer` não aparece com esse nome na API da engine (§4.5): os sistemas usam `context.spawn/destroy/add/remove`, que são adiados.
- Glossário (`CLAUDE.md`): o mesmo nome nas três camadas — `Engine` (C++ `oly::app::Engine`, C `OlyEngine`/`olyEngine*`, TS `Engine`), `frame`, `setHidden`, `pause`, `resume`, `addSystem`, `addNativeSystem`, `whilePaused`. `oly` só na API C.
- Estilo `CLAUDE.md`: chaves em todo bloco, nomes completos, dados no topo da classe, um `namespace` por arquivo, includes do projeto com aspas depois da biblioteca padrão. Não formatar à mão (hook `clang-format`).

### Decisões que o spec deixa em aberto (fixadas aqui)

- **Nomes:** classe `oly::app::Engine` (não `App`), para bater com o `Engine` do TS e o `OlyEngine` da API C. Config `EngineConfig { double fixedHz = 60.0; uint32_t maxStepsPerFrame = 5; }` (C++ e TS).
- **`Engine` não é dono do `World`:** `Engine(ecs::World& world, EngineConfig config = {})`. O `OlyEngine` da API C guarda um `OlyWorld` e a `Engine` sobre ele; `olyEngineWorld` devolve esse `OlyWorld*`, e o TS reaproveita `World`/`Query` sobre ele.
- **Um `CommandBuffer` por `Engine`**, aplicado (`world.flush`) ao fim de cada fase, inclusive `render`. É equivalente a um por fase e evita quatro buffers.
- **Relógio:** `time()` = nº de passos não pausados já concluídos × `fixedDeltaTime` (em `double`, sem soma acumulada). Durante um passo, `time()` é o tempo do início do passo.
- **Tolerância do acumulador:** roda um passo enquanto `accumulator >= fixedDeltaTime * (1 - 1e-9)`; depois de subtrair, se negativo vira 0. Assim `frame(k/60)` roda exatamente k passos.
- **Excedente:** após `maxStepsPerFrame` passos, se ainda couber um passo, o acumulador fica só com a fração (`fmod(accumulator, fixedDeltaTime)`).
- **Primeiro `frame`** (e o primeiro depois de `setHidden(false)` ou de uma recuperação) só registra o tempo: não roda passo nem `render`.
- **`paused()`** devolve o pedido mais recente; o efeito (`pausedThisStep_`) é aplicado no início de cada passo fixo.
- **`.whilePaused()` em sistema de `render`** é erro de contrato (`OLY_ASSERT` no C++, `ContractError` no TS).
- **Registrar sistema durante um `frame`** é erro de contrato. `frame` com tempo menor que o anterior também.
- **Passos 1, 2, 4, 5 do §4.3** viram funções privadas vazias da `Engine` (`captureActions`, `runDueSchedule`, `dispatchEvents`, `applyStateTransition`), chamadas na ordem do spec; `runDueSchedule` só é chamada sem pausa.
- **Exceção em sistema TS:** o JS desenrola o WASM sem destrutores e `inFrame_` fica `true`. O próximo `frame` vê isso e **recupera**: descarta os comandos pendentes (`commands_ = world.commands()`, que libera os índices reservados), zera o acumulador e se comporta como primeiro frame. O que os sistemas já aplicaram (fases concluídas, valores escritos) fica. No TS, `Engine.frame` usa `try/finally` para destravar o `World` e invalidar contextos.
- **Ponto de entrada do TS no C++:** função `EM_JS` `olyHostInvokeScript(OlyEngine*, uint32_t systemId)` que chama `Module["olyInvokeScript"](engine, systemId)`. O `Runtime` instala `module.olyInvokeScript` uma vez e despacha para a `Engine` TS pelo handle (várias engines por módulo). No nativo, `oly::bridge::setScriptInvoker` troca o invocador (testes); o padrão nativo dispara `OLY_ASSERT`.
- **`SystemContext` TS:** um objeto por sistema, válido só durante a chamada daquele sistema (fora dela, `ContractError`). Antes de chamar o sistema, o `Engine` TS faz `bridge.sync()` (um sistema C++ anterior pode ter crescido a memória).
- **Mudança estrutural direta no `World` TS durante `frame`** é `ContractError` ("use context.spawn/destroy/add/remove"). `Engine.world.dispose()` é `ContractError` (o `World` é da `Engine`).
- **Fora do escopo desta fase:** o armazenamento TS por `Entity` (§4.1), que a fase 1 adiou para cá. A limpeza dele precisa saber das destruições feitas pelo C++, e isso só faz sentido junto com eventos ou estados; fica para a fase 6 (escopo de estados) ou 7. `single(...)` e `inState` também ficam para as fases 6–7.
- **Host do navegador:** `runInBrowser(target, environment?)` pede o próximo rAF **antes** de chamar `frame`; uma exceção no `frame` sobe para o navegador (aparece no console) e o laço continua.

## Review Focus

1. **Relógio em múltiplos exatos de `1/60`** (`frame(k/60)` para k = 0…120): exatamente 120 passos, sem perder nem duplicar por erro de ponto flutuante (Task 1).
2. **Memória do WASM cresce num sistema C++ antes de um sistema TS** (flush de muitos `spawn`): o sistema TS seguinte lê e escreve na memória certa (Task 4).
3. **Exceção dentro de `eachBlock` num sistema TS:** chega a quem chamou `frame`; o `World` aceita mudança estrutural depois, `checkInvariants` limpo, entidade adiada da fase interrompida não existe, e os frames seguintes voltam a rodar passos (Task 5).
4. **Aba volta depois de muito tempo / `frame` com salto grande** (10 s): no máximo `maxStepsPerFrame` passos, excedente descartado, sem espiral (Task 1).
5. **`pause()` chamado por um sistema no meio do passo:** os sistemas seguintes do mesmo passo ainda rodam; o próximo passo já é pausado (Task 2).

---

### Task 1: Módulo `app` — fases, passo fixo, render e mudanças adiadas

**Files:**
- Create: `engine/runtime/app/CMakeLists.txt`, `engine/runtime/app/include/oly/app/{phase.hpp,system.hpp,engine.hpp}`, `engine/runtime/app/src/{engine.cpp,system.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,support/log.hpp,loop.cpp,systems.cpp}`
- Modify: `engine/runtime/CMakeLists.txt` (`add_subdirectory(app)` entre `ecs` e `bridge`)

**Interfaces:**
- Produces (namespace `oly::app`):
  - `phase.hpp`: `enum class Phase : uint8_t { UpdateBefore, Update, UpdateAfter, Render };` `inline constexpr uint32_t PhaseCount = 4;` `constexpr bool isGameplayPhase(Phase phase) noexcept;`
  - `system.hpp`: `using SystemId = uint32_t;` `class SystemContext;` `using SystemFunction = void (*)(SystemContext& context);`
    `SystemContext`: `Phase phase() const`, `SystemId systemId() const`, `std::string_view name() const`, `void* userData() const`, `float fixedDeltaTime() const`, `double time() const`, `float interpolation() const` (fração do passo, a mesma em todo o frame), `void pause()`, `void resume()`, `bool paused() const`; leitura: `template <class... Ts, class... Filters> ecs::Query<Ts...>& query(Filters...)`, `get<T>(Entity)`, `tryGet<T>(Entity)`, `has<T>(Entity)`, `isAlive(Entity)`; adiadas: `Entity spawn()`, `template <class... Ts> Entity spawn(Ts&&... components)`, `destroy(Entity)`, `template <class T> add(Entity, T&&)`, `template <class T> remove(Entity)`, `addById(Entity, ComponentId, void* value)`, `removeById(Entity, ComponentId)`.
    Registro por nome: `SystemFunction findNamedSystem(std::string_view name) noexcept` (nullptr se não existe); `struct NamedSystemRegistration { NamedSystemRegistration(std::string_view name, SystemFunction function); };` e a macro `OLY_SYSTEM(name)` (declara `void name(oly::app::SystemContext& context)` e um registro estático com o nome `#name`).
  - `engine.hpp`: `struct EngineConfig { double fixedHz = 60.0; uint32_t maxStepsPerFrame = 5; };` `class Engine` com `Engine(ecs::World& world, EngineConfig config = {})`, `ecs::World& world()`, `SystemId addSystem(Phase phase, std::string_view name, SystemFunction function, void* userData = nullptr)`, `void setWhilePaused(SystemId system, bool whilePaused)`, `void frame(double nowSeconds)`, `void setHidden(bool hidden)`, `void pause() noexcept`, `void resume() noexcept`, `bool paused() const noexcept`, `double time() const noexcept`, `double fixedDeltaTime() const noexcept`, `double interpolation() const noexcept`, `bool inFrame() const noexcept`.

- [ ] **Step 1: Branch** `git switch -c feat/engine-fase2-loop`.

- [ ] **Step 2: Testes que falham.** `oly_add_module(NAME app DEPENDS ecs core)`; `tests/CMakeLists.txt` com `oly_add_test(MODULE app NAME loop SOURCES loop.cpp SUPPORT engine/runtime/ecs/tests/support)` e o mesmo para `systems`. `support/log.hpp`: `struct Log { std::vector<std::string> calls; std::vector<float> interpolations; std::vector<double> times; };` e `void recordName(SystemContext& context)` (empurra `context.name()` no `Log*` de `userData`; em `Render` também empurra `interpolation()`; nas outras, `time()`). Todos os testes chamam `checkInvariants(world)` no fim.

`loop.cpp`:
```cpp
TEST_CASE("primeiro frame só registra o tempo") { /* Update "u", Render "r"; frame(10.0) */ CHECK(log.calls.empty()); /* frame(10.0 + 1.0/60) */ CHECK(log.calls == {"u","r"}); }
TEST_CASE("frame em múltiplos exatos de 1/60 roda um passo por frame") { /* frame(k / 60.0), k = 0..120 */ CHECK(count("u") == 120); CHECK(count("r") == 120); }
TEST_CASE("frações acumulam entre frames") { /* frame(0); frame(0.5/60) */ CHECK(count("u") == 0); CHECK(count("r") == 1); /* frame(1.0/60) */ CHECK(count("u") == 1); }
TEST_CASE("limite de passos por quadro descarta o excedente") { /* maxStepsPerFrame = 5; frame(0); frame(10.0) */ CHECK(count("u") == 5); /* frame(10.0 + 1.0/60) */ CHECK(count("u") == 6); }
TEST_CASE("render recebe a interpolação") { /* frame(0); frame(1.5/60) */ CHECK(log.interpolations.back() == doctest::Approx(0.5).epsilon(1e-6)); }
TEST_CASE("time é o início do passo e fixedDeltaTime vem do fixedHz") { /* fixedHz = 50; frame(0); frame(0.06) */ CHECK(log.times == {0.0, 0.02, 0.04}); /* contexto: fixedDeltaTime() == 0.02f */ CHECK(engine.time() == doctest::Approx(0.06)); }
```

`systems.cpp`:
```cpp
TEST_CASE("fases em ordem e, dentro da fase, ordem de registro") { /* registra Render r, Update u1, UpdateBefore b, UpdateAfter a, Update u2; um passo */ CHECK(log.calls == {"b","u1","u2","a","r"}); }
OLY_SYSTEM(appTestNamed) { recordName(context); }
TEST_CASE("OLY_SYSTEM registra por nome") { CHECK(findNamedSystem("appTestNamed") != nullptr); CHECK(findNamedSystem("naoExiste") == nullptr); }
TEST_CASE("spawn adiado: viva só na fase seguinte") { /* Update: e = context.spawn(Position{1, 2}); CHECK(!context.isAlive(e)); UpdateAfter: CHECK(context.isAlive(e)); CHECK(context.get<Position>(e).x == 1) */ }
TEST_CASE("comandos de render aplicados ao fim do frame") { /* Render: e = context.spawn(); depois do frame */ CHECK(world.isAlive(e)); }
TEST_CASE("destroy e add adiados na mesma fase resolvem como o CommandBuffer") { /* Update: add<Velocity>(e) e destroy(e) */ CHECK(!world.isAlive(e)); }
```
`Position`/`Velocity` vêm de `components.hpp` do suporte do ECS (ou definidos no teste, se lá não houver).

- [ ] **Step 3: Ver falhar** — `$DC make test T='^app\.'` → erro de compilação (headers não existem).

- [ ] **Step 4: Implementar.** `Engine` guarda `std::array<std::vector<SystemEntry>, PhaseCount>` com `SystemEntry { SystemFunction function; void* userData; std::string name; SystemId id; bool whilePaused; }` e um índice `SystemId → (fase, posição)`. `frame` segue a seção "Decisões" (tolerância, excedente, primeiro frame). Um passo fixo: aplica a pausa pedida; `captureActions()`; `runDueSchedule()` se não pausado; `UpdateBefore`, `Update`, `UpdateAfter` (cada uma roda os sistemas com `whilePaused == pausado` e depois `world.flush(commands_)`); conta o passo no relógio se não pausado; `dispatchEvents()`; `applyStateTransition()`. Depois dos passos, `interpolation_ = accumulator / dt` e a fase `Render` (todos os sistemas, depois flush). `SystemContext` é montado na pilha para cada chamada. `findNamedSystem` usa um `std::vector<std::pair<std::string, SystemFunction>>` estático dentro de uma função (ordem de inicialização estática segura). Nesta task, `pause`/`setHidden` podem existir só como estado; os testes deles são da Task 2.

- [ ] **Step 5: Ver passar** — `$DC make test` (tudo, incluindo `oly.lint.boundaries`) e `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 6: Commit** `feat: módulo app com loop de passo fixo, fases e mudanças adiadas`.

---

### Task 2: Pausa, aba oculta, `.whilePaused()` e recuperação

**Files:**
- Modify: `engine/runtime/app/src/engine.cpp`, `engine/runtime/app/tests/CMakeLists.txt`
- Create: `engine/runtime/app/tests/{pause.cpp,recovery.cpp,contract_death.cpp}`

**Interfaces:**
- Consumes: tudo da Task 1.
- Produces: comportamento de `pause`/`resume`/`setWhilePaused`/`setHidden` e a recuperação de frame interrompido, descritos em "Decisões".

- [ ] **Step 1: Testes que falham.** `pause.cpp` (Update `u`, Update `p` com `setWhilePaused(p, true)`, Render `r`, um passo por frame):
```cpp
TEST_CASE("pausado: só whilePaused roda, relógio congela, render continua") { /* pause(); 2 frames */ CHECK(log.calls == {"p","r","p","r"}); CHECK(engine.time() == 0.0); /* resume(); 1 frame */ CHECK(last == {"u","r"}); }
TEST_CASE("pause pedido por um sistema vale no próximo passo") { /* Update "pauser" chama context.pause(); UpdateAfter "a"; 1 passo */ CHECK(calls == {"pauser","a"}); CHECK(engine.paused()); /* 1 passo */ CHECK(nenhum de "pauser","a"); }
TEST_CASE("sistema whilePaused retoma pelo contexto") { /* p chama context.resume(); pause(); 2 passos */ CHECK(calls == {"p","u"}); }
TEST_CASE("aba oculta ignora frames e o primeiro depois só registra o tempo") { /* frame(0); setHidden(true); frame(1.0) */ CHECK(vazio); /* setHidden(false); frame(2.0) */ CHECK(vazio); /* frame(2.0 + 1.0/60) */ CHECK(calls == {"u","r"}); }
TEST_CASE("aba oculta não desfaz a pausa do jogo") { /* pause(); setHidden(true); setHidden(false) */ CHECK(engine.paused()); }
```
`recovery.cpp` (só fora do Emscripten: `#if !defined(__EMSCRIPTEN__)`, porque a exceção C++ atravessa a `Engine` compilada sem `-fwasm-exceptions`; o caso real, exceção JS, é testado na Task 5):
```cpp
TEST_CASE("frame interrompido por exceção se recupera") {
  // Update: e = context.spawn(); na 1ª chamada lança std::runtime_error
  // frame(0); CHECK_THROWS(engine.frame(1.0/60)); CHECK(!world.isAlive(e)); checkInvariants
  // frame(2.0/60) não roda passo; frame(3.0/60) roda 1 passo; checkInvariants
}
```
`contract_death.cpp` (mesmo protocolo de `engine/runtime/bridge/tests/contract_death.cpp`), registrado com `oly_add_contract_death(MODULE app SOURCE contract_death.cpp CASES ...)`:

| Caso | Ação | Substrings |
|---|---|---|
| `add_system_during_frame` | sistema chama `engine.addSystem` (engine via `userData`) | `addSystem`, `durante frame` |
| `while_paused_render` | `setWhilePaused` num sistema de `Render` | `whilePaused`, `render` |
| `frame_time_backwards` | `frame(1.0)` e depois `frame(0.5)` | `frame`, `tempo` |

- [ ] **Step 2: Ver falhar** — `$DC make test T='^app\.'` → falhas em `pause`/`recovery`, casos de morte "assert não disparou".

- [ ] **Step 3: Implementar** a pausa efetiva por passo, o `setHidden` (enquanto oculto, `frame` volta logo; ao mostrar, o próximo frame só registra o tempo), a recuperação no início de `frame` quando `inFrame_` já é `true`, e os três `OLY_ASSERT` com nome do sistema quando houver.

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: pausa, aba oculta, whilePaused e recuperação de frame interrompido`.

---

### Task 3: Engine na API C (`OlyEngine`) e chamada de volta para scripts

**Files:**
- Create: `engine/runtime/bridge/include/oly/bridge/{engine_api.hpp,engine.hpp}`, `engine/runtime/bridge/src/{handles.hpp,engine_api.cpp}`, `engine/runtime/bridge/tests/engine_api.cpp`
- Modify: `engine/runtime/bridge/CMakeLists.txt` (`DEPENDS app ecs core`), `engine/runtime/bridge/src/c_api.cpp` (mover `struct OlyWorld`/`OlyIteration` para `src/handles.hpp`), `engine/runtime/bridge/tests/{CMakeLists.txt,contract_death.cpp}`

**Interfaces:**
- Consumes: `oly::app::Engine`, `SystemContext`, `findNamedSystem`, `Phase`, `PhaseCount`.
- Produces:
  - `engine_api.hpp` (`extern "C"`, `OLY_API`): `struct OlyEngine;` `#define OLY_INVALID_SYSTEM 0xFFFFFFFFu`;
    `OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame)`, `void olyEngineDestroy(OlyEngine*)`, `OlyWorld* olyEngineWorld(OlyEngine*)`,
    `void olyEngineFrame(OlyEngine*, double nowSeconds)`, `void olyEngineSetHidden(OlyEngine*, int32_t hidden)`, `void olyEnginePause(OlyEngine*)`, `void olyEngineResume(OlyEngine*)`, `int32_t olyEngineIsPaused(OlyEngine*)`,
    `double olyEngineTime(OlyEngine*)`, `double olyEngineFixedDeltaTime(OlyEngine*)`, `double olyEngineInterpolation(OlyEngine*)`,
    `uint32_t olyEngineAddNativeSystem(OlyEngine*, uint32_t phase, const char* name)` (`OLY_INVALID_SYSTEM` se o nome não existe), `uint32_t olyEngineAddScriptSystem(OlyEngine*, uint32_t phase, const char* name)`, `void olyEngineSetWhilePaused(OlyEngine*, uint32_t systemId, int32_t whilePaused)`,
    `void olySystemSpawn(OlyEngine*, OlyEntity* out)`, `void olySystemDestroy(OlyEngine*, uint32_t index, uint32_t generation)`, `void olySystemAdd(OlyEngine*, uint32_t index, uint32_t generation, uint32_t componentId, void* value)`, `void olySystemRemove(OlyEngine*, uint32_t index, uint32_t generation, uint32_t componentId)`.
  - `engine.hpp` (C++): `oly::app::Engine& engineOf(OlyEngine*) noexcept;` `using ScriptInvoker = void (*)(OlyEngine* engine, uint32_t systemId);` `ScriptInvoker setScriptInvoker(ScriptInvoker invoker) noexcept;` (devolve o anterior).
  - Import JS (só Emscripten): `EM_JS(void, olyHostInvokeScript, (OlyEngine* engine, uint32_t systemId), { Module["olyInvokeScript"](engine, systemId); })` — invocador padrão no WASM.

- [ ] **Step 1: Testes que falham** em `tests/engine_api.cpp` (nativo e `wasm-debug`; registrar com `oly_add_test(MODULE bridge NAME engine_api SOURCES engine_api.cpp test_schema_registry.cpp SUPPORT engine/runtime/ecs/tests/support)`; um invocador de teste grava `(engine, systemId)` num vetor estático e pode rodar uma ação):
```cpp
TEST_CASE("sistema de script é chamado pelo invocador, na ordem com os nativos") { /* script "s" Update; OLY_SYSTEM(bridgeTestNative) grava "n"; ordem s, n, s → um passo */ CHECK(gravado == {"s","n","s"}); CHECK(ids batem com os devolvidos por olyEngineAddScriptSystem); }
TEST_CASE("nome nativo desconhecido devolve OLY_INVALID_SYSTEM") { CHECK(olyEngineAddNativeSystem(engine, 1, "naoExiste") == OLY_INVALID_SYSTEM); }
TEST_CASE("olySystemSpawn e olySystemAdd dentro do script são aplicados ao fim da fase") { /* invocador: olySystemSpawn + olySystemAdd(A{7}); depois do frame */ CHECK(olyEntityIsAlive(...)); CHECK(get A == 7); invariantes }
TEST_CASE("duas engines no mesmo processo: o invocador recebe a engine certa") { /* frame só na 2ª */ CHECK(todos os registros com engine == segunda); }
TEST_CASE("olyEngineWorld serve à API de World existente") { /* olyEntitySpawn(olyEngineWorld(engine)) */ CHECK(olyEntityIsAlive(...)); }
```
`contract_death.cpp`: casos `system_spawn_outside_system` (`olySystemSpawn` sem frame; substrings `olySystemSpawn`, `fora de sistema`) e `engine_phase_out_of_range` (`olyEngineAddScriptSystem(engine, 4, "x")`; `phase`).

- [ ] **Step 2: Ver falhar** — `$DC make test T='^bridge\.'` → erro de compilação.

- [ ] **Step 3: Implementar.** `struct OlyEngine { OlyWorld world; oly::app::Engine engine; oly::app::SystemContext* currentContext = nullptr; std::vector<std::unique_ptr<ScriptSystem>> scripts; };` (nessa ordem: `engine` referencia `world.world`). Um sistema de script é registrado com uma função do bridge e `userData` = `OlyEngine*`; ela guarda `currentContext`, chama o invocador com `context.systemId()` e limpa `currentContext`. `olyEngineFrame` zera `currentContext` antes de chamar `frame` (após uma exceção JS ele ficou apontando para a pilha antiga). `olySystem*` exigem `currentContext != nullptr` e delegam para `spawn`/`destroy`/`addById`/`removeById` do contexto. `phase < PhaseCount` é `OLY_ASSERT`.

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: Engine na API C com sistemas nativos por nome e chamada de volta para scripts`.

---

### Task 4: `Engine` e `SystemContext` no `@oly/core`

**Files:**
- Create: `engine/packages/core/src/{engine.ts,system_context.ts}`, `engine/testing/tests/wasm/engine.test.ts`
- Modify: `engine/packages/core/src/{module.ts,bridge.ts,runtime.ts,world.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/{schema.ts,systems.cpp}`, `engine/testing/tests/wasm/fixture.ts`, `engine/testing/CMakeLists.txt` (`DEPENDS ecs app bridge`)

**Interfaces:**
- Consumes: a API C da Task 3.
- Produces (`@oly/core`):
  - `export const Phase = { UpdateBefore: 0, Update: 1, UpdateAfter: 2, Render: 3 } as const; export type Phase = (typeof Phase)[keyof typeof Phase];`
  - `interface EngineConfig { fixedHz?: number; maxStepsPerFrame?: number }`; `Runtime.createEngine(config?: EngineConfig): Engine`.
  - `class Engine`: `readonly world: World`, `frame(nowSeconds: number): void`, `setHidden(hidden: boolean): void`, `pause(): void`, `resume(): void`, `get paused(): boolean`, `get time(): number`, `get fixedDeltaTime(): number`, `addSystem(phase: Phase, name: string, system: SystemFunction): SystemRegistration`, `addNativeSystem(phase: Phase, name: string): SystemRegistration`, `dispose(): void`.
  - `type SystemFunction = (context: SystemContext) => void`; `interface SystemRegistration { readonly id: number; whilePaused(): SystemRegistration }`.
  - `class SystemContext`: `readonly phase: Phase`, `readonly name: string`, `get fixedDeltaTime()`, `get time()`, `get interpolation()`, `get paused()`, `pause()`, `resume()`, `query(spec)`, `get`, `set`, `has`, `isAlive` (delegam ao `World` da engine), e adiadas `spawn(types, values?) → Entity`, `destroy(entity)`, `add(entity, type, value?)`, `remove(entity, type)` (mesmas validações e mensagens do `World.spawn/add`).
  - `OlyModule`: os exports `_olyEngine*`/`_olySystem*` e `olyInvokeScript?: (engine: number, systemId: number) => void`.
  - Interno (`world.ts`, não exportado em `internal.ts`): `setStructureLocked(world, locked: boolean)`, e `World` criado pela engine com `owned = false`.
- Fixture: `Trace = component("Trace", { value: "f64" })` no esquema; em `systems.cpp`, `OLY_SYSTEM(noop) {}`, `OLY_SYSTEM(move)` (o `move` do §4.8, com `context.fixedDeltaTime()`), `OLY_SYSTEM(appendNine)` (`trace.value = trace.value * 10 + 9` em toda entidade com `Trace`) e `OLY_SYSTEM(spawnBatch)` (`context.spawn(Position{1, 0})` 400 000 vezes; grande o bastante para o heap crescer).

- [ ] **Step 1: Testes que falham** em `engine/testing/tests/wasm/engine.test.ts` (`fixedHz: 60`; helper `step(engine)` que chama `frame` com o próximo múltiplo de `1/60`; `checkInvariants() === null` no fim de cada teste):
```ts
test("sistemas TS e C++ na ordem das fases e de registro", ...)   // Trace {0}; UpdateBefore TS ×10+1, Update TS ×10+2, Update nativo appendNine, Update TS ×10+3; 1 passo → trace.value === 1293
test("frame(k/60) para k = 0..60 roda 60 passos", ...)
test("context.spawn é adiado e o C++ vê o valor", ...)           // Update: e = context.spawn([Position], {Position:{x:5}}); isAlive(e) === false; UpdateAfter: context.get(e, Position).x === 5; depois fixtureSumPositionX(world.handle) === 5
test("memória cresce num sistema C++ antes de um sistema TS", ...) // Update nativo spawnBatch; UpdateAfter TS soma x por eachBlock → 400000; HEAPU8.buffer mudou durante o frame
test("mudança estrutural direta no World durante frame é ContractError", ...) // dentro do sistema: engine.world.spawn lança ContractError com "context.spawn"; fora do frame funciona
test("contexto usado fora da chamada é ContractError", ...)
test("addNativeSystem com nome desconhecido é ContractError com o nome", ...)
test("addSystem durante frame é ContractError", ...)
test("duas engines no mesmo módulo recebem só os próprios sistemas", ...)
test("engine.world.dispose() é ContractError; frame depois de engine.dispose() é ContractError", ...)
```
Atualizar `surface.test.ts`: `Runtime` = `["createEngine", "createWorld", "schema"]`; novo teste com os membros de `Engine` e de `SystemContext` exatamente como em Produces; `public-exports.txt` ganha `Engine`, `EngineConfig`, `Phase`, `SystemContext`, `SystemFunction`, `SystemRegistration`.

- [ ] **Step 2: Ver falhar** — `$WC make games-test` → erro de tipos no `tsc` (`createEngine` não existe).

- [ ] **Step 3: Implementar.** `Bridge` ganha um método por export novo (os que podem alocar — `engineCreate`, `engineFrame`, `addNativeSystem`, `addScriptSystem`, `systemSpawn`, `systemAdd` — terminam em `sync()`) e `#writeString(text)` que grava UTF-8 + NUL num buffer de rascunho que cresce. O `Runtime` instala `module.olyInvokeScript` no construtor e mantém `Map<number, Engine>`; a chamada vai para `invoke(systemId)` interno da `Engine`, que faz `bridge.sync()`, ativa o `SystemContext` do sistema (array indexado por `systemId`), chama a função e desativa em `finally`. `Engine.frame` valida (`Number.isFinite`, tempo não volta, não reentrante, não liberada), trava o `World` e chama `olyEngineFrame` dentro de `try/finally`. `addSystem`/`addNativeSystem` dentro de `frame` são `ContractError`.

- [ ] **Step 4: Ver passar** — `$WC make games-test` (inclui `ts.core.unit`, `ts.testing.wasm`, `bridge.*`, `app.*`) e `$DC make test`.

- [ ] **Step 5: Commit** `feat: Engine e SystemContext no @oly/core com sistemas TS chamados pelo núcleo`.

---

### Task 5: Pausa, aba oculta e exceções vistas do TS

**Files:**
- Create: `engine/testing/tests/wasm/engine_pause.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,system_context.ts}` (só se os testes acharem lacunas)

**Interfaces:**
- Consumes: `Engine`, `SystemContext`, `SystemRegistration.whilePaused()` da Task 4.

- [ ] **Step 1: Testes** (podem já passar em parte; os que falharem guiam o Step 3):
```ts
test("pausado: só whilePaused roda, time congela, render continua", ...)
test("sistema whilePaused chama context.resume() e o próximo passo roda o gameplay", ...)
test("whilePaused() num sistema de Render é ContractError", ...)
test("setHidden(true) ignora frames; depois de setHidden(false) o 1º frame só registra o tempo", ...)
test("exceção dentro de eachBlock num sistema TS chega a quem chamou frame e o frame seguinte se recupera", ...)
  // Update: e = context.spawn([Position]); query.eachBlock(() => { throw new Error("boom") }) só na 1ª chamada
  // assert.throws(() => step(engine), /boom/); engine.world.checkInvariants() === null; engine.world.isAlive(e) === false
  // engine.world.spawn([Position]) funciona (World destravado); próximo frame: 0 passos; o seguinte: 1 passo
test("frame chamado de dentro de um sistema é ContractError", ...)
test("frame com tempo menor que o anterior ou NaN é ContractError", ...)
```

- [ ] **Step 2: Rodar** — `$WC make games-test T='^ts\.testing'` e anotar o que falha.

- [ ] **Step 3: Corrigir** o que falhou, sempre no lugar certo: regra de loop no C++ (`engine.cpp`), validação e destrave no TS.

- [ ] **Step 4: Ver passar** — `$WC make games-test` e, em Release, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (sem asserts, só as validações TS seguram os erros).

- [ ] **Step 5: Commit** `test: pausa, aba oculta e recuperação de exceção da Engine vistas do TS`.

---

### Task 6: Host do navegador em `@oly/client`

**Files:**
- Create: `engine/packages/client/src/browser_loop.ts`, `engine/packages/client/tests/browser_loop.test.ts`
- Modify: `engine/packages/client/src/index.ts`

**Interfaces:**
- Produces: `interface FrameTarget { frame(nowSeconds: number): void; setHidden(hidden: boolean): void }`; `interface BrowserEnvironment { requestAnimationFrame(callback: (timestampMilliseconds: number) => void): number; cancelAnimationFrame(handle: number): void; readonly document: { readonly hidden: boolean; addEventListener(type: "visibilitychange", listener: () => void): void; removeEventListener(type: "visibilitychange", listener: () => void): void } }`; `interface BrowserLoop { stop(): void }`; `function runInBrowser(target: FrameTarget, environment: BrowserEnvironment = globalThis-based): BrowserLoop`. `Engine` do `@oly/core` satisfaz `FrameTarget` por tipo estrutural.

- [ ] **Step 1: Testes que falham** (`node:test`, ambiente falso que guarda os callbacks de rAF e o listener):
```ts
test("cada rAF chama frame com o timestamp em segundos", ...)        // rAF(1500) → frame(1.5)
test("visibilitychange repassa setHidden com document.hidden", ...)
test("documento já oculto no início chama setHidden(true)", ...)
test("stop cancela o rAF pendente e remove o listener", ...)
test("exceção em frame não para o laço: o próximo rAF já foi pedido", ...)
```

- [ ] **Step 2: Ver falhar** — `$WC make games-test T='^ts\.client'` → erro de tipos (`runInBrowser` não existe).

- [ ] **Step 3: Implementar** `runInBrowser`: pede o próximo rAF antes de chamar `frame(timestamp / 1000)`.

- [ ] **Step 4: Ver passar** — `$WC make games-test`.

- [ ] **Step 5: Commit** `feat: runInBrowser no @oly/client com requestAnimationFrame e visibilitychange`.

---

### Task 7: Custo do loop (§7), resultados e documentação

**Files:**
- Create: `engine/testing/bench/common.ts`, `engine/testing/bench/loop.ts`, `docs/benchmarks/bridge/loop_fase2.json` (gerado)
- Modify: `engine/testing/bench/boundary.ts` (usa `common.ts`), `docs/benchmarks/bridge/README.md`, `docs/benchmarks/README.md`, `CLAUDE.md`

**Interfaces:**
- Consumes: `Runtime.createEngine`, `Engine.addSystem/addNativeSystem/frame`, sistemas `noop` e `move` do fixture, `fixtureMove`.
- Produces: `common.ts` com `parseOptions`, `loadBenchRuntime(modulePath)`, `median`, `rotated`, `expectedPositionX` (movidos de `boundary.ts`, sem mudar o resultado dele).

- [ ] **Step 1: `loop.ts`** — mesmo protocolo do `boundary.ts` (aquecimento, rodadas intercaladas com ordem girando, mediana), com um passo por `frame` (`frame(k * fixedDeltaTime)`), em ns/passo:
  1. K ∈ {1, 4, 16, 64} sistemas vazios: `cpp` (K × `addNativeSystem("noop")`), `ts` (K × sistema TS vazio) e `zero` (nenhum sistema, custo fixo do `frame`); razões `ts/cpp` e custo por chamada de volta `(ts − cpp) / K`.
  2. K ∈ {4, 16, 64}, K/2 + K/2 na mesma fase: `alternado` (`C,T,C,T…`) e `agrupado` (`C…C,T…T`); razão `alternado/agrupado`.
  3. Fronteira dentro do loop, N ∈ {1000, 10000, 100000, 1000000}, ns/entidade: `cpp-loop` (`move` nativo), `ts-loop` (sistema TS com `eachBlock` + views) e `ts-manual` (o mesmo de `boundary.ts`); razões `ts-loop/ts-manual` e `ts-loop/cpp-loop`. Conferir `x` final como o `boundary.ts` (divergência → exit 1).
  Saída JSON `{ meta, emptySystems, interleaving, boundaryInLoop }` com `meta` igual ao do `boundary.ts`.

- [ ] **Step 2: Verificar que o `boundary.ts` refatorado ainda roda** — `$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'` e `$WC node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs --out /tmp/b.json --sizes 1000 --rounds 2` → tabela de razões impressa, exit 0.

- [ ] **Step 3: Medir** — `$WC node engine/testing/dist/bench/loop.js --module build/wasm-games-release/games/fixture_game.mjs --out docs/benchmarks/bridge/loop_fase2.json --commit $(git rev-parse --short HEAD)` → exit 0 e três tabelas impressas.

- [ ] **Step 4: Registrar** — seção "Custo do loop — fase 2" em `docs/benchmarks/bridge/README.md` (comando, ambiente, protocolo, tabelas, leitura em poucas linhas: custo por chamada de volta, se intercalar custa mais que o nº de chamadas explica, e se vale agrupar sistemas TS) e uma linha em `docs/benchmarks/README.md`. Sem meta numérica (§7).

- [ ] **Step 5: `CLAUDE.md`** — árvore com `app/`; dependências `core ← ecs ← app ← bridge ← jogos`; parágrafo curto sobre a `Engine` (loop no núcleo, `CommandBuffer` por fase escondido atrás de `context.spawn/...`, chamada de volta única, recuperação); linhas do glossário para `frame`, `setHidden`, `pause`/`resume`, `addSystem`/`addNativeSystem`/`OLY_SYSTEM`, `whilePaused`, e as operações adiadas (`SystemContext::spawn` / `olySystemSpawn` / `context.spawn`); `app.*` na lista de prefixos do ctest; `runInBrowser` na linha do `@oly/client`.

- [ ] **Step 6: Verificação final** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`: tudo verde.

- [ ] **Step 7: Commit** `bench: custo do loop da fase 2 e documentação da Engine`.
