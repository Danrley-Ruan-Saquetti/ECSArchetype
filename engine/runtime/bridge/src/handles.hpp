#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "oly/app/engine.hpp"
#include "oly/bridge/c_api.hpp"
#include "oly/bridge/engine_api.hpp"
#include "oly/ecs/query_by_id.hpp"
#include "oly/ecs/world.hpp"

struct OlyIteration {
  std::optional<oly::ecs::QueryIteration> iteration;
};

struct OlyWorld {
  oly::ecs::World world;
  // Slot stack: grows to the largest depth seen and never shrinks; the pointers are stable.
  std::vector<std::unique_ptr<OlyIteration>> iterationSlots;
  uint32_t activeIterations = 0;
};

struct OlyEngine {
  OlyWorld world; // before `engine`: it keeps a reference to world.world
  oly::app::Engine engine;
  oly::app::SystemContext* currentContext = nullptr;
  oly::app::DirectContext* directContext = nullptr;

  explicit OlyEngine(oly::app::EngineConfig config)
    : engine(world.world, config) {}
};
