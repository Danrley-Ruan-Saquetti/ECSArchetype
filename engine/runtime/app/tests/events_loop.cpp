#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"
#include "oly/app/handler.hpp"

using oly::app::Engine;
using oly::app::HandlerContext;
using oly::app::Phase;
using oly::app::SystemContext;

struct Health {
  float value;
};
struct Enemy {};
struct Coin {};
struct Ping {
  uint32_t value;
};
struct EnemyDied {
  oly::ecs::Entity entity;
  float x, y;
};
struct CoinDropped {
  oly::ecs::Entity coin;
};

namespace {
  struct Recorder {
    std::vector<std::string> trace;
    std::vector<uint32_t> batchSizes;
    std::vector<uint32_t> pings;
    std::vector<double> systemTimes;
    std::vector<double> handlerTimes;
    uint32_t score = 0;
    bool aliveWhenScored = true;
    bool aliveInNextRound = true;
    int deathsEmittedPerStep = 1;
  };

  struct Fixture {
    oly::ecs::World world;
    Engine engine{world};
    Recorder recorder;
    Log log;
    double now = 0.0;

    Fixture() {
      engine.declareEvent<EnemyDied>();
      engine.declareEvent<CoinDropped>();
      engine.declareEvent<Ping>();
    }

    void start() {
      engine.frame(0.0);
    }

    void step() {
      now += 1.0 / 60;
      engine.frame(now);
    }
  };

  void damage(SystemContext& context) {
    const int deaths = static_cast<Recorder*>(context.userData())->deathsEmittedPerStep;

    context.query<Health, const Position>(oly::ecs::With<Enemy>{}).each([&](oly::ecs::Entity entity, Health& health, const Position& position) {
      health.value -= 10;
      if (health.value <= 0) {
        for (int index = 0; index < deaths; ++index) {
          context.emit(EnemyDied{entity, position.x, position.y});
        }
      }
    });
  }

  void dropCoin(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());
    recorder.trace.push_back("dropCoin");
    recorder.batchSizes.push_back(context.eventCount());

    for (const EnemyDied& died : context.events<EnemyDied>()) {
      const oly::ecs::Entity coin = context.spawn(Position{died.x, died.y}, Coin{});
      context.emit(CoinDropped{coin});
    }
  }

  void addScore(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());
    recorder.trace.push_back("addScore");

    for (const EnemyDied& died : context.events<EnemyDied>()) {
      recorder.aliveWhenScored = recorder.aliveWhenScored && context.isAlive(died.entity) && context.tryGet<Position>(died.entity) != nullptr;
      ++recorder.score;
    }
  }

  void destroyDead(HandlerContext& context) {
    static_cast<Recorder*>(context.userData())->trace.push_back("destroyDead");

    for (const EnemyDied& died : context.events<EnemyDied>()) {
      if (context.isAlive(died.entity)) {
        context.destroy(died.entity);
      }
    }
  }

  void countCoin(HandlerContext& context) {
    static_cast<Recorder*>(context.userData())->trace.push_back("countCoin");
  }

  void destroyThenAnnounce(HandlerContext& context) {
    for (const EnemyDied& died : context.events<EnemyDied>()) {
      context.destroy(died.entity);
      context.emit(CoinDropped{died.entity});
    }
  }

  void observeDestroyed(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());

    for (const CoinDropped& dropped : context.events<CoinDropped>()) {
      recorder.aliveInNextRound = context.isAlive(dropped.coin);
    }
  }

  void emitPingWhilePaused(SystemContext& context) {
    context.emit(Ping{1});
  }

  void recordPingValues(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());

    for (const Ping& ping : context.events<Ping>()) {
      recorder.pings.push_back(ping.value);
    }
  }

  void recordPingInLog(HandlerContext& context) {
    static_cast<Log*>(context.userData())->calls.push_back("ping");
  }

  void renderEmitsOnce(SystemContext& context) {
    Log& log = *static_cast<Log*>(context.userData());

    if (log.calls.empty()) {
      context.emit(Ping{1});
    }
    log.calls.push_back("render");
  }

  void emitPingEveryStep(SystemContext& context) {
    static_cast<Recorder*>(context.userData())->systemTimes.push_back(context.time());
    context.emit(Ping{1});
  }

  void recordHandlerTime(HandlerContext& context) {
    static_cast<Recorder*>(context.userData())->handlerTimes.push_back(context.time());
  }

  oly::ecs::Entity spawnEnemy(oly::ecs::World& world) {
    return world.spawn(Health{10}, Position{3, 4}, Enemy{});
  }

  void registerReferenceFlow(Fixture& fixture) {
    fixture.engine.addSystem(Phase::UpdateAfter, "damage", damage, &fixture.recorder);
    fixture.engine.on<EnemyDied>("dropCoin", dropCoin, &fixture.recorder);
    fixture.engine.on<EnemyDied>("addScore", addScore, &fixture.recorder);
    fixture.engine.on<EnemyDied>("destroyDead", destroyDead, &fixture.recorder);
    fixture.engine.on<CoinDropped>("countCoin", countCoin, &fixture.recorder);
  }
}

TEST_CASE("reference flow: damage, coin, score and destruction at the end of dispatch") {
  Fixture fixture;
  registerReferenceFlow(fixture);
  const oly::ecs::Entity enemy = spawnEnemy(fixture.world);
  fixture.start();

  fixture.step();

  CHECK(!fixture.world.isAlive(enemy));
  CHECK(fixture.recorder.score == 1);
  CHECK(fixture.recorder.aliveWhenScored);
  CHECK(fixture.recorder.trace == std::vector<std::string>{"dropCoin", "addScore", "destroyDead", "countCoin"});

  uint32_t coins = 0;
  fixture.world.query<const Position>(oly::ecs::With<Coin>{}).each([&](const Position& position) {
    CHECK(position.x == 3);
    CHECK(position.y == 4);
    ++coins;
  });
  CHECK(coins == 1);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("repeated event: two deaths of the same enemy in the same batch") {
  Fixture fixture;
  fixture.recorder.deathsEmittedPerStep = 2;
  registerReferenceFlow(fixture);
  const oly::ecs::Entity enemy = spawnEnemy(fixture.world);
  fixture.start();

  fixture.step();

  CHECK(fixture.recorder.batchSizes == std::vector<uint32_t>{2});
  CHECK(!fixture.world.isAlive(enemy));
  CHECK(fixture.recorder.score == 2);

  uint32_t coins = 0;
  fixture.world.query<const Position>(oly::ecs::With<Coin>{}).each([&](const Position&) { ++coins; });
  CHECK(coins == 2);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("entity destroyed in an earlier round arrives dead in the next round") {
  Fixture fixture;
  fixture.engine.on<EnemyDied>("destroy", destroyThenAnnounce, &fixture.recorder);
  fixture.engine.on<CoinDropped>("observe", observeDestroyed, &fixture.recorder);
  const oly::ecs::Entity entity = fixture.world.spawn(Position{0, 0});
  fixture.start();
  fixture.engine.emit(EnemyDied{entity, 0, 0});

  fixture.step();

  CHECK(!fixture.recorder.aliveInNextRound);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("handlers run in a paused step") {
  Fixture fixture;
  const auto system = fixture.engine.addSystem(Phase::Update, "emit", emitPingWhilePaused);
  fixture.engine.setWhilePaused(system, true);
  fixture.engine.on<Ping>("pings", recordPingValues, &fixture.recorder);
  fixture.start();
  fixture.engine.pause();

  fixture.step();

  CHECK(fixture.recorder.pings == std::vector<uint32_t>{1});
  CHECK(fixture.engine.time() == 0.0);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("event emitted in Render is dispatched in the next step") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Render, "render", renderEmitsOnce, &fixture.log);
  fixture.engine.on<Ping>("ping", recordPingInLog, &fixture.log);
  fixture.start();

  fixture.step();
  CHECK(fixture.log.calls == std::vector<std::string>{"render"});

  fixture.step();
  CHECK(fixture.log.calls == std::vector<std::string>{"render", "ping", "render"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("event emitted outside frame goes to the next step") {
  Fixture fixture;
  fixture.engine.on<Ping>("pings", recordPingValues, &fixture.recorder);
  fixture.engine.emit(Ping{7});

  fixture.engine.frame(0.0);
  CHECK(fixture.recorder.pings.empty());

  fixture.engine.frame(1.0 / 60);
  CHECK(fixture.recorder.pings == std::vector<uint32_t>{7});

  fixture.engine.frame(2.0 / 60);
  CHECK(fixture.recorder.pings == std::vector<uint32_t>{7});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("handler sees the time() of the step start, same as the systems") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "emit", emitPingEveryStep, &fixture.recorder);
  fixture.engine.on<Ping>("time", recordHandlerTime, &fixture.recorder);
  fixture.start();

  fixture.step();
  fixture.step();

  CHECK(fixture.recorder.systemTimes == std::vector<double>{0.0, 1.0 / 60});
  CHECK(fixture.recorder.handlerTimes == fixture.recorder.systemTimes);
  CHECK_INVARIANTS(fixture.world);
}
