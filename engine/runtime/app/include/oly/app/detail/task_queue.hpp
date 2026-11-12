#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "oly/app/task.hpp"

namespace oly::app::detail {
  struct DueTask {
    TaskFunction function;
    TaskHandle handle;
    bool lastRun;
    TaskPayload payload;
  };

  // Min-heap by (due time, sequence) over generational slots. Cancelling only frees the slot; the heap entry stays
  // and is dropped when it reaches the top. It does not know the Engine: the due time comes ready from the caller that schedules.
  class TaskQueue {
   private:
    struct Slot {
      TaskFunction function = nullptr;
      TaskDiscardFunction discard = nullptr;
      TaskPayload payload;
      // The every's due time is anchor + periods * interval, not a repeated sum: the rounding error of the
      // sum grows and, after a few minutes, the firing would stop reaching the step limit.
      double anchor = 0.0;
      double periods = 0.0;
      double interval = 0.0;
      uint32_t generation = 0;
      bool live = false;
      bool scoped = false;
    };

    struct HeapEntry {
      double due;
      uint64_t sequence;
      uint32_t slot;
      uint32_t generation;
    };

    std::vector<Slot> slots_;
    std::vector<uint32_t> freeSlots_;
    std::vector<HeapEntry> heap_;
    // Tasks created during the run wait here: with the limit's tolerance, a new one due "now"
    // would sit at the top ahead of an old overdue one and block it.
    std::vector<HeapEntry> deferred_;
    uint64_t nextSequence_ = 0;
    uint32_t pendingCount_ = 0;
    bool running_ = false;

   public:
    TaskQueue() = default;
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;
    ~TaskQueue();

    // The queue validates neither delay nor function; that belongs to the Scheduler. interval 0 is a single-run task.
    // A scoped task is cancelled by cancelScoped (state change) until it receives persist.
    TaskHandle push(double due, double interval, TaskFunction function, TaskDiscardFunction discard, const TaskPayload& payload, bool scoped = false);
    // The default handle and a stale handle (reused slot) return false. A slot out of range is a contract violation.
    // The discard runs with a copy of the payload after freeing the slot, so it may touch the queue.
    bool cancel(TaskHandle task);
    bool isPending(TaskHandle task) const;
    // Removes the scope of a pending task; false for the default handle or a task that is not pending.
    bool persist(TaskHandle task);
    uint32_t pendingCount() const noexcept {
      return pendingCount_;
    }

    void beginRun() noexcept;
    // An every goes back to the heap with the next due time, without drift and always after limit, so it yields once per
    // call; an after leaves the queue (without discard) before being returned.
    std::optional<DueTask> popDue(double limit);
    void endRun();
    void recoverFromInterruptedRun();

    // Discards each pending task once, including those scheduled by discards.
    void discardAll();
    // Cancels, with discard, each scoped task, including the scoped ones scheduled by discards.
    void cancelScoped();

   private:
    bool matches(TaskHandle task, const char* operation) const;
    void release(uint32_t slot);
    double advance(Slot& slot, double limit) const;
    void flushDeferred();
  };
}
