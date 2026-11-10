#include "oly/bridge/engine_api.hpp"

#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include "components.hpp"
#include "oly/app/system.hpp"
#include "oly/bridge/c_api.hpp"
#include "oly/bridge/engine.hpp"

namespace {
  std::vector<std::string> g_order;
  std::vector<std::pair<OlyEngine*, uint32_t>> g_invocations;
  void (*g_action)(OlyEngine*, uint32_t) = nullptr;

  void recordingInvoker(OlyEngine* engine, uint32_t systemId) {
    g_order.push_back("s");
    g_invocations.emplace_back(engine, systemId);
    if (g_action) {
      g_action(engine, systemId);
    }
  }

  struct EngineFixture {
    OlyEngine* engine = olyEngineCreate(60.0, 5);
    oly::bridge::ScriptInvoker previous = oly::bridge::setScriptInvoker(&recordingInvoker);

    EngineFixture() {
      g_order.clear();
      g_invocations.clear();
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
