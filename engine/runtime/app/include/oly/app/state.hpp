#pragma once

#include <cstdint>
#include <string_view>

namespace oly::app {
  class StateContext;

  // Índice denso na ordem de declaração. A máscara de inState tem um bit por estado, por isso o máximo é 32.
  using StateId = uint32_t;
  using StateMask = uint32_t;
  using StateHookId = uint32_t;

  inline constexpr StateId NoState = 0xFFFFFFFFu;
  inline constexpr uint32_t MaxStates = 32;

  using StateHookFunction = void (*)(StateContext& context);

  // nullptr se o nome não existe.
  StateHookFunction findNamedStateHook(std::string_view name) noexcept;

  struct NamedStateHookRegistration {
    NamedStateHookRegistration(std::string_view name, StateHookFunction function);
  };
}

#define OLY_STATE_HOOK(name)                                                          \
  void name(oly::app::StateContext& context);                                         \
  static const oly::app::NamedStateHookRegistration name##Registration(#name, &name); \
  void name([[maybe_unused]] oly::app::StateContext& context)
