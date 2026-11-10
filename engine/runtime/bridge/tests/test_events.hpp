#pragma once

#include <cstdint>

struct BridgeTestEvent {
  uint32_t value;
  float amount;
};

// Fora do registro de teste: serve para provar que um handler de outro tipo é recusado.
struct BridgeOtherEvent {
  uint32_t value;
};
