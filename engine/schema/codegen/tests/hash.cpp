#include "oly/schema/hash.hpp"

#include <doctest/doctest.h>

namespace {
  oly::schema::Type component(std::string name, std::vector<oly::schema::Field> fields) {
    return {oly::schema::TypeKind::Component, std::move(name), std::move(fields)};
  }

  oly::schema::Schema everyDeclaration() {
    using oly::schema::ScalarType;

    oly::schema::Schema schema;
    schema.types.push_back(component("A", {{"x", ScalarType::F32}, {"y", ScalarType::F64}}));
    schema.types.push_back(component("C", {{"a", ScalarType::I8}, {"b", ScalarType::U8}, {"c", ScalarType::I16}, {"d", ScalarType::U16}, {"e", ScalarType::I32}, {"f", ScalarType::U32}}));
    schema.types.push_back({oly::schema::TypeKind::Tag, "B"});
    schema.events.push_back({oly::schema::TypeKind::Event, "E", {{"target", ScalarType::Entity}, {"value", ScalarType::U8}}});
    schema.actions = {{"go", oly::schema::ActionKind::Button}, {"aim", oly::schema::ActionKind::Pointer}, {"move", oly::schema::ActionKind::Axis2d}};
    schema.states = {{"menu"}, {"playing"}};

    return schema;
  }
}

TEST_CASE("fnv1a32 matches the FNV-1a reference values") {
  CHECK(oly::schema::fnv1a32("") == 0x811c9dc5u);
  CHECK(oly::schema::fnv1a32("a") == 0xe40c292cu);
}

TEST_CASE("canonical form: types, then events, actions and states, each in order") {
  CHECK(oly::schema::canonicalForm(everyDeclaration()) == "A{x:f32,y:f64};C{a:i8,b:u8,c:i16,d:u16,e:i32,f:u32};B;!E{target:entity,value:u8};@go:button;@aim:pointer;@move:axis2d;#menu;#playing;");
}

TEST_CASE("the hash of the canonical form is the one the TS client computed") {
  CHECK(oly::schema::fnv1a32(oly::schema::canonicalForm(everyDeclaration())) == 0x205a6f72u);
}

TEST_CASE("an empty schema has an empty canonical form") {
  CHECK(oly::schema::canonicalForm(oly::schema::Schema{}).empty());
}
