#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"

using oly::app::Engine;
using oly::app::Phase;
using oly::app::SystemContext;

namespace {
  void pauseSelf(SystemContext& context) {
    recordName(context);
    context.pause();
  }

  void resumeSelf(SystemContext& context) {
    recordName(context);
    context.resume();
  }

  struct Fixture {
    oly::ecs::World world;
    Engine engine{world};
    Log log;
    double now = 0.0;

    void step() {
      now += 1.0 / 60;
      engine.frame(now);
    }
  };
}

TEST_CASE("pausado: só whilePaused roda, relógio congela, render continua") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "u", recordName, &fixture.log);
  const auto pausedSystem = fixture.engine.addSystem(Phase::Update, "p", recordName, &fixture.log);
  fixture.engine.setWhilePaused(pausedSystem, true);
  fixture.engine.addSystem(Phase::Render, "r", recordName, &fixture.log);
  fixture.engine.frame(0.0);

  fixture.engine.pause();
  fixture.step();
  fixture.step();

  CHECK(fixture.log.calls == std::vector<std::string>{"p", "r", "p", "r"});
  CHECK(fixture.engine.time() == 0.0);

  fixture.engine.resume();
  fixture.log.calls.clear();
  fixture.step();

  CHECK(fixture.log.calls == std::vector<std::string>{"u", "r"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("pause pedido por um sistema vale no próximo passo") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "pauser", pauseSelf, &fixture.log);
  fixture.engine.addSystem(Phase::UpdateAfter, "a", recordName, &fixture.log);
  fixture.engine.frame(0.0);

  fixture.step();

  CHECK(fixture.log.calls == std::vector<std::string>{"pauser", "a"});
  CHECK(fixture.engine.paused());

  fixture.log.calls.clear();
  fixture.step();

  CHECK(fixture.log.calls.empty());
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("sistema whilePaused retoma pelo contexto") {
  Fixture fixture;
  const auto pausedSystem = fixture.engine.addSystem(Phase::Update, "p", resumeSelf, &fixture.log);
  fixture.engine.setWhilePaused(pausedSystem, true);
  fixture.engine.addSystem(Phase::Update, "u", recordName, &fixture.log);
  fixture.engine.frame(0.0);

  fixture.engine.pause();
  fixture.step();
  fixture.step();

  CHECK(fixture.log.calls == std::vector<std::string>{"p", "u"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("aba oculta ignora frames e o primeiro depois só registra o tempo") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "u", recordName, &fixture.log);
  fixture.engine.addSystem(Phase::Render, "r", recordName, &fixture.log);
  fixture.engine.frame(0.0);

  fixture.engine.setHidden(true);
  fixture.engine.frame(1.0);
  CHECK(fixture.log.calls.empty());

  fixture.engine.setHidden(false);
  fixture.engine.frame(2.0);
  CHECK(fixture.log.calls.empty());

  fixture.engine.frame(2.0 + 1.0 / 60);
  CHECK(fixture.log.calls == std::vector<std::string>{"u", "r"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("aba oculta não desfaz a pausa do jogo") {
  Fixture fixture;

  fixture.engine.pause();
  fixture.engine.setHidden(true);
  fixture.engine.setHidden(false);

  CHECK(fixture.engine.paused());
}
