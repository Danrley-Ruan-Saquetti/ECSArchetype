# Multi-environment, phase 4 (native) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A second environment, `native`, runs a C++-only game in a desktop window on Linux and Windows, and a C
client proves that the C API works outside WASM.

**Architecture:** `oly-schema` emits `oly::app::declareSchema`, so a native game declares its schema into an
`oly::app::Engine` without `capi`. A new module `native_host` (`engine/environments/native/host/`, on SDL3) gives the
game a `Window` and a `run` loop it calls from its own `main`. It turns SDL events into input calls and visibility
changes, and it presents after the `Render` phase, where the game draws. `oly_add_game` gains `ENVIRONMENT web|native`.
The C API headers become valid C (`.h`), and a `.c` test drives an engine only through `oly*`. Linux builds in the
`dev` image, now with X11 and Wayland development packages. Windows is cross-compiled with MinGW in a new `windows`
image, and ctest runs every test `.exe` under Wine through `CMAKE_CROSSCOMPILING_EMULATOR`. The example game is
`games/drift`.

**Tech Stack:** C++20, C11, CMake presets, SDL3 (FetchContent, static), doctest, mingw-w64 GCC, Wine, Docker Compose
(`dev`, `wasm`, `windows`).

**Spec:** `docs/superpowers/specs/2026-11-13-multi-environment-design.md` (§3.1, §3.3, §6, §8 phase 4, §10).

## Global Constraints

- The toolchain runs only in Docker: `DC="docker compose run --rm dev"`, `WC="docker compose run --rm wasm"`,
  `XC="docker compose run --rm windows"` (the last one from Task 7 on).
- Every existing test passes after each task, and `arena` runs on `web` (`make games-test` in `wasm`).
- Spec §3.1: "A C++ game and a C++ host use `oly::app::Engine` directly, never the C API"; "The native host depends on
  `app`, not on `capi`".
- Spec §3.3: nothing in `engine/runtime/` includes `engine/environments/`; one environment does not include another;
  `__EMSCRIPTEN__` and `EM_*` appear only in `engine/environments/web/` and in `OLY_API`.
- Spec §6.2: the game draws in a `Render` system and there is no draw hook; minimized, hidden or occluded →
  `setHidden(true)`, restored or exposed → `setHidden(false)`, focus lost → `releaseAll` only; button =
  `0x0007'0000 | scancode`, and only scancodes that `Key` declares are forwarded; a press counts only inside the window,
  a release anywhere; the pointer is in the renderer's logical coordinates.
- Spec §6.5: the C API headers are valid C, renamed to `oly/capi/c_api.h` and `oly/capi/engine_api.h`, with no alias
  header; the test is a `.c` file that includes only those headers.
- Spec §6.7: Linux is playable (X11 and Wayland backends); Windows is a separate image with mingw-w64 and Wine; the
  Windows `.exe` runs with a double click on a machine with nothing installed.
- Glossary rule: no alias, no compatibility wrapper. `c_api.hpp` and `engine_api.hpp` disappear.
- New names: header `oly/app/schema.hpp` with `oly::app::declareSchema(Engine&)`; generated file
  `schema_declaration.generated.cpp`; C++ module `native_host` (target `oly::native_host`, include
  `oly/native_host/...`, namespace `oly::native_host`) with `WindowConfig`, `Window`, `RunOptions`, `run`,
  `keyButtonOf` and `detail::InputTranslation`; `oly::input::isKey`; CMake option `OLY_BUILD_NATIVE_HOST`, variable
  `OLY_ENVIRONMENT`; presets `native-games`, `native-games-release`, `windows`, `windows-release`; toolchain file
  `cmake/toolchains/mingw-w64.cmake`; `Dockerfile.windows` and compose service `windows`; make targets
  `native-games-test`, `windows-test`, `desktop`; game `drift` (`games/drift/`, target `drift_game`); ctest names
  `native_host.input`, `native_host.window`, `capi.foreign_client`, `drift.desktop`.
- Game names only in `engine/testing/fixture/` and `games/`. Host tests use neutral names (`jump`, `move`, `aim`).
- New tests follow the `add-test-case` skill: a doctest suite registered with `oly_add_test`, or a case in an existing
  suite. The C test is a plain `add_executable` + `add_test`, because a C file cannot use doctest.
- Commits, comments and assert messages in English. Commit subject: one short line on what changes in the repository,
  conventional prefix as in `git log`; never cite the spec, the plan or task numbers.
- `.cpp`/`.hpp` written through Edit or Write are formatted by the hook. `.h` and `.c` files, and any file written
  another way, get `$DC clang-format -i <files>`.
- Comments only for a reason, a non-local invariant or a contract. The comments in the blocks below are decided text:
  copy them as they are and add no step-by-step or edit-narrating comment.
- Do not touch `docs/superpowers/` (except committing this plan) nor `docs/benchmarks/`.
- Phase 4 merges as one PR: the docs task (Task 8) updates the guide for every public API change of Tasks 1–7, which is
  the "same PR" of `docs/development/guide-style.md`.

**Decided here (not in the spec):**

- **Where `declareSchema` is declared.** `oly/app/schema.hpp` declares `oly::app::declareSchema(Engine&)`, and the
  game's generated `schema_declaration.generated.cpp` defines it. This is the pattern the generated `oly::capi` tables
  already follow (a runtime header declares, the game defines). A declaration in the generated header could not be
  called by `capi`, and a declaration in `capi` would make a native game define a `capi` name.
- **The order check moves into the generated function.** `declareSchema` asserts once, on entry, that the Engine
  declares no event, action or state yet. With that, the schema index is the id, which is the check that
  `olyEngineCreate` made item by item before. `capi` keeps its tables (sizes, alignments, kinds, counts) for its own
  `olySchema*` functions; `SchemaEvent` loses its `declare` field.
- **`OLY_SCHEMA_TOOL` only on WASM.** A native build (Linux or the Windows cross build) runs the in-tree `oly-schema`
  target. In the cross build, CMake prefixes a target `COMMAND` with the emulator, so `oly-schema.exe` runs under Wine
  with no extra code. WASM builds keep `OLY_SCHEMA_TOOL` and `make schema-tool`.
- **`OLY_ENVIRONMENT`** is `web` under Emscripten and `native` otherwise. `oly_add_game(... ENVIRONMENT <e>)` fails at
  configure when `<e>` is not `OLY_ENVIRONMENT`. Each game's `CMakeLists.txt` adds its target only in its environment.
  The web fixture and the TS tests are added only on `web`.
- **`OLY_BUILD_NATIVE_HOST`** (default `OFF`) builds the host and fetches SDL3. It fails at configure on Emscripten.
  `OLY_BUILD_GAMES=ON` on native requires it. The existing native presets do not change, so `debug-asan` and the
  benchmark presets never download SDL.
- **The host's error model.** SDL failures (no video device, window creation) throw `std::runtime_error` with
  `SDL_GetError()`: they come from the machine, not from a contract violation, so they are not `OLY_ASSERT`.
- **`RunOptions::frameSeconds`.** If it is greater than 0, the host advances the time by exactly this much per frame
  instead of reading the clock. Tests and the game's `--frames` mode use it, so a fixed number of frames covers a known
  amount of game time.
- **The loop when hidden.** While the window is hidden, each iteration first waits up to 100 ms for an event
  (`SDL_WaitEventTimeout`) instead of spinning: a minimized window has no vsync to pace the loop.
- **Static linking.** SDL3 is a static library everywhere. The Windows toolchain links with `-static`, so the `.exe`
  needs no `libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` or `SDL3.dll`. This answers the open question
  of spec §10. Under Wine, ctest runs every suite the `windows` preset builds, not only the native ones.
- **SDL3 subsystems.** Only video, events and render are needed. The audio, camera, joystick, haptic, HIDAPI, sensor
  and power subsystems are off, so that SDL builds faster and needs no audio packages.
- **Example game: `drift`.** The player steers a square at the bottom of the screen, rocks fall from the top, and
  touching one ends the run; Enter or Space restarts. It uses a schema, systems in three phases, an event and a handler,
  two states with scope, a repeating task, two action kinds, and drawing in `Render` with `SDL_RenderDebugText` for the
  score. Its gameplay (`native/`) does not include SDL. Only `desktop/` (the `main` and the drawing) does.
- **Guide page.** Until phase 5 reorganizes the guide, the native environment gets an unnumbered reference page
  `docs/guide/native-desktop.md`, linked from the index and from page 1. Phase 5 moves it into
  `environments/native/getting-started.md`.

## Review Focus

1. **A spurious `setHidden(false)`.** `Engine::setHidden` always restarts the clock, and SDL sends `EXPOSED` or
   `SHOWN` while the window is already visible (on resize, for example). If the host forwards those events, a visible
   game loses a frame of time. Expected: the host calls `setHidden` only when visibility changes. Pinned in Task 4.
2. **A press in the letterbox bars.** When the window's aspect ratio differs from the logical size, a click in a bar
   converts to a position outside the logical area. Expected: the pointer moves, but the press does not count, as a
   click outside the canvas on `web`. Pinned in Task 4.
3. **A Linux binary that cannot open a window.** SDL built without X11 or Wayland headers passes every headless test.
   Expected: `native_host.window` fails on Linux when `x11` or `wayland` is missing from the compiled video drivers.
   Pinned in Task 3.
4. **A Windows `.exe` that needs DLLs.** Wine finds DLLs that a clean Windows machine does not have. Expected:
   `objdump -p` of `drift_game.exe` lists only Windows system DLLs. Checked in Task 7. Following the "test runs the
   real artefact" rule, the check runs on the game's `.exe`, not on a test executable.
5. **Keys held when the window loses focus.** The player releases a key in another window, and the engine never sees
   the release. Expected: focus lost → `releaseAll`, and the held mouse buttons are forgotten too. Pinned in Task 4.

## File Structure

| File | Responsibility |
|---|---|
| `engine/runtime/app/include/oly/app/schema.hpp` (new) | declaration of `declareSchema` |
| `engine/schema/codegen/src/cpp_emitter.cpp`, `include/oly/schema/cpp_emitter.hpp` | emit `schema_declaration.generated.cpp`; registry without `declare` |
| `engine/runtime/capi/include/oly/capi/{c_api,engine_api}.h` (renamed from `.hpp`) | the C API, valid C11 and C++ |
| `engine/runtime/capi/src/engine_api.cpp`, `include/oly/capi/schema_registry.hpp` | `olyEngineCreate` calls `declareSchema`; `SchemaEvent` without `declare` |
| `engine/runtime/capi/tests/foreign_client.c` (new) | the C client test |
| `engine/runtime/input/include/oly/input/physical_button.hpp` | `isKey` |
| `engine/environments/native/host/` (new) | module `native_host`: `window.hpp`, `run.hpp`, `keys.hpp`, `detail/input_translation.hpp`, `src/*.cpp`, `tests/` |
| `cmake/{OlyOptions,Dependencies,OlyGame}.cmake`, `cmake/toolchains/mingw-w64.cmake` (new) | options, SDL3, `oly_add_game` per environment, MinGW toolchain |
| `CMakePresets.json`, `Makefile`, `Dockerfile`, `Dockerfile.windows` (new), `docker-compose.yaml` | presets, targets and images |
| `engine/CMakeLists.txt`, `games/CMakeLists.txt`, `games/arena/CMakeLists.txt` | add each piece only in its environment |
| `games/drift/` (new) | `drift.oly`, `native/`, `desktop/`, `CMakeLists.txt`, `README.md` |
| `tools/check_boundaries.py`, `tools/test_check_boundaries.py` | `.c` files linted; the `OLY_API` header path |
| `engine/runtime/ecs/tests/support/alloc_counter.cpp` | aligned allocation on Windows |
| `docs/`, `CLAUDE.md` | the native environment, the C headers, the new commands |

---

### Task 1: The generated `declareSchema`

**Files:**
- Create: `engine/runtime/app/include/oly/app/schema.hpp`
- Modify: `engine/schema/codegen/src/cpp_emitter.cpp`, `engine/schema/codegen/include/oly/schema/cpp_emitter.hpp`
- Modify: `engine/runtime/capi/include/oly/capi/schema_registry.hpp`, `engine/runtime/capi/src/engine_api.cpp`
- Modify: `engine/runtime/capi/tests/test_schema_registry.cpp`, `cmake/OlyGame.cmake`
- Test: `engine/schema/codegen/tests/cpp_emitter.cpp`, `engine/schema/codegen/tests/generator.cpp`

**Interfaces:**
- Produces: `namespace oly::app { void declareSchema(Engine& engine); }` in `oly/app/schema.hpp`; the C++ emitter
  returns three files: `schema.generated.hpp`, `schema_declaration.generated.cpp`, `schema_registry.generated.cpp`;
  `oly_add_game` compiles `schema_declaration.generated.cpp` in every environment, and `schema_registry.generated.cpp`
  only on `web` (Task 6 adds the environments).

- [ ] **Step 1: Write the failing emitter tests**

In `engine/schema/codegen/tests/cpp_emitter.cpp`, add `TEST_CASE("the declaration declares events, actions and states
in schema order")`. Parse:

```
component P { x: f32 }
event E { v: u32 }
action go: button
action aim: pointer
state menu
```

and check that the declaration file is exactly:

```cpp
// Generated by oly-schema: do not edit.
#include "oly/app/engine.hpp"
#include "oly/app/schema.hpp"
#include "oly/core/assert.hpp"
#include "schema.generated.hpp"

namespace oly::app {
  void declareSchema(Engine& engine) {
    OLY_ASSERT(engine.eventTypeCount() == 0 && engine.actions().actionCount() == 0 && engine.stateCount() == 0, "declareSchema: the Engine already declares events, actions or states");

    engine.declareEvent<E>("E");
    engine.actions().declare(oly::input::ActionKind::Button, "go");
    engine.actions().declare(oly::input::ActionKind::Pointer, "aim");
    engine.declareState("menu");
  }
}
```

Add `TEST_CASE("the declaration of a schema without events, actions or states only checks the Engine")` for
`component P { x: f32 }`: the body is the `OLY_ASSERT` line alone, and the parameter is
`[[maybe_unused]] Engine& engine`. With asserts off, the parameter would otherwise be unused, and `-Werror` would break
the release build.

Update the existing registry expectations: the `SchemaEvent` entries lose their lambda (`{"E", sizeof(E), alignof(E)}`),
and the registry no longer includes `oly/app/engine.hpp`. In `engine/schema/codegen/tests/generator.cpp`, the
`--emit cpp=<dir>` case expects three files.

- [ ] **Step 2: Run them to verify they fail**

Run: `$DC make test T='^schema\.'`
Expected: FAIL on the new cases (there is no declaration file) and on the registry strings.

- [ ] **Step 3: Implement**

- `oly/app/schema.hpp`: the declaration, with the comment `// Defined by the game's generated code
  (schema_declaration.generated.cpp). Declares the schema's events, actions and states in schema order, so the index in
  the schema is the id; the Engine must not declare any of them yet (OLY_ASSERT).`
- `cpp_emitter.cpp`: `std::string emitCppDeclaration(const Schema& schema)` next to `emitCppRegistry`, declared in
  `cpp_emitter.hpp`; `emitCpp` returns the three files in the order of the Interfaces block. The registry drops the
  `declare` lambda and the `oly/app/engine.hpp` include.
- `schema_registry.hpp`: `SchemaEvent` keeps `name`, `size` and `alignment`; drop the `oly::app::Engine` forward
  declaration.
- `engine_api.cpp`: `olyEngineCreate` creates the engine and calls `oly::app::declareSchema(engine->engine)`; the three
  loops go. Update the `olyEngineCreate` comment in `engine_api.hpp` (renamed to `.h` in Task 2): "Declares the schema
  with oly::app::declareSchema: the event's index in the schema is its event type, the action's index its ActionId and
  the state's index its StateId."
- `test_schema_registry.cpp`: define `oly::app::declareSchema` by hand with the same `CapiTestEvent`, three actions and
  two states, and drop the lambda from the table.
- `OlyGame.cmake`: add `schema_declaration.generated.cpp` to the custom command's `OUTPUT` and to the executable's
  sources.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `$DC make test` and `$WC make games-test`
Expected: PASS, including `capi.engine_api` (declaration order) and `ts.arena.smoke`.

- [ ] **Step 5: Commit**

```bash
git add engine/runtime/app engine/schema/codegen engine/runtime/capi cmake/OlyGame.cmake
git commit -m "feat(schema): emit declareSchema for oly::app::Engine"
```

---

### Task 2: The C API in C, and a C client test

**Files:**
- Rename: `engine/runtime/capi/include/oly/capi/c_api.hpp` → `c_api.h`, `engine_api.hpp` → `engine_api.h` (`git mv`)
- Modify: every include of them (`grep -rln 'c_api.hpp\|engine_api.hpp' engine games tools docs --exclude-dir=superpowers`)
- Modify: `CMakeLists.txt` (`LANGUAGES CXX C`), `engine/runtime/capi/tests/CMakeLists.txt`
- Modify: `tools/check_boundaries.py`, `tools/test_check_boundaries.py`
- Create: `engine/runtime/capi/tests/foreign_client.c`

**Interfaces:**
- Produces: `#include "oly/capi/c_api.h"` and `#include "oly/capi/engine_api.h"`, valid in C11 and C++20; `OLY_API` is
  `EMSCRIPTEN_KEEPALIVE` under Emscripten, `__declspec(dllexport)` under `_WIN32` and
  `__attribute__((visibility("default")))` otherwise; ctest `capi.foreign_client`.

- [ ] **Step 1: Write the failing C test**

`foreign_client.c` includes only `<stdint.h>`, `<stdio.h>`, `"oly/capi/c_api.h"` and `"oly/capi/engine_api.h"`, and
uses the schema of `test_schema_registry.cpp` (components `A { uint64_t value; }`, `B`, `Tag`; event `CapiTestEvent
{ uint32_t value; float amount; }`; actions `capiJump`, `capiMove`, `capiAim`; states `capiMenu`, `capiPlaying`). It
redeclares the two structs it reads in C, with the same layout, and has a `check(condition, line)` helper that prints
`foreign_client.c:<line>` and sets a failure flag. `main` returns 1 if any check failed.

- ABI: `_Static_assert(sizeof(OlyEntity) == 8, ...)`, `sizeof(OlyTaskHandle) == 8`, `sizeof(OlyActionState) == 12`.
- Schema: `olySchemaComponentCount() == 3`, `olyComponentSize(olySchemaComponentId(0)) == 8`,
  `olySchemaEventCount() == 1`, `olySchemaEventSize(0) == 8`, `olySchemaActionCount() == 3`,
  `olySchemaActionKind(1) == OLY_ACTION_AXIS2D`, `olySchemaStateCount() == 2`.
- Setup: `olyEngineCreate(60.0, 5, 16)`; `olyEngineSetScriptInvoker` with a C function and a `struct Record*` as
  `userData`; spawn one entity with `A { 41 }`; `olyEngineAddScriptSystem(engine, 1, "addOne")`: in the invoker, a
  write query on `A` over `olyQueryBegin`/`olyQueryEnd` adds 1 to every `value`; `olyEngineAddScriptHandler(engine, 0,
  "sum")` adds every `value` of the batch to `record->eventSum`; `olyEngineScheduleScriptTask(engine, 0.5, 0.0, 9,
  &handle)`; `olyEngineAddScriptStateHook(engine, OLY_STATE_ENTER, 1, "entered")` stores `eventCount` (the source
  state) in `record->enteredFrom`; `olyEngineRequestState(engine, 1)`; `olyEngineEmit(engine, 0, &event)` with
  `{7, 1.5f}`.
- Run: `olyEngineFrame(engine, 0.0)`, then `olyEngineFrame(engine, k / 60.0)` for `k = 1..60`.
- Checks: `A.value == 101`; `record->eventSum == 7` and the handler ran once; the task ran once, with `eventCount == 1`
  and `id == 9`; `record->enteredFrom == OLY_NO_STATE`; `olyEngineState(engine) == 1`;
  `olyEngineIsTaskPending(engine, handle.slot, handle.generation) == 0`; then `olyEngineDestroy`.

Register it in `engine/runtime/capi/tests/CMakeLists.txt`:

```cmake
# Plain C, without doctest: it includes the C API as a client in another language would.
add_executable(capi_foreign_client foreign_client.c test_schema_registry.cpp)
target_link_libraries(capi_foreign_client PRIVATE oly::capi)
target_include_directories(capi_foreign_client PRIVATE ${PROJECT_SOURCE_DIR}/engine/runtime/ecs/tests/support)
set_target_properties(capi_foreign_client PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF
  LINKER_LANGUAGE CXX RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
oly_warnings(capi_foreign_client)
add_test(NAME capi.foreign_client COMMAND capi_foreign_client)
```

- [ ] **Step 2: Run it to verify it fails**

Run: `$DC make test T='^capi\.foreign_client'`
Expected: FAIL at compile time (the headers are not found, or are not C).

- [ ] **Step 3: Rename the headers and make them valid C**

- `git mv` both headers, then update every include found by the grep in Files. Old plans and specs in
  `docs/superpowers/` keep the old names.
- `c_api.h`: `<stdint.h>`; the three `OLY_API` branches of the Interfaces block; `typedef struct OlyWorld OlyWorld;`
  and the same for `OlyQuery`, `OlyIteration`; `typedef struct OlyEntity { ... } OlyEntity;`; `extern "C"` only under
  `#ifdef __cplusplus`; every empty parameter list becomes `(void)`.
- `engine_api.h`: the same, for `OlyEngine`, `OlyTaskHandle` and `OlyActionState`. `capi/src/handles.hpp` keeps
  defining `struct OlyEngine`, which completes the C typedef.
- Root `CMakeLists.txt`: `project(Oly VERSION 1.0 LANGUAGES CXX C)`.
- `check_boundaries.py`: `OLY_API_HEADER = "engine/runtime/capi/include/oly/capi/c_api.h"`, and `.c` joins
  `CPP_EXTENSIONS`. Add a selftest case: a `.c` file in `engine/runtime/` with `#ifdef __EMSCRIPTEN__` is a violation.
- Format the `.h` and `.c` files: `$DC clang-format -i engine/runtime/capi/include/oly/capi/*.h engine/runtime/capi/tests/foreign_client.c`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `$DC make test` and `$WC make games-test`
Expected: PASS, including `capi.foreign_client`, `oly.lint.boundaries` and `oly.lint.boundaries_selftest` on both. The C
test also runs on WASM, under Node.

- [ ] **Step 5: Commit**

```bash
git add -A engine/runtime/capi CMakeLists.txt tools engine games docs/architecture docs/development docs/guide
git commit -m "feat(capi): make the C API headers valid C and test them from a C client"
```

---

### Task 3: Build the native host with SDL3, and open a window

**Files:**
- Modify: `cmake/OlyOptions.cmake`, `cmake/Dependencies.cmake`, `engine/CMakeLists.txt`, `CMakePresets.json`, `Dockerfile`
- Create: `engine/environments/native/host/CMakeLists.txt`, `include/oly/native_host/window.hpp`, `src/window.cpp`,
  `tests/CMakeLists.txt`, `tests/window.cpp`

**Interfaces:**
- Produces:

```cpp
namespace oly::native_host {
  struct WindowConfig {
    std::string title;
    int width;
    int height;
  };

  class Window {
   public:
    // Throws std::runtime_error with SDL's message if SDL cannot start its video subsystem or create the window.
    explicit Window(const WindowConfig& config);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    SDL_Window* window() const noexcept;
    SDL_Renderer* renderer() const noexcept;
  };
}
```

  `window.hpp` forward-declares `struct SDL_Window; struct SDL_Renderer;`. The module links `SDL3::SDL3-static`
  `PUBLIC`, so a game's drawing code can include `<SDL3/SDL.h>`. CMake: option `OLY_BUILD_NATIVE_HOST`, variable
  `OLY_ENVIRONMENT`; presets `native-games` (inherits `debug-asan`; `OLY_BUILD_NATIVE_HOST` and `OLY_BUILD_GAMES` on)
  and `native-games-release` (inherits `release`; the same two on, `OLY_BUILD_TESTS` and `OLY_BUILD_BENCH` off), each
  with build and test presets.

- [ ] **Step 1: Add the Linux packages and the build switches**

- `Dockerfile`: add `pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev
  libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev libdrm-dev
  libgbm-dev` to the `apt-get install` line, with the comment `# Headers for SDL3's X11 and Wayland video backends; SDL
  loads the libraries at run time.` Rebuild with `docker compose build dev`.
- `OlyOptions.cmake`: `option(OLY_BUILD_NATIVE_HOST "Build the native host (fetches SDL3)" OFF)`; `OLY_ENVIRONMENT`
  as in "Decided here"; `FATAL_ERROR` for `OLY_BUILD_NATIVE_HOST` on Emscripten, and for `OLY_BUILD_GAMES` on native
  without `OLY_BUILD_NATIVE_HOST`.
- `Dependencies.cmake`, under `if(OLY_BUILD_NATIVE_HOST)`: `FetchContent_Declare(SDL3 GIT_REPOSITORY
  https://github.com/libsdl-org/SDL.git GIT_TAG <tag> SYSTEM)`, where `<tag>` is the highest `release-3.2.*` from
  `git ls-remote --tags https://github.com/libsdl-org/SDL.git 'release-3.2.*'`. Before `FetchContent_MakeAvailable`:
  `SDL_SHARED OFF`, `SDL_STATIC ON`, `SDL_TEST_LIBRARY OFF`, and `OFF` for `SDL_AUDIO`, `SDL_CAMERA`, `SDL_JOYSTICK`,
  `SDL_HAPTIC`, `SDL_HIDAPI`, `SDL_SENSOR`, `SDL_POWER`, each `CACHE BOOL "" FORCE`. SDL ignores a misspelled name
  without a warning, so check that the configure summary lists these subsystems as disabled.
- `engine/CMakeLists.txt`: `if(OLY_BUILD_NATIVE_HOST) add_subdirectory(environments/native/host) endif()`.
- `native/host/CMakeLists.txt`: `oly_add_module(NAME native_host DEPENDS app input ecs core)`, then
  `target_link_libraries(oly_native_host PUBLIC SDL3::SDL3-static)`, and the tests under `OLY_BUILD_TESTS`.

- [ ] **Step 2: Write the failing tests**

`tests/window.cpp`, registered as `oly_add_test(MODULE native_host NAME window SOURCES window.cpp)` followed by
`set_tests_properties(native_host.window PROPERTIES ENVIRONMENT SDL_VIDEO_DRIVER=dummy)`:

- `TEST_CASE("a window gives a renderer with the configured logical size")`: `Window window({"test", 320, 200})`;
  `window.window()` and `window.renderer()` are not null; `SDL_GetRenderLogicalPresentation` returns 320 × 200 with
  `SDL_LOGICAL_PRESENTATION_LETTERBOX`.
- `TEST_CASE("the video drivers of a playable build are compiled in")`: collect `SDL_GetVideoDriver(i)` for
  `i < SDL_GetNumVideoDrivers()`; it contains `"dummy"`; under `__linux__` it contains `"x11"` and `"wayland"`; under
  `_WIN32` it contains `"windows"`.
- `TEST_CASE("a window that SDL cannot open throws with SDL's message")`:
  `SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "nonexistent", SDL_HINT_OVERRIDE)` (an override, because the
  environment variable wins over a normal hint); `CHECK_THROWS_AS(Window({"test", 320, 200}), std::runtime_error)`;
  then `SDL_ResetHint(SDL_HINT_VIDEO_DRIVER)`.

- [ ] **Step 3: Run them to verify they fail**

Run: `$DC make test PRESET=native-games T='^native_host\.window'`
Expected: FAIL at compile time (`Window` does not exist).

- [ ] **Step 4: Implement `Window` in `src/window.cpp`**

The constructor: `SDL_InitSubSystem(SDL_INIT_VIDEO)`; `SDL_CreateWindowAndRenderer(title, width, height,
SDL_WINDOW_RESIZABLE, ...)`; `SDL_SetRenderLogicalPresentation(renderer, width, height,
SDL_LOGICAL_PRESENTATION_LETTERBOX)`; `SDL_SetRenderVSync(renderer, 1)`, whose failure is ignored (comment: `// The
dummy driver has no vsync; the loop then runs unpaced, which headless tests want anyway.`). On any other failure,
release what was created and throw. The destructor destroys the renderer and the window, then calls
`SDL_QuitSubSystem(SDL_INIT_VIDEO)`.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `$DC make test PRESET=native-games T='^native_host\.'` and `$DC make test`
Expected: PASS. `debug-asan` does not download SDL (check that `build/debug-asan/_deps` has no `sdl3-src`). If
LeakSanitizer reports leaks inside SDL, stop and report them: the fix (a suppression or an SDL option) is a decision.

- [ ] **Step 6: Commit**

```bash
git add Dockerfile cmake CMakePresets.json engine/CMakeLists.txt engine/environments/native
git commit -m "feat(native-host): open an SDL3 window with a logical-size renderer"
```

---

### Task 4: Input and visibility translation

**Files:**
- Modify: `engine/runtime/input/include/oly/input/physical_button.hpp`, `engine/runtime/input/tests/physical_button.cpp`
- Create: `engine/environments/native/host/include/oly/native_host/keys.hpp`,
  `include/oly/native_host/detail/input_translation.hpp`, `src/keys.cpp`, `src/input_translation.cpp`, `tests/input.cpp`

**Interfaces:**
- Consumes: `Window` (Task 3).
- Produces:

```cpp
namespace oly::input {
  // True if `usage` is one of the Key values (the keyboard page, without the gaps the enum skips).
  constexpr bool isKey(uint32_t usage) noexcept;
}

namespace oly::native_host {
  // The engine's button for an SDL scancode, or nullopt if Key does not declare it.
  std::optional<oly::input::PhysicalButton> keyButtonOf(SDL_Scancode scancode) noexcept;
}

namespace oly::native_host::detail {
  class InputTranslation {
   public:
    // Applies one SDL event to the engine; false on SDL_EVENT_QUIT and SDL_EVENT_WINDOW_CLOSE_REQUESTED.
    bool apply(oly::app::Engine& engine, SDL_Renderer* renderer, SDL_Event& event);
    bool hidden() const noexcept;
  };
}
```

`keys.hpp` includes `<SDL3/SDL_scancode.h>`.

- [ ] **Step 1: Write the failing tests**

- `physical_button.cpp`: `TEST_CASE("isKey accepts every Key and rejects the gaps")`: true for `Key::A`,
  `Key::Backslash`, `Key::Semicolon`, `Key::ContextMenu`, `Key::ControlLeft`, `Key::MetaRight`; false for `0x0007'0003`,
  `0x0007'0032`, `0x0007'0066`, `0x0007'00DF`, `0x0007'00E8` and `MouseButton::Left`.
- `tests/input.cpp`, registered as `oly_add_test(MODULE native_host NAME input SOURCES input.cpp)` with the same
  `SDL_VIDEO_DRIVER=dummy` property:
  - `TEST_CASE("every Key is the SDL scancode on the keyboard page")`: a table of `{SDL_SCANCODE_..., Key::...}` pairs
    for every value of `Key`, from `{SDL_SCANCODE_A, Key::A}` to `{SDL_SCANCODE_RGUI, Key::MetaRight}`;
    `keyButtonOf(scancode)` equals `PhysicalButton(key)` for each pair. `keyButtonOf(SDL_SCANCODE_NONUSHASH)` and
    `keyButtonOf(SDL_SCANCODE_POWER)` are `nullopt`.
  - Each case below builds a `World`, an `Engine` and `Window({"test", 320, 200})`, and fills an `SDL_Event` with the
    window's id (`SDL_GetWindowID`). The checks read `engine.actions()` after `engine.actions().capture()`:
    - `"a key press and release reach the bound button"`: `declareButton("jump")` bound to `Key::W`; key down `W` →
      `held`; key up → not `held`. A key down with `repeat` set changes nothing.
    - `"losing focus releases keys and mouse buttons"`: `W` and the left mouse button are down; `FOCUS_LOST` → neither
      is `held`, and a later mouse up of the left button leaves it not `held`.
    - `"a press counts only inside the logical area"`: `SDL_SetRenderLogicalPresentation(renderer, 160, 200,
      LETTERBOX)`, so the bars are 80 px wide on each side; a `declarePointer("aim")` and a button bound to
      `MouseButton::Left`; motion to window `(100, 50)` → pointer `valid` at logical `(20, 50)`; a press at window
      `(10, 50)` → the pointer moves to logical `(-70, 50)` and the button is not `held`; a press at `(100, 50)` →
      `held`; the release at `(10, 50)` → not `held`.
    - `"visibility changes hide and show the engine once"`: `engine.frame(0.0)`, `engine.frame(1.0 / 60)` →
      `engine.time() == doctest::Approx(1.0 / 60)`; `EXPOSED` and `SHOWN` while visible, then
      `engine.frame(2.0 / 60)` → `engine.time() == doctest::Approx(2.0 / 60)` (no spurious clock restart); `MINIMIZED`
      → `translation.hidden()`, and two more frames leave the time unchanged; `RESTORED` → not hidden.
      `OCCLUDED` and `HIDDEN` hide, `SHOWN` and `EXPOSED` show.
    - `"quit and close requests end the loop"`: `apply` returns false for `SDL_EVENT_QUIT` and for
      `SDL_EVENT_WINDOW_CLOSE_REQUESTED`, and true for a key event.

- [ ] **Step 2: Run them to verify they fail**

Run: `$DC make test PRESET=native-games T='^(input\.physical_button|native_host\.input)'`
Expected: FAIL at compile time.

- [ ] **Step 3: Implement**

- `isKey`: the three ranges of the enum, `Key::A..Key::Backslash`, `Key::Semicolon..Key::ContextMenu` and
  `Key::ControlLeft..Key::MetaRight`, written with the enum values, not with literals.
- `keyButtonOf`: `PhysicalButton(0x0007'0000 | scancode)` when `isKey` accepts it. Comment: `// SDL3 scancodes are the
  USB HID usages of the keyboard page, the same numbering Key uses.`
- `InputTranslation`: key down without repeat → `buttonDown`, key up → `buttonUp`, both through `keyButtonOf`; mouse
  events go through `SDL_ConvertEventToRenderCoordinates` first. Motion → `pointerMove`. A button down → `pointerMove`,
  then `buttonDown` only inside `[0, logicalWidth) × [0, logicalHeight)` from `SDL_GetRenderLogicalPresentation`, and
  the button is remembered. A button up → `buttonUp` only for a remembered button. `FOCUS_LOST` → `releaseAll` and
  forget the mouse buttons. The visibility events of spec §6.2 → `setHidden` only when `hidden_` changes; the private
  members carry the reason: `// Engine::setHidden always restarts the clock, and SDL sends EXPOSED and SHOWN to a
  visible window too.`

- [ ] **Step 4: Run the tests to verify they pass**

Run: `$DC make test PRESET=native-games T='^(input\.|native_host\.)'` and `$DC make test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/runtime/input engine/environments/native/host
git commit -m "feat(native-host): turn SDL keyboard, mouse and window events into engine input"
```

---

### Task 5: The `run` loop

**Files:**
- Create: `engine/environments/native/host/include/oly/native_host/run.hpp`, `src/run.cpp`
- Test: `engine/environments/native/host/tests/window.cpp`

**Interfaces:**
- Consumes: `Window` (Task 3), `detail::InputTranslation` (Task 4).
- Produces:

```cpp
namespace oly::native_host {
  struct RunOptions {
    uint64_t maxFrames = 0;    // 0 = until the window is closed
    double frameSeconds = 0.0; // greater than 0: fixed time per frame instead of the clock
  };

  // Drives the engine until the window is closed or maxFrames frames ran; returns the process exit code.
  int run(oly::app::Engine& engine, Window& window, RunOptions options = {});
}
```

- [ ] **Step 1: Write the failing tests** (in `tests/window.cpp`)

- `"run drives frames and draws after each one"`: a `Render` system that counts its calls through `userData`;
  `run(engine, window, {.maxFrames = 11, .frameSeconds = 1.0 / 60})` returns 0; `engine.time() ==
  doctest::Approx(10.0 / 60)`; the counter is 10 (the first frame only records the time).
- `"a quit event ends run before the next frame"`: push `SDL_EVENT_QUIT` with `SDL_PushEvent` before `run(engine,
  window, {.maxFrames = 5, .frameSeconds = 1.0 / 60})`; it returns 0, and the counter is 0.
- `"run forwards pending input before the frame"`: an `Update` system records whether `jump` (bound to `Key::W`) is
  `pressed`; push a key down `W` for the window; `run` with `maxFrames = 2` and `frameSeconds = 1.0 / 60`; the system
  saw `pressed`.

- [ ] **Step 2: Run them to verify they fail**

Run: `$DC make test PRESET=native-games T='^native_host\.window'`
Expected: FAIL at compile time.

- [ ] **Step 3: Implement `run` in `src/run.cpp`**

Each iteration: if `translation.hidden()`, wait up to 100 ms with `SDL_WaitEventTimeout`, then apply that event (the
reason is in "Decided here"); drain `SDL_PollEvent` through `InputTranslation::apply`, and leave the loop when it
returns false; clear the renderer to black; `engine.frame(now)`, where `now` is `frames * frameSeconds` when
`frameSeconds > 0` and `SDL_GetTicksNS() / 1e9` otherwise; `SDL_RenderPresent`; count the frame. An exception from
`frame` leaves `run` (a C++ system that throws is a bug in the game).

- [ ] **Step 4: Run the tests to verify they pass**

Run: `$DC make test PRESET=native-games T='^native_host\.'`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add engine/environments/native/host
git commit -m "feat(native-host): add the run loop"
```

---

### Task 6: Games per environment, and the `drift` example

**Files:**
- Modify: `cmake/OlyGame.cmake`, `engine/CMakeLists.txt`, `games/CMakeLists.txt`, `games/arena/CMakeLists.txt`, `Makefile`
- Create: `games/drift/drift.oly`, `games/drift/native/drift.hpp`, `games/drift/native/*.cpp`,
  `games/drift/desktop/main.cpp`, `games/drift/desktop/render.cpp`, `games/drift/desktop/render.hpp`,
  `games/drift/CMakeLists.txt`, `games/drift/README.md`

**Interfaces:**
- Consumes: `declareSchema` (Task 1), `Window`, `run`, `RunOptions` (Tasks 3, 5).
- Produces: `oly_add_game(<target> ENVIRONMENT web|native SCHEMA <file.oly> SOURCES <cpp...> DEPENDS <m...>)`; on
  `native`: an executable in `build/<preset>/games/` built from the header, the declaration, the sources and the
  `DEPENDS`, with no registry, web host or `.mjs`; ctest `drift.desktop`; make targets `native-games-test` and
  `desktop G=<game>`.

- [ ] **Step 1: `oly_add_game` per environment**

- `ENVIRONMENT` is a required one-value argument; a value other than `OLY_ENVIRONMENT` →
  `FATAL_ERROR "<target> is a <e> game, but this build is <OLY_ENVIRONMENT>"`.
- The generator: `if(TARGET oly-schema)`, the custom command runs `oly-schema` by target name and depends on the target
  (comment: `# A target COMMAND gets CMAKE_CROSSCOMPILING_EMULATOR, so the Windows build runs oly-schema.exe under
  Wine.`); otherwise the current `OLY_SCHEMA_TOOL` check and path.
- `web`: today's sources (header, declaration, registry), `.mjs` and link options. `native`: header and declaration
  only, no suffix change.
- `games/arena/CMakeLists.txt` wraps its contents in `if(OLY_ENVIRONMENT STREQUAL "web")` and passes
  `ENVIRONMENT web`; `engine/testing/CMakeLists.txt` passes `ENVIRONMENT web`; `engine/CMakeLists.txt` adds the TS tests
  and `testing` only when `OLY_ENVIRONMENT` is `web`; `games/CMakeLists.txt` adds `drift`.
- `Makefile`: `native-games-test: $(MAKE) test PRESET=native-games`; `desktop` builds `DESKTOP_PRESET` (the `PRESET`
  given on the command line or in the environment, otherwise `native-games-release`), requires `G`, and prints the path
  of `build/$(DESKTOP_PRESET)/games/$(G)_game` (with `.exe` for a `windows*` preset).

- [ ] **Step 2: Write the failing smoke test**

`games/drift/CMakeLists.txt`, inside `if(OLY_ENVIRONMENT STREQUAL "native")`: `oly_add_game(drift_game ENVIRONMENT
native SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/drift.oly SOURCES <native/*.cpp and desktop/*.cpp by glob> DEPENDS ecs app
input native_host)`; under `OLY_BUILD_TESTS`, `add_test(NAME drift.desktop COMMAND drift_game --frames 600)` with
`ENVIRONMENT SDL_VIDEO_DRIVER=dummy`. The test runs the game's own executable (ten seconds of game time, headless),
not a library linked into a test.

Run: `$DC make native-games-test T='^drift\.'`
Expected: FAIL (`drift.oly` and the sources do not exist).

- [ ] **Step 3: Write the game**

`drift.oly`:

```
# drift.oly
component Position { x: f32, y: f32 }
component Velocity { x: f32, y: f32 }
component Extent { halfWidth: f32, halfHeight: f32 }
tag Player
tag Rock
event PlayerHit { player: entity }
action move: axis2d
action restart: button
state playing
state gameOver
```

- `native/drift.hpp`: the constants (logical size 640 × 480, player speed, rock spawn interval and fall-speed range),
  under the comment `// Initial values, not measured: tuned by playing.`; `struct Run { double startTime; double
  survivedSeconds; std::minstd_rand random; };`; `void installGameplay(oly::app::Engine& engine, Run& run);`. Nothing in
  `native/` includes SDL.
- `installGameplay` binds `move` to WASD and the arrows and `restart` to Enter and Space, registers the systems,
  handler and hooks below, and requests `playing`. In `playing`: the `onEnter` hook spawns the player (scoped) at the
  bottom centre, resets the `Run`, and schedules the rock spawner (`every`) from the hook, so the state scope cancels it on exit (check the scope rule in `docs/guide/09-states.md`);
  `steerPlayer` (`UpdateBefore`), `integrate` and `confinePlayer` (`Update`), and `dropFallenRocks` and `collide`
  (`UpdateAfter`, which emits `PlayerHit`). The `PlayerHit` handler stores `survivedSeconds` and requests `gameOver`.
  In `gameOver`, a system requests `playing` when `restart` is `pressed`.
- `desktop/render.cpp`: `void drawDrift(oly::app::SystemContext& context)`, whose `userData` is a `struct Canvas {
  SDL_Renderer* renderer; const Run* run; }`. It fills a rectangle per `Position` + `Extent` (player and rocks in
  different colours), and writes the score with `SDL_RenderDebugText`, plus `"game over - Enter to restart"` in
  `gameOver`.
- `desktop/main.cpp`: `World`, `Engine`, `oly::app::declareSchema(engine)`, `Run{.random = std::minstd_rand(seed)}`
  with a seed from `std::random_device`, `installGameplay`, `Window({"Drift", 640, 480})`, the `Render` system with the
  `Canvas`, then `return oly::native_host::run(engine, window, options);`. `--frames <n>` sets `maxFrames = n` and
  `frameSeconds = 1.0 / 60`, under the comment `// Headless runs: a fixed number of frames over a known amount of game
  time.` Any other argument prints the usage and returns 64.
- `README.md`: follows `games/arena/README.md`: what the game shows of the engine, the controls, how to build and run
  it on Linux (`make desktop G=drift`) and on Windows (Task 7), and the layout of its folders.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `$DC make native-games-test`, `$DC make test` and `$WC make games-test`
Expected: PASS, including `drift.desktop`, `oly.lint.boundaries` with `drift_game` in the manifest, and `ts.arena.smoke`.

- [ ] **Step 5: Play it on Linux, if a WSL distribution is available**

Run: `$DC make desktop G=drift`, copy `build/native-games-release/games/drift_game` into the WSL distribution and run
it there. Expected: a 640 × 480 window; arrows and WASD move the player; a rock ends the run; Enter restarts;
minimizing pauses the game. If no WSL distribution exists, write "Linux window not checked by hand" in the task report.

- [ ] **Step 6: Commit**

```bash
git add cmake/OlyGame.cmake engine/CMakeLists.txt engine/testing/CMakeLists.txt games Makefile
git commit -m "feat: build games per environment and add the drift desktop example"
```

---

### Task 7: Windows cross-build, tested under Wine

**Files:**
- Create: `Dockerfile.windows`, `cmake/toolchains/mingw-w64.cmake`
- Modify: `docker-compose.yaml`, `CMakePresets.json`, `Makefile`, `engine/runtime/ecs/tests/support/alloc_counter.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: compose service `windows`; presets `windows` (Debug, `OLY_SANITIZE` off, tests, games and native host on)
  and `windows-release` (Release, `OLY_ENABLE_ASSERTS=0`, tests off, games and native host on), both with
  `toolchainFile` `${sourceDir}/cmake/toolchains/mingw-w64.cmake`; make target `windows-test`.

- [ ] **Step 1: The image and the toolchain**

- `Dockerfile.windows`: `FROM debian:trixie-slim`; install `cmake make git python3 ca-certificates
  g++-mingw-w64-x86-64-posix gcc-mingw-w64-x86-64-posix binutils-mingw-w64-x86-64 wine wine64`;
  `ENV WINEPREFIX=/opt/wine WINEDEBUG=-all`; `RUN wineboot --init` (comment: `# Created once at image build: the first
  Wine start takes many seconds and would otherwise land inside a ctest timeout.`); `WORKDIR /app`. If a package name
  does not exist in trixie, use `apt-cache search` to find its current name and record it in the commit message.
- `docker-compose.yaml`: a `windows` service like `wasm` (`dockerfile: Dockerfile.windows`, same volumes).
- `mingw-w64.cmake`: `CMAKE_SYSTEM_NAME Windows`, `CMAKE_SYSTEM_PROCESSOR x86_64`, the `x86_64-w64-mingw32-gcc-posix`
  and `-g++-posix` compilers, `x86_64-w64-mingw32-windres` as `CMAKE_RC_COMPILER`, `CMAKE_FIND_ROOT_PATH
  /usr/x86_64-w64-mingw32` with `PROGRAM NEVER` and `LIBRARY`/`INCLUDE`/`PACKAGE ONLY`,
  `CMAKE_CROSSCOMPILING_EMULATOR wine`, `CMAKE_EXE_LINKER_FLAGS_INIT -static`, and `-D__USE_MINGW_ANSI_STDIO=1` in
  `CMAKE_C_FLAGS_INIT` and `CMAKE_CXX_FLAGS_INIT`. The comment on that define: `# msvcrt's printf does not know %zu;
  this selects MinGW's C99 printf, which OLY_ASSERT messages rely on.`
- `Makefile`: `windows-test: $(MAKE) test PRESET=windows`.

- [ ] **Step 2: Build and run every test under Wine**

Run: `docker compose build windows`, then `$XC make windows-test`
Expected on the first run: compile errors that only MinGW shows. Fix each at its cause, in portable code:
- `alloc_counter.cpp`: under `_WIN32`, `_aligned_malloc` in `allocateAligned`, and `_aligned_free` in the aligned
  `operator delete` overloads (comment: `// msvcrt has no aligned_alloc, and memory from _aligned_malloc must go back
  through _aligned_free.`).
- `-Wformat` on `%zu` inside `OLY_ASSERT`: `oly/core/assert.hpp` uses `format(gnu_printf, 4, 5)` under `__MINGW32__`,
  since MinGW reads `printf` as the Microsoft format.
- Any other error: fix it at its cause; if the fix would change behaviour on Linux or WASM, stop and report.

Then the same command passes: every suite of the preset, `capi.foreign_client`, `native_host.*` and `drift.desktop`,
each run under Wine.

- [ ] **Step 3: Check the release `.exe` for DLLs**

Run: `$XC make desktop G=drift PRESET=windows-release`, then
`$XC x86_64-w64-mingw32-objdump -p build/windows-release/games/drift_game.exe | grep 'DLL Name'`
Expected: only Windows system DLLs (`KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `msvcrt.dll`, `ADVAPI32.dll`,
`SHELL32.dll`, `ole32.dll`, `IMM32.dll`, `VERSION.dll`, `SETUPAPI.dll`, `WINMM.dll`, `OLEAUT32.dll` and similar); no
`libstdc++-6.dll`, `libgcc_s_seh-1.dll`, `libwinpthread-1.dll` or `SDL3.dll`.

- [ ] **Step 4: Check that Linux and WASM still pass**

Run: `$DC make test`, `$DC make native-games-test` and `$WC make games-test`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add Dockerfile.windows docker-compose.yaml cmake/toolchains CMakePresets.json Makefile engine
git commit -m "build: cross-compile for Windows with MinGW and run the tests under Wine"
```

---

### Task 8: Docs

**Files:**
- Create: `docs/guide/native-desktop.md`
- Modify: `docs/guide/README.md`, `docs/guide/01-getting-started.md`, `docs/guide/cheat-sheet.md`,
  `docs/development/guide-style.md`, `docs/development/commands.md`, `docs/architecture/{limits,modules-and-boundaries,build-and-tests,games,glossary,repository}.md`,
  `CLAUDE.md`

- [ ] **Step 1: The guide (follow `docs/development/guide-style.md`)**

- `native-desktop.md`: what a desktop game is and when to choose it; the smallest `main.cpp` (the shape of spec §6.2:
  `declareSchema`, systems, `Window`, `Render` system, `run`); drawing in `Render` with the renderer in `userData`;
  what the host does with focus, minimizing, the pointer and the keys; `RunOptions` and headless runs;
  `oly_add_game(... ENVIRONMENT native ...)`; building and running on Linux (`make desktop G=<game>`) and on Windows
  (`windows` image, `make desktop G=<game> PRESET=windows-release`, the `.exe` path); `drift` as the example.
- `README.md`: the page in the references table and `drift` in the example games. `01-getting-started.md`: one line
  pointing a C++-only game to the new page, and `ENVIRONMENT web` in its `oly_add_game` example.
- `cheat-sheet.md`: `oly::app::declareSchema`, `oly::input::isKey`, the `oly::native_host` names, `ENVIRONMENT`, and
  the C API headers `oly/capi/c_api.h` and `engine_api.h` (valid C11).
- `guide-style.md`: the C API row points to `c_api.h` and `engine_api.h`; the C++ public headers row adds
  `engine/environments/native/host/include/oly/native_host/*.hpp`, except `detail/`; the "how a game is built" row adds
  `ENVIRONMENT` and the `desktop`, `native-games-test` and `windows-test` targets.
- Search `docs/guide/` for `c_api.hpp`, `engine_api.hpp` and `oly_add_game(`, and check every link you touched.

- [ ] **Step 2: Architecture, commands and `CLAUDE.md`**

- `limits.md`: the "no native host" limit goes; new limits: no shared library for foreign clients yet, Windows checked
  under Wine and by hand only, Linux window checked by hand only.
- `modules-and-boundaries.md`: the `native_host` module and its dependencies (`app input ecs core` + SDL3), and the C
  headers.
- `build-and-tests.md`: `OLY_BUILD_NATIVE_HOST`, `OLY_ENVIRONMENT`, the four new presets, the `windows` image, the
  in-tree `oly-schema` on native builds, Wine as the cross-compiling emulator.
- `games.md`: `drift` and `ENVIRONMENT`. `glossary.md`: native host, `Window`, `run`, `declareSchema`.
  `repository.md`: `engine/environments/native/host/`, `games/drift/`, `Dockerfile.windows`.
- `commands.md`: `native-games-test`, `windows-test`, `desktop`, and `docker compose run --rm windows`.
- `CLAUDE.md`: the toolchain rule lists the `windows` service (MinGW cross-build and Wine).

- [ ] **Step 3: Commit**

```bash
git add docs/guide docs/development docs/architecture CLAUDE.md
git commit -m "docs: describe the native environment, the desktop host and the C headers"
```

---

### Task 9: Clean-checkout verification

- [ ] **Step 1: Every preset from a clean build directory**

Run: `rm -rf build/debug-asan build/native-games build/wasm-games build/windows build/schema-tool`, then
`$DC make test`, `$DC make native-games-test`, `$WC make games-test` and `$XC make windows-test`.
Expected: all PASS. Report the test counts per preset.

- [ ] **Step 2: The boundaries from spec §3.3**

Run: `grep -rn "__EMSCRIPTEN__\|EM_JS\|EM_ASM" engine/runtime engine/schema engine/environments/native games/drift`
Expected: only `engine/runtime/capi/include/oly/capi/c_api.h` and the test exemption already in the lint.
`oly.lint.boundaries` passed in Step 1 on all four presets.

- [ ] **Step 3: Hand check on Windows**

Give the user the path `build/windows-release/games/drift_game.exe`. Expected from the user: a double click opens the
window, and the game plays as in Task 6, Step 5. Record the answer in the PR description.
