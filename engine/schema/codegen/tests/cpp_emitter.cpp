#include "oly/schema/cpp_emitter.hpp"

#include <doctest/doctest.h>

#include <sstream>
#include <string>

#include "oly/schema/schema.hpp"

namespace {
  oly::schema::Schema schemaOf(const std::string& source) {
    return oly::schema::parseSchema(source, "test.oly");
  }

  bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
  }
}

TEST_CASE("struct in declaration order") {
  const std::string header = oly::schema::emitCppHeader(schemaOf("component P { a: u8, b: f64, owner: entity }\ntag T\n"));

  CHECK(contains(header, "struct P {\n  uint8_t a;\n  double b;\n  oly::ecs::Entity owner;\n};"));
}

TEST_CASE("size and offset static_asserts") {
  const std::string header = oly::schema::emitCppHeader(schemaOf("component P { a: u8, b: f64, owner: entity }\ntag T\n"));

  CHECK(contains(header, "sizeof(P) == 24 && alignof(P) == 8"));
  CHECK(contains(header, "offsetof(P, b) == 8"));
  CHECK(contains(header, "offsetof(P, owner) == 16"));
  CHECK(contains(header, "schema: layout of P diverges from oly-schema"));
}

TEST_CASE("tag becomes an empty struct") {
  CHECK(contains(oly::schema::emitCppHeader(schemaOf("component P { a: u8 }\ntag T\n")), "struct T {};"));
}

TEST_CASE("registry with hash and ids in order") {
  const oly::schema::Schema schema = schemaOf("component P { a: u8, b: f64, owner: entity }\ntag T\n");
  std::ostringstream hash;
  hash << std::hex << schema.hash;

  const std::string registry = oly::schema::emitCppRegistry(schema);

  CHECK(contains(registry, "0x" + hash.str() + "u"));
  CHECK(contains(registry, "oly::ecs::componentId<P>(),\n      oly::ecs::componentId<T>()"));
  CHECK(contains(registry, "namespace oly::capi"));
  CHECK(contains(registry, "#include \"oly/capi/schema_registry.hpp\""));
}

TEST_CASE("event becomes a struct with layout static_assert") {
  const std::string header = oly::schema::emitCppHeader(schemaOf("component Q { a: u8 }\nevent E { target: entity, amount: f32 }\ntag U\nevent F { power: u32 }\n"));

  CHECK(contains(header, "struct E {\n  oly::ecs::Entity target;\n  float amount;\n};"));
  CHECK(contains(header, "sizeof(E) == 12 && alignof(E) == 4"));
  CHECK(contains(header, "offsetof(E, amount) == 8"));
  CHECK(contains(header, "schema: layout of E diverges from oly-schema"));
}

TEST_CASE("registry declares the events in schema order") {
  const std::string registry = oly::schema::emitCppRegistry(schemaOf("component Q { a: u8 }\nevent E { target: entity, amount: f32 }\ntag U\nevent F { power: u32 }\n"));

  CHECK(contains(registry, "#include \"oly/app/engine.hpp\""));
  CHECK(registry.find("engine.declareEvent<E>(\"E\");") < registry.find("engine.declareEvent<F>(\"F\");"));
  CHECK(contains(registry, "engine.declareEvent<F>(\"F\");"));
  CHECK(contains(registry, "schemaEvents()"));
}

TEST_CASE("schema without events generates an empty schemaEvents") {
  CHECK(contains(oly::schema::emitCppRegistry(schemaOf("component P { a: u8 }\n")), "schemaEvents() {\n    return {};"));
}

TEST_CASE("header with actions declares actions::<name> with the handle of the kind") {
  const std::string header = oly::schema::emitCppHeader(schemaOf("component R { a: u8 }\naction go: button\naction aim: axis2d\n"));

  CHECK(contains(header, "#include \"oly/input/action_map.hpp\""));
  CHECK(contains(header, "inline constexpr oly::input::ButtonAction go{0};"));
  CHECK(contains(header, "inline constexpr oly::input::Axis2dAction aim{1};"));
}

TEST_CASE("header without actions does not include the input module") {
  CHECK(!contains(oly::schema::emitCppHeader(schemaOf("component P { a: u8 }\n")), "oly/input"));
}

TEST_CASE("registry lists the actions in schema order") {
  const std::string registry = oly::schema::emitCppRegistry(schemaOf("component R { a: u8 }\naction go: button\naction aim: axis2d\n"));

  CHECK(contains(registry, "{\"go\", oly::input::ActionKind::Button}"));
  CHECK(registry.find("{\"go\", oly::input::ActionKind::Button}") < registry.find("{\"aim\", oly::input::ActionKind::Axis2d}"));
}

TEST_CASE("schema without actions generates an empty schemaActions") {
  CHECK(contains(oly::schema::emitCppRegistry(schemaOf("component P { a: u8 }\n")), "schemaActions() {\n    return {};"));
}

TEST_CASE("header with states declares states::<name> with the StateId") {
  const std::string header = oly::schema::emitCppHeader(schemaOf("component S { a: u8 }\nstate menu\nstate playing\n"));

  CHECK(contains(header, "#include \"oly/app/state.hpp\""));
  CHECK(contains(header, "namespace states {"));
  CHECK(contains(header, "inline constexpr oly::app::StateId menu{0};"));
  CHECK(contains(header, "inline constexpr oly::app::StateId playing{1};"));
}

TEST_CASE("header without states does not include oly/app/state") {
  CHECK(!contains(oly::schema::emitCppHeader(schemaOf("component P { a: u8 }\n")), "oly/app/state"));
}

TEST_CASE("registry lists the states in schema order") {
  const std::string registry = oly::schema::emitCppRegistry(schemaOf("component S { a: u8 }\nstate menu\nstate playing\n"));

  CHECK(contains(registry, "{\"menu\"}"));
  CHECK(registry.find("{\"menu\"}") < registry.find("{\"playing\"}"));
  CHECK(contains(registry, "schemaStates()"));
}

TEST_CASE("schema without states generates an empty schemaStates") {
  CHECK(contains(oly::schema::emitCppRegistry(schemaOf("component P { a: u8 }\n")), "schemaStates() {\n    return {};"));
}

TEST_CASE("header: exact text for a component and a tag") {
  const oly::schema::Schema schema = schemaOf("component P { a: u8, b: f64, owner: entity }\ntag T\n");

  CHECK(oly::schema::emitCppHeader(schema) == "// Generated by oly-schema: do not edit.\n"
                                              "#pragma once\n"
                                              "\n"
                                              "#include <cstddef>\n"
                                              "#include <cstdint>\n"
                                              "\n"
                                              "#include \"oly/ecs/entity.hpp\"\n"
                                              "\n"
                                              "struct P {\n"
                                              "  uint8_t a;\n"
                                              "  double b;\n"
                                              "  oly::ecs::Entity owner;\n"
                                              "};\n"
                                              "static_assert(sizeof(P) == 24 && alignof(P) == 8, \"schema: layout of P diverges from oly-schema\");\n"
                                              "static_assert(offsetof(P, a) == 0, \"schema: layout of P diverges from oly-schema\");\n"
                                              "static_assert(offsetof(P, b) == 8, \"schema: layout of P diverges from oly-schema\");\n"
                                              "static_assert(offsetof(P, owner) == 16, \"schema: layout of P diverges from oly-schema\");\n"
                                              "\n"
                                              "struct T {};\n");
}

TEST_CASE("registry: exact text for a schema without events, actions or states") {
  const oly::schema::Schema schema = schemaOf("component P { a: u8 }\ntag T\n");
  std::ostringstream hash;
  hash << std::hex << schema.hash;

  CHECK(oly::schema::emitCppRegistry(schema) == "// Generated by oly-schema: do not edit.\n"
                                                "#include \"oly/app/engine.hpp\"\n"
                                                "#include \"oly/capi/schema_registry.hpp\"\n"
                                                "#include \"schema.generated.hpp\"\n"
                                                "\n"
                                                "namespace oly::capi {\n"
                                                "  uint32_t schemaHash() noexcept {\n"
                                                "    return 0x"
          + hash.str() + "u;\n"
                         "  }\n"
                         "\n"
                         "  std::span<const oly::ecs::ComponentId> schemaComponentIds() {\n"
                         "    static const oly::ecs::ComponentId ids[] = {\n"
                         "      oly::ecs::componentId<P>(),\n"
                         "      oly::ecs::componentId<T>()};\n"
                         "    return ids;\n"
                         "  }\n"
                         "\n"
                         "  std::span<const SchemaEvent> schemaEvents() {\n"
                         "    return {};\n"
                         "  }\n"
                         "\n"
                         "  std::span<const SchemaAction> schemaActions() {\n"
                         "    return {};\n"
                         "  }\n"
                         "\n"
                         "  std::span<const SchemaState> schemaStates() {\n"
                         "    return {};\n"
                         "  }\n"
                         "}\n");
}

TEST_CASE("schema without components or tags generates an empty schemaComponentIds") {
  CHECK(contains(oly::schema::emitCppRegistry(schemaOf("action go: button\n")), "schemaComponentIds() {\n    return {};\n  }"));
}
