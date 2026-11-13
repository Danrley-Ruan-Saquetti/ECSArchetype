#pragma once

#include <cstdint>

#include "oly/app/engine.hpp"
#include "oly/capi/engine_api.hpp"

namespace oly::capi {
  // Access to the Engine behind the C API's opaque handle, for games written in C++.
  oly::app::Engine& engineOf(OlyEngine* engine) noexcept;
}
