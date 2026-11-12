#include "components.hpp"
#include "oly/app/engine.hpp"
#include "oly/bridge/schema_registry.hpp"
#include "test_events.hpp"

namespace oly::bridge {
  uint32_t schemaHash() noexcept {
    return 0x1234u;
  }

  std::span<const oly::ecs::ComponentId> schemaComponentIds() {
    static const oly::ecs::ComponentId ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>(), oly::ecs::componentId<Tag>()};
    return ids;
  }

  std::span<const SchemaEvent> schemaEvents() {
    static const SchemaEvent events[] = {
      {"BridgeTestEvent", sizeof(BridgeTestEvent), alignof(BridgeTestEvent), [](oly::app::Engine& engine) { engine.declareEvent<BridgeTestEvent>("BridgeTestEvent"); }},
    };
    return events;
  }

  std::span<const SchemaAction> schemaActions() {
    static const SchemaAction actions[] = {
      {"bridgeJump", oly::input::ActionKind::Button},
      {"bridgeMove", oly::input::ActionKind::Axis2d},
      {"bridgeAim", oly::input::ActionKind::Pointer},
    };
    return actions;
  }

  std::span<const SchemaState> schemaStates() {
    static const SchemaState states[] = {
      {"bridgeMenu"},
      {"bridgePlaying"},
    };
    return states;
  }
}
