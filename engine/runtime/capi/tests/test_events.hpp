#pragma once

#include <cstdint>

struct CapiTestEvent {
  uint32_t value;
  float amount;
};

// Not in the test registry: used to prove that a handler of another type is rejected.
struct CapiOtherEvent {
  uint32_t value;
};
