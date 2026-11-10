#pragma once

#include <cstdint>
#include <span>

#include "oly/ecs/type_info.hpp"

namespace engine {
  // Definidas pelo código gerado do jogo (ou, nos testes, por um registro escrito à mão).
  uint32_t schemaHash() noexcept;
  std::span<const ecs::ComponentId> schemaComponentIds();
}
