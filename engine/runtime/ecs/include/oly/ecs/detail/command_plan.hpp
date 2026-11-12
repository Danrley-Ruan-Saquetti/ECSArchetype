#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "oly/ecs/component.hpp"
#include "oly/ecs/entity.hpp"

namespace oly::ecs::detail {
  enum class CommandOperation : uint8_t { Spawn,
                                          Destroy,
                                          Add,
                                          Remove };

  struct CommandView {
    CommandOperation operation;
    Entity entity;
    ComponentId componentId;
    void* payload; // nullptr for Spawn, Destroy, Remove and for an Add of a tag
  };

  struct NetComponentOperation {
    ComponentId componentId;
    bool present;
    void* payload; // value of the last Add (nullptr for tags and removals)
  };

  struct EntityPlan {
    Entity entity;
    uint32_t begin = 0, count = 0; // range in CommandPlan::order (or offset/8 of the first record, if contiguous)
  };

  struct NetEffect {
    bool spawned = false;
    bool destroyed = false;
    std::vector<NetComponentOperation> operations; // sorted by componentId
  };

  // Stores only the records' offsets/8, to touch little memory; the net effect comes from
  // netEffect, one entity at a time.
  struct CommandPlan {
    std::vector<EntityPlan> owned;
    // In order of first appearance. Points to `owned` or to the runs stored in the CommandBuffer,
    // so the plan is only valid while the buffer does not change.
    std::span<const EntityPlan> entities;
    // Empty when each entity's commands are already consecutive in the buffer (the common batch case):
    // then `EntityPlan::begin` is the offset/8 of the first record and walking `count` records is enough.
    // Otherwise, the offset/8 of each record, grouped by entity and in recording order.
    std::vector<uint32_t> order;
    bool contiguous = true;

    CommandPlan() = default;
    CommandPlan(CommandPlan&&) = default;
    CommandPlan(const CommandPlan&) = delete; // the copy's `entities` span would point to the original's `owned`
  };
}
