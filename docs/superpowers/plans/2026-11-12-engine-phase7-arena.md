# Engine — Phase 7 (Validation game) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The shooter from §1.1 (`games/arena/`) playable in Chromium, with per-entity gameplay in C++ and render, HUD, hooks and waves in TS, plus the generic gaps it reveals in the engine (`single`/`trySingle` and the boundary lint on games).

**Architecture:** `single`/`trySingle` go first into the ECS `Query<Ts...>` and then into the `@oly/core` `Query`. The game is an npm workspace and an `oly_add_game` target in `games/arena/`: the schema generates the C++; `native/` holds the C++ systems and handlers registered by name; `src/game.ts` (`installGame`, no DOM) registers everything in the order of the §4.9 table; `src/main.ts` is the browser shell. The smoke test runs `installGame` in node against the real WASM, with a fake clock and fake input. esbuild bundles the TS for the browser and serves the static folder assembled by `make game`.

**Tech Stack:** C++20, GCC 14 (native), Emscripten 3.1.74 + node 20.18 (`wasm` and `web` services), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`, esbuild 0.24.2.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (phase 7 of §5; all of §4.9, §1.1, §2, §4.2 for `single`, §4.4 for the reference event flow, §4.7 for states and scope, §6, §7 "Validation game"). `§N` below refer to that spec.

## Global Constraints

- Branch `feat/engine-phase7-arena` from `develop`. Commits, comments, assert and error messages in English; commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain only in Docker: `DC="docker compose run --rm dev"` (native, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). `wasm-games*` presets require `make ts` first. In the emsdk image `npm` is not on the `PATH`: outside the `Makefile`, use `$WC sh -c '"$(dirname "$(command -v node)")/npm" …'`.
- `-Wall -Wextra -Wpedantic -Werror` on every C++ target; TS `strict` (§3).
- Layers: `core ← input ← app ← bridge ← games` and `core ← ecs ← app`. No new module. The game declares in `DEPENDS` every module it includes (`ecs app bridge`; `input` only if it includes `oly/input/...`).
- No game concepts in `engine/`. Game names (enemy, coin, dash, arena) only in `games/arena/`. The game does not import `@oly/core/internal`.
- **Gap rule (§4.9):** if the game needs something generic that the engine lacks, stop the task and treat it as an engine task in the same phase (C++ first, then C API and TS, tests in the module, death cases, `CLAUDE.md`/spec) and record it in the phase list (Task 7). Subagent executor: report the gap instead of working around it.
- Boundary: nothing per entity. `single` crosses a constant number of times per call; TS handlers receive a batch.
- No benchmark in this phase (§7).
- `CLAUDE.md` style and the `blank-lines`/`code-comments` skills: braces on every block, full names, data at the top of the class, one `namespace` per file, project includes with quotes after the standard library. Do not format by hand (`clang-format` hook). Comments only for a reason, an external constraint or a non-obvious contract; no narration of the edit; tests in Arrange/Act/Assert.

### Decisions the spec leaves open (settled here)

- **`single` in C++.** `std::tuple<Entity, Ts&...> single()` and `std::optional<std::tuple<Entity, Ts&...>> trySingle()` in `Query<Ts...>`, for `auto [player, position] = query.single();`. They sum the `count()` of the matched tables (O(tables)), without `IterationGuard`, because they do not call user code. Without asserts, `single()` with 0 rows is undefined behaviour (like `get` on a dead entity) and more than one row returns the first. Messages: `"single: %.*s with %u rows; expected 1"` and `"trySingle: %.*s with %u rows; expected 0 or 1"`, with `core::typeName<Query<Ts...>>()`.
- **`single` in TS without a new export.** `query.single()` uses `olyQueryBegin`/`olyQueryEnd` (two calls per query, constant): it sums the descriptors' `count`, reads the entity and copies the `read` and `write` components before `End`. §4.2 says "once per call"; the intent is "not per entity", and one more export would only save one crossing. Task 2 adjusts the sentence in the spec. The return is `SingleRow` (`entity` and `get(type)`, a copy like `World.get`); writing still goes through `set`.
- **The game's TS projects.** `games/arena/tsconfig.json` compiles `schema.ts` and `src/` except `main.ts`, with `lib: ["ES2022"]` and `types: []`: the game using the DOM is a compilation error. `tsconfig.browser.json` compiles only `src/main.ts` with `DOM`. `tests/tsconfig.json` uses `types: ["node"]`. All with `rootDir` at the game folder and output in `dist/` (tests in `dist-tests/`). That is why the bundle starts from `dist/src/main.js`, not from `dist/main.js` as in §4.9: `schema.ts` sits at the game root. Task 7 adjusts the path in the spec.
- **Constants on both sides.** Arena size and numbers used in both languages live in `native/arena.hpp` and `src/arena.ts`, each pointing to the other in a comment. A test (Task 4) pins the two together: the player pushed out ends up at `ArenaWidth − radius`.
- **Initial numbers** (§4.9: no measurement, tuned by playing; the comment says so):

| Item | Values |
|---|---|
| Arena | 960 × 540 |
| Player | radius 12, speed 220, health 3, weapon with 0.15 s interval; dash with speed 600, duration 0.15 s, recharge 0.8 s |
| Bullet | radius 3, speed 600 |
| Enemy | radius 12, health 1, chase 90 |
| Coin | radius 8; loose one every 2 s; 30% chance on an enemy's death |
| Waves | every 5 s, the first at 5 s; wave `n` (1, 2, …) has `2 + n` enemies |
| Invulnerable | 1 s after damage |
| Score | `score = 10 × kills + 5 × coins` |

- **Repeated deaths.** `collide` emits `EnemyDied` for every bullet that leaves health ≤ 0, so two bullets in the same step produce two deaths in the batch (the case §4.4 tells the game to handle). Handlers that count or create (`countKill`, `dropCoin`) ignore the repeated entity within the batch; `destroyDead` checks `isAlive`.
- **Randomness.** C++: `std::minstd_rand` with a fixed seed and `generator() % 100 < CoinDropPercent` (the `minstd_rand` sequence is the same in any standard library; the distributions are not). TS: the injected `random`.
- **`installGame(engine, { drawer, random })`** calls `engine.requestState(menu)` and returns `ArenaScore` (`kills`, `coins`, `score`, `finalScore: number | null`), a live read-only object. `endRound` writes `finalScore`; `startRound` resets everything.
- **Smoke test.** A new `Engine` per `test()`. Each `step` checks `checkInvariants() === null` ("at every step"). Arranged scenarios use `engine.world` between frames (direct spawn is persistent, §4.7); the test destroys whatever is left of the arrangements.
- **Web.** `make game`/`make serve` use `PRESET` only when it comes from the command line or the environment; otherwise, `wasm-games-release`. A preset outside `wasm-games*` is a `make` error. `serve` runs esbuild in server mode with `--servedir` on the assembled folder, port `0.0.0.0:8000`.
- **Lint on games.** No plan or commit records why the lint was left out of Emscripten (the modularization plan, Task 2 Step 5, only says "outside Emscripten"; commit `c967544` too). The likely reason is not running it twice in the WASM study presets. The condition becomes `NOT EMSCRIPTEN OR OLY_BUILD_GAMES`.
- **Manual checklist** in Chromium: by the author, or by the agent with the Playwright MCP at `http://localhost:8000`. The final report says who did it.
- **Out of scope:** render interpolation, camera, audio, gamepad, benchmark.

## Review Focus

1. **Two bullets kill the same enemy in the same step:** the score counts one death, at most one coin drops and nothing triggers an assert (Task 4).
2. **Arena size diverging between C++ and TS:** the player pushed out ends up at `ArenaWidth − radius`, `ArenaHeight − radius` (Task 4).
3. **Playing again after `gameOver`:** the previous round's `every` tasks were cancelled; at 2 s there is a single loose coin, not two, and the score starts at zero (Task 5).
4. **Render in `menu` and `gameOver`, without a player:** the HUD uses `trySingle` and does not throw (Task 5).
5. **Pause with an active dash:** the end of the dash freezes with the clock and happens in the remaining time after resuming (Task 5).

---

### Task 1: `single` and `trySingle` in the ECS `Query<Ts...>`

**Files:**
- Modify: `engine/runtime/ecs/include/oly/ecs/query.hpp`, `engine/runtime/ecs/tests/queries.cpp`, `engine/runtime/ecs/tests/contract_death.cpp`, `engine/runtime/ecs/tests/CMakeLists.txt` (`CASES`)

**Interfaces:**
- Produces (`oly::ecs::Query<Ts...>`): `std::tuple<Entity, Ts&...> single();` `std::optional<std::tuple<Entity, Ts&...>> trySingle();` (`Ts` const → `const T&`). Contracts and messages in the decisions above.

- [ ] **Step 1: Branch** `git switch -c feat/engine-phase7-arena`.

- [ ] **Step 2: Failing tests** in `tests/queries.cpp` (components from `support/components.hpp`; `CHECK_INVARIANTS(world)` at the end):
```cpp
TEST_CASE("single returns the entity and references of the only row") { /* spawn(A, B) and spawn(B) */ auto [entity, a, b] = world.query<A, const B>().single(); CHECK(entity == expected); a.value = 7; CHECK(world.get<A>(expected).value == 7); }
TEST_CASE("single finds the row in any matched table and respects With/Without") { /* 3 matched archetypes: one emptied by destroy, one excluded by Without<Tag> */ CHECK(std::get<0>(world.query<A>(oly::ecs::Without<Tag>{}).single()) == expected); }
TEST_CASE("trySingle: empty with no rows and the row with one") { CHECK(!world.query<A>().trySingle()); /* spawn */ CHECK(std::get<0>(*world.query<A>().trySingle()) == entity); }
```
  Death cases (`add-test-case` skill), with the required substrings: `single_empty` (`"single:"`, `"0 rows"`), `single_many` (`"single:"`, `"2 rows"`), `try_single_many` (`"trySingle:"`, `"2 rows"`).

- [ ] **Step 3: See it fail** — `$DC make test T='^ecs\.'` → compilation error (`single` does not exist).

- [ ] **Step 4: Implement** in `query.hpp` with a private helper that sums the rows and keeps the first non-empty table; the tuple comes from row 0 of that table, through the same columns as `callRows`.

- [ ] **Step 5: See it pass** — `$DC make test T='^ecs\.'` and `$WC make test PRESET=wasm-debug T='^ecs\.'`. Run the `perf-reviewer` agent on the `query.hpp` diff (`each`/`eachBlock` must not change in the assembly).

- [ ] **Step 6: Commit** `feat: single and trySingle in the ECS Query`.

---

### Task 2: `single` and `trySingle` in `@oly/core`

**Files:**
- Modify: `engine/packages/core/src/{query.ts,index.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/tests/wasm/query.test.ts`, `CLAUDE.md` (glossary), `docs/superpowers/specs/2026-11-09-game-engine-design.md` (§4.2, the sentence about the crossing)

**Interfaces:**
- Consumes: `Bridge.queryBegin/queryEnd`, `readScalar`, `assertWorldUsable`.
- Produces (`@oly/core`): `Query.single(): SingleRow` and `Query.trySingle(): SingleRow | null`; `class SingleRow { readonly entity: Entity; get<F extends Fields>(type: ComponentType<string, F>): ValueOf<F> }` (returns the copy made by the query; a type outside `read`/`write` throws `ContractError` `"get type=<T>: type is not in the query"`). Errors: `"single: query with <n> rows; expected 1"`, `"trySingle: query with <n> rows; expected 0 or 1"`; released `World`, the usual message. `public-exports.txt` gains `SingleRow`.

- [ ] **Step 1: Failing tests** in `engine/testing/tests/wasm/query.test.ts`:
```ts
test("single returns the entity and copies of the components of the only row", ...) // row.entity === player; deepEqual(row.get(Position), { x: 1, y: 2 }); changing the copy does not change world.get
test("single picks by the spec among several entities", ...)                         // with: [Player] and without: [Enemy]
test("trySingle returns null with no rows and the row with one", ...)
test("single inside another query's eachBlock", ...)                                // nested iteration closes in order; checkInvariants() === null
test("errors from single, trySingle and SingleRow.get", ...)                        // /single: query with 0 rows/, /2 rows/ in both, /get type=Velocity: type is not in the query/
```
  `surface.test.ts`: `Query` gains `single` and `trySingle`; `SingleRow` exported with `entity` and `get`.

- [ ] **Step 2: See it fail** — `$WC make games-test T='^ts\.'` → type error (`single` does not exist).

- [ ] **Step 3: Implement.** `#readSingle(operation, maxRows)` does the `queryBegin`, copies the descriptors like `eachBlock`, sums the `count`s, reads row 0 of the non-empty block and calls `queryEnd` in the `finally`; the `ContractError` is thrown after `End`, so the `World` is not left marked as iterating.

- [ ] **Step 4: Document.** `CLAUDE.md` glossary, new row: `| read the only row of a query (exactly one / at most one) | Query::single() -> tuple<Entity, Ts&...> / trySingle() -> optional | (olyQueryBegin/End) | query.single() / trySingle() -> SingleRow (copy) |`. In §4.2, replace "cross the boundary once per call" with "cross the boundary a constant number of times per call (`olyQueryBegin`/`End`)".

- [ ] **Step 5: See it pass** — `$WC make games-test` and `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`.

- [ ] **Step 6: Commit** `feat: single and trySingle in @oly/core`.

---

### Task 3: `arena` skeleton — build, schema, lint on games and entry into `menu`/`playing`

**Files:**
- Create: `games/CMakeLists.txt`, `games/arena/{CMakeLists.txt,package.json,tsconfig.json,schema.ts}`, `games/arena/src/{arena.ts,game.ts,render.ts}`, `games/arena/tests/{tsconfig.json,harness.ts,smoke.test.ts}`
- Modify: `CMakeLists.txt` (root), `tsconfig.json` (root), `package-lock.json`

**Interfaces:**
- Consumes: `query.single/trySingle` (Task 2).
- Produces:
  - `schema.ts`: components, tags, events, actions and states from §4.9 with those names and fields (`Health.value: "i32"`, `Dash.ready/active: "u8"`, event fields `entity`/`coin` of type `"entity"`, the rest `"f32"`), exported one by one and in `defineSchema` (default).
  - `src/arena.ts`: the constants from the numbers table (`ArenaWidth`, `ArenaHeight`, `PlayerRadius`, `PlayerSpeed`, `PlayerHealth`, …, `KillPoints`, `CoinPoints`).
  - `src/render.ts` (only the interface in this task): `interface Drawer { clear(): void; circle(x: number, y: number, radius: number, color: string): void; rect(x: number, y: number, width: number, height: number, color: string): void; text(x: number, y: number, content: string, color: string): void }`.
  - `src/game.ts`: `interface GameOptions { readonly drawer: Drawer; readonly random: () => number }`; `interface ArenaScore { readonly kills: number; readonly coins: number; readonly score: number; readonly finalScore: number | null }`; `installGame(engine: Engine, options: GameOptions): ArenaScore`. In this task: bindings (`move` WASD, `shoot` MouseLeft, `dash` Space, `pause` Escape, `confirm` Enter and MouseLeft), `startOnConfirm` (`updateBefore`, `inState(menu, gameOver)`), `onEnter(playing, "startRound")` (resets the score, creates the player in the centre with the values from the table) and `requestState(menu)`.
  - `tests/harness.ts`: `loadArena(): Promise<Runtime>` (factory from `OLY_ARENA_MODULE`, like `fixtureFactory`); `interface ArenaTest { engine; score; drawer: RecordingDrawer; step(count?: number): void; stepSeconds(seconds: number): void; tap(button: ButtonName): void; hold(button: ButtonName): void; release(button: ButtonName): void; count(...types: SchemaType[]): number; player(): Entity }`; `withArena(runtime, body, random = sequenceRandom([...]))` creates the `Engine` (`fixedHz: 60`), calls `installGame`, `frame(0)`, runs `body` and releases. `tap` = `buttonDown` + `buttonUp` + `step()`: a short tap, which gives `pressed` but not `held` (§4.5); to shoot (`shoot.held`) use `hold` + `step` + `release`. `count` sums the `count`s of `query({ with: types }).eachBlock`; without types, it counts all entities. `player()` = `query({ with: [Player] }).single().entity`. `RecordingDrawer` keeps the calls since the last `clear` (`texts()`, `circles()`). `sequenceRandom(values)` repeats the list.
  - CMake: `games/arena/CMakeLists.txt` with `file(GLOB nativeSources CONFIGURE_DEPENDS native/*.cpp)` and `oly_add_game(arena_game SCHEMA_JS ${CMAKE_CURRENT_SOURCE_DIR}/dist/schema.js SOURCES ${nativeSources} DEPENDS ecs app bridge)`; with `OLY_BUILD_TESTS`, `ts.arena.smoke` (`${OLY_NODE} --test ${CMAKE_CURRENT_SOURCE_DIR}/dist-tests/`, `ENVIRONMENT OLY_ARENA_MODULE=$<TARGET_FILE:arena_game>`). Root: `if(OLY_BUILD_GAMES) add_subdirectory(games) endif()` right after `add_subdirectory(engine)` (before the boundary manifest), and the lint registered with `if(NOT EMSCRIPTEN OR OLY_BUILD_GAMES)`.
  - `package.json`: `{ "name": "arena", "private": true, "version": "0.1.0", "type": "module", "dependencies": { "@oly/core": "*", "@oly/client": "*" } }`. `tsconfig.json` references `engine/packages/core`; `tests/tsconfig.json` references `..` and the core. The root gains `games/arena` and `games/arena/tests`.

- [ ] **Step 1: Failing tests** in `tests/smoke.test.ts`:
```ts
test("installGame enters menu on the first step, without a player", ...)     // step(); engine.state?.name === "menu"; count(Player) === 0
test("Enter in menu starts: playing with a player in the centre, health 3 and score at zero", ...)
  // tap("Enter"); step(); state playing; player() exists; get(Position) = { x: 480, y: 270 }; get(Health).value === 3; score deepEqual { kills: 0, coins: 0, score: 0, finalScore: null }
test("a click (MouseLeft) in menu also starts", ...)
```

- [ ] **Step 2: See it fail** — `$WC sh -c '"$(dirname "$(command -v node)")/npm" install --no-audit --no-fund'` (links the workspace and updates the lock), then `$WC make games-test T='arena'` → type error (`installGame` does not exist).

- [ ] **Step 3: Implement** the files in the Interfaces list.

- [ ] **Step 4: See it pass** — `$WC make games-test` (includes `ts.arena.smoke`, `oly.lint.boundaries` and `oly.lint.boundaries_selftest`; `ctest --preset wasm-games -N | grep lint` lists both). `$DC make test` stays green and without `ts.arena.smoke`.

- [ ] **Step 5: Commit** `feat: arena skeleton with schema, entry into menu and boundary lint on games`.

---

### Task 4: arena C++ gameplay and handlers

**Files:**
- Create: `games/arena/native/{arena.hpp,movement.cpp,combat.cpp}`
- Modify: `games/arena/src/{arena.ts,game.ts}`, `games/arena/tests/smoke.test.ts`

**Interfaces:**
- Consumes: `Query::single` (Task 1); `schema.generated.hpp` (structs, `actions::`, `states::`); `installGame` and the harness (Task 3).
- Produces (C++, names equal to those of `addNativeSystem`/`addNativeHandler`):
  - `arena.hpp`: `inline constexpr` constants used by the C++ (`ArenaWidth`, `ArenaHeight`, `BulletSpeed`, `BulletRadius`, `CoinRadius`, `InvulnerableSeconds`, `CoinDropPercent`); `struct PlayerTarget { oly::ecs::Entity player; };` (task payload); `struct Direction { float x; float y; };` and `Direction directionTo(float fromX, float fromY, float toX, float toY)` (normalized; `{0, 0}` at distance 0).
  - `movement.cpp`: `OLY_SYSTEM(steerPlayer)`, `OLY_SYSTEM(chasePlayer)`, `OLY_SYSTEM(integrate)`, `OLY_SYSTEM(confine)`; tasks `endDash` (`active = 0`) and `rechargeDash` (`ready = 1`), which check `isAlive`.
  - `combat.cpp`: `OLY_SYSTEM(fireWeapon)`, `OLY_SYSTEM(collide)`, `OLY_HANDLER(dropCoin, EnemyDied)`, `OLY_HANDLER(destroyDead, EnemyDied)`; task `endInvulnerability` (removes `Invulnerable` if alive).
- Produces (TS, in `installGame`, in the order of the §4.9 table): `addNativeSystem` of `steerPlayer` (`updateBefore`), `fireWeapon`, `chasePlayer`, `integrate` (`update`), `collide`, `confine` (`updateAfter`), all `inState(playing)`; `EnemyDied`: `addNativeHandler(dropCoin)`, `on(countKill)`, `addNativeHandler(destroyDead)`; `CoinCollected`: `on(collectCoin)` (checks `isAlive`, direct `destroy`, `coins += 1`); `PlayerDied`: `on(endRound)` (`finalScore = score`, `requestState(gameOver)`).

Rules the §4.9 table does not fix:
- `steerPlayer`: with `active == 0`, velocity = `move × PlayerSpeed`. With `dash.pressed` and `ready == 1`: direction = `move` if non-zero, otherwise `directionTo(player, aim)` if `aim.valid`; a zero direction does not dash (the recharge is not spent). With a direction: `ready = 0`, `active = 1`, velocity = direction × `dash.speed`, `after(dash.duration, endDash)` and `after(dash.recharge, rechargeDash)`.
- `fireWeapon`: `cooldown = max(0, cooldown − step)`; with `shoot.held`, `cooldown == 0` and a non-zero direction towards `aim`, creates a `Bullet` at the player's position with velocity `BulletSpeed` and `cooldown = interval`.
- `collide` (brute-force pairs):
```
for each bullet: for each enemy it touches (distance ≤ sum of the radii):
    health -= 1; deferred destroy(bullet); if health ≤ 0: emit EnemyDied{enemy, x, y}; stop (one bullet hits one enemy)
if the player has no Invulnerable and touches some enemy:
    health -= 1; deferred add Invulnerable; after(InvulnerableSeconds, endInvulnerability); if health ≤ 0: emit PlayerDied{player}
for each coin touching the player: emit CoinCollected{coin}
```
- `confine`: player kept within `[radius, ArenaWidth − radius] × [radius, ArenaHeight − radius]`; a bullet with a position outside `[0, ArenaWidth] × [0, ArenaHeight]` is destroyed.
- `dropCoin`: a repeated entity in the batch is ignored (vector of seen entities, per batch); with `generator() % 100 < CoinDropPercent`, creates a `Coin` with `Position {x, y}` and `Collider {CoinRadius}` through the direct context (with the `playing` scope). `countKill` ignores repeats with a `Set<Entity>` per batch.

- [ ] **Step 1: Failing tests** in `tests/smoke.test.ts` (each starts from `playing`; enemies, bullets and coins arranged by `engine.world.spawn`, with `Chaser.speed = 0` when the enemy must stay still):
```ts
test("WASD moves the player at 220 px/s; normalized diagonal", ...)            // hold KeyD: Velocity.x === 220; KeyD + KeyS: length 220
test("dash: 600 impulse in the movement direction, ends at 0.15 s and recharges at 0.8 s", ...)
  // hold KeyD; tap Space: active 1, ready 0, Velocity.x 600; stepSeconds(0.2): active 0, Velocity.x 220; at 0.5 s ready 0; at 0.9 s ready 1
test("a standing dash goes towards the pointer; without a valid pointer it does not spend the recharge", ...)
test("chasePlayer points the enemy's velocity at the player with the Chaser speed", ...)
test("confine keeps the player at ArenaWidth − radius and destroys the bullet outside the arena", ...)
  // set Position { x: 5000, y: -50 }; step: { x: ArenaWidth - PlayerRadius, y: PlayerRadius }; one shot (hold MouseLeft, step, release) to the right with no enemy: count(Bullet) === 0 after 2 s
test("a shot towards the pointer kills the enemy with no leftover bullet or enemy", ...)
  // pointerMove 200 px to the right; enemy standing still 100 px away; hold MouseLeft, step, release; stepSeconds(0.5): kills 1, isAlive false, count(Bullet) 0
test("two bullets on the same enemy in the same step: one death on the score and at most one coin", ...)
  // two still bullets over an enemy with health 1; step: kills 1, isAlive false, count(Bullet) 0, count(Coin) ≤ 1
test("a coin touching the player disappears and adds to the score", ...)      // coins 1, score 5
test("damage gives 1 s of invulnerability; health 0 leads to gameOver without a player", ...)
  // enemy standing on the player: health 2 and Invulnerable; 0.5 s later still 2; 1.1 s later 1; set health 1 → gameOver, count(Player) 0, finalScore === score
```

- [ ] **Step 2: See it fail** — `$WC make games-test T='arena'` → the new cases fail (no native system registered).

- [ ] **Step 3: Implement** the C++ and the registration in `installGame`. The numbers in `arena.hpp` and `arena.ts` carry the comment that they are initial and unmeasured, and where their counterpart on the other side lives.

- [ ] **Step 4: See it pass** — `$WC make games-test` (includes `oly.lint.boundaries`) and `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`.

- [ ] **Step 5: Commit** `feat: arena C++ gameplay with death, coin and end-of-round handlers`.

---

### Task 5: Pause, waves, loose coins and render

**Files:**
- Modify: `games/arena/src/{arena.ts,game.ts,render.ts}`, `games/arena/tests/smoke.test.ts`

**Interfaces:**
- Consumes: `installGame` and handlers (Tasks 3 and 4); `query.trySingle` (Task 2).
- Produces:
  - `render.ts`: `createRenderSystem(drawer: Drawer, score: ArenaScore): SystemFunction`, registered in `Phase.Render` without `inState`. Draws a circle per entity with `Position` and `Collider`, one query per tag (player in another colour when `Invulnerable`). HUD with `trySingle({ read: [Health, Dash], with: [Player] })`. Exact texts: menu `"OLY ARENA"` and `"Enter or click to play"`; HUD `` `Health ${health}` ``, `` `Score ${score}` ``, `"Dash ready"`/`"Dash recharging"`; pause `"Paused — Esc to continue"`; end `` `Game over — ${finalScore} points` `` and `"Enter or click to play again"`. No interpolation (§4.9).
  - `game.ts`: `pauseOnPress` (`inState(playing)`, `pause.pressed` → `pause()`) and `resumeOnPress` (`whilePaused().inState(playing)`, `resume()`), registered between `startOnConfirm` and `steerPlayer`. `startRound` schedules `every(WaveInterval, spawnWave)` and `every(CoinInterval, spawnLooseCoin)`; the wave counter goes back to 0 on each `startRound`. `spawnWave` creates `2 + n` enemies positioned on an edge drawn by `random` (side = `floor(random() × 4)`, coordinate along it = `random() × length`). `spawnLooseCoin` creates a coin in `[CoinRadius, ArenaWidth − CoinRadius] × [CoinRadius, ArenaHeight − CoinRadius]`. Both receive a `TaskContext` (spawn with the `playing` scope).

- [ ] **Step 1: Failing tests** in `tests/smoke.test.ts`:
```ts
test("Esc pauses: clock, positions and dash freeze; Esc again resumes and the dash ends in the remaining time", ...)
  // hold KeyD; tap Space; tap Escape; step(60): engine.time and Position unchanged, Dash.active 1; tap Escape; step(3): active 1; step(10): active 0
test("waves every 5 s with 2 + n enemies on the edges; one loose coin every 2 s inside the arena", ...)
  // player health at 1000 (the enemies reach it before 10 s); stepSeconds(4.9): count(Enemy) 0, count(Coin) 2;
  // step by step until count(Enemy) > 0: 3 enemies, each ≤ 2 px from an edge (one step moves 1.5 px); stepSeconds(5): count(Enemy) 7
test("render: menu without a player, HUD in playing, pause and game over", ...)
  // menu: texts() includes "Enter or click to play", circles() 0; playing: "Health 3", "Score 0", "Dash ready", circles() === count(Collider); pause: "Paused — Esc to continue"
test("script: menu → playing → wave → death → gameOver with an empty world → play again from scratch", ...)
  // wait for the wave; player with health 1 brought to an enemy: gameOver, count() === 0 (no entities), texts() includes "Game over — <finalScore> points";
  // tap Enter; step: kills 0, coins 0, score 0, count(Player) 1; stepSeconds(2.05): count(Coin) === 1
```

- [ ] **Step 2: See it fail** — `$WC make games-test T='arena'` → type error (`createRenderSystem` does not exist).

- [ ] **Step 3: Implement.**

- [ ] **Step 4: See it pass** — `$WC make games-test` and `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`.

- [ ] **Step 5: Commit** `feat: arena pause, waves, loose coins and render`.

---

### Task 6: Browser shell, esbuild, `make game`/`make serve` and `web` service

**Files:**
- Create: `games/arena/src/main.ts`, `games/arena/tsconfig.browser.json`, `games/arena/web/index.html`
- Modify: `games/arena/tsconfig.json` (`exclude: ["src/main.ts"]`; `tsconfig.browser.json` references `./tsconfig.json`, the core and the client), `tsconfig.json` (root, + `games/arena/tsconfig.browser.json`), `package.json` + `package-lock.json` (root), `Makefile`, `docker-compose.yaml`

**Interfaces:**
- Consumes: `installGame`, `Drawer`, `ArenaWidth`/`ArenaHeight`; `@oly/client` (`moduleFactoryFromUrl`, `runInBrowser`, `attachBrowserInput`); `loadRuntime`.
- Produces:
  - `main.ts`: gets the `<canvas id="arena">`, sets `width`/`height` to `ArenaWidth`/`ArenaHeight`, builds a `Drawer` over `CanvasRenderingContext2D` (`fillRect`, `arc`, `fillText`), `loadRuntime(moduleFactoryFromUrl("./arena_game.mjs"), schema)`, `createEngine({ fixedHz: 60 })`, `installGame(engine, { drawer, random: Math.random })`, `attachBrowserInput(engine, canvas)`, `runInBrowser(engine)`. A boot error goes to `console.error` and to the page text.
  - `web/index.html`: the canvas and `<script type="module" src="./main.js">`.
  - Root: `esbuild` pinned to `0.24.2` in `devDependencies`.
  - `Makefile`:
```make
# game/serve only work with the wasm-games* presets: without PRESET on the command line or in the environment, Release applies.
WEB_PRESET = $(if $(filter command line environment,$(origin PRESET)),$(PRESET),wasm-games-release)
WEB_DIR = build/$(WEB_PRESET)/web/$(G)
```
    `game`: requires `G` and `WEB_PRESET` in `wasm-games*` (otherwise `$(error ...)`); depends on `ts`; `$(MAKE) build PRESET=$(WEB_PRESET)`; copies `games/$(G)/web/index.html` and `build/$(WEB_PRESET)/games/$(G)_game.{mjs,wasm}` to `$(WEB_DIR)`; `node_modules/.bin/esbuild games/$(G)/dist/src/main.js --bundle --format=esm --platform=browser --target=es2022 --outfile=$(WEB_DIR)/main.js`. `serve`: depends on `game`; the same esbuild with `--servedir=$(WEB_DIR) --serve=0.0.0.0:8000`.
  - `docker-compose.yaml`: `web` service equal to `wasm` (image from `Dockerfile.wasm`, same volumes, `command: bash`) plus `ports: ["8000:8000"]`.

- [ ] **Step 1: See it fail** — `$WC make game G=arena` → `make: *** No rule to make target 'game'`.

- [ ] **Step 2: Implement.** Install esbuild with `$WC sh -c '"$(dirname "$(command -v node)")/npm" install --save-dev --save-exact esbuild@0.24.2 --no-audit --no-fund'`.

- [ ] **Step 3: Check the page build.**
  - `$WC make game G=arena` → `build/wasm-games-release/web/arena/` has `index.html`, `main.js`, `arena_game.mjs` and `arena_game.wasm`; `grep -c '@oly/' build/wasm-games-release/web/arena/main.js` → `0` (everything in the bundle).
  - `$WC make game G=arena PRESET=debug-asan` → `make` error mentioning `wasm-games`.
  - `$WC make games-test` green (the `tsc` of `main.ts` with DOM and the game's one without DOM).

- [ ] **Step 4: Check the server.** In the background, `docker compose run --rm --service-ports web make serve G=arena`; on the host, `curl -s http://localhost:8000/ | grep -c 'id="arena"'` → `1` and `curl -sI http://localhost:8000/arena_game.wasm` → `200` with `Content-Type: application/wasm` (if another type comes back, Emscripten falls back to instantiation through an `ArrayBuffer`: note it in the README). Stop the container.

- [ ] **Step 5: Commit** `feat: arena browser shell with esbuild, make game/serve and web service`.

---

### Task 7: Phase list, manual checklist and documentation

**Files:**
- Create: `games/arena/README.md`
- Modify: `CLAUDE.md`, `docs/superpowers/specs/2026-11-09-game-engine-design.md` (§4.9, the `dist/src/main.js` path)

- [ ] **Step 1: `games/arena/README.md`** with three sections:
  - **How to run:** `$WC make games-test` (includes `ts.arena.smoke`); `docker compose run --rm --service-ports web make serve G=arena` and open `http://localhost:8000` in Chromium; `make game G=arena` for the static folder.
  - **Manual checklist (Chromium):** the page opens with no console error and shows the menu; Enter and click start; WASD moves and the diagonal is not faster; holding the left button shoots towards the mouse; right click does not open a menu and Space does not scroll the page; Space dashes and the HUD shows the recharge; waves on the edges every 5 s chase the player; a shot kills and sometimes a coin drops; loose coins appear and add points; Esc pauses and resumes with everything frozen; switching tabs and coming back does not make the game jump; damage leaves the player invulnerable for 1 s; 3 hits lead to game over with the points; Enter plays again with the score at zero and a clean arena; F5 reloads. If it stutters, investigate with the Chromium profiler and record it in the list.
  - **The phase list (§4.9):** *missing from the engine (added)*: `single`/`trySingle` in C++ and TS (player position in `chasePlayer` and `fireWeapon`, health and dash in the HUD; commits from Tasks 1 and 2); boundary lint on the `wasm-games*` presets (the games' `DEPENDS` was not checked; commit from Task 3); and every gap found in Tasks 3–6. *Done in the game because it belongs to the game*: Canvas 2D render through `Drawer`, brute-force collision, score, waves, randomness (`minstd_rand` with a fixed seed in C++, injected `random` in TS), initial numbers. *Worked around*: empty, or each item with why the fix was left for later.

- [ ] **Step 2: `CLAUDE.md`** — tree (`games/arena/` with `native/`, `src/`, `tests/`, `web/`; remove "does not exist yet"; `oly_add_game` also generates `arena_game.mjs`); commands `make game G=<game>`, `make serve G=<game>` and the `web` service (`--service-ports`, port 8000); `ts.arena.smoke` in the ctest names; lint also on the `wasm-games*` presets. In the spec, §4.9, the bundle starts from `games/arena/dist/src/main.js`.

- [ ] **Step 3: Final verification** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`: all green.

- [ ] **Step 4: Manual checklist** in Chromium with `make serve` (author or agent through the Playwright MCP). Each failed item becomes a fix in the game or an engine gap (gap rule) before the commit; the README says who did the check.

- [ ] **Step 5: Commit** `docs: phase 7 list, arena manual checklist and CLAUDE.md`.
