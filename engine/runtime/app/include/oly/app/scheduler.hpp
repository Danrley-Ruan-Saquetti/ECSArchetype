#pragma once

#include <cstdint>

#include "oly/app/detail/task_queue.hpp"
#include "oly/app/task.hpp"

namespace oly::app {
  class Engine;

  // Tasks on the game clock (Engine::time()). Due time = time() + delay: inside a step, time() is the start
  // of the step; outside frame (and in the render phase), the start of the next one. Due tasks run in stage 2 of the fixed step,
  // only in unpaused steps, in due-time order and, on ties, in creation order. A task created during stage 2
  // only runs from the next step on. With an active state the task is born scoped: a state change cancels it
  // (with discard) unless it receives persist.
  class Scheduler {
   private:
    friend class Engine;

    Engine& engine_;
    detail::TaskQueue queue_;

   public:
    explicit Scheduler(Engine& engine) noexcept
      : engine_(engine) {}
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Base of the templates and of the C API. intervalSeconds 0 schedules an after; greater than 0, an every with the first firing
    // at time() + delaySeconds. Delay and interval must be finite and non-negative, and the function cannot be
    // null (OLY_ASSERT). The discard, if any, runs once with the payload when the task leaves the queue without its last
    // run (cancel, Engine destruction); never after the last firing of an after.
    TaskHandle schedule(double delaySeconds, double intervalSeconds, TaskFunction function, const TaskPayload& payload = {}, TaskDiscardFunction discard = nullptr);

    TaskHandle after(double delaySeconds, TaskFunction function) {
      return schedule(delaySeconds, 0.0, function);
    }
    template <class P>
    TaskHandle after(double delaySeconds, TaskFunction function, const P& payload, TaskDiscardFunction discard = nullptr) {
      return schedule(delaySeconds, 0.0, function, TaskPayload::of(payload), discard);
    }

    // The interval must be positive and finite (OLY_ASSERT). The next due time does not drift and each task fires
    // at most once per step.
    TaskHandle every(double intervalSeconds, TaskFunction function) {
      requirePositiveInterval(intervalSeconds);

      return schedule(intervalSeconds, intervalSeconds, function);
    }
    template <class P>
    TaskHandle every(double intervalSeconds, TaskFunction function, const P& payload, TaskDiscardFunction discard = nullptr) {
      requirePositiveInterval(intervalSeconds);

      return schedule(intervalSeconds, intervalSeconds, function, TaskPayload::of(payload), discard);
    }

    // false for a task already finished or cancelled and for TaskHandle{} (no task); an after that cancels itself
    // has already left the queue. A slot out of range is a contract violation.
    bool cancel(TaskHandle task) {
      return queue_.cancel(task);
    }
    bool isPending(TaskHandle task) const {
      return queue_.isPending(task);
    }
    // Removes the scope of a pending task; false if it is not pending.
    bool persist(TaskHandle task) {
      return queue_.persist(task);
    }
    uint32_t pendingCount() const noexcept {
      return queue_.pendingCount();
    }

   private:
    static void requirePositiveInterval(double intervalSeconds);
  };
}
