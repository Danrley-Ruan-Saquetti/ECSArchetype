#include "oly/app/detail/event_queues.hpp"

#include <algorithm>
#include <cstring>

namespace oly::app::detail {
  EventTypeId EventQueues::declare(EventKey key, std::string_view name, uint32_t size) {
    const EventTypeId type = static_cast<EventTypeId>(queues_.size());

    if (typeByKey_.size() <= key) {
      typeByKey_.resize(key + 1, InvalidEventType);
    }
    typeByKey_[key] = type;
    queues_.push_back(Queue{std::string(name), size, {}, {}});

    return type;
  }

  EventTypeId EventQueues::typeOf(EventKey key) const noexcept {
    return key < typeByKey_.size() ? typeByKey_[key] : InvalidEventType;
  }

  uint32_t EventQueues::typeCount() const noexcept {
    return static_cast<uint32_t>(queues_.size());
  }

  std::string_view EventQueues::name(EventTypeId type) const {
    return queues_[type].name;
  }

  void EventQueues::push(EventTypeId type, const void* event) {
    Queue& queue = queues_[type];
    const std::size_t offset = queue.pending.size();

    queue.pending.resize(offset + queue.size);
    std::memcpy(queue.pending.data() + offset, event, queue.size);
  }

  bool EventQueues::beginRound() {
    bool any = false;

    for (Queue& queue : queues_) {
      if (!queue.pending.empty()) {
        queue.pending.swap(queue.dispatching);
        any = true;
      }
    }

    return any;
  }

  const std::byte* EventQueues::batchData(EventTypeId type) const {
    return queues_[type].dispatching.data();
  }

  uint32_t EventQueues::batchCount(EventTypeId type) const {
    const Queue& queue = queues_[type];
    return static_cast<uint32_t>(queue.dispatching.size() / queue.size);
  }

  void EventQueues::endRound() {
    for (Queue& queue : queues_) {
      queue.dispatching.clear();
    }
  }

  void EventQueues::clear() {
    for (Queue& queue : queues_) {
      queue.pending.clear();
      queue.dispatching.clear();
    }
  }

  std::string EventQueues::pendingNames() const {
    std::string names;

    for (const Queue& queue : queues_) {
      if (queue.pending.empty() && queue.dispatching.empty()) {
        continue;
      }

      if (!names.empty()) {
        names += ", ";
      }
      names += queue.name;
    }

    return names;
  }
}
