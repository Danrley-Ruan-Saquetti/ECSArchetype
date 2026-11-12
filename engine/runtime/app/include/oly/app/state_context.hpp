#pragma once

#include <string_view>

#include "oly/app/context.hpp"
#include "oly/app/state.hpp"

namespace oly::app {
  class Engine;

  // Válido só durante a chamada do gancho. Roda no passo 5, fora de qualquer iteração, com mudanças estruturais
  // diretas como as do HandlerContext.
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
    // fromState é NoState na primeira troca.
    StateId fromState() const noexcept {
      return fromState_;
    }
    StateId toState() const noexcept {
      return toState_;
    }
    // Verdadeiro no onEnter; um gancho nativo registrado nos dois momentos usa isto para saber o lado.
    bool entering() const noexcept {
      return entering_;
    }
  };
}
