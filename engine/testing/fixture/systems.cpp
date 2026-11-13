#include <cstdint>

#include "oly/app/handler.hpp"
#include "oly/app/state_context.hpp"
#include "oly/app/system.hpp"
#include "oly/app/task_context.hpp"
#include "oly/capi/c_api.hpp"
#include "oly/capi/engine.hpp"
#include "oly/capi/world.hpp"
#include "oly/ecs/entity.hpp"
#include "schema.generated.hpp"

namespace {
  uint32_t fixtureFirePressCount = 0;
  uint32_t fixturePingCount = 0;
  double fixturePingSum = 0;

  struct AppendDigit {
    oly::ecs::Entity target;
    uint32_t digit;
  };

  void appendDigitTask(oly::app::TaskContext& context) {
    const AppendDigit& payload = context.payload<AppendDigit>();
    Trace& trace = context.get<Trace>(payload.target);
    trace.value = trace.value * 10 + payload.digit;
  }
}

extern "C" {
OLY_API void fixtureMove(OlyWorld* world, float deltaTime) {
  oly::capi::worldOf(world).query<Position, const Velocity>().each([deltaTime](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}

OLY_API double fixtureSumPositionX(OlyWorld* world) {
  double sum = 0;
  oly::capi::worldOf(world).query<const Position>().each([&sum](const Position& position) { sum += position.x; });

  return sum;
}

OLY_API void fixtureSetPingCount(uint32_t count) {
  fixturePingCount = count;
}

OLY_API uint32_t fixtureTakeFirePressCount() {
  const uint32_t count = fixtureFirePressCount;
  fixtureFirePressCount = 0;

  return count;
}

OLY_API void fixtureScheduleDigit(OlyEngine* engine, double delaySeconds, uint32_t index, uint32_t generation, uint32_t digit) {
  oly::capi::engineOf(engine).schedule().after(delaySeconds, appendDigitTask, AppendDigit{oly::ecs::Entity{index, generation}, digit});
}

OLY_API double fixtureTakePingSum() {
  const double sum = fixturePingSum;
  fixturePingSum = 0;

  return sum;
}

OLY_API double fixtureMixedChecksum(OlyWorld* world, uint32_t index, uint32_t generation) {
  const Mixed& mixed = oly::capi::worldOf(world).get<Mixed>(oly::ecs::Entity{index, generation});

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

OLY_SYSTEM(countFirePresses) {
  if (context.actions().button(actions::fire).pressed) {
    ++fixtureFirePressCount;
  }
}

OLY_SYSTEM(appendNine) {
  context.query<Trace>().each([](Trace& trace) { trace.value = trace.value * 10 + 9; });
}

// Large enough for the WASM heap to grow before the next TS system.
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

OLY_STATE_HOOK(appendEight) {
  context.query<Trace>().each([](Trace& trace) { trace.value = trace.value * 10 + 8; });
}
