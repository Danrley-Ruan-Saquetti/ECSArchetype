#include "components.hpp"
#include "oly/bridge/schema_registry.hpp"

namespace oly::bridge {
  uint32_t schemaHash() noexcept {
    return 0x1234u;
  }

  std::span<const oly::ecs::ComponentId> schemaComponentIds() {
    static const oly::ecs::ComponentId ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>(), oly::ecs::componentId<Tag>()};
    return ids;
  }
}
