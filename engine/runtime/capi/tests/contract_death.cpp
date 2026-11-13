// Each case violates a C API contract and must trigger OLY_ASSERT with the required substrings; only compiled
// with asserts enabled. Same protocol as the ecs contract_death.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "oly/capi/c_api.hpp"
#include "oly/capi/engine_api.hpp"
#include "oly/capi/schema_registry.hpp"
#include "oly/core/assert.hpp"
#include "oly/ecs/component.hpp"

namespace oly::capi {
  uint32_t schemaHash() noexcept {
    return 0;
  }

  std::span<const oly::ecs::ComponentId> schemaComponentIds() {
    return {};
  }

  std::span<const SchemaEvent> schemaEvents() {
    return {};
  }

  std::span<const SchemaAction> schemaActions() {
    static const SchemaAction actions[] = {
      {"jump", oly::input::ActionKind::Button},
      {"move", oly::input::ActionKind::Axis2d},
    };
    return actions;
  }

  std::span<const SchemaState> schemaStates() {
    return {};
  }
}

namespace {
  struct Counter {
    uint64_t value;
  };

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

  void queryEndOutOfOrder() {
    OlyWorld* world = olyWorldCreate();
    const uint32_t ids[] = {oly::ecs::componentId<Counter>()};
    OlyQuery* query = olyQueryCreate(world, ids, 1, 0, 0, 0);

    uint32_t blockCount = 0;
    const uintptr_t* descriptors = nullptr;
    OlyIteration* outer = olyQueryBegin(world, query, &blockCount, &descriptors);
    olyQueryBegin(world, query, &blockCount, &descriptors);
    olyQueryEnd(world, outer);
  }

  void systemSpawnOutsideSystem() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);
    OlyEntity entity{};

    olySystemSpawn(engine, &entity);
  }

  void enginePhaseOutOfRange() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);

    olyEngineAddScriptSystem(engine, 4, "x");
  }

  void engineEmitOutOfRange() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);
    uint32_t value = 0;

    olyEngineEmit(engine, 5, &value);
  }

  void scheduleScriptTaskNegativeInterval() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);
    OlyTaskHandle task{};

    olyEngineScheduleScriptTask(engine, 0.0, -1.0, 1, &task);
  }

  void bindButtonWrongKind() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);
    olyEngineBindButton(engine, 1, 0x0007002Cu);
  }

  void actionOutOfRange() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);
    olyEngineClearBindings(engine, 9);
  }

  void directSpawnOutsideContext() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);
    OlyEntity entity{};

    olyDirectSpawn(engine, &entity);
  }

  void contextPersistOutsideContext() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);

    olyContextPersist(engine, 0, 0);
  }

  void stateHookInvalidWhen() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);

    olyEngineAddScriptStateHook(engine, 2, 0, "x");
  }

  void requestStateOutOfRangeC() {
    OlyEngine* engine = olyEngineCreate(60.0, 5, 16);

    olyEngineRequestState(engine, 9);
  }

  struct Case {
    const char* name;
    void (*run)();
    std::vector<const char*> required;
  };
}

int main(int argumentCount, char** arguments) {
  static const std::vector<Case> cases = {
    {"query_end_out_of_order", queryEndOutOfOrder, {"olyQueryEnd", "out of order"}},
    {"direct_spawn_outside_context", directSpawnOutsideContext, {"olyDirectSpawn", "outside"}},
    {"context_persist_outside_context", contextPersistOutsideContext, {"olyContextPersist", "outside"}},
    {"state_hook_invalid_when", stateHookInvalidWhen, {"when", "invalid"}},
    {"request_state_out_of_range_c", requestStateOutOfRangeC, {"requestState", "out of range"}},
    {"system_spawn_outside_system", systemSpawnOutsideSystem, {"olySystemSpawn", "outside a system"}},
    {"engine_phase_out_of_range", enginePhaseOutOfRange, {"phase"}},
    {"bind_button_wrong_kind", bindButtonWrongKind, {"olyEngineBindButton", "kind"}},
    {"action_out_of_range", actionOutOfRange, {"olyEngineClearBindings", "out of range"}},
    {"engine_emit_out_of_range", engineEmitOutOfRange, {"olyEngineEmit", "eventType"}},
    {"schedule_script_task_negative_interval", scheduleScriptTaskNegativeInterval, {"schedule", "interval"}},
  };

  if (argumentCount != 2) {
    std::fprintf(stderr, "usage: capi_contract_death <case>\n");
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
