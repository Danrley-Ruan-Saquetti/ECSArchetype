#include <doctest/doctest.h>

#include <cstdint>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/detail/state_scoped.hpp"
#include "oly/app/engine.hpp"
#include "oly/app/state_context.hpp"
#include "oly/app/task_context.hpp"
#include "oly/ecs/diagnostics.hpp"

using oly::app::Engine;
using oly::app::HandlerContext;
using oly::app::Phase;
using oly::app::Scheduler;
using oly::app::StateContext;
using oly::app::StateId;
using oly::app::SystemContext;
using oly::app::TaskContext;
using oly::app::TaskHandle;
using oly::app::detail::StateScoped;
using oly::ecs::Entity;

namespace {
  struct Ping {
    uint32_t value;
  };

  struct Cited {
    Entity entity;
  };

  struct Created {
    Entity fromSystem;
    Entity fromHandler;
    Entity fromTask;
    Entity fromHook;
    bool armed = false;
  };

  struct Counter {
    int* count;
  };

  struct DiscardingEngine {
    Engine* engine;
    int* count;
  };

  struct Fixture {
    oly::ecs::World world;
    Engine engine{world};
    StateId menu = engine.declareState("menu");
    StateId playing = engine.declareState("playing");
    StateId gameOver = engine.declareState("gameOver");
    double now = 0.0;

    Fixture() {
      engine.frame(0.0);
    }

    void step() {
      now += 1.0 / 60;
      engine.frame(now);
    }

    void switchTo(StateId state) {
      engine.requestState(state);
      step();
    }
  };

  void noopTask(TaskContext&) {}

  void countDiscard(void* payload) {
    ++*static_cast<Counter*>(payload)->count;
  }

  void discardAndSchedule(void* payload) {
    const DiscardingEngine& discarding = *static_cast<DiscardingEngine*>(payload);
    ++*discarding.count;
    discarding.engine->schedule().after(10.0, noopTask, Counter{discarding.count}, countDiscard);
  }

  void spawnFromSystem(SystemContext& context) {
    Created& created = *static_cast<Created*>(context.userData());
    if (created.armed) {
      created.armed = false;
      created.fromSystem = context.spawn();
    }
  }

  void spawnFromHandler(HandlerContext& context) {
    static_cast<Created*>(context.userData())->fromHandler = context.spawn();
  }

  void spawnFromTask(TaskContext& context) {
    *context.payload<Entity*>() = context.spawn();
  }

  void spawnWithComponentFromTask(TaskContext& context) {
    *context.payload<Entity*>() = context.spawn(Position{1.0f, 2.0f});
  }

  void spawnFromHook(StateContext& context) {
    static_cast<Created*>(context.userData())->fromHook = context.spawn();
  }

  void spawnPersistFromTask(TaskContext& context) {
    const Entity entity = context.spawn();
    context.persist(entity);
    context.persist(entity);
    *context.payload<Entity*>() = entity;
  }

  struct SpawnAndPersist {
    Entity entity;
    StateId target;
    bool armed = false;
  };

  void spawnPersistAndRequestFromSystem(SystemContext& context) {
    SpawnAndPersist& state = *static_cast<SpawnAndPersist*>(context.userData());
    if (!state.armed) {
      return;
    }

    state.armed = false;
    state.entity = context.spawn();
    context.persist(state.entity);
    context.requestState(state.target);
  }

  struct ExitEnter {
    Entity fromExit;
    Entity fromEnter;
    int exitDiscards = 0;
    int enterDiscards = 0;
  };

  void spawnAndScheduleOnExit(StateContext& context) {
    ExitEnter& shared = *static_cast<ExitEnter*>(context.userData());
    shared.fromExit = context.spawn();
    context.schedule().after(10.0, noopTask, Counter{&shared.exitDiscards}, countDiscard);
  }

  void spawnAndScheduleOnEnter(StateContext& context) {
    ExitEnter& shared = *static_cast<ExitEnter*>(context.userData());
    shared.fromEnter = context.spawn();
    context.schedule().after(10.0, noopTask, Counter{&shared.enterDiscards}, countDiscard);
  }

  struct CitationProbe {
    Entity cited;
    bool sawDead = false;
    bool received = false;
  };

  void emitCitationOnExit(StateContext& context) {
    context.emit(Cited{static_cast<CitationProbe*>(context.userData())->cited});
  }

  void recordCitation(HandlerContext& context) {
    CitationProbe& probe = *static_cast<CitationProbe*>(context.userData());
    probe.received = true;
    probe.sawDead = !context.isAlive(context.events<Cited>()[0].entity);
  }

  void spawnThreeAndRepeat(StateContext& context) {
    for (int index = 0; index < 3; ++index) {
      context.spawn(Position{});
    }

    context.schedule().every(1.0, noopTask);
  }
}

TEST_CASE("entities created by a system, handler, task and hook with an active state are destroyed on the change") {
  Fixture fixture;
  Created created;
  Entity fromTask{};
  fixture.engine.declareEvent<Ping>("Ping");
  fixture.engine.addSystem(Phase::Update, "spawner", spawnFromSystem, &created);
  fixture.engine.on<Ping>("spawner", spawnFromHandler, &created);
  fixture.engine.onEnter(fixture.menu, "spawner", spawnFromHook, &created);
  fixture.switchTo(fixture.menu);
  created.armed = true;
  fixture.engine.emit(Ping{1});
  fixture.engine.schedule().after(0.0, spawnFromTask, &fromTask);

  fixture.step();

  CHECK(fixture.world.isAlive(created.fromSystem));
  CHECK(fixture.world.isAlive(created.fromHandler));
  CHECK(fixture.world.isAlive(fromTask));
  CHECK(fixture.world.isAlive(created.fromHook));

  fixture.switchTo(fixture.playing);

  CHECK(!fixture.world.isAlive(created.fromSystem));
  CHECK(!fixture.world.isAlive(created.fromHandler));
  CHECK(!fixture.world.isAlive(fromTask));
  CHECK(!fixture.world.isAlive(created.fromHook));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("direct spawn on the World is persistent") {
  Fixture fixture;
  fixture.switchTo(fixture.menu);
  const Entity setup = fixture.world.spawn(Position{});

  fixture.switchTo(fixture.playing);

  CHECK(fixture.world.isAlive(setup));
  CHECK(!fixture.world.has<StateScoped>(setup));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("with no active state no entity receives the tag") {
  Fixture fixture;
  Entity entity{};
  fixture.engine.schedule().after(0.0, spawnFromTask, &entity);

  fixture.step();

  CHECK(fixture.world.isAlive(entity));
  CHECK(!fixture.world.has<StateScoped>(entity));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("spawn with components in a direct context gets the tag in a single transition") {
  Fixture fixture;
  Entity entity{};
  fixture.switchTo(fixture.menu);
  fixture.engine.schedule().after(0.0, spawnWithComponentFromTask, &entity);

  fixture.step();

  CHECK(fixture.world.has<StateScoped>(entity));
  CHECK(fixture.world.has<Position>(entity));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("what onExit creates is cleared on the same change; what onEnter creates stays until the next change") {
  Fixture fixture;
  ExitEnter shared;
  fixture.engine.onExit(fixture.menu, "exit", spawnAndScheduleOnExit, &shared);
  fixture.engine.onEnter(fixture.playing, "enter", spawnAndScheduleOnEnter, &shared);
  fixture.switchTo(fixture.menu);

  fixture.switchTo(fixture.playing);

  CHECK(!fixture.world.isAlive(shared.fromExit));
  CHECK(shared.exitDiscards == 1);
  CHECK(fixture.world.isAlive(shared.fromEnter));
  CHECK(shared.enterDiscards == 0);

  fixture.switchTo(fixture.gameOver);

  CHECK(!fixture.world.isAlive(shared.fromEnter));
  CHECK(shared.enterDiscards == 1);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("persist in a system in the step of the change: the tag's add and remove cancel out and the entity survives") {
  Fixture fixture;
  SpawnAndPersist state;
  state.target = fixture.playing;
  fixture.engine.addSystem(Phase::Update, "spawner", spawnPersistAndRequestFromSystem, &state);
  fixture.switchTo(fixture.menu);
  state.armed = true;

  fixture.step();

  CHECK(fixture.engine.state() == fixture.playing);
  CHECK(fixture.world.isAlive(state.entity));
  CHECK(!fixture.world.has<StateScoped>(state.entity));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("persist in a direct context removes the tag; without the tag it does nothing") {
  Fixture fixture;
  Entity entity{};
  fixture.switchTo(fixture.menu);
  fixture.engine.schedule().after(0.0, spawnPersistFromTask, &entity);

  fixture.step();

  CHECK(!fixture.world.has<StateScoped>(entity));

  fixture.switchTo(fixture.playing);

  CHECK(fixture.world.isAlive(entity));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("scoped tasks are cancelled with discard once; persist keeps them; not pending returns false") {
  Fixture fixture;
  int discards = 0;
  fixture.switchTo(fixture.menu);
  Scheduler& schedule = fixture.engine.schedule();
  schedule.after(10.0, noopTask, Counter{&discards}, countDiscard);
  const TaskHandle persisted = schedule.after(10.0, noopTask, Counter{&discards}, countDiscard);
  const TaskHandle finished = schedule.after(0.0, noopTask);
  fixture.step();

  const bool persistedPending = schedule.persist(persisted);
  const bool finishedPending = schedule.persist(finished);
  fixture.switchTo(fixture.playing);

  CHECK(persistedPending);
  CHECK(!finishedPending);
  CHECK(discards == 1);
  CHECK(schedule.isPending(persisted));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("task scheduled outside frame with an active state is scoped") {
  Fixture fixture;
  fixture.switchTo(fixture.menu);

  const TaskHandle task = fixture.engine.schedule().after(10.0, noopTask);
  fixture.switchTo(fixture.playing);

  CHECK(!fixture.engine.schedule().isPending(task));
}

TEST_CASE("discard that schedules during the cleanup is also cancelled") {
  Fixture fixture;
  int discards = 0;
  fixture.switchTo(fixture.menu);
  fixture.engine.schedule().after(10.0, noopTask, DiscardingEngine{&fixture.engine, &discards}, discardAndSchedule);

  fixture.switchTo(fixture.playing);

  CHECK(discards == 2);
  CHECK(fixture.engine.schedule().pendingCount() == 0);
}

TEST_CASE("event emitted in onExit about a scoped entity arrives with the entity already destroyed") {
  Fixture fixture;
  CitationProbe probe;
  fixture.engine.declareEvent<Cited>("Cited");
  fixture.engine.on<Cited>("recorder", recordCitation, &probe);
  fixture.engine.onExit(fixture.menu, "emitter", emitCitationOnExit, &probe);
  fixture.switchTo(fixture.menu);
  fixture.engine.schedule().after(0.0, spawnFromTask, &probe.cited);
  fixture.step();

  fixture.switchTo(fixture.playing);

  CHECK(probe.received);
  CHECK(probe.sawDead);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("playing → gameOver → playing with no leftovers in the World") {
  Fixture fixture;
  fixture.engine.onEnter(fixture.playing, "populate", spawnThreeAndRepeat);
  fixture.world.spawn(Position{});
  fixture.world.spawn(Position{});
  fixture.switchTo(fixture.menu);

  fixture.switchTo(fixture.playing);
  fixture.switchTo(fixture.gameOver);
  fixture.switchTo(fixture.playing);

  CHECK(oly::ecs::stats(fixture.world).liveEntities == 3 + 2);
  CHECK(fixture.engine.schedule().pendingCount() == 1);
  CHECK_INVARIANTS(fixture.world);
}
