# Engine — Phase 5 (Schedule) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tasks scheduled on the game clock (`after`/`every`/cancel), fired at step 2 of the fixed step by the C++ core, with copied payload and discard, exposed in the C API for TS tasks and used by `@oly/core` in `engine.schedule` and `context.schedule`.

**Architecture:** A queue `detail::TaskQueue` (min-heap by `(due time, sequence)` over slots with generation, lazy cancellation) lives in `app` and does not know the `Engine`. The public class `Scheduler` computes the due time from `Engine::time()` and delegates to the queue; the `Engine` holds it, runs the due tasks in `runDueSchedule()` (step 2, only in a non-paused step) with a `TaskContext`, and tasks survive exception recovery. The direct structural changes move out of `HandlerContext` into `DirectContext`, the base of `HandlerContext` and `TaskContext`. The `bridge` registers a TS task as an ordinary C++ task whose payload holds `OlyEngine*` and the `scriptId`; firing and discarding call `olyHostInvokeScript` with a new `kind`. In TS, `Scheduler` holds `scriptId → closure` and deletes the entry on the last run or on discard.

**Tech Stack:** C++20, GCC 14 (native), Emscripten 3.1.74 + node 20.18 (`wasm` service), CMake ≥ 3.25, doctest 2.4.11, TypeScript 5.6.3 `strict`, `node:test`.

**Spec:** `docs/superpowers/specs/2026-11-09-game-engine-design.md` (phase 5 of §5; all of §4.6, §4.3 for the step order, pause and recovery, §4.4 for events emitted in a task, §6, §7 "Schedule"). `§N` below refer to that spec.

## Global Constraints

- Branch `feat/engine-phase5-schedule` from `develop`. Commits, comments, assert and error messages in English; commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Toolchain only in Docker: `DC="docker compose run --rm dev"` (native, `debug-asan`), `WC="docker compose run --rm wasm"` (Emscripten, node, npm). `wasm-games*` presets require `make ts` first.
- `-Wall -Wextra -Wpedantic -Werror` on every C++ target; TS `strict` (§3).
- Layers: `core ← input ← app ← bridge ← games` and `core ← ecs ← app`. No new module: the queue goes in `engine/runtime/app/include/oly/app/detail/` (§4.6). The ECS and `input` do not change.
- Full engine in C++ (§2): due time, order, drift-free `every`, pause, cancellation, discard and recovery live in `app`. TS only validates usage, holds closures and forwards.
- Boundary: one call per firing and one per discard of a TS task; scheduling, cancelling and querying cross once per call. Nothing per entity.
- Clock: the `Engine`'s `time()` (sum of non-paused fixed steps). Inside a step, `time()` is the start of the step; outside `frame` (and in the `render` phase), the start of the next step.
- Payload: trivially copyable, `sizeof ≤ 32`, `alignof ≤ 8` (`static_assert`) (§4.6).
- No game concepts in `engine/runtime` or in `engine/packages`; game names only in `engine/testing/fixture/` and in the tests.
- No benchmark in this phase (§7).
- Glossary (`CLAUDE.md`): same verb in all three layers — `after`, `every`, `cancel`, `isPending` (C++ `Scheduler`; C `olyEngineScheduleScriptTask`, `olyEngineCancelTask`, `olyEngineIsTaskPending`; TS `schedule.after/every/cancel/isPending`). `oly` only in the C API.
- `CLAUDE.md` style: braces on every block, full names, data at the top of the class, one `namespace` per file, project includes with quotes after the standard library. Do not format by hand (`clang-format` hook). Comments only for a reason, an external constraint or a non-obvious contract; no narration of the edit.

### Decisions the spec leaves open (settled here)

- **Files.** `oly/app/task.hpp`: basic types without context (`TaskHandle`, `InvalidTaskSlot`, `TaskFunction`, `TaskDiscardFunction`, `PayloadKey`, `payloadKey<P>()`, `TaskPayload`, limits). `oly/app/detail/task_queue.hpp`: the queue. `oly/app/scheduler.hpp`: `Scheduler`. `oly/app/task_context.hpp`: `TaskContext`. `DirectContext` goes in `context.hpp`, next to `EngineContext`. The `task.hpp`/`task_context.hpp` split mirrors `event.hpp`/`handler.hpp` and avoids the cycle `context.hpp → scheduler.hpp → TaskContext → context.hpp`.
- **Payload type.** `PayloadKey` is dense per process, like `EventKey` (`detail::nextPayloadKey()` in `src/task.cpp`); `NoPayloadKey = 0xFFFFFFFFu` for a task without payload. `TaskPayload` also stores `typeName<P>()` for the error message.
  ```cpp
  struct TaskPayload {
    alignas(MaxTaskPayloadAlignment) std::byte bytes[MaxTaskPayloadSize];
    PayloadKey key = NoPayloadKey;
    std::string_view typeName;

    template <class P>
    static TaskPayload of(const P& value);   // static_asserts from §4.6; memcpy into bytes
  };
  ```
- **Type-erased base.** `TaskHandle Scheduler::schedule(double delaySeconds, double intervalSeconds, TaskFunction function, const TaskPayload& payload = {}, TaskDiscardFunction discard = nullptr)`: `intervalSeconds == 0` is `after`; `> 0` is `every` with the first firing at `time() + delaySeconds`. The `after`/`every` templates are thin and delegate (`every(interval, ...)` passes `delaySeconds = interval`). It is what the C API calls; TS always passes `delay = interval` for `every`.
- **Default handle.** `cancel(TaskHandle{})` and `isPending(TaskHandle{})` return `false` without assert: the default handle means "no task", so the game can write `schedule.cancel(dashTimer)` without testing first. Only a `slot` out of range **and** different from `InvalidTaskSlot` is a contract violation.
- **Slots and generation.** Each slot holds function, discard, `TaskPayload`, due time, interval, sequence, generation and `live`. The handle captures the slot's generation; freeing the slot (cancellation, last firing of `after`, discard) increments the generation and returns the slot to the free list. The heap entry is `{due, sequence, slot, generation}`; an entry whose generation differs from the slot's, or a dead slot, is dropped when it reaches the top (lazy cancellation).
- **Tasks created during step 2.** `beginRun()` turns on a mode in which `push` reserves the slot immediately (valid handle, `isPending` true) but keeps the heap entry in `deferred_`; `endRun()` and `recoverFromInterruptedRun()` move them into the heap. A sequence boundary in the heap does not work: with the tolerance, a new task due `now` would sit on top ahead of an old one due in `(now, now + tolerance]` and block it.
- **`popDue(limit)`** (the `every` goes back into the heap and the `after` leaves before the callback runs, §4.6):
  ```
  repeat:
    heap empty → nothing
    top dead or from an old generation → drop the entry, continue
    top.due > limit → nothing
    take the top; copy function, handle and payload into DueTask
    after: lastRun = true; free the slot (without calling the discard)
    every: next = due + interval
           if next ≤ limit: next = due + (floor((limit − due) / interval) + 1) · interval
           if still next ≤ limit (rounding): next = limit + interval
           back into the heap with the same sequence; lastRun = false
    return DueTask
  ```
  `limit = time() + fixedDeltaTime · StepTolerance`, the same tolerance as the accumulator (`StepTolerance` moves out of the anonymous namespace in `engine.cpp` into a private `constexpr` of the `Engine`).
- **Discard.** `cancel` copies the payload into a local variable, frees the slot and only then calls `discard(local.bytes)`: the discard can schedule or cancel other tasks without catching the queue halfway. `discardAll()` repeats "cancel every live slot" until `pendingCount() == 0` (covers a discard that schedules) and clears the heap and `deferred_`; it runs in the `TaskQueue` destructor, so when the `Engine` is destroyed.
- **`StepTolerance` and `time()` in the `Scheduler`.** The `Scheduler` holds `Engine&` (constructed with `*this` in the `Engine`) and reads `engine.time()` in `schedule.cpp`; the header only declares `class Engine;`. The `Engine` is a `friend` of the `Scheduler` to reach the `TaskQueue` in `runDueSchedule()`.
- **Contracts (`OLY_ASSERT`).** `schedule`: `"schedule: delay=%f negative or not finite"`, `"schedule: interval=%f negative or not finite"`, `"schedule: null function"`. `every`: `"every: interval=%f must be positive and finite"` (in the template, before delegating). `cancel`/`isPending`: `"%s: slot=%u out of range (%u slots)"`. `TaskContext::payload<P>()`: `"payload: type \"%s\" differs from the scheduled \"%s\""` (scheduled without payload shows as `"(none)"`).
- **C API.** `OLY_SCRIPT_TASK 2u` and `OLY_SCRIPT_TASK_DISCARD 3u` use the same `olyHostInvokeScript`/`ScriptInvoker`: `events` null and `eventCount` = `lastRun` (0 or 1) on firing, 0 on discard. TS task payload: `struct ScriptTaskPayload { OlyEngine* engine; uint32_t scriptId; };`. `olyEngineScheduleScriptTask` forwards to `Scheduler::schedule` without its own validation; the `Scheduler` asserts apply.
- **Order when destroying the `OlyEngine` (TS).** `olyEngineDestroy` discards the pending tasks and calls JS with `OLY_SCRIPT_TASK_DISCARD` before returning. That is why `Engine.dispose()` in TS only removes the engine from the `Runtime` map (`onDispose`) **after** `engineDestroy`, as today, and handling the discard does not go through `#assertUsable`.
- **`TaskHandle` in TS.** Frozen object, with no visible fields, typed as `interface TaskHandle { readonly [taskHandleBrand]: true }` (`declare const taskHandleBrand: unique symbol`). The `Scheduler` keeps `WeakMap<TaskHandle, { slot, generation }>`: comparing is `===`, and a handle not in the map is "from another `Engine`".
- **Contexts in TS.** `DirectContext` (new `direct_context.ts`, not exported from `index.ts`, like `EngineContext`) takes the direct `spawn`/`destroy`/`add`/`remove` from `HandlerContext`. `EngineContext` gains `schedule` (getter). `TaskContext extends DirectContext` with `handle` and `lastRun` (getters), `ContextKind { label: "task", className: "TaskContext", callNoun: "task" }`. One `TaskContext` per task, created when scheduling; `lastRun` is updated on each firing.
- **C++/TS interleaving in the WASM test.** C++ tasks are only scheduled by C++ (§4.6). The fixture gains `fixtureScheduleDigit(OlyEngine*, double delaySeconds, uint32_t index, uint32_t generation, uint32_t digit)`, which schedules `after(delay, appendDigitTask, AppendDigit{entity, digit})`; the task does `trace.value = trace.value * 10 + digit`, the same ordering trick as `appendNineOnPing`. For the test to reach the `OlyEngine*`, `@oly/core/internal` gains `engineHandleOf(engine: Engine): number`.
- **Out of scope:** state scope (phase 6), native task by name, `pendingCount` in TS, benchmark.

## Review Focus

1. **Old handle after the slot is reused** (the dash `every` cancels itself, a new task takes the same slot, and the game cancels again through the old handle): `cancel` returns `false` and the new task stays pending; `TaskHandle{}` also returns `false` without assert (Task 1, Task 2).
2. **`every` with an interval much smaller than the step** (`every(1e-9)`) or after many paused steps: fires once per step, with no long loop and no repeated firing in the same step (Task 1).
3. **Reentrant discard** (the discard of one task schedules or cancels another, including during the destruction of the `Engine`): each task is discarded once and the queue ends empty (Task 1).
4. **The same TS closure scheduled twice**: cancelling one handle does not affect the other; the map is by `scriptId`, not by function (Task 4).
5. **A TS `every` that throws**: the exception reaches whoever called `frame`, the next `frame` recovers, the task stays pending and runs again, and the closure is not deleted (Task 4).

---

### Task 1: `detail::TaskQueue` — heap with slots, generation and discard

**Files:**
- Create: `engine/runtime/app/include/oly/app/task.hpp`, `engine/runtime/app/include/oly/app/detail/task_queue.hpp`, `engine/runtime/app/src/task.cpp`, `engine/runtime/app/src/task_queue.cpp`, `engine/runtime/app/tests/task_queue.cpp`
- Modify: `engine/runtime/app/tests/CMakeLists.txt`

**Interfaces:**
- Produces (`oly::app`, `task.hpp`):
  - `class TaskContext;` (declaration); `using TaskFunction = void (*)(TaskContext& context);` `using TaskDiscardFunction = void (*)(void* payload);`
  - `inline constexpr uint32_t InvalidTaskSlot = 0xFFFFFFFFu;` `struct TaskHandle { uint32_t slot = InvalidTaskSlot; uint32_t generation = 0; bool operator==(const TaskHandle&) const = default; };`
  - `using PayloadKey = uint32_t;` `inline constexpr PayloadKey NoPayloadKey = 0xFFFFFFFFu;` `namespace detail { PayloadKey nextPayloadKey() noexcept; }` `template <class P> PayloadKey payloadKey() noexcept;` (local static, like `eventKey`).
  - `inline constexpr std::size_t MaxTaskPayloadSize = 32;` `inline constexpr std::size_t MaxTaskPayloadAlignment = 8;` `struct TaskPayload` as in "Decisions", with `static_assert` of trivially copyable, size and alignment in `of<P>` (messages in English with "payload").
- Produces (`oly::app::detail`, `task_queue.hpp`):
  - `struct DueTask { TaskFunction function; TaskHandle handle; bool lastRun; TaskPayload payload; };`
  - `class TaskQueue` with `~TaskQueue()` (calls `discardAll`), non-copyable; `TaskHandle push(double due, double interval, TaskFunction function, TaskDiscardFunction discard, const TaskPayload& payload)`, `bool cancel(TaskHandle task)`, `bool isPending(TaskHandle task) const`, `uint32_t pendingCount() const noexcept`, `void beginRun() noexcept`, `std::optional<DueTask> popDue(double limit)`, `void endRun()`, `void recoverFromInterruptedRun()`, `void discardAll()`.
  - Contract: `cancel`/`isPending` with `slot` out of range and different from `InvalidTaskSlot` (`OLY_ASSERT`, message from "Decisions"). The queue validates neither delay nor function; that belongs to the `Scheduler` (Task 2).

- [ ] **Step 1: Branch** `git switch -c feat/engine-phase5-schedule`.

- [ ] **Step 2: Failing tests** in `tests/task_queue.cpp` (`oly_add_test(MODULE app NAME task_queue SOURCES task_queue.cpp)`). No `Engine` or `World`: the callbacks are not called, only the order and state of the `DueTask`s are checked. A test discard increments a global counter and records the payload it receives.
```cpp
TEST_CASE("popDue returns in due order and, on a tie, in creation order") { /* push 2.0 "a", 1.0 "b", 1.0 "c"; popDue(5) ×3 */ CHECK(order == {b, c, a}); }
TEST_CASE("popDue respects the limit") { /* push 1.0; popDue(0.5) */ CHECK(!result); CHECK(popDue(1.0)); }
TEST_CASE("after leaves the queue before being returned") { CHECK(task->lastRun); CHECK(!queue.isPending(task->handle)); CHECK(queue.pendingCount() == 0); CHECK(discards == 0); }
TEST_CASE("every goes back into the heap without drift") { /* push(due 0.1, interval 0.1); popDue(0.1) → next 0.2; popDue(0.25) → next 0.3 */ CHECK(!task->lastRun); CHECK(queue.isPending(task->handle)); }
TEST_CASE("a late every jumps to the first multiple after the limit") { /* due 0.1, interval 0.1; popDue(0.55) returns 1; popDue(0.55) */ CHECK(!second); CHECK(popDue(0.6)); }
TEST_CASE("every with a tiny interval fires once per popDue call") { /* due 1e-9, interval 1e-9; popDue(1.0/60) */ CHECK(popDue(1.0/60).has_value()); CHECK(!popDue(1.0/60).has_value()); }
TEST_CASE("a cancelled task does not come out of popDue and cancelling again returns false") { CHECK(queue.cancel(handle)); CHECK(!queue.cancel(handle)); CHECK(!popDue(10)); CHECK(discards == 1); }
TEST_CASE("an old handle does not cancel the task that reused the slot") { /* push A; cancel A; push B (same slot) */ CHECK(b.slot == a.slot); CHECK(!queue.cancel(a)); CHECK(queue.isPending(b)); }
TEST_CASE("default handle: cancel and isPending return false") { CHECK(!queue.cancel(TaskHandle{})); CHECK(!queue.isPending(TaskHandle{})); }
TEST_CASE("push during the run only enters the heap at endRun") { /* beginRun; push due 0; */ CHECK(queue.isPending(newTask)); CHECK(!popDue(1)); /* endRun */ CHECK(popDue(1)); }
TEST_CASE("recoverFromInterruptedRun keeps the tasks created before the exception") { /* beginRun; push; no endRun; recoverFromInterruptedRun */ CHECK(popDue(1)); }
TEST_CASE("the discard receives the copy of the payload and runs once") { /* payload {7}; cancel */ CHECK(receivedPayload == 7); CHECK(discards == 1); }
TEST_CASE("discardAll discards each pending task once, including the one scheduled by a discard") { /* 2 tasks; the 1st one's discard pushes a 3rd with a discard */ CHECK(discards == 3); CHECK(queue.pendingCount() == 0); }
TEST_CASE("the destructor discards whatever is pending") { /* scope with 2 tasks */ CHECK(discards == 2); }
TEST_CASE("payloadKey tells types apart and TaskPayload::of copies the bytes") { CHECK(payloadKey<A>() != payloadKey<B>()); CHECK(payload.key == payloadKey<A>()); }
```

- [ ] **Step 3: See it fail** — `$DC make test T='app.task_queue'` → compilation error (`oly/app/detail/task_queue.hpp` does not exist).

- [ ] **Step 4: Implement** according to "Decisions" (`std::vector<HeapEntry>` with `std::push_heap`/`std::pop_heap` and a "greater" comparison on `(due, sequence)`; slots in `std::vector`; free list in `std::vector<uint32_t>`; increasing `uint64_t` sequence).

- [ ] **Step 5: See it pass** — `$DC make test T='^app\.'` and `$WC make test PRESET=wasm-debug T='app.task_queue'`.

- [ ] **Step 6: Commit** `feat: app task queue with heap, generation and discard`.

---

### Task 2: `Scheduler`, `TaskContext` and `DirectContext` in the `Engine`

**Files:**
- Create: `engine/runtime/app/include/oly/app/{scheduler.hpp,task_context.hpp}`, `engine/runtime/app/src/scheduler.cpp`, `engine/runtime/app/tests/schedule.cpp`
- Modify: `engine/runtime/app/include/oly/app/{engine.hpp,context.hpp,handler.hpp}`, `engine/runtime/app/src/{engine.cpp,context.cpp}`, `engine/runtime/app/tests/{CMakeLists.txt,contract_death.cpp,recovery.cpp}`

**Interfaces:**
- Consumes: Task 1 (`TaskQueue`, `TaskHandle`, `TaskPayload`, `payloadKey`).
- Produces:
  - `class Scheduler` with the API from §4.6 (`after`/`every` with and without payload, `cancel`, `isPending`, `pendingCount`) plus `TaskHandle schedule(double delaySeconds, double intervalSeconds, TaskFunction function, const TaskPayload& payload = {}, TaskDiscardFunction discard = nullptr)`. Constructor `explicit Scheduler(Engine& engine)`, non-copyable.
  - `class DirectContext : public EngineContext` (in `context.hpp`) with `spawn()`, `spawn(Ts&&...)`, `destroy`, `add<T>`, `remove<T>`, `addById`, `removeById`, all direct on the `World` (moved from `HandlerContext`, which now inherits from `DirectContext` without changing behaviour).
  - `EngineContext::schedule() const noexcept -> Scheduler&`; `context.hpp` includes `scheduler.hpp`.
  - `class TaskContext : public DirectContext` with `TaskHandle handle() const noexcept`, `bool lastRun() const noexcept`, `template <class P> const P& payload() const` (contract from "Decisions"). Private constructor, `friend class Engine`.
  - `Engine::schedule() noexcept -> Scheduler&` (and `const`). `runDueSchedule()` is no longer empty; `recoverFromInterruptedFrame` calls `recoverFromInterruptedRun()` and does **not** clear the tasks.

- [ ] **Step 1: Failing tests** in `tests/schedule.cpp` (`oly_add_test(MODULE app NAME schedule SOURCES schedule.cpp SUPPORT engine/runtime/ecs/tests/support)`). Fixture like `pause.cpp` (`frame(0)` only records the time; `step()` advances 1/60). Tasks record `context.time()` and a label from the payload into a `Log` pointed to by the payload (`struct Mark { Log* log; char label; }`). `CHECK_INVARIANTS(world)` at the end of each case. Times compared with `doctest::Approx` (`time()` is `steps · (1/60)`, which can differ from `k/60.0` in the last bit).
```cpp
TEST_CASE("after scheduled outside frame fires on the step whose start reaches the due time") { /* after(2/60) at t=0; 3 steps */ CHECK(log.times.size() == 1); CHECK(log.times[0] == Approx(2.0/60)); }
TEST_CASE("after(0) outside frame runs at step 2 of the next step") { /* after(0); 1 step */ CHECK(log.calls == {"task", "system"}); }
TEST_CASE("after(0) inside a task only runs on the following step") { CHECK(log.times == {0.0, 1.0/60}); }
TEST_CASE("after(0) scheduled by a system runs on the step after its own") { }
TEST_CASE("after(0) scheduled in the render phase runs on the next step") { }
TEST_CASE("same step: due order and, on a tie, creation order") { /* after(1.5/60) a, after(1.2/60) b, after(1.2/60) c; 3 steps */ CHECK(labels == "bca"); }
TEST_CASE("every without drift") { /* every(0.025); 60 steps */ CHECK(firings == 39); CHECK(times[k] == Approx(ceil(1.5·(k+1)) / 60)); }
TEST_CASE("every with an interval smaller than the step becomes once per step") { /* every(0.004); 10 steps */ CHECK(firings == 9); }
TEST_CASE("cancel before, during and after") { /* before: cancel outside frame → does not run; during: A cancels B due in the same step → B does not run; after: cancel of an already fired after → false */ }
TEST_CASE("every cancels itself inside the callback") { CHECK(firings == 1); CHECK(discards == 1); CHECK(!engine.schedule().isPending(handle)); }
TEST_CASE("after: lastRun true, cancelling itself returns false and the discard never runs afterwards") { CHECK(lastRun); CHECK(!cancelled); CHECK(discards == 0); }
TEST_CASE("while paused nothing becomes due and the clock stands still") { /* after(1/60); pause; 5 steps */ CHECK(firings == 0); /* resume; 2 steps */ CHECK(firings == 1); }
TEST_CASE("a hidden tab does not fire and does not accumulate") { /* after(2/60); setHidden(true); frames; setHidden(false); frame only records; 2 steps */ CHECK(firings == 0); /* 1 step */ CHECK(firings == 1); }
TEST_CASE("dropped steps do not advance the clock") { /* maxStepsPerFrame=2; after(3/60); frame(0); frame(1.0) */ CHECK(firings == 0); CHECK(engine.time() == 2.0/60); }
TEST_CASE("the payload is copied when scheduling") { /* local Mark changed after the after */ CHECK(label == original value); }
TEST_CASE("a task makes direct structural changes") { /* spawn Position, add Velocity, destroy another */ CHECK(world.isAlive(newEntity)); CHECK_INVARIANTS(world); }
TEST_CASE("an event emitted in a task is dispatched in the same step") { /* handler records time() */ CHECK(handlerTime == taskTime); }
TEST_CASE("context.schedule() is the same Scheduler as the Engine's") { CHECK(&context.schedule() == &engine.schedule()); }
TEST_CASE("destroying the Engine discards the pending tasks once") { CHECK(discards == 2); }
TEST_CASE("cancel and isPending with TaskHandle{} return false") { }
```
`recovery.cpp` (native only, like the existing cases): `TEST_CASE("tasks survive recovery, including those created before the exception")` — an `every(1/60)` schedules an `after(0)` and throws on the first firing; the next `frame` recovers; both are pending and run on the following steps; `CHECK_INVARIANTS`.

`contract_death.cpp` + `CASES`:

| Case | Action | Substrings |
|---|---|---|
| `schedule_negative_delay` | `engine.schedule().after(-1.0, task)` | `schedule`, `delay` |
| `schedule_not_finite_delay` | `after(NAN, task)` | `schedule`, `delay` |
| `every_zero_interval` | `every(0.0, task)` | `every`, `interval` |
| `schedule_null_function` | `after(1.0, nullptr)` | `schedule`, `null function` |
| `task_payload_wrong_type` | task with a `Mark` payload reads `payload<Other>()` | `payload`, `Other` |
| `cancel_slot_out_of_range` | `cancel(TaskHandle{7, 0})` with no tasks | `cancel`, `out of range` |

- [ ] **Step 2: See it fail** — `$DC make test T='^app\.'` → compilation error (`schedule()` and `oly/app/task_context.hpp` do not exist).

- [ ] **Step 3: Implement.** Member `Scheduler scheduler_{*this}` in the `Engine` (data block, after `actions_`). `runDueSchedule()`: `beginRun()`, a `popDue(limit)` loop creating a `TaskContext` with the `DueTask` and calling the function, `endRun()`. Update the comments that became false: the one on the empty stages in `engine.hpp` (only `applyStateTransition` remains) and the one on `frame` about recovery (the tasks stay). The docs of `Scheduler::schedule`/`after`/`every`/`cancel` cover: when it becomes due, that pause freezes, that it works inside and outside `frame`, that the discard runs once and never after the last firing, that `cancel` returns `false` for a finished task or a default handle.

- [ ] **Step 4: See it pass** — `$DC make test` (includes `oly.lint.boundaries`) and `$WC make test PRESET=wasm-debug T='^app\.'`.

- [ ] **Step 5: Commit** `feat: Scheduler with after/every on the game clock and TaskContext`.

---

### Task 3: Script tasks in the C API

**Files:**
- Modify: `engine/runtime/bridge/include/oly/bridge/{engine_api.hpp,engine.hpp}`, `engine/runtime/bridge/src/engine_api.cpp`, `engine/runtime/bridge/tests/{engine_api.cpp,contract_death.cpp,CMakeLists.txt}`

**Interfaces:**
- Consumes: Task 2 (`Engine::schedule()`, `Scheduler::schedule/cancel/isPending`, `TaskContext::payload/lastRun`).
- Produces (C API, `extern "C"`, `OLY_API`): `OLY_SCRIPT_TASK 2u`, `OLY_SCRIPT_TASK_DISCARD 3u`, `typedef struct OlyTaskHandle { uint32_t slot; uint32_t generation; } OlyTaskHandle;`, `olyEngineScheduleScriptTask`, `olyEngineCancelTask`, `olyEngineIsTaskPending` with the signatures from §4.6. In the `bridge` (anonymous): `ScriptTaskPayload`, `runScriptTask(TaskContext&)` → `g_invoker(engine, OLY_SCRIPT_TASK, scriptId, nullptr, lastRun ? 1 : 0)`, `discardScriptTask(void*)` → `g_invoker(engine, OLY_SCRIPT_TASK_DISCARD, scriptId, nullptr, 0)`. The `ScriptInvoker` comment in `engine.hpp` now describes the four `kind`s.

- [ ] **Step 1: Failing tests** in `tests/engine_api.cpp` (the `recordingInvoker` already records `kind`, `id` and `eventCount`):
```cpp
TEST_CASE("after script task: OLY_SCRIPT_TASK with lastRun 1, no discard") { /* schedule(0, 0, 42); 1 step */ CHECK(g_kinds == {OLY_SCRIPT_TASK}); CHECK(g_invocations == {42}); CHECK(g_eventCounts == {1}); CHECK(olyEngineIsTaskPending(...) == 0); }
TEST_CASE("every script task: lastRun 0 and cancelling discards once") { /* schedule(1/60, 1/60, 7); 3 steps; Cancel → 1; Cancel → 0 */ CHECK(count of OLY_SCRIPT_TASK_DISCARD with id 7 == 1); }
TEST_CASE("olyEngineDestroy discards the pending script tasks") { /* 2 tasks; destroy */ CHECK(discards with ids {1, 2}); }
TEST_CASE("script and native tasks in due order") { /* native via engineOf(engine).schedule().after(1/60) records into g_order; script after(0) and after(2/60); 3 steps */ CHECK(g_order == {script, native, script}); }
```
`contract_death.cpp` + `CASES`: `schedule_script_task_negative_interval` (`olyEngineScheduleScriptTask(engine, 0, -1, 1, &out)`; substrings `schedule`, `interval`).

- [ ] **Step 2: See it fail** — `$DC make test T='^bridge\.'` → compilation error.

- [ ] **Step 3: Implement** the three functions as forwarding to `engine->engine.schedule()` (`TaskPayload::of(ScriptTaskPayload{engine, scriptId})`, discard `discardScriptTask`).

- [ ] **Step 4: See it pass** — `$DC make test` and `$WC make test PRESET=wasm-debug T='^bridge\.'`.

- [ ] **Step 5: Commit** `feat: script tasks in the C API with firing and discard through the invoker`.

---

### Task 4: `schedule` in `@oly/core`, WASM test and documentation

**Files:**
- Create: `engine/packages/core/src/{scheduler.ts,task_context.ts,direct_context.ts}`, `engine/testing/tests/wasm/schedule.test.ts`
- Modify: `engine/packages/core/src/{engine.ts,context.ts,handler_context.ts,system_context.ts,bridge.ts,module.ts,index.ts,internal.ts}`, `engine/packages/core/api/public-exports.txt`, `engine/packages/core/tests/unit/surface.test.ts`, `engine/testing/fixture/systems.cpp`, `engine/testing/tests/wasm/fixture.ts`, `CLAUDE.md`

**Interfaces:**
- Consumes: C API from Task 3.
- Produces (`@oly/core`):
  - `scheduler.ts`: `type TaskFunction = (task: TaskContext) => void;` `interface TaskHandle` (brand from "Decisions"); `class Scheduler` with `after(delaySeconds: number, callback: TaskFunction): TaskHandle`, `every(intervalSeconds: number, callback: TaskFunction): TaskHandle`, `cancel(handle: TaskHandle): boolean`, `isPending(handle: TaskHandle): boolean`. Internal constructor `(runtime, bridge, engineHandle, world, actions, assertUsable: (operation: string) => void)`. Module functions (outside `index.ts`): `invokeScriptTask(scheduler, scriptId, lastRun: boolean)`, `discardScriptTask(scheduler, scriptId)`, `scriptTaskCount(scheduler): number`.
  - Errors (`ContractError`, before calling the C API): negative or non-finite delay; interval ≤ 0 or non-finite; a callback that is not a function; a handle not in the `WeakMap` ("handle from another Engine"); released `Engine`. Cancelling an already finished handle returns `false`.
  - `direct_context.ts`: `class DirectContext extends EngineContext` with `spawn`/`destroy`/`add`/`remove` (moved from `handler_context.ts`); `HandlerContext extends DirectContext`.
  - `task_context.ts`: `class TaskContext extends DirectContext` with `get handle(): TaskHandle` and `get lastRun(): boolean`.
  - `EngineContext.schedule: Scheduler` (getter; `ContractError` outside the call); `Engine.schedule: Scheduler` (getter; `ContractError` with the `Engine` released). Both return the same object. The context constructors receive the `Scheduler` after `actions`.
  - `Engine`: the `olyInvokeScript` dispatch now handles `kind` 2 (`invokeScriptTask(scheduler, id, eventCount !== 0)`) and 3 (`discardScriptTask`). The task call does `bridge.sync()`, activates the context and unlocks the `World` structure as in `#invokeHandler`; in the `finally`, it deletes the closure if `lastRun`, deactivates the context and locks again.
  - `Bridge`: `engineScheduleScriptTask(engine, delaySeconds, intervalSeconds, scriptId): { slot: number; generation: number }` (output at `scratch + OutputOffset`, like `systemSpawn`), `engineCancelTask(engine, slot, generation): boolean`, `engineIsTaskPending(engine, slot, generation): boolean`; scheduling and cancelling end with `sync()` (the discard calls JS).
  - `OlyModule`: `_olyEngineScheduleScriptTask`, `_olyEngineCancelTask`, `_olyEngineIsTaskPending`.
  - `internal.ts`: + `engineHandleOf(engine: Engine): number`, `scriptTaskCount`.
  - `public-exports.txt`: + `Scheduler`, `TaskContext`, `TaskFunction`, `TaskHandle`.
- Fixture (`systems.cpp`): `struct AppendDigit { oly::ecs::Entity target; uint32_t digit; };`, task `appendDigitTask` and export `fixtureScheduleDigit` from "Decisions"; `fixture.ts` exposes `fixtureScheduleDigit(engine: number, delaySeconds: number, index: number, generation: number, digit: number)`.

- [ ] **Step 1: Failing tests.**
  - `engine/testing/tests/wasm/schedule.test.ts` (helpers `withEngine` and `stepper` as in `events.test.ts`; `checkInvariants() === null` at the end of each case):
```ts
test("after and every from engine.schedule fire on the due step", ...)                  // time read in the callback: after(2/60) → [2/60]; every(0.025) over 60 steps → 39
test("C++ and TS tasks interleaved by due time and creation", ...)                      // fixtureScheduleDigit(0, d1), TS after(0) d2, fixtureScheduleDigit(1/60, d3), TS after(1/60) d4 → Trace 1234 after 2 steps
test("a TS task makes direct changes and the emitted event is dispatched in the same step", ...) // spawn + emit(Ping); TS handler sees the Ping with the same context.time
test("cancel, isPending and an every that cancels itself", ...)
test("the same closure scheduled twice: cancelling one handle does not affect the other", ...)
test("an exception in a TS after reaches whoever called frame; the next frame recovers and the closure is gone", ...)
test("a TS every that throws stays pending and runs again after recovery", ...)
test("memory grows inside the TS task and the context stays valid", ...)               // like the handler test in events.test.ts: context.spawn until the buffer changes, then context.get/set correct
test("the closure map ends empty on every path", ...)                                  // after firing, cancel, self-cancellation, exception and dispose with a pending every: scriptTaskCount(scheduler) === 0
test("context.schedule and engine.schedule are the same object", ...)
test("errors: negative or NaN delay, interval 0, callback that is not a function, handle from another Engine, released Engine", ...)
test("TaskContext outside the task call is a ContractError", ...)
```
  - `surface.test.ts`: `Scheduler` and `TaskContext` members exactly as in Produces; `Engine`, `SystemContext`, `HandlerContext` and `TaskContext` gain `schedule`; `HandlerContext` keeps `spawn`/`destroy`/`add`/`remove`.

- [ ] **Step 2: See it fail** — `$WC make games-test` → type error in `tsc` (`Scheduler` does not exist).

- [ ] **Step 3: Implement.** The `Scheduler` uses its own `scriptId` counter and `Map<number, { callback, context, handle }>`. `after(d, cb)` calls `engineScheduleScriptTask(handle, d, 0, id)`; `every(i, cb)` passes `(i, i, id)`. The `Engine` creates the `Scheduler` in the constructor, after the `ActionMap`, and passes it to the contexts.

- [ ] **Step 4: `CLAUDE.md`** — a short new section "Engine: schedule" (queue in `app/detail`, game clock, firing at step 2 only when not paused, tasks created at step 2 wait for the following step, drift-free `every`, copied payload ≤ 32 B, discard once and never after `lastRun`, tasks survive recovery, `DirectContext` as the base of handler and task); in the bridge, `OLY_SCRIPT_TASK`/`OLY_SCRIPT_TASK_DISCARD` in the description of `olyHostInvokeScript`; glossary rows for scheduling and for cancelling/querying; `Scheduler` and `TaskContext` in the `@oly/core` list; the `app` summary in the tree gains "schedule".

- [ ] **Step 5: Final verification** — `$DC make test`, `$WC make test PRESET=wasm-debug`, `$WC make games-test`, `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` (no asserts: only the TS validations catch the errors): all green.

- [ ] **Step 6: Commit** `feat: schedule in @oly/core with TS tasks and documentation`.
