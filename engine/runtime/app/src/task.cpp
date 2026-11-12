#include "oly/app/task.hpp"

namespace oly::app::detail {
  PayloadKey nextPayloadKey() noexcept {
    static PayloadKey next = 0;
    return next++;
  }
}
