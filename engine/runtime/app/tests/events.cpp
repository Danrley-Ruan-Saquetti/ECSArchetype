#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"
#include "oly/app/handler.hpp"

using oly::app::Engine;
using oly::app::EngineConfig;
using oly::app::HandlerContext;
using oly::app::Phase;
using oly::app::SystemContext;

struct Ping {
  uint32_t value;
};
struct Echo {
  uint32_t depth;
};

OLY_HANDLER(appTestPingHandler, Ping) {
  (void)context;
  (void)events;
}

namespace {
  struct Recorder {
    std::vector<uint32_t> values;
    std::vector<uint32_t> depths;
    std::vector<std::string> trace;
    oly::ecs::Entity spawned;
    bool aliveInHandler = false;
    uint32_t spanSizeAfterEmit = 0;
    uint32_t firstValueAfterEmit = 0;
    int handlerCalls = 0;
    int systemCalls = 0;
  };

  struct Fixture {
    oly::ecs::World world;
    Engine engine;
    Recorder recorder;
    Log log;
    double now = 0.0;

    explicit Fixture(EngineConfig config = {})
      : engine(world, config) {
      engine.declareEvent<Ping>();
      engine.declareEvent<Echo>();
      engine.frame(0.0);
    }

    void step() {
      now += 1.0 / 60;
      engine.frame(now);
    }
  };

  void emitOneTwoThree(SystemContext& context) {
    context.emit(Ping{1});
    context.emit(Ping{2});
    context.emit(Ping{3});
  }

  void emitEchoZero(SystemContext& context) {
    context.emit(Echo{0});
  }

  void emitPingZero(SystemContext& context) {
    context.emit(Ping{0});
  }

  void recordPingValues(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());
    ++recorder.handlerCalls;

    for (const Ping& ping : context.events<Ping>()) {
      recorder.values.push_back(ping.value);
    }
  }

  void recordHandlerName(HandlerContext& context) {
    static_cast<Log*>(context.userData())->calls.push_back(std::string(context.name()));
  }

  void traceRoundsAndCascade(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());
    const auto pings = context.events<Ping>();
    recorder.trace.push_back("Ping:" + std::to_string(pings.size()) + ":" + std::to_string(pings[0].value));

    if (pings[0].value == 0) {
      context.emit(Echo{1});
      context.emit(Ping{99});
    }
  }

  void traceEcho(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());
    const auto echoes = context.events<Echo>();
    recorder.trace.push_back("Echo:" + std::to_string(echoes.size()) + ":" + std::to_string(echoes[0].depth));
  }

  void flood(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());
    const auto pings = context.events<Ping>();

    if (pings[0].value == 0) {
      for (int index = 0; index < 10000; ++index) {
        context.emit(Ping{1});
      }

      recorder.spanSizeAfterEmit = static_cast<uint32_t>(pings.size());
      recorder.firstValueAfterEmit = pings[0].value;
    }
  }

  void cascadeEcho(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());
    ++recorder.handlerCalls;

    for (const Echo& echo : context.events<Echo>()) {
      recorder.depths.push_back(echo.depth);
      if (echo.depth < 5) {
        context.emit(Echo{echo.depth + 1});
      }
    }
  }

  void cascadeEchoUntilTwo(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());

    for (const Echo& echo : context.events<Echo>()) {
      recorder.depths.push_back(echo.depth);
      if (echo.depth < 2) {
        context.emit(Echo{echo.depth + 1});
      }
    }
  }

  void spawnFromHandler(HandlerContext& context) {
    Recorder& recorder = *static_cast<Recorder*>(context.userData());
    recorder.spawned = context.spawn(Position{1, 2});
    recorder.aliveInHandler = context.isAlive(recorder.spawned);
  }
}

TEST_CASE("handler recebe o lote inteiro, na ordem de emissão") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "emit", emitOneTwoThree);
  fixture.engine.on<Ping>("pings", recordPingValues, &fixture.recorder);

  fixture.step();

  CHECK(fixture.recorder.values == std::vector<uint32_t>{1, 2, 3});
  CHECK(fixture.recorder.handlerCalls == 1);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("handlers do mesmo tipo na ordem de registro") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "emit", emitOneTwoThree);
  fixture.engine.on<Ping>("h1", recordHandlerName, &fixture.log);
  fixture.engine.on<Ping>("h2", recordHandlerName, &fixture.log);
  fixture.engine.on<Ping>("h3", recordHandlerName, &fixture.log);

  fixture.step();

  CHECK(fixture.log.calls == std::vector<std::string>{"h1", "h2", "h3"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("tipos despachados na ordem de declaração, não na de emissão") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "emit", [](SystemContext& context) {
    context.emit(Echo{0});
    context.emit(Ping{0});
  });
  fixture.engine.on<Echo>("echo", recordHandlerName, &fixture.log);
  fixture.engine.on<Ping>("ping", recordHandlerName, &fixture.log);

  fixture.step();

  CHECK(fixture.log.calls == std::vector<std::string>{"ping", "echo"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("evento emitido durante a rodada vai para a rodada seguinte") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "emit", emitPingZero);
  fixture.engine.on<Ping>("ping", traceRoundsAndCascade, &fixture.recorder);
  fixture.engine.on<Echo>("echo", traceEcho, &fixture.recorder);

  fixture.step();

  CHECK(fixture.recorder.trace == std::vector<std::string>{"Ping:1:0", "Ping:1:99", "Echo:1:1"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("lote não muda quando o handler emite muitos do próprio tipo") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "emit", emitPingZero);
  fixture.engine.on<Ping>("flood", flood, &fixture.recorder);

  fixture.step();

  CHECK(fixture.recorder.spanSizeAfterEmit == 1);
  CHECK(fixture.recorder.firstValueAfterEmit == 0);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("cascata termina no mesmo passo quando as filas esvaziam") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "emit", emitEchoZero);
  fixture.engine.on<Echo>("cascade", cascadeEcho, &fixture.recorder);

  fixture.step();

  CHECK(fixture.recorder.depths == std::vector<uint32_t>{0, 1, 2, 3, 4, 5});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("handler faz mudança estrutural direta") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "emit", emitPingZero);
  fixture.engine.on<Ping>("spawn", spawnFromHandler, &fixture.recorder);

  fixture.step();

  CHECK(fixture.recorder.aliveInHandler);
  CHECK(fixture.world.isAlive(fixture.recorder.spawned));
  CHECK(fixture.world.get<Position>(fixture.recorder.spawned).x == 1);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("evento sem handler é descartado no fim do passo") {
  Fixture fixture;
  fixture.engine.emit(Ping{1});
  fixture.step();

  fixture.engine.on<Ping>("pings", recordPingValues, &fixture.recorder);
  fixture.step();

  CHECK(fixture.recorder.handlerCalls == 0);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("OLY_HANDLER registra por nome com o tipo") {
  CHECK(oly::app::findNamedHandler("appTestPingHandler") != nullptr);
  CHECK(oly::app::findNamedHandler("appTestPingHandler")->eventKey == oly::app::eventKey<Ping>());
  CHECK(oly::app::findNamedHandler("naoExiste") == nullptr);
}

TEST_CASE("limite de rodadas configurável: exatamente maxEventRounds rodadas é permitido") {
  EngineConfig config;
  config.maxEventRounds = 3;
  Fixture fixture(config);
  fixture.engine.addSystem(Phase::Update, "emit", emitEchoZero);
  fixture.engine.on<Echo>("cascade", cascadeEchoUntilTwo, &fixture.recorder);

  fixture.step();

  CHECK(fixture.recorder.depths == std::vector<uint32_t>{0, 1, 2});
  CHECK_INVARIANTS(fixture.world);
}
