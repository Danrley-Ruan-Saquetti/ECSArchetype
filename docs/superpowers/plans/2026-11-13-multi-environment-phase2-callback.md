# Multi-environment, phase 2 (neutral callback) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The core calls script code through a per-engine invoker set by the C API, and the only JS-aware C++ moves to
the web host, so `engine/runtime/` no longer depends on Emscripten except for `OLY_API`.

**Architecture:** `OlyEngine` gains an invoker and a `userData` pointer, set by `olyEngineSetScriptInvoker`; every
script call goes through them. The `EM_JS` import leaves `capi` for a new C++ module `web_host`
(`engine/environments/web/host/native/`), linked into every web game's WASM; it exports
`olyWebHostInstallScriptInvoker(engine)`, which the TS `Bridge` calls right after `olyEngineCreate`. The process-wide
test hook `oly::capi::setScriptInvoker` is removed. The boundary lint gains the environment and Emscripten rules of
spec §3.3. The `loop` and `events` benchmarks, run before and after, check that the web hot path did not get slower.

**Tech Stack:** C++20 + CMake presets, Emscripten 3.1.74 (`EM_JS`), TypeScript 5.6, doctest, Python boundary lint,
Docker Compose (`dev`, `wasm`).

**Spec:** `docs/superpowers/specs/2026-11-13-multi-environment-design.md` (§3.3, §5, §8 phase 2, §9).

## Global Constraints

- The toolchain runs only in Docker: `DC="docker compose run --rm dev"`, `WC="docker compose run --rm wasm"`.
- Every existing test passes after each task, and `arena` runs on `web` (`ts.arena.smoke` and `make game G=arena`).
- Spec §5: "`OLY_API` is the only Emscripten-aware line in the core"; "Calling a script with no invoker installed stays a
  contract error (`OLY_ASSERT`, with kind and id)"; "No `addFunction`, so no table growth and no change to the link
  options"; the web hot path target is "no measurable regression".
- Spec §3.3: "`__EMSCRIPTEN__` and `EM_*` macros appear only in `engine/environments/web/` and in `OLY_API`"; tests that
  skip a case on WASM are exempt.
- Glossary rule: no alias, no compatibility wrapper. `oly::capi::setScriptInvoker` and `oly::capi::ScriptInvoker` are
  removed, not kept next to the new function.
- New names: C type `OlyScriptInvoker`, C function `olyEngineSetScriptInvoker`, C++ module `web_host` (target
  `oly::web_host`, `oly_web_host`, include `oly/web_host/...`), exported function `olyWebHostInstallScriptInvoker`,
  death cases `capi.contract_death.set_script_invoker_null` and `capi.contract_death.script_without_invoker`.
- The `kind` contract (`OLY_SCRIPT_SYSTEM`, `_HANDLER`, `_TASK`, `_TASK_DISCARD`, `_STATE_HOOK`) and the JS function
  `Module.olyInvokeScript(engine, kind, id, events, eventCount)` do not change.
- New tests follow the `add-test-case` skill (doctest case in an existing suite, death case in `contract_death`).
- Commits, comments and assert messages in English. Commit subject: one short line on what changes in the repository,
  conventional prefix as in `git log`; never cite the spec, the plan or task numbers.
- `.cpp`/`.hpp` written through the Edit or Write tool are formatted by the hook; any written another way get
  `$DC clang-format -i <files>`.
- Comments only for a reason, a non-local invariant or a contract. The comments in the blocks below are decided text:
  copy them as they are and add no step-by-step or edit-narrating comment.
- Do not touch `docs/superpowers/`. `docs/benchmarks/bridge/` only gains the new results (Task 4).

**Decided here (not in the spec):**

- `olyEngineSetScriptInvoker` with a null `invoker` is a contract violation (`OLY_ASSERT`). There is no "uninstall":
  nothing needs it, and a null pointer would cost a branch on every script call.
- Until the first `olyEngineSetScriptInvoker`, an engine holds `oly::capi::missingScriptInvoker`, which asserts with
  kind and id. The hot path stays branch-free, as today's `g_invoker`; with asserts disabled the call does nothing, as
  the native default does today.
- The invoker fields are declared **before** `engine` in `OlyEngine`: the `Engine` destructor discards the pending
  script tasks through the invoker, so the fields must still be alive then. ASan does not flag the wrong order (the
  memory is still allocated), so the reviewer checks it by reading.
- The TS `Bridge.engineCreate` installs the web invoker itself (one place: every engine the TS client creates gets it).
  `@oly/core` calls a WASM export of the web host, not an npm import of `@oly/web-host`, so the rule "`@oly/core`
  cannot depend on `@oly/web-host`" is untouched.
- `web_host` is built only with Emscripten (`engine/CMakeLists.txt`) and linked by `oly_add_game` only with
  Emscripten. Phase 4 turns this into the per-environment `oly_add_game`.
- The lint finds a module's environment from the filesystem (`<dir>/include/oly/<name>/`) and a package's from its
  `package.json`, not from the CMake manifest: `web_host` is in the manifest only in Emscripten builds, and the native
  lint run must still catch `engine/runtime/` including it.
- No user guide page changes in this phase. `oly/capi/engine.hpp` loses `setScriptInvoker` (the guide never mentions
  it) and `oly/capi/engine_api.hpp` gains `olyEngineSetScriptInvoker`, an API for client authors, not for game
  developers. The architecture docs describe it now; the guide's C API column arrives with the cheat sheet of phase 5
  (spec §7).

## Review Focus

1. **A foreign client runs a frame before installing an invoker.** Expected: `OLY_ASSERT` naming kind and id, not a
   silent no-op or a JS `TypeError`. Pinned by `capi.contract_death.script_without_invoker` (Task 2).
2. **A client installs a new invoker between frames** (a managed client recreating its handle). Expected: the next
   script call uses the new invoker and `userData`. Pinned by `the invoker installed between frames runs the next script
   call` (Task 1).
3. **An engine destroyed with pending script tasks.** Expected: every discard reaches that engine's invoker with its own
   `userData`, which needs the member order of "Decided here". Pinned by the `userData` check added to `olyEngineDestroy
   discards the pending script tasks` (Task 1); the order itself is checked by reading.
4. **Two engines in one process with different `userData`.** Expected: each frame calls only its own engine's invoker
   with its own `userData`. Pinned by `two engines in the same process: each calls its own invoker with its own
   userData` (Task 1).
5. **A WASM game built without the web host.** Expected: it fails at `createEngine` with a `TypeError` (missing export),
   not inside a frame at the first script call. Pinned by the `.mjs` export check of Task 2, Step 6, and by
   `oly_add_game` linking `oly::web_host` under `if(EMSCRIPTEN)`.

## File Structure

| File | Responsibility |
|---|---|
| `engine/runtime/capi/include/oly/capi/engine_api.hpp` | the C ABI: `OlyScriptInvoker`, `olyEngineSetScriptInvoker` and their contract |
| `engine/runtime/capi/src/handles.hpp` | `OlyEngine` holds the invoker and `userData`; `missingScriptInvoker` |
| `engine/runtime/capi/src/engine_api.cpp` | every script call goes through `invokeScript`; no Emscripten after Task 2 |
| `engine/environments/web/host/native/` (new) | module `web_host`: the `EM_JS` import and `olyWebHostInstallScriptInvoker` |
| `engine/environments/web/clients/typescript/core/src/{module,bridge}.ts` | the TS client installs the web invoker on each engine |
| `cmake/OlyGame.cmake`, `engine/CMakeLists.txt` | build `web_host` and link it into every WASM game |
| `tools/check_boundaries.py` | the environment and Emscripten rules |
| `docs/architecture/*.md`, `docs/benchmarks/bridge/` | the new contract and the before/after numbers |

---

### Task 1: Per-engine script invoker in the C API

**Files:**
- Modify: `engine/runtime/capi/include/oly/capi/engine_api.hpp`, `engine/runtime/capi/include/oly/capi/engine.hpp`
- Modify: `engine/runtime/capi/src/handles.hpp`, `engine/runtime/capi/src/engine_api.cpp`
- Test: `engine/runtime/capi/tests/engine_api.cpp`, `engine/runtime/capi/tests/contract_death.cpp`, `engine/runtime/capi/tests/CMakeLists.txt`
- Docs: `docs/architecture/bridge.md`
- Outputs outside git: `build/bench/loop_callback_before.json`, `build/bench/events_callback_before.json`

**Interfaces:**
- Produces (C, `engine_api.hpp`):
  `typedef void (*OlyScriptInvoker)(void* userData, OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount);`
  and `OLY_API void olyEngineSetScriptInvoker(OlyEngine* engine, OlyScriptInvoker invoker, void* userData);`
- Produces (internal, `handles.hpp`): `oly::capi::missingScriptInvoker` (same signature as `OlyScriptInvoker`) and the
  `OlyEngine` fields `scriptInvoker`, `scriptUserData`.
- Produces: the base commit (Step 1), which Task 4 compares against.
- Removes: `oly::capi::ScriptInvoker`, `oly::capi::setScriptInvoker`. `oly::capi::engineOf` stays.
- Keeps, until Task 2: under `__EMSCRIPTEN__`, `missingScriptInvoker` still calls the `EM_JS` import, so the web keeps
  working between the two commits.

- [ ] **Step 1: Record the benchmark baseline, before any code change**

If this plan file is still untracked, commit it first
(`git add docs/superpowers/plans/2026-11-13-multi-environment-phase2-callback.md && git commit -m "docs(plans): add the multi-environment phase 2 plan"`).
Then `git status --short` must be empty; note `git rev-parse --short HEAD` as the **base commit**. Run:

```bash
$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'
mkdir -p build/bench
$WC node engine/testing/dist/bench/loop.js --module build/wasm-games-release/games/fixture_game.mjs \
  --out build/bench/loop_callback_before.json --commit $(git rev-parse --short HEAD)
$WC node engine/testing/dist/bench/events.js --module build/wasm-games-release/games/fixture_game.mjs \
  --out build/bench/events_callback_before.json --commit $(git rev-parse --short HEAD)
```

Expected: all exit 0 and the two JSON files exist. They stay under `build/` (ignored), so later commits do not pick them
up. `boundary.js` is not run: it never crosses the script callback.

- [ ] **Step 2: Write the failing tests**

In `engine/runtime/capi/tests/engine_api.cpp`:

- Add `std::vector<void*> g_userData;` next to the other recording vectors.
- `recordingInvoker` takes `void* userData` as its first parameter (the `OlyScriptInvoker` signature) and pushes it to
  `g_userData` right after `g_order.push_back("s");`.
- `EngineFixture`: remove the `previous` member and the `setScriptInvoker(previous)` line of the destructor; the
  constructor clears `g_userData` with the other vectors and ends with
  `olyEngineSetScriptInvoker(engine, &recordingInvoker, this);`. `#include "oly/capi/engine.hpp"` stays (`engineOf`).
- Replace the test `two engines in the same process: the invoker receives the right engine` with:

```cpp
TEST_CASE("two engines in the same process: each calls its own invoker with its own userData") {
  EngineFixture fixture;
  EngineFixture other;
  olyEngineAddScriptSystem(fixture.engine, 1, "first");
  olyEngineAddScriptSystem(other.engine, 1, "second");

  other.step();

  REQUIRE(!g_invocations.empty());
  for (size_t index = 0; index < g_invocations.size(); ++index) {
    CHECK(g_invocations[index].first == other.engine);
    CHECK(g_userData[index] == &other);
  }
}
```

- New test, after it:

```cpp
TEST_CASE("the invoker installed between frames runs the next script call") {
  EngineFixture fixture;
  int replacement = 0;
  olyEngineAddScriptSystem(fixture.engine, 1, "s");
  fixture.next();

  olyEngineSetScriptInvoker(fixture.engine, &recordingInvoker, &replacement);
  fixture.next();

  CHECK(g_userData == std::vector<void*>{&fixture, &replacement});
}
```

- In `olyEngineDestroy discards the pending script tasks`, add as the last check
  `CHECK(g_userData == std::vector<void*>{&fixture, &fixture});`.

In `engine/runtime/capi/tests/contract_death.cpp`, a new case registered as
`{"set_script_invoker_null", setScriptInvokerNull, {"olyEngineSetScriptInvoker", "null"}}`:

```cpp
  void setScriptInvokerNull() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);

    olyEngineSetScriptInvoker(engine, nullptr, nullptr);
  }
```

and `set_script_invoker_null` appended to `CASES` in `engine/runtime/capi/tests/CMakeLists.txt`.

- [ ] **Step 3: Run them to verify they fail**

Run: `$DC make test T='^capi\.'`
Expected: FAIL to build, `olyEngineSetScriptInvoker` was not declared.

- [ ] **Step 4: Add the invoker to the C API**

`engine_api.hpp`. The comment above the `OLY_SCRIPT_*` defines becomes
``// `kind` of a script call: what the invoker runs and the meaning of `id`, `events` and `eventCount` (OlyScriptInvoker).``
At the top of the `extern "C"` block (the function type has C linkage, since foreign clients get it):

```cpp
// Runs the script code of one engine. `kind` is OLY_SCRIPT_SYSTEM (`id` is the system; `events` null and `eventCount`
// 0), OLY_SCRIPT_HANDLER (`id` is the handler; `events` is the batch, with the event struct's layout), OLY_SCRIPT_TASK
// (`id` is the task's scriptId; `events` null and `eventCount` 1 on the last run, 0 on the others),
// OLY_SCRIPT_TASK_DISCARD (`id` is the scriptId; `events` null and `eventCount` 0) or OLY_SCRIPT_STATE_HOOK (`id` is
// the hookId; `events` null and `eventCount` is the state on the other side of the switch). `userData` is the pointer
// given to olyEngineSetScriptInvoker.
typedef void (*OlyScriptInvoker)(void* userData, OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount);
```

After `olyEngineWorld`:

```cpp
// `invoker` is not null (OLY_ASSERT). Valid inside and outside olyEngineFrame: the next script call uses it. Before
// the first call, a script call is a contract violation (OLY_ASSERT with kind and id), except in WASM until the web host
// installs its invoker. olyEngineDestroy calls the invoker with OLY_SCRIPT_TASK_DISCARD for each pending script task,
// so `userData` must stay valid until then.
OLY_API void olyEngineSetScriptInvoker(OlyEngine* engine, OlyScriptInvoker invoker, void* userData);
```

The comment of `olyEngineAddScriptSystem` says "called by the engine's script invoker"; the one of the state hooks
says "The script one calls the engine's invoker with (OLY_SCRIPT_STATE_HOOK, hookId, NULL, other)", keeping the rest of
the sentence about `other`.

`engine.hpp`: remove the `ScriptInvoker` comment, the `using` and `setScriptInvoker`; keep `engineOf`.

`handles.hpp`, before `struct OlyEngine`, declare
`void missingScriptInvoker(void* userData, OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount);`
in `namespace oly::capi`, with the comment
`// Every OlyEngine starts with it: a script call before olyEngineSetScriptInvoker is a contract violation.`
In `OlyEngine`, between `world` and `engine`:

```cpp
  // Before `engine`: its destructor discards the pending script tasks through the invoker.
  OlyScriptInvoker scriptInvoker = &oly::capi::missingScriptInvoker;
  void* scriptUserData = nullptr;
```

`engine_api.cpp`:

- Remove `defaultInvoker`, `g_invoker` and the `setScriptInvoker` definition.
- Define `missingScriptInvoker` at the start of the `oly::capi` block, before the anonymous namespace, with
  `[[maybe_unused]]` on every parameter. Its body is today's `defaultInvoker` body; only the assert message changes, to
  `"script called with no invoker installed (olyEngineSetScriptInvoker) kind=%u id=%u"`.
- First function of the anonymous namespace:
  `void invokeScript(OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount)`, one line
  that calls `engine->scriptInvoker(engine->scriptUserData, engine, kind, id, events, eventCount)`. No branch: it is the
  hot path, and it must stay inlinable.
- The five `g_invoker(...)` calls become `invokeScript(...)` with the same arguments.
- `olyEngineSetScriptInvoker`, after `olyEngineWorld` in `extern "C"`: `OLY_ASSERT(invoker != nullptr,
  "olyEngineSetScriptInvoker: null invoker")`, then store both fields.

- [ ] **Step 5: Update the docs**

`docs/architecture/bridge.md`: the bullet "A single imported JS function" becomes "**A per-engine script invoker.**
`olyEngineSetScriptInvoker(engine, invoker, userData)` installs the function that runs all script code of that
engine; `userData` lets a client pass its own handle. `kind` says what to call and the meaning of the other
parameters:" followed by the existing `kind` table. The last bullet ("In the native build,
`oly::capi::setScriptInvoker` swaps the invoker for tests") becomes "A script call with no invoker installed is a
contract violation (`OLY_ASSERT`, with kind and id)." Add `olyEngineSetScriptInvoker` to the "Engine and loop" row of
the function table.

- [ ] **Step 6: Run the tests**

Run: `$DC make test`
Expected: PASS, with `capi.engine_api` (the three new or changed cases) and
`capi.contract_death.set_script_invoker_null` listed.

Run: `$WC make games-test`
Expected: PASS (`ts.testing.wasm`, `ts.arena.smoke`, `capi.*` in WASM).

Run: `git grep -n -E 'g_invoker|defaultInvoker|setScriptInvoker\(|ScriptInvoker previous' -- engine`
Expected: no output (`olyEngineSetScriptInvoker` has a capital `S` after `olyEngine` and does not match).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat(capi): add a per-engine script invoker"
```

---

### Task 2: The JS script invoker moves to the web host

**Files:**
- Create: `engine/environments/web/host/native/CMakeLists.txt`
- Create: `engine/environments/web/host/native/include/oly/web_host/script_invoker.hpp`
- Create: `engine/environments/web/host/native/src/script_invoker.cpp`
- Modify: `engine/CMakeLists.txt`, `cmake/OlyGame.cmake`
- Modify: `engine/runtime/capi/src/engine_api.cpp`, `engine/runtime/capi/include/oly/capi/engine_api.hpp`
- Modify: `engine/environments/web/clients/typescript/core/src/module.ts`, `engine/environments/web/clients/typescript/core/src/bridge.ts`
- Test: `engine/runtime/capi/tests/contract_death.cpp`, `engine/runtime/capi/tests/CMakeLists.txt`
- Docs: `docs/architecture/{bridge,README,decisions,modules-and-boundaries,repository,schema-and-codegen,build-and-tests,glossary}.md`, `docs/guide/01-getting-started.md`

**Interfaces:**
- Consumes: `OlyScriptInvoker`, `olyEngineSetScriptInvoker`, `oly::capi::missingScriptInvoker` (Task 1).
- Produces: module `web_host` (`oly_add_module(NAME web_host DEPENDS capi OBJECT)`), header
  `oly/web_host/script_invoker.hpp`, export `olyWebHostInstallScriptInvoker(OlyEngine*)`, present in every WASM built
  by `oly_add_game`.
- Produces (TS): `OlyModule._olyWebHostInstallScriptInvoker(engine: number): void`, called by `Bridge.engineCreate`.
- After this task, `engine/runtime/` has no `EM_JS`, no `<emscripten.h>` outside `c_api.hpp` and no `__EMSCRIPTEN__`
  outside `c_api.hpp` and `app/tests/recovery.cpp`. Task 3's lint relies on that.

- [ ] **Step 1: Write the failing test and remove the WASM fallback**

In `engine/runtime/capi/tests/contract_death.cpp`, a new case registered as
`{"script_without_invoker", scriptWithoutInvoker, {"no invoker", "kind=0"}}`, and `script_without_invoker` appended to
`CASES`:

```cpp
  void scriptWithoutInvoker() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);
    olyEngineAddScriptSystem(engine, 1, "x");

    olyEngineFrame(engine, 0.0);
    olyEngineFrame(engine, 1.0 / 60);
  }
```

In `engine_api.cpp`, remove the top `#if defined(__EMSCRIPTEN__)` block (`#include <emscripten.h>` and the `EM_JS`) and
the `#if`/`#else`/`#endif` inside `missingScriptInvoker`, which keeps only the `OLY_ASSERT`. In `engine_api.hpp`, the
`olyEngineSetScriptInvoker` comment drops ", except in WASM until the web host installs its invoker".

- [ ] **Step 2: Run the tests to verify the web fails**

Run: `$WC make games-test`
Expected: FAIL in `ts.testing.wasm` and `ts.arena.smoke`, with "script called with no invoker installed" (no engine
created from TS has an invoker yet). `capi.contract_death.script_without_invoker` passes.

- [ ] **Step 3: Create the web host module**

`engine/environments/web/host/native/CMakeLists.txt`:

```cmake
# OBJECT for the same reason as capi: a static library would drop the unreferenced EMSCRIPTEN_KEEPALIVE export.
oly_add_module(NAME web_host DEPENDS capi OBJECT)
```

`include/oly/web_host/script_invoker.hpp` includes `oly/capi/c_api.hpp` and `oly/capi/engine_api.hpp` and declares,
in `extern "C"`:

```cpp
// Installs on `engine` the invoker that calls Module.olyInvokeScript(engine, kind, id, events, eventCount); that JS
// function must exist before the first script call. The TS client calls it right after olyEngineCreate.
OLY_API void olyWebHostInstallScriptInvoker(OlyEngine* engine);
```

`src/script_invoker.cpp`: `#include <emscripten.h>`; the `EM_JS(void, olyHostInvokeScript, ...)` line and the comment
above it, moved verbatim from `engine_api.cpp` (Step 1 removed them there); in an anonymous namespace,
`invokeJavaScript` with the `OlyScriptInvoker` signature (`userData` is `[[maybe_unused]]`) that forwards to
`olyHostInvokeScript`; `olyWebHostInstallScriptInvoker` calls
`olyEngineSetScriptInvoker(engine, &invokeJavaScript, nullptr)`. No `addFunction`: the invoker is a C++ function, so
the function table and the link options do not change.

`engine/CMakeLists.txt`, right after `add_subdirectory(runtime)`:

```cmake
if(EMSCRIPTEN)
  add_subdirectory(environments/web/host/native)
endif()
```

`cmake/OlyGame.cmake`: inside the existing `if(EMSCRIPTEN)` block, before `target_link_options`,
`target_link_libraries(${target} PRIVATE oly::web_host)`. The header comment of `oly_add_game` ends with "and links
the WASM module (.mjs) with the web host's script invoker".

- [ ] **Step 4: The TS client installs the web invoker**

`module.ts`: add `_olyWebHostInstallScriptInvoker(engine: number): void;` after `_olyEngineWorld`; the comment above
`olyInvokeScript` becomes
`// Called by the web host's script invoker (olyWebHostInstallScriptInvoker): the Runtime installs it once and dispatches by engine.`

`bridge.ts`: `engineCreate` calls `this.module._olyWebHostInstallScriptInvoker(engine)` right after `_olyEngineCreate`
and before `this.sync()`.

- [ ] **Step 5: Update the docs**

- `bridge.md`, first paragraph: `capi` "exposes the `extern "C"` C API (`c_api.cpp`, `engine_api.cpp`). It knows no
  environment: on `web`, the web host (`engine/environments/web/host/native/`) installs the invoker that calls JS." In
  the invoker bullet, add: "On `web`, `olyWebHostInstallScriptInvoker(engine)` installs an invoker that calls
  `Module.olyInvokeScript`; the TS `Bridge` calls it right after `olyEngineCreate`. There is no `addFunction` and no
  growing function table."
- `README.md` diagram: `capi: C API oly*, callback` → `capi: C API oly*, script invoker`;
  `C API (extern "C") + 1 imported JS function` → `C API (extern "C") + per-engine script invoker`; index row of
  `bridge.md`: "the C API and the callback into TS" → "the C API and the per-engine script invoker".
- `docs/guide/01-getting-started.md` pipeline diagram: `engine (ecs + app + input + capi)` →
  `engine (ecs + app + input + capi + web_host)`, keeping the column alignment.
- `decisions.md`: the row "A single imported JS function, with `kind`" becomes row (a) of the block below.
- `modules-and-boundaries.md`: the graph gains `webHost_native["web_host (C++, web only)"] --> capi` and
  `games --> webHost_native`; the table gains row (b); the sentence about `OBJECT` says
  "`capi` and `web_host` are `OBJECT` because ...".
- `repository.md` tree, under `environments/`:
  `web/host/native/              web_host: C++ linked into every web game's WASM (JS script invoker)`.
- `schema-and-codegen.md` diagram: `oly::capi + app + input + ecs + core` →
  `oly::capi + app + input + ecs + core + oly::web_host`.
- `build-and-tests.md`: the `oly_add_game` row says "runs codegen and links a game's `.mjs`/`.wasm` module, with
  `oly::web_host`".
- `glossary.md`: gains row (c).

```markdown
(a) | A per-engine script invoker set through the C API; the web host installs the JS one | the core knows no environment; a managed client passes its own handle in `userData` | one function pointer and one `userData` per engine; dispatch by `kind` in each client |
(b) | `web_host` | `oly::web_host` | `capi` | `OBJECT`, built only with Emscripten |
(c) | call script code | — (C++ code is called directly) | `olyEngineSetScriptInvoker` (per engine, dispatch by `kind`) | installed by `Runtime.createEngine` |
```

The `(a)`/`(b)`/`(c)` labels are not part of the rows.

- [ ] **Step 6: Run the tests**

Run: `$WC make games-test`
Expected: PASS, `ts.testing.wasm`, `ts.arena.smoke` and `capi.contract_death.script_without_invoker` included.

Run: `$DC make test`
Expected: PASS.

Run: `$WC make game G=arena`
Expected: PASS; `build/wasm-games-release/web/arena/` holds `index.html`, `main.js`, `arena_game.mjs`, `arena_game.wasm`.

Run: `grep -c _olyWebHostInstallScriptInvoker build/wasm-games-release/web/arena/arena_game.mjs`
Expected: a count of 1 or more (the export reached the game's module).

Run: `git grep -n -E '__EMSCRIPTEN__|(^|[^A-Z_])EM_[A-Z]+|emscripten\.h|olyHostInvokeScript' -- engine/runtime`
Expected: only `engine/runtime/capi/include/oly/capi/c_api.hpp` and `engine/runtime/app/tests/recovery.cpp`.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "refactor: move the JS script invoker into the web host"
```

---

### Task 3: Boundary lint for environments and Emscripten

**Files:**
- Modify: `tools/check_boundaries.py`
- Test: `tools/test_check_boundaries.py`
- Docs: `docs/architecture/modules-and-boundaries.md`

**Interfaces:**
- Consumes: the tree after Task 2 (no Emscripten in `engine/runtime/` outside the allowed places).
- Produces lint messages:
  - `<file>:<line>: engine/runtime cannot depend on environment <env> (<oly/x or @oly/x>)`
  - `<file>:<line>: environment <a> cannot depend on environment <b> (<oly/x or @oly/x>)`
  - `<file>:<line>: Emscripten (<match>) is allowed only in engine/environments/web/ and OLY_API`

- [ ] **Step 1: Write the failing tests**

Add to `tools/test_check_boundaries.py`:

```python
    def test_runtime_including_environment_fails(self):
        self.write("engine/environments/web/host/native/include/oly/web_host/script_invoker.hpp", "#pragma once\n")
        self.write("engine/runtime/capi/src/x.cpp", '#include "oly/web_host/script_invoker.hpp"\n')

        code, output = self.run_check([("web_host", "engine/environments/web/host/native", ["capi"]), ("capi", "engine/runtime/capi", ["web_host"])])

        self.assertEqual(code, 1)
        self.assertIn("engine/runtime/capi/src/x.cpp:1: engine/runtime cannot depend on environment web (oly/web_host)", output)

    def test_environment_including_other_environment_fails(self):
        self.write("engine/environments/native/host/include/oly/native_host/window.hpp", "#pragma once\n")
        self.write("engine/environments/web/host/native/src/x.cpp", '#include "oly/native_host/window.hpp"\n')

        code, output = self.run_check([("native_host", "engine/environments/native/host", []), ("web_host", "engine/environments/web/host/native", ["native_host"])])

        self.assertEqual(code, 1)
        self.assertIn("engine/environments/web/host/native/src/x.cpp:1: environment web cannot depend on environment native (oly/native_host)", output)

    def test_environment_importing_other_environment_package_fails(self):
        self.write("engine/environments/other/clients/x/package.json", '{"name": "@oly/other-client"}')
        self.write("engine/environments/web/clients/typescript/core/src/a.ts", 'import { thing } from "@oly/other-client";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("engine/environments/web/clients/typescript/core/src/a.ts:1: environment web cannot depend on environment other (@oly/other-client)", output)

    def test_emscripten_in_runtime_fails(self):
        self.write("engine/runtime/app/src/x.cpp", "#if defined(__EMSCRIPTEN__)\n#endif\n")

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("engine/runtime/app/src/x.cpp:1: Emscripten (__EMSCRIPTEN__) is allowed only in engine/environments/web/ and OLY_API", output)

    def test_em_macro_in_runtime_test_fails(self):
        self.write("engine/runtime/app/tests/t.cpp", "EM_ASM({});\n")

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("engine/runtime/app/tests/t.cpp:1: Emscripten (EM_ASM) is allowed only in engine/environments/web/ and OLY_API", output)

    def test_emscripten_in_allowed_places_passes(self):
        self.write("engine/runtime/capi/include/oly/capi/c_api.hpp", "#include <emscripten.h>\n#define OLY_API EMSCRIPTEN_KEEPALIVE\n")
        self.write("engine/environments/web/host/native/src/x.cpp", "#include <emscripten.h>\nEM_JS(void, f, (), {});\n")
        self.write("engine/runtime/app/tests/t.cpp", "#if !defined(__EMSCRIPTEN__)\n#endif\n")

        code, output = self.run_check([])

        self.assertEqual(code, 0)
        self.assertEqual(output, "")
```

- [ ] **Step 2: Run them to verify they fail**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: FAIL in the five `*_fails` tests (no such rule yet); `test_emscripten_in_allowed_places_passes` passes. A
`*_fails` test that passes here means its rule would never fire: fix the test before Step 3.

- [ ] **Step 3: Implement the rules in `tools/check_boundaries.py`**

- Add `".claude"` to `IGNORED_DIRECTORIES`: a git worktree under `.claude/worktrees/` would otherwise be linted as a
  second copy of the tree and could remap a module name.
- The module docstring gains "and environment boundaries, and Emscripten outside the web environment".
- Constants after `RUNTIME_PREFIX` (the regexes are the rule, so they are fixed here):

```python
ENVIRONMENTS_PREFIX = "engine/environments/"
MODULE_INCLUDE_DIRECTORY = re.compile(r"^(.+)/include/oly/([a-z_]+)$")

EMSCRIPTEN_PATTERN = re.compile(r"__EMSCRIPTEN__|\bEM_[A-Z_]+|\bEMSCRIPTEN_[A-Z_]+|<emscripten[./]")
EMSCRIPTEN_ALLOWED_PREFIX = "engine/environments/web/"
# OLY_API is the core's only Emscripten-aware line (EMSCRIPTEN_KEEPALIVE on WASM).
OLY_API_HEADER = "engine/runtime/capi/include/oly/capi/c_api.hpp"
```

- `repository_layout(root) -> (module_directories, package_directories)`: one `os.walk` that skips
  `IGNORED_DIRECTORIES`; a directory whose relative path matches `MODULE_INCLUDE_DIRECTORY` maps the module name to
  `group(1) + "/"`; a directory with a `package.json` that has a `name` maps that name to the directory + `"/"`.
- `environment_of(relative_path) -> str | None`: the first path segment after `ENVIRONMENTS_PREFIX`, or `None` outside it.
- `environment_violation(relative_path, target_directory, target_name) -> str | None`: `None` when the target is not
  in an environment; the `engine/runtime cannot depend on ...` message when the source is under `RUNTIME_PREFIX`; the
  `environment <a> cannot depend on ...` message when both are in environments and they differ.
- `emscripten_violation(relative_path, line) -> str | None`: `None` when `EMSCRIPTEN_PATTERN` finds nothing, the file
  is under `EMSCRIPTEN_ALLOWED_PREFIX` or is `OLY_API_HEADER`, or the file is under a `/tests/` directory and every
  match is `__EMSCRIPTEN__` (comment there: `# A test may skip a case on WASM, but never calls Emscripten.`); otherwise
  the message with the first match.
- `check_cpp_file` gains `module_directories`: each line first goes through `emscripten_violation`; after the two
  existing include checks, the included module goes through `environment_violation` with
  `module_directories.get(included_module)` and `f"oly/{included_module}"`.
- `check_ts_file` gains `package_directories`: a specifier starting with `@oly/` gives
  `package_name = "/".join(specifier.split("/")[:2])`, checked with `environment_violation` and
  `package_directories.get(package_name)`.
- `check(root, manifest)` calls `repository_layout(root)` once, before the loop, and passes each map to its checker.
- Every new message gets the existing `f"{relative_path}:{line_number}: "` prefix.

- [ ] **Step 4: Update the docs**

`modules-and-boundaries.md`, list of what `oly.lint.boundaries` rejects, three new items:
- `engine/runtime/` including a module or importing a package of `engine/environments/`;
- one environment including or importing another;
- `__EMSCRIPTEN__`, `EM_*`, `EMSCRIPTEN_*` or `<emscripten.h>` outside `engine/environments/web/` and `OLY_API`
  (`c_api.hpp`); a test under `tests/` may use `__EMSCRIPTEN__` to skip a case on WASM.

- [ ] **Step 5: Verify**

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: PASS.

Run: `$DC make test T='^oly\.lint'`
Expected: PASS (`oly.lint.boundaries` over the real tree, native manifest).

Run: `$WC make games-test`
Expected: PASS (`oly.lint.boundaries` with `web_host` in the manifest).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "chore: lint environment boundaries and Emscripten outside the web environment"
```

---

### Task 4: Benchmark check and clean verification

**Files:**
- Create: `docs/benchmarks/bridge/loop_callback_before.json`, `loop_callback_after.json`, `events_callback_before.json`, `events_callback_after.json`
- Modify: `docs/benchmarks/bridge/README.md` (new section at the end)

**Interfaces:**
- Consumes: the base commit and the two `build/bench/*_before.json` files (Task 1, Step 1).

- [ ] **Step 1: Run the benchmarks on the new code**

Same build and commands as Task 1, Step 1, with `--out build/bench/loop_callback_after.json` and
`--out build/bench/events_callback_after.json`.
Expected: both exit 0 (each benchmark checks its own result and exits 1 on divergence).

- [ ] **Step 2: Compare**

From the four JSON files, take the medians of the `ts` variants that cross the callback: `loop` empty TS systems
(K = 1, 4, 16, 64) and `events` TS handler (N = 16, 256, 4096). Compute `after / before` per row.

Expected: every ratio in `[0.85, 1.15]` (the environment's ±15 % spread, `docs/benchmarks/bridge/README.md`). If K = 16
and K = 64 of `loop`, or two rows of `events`, are above 1.15: stop, run the `perf-reviewer` agent on
`git diff <base commit>..HEAD -- engine/runtime/capi engine/environments/web/host/native`, and report to the user
before changing anything. A single row outside the band: rerun only `after` once; if it stays outside, record it as is
and say so in the Reading.

- [ ] **Step 3: Record the result**

Copy the four JSON files from `build/bench/` to `docs/benchmarks/bridge/`. Append to `docs/benchmarks/bridge/README.md`
a section `## Per-engine script invoker`, in the style of the sections above: raw files and commits, the two commands,
"Environment: the same as above", one table per benchmark (`before`, `after`, `after / before`, the `ns per callback`
column for `loop`) and a short "Reading" saying whether the indirect call through `OlyEngine` changed the callback cost.
Report ratios, not absolute numbers across runs; only measured numbers; no target besides the `[0.85, 1.15]` band.

- [ ] **Step 4: Clean-checkout verification**

Run: `git status --short` (only the Step 3 files), then
`rm -rf build/debug-asan build/wasm-debug build/wasm-games build/wasm-games-release`.

Run: `$DC make test`
Expected: PASS.

Run: `$WC make test PRESET=wasm-debug`
Expected: PASS (`web_host` compiles in this preset even though no game links it).

Run: `$WC make games-test`
Expected: PASS.

Run: `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`
Expected: PASS.

Run: `$WC make game G=arena`
Expected: PASS.

Run: `git grep -n -E 'g_invoker|setScriptInvoker\(|oly::capi::ScriptInvoker|1 imported JS function|single imported JS function' -- . ':!docs/superpowers' ':!docs/benchmarks'`
Expected: no output.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "docs(benchmarks): record the script callback cost with the per-engine invoker"
```

Manual check for the user (not automated): `docker compose run --rm --service-ports web make serve G=arena`, open
http://localhost:8000 and play one round.
