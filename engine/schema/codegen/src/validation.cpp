#include "validation.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace oly::schema {
  namespace {
    // The names become C++ identifiers (struct names, fields, actions::name, states::name).
    constexpr std::string_view cppReservedWords[] = {
      "class", "struct", "int", "float", "double", "char", "long", "short", "unsigned", "signed", "bool", "void", "auto", "return",
      "operator", "template", "namespace", "new", "delete", "this", "default", "case", "switch", "register", "union", "enum", "const",
      "static", "virtual", "public", "private", "protected", "friend", "using", "typename", "do", "if", "else", "for", "while", "goto",
      "break", "continue", "try", "catch", "throw", "true", "false", "nullptr", "sizeof", "alignof", "export", "extern", "inline",
      "mutable", "volatile", "explicit", "concept", "requires", "and", "or", "not", "xor"};

    enum class DeclarationKind { Component,
                                 Tag,
                                 Event,
                                 Action,
                                 State };

    struct Declaration {
      DeclarationKind kind;
      const Type* type;
      const std::string* name;
      SourceLocation location;
    };

    bool isLetter(char character, bool upper) {
      return upper ? (character >= 'A' && character <= 'Z') : (character >= 'a' && character <= 'z');
    }

    bool isAlphanumeric(char character) {
      return isLetter(character, true) || isLetter(character, false) || (character >= '0' && character <= '9');
    }

    bool hasShape(std::string_view name, bool upperFirst) {
      return !name.empty() && isLetter(name.front(), upperFirst) && std::all_of(name.begin(), name.end(), isAlphanumeric);
    }

    bool isPascalCase(std::string_view name) {
      return hasShape(name, true);
    }

    bool isCamelCase(std::string_view name) {
      return hasShape(name, false) && std::find(std::begin(cppReservedWords), std::end(cppReservedWords), name) == std::end(cppReservedWords);
    }

    std::string camelCaseMessage(std::string_view what, std::string_view name) {
      return "invalid " + std::string(what) + " \"" + std::string(name) + "\"";
    }

    std::vector<Declaration> inSourceOrder(const Schema& schema) {
      std::vector<Declaration> declarations;
      for (const Type& type : schema.types) {
        declarations.push_back({type.kind == TypeKind::Tag ? DeclarationKind::Tag : DeclarationKind::Component, &type, &type.name, type.location});
      }

      for (const Type& event : schema.events) {
        declarations.push_back({DeclarationKind::Event, &event, &event.name, event.location});
      }

      for (const Action& action : schema.actions) {
        declarations.push_back({DeclarationKind::Action, nullptr, &action.name, action.location});
      }

      for (const State& state : schema.states) {
        declarations.push_back({DeclarationKind::State, nullptr, &state.name, state.location});
      }

      std::stable_sort(declarations.begin(), declarations.end(), [](const Declaration& left, const Declaration& right) {
        return left.location.line != right.location.line ? left.location.line < right.location.line : left.location.column < right.location.column;
      });

      return declarations;
    }

    void validateName(const Schema& schema, const Declaration& declaration) {
      const bool isType = declaration.type != nullptr;
      if (isType && !isPascalCase(*declaration.name)) {
        throw SchemaError(schema.fileName, declaration.location, "invalid type name \"" + *declaration.name + "\": use PascalCase ([A-Z][A-Za-z0-9]*)");
      }

      if (!isType && !isCamelCase(*declaration.name)) {
        const std::string_view what = declaration.kind == DeclarationKind::Action ? "action" : "state";
        throw SchemaError(schema.fileName, declaration.location, camelCaseMessage(what, *declaration.name) + ": use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words");
      }
    }

    void validateFields(const Schema& schema, const Declaration& declaration) {
      const Type& type = *declaration.type;
      if (type.fields.empty() && declaration.kind == DeclarationKind::Component) {
        throw SchemaError(schema.fileName, declaration.location, "component " + type.name + " has no fields: use tag " + type.name);
      }

      if (type.fields.empty() && declaration.kind == DeclarationKind::Event) {
        throw SchemaError(schema.fileName, declaration.location, "event " + type.name + " has no fields: an event needs at least one field");
      }

      std::set<std::string_view> seen;
      for (const Field& field : type.fields) {
        if (!isCamelCase(field.name)) {
          throw SchemaError(schema.fileName, field.location, camelCaseMessage("field", field.name) + " in " + type.name + ": use camelCase ([a-z][A-Za-z0-9]*) and avoid C++ reserved words");
        }

        if (!seen.insert(field.name).second) {
          throw SchemaError(schema.fileName, field.location, "repeated field \"" + field.name + "\" in " + type.name);
        }
      }
    }
  }

  void validateSchema(const Schema& schema) {
    std::set<std::string_view> names;
    std::size_t stateCount = 0;
    for (const Declaration& declaration : inSourceOrder(schema)) {
      validateName(schema, declaration);
      if (declaration.type != nullptr) {
        validateFields(schema, declaration);
      }

      if (!names.insert(*declaration.name).second) {
        throw SchemaError(schema.fileName, declaration.location, "repeated name \"" + *declaration.name + "\"");
      }

      if (declaration.kind == DeclarationKind::State && ++stateCount > MaxStates) {
        throw SchemaError(schema.fileName, declaration.location, std::to_string(stateCount) + " states; the maximum is " + std::to_string(MaxStates));
      }
    }
  }
}
