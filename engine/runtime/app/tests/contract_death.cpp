// Cada caso viola um contrato do app e precisa disparar OLY_ASSERT com as substrings exigidas; só é compilado com
// asserts ligados. Mesmo protocolo do contract_death do ecs.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "oly/app/engine.hpp"
#include "oly/app/handler.hpp"
#include "oly/app/task_context.hpp"
#include "oly/core/assert.hpp"

namespace {
  using oly::app::Engine;
  using oly::app::HandlerContext;
  using oly::app::Phase;
  using oly::app::SystemContext;
  using oly::app::TaskContext;
  using oly::app::TaskHandle;

  const std::vector<const char*>* g_required = nullptr;

  void handler(const char* message) noexcept {
    std::fprintf(stderr, "%s\n", message);

    if (g_required) {
      for (const char* needle : *g_required) {
        if (!std::strstr(message, needle)) {
          std::fprintf(stderr, "mensagem sem a substring exigida: \"%s\"\n", needle);
          std::fflush(stderr);
          std::_Exit(2);
        }
      }
    }

    std::fflush(stderr);
    std::_Exit(0);
  }

  void addSystemFromSystem(SystemContext& context) {
    Engine& engine = *static_cast<Engine*>(context.userData());
    engine.addSystem(Phase::Update, "outro", [](SystemContext&) {});
  }

  void addSystemDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.addSystem(Phase::Update, "adder", addSystemFromSystem, &engine);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void whilePausedRender() {
    oly::ecs::World world;
    Engine engine(world);
    const auto system = engine.addSystem(Phase::Render, "desenho", [](SystemContext&) {});

    engine.setWhilePaused(system, true);
  }

  void frameTimeBackwards() {
    oly::ecs::World world;
    Engine engine(world);

    engine.frame(1.0);
    engine.frame(0.5);
  }

  struct Ping {
    uint32_t value;
  };
  struct Echo {
    uint32_t depth;
  };

  void nothing(HandlerContext&) {}

  void emitUndeclaredEvent() {
    oly::ecs::World world;
    Engine engine(world);

    engine.emit(Ping{});
  }

  void onUndeclaredEvent() {
    oly::ecs::World world;
    Engine engine(world);

    engine.on<Ping>("h", nothing);
  }

  void declareEventTwice() {
    oly::ecs::World world;
    Engine engine(world);

    engine.declareEvent<Ping>();
    engine.declareEvent<Ping>();
  }

  void onFromSystem(SystemContext& context) {
    Engine& engine = *static_cast<Engine*>(context.userData());
    engine.on<Ping>("tarde", nothing);
  }

  void onDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareEvent<Ping>();
    engine.addSystem(Phase::Update, "registrador", onFromSystem, &engine);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void declareEventFromSystem(SystemContext& context) {
    Engine& engine = *static_cast<Engine*>(context.userData());
    engine.declareEvent<Echo>();
  }

  void declareEventDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.addSystem(Phase::Update, "declarador", declareEventFromSystem, &engine);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void declareActionFromSystem(SystemContext& context) {
    context.actions().declareButton("late");
  }

  void declareActionDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.addSystem(Phase::Update, "declarador", declareActionFromSystem);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void emitEchoAgain(HandlerContext& context) {
    context.emit(Echo{0});
  }

  void eventRoundLimit() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareEvent<Echo>("Echo");
    engine.on<Echo>("loop", emitEchoAgain);
    engine.emit(Echo{0});

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void readWrongType(HandlerContext& context) {
    context.events<Echo>();
  }

  void eventsWrongType() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareEvent<Ping>();
    engine.declareEvent<Echo>();
    engine.on<Ping>("errado", readWrongType);
    engine.emit(Ping{});

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  struct Mark {
    int value;
  };

  struct Other {
    double value;
  };

  void nothingTask(TaskContext&) {}

  void readWrongPayload(TaskContext& context) {
    context.payload<Other>();
  }

  void scheduleNegativeDelay() {
    oly::ecs::World world;
    Engine engine(world);

    engine.schedule().after(-1.0, nothingTask);
  }

  void scheduleNotFiniteDelay() {
    oly::ecs::World world;
    Engine engine(world);

    engine.schedule().after(NAN, nothingTask);
  }

  void everyZeroInterval() {
    oly::ecs::World world;
    Engine engine(world);

    engine.schedule().every(0.0, nothingTask);
  }

  void scheduleNullFunction() {
    oly::ecs::World world;
    Engine engine(world);

    engine.schedule().after(1.0, nullptr);
  }

  void taskPayloadWrongType() {
    oly::ecs::World world;
    Engine engine(world);
    engine.schedule().after(0.0, readWrongPayload, Mark{1});

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void cancelSlotOutOfRange() {
    oly::ecs::World world;
    Engine engine(world);

    engine.schedule().cancel(TaskHandle{7, 0});
  }

  struct Case {
    const char* name;
    void (*run)();
    std::vector<const char*> required;
  };
}

int main(int argumentCount, char** arguments) {
  static const std::vector<Case> cases = {
    {"add_system_during_frame", addSystemDuringFrame, {"addSystem", "durante frame"}},
    {"while_paused_render", whilePausedRender, {"whilePaused", "render"}},
    {"frame_time_backwards", frameTimeBackwards, {"frame", "tempo"}},
    {"emit_undeclared_event", emitUndeclaredEvent, {"emit", "não declarado"}},
    {"on_undeclared_event", onUndeclaredEvent, {"on", "não declarado"}},
    {"declare_event_twice", declareEventTwice, {"declareEvent", "já declarado"}},
    {"on_during_frame", onDuringFrame, {"on", "durante frame"}},
    {"declare_event_during_frame", declareEventDuringFrame, {"declareEvent", "durante frame"}},
    {"declare_action_during_frame", declareActionDuringFrame, {"declare", "durante frame"}},
    {"event_round_limit", eventRoundLimit, {"dispatchEvents", "rodadas", "Echo"}},
    {"events_wrong_type", eventsWrongType, {"events", "tipo"}},
    {"schedule_negative_delay", scheduleNegativeDelay, {"schedule", "atraso"}},
    {"schedule_not_finite_delay", scheduleNotFiniteDelay, {"schedule", "atraso"}},
    {"every_zero_interval", everyZeroInterval, {"every", "intervalo"}},
    {"schedule_null_function", scheduleNullFunction, {"schedule", "função nula"}},
    {"task_payload_wrong_type", taskPayloadWrongType, {"payload", "Other"}},
    {"cancel_slot_out_of_range", cancelSlotOutOfRange, {"cancel", "fora do intervalo"}},
  };

  if (argumentCount != 2) {
    std::fprintf(stderr, "uso: app_contract_death <caso>\n");
    return 64;
  }

  for (const Case& testCase : cases) {
    if (std::strcmp(testCase.name, arguments[1]) != 0) {
      continue;
    }

    g_required = &testCase.required;
    oly::core::setAssertHandler(&handler);
    testCase.run();
    std::fprintf(stderr, "assert não disparou\n");
    return 1;
  }

  std::fprintf(stderr, "caso desconhecido: %s\n", arguments[1]);
  return 64;
}
