#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oly::schema {
  enum class ScalarType : uint8_t { F32,
                                    F64,
                                    I8,
                                    U8,
                                    I16,
                                    U16,
                                    I32,
                                    U32,
                                    Entity };

  enum class ActionKind : uint8_t { Button,
                                    Axis2d,
                                    Pointer };

  enum class TypeKind : uint8_t { Component,
                                  Tag,
                                  Event };

  // 1-based; the column counts bytes.
  struct SourceLocation {
    uint32_t line = 1;
    uint32_t column = 1;
  };

  struct Field {
    std::string name{};
    ScalarType type = ScalarType::F32;
    uint32_t offset = 0;
    SourceLocation location{};
  };

  struct Layout {
    uint32_t size = 0;
    uint32_t alignment = 1;
  };

  // A component, a tag (no fields) or an event. `location` is the name's.
  struct Type {
    TypeKind kind = TypeKind::Component;
    std::string name{};
    std::vector<Field> fields{};
    Layout layout{};
    SourceLocation location{};
  };

  struct Action {
    std::string name{};
    ActionKind kind = ActionKind::Button;
    SourceLocation location{};
  };

  struct State {
    std::string name{};
    SourceLocation location{};
  };

  // Each group in declaration order, which is its id order: the ComponentId order for `types`, the dispatch order for
  // `events`, the ActionId for `actions` and the StateId for `states`.
  struct Schema {
    std::string fileName{};
    std::vector<Type> types{};
    std::vector<Type> events{};
    std::vector<Action> actions{};
    std::vector<State> states{};
    uint32_t hash = 0;
  };

  inline constexpr std::size_t MaxStates = 32;

  // A syntax or validation error in a .oly file; what() is "<file>:<line>:<column>: <message>".
  class SchemaError : public std::runtime_error {
   public:
    SchemaError(std::string_view fileName, SourceLocation location, std::string_view message);
  };

  // The .oly spelling: "f32", "entity", "button", "axis2d".
  std::string_view scalarTypeName(ScalarType type);
  std::string_view actionKindName(ActionKind kind);

  // Parses, validates, lays out and hashes a .oly source. `fileName` prefixes the errors and is kept in the Schema.
  // Throws SchemaError at the first error.
  Schema parseSchema(std::string_view source, std::string_view fileName);
}
