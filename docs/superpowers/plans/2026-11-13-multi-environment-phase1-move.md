# Multi-environment, phase 1 (move) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the repository to the core/environments tree of the multi-environment design, with no behaviour change.

**Architecture:** Four renames, each a `git mv` plus the edits that keep the build, the boundary lint, the npm
workspaces and the docs pointing at the new place: the C++ module `bridge` becomes `capi`; `@oly/core` moves to
`engine/environments/web/clients/typescript/core/`; `@oly/client` becomes `@oly/web-host` in
`engine/environments/web/host/js/`; the arena's TS package moves into `games/arena/web/`. Folders that only phases 2–4
fill (`engine/schema/`, `environments/web/host/native/`, `environments/native/`) are not created now.

**Tech Stack:** C++20 + CMake presets, Emscripten, TypeScript 5.6 project references, npm workspaces, Python boundary
lint, Docker Compose (`dev`, `wasm`).

**Spec:** `docs/superpowers/specs/2026-11-13-multi-environment-design.md` (§3.2, §3.3, §8 phase 1).

## Global Constraints

- The toolchain runs only in Docker: `DC="docker compose run --rm dev"`, `WC="docker compose run --rm wasm"`.
- No behaviour change: every existing test passes after each task and `arena` runs on `web`.
- Glossary rule: no alias, no compatibility `using`, no alias package. `oly::bridge`, `oly::capi`-and-`oly::bridge`
  side by side, or an `@oly/client` that re-exports `@oly/web-host` are all forbidden.
- New names: namespace `oly::capi`, target `oly::capi` (`oly_capi`), include `oly/capi/...`, ctest prefix `capi.`;
  npm package `@oly/web-host`; `@oly/core` keeps its name.
- Commits, comments and assert messages in English. Commit subject: one short line on what changes in the repository,
  conventional prefix as in `git log`; never cite the spec, the plan or task numbers.
- Do not touch `docs/superpowers/` (historical specs and plans) nor `docs/benchmarks/` (recorded results; its
  `bridge/` folder names the WASM↔TS boundary that was measured, and the commands there keep working).
- Docs that name a moved path or a renamed public name are updated in the task that moves it (the `CLAUDE.md` guide
  rule). Only names and paths change; the guide's reorganization is phase 5.
- `.cpp`/`.hpp` edited through `sed` skip the clang-format hook: each C++ task runs clang-format on the files it touched.

**Names that stay** (decided here, not in the spec): the TS internal class `Bridge`, `bridge.ts`, `bridgeOf` and the
`@oly/core/internal` exports. They are the JS side of the WASM↔JS boundary, which is still a bridge on `web`; the
rename applies to the C++ module only. `ts.core.unit`, `ts.codegen.unit`, `ts.testing.wasm` and `ts.arena.smoke` keep
their names; `ts.client.unit` becomes `ts.web_host.unit`.

## Review Focus

1. **A stale `node_modules/@oly/*` link or an old `dist/` left at the old path.** `make ts` runs `npm ci` only when
   `node_modules` is missing, and `engine/packages/core/dist/` survives `git mv` (it is ignored). A link to the old
   folder would build and test against stale output. Expected: after each TS move, the old folder is gone and
   `readlink` of the link names the new path (Tasks 2–4).
2. **`package-lock.json` with the old workspace paths.** A clean checkout runs `npm ci`, which fails on a lock that
   does not match the workspaces. Expected: `npm ci` from an empty `node_modules` passes (Task 5).
3. **The boundary lint going silent.** If `check_boundaries.py` keeps the `engine/packages/core/` prefix, the rule
   "core cannot import the host" matches nothing and passes forever. Expected: a selftest case with the new paths
   fails before the lint changes (Tasks 2–3).
4. **The guide's new-game files at the wrong depth.** A game now lives one folder deeper (`games/<game>/web/`); a guide
   snippet with `../../` instead of `../../../` breaks the reader's first build. Expected: the guide's `extends` and
   `references` strings are the arena's, character for character (Task 4).
5. **Old build directories hiding a broken configure.** `build/<preset>/` keeps old executables and cache entries.
   Expected: the final run uses fresh build directories (Task 5).

---

### Task 1: The C++ module `bridge` becomes `capi`

**Files:**
- Move: `engine/runtime/bridge/` → `engine/runtime/capi/`; inside it `include/oly/bridge/` → `include/oly/capi/`
- Modify: `engine/runtime/CMakeLists.txt`, `engine/runtime/capi/CMakeLists.txt`, `engine/runtime/capi/tests/CMakeLists.txt`
- Modify: every file under `engine/runtime/capi/` (includes, namespace, test identifiers)
- Modify: `engine/testing/CMakeLists.txt`, `engine/testing/fixture/systems.cpp`, `games/arena/CMakeLists.txt`
- Modify: `engine/tools/codegen/src/emit_cpp.ts`, `engine/tools/codegen/tests/generator.test.ts`
- Modify: `tools/test_check_boundaries.py` (fixture module names only)
- Modify docs: `README.md`, `docs/architecture/{README,repository,modules-and-boundaries,build-and-tests,bridge,schema-and-codegen,app,ecs,input,limits,performance,decisions}.md` where they name the module, `docs/development/{commands,guide-style}.md`, `docs/guide/{01-getting-started,10-cpp-gameplay}.md`, `.claude/skills/add-test-case/SKILL.md`

**Interfaces:**
- Produces: module `capi` (`oly_add_module(NAME capi DEPENDS app input ecs core OBJECT)`), alias `oly::capi`,
  headers `oly/capi/{c_api,engine,engine_api,schema_registry,world}.hpp`, namespace `oly::capi` (`worldOf`,
  `engineOf`, `schemaHash`, `schemaEvents`, `schemaActions`, `schemaStates`, `setScriptInvoker`, ...), ctest names
  `capi.c_api`, `capi.engine_api`, `capi.contract_death.<case>`, executable `capi_contract_death`.
- Produces: the generated `schema_registry.generated.cpp` includes `oly/capi/schema_registry.hpp` and opens
  `namespace oly::capi`.
- Games and the fixture declare `DEPENDS ecs app capi`.

- [ ] **Step 1: Write the failing tests**

In `engine/tools/codegen/tests/generator.test.ts`, change the registry assertion to:

```ts
assert.match(registry, /namespace oly::capi/);
assert.match(registry, /#include "oly\/capi\/schema_registry.hpp"/);
```

- [ ] **Step 2: Run them to verify they fail**

Run: `$WC sh -c 'make ts && node --test engine/tools/codegen/dist-tests/'`
Expected: FAIL, the registry still says `oly::bridge`.

Run: `$DC make test T='^capi\.'`
Expected: FAIL with `No tests were found`.

- [ ] **Step 3: Rename the module**

`git mv engine/runtime/bridge engine/runtime/capi`, then `git mv engine/runtime/capi/include/oly/bridge engine/runtime/capi/include/oly/capi`.

Inside `engine/runtime/capi/`: `oly/bridge/` → `oly/capi/` and `oly::bridge` → `oly::capi` in every file; in
`tests/` also `Bridge` → `Capi` and `bridge` → `capi` (test identifiers `CapiTestEvent`, `capiTestNative`,
`"capiJump"`, the usage string `capi_contract_death`, `MODULE capi`). Module `CMakeLists.txt`: `NAME capi`.
`engine/runtime/CMakeLists.txt`: `add_subdirectory(capi)`. `src/engine_api.cpp` keeps its `EM_JS` import (phase 2).

Consumers: `DEPENDS ecs app capi` in `engine/testing/CMakeLists.txt` and `games/arena/CMakeLists.txt`; includes and
`oly::capi::` in `engine/testing/fixture/systems.cpp`; include and namespace in the template of `emit_cpp.ts`. In
`tools/test_check_boundaries.py`, the synthetic module `bridge` / `engine/runtime/bridge` becomes `capi` /
`engine/runtime/capi` (the `@oly/core/internal` `Bridge` imports there stay).

Then: `$DC sh -c 'find engine/runtime/capi engine/testing -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i'`.

- [ ] **Step 4: Update the docs**

Every mention that names the C++ module, its path, namespace, target, header or ctest prefix becomes `capi`
(`oly::capi::worldOf`, `engine/runtime/capi/include/oly/capi/c_api.hpp`, `capi.*`, `DEPENDS ecs app capi`). Prose
about the WASM↔JS boundary and the TS `Bridge` stays. `docs/architecture/bridge.md` keeps its file name (renamed in
phase 5); its content uses the new names.

- [ ] **Step 5: Verify no old name is left**

Run:

```bash
git grep -n -E 'oly/bridge|oly::bridge|oly_bridge|runtime/bridge|bridge_contract_death|MODULE bridge|NAME bridge|app bridge\)|bridge\.\*' -- . ':!docs/superpowers' ':!docs/benchmarks'
```

Expected: no output.

- [ ] **Step 6: Run the tests**

Run: `$DC make test`
Expected: PASS, with `capi.c_api`, `capi.engine_api` and the `capi.contract_death.*` cases listed.

Run: `$WC make games-test`
Expected: PASS (`ts.codegen.unit`, `ts.testing.wasm`, `ts.arena.smoke`, `oly.lint.boundaries*`).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: rename the bridge module to capi"
```

---

### Task 2: `@oly/core` moves to `engine/environments/web/clients/typescript/core/`

**Files:**
- Move: `engine/packages/core/` → `engine/environments/web/clients/typescript/core/`
- Modify: its `tsconfig.json` and `tests/tsconfig.json` (`extends` depth)
- Modify: `tsconfig.json`, `package.json` (workspaces), `package-lock.json`, `engine/CMakeLists.txt` (`ts.core.unit` path)
- Modify: `engine/packages/client/tsconfig.json`, `engine/packages/client/tests/tsconfig.json`, `engine/testing/tsconfig.json`, `engine/tools/codegen/tsconfig.json`, `engine/tools/codegen/tests/tsconfig.json`, `games/arena/tsconfig.json`, `games/arena/tsconfig.browser.json`, `games/arena/tests/tsconfig.json` (references)
- Modify: `tools/check_boundaries.py`, `tools/test_check_boundaries.py`
- Modify docs: `README.md`, `docs/architecture/{repository,modules-and-boundaries,ts-packages}.md`, `docs/development/guide-style.md`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces: `@oly/core` at `engine/environments/web/clients/typescript/core/` (same package name, same exports).
  Relative paths from it: `extends` = `../../../../../../tsconfig.base.json`; from its `tests/`, seven `../`.
- Produces: workspace entry `"engine/environments/web/clients/typescript/core"` (explicit, not a glob: the TS emitter
  of phase 3 lands next to it in `typescript/codegen/` with no `package.json`).

- [ ] **Step 1: Write the failing test**

In `tools/test_check_boundaries.py`, `test_core_importing_client_fails` writes the file at
`engine/environments/web/clients/typescript/core/src/a.ts` and asserts
`"engine/environments/web/clients/typescript/core/src/a.ts:1: @oly/core cannot depend on @oly/client"`.

- [ ] **Step 2: Run it to verify it fails**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: FAIL in `test_core_importing_client_fails` (the lint still checks `engine/packages/core/`).

- [ ] **Step 3: Move the package and point everything at it**

`git mv engine/packages/core engine/environments/web/clients/typescript/core`. In `check_boundaries.py`, the
`inside_core` prefix becomes `engine/environments/web/clients/typescript/core/`. Update the `extends` depths, every
`references` path listed under Files, the root `tsconfig.json`, the workspaces in `package.json` and the
`ts.core.unit` path in `engine/CMakeLists.txt`.

Remove what the move left behind and relink: `git ls-files engine/packages/core` must print nothing, then
`rm -rf engine/packages/core node_modules` and
`$WC sh -c '"$(dirname "$(command -v node)")/npm" install --no-audit --no-fund'` (regenerates `package-lock.json`).

- [ ] **Step 4: Update the docs**

Each path `engine/packages/core/...` becomes `engine/environments/web/clients/typescript/core/...`;
`repository.md`'s tree shows `engine/environments/web/clients/typescript/core/` instead of `packages/core/`.

- [ ] **Step 5: Verify**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: PASS.

Run: `$WC sh -c 'readlink node_modules/@oly/core'` 
Expected: `../../engine/environments/web/clients/typescript/core`.

Run: `git grep -n 'packages/core' -- . ':!docs/superpowers' ':!docs/benchmarks'`
Expected: no output.

Run: `$WC make games-test`
Expected: PASS, `ts.core.unit` included.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: move @oly/core under the web environment"
```

---

### Task 3: `@oly/client` becomes `@oly/web-host` in `engine/environments/web/host/js/`

**Files:**
- Move: `engine/packages/client/` → `engine/environments/web/host/js/`
- Modify: its `package.json` (`"name": "@oly/web-host"`), `tsconfig.json`, `tests/tsconfig.json`
- Modify: `tsconfig.json`, `package.json` (workspaces), `package-lock.json`, `engine/CMakeLists.txt`
- Modify: `games/arena/package.json`, `games/arena/src/main.ts`, `games/arena/tsconfig.browser.json`
- Modify: `engine/environments/web/clients/typescript/core/src/internal.ts` (comment listing the sibling packages)
- Modify: `tools/check_boundaries.py`, `tools/test_check_boundaries.py`
- Modify docs: `README.md`, `docs/architecture/{README,repository,modules-and-boundaries,ts-packages,decisions,games}.md`, `docs/development/{commands,guide-style}.md`, `docs/guide/{README,01-getting-started,05-engine-and-systems,07-input,cheat-sheet}.md`

**Interfaces:**
- Consumes: `@oly/core` at `engine/environments/web/clients/typescript/core/` (Task 2).
- Produces: npm package `@oly/web-host` with the same exports as `@oly/client` today (`runInBrowser`,
  `attachBrowserInput`, `moduleFactoryFromUrl` and their types). From `engine/environments/web/host/js/`:
  `extends` = `../../../../../tsconfig.base.json`, core reference = `../../clients/typescript/core`; from its
  `tests/`: one more `../` each.
- Produces: ctest `ts.web_host.unit` (replaces `ts.client.unit`), workspace entry `"engine/environments/web/host/js"`.
- Produces: lint message `@oly/core cannot depend on @oly/web-host`.

- [ ] **Step 1: Write the failing tests**

In `tools/test_check_boundaries.py`:
- `test_core_importing_client_fails` is renamed `test_core_importing_web_host_fails`; it imports
  `moduleFactoryFromUrl` from `"@oly/web-host"` and asserts
  `"engine/environments/web/clients/typescript/core/src/a.ts:1: @oly/core cannot depend on @oly/web-host"`.
- `test_relative_across_packages_fails` writes `package.json` in `engine/environments/web/host/js/` and
  `engine/environments/web/clients/typescript/core/`, imports `"../../../clients/typescript/core/src/world.js"` from
  `engine/environments/web/host/js/src/a.ts`, and asserts
  `"engine/environments/web/host/js/src/a.ts:1: relative import crosses packages; use the package name"`.

- [ ] **Step 2: Run them to verify they fail**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: FAIL in `test_core_importing_web_host_fails` (the lint matches `@oly/client`).

- [ ] **Step 3: Move and rename the package**

`git mv engine/packages/client engine/environments/web/host/js`; `engine/packages/` is then empty in git. Package
name `@oly/web-host`; the arena depends on it and `main.ts` imports from it. In `check_boundaries.py`, the specifier
and the message say `@oly/web-host`. `engine/CMakeLists.txt`: `ts.web_host.unit` on
`engine/environments/web/host/js/dist-tests/`. Update depths, references, root `tsconfig.json` and workspaces.

Cleanup as in Task 2: `git ls-files engine/packages` prints nothing, then `rm -rf engine/packages node_modules` and
the same `npm install` (regenerates the lock).

- [ ] **Step 4: Update the docs**

`@oly/client` → `@oly/web-host` in every import, dependency list and table; `engine/packages/client` →
`engine/environments/web/host/js`; `ts.client.unit` → `ts.web_host.unit`; the Mermaid node in
`modules-and-boundaries.md` and the diagram of `docs/architecture/README.md` say `@oly/web-host`.

- [ ] **Step 5: Verify**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: PASS.

Run: `$WC sh -c 'readlink node_modules/@oly/web-host; test ! -e node_modules/@oly/client && echo gone'`
Expected: `../../engine/environments/web/host/js` and `gone`.

Run: `git grep -n -E '@oly/client|engine/packages|ts\.client' -- . ':!docs/superpowers' ':!docs/benchmarks'`
Expected: no output.

Run: `$WC make games-test`
Expected: PASS, `ts.web_host.unit` included.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: rename @oly/client to @oly/web-host under the web environment"
```

---

### Task 4: The arena's TS package moves into `games/arena/web/`

**Files:**
- Move: `games/arena/{package.json,tsconfig.json,tsconfig.browser.json,schema.ts,src,tests}` → `games/arena/web/` (next to the existing `web/index.html`)
- Modify: the three moved `tsconfig` files (depths), `games/arena/CMakeLists.txt`, `Makefile`, `package.json` (workspaces), `package-lock.json`, `tsconfig.json`
- Modify docs: `games/arena/README.md` (paths only), `docs/architecture/games.md`, `docs/guide/{01-getting-started,11-testing}.md`, `docs/development/guide-style.md` (if it names the game layout)

**Interfaces:**
- Consumes: `@oly/core` (Task 2) and `@oly/web-host` (Task 3) paths.
- Produces: the example-game layout `games/<game>/{CMakeLists.txt, README.md, native/, web/}`, where `web/` holds the
  npm package (`package.json`, `tsconfig.json`, `tsconfig.browser.json`, `schema.ts`, `src/`, `tests/`, `index.html`).
  Phase 3 adds `<game>.oly` at the game root.
- From `games/<game>/web/`: `extends` = `../../../tsconfig.base.json`, core =
  `../../../engine/environments/web/clients/typescript/core`, host = `../../../engine/environments/web/host/js`;
  from `web/tests/`: one more `../` each.
- `games/arena/CMakeLists.txt`: `SCHEMA_JS ${CMAKE_CURRENT_SOURCE_DIR}/web/dist/schema.js`, smoke test on
  `${CMAKE_CURRENT_SOURCE_DIR}/web/dist/tests/`. `Makefile` `ESBUILD` entry: `games/$(G)/web/dist/src/main.js`
  (the `index.html` copy line is unchanged). Workspace entry `"games/*/web"`.

- [ ] **Step 1: Write the failing check**

Run: `$WC make game G=arena` after only the `Makefile` change to `games/$(G)/web/dist/src/main.js`.
Expected: FAIL, esbuild cannot find `games/arena/web/dist/src/main.js`.

- [ ] **Step 2: Move the package**

`git mv` the six entries into `games/arena/web/`; update the depths, `CMakeLists.txt`, workspaces and the root
`tsconfig.json` references (`games/arena/web`, `games/arena/web/tests`, `games/arena/web/tsconfig.browser.json`).
`git ls-files games/arena/dist games/arena/dist-tests` prints nothing; `rm -rf games/arena/dist games/arena/dist-tests node_modules`
and run the same `npm install` as in Task 2.

- [ ] **Step 3: Update the docs**

`games.md` and the guide's new-game section show the layout under Interfaces. The guide's `package.json`,
`tsconfig.json`, `tsconfig.browser.json`, `tests/tsconfig.json` and `CMakeLists.txt` snippets use the arena's paths
with the game name changed; the pipeline diagram in `01-getting-started.md` reads `games/<game>/web/schema.ts` and
`games/<game>/web/src/*.ts`; `11-testing.md` adds `games/<game>/web/tests` to the root `references`.

- [ ] **Step 4: Verify**

Run: `for s in '"extends": "../../../tsconfig.base.json"' '../../../engine/environments/web/clients/typescript/core' '../../../engine/environments/web/host/js' '../../../../engine/environments/web/clients/typescript/core'; do grep -qF "$s" docs/guide/01-getting-started.md docs/guide/11-testing.md || echo "missing: $s"; done`
Expected: no output.

Run: `git grep -n -E 'games/(arena|<game>)/(src|tests|schema\.ts|tsconfig|package\.json|dist)' -- . ':!docs/superpowers' ':!docs/benchmarks'`
Expected: no output.

Run: `$WC make games-test`
Expected: PASS, `ts.arena.smoke` included.

Run: `$WC make game G=arena`
Expected: PASS; `build/wasm-games-release/web/arena/` holds `index.html`, `main.js`, `arena_game.mjs`, `arena_game.wasm`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: move the arena's web package into games/arena/web"
```

---

### Task 5: Clean-checkout verification

No code: this task proves the moves hold from nothing, which no single task can.

**Files:**
- Modify: `docs/architecture/README.md` only if a check below finds a stale path.

- [ ] **Step 1: Clean the ignored state**

Run: `git status --short` (must be empty), then `rm -rf build node_modules` and
`git clean -ndX -- engine games` (lists ignored leftovers; delete them with `git clean -fdX -- engine games` after reading the list).

- [ ] **Step 2: Install from the lock**

Run: `$WC sh -c '"$(dirname "$(command -v node)")/npm" ci --no-audit --no-fund'`
Expected: PASS with no lock change (`git status --short` still empty).

- [ ] **Step 3: Run every suite on fresh build directories**

Run: `$DC make test`
Expected: PASS.

Run: `$WC make test PRESET=wasm-debug`
Expected: PASS.

Run: `$WC make games-test`
Expected: PASS.

Run: `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`
Expected: PASS.

Run: `$WC make game G=arena`
Expected: PASS.

- [ ] **Step 4: Check the tree against the spec**

Run: `git ls-files | grep -E '^engine/(packages|runtime/bridge)/'`
Expected: no output.

Run: `ls engine/environments/web/host/js engine/environments/web/clients/typescript/core engine/runtime/capi games/arena/web`
Expected: the four folders exist.

- [ ] **Step 5: Commit (only if Step 4 or the docs needed a fix)**

```bash
git add -A
git commit -m "docs: fix paths left after the environment move"
```
