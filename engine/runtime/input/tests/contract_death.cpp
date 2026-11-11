// Cada caso viola um contrato do input e precisa disparar OLY_ASSERT com as substrings exigidas; só é compilado com
// asserts ligados. Mesmo protocolo do contract_death do app.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "oly/core/assert.hpp"
#include "oly/input/action_map.hpp"

namespace {
  using namespace oly::input;

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

  void declareDuplicateName() {
    ActionMap map;
    map.declareButton("jump");
    map.declareButton("jump");
  }

  void declareWhileLocked() {
    ActionMap map;
    map.setDeclarationsLocked(true);
    map.declareButton("jump");
  }

  void bindWrongKind() {
    ActionMap map;
    const Axis2dAction move = map.declareAxis2d("move");
    map.bind(ButtonAction{move.id}, Key::Space);
  }

  void actionOutOfRange() {
    ActionMap map;
    map.declareButton("jump");
    map.clearBindings(7);
  }

  void buttonUsageZero() {
    ActionMap map;
    map.buttonDown(PhysicalButton(0u));
  }

  void pointerNotFinite() {
    ActionMap map;
    map.pointerMove(NAN, 0.0f);
  }

  struct Case {
    const char* name;
    void (*run)();
    std::vector<const char*> required;
  };
}

int main(int argumentCount, char** arguments) {
  static const std::vector<Case> cases = {
    {"declare_duplicate_name", declareDuplicateName, {"declare", "jump", "já declarada"}},
    {"declare_while_locked", declareWhileLocked, {"declare", "durante frame"}},
    {"bind_wrong_kind", bindWrongKind, {"bind", "tipo"}},
    {"action_out_of_range", actionOutOfRange, {"clearBindings", "fora do intervalo"}},
    {"button_usage_zero", buttonUsageZero, {"buttonDown", "usage 0"}},
    {"pointer_not_finite", pointerNotFinite, {"pointerMove", "não finito"}},
  };

  if (argumentCount != 2) {
    std::fprintf(stderr, "uso: input_contract_death <caso>\n");
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
