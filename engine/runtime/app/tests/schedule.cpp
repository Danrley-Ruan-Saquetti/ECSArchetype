#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"
#include "oly/app/task_context.hpp"

using doctest::Approx;
using oly::app::Engine;
using oly::app::EngineConfig;
using oly::app::HandlerContext;
using oly::app::Phase;
using oly::app::Scheduler;
using oly::app::SystemContext;
using oly::app::TaskContext;
using oly::app::TaskHandle;

namespace {
  struct Mark {
    Log* log;
    char label;
  };

  struct Counters {
    int* runs;
    int* discards;
  };

  struct Chain {
    Log* log;
    int remaining;
  };

  struct CancelTarget {
    TaskHandle target;
  };

  struct LastRunProbe {
    bool* lastRun;
    bool* cancelled;
    int* discards;
  };

  struct Targets {
    oly::ecs::Entity existing;
    oly::ecs::Entity doomed;
    oly::ecs::Entity* created;
  };

  struct Ping {
    uint32_t value;
  };

  struct SchedulingSystem {
    Log* log;
    bool done = false;
  };

  std::string labels(const Log& log) {
    std::string result;
    for (const std::string& call : log.calls) {
      result += call;
    }

    return result;
  }

  void markTask(TaskContext& context) {
    const Mark& mark = context.payload<Mark>();
    mark.log->calls.push_back(std::string(1, mark.label));
    mark.log->times.push_back(context.time());
  }

  void chainTask(TaskContext& context) {
    const Chain& chain = context.payload<Chain>();
    chain.log->times.push_back(context.time());
    if (chain.remaining > 0) {
      context.schedule().after(0.0, chainTask, Chain{chain.log, chain.remaining - 1});
    }
  }

  void cancelTargetTask(TaskContext& context) {
    context.schedule().cancel(context.payload<CancelTarget>().target);
  }

  void countRun(TaskContext& context) {
    ++*context.payload<Counters>().runs;
  }

  void countDiscard(void* payload) {
    ++*static_cast<Counters*>(payload)->discards;
  }

  void cancelSelfTask(TaskContext& context) {
    ++*context.payload<Counters>().runs;
    context.schedule().cancel(context.handle());
  }

  void probeLastRunTask(TaskContext& context) {
    const LastRunProbe& probe = context.payload<LastRunProbe>();
    *probe.lastRun = context.lastRun();
    *probe.cancelled = context.schedule().cancel(context.handle());
  }

  void probeDiscard(void* payload) {
    ++*static_cast<LastRunProbe*>(payload)->discards;
  }

  void structuralTask(TaskContext& context) {
    const Targets& targets = context.payload<Targets>();
    *targets.created = context.spawn(Position{1.0f, 2.0f});
    context.add(targets.existing, Velocity{3.0f, 4.0f});
    context.destroy(targets.doomed);
  }

  void emitPingTask(TaskContext& context) {
    context.payload<Mark>().log->times.push_back(context.time());
    context.emit(Ping{1});
  }

  void recordHandlerTime(HandlerContext& context) {
    static_cast<Log*>(context.userData())->times.push_back(context.time());
  }

  void scheduleOnceFromSystem(SystemContext& context) {
    SchedulingSystem& system = *static_cast<SchedulingSystem*>(context.userData());
    if (system.done) {
      return;
    }

    system.done = true;
    context.schedule().after(0.0, markTask, Mark{system.log, 'x'});
  }

  void captureScheduler(SystemContext& context) {
    *static_cast<Scheduler**>(context.userData()) = &context.schedule();
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

    void steps(int count) {
      for (int index = 0; index < count; ++index) {
        step();
      }
    }
  };
}

TEST_CASE("after scheduled outside frame fires in the step whose start reaches the due time") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().after(2.0 / 60, markTask, Mark{&fixture.log, 'a'});

  fixture.steps(3);

  REQUIRE(fixture.log.times.size() == 1);
  CHECK(fixture.log.times[0] == Approx(2.0 / 60));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("after(0) outside frame runs in stage 2 of the next step") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "s", recordName, &fixture.log);
  fixture.engine.frame(0.0);
  fixture.engine.schedule().after(0.0, markTask, Mark{&fixture.log, 't'});

  fixture.step();

  CHECK(fixture.log.calls == std::vector<std::string>{"t", "s"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("after(0) inside a task only runs in the next step") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().after(0.0, chainTask, Chain{&fixture.log, 1});

  fixture.step();

  CHECK(fixture.log.times.size() == 1);

  fixture.step();

  REQUIRE(fixture.log.times.size() == 2);
  CHECK(fixture.log.times[0] == Approx(0.0));
  CHECK(fixture.log.times[1] == Approx(1.0 / 60));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("after(0) scheduled by a system runs in the step after its own") {
  Fixture fixture;
  SchedulingSystem system{&fixture.log};
  fixture.engine.addSystem(Phase::Update, "scheduler", scheduleOnceFromSystem, &system);
  fixture.engine.frame(0.0);

  fixture.step();

  CHECK(fixture.log.times.empty());

  fixture.step();

  REQUIRE(fixture.log.times.size() == 1);
  CHECK(fixture.log.times[0] == Approx(1.0 / 60));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("after(0) scheduled in the render phase runs in the next step") {
  Fixture fixture;
  SchedulingSystem system{&fixture.log};
  fixture.engine.addSystem(Phase::Render, "scheduler", scheduleOnceFromSystem, &system);
  fixture.engine.frame(0.0);

  fixture.step();

  CHECK(fixture.log.times.empty());

  fixture.step();

  REQUIRE(fixture.log.times.size() == 1);
  CHECK(fixture.log.times[0] == Approx(1.0 / 60));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("same step: due-time order and, on ties, creation order") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  Scheduler& schedule = fixture.engine.schedule();
  schedule.after(1.5 / 60, markTask, Mark{&fixture.log, 'a'});
  schedule.after(1.2 / 60, markTask, Mark{&fixture.log, 'b'});
  schedule.after(1.2 / 60, markTask, Mark{&fixture.log, 'c'});

  fixture.steps(3);

  CHECK(labels(fixture.log) == "bca");
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("every without drift") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().every(0.025, markTask, Mark{&fixture.log, 'e'});

  fixture.steps(60);

  REQUIRE(fixture.log.times.size() == 39);
  for (size_t index = 0; index < fixture.log.times.size(); ++index) {
    CHECK(fixture.log.times[index] == Approx(std::ceil(1.5 * static_cast<double>(index + 1)) / 60));
  }
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("every with an interval smaller than the step becomes once per step") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().every(0.004, markTask, Mark{&fixture.log, 'e'});

  fixture.steps(10);

  CHECK(fixture.log.times.size() == 9);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("every with the step interval does not miss a firing in a long game") {
  Fixture fixture;
  int runs = 0;
  int discards = 0;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().every(fixture.engine.fixedDeltaTime(), countRun, Counters{&runs, &discards});

  fixture.steps(20000);

  CHECK(runs == 19999);
}

TEST_CASE("cancel before, during and after") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  Scheduler& schedule = fixture.engine.schedule();

  SUBCASE("before it is due") {
    const TaskHandle handle = schedule.after(1.0 / 60, markTask, Mark{&fixture.log, 'a'});

    CHECK(schedule.cancel(handle));
    fixture.steps(3);

    CHECK(fixture.log.times.empty());
  }

  SUBCASE("during the step in which the other would be due") {
    const TaskHandle second = schedule.after(1.5 / 60, markTask, Mark{&fixture.log, 'b'});
    schedule.after(1.2 / 60, cancelTargetTask, CancelTarget{second});

    fixture.steps(3);

    CHECK(fixture.log.times.empty());
    CHECK(!schedule.isPending(second));
  }

  SUBCASE("after firing") {
    const TaskHandle handle = schedule.after(1.0 / 60, markTask, Mark{&fixture.log, 'a'});
    fixture.steps(3);

    CHECK(fixture.log.times.size() == 1);
    CHECK(!schedule.cancel(handle));
  }

  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("every cancels itself inside the callback") {
  Fixture fixture;
  int runs = 0;
  int discards = 0;
  fixture.engine.frame(0.0);
  const TaskHandle handle = fixture.engine.schedule().every(1.0 / 60, cancelSelfTask, Counters{&runs, &discards}, countDiscard);

  fixture.steps(4);

  CHECK(runs == 1);
  CHECK(discards == 1);
  CHECK(!fixture.engine.schedule().isPending(handle));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("after: lastRun true, cancelling itself returns false and the discard never runs afterwards") {
  Fixture fixture;
  bool lastRun = false;
  bool cancelled = true;
  int discards = 0;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().after(1.0 / 60, probeLastRunTask, LastRunProbe{&lastRun, &cancelled, &discards}, probeDiscard);

  fixture.steps(4);

  CHECK(lastRun);
  CHECK(!cancelled);
  CHECK(discards == 0);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("while paused nothing comes due and the clock stays still") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().after(1.0 / 60, markTask, Mark{&fixture.log, 'a'});
  fixture.engine.pause();

  fixture.steps(5);

  CHECK(fixture.log.times.empty());

  fixture.engine.resume();
  fixture.steps(2);

  CHECK(fixture.log.times.size() == 1);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("hidden tab does not fire and does not accumulate") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().after(2.0 / 60, markTask, Mark{&fixture.log, 'a'});

  fixture.engine.setHidden(true);
  fixture.steps(10);
  fixture.engine.setHidden(false);
  fixture.step();
  fixture.steps(2);

  CHECK(fixture.log.times.empty());

  fixture.step();

  CHECK(fixture.log.times.size() == 1);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("discarded steps do not advance the clock") {
  oly::ecs::World world;
  EngineConfig config;
  config.maxStepsPerFrame = 2;
  Engine engine(world, config);
  Log log;
  engine.frame(0.0);
  engine.schedule().after(3.0 / 60, markTask, Mark{&log, 'a'});

  engine.frame(1.0);

  CHECK(log.times.empty());
  CHECK(engine.time() == Approx(2.0 / 60));
  CHECK_INVARIANTS(world);
}

TEST_CASE("payload is copied when scheduling") {
  Fixture fixture;
  fixture.engine.frame(0.0);
  Mark mark{&fixture.log, 'a'};
  fixture.engine.schedule().after(1.0 / 60, markTask, mark);
  mark.label = 'z';

  fixture.steps(3);

  CHECK(labels(fixture.log) == "a");
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("task makes direct structural changes") {
  Fixture fixture;
  const oly::ecs::Entity existing = fixture.world.spawn(Position{});
  const oly::ecs::Entity doomed = fixture.world.spawn(Position{});
  oly::ecs::Entity created;
  fixture.engine.frame(0.0);
  fixture.engine.schedule().after(0.0, structuralTask, Targets{existing, doomed, &created});

  fixture.step();

  CHECK(fixture.world.isAlive(created));
  CHECK(fixture.world.has<Velocity>(existing));
  CHECK(!fixture.world.isAlive(doomed));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("event emitted in a task is dispatched in the same step") {
  Fixture fixture;
  Log handlerLog;
  fixture.engine.declareEvent<Ping>();
  fixture.engine.on<Ping>("ping", recordHandlerTime, &handlerLog);
  fixture.engine.frame(0.0);
  fixture.engine.schedule().after(2.0 / 60, emitPingTask, Mark{&fixture.log, 'p'});

  fixture.steps(3);

  REQUIRE(fixture.log.times.size() == 1);
  REQUIRE(handlerLog.times.size() == 1);
  CHECK(handlerLog.times[0] == fixture.log.times[0]);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("context.schedule() is the same Scheduler as the Engine's") {
  Fixture fixture;
  Scheduler* captured = nullptr;
  fixture.engine.addSystem(Phase::Update, "capture", captureScheduler, &captured);
  fixture.engine.frame(0.0);

  fixture.step();

  CHECK(captured == &fixture.engine.schedule());
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("destroying the Engine discards the pending tasks once") {
  int runs = 0;
  int discards = 0;

  {
    oly::ecs::World world;
    Engine engine(world);
    engine.schedule().after(10.0, countRun, Counters{&runs, &discards}, countDiscard);
    engine.schedule().every(1.0, countRun, Counters{&runs, &discards}, countDiscard);
  }

  CHECK(runs == 0);
  CHECK(discards == 2);
}

TEST_CASE("cancel and isPending with TaskHandle{} return false") {
  Fixture fixture;

  CHECK(!fixture.engine.schedule().cancel(TaskHandle{}));
  CHECK(!fixture.engine.schedule().isPending(TaskHandle{}));
}
