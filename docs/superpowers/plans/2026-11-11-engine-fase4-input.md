# Engine — Fase 4 (Ações de input) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ações de input no núcleo C++ (módulo `input` com `ActionMap`, botões físicos HID, fila de bordas e captura no passo 1), ligadas à `Engine`, declaradas no esquema, expostas na API C, lidas pelos contextos em C++ e TS sem cruzar a fronteira, e entregues pelo navegador com `attachBrowserInput`.

**Architecture:** Um módulo novo `oly::input` (`DEPENDS core`) guarda declarações, ligações, estado físico, fila de bordas e o snapshot (`ActionState[]`, 12 B por ação). Ele não conhece ECS nem `Engine`. O `app` passa a depender de `input`: a `Engine` guarda um `ActionMap`, chama `capture()` no passo 1 de todo passo fixo, `releaseAll()` em `setHidden(true)` e trava declarações durante `frame`. O `bridge` declara as ações do esquema na criação da `OlyEngine` e expõe ligações, entrada do host e o endereço do snapshot. No TS, `action()` entra no esquema, `ActionMap` (mesmo objeto em `engine.actions` e `context.actions`) valida e repassa, e lê o snapshot por views da `Bridge`. `@oly/client` ganha `attachBrowserInput`.

**Tech Stack:** C++20, GCC 14 (nativo), Emscripten 3.1.74 + node 20.18 (serviço `wasm`), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (fase 4 da §5; §4.5 inteira, §4.3 para a ordem do passo e o host, §4.1 para o esquema, §6, §7 "Ações de input"). `§N` abaixo são desse spec.

## Global Constraints

- Branch `feat/engine-fase4-input` a partir de `develop`. Commits, comentários, mensagens de assert e de erro em pt-BR; commits terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain só em Docker: `DC="docker compose run --rm dev"` (nativo, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). Presets `wasm-games*` exigem `make ts` antes.
- `-Wall -Wextra -Wpedantic -Werror` em todo alvo C++; TS `strict` (§3).
- Camadas: `core ← input ← app ← bridge ← jogos` e `core ← ecs ← app`. `input` não inclui nada de `oly/ecs` nem de `oly/app` (o `oly.lint.boundaries` reprova). O ECS não muda nesta fase.
- Engine completa em C++ (§2): bordas, auto-repeat, captura, eixo, ponteiro, religação e soltura vivem em `oly::input::ActionMap`. O TS só valida uso, traduz nomes W3C em usages e repassa.
- Fronteira: entrada do host cruza uma vez por evento de input; a captura não cruza; ler uma ação no TS não cruza (views do snapshot).
- Nada de conceito de jogo em `engine/runtime` nem em `engine/packages`; nomes de jogo só em `engine/testing/fixture/` e nos testes.
- Sem benchmark nesta fase (§7).
- Glossário (`CLAUDE.md`): mesmo verbo nas três camadas — `declareButton`/`declareAxis2d`/`declarePointer` (C++; no TS, `action(name, kind)` no esquema), `bind` (C `olyEngineBindButton`/`olyEngineBindAxis2d`), `clearBindings`, `isBound`, `buttonDown`, `buttonUp`, `pointerMove`, `releaseAll`, `button`/`axis2d`/`pointer` (leitura). `oly` só na API C.
- Estilo `CLAUDE.md`: chaves em todo bloco, nomes completos, dados no topo da classe, um `namespace` por arquivo, includes do projeto com aspas depois da biblioteca padrão. Não formatar à mão (hook `clang-format`). Comentário só para razão, restrição externa ou contrato não óbvio.

### Decisões que o spec deixa em aberto (fixadas aqui)

- **`PhysicalButton`** é `struct PhysicalButton { uint32_t usage; }` com construtores `constexpr` implícitos a partir de `Key` e `MouseButton`, um `explicit` a partir de `uint32_t` (API C) e `operator==` padrão. Um `enum class` não converte sozinho; o struct é o que faz "convertidos implicitamente em `PhysicalButton`" (§4.5) compilar, e `Axis2dBinding{Key::W, Key::S, Key::A, Key::D}` funcionar.
- **Teclas da tabela** (C++ `Key` e TS `buttonUsages`, página `0x07`): `KeyA`–`KeyZ` (0x04–0x1D), `Digit1`–`Digit9`, `Digit0` (0x1E–0x27), `Enter`, `Escape`, `Backspace`, `Tab`, `Space` (0x28–0x2C), `Minus`, `Equal`, `BracketLeft`, `BracketRight`, `Backslash` (0x2D–0x31), `Semicolon`, `Quote`, `Backquote`, `Comma`, `Period`, `Slash`, `CapsLock` (0x33–0x39), `F1`–`F12` (0x3A–0x45), `PrintScreen`, `ScrollLock`, `Pause`, `Insert`, `Home`, `PageUp`, `Delete`, `End`, `PageDown`, `ArrowRight`, `ArrowLeft`, `ArrowDown`, `ArrowUp` (0x46–0x52), `NumLock`, `NumpadDivide`, `NumpadMultiply`, `NumpadSubtract`, `NumpadAdd`, `NumpadEnter`, `Numpad1`–`Numpad9`, `Numpad0`, `NumpadDecimal` (0x53–0x63), `IntlBackslash`, `ContextMenu` (0x64–0x65), `ControlLeft`, `ShiftLeft`, `AltLeft`, `MetaLeft`, `ControlRight`, `ShiftRight`, `AltRight`, `MetaRight` (0xE0–0xE7). Mouse (página `0x09`): `MouseLeft` 1, `MouseRight` 2, `MouseMiddle` 3. O nome C++ é o nome W3C sem o prefixo `Key` das letras (`Key::W`, `Key::Digit1`, `Key::ArrowUp`); `MouseButton::Left/Right/Middle`.
- **Amostras da tabela** (as mesmas nos testes C++ e TS): `KeyA` 0x0007'0004, `KeyW` 0x0007'001A, `KeyZ` 0x0007'001D, `Digit1` 0x0007'001E, `Digit0` 0x0007'0027, `Enter` 0x0007'0028, `Escape` 0x0007'0029, `Space` 0x0007'002C, `F1` 0x0007'003A, `F12` 0x0007'0045, `ArrowRight` 0x0007'004F, `ArrowLeft` 0x0007'0050, `ArrowDown` 0x0007'0051, `ArrowUp` 0x0007'0052, `ControlLeft` 0x0007'00E0, `ShiftLeft` 0x0007'00E1, `AltLeft` 0x0007'00E2, `MetaLeft` 0x0007'00E3, `ShiftRight` 0x0007'00E5, `MouseLeft` 0x0009'0001, `MouseRight` 0x0009'0002, `MouseMiddle` 0x0009'0003.
- **Estado físico em dois tempos.** `hostPressed_` muda na hora da chamada do host e decide o que entra na fila: `buttonDown` de botão já em `hostPressed_` é ignorado (auto-repeat) e `buttonUp` de botão fora de `hostPressed_` também (keyup que chega depois de um `releaseAll` do `blur`). `capturedPressed_` é o estado que a captura reproduz; no fim de toda captura, os dois são iguais. Os dois são `std::vector<PhysicalButton>` com busca linear: são poucos botões pressionados ao mesmo tempo.
- **Captura** (determina `pressed`/`released` com várias ligações; o resto sai dos testes):
  ```
  para cada ação de botão: held = algum botão ligado está em capturedPressed_; pressed = released = false
  para cada borda da fila, na ordem: aplica a borda em capturedPressed_;
    para cada ação de botão ligada a esse botão: agora = algum botão ligado em capturedPressed_;
      se !held && agora: pressed = true;  se held && !agora: released = true;  held = agora
  limpa a fila; eixos e ponteiros saem do estado final
  ```
  Recalcular `held` do início a partir de `capturedPressed_` é o que faz a religação valer na próxima captura sem bordas fantasmas: ligar `Space` a uma ação com `Space` já pressionado dá `held` sem `pressed`; tirar a ligação dá `held` falso sem `released`.
- **Ligações:** ligar de novo o mesmo botão (ou o mesmo `Axis2dBinding`) à mesma ação não duplica. Um botão pode estar ligado a várias ações; todas reagem. Eixo com várias ligações: cada direção é o OU entre as ligações (W ou ↑ = cima). `clearBindings` de uma ação de ponteiro não faz nada.
- **Snapshot:** `ActionState { float x; float y; uint32_t flags; }`, bits `ActionPressed = 1u << 0`, `ActionHeld = 1u << 1`, `ActionReleased = 1u << 2`, `ActionValid = 1u << 3`. Botão: `x = y = 0`. Eixo: `flags = 0`. Ponteiro: última posição e `ActionValid` depois do primeiro `pointerMove`; todas as ações de ponteiro recebem a mesma fonte. Antes da primeira captura, tudo zero. `snapshot()` vale até a próxima declaração; na `OlyEngine` todas as ações são declaradas na criação, então o ponteiro da API C é estável até `olyEngineDestroy`.
- **Declaração durante `frame`:** o `ActionMap` tem `setDeclarationsLocked(bool)`; a `Engine` trava ao entrar em `frame` (junto com `inFrame_ = true`) e destrava ao sair e em `recoverFromInterruptedFrame`. A mensagem do assert diz "durante frame".
- **`ActionMap::declare(ActionKind, name)`** é público (o `bridge` o usa com o tipo vindo do esquema); `declareButton/declareAxis2d/declarePointer` delegam para ele e devolvem o handle tipado.
- **Ponteiro não finito:** `pointerMove` com `x` ou `y` não finito é `OLY_ASSERT` no C++ e `ContractError` no TS.
- **Nome de ação no esquema:** camelCase (`/^[a-z][A-Za-z0-9]*$/`) e fora das palavras reservadas do C++ (a mesma lista dos campos), para `actions::<nome>` compilar. O namespace `actions` só é gerado se houver ações; o header gerado só inclui `oly/input/action_map.hpp` nesse caso.
- **Hash:** a forma canônica de hoje seguida de `@nome:tipo;` por ação. Esquema sem ações mantém o hash atual.
- **Tipo da ação na API C:** `OLY_ACTION_BUTTON 0u`, `OLY_ACTION_AXIS2D 1u`, `OLY_ACTION_POINTER 2u` = `static_cast<uint32_t>(ActionKind)`.
- **TS:** um único `ActionMap` por `Engine`, exposto como `engine.actions` e `context.actions` (o mesmo objeto, como no C++). O endereço do snapshot é lido uma vez na criação da `Engine`.
- **`attachBrowserInput`:** `pointerdown` manda `pointerMove` antes de `buttonDown` (o clique já sai com a mira certa); `contextmenu` no canvas recebe `preventDefault` se `MouseRight` estiver ligado (o menu do botão direito vem desse evento, não do `pointerdown`); `PointerEvent.button` 0 → `MouseLeft`, 1 → `MouseMiddle`, 2 → `MouseRight`, outros ignorados; canvas com `rect.width` ou `rect.height` 0 ignora o evento de ponteiro.
- **Fora do escopo:** gamepad, ponteiro com ligação, `Engine.on`/estados restritos a ações, benchmark.

## Review Focus

1. **Canvas com tamanho 0 na tela** (`display: none`, aba recém-aberta) recebendo `pointermove`: a conversão divide por zero; o esperado é ignorar o evento, sem exceção no listener e sem `NaN` no snapshot (Task 6).
2. **`keyup` real chegando depois do `releaseAll` do `blur`** (o jogador segura W, troca de janela, solta W lá fora e volta): só um `released`, sem borda nova nem assert (Task 1).
3. **Mesmo botão em duas ações e ligação repetida** (`Space` em `jump` e em `confirm`; `bind(jump, Space)` duas vezes): as duas ações reagem; a repetida não gera dois `pressed` nem impede o `released` (Task 1).
4. **Religar com o botão já pressionado** (menu de teclas): nem `pressed` nem `released` fantasma na próxima captura; `held` reflete a ligação nova (Task 1, e do TS na Task 5).
5. **Memória do WASM cresce antes de um sistema TS ler ações** (sistema nativo que cria muitas entidades antes): a leitura do snapshot continua certa depois do `sync()` (Task 5).

---

### Task 1: Módulo `input` — botões físicos, `ActionMap` e captura

**Files:**
- Create: `engine/runtime/input/CMakeLists.txt`, `engine/runtime/input/include/oly/input/{physical_button.hpp,action_map.hpp}`, `engine/runtime/input/src/action_map.cpp`, `engine/runtime/input/tests/{CMakeLists.txt,physical_button.cpp,action_map.cpp,contract_death.cpp}`
- Modify: `engine/runtime/CMakeLists.txt` (`add_subdirectory(input)` depois de `core`, antes de `ecs`)

**Interfaces:**
- Consumes: `OLY_ASSERT` (`oly/core/assert.hpp`).
- Produces (namespace `oly::input`):
  - `physical_button.hpp`: `enum class Key : uint32_t` e `enum class MouseButton : uint32_t` com a tabela de "Decisões"; `struct PhysicalButton` como em "Decisões".
  - `action_map.hpp`: `using ActionId = uint32_t;` `enum class ActionKind : uint8_t { Button, Axis2d, Pointer };` `struct ButtonAction { ActionId id; };` `struct Axis2dAction { ActionId id; };` `struct PointerAction { ActionId id; };` `struct ButtonState { bool pressed; bool held; bool released; };` `struct Vector2 { float x; float y; };` `struct PointerState { float x; float y; bool valid; };` `struct Axis2dBinding { PhysicalButton up, down, left, right; };` `struct ActionState { float x; float y; uint32_t flags; };` (`static_assert` de 12 B e offsets 0/4/8) e as constantes `ActionPressed/ActionHeld/ActionReleased/ActionValid`.
  - `class ActionMap` com os métodos do §4.5 (`declareButton`, `declareAxis2d`, `declarePointer`, os dois `bind`, `clearBindings`, `isBound`, `buttonDown`, `buttonUp`, `pointerMove`, `releaseAll`, `capture`, `button`, `axis2d`, `pointer`, `snapshot`) mais `ActionId declare(ActionKind kind, std::string_view name)`, `uint32_t actionCount() const noexcept`, `ActionKind kind(ActionId action) const`, `std::string_view name(ActionId action) const`, `void setDeclarationsLocked(bool locked) noexcept`.
  - Contratos (`OLY_ASSERT`, mensagem com a operação e o nome da ação quando houver): nome repetido (`já declarada`), declarar travado (`durante frame`), `ActionId` fora do intervalo (`fora do intervalo`), handle de outro tipo (`tipo`), usage `0` (`usage 0`), ponteiro não finito (`não finito`).

- [ ] **Step 1: Branch** `git switch -c feat/engine-fase4-input`.

- [ ] **Step 2: Testes que falham.** `tests/CMakeLists.txt`: `oly_add_test(MODULE input NAME unit SOURCES physical_button.cpp action_map.cpp)` e `oly_add_contract_death(MODULE input SOURCE contract_death.cpp CASES ...)`. `CMakeLists.txt` do módulo: `oly_add_module(NAME input DEPENDS core)` + `add_subdirectory(tests)` sob `OLY_BUILD_TESTS`, como o `app`. Sem `Engine` nem `World`: cada caso declara, liga, entrega input e chama `capture()`.
```cpp
TEST_CASE("amostras da tabela HID") { /* cada amostra de "Decisões" */ CHECK(PhysicalButton(Key::W).usage == 0x0007'001Au); CHECK(PhysicalButton(MouseButton::Left).usage == 0x0009'0001u); }
TEST_CASE("pressed no passo da borda, held enquanto pressionado, released ao soltar") { /* down; capture → {1,1,0}; capture → {0,1,0}; up; capture → {0,0,1}; capture → {0,0,0} */ }
TEST_CASE("toque mais curto que um passo não se perde") { /* down, up; capture */ CHECK(state == ButtonState{true, false, true}); /* capture */ CHECK(state == ButtonState{false, false, false}); }
TEST_CASE("fila vazia mantém o estado") { /* down; capture; capture; capture */ CHECK(held && !pressed); }
TEST_CASE("auto-repeat é ignorado") { /* down, down, down; capture */ CHECK(pressed); /* up; capture */ CHECK(released && !held); }
TEST_CASE("duas teclas na mesma ação: soltar uma não gera released") { /* bind Space e ShiftLeft; down Space, down Shift; capture; up Space; capture */ CHECK(!released && held); /* up Shift; capture */ CHECK(released); }
TEST_CASE("mesmo botão em duas ações: as duas reagem") { CHECK(jump.pressed && confirm.pressed); }
TEST_CASE("ligação repetida não duplica") { /* bind(jump, Space) 2×; down, up; capture */ CHECK(state == ButtonState{true, false, true}); }
TEST_CASE("releaseAll solta tudo e o keyup atrasado é ignorado") { /* down W; capture; releaseAll; up W; capture */ CHECK(released); /* capture */ CHECK(!released && !held); }
TEST_CASE("eixo: WASD, anulação e diagonal normalizada") { /* W → (0,-1); W+S → (0,0); W+D → (0.70710677f, -0.70710677f) com doctest::Approx */ }
TEST_CASE("eixo com duas ligações: cada direção é OU") { /* WASD e setas; A e ArrowRight → (0,0); W e ArrowUp → (0,-1) */ }
TEST_CASE("ponteiro: inválido até o primeiro pointerMove, depois a última posição") { /* capture → valid false; move(1,2), move(3,4); capture → {3,4,true} */ }
TEST_CASE("religar com o botão pressionado não gera bordas") { /* down Space sem ligação; capture; bind(jump, Space); capture */ CHECK(state == ButtonState{false, true, false}); /* clearBindings; capture */ CHECK(state == ButtonState{false, false, false}); }
TEST_CASE("religação vale só a partir da próxima captura") { /* bind e down antes da 1ª captura; button() antes de capture */ CHECK(!pressed); }
TEST_CASE("isBound vale para botão e eixo") { CHECK(isBound(Key::Space)); CHECK(isBound(Key::D)); CHECK(!isBound(Key::Q)); }
TEST_CASE("snapshot: um ActionState por ação na ordem de declaração com os bits") { /* botão pressionado e ponteiro válido */ CHECK(snapshot[0].flags == (ActionPressed | ActionHeld)); CHECK(snapshot[2].flags == ActionValid); }
```
`contract_death.cpp` (mesmo protocolo do `app`):

| Caso | Ação | Substrings |
|---|---|---|
| `declare_duplicate_name` | `declareButton("jump")` duas vezes | `declare`, `jump`, `já declarada` |
| `declare_while_locked` | `setDeclarationsLocked(true)`; `declareButton("jump")` | `declare`, `durante frame` |
| `bind_wrong_kind` | `ActionId` de eixo usado como `ButtonAction` em `bind` | `bind`, `tipo` |
| `action_out_of_range` | `clearBindings(7)` com 1 ação | `clearBindings`, `fora do intervalo` |
| `button_usage_zero` | `buttonDown(PhysicalButton(0u))` | `buttonDown`, `usage 0` |
| `pointer_not_finite` | `pointerMove(NAN, 0)` | `pointerMove`, `não finito` |

- [ ] **Step 3: Ver falhar** — `$DC make test T='^input\.'` → erro de configuração/compilação (`oly/input/action_map.hpp` não existe).

- [ ] **Step 4: Implementar** `ActionMap` conforme "Decisões" (fila de bordas `std::vector<Edge>` com `Edge { PhysicalButton button; bool down; }`, ligações por ação em vetores, captura pelo algoritmo de "Decisões"). A diagonal usa `1/√2` como constante.

- [ ] **Step 5: Ver passar** — `$DC make test` (inclui `oly.lint.boundaries`, que agora vê o módulo `input`) e `$WC make test PRESET=wasm-debug T='^input\.'`.

- [ ] **Step 6: Commit** `feat: módulo input com ActionMap, botões HID e captura por passo`.

---

### Task 2: `ActionMap` na `Engine` — captura no passo 1, aba oculta e contexto

**Files:**
- Create: `engine/runtime/app/tests/actions.cpp`
- Modify: `engine/runtime/app/CMakeLists.txt` (`DEPENDS input ecs core`), `engine/runtime/app/include/oly/app/{engine.hpp,context.hpp}`, `engine/runtime/app/src/{engine.cpp,context.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp}`

**Interfaces:**
- Consumes: `oly::input::ActionMap` da Task 1.
- Produces: `input::ActionMap& Engine::actions() noexcept` (e a versão `const`); `input::ActionMap& EngineContext::actions() const noexcept`. `captureActions()` deixa de ser vazia; `setHidden(true)` chama `releaseAll()`; declarações travadas durante `frame`.

- [ ] **Step 1: Testes que falham** em `tests/actions.cpp` (`oly_add_test(MODULE app NAME actions SOURCES actions.cpp SUPPORT engine/runtime/ecs/tests/support)`). Fixture como `pause.cpp` (`frame(0)` só registra o tempo; `step()` avança 1/60). Sistemas gravam `context.actions().button(jump)` num vetor via `userData`. `CHECK_INVARIANTS(world)` no fim.
```cpp
TEST_CASE("uma captura por passo, também pausado") { /* pause(); sistema whilePaused; down Space; step → pressed; step → !pressed && held */ }
TEST_CASE("bordas só no primeiro passo do frame") { /* down, up; frame que roda 3 passos */ CHECK(estados == {{1,0,1},{0,0,0},{0,0,0}}); }
TEST_CASE("frame sem passo guarda a borda para o próximo passo") { /* down, up; frame com avanço < 1 passo; depois frame que completa o passo */ CHECK(pressed no único passo); }
TEST_CASE("sair da pausa por uma ação num sistema whilePaused") { /* sistema whilePaused: se button(togglePause).pressed, context.resume(); pause(); down; step; up */ CHECK(!engine.paused()); /* próximo step roda o sistema normal */ }
TEST_CASE("setHidden(true) solta os botões") { /* down Space; step; setHidden(true); setHidden(false); frame(só tempo); step */ CHECK(released && !held); }
TEST_CASE("sistema religa e a troca vale na captura seguinte") { /* sistema em Update: bind(jump, Key::Q) no 1º passo; Q pressionado antes */ CHECK(passo 1: !held); CHECK(passo 2: held && !pressed); }
TEST_CASE("handler lê as ações pelo mesmo contexto") { /* evento Ping emitido em Update; handler grava button(jump).pressed */ CHECK(pressed no handler do passo da borda); }
```
`contract_death.cpp`: caso `declare_action_during_frame` (sistema chama `context.actions().declareButton("late")`; substrings `declareButton`, `durante frame`), na lista `CASES`.

- [ ] **Step 2: Ver falhar** — `$DC make test T='^app\.'` → erro de compilação (`actions()` não existe).

- [ ] **Step 3: Implementar.** Membro `input::ActionMap actions_` na `Engine` (no bloco de dados, junto dos de evento). `frame`: `actions_.setDeclarationsLocked(true)` ao ligar `inFrame_` e `false` ao desligar; `recoverFromInterruptedFrame` também destrava. Atualizar o comentário das etapas vazias (só restam `runDueSchedule` e `applyStateTransition`) e o de `setHidden` (agora solta os botões).

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: captura de ações no passo fixo da Engine e soltura na aba oculta`.

---

### Task 3: `action` no esquema, no gerador e na verificação do WASM

**Files:**
- Modify: `engine/packages/core/src/{schema.ts,schema_hash.ts,runtime.ts,module.ts,index.ts,internal.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/{schema.test.ts,surface.test.ts}`, `engine/tools/codegen/src/emit_cpp.ts`, `engine/tools/codegen/tests/generator.test.ts`, `engine/runtime/bridge/CMakeLists.txt` (`DEPENDS app input ecs core`), `engine/runtime/bridge/include/oly/bridge/{schema_registry.hpp,c_api.hpp}`, `engine/runtime/bridge/src/c_api.cpp`, `engine/runtime/bridge/tests/{test_schema_registry.cpp,c_api.cpp}`, `engine/testing/tests/wasm/load.test.ts`

**Interfaces:**
- Produces:
  - TS (`@oly/core`): `type ActionKind = "button" | "axis2d" | "pointer";` `interface ActionType<Name extends string = string, Kind extends ActionKind = ActionKind> { readonly kind: "action"; readonly actionKind: Kind; readonly name: Name; readonly schemaIndex: number }`; `type ButtonAction = ActionType<string, "button">`, `Axis2dAction`, `PointerAction` idem; `function action<const Name extends string, const Kind extends ActionKind>(name: Name, kind: Kind): ActionType<Name, Kind>` (`ContractError` para nome fora do padrão de "Decisões" ou `kind` desconhecido); `Schema` ganha `readonly actions: readonly ActionType[]`; `defineSchema` aceita `SchemaType | EventType | ActionType`.
  - TS interno: `actionIndexOf(runtime: Runtime, action: ActionType): number` (em `runtime.ts`, exportado por `internal.ts`; `ContractError` "ação não pertence ao esquema deste runtime" como `eventIndexOf`).
  - C++ (`oly/bridge/schema_registry.hpp`): `struct SchemaAction { std::string_view name; oly::input::ActionKind kind; };` `std::span<const SchemaAction> schemaActions();` (definida pelo código gerado).
  - API C: `#define OLY_ACTION_BUTTON 0u`, `OLY_ACTION_AXIS2D 1u`, `OLY_ACTION_POINTER 2u`; `uint32_t olySchemaActionCount()`, `uint32_t olySchemaActionKind(uint32_t actionIndex)` (índice sem verificação, como `olySchemaEventSize`). `static_assert` de que os três valores batem com `ActionKind`.
  - Gerador: em `schema.generated.hpp`, se houver ações, `#include "oly/input/action_map.hpp"` e `namespace actions { inline constexpr oly::input::ButtonAction fire{0}; ... }` (tipo do handle pelo `kind`, valor = `schemaIndex`); em `schema_registry.generated.cpp`, `schemaActions()` com `{"fire", oly::input::ActionKind::Button}` por ação, na ordem, ou `return {};`.

- [ ] **Step 1: Testes que falham.**
  - `schema.test.ts`: `action("Fire", "button")`, `action("for", "button")` e `action("fire", "trigger" as never)` lançam `ContractError`; `defineSchema([A, fire, Hit, move])` dá `actions` = `[fire, move]` com `schemaIndex` 0 e 1, sem mexer em `types`/`events`; nome repetido entre ação e componente/evento lança `ContractError`; os testes de hash existentes passam sem mudança (esquema sem ações); acrescentar uma ação muda o hash, e trocar o `kind` de uma ação também.
  - `generator.test.ts`: header de esquema com ações contém `#include "oly/input/action_map.hpp"`, `inline constexpr oly::input::ButtonAction fire{0};` e `inline constexpr oly::input::Axis2dAction move{1};`; header sem ações não contém `oly/input`; registro contém `{"fire", oly::input::ActionKind::Button}` antes de `{"move", oly::input::ActionKind::Axis2d}`.
  - `c_api.cpp` (bridge): `olySchemaActionCount() == 3`, `olySchemaActionKind(0) == OLY_ACTION_BUTTON`, `(1) == OLY_ACTION_AXIS2D`, `(2) == OLY_ACTION_POINTER` (`test_schema_registry.cpp` passa a definir `schemaActions()` com `bridgeJump`, `bridgeMove`, `bridgeAim`, nessa ordem).
  - `load.test.ts`: esquema TS com uma ação a mais que o WASM → `SchemaMismatchError` com "ações no WASM"; mesmo nº de ações com `kind` diferente → `SchemaMismatchError` com o nome da ação (montar como os casos de divergência de evento).
  - `surface.test.ts` / `public-exports.txt`: `action`, `ActionKind`, `ActionType`, `ButtonAction`, `Axis2dAction`, `PointerAction`.

- [ ] **Step 2: Ver falhar** — `$WC make ts` → erro de tipos (`action` não existe); `$DC make test T='^bridge\.'` → erro de compilação (`schemaActions`).

- [ ] **Step 3: Implementar.** `canonicalSchema(types, events, actions = [])` acrescenta `@nome:tipo;`. `loadRuntime` confere `_olySchemaActionCount() === schema.actions.length` e o tipo de cada ação (`["button","axis2d","pointer"].indexOf(actionKind)`); a mensagem de `SchemaMismatchError` ganha "ações no WASM: n, no TS: m" e o primeiro divergente pode ser ação. `OlyModule` ganha os dois exports.

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make games-test` (o fixture ainda não tem ações: `schemaActions()` gerado vazio).

- [ ] **Step 5: Commit** `feat: action no esquema, no gerador e na verificação do WASM`.

---

### Task 4: Ações na API C — declaração do esquema, ligações, entrada do host e snapshot

**Files:**
- Modify: `engine/runtime/bridge/include/oly/bridge/engine_api.hpp`, `engine/runtime/bridge/src/engine_api.cpp`, `engine/runtime/bridge/tests/{engine_api.cpp,contract_death.cpp,CMakeLists.txt}`

**Interfaces:**
- Consumes: Task 1 (`ActionMap::declare/kind/actionCount`, `ActionState`, constantes de flags), Task 2 (`Engine::actions()`), Task 3 (`schemaActions()`).
- Produces (API C, `extern "C"`, `OLY_API`): as nove funções do §4.5 (`olyEngineBindButton`, `olyEngineBindAxis2d`, `olyEngineClearBindings`, `olyEngineIsBound`, `olyEngineButtonDown`, `olyEngineButtonUp`, `olyEnginePointerMove`, `olyEngineReleaseAll`, `olyEngineActionSnapshot`) com exatamente aquelas assinaturas; `struct OlyActionState { float x; float y; uint32_t flags; };` e `#define OLY_ACTION_PRESSED 1u`, `OLY_ACTION_HELD 2u`, `OLY_ACTION_RELEASED 4u`, `OLY_ACTION_VALID 8u`, com `static_assert` de tamanho, offsets e valores contra `oly::input::ActionState` e as constantes. `olyEngineCreate` declara, depois dos eventos, `schemaActions()` na ordem (`OLY_ASSERT` que o id devolvido é o índice). `olyEngineBindButton`/`BindAxis2d` conferem `action < actionCount()` e o tipo (`OLY_ASSERT` com o nome da função, `action` e o tipo esperado); `olyEngineClearBindings` confere só o intervalo.

- [ ] **Step 1: Testes que falham** em `tests/engine_api.cpp` (sem script: um sistema nativo `OLY_SYSTEM(bridgeTestReadJump)` grava `context.actions().button(ButtonAction{0})` num global):
```cpp
TEST_CASE("ações do esquema são declaradas na criação, na ordem") { CHECK(engineOf(engine).actions().actionCount() == 3); CHECK(name(0) == "bridgeJump"); CHECK(kind(2) == ActionKind::Pointer); }
TEST_CASE("ligar, apertar e ler pelo snapshot") { /* BindButton(0, Space); ButtonDown(Space); 1 passo */ CHECK(olyEngineActionSnapshot(engine)[0].flags == (OLY_ACTION_PRESSED | OLY_ACTION_HELD)); CHECK(sistema nativo viu pressed); }
TEST_CASE("eixo e ponteiro pela API C") { /* BindAxis2d(1, W,S,A,D); ButtonDown(D); PointerMove(5, 6); 1 passo */ CHECK(snapshot[1].x == 1.0f); CHECK(snapshot[2] == {5, 6, OLY_ACTION_VALID}); }
TEST_CASE("isBound, clearBindings e releaseAll") { CHECK(olyEngineIsBound(engine, Space) == 1); /* ClearBindings(0) */ CHECK(olyEngineIsBound(engine, Space) == 0); /* religa, down, passo, ReleaseAll, passo */ CHECK(released); }
TEST_CASE("o endereço do snapshot não muda entre frames") { CHECK(olyEngineActionSnapshot(engine) == endereço lido antes de 3 passos com input); }
```
`contract_death.cpp` + `CASES`: `bind_button_wrong_kind` (`olyEngineBindButton(engine, 1, Space)`; substrings `olyEngineBindButton`, `tipo`), `action_out_of_range` (`olyEngineClearBindings(engine, 9)`; `olyEngineClearBindings`, `fora do intervalo`).

- [ ] **Step 2: Ver falhar** — `$DC make test T='^bridge\.'` → erro de compilação.

- [ ] **Step 3: Implementar** as funções como repasse ao `engine->engine.actions()`, com `PhysicalButton(button)` explícito e `reinterpret_cast<const OlyActionState*>(snapshot().data())`.

- [ ] **Step 4: Ver passar** — `$DC make test` e `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: ações na API C com ligações, entrada do host e snapshot estável`.

---

### Task 5: Ações no `@oly/core` — `ActionMap`, tabela W3C → HID e leitura nos contextos

**Files:**
- Create: `engine/packages/core/src/{buttons.ts,action_map.ts}`, `engine/packages/core/tests/unit/buttons.test.ts`, `engine/testing/tests/wasm/actions.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,context.ts,bridge.ts,module.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/{schema.ts,systems.cpp}`, `engine/testing/tests/wasm/fixture.ts`

**Interfaces:**
- Consumes: API C da Task 4; `ActionType`, `actionIndexOf` da Task 3.
- Produces (`@oly/core`):
  - `buttons.ts`: `const buttonUsages` (nome W3C → usage, a tabela de "Decisões", `as const`); `type ButtonName = keyof typeof buttonUsages;` `function isButtonName(name: string): name is ButtonName`.
  - `action_map.ts`: `interface ButtonState { readonly pressed: boolean; readonly held: boolean; readonly released: boolean }`, `interface Vector2 { readonly x: number; readonly y: number }`, `interface PointerState { readonly x: number; readonly y: number; readonly valid: boolean }`, `interface Axis2dBinding { readonly up: ButtonName; readonly down: ButtonName; readonly left: ButtonName; readonly right: ButtonName }`; `class ActionMap` com `button(action: ButtonAction): ButtonState`, `axis2d(action: Axis2dAction): Vector2`, `pointer(action: PointerAction): PointerState`, `bind(action: ButtonAction, button: ButtonName): void`, `bind(action: Axis2dAction, binding: Axis2dBinding): void`, `clearBindings(action: ActionType): void`, `isBound(button: ButtonName): boolean`, `buttonDown(button: ButtonName): void`, `buttonUp(button: ButtonName): void`, `pointerMove(x: number, y: number): void`, `releaseAll(): void`. Construtor interno `(runtime, bridge, engineHandle, snapshotAddress, assertUsable: (operation: string) => void)`.
  - Erros (`ContractError`, antes de chamar a API C): nome de botão fora da tabela; ação de outro esquema; `actionKind` diferente do método (JS sem tipos); `pointerMove` não finito; `Engine` liberada.
  - `Engine.actions: ActionMap` (getter; `ContractError` com a `Engine` liberada); `EngineContext.actions: ActionMap` (getter; `ContractError` fora da chamada). Os dois devolvem o mesmo objeto.
  - `Bridge`: `engineBindButton`, `engineBindAxis2d`, `engineClearBindings`, `engineIsBound`, `engineButtonDown`, `engineButtonUp`, `enginePointerMove`, `engineReleaseAll`, `engineActionSnapshot`; os que podem alocar (ligar, entrada do host) terminam em `sync()`. Leitura: `bridge.heap.f32`/`u32` em `snapshotAddress + 12 * actionIndex`, sem chamar export.
  - `OlyModule`: os nove `_olyEngine*` da Task 4.
  - `public-exports.txt`: + `ActionMap`, `Axis2dBinding`, `ButtonName`, `ButtonState`, `PointerState`, `Vector2`, `isButtonName`.
- Fixture: `fire = action("fire", "button")`, `move = action("move", "axis2d")`, `aim = action("aim", "pointer")`, `togglePause = action("togglePause", "button")`, nessa ordem, no fim de `defineSchema`. Em `systems.cpp`: `OLY_SYSTEM(countFirePresses)` soma 1 num contador estático quando `context.actions().button(actions::fire).pressed`; export `uint32_t fixtureTakeFirePressCount()` (devolve e zera).

- [ ] **Step 1: Testes que falham.**
  - `buttons.test.ts`: cada amostra de "Decisões" (`buttonUsages.KeyW === 0x0007_001a`, ..., `MouseMiddle === 0x0009_0003`); `isButtonName("KeyW")` verdadeiro, `isButtonName("w")` e `isButtonName("Unidentified")` falsos.
  - `engine/testing/tests/wasm/actions.test.ts` (helpers `withEngine` e `stepper` como em `engine_pause.test.ts`; `checkInvariants() === null` no fim):
```ts
test("antes da primeira captura tudo é falso e o ponteiro é inválido", ...)          // ler fora de frame logo após createEngine
test("sistema TS lê pressed, held e released", ...)                                // bind(fire, "Space"); buttonDown; step; step; buttonUp; step
test("toque mais curto que um passo aparece num passo só", ...)                     // down+up antes do step → {true,false,true}; step seguinte → tudo false
test("sistemas C++ e TS veem o mesmo snapshot no mesmo passo", ...)                 // addNativeSystem("countFirePresses") e sistema TS; 1 toque → fixtureTakeFirePressCount() === 1 e TS viu pressed
test("eixo WASD com diagonal normalizada e ponteiro", ...)                          // W+D → x ≈ 0.7071, y ≈ -0.7071 (f32); pointerMove(10, 20) → {10, 20, true}
test("sair da pausa por togglePause num sistema whilePaused", ...)
test("religar de dentro de um sistema vale na captura seguinte, sem bordas fantasmas", ...) // KeyQ já pressionado
test("memória cresce antes do sistema TS e a leitura continua certa", ...)          // addNativeSystem("spawnBatch") antes do sistema TS que lê fire; buffer muda; pressed lido certo
test("isBound e clearBindings", ...)
test("erros: botão fora da tabela, ação de outro esquema, tipo errado, ponteiro não finito, Engine liberada", ...) // ContractError em cada um
test("context.actions fora da chamada do sistema é ContractError", ...)
```
  - `surface.test.ts`: membros de `ActionMap` exatamente como em Produces; `Engine` e `SystemContext`/`HandlerContext` ganham `actions`.

- [ ] **Step 2: Ver falhar** — `$WC make games-test` → erro de tipos no `tsc` (`ActionMap` não existe).

- [ ] **Step 3: Implementar.** A `Engine` cria o `ActionMap` no construtor (`bridge.engineActionSnapshot(handle)` uma vez) e o repassa aos contextos que cria. `button()` monta um objeto novo a cada leitura (§4.5: poucas por passo). `bind`/`clearBindings` aceitam ser chamados durante `frame` (religar de dentro de sistema).

- [ ] **Step 4: Ver passar** — `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (sem asserts: só as validações TS seguram os erros) e `$DC make test`.

- [ ] **Step 5: Commit** `feat: ações no @oly/core com leitura pelo snapshot e tabela W3C para HID`.

---

### Task 6: `attachBrowserInput` no `@oly/client` e documentação

**Files:**
- Create: `engine/packages/client/src/browser_input.ts`, `engine/packages/client/tests/browser_input.test.ts`
- Modify: `engine/packages/client/src/index.ts`, `CLAUDE.md`

**Interfaces:**
- Consumes: `ActionMap`, `ButtonName`, `isButtonName` do `@oly/core` (público).
- Produces (`@oly/client`):
  - `interface InputTarget { readonly actions: Pick<ActionMap, "buttonDown" | "buttonUp" | "pointerMove" | "releaseAll" | "isBound"> }` (a `Engine` satisfaz).
  - `interface InputCanvas { readonly width: number; readonly height: number; getBoundingClientRect(): { left: number; top: number; width: number; height: number }; addEventListener`/`removeEventListener` para `"pointerdown"` e `"contextmenu"` }` e `interface InputEnvironment { readonly window: { addEventListener`/`removeEventListener` para `"keydown"`, `"keyup"`, `"blur"`, `"pointerup"` e `"pointermove"` } }`. Os listeners recebem só as formas mínimas usadas (`KeyInput { code; preventDefault() }`, `PointerInput { button; clientX; clientY; preventDefault() }`), sem `any`, para o teste montar eventos falsos sem DOM.
  - `function attachBrowserInput(target: InputTarget, canvas: InputCanvas, environment?: InputEnvironment): BrowserInput`; `interface BrowserInput { detach(): void }` (`detach` remove todos os listeners; chamar de novo não faz nada).

- [ ] **Step 1: Testes que falham** em `browser_input.test.ts`, com janela e canvas falsos que guardam listeners e um alvo que grava chamadas (estilo de `browser_loop.test.ts`):
```ts
test("keydown/keyup viram buttonDown/buttonUp pelo code W3C", ...)      // code "KeyW"
test("code fora da tabela é ignorado", ...)                              // "Unidentified" → nenhuma chamada
test("preventDefault só para botão ligado", ...)                          // isBound("Space") true → prevented; "F5" → não
test("blur na janela chama releaseAll", ...)
test("pointerdown no canvas manda pointerMove e depois buttonDown", ...)  // rect {left 10, top 20, width 200, height 100}, canvas 400×200, client (60, 70) → (100, 100); button 0 → "MouseLeft"
test("pointerup e pointermove vêm da janela", ...)                        // soltar fora do canvas chega como buttonUp
test("botão do mouse 1 é MouseMiddle, 2 é MouseRight, 3 é ignorado", ...)
test("contextmenu no canvas só é impedido com MouseRight ligado", ...)
test("canvas com tamanho 0 ignora eventos de ponteiro", ...)              // rect width 0 → nenhuma chamada a pointerMove
test("detach remove os listeners e é idempotente", ...)
```

- [ ] **Step 2: Ver falhar** — `$WC make ts` → erro de tipos (`attachBrowserInput` não existe).

- [ ] **Step 3: Implementar.** Ambiente padrão a partir de `globalThis` (funções embrulhadas, como em `browser_loop.ts`). Conversão: `x = (clientX − rect.left) · canvas.width / rect.width`, idem para `y` com a altura. Exportar `attachBrowserInput`, `BrowserInput`, `InputCanvas`, `InputEnvironment`, `InputTarget` no `index.ts`.

- [ ] **Step 4: `CLAUDE.md`** — árvore: módulo `input` (`DEPENDS core`) e a cadeia `core ← input ← app`; seção nova curta "Engine: ações de input" (`ActionMap` sem ECS nem `Engine`, `PhysicalButton` HID, fila de bordas com auto-repeat ignorado, captura no passo 1 também pausado, bordas por um passo, `releaseAll` em `setHidden(true)`, snapshot de 12 B por ação lido sem cruzar a fronteira); na ponte, as ações do esquema declaradas na criação da `OlyEngine`; linhas do glossário para declarar ação, ligar, entregar input e ler; `action`, `ActionMap` e `isButtonName` na lista do `@oly/core`; `attachBrowserInput` na do `@oly/client`; `input.*` nos nomes do ctest.

- [ ] **Step 5: Verificação final** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`: tudo verde.

- [ ] **Step 6: Commit** `feat: attachBrowserInput no @oly/client e documentação das ações`.
