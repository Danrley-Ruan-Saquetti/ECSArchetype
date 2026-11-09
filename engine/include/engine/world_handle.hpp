#pragma once

#include <cstdint>
#include <vector>

#include "ecs/world.hpp"

struct EngineWorld {
  ecs::World world;
  std::vector<uintptr_t> blockDescriptors;
};
