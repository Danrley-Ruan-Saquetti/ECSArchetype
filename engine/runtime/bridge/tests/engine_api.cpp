#include "oly/bridge/engine_api.hpp"

#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include "components.hpp"
#include "oly/app/handler.hpp"
#include "oly/app/system.hpp"
#include "oly/app/task_context.hpp"
#include "oly/bridge/c_api.hpp"
#include "oly/bridge/engine.hpp"
#include "test_events.hpp"

namespace {
  std::vector<std::string> g_order;
  std::vector<std::pair<OlyEngine*, uint32_t>> g_invocations;
  std::vector<uint32_t> g_kinds;
  std::vector<uint32_t> g_eventCounts;
  std::vector<uint32_t> g_values;
  void (*g_action)(OlyEngine*, uint32_t) = nullptr;

  void recordingInvoker(OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount) {
    g_order.push_back("s");
    g_invocations.emplace_back(engine, id);
    g_kinds.push_back(kind);
    g_eventCounts.push_back(eventCount);
    const auto* batch = static_cast<const BridgeTestEvent*>(events);
    for (uint32_t index = 0; batch != nullptr && index < eventCount; ++index) {
      g_values.push_back(batch[index].value);
    }
    if (g_action && kind == OLY_SCRIPT_SYSTEM) {
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
    oly::bridge::ScriptInvoker previous = oly::bridge::setScriptInvoker(&recordingInvoker);

    EngineFixture() {
      g_order.clear();
      g_invocations.clear();
      g_kinds.clear();
      g_eventCounts.clear();
      g_values.clear();
      g_action = nullptr;
    }

    ~EngineFixture() {
      oly::bridge::setScriptInvoker(previous);
      olyEngineDestroy(engine);
    }

    void step() {
      olyEngineFrame(engine, 0.0);
      olyEngineFrame(engine, 1.0 / 60);
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
    BridgeTestEvent event{value, 0.0f};
    olyEngineEmit(engine, 0, &event);
  }

  void emitTwoFromScript(OlyEngine* engine, uint32_t) {
    emitValue(engine, 5);
  }

  // Varre só os primeiros índices e gerações: bastam para os poucos spawns destes testes.
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

OLY_SYSTEM(bridgeTestNative) {
  g_order.push_back("n");
}

OLY_HANDLER(bridgeTestNativeHandler, BridgeTestEvent) {
  g_order.push_back("n");
  for (const BridgeTestEvent& event : events) {
    g_values.push_back(event.value);
  }
}

OLY_HANDLER(bridgeTestOtherHandler, BridgeOtherEvent) {}

TEST_CASE("sistema de script é chamado pelo invocador, na ordem com os nativos") {
  EngineFixture fixture;
  const uint32_t first = olyEngineAddScriptSystem(fixture.engine, 1, "s1");
  const uint32_t native = olyEngineAddNativeSystem(fixture.engine, 1, "bridgeTestNative");
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

TEST_CASE("nome nativo desconhecido devolve OLY_INVALID_SYSTEM") {
  EngineFixture fixture;

  CHECK(olyEngineAddNativeSystem(fixture.engine, 1, "naoExiste") == OLY_INVALID_SYSTEM);
}

TEST_CASE("olySystemSpawn e olySystemAdd dentro do script são aplicados ao fim da fase") {
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

TEST_CASE("duas engines no mesmo processo: o invocador recebe a engine certa") {
  EngineFixture fixture;
  EngineFixture other;
  olyEngineAddScriptSystem(fixture.engine, 1, "first");
  olyEngineAddScriptSystem(other.engine, 1, "second");

  other.step();

  REQUIRE(!g_invocations.empty());
  for (const auto& [engine, systemId] : g_invocations) {
    CHECK(engine == other.engine);
  }
}

TEST_CASE("olyEngineWorld serve à API de World existente") {
  EngineFixture fixture;
  OlyWorld* world = olyEngineWorld(fixture.engine);
  OlyEntity entity{};

  olyEntitySpawn(world, &entity);

  CHECK(olyEntityIsAlive(world, entity.index, entity.generation) == 1);
}

TEST_CASE("eventos do esquema são declarados na criação, na ordem") {
  EngineFixture fixture;

  CHECK(oly::bridge::engineOf(fixture.engine).eventTypeOf(oly::app::eventKey<BridgeTestEvent>()) == 0);
  CHECK(oly::bridge::engineOf(fixture.engine).eventTypeCount() == 1);
}

TEST_CASE("handler de script recebe kind, id e o lote") {
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

TEST_CASE("handlers de script e nativo por nome na ordem de registro") {
  EngineFixture fixture;
  olyEngineAddScriptHandler(fixture.engine, 0, "s1");
  const uint32_t native = olyEngineAddNativeHandler(fixture.engine, 0, "bridgeTestNativeHandler");
  olyEngineAddScriptHandler(fixture.engine, 0, "s2");
  emitValue(fixture.engine, 1);

  fixture.step();

  CHECK(native != OLY_INVALID_HANDLER);
  CHECK(native != OLY_HANDLER_TYPE_MISMATCH);
  CHECK(g_order == std::vector<std::string>{"s", "n", "s"});
}

TEST_CASE("sistema de script continua recebendo OLY_SCRIPT_SYSTEM") {
  EngineFixture fixture;
  olyEngineAddScriptSystem(fixture.engine, 1, "s");

  fixture.step();

  REQUIRE(g_kinds.size() == 1);
  CHECK(g_kinds[0] == OLY_SCRIPT_SYSTEM);
  CHECK(g_eventCounts[0] == 0);
}

TEST_CASE("nome de handler desconhecido e handler de outro tipo") {
  EngineFixture fixture;

  CHECK(olyEngineAddNativeHandler(fixture.engine, 0, "naoExiste") == OLY_INVALID_HANDLER);
  CHECK(olyEngineAddNativeHandler(fixture.engine, 0, "bridgeTestOtherHandler") == OLY_HANDLER_TYPE_MISMATCH);
}

TEST_CASE("olyEngineEmit dentro de um sistema de script é despachado no mesmo passo") {
  EngineFixture fixture;
  olyEngineAddScriptSystem(fixture.engine, 1, "emitter");
  olyEngineAddNativeHandler(fixture.engine, 0, "bridgeTestNativeHandler");
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
}

OLY_SYSTEM(bridgeTestReadJump) {
  g_sawJumpPressed = context.actions().button(oly::input::ButtonAction{0}).pressed;
}

TEST_CASE("ações do esquema são declaradas na criação, na ordem") {
  EngineFixture fixture;
  const oly::input::ActionMap& actions = oly::bridge::engineOf(fixture.engine).actions();

  CHECK(actions.actionCount() == 3);
  CHECK(actions.name(0) == "bridgeJump");
  CHECK(actions.kind(2) == oly::input::ActionKind::Pointer);
}

TEST_CASE("ligar, apertar e ler pelo snapshot") {
  ActionFixture fixture;
  g_sawJumpPressed = false;
  olyEngineAddNativeSystem(fixture.engine, 1, "bridgeTestReadJump");

  olyEngineBindButton(fixture.engine, 0, SpaceUsage);
  olyEngineButtonDown(fixture.engine, SpaceUsage);
  fixture.stepOnce();

  CHECK(olyEngineActionSnapshot(fixture.engine)[0].flags == (OLY_ACTION_PRESSED | OLY_ACTION_HELD));
  CHECK(g_sawJumpPressed);
}

TEST_CASE("eixo e ponteiro pela API C") {
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

TEST_CASE("isBound, clearBindings e releaseAll") {
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

TEST_CASE("o endereço do snapshot não muda entre frames") {
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

TEST_CASE("tarefa de script after: OLY_SCRIPT_TASK com lastRun 1, sem descarte") {
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

TEST_CASE("tarefa de script every: lastRun 0 e cancelar descarta uma vez") {
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

TEST_CASE("olyEngineDestroy descarta as tarefas de script pendentes") {
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
}

TEST_CASE("tarefas de script e nativas na ordem de vencimento") {
  EngineFixture fixture;
  OlyTaskHandle task{};
  olyEngineScheduleScriptTask(fixture.engine, 0.0, 0.0, 1, &task);
  oly::bridge::engineOf(fixture.engine).schedule().after(1.0 / 60, nativeTask);
  olyEngineScheduleScriptTask(fixture.engine, 2.0 / 60, 0.0, 2, &task);

  for (int step = 0; step <= 3; ++step) {
    olyEngineFrame(fixture.engine, static_cast<double>(step) / 60);
  }

  CHECK(g_order == std::vector<std::string>{"s", "n", "s"});
  CHECK(fixture.invariantsHold());
}
