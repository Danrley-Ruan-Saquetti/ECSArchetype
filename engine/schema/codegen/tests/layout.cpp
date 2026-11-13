#include "oly/schema/layout.hpp"

#include <doctest/doctest.h>

#include <vector>

namespace {
  std::vector<oly::schema::Field> fieldsOf(std::vector<oly::schema::ScalarType> types) {
    std::vector<oly::schema::Field> fields;
    for (const oly::schema::ScalarType type : types) {
      fields.push_back({"f" + std::to_string(fields.size()), type});
    }

    return fields;
  }

  std::vector<uint32_t> offsetsOf(const std::vector<oly::schema::Field>& fields) {
    std::vector<uint32_t> offsets;
    for (const oly::schema::Field& field : fields) {
      offsets.push_back(field.offset);
    }

    return offsets;
  }
}

using oly::schema::ScalarType;

TEST_CASE("two f32 sit together, without padding") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::F32, ScalarType::F32});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 4});
  CHECK(layout.size == 8);
  CHECK(layout.alignment == 4);
}

TEST_CASE("padding before f32") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::U8, ScalarType::F32});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 4});
  CHECK(layout.size == 8);
}

TEST_CASE("f64 aligns the struct to 8") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::F32, ScalarType::F64});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 8});
  CHECK(layout.size == 16);
  CHECK(layout.alignment == 8);
}

TEST_CASE("u16 between u8 aligns the struct to 2") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::U8, ScalarType::U16, ScalarType::U8});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 2, 4});
  CHECK(layout.size == 6);
  CHECK(layout.alignment == 2);
}

TEST_CASE("entity has 8 bytes and alignment 4") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::Entity, ScalarType::U8});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 8});
  CHECK(layout.size == 12);
  CHECK(layout.alignment == 4);
}

TEST_CASE("u8, f64 and entity: 24 bytes, alignment 8") {
  std::vector<oly::schema::Field> fields = fieldsOf({ScalarType::U8, ScalarType::F64, ScalarType::Entity});

  const oly::schema::Layout layout = oly::schema::computeLayout(fields);

  CHECK(offsetsOf(fields) == std::vector<uint32_t>{0, 8, 16});
  CHECK(layout.size == 24);
  CHECK(layout.alignment == 8);
}
