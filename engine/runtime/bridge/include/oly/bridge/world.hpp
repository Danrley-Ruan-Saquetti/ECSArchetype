#pragma once

#include "oly/bridge/c_api.hpp"
#include "oly/ecs/world.hpp"

namespace oly::bridge {
  // Acesso ao World por trás do handle opaco da API C, para jogos escritos em C++.
  oly::ecs::World& worldOf(OlyWorld* world) noexcept;
}
