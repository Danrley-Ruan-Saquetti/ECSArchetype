#pragma once

#include <cmath>
#include <cstdint>

#include "oly/ecs/entity.hpp"

// Valores iniciais, sem medição: ajustados jogando. Cada um tem par em src/arena.ts, que precisa
// mudar junto (o teste de fumaça do confine prende ArenaWidth e ArenaHeight).
namespace arena {
  inline constexpr float ArenaWidth = 960.0f;
  inline constexpr float ArenaHeight = 540.0f;
  inline constexpr float PlayerSpeed = 220.0f;
  inline constexpr float BulletSpeed = 600.0f;
  inline constexpr float BulletRadius = 3.0f;
  inline constexpr float CoinRadius = 8.0f;
  inline constexpr double InvulnerableSeconds = 1.0;
  inline constexpr uint32_t CoinDropPercent = 30;

  // Payload das tarefas que agem sobre o jogador.
  struct PlayerTarget {
    oly::ecs::Entity player;
  };

  struct Direction {
    float x;
    float y;
  };

  // Normalizada; {0, 0} quando os pontos coincidem.
  inline Direction directionTo(float fromX, float fromY, float toX, float toY) {
    const float deltaX = toX - fromX;
    const float deltaY = toY - fromY;
    const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (length == 0.0f) {
      return Direction{0.0f, 0.0f};
    }

    return Direction{deltaX / length, deltaY / length};
  }

  inline bool touches(float firstX, float firstY, float firstRadius, float secondX, float secondY, float secondRadius) {
    const float deltaX = secondX - firstX;
    const float deltaY = secondY - firstY;
    const float radii = firstRadius + secondRadius;

    return deltaX * deltaX + deltaY * deltaY <= radii * radii;
  }
}
