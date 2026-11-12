#pragma once

#include <cmath>
#include <cstdint>

#include "oly/ecs/entity.hpp"

// Initial values, not measured: tuned by playing. Each one has a pair in src/arena.ts that must
// change with it (the confine smoke test pins ArenaWidth and ArenaHeight).
namespace arena {
  inline constexpr float ArenaWidth = 960.0f;
  inline constexpr float ArenaHeight = 540.0f;
  inline constexpr float PlayerSpeed = 220.0f;
  inline constexpr float BulletSpeed = 600.0f;
  inline constexpr float BulletRadius = 3.0f;
  inline constexpr float CoinRadius = 8.0f;
  inline constexpr double InvulnerableSeconds = 1.0;
  inline constexpr uint32_t CoinDropPercent = 30;

  // Payload of the tasks that act on the player.
  struct PlayerTarget {
    oly::ecs::Entity player;
  };

  struct Direction {
    float x;
    float y;
  };

  // Normalized; {0, 0} when the points coincide.
  inline Direction directionTo(float fromX, float fromY, float toX, float toY) {
    const float deltaX = toX - fromX;
    const float deltaY = toY - fromY;
    const float length = std::sqrt(deltaX * deltaX + deltaY * deltaY);
    if (length == 0.0f) {
      return Direction{0.0f, 0.0f};
    }

    return Direction{deltaX / length, deltaY / length};
  }

  inline bool isOutsideArena(float x, float y) {
    return x < 0.0f || x > ArenaWidth || y < 0.0f || y > ArenaHeight;
  }

  inline bool touches(float firstX, float firstY, float firstRadius, float secondX, float secondY, float secondRadius) {
    const float deltaX = secondX - firstX;
    const float deltaY = secondY - firstY;
    const float radii = firstRadius + secondRadius;

    return deltaX * deltaX + deltaY * deltaY <= radii * radii;
  }
}
