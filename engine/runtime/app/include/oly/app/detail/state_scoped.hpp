#pragma once

namespace oly::app::detail {
  // Scope tag: every entity created with an active state receives it, and a state change destroys the ones that still have it.
  struct StateScoped {};
}
