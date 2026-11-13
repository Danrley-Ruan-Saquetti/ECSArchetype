#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "oly/ecs/component.hpp"
#include "oly/input/action_map.hpp"

namespace oly::app {
  class Engine;
}

namespace oly::capi {
  struct SchemaEvent {
    std::string_view name;
    uint32_t size;
    uint32_t alignment;
    void (*declare)(oly::app::Engine& engine);
  };

  struct SchemaAction {
    std::string_view name;
    oly::input::ActionKind kind;
  };

  struct SchemaState {
    std::string_view name;
  };

  // Defined by the game's generated code (or, in tests, by a hand-written registry).
  uint32_t schemaHash() noexcept;
  std::span<const oly::ecs::ComponentId> schemaComponentIds();
  // In schema order, which is the declaration (and dispatch) order of the events in the Engine.
  std::span<const SchemaEvent> schemaEvents();
  // In schema order; the index is the ActionId in the Engine's ActionMap.
  std::span<const SchemaAction> schemaActions();
  // In schema order; the index is the StateId in the Engine.
  std::span<const SchemaState> schemaStates();
}
