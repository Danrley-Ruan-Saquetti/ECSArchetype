#pragma once

#include <string_view>

#include "oly/app/context.hpp"
#include "oly/app/task.hpp"
#include "oly/core/assert.hpp"
#include "oly/core/type_name.hpp"

namespace oly::app {
  class Engine;

  // Valid only during the task call. Runs in stage 2, outside any iteration, with direct structural changes
  // like those of HandlerContext.
  class TaskContext : public DirectContext {
   private:
    friend class Engine;

    TaskHandle handle_;
    bool lastRun_;
    TaskPayload payload_;

    TaskContext(Engine& engine, ecs::World& world, TaskHandle handle, bool lastRun, const TaskPayload& payload) noexcept
      : DirectContext(engine, world),
        handle_(handle),
        lastRun_(lastRun),
        payload_(payload) {}

   public:
    TaskHandle handle() const noexcept {
      return handle_;
    }
    // True on an after's firing: the task does not run again and no discard will come, so the payload is released here.
    bool lastRun() const noexcept {
      return lastRun_;
    }

    template <class P>
    const P& payload() const {
      [[maybe_unused]] constexpr std::string_view requested = oly::core::typeName<P>();
      [[maybe_unused]] const std::string_view scheduled = payload_.typeName.empty() ? std::string_view("(none)") : payload_.typeName;
      OLY_ASSERT(payload_.key == payloadKey<P>(), "payload: type \"%.*s\" differs from the scheduled \"%.*s\"", static_cast<int>(requested.size()), requested.data(), static_cast<int>(scheduled.size()), scheduled.data());

      return *reinterpret_cast<const P*>(payload_.bytes);
    }
  };
}
