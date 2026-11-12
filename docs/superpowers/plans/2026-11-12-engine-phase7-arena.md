# Engine — Fase 7 (Jogo de validação) — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O jogo de tiro do §1.1 (`games/arena/`) jogável no Chromium, com gameplay por entidade em C++ e render, HUD, ganchos e ondas em TS, mais as faltas genéricas que ele revela na engine (`single`/`trySingle` e o lint de fronteiras nos jogos).

**Architecture:** `single`/`trySingle` entram primeiro no `Query<Ts...>` do ECS e depois no `Query` do `@oly/core`. O jogo é um workspace npm e um alvo `oly_add_game` em `games/arena/`: o esquema gera o C++; `native/` traz os sistemas e handlers C++ registrados por nome; `src/game.ts` (`installGame`, sem DOM) registra tudo na ordem da tabela do §4.9; `src/main.ts` é a casca do navegador. O teste de fumaça roda `installGame` em node contra o WASM real, com relógio e input falsos. O esbuild junta o TS para o navegador e serve a pasta estática montada pelo `make game`.

**Tech Stack:** C++20, GCC 14 (nativo), Emscripten 3.1.74 + node 20.18 (serviços `wasm` e `web`), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`, esbuild 0.24.2.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (fase 7 da §5; §4.9 inteira, §1.1, §2, §4.2 para `single`, §4.4 para o fluxo de referência dos eventos, §4.7 para estados e escopo, §6, §7 "Jogo de validação"). `§N` abaixo são desse spec.

## Global Constraints

- Branch `feat/engine-phase7-arena` a partir de `develop`. Commits, comentários, mensagens de assert e de erro em pt-BR; commits terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain só em Docker: `DC="docker compose run --rm dev"` (nativo, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). Presets `wasm-games*` exigem `make ts` antes. Na imagem do emsdk o `npm` não está no `PATH`: fora do `Makefile`, use `$WC sh -c '"$(dirname "$(command -v node)")/npm" …'`.
- `-Wall -Wextra -Wpedantic -Werror` em todo alvo C++; TS `strict` (§3).
- Camadas: `core ← input ← app ← bridge ← jogos` e `core ← ecs ← app`. Nenhum módulo novo. O jogo declara em `DEPENDS` todo módulo que inclui (`ecs app bridge`; `input` só se incluir `oly/input/...`).
- Nada de conceito do jogo em `engine/`. Nomes do jogo (inimigo, moeda, dash, arena) só em `games/arena/`. O jogo não importa `@oly/core/internal`.
- **Regra das faltas (§4.9):** se o jogo precisar de algo genérico que a engine não tem, pare a task e trate como tarefa da engine na mesma fase (C++ primeiro, depois API C e TS, testes no módulo, casos de morte, `CLAUDE.md`/spec) e registre na lista da fase (Task 7). Executor por subagente: reporte a falta em vez de contornar.
- Fronteira: nada por entidade. `single` cruza um número constante de vezes por chamada; handlers TS recebem lote.
- Sem benchmark nesta fase (§7).
- Estilo `CLAUDE.md` e skills `blank-lines`/`code-comments`: chaves em todo bloco, nomes completos, dados no topo da classe, um `namespace` por arquivo, includes do projeto com aspas depois da biblioteca padrão. Não formatar à mão (hook `clang-format`). Comentário só para razão, restrição externa ou contrato não óbvio; nada de narração da edição; testes em Arrange/Act/Assert.

### Decisões que o spec deixa em aberto (fixadas aqui)

- **`single` em C++.** `std::tuple<Entity, Ts&...> single()` e `std::optional<std::tuple<Entity, Ts&...>> trySingle()` em `Query<Ts...>`, para `auto [player, position] = query.single();`. Somam o `count()` das tabelas casadas (O(tabelas)), sem `IterationGuard`, porque não chamam código do usuário. Sem asserts, `single()` com 0 linhas é comportamento indefinido (como `get` de entidade morta) e mais de uma linha devolve a primeira. Mensagens: `"single: %.*s com %u linhas; esperava 1"` e `"trySingle: %.*s com %u linhas; esperava 0 ou 1"`, com `core::typeName<Query<Ts...>>()`.
- **`single` em TS sem export novo.** `query.single()` usa `olyQueryBegin`/`olyQueryEnd` (duas chamadas por consulta, constante): soma os `count` dos descritores, lê a entidade e copia os componentes de `read` e `write` antes do `End`. O §4.2 diz "uma vez por chamada"; a intenção é "não por entidade", e um export a mais só pouparia uma travessia. A Task 2 ajusta a frase no spec. O retorno é `SingleRow` (`entity` e `get(type)`, cópia como o `World.get`); escrita continua por `set`.
- **Projetos TS do jogo.** `games/arena/tsconfig.json` compila `schema.ts` e `src/` menos `main.ts`, com `lib: ["ES2022"]` e `types: []`: o jogo usar DOM é erro de compilação. `tsconfig.browser.json` compila só `src/main.ts` com `DOM`. `tests/tsconfig.json` usa `types: ["node"]`. Todos com `rootDir` na pasta do jogo e saída em `dist/` (testes em `dist-tests/`). Por isso o bundle parte de `dist/src/main.js`, não de `dist/main.js` como no §4.9: o `schema.ts` fica na raiz do jogo. A Task 7 ajusta o caminho no spec.
- **Constantes nos dois lados.** Tamanho da arena e números usados nas duas linguagens ficam em `native/arena.hpp` e `src/arena.ts`, cada um apontando para o outro em comentário. Um teste (Task 4) prende os dois: o jogador empurrado para fora fica em `ArenaWidth − raio`.
- **Números iniciais** (§4.9: sem medição, ajustados jogando; o comentário diz isso):

| Item | Valores |
|---|---|
| Arena | 960 × 540 |
| Jogador | raio 12, velocidade 220, vida 3, arma com intervalo 0,15 s; dash com velocidade 600, duração 0,15 s, recarga 0,8 s |
| Bala | raio 3, velocidade 600 |
| Inimigo | raio 12, vida 1, perseguição 90 |
| Moeda | raio 8; solta a cada 2 s; 30% de chance na morte de um inimigo |
| Ondas | a cada 5 s, a primeira aos 5 s; a onda `n` (1, 2, …) tem `2 + n` inimigos |
| Invulnerável | 1 s depois de um dano |
| Placar | `score = 10 × kills + 5 × coins` |

- **Mortes repetidas.** `collide` emite `EnemyDied` a cada bala que deixa a vida ≤ 0, então duas balas no mesmo passo geram duas mortes no lote (é o caso que o §4.4 manda o jogo tratar). Handlers que contam ou criam (`countKill`, `dropCoin`) ignoram a entidade repetida dentro do lote; `destroyDead` confere `isAlive`.
- **Aleatoriedade.** C++: `std::minstd_rand` com semente fixa e `generator() % 100 < CoinDropPercent` (a sequência do `minstd_rand` é a mesma em qualquer biblioteca padrão; as distribuições não). TS: o `random` injetado.
- **`installGame(engine, { drawer, random })`** chama `engine.requestState(menu)` e devolve `ArenaScore` (`kills`, `coins`, `score`, `finalScore: number | null`), objeto vivo só de leitura. `endRound` grava `finalScore`; `startRound` zera tudo.
- **Teste de fumaça.** Uma `Engine` nova por `test()`. Cada `step` confere `checkInvariants() === null` ("a cada etapa"). Cenários arrumados usam `engine.world` entre frames (spawn direto é persistente, §4.7); o teste destrói o que sobrar dos arranjos.
- **Web.** `make game`/`make serve` usam `PRESET` só quando ele vem da linha de comando ou do ambiente; senão, `wasm-games-release`. Preset fora de `wasm-games*` é erro do `make`. O `serve` roda o esbuild em modo servidor com `--servedir` na pasta montada, porta `0.0.0.0:8000`.
- **Lint nos jogos.** Nenhum plano nem commit registra por que o lint ficou fora do Emscripten (o plano da modularização, Task 2 Step 5, só diz "fora do Emscripten"; o commit `c967544` também). A razão provável é não rodá-lo duas vezes nos presets do estudo WASM. A condição vira `NOT EMSCRIPTEN OR OLY_BUILD_GAMES`.
- **Checklist manual** no Chromium: pelo autor, ou pelo agente com o MCP do Playwright em `http://localhost:8000`. O relatório final diz quem fez.
- **Fora do escopo:** interpolação no render, câmera, áudio, gamepad, benchmark.

## Review Focus

1. **Duas balas matam o mesmo inimigo no mesmo passo:** o placar conta uma morte, cai no máximo uma moeda e nada dispara assert (Task 4).
2. **Tamanho da arena divergente entre C++ e TS:** o jogador empurrado para fora fica em `ArenaWidth − raio`, `ArenaHeight − raio` (Task 4).
3. **Jogar de novo depois do `gameOver`:** as tarefas `every` da partida anterior foram canceladas; aos 2 s há uma só moeda solta, não duas, e o placar começa zerado (Task 5).
4. **Render no `menu` e no `gameOver`, sem jogador:** o HUD usa `trySingle` e não lança (Task 5).
5. **Pausa com dash ativo:** o fim do dash congela com o relógio e acontece no tempo restante depois de retomar (Task 5).

---

### Task 1: `single` e `trySingle` no `Query<Ts...>` do ECS

**Files:**
- Modify: `engine/runtime/ecs/include/oly/ecs/query.hpp`, `engine/runtime/ecs/tests/queries.cpp`, `engine/runtime/ecs/tests/contract_death.cpp`, `engine/runtime/ecs/tests/CMakeLists.txt` (`CASES`)

**Interfaces:**
- Produces (`oly::ecs::Query<Ts...>`): `std::tuple<Entity, Ts&...> single();` `std::optional<std::tuple<Entity, Ts&...>> trySingle();` (`Ts` const → `const T&`). Contratos e mensagens nas decisões acima.

- [ ] **Step 1: Branch** `git switch -c feat/engine-phase7-arena`.

- [ ] **Step 2: Testes que falham** em `tests/queries.cpp` (componentes de `support/components.hpp`; `CHECK_INVARIANTS(world)` no fim):
```cpp
TEST_CASE("single devolve a entidade e referências da única linha") { /* spawn(A, B) e spawn(B) */ auto [entity, a, b] = world.query<A, const B>().single(); CHECK(entity == esperada); a.value = 7; CHECK(world.get<A>(esperada).value == 7); }
TEST_CASE("single acha a linha em qualquer tabela casada e respeita With/Without") { /* 3 arquétipos casados: um esvaziado por destroy, um excluído por Without<Tag> */ CHECK(std::get<0>(world.query<A>(oly::ecs::Without<Tag>{}).single()) == esperada); }
TEST_CASE("trySingle: vazio sem linhas e a linha com uma") { CHECK(!world.query<A>().trySingle()); /* spawn */ CHECK(std::get<0>(*world.query<A>().trySingle()) == entity); }
```
  Casos de morte (skill `add-test-case`), com as substrings exigidas: `single_empty` (`"single:"`, `"0 linhas"`), `single_many` (`"single:"`, `"2 linhas"`), `try_single_many` (`"trySingle:"`, `"2 linhas"`).

- [ ] **Step 3: Ver falhar** — `$DC make test T='^ecs\.'` → erro de compilação (`single` não existe).

- [ ] **Step 4: Implementar** em `query.hpp` com um auxiliar privado que soma as linhas e guarda a primeira tabela não vazia; a tupla sai da linha 0 dessa tabela, pelas mesmas colunas de `callRows`.

- [ ] **Step 5: Ver passar** — `$DC make test T='^ecs\.'` e `$WC make test PRESET=wasm-debug T='^ecs\.'`. Rodar o agente `perf-reviewer` no diff de `query.hpp` (`each`/`eachBlock` não podem mudar no assembly).

- [ ] **Step 6: Commit** `feat: single e trySingle no Query do ECS`.

---

### Task 2: `single` e `trySingle` no `@oly/core`

**Files:**
- Modify: `engine/packages/core/src/{query.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/tests/wasm/query.test.ts`, `CLAUDE.md` (glossário), `docs/superpowers/specs/2026-11-09-game-engine-design.md` (§4.2, a frase sobre a travessia)

**Interfaces:**
- Consumes: `Bridge.queryBegin/queryEnd`, `readScalar`, `assertWorldUsable`.
- Produces (`@oly/core`): `Query.single(): SingleRow` e `Query.trySingle(): SingleRow | null`; `class SingleRow { readonly entity: Entity; get<F extends Fields>(type: ComponentType<string, F>): ValueOf<F> }` (devolve a cópia feita na consulta; tipo fora de `read`/`write` lança `ContractError` `"get type=<T>: tipo não está na query"`). Erros: `"single: query com <n> linhas; esperava 1"`, `"trySingle: query com <n> linhas; esperava 0 ou 1"`; `World` liberado, a mensagem de sempre. `public-exports.txt` ganha `SingleRow`.

- [ ] **Step 1: Testes que falham** em `engine/testing/tests/wasm/query.test.ts`:
```ts
test("single devolve a entidade e cópias dos componentes da única linha", ...) // row.entity === player; deepEqual(row.get(Position), { x: 1, y: 2 }); mudar a cópia não muda world.get
test("single escolhe pela spec entre várias entidades", ...)                     // with: [Player] e without: [Enemy]
test("trySingle devolve null sem linhas e a linha com uma", ...)
test("single dentro do eachBlock de outra query", ...)                          // iteração aninhada fecha na ordem; checkInvariants() === null
test("erros de single, trySingle e SingleRow.get", ...)                        // /single: query com 0 linhas/, /2 linhas/ nos dois, /get type=Velocity: tipo não está na query/
```
  `surface.test.ts`: `Query` ganha `single` e `trySingle`; `SingleRow` exportado com `entity` e `get`.

- [ ] **Step 2: Ver falhar** — `$WC make games-test T='^ts\.'` → erro de tipos (`single` não existe).

- [ ] **Step 3: Implementar.** `#readSingle(operation, maxRows)` faz o `queryBegin`, copia os descritores como o `eachBlock`, soma os `count`, lê a linha 0 do bloco não vazio e chama `queryEnd` no `finally`; o `ContractError` sai depois do `End`, para o `World` não ficar marcado como iterando.

- [ ] **Step 4: Documentar.** Glossário do `CLAUDE.md`, linha nova: `| ler a única linha de uma query (exatamente uma / no máximo uma) | Query::single() -> tuple<Entity, Ts&...> / trySingle() -> optional | (olyQueryBegin/End) | query.single() / trySingle() -> SingleRow (cópia) |`. No §4.2, trocar "cruzam a fronteira uma vez por chamada" por "cruzam a fronteira um número constante de vezes por chamada (`olyQueryBegin`/`End`)".

- [ ] **Step 5: Ver passar** — `$WC make games-test` e `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`.

- [ ] **Step 6: Commit** `feat: single e trySingle no @oly/core`.

---

### Task 3: Esqueleto do `arena` — build, esquema, lint nos jogos e entrada em `menu`/`playing`

**Files:**
- Create: `games/CMakeLists.txt`, `games/arena/{CMakeLists.txt,package.json,tsconfig.json,schema.ts}`, `games/arena/src/{arena.ts,game.ts,render.ts}`, `games/arena/tests/{tsconfig.json,harness.ts,smoke.test.ts}`
- Modify: `CMakeLists.txt` (raiz), `tsconfig.json` (raiz), `package-lock.json`

**Interfaces:**
- Consumes: `query.single/trySingle` (Task 2).
- Produces:
  - `schema.ts`: componentes, tags, eventos, ações e estados do §4.9 com esses nomes e campos (`Health.value: "i32"`, `Dash.ready/active: "u8"`, campos de evento `entity`/`coin` do tipo `"entity"`, os demais `"f32"`), exportados um a um e no `defineSchema` (default).
  - `src/arena.ts`: as constantes da tabela de números (`ArenaWidth`, `ArenaHeight`, `PlayerRadius`, `PlayerSpeed`, `PlayerHealth`, …, `KillPoints`, `CoinPoints`).
  - `src/render.ts` (só a interface nesta task): `interface Drawer { clear(): void; circle(x: number, y: number, radius: number, color: string): void; rect(x: number, y: number, width: number, height: number, color: string): void; text(x: number, y: number, content: string, color: string): void }`.
  - `src/game.ts`: `interface GameOptions { readonly drawer: Drawer; readonly random: () => number }`; `interface ArenaScore { readonly kills: number; readonly coins: number; readonly score: number; readonly finalScore: number | null }`; `installGame(engine: Engine, options: GameOptions): ArenaScore`. Nesta task: ligações (`move` WASD, `shoot` MouseLeft, `dash` Space, `pause` Escape, `confirm` Enter e MouseLeft), `startOnConfirm` (`updateBefore`, `inState(menu, gameOver)`), `onEnter(playing, "startRound")` (zera o placar, cria o jogador no centro com os valores da tabela) e `requestState(menu)`.
  - `tests/harness.ts`: `loadArena(): Promise<Runtime>` (fábrica pelo `OLY_ARENA_MODULE`, como `fixtureFactory`); `interface ArenaTest { engine; score; drawer: RecordingDrawer; step(count?: number): void; stepSeconds(seconds: number): void; tap(button: ButtonName): void; hold(button: ButtonName): void; release(button: ButtonName): void; count(...types: SchemaType[]): number; player(): Entity }`; `withArena(runtime, body, random = sequenceRandom([...]))` cria a `Engine` (`fixedHz: 60`), chama `installGame`, `frame(0)`, roda `body` e libera. `tap` = `buttonDown` + `buttonUp` + `step()`: toque curto, que dá `pressed` mas não `held` (§4.5); para atirar (`shoot.held`) use `hold` + `step` + `release`. `count` soma os `count` de `query({ with: types }).eachBlock`; sem tipos, conta todas as entidades. `player()` = `query({ with: [Player] }).single().entity`. `RecordingDrawer` guarda as chamadas desde o último `clear` (`texts()`, `circles()`). `sequenceRandom(values)` repete a lista.
  - CMake: `games/arena/CMakeLists.txt` com `file(GLOB nativeSources CONFIGURE_DEPENDS native/*.cpp)` e `oly_add_game(arena_game SCHEMA_JS ${CMAKE_CURRENT_SOURCE_DIR}/dist/schema.js SOURCES ${nativeSources} DEPENDS ecs app bridge)`; com `OLY_BUILD_TESTS`, `ts.arena.smoke` (`${OLY_NODE} --test ${CMAKE_CURRENT_SOURCE_DIR}/dist-tests/`, `ENVIRONMENT OLY_ARENA_MODULE=$<TARGET_FILE:arena_game>`). Raiz: `if(OLY_BUILD_GAMES) add_subdirectory(games) endif()` logo depois de `add_subdirectory(engine)` (antes do manifesto de fronteiras), e o lint registrado com `if(NOT EMSCRIPTEN OR OLY_BUILD_GAMES)`.
  - `package.json`: `{ "name": "arena", "private": true, "version": "0.1.0", "type": "module", "dependencies": { "@oly/core": "*", "@oly/client": "*" } }`. `tsconfig.json` referencia `engine/packages/core`; `tests/tsconfig.json` referencia `..` e o core. A raiz ganha `games/arena` e `games/arena/tests`.

- [ ] **Step 1: Testes que falham** em `tests/smoke.test.ts`:
```ts
test("installGame entra em menu no primeiro passo, sem jogador", ...)     // step(); engine.state?.name === "menu"; count(Player) === 0
test("Enter em menu começa: playing com um jogador no centro, vida 3 e placar zerado", ...)
  // tap("Enter"); step(); state playing; player() existe; get(Position) = { x: 480, y: 270 }; get(Health).value === 3; score deepEqual { kills: 0, coins: 0, score: 0, finalScore: null }
test("clique (MouseLeft) em menu também começa", ...)
```

- [ ] **Step 2: Ver falhar** — `$WC sh -c '"$(dirname "$(command -v node)")/npm" install --no-audit --no-fund'` (liga o workspace e atualiza o lock), depois `$WC make games-test T='arena'` → erro de tipos (`installGame` não existe).

- [ ] **Step 3: Implementar** os arquivos da lista de Interfaces.

- [ ] **Step 4: Ver passar** — `$WC make games-test` (inclui `ts.arena.smoke`, `oly.lint.boundaries` e `oly.lint.boundaries_selftest`; `ctest --preset wasm-games -N | grep lint` lista os dois). `$DC make test` continua verde e sem `ts.arena.smoke`.

- [ ] **Step 5: Commit** `feat: esqueleto do arena com esquema, entrada em menu e lint de fronteiras nos jogos`.

---

### Task 4: Gameplay C++ e handlers do arena

**Files:**
- Create: `games/arena/native/{arena.hpp,movement.cpp,combat.cpp}`
- Modify: `games/arena/src/{arena.ts,game.ts}`, `games/arena/tests/smoke.test.ts`

**Interfaces:**
- Consumes: `Query::single` (Task 1); `schema.generated.hpp` (structs, `actions::`, `states::`); `installGame` e o harness (Task 3).
- Produces (C++, nomes iguais aos de `addNativeSystem`/`addNativeHandler`):
  - `arena.hpp`: constantes `inline constexpr` usadas pelo C++ (`ArenaWidth`, `ArenaHeight`, `BulletSpeed`, `BulletRadius`, `CoinRadius`, `InvulnerableSeconds`, `CoinDropPercent`); `struct PlayerTarget { oly::ecs::Entity player; };` (payload das tarefas); `struct Direction { float x; float y; };` e `Direction directionTo(float fromX, float fromY, float toX, float toY)` (normalizada; `{0, 0}` com distância 0).
  - `movement.cpp`: `OLY_SYSTEM(steerPlayer)`, `OLY_SYSTEM(chasePlayer)`, `OLY_SYSTEM(integrate)`, `OLY_SYSTEM(confine)`; tarefas `endDash` (`active = 0`) e `rechargeDash` (`ready = 1`), que conferem `isAlive`.
  - `combat.cpp`: `OLY_SYSTEM(fireWeapon)`, `OLY_SYSTEM(collide)`, `OLY_HANDLER(dropCoin, EnemyDied)`, `OLY_HANDLER(destroyDead, EnemyDied)`; tarefa `endInvulnerability` (remove `Invulnerable` se viva).
- Produces (TS, em `installGame`, na ordem da tabela do §4.9): `addNativeSystem` de `steerPlayer` (`updateBefore`), `fireWeapon`, `chasePlayer`, `integrate` (`update`), `collide`, `confine` (`updateAfter`), todos `inState(playing)`; `EnemyDied`: `addNativeHandler(dropCoin)`, `on(countKill)`, `addNativeHandler(destroyDead)`; `CoinCollected`: `on(collectCoin)` (confere `isAlive`, `destroy` direto, `coins += 1`); `PlayerDied`: `on(endRound)` (`finalScore = score`, `requestState(gameOver)`).

Regras que a tabela do §4.9 não fixa:
- `steerPlayer`: com `active == 0`, velocidade = `move × PlayerSpeed`. Com `dash.pressed` e `ready == 1`: direção = `move` se não nula, senão `directionTo(jogador, aim)` se `aim.valid`; direção nula não faz dash (a recarga não é gasta). Com direção: `ready = 0`, `active = 1`, velocidade = direção × `dash.speed`, `after(dash.duration, endDash)` e `after(dash.recharge, rechargeDash)`.
- `fireWeapon`: `cooldown = max(0, cooldown − passo)`; com `shoot.held`, `cooldown == 0` e direção não nula até `aim`, cria `Bullet` na posição do jogador com velocidade `BulletSpeed` e `cooldown = interval`.
- `collide` (pares por força bruta):
```
para cada bala: para cada inimigo que encosta (distância ≤ soma dos raios):
    vida -= 1; destroy(bala) adiado; se vida ≤ 0: emit EnemyDied{inimigo, x, y}; para (uma bala acerta um inimigo)
se o jogador não tem Invulnerable e encosta em algum inimigo:
    vida -= 1; add Invulnerable adiado; after(InvulnerableSeconds, endInvulnerability); se vida ≤ 0: emit PlayerDied{jogador}
para cada moeda que encosta no jogador: emit CoinCollected{moeda}
```
- `confine`: jogador preso em `[raio, ArenaWidth − raio] × [raio, ArenaHeight − raio]`; bala com posição fora de `[0, ArenaWidth] × [0, ArenaHeight]` é destruída.
- `dropCoin`: entidade repetida no lote é ignorada (vetor de entidades vistas, por lote); com `generator() % 100 < CoinDropPercent`, cria `Coin` com `Position {x, y}` e `Collider {CoinRadius}` pelo contexto direto (com escopo de `playing`). `countKill` ignora repetidas com um `Set<Entity>` por lote.

- [ ] **Step 1: Testes que falham** em `tests/smoke.test.ts` (cada um parte de `playing`; inimigos, balas e moedas arrumados por `engine.world.spawn`, com `Chaser.speed = 0` quando o inimigo deve ficar parado):
```ts
test("WASD move o jogador a 220 px/s; diagonal normalizada", ...)            // hold KeyD: Velocity.x === 220; KeyD + KeyS: comprimento 220
test("dash: impulso de 600 na direção do movimento, fim em 0,15 s e recarga em 0,8 s", ...)
  // hold KeyD; tap Space: active 1, ready 0, Velocity.x 600; stepSeconds(0.2): active 0, Velocity.x 220; em 0,5 s ready 0; em 0,9 s ready 1
test("dash parado vai para o ponteiro; sem ponteiro válido não gasta a recarga", ...)
test("chasePlayer aponta a velocidade do inimigo para o jogador com a velocidade do Chaser", ...)
test("confine prende o jogador em ArenaWidth − raio e destrói a bala fora da arena", ...)
  // set Position { x: 5000, y: -50 }; step: { x: ArenaWidth - PlayerRadius, y: PlayerRadius }; um tiro (hold MouseLeft, step, release) para a direita sem inimigo: count(Bullet) === 0 depois de 2 s
test("tiro na direção do ponteiro mata o inimigo sem sobra de bala nem de inimigo", ...)
  // pointerMove 200 px à direita; inimigo parado a 100 px; hold MouseLeft, step, release; stepSeconds(0.5): kills 1, isAlive false, count(Bullet) 0
test("duas balas no mesmo inimigo no mesmo passo: uma morte no placar e no máximo uma moeda", ...)
  // duas balas paradas sobre um inimigo de vida 1; step: kills 1, isAlive false, count(Bullet) 0, count(Coin) ≤ 1
test("moeda encostada no jogador some e soma no placar", ...)                  // coins 1, score 5
test("dano dá 1 s de invulnerabilidade; vida 0 leva a gameOver sem jogador", ...)
  // inimigo parado sobre o jogador: vida 2 e Invulnerable; 0,5 s depois ainda 2; 1,1 s depois 1; set vida 1 → gameOver, count(Player) 0, finalScore === score
```

- [ ] **Step 2: Ver falhar** — `$WC make games-test T='arena'` → os casos novos falham (nenhum sistema nativo registrado).

- [ ] **Step 3: Implementar** o C++ e o registro em `installGame`. Os números de `arena.hpp` e `arena.ts` trazem o comentário de que são iniciais e sem medição, e de onde fica o par do outro lado.

- [ ] **Step 4: Ver passar** — `$WC make games-test` (inclui `oly.lint.boundaries`) e `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`.

- [ ] **Step 5: Commit** `feat: gameplay C++ do arena com handlers de morte, moeda e fim de partida`.

---

### Task 5: Pausa, ondas, moedas soltas e render

**Files:**
- Modify: `games/arena/src/{arena.ts,game.ts,render.ts}`, `games/arena/tests/smoke.test.ts`

**Interfaces:**
- Consumes: `installGame` e handlers (Tasks 3 e 4); `query.trySingle` (Task 2).
- Produces:
  - `render.ts`: `createRenderSystem(drawer: Drawer, score: ArenaScore): SystemFunction`, registrado em `Phase.Render` sem `inState`. Desenha um círculo por entidade com `Position` e `Collider`, uma query por tag (jogador com outra cor quando `Invulnerable`). HUD com `trySingle({ read: [Health, Dash], with: [Player] })`. Textos exatos: menu `"OLY ARENA"` e `"Enter ou clique para jogar"`; HUD `` `Vida ${vida}` ``, `` `Pontos ${score}` ``, `"Dash pronto"`/`"Dash recarregando"`; pausa `"Pausado — Esc para continuar"`; fim `` `Fim de jogo — ${finalScore} pontos` `` e `"Enter ou clique para jogar de novo"`. Sem interpolação (§4.9).
  - `game.ts`: `pauseOnPress` (`inState(playing)`, `pause.pressed` → `pause()`) e `resumeOnPress` (`whilePaused().inState(playing)`, `resume()`), registrados entre `startOnConfirm` e `steerPlayer`. `startRound` agenda `every(WaveInterval, spawnWave)` e `every(CoinInterval, spawnLooseCoin)`; o contador de ondas volta a 0 em cada `startRound`. `spawnWave` cria `2 + n` inimigos com posição numa borda sorteada pelo `random` (lado = `floor(random() × 4)`, coordenada ao longo dele = `random() × comprimento`). `spawnLooseCoin` cria uma moeda em `[CoinRadius, ArenaWidth − CoinRadius] × [CoinRadius, ArenaHeight − CoinRadius]`. As duas recebem `TaskContext` (spawn com escopo de `playing`).

- [ ] **Step 1: Testes que falham** em `tests/smoke.test.ts`:
```ts
test("Esc pausa: relógio, posições e dash congelam; Esc de novo retoma e o dash acaba no tempo restante", ...)
  // hold KeyD; tap Space; tap Escape; step(60): engine.time e Position iguais, Dash.active 1; tap Escape; step(3): active 1; step(10): active 0
test("ondas a cada 5 s com 2 + n inimigos nas bordas; uma moeda solta a cada 2 s dentro da arena", ...)
  // vida do jogador em 1000 (os inimigos chegam a ele antes dos 10 s); stepSeconds(4.9): count(Enemy) 0, count(Coin) 2;
  // passo a passo até count(Enemy) > 0: 3 inimigos, cada um a ≤ 2 px de uma borda (um passo move 1,5 px); stepSeconds(5): count(Enemy) 7
test("render: menu sem jogador, HUD em playing, pausa e fim de jogo", ...)
  // menu: texts() inclui "Enter ou clique para jogar", circles() 0; playing: "Vida 3", "Pontos 0", "Dash pronto", circles() === count(Collider); pausa: "Pausado — Esc para continuar"
test("roteiro: menu → playing → onda → morte → gameOver com o mundo vazio → jogar de novo do zero", ...)
  // espera a onda; jogador com vida 1 levado até um inimigo: gameOver, count() === 0 (nenhuma entidade), texts() inclui "Fim de jogo — <finalScore> pontos";
  // tap Enter; step: kills 0, coins 0, score 0, count(Player) 1; stepSeconds(2.05): count(Coin) === 1
```

- [ ] **Step 2: Ver falhar** — `$WC make games-test T='arena'` → erro de tipos (`createRenderSystem` não existe).

- [ ] **Step 3: Implementar.**

- [ ] **Step 4: Ver passar** — `$WC make games-test` e `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`.

- [ ] **Step 5: Commit** `feat: pausa, ondas, moedas soltas e render do arena`.

---

### Task 6: Casca do navegador, esbuild, `make game`/`make serve` e serviço `web`

**Files:**
- Create: `games/arena/src/main.ts`, `games/arena/tsconfig.browser.json`, `games/arena/web/index.html`
- Modify: `games/arena/tsconfig.json` (`exclude: ["src/main.ts"]`; o `tsconfig.browser.json` referencia `./tsconfig.json`, o core e o client), `tsconfig.json` (raiz, + `games/arena/tsconfig.browser.json`), `package.json` + `package-lock.json` (raiz), `Makefile`, `docker-compose.yaml`

**Interfaces:**
- Consumes: `installGame`, `Drawer`, `ArenaWidth`/`ArenaHeight`; `@oly/client` (`moduleFactoryFromUrl`, `runInBrowser`, `attachBrowserInput`); `loadRuntime`.
- Produces:
  - `main.ts`: pega o `<canvas id="arena">`, fixa `width`/`height` em `ArenaWidth`/`ArenaHeight`, monta um `Drawer` sobre `CanvasRenderingContext2D` (`fillRect`, `arc`, `fillText`), `loadRuntime(moduleFactoryFromUrl("./arena_game.mjs"), schema)`, `createEngine({ fixedHz: 60 })`, `installGame(engine, { drawer, random: Math.random })`, `attachBrowserInput(engine, canvas)`, `runInBrowser(engine)`. Erro no boot vai para o `console.error` e para o texto da página.
  - `web/index.html`: o canvas e `<script type="module" src="./main.js">`.
  - Raiz: `esbuild` exato `0.24.2` em `devDependencies`.
  - `Makefile`:
```make
# game/serve só servem aos presets wasm-games*: sem PRESET na linha de comando ou no ambiente, vale o Release.
WEB_PRESET = $(if $(filter command line environment,$(origin PRESET)),$(PRESET),wasm-games-release)
WEB_DIR = build/$(WEB_PRESET)/web/$(G)
```
    `game`: exige `G` e `WEB_PRESET` em `wasm-games*` (senão `$(error ...)`); depende de `ts`; `$(MAKE) build PRESET=$(WEB_PRESET)`; copia `games/$(G)/web/index.html` e `build/$(WEB_PRESET)/games/$(G)_game.{mjs,wasm}` para `$(WEB_DIR)`; `node_modules/.bin/esbuild games/$(G)/dist/src/main.js --bundle --format=esm --platform=browser --target=es2022 --outfile=$(WEB_DIR)/main.js`. `serve`: depende de `game`; o mesmo esbuild com `--servedir=$(WEB_DIR) --serve=0.0.0.0:8000`.
  - `docker-compose.yaml`: serviço `web` igual ao `wasm` (imagem do `Dockerfile.wasm`, mesmos volumes, `command: bash`) mais `ports: ["8000:8000"]`.

- [ ] **Step 1: Ver falhar** — `$WC make game G=arena` → `make: *** No rule to make target 'game'`.

- [ ] **Step 2: Implementar.** Instalar o esbuild com `$WC sh -c '"$(dirname "$(command -v node)")/npm" install --save-dev --save-exact esbuild@0.24.2 --no-audit --no-fund'`.

- [ ] **Step 3: Verificar o build da página.**
  - `$WC make game G=arena` → `build/wasm-games-release/web/arena/` tem `index.html`, `main.js`, `arena_game.mjs` e `arena_game.wasm`; `grep -c '@oly/' build/wasm-games-release/web/arena/main.js` → `0` (tudo no bundle).
  - `$WC make game G=arena PRESET=debug-asan` → erro do `make` citando `wasm-games`.
  - `$WC make games-test` verde (o `tsc` do `main.ts` com DOM e o do jogo sem DOM).

- [ ] **Step 4: Verificar o servidor.** Em segundo plano, `docker compose run --rm --service-ports web make serve G=arena`; no host, `curl -s http://localhost:8000/ | grep -c 'id="arena"'` → `1` e `curl -sI http://localhost:8000/arena_game.wasm` → `200` com `Content-Type: application/wasm` (se vier outro tipo, o Emscripten cai para a instanciação por `ArrayBuffer`: anote no README). Parar o container.

- [ ] **Step 5: Commit** `feat: casca do navegador do arena com esbuild, make game/serve e serviço web`.

---

### Task 7: Lista da fase, checklist manual e documentação

**Files:**
- Create: `games/arena/README.md`
- Modify: `CLAUDE.md`, `docs/superpowers/specs/2026-11-09-game-engine-design.md` (§4.9, o caminho `dist/src/main.js`)

- [ ] **Step 1: `games/arena/README.md`** com três seções:
  - **Como rodar:** `$WC make games-test` (inclui `ts.arena.smoke`); `docker compose run --rm --service-ports web make serve G=arena` e abrir `http://localhost:8000` no Chromium; `make game G=arena` para a pasta estática.
  - **Checklist manual (Chromium):** página abre sem erro no console e mostra o menu; Enter e clique começam; WASD move e a diagonal não é mais rápida; segurar o botão esquerdo atira na direção do mouse; clique direito não abre menu e Space não rola a página; Space faz dash e o HUD mostra a recarga; ondas nas bordas a cada 5 s perseguem o jogador; tiro mata e às vezes cai moeda; moedas soltas aparecem e somam pontos; Esc pausa e retoma com tudo congelado; trocar de aba e voltar não faz o jogo saltar; dano deixa o jogador invulnerável por 1 s; 3 danos levam ao fim de jogo com os pontos; Enter joga de novo com placar zerado e arena limpa; F5 recarrega. Se travar, investigar com o profiler do Chromium e registrar na lista.
  - **A lista da fase (§4.9):** *faltava na engine (adicionado)*: `single`/`trySingle` em C++ e TS (posição do jogador em `chasePlayer` e `fireWeapon`, vida e dash no HUD; commits das Tasks 1 e 2); lint de fronteiras nos presets `wasm-games*` (o `DEPENDS` dos jogos não era conferido; commit da Task 3); e toda falta achada nas Tasks 3–6. *Feito no jogo por ser do jogo*: render em Canvas 2D por `Drawer`, colisão por força bruta, placar, ondas, aleatoriedade (`minstd_rand` com semente fixa no C++, `random` injetado no TS), números iniciais. *Contornado*: vazio, ou cada item com o porquê de a correção ter ficado para depois.

- [ ] **Step 2: `CLAUDE.md`** — árvore (`games/arena/` com `native/`, `src/`, `tests/`, `web/`; tira "ainda não existe"; `oly_add_game` gera também `arena_game.mjs`); comandos `make game G=<jogo>`, `make serve G=<jogo>` e o serviço `web` (`--service-ports`, porta 8000); `ts.arena.smoke` nos nomes do ctest; lint também nos presets `wasm-games*`. No spec, §4.9, o bundle parte de `games/arena/dist/src/main.js`.

- [ ] **Step 3: Verificação final** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`: tudo verde.

- [ ] **Step 4: Checklist manual** no Chromium com `make serve` (autor ou agente pelo MCP do Playwright). Cada item reprovado vira correção no jogo ou falta da engine (regra das faltas) antes do commit; o README diz quem fez a checagem.

- [ ] **Step 5: Commit** `docs: lista da fase 7, checklist manual do arena e CLAUDE.md`.
