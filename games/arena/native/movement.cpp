#include <algorithm>
#include <cmath>
#include <tuple>

#include "arena.hpp"
#include "oly/app/system.hpp"
#include "oly/app/task_context.hpp"
#include "oly/ecs/query.hpp"
#include "schema.generated.hpp"

namespace {
  void endDash(oly::app::TaskContext& context) {
    const oly::ecs::Entity player = context.payload<arena::PlayerTarget>().player;
    if (context.isAlive(player)) {
      context.get<Dash>(player).active = 0;
    }
  }

  void rechargeDash(oly::app::TaskContext& context) {
    const oly::ecs::Entity player = context.payload<arena::PlayerTarget>().player;
    if (context.isAlive(player)) {
      context.get<Dash>(player).ready = 1;
    }
  }

  // A diagonal de WASD chega com comprimento raiz de 2; o jogador não pode ser mais rápido nela.
  arena::Direction limitedToUnit(const oly::input::Vector2& axis) {
    const float length = std::sqrt(axis.x * axis.x + axis.y * axis.y);
    if (length <= 1.0f) {
      return arena::Direction{axis.x, axis.y};
    }

    return arena::Direction{axis.x / length, axis.y / length};
  }

  arena::Direction dashDirection(const arena::Direction& movement, const oly::input::PointerState& aim, const Position& position) {
    if (movement.x != 0.0f || movement.y != 0.0f) {
      return arena::directionTo(0.0f, 0.0f, movement.x, movement.y);
    }
    if (aim.valid) {
      return arena::directionTo(position.x, position.y, aim.x, aim.y);
    }

    return arena::Direction{0.0f, 0.0f};
  }
}

OLY_SYSTEM(steerPlayer) {
  auto [player, position, velocity, dash] = context.query<const Position, Velocity, Dash>(oly::ecs::With<Player>{}).single();
  if (dash.active != 0) {
    return;
  }

  const arena::Direction movement = limitedToUnit(context.actions().axis2d(actions::move));
  velocity = Velocity{movement.x * arena::PlayerSpeed, movement.y * arena::PlayerSpeed};
  if (!context.actions().button(actions::dash).pressed || dash.ready == 0) {
    return;
  }

  const arena::Direction direction = dashDirection(movement, context.actions().pointer(actions::aim), position);
  if (direction.x == 0.0f && direction.y == 0.0f) {
    return;
  }

  dash.ready = 0;
  dash.active = 1;
  velocity = Velocity{direction.x * dash.speed, direction.y * dash.speed};
  context.schedule().after(dash.duration, endDash, arena::PlayerTarget{player});
  context.schedule().after(dash.recharge, rechargeDash, arena::PlayerTarget{player});
}

OLY_SYSTEM(chasePlayer) {
  const Position target = std::get<1>(context.query<const Position>(oly::ecs::With<Player>{}).single());

  context.query<const Position, const Chaser, Velocity>(oly::ecs::With<Enemy>{}).each([target](const Position& position, const Chaser& chaser, Velocity& velocity) {
    const arena::Direction direction = arena::directionTo(position.x, position.y, target.x, target.y);
    velocity = Velocity{direction.x * chaser.speed, direction.y * chaser.speed};
  });
}

OLY_SYSTEM(integrate) {
  const float deltaTime = context.fixedDeltaTime();

  context.query<Position, const Velocity>().each([deltaTime](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}

OLY_SYSTEM(confine) {
  auto [player, position, collider] = context.query<Position, const Collider>(oly::ecs::With<Player>{}).single();
  position.x = std::clamp(position.x, collider.radius, arena::ArenaWidth - collider.radius);
  position.y = std::clamp(position.y, collider.radius, arena::ArenaHeight - collider.radius);

  context.query<const Position>(oly::ecs::With<Bullet>{}).each([&context](oly::ecs::Entity bullet, const Position& bulletPosition) {
    if (arena::isOutsideArena(bulletPosition.x, bulletPosition.y)) {
      context.destroy(bullet);
    }
  });
}
