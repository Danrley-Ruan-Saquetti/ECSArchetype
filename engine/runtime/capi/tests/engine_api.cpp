#include "oly/capi/engine_api.hpp"

#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include "components.hpp"
#include "oly/app/handler.hpp"
#include "oly/app/system.hpp"
#include "oly/app/task_context.hpp"
#include "oly/capi/c_api.hpp"
#include "oly/capi/engine.hpp"
#include "test_events.hpp"

namespace {
  std::vector<std::string> g_order;
  std::vector<std::pair<OlyEngine*, uint32_t>> g_invocations;
  std::vector<uint32_t> g_kinds;
  std::vector<uint32_t> g_eventCounts;
  std::vector<uint32_t> g_values;
  std::vector<void*> g_userData;
  void (*g_action)(OlyEngine*, uint32_t) = nullptr;
  uint32_t g_actionKind = OLY_SCRIPT_SYSTEM;
  OlyEntity g_created{};
  OlyEntity g_createdByHandler{};
  int g_actionCalls = 0;

  void recordingInvoker(void* userData, OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount) {
    g_order.push_back("s");
    g_userData.push_back(userData);
    g_invocations.emplace_back(engine, id);
    g_kinds.push_back(kind);
    g_eventCounts.push_back(eventCount);
    const auto* batch = static_cast<const CapiTestEvent*>(events);
    for (uint32_t index = 0; batch != nullptr && index < eventCount; ++index) {
      g_values.push_back(batch[index].value);
    }
    if (g_action && kind == g_actionKind) {
      g_action(engine, id);
    }
  }

  void nativeTask(oly::app::TaskContext&) {
    g_order.push_back("n");
  }

  uint32_t discardsOf(uint32_t scriptId) {
    uint32_t count = 0;
    for (size_t index = 0; index < g_kinds.size(); ++index) {
      if (g_kinds[index] == OLY_SCRIPT_TASK_DISCARD && g_invocations[index].second == scriptId) {
        ++count;
      }
    }

    return count;
  }

  struct EngineFixture {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);
    double now = 0.0;
    bool primed = false;

    EngineFixture() {
      g_order.clear();
      g_invocations.clear();
      g_kinds.clear();
      g_eventCounts.clear();
      g_values.clear();
      g_userData.clear();
      g_action = nullptr;
      g_actionKind = OLY_SCRIPT_SYSTEM;
      g_created = OlyEntity{};
      g_createdByHandler = OlyEntity{};
      g_actionCalls = 0;
      olyEngineSetScriptInvoker(engine, &recordingInvoker, this);
    }

    ~EngineFixture() {
      olyEngineDestroy(engine);
    }

    void step() {
      olyEngineFrame(engine, 0.0);
      olyEngineFrame(engine, 1.0 / 60);
    }

    // One step per call, in sequence (step only suits single-step tests).
    void next() {
      if (!primed) {
        olyEngineFrame(engine, 0.0);
        primed = true;
      }

      now += 1.0 / 60;
      olyEngineFrame(engine, now);
    }

    bool invariantsHold() {
      char message[256];
      const bool ok = olyWorldCheckInvariants(olyEngineWorld(engine), message, sizeof(message)) == 1;
      CHECK_MESSAGE(ok, message);
      return ok;
    }
  };

  void spawnWithA(OlyEngine* engine, uint32_t) {
    OlyEntity entity{};
    olySystemSpawn(engine, &entity);
    A value{7};
    olySystemAdd(engine, entity.index, entity.generation, oly::ecs::componentId<A>(), &value);
  }

  void emitValue(OlyEngine* engine, uint32_t value) {
    CapiTestEvent event{value, 0.0f};
    olyEngineEmit(engine, 0, &event);
  }

  void emitTwoFromScript(OlyEngine* engine, uint32_t) {
    emitValue(engine, 5);
  }

  // Scans only the first indices and generations: enough for the few spawns in these tests.
  std::vector<OlyEntity> aliveEntities(OlyWorld* world) {
    std::vector<OlyEntity> alive;
    for (uint32_t index = 0; index < 4; ++index) {
      for (uint32_t generation = 0; generation < 3; ++generation) {
        if (olyEntityIsAlive(world, index, generation) == 1) {
          alive.push_back(OlyEntity{index, generation});
        }
      }
    }

    return alive;
  }
}

OLY_SYSTEM(capiTestNative) {
  g_order.push_back("n");
}

OLY_HANDLER(capiTestNativeHandler, CapiTestEvent) {
  g_order.push_back("n");
  for (const CapiTestEvent& event : events) {
    g_values.push_back(event.value);
  }
}

OLY_HANDLER(capiTestOtherHandler, CapiOtherEvent) {}

TEST_CASE("script system is called by the invoker, in order with the native ones") {
  EngineFixture fixture;
  const uint32_t first = olyEngineAddScriptSystem(fixture.engine, 1, "s1");
  const uint32_t native = olyEngineAddNativeSystem(fixture.engine, 1, "capiTestNative");
  const uint32_t second = olyEngineAddScriptSystem(fixture.engine, 1, "s2");

  fixture.step();

  CHECK(native != OLY_INVALID_SYSTEM);
  CHECK(g_order == std::vector<std::string>{"s", "n", "s"});
  REQUIRE(g_invocations.size() == 2);
  CHECK(g_invocations[0].first == fixture.engine);
  CHECK(g_invocations[0].second == first);
  CHECK(g_invocations[1].second == second);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("unknown native name returns OLY_INVALID_SYSTEM") {
  EngineFixture fixture;

  CHECK(olyEngineAddNativeSystem(fixture.engine, 1, "doesNotExist") == OLY_INVALID_SYSTEM);
}

TEST_CASE("olySystemSpawn and olySystemAdd inside the script are applied at the end of the phase") {
  EngineFixture fixture;
  olyEngineAddScriptSystem(fixture.engine, 1, "spawner");
  g_action = spawnWithA;

  fixture.step();

  OlyWorld* world = olyEngineWorld(fixture.engine);
  const std::vector<OlyEntity> alive = aliveEntities(world);
  REQUIRE(alive.size() == 1);
  const A* value = static_cast<const A*>(olyComponentGet(world, alive[0].index, alive[0].generation, oly::ecs::componentId<A>()));
  REQUIRE(value != nullptr);
  CHECK(value->value == 7);
  CHECK(fixture.invariantsHold());
}

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

TEST_CASE("the invoker installed between frames runs the next script call") {
  EngineFixture fixture;
  int replacement = 0;
  olyEngineAddScriptSystem(fixture.engine, 1, "s");
  fixture.next();

  olyEngineSetScriptInvoker(fixture.engine, &recordingInvoker, &replacement);
  fixture.next();

  CHECK(g_userData == std::vector<void*>{&fixture, &replacement});
}

TEST_CASE("olyEngineWorld works with the existing World API") {
  EngineFixture fixture;
  OlyWorld* world = olyEngineWorld(fixture.engine);
  OlyEntity entity{};

  olyEntitySpawn(world, &entity);

  CHECK(olyEntityIsAlive(world, entity.index, entity.generation) == 1);
}

TEST_CASE("schema events are declared at creation, in order") {
  EngineFixture fixture;

  CHECK(oly::capi::engineOf(fixture.engine).eventTypeOf(oly::app::eventKey<CapiTestEvent>()) == 0);
  CHECK(oly::capi::engineOf(fixture.engine).eventTypeCount() == 1);
}

TEST_CASE("script handler receives kind, id and the batch") {
  EngineFixture fixture;
  const uint32_t handler = olyEngineAddScriptHandler(fixture.engine, 0, "h");
  emitValue(fixture.engine, 1);
  emitValue(fixture.engine, 2);

  fixture.step();

  REQUIRE(g_kinds.size() == 1);
  CHECK(g_kinds[0] == OLY_SCRIPT_HANDLER);
  CHECK(g_invocations[0].second == handler);
  CHECK(g_eventCounts[0] == 2);
  CHECK(g_values == std::vector<uint32_t>{1, 2});
  CHECK(fixture.invariantsHold());
}

TEST_CASE("script handlers and a native one by name in registration order") {
  EngineFixture fixture;
  olyEngineAddScriptHandler(fixture.engine, 0, "s1");
  const uint32_t native = olyEngineAddNativeHandler(fixture.engine, 0, "capiTestNativeHandler");
  olyEngineAddScriptHandler(fixture.engine, 0, "s2");
  emitValue(fixture.engine, 1);

  fixture.step();

  CHECK(native != OLY_INVALID_HANDLER);
  CHECK(native != OLY_HANDLER_TYPE_MISMATCH);
  CHECK(g_order == std::vector<std::string>{"s", "n", "s"});
}

TEST_CASE("script system still receives OLY_SCRIPT_SYSTEM") {
  EngineFixture fixture;
  olyEngineAddScriptSystem(fixture.engine, 1, "s");

  fixture.step();

  REQUIRE(g_kinds.size() == 1);
  CHECK(g_kinds[0] == OLY_SCRIPT_SYSTEM);
  CHECK(g_eventCounts[0] == 0);
}

TEST_CASE("unknown handler name and handler of another type") {
  EngineFixture fixture;

  CHECK(olyEngineAddNativeHandler(fixture.engine, 0, "doesNotExist") == OLY_INVALID_HANDLER);
  CHECK(olyEngineAddNativeHandler(fixture.engine, 0, "capiTestOtherHandler") == OLY_HANDLER_TYPE_MISMATCH);
}

TEST_CASE("olyEngineEmit inside a script system is dispatched in the same step") {
  EngineFixture fixture;
  olyEngineAddScriptSystem(fixture.engine, 1, "emitter");
  olyEngineAddNativeHandler(fixture.engine, 0, "capiTestNativeHandler");
  g_action = emitTwoFromScript;

  fixture.step();

  CHECK(g_values == std::vector<uint32_t>{5});
}

namespace {
  constexpr uint32_t SpaceUsage = 0x0007'002Cu;
  constexpr uint32_t KeyWUsage = 0x0007'001Au;
  constexpr uint32_t KeySUsage = 0x0007'0016u;
  constexpr uint32_t KeyAUsage = 0x0007'0004u;
  constexpr uint32_t KeyDUsage = 0x0007'0007u;

  bool g_sawJumpPressed = false;

  struct ActionFixture : EngineFixture {
    double now = 0.0;

    ActionFixture() {
      olyEngineFrame(engine, now);
    }

    void stepOnce() {
      now += 1.0 / 60;
      olyEngineFrame(engine, now);
    }
  };

  constexpr uint32_t MenuState = 0;
  constexpr uint32_t PlayingState = 1;

  uint32_t countKind(uint32_t kind) {
    uint32_t count = 0;
    for (const uint32_t recorded : g_kinds) {
      count += recorded == kind ? 1 : 0;
    }

    return count;
  }

  void spawnWithScopeOnce(OlyEngine* engine, uint32_t) {
    if (olyEngineState(engine) != MenuState || g_actionCalls > 0) {
      return;
    }

    ++g_actionCalls;
    olyDirectSpawn(engine, &g_created);
  }

  void spawnAndPersistOnce(OlyEngine* engine, uint32_t) {
    if (olyEngineState(engine) != MenuState || g_actionCalls > 0) {
      return;
    }

    ++g_actionCalls;
    if (g_actionKind == OLY_SCRIPT_SYSTEM) {
      olySystemSpawn(engine, &g_created);
      olyContextPersist(engine, g_created.index, g_created.generation);
    } else {
      olyDirectSpawn(engine, &g_createdByHandler);
      olyContextPersist(engine, g_createdByHandler.index, g_createdByHandler.generation);
    }
  }
}

OLY_SYSTEM(capiTestReadJump) {
  g_sawJumpPressed = context.actions().button(oly::input::ButtonAction{0}).pressed;
}

TEST_CASE("schema actions are declared at creation, in order") {
  EngineFixture fixture;
  const oly::input::ActionMap& actions = oly::capi::engineOf(fixture.engine).actions();

  CHECK(actions.actionCount() == 3);
  CHECK(actions.name(0) == "capiJump");
  CHECK(actions.kind(2) == oly::input::ActionKind::Pointer);
}

TEST_CASE("olyEngineCreate declares the schema states in order") {
  EngineFixture fixture;

  CHECK(oly::capi::engineOf(fixture.engine).stateCount() == 2);
}

TEST_CASE("bind, press and read through the snapshot") {
  ActionFixture fixture;
  g_sawJumpPressed = false;
  olyEngineAddNativeSystem(fixture.engine, 1, "capiTestReadJump");

  olyEngineBindButton(fixture.engine, 0, SpaceUsage);
  olyEngineButtonDown(fixture.engine, SpaceUsage);
  fixture.stepOnce();

  CHECK(olyEngineActionSnapshot(fixture.engine)[0].flags == (OLY_ACTION_PRESSED | OLY_ACTION_HELD));
  CHECK(g_sawJumpPressed);
}

TEST_CASE("axis and pointer through the C API") {
  ActionFixture fixture;

  olyEngineBindAxis2d(fixture.engine, 1, KeyWUsage, KeySUsage, KeyAUsage, KeyDUsage);
  olyEngineButtonDown(fixture.engine, KeyDUsage);
  olyEnginePointerMove(fixture.engine, 5.0f, 6.0f);
  fixture.stepOnce();

  const OlyActionState* snapshot = olyEngineActionSnapshot(fixture.engine);
  CHECK(snapshot[1].x == 1.0f);
  CHECK(snapshot[1].y == 0.0f);
  CHECK(snapshot[2].x == 5.0f);
  CHECK(snapshot[2].y == 6.0f);
  CHECK(snapshot[2].flags == OLY_ACTION_VALID);
}

TEST_CASE("isBound, clearBindings and releaseAll") {
  ActionFixture fixture;

  olyEngineBindButton(fixture.engine, 0, SpaceUsage);
  CHECK(olyEngineIsBound(fixture.engine, SpaceUsage) == 1);

  olyEngineClearBindings(fixture.engine, 0);
  CHECK(olyEngineIsBound(fixture.engine, SpaceUsage) == 0);

  olyEngineBindButton(fixture.engine, 0, SpaceUsage);
  olyEngineButtonDown(fixture.engine, SpaceUsage);
  fixture.stepOnce();
  olyEngineReleaseAll(fixture.engine);
  fixture.stepOnce();

  CHECK((olyEngineActionSnapshot(fixture.engine)[0].flags & OLY_ACTION_RELEASED) != 0);
}

TEST_CASE("the snapshot address does not change between frames") {
  ActionFixture fixture;
  const OlyActionState* before = olyEngineActionSnapshot(fixture.engine);

  olyEngineBindButton(fixture.engine, 0, SpaceUsage);
  for (int step = 0; step < 3; ++step) {
    olyEngineButtonDown(fixture.engine, SpaceUsage);
    fixture.stepOnce();
    olyEngineButtonUp(fixture.engine, SpaceUsage);
  }

  CHECK(olyEngineActionSnapshot(fixture.engine) == before);
}

TEST_CASE("after script task: OLY_SCRIPT_TASK with lastRun 1, no discard") {
  EngineFixture fixture;
  OlyTaskHandle task{};
  olyEngineScheduleScriptTask(fixture.engine, 0.0, 0.0, 42, &task);

  olyEngineFrame(fixture.engine, 0.0);
  olyEngineFrame(fixture.engine, 1.0 / 60);

  CHECK(g_kinds == std::vector<uint32_t>{OLY_SCRIPT_TASK});
  REQUIRE(g_invocations.size() == 1);
  CHECK(g_invocations[0] == std::pair<OlyEngine*, uint32_t>{fixture.engine, 42});
  CHECK(g_eventCounts == std::vector<uint32_t>{1});
  CHECK(olyEngineIsTaskPending(fixture.engine, task.slot, task.generation) == 0);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("every script task: lastRun 0 and cancel discards once") {
  EngineFixture fixture;
  OlyTaskHandle task{};
  olyEngineScheduleScriptTask(fixture.engine, 1.0 / 60, 1.0 / 60, 7, &task);

  for (int step = 0; step <= 3; ++step) {
    olyEngineFrame(fixture.engine, static_cast<double>(step) / 60);
  }

  CHECK(g_eventCounts == std::vector<uint32_t>{0, 0});
  CHECK(olyEngineIsTaskPending(fixture.engine, task.slot, task.generation) == 1);
  CHECK(olyEngineCancelTask(fixture.engine, task.slot, task.generation) == 1);
  CHECK(olyEngineCancelTask(fixture.engine, task.slot, task.generation) == 0);
  CHECK(discardsOf(7) == 1);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyEngineDestroy discards the pending script tasks") {
  EngineFixture fixture;
  OlyTaskHandle first{};
  OlyTaskHandle second{};
  olyEngineScheduleScriptTask(fixture.engine, 10.0, 0.0, 1, &first);
  olyEngineScheduleScriptTask(fixture.engine, 1.0, 1.0, 2, &second);

  olyEngineDestroy(fixture.engine);
  fixture.engine = nullptr;

  CHECK(discardsOf(1) == 1);
  CHECK(discardsOf(2) == 1);
  CHECK(g_kinds.size() == 2);
  CHECK(g_userData == std::vector<void*>{&fixture, &fixture});
}

TEST_CASE("script and native tasks in due order") {
  EngineFixture fixture;
  OlyTaskHandle task{};
  olyEngineScheduleScriptTask(fixture.engine, 0.0, 0.0, 1, &task);
  oly::capi::engineOf(fixture.engine).schedule().after(1.0 / 60, nativeTask);
  olyEngineScheduleScriptTask(fixture.engine, 2.0 / 60, 0.0, 2, &task);

  for (int step = 0; step <= 3; ++step) {
    olyEngineFrame(fixture.engine, static_cast<double>(step) / 60);
  }

  CHECK(g_order == std::vector<std::string>{"s", "n", "s"});
  CHECK(fixture.invariantsHold());
}

OLY_STATE_HOOK(capiNamedEnter) {
  g_order.push_back("ne");
}

TEST_CASE("olyEngineState starts at OLY_NO_STATE and changes in step 5") {
  EngineFixture fixture;
  CHECK(olyEngineState(fixture.engine) == OLY_NO_STATE);

  olyEngineRequestState(fixture.engine, PlayingState);

  CHECK(olyEngineState(fixture.engine) == OLY_NO_STATE);

  fixture.next();

  CHECK(olyEngineState(fixture.engine) == PlayingState);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("script hook: OLY_SCRIPT_STATE_HOOK with the hookId and the other state") {
  EngineFixture fixture;
  const uint32_t enterPlaying = olyEngineAddScriptStateHook(fixture.engine, OLY_STATE_ENTER, PlayingState, "enterPlaying");
  const uint32_t exitMenu = olyEngineAddScriptStateHook(fixture.engine, OLY_STATE_EXIT, MenuState, "exitMenu");
  olyEngineRequestState(fixture.engine, MenuState);
  fixture.next();
  fixture.next();
  g_kinds.clear();
  g_eventCounts.clear();
  g_invocations.clear();

  olyEngineRequestState(fixture.engine, PlayingState);
  fixture.next();

  CHECK(g_kinds == std::vector<uint32_t>{OLY_SCRIPT_STATE_HOOK, OLY_SCRIPT_STATE_HOOK});
  CHECK(g_eventCounts == std::vector<uint32_t>{PlayingState, MenuState});
  REQUIRE(g_invocations.size() == 2);
  CHECK(g_invocations[0].second == exitMenu);
  CHECK(g_invocations[1].second == enterPlaying);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("native hook by name and OLY_INVALID_STATE_HOOK for an unknown name") {
  EngineFixture fixture;
  const uint32_t named = olyEngineAddNativeStateHook(fixture.engine, OLY_STATE_ENTER, MenuState, "capiNamedEnter");
  const uint32_t unknown = olyEngineAddNativeStateHook(fixture.engine, OLY_STATE_ENTER, MenuState, "doesNotExist");
  olyEngineRequestState(fixture.engine, MenuState);

  fixture.next();
  fixture.next();

  CHECK(named != OLY_INVALID_STATE_HOOK);
  CHECK(unknown == OLY_INVALID_STATE_HOOK);
  CHECK(g_order == std::vector<std::string>{"ne"});
  CHECK(fixture.invariantsHold());
}

TEST_CASE("state mask on system and handler; 0 removes the restriction") {
  EngineFixture fixture;
  const uint32_t system = olyEngineAddScriptSystem(fixture.engine, 1, "restricted");
  const uint32_t handler = olyEngineAddScriptHandler(fixture.engine, 0, "restricted");
  olyEngineSetSystemStates(fixture.engine, system, 1u << PlayingState);
  olyEngineSetHandlerStates(fixture.engine, handler, 1u << PlayingState);
  emitValue(fixture.engine, 1);

  fixture.next();

  CHECK(countKind(OLY_SCRIPT_SYSTEM) == 0);
  CHECK(countKind(OLY_SCRIPT_HANDLER) == 0);

  olyEngineRequestState(fixture.engine, PlayingState);
  fixture.next();
  emitValue(fixture.engine, 2);
  fixture.next();

  CHECK(countKind(OLY_SCRIPT_SYSTEM) == 1);
  CHECK(countKind(OLY_SCRIPT_HANDLER) == 1);

  olyEngineSetSystemStates(fixture.engine, system, 0);
  olyEngineRequestState(fixture.engine, MenuState);
  fixture.next();
  fixture.next();

  CHECK(countKind(OLY_SCRIPT_SYSTEM) == 3);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyDirectSpawn in a script handler is scoped and disappears on the switch") {
  EngineFixture fixture;
  olyEngineAddScriptHandler(fixture.engine, 0, "spawner");
  g_action = spawnWithScopeOnce;
  g_actionKind = OLY_SCRIPT_HANDLER;
  olyEngineRequestState(fixture.engine, MenuState);
  fixture.next();
  fixture.next();
  emitValue(fixture.engine, 1);
  fixture.next();
  OlyWorld* world = olyEngineWorld(fixture.engine);

  CHECK(olyEntityIsAlive(world, g_created.index, g_created.generation) == 1);

  olyEngineRequestState(fixture.engine, PlayingState);
  fixture.next();

  CHECK(olyEntityIsAlive(world, g_created.index, g_created.generation) == 0);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyContextPersist in a script system (deferred) and in a script handler (direct)") {
  EngineFixture fixture;
  olyEngineAddScriptSystem(fixture.engine, 1, "persister");
  olyEngineAddScriptHandler(fixture.engine, 0, "persister");
  g_action = spawnAndPersistOnce;
  olyEngineRequestState(fixture.engine, MenuState);
  fixture.next();
  fixture.next();
  OlyEntity persistedBySystem = g_created;
  g_actionKind = OLY_SCRIPT_HANDLER;
  g_actionCalls = 0;
  emitValue(fixture.engine, 1);
  fixture.next();
  OlyEntity persistedByHandler = g_createdByHandler;

  olyEngineRequestState(fixture.engine, PlayingState);
  fixture.next();
  OlyWorld* world = olyEngineWorld(fixture.engine);

  CHECK(olyEntityIsAlive(world, persistedBySystem.index, persistedBySystem.generation) == 1);
  CHECK(olyEntityIsAlive(world, persistedByHandler.index, persistedByHandler.generation) == 1);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyEnginePersistTask keeps the script task across the switch and returns 0 for a non-pending task") {
  EngineFixture fixture;
  olyEngineRequestState(fixture.engine, MenuState);
  fixture.next();
  fixture.next();
  OlyTaskHandle scoped{};
  OlyTaskHandle persisted{};
  olyEngineScheduleScriptTask(fixture.engine, 100.0, 0.0, 11, &scoped);
  olyEngineScheduleScriptTask(fixture.engine, 100.0, 0.0, 12, &persisted);
  OlyTaskHandle canceled{};
  olyEngineScheduleScriptTask(fixture.engine, 100.0, 0.0, 13, &canceled);
  olyEngineCancelTask(fixture.engine, canceled.slot, canceled.generation);

  const int32_t persistedResult = olyEnginePersistTask(fixture.engine, persisted.slot, persisted.generation);
  const int32_t canceledResult = olyEnginePersistTask(fixture.engine, canceled.slot, canceled.generation);
  olyEngineRequestState(fixture.engine, PlayingState);
  fixture.next();

  CHECK(persistedResult == 1);
  CHECK(canceledResult == 0);
  CHECK(olyEngineIsTaskPending(fixture.engine, persisted.slot, persisted.generation) == 1);
  CHECK(olyEngineIsTaskPending(fixture.engine, scoped.slot, scoped.generation) == 0);
  CHECK(discardsOf(11) == 1);
  CHECK(discardsOf(12) == 0);

  olyEngineCancelTask(fixture.engine, persisted.slot, persisted.generation);

  CHECK(fixture.invariantsHold());
}
