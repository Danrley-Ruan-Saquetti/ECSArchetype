#pragma once

#include "oly/bridge/c_api.hpp"
#include "oly/ecs/world.hpp"

namespace oly::bridge {
  // Access to the World behind the C API's opaque handle, for games written in C++.
  oly::ecs::World& worldOf(OlyWorld* world) noexcept;
}
