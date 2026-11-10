// A exceção C++ atravessa a Engine, o que só é seguro fora do Emscripten (sem -fwasm-exceptions).
// O caso real, exceção JS, é testado pelos testes TS.
#if !defined(__EMSCRIPTEN__)
#include <doctest/doctest.h>

#include <stdexcept>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"
#include "oly/app/handler.hpp"

using oly::app::Engine;
using oly::app::HandlerContext;
using oly::app::Phase;
using oly::app::SystemContext;

namespace {
  struct Shared {
    oly::ecs::Entity entity;
    int calls = 0;
    int stepsAfterFailure = 0;
  };

  void spawnThenThrowOnce(SystemContext& context) {
    Shared& shared = *static_cast<Shared*>(context.userData());
    ++shared.calls;
    if (shared.calls == 1) {
      shared.entity = context.spawn();
      throw std::runtime_error("boom");
    }

    ++shared.stepsAfterFailure;
  }

  struct Ping {
    uint32_t value;
  };
  struct Echo {
    uint32_t depth;
  };

  struct EventShared {
    int throwerCalls = 0;
    int echoes = 0;
  };

  void emitEchoThenThrowOnce(HandlerContext& context) {
    EventShared& shared = *static_cast<EventShared*>(context.userData());
    ++shared.throwerCalls;

    context.emit(Echo{0});
    if (shared.throwerCalls == 1) {
      throw std::runtime_error("boom");
    }
  }

  void countEchoes(HandlerContext& context) {
    static_cast<EventShared*>(context.userData())->echoes += static_cast<int>(context.eventCount());
  }
}

TEST_CASE("exceção num sistema deixa o spawn adiado sem aplicar") {
  oly::ecs::World world;
  Engine engine(world);
  Shared shared;
  engine.addSystem(Phase::Update, "thrower", spawnThenThrowOnce, &shared);
  engine.frame(0.0);

  CHECK_THROWS(engine.frame(1.0 / 60));

  CHECK(!world.isAlive(shared.entity));
  CHECK_INVARIANTS(world);
}

TEST_CASE("frame seguinte à exceção só registra o tempo e o próximo volta a rodar") {
  oly::ecs::World world;
  Engine engine(world);
  Shared shared;
  engine.addSystem(Phase::Update, "thrower", spawnThenThrowOnce, &shared);
  engine.frame(0.0);
  CHECK_THROWS(engine.frame(1.0 / 60));

  engine.frame(2.0 / 60);
  engine.frame(3.0 / 60);

  CHECK(shared.stepsAfterFailure == 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("exceção num handler descarta os eventos pendentes no frame seguinte") {
  oly::ecs::World world;
  Engine engine(world);
  EventShared shared;
  engine.declareEvent<Ping>();
  engine.declareEvent<Echo>();
  engine.on<Ping>("thrower", emitEchoThenThrowOnce, &shared);
  engine.on<Echo>("echo", countEchoes, &shared);
  engine.frame(0.0);
  engine.emit(Ping{1});

  CHECK_THROWS(engine.frame(1.0 / 60));
  engine.frame(2.0 / 60);
  engine.frame(3.0 / 60);

  CHECK(shared.echoes == 0);

  engine.emit(Ping{2});
  engine.frame(4.0 / 60);

  CHECK(shared.echoes == 1);
  CHECK_INVARIANTS(world);
}
#endif
