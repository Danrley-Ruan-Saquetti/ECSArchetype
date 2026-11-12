// Each case violates an input contract and must trigger OLY_ASSERT with the required substrings; only compiled with
// asserts enabled. Same protocol as the app contract_death.
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
          std::fprintf(stderr, "message missing the required substring: \"%s\"\n", needle);
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
    {"declare_duplicate_name", declareDuplicateName, {"declare", "jump", "already declared"}},
    {"declare_while_locked", declareWhileLocked, {"declare", "during frame"}},
    {"bind_wrong_kind", bindWrongKind, {"bind", "kind"}},
    {"action_out_of_range", actionOutOfRange, {"clearBindings", "out of range"}},
    {"button_usage_zero", buttonUsageZero, {"buttonDown", "usage 0"}},
    {"pointer_not_finite", pointerNotFinite, {"pointerMove", "non-finite"}},
  };

  if (argumentCount != 2) {
    std::fprintf(stderr, "usage: input_contract_death <case>\n");
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
