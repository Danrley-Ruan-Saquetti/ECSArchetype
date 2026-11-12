// A exceção C++ atravessa a Engine, o que só é seguro fora do Emscripten (sem -fwasm-exceptions).
// O caso real, exceção JS, é testado pelos testes TS.
#if !defined(__EMSCRIPTEN__)
#include <doctest/doctest.h>

#include <stdexcept>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"
#include "oly/app/handler.hpp"
#include "oly/app/state_context.hpp"
#include "oly/app/task_context.hpp"

using oly::app::Engine;
using oly::app::HandlerContext;
using oly::app::Phase;
using oly::app::SystemContext;
using oly::app::TaskContext;

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

  struct TaskCounts {
    int everyRuns = 0;
    int childRuns = 0;
  };

  void countChild(TaskContext& context) {
    ++context.payload<TaskCounts*>()->childRuns;
  }

  void scheduleChildThenThrowOnce(TaskContext& context) {
    TaskCounts* counts = context.payload<TaskCounts*>();
    ++counts->everyRuns;
    if (counts->everyRuns == 1) {
      context.schedule().after(0.0, countChild, counts);
      throw std::runtime_error("boom");
    }
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

  struct StateShared {
    oly::app::StateId target = oly::app::NoState;
    int calls = 0;
  };

  void requestThenThrowOnSecondCall(SystemContext& context) {
    StateShared& shared = *static_cast<StateShared*>(context.userData());
    ++shared.calls;
    if (shared.calls == 2) {
      context.requestState(shared.target);
      throw std::runtime_error("boom");
    }
  }

  void throwHook(oly::app::StateContext&) {
    throw std::runtime_error("boom");
  }

  void countHook(oly::app::StateContext& context) {
    ++static_cast<StateShared*>(context.userData())->calls;
  }

  void spawnHook(oly::app::StateContext& context) {
    *static_cast<oly::ecs::Entity*>(context.userData()) = context.spawn();
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

TEST_CASE("tarefas sobrevivem à recuperação, inclusive as criadas antes da exceção") {
  oly::ecs::World world;
  Engine engine(world);
  TaskCounts counts;
  TaskCounts* countsPointer = &counts;
  const auto handle = engine.schedule().every(1.0 / 60, scheduleChildThenThrowOnce, countsPointer);
  engine.frame(0.0);
  engine.frame(1.0 / 60);
  CHECK_THROWS(engine.frame(2.0 / 60));

  engine.frame(3.0 / 60);

  CHECK(engine.schedule().isPending(handle));
  CHECK(engine.schedule().pendingCount() == 2);

  for (int step = 4; step < 8; ++step) {
    engine.frame(static_cast<double>(step) / 60);
  }

  CHECK(counts.everyRuns > 1);
  CHECK(counts.childRuns == 1);
  CHECK(engine.schedule().isPending(handle));
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


TEST_CASE("a recuperação descarta o pedido pendente") {
  oly::ecs::World world;
  Engine engine(world);
  StateShared shared;
  const auto first = engine.declareState("a");
  shared.target = engine.declareState("b");
  engine.addSystem(Phase::Update, "requester", requestThenThrowOnSecondCall, &shared);
  engine.requestState(first);
  engine.frame(0.0);
  engine.frame(1.0 / 60);

  CHECK_THROWS(engine.frame(2.0 / 60));
  engine.frame(3.0 / 60);
  engine.frame(4.0 / 60);
  engine.frame(5.0 / 60);

  CHECK(engine.state() == first);
  CHECK_INVARIANTS(world);
}

TEST_CASE("exceção em onExit: o estado não muda") {
  oly::ecs::World world;
  Engine engine(world);
  const auto first = engine.declareState("a");
  const auto second = engine.declareState("b");
  engine.onExit(first, "thrower", throwHook);
  engine.requestState(first);
  engine.frame(0.0);
  engine.frame(1.0 / 60);
  engine.requestState(second);

  CHECK_THROWS(engine.frame(2.0 / 60));

  CHECK(engine.state() == first);
  CHECK_INVARIANTS(world);
}

TEST_CASE("exceção em onEnter: o estado já é o novo e os ganchos seguintes não rodam") {
  oly::ecs::World world;
  Engine engine(world);
  StateShared shared;
  const auto first = engine.declareState("a");
  engine.onEnter(first, "thrower", throwHook);
  engine.onEnter(first, "second", countHook, &shared);
  engine.requestState(first);
  engine.frame(0.0);

  CHECK_THROWS(engine.frame(1.0 / 60));

  CHECK(engine.state() == first);
  CHECK(shared.calls == 0);
  CHECK_INVARIANTS(world);
}

TEST_CASE("exceção em onExit não limpa o escopo") {
  oly::ecs::World world;
  Engine engine(world);
  oly::ecs::Entity scoped{};
  const auto first = engine.declareState("a");
  const auto second = engine.declareState("b");
  engine.onEnter(first, "spawner", spawnHook, &scoped);
  engine.onExit(first, "thrower", throwHook);
  engine.requestState(first);
  engine.frame(0.0);
  engine.frame(1.0 / 60);
  engine.requestState(second);

  CHECK_THROWS(engine.frame(2.0 / 60));

  CHECK(world.isAlive(scoped));
  CHECK_INVARIANTS(world);
}
#endif
