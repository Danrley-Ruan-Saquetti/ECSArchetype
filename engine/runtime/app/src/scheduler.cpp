#include "oly/app/scheduler.hpp"

#include <cmath>

#include "oly/app/engine.hpp"
#include "oly/core/assert.hpp"

namespace oly::app {
  TaskHandle Scheduler::schedule(double delaySeconds, double intervalSeconds, TaskFunction function, const TaskPayload& payload, TaskDiscardFunction discard) {
    OLY_ASSERT(std::isfinite(delaySeconds) && delaySeconds >= 0.0, "schedule: atraso=%f negativo ou não finito", delaySeconds);
    OLY_ASSERT(std::isfinite(intervalSeconds) && intervalSeconds >= 0.0, "schedule: intervalo=%f negativo ou não finito", intervalSeconds);
    OLY_ASSERT(function != nullptr, "schedule: função nula");

    return queue_.push(engine_.time() + delaySeconds, intervalSeconds, function, discard, payload);
  }

  void Scheduler::requirePositiveInterval([[maybe_unused]] double intervalSeconds) {
    OLY_ASSERT(std::isfinite(intervalSeconds) && intervalSeconds > 0.0, "every: intervalo=%f deve ser positivo e finito", intervalSeconds);
  }
}
