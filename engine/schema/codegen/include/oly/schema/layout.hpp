#pragma once

#include <cstdint>
#include <vector>

#include "oly/schema/schema.hpp"

namespace oly::schema {
  uint32_t scalarSize(ScalarType type);

  // `entity` is oly::ecs::Entity: two u32, alignment 4.
  uint32_t scalarAlignment(ScalarType type);

  // Natural alignment in field order: fills each field's offset and returns the struct's size and alignment.
  Layout computeLayout(std::vector<Field>& fields);
}
