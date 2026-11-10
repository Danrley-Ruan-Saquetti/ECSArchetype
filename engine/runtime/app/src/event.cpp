#include "oly/app/event.hpp"

namespace oly::app::detail {
  EventKey nextEventKey() noexcept {
    static EventKey next = 0;
    return next++;
  }
}
