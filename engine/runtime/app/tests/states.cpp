#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "components.hpp"
#include "log.hpp"
#include "oly/app/engine.hpp"
#include "oly/app/state_context.hpp"
#include "oly/app/task_context.hpp"

using oly::app::Engine;
using oly::app::EngineConfig;
using oly::app::HandlerContext;
using oly::app::NoState;
using oly::app::Phase;
using oly::app::StateContext;
using oly::app::StateId;
using oly::app::SystemContext;
using oly::app::TaskContext;

namespace {
  struct Ping {
    uint32_t value;
  };

  struct Probe {
    Log log;
    std::vector<StateId> froms;
    std::vector<StateId> tos;
    std::vector<StateId> seen;
    StateId next = NoState;
    StateId requestWhen = NoState;
  };

  void recordHook(StateContext& context) {
    Probe& probe = *static_cast<Probe*>(context.userData());
    probe.log.calls.push_back(std::string(context.entering() ? "enter:" : "exit:") + std::string(context.name()));
    probe.froms.push_back(context.fromState());
    probe.tos.push_back(context.toState());
  }

  void requestNextHook(StateContext& context) {
    context.requestState(static_cast<Probe*>(context.userData())->next);
  }

  void emitPingHook(StateContext& context) {
    context.emit(Ping{1});
  }

  void emitPingSystem(SystemContext& context) {
    context.emit(Ping{1});
  }

  void recordHandlerTime(HandlerContext& context) {
    static_cast<Log*>(context.userData())->times.push_back(context.time());
  }

  void countPings(HandlerContext& context) {
    static_cast<Log*>(context.userData())->calls.push_back("handler");
  }

  void requestWhenSeen(SystemContext& context) {
    Probe& probe = *static_cast<Probe*>(context.userData());
    probe.seen.push_back(context.state());
    if (context.state() == probe.requestWhen) {
      context.requestState(probe.next);
    }
  }

  template <class Context>
  void seeAndRequest(Context& context) {
    Probe& probe = *static_cast<Probe*>(context.userData());
    probe.seen.push_back(context.state());
    context.requestState(probe.next);
  }

  void seeAndRequestTask(TaskContext& context) {
    Probe& probe = *context.payload<Probe*>();
    probe.seen.push_back(context.state());
    context.requestState(probe.next);
  }

  struct Fixture {
    oly::ecs::World world;
    Engine engine;
    Probe probe;
    StateId menu;
    StateId playing;
    StateId gameOver;
    double now = 0.0;

    explicit Fixture(EngineConfig config = {})
      : engine(world, config),
        menu(engine.declareState("menu")),
        playing(engine.declareState("playing")),
        gameOver(engine.declareState("gameOver")) {}

    void start() {
      engine.frame(0.0);
    }

    void step() {
      now += 1.0 / 60;
      engine.frame(now);
    }

    void enter(StateId state) {
      engine.requestState(state);
      step();
      probe = Probe{};
    }
  };
}

OLY_STATE_HOOK(namedEnter) {}

TEST_CASE("estado inicial entra no passo 5 do primeiro passo; sistemas inState só rodam a partir do seguinte") {
  Fixture fixture;
  fixture.engine.onEnter(fixture.menu, "menu", recordHook, &fixture.probe);
  const auto system = fixture.engine.addSystem(Phase::Update, "menuSystem", recordName, &fixture.probe.log);
  fixture.engine.setSystemStates(system, {fixture.menu});
  fixture.engine.requestState(fixture.menu);
  fixture.start();

  fixture.step();

  CHECK(fixture.probe.log.calls == std::vector<std::string>{"enter:menu"});
  CHECK(fixture.probe.froms == std::vector<StateId>{NoState});
  CHECK(fixture.probe.tos == std::vector<StateId>{fixture.menu});

  fixture.step();

  CHECK(fixture.probe.log.count("menuSystem") == 1);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("state() só muda no passo 5") {
  Fixture fixture;
  fixture.engine.addSystem(Phase::Update, "requester", requestWhenSeen, &fixture.probe);
  fixture.probe.requestWhen = fixture.playing;
  fixture.probe.next = fixture.gameOver;
  fixture.start();
  fixture.engine.requestState(fixture.playing);
  fixture.step();
  fixture.probe.seen.clear();

  fixture.step();

  CHECK(fixture.probe.seen == std::vector<StateId>{fixture.playing});
  CHECK(fixture.engine.state() == fixture.gameOver);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("onExit antes de onEnter, cada um na ordem de registro, com fromState e toState") {
  Fixture fixture;
  fixture.engine.onEnter(fixture.playing, "b1", recordHook, &fixture.probe);
  fixture.engine.onExit(fixture.menu, "a1", recordHook, &fixture.probe);
  fixture.engine.onEnter(fixture.playing, "b2", recordHook, &fixture.probe);
  fixture.engine.onExit(fixture.menu, "a2", recordHook, &fixture.probe);
  fixture.start();
  fixture.enter(fixture.menu);

  fixture.engine.requestState(fixture.playing);
  fixture.step();

  CHECK(fixture.probe.log.calls == std::vector<std::string>{"exit:a1", "exit:a2", "enter:b1", "enter:b2"});
  CHECK(fixture.probe.froms == std::vector<StateId>(4, fixture.menu));
  CHECK(fixture.probe.tos == std::vector<StateId>(4, fixture.playing));
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("dois pedidos no mesmo passo: vale o último") {
  Fixture fixture;
  fixture.engine.onEnter(fixture.menu, "menu", recordHook, &fixture.probe);
  fixture.start();

  fixture.engine.requestState(fixture.menu);
  fixture.engine.requestState(fixture.playing);
  fixture.step();

  CHECK(fixture.engine.state() == fixture.playing);
  CHECK(fixture.probe.log.calls.empty());
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("reentrada roda onExit e onEnter do mesmo estado") {
  Fixture fixture;
  fixture.engine.onEnter(fixture.playing, "playing", recordHook, &fixture.probe);
  fixture.engine.onExit(fixture.playing, "playing", recordHook, &fixture.probe);
  fixture.start();
  fixture.enter(fixture.playing);

  fixture.engine.requestState(fixture.playing);
  fixture.step();

  CHECK(fixture.probe.log.calls == std::vector<std::string>{"exit:playing", "enter:playing"});
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("pedido feito num gancho vale no passo seguinte") {
  Fixture fixture;
  fixture.probe.next = fixture.gameOver;
  fixture.engine.onEnter(fixture.playing, "playing", requestNextHook, &fixture.probe);
  fixture.start();
  fixture.engine.requestState(fixture.playing);

  fixture.step();

  CHECK(fixture.engine.state() == fixture.playing);

  fixture.step();

  CHECK(fixture.engine.state() == fixture.gameOver);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("troca em passo pausado acontece e não mexe na pausa") {
  Fixture fixture;
  fixture.start();
  fixture.engine.pause();
  fixture.engine.requestState(fixture.playing);

  fixture.step();

  CHECK(fixture.engine.state() == fixture.playing);
  CHECK(fixture.engine.paused());
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("inState combina com whilePaused") {
  Fixture fixture;
  const auto system = fixture.engine.addSystem(Phase::Update, "pausedMenu", recordName, &fixture.probe.log);
  fixture.engine.setWhilePaused(system, true);
  fixture.engine.setSystemStates(system, {fixture.menu});
  fixture.start();
  fixture.enter(fixture.menu);

  fixture.step();

  CHECK(fixture.probe.log.count("pausedMenu") == 0);

  fixture.engine.pause();
  fixture.step();

  CHECK(fixture.probe.log.count("pausedMenu") == 1);

  fixture.engine.requestState(fixture.playing);
  fixture.step();

  CHECK(fixture.probe.log.count("pausedMenu") == 2);

  fixture.step();

  CHECK(fixture.probe.log.count("pausedMenu") == 2);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("inState vale na fase render") {
  Fixture fixture;
  const auto inMenu = fixture.engine.addSystem(Phase::Render, "menuRender", recordName, &fixture.probe.log);
  const auto inPlaying = fixture.engine.addSystem(Phase::Render, "playingRender", recordName, &fixture.probe.log);
  fixture.engine.setSystemStates(inMenu, {fixture.menu});
  fixture.engine.setSystemStates(inPlaying, {fixture.playing});
  fixture.engine.requestState(fixture.menu);
  fixture.start();

  fixture.step();

  CHECK(fixture.probe.log.count("menuRender") == 1);
  CHECK(fixture.probe.log.count("playingRender") == 0);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("lista vazia tira a restrição") {
  Fixture fixture;
  const auto system = fixture.engine.addSystem(Phase::Update, "free", recordName, &fixture.probe.log);
  fixture.engine.setSystemStates(system, {fixture.menu});
  fixture.engine.setSystemStates(system, {});
  fixture.start();

  fixture.step();

  CHECK(fixture.probe.log.count("free") == 1);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("handler com inState: confere o estado em cada rodada e no despacho do item 6 vale o novo") {
  Fixture fixture;
  Log inPlayingLog;
  Log inMenuLog;
  fixture.engine.declareEvent<Ping>("Ping");
  const auto inPlaying = fixture.engine.on<Ping>("inPlaying", recordHandlerTime, &inPlayingLog);
  const auto inMenu = fixture.engine.on<Ping>("inMenu", recordHandlerTime, &inMenuLog);
  fixture.engine.setHandlerStates(inPlaying, {fixture.playing});
  fixture.engine.setHandlerStates(inMenu, {fixture.menu});
  fixture.engine.onEnter(fixture.playing, "emit", emitPingHook);
  fixture.engine.requestState(fixture.playing);
  fixture.start();

  fixture.step();

  CHECK(inPlayingLog.times == std::vector<double>{0.0});
  CHECK(inMenuLog.times.empty());
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("evento emitido em gancho é despachado no mesmo passo e conta no limite de rodadas") {
  Fixture fixture(EngineConfig{.maxEventRounds = 2});
  fixture.engine.declareEvent<Ping>("Ping");
  fixture.engine.on<Ping>("counter", countPings, &fixture.probe.log);
  fixture.engine.addSystem(Phase::Update, "emitter", emitPingSystem);
  fixture.engine.onEnter(fixture.playing, "emit", emitPingHook);
  fixture.engine.requestState(fixture.playing);
  fixture.start();

  fixture.step();

  CHECK(fixture.probe.log.count("handler") == 2);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("pedido com a aba oculta espera o primeiro passo depois de voltar") {
  Fixture fixture;
  fixture.start();
  fixture.engine.setHidden(true);
  fixture.engine.requestState(fixture.menu);

  fixture.engine.frame(1.0);
  fixture.engine.frame(2.0);
  fixture.engine.setHidden(false);
  fixture.engine.frame(3.0);

  CHECK(fixture.engine.state() == NoState);

  fixture.engine.frame(3.0 + 1.0 / 60);

  CHECK(fixture.engine.state() == fixture.menu);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("requestState e state pelo contexto de sistema") {
  Fixture fixture;
  fixture.probe.next = fixture.playing;
  fixture.engine.addSystem(Phase::Update, "requester", seeAndRequest<SystemContext>, &fixture.probe);
  fixture.start();

  fixture.step();

  CHECK(fixture.probe.seen.front() == NoState);
  CHECK(fixture.engine.state() == fixture.playing);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("requestState e state pelo contexto de handler") {
  Fixture fixture;
  fixture.probe.next = fixture.playing;
  fixture.engine.declareEvent<Ping>("Ping");
  fixture.engine.on<Ping>("requester", seeAndRequest<HandlerContext>, &fixture.probe);
  fixture.start();
  fixture.engine.emit(Ping{1});

  fixture.step();

  CHECK(fixture.probe.seen == std::vector<StateId>{NoState});
  CHECK(fixture.engine.state() == fixture.playing);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("requestState e state pelo contexto de tarefa") {
  Fixture fixture;
  Probe* probe = &fixture.probe;
  probe->next = fixture.playing;
  fixture.start();
  fixture.engine.schedule().after(0.0, seeAndRequestTask, probe);

  fixture.step();

  CHECK(fixture.probe.seen == std::vector<StateId>{NoState});
  CHECK(fixture.engine.state() == fixture.playing);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("requestState e state pelo contexto de gancho") {
  Fixture fixture;
  fixture.probe.next = fixture.playing;
  fixture.engine.onEnter(fixture.menu, "requester", seeAndRequest<StateContext>, &fixture.probe);
  fixture.engine.requestState(fixture.menu);
  fixture.start();

  fixture.step();

  CHECK(fixture.probe.seen == std::vector<StateId>{fixture.menu});

  fixture.step();

  CHECK(fixture.engine.state() == fixture.playing);
  CHECK_INVARIANTS(fixture.world);
}

TEST_CASE("OLY_STATE_HOOK registra por nome") {
  CHECK(oly::app::findNamedStateHook("namedEnter") == &namedEnter);
  CHECK(oly::app::findNamedStateHook("nenhum") == nullptr);
}

TEST_CASE("inState com o estado de índice 31 usa o bit mais alto da máscara") {
  oly::ecs::World world;
  Engine engine(world);
  Probe probe;
  StateId last = NoState;
  for (uint32_t index = 0; index < oly::app::MaxStates; ++index) {
    last = engine.declareState("state" + std::to_string(index));
  }
  const auto system = engine.addSystem(Phase::Update, "lastOnly", recordName, &probe.log);
  engine.setSystemStates(system, {last});
  engine.frame(0.0);

  engine.requestState(0);
  engine.frame(1.0 / 60);
  engine.frame(2.0 / 60);

  CHECK(probe.log.count("lastOnly") == 0);

  engine.requestState(last);
  engine.frame(3.0 / 60);
  engine.frame(4.0 / 60);

  CHECK(last == 31);
  CHECK(probe.log.count("lastOnly") == 1);
  CHECK_INVARIANTS(world);
}
