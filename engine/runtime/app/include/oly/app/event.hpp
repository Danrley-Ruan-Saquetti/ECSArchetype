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

  // Denso por processo e separado dos ComponentId: eventos não são componentes.
  template <class T>
  EventKey eventKey() noexcept {
    static const EventKey key = detail::nextEventKey();
    return key;
  }
}
