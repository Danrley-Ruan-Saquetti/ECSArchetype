#include "oly/schema/schema.hpp"

namespace oly::schema {
  SchemaError::SchemaError(std::string_view fileName, SourceLocation location, std::string_view message)
    : std::runtime_error(std::string(fileName) + ":" + std::to_string(location.line) + ":" + std::to_string(location.column) + ": " + std::string(message)) {}

  std::string_view scalarTypeName(ScalarType type) {
    switch (type) {
      case ScalarType::F32:
        return "f32";
      case ScalarType::F64:
        return "f64";
      case ScalarType::I8:
        return "i8";
      case ScalarType::U8:
        return "u8";
      case ScalarType::I16:
        return "i16";
      case ScalarType::U16:
        return "u16";
      case ScalarType::I32:
        return "i32";
      case ScalarType::U32:
        return "u32";
      case ScalarType::Entity:
        return "entity";
    }

    return {};
  }

  std::string_view actionKindName(ActionKind kind) {
    switch (kind) {
      case ActionKind::Button:
        return "button";
      case ActionKind::Axis2d:
        return "axis2d";
      case ActionKind::Pointer:
        return "pointer";
    }

    return {};
  }
}
