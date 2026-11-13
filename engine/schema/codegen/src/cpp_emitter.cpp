#include "oly/schema/cpp_emitter.hpp"

#include <sstream>
#include <string_view>

namespace oly::schema {
  namespace {
    std::string_view cppType(ScalarType type) {
      switch (type) {
      case ScalarType::F32:
        return "float";
      case ScalarType::F64:
        return "double";
      case ScalarType::I8:
        return "int8_t";
      case ScalarType::U8:
        return "uint8_t";
      case ScalarType::I16:
        return "int16_t";
      case ScalarType::U16:
        return "uint16_t";
      case ScalarType::I32:
        return "int32_t";
      case ScalarType::U32:
        return "uint32_t";
      case ScalarType::Entity:
        return "oly::ecs::Entity";
      }

      return {};
    }

    std::string_view cppActionHandle(ActionKind kind) {
      switch (kind) {
      case ActionKind::Button:
        return "ButtonAction";
      case ActionKind::Axis2d:
        return "Axis2dAction";
      case ActionKind::Pointer:
        return "PointerAction";
      }

      return {};
    }

    std::string_view cppActionKind(ActionKind kind) {
      switch (kind) {
      case ActionKind::Button:
        return "Button";
      case ActionKind::Axis2d:
        return "Axis2d";
      case ActionKind::Pointer:
        return "Pointer";
      }

      return {};
    }

    std::string emitLayoutAsserts(const Type& type) {
      const std::string message = "\"schema: layout of " + type.name + " diverges from oly-schema\"";
      std::string asserts = "static_assert(sizeof(" + type.name + ") == " + std::to_string(type.layout.size) + " && alignof(" + type.name + ") == " + std::to_string(type.layout.alignment) + ", " + message + ");\n";
      for (const Field& field : type.fields) {
        asserts += "static_assert(offsetof(" + type.name + ", " + field.name + ") == " + std::to_string(field.offset) + ", " + message + ");\n";
      }

      return asserts;
    }

    std::string emitStruct(const Type& type) {
      if (type.kind == TypeKind::Tag) {
        return "struct " + type.name + " {};\n";
      }

      std::string fields;
      for (const Field& field : type.fields) {
        fields += "  " + std::string(cppType(field.type)) + " " + field.name + ";\n";
      }

      return "struct " + type.name + " {\n" + fields + "};\n" + emitLayoutAsserts(type);
    }

    std::string emitActions(const std::vector<Action>& actions) {
      if (actions.empty()) {
        return "";
      }

      std::string handles;
      for (std::size_t index = 0; index < actions.size(); ++index) {
        handles += "  inline constexpr oly::input::" + std::string(cppActionHandle(actions[index].kind)) + " " + actions[index].name + "{" + std::to_string(index) + "};\n";
      }

      return "\nnamespace actions {\n" + handles + "}\n";
    }

    std::string emitStates(const std::vector<State>& states) {
      if (states.empty()) {
        return "";
      }

      std::string handles;
      for (std::size_t index = 0; index < states.size(); ++index) {
        handles += "  inline constexpr oly::app::StateId " + states[index].name + "{" + std::to_string(index) + "};\n";
      }

      return "\nnamespace states {\n" + handles + "}\n";
    }

    std::string emptyRegistryFunction(std::string_view signature) {
      return "  " + std::string(signature) + " {\n    return {};\n  }\n";
    }

    template <typename Item, typename Entry>
    std::string joinEntries(const std::vector<Item>& items, Entry entry) {
      std::string joined;
      for (const Item& item : items) {
        if (!joined.empty()) {
          joined += ",\n";
        }

        joined += entry(item);
      }

      return joined;
    }

    std::string tableRegistryFunction(std::string_view signature, std::string_view elementType, std::string_view variable, const std::string& entries) {
      return "  " + std::string(signature) + " {\n    static const " + std::string(elementType) + " " + std::string(variable) + "[] = {\n" + entries + ",\n    };\n    return " + std::string(variable) + ";\n  }\n";
    }

    std::string emitComponentIdsRegistry(const Schema& schema) {
      constexpr std::string_view signature = "std::span<const oly::ecs::ComponentId> schemaComponentIds()";
      if (schema.types.empty()) {
        return emptyRegistryFunction(signature);
      }

      const std::string ids = joinEntries(schema.types, [](const Type& type) { return "      oly::ecs::componentId<" + type.name + ">()"; });

      return "  " + std::string(signature) + " {\n    static const oly::ecs::ComponentId ids[] = {\n" + ids + "};\n    return ids;\n  }\n";
    }

    std::string emitEventsRegistry(const Schema& schema) {
      constexpr std::string_view signature = "std::span<const SchemaEvent> schemaEvents()";
      if (schema.events.empty()) {
        return emptyRegistryFunction(signature);
      }

      const std::string entries = joinEntries(schema.events, [](const Type& event) {
        return "      {\"" + event.name + "\", sizeof(" + event.name + "), alignof(" + event.name + "), [](oly::app::Engine& engine) { engine.declareEvent<" + event.name + ">(\"" + event.name + "\"); }}";
      });

      return tableRegistryFunction(signature, "SchemaEvent", "events", entries);
    }

    std::string emitActionsRegistry(const Schema& schema) {
      constexpr std::string_view signature = "std::span<const SchemaAction> schemaActions()";
      if (schema.actions.empty()) {
        return emptyRegistryFunction(signature);
      }

      const std::string entries = joinEntries(schema.actions, [](const Action& action) {
        return "      {\"" + action.name + "\", oly::input::ActionKind::" + std::string(cppActionKind(action.kind)) + "}";
      });

      return tableRegistryFunction(signature, "SchemaAction", "actions", entries);
    }

    std::string emitStatesRegistry(const Schema& schema) {
      constexpr std::string_view signature = "std::span<const SchemaState> schemaStates()";
      if (schema.states.empty()) {
        return emptyRegistryFunction(signature);
      }

      const std::string entries = joinEntries(schema.states, [](const State& state) { return "      {\"" + state.name + "\"}"; });

      return tableRegistryFunction(signature, "SchemaState", "states", entries);
    }
  }

  std::string emitCppHeader(const Schema& schema) {
    std::string structs;
    for (const std::vector<Type>* group : {&schema.types, &schema.events}) {
      for (const Type& type : *group) {
        if (!structs.empty()) {
          structs += "\n";
        }

        structs += emitStruct(type);
      }
    }

    const std::string inputInclude = schema.actions.empty() ? "" : "#include \"oly/input/action_map.hpp\"\n";
    const std::string stateInclude = schema.states.empty() ? "" : "#include \"oly/app/state.hpp\"\n";

    return "// Generated by oly-schema: do not edit.\n#pragma once\n\n#include <cstddef>\n#include <cstdint>\n\n#include \"oly/ecs/entity.hpp\"\n" + inputInclude + stateInclude + "\n" + structs + emitActions(schema.actions) + emitStates(schema.states);
  }

  std::string emitCppRegistry(const Schema& schema) {
    std::ostringstream hash;
    hash << std::hex << schema.hash;

    return "// Generated by oly-schema: do not edit.\n"
           "#include \"oly/app/engine.hpp\"\n"
           "#include \"oly/capi/schema_registry.hpp\"\n"
           "#include \"schema.generated.hpp\"\n"
           "\n"
           "namespace oly::capi {\n"
           "  uint32_t schemaHash() noexcept {\n"
           "    return 0x"
      + hash.str() + "u;\n  }\n\n" + emitComponentIdsRegistry(schema) + "\n" + emitEventsRegistry(schema) + "\n" + emitActionsRegistry(schema) + "\n" + emitStatesRegistry(schema) + "}\n";
  }

  std::vector<EmittedFile> emitCpp(const Schema& schema, const std::filesystem::path& target) {
    return {{target / "schema.generated.hpp", emitCppHeader(schema)}, {target / "schema_registry.generated.cpp", emitCppRegistry(schema)}};
  }
}
