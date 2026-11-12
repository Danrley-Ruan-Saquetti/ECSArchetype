#pragma once

#include <cstdint>
#include <utility>

#include "oly/ecs/command_buffer.hpp"
#include "oly/ecs/detail/entity_store.hpp"
#include "oly/ecs/world.hpp"

namespace oly::ecs::detail {
  // Hooks only for the module's tests and benchmarks; production code does not use them.
  struct WorldTestAccess {
    static void flushSequential(World& world, CommandBuffer& commandBuffer) {
      world.flushSequential(commandBuffer);
    }
  };

  struct CommandBufferTestAccess {
    template <class F>
    static void forEach(const CommandBuffer& commandBuffer, F&& callback) {
      commandBuffer.forEach(std::forward<F>(callback));
    }
  };

  struct EntityStoreTestAccess {
    static void setGeneration(EntityStore& store, uint32_t index, uint32_t generation) noexcept {
      store.records_[index].generation = generation;
    }
  };
}
