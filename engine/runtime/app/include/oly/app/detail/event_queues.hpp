#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "oly/app/event.hpp"

namespace oly::app::detail {
  // Per type, two queues of raw bytes in the struct's layout: `pending` receives the emissions and `dispatching` is the batch
  // of the current round. Emitting during the round writes to `pending`, so the batch never changes or moves.
  class EventQueues {
   private:
    struct Queue {
      std::string name;
      uint32_t size;
      std::vector<std::byte> pending;
      std::vector<std::byte> dispatching;
    };

    std::vector<Queue> queues_;
    std::vector<EventTypeId> typeByKey_; // [EventKey]

   public:
    EventTypeId declare(EventKey key, std::string_view name, uint32_t size);
    EventTypeId typeOf(EventKey key) const noexcept;
    uint32_t typeCount() const noexcept;
    std::string_view name(EventTypeId type) const;

    void push(EventTypeId type, const void* event);

    // Swaps `pending` and `dispatching` of the types with pending events; false if nothing is pending.
    bool beginRound();
    const std::byte* batchData(EventTypeId type) const;
    uint32_t batchCount(EventTypeId type) const;
    // Keeps the capacity: after warm-up, dispatch does not allocate.
    void endRound();

    void clear();

    std::string pendingNames() const;
  };
}
