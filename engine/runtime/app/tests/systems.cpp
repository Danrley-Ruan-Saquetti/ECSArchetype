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

TEST_CASE("phases in order and, within a phase, registration order") {
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

TEST_CASE("OLY_SYSTEM registers by name") {
  CHECK(oly::app::findNamedSystem("appTestNamed") != nullptr);
  CHECK(oly::app::findNamedSystem("doesNotExist") == nullptr);
}

TEST_CASE("deferred spawn: alive only in the next phase") {
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

TEST_CASE("render commands applied at the end of the frame") {
  oly::ecs::World world;
  Engine engine(world);
  Shared shared;
  engine.addSystem(Phase::Render, "spawn", spawnInRender, &shared);
  engine.frame(0.0);

  engine.frame(1.0 / 60);

  CHECK(world.isAlive(shared.entity));
  CHECK_INVARIANTS(world);
}

TEST_CASE("destroy and add deferred in the same phase resolve like the CommandBuffer") {
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
