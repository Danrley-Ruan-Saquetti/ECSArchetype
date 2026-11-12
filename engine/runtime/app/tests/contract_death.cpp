// Cada caso viola um contrato do app e precisa disparar OLY_ASSERT com as substrings exigidas; só é compilado com
// asserts ligados. Mesmo protocolo do contract_death do ecs.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "oly/app/engine.hpp"
#include "oly/app/handler.hpp"
#include "oly/app/state_context.hpp"
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

  void declareStateFromSystem(SystemContext& context) {
    static_cast<Engine*>(context.userData())->declareState("tarde");
  }

  void declareStateDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.addSystem(Phase::Update, "declarador", declareStateFromSystem, &engine);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void declareStateTwice() {
    oly::ecs::World world;
    Engine engine(world);

    engine.declareState("a");
    engine.declareState("a");
  }

  void tooManyStates() {
    oly::ecs::World world;
    Engine engine(world);

    for (uint32_t index = 0; index <= oly::app::MaxStates; ++index) {
      engine.declareState("estado" + std::to_string(index));
    }
  }

  void requestStateOutOfRange() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareState("a");
    engine.declareState("b");

    engine.requestState(5);
  }

  void onEnterFromSystem(SystemContext& context) {
    Engine& engine = *static_cast<Engine*>(context.userData());
    engine.onEnter(0, "tarde", [](oly::app::StateContext&) {});
  }

  void onEnterDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareState("a");
    engine.addSystem(Phase::Update, "registrador", onEnterFromSystem, &engine);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void setSystemStatesFromSystem(SystemContext& context) {
    Engine& engine = *static_cast<Engine*>(context.userData());
    engine.setSystemStates(context.systemId(), {0});
  }

  void systemStatesDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareState("a");
    engine.addSystem(Phase::Update, "restritor", setSystemStatesFromSystem, &engine);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void persistFromTask(TaskContext& context) {
    context.persist(context.payload<oly::ecs::Entity>());
  }

  void persistDeadEntity() {
    oly::ecs::World world;
    Engine engine(world);
    const oly::ecs::Entity entity = world.spawn();
    world.destroy(entity);
    engine.schedule().after(0.0, persistFromTask, entity);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void systemStateMaskUndeclared() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareState("a");
    engine.declareState("b");
    const auto system = engine.addSystem(Phase::Update, "s", [](SystemContext&) {});

    engine.setSystemStateMask(system, 0b100);
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
    {"declare_state_during_frame", declareStateDuringFrame, {"declareState", "durante frame"}},
    {"declare_state_twice", declareStateTwice, {"declareState", "já declarado"}},
    {"too_many_states", tooManyStates, {"declareState", "máximo"}},
    {"request_state_out_of_range", requestStateOutOfRange, {"requestState", "fora do intervalo"}},
    {"on_enter_during_frame", onEnterDuringFrame, {"onEnter", "durante frame"}},
    {"system_states_during_frame", systemStatesDuringFrame, {"setSystemStates", "durante frame"}},
    {"system_state_mask_undeclared", systemStateMaskUndeclared, {"setSystemStates", "não declarado"}},
    {"persist_dead_entity", persistDeadEntity, {"remove", "StateScoped"}},
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
