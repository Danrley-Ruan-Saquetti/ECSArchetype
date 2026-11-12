#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

#include "arena.hpp"
#include "oly/app/handler.hpp"
#include "oly/app/system.hpp"
#include "oly/app/task_context.hpp"
#include "oly/ecs/query.hpp"
#include "schema.generated.hpp"

namespace {
  constexpr uint32_t CoinSeed = 20261112;

  // Semente fixa: o std::minstd_rand dá a mesma sequência em qualquer biblioteca padrão (as distribuições não).
  std::minstd_rand coinGenerator(CoinSeed);

  void endInvulnerability(oly::app::TaskContext& context) {
    const oly::ecs::Entity player = context.payload<arena::PlayerTarget>().player;
    if (context.isAlive(player)) {
      context.remove<Invulnerable>(player);
    }
  }

  // Cada bala acerta no máximo um inimigo. Duas balas no mesmo inimigo emitem duas mortes.
  void hitEnemiesWithBullets(oly::app::SystemContext& context) {
    auto& enemies = context.query<const Position, const Collider, Health>(oly::ecs::With<Enemy>{});

    context.query<const Position, const Collider>(oly::ecs::With<Bullet>{}).each([&](oly::ecs::Entity bullet, const Position& bulletPosition, const Collider& bulletCollider) {
      bool hit = false;
      enemies.each([&](oly::ecs::Entity enemy, const Position& enemyPosition, const Collider& enemyCollider, Health& health) {
        if (hit || !arena::touches(bulletPosition.x, bulletPosition.y, bulletCollider.radius, enemyPosition.x, enemyPosition.y, enemyCollider.radius)) {
          return;
        }

        hit = true;
        health.value -= 1;
        context.destroy(bullet);
        if (health.value <= 0) {
          context.emit(EnemyDied{enemy, enemyPosition.x, enemyPosition.y});
        }
      });
    });
  }

  void hitPlayerWithEnemies(oly::app::SystemContext& context) {
    auto [player, playerPosition, playerCollider, health] = context.query<const Position, const Collider, Health>(oly::ecs::With<Player>{}).single();
    if (context.has<Invulnerable>(player)) {
      return;
    }

    const Position position = playerPosition;
    const float radius = playerCollider.radius;
    bool touched = false;
    context.query<const Position, const Collider>(oly::ecs::With<Enemy>{}).each([&](const Position& enemyPosition, const Collider& enemyCollider) {
      touched = touched || arena::touches(position.x, position.y, radius, enemyPosition.x, enemyPosition.y, enemyCollider.radius);
    });
    if (!touched) {
      return;
    }

    health.value -= 1;
    context.add(player, Invulnerable{});
    context.schedule().after(arena::InvulnerableSeconds, endInvulnerability, arena::PlayerTarget{player});
    if (health.value <= 0) {
      context.emit(PlayerDied{player});
    }
  }

  void collectCoinsTouchingPlayer(oly::app::SystemContext& context) {
    auto [player, playerPosition, playerCollider] = context.query<const Position, const Collider>(oly::ecs::With<Player>{}).single();
    const Position position = playerPosition;
    const float radius = playerCollider.radius;

    context.query<const Position, const Collider>(oly::ecs::With<Coin>{}).each([&](oly::ecs::Entity coin, const Position& coinPosition, const Collider& coinCollider) {
      if (arena::touches(position.x, position.y, radius, coinPosition.x, coinPosition.y, coinCollider.radius)) {
        context.emit(CoinCollected{coin});
      }
    });
  }
}

OLY_SYSTEM(fireWeapon) {
  auto [player, position, weapon] = context.query<const Position, Weapon>(oly::ecs::With<Player>{}).single();
  weapon.cooldown = std::max(0.0f, weapon.cooldown - context.fixedDeltaTime());
  if (!context.actions().button(actions::shoot).held || weapon.cooldown > 0.0f) {
    return;
  }

  const oly::input::PointerState aim = context.actions().pointer(actions::aim);
  if (!aim.valid) {
    return;
  }

  const arena::Direction direction = arena::directionTo(position.x, position.y, aim.x, aim.y);
  if (direction.x == 0.0f && direction.y == 0.0f) {
    return;
  }

  weapon.cooldown = weapon.interval;
  context.spawn(Bullet{}, Position{position.x, position.y}, Velocity{direction.x * arena::BulletSpeed, direction.y * arena::BulletSpeed}, Collider{arena::BulletRadius});
}

OLY_SYSTEM(collide) {
  hitEnemiesWithBullets(context);
  hitPlayerWithEnemies(context);
  collectCoinsTouchingPlayer(context);
}

OLY_HANDLER(dropCoin, EnemyDied) {
  std::vector<oly::ecs::Entity> seen;
  for (const EnemyDied& died : events) {
    if (std::find(seen.begin(), seen.end(), died.entity) != seen.end()) {
      continue;
    }

    seen.push_back(died.entity);
    if (coinGenerator() % 100 < arena::CoinDropPercent) {
      context.spawn(Coin{}, Position{died.x, died.y}, Collider{arena::CoinRadius});
    }
  }
}

OLY_HANDLER(destroyDead, EnemyDied) {
  for (const EnemyDied& died : events) {
    if (context.isAlive(died.entity)) {
      context.destroy(died.entity);
    }
  }
}
