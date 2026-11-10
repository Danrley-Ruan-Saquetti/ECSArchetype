#include <cstdint>

#include "oly/bridge/c_api.hpp"
#include "oly/bridge/world.hpp"
#include "oly/ecs/entity.hpp"
#include "schema.generated.hpp"

extern "C" {
OLY_API void fixtureMove(OlyWorld* world, float deltaTime) {
  oly::bridge::worldOf(world).query<Position, const Velocity>().each([deltaTime](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}

OLY_API double fixtureSumPositionX(OlyWorld* world) {
  double sum = 0;
  oly::bridge::worldOf(world).query<const Position>().each([&sum](const Position& position) { sum += position.x; });

  return sum;
}

OLY_API double fixtureMixedChecksum(OlyWorld* world, uint32_t index, uint32_t generation) {
  const Mixed& mixed = oly::bridge::worldOf(world).get<Mixed>(oly::ecs::Entity{index, generation});

  return mixed.a + mixed.b + mixed.c + mixed.owner.index + mixed.owner.generation;
}
}
