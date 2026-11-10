#include <cstdint>

#include "oly/app/handler.hpp"
#include "oly/app/system.hpp"
#include "oly/bridge/c_api.hpp"
#include "oly/bridge/world.hpp"
#include "oly/ecs/entity.hpp"
#include "schema.generated.hpp"

namespace {
  uint32_t fixturePingCount = 0;
  double fixturePingSum = 0;
}

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

OLY_API void fixtureSetPingCount(uint32_t count) {
  fixturePingCount = count;
}

OLY_API double fixtureTakePingSum() {
  const double sum = fixturePingSum;
  fixturePingSum = 0;

  return sum;
}

OLY_API double fixtureMixedChecksum(OlyWorld* world, uint32_t index, uint32_t generation) {
  const Mixed& mixed = oly::bridge::worldOf(world).get<Mixed>(oly::ecs::Entity{index, generation});

  return mixed.a + mixed.b + mixed.c + mixed.owner.index + mixed.owner.generation;
}
}

OLY_SYSTEM(noop) {}

OLY_SYSTEM(move) {
  const float deltaTime = context.fixedDeltaTime();
  context.query<Position, const Velocity>().each([deltaTime](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}

OLY_SYSTEM(appendNine) {
  context.query<Trace>().each([](Trace& trace) { trace.value = trace.value * 10 + 9; });
}

// Grande o bastante para o heap do WASM crescer antes do sistema TS seguinte.
OLY_SYSTEM(spawnBatch) {
  for (uint32_t count = 0; count < 400000; ++count) {
    context.spawn(Position{1, 0});
  }
}

OLY_HANDLER(appendNineOnPing, Ping) {
  for (const Ping& ping : events) {
    Trace& trace = context.get<Trace>(ping.target);
    trace.value = trace.value * 10 + 9;
  }
}

OLY_HANDLER(sumPing, Ping) {
  for (const Ping& ping : events) {
    fixturePingSum += ping.value;
  }
}

OLY_SYSTEM(emitPings) {
  for (uint32_t count = 0; count < fixturePingCount; ++count) {
    context.emit(Ping{oly::ecs::NullEntity, 1});
  }
}
