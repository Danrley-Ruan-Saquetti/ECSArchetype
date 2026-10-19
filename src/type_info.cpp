#include "ecs/type_info.hpp"

#include <deque>

namespace ecs {
  namespace {
    // Função com static local: evita problemas de ordem de inicialização (R-TI-4).
    // std::deque mantém referências estáveis a cada TypeInfo.
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
