#pragma once

#include <cstdint>
#include <span>

#include "oly/ecs/component.hpp"

namespace oly::bridge {
  // Definidas pelo código gerado do jogo (ou, nos testes, por um registro escrito à mão).
  uint32_t schemaHash() noexcept;
  std::span<const oly::ecs::ComponentId> schemaComponentIds();
}
