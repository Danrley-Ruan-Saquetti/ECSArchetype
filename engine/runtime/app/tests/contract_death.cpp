// Cada caso viola um contrato do app e precisa disparar OLY_ASSERT com as substrings exigidas; só é compilado com
// asserts ligados. Mesmo protocolo do contract_death do ecs.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "oly/app/engine.hpp"
#include "oly/core/assert.hpp"

namespace {
  using oly::app::Engine;
  using oly::app::Phase;
  using oly::app::SystemContext;

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
