#pragma once

#include <cstdint>
#include <vector>

#include "oly/ecs/world.hpp"

struct EngineWorld {
  ecs::World world;
  std::vector<uintptr_t> blockDescriptors;
};
