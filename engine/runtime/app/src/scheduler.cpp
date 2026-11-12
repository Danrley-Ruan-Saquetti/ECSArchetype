#include "oly/app/scheduler.hpp"

#include <cmath>

#include "oly/app/engine.hpp"
#include "oly/core/assert.hpp"

namespace oly::app {
  TaskHandle Scheduler::schedule(double delaySeconds, double intervalSeconds, TaskFunction function, const TaskPayload& payload, TaskDiscardFunction discard) {
    OLY_ASSERT(std::isfinite(delaySeconds) && delaySeconds >= 0.0, "schedule: delay=%f negative or not finite", delaySeconds);
    OLY_ASSERT(std::isfinite(intervalSeconds) && intervalSeconds >= 0.0, "schedule: interval=%f negative or not finite", intervalSeconds);
    OLY_ASSERT(function != nullptr, "schedule: null function");

    return queue_.push(engine_.time() + delaySeconds, intervalSeconds, function, discard, payload, engine_.state() != NoState);
  }

  void Scheduler::requirePositiveInterval([[maybe_unused]] double intervalSeconds) {
    OLY_ASSERT(std::isfinite(intervalSeconds) && intervalSeconds > 0.0, "every: interval=%f must be positive and finite", intervalSeconds);
  }
}
