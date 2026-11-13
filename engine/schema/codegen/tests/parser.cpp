#include <doctest/doctest.h>

#include "oly/schema/hash.hpp"
#include "oly/schema/schema.hpp"

TEST_CASE("each declaration goes to its group, in source order") {
  const oly::schema::Schema schema = oly::schema::parseSchema(
    "state menu\n"
    "action go: button\n"
    "component P { a: u8, b: f64 }\n"
    "event E { target: entity }\n"
    "tag T\n"
    "action aim: pointer\n"
    "state playing\n",
    "test.oly");

  REQUIRE(schema.types.size() == 2);
  CHECK(schema.types[0].name == "P");
  CHECK(schema.types[1].kind == oly::schema::TypeKind::Tag);
  REQUIRE(schema.events.size() == 1);
  CHECK(schema.events[0].kind == oly::schema::TypeKind::Event);
  REQUIRE(schema.actions.size() == 2);
  CHECK(schema.actions[1].kind == oly::schema::ActionKind::Pointer);
  REQUIRE(schema.states.size() == 2);
  CHECK(schema.states[1].name == "playing");
  CHECK(schema.fileName == "test.oly");
}

TEST_CASE("fields keep their order and get the layout") {
  const oly::schema::Schema schema = oly::schema::parseSchema("component P {\n  a: u8,\n  b: f64,\n  owner: entity,\n}\n", "test.oly");

  const oly::schema::Type& type = schema.types.at(0);

  REQUIRE(type.fields.size() == 3);
  CHECK(type.fields[2].name == "owner");
  CHECK(type.fields[2].type == oly::schema::ScalarType::Entity);
  CHECK(type.fields[2].offset == 16);
  CHECK(type.layout.size == 24);
  CHECK(type.layout.alignment == 8);
}

TEST_CASE("names and fields carry their source location") {
  const oly::schema::Schema schema = oly::schema::parseSchema("# comment\n  component P { a: u8 }\n", "test.oly");

  CHECK(schema.types.at(0).location.line == 2);
  CHECK(schema.types.at(0).location.column == 13);
  CHECK(schema.types.at(0).fields.at(0).location.column == 17);
}

TEST_CASE("the hash is FNV-1a of the canonical form") {
  const oly::schema::Schema schema = oly::schema::parseSchema("component P { a: u8 }\ntag T\naction go: button\n", "test.oly");

  CHECK(schema.hash == oly::schema::fnv1a32("P{a:u8};T;@go:button;"));
}

TEST_CASE("an empty source is an empty schema") {
  const oly::schema::Schema schema = oly::schema::parseSchema("", "test.oly");

  CHECK(schema.types.empty());
  CHECK(schema.hash == 0x811c9dc5u);
}

TEST_CASE("carriage returns are whitespace") {
  CHECK(oly::schema::parseSchema("tag T\r\ntag U\r\n", "test.oly").types.size() == 2);
}
