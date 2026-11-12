#pragma once

#include <cstdint>

namespace oly::app {
  using EventTypeId = uint32_t;
  using HandlerId = uint32_t;
  using EventKey = uint32_t;

  inline constexpr EventTypeId InvalidEventType = 0xFFFFFFFFu;

  namespace detail {
    EventKey nextEventKey() noexcept;
  }

  // Dense per process and separate from ComponentId: events are not components.
  template <class T>
  EventKey eventKey() noexcept {
    static const EventKey key = detail::nextEventKey();
    return key;
  }
}
