#include <cstdint>

#include "oly/ecs/entity.hpp"
#include "oly/bridge/c_api.hpp"
#include "oly/bridge/world_handle.hpp"
#include "schema.generated.hpp"

extern "C" {
ENGINE_API void fixtureMove(EngineWorld* world, float deltaTime) {
  world->world.query<Position, const Velocity>().each([deltaTime](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}

ENGINE_API double fixtureSumPositionX(EngineWorld* world) {
  double sum = 0;
  world->world.query<const Position>().each([&sum](const Position& position) { sum += position.x; });

  return sum;
}

ENGINE_API double fixtureMixedChecksum(EngineWorld* world, uint32_t index, uint32_t generation) {
  const Mixed& mixed = world->world.get<Mixed>(oly::ecs::Entity{index, generation});

  return mixed.a + mixed.b + mixed.c + mixed.owner.index + mixed.owner.generation;
}
}
