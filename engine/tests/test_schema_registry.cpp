#include "components.hpp"
#include "engine/schema_registry.hpp"

namespace engine {
  uint32_t schemaHash() noexcept {
    return 0x1234u;
  }

  std::span<const ecs::ComponentId> schemaComponentIds() {
    static const ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<B>(), ecs::componentId<Tag>()};
    return ids;
  }
}
