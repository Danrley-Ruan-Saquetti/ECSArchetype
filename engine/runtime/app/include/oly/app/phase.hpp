#pragma once

#include <cstdint>

namespace oly::app {
  enum class Phase : uint8_t { UpdateBefore,
                               Update,
                               UpdateAfter,
                               Render };

  inline constexpr uint32_t PhaseCount = static_cast<uint32_t>(Phase::Render) + 1;

  constexpr bool isGameplayPhase(Phase phase) noexcept {
    return phase != Phase::Render;
  }
}
