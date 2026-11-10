#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"

using oly::app::Engine;
using oly::app::Phase;
using oly::app::SystemContext;

OLY_SYSTEM(appTestNamed) {
  recordName(context);
}

namespace {
  struct Shared {
    oly::ecs::Entity entity;
    bool aliveInSameSystem = true;
    bool aliveInNextPhase = false;
    float x = 0;
  };

  void spawnInUpdate(SystemContext& context) {
    Shared& shared = *static_cast<Shared*>(context.userData());
    shared.entity = context.spawn(Position{1, 2});
    shared.aliveInSameSystem = context.isAlive(shared.entity);
  }

  void observeInUpdateAfter(SystemContext& context) {
    Shared& shared = *static_cast<Shared*>(context.userData());
    shared.aliveInNextPhase = context.isAlive(shared.entity);
    shared.x = context.get<Position>(shared.entity).x;
  }

  void spawnInRender(SystemContext& context) {
    static_cast<Shared*>(context.userData())->entity = context.spawn();
  }

  void addAndDestroy(SystemContext& context) {
    Shared& shared = *static_cast<Shared*>(context.userData());
    context.add(shared.entity, Velocity{});
    context.destroy(shared.entity);
  }
}

TEST_CASE("fases em ordem e, dentro da fase, ordem de registro") {
  oly::ecs::World world;
  Engine engine(world);
  Log log;
  engine.addSystem(Phase::Render, "r", recordName, &log);
  engine.addSystem(Phase::Update, "u1", recordName, &log);
  engine.addSystem(Phase::UpdateBefore, "b", recordName, &log);
  engine.addSystem(Phase::UpdateAfter, "a", recordName, &log);
  engine.addSystem(Phase::Update, "u2", recordName, &log);

  engine.frame(0.0);
  engine.frame(1.0 / 60);

  CHECK(log.calls == std::vector<std::string>{"b", "u1", "u2", "a", "r"});
  CHECK_INVARIANTS(world);
}

TEST_CASE("OLY_SYSTEM registra por nome") {
  CHECK(oly::app::findNamedSystem("appTestNamed") != nullptr);
  CHECK(oly::app::findNamedSystem("naoExiste") == nullptr);
}

TEST_CASE("spawn adiado: viva só na fase seguinte") {
  oly::ecs::World world;
  Engine engine(world);
  Shared shared;
  engine.addSystem(Phase::Update, "spawn", spawnInUpdate, &shared);
  engine.addSystem(Phase::UpdateAfter, "observe", observeInUpdateAfter, &shared);

  engine.frame(0.0);
  engine.frame(1.0 / 60);

  CHECK(!shared.aliveInSameSystem);
  CHECK(shared.aliveInNextPhase);
  CHECK(shared.x == 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("comandos de render aplicados ao fim do frame") {
  oly::ecs::World world;
  Engine engine(world);
  Shared shared;
  engine.addSystem(Phase::Render, "spawn", spawnInRender, &shared);

  engine.frame(0.0);
  engine.frame(1.0 / 60);

  CHECK(world.isAlive(shared.entity));
  CHECK_INVARIANTS(world);
}

TEST_CASE("destroy e add adiados na mesma fase resolvem como o CommandBuffer") {
  oly::ecs::World world;
  Engine engine(world);
  Shared shared;
  shared.entity = world.spawn(Position{});
  engine.addSystem(Phase::Update, "addAndDestroy", addAndDestroy, &shared);

  engine.frame(0.0);
  engine.frame(1.0 / 60);

  CHECK(!world.isAlive(shared.entity));
  CHECK_INVARIANTS(world);
}
