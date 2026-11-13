#include "oly/schema/layout.hpp"

#include <algorithm>

namespace oly::schema {
  namespace {
    uint32_t alignUp(uint32_t value, uint32_t alignment) {
      return (value + alignment - 1) / alignment * alignment;
    }
  }

  uint32_t scalarSize(ScalarType type) {
    switch (type) {
      case ScalarType::I8:
      case ScalarType::U8:
        return 1;
      case ScalarType::I16:
      case ScalarType::U16:
        return 2;
      case ScalarType::F32:
      case ScalarType::I32:
      case ScalarType::U32:
        return 4;
      case ScalarType::F64:
      case ScalarType::Entity:
        return 8;
    }

    return 0;
  }

  uint32_t scalarAlignment(ScalarType type) {
    return type == ScalarType::Entity ? 4 : scalarSize(type);
  }

  Layout computeLayout(std::vector<Field>& fields) {
    Layout layout;
    uint32_t offset = 0;
    for (Field& field : fields) {
      const uint32_t fieldAlignment = scalarAlignment(field.type);
      offset = alignUp(offset, fieldAlignment);
      field.offset = offset;
      offset += scalarSize(field.type);
      layout.alignment = std::max(layout.alignment, fieldAlignment);
    }

    layout.size = alignUp(offset, layout.alignment);
    return layout;
  }
}
