#include "components.hpp"
#include "oly/app/engine.hpp"
#include "oly/capi/schema_registry.hpp"
#include "test_events.hpp"

namespace oly::capi {
  uint32_t schemaHash() noexcept {
    return 0x1234u;
  }

  std::span<const oly::ecs::ComponentId> schemaComponentIds() {
    static const oly::ecs::ComponentId ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>(), oly::ecs::componentId<Tag>()};
    return ids;
  }

  std::span<const SchemaEvent> schemaEvents() {
    static const SchemaEvent events[] = {
      {"CapiTestEvent", sizeof(CapiTestEvent), alignof(CapiTestEvent), [](oly::app::Engine& engine) { engine.declareEvent<CapiTestEvent>("CapiTestEvent"); }},
    };
    return events;
  }

  std::span<const SchemaAction> schemaActions() {
    static const SchemaAction actions[] = {
      {"capiJump", oly::input::ActionKind::Button},
      {"capiMove", oly::input::ActionKind::Axis2d},
      {"capiAim", oly::input::ActionKind::Pointer},
    };
    return actions;
  }

  std::span<const SchemaState> schemaStates() {
    static const SchemaState states[] = {
      {"capiMenu"},
      {"capiPlaying"},
    };
    return states;
  }
}
