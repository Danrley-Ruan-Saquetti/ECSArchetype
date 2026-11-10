// Testes de morte da API C: violações de contrato devem disparar OLY_ASSERT (só compilado com asserts ligados).
// Uso: bridge_contract_death <caso>. Mesmo protocolo do contract_death do ecs.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

#include "oly/bridge/c_api.hpp"
#include "oly/bridge/engine_api.hpp"
#include "oly/bridge/schema_registry.hpp"
#include "oly/core/assert.hpp"
#include "oly/ecs/component.hpp"

namespace oly::bridge {
  uint32_t schemaHash() noexcept {
    return 0;
  }

  std::span<const oly::ecs::ComponentId> schemaComponentIds() {
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
          std::fprintf(stderr, "mensagem sem a substring exigida: \"%s\"\n", needle);
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
    OlyIteration* first = olyQueryBegin(world, query, &blockCount, &descriptors);
    OlyIteration* second = olyQueryBegin(world, query, &blockCount, &descriptors);
    (void)second;
    olyQueryEnd(world, first);
  }

  void systemSpawnOutsideSystem() {
    OlyEngine* engine = olyEngineCreate(60.0, 5);
    OlyEntity entity{};

    olySystemSpawn(engine, &entity);
  }

  void enginePhaseOutOfRange() {
    OlyEngine* engine = olyEngineCreate(60.0, 5);

    olyEngineAddScriptSystem(engine, 4, "x");
  }

  struct Case {
    const char* name;
    void (*run)();
    std::vector<const char*> required;
  };
}

int main(int argumentCount, char** arguments) {
  static const std::vector<Case> cases = {
    {"query_end_out_of_order", queryEndOutOfOrder, {"olyQueryEnd", "fora de ordem"}},
    {"system_spawn_outside_system", systemSpawnOutsideSystem, {"olySystemSpawn", "fora de sistema"}},
    {"engine_phase_out_of_range", enginePhaseOutOfRange, {"phase"}},
  };

  if (argumentCount != 2) {
    std::fprintf(stderr, "uso: bridge_contract_death <caso>\n");
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
