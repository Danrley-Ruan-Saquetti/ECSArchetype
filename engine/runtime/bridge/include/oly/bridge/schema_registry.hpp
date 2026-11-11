#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "oly/ecs/component.hpp"
#include "oly/input/action_map.hpp"

namespace oly::app {
  class Engine;
}

namespace oly::bridge {
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

  // Definidas pelo código gerado do jogo (ou, nos testes, por um registro escrito à mão).
  uint32_t schemaHash() noexcept;
  std::span<const oly::ecs::ComponentId> schemaComponentIds();
  // Na ordem do esquema, que é a ordem de declaração (e de despacho) dos eventos na Engine.
  std::span<const SchemaEvent> schemaEvents();
  // Na ordem do esquema; o índice é o ActionId na ActionMap da Engine.
  std::span<const SchemaAction> schemaActions();
}
