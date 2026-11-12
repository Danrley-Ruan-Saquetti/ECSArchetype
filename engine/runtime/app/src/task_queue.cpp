#include "oly/app/detail/task_queue.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "oly/core/assert.hpp"

namespace oly::app::detail {
  namespace {
    bool later(const auto& left, const auto& right) {
      if (left.due != right.due) {
        return left.due > right.due;
      }

      return left.sequence > right.sequence;
    }
  }

  TaskQueue::~TaskQueue() {
    discardAll();
  }

  TaskHandle TaskQueue::push(double due, double interval, TaskFunction function, TaskDiscardFunction discard, const TaskPayload& payload, bool scoped) {
    uint32_t index;
    if (freeSlots_.empty()) {
      index = static_cast<uint32_t>(slots_.size());
      slots_.emplace_back();
    } else {
      index = freeSlots_.back();
      freeSlots_.pop_back();
    }

    Slot& slot = slots_[index];
    slot.function = function;
    slot.discard = discard;
    slot.payload = payload;
    slot.anchor = due;
    slot.periods = 0.0;
    slot.interval = interval;
    slot.live = true;
    slot.scoped = scoped;
    ++pendingCount_;

    const HeapEntry entry{due, nextSequence_++, index, slot.generation};
    if (running_) {
      deferred_.push_back(entry);
    } else {
      heap_.push_back(entry);
      std::push_heap(heap_.begin(), heap_.end(), later<HeapEntry, HeapEntry>);
    }

    return TaskHandle{index, slot.generation};
  }

  bool TaskQueue::cancel(TaskHandle task) {
    if (!matches(task, "cancel")) {
      return false;
    }

    const TaskDiscardFunction discard = slots_[task.slot].discard;
    TaskPayload payload = slots_[task.slot].payload;
    release(task.slot);

    if (discard != nullptr) {
      discard(payload.bytes);
    }

    return true;
  }

  bool TaskQueue::isPending(TaskHandle task) const {
    return matches(task, "isPending");
  }

  bool TaskQueue::persist(TaskHandle task) {
    if (!matches(task, "persist")) {
      return false;
    }

    slots_[task.slot].scoped = false;

    return true;
  }

  void TaskQueue::beginRun() noexcept {
    running_ = true;
  }

  std::optional<DueTask> TaskQueue::popDue(double limit) {
    while (!heap_.empty()) {
      const HeapEntry top = heap_.front();
      Slot& slot = slots_[top.slot];

      if (!slot.live || slot.generation != top.generation) {
        std::pop_heap(heap_.begin(), heap_.end(), later<HeapEntry, HeapEntry>);
        heap_.pop_back();
        continue;
      }

      if (top.due > limit) {
        return std::nullopt;
      }

      std::pop_heap(heap_.begin(), heap_.end(), later<HeapEntry, HeapEntry>);
      heap_.pop_back();

      DueTask due{slot.function, TaskHandle{top.slot, top.generation}, slot.interval == 0.0, slot.payload};
      if (due.lastRun) {
        release(top.slot);
      } else {
        heap_.push_back(HeapEntry{advance(slot, limit), top.sequence, top.slot, top.generation});
        std::push_heap(heap_.begin(), heap_.end(), later<HeapEntry, HeapEntry>);
      }

      return due;
    }

    return std::nullopt;
  }

  void TaskQueue::endRun() {
    running_ = false;
    flushDeferred();
  }

  void TaskQueue::recoverFromInterruptedRun() {
    running_ = false;
    flushDeferred();
  }

  void TaskQueue::discardAll() {
    while (pendingCount_ > 0) {
      for (uint32_t index = 0; index < slots_.size(); ++index) {
        if (slots_[index].live) {
          cancel(TaskHandle{index, slots_[index].generation});
        }
      }
    }

    heap_.clear();
    deferred_.clear();
  }

  void TaskQueue::cancelScoped() {
    bool canceledAny = true;
    while (canceledAny) {
      canceledAny = false;
      for (uint32_t index = 0; index < slots_.size(); ++index) {
        if (slots_[index].live && slots_[index].scoped) {
          cancel(TaskHandle{index, slots_[index].generation});
          canceledAny = true;
        }
      }
    }
  }

  bool TaskQueue::matches(TaskHandle task, const char* operation) const {
    if (task.slot == InvalidTaskSlot) {
      return false;
    }

    OLY_ASSERT(task.slot < slots_.size(), "%s: slot=%u out of range (%u slots)", operation, task.slot, static_cast<uint32_t>(slots_.size()));
    (void)operation;

    if (task.slot >= slots_.size()) {
      return false;
    }

    const Slot& slot = slots_[task.slot];

    return slot.live && slot.generation == task.generation;
  }

  void TaskQueue::release(uint32_t index) {
    Slot& slot = slots_[index];
    slot.live = false;
    ++slot.generation;
    freeSlots_.push_back(index);
    --pendingCount_;
  }

  double TaskQueue::advance(Slot& slot, double limit) const {
    slot.periods += 1.0;
    double next = slot.anchor + slot.periods * slot.interval;
    if (next <= limit) {
      slot.periods = std::floor((limit - slot.anchor) / slot.interval) + 1.0;
      next = slot.anchor + slot.periods * slot.interval;
    }

    // An interval smaller than one ulp of the limit (or denormal) does not move; without this the task would repeat in the same
    // round or get stuck at infinity.
    if (!(next > limit) || !std::isfinite(next)) {
      next = std::nextafter(limit, std::numeric_limits<double>::infinity());
    }

    return next;
  }

  void TaskQueue::flushDeferred() {
    for (const HeapEntry& entry : deferred_) {
      heap_.push_back(entry);
      std::push_heap(heap_.begin(), heap_.end(), later<HeapEntry, HeapEntry>);
    }

    deferred_.clear();
  }
}
