# Multi-environment, phase 3 (schema) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A game's schema is a neutral `<game>.oly` file, read by `oly-schema`, a generator written in C++ that emits the
C++ code and the TS code of every client; the TS schema API and `@oly/codegen` are removed.

**Architecture:** A new C++ module `schema` (`engine/schema/codegen/`) parses, validates, lays out and hashes a `.oly`
file, owns the emitter interface and the C++ emitter, and runs the command line of the `oly-schema` executable. The TS
emitter is a module of the web client (`engine/environments/web/clients/typescript/codegen/`) that registers itself
into `oly-schema` when linked. `oly_add_game` runs `oly-schema --emit cpp=...`; `make ts` builds `oly-schema` for the
machine and runs `--emit ts=...` for every TS consumer before `tsc`. The generated TS calls constructors from the
`@oly/core/generated` subpath, so game code keeps typed, literal schema objects. The hash keeps today's canonical form
and FNV-1a: the parity check of Task 5 proves that `arena` and the fixture keep their hash and their C++ byte for byte.

**Tech Stack:** C++20 + CMake presets, doctest, TypeScript 5.6, GNU make, Python boundary lint, Docker Compose (`dev`,
`wasm`).

**Spec:** `docs/superpowers/specs/2026-11-13-multi-environment-design.md` (§3.2, §4, §8 phase 3, §9, §10).

## Global Constraints

- The toolchain runs only in Docker: `DC="docker compose run --rm dev"`, `WC="docker compose run --rm wasm"`.
- Every existing test passes after each task, and `arena` runs on `web` (`ts.arena.smoke` and `make game G=arena`).
- Spec §4.2: "The hash keeps today's canonical form and FNV-1a, so that the hash of an existing schema does not
  change"; "The generated C++ keeps its `static_assert`s on `sizeof`, `alignof` and `offsetof`"; "Adding a language
  does not touch the generator core"; command line `oly-schema <file.oly> --emit cpp=<dir> --emit ts=<file>`.
- Spec §4.1: five declarations; scalar types `f32 f64 i8 u8 i16 u16 i32 u32 entity`; action kinds
  `button axis2d pointer`; declaration order inside each group is the id order; `#` comments; free whitespace; one file
  per game; "Errors report file, line and column, and the build fails".
- Spec §4.3: game code changes only the import path; `@oly/core/generated` is allowed only in `*.generated.ts`;
  `schema.generated.ts` is generated on every build and never committed; "A C++-only game no longer needs Node".
- `oly-schema` is C++20 that the `wasm` image's own `g++` (Ubuntu's, older than the `dev` image's GCC 14) compiles: no
  `<format>`, no `std::expected`, no `std::ranges::to`.
- Glossary rule: no alias, no compatibility wrapper. `component`, `tag`, `event`, `action`, `state`, `defineSchema`,
  `computeLayout` and `@oly/codegen` are removed, not kept next to the new path.
- Game names only in `engine/testing/fixture/` and `games/`. The `.oly` corpus and the C++ tests use neutral names
  (`A`, `P`, `T`, `E`, `go`, `menu`).
- New names: C++ module `schema` (target `oly::schema`, include `oly/schema/...`, namespace `oly::schema`), executable
  `oly-schema`, C++ module `schema_typescript` (target `oly::schema_typescript`, include `oly/schema_typescript/...`,
  namespace `oly::schema_typescript`), CMake preset `schema-tool`, make target `schema-tool`, cache variable
  `OLY_SCHEMA_TOOL`, TS subpath `@oly/core/generated` with `componentType`, `tagType`, `eventType`, `actionType`,
  `stateType`, `schemaOf`, ctest suites `schema.*` and `schema_typescript.*`.
- New tests follow the `add-test-case` skill (a doctest suite registered with `oly_add_test`).
- Commits, comments and assert messages in English. Commit subject: one short line on what changes in the repository,
  conventional prefix as in `git log`; never cite the spec, the plan or task numbers.
- `.cpp`/`.hpp` written through the Edit or Write tool are formatted by the hook; any written another way get
  `$DC clang-format -i <files>`.
- Comments only for a reason, a non-local invariant or a contract. The comments in the blocks below are decided text:
  copy them as they are and add no step-by-step or edit-narrating comment.
- Do not touch `docs/superpowers/` (except committing this plan) nor `docs/benchmarks/`.
- Phase 3 merges as one PR: the docs task (Task 8) updates the guide for every public API change of Tasks 4–7, which
  is the "same PR" of `docs/development/guide-style.md`.

**Decided here (not in the spec):**

- **Where `oly-schema` is built.** Native presets build it with the tree (`engine/CMakeLists.txt`, outside
  `if(EMSCRIPTEN)`), with its tests. A WASM build cannot run a tool it compiles to WASM, so the `wasm` image builds it
  for the machine with a new preset `schema-tool` (Release, no tests, target `oly-schema` only), through
  `make schema-tool`, which `make ts` runs first. `oly_add_game` runs the executable named by the cache variable
  `OLY_SCHEMA_TOOL` (default `build/schema-tool/bin/oly-schema`). Phase 4, which builds games natively, points it at the
  in-tree target.
- **Emitter registration.** The C++ emitter is built into `schema`: C++ is the engine language, not a client. A client
  emitter is an `OBJECT` module that registers itself from a namespace-scope initializer (`registerEmitter`) and links
  itself into `oly-schema` from its own `CMakeLists.txt`. Adding a language is one folder and one `add_subdirectory`;
  no file of `engine/schema/` changes, and the lint forbids `engine/schema/` from including an environment. The
  alternative, a `main.cpp` that lists the emitters, would make the generator core include every client.
- **Emitters are pure.** An emitter returns `{path, contents}` pairs; the driver checks every `--emit` before writing
  and writes each file whole. Tests compare strings and never touch the disk, except the driver's.
- **Grammar** (written down in `engine/schema/format/README.md`, Task 2): `component Name { field: type, ... }`,
  `event Name { field: type, ... }`, `tag Name`, `action name: kind`, `state name`; a trailing comma is allowed; keywords
  are recognized only at the start of a declaration, so a field may be named `state`. A word is `[A-Za-z0-9_]+`, so
  `1abc` reaches the naming rule and gets its message. Only the first error is reported.
- **Error order.** The parser stops at the first syntax error, unknown field type or unknown action kind. Then the
  validator checks, in source order: the name rule, a component or event without fields, each field's name rule, a
  repeated field, a name repeated across the whole schema, and the 33rd state.
- **New rule: a repeated field** (`component A { x: f32, x: f64 }`) is an error. A TS object literal could not repeat a
  key, so `schema.ts` never needed it.
- **The TS emitter rejects** an action or state named like a TypeScript reserved word that C++ allows (`function`,
  `let`, ...) or like a constructor the generated module imports (`schemaOf`, ...): it would be an `export const` that
  does not compile. The rule lives in the TS emitter, not in the core: it is a TypeScript constraint.
- **C++ output.** Byte-identical to today's `@oly/codegen` except two strings: the header comment
  (`oly-codegen` → `oly-schema`) and the `static_assert` message (`diverges from TS` → `diverges from oly-schema`), since
  the layout now comes from the generator. A schema with no component or tag gets `return {};` in
  `schemaComponentIds()` instead of an empty array, which does not compile today.
- **The hash parity test of spec §4.2** is the diff of Task 5, Step 7, run while the TS code still exists ("before the
  TS code is removed"). The canonical form stays pinned for good by `schema.hash`. A permanent test holding `arena`'s or
  the fixture's hash would need a new value on every schema edit, which is the "written twice" anti-goal.
- **The generated `declareSchema` function** of spec §3.1 arrives in phase 4 with its first consumer, the native host.
  Phase 3 keeps today's registry functions (`oly::capi::schemaHash`, `schemaComponentIds`, ...).
- **TS outputs.** `make ts` generates `games/<game>/web/schema.generated.ts` from `games/<game>/<game>.oly` for every
  game with a `web/` folder, and `<stem>.generated.ts` next to every `.oly` under `engine/testing/`. It always
  regenerates (a rebuild of the affected TS projects costs seconds), and `*.generated.ts` is in `.gitignore`. The
  fixture's module is `engine/testing/fixture/fixture.generated.ts`; the WASM tests' small schemas live in
  `engine/testing/fixture/schemas/`, where game names are allowed.
- **TS surface.** The schema types (`ComponentType`, `TagType`, `EventType`, `ActionType`, `State`, `Schema`, `Fields`,
  `ValueOf`, ...) stay public. `@oly/core/internal` loses `computeLayout`, `scalarAlignment`, `canonicalSchema`,
  `fnv1a32` and `schemaHash`; `scalarSize` stays (the field views use it). The constructors build objects with
  today's property order (`kind`, `name`, `fields`, `layout`, `schemaIndex`), so the hot path sees the same object
  shapes; no benchmark is run in this phase.

## Review Focus

1. **The hash of an existing schema changes.** Expected: `arena` and the fixture keep their hash and their generated
   C++ (Task 5, Step 7, diff against the copies of Step 1); the canonical form of every declaration kind and scalar type
   is pinned by `schema.hash` against a value computed with today's TS code (Task 1).
2. **A layout differs between C++ and TS.** Expected: the C++ `static_assert`s still compile for every game, and the TS
   views read the padded fixture component `Mixed` right (`ts.testing.wasm`, `fixtureMixedChecksum`).
3. **A wrong `.oly` file.** Expected: the build stops with `<file>:<line>:<column>: <message>`, and no file is written.
   Pinned by the corpus (`schema.corpus`, Task 2), the driver tests (Task 3) and the manual check of Task 9.
4. **Hand-written TS declares a schema object** through `@oly/core/generated`. Expected: `oly.lint.boundaries` fails
   (Task 7).
5. **The generator core grows a dependency on a client.** Expected: `oly.lint.boundaries` fails for any include of an
   environment module from `engine/schema/` (Task 7).
6. **The TS emitter is dropped by the linker** (a client emitter registers only through a static initializer).
   Expected: `schema_typescript.emitter` lists `ts` after `cpp`, and `make ts` would fail with
   `unknown emitter "ts"` in the Release `schema-tool` build otherwise (Task 5).
7. **An action or state named `function`** in a game. Expected: `oly-schema --emit ts=...` fails with its location
   (Task 4); `--emit cpp=...` alone still works.

## File Structure

| File | Responsibility |
|---|---|
| `engine/schema/codegen/include/oly/schema/schema.hpp` | the schema model, `SchemaError`, the `.oly` spellings, `parseSchema` |
| `engine/schema/codegen/include/oly/schema/{layout,hash}.hpp` + `src/{layout,hash}.cpp` | natural-alignment layout; FNV-1a over the canonical form |
| `engine/schema/codegen/src/{parser,validation}.cpp`, `src/validation.hpp` | `.oly` syntax; naming and structure rules |
| `engine/schema/codegen/include/oly/schema/{emitter,cpp_emitter,generator}.hpp` + `src/` | emitter interface and registry, the C++ emitter, the command line |
| `engine/schema/codegen/tool/main.cpp` | `oly-schema`'s `main` |
| `engine/schema/format/README.md`, `engine/schema/format/corpus/{valid,invalid}/*.oly` | the `.oly` language and its test corpus |
| `engine/environments/web/clients/typescript/codegen/` (new) | module `schema_typescript`: the TS emitter |
| `engine/environments/web/clients/typescript/core/src/generated.ts` (new) | `@oly/core/generated`: constructors for generated TS |
| `CMakePresets.json`, `Makefile`, `cmake/OlyGame.cmake`, `engine/CMakeLists.txt`, `.gitignore` | build `oly-schema`, generate C++ and TS |
| `games/arena/arena.oly`, `engine/testing/fixture/fixture.oly`, `engine/testing/fixture/schemas/*.oly` (new) | the schemas that replace `schema.ts` and the on-the-fly test schemas |
| `tools/check_boundaries.py` | `@oly/core/generated` only in generated code; `engine/schema/` knows no environment |
| `docs/` | the new schema language and build flow |

---

### Task 1: Schema model, layout and hash in C++

**Files:**
- Create: `engine/schema/codegen/CMakeLists.txt`, `engine/schema/codegen/include/oly/schema/{schema,layout,hash}.hpp`,
  `engine/schema/codegen/src/{schema,layout,hash}.cpp`
- Create (tests): `engine/schema/codegen/tests/{CMakeLists.txt,layout.cpp,hash.cpp}`
- Modify: `engine/CMakeLists.txt`

**Interfaces:**
- Produces: module `schema` (`oly_add_module(NAME schema)`, no dependency), the types below, `scalarTypeName`,
  `actionKindName`, `scalarSize`, `scalarAlignment`, `computeLayout`, `fnv1a32`, `canonicalForm`.
- Produces: the base commit with this plan (Step 1).

- [ ] **Step 1: Commit the plan and compute the reference hash with today's TS code**

If this plan file is still untracked, commit it first
(`git add docs/superpowers/plans/2026-11-13-multi-environment-phase3-schema.md && git commit -m "docs(plans): add the multi-environment phase 3 plan"`).

Run:

```bash
$WC make ts
$WC node --input-type=module -e 'import { fnv1a32 } from "@oly/core/internal"; console.log(fnv1a32("A{x:f32,y:f64};C{a:i8,b:u8,c:i16,d:u16,e:i32,f:u32};B;!E{target:entity,value:u8};@go:button;@aim:pointer;@move:axis2d;#menu;#playing;").toString(16))'
```

Expected: one lowercase hex number. It is `REFERENCE_HASH` in Step 2: the hash today's `@oly/core` gives to that
canonical form, which the C++ code must reproduce.

- [ ] **Step 2: Write the failing tests**

`engine/schema/codegen/tests/CMakeLists.txt`:

```cmake
oly_add_test(MODULE schema NAME layout SOURCES layout.cpp)
oly_add_test(MODULE schema NAME hash SOURCES hash.cpp)
```

`engine/schema/codegen/tests/layout.cpp`, includes ordered as in `engine/runtime/capi/tests/engine_api.cpp` (header
under test, doctest, standard library):

```cpp
#include "oly/schema/layout.hpp"

#include <doctest/doctest.h>

#include <vector>

namespace {
  std::vector<oly::schema::Field> fieldsOf(std::vector<oly::schema::ScalarType> types) {
    std::vector<oly::schema::Field> fields;
    for (const oly::schema::ScalarType type : types) {
      fields.push_back({"f" + std::to_string(fields.size()), type});
    }

    return fields;
  }

  std::vector<uint32_t> offsetsOf(const std::vector<oly::schema::Field>& fields) {
    std::vector<uint32_t> offsets;
    for (const oly::schema::Field& field : fields) {
      offsets.push_back(field.offset);
    }

    return offsets;
  }
}

using oly::schema::ScalarType;

TEST_CASE("two f32 sit together, without padding") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::F32, ScalarType::F32});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 4});
  CHECK(layout.size == 8);
  CHECK(layout.alignment == 4);
}

TEST_CASE("padding before f32") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::U8, ScalarType::F32});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 4});
  CHECK(layout.size == 8);
}

TEST_CASE("f64 aligns the struct to 8") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::F32, ScalarType::F64});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 8});
  CHECK(layout.size == 16);
  CHECK(layout.alignment == 8);
}

TEST_CASE("u16 between u8 aligns the struct to 2") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::U8, ScalarType::U16, ScalarType::U8});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 2, 4});
  CHECK(layout.size == 6);
  CHECK(layout.alignment == 2);
}

TEST_CASE("entity has 8 bytes and alignment 4") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::Entity, ScalarType::U8});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 8});
  CHECK(layout.size == 12);
  CHECK(layout.alignment == 4);
}

TEST_CASE("u8, f64 and entity: 24 bytes, alignment 8") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::U8, ScalarType::F64, ScalarType::Entity});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 8, 16});
  CHECK(layout.size == 24);
  CHECK(layout.alignment == 8);
}
```

(`<string>` for `std::to_string` comes with the header under test; add it if the build says otherwise.)

`engine/schema/codegen/tests/hash.cpp`, with `REFERENCE_HASH` replaced by the number of Step 1:

```cpp
#include "oly/schema/hash.hpp"

#include <doctest/doctest.h>

namespace {
  oly::schema::Type component(std::string name, std::vector<oly::schema::Field> fields) {
    return {oly::schema::TypeKind::Component, std::move(name), std::move(fields)};
  }

  oly::schema::Schema everyDeclaration() {
    using oly::schema::ScalarType;

    oly::schema::Schema schema;
    schema.types.push_back(component("A", {{"x", ScalarType::F32}, {"y", ScalarType::F64}}));
    schema.types.push_back(component("C", {{"a", ScalarType::I8}, {"b", ScalarType::U8}, {"c", ScalarType::I16}, {"d", ScalarType::U16}, {"e", ScalarType::I32}, {"f", ScalarType::U32}}));
    schema.types.push_back({oly::schema::TypeKind::Tag, "B"});
    schema.events.push_back({oly::schema::TypeKind::Event, "E", {{"target", ScalarType::Entity}, {"value", ScalarType::U8}}});
    schema.actions = {{"go", oly::schema::ActionKind::Button}, {"aim", oly::schema::ActionKind::Pointer}, {"move", oly::schema::ActionKind::Axis2d}};
    schema.states = {{"menu"}, {"playing"}};

    return schema;
  }
}

TEST_CASE("fnv1a32 matches the FNV-1a reference values") {
  CHECK(oly::schema::fnv1a32("") == 0x811c9dc5u);
  CHECK(oly::schema::fnv1a32("a") == 0xe40c292cu);
}

TEST_CASE("canonical form: types, then events, actions and states, each in order") {
  CHECK(oly::schema::canonicalForm(everyDeclaration()) == "A{x:f32,y:f64};C{a:i8,b:u8,c:i16,d:u16,e:i32,f:u32};B;!E{target:entity,value:u8};@go:button;@aim:pointer;@move:axis2d;#menu;#playing;");
}

TEST_CASE("the hash of the canonical form is the one the TS client computed") {
  CHECK(oly::schema::fnv1a32(oly::schema::canonicalForm(everyDeclaration())) == 0xREFERENCE_HASHu);
}

TEST_CASE("an empty schema has an empty canonical form") {
  CHECK(oly::schema::canonicalForm(oly::schema::Schema{}).empty());
}
```

`engine/schema/codegen/CMakeLists.txt`:

```cmake
oly_add_module(NAME schema)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
```

`engine/CMakeLists.txt`: the `if(EMSCRIPTEN)` block right after `add_subdirectory(runtime)` gains an `else()`:

```cmake
if(EMSCRIPTEN)
  add_subdirectory(environments/web/host/native)
else()
  # oly-schema runs at build time, so it is built for the machine; WASM builds use the schema-tool preset's.
  add_subdirectory(schema/codegen)
endif()
```

- [ ] **Step 3: Run them to verify they fail**

Run: `$DC make test T='^schema'`
Expected: FAIL to configure or build (`oly/schema/layout.hpp` not found / no sources).

- [ ] **Step 4: Implement the model, the layout and the hash**

`include/oly/schema/schema.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oly::schema {
  enum class ScalarType : uint8_t { F32, F64, I8, U8, I16, U16, I32, U32, Entity };

  enum class ActionKind : uint8_t { Button, Axis2d, Pointer };

  enum class TypeKind : uint8_t { Component, Tag, Event };

  // 1-based; the column counts bytes.
  struct SourceLocation {
    uint32_t line = 1;
    uint32_t column = 1;
  };

  struct Field {
    std::string name{};
    ScalarType type = ScalarType::F32;
    uint32_t offset = 0;
    SourceLocation location{};
  };

  struct Layout {
    uint32_t size = 0;
    uint32_t alignment = 1;
  };

  // A component, a tag (no fields) or an event. `location` is the name's.
  struct Type {
    TypeKind kind = TypeKind::Component;
    std::string name{};
    std::vector<Field> fields{};
    Layout layout{};
    SourceLocation location{};
  };

  struct Action {
    std::string name{};
    ActionKind kind = ActionKind::Button;
    SourceLocation location{};
  };

  struct State {
    std::string name{};
    SourceLocation location{};
  };

  // Each group in declaration order, which is its id order: the ComponentId order for `types`, the dispatch order for
  // `events`, the ActionId for `actions` and the StateId for `states`.
  struct Schema {
    std::string fileName{};
    std::vector<Type> types{};
    std::vector<Type> events{};
    std::vector<Action> actions{};
    std::vector<State> states{};
    uint32_t hash = 0;
  };

  inline constexpr std::size_t MaxStates = 32;

  // A syntax or validation error in a .oly file; what() is "<file>:<line>:<column>: <message>".
  class SchemaError : public std::runtime_error {
  public:
    SchemaError(std::string_view fileName, SourceLocation location, std::string_view message);
  };

  // The .oly spelling: "f32", "entity", "button", "axis2d".
  std::string_view scalarTypeName(ScalarType type);
  std::string_view actionKindName(ActionKind kind);
}
```

`include/oly/schema/layout.hpp` (includes `schema.hpp`):

```cpp
  uint32_t scalarSize(ScalarType type);
  // `entity` is oly::ecs::Entity: two u32, alignment 4.
  uint32_t scalarAlignment(ScalarType type);
  // Natural alignment in field order: fills each field's offset and returns the struct's size and alignment.
  Layout computeLayout(std::vector<Field>& fields);
```

`include/oly/schema/hash.hpp` (includes `schema.hpp`):

```cpp
  // 32-bit FNV-1a over the bytes. Names are ASCII (validation), so the bytes are the UTF-16 code units the TS client
  // hashed before oly-schema existed.
  uint32_t fnv1a32(std::string_view text);
  // `Name{field:type,...};` per component, `Name;` per tag, `!Name{field:type,...};` per event, `@name:kind;` per action
  // and `#name;` per state, each group in order. Changing it changes the hash of every existing schema.
  std::string canonicalForm(const Schema& schema);
```

Every member has a default member initializer (`{}` for class types): the tests brace-initialize these structs
partially, and `-Wextra -Werror` rejects a missing initializer for a member without one
(`-Wmissing-field-initializers`).

`std::string + std::string_view` has no `operator+` before C++26: wrap the `string_view` in `std::string(...)` or use
`append` in every concatenation below (here, in the C++ emitter and in the TS emitter).

`src/schema.cpp`: `SchemaError`'s constructor builds the message
`std::string(fileName) + ":" + std::to_string(location.line) + ":" + std::to_string(location.column) + ": " + std::string(message)`;
`scalarTypeName` and `actionKindName` are a `switch` returning the `.oly` spelling, with a final `return {};` after
the `switch` (GCC warns about the end of a non-void function even when every enumerator returns).

`src/layout.cpp`: `scalarSize` is a `switch` (1 for `I8`/`U8`, 2 for `I16`/`U16`, 4 for `F32`/`I32`/`U32`, 8 for
`F64`/`Entity`, final `return 0;`); `scalarAlignment` returns 4 for `Entity` and `scalarSize` otherwise; in an anonymous
namespace, `uint32_t alignUp(uint32_t value, uint32_t alignment)` returns `(value + alignment - 1) / alignment * alignment`.

```cpp
  Layout computeLayout(std::vector<Field>& fields) {
    Layout layout;
    uint32_t offset = 0;
    for (Field& field : fields) {
      const uint32_t fieldAlignment = scalarAlignment(field.type);
      offset = alignUp(offset, fieldAlignment);
      field.offset = offset;
      offset += scalarSize(field.type);
      layout.alignment = std::max(layout.alignment, fieldAlignment);
    }

    layout.size = alignUp(offset, layout.alignment);
    return layout;
  }
```

`src/hash.cpp`:

```cpp
  uint32_t fnv1a32(std::string_view text) {
    uint32_t hash = 0x811c9dc5u;
    for (const char character : text) {
      hash = (hash ^ static_cast<uint8_t>(character)) * 0x01000193u;
    }

    return hash;
  }
```

`canonicalForm` appends, in this order: for each of `types`, `name + ";"` for a tag or
`name + "{" + fields + "};"` otherwise; for each event, `"!" + name + "{" + fields + "};"`; for each action,
`"@" + name + ":" + actionKindName(kind) + ";"`; for each state, `"#" + name + ";"`. `fields` is
`name + ":" + scalarTypeName(type)` per field, joined by `","`, in an anonymous-namespace helper.

- [ ] **Step 5: Run the tests**

Run: `$DC make test T='^schema'`
Expected: PASS, `schema.layout` and `schema.hash` listed.

Run: `$DC make test`
Expected: PASS (`oly.lint.boundaries` included: the module has no dependency).

Run: `$WC make games-test`
Expected: PASS (Emscripten builds do not configure `engine/schema/`).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(schema): add the schema model, layout and hash in C++"
```

---

### Task 2: The `.oly` language: parser, validation and corpus

**Files:**
- Create: `engine/schema/codegen/src/{parser,validation}.cpp`, `engine/schema/codegen/src/validation.hpp`
- Modify: `engine/schema/codegen/include/oly/schema/schema.hpp`, `.gitattributes`
- Create: `engine/schema/format/README.md`, `engine/schema/format/corpus/valid/*.oly`, `engine/schema/format/corpus/invalid/*.oly`
- Create (tests): `engine/schema/codegen/tests/{parser,corpus}.cpp`; Modify: `engine/schema/codegen/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: the model, `computeLayout`, `canonicalForm`, `fnv1a32` (Task 1).
- Produces (`schema.hpp`, after `actionKindName`):

```cpp
  // Parses, validates, lays out and hashes a .oly source. `fileName` prefixes the errors and is kept in the Schema.
  // Throws SchemaError at the first error.
  Schema parseSchema(std::string_view source, std::string_view fileName);
```

- Produces (internal, `src/validation.hpp`): `void validateSchema(const Schema& schema);`, throwing `SchemaError`.

- [ ] **Step 1: Write the corpus and the failing tests**

`.gitattributes` gains `*.oly text eol=lf`: the repository runs with `core.autocrlf=true`, so without it every `.oly`
comes back with CRLF on checkout (clone, worktree), the corpus expectations keep a `\r` and line-based edits of a
schema miss. The parser still treats `\r` as whitespace for files written outside git.

Every corpus file ends with a newline. Each file in `corpus/invalid/` starts with a line `# expect: <line>:<column>: <message>`
and has its declarations from line 2, so the expected location counts that first line. Files and their content
(line 2 onward) and expectation:

| File | Content | `# expect:` |
|---|---|---|
| `unexpected_character.oly` | `component A { x: f32 }$` | `2:23: unexpected character '$'` |
| `unknown_declaration.oly` | `comp A { x: f32 }` | `2:1: unknown declaration "comp"; use component, tag, event, action or state` |
| `missing_type_name.oly` | `component { x: f32 }` | `2:11: expected a type name after component, found "{"` |
| `missing_colon.oly` | `component A { x f32 }` | `2:17: expected ":" after field x, found "f32"` |
| `unterminated_fields.oly` | `component A { x: f32` | `3:1: expected "," or "}" after field x, found end of file` |
| `unknown_scalar.oly` | `component A { x: f16 }` | `2:18: unknown type "f16" for field x in A; use f32, f64, i8, u8, i16, u16, i32, u32 or entity` |
| `action_kind.oly` | `action go: trigger` | `2:12: action go: unknown kind "trigger"; use button, axis2d or pointer` |
| `empty_component.oly` | `component A {}` | `2:11: component A has no fields: use tag A` |
| `empty_event.oly` | `event E {}` | `2:7: event E has no fields: an event needs at least one field` |
| `type_name_lowercase.oly` | `tag lower` | `2:5: invalid type name "lower": use PascalCase ([A-Z][A-Za-z0-9]*)` |
| `type_name_digit.oly` | `component 1abc { x: f32 }` | `2:11: invalid type name "1abc": use PascalCase ([A-Z][A-Za-z0-9]*)` |
| `field_reserved.oly` | `component A { class: f32 }` | `2:15: invalid field "class" in A: use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words` |
| `field_underscore.oly` | `component A { _x: f32 }` | `2:15: invalid field "_x" in A: use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words` |
| `field_uppercase.oly` | `component A { X: f32 }` | `2:15: invalid field "X" in A: use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words` |
| `repeated_field.oly` | `component A { x: f32, x: f64 }` | `2:23: repeated field "x" in A` |
| `action_name.oly` | `action Go: button` | `2:8: invalid action "Go": use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words` |
| `action_reserved.oly` | `action for: button` | `2:8: invalid action "for": use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words` |
| `state_name.oly` | `state Menu` | `2:7: invalid state "Menu": use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words` |
| `repeated_name.oly` | `component Dup { x: f32 }` then, on line 3, `event Dup { y: f32 }` | `3:7: repeated name "Dup"` |
| `repeated_action_state.oly` | `action go: button` then, on line 3, `state go` | `3:7: repeated name "go"` |
| `too_many_states.oly` | `state s0` to `state s32`, one per line (lines 2–34) | `34:7: 33 states; the maximum is 32` |

`corpus/valid/`:
- `every_declaration.oly`: comments (whole-line and after a declaration), blank lines, declarations of the five kinds
  interleaved (`action go: button` before the first component), one component using every scalar type, a field list
  split over several lines with a trailing comma, a tag, an event.
- `contextual_keywords.oly`: `component A { state: f32, tag: u8, event: i16, action: u32, component: f64 }`.
- `only_comments.oly`: two comment lines.
- `thirty_two_states.oly`: `state s0` to `state s31`.

`engine/schema/codegen/tests/corpus.cpp`:

```cpp
#include "oly/schema/schema.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {
  const std::filesystem::path corpus = OLY_SCHEMA_CORPUS;

  std::vector<std::filesystem::path> schemaFilesIn(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
      if (entry.path().extension() == ".oly") {
        files.push_back(entry.path());
      }
    }

    std::sort(files.begin(), files.end());
    return files;
  }

  std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();

    return text.str();
  }
}

TEST_CASE("every valid corpus file parses") {
  const std::vector<std::filesystem::path> files = schemaFilesIn(corpus / "valid");
  REQUIRE(!files.empty());

  for (const std::filesystem::path& file : files) {
    INFO(file.filename().string());
    CHECK_NOTHROW(oly::schema::parseSchema(readText(file), file.filename().string()));
  }
}

TEST_CASE("every invalid corpus file fails with the error of its first line") {
  const std::vector<std::filesystem::path> files = schemaFilesIn(corpus / "invalid");
  REQUIRE(!files.empty());

  for (const std::filesystem::path& file : files) {
    const std::string text = readText(file);
    const std::string expectPrefix = "# expect: ";
    REQUIRE(text.rfind(expectPrefix, 0) == 0);
    const std::string expected = file.filename().string() + ":" + text.substr(expectPrefix.size(), text.find('\n') - expectPrefix.size());

    INFO(file.filename().string());
    CHECK_THROWS_WITH_AS(oly::schema::parseSchema(text, file.filename().string()), expected.c_str(), oly::schema::SchemaError);
  }
}
```

`engine/schema/codegen/tests/parser.cpp` (the model the corpus does not see):

```cpp
#include "oly/schema/schema.hpp"

#include <doctest/doctest.h>

#include "oly/schema/hash.hpp"

TEST_CASE("each declaration goes to its group, in source order") {
  const oly::schema::Schema schema = oly::schema::parseSchema(
    "state menu\n"
    "action go: button\n"
    "component P { a: u8, b: f64 }\n"
    "event E { target: entity }\n"
    "tag T\n"
    "action aim: pointer\n"
    "state playing\n",
    "test.oly");

  REQUIRE(schema.types.size() == 2);
  CHECK(schema.types[0].name == "P");
  CHECK(schema.types[1].kind == oly::schema::TypeKind::Tag);
  REQUIRE(schema.events.size() == 1);
  CHECK(schema.events[0].kind == oly::schema::TypeKind::Event);
  REQUIRE(schema.actions.size() == 2);
  CHECK(schema.actions[1].kind == oly::schema::ActionKind::Pointer);
  REQUIRE(schema.states.size() == 2);
  CHECK(schema.states[1].name == "playing");
  CHECK(schema.fileName == "test.oly");
}

TEST_CASE("fields keep their order and get the layout") {
  const oly::schema::Schema schema = oly::schema::parseSchema("component P {\n  a: u8,\n  b: f64,\n  owner: entity,\n}\n", "test.oly");

  const oly::schema::Type& type = schema.types.at(0);

  REQUIRE(type.fields.size() == 3);
  CHECK(type.fields[2].name == "owner");
  CHECK(type.fields[2].type == oly::schema::ScalarType::Entity);
  CHECK(type.fields[2].offset == 16);
  CHECK(type.layout.size == 24);
  CHECK(type.layout.alignment == 8);
}

TEST_CASE("names and fields carry their source location") {
  const oly::schema::Schema schema = oly::schema::parseSchema("# comment\n  component P { a: u8 }\n", "test.oly");

  CHECK(schema.types.at(0).location.line == 2);
  CHECK(schema.types.at(0).location.column == 13);
  CHECK(schema.types.at(0).fields.at(0).location.column == 17);
}

TEST_CASE("the hash is FNV-1a of the canonical form") {
  const oly::schema::Schema schema = oly::schema::parseSchema("component P { a: u8 }\ntag T\naction go: button\n", "test.oly");

  CHECK(schema.hash == oly::schema::fnv1a32("P{a:u8};T;@go:button;"));
}

TEST_CASE("an empty source is an empty schema") {
  const oly::schema::Schema schema = oly::schema::parseSchema("", "test.oly");

  CHECK(schema.types.empty());
  CHECK(schema.hash == 0x811c9dc5u);
}

TEST_CASE("carriage returns are whitespace") {
  CHECK(oly::schema::parseSchema("tag T\r\ntag U\r\n", "test.oly").types.size() == 2);
}
```

`tests/CMakeLists.txt` gains:

```cmake
oly_add_test(MODULE schema NAME parser SOURCES parser.cpp)
oly_add_test(MODULE schema NAME corpus SOURCES corpus.cpp)
target_compile_definitions(schema_corpus PRIVATE OLY_SCHEMA_CORPUS="${PROJECT_SOURCE_DIR}/engine/schema/format/corpus")
```

- [ ] **Step 2: Run them to verify they fail**

Run: `$DC make test T='^schema'`
Expected: FAIL to build, `parseSchema` was not declared.

- [ ] **Step 3: Implement the parser**

Add the `parseSchema` declaration to `schema.hpp`. `src/parser.cpp`, in `namespace oly::schema` with the helpers in an
anonymous namespace:

- **Lexer.** Tokens: word (`[A-Za-z0-9_]+`), `{`, `}`, `:`, `,`, end of file. Space, tab, `\r` and `\n` separate
  tokens; `#` starts a comment that runs to the end of the line. Each token keeps its `SourceLocation`; the end of file
  is at the position after the last character (line 3, column 1 for a two-line file ending in a newline). Any other
  character: `unexpected character '<c>'` at its location.
- **Rendering a token in a message:** a word or a punctuation mark between double quotes (`"f32"`, `"{"`); the end as
  `end of file`.
- **Declarations**, read until the end of file. The first word decides; anything else at that position is
  `unknown declaration "<word>"; use component, tag, event, action or state` (a non-word token is
  `expected a declaration, found <token>`).
  - `component` / `event`: a word (else `expected a type name after <keyword>, found <token>`), then `{` (else
    `expected "{" after <keyword> <Name>, found <token>`), then the fields: `}` ends the list; otherwise a word (else
    `expected a field name in <Name>, found <token>`), `:` (else `expected ":" after field <name>, found <token>`), a word
    (else `expected a type for field <name>, found <token>`) that must be a scalar spelling (else
    `unknown type "<word>" for field <name> in <Name>; use f32, f64, i8, u8, i16, u16, i32, u32 or entity`, at the
    word), then `,` or `}` (else `expected "," or "}" after field <name>, found <token>`); after `,`, a `}` ends the list
    (trailing comma).
  - `tag`: a word (else `expected a type name after tag, found <token>`).
  - `action`: a word (else `expected an action name after action, found <token>`), `:` (else
    `expected ":" after action <name>, found <token>`), a word (else `expected an action kind for <name>, found <token>`)
    that must be `button`, `axis2d` or `pointer` (else
    `action <name>: unknown kind "<word>"; use button, axis2d or pointer`, at the word).
  - `state`: a word (else `expected a state name after state, found <token>`).
- Each declaration is appended to its group (`types` for component and tag, `events`, `actions`, `states`) with the
  location of its name; each field with the location of its name.
- After the last declaration: `validateSchema(schema)`, then `computeLayout` on every component's and event's fields
  (a tag keeps `Layout{}`), then `schema.hash = fnv1a32(canonicalForm(schema))`.

- [ ] **Step 4: Implement the validation**

`src/validation.hpp` declares `validateSchema`; `src/validation.cpp` defines it with the helpers in an anonymous
namespace:

- `isPascalCase` (`[A-Z][A-Za-z0-9]*`) and `isCamelCase` (`[a-z][A-Za-z0-9]*`), checked character by character.
- The C++ reserved words, the same list as today's `schema.ts`:

```cpp
    // The names become C++ identifiers (struct names, fields, actions::name, states::name).
    constexpr std::string_view cppReservedWords[] = {
      "class", "struct", "int", "float", "double", "char", "long", "short", "unsigned", "signed", "bool", "void", "auto", "return",
      "operator", "template", "namespace", "new", "delete", "this", "default", "case", "switch", "register", "union", "enum", "const",
      "static", "virtual", "public", "private", "protected", "friend", "using", "typename", "do", "if", "else", "for", "while", "goto",
      "break", "continue", "try", "catch", "throw", "true", "false", "nullptr", "sizeof", "alignof", "export", "extern", "inline",
      "mutable", "volatile", "explicit", "concept", "requires", "and", "or", "not", "xor"};
```

- Declarations are checked in source order, which is not group order: merge the four groups by location
  (line, then column) before checking. For each declaration, in this order: the name rule (types:
  `invalid type name "<n>": use PascalCase ([A-Z][A-Za-z0-9]*)`; actions and states:
  `invalid action|state "<n>": use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words`); a component without
  fields (`component <N> has no fields: use tag <N>`) or an event without fields
  (`event <N> has no fields: an event needs at least one field`), at the name; for each field, the name rule
  (`invalid field "<f>" in <N>: use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words`) and then a repeat
  inside the type (`repeated field "<f>" in <N>`), at the field; a name already used by any earlier declaration
  (`repeated name "<n>"`), at the second one; the 33rd state (`33 states; the maximum is 32`, from `MaxStates`), at it.
- Every error is `throw SchemaError(schema.fileName, location, message)`.

- [ ] **Step 5: Write `engine/schema/format/README.md`**

Sections, in this order: what a `.oly` file is (one per game, the single source of the data layout for every
environment); the grammar in EBNF:

```
file        = { declaration } ;
declaration = component | event | tag | action | state ;
component   = "component" word "{" [ fields ] "}" ;
event       = "event" word "{" [ fields ] "}" ;
tag         = "tag" word ;
action      = "action" word ":" kind ;
state       = "state" word ;
fields      = field { "," field } [ "," ] ;
field       = word ":" scalar ;
scalar      = "f32" | "f64" | "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "entity" ;
kind        = "button" | "axis2d" | "pointer" ;
word        = ( letter | digit | "_" ) { letter | digit | "_" } ;
```

then the lexical rules (whitespace, `#` comments, keywords only at the start of a declaration); the naming rules and
limits (the table of Step 4: PascalCase types, camelCase fields, actions and states, no C++ reserved word, at least one
field in components and events, no repeated name or field, at most 32 states); the order rule (each group's order is
its id order: ComponentId, event dispatch order, ActionId, StateId); the layout rule (natural alignment in field order,
`entity` 8 bytes with alignment 4); the hash (FNV-1a of the canonical form, the form of `hash.hpp`); the error format
`<file>:<line>:<column>: <message>`, first error only; and the corpus (`corpus/valid/` must parse, each
`corpus/invalid/` file names its error on its first line; a new rule adds a file there).

- [ ] **Step 6: Run the tests**

Run: `$DC make test T='^schema'`
Expected: PASS, `schema.parser` and `schema.corpus` listed.

Run: `$DC make test`
Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat(schema): parse and validate the .oly schema language"
```

---

### Task 3: Emitter interface, C++ emitter and the `oly-schema` command line

**Files:**
- Create: `engine/schema/codegen/include/oly/schema/{emitter,cpp_emitter,generator}.hpp`,
  `engine/schema/codegen/src/{emitter,cpp_emitter,generator}.cpp`, `engine/schema/codegen/tool/main.cpp`
- Modify: `engine/schema/codegen/CMakeLists.txt`, `engine/schema/codegen/tests/CMakeLists.txt`
- Create (tests): `engine/schema/codegen/tests/{cpp_emitter,generator}.cpp`

**Interfaces:**
- Consumes: `parseSchema`, `SchemaError` (Task 2).
- Produces (`emitter.hpp`):

```cpp
  struct EmittedFile {
    std::filesystem::path path;
    std::string contents;
  };

  // `target` is what follows `--emit <name>=` on the command line: a directory or a file, as each emitter defines.
  // Throws SchemaError when the schema cannot be expressed in the emitter's language.
  using EmitFunction = std::vector<EmittedFile> (*)(const Schema& schema, const std::filesystem::path& target);

  struct Emitter {
    std::string_view name;
    EmitFunction emit;
  };

  // Called from a namespace-scope initializer in a client's emitter module, which is an OBJECT library so that the
  // linker keeps it. Returns true so that the call can initialize a constant.
  bool registerEmitter(Emitter emitter);

  // The C++ emitter first (C++ is the engine language, not a client), then the registered ones in registration order.
  std::span<const Emitter> emitters();
```

- Produces (`cpp_emitter.hpp`): `std::string emitCppHeader(const Schema&)`, `std::string emitCppRegistry(const Schema&)`,
  and

```cpp
  // `target` is a directory: schema.generated.hpp and schema_registry.generated.cpp.
  std::vector<EmittedFile> emitCpp(const Schema& schema, const std::filesystem::path& target);
```

- Produces (`generator.hpp`):

```cpp
  // The oly-schema command line: `<file.oly> --emit <emitter>=<target> [--emit <emitter>=<target> ...]`. Returns 0, or
  // 1 after writing one error line to `errors`. Checks every --emit before writing anything; writes each file whole,
  // creating its directory.
  int runGenerator(std::span<const std::string_view> arguments, std::ostream& errors);
```

- Produces: the executable `oly-schema`, at `${CMAKE_BINARY_DIR}/bin/oly-schema`.

- [ ] **Step 1: Write the failing tests**

`engine/schema/codegen/tests/cpp_emitter.cpp` ports `engine/tools/codegen/tests/generator.test.ts` case by case: each
TS case becomes a `TEST_CASE` with the same name, the schema written as `.oly` text passed to `parseSchema`, and
`assert.match`/`includes` becoming `CHECK(text.find(...) != std::string::npos)` on the same substrings. Game names
become neutral ones: `Hit` → `E`, `Boom` → `F`, action `fire` → `go`, action `move` → `aim` (with kind `axis2d`
kept). Each regex `\s*` becomes the exact whitespace `emit_cpp.ts` writes at that point, which is not always the same:
`"\n  "` between struct fields, `"\n"` before a struct's `};`, `"\n      "` between two `componentId<...>()` lines,
`"\n    "` between `schemaEvents() {` (or `schemaActions()`, `schemaStates()`) and `return {};`. Copy them from the two
exact-text cases below, never the other way round: these tests pin today's output, and the emitter follows them. The
assert message substrings are `schema: layout of P diverges from oly-schema` and
`schema: layout of E diverges from oly-schema`. Add three cases:

```cpp
TEST_CASE("header: exact text for a component and a tag") {
  const oly::schema::Schema schema = oly::schema::parseSchema("component P { a: u8, b: f64, owner: entity }\ntag T\n", "test.oly");

  CHECK(oly::schema::emitCppHeader(schema) ==
        "// Generated by oly-schema: do not edit.\n"
        "#pragma once\n"
        "\n"
        "#include <cstddef>\n"
        "#include <cstdint>\n"
        "\n"
        "#include \"oly/ecs/entity.hpp\"\n"
        "\n"
        "struct P {\n"
        "  uint8_t a;\n"
        "  double b;\n"
        "  oly::ecs::Entity owner;\n"
        "};\n"
        "static_assert(sizeof(P) == 24 && alignof(P) == 8, \"schema: layout of P diverges from oly-schema\");\n"
        "static_assert(offsetof(P, a) == 0, \"schema: layout of P diverges from oly-schema\");\n"
        "static_assert(offsetof(P, b) == 8, \"schema: layout of P diverges from oly-schema\");\n"
        "static_assert(offsetof(P, owner) == 16, \"schema: layout of P diverges from oly-schema\");\n"
        "\n"
        "struct T {};\n");
}

TEST_CASE("registry: exact text for a schema without events, actions or states") {
  const oly::schema::Schema schema = oly::schema::parseSchema("component P { a: u8 }\ntag T\n", "test.oly");
  std::ostringstream hash;
  hash << std::hex << schema.hash;

  CHECK(oly::schema::emitCppRegistry(schema) ==
        "// Generated by oly-schema: do not edit.\n"
        "#include \"oly/app/engine.hpp\"\n"
        "#include \"oly/capi/schema_registry.hpp\"\n"
        "#include \"schema.generated.hpp\"\n"
        "\n"
        "namespace oly::capi {\n"
        "  uint32_t schemaHash() noexcept {\n"
        "    return 0x" + hash.str() + "u;\n"
        "  }\n"
        "\n"
        "  std::span<const oly::ecs::ComponentId> schemaComponentIds() {\n"
        "    static const oly::ecs::ComponentId ids[] = {\n"
        "      oly::ecs::componentId<P>(),\n"
        "      oly::ecs::componentId<T>()};\n"
        "    return ids;\n"
        "  }\n"
        "\n"
        "  std::span<const SchemaEvent> schemaEvents() {\n"
        "    return {};\n"
        "  }\n"
        "\n"
        "  std::span<const SchemaAction> schemaActions() {\n"
        "    return {};\n"
        "  }\n"
        "\n"
        "  std::span<const SchemaState> schemaStates() {\n"
        "    return {};\n"
        "  }\n"
        "}\n");
}

TEST_CASE("schema without components or tags generates an empty schemaComponentIds") {
  const oly::schema::Schema schema = oly::schema::parseSchema("action go: button\n", "test.oly");

  CHECK(oly::schema::emitCppRegistry(schema).find("schemaComponentIds() {\n    return {};\n  }") != std::string::npos);
}
```

`engine/schema/codegen/tests/generator.cpp`:

```cpp
#include "oly/schema/generator.hpp"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "oly/schema/cpp_emitter.hpp"

namespace {
  std::filesystem::path freshDirectory(std::string_view name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "oly_schema_generator" / name;
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);

    return directory;
  }

  void writeText(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
  }

  std::string readText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream text;
    text << file.rdbuf();

    return text.str();
  }

  int run(const std::vector<std::string>& arguments, std::ostream& errors) {
    const std::vector<std::string_view> views(arguments.begin(), arguments.end());

    return oly::schema::runGenerator(views, errors);
  }
}

TEST_CASE("--emit cpp writes the header and the registry into the target directory") {
  const std::filesystem::path directory = freshDirectory("emit_cpp");
  writeText(directory / "game.oly", "component P { a: u8 }\n");
  std::ostringstream errors;

  const int exitCode = run({(directory / "game.oly").string(), "--emit", "cpp=" + (directory / "out").string()}, errors);

  const oly::schema::Schema schema = oly::schema::parseSchema("component P { a: u8 }\n", "game.oly");
  CHECK(exitCode == 0);
  CHECK(errors.str().empty());
  CHECK(readText(directory / "out" / "schema.generated.hpp") == oly::schema::emitCppHeader(schema));
  CHECK(readText(directory / "out" / "schema_registry.generated.cpp") == oly::schema::emitCppRegistry(schema));
}

TEST_CASE("a schema error is reported with the path as given and nothing is written") {
  const std::filesystem::path directory = freshDirectory("schema_error");
  const std::string input = (directory / "game.oly").string();
  writeText(directory / "game.oly", "component a { x: f32 }\n");
  std::ostringstream errors;

  const int exitCode = run({input, "--emit", "cpp=" + (directory / "out").string()}, errors);

  CHECK(exitCode == 1);
  CHECK(errors.str() == input + ":1:11: invalid type name \"a\": use PascalCase ([A-Z][A-Za-z0-9]*)\n");
  CHECK(!std::filesystem::exists(directory / "out"));
}

TEST_CASE("an unknown emitter names the available ones and nothing is written") {
  const std::filesystem::path directory = freshDirectory("unknown_emitter");
  writeText(directory / "game.oly", "tag T\n");
  std::ostringstream errors;

  const int exitCode = run({(directory / "game.oly").string(), "--emit", "cpp=" + (directory / "out").string(), "--emit", "nope=x"}, errors);

  CHECK(exitCode == 1);
  CHECK(errors.str() == "unknown emitter \"nope\"; available: cpp\n");
  CHECK(!std::filesystem::exists(directory / "out"));
}

TEST_CASE("usage errors: no input, no --emit, --emit without a value or without =") {
  for (const std::vector<std::string>& arguments : std::vector<std::vector<std::string>>{{}, {"game.oly"}, {"game.oly", "--emit"}, {"game.oly", "--emit", "cpp"}}) {
    std::ostringstream errors;

    CHECK(run(arguments, errors) == 1);
    CHECK(errors.str().rfind("usage: oly-schema <file.oly> --emit <emitter>=<target>", 0) == 0);
  }
}

TEST_CASE("an unreadable input is reported") {
  std::ostringstream errors;

  CHECK(run({"missing.oly", "--emit", "cpp=out"}, errors) == 1);
  CHECK(errors.str() == "cannot read missing.oly\n");
}
```

`tests/CMakeLists.txt` gains `oly_add_test(MODULE schema NAME cpp_emitter SOURCES cpp_emitter.cpp)` and
`oly_add_test(MODULE schema NAME generator SOURCES generator.cpp)`.

- [ ] **Step 2: Run them to verify they fail**

Run: `$DC make test T='^schema'`
Expected: FAIL to build (`oly/schema/cpp_emitter.hpp` not found).

- [ ] **Step 3: Implement the C++ emitter**

`src/cpp_emitter.cpp` ports `engine/tools/codegen/src/emit_cpp.ts` function by function (`emitStruct`,
`emitLayoutAsserts`, `emitActions`, `emitStates`, `emitHeader`, `emitEventsRegistry`, `emitActionsRegistry`,
`emitStatesRegistry`, `emitRegistry`), as `std::string` concatenation in an anonymous namespace, keeping every
character of the output: the result must be byte-identical to today's except the two strings of "Decided here", and
`schemaComponentIds()` returns `{}` (same text as `schemaEvents()` without events) when `schema.types` is empty. The
C++ spellings: `float double int8_t uint8_t int16_t uint16_t int32_t uint32_t oly::ecs::Entity`; action handles
`ButtonAction Axis2dAction PointerAction`; action kinds `Button Axis2d Pointer`. The hash is written as `0x`, then
lowercase hex without padding (`std::ostringstream` with `std::hex`), then `u`. `emitCpp` returns
`{{target / "schema.generated.hpp", emitCppHeader(schema)}, {target / "schema_registry.generated.cpp", emitCppRegistry(schema)}}`.

`src/emitter.cpp`:

```cpp
  namespace {
    // Function-local, so a client's initializer can register before main regardless of the order of static
    // initialization across translation units.
    std::vector<Emitter>& registry() {
      static std::vector<Emitter> registered{Emitter{"cpp", &emitCpp}};
      return registered;
    }
  }

  bool registerEmitter(Emitter emitter) {
    registry().push_back(emitter);
    return true;
  }

  std::span<const Emitter> emitters() {
    return registry();
  }
```

- [ ] **Step 4: Implement the command line and the executable**

`src/generator.cpp`, `runGenerator`:

1. The first argument is the input; then pairs `--emit <name>=<target>`. No input, no `--emit`, an `--emit` without a
   value, a value without `=`, or any other argument: write
   `usage: oly-schema <file.oly> --emit <emitter>=<target> [--emit <emitter>=<target> ...]\n` and return 1.
2. Every `--emit` name must be in `emitters()`; else write `unknown emitter "<name>"; available: cpp, ts\n` (the
   registered names joined by `", "`) and return 1.
3. Read the input in binary mode; if it cannot be opened, write `cannot read <input>\n` and return 1.
4. `parseSchema(text, input)`, then run every emitter and collect its files; a `SchemaError` writes `what()` plus `\n`
   and returns 1. Nothing is written until every emitter has succeeded.
5. Write each file: `create_directories(path.parent_path())` when it is not empty, then an `std::ofstream` in binary
   mode; a failure writes `cannot write <path>\n` and returns 1. Return 0.

`tool/main.cpp`:

```cpp
#include <iostream>
#include <string_view>
#include <vector>

#include "oly/schema/generator.hpp"

int main(int argumentCount, char** argumentValues) {
  const std::vector<std::string_view> arguments(argumentValues + 1, argumentValues + argumentCount);

  return oly::schema::runGenerator(arguments, std::cerr);
}
```

`engine/schema/codegen/CMakeLists.txt`, between `oly_add_module` and the tests:

```cmake
add_executable(oly-schema tool/main.cpp)
target_link_libraries(oly-schema PRIVATE oly::schema)
oly_warnings(oly-schema)
set_target_properties(oly-schema PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
```

- [ ] **Step 5: Run the tests**

Run: `$DC make test T='^schema'`
Expected: PASS, `schema.cpp_emitter` and `schema.generator` listed.

Run: `$DC sh -c 'printf "component P { a: u8 }\n" > build/smoke.oly && build/debug-asan/bin/oly-schema build/smoke.oly --emit cpp=build/smoke-cpp && ls build/smoke-cpp'`
Expected: `schema.generated.hpp` and `schema_registry.generated.cpp`.

Run: `$DC make test`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(schema): emit the C++ schema from oly-schema"
```

---

### Task 4: The TS emitter and `@oly/core/generated`

**Files:**
- Create: `engine/environments/web/clients/typescript/codegen/CMakeLists.txt`,
  `engine/environments/web/clients/typescript/codegen/include/oly/schema_typescript/typescript_emitter.hpp`,
  `engine/environments/web/clients/typescript/codegen/src/typescript_emitter.cpp`
- Create (tests): `engine/environments/web/clients/typescript/codegen/tests/{CMakeLists.txt,typescript_emitter.cpp}`
- Create: `engine/environments/web/clients/typescript/core/src/generated.ts`,
  `engine/environments/web/clients/typescript/core/tests/unit/generated.test.ts`
- Modify: `engine/environments/web/clients/typescript/core/package.json`, `engine/CMakeLists.txt`

**Interfaces:**
- Consumes: `Schema`, `SchemaError`, `EmittedFile`, `registerEmitter` (Tasks 1–3).
- Produces: module `schema_typescript` (`OBJECT`, depends on `schema`), linked into `oly-schema`, registered as `ts`;

```cpp
  // The schema.generated.ts module for @oly/core: one exported constant per entry and the default Schema. Throws
  // oly::schema::SchemaError for an action or state whose name cannot be a TypeScript constant.
  std::string emitTypescript(const oly::schema::Schema& schema);
```

- Produces (TS, `@oly/core/generated`): `componentType`, `tagType`, `eventType`, `actionType`, `stateType`, `schemaOf`.

- [ ] **Step 1: Write the failing tests**

`engine/environments/web/clients/typescript/codegen/tests/typescript_emitter.cpp`:

```cpp
#include "oly/schema_typescript/typescript_emitter.hpp"

#include <doctest/doctest.h>

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "oly/schema/emitter.hpp"

namespace {
  std::string hashOf(const oly::schema::Schema& schema) {
    std::ostringstream hash;
    hash << "0x" << std::hex << schema.hash;

    return hash.str();
  }
}

TEST_CASE("one constant per entry, grouped like the schema, and the default Schema with the hash") {
  const oly::schema::Schema schema = oly::schema::parseSchema(
    "state menu\n"
    "component P { a: u8, b: f64 }\n"
    "action go: button\n"
    "tag T\n"
    "event E { target: entity, amount: f32 }\n"
    "action aim: pointer\n",
    "test.oly");

  CHECK(oly::schema_typescript::emitTypescript(schema) ==
        "// Generated by oly-schema: do not edit.\n"
        "import { actionType, componentType, eventType, schemaOf, stateType, tagType } from \"@oly/core/generated\";\n"
        "\n"
        "export const P = componentType(\"P\", { a: \"u8\", b: \"f64\" }, 0, { size: 16, alignment: 8, offsets: { a: 0, b: 8 } });\n"
        "export const T = tagType(\"T\", 1);\n"
        "export const E = eventType(\"E\", { target: \"entity\", amount: \"f32\" }, 0, { size: 12, alignment: 4, offsets: { target: 0, amount: 8 } });\n"
        "export const go = actionType(\"go\", \"button\", 0);\n"
        "export const aim = actionType(\"aim\", \"pointer\", 1);\n"
        "export const menu = stateType(\"menu\", 0);\n"
        "\n"
        "export default schemaOf([P, T, E, go, aim, menu], " + hashOf(schema) + ");\n");
}

TEST_CASE("imports only the constructors the schema uses") {
  const oly::schema::Schema schema = oly::schema::parseSchema("component P { a: u8 }\n", "test.oly");

  CHECK(oly::schema_typescript::emitTypescript(schema).find("import { componentType, schemaOf } from \"@oly/core/generated\";\n") != std::string::npos);
}

TEST_CASE("an empty schema exports only the Schema") {
  const oly::schema::Schema schema = oly::schema::parseSchema("", "test.oly");

  CHECK(oly::schema_typescript::emitTypescript(schema) ==
        "// Generated by oly-schema: do not edit.\n"
        "import { schemaOf } from \"@oly/core/generated\";\n"
        "\n"
        "export default schemaOf([], 0x811c9dc5);\n");
}

TEST_CASE("an action or state that cannot be a TypeScript constant is an error at its name") {
  CHECK_THROWS_WITH_AS(oly::schema_typescript::emitTypescript(oly::schema::parseSchema("tag T\naction function: button\n", "test.oly")),
                       "test.oly:2:8: action \"function\" cannot be a TypeScript constant: it is a reserved word or a name the generated module imports",
                       oly::schema::SchemaError);
  CHECK_THROWS_WITH_AS(oly::schema_typescript::emitTypescript(oly::schema::parseSchema("state schemaOf\n", "test.oly")),
                       "test.oly:1:7: state \"schemaOf\" cannot be a TypeScript constant: it is a reserved word or a name the generated module imports",
                       oly::schema::SchemaError);
}

TEST_CASE("oly-schema lists ts after cpp when this module is linked") {
  std::vector<std::string_view> names;
  for (const oly::schema::Emitter& emitter : oly::schema::emitters()) {
    names.push_back(emitter.name);
  }

  CHECK(names == std::vector<std::string_view>{"cpp", "ts"});
}
```

Its `CMakeLists.txt`: `oly_add_test(MODULE schema_typescript NAME emitter SOURCES typescript_emitter.cpp)`.

`engine/environments/web/clients/typescript/core/tests/unit/generated.test.ts`:

```ts
import assert from "node:assert/strict";
import { test } from "node:test";

import type { ValueOf } from "@oly/core";
import { actionType, componentType, eventType, schemaOf, stateType, tagType } from "@oly/core/generated";

const P = componentType("P", { a: "u8", b: "f64" }, 0, { size: 16, alignment: 8, offsets: { a: 0, b: 8 } });
const T = tagType("T", 1);
const E = eventType("E", { target: "entity", amount: "f32" }, 0, { size: 12, alignment: 4, offsets: { target: 0, amount: 8 } });
const go = actionType("go", "button", 0);
const menu = stateType("menu", 0);

test("componentType builds the layout in field order from the generator's offsets", () => {
  assert.deepEqual(P.layout, {
    size: 16,
    alignment: 8,
    fields: [
      { name: "a", type: "u8", offset: 0 },
      { name: "b", type: "f64", offset: 8 },
    ],
  });
  assert.equal(P.schemaIndex, 0);
  assert.ok(Object.isFrozen(P));
});

test("schemaOf groups the entries by kind, in order, and keeps the hash", () => {
  const schema = schemaOf([P, T, E, go, menu], 0x1234);

  assert.deepEqual(schema.types, [P, T]);
  assert.deepEqual(schema.events, [E]);
  assert.deepEqual(schema.actions, [go]);
  assert.deepEqual(schema.states, [menu]);
  assert.equal(schema.hash, 0x1234);
  assert.ok(Object.isFrozen(schema.types));
});

test("the generated constants keep literal names, fields and kinds", () => {
  const name: "P" = P.name;
  const kind: "button" = go.actionKind;
  const value: ValueOf<typeof P.fields> = { a: 1, b: 2 };
  // @ts-expect-error: a field the schema does not declare must be a compile error, so the field names stay literal.
  const unknownField: ValueOf<typeof P.fields> = { a: 1, b: 2, c: 3 };

  assert.deepEqual([name, kind, value.a, unknownField.a], ["P", "button", 1, 1]);
});
```

- [ ] **Step 2: Run them to verify they fail**

Run: `$DC make test T='^schema_typescript'`
Expected: FAIL (no tests found: the module does not exist yet; `--no-tests=error`).

Run: `$WC make ts`
Expected: FAIL in `tsc`, `@oly/core/generated` cannot be resolved.

- [ ] **Step 3: Implement `@oly/core/generated`**

`core/package.json`, `exports` gains
`"./generated": { "types": "./dist/generated.d.ts", "default": "./dist/generated.js" }` after `"./internal"`.

`core/src/generated.ts`:

```ts
import type { ComponentLayout } from "./layout.js";
import type { ActionKind, ActionType, ComponentType, EventType, Fields, Schema, SchemaType, State, TagType } from "./schema.js";

// Constructors for the code oly-schema generates (`*.generated.ts`); the boundary lint keeps hand-written code out.
// They trust their input: oly-schema has validated the names and computed the layout. Properties keep the order the
// old defineSchema objects had, so the hot path sees the same object shapes.

export interface GeneratedLayout<F extends Fields> {
  readonly size: number;
  readonly alignment: number;
  readonly offsets: { readonly [K in keyof F]: number };
}

function layoutOf<F extends Fields>(fields: F, layout: GeneratedLayout<F>): ComponentLayout {
  const offsets = layout.offsets as Readonly<Record<string, number>>;

  return Object.freeze({
    size: layout.size,
    alignment: layout.alignment,
    fields: Object.freeze(Object.entries(fields).map(([name, type]) => Object.freeze({ name, type, offset: offsets[name]! }))),
  });
}

export function componentType<const Name extends string, const F extends Fields>(
  name: Name,
  fields: F,
  schemaIndex: number,
  layout: GeneratedLayout<F>,
): ComponentType<Name, F> {
  return Object.freeze({ kind: "component", name, fields, layout: layoutOf(fields, layout), schemaIndex });
}

export function tagType<const Name extends string>(name: Name, schemaIndex: number): TagType<Name> {
  return Object.freeze({ kind: "tag", name, schemaIndex });
}

export function eventType<const Name extends string, const F extends Fields>(
  name: Name,
  fields: F,
  schemaIndex: number,
  layout: GeneratedLayout<F>,
): EventType<Name, F> {
  return Object.freeze({ kind: "event", name, fields, layout: layoutOf(fields, layout), schemaIndex });
}

export function actionType<const Name extends string, const Kind extends ActionKind>(
  name: Name,
  kind: Kind,
  schemaIndex: number,
): ActionType<Name, Kind> {
  return Object.freeze({ kind: "action", actionKind: kind, name, schemaIndex });
}

export function stateType<const Name extends string>(name: Name, schemaIndex: number): State<Name> {
  return Object.freeze({ kind: "state", name, schemaIndex });
}

export function schemaOf(entries: readonly (SchemaType | EventType | ActionType | State)[], hash: number): Schema {
  return Object.freeze({
    types: Object.freeze(entries.filter((entry): entry is SchemaType => entry.kind === "component" || entry.kind === "tag")),
    events: Object.freeze(entries.filter((entry): entry is EventType => entry.kind === "event")),
    actions: Object.freeze(entries.filter((entry): entry is ActionType => entry.kind === "action")),
    states: Object.freeze(entries.filter((entry): entry is State => entry.kind === "state")),
    hash,
  });
}
```

If `tsc` rejects a `kind` literal inside `Object.freeze({...})` as `string`, add `as const` to that literal only.

- [ ] **Step 4: Implement the TS emitter module**

`engine/environments/web/clients/typescript/codegen/CMakeLists.txt`:

```cmake
# OBJECT: the emitter registers itself from a namespace-scope initializer, which a static library would drop.
oly_add_module(NAME schema_typescript DEPENDS schema OBJECT)
target_link_libraries(oly-schema PRIVATE oly::schema_typescript)

if(OLY_BUILD_TESTS)
  add_subdirectory(tests)
endif()
```

`engine/CMakeLists.txt`, in the `else()` of Task 1, after `add_subdirectory(schema/codegen)`:
`add_subdirectory(environments/web/clients/typescript/codegen)`.

`src/typescript_emitter.cpp`, in `namespace oly::schema_typescript`, helpers in an anonymous namespace:

```cpp
    // Names that pass the .oly rules but cannot be a TypeScript `export const`: reserved words that C++ allows, and the
    // constructors the generated module imports.
    constexpr std::string_view unavailableNames[] = {
      "arguments", "await", "debugger", "eval", "extends", "finally", "function", "implements", "import", "in", "instanceof",
      "interface", "let", "null", "package", "super", "typeof", "var", "with", "yield",
      "actionType", "componentType", "eventType", "schemaOf", "stateType", "tagType"};
```

- `emitTypescript` first checks every action, then every state, against `unavailableNames`, throwing
  `oly::schema::SchemaError(schema.fileName, location, "<action|state> \"<name>\" cannot be a TypeScript constant: it is a reserved word or a name the generated module imports")`.
- Then it writes, as the Step 1 test shows: the header comment; one `import { ... } from "@oly/core/generated";` with
  the constructors used, sorted (`actionType componentType eventType schemaOf stateType tagType`; `schemaOf` always);
  a blank line; one `export const` per entry, groups in order (types, events, actions, states), each group in
  declaration order, the index being the position inside the group; a blank line only when there was at least one
  constant; `export default schemaOf([<all constants in that order>], 0x<hash>);`.
- A component or event: `componentType("<N>", { f: "<type>", ... }, <index>, { size: <s>, alignment: <a>, offsets: { f: <offset>, ... } })`
  (`eventType` for an event); a tag: `tagType("<N>", <index>)`; an action: `actionType("<n>", "<kind>", <index>)`; a
  state: `stateType("<n>", <index>)`. Spellings from `scalarTypeName` and `actionKindName`.
- At the end of the anonymous namespace:

```cpp
    std::vector<oly::schema::EmittedFile> emitFile(const oly::schema::Schema& schema, const std::filesystem::path& target) {
      return {{target, emitTypescript(schema)}};
    }

    [[maybe_unused]] const bool registered = oly::schema::registerEmitter({"ts", &emitFile});
```

- [ ] **Step 5: Run the tests**

Run: `$DC make test T='^schema'`
Expected: PASS, `schema_typescript.emitter` included.

Run: `$DC sh -c 'printf "component P { a: u8 }\naction go: button\n" > build/smoke.oly && build/debug-asan/bin/oly-schema build/smoke.oly --emit ts=build/smoke.ts && cat build/smoke.ts'`
Expected: the module with `componentType("P", ...)`, `actionType("go", "button", 0)` and `schemaOf([P, go], 0x...)`.

Run: `$DC make test`
Expected: PASS (`oly.lint.boundaries`: `schema_typescript` depends on `schema`, which is outside the environments).

Run: `$WC make games-test`
Expected: PASS (`ts.core.unit` with the three new cases).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat(schema): emit the TypeScript schema for @oly/core"
```

---

### Task 5: Games and the fixture build from `.oly`

**Files:**
- Modify: `CMakePresets.json`, `Makefile`, `cmake/OlyGame.cmake`, `.gitignore`
- Create: `games/arena/arena.oly`, `engine/testing/fixture/fixture.oly`; Delete: `games/arena/web/schema.ts`, `engine/testing/fixture/schema.ts`
- Modify: `games/arena/CMakeLists.txt`, `engine/testing/CMakeLists.txt`, `games/arena/web/tsconfig.json`
- Modify (imports): `games/arena/web/src/{game,main,render}.ts`, `games/arena/web/tests/{harness,smoke.test}.ts`,
  `engine/testing/tests/wasm/fixture.ts`, `engine/testing/bench/{boundary,common,events,loop}.ts`

**Interfaces:**
- Consumes: `oly-schema` with the `cpp` and `ts` emitters (Tasks 3–4).
- Produces: `oly_add_game(<target> SCHEMA <file.oly> SOURCES <cpp...> DEPENDS <m...>)` (replaces `SCHEMA_JS`); cache
  variable `OLY_SCHEMA_TOOL`; preset `schema-tool`; make target `schema-tool`; `make ts` generates the TS schemas.
- Produces (TS modules): `games/arena/web/schema.generated.ts`, `engine/testing/fixture/fixture.generated.ts`.

- [ ] **Step 1: Keep today's generated C++ for the parity check**

Run: `$WC make games-test`
Expected: PASS (still the TS schemas and `@oly/codegen`).

Run:

```bash
mkdir -p build/parity/fixture build/parity/arena
cp build/wasm-games/engine/testing/fixture_game_generated/* build/parity/fixture/
cp build/wasm-games/games/arena/arena_game_generated/* build/parity/arena/
```

Expected: each folder holds `schema.generated.hpp` and `schema_registry.generated.cpp`.

- [ ] **Step 2: Build `oly-schema` for the machine in the `wasm` image**

`CMakePresets.json`: a configure preset after `relwithdebinfo`:

```json
    {
      "name": "schema-tool", "inherits": "base",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release", "OLY_SANITIZE": "OFF", "OLY_WERROR": "OFF",
        "OLY_BUILD_TESTS": "OFF", "OLY_BUILD_BENCH": "OFF"
      }
    },
```

`OLY_WERROR` is off only here. The native presets already build the same code with `-Werror` and GCC 14. This preset
uses the `wasm` image's older GCC. In Release, that GCC can raise false `-Wrestrict`/`-Wstringop-*` warnings on
`std::string` concatenation. Such a warning would stop `make ts` and with it every WASM flow, without any defect in
the code. JSON has no comments, so this reason goes in the commit message.

and a build preset `{ "name": "schema-tool", "configurePreset": "schema-tool", "targets": ["oly-schema"] }`. No test
preset.

`Makefile`, after the `run`/`all` targets:

```make
SCHEMA_TOOL = build/schema-tool/bin/oly-schema

# Built for the machine: a WASM build cannot run the oly-schema it compiles. Only in the wasm image; native presets
# build oly-schema with the tree.
schema-tool:
	cmake --preset schema-tool
	cmake --build --preset schema-tool -j
.PHONY: schema-tool
```

`ts` becomes:

```make
# The TS code imports the generated schemas, so they exist before tsc: a game's goes into its web/ package, a test
# schema's next to its .oly.
ts: schema-tool
	for web in games/*/web; do game=$$(basename $$(dirname $$web)); $(SCHEMA_TOOL) games/$$game/$$game.oly --emit ts=$$web/schema.generated.ts || exit 1; done
	for schema in $$(find engine/testing -name '*.oly'); do $(SCHEMA_TOOL) $$schema --emit ts=$${schema%.oly}.generated.ts || exit 1; done
	([ -d node_modules ] || $(NPM) ci --no-audit --no-fund) && $(NPM) run build
.PHONY: ts
```

`.gitignore` gains `*.generated.ts`.

- [ ] **Step 3: `oly_add_game` runs `oly-schema`**

`cmake/OlyGame.cmake`:

```cmake
find_program(OLY_NODE node)
set(OLY_SCHEMA_TOOL ${PROJECT_SOURCE_DIR}/build/schema-tool/bin/oly-schema CACHE FILEPATH "oly-schema executable that oly_add_game runs")

# oly_add_game(<target> SCHEMA <file.oly> SOURCES <cpp...> DEPENDS <m...>)
# Generates the schema structs and registration with oly-schema, compiles the game with the modules and links the WASM module (.mjs) with the web host's script invoker.
function(oly_add_game target)
  cmake_parse_arguments(GAME "" "SCHEMA" "SOURCES;DEPENDS" ${ARGN})

  if(NOT EXISTS ${OLY_SCHEMA_TOOL})
    message(FATAL_ERROR "oly-schema not found at ${OLY_SCHEMA_TOOL}: run 'make schema-tool' (or 'make ts') before configuring with OLY_BUILD_GAMES=ON")
  endif()
```

The custom command becomes
`COMMAND ${OLY_SCHEMA_TOOL} ${GAME_SCHEMA} --emit cpp=${generatedDirectory}` with
`DEPENDS ${GAME_SCHEMA} ${OLY_SCHEMA_TOOL}`; the `OLY_GENERATOR` variable and the node check go away. The rest of the
function does not change. `find_program(OLY_NODE node)` stays: the TS tests run with it.

- [ ] **Step 4: Write the two schemas**

`engine/testing/fixture/fixture.oly`:

```
component Position { x: f32, y: f32 }
component Velocity { x: f32, y: f32 }
component Mixed { a: u8, b: f64, c: i16, owner: entity }
component Trace { value: f64 }
tag Player
tag Enemy

event Ping { target: entity, value: f32 }
event Echo { depth: u32 }

action fire: button
action move: axis2d
action aim: pointer
action togglePause: button

state menu
state playing
state gameOver
```

`games/arena/arena.oly`: the declarations of `games/arena/web/schema.ts` in the same order and with the same fields
(seven components, five tags, three events, six actions, three states), grouped like the fixture's with one blank
line between groups. Then `git rm games/arena/web/schema.ts engine/testing/fixture/schema.ts`.

`engine/testing/CMakeLists.txt`: `SCHEMA_JS ${PROJECT_SOURCE_DIR}/engine/testing/dist/fixture/schema.js` →
`SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/fixture/fixture.oly`. `games/arena/CMakeLists.txt`:
`SCHEMA_JS ${CMAKE_CURRENT_SOURCE_DIR}/web/dist/schema.js` → `SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/arena.oly`.

- [ ] **Step 5: TS imports the generated modules**

- `games/arena/web/tsconfig.json`: `"include": ["schema.generated.ts", "src"]`.
- `games/arena/web/src/{game,main,render}.ts` and `games/arena/web/tests/{harness,smoke.test}.ts`: `"../schema.js"` →
  `"../schema.generated.js"`.
- `engine/testing/tests/wasm/fixture.ts`: both `"../../fixture/schema.js"` → `"../../fixture/fixture.generated.js"`.
- `engine/testing/bench/{boundary,common,events,loop}.ts`: `"../fixture/schema.js"` → `"../fixture/fixture.generated.js"`.
- `games/arena/README.md`: the sentence about the test TS projects says `../schema.generated.js` instead of
  `../schema.js`.

- [ ] **Step 6: Run the tests**

Run: `$WC make games-test`
Expected: PASS (`ts.testing.wasm`, `ts.arena.smoke`: `loadRuntime` accepted the generated TS schemas, so their hash
matches the C++ registry).

Run: `$DC make test`
Expected: PASS.

Run: `git status --short | grep generated`
Expected: no output (`*.generated.ts` is ignored).

- [ ] **Step 7: Parity with the TS-generated C++**

Run:

```bash
$WC bash -c 'for game in fixture arena; do
  case $game in fixture) generated=build/wasm-games/engine/testing/fixture_game_generated ;; arena) generated=build/wasm-games/games/arena/arena_game_generated ;; esac
  for file in schema.generated.hpp schema_registry.generated.cpp; do
    diff <(sed -e "s/oly-codegen/oly-schema/" -e "s/diverges from TS/diverges from oly-schema/" build/parity/$game/$file) $generated/$file || echo "DIFFERS: $game/$file"
  done
done'
```

Expected: no output. The registry holds `schemaHash()`, so equal files mean equal hashes. Any output: stop and fix the
C++ emitter (Task 3) before going on; the parity copies stay under `build/` (ignored).

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "build: generate the game schemas from .oly files with oly-schema"
```

---

### Task 6: WASM tests use `.oly` files for their extra schemas

**Files:**
- Create: `engine/testing/fixture/schemas/{position_f64,extra_event,wider_ping,extra_action,fire_axis2d,foreign_action,foreign_state}.oly`
- Modify: `engine/testing/tests/wasm/{load,actions,states}.test.ts`

**Interfaces:**
- Consumes: `make ts` generating `<stem>.generated.ts` next to every `.oly` under `engine/testing/` (Task 5).

- [ ] **Step 1: Write the schemas**

In `engine/testing/fixture/schemas/`, each file is the schema the test builds today with `defineSchema`, in the same
order. "Fixture types" are the six declarations of `fixture.oly`'s first group (Position, Velocity, Mixed, Trace,
Player, Enemy); "fixture events" are its `Ping` and `Echo`.

| File | Content |
|---|---|
| `position_f64.oly` | `component Position { x: f64, y: f32 }`, Velocity, Mixed, `tag Player`, `tag Enemy` (no Trace, as today) |
| `extra_event.oly` | fixture types, fixture events, `event Extra { x: f32 }` |
| `wider_ping.oly` | fixture types, `event Ping { target: entity, value: f64 }`, `event Echo { depth: u32 }` |
| `extra_action.oly` | fixture types, fixture events, `fire: button`, `move: axis2d`, `aim: pointer`, `togglePause: button`, `extra: button` |
| `fire_axis2d.oly` | fixture types, fixture events, `fire: axis2d`, `move: axis2d`, `aim: pointer`, `togglePause: button` |
| `foreign_action.oly` | `action fire: button` |
| `foreign_state.oly` | `state foreign` |

- [ ] **Step 2: The tests import them**

`load.test.ts`:
- The `@oly/core` import keeps `ContractError`, `SchemaMismatchError`, `loadRuntime`; `fixtureTypes()` goes away.
- `import positionF64Schema from "../../fixture/schemas/position_f64.generated.js";` and likewise `extraEventSchema`,
  `widerPingSchema`, `extraActionSchema`, `fireAxis2dSchema`.
- Each test passes its imported schema to `loadRuntime` where it built `otherSchema`, `withExtraEvent`,
  `withWiderPing`, `withAction` or `withWrongKind`. The assertions do not change.

`actions.test.ts`: the `@oly/core` import drops `action` and `defineSchema`; add
`import { fire as foreignFire } from "../../fixture/schemas/foreign_action.generated.js";` and the line
`const foreign = defineSchema([action("fire", "button")]).actions[0]!;` becomes `const foreign = foreignFire;`.

`states.test.ts`: the `@oly/core` import drops `state`; add
`import { foreign } from "../../fixture/schemas/foreign_state.generated.js";` and remove `const foreign = state("foreign");`.

A foreign entry now has `schemaIndex` 0 instead of -1; the runtime still rejects it, because it compares the object
with the runtime schema's entry at that index.

- [ ] **Step 3: Run the tests**

Run: `$WC make games-test`
Expected: PASS, every case of `ts.testing.wasm` included.

Run: `git grep -n -E 'defineSchema|\b(component|tag|event|action|state)\(' -- engine/testing games`
Expected: no output.

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "test: declare the WASM tests' extra schemas in .oly files"
```

---

### Task 7: Remove the TS schema API and `@oly/codegen`; lint the new boundaries

**Files:**
- Modify: `engine/environments/web/clients/typescript/core/src/{schema,layout,index,internal}.ts`,
  `engine/environments/web/clients/typescript/core/api/public-exports.txt`
- Delete: `engine/environments/web/clients/typescript/core/src/schema_hash.ts`,
  `engine/environments/web/clients/typescript/core/tests/unit/{schema,layout}.test.ts`, `engine/tools/codegen/`
- Modify: `package.json`, `package-lock.json`, `tsconfig.json`, `engine/CMakeLists.txt`
- Modify: `tools/check_boundaries.py`; Test: `tools/test_check_boundaries.py`

**Interfaces:**
- Removes (public): `component`, `tag`, `event`, `action`, `state`, `defineSchema`. Removes (internal):
  `computeLayout`, `scalarAlignment`, `canonicalSchema`, `fnv1a32`, `schemaHash`. Removes: `@oly/codegen`,
  `ts.codegen.unit`.
- Produces lint messages:
  - `<file>:<line>: @oly/core/generated can only be imported by generated code (*.generated.ts)`
  - `<file>:<line>: engine/schema cannot depend on environment <env> (<oly/x or @oly/x>)`

- [ ] **Step 1: Write the failing lint tests**

Add to `tools/test_check_boundaries.py`:

```python
    def test_generated_subpath_in_hand_written_code_fails(self):
        self.write("games/g/web/src/a.ts", 'import { componentType } from "@oly/core/generated";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("games/g/web/src/a.ts:1: @oly/core/generated can only be imported by generated code (*.generated.ts)", output)

    def test_generated_subpath_in_generated_code_and_in_core_passes(self):
        self.write("games/g/web/schema.generated.ts", 'import { componentType } from "@oly/core/generated";\n')
        self.write("engine/environments/web/clients/typescript/core/tests/unit/generated.test.ts", 'import { schemaOf } from "@oly/core/generated";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 0)
        self.assertEqual(output, "")

    def test_schema_tooling_including_environment_fails(self):
        self.write("engine/environments/web/clients/typescript/codegen/include/oly/schema_typescript/typescript_emitter.hpp", "#pragma once\n")
        self.write("engine/schema/codegen/src/x.cpp", '#include "oly/schema_typescript/typescript_emitter.hpp"\n')

        code, output = self.run_check([("schema", "engine/schema/codegen", ["schema_typescript"]), ("schema_typescript", "engine/environments/web/clients/typescript/codegen", ["schema"])])

        self.assertEqual(code, 1)
        self.assertIn("engine/schema/codegen/src/x.cpp:1: engine/schema cannot depend on environment web (oly/schema_typescript)", output)
```

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: FAIL in the two `*_fails` tests; the `*_passes` test passes. A `*_fails` test that passes here means its rule
would never fire: fix the test first.

- [ ] **Step 2: Implement the lint rules**

`tools/check_boundaries.py`:
- The docstring gains ", generated-code imports and the core's independence from environments".
- `RUNTIME_PREFIX` stays (the `detail/` rule uses it); add
  `CORE_PREFIXES = ("engine/runtime/", "engine/schema/")` with the comment
  `# The engine and its schema tooling know no environment; environments depend on them.`
- `environment_violation`: the `relative_path.startswith(RUNTIME_PREFIX)` branch becomes: the first prefix of
  `CORE_PREFIXES` that `relative_path` starts with, giving
  `f"{prefix.rstrip('/')} cannot depend on environment {target_environment} ({target_name})"`.
- `check_ts_file`, next to the `@oly/core/internal` rule:
  `if specifier.startswith("@oly/core/generated") and not relative_path.endswith(".generated.ts") and not inside_core:`
  appends `f"{relative_path}:{line_number}: @oly/core/generated can only be imported by generated code (*.generated.ts)"`.

Run: `$DC python3 -I tools/test_check_boundaries.py`
Expected: PASS.

- [ ] **Step 3: Remove the TS schema API**

- `schema.ts`: remove the imports of `ContractError`, `computeLayout` and `schemaHash`, `MaxStates`, the name patterns,
  `actionKinds`, `cppReservedWords`, the three `validate*` functions and the six functions `component`, `event`,
  `action`, `state`, `tag`, `defineSchema`. Keep every type. `import type { ComponentLayout, ScalarType } from "./layout.js";`.
  The `schemaIndex` comments become:
  - `ComponentType` and `TagType`: `/** Position in \`schema.types\`, which is also the ComponentId order in the WASM. */`
  - `EventType`: `/** Position in \`schema.events\`, which is the dispatch order. */`
  - `ActionType`: `/** Position in \`schema.actions\`, which is also the ActionId. */`
  - `State`: `/** Position in \`schema.states\`, which is also the StateId. */`
- `layout.ts`: remove `import type { Fields }`, the `entity` alignment comment, `scalarAlignment`, `alignUp` and
  `computeLayout`; keep the types, `scalarSizes` and `scalarSize`.
- `git rm engine/environments/web/clients/typescript/core/src/schema_hash.ts` and the unit tests `schema.test.ts` and
  `layout.test.ts` (their cases live in `schema.layout`, `schema.hash`, `schema.parser` and the corpus).
- `index.ts`: remove the line `export { action, component, defineSchema, event, state, tag } from "./schema.js";`.
- `internal.ts`: its first comment becomes `(web-host, testing)`; the layout line becomes
  `export { scalarSize } from "./layout.js";`; the `schema_hash.js` line goes away.
- `api/public-exports.txt`: remove `action`, `component`, `defineSchema`, `event`, `state`, `tag`.

- [ ] **Step 4: Remove `@oly/codegen`**

- `git rm -r engine/tools/codegen`, then `rm -rf engine/tools` (the ignored `dist/`, `dist-tests/` and
  `tsconfig.tsbuildinfo` stay on disk otherwise).
- Root `package.json`: `workspaces` loses `"engine/tools/*"`. Root `tsconfig.json`: remove the references
  `engine/tools/codegen` and `engine/tools/codegen/tests`.
- `engine/CMakeLists.txt`: remove the `ts.codegen.unit` test.
- Run: `$WC sh -c '$(dirname $(command -v node))/npm install --package-lock-only --no-audit --no-fund'`
  Expected: exit 0; `git diff package-lock.json` only drops `@oly/codegen` and its `node_modules` link.

- [ ] **Step 5: Run the tests**

Run: `$WC sh -c 'rm -rf node_modules engine/environments/web/clients/typescript/core/dist && make games-test'`
Expected: PASS (`ts.core.unit` with the surface test against the shorter export list, `oly.lint.boundaries` over
generated files on disk).

Run: `$DC make test`
Expected: PASS.

Run: `git grep -n -E 'defineSchema|computeLayout|canonicalSchema|fnv1a32|schemaHash' -- '*.ts'`
Expected: no output (`computeLayout`, `fnv1a32` and `schemaHash` are C++ names now, so the check covers only TS).

Run: `git grep -n -E '@oly/codegen|oly-codegen|SCHEMA_JS|tools/codegen' -- engine games tools cmake Makefile package.json tsconfig.json CMakePresets.json`
Expected: no output.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: remove the TS schema API and @oly/codegen"
```

---

### Task 8: Docs

**Files:**
- Modify: `docs/architecture/{README,schema-and-codegen,decisions,repository,modules-and-boundaries,ts-packages,build-and-tests,glossary,games,limits}.md`
- Modify: `docs/development/{commands,guide-style}.md`
- Modify: `docs/guide/{README,01-getting-started,02-schema,03-world-and-entities,05-engine-and-systems,06-events,07-input,09-states,11-testing,13-troubleshooting,cheat-sheet,recipes}.md`
- Modify: `README.md`

Every rewritten page keeps its current style; the guide follows `docs/development/guide-style.md` (game-agnostic,
examples from the reader's side). A TS declaration snippet `export const X = component("X", { f: "f32" });` becomes a
`.oly` line `component X { f: f32 }` in a block whose first line is `# <game>.oly` (same for `tag`, `event`, `action`,
`state`); TS imports from `../schema.js` become `../schema.generated.js`.

- [ ] **Step 1: Architecture docs**

- `README.md` rule 3: "**There is a single source for the data layout**: the game's `.oly` file. `oly-schema`, a C++
  generator, produces the C++ and each client's code from it." The index rows of `schema-and-codegen.md` ("the `.oly`
  schema, `oly-schema` and each game's WASM module") and `ts-packages.md` (`@oly/core`, `@oly/web-host`,
  `@oly/testing`).
- `schema-and-codegen.md`, rewritten: the `.oly` file (the example of spec §4.1, a pointer to
  `engine/schema/format/README.md` for the grammar and rules); `oly-schema` (parse, validate, layout and hash once; the
  C++ emitter in the core, client emitters registered from their environment folder; command line); the build flow
  diagram:

```
<game>.oly ──► oly-schema ─┬─ cpp ──► schema.generated.hpp + schema_registry.generated.cpp
                           │            │
                           │   the game's C++ systems (OLY_SYSTEM, OLY_HANDLER) ──┤
                           │   oly::capi + app + input + ecs + core + oly::web_host ┴──► emcc ──► <game>_game.mjs + .wasm
                           └─ ts ───► web/schema.generated.ts (imports @oly/core/generated)
```

  then the bullets about `oly_add_game`, the `static_assert`s ("diverges from oly-schema"), the hash (unchanged
  canonical form, FNV-1a, checked by `loadRuntime`), where `oly-schema` is built (native presets; `schema-tool` in the
  `wasm` image) and that `*.generated.ts` is never committed.
- `decisions.md`: the row "TS schema as the single source + codegen + hash" becomes
  `| \`.oly\` as the single source + C++ generator + hash; each client gets generated code | one schema for every environment and language; layout and hash computed once | the TS schema API and the TS generator are gone; \`make ts\` builds \`oly-schema\` first |`
  (keep the column count of the table).
- `repository.md` tree: `engine/schema/format/` (the `.oly` language and its corpus), `engine/schema/codegen/`
  (`oly-schema`: parser, validation, layout, hash, emitters, C++ emitter),
  `engine/environments/web/clients/typescript/codegen/` (TS emitter), `games/<game>/<game>.oly`; `tools/codegen/` goes
  away.
- `modules-and-boundaries.md`: the graph loses `codegen["@oly/codegen"]` and gains `schema` and
  `schema_typescript --> schema`; the module table gains `schema` (no dependency, `STATIC`, native builds only) and
  `schema_typescript` (`schema`, `OBJECT`, native builds only); the lint list gains "`@oly/core/generated` imported
  outside `*.generated.ts` and `@oly/core`" and "`engine/schema/` including a module or importing a package of an
  environment".
- `ts-packages.md`: the schema bullet becomes "**Schema**: the types (`ComponentType`, `TagType`, `EventType`,
  `ActionType`, `State`, `Schema`); the objects come from the game's generated `schema.generated.ts`, built with the
  `@oly/core/generated` constructors"; the `@oly/codegen` section goes away (the title becomes `## \`@oly/testing\``).
- `build-and-tests.md`: `oly_add_game` "runs `oly-schema` and links a game's `.mjs`/`.wasm` module, with
  `oly::web_host`"; the ctest names list `schema.*`, `schema_typescript.*` and drops `ts.codegen.unit`; the presets
  table gains `schema-tool`.
- `glossary.md`: the TS column of "declare event", "declare action" and "declare state" becomes
  `` `event Name { ... }` in the `.oly` schema ``, `` `action name: kind` in the `.oly` schema ``,
  `` `state name` in the `.oly` schema ``; add a row for components and tags if the table has none, in the same form.
- `games.md`: the tree shows `<game>.oly` at the game root and `web/schema.generated.ts` (generated, not committed);
  `schema.ts` goes away.
- `limits.md`: any sentence about the TS schema or `@oly/codegen` follows the new flow
  (`git grep -n -i 'schema\|codegen' docs/architecture/limits.md`).

- [ ] **Step 2: Development docs**

- `commands.md`: add `$WC make schema-tool   # builds oly-schema for the machine (preset schema-tool)`; the caveat
  becomes "The `wasm-games*` presets require `make ts` (or `make schema-tool`) before configuring: `oly_add_game` runs
  `build/schema-tool/bin/oly-schema`."; `make ts` "builds `oly-schema`, generates every TS schema and compiles the TS".
- `guide-style.md`, the surface row: `| the schema language and the generated code | \`engine/schema/format/\`, \`engine/schema/codegen/\`, \`engine/environments/web/clients/typescript/codegen/\`, \`@oly/core/generated\` |`;
  the "how a game is built" row adds the `schema-tool` target.

- [ ] **Step 3: The user guide and the root README**

- `02-schema.md`, rewritten on the same sections: the schema is a `<game>.oly` file, the single source of the data for
  C++ and TS; "The five kinds of entries" table with the `.oly` syntax (`component Health { value: i32 }`, `tag Enemy`,
  `event EnemyDied { entity: entity, x: f32, y: f32 }`, `action shoot: button`, `state playing`) and the example
  schema as one `.oly` block; how TS gets the entries (`import { Health, shoot } from "../schema.generated.js";`, the
  default export is the schema for `loadRuntime`) and C++ (`#include "schema.generated.hpp"`); field types unchanged;
  naming rules unchanged, but a broken rule stops the build with `<game>.oly:<line>:<column>: <message>`, plus "a field
  name appears once per component or event" and "an action or state cannot be a TypeScript reserved word such as
  `function`"; order matters (the order inside each group); memory layout with the `.oly` line and the C++ block (message
  `diverges from oly-schema`); "What the build generates" shows both files (`schema.generated.hpp` as today,
  `schema.generated.ts` with one `componentType(...)` line and the `schemaOf` line) and says they are generated on
  every build, not committed, and never edited; the start-up check unchanged; "Using the schema in several places"
  says to import the generated module everywhere.
- `01-getting-started.md`: the build diagram's first line becomes
  `games/<game>/<game>.oly ──oly-schema──► schema.generated.hpp (C++) + web/schema.generated.ts (TS)`; the folder
  layout has `hello.oly` at the game root and no `web/schema.ts` (`web/schema.generated.ts` noted as generated); the
  `tsconfig.json` `include` is `["schema.generated.ts", "src"]`; the CMake block uses
  `SCHEMA ${CMAKE_CURRENT_SOURCE_DIR}/hello.oly`; section 5 becomes `` ### 5. `hello.oly` `` with the four entries in
  `.oly`; "`schema.generated.hpp` is created by the build from `hello.oly`"; imports from `../schema.generated.js`; the
  "TS only" row says "`hello.oly` and TS systems".
- `README.md` (guide): the example's schema block becomes a `# hello.oly` block; imports from `../schema.generated.js`.
- `03`, `05`, `06`, `07`, `09`, `recipes.md`: each declaration snippet becomes `.oly` (rule above).
- `11-testing.md`: imports from `../schema.generated.js`; the tsconfig sentence names `../schema.generated.js`.
- `13-troubleshooting.md`: the "does not belong to this runtime's schema" row's cause becomes "an entry from another
  generated schema module" and its fix "import the entries from the game's `schema.generated.js`"; add a row
  "`<game>.oly:<line>:<column>: ...` when building | a schema rule is broken | fix that line; the rules are in
  [Schema](02-schema.md#naming-rules)"; step 1 of the rebuild list becomes "`$WC make ts` — builds `oly-schema`,
  generates the TS schemas and compiles the TS".
- `cheat-sheet.md`: the schema rows become the `.oly` forms (`component Name { field: f32 }`, `tag Name`,
  `event Name { field: u32 }`, `action name: button | axis2d | pointer`, `state name`), and the `defineSchema` row
  becomes "`import schema from \"../schema.generated.js\"` | pass it to `loadRuntime`".
- Root `README.md`: the package row of `@oly/codegen` becomes `| oly-schema | engine/schema/codegen | Generates the C++
  and the TS of a game's .oly schema. |` (same columns as the table); the schema section says "The game declares the
  schema in a `.oly` file. `oly-schema` generates the C++ and the TS from it." with a `.oly` block (Position, Velocity,
  Enemy) and the TS line `import schema, { Position, Velocity, Enemy } from "./schema.generated.js";` before
  `loadRuntime`.

- [ ] **Step 4: Verify**

Run: `git grep -n -E 'defineSchema|schema\.ts|schema\.js|@oly/codegen|oly-codegen|SCHEMA_JS|component\("|\btag\("|\bevent\("|\baction\("|\bstate\("' -- README.md docs ':!docs/superpowers' ':!docs/benchmarks'`
Expected: no output.

Run: `git grep -n -E '\]\(([^)#]+\.md)' -- docs/guide docs/architecture | grep -v http` and check that every linked file
exists (`02-schema.md#naming-rules` included).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "docs: describe the .oly schema and oly-schema"
```

---

### Task 9: Clean-checkout verification

- [ ] **Step 1: Rebuild from scratch**

Run: `git status --short` (empty), then
`rm -rf build/debug-asan build/wasm-debug build/wasm-games build/wasm-games-release build/schema-tool build/parity build/smoke*`
and `find . -name '*.generated.ts' -not -path './node_modules/*' -delete`.

Run: `$DC make test`
Expected: PASS (`schema.*`, `schema_typescript.*`, `oly.lint.*` included).

Run: `$WC make test PRESET=wasm-debug`
Expected: PASS (no schema module in Emscripten builds).

Run: `$WC make games-test`
Expected: PASS (builds `oly-schema` with the `schema-tool` preset first).

Run: `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'`
Expected: PASS.

Run: `$WC make game G=arena`
Expected: PASS; `build/wasm-games-release/web/arena/` holds `index.html`, `main.js`, `arena_game.mjs`, `arena_game.wasm`.

- [ ] **Step 2: A broken schema stops the build**

Run: `sed -i 's/^tag Coin$/tag coin/' games/arena/arena.oly && $WC make ts; git checkout games/arena/arena.oly`
Expected: `make ts` fails with `games/arena/arena.oly:<line>:5: invalid type name "coin": use PascalCase ([A-Z][A-Za-z0-9]*)`,
and `git status --short` is empty afterwards.

- [ ] **Step 3: Leftovers**

Run: `git grep -n -E 'defineSchema|@oly/codegen|oly-codegen|SCHEMA_JS|schema\.ts\b' -- . ':!docs/superpowers' ':!docs/benchmarks' ':!package-lock.json'`
Expected: no output.

Run: `git grep -n -E 'computeLayout|canonicalSchema|fnv1a32' -- '*.ts'`
Expected: no output.

No commit unless a step needed a fix. Manual check for the user (not automated):
`docker compose run --rm --service-ports web make serve G=arena`, open http://localhost:8000 and play one round.
