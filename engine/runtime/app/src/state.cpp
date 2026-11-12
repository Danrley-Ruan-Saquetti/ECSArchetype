#include "oly/app/state.hpp"

#include <string>
#include <utility>
#include <vector>

namespace oly::app {
  namespace {
    // Estático local, não global (mesmo motivo de namedSystems).
    std::vector<std::pair<std::string, StateHookFunction>>& namedStateHooks() {
      static std::vector<std::pair<std::string, StateHookFunction>> hooks;
      return hooks;
    }
  }

  StateHookFunction findNamedStateHook(std::string_view name) noexcept {
    for (const auto& [registeredName, function] : namedStateHooks()) {
      if (registeredName == name) {
        return function;
      }
    }

    return nullptr;
  }

  NamedStateHookRegistration::NamedStateHookRegistration(std::string_view name, StateHookFunction function) {
    namedStateHooks().emplace_back(std::string(name), function);
  }
}
