---
name: add-test-case
description: Adds tests to the ECS the way the build and the contract expect, whether a new doctest suite in engine/runtime/<module>/tests/, a TEST_CASE in an existing suite or a death case in contract_death (a contract violation that must trigger OLY_ASSERT). Use whenever you write, create or register a test in this repository, cover a requirement R-* or an invariant I-* with a test, test a contract violation or a new assert, or when a file in a module's tests/ seems not to be running, even if the user does not say "suite" or "contract_death".
argument-hint: "[suite <name> | case <name> | death <name>]"
---

# add-test-case

A new test in this repository has steps that do not show up in the code it tests. A suite that was not registered in the module's CMake compiles zero times and always passes. A death case left out of the `foreach` list never runs. And a substring that is too loose makes the case pass because of another assert. None of these errors breaks the build. This skill exists so that the new test really runs and fails for the right reason.

## Boundary

**Territory**: chooses where the test lives (new suite, `TEST_CASE` in an existing suite or death case), defines the wiring each form needs to be compiled and run, and sets the checks every test carries (invariants after each operation, life counting for non-trivial components, substrings that identify the right assert).

**Outside the boundary**: which behavior deserves a test and what the expected result is (comes from the spec and the user's request); fixing the implementation when the test fails; the `OLY_ASSERT` message itself, which is written together with the code it protects; benchmarks; creating the commit, which is the user's act.

**What I do not need to know**: how `World`, `Table` and `CommandBuffer` work inside. The test exercises the public API and checks the result with `checkInvariants(world)` (free function in `diagnostics.hpp`), which already knows the invariants. If a test needs to read internal structure to decide whether it passed, an invariant is probably missing, and that is another conversation.

**Edge contract**: I receive the behavior to test (or the contract violation to provoke) and, optionally, the form; I deliver the changed or created test file, the wiring in the module's `tests/CMakeLists.txt` and the run output that shows the new test running and passing. I do not commit.

## 1. Choose the form

| What you want to prove | Form |
|---|---|
| Correct behavior of a module that already has a suite | `TEST_CASE` in the module's existing suite |
| New module or feature, with no suite representing it | New suite (new file in `tests/`) |
| A use forbidden by the contract aborts with `OLY_ASSERT` | Death case in the module's `contract_death.cpp` (`ecs` or `bridge`) |
| An exception from user code crosses the ECS without corrupting state | `CHECK_THROWS` in a doctest suite |

Why a contract violation cannot be a `TEST_CASE`: the default `OLY_ASSERT` handler calls `std::abort()`, which takes down the whole suite executable. Besides, `OLY_ASSERT` disappears with `OLY_ENABLE_ASSERTS=0`, and the doctest suites also run in the `release` preset. So a `TEST_CASE` can never depend on an assert firing. Death cases only exist with asserts enabled (`OLY_ENABLE_ASSERTS`). `contract_death` is only compiled when asserts are enabled, and that is why these cases live there.

When in doubt between a new and an existing suite, prefer the existing one: each suite is one more executable to compile and link.

## 2a. New suite

Two steps, and the second is the one that gets forgotten:

1. Create `engine/runtime/<module>/tests/<name>.cpp`.
2. Register it in `engine/runtime/<module>/tests/CMakeLists.txt`, next to the other `oly_add_test`:

   ```cmake
   oly_add_test(MODULE ecs NAME <name> SOURCES <name>.cpp)
   ```

   The test shows up in ctest as `<module>.<name>` and the executable is `build/<preset>/tests/<module>_<name>`. Support sources go into `SOURCES` (e.g. `support/alloc_counter.cpp`, if the suite uses `alloc_counter.hpp`). Tests do **not** come in by glob: a `.cpp` in `tests/` without that line simply does not exist for the build. The `src/` sources, on the other hand, come in by glob, which is often confusing.

What `oly_add_test` already does and you must not repeat: links `doctest_main.cpp` (from `engine/runtime/core/tests/support/`) (so the file does **not** define `main` or `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`), puts the module's `tests/support` on the include path (that is why it is `"components.hpp"`, without a path), applies the warnings with `-Werror` and registers the executable in CTest under the same name.

Template:

```cpp
#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "components.hpp"
#include "oly/ecs/world.hpp"

TEST_CASE("removing a component preserves the values of the other entities") {
  oly::ecs::World world;
  std::vector<oly::ecs::Entity> entities;
  for (uint64_t i = 0; i < 16; ++i) {
    entities.push_back(world.spawn(A{i}, B{i, 2 * i}));
    CHECK_INVARIANTS(world);
  }

  world.remove<B>(entities[3]);
  CHECK_INVARIANTS(world);

  for (uint64_t i = 0; i < 16; ++i) {
    CHECK(world.get<A>(entities[i]).value == i);
  }
}

TEST_CASE("I-2: removing LifeCounter destroys exactly one value") {
  LifeCounter::alive = 0;
  {
    oly::ecs::World world;
    auto entity = world.spawn(A{1}, LifeCounter{7});
    CHECK(LifeCounter::alive == 1);

    world.remove<LifeCounter>(entity);
    CHECK_INVARIANTS(world);
    CHECK(LifeCounter::alive == 0);
  }
  CHECK(LifeCounter::alive == 0);
}
```

## 2b. Case in an existing suite

Only the `TEST_CASE`; the wiring already exists. Check that the file appears in an `oly_add_test` before trusting that it runs: an orphan file in `tests/` is exactly the defect this skill prevents.

## 2c. Death case

`<module>_contract_death` (`ecs_contract_death`, `bridge_contract_death`) is an executable without doctest. Each case is a command-line argument. The installed handler checks whether the assert message contains all the required substrings: it exits with 0 if it does and with 2 if any is missing. If the case ends without an assert, `main` returns 1. A new case touches **three** places, and all three must use the same name:

1. **The function**, inside the anonymous `namespace` of `engine/runtime/<module>/tests/contract_death.cpp`, in `camelCase`, with no return value and no parameters:

   ```cpp
   void removeDuringEach() {
     oly::ecs::World world;
     auto entity = world.spawn(A{1}, B{2, 3});
     world.query<A>().each([&](A&) { world.remove<B>(entity); });
   }
   ```

2. **The entry in the `cases` table** of `main`, with the name in `snake_case` and the required substrings:

   ```cpp
   {"remove_during_each", removeDuringEach, {"remove", "e="}},
   ```

3. **The name in `CASES`** in the `oly_add_contract_death(MODULE <module> SOURCE contract_death.cpp CASES ...)` call of `tests/CMakeLists.txt`. This list is what creates the CTest test `<module>.contract_death.<name>`. Without it the case compiles, exists in the table and **never runs**, and nothing warns you.

If the name is in the list but missing from the table, the executable exits with 64 ("unknown case") and the test fails. That error shows up on its own; the opposite does not.

### Choosing the substrings

The handler receives the full message, built by `assertFail` as:

```
OLY_ASSERT failed: <expression> | <formatted message> (<file>:<line>)
```

The search is a `strstr` over the whole line, so a substring can match the **expression** or the **file path**, and not only the message. Choose substrings that only the intended assert's message would have:

- **The operation** (`"add"`, `"flush"`, `"remove"`), which is what separates this assert from its neighbors.
- **`"e="`** when the assert is about an entity: it confirms the message identifies the entity, as `docs/development/code-style.md` requires.
- **The type name**, when the assert prints it. Use a local type with its own name (`struct Position { float x; };` inside the function, as in `getDeadEntity`), not `A` or `B`: a lone letter matches almost any message and proves nothing.

Before trusting the case, read the target `OLY_ASSERT` in `src/` and copy the substrings from its format.

### One violation at a time

Several asserts join two conditions into one, like `isAlive(entity) && iterating_ == 0` in `destroy`, `add` and `remove`, and print the same message for either of them. A "during iteration" case that uses an already destroyed entity passes, but for the wrong reason. Build the scenario so that only the intended violation is present: entity alive when the case is about iteration, no iteration when the case is about a dead entity.

The handler exits on the first assert. Nothing after the violation runs, so do not put checks after it.

### New assert together with the case

If the case covers an `OLY_ASSERT` that is still to be written, its message follows the rule in `docs/development/code-style.md` (operation, `Entity` as `e=%u:%u`, type name). The case's substrings enforce exactly that rule, and that is why they are worth it.

## 3. The checks every test carries

**`CHECK_INVARIANTS(world)` after each structural operation** (spawn, destroy, add, remove, flush). Once at the end is not enough: one operation that corrupts the state and another that "fixes" it by chance pass a final check. The macro is in `components.hpp`, calls the free function `oly::ecs::checkInvariants` and shows the failure reason. It does not exist in `contract_death`, which has no doctest and where the process dies before any check.

**Values derived from the entity or the index** (`A{i}`, `B{i, 2 * i}`), not constants. When every row is `A{1}`, a botched row swap in a swap-remove still gives the right value. With distinct values, it shows up as a wrong value.

**`LifeCounter` whenever the test involves a non-trivial component** (move, remove, destroy, flush with a component that has a destructor). It counts constructions minus destructions and catches an undestroyed moved-from and a double destroy (I-2). Three precautions:

- `LifeCounter::alive` is `static inline`, shared by all the `TEST_CASE`s of the executable. Reset it at the start of the case.
- Put the `World` in a `{ }` block and check `alive == 0` **after** the block: that is what proves the `World` destructor destroyed everything that was left.
- The trivial types (`A`, `B`, `Wide`) go through the `memcpy` path and do not exercise move/destroy. If the code under test has both paths, cover both.

**Randomness**: if the test draws operations at random, use the seed and the operation count from `engine/runtime/ecs/tests/support/model.hpp` (`fuzzSeed()`, `fuzzOperationCount()`, variables `OLY_FUZZ_SEED` and `OLY_FUZZ_OPS`) and print the seed to stderr, as the fuzz suites already do. A random test without a reproducible seed fails once and never again.

## 4. Prove that it runs

The build and test commands, and how to filter a test by name, are in `docs/development/commands.md`. Everything runs in the container, in the default preset (ASan + UBSan + asserts).

- **Doctest suite or case:** run only the affected suite (`make test T='^ecs\.<name>'`) and confirm in the output that the number of cases went up. "0 test cases" or a filter with no test found means the wiring failed, not that it passed.
- **Death case:** run the executable directly, `build/debug-asan/tests/<module>_contract_death <name>`, and read the message printed to stderr. It must be the intended assert's message, not just "some assert". Then run the `<module>.contract_death.<name>` filter through CTest to confirm the `foreach` created the test.
- **Proof that the test detects the defect**, when it is cheap: swap a substring for one that does not exist (the case must exit with 2) or comment out the line that violates the contract (it must exit with 1), run and undo. A test that was never seen failing was never seen testing.

If an existing test breaks, stop and report: deciding whether the defect is in the test or in the code is outside this skill.

## Final report

List the touched files and, for each new test, one line: name, form (new suite, case in an existing suite, death case), what it proves and the evidence that it ran (case count in the doctest output, or the assert message printed by `contract_death`). Propose the commit message (`test: ...`, in English) without committing.
