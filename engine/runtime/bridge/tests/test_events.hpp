#pragma once

#include <cstdint>

struct BridgeTestEvent {
  uint32_t value;
  float amount;
};

// Not in the test registry: used to prove that a handler of another type is rejected.
struct BridgeOtherEvent {
  uint32_t value;
};
