#pragma once

#include <string_view>

#include "oly/app/context.hpp"
#include "oly/app/state.hpp"

namespace oly::app {
  class Engine;

  // Valid only during the hook call. Runs in stage 5, outside any iteration, with direct structural changes
  // like those of HandlerContext.
  class StateContext : public DirectContext {
   private:
    friend class Engine;

    StateHookId hookId_;
    std::string_view name_;
    void* userData_;
    StateId fromState_;
    StateId toState_;
    bool entering_;

    StateContext(Engine& engine, ecs::World& world, StateHookId hookId, std::string_view name, void* userData, StateId fromState, StateId toState, bool entering) noexcept
      : DirectContext(engine, world),
        hookId_(hookId),
        name_(name),
        userData_(userData),
        fromState_(fromState),
        toState_(toState),
        entering_(entering) {}

   public:
    StateHookId hookId() const noexcept {
      return hookId_;
    }
    std::string_view name() const noexcept {
      return name_;
    }
    void* userData() const noexcept {
      return userData_;
    }
    // fromState is NoState on the first change.
    StateId fromState() const noexcept {
      return fromState_;
    }
    StateId toState() const noexcept {
      return toState_;
    }
    // True in onEnter; a native hook registered for both moments uses this to know which side it is on.
    bool entering() const noexcept {
      return entering_;
    }
  };
}
