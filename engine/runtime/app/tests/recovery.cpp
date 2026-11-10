// A exceção C++ atravessa a Engine, o que só é seguro fora do Emscripten (sem -fwasm-exceptions).
// O caso real, exceção JS, é testado pelos testes TS.
#if !defined(__EMSCRIPTEN__)
#include <doctest/doctest.h>

#include <stdexcept>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"

using oly::app::Engine;
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
#endif
