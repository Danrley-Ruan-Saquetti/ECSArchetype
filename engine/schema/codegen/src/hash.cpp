#include "oly/schema/hash.hpp"

namespace oly::schema {
  namespace {
    std::string fieldList(const std::vector<Field>& fields) {
      std::string list;
      for (const Field& field : fields) {
        if (!list.empty()) {
          list += ",";
        }

        list += field.name + ":" + std::string(scalarTypeName(field.type));
      }

      return list;
    }
  }

  uint32_t fnv1a32(std::string_view text) {
    uint32_t hash = 0x811c9dc5u;
    for (const char character : text) {
      hash = (hash ^ static_cast<uint8_t>(character)) * 0x01000193u;
    }

    return hash;
  }

  std::string canonicalForm(const Schema& schema) {
    std::string form;
    for (const Type& type : schema.types) {
      form += type.kind == TypeKind::Tag ? type.name + ";" : type.name + "{" + fieldList(type.fields) + "};";
    }

    for (const Type& event : schema.events) {
      form += "!" + event.name + "{" + fieldList(event.fields) + "};";
    }

    for (const Action& action : schema.actions) {
      form += "@" + action.name + ":" + std::string(actionKindName(action.kind)) + ";";
    }

    for (const State& state : schema.states) {
      form += "#" + state.name + ";";
    }

    return form;
  }
}
