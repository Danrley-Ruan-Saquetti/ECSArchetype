#include "oly/app/handler.hpp"

#include <deque>
#include <string>
#include <utility>

namespace oly::app {
  namespace {
    // Local static, not global (same reason as namedSystems); deque because findNamedHandler returns pointers
    // that must survive new registrations.
    std::deque<std::pair<std::string, NamedHandler>>& namedHandlers() {
      static std::deque<std::pair<std::string, NamedHandler>> handlers;
      return handlers;
    }
  }

  const NamedHandler* findNamedHandler(std::string_view name) noexcept {
    for (const auto& [registeredName, handler] : namedHandlers()) {
      if (registeredName == name) {
        return &handler;
      }
    }

    return nullptr;
  }

  NamedHandlerRegistration::NamedHandlerRegistration(std::string_view name, HandlerFunction function, EventKey eventKey) {
    namedHandlers().emplace_back(std::string(name), NamedHandler{function, eventKey});
  }
}
