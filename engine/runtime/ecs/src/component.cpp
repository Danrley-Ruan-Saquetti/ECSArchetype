#include "oly/ecs/component.hpp"

#include <deque>

namespace oly::ecs {
  namespace {
    // Function with a local static: avoids initialization order problems (R-TI-4).
    // std::deque keeps stable references to each TypeInfo.
    std::deque<TypeInfo>& registry() {
      static std::deque<TypeInfo> entries;
      return entries;
    }
  }

  const TypeInfo& typeInfo(ComponentId id) {
    return registry()[id];
  }

  uint32_t registeredTypeCount() noexcept {
    return static_cast<uint32_t>(registry().size());
  }

  ComponentId detail::registerType(const TypeInfo& info) {
    registry().push_back(info);
    return static_cast<ComponentId>(registry().size() - 1);
  }
}
