#pragma once

#include <vector>

#include "oly/ecs/query_by_id.hpp"
#include "oly/ecs/world.hpp"

struct EngineWorld {
  oly::ecs::World world;
  std::vector<oly::ecs::QueryIteration> activeIterations; // pilha: a mais interna no fim
};
