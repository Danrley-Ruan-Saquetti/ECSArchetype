#include "oly/schema_typescript/typescript_emitter.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

#include "oly/schema/emitter.hpp"

namespace oly::schema_typescript {
  namespace {
    // Names that pass the .oly rules but cannot be a TypeScript `export const`: reserved words that C++ allows, and the
    // constructors the generated module imports.
    constexpr std::string_view unavailableNames[] = {
      "arguments", "await", "debugger", "eval", "extends", "finally", "function", "implements", "import", "in", "instanceof",
      "interface", "let", "null", "package", "super", "typeof", "var", "with", "yield",
      "actionType", "componentType", "eventType", "schemaOf", "stateType", "tagType"};

    struct Constants {
      std::string lines;
      std::vector<std::string> names;
      std::set<std::string_view> constructors{"schemaOf"};
    };

    template <typename Entry>
    void checkName(const oly::schema::Schema& schema, std::string_view what, const Entry& entry) {
      if (std::find(std::begin(unavailableNames), std::end(unavailableNames), entry.name) != std::end(unavailableNames)) {
        throw oly::schema::SchemaError(schema.fileName, entry.location, std::string(what) + " \"" + entry.name + "\" cannot be a TypeScript constant: it is a reserved word or a name the generated module imports");
      }
    }

    std::string fieldTypes(const std::vector<oly::schema::Field>& fields) {
      std::string text;
      for (const oly::schema::Field& field : fields) {
        text += (text.empty() ? "" : ", ") + field.name + ": \"" + std::string(oly::schema::scalarTypeName(field.type)) + "\"";
      }

      return "{ " + text + " }";
    }

    std::string fieldOffsets(const std::vector<oly::schema::Field>& fields) {
      std::string text;
      for (const oly::schema::Field& field : fields) {
        text += (text.empty() ? "" : ", ") + field.name + ": " + std::to_string(field.offset);
      }

      return "{ " + text + " }";
    }

    void addConstant(Constants& constants, std::string_view constructor, const std::string& name, const std::string& arguments) {
      constants.lines += "export const " + name + " = " + std::string(constructor) + "(" + arguments + ");\n";
      constants.names.push_back(name);
      constants.constructors.insert(constructor);
    }

    void addFieldedType(Constants& constants, std::string_view constructor, const oly::schema::Type& type, std::size_t index) {
      const std::string layout = "{ size: " + std::to_string(type.layout.size) + ", alignment: " + std::to_string(type.layout.alignment) + ", offsets: " + fieldOffsets(type.fields) + " }";
      addConstant(constants, constructor, type.name, "\"" + type.name + "\", " + fieldTypes(type.fields) + ", " + std::to_string(index) + ", " + layout);
    }

    std::string importLine(const std::set<std::string_view>& constructors) {
      std::string names;
      for (const std::string_view constructor : constructors) {
        names += (names.empty() ? "" : ", ") + std::string(constructor);
      }

      return "import { " + names + " } from \"@oly/core/generated\";\n";
    }

    std::vector<oly::schema::EmittedFile> emitFile(const oly::schema::Schema& schema, const std::filesystem::path& target) {
      return {{target, emitTypescript(schema)}};
    }

    [[maybe_unused]] const bool registered = oly::schema::registerEmitter({"ts", &emitFile});
  }

  std::string emitTypescript(const oly::schema::Schema& schema) {
    for (const oly::schema::Action& action : schema.actions) {
      checkName(schema, "action", action);
    }

    for (const oly::schema::State& state : schema.states) {
      checkName(schema, "state", state);
    }

    Constants constants;
    for (std::size_t index = 0; index < schema.types.size(); ++index) {
      const oly::schema::Type& type = schema.types[index];
      if (type.kind == oly::schema::TypeKind::Tag) {
        addConstant(constants, "tagType", type.name, "\"" + type.name + "\", " + std::to_string(index));
      } else {
        addFieldedType(constants, "componentType", type, index);
      }
    }

    for (std::size_t index = 0; index < schema.events.size(); ++index) {
      addFieldedType(constants, "eventType", schema.events[index], index);
    }

    for (std::size_t index = 0; index < schema.actions.size(); ++index) {
      const oly::schema::Action& action = schema.actions[index];
      addConstant(constants, "actionType", action.name, "\"" + action.name + "\", \"" + std::string(oly::schema::actionKindName(action.kind)) + "\", " + std::to_string(index));
    }

    for (std::size_t index = 0; index < schema.states.size(); ++index) {
      const oly::schema::State& state = schema.states[index];
      addConstant(constants, "stateType", state.name, "\"" + state.name + "\", " + std::to_string(index));
    }

    std::string entries;
    for (const std::string& name : constants.names) {
      entries += (entries.empty() ? "" : ", ") + name;
    }

    std::ostringstream hash;
    hash << std::hex << schema.hash;

    return "// Generated by oly-schema: do not edit.\n" + importLine(constants.constructors) + "\n" + constants.lines + (constants.names.empty() ? "" : "\n") + "export default schemaOf([" + entries + "], 0x" + hash.str() + ");\n";
  }
}
