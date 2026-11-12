// Each case violates an app contract and must trigger OLY_ASSERT with the required substrings; it is only compiled with
// asserts enabled. Same protocol as the ecs contract_death.
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
          std::fprintf(stderr, "message missing the required substring: \"%s\"\n", needle);
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
    engine.addSystem(Phase::Update, "other", [](SystemContext&) {});
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
    const auto system = engine.addSystem(Phase::Render, "drawing", [](SystemContext&) {});

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
    engine.on<Ping>("late", nothing);
  }

  void onDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareEvent<Ping>();
    engine.addSystem(Phase::Update, "registrar", onFromSystem, &engine);

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
    engine.addSystem(Phase::Update, "declarer", declareEventFromSystem, &engine);

    engine.frame(0.0);
    engine.frame(1.0 / 60);
  }

  void declareActionFromSystem(SystemContext& context) {
    context.actions().declareButton("late");
  }

  void declareActionDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.addSystem(Phase::Update, "declarer", declareActionFromSystem);

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
    engine.on<Ping>("wrong", readWrongType);
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
    static_cast<Engine*>(context.userData())->declareState("late");
  }

  void declareStateDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.addSystem(Phase::Update, "declarer", declareStateFromSystem, &engine);

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
      engine.declareState("state" + std::to_string(index));
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
    engine.onEnter(0, "late", [](oly::app::StateContext&) {});
  }

  void onEnterDuringFrame() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareState("a");
    engine.addSystem(Phase::Update, "registrar", onEnterFromSystem, &engine);

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
    engine.addSystem(Phase::Update, "restrictor", setSystemStatesFromSystem, &engine);

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

  void systemStatesUnknownSystem() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareState("a");

    engine.setSystemStateMask(7, 0b1);
  }

  void handlerStatesUnknownHandler() {
    oly::ecs::World world;
    Engine engine(world);
    engine.declareState("a");

    engine.setHandlerStateMask(7, 0b1);
  }

  struct Case {
    const char* name;
    void (*run)();
    std::vector<const char*> required;
  };
}

int main(int argumentCount, char** arguments) {
  static const std::vector<Case> cases = {
    {"add_system_during_frame", addSystemDuringFrame, {"addSystem", "during frame"}},
    {"while_paused_render", whilePausedRender, {"whilePaused", "render"}},
    {"frame_time_backwards", frameTimeBackwards, {"frame", "time"}},
    {"emit_undeclared_event", emitUndeclaredEvent, {"emit", "not declared"}},
    {"on_undeclared_event", onUndeclaredEvent, {"on", "not declared"}},
    {"declare_event_twice", declareEventTwice, {"declareEvent", "already declared"}},
    {"on_during_frame", onDuringFrame, {"on", "during frame"}},
    {"declare_event_during_frame", declareEventDuringFrame, {"declareEvent", "during frame"}},
    {"declare_action_during_frame", declareActionDuringFrame, {"declare", "during frame"}},
    {"event_round_limit", eventRoundLimit, {"dispatchEvents", "rounds", "Echo"}},
    {"events_wrong_type", eventsWrongType, {"events", "type"}},
    {"schedule_negative_delay", scheduleNegativeDelay, {"schedule", "delay"}},
    {"schedule_not_finite_delay", scheduleNotFiniteDelay, {"schedule", "delay"}},
    {"every_zero_interval", everyZeroInterval, {"every", "interval"}},
    {"schedule_null_function", scheduleNullFunction, {"schedule", "null function"}},
    {"task_payload_wrong_type", taskPayloadWrongType, {"payload", "Other"}},
    {"cancel_slot_out_of_range", cancelSlotOutOfRange, {"cancel", "out of range"}},
    {"declare_state_during_frame", declareStateDuringFrame, {"declareState", "during frame"}},
    {"declare_state_twice", declareStateTwice, {"declareState", "already declared"}},
    {"too_many_states", tooManyStates, {"declareState", "maximum"}},
    {"request_state_out_of_range", requestStateOutOfRange, {"requestState", "out of range"}},
    {"on_enter_during_frame", onEnterDuringFrame, {"onEnter", "during frame"}},
    {"system_states_during_frame", systemStatesDuringFrame, {"setSystemStates", "during frame"}},
    {"system_state_mask_undeclared", systemStateMaskUndeclared, {"setSystemStates", "undeclared"}},
    {"system_states_unknown_system", systemStatesUnknownSystem, {"setSystemStates", "out of range"}},
    {"handler_states_unknown_handler", handlerStatesUnknownHandler, {"setHandlerStates", "out of range"}},
    {"persist_dead_entity", persistDeadEntity, {"remove", "StateScoped"}},
  };

  if (argumentCount != 2) {
    std::fprintf(stderr, "usage: app_contract_death <case>\n");
    return 64;
  }

  for (const Case& testCase : cases) {
    if (std::strcmp(testCase.name, arguments[1]) != 0) {
      continue;
    }

    g_required = &testCase.required;
    oly::core::setAssertHandler(&handler);
    testCase.run();
    std::fprintf(stderr, "assert did not fire\n");
    return 1;
  }

  std::fprintf(stderr, "unknown case: %s\n", arguments[1]);
  return 64;
}
