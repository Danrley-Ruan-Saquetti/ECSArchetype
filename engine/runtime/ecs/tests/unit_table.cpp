#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "components.hpp"
#include "oly/core/assert.hpp"
#include "oly/ecs/detail/table.hpp"
#include "oly/ecs/component.hpp"

namespace {
  uint16_t columnIndexOf(const oly::ecs::detail::Table& table, oly::ecs::ComponentId componentId) {
    auto columnTypes = table.columnTypes();
    return static_cast<uint16_t>(std::lower_bound(columnTypes.begin(), columnTypes.end(), componentId) - columnTypes.begin());
  }

  void construct(oly::ecs::detail::Table& table, uint32_t row, uint64_t value) {
    new (table.column(columnIndexOf(table, oly::ecs::componentId<A>())).at(row)) A{value};
    new (table.column(columnIndexOf(table, oly::ecs::componentId<LifeCounter>())).at(row)) LifeCounter{value};
  }

  uint64_t valueA(oly::ecs::detail::Table& table, uint32_t row) {
    return static_cast<A*>(table.column(columnIndexOf(table, oly::ecs::componentId<A>())).at(row))->value;
  }

  void checkI1I2(const oly::ecs::detail::Table& table) {
    CHECK(table.entities().size() == table.count());
    CHECK(table.capacity() >= table.count());
    CHECK(LifeCounter::alive == table.count());
  }
}

TEST_CASE("smoke: asserts enabled in debug") {
#ifdef NDEBUG
  CHECK(OLY_ENABLE_ASSERTS == 0);
#else
  CHECK(OLY_ENABLE_ASSERTS == 1);
#endif
  OLY_ASSERT(1 + 1 == 2, "op=%s", "smoke");
}

TEST_CASE("push, destroyRow, fillHole with swap and reallocation") {
  LifeCounter::alive = 0;
  {
    oly::ecs::ComponentId componentIds[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<LifeCounter>()};
    std::sort(std::begin(componentIds), std::end(componentIds));
    oly::ecs::detail::Table table(componentIds);

    for (uint32_t i = 0; i < 200; ++i) {
      auto row = table.pushUninitialized({i, 0});
      construct(table, row, i);
      checkI1I2(table);
    }
    CHECK(table.capacity() == 256); // 64 → 128 → 256

    table.destroyRow(10);
    auto moved = table.fillHole(10);
    REQUIRE(moved);
    CHECK(moved->index == 199);
    CHECK(table.entities()[10].index == 199);
    CHECK(valueA(table, 10) == 199); // the value followed the entity
    checkI1I2(table);

    table.destroyRow(table.count() - 1);
    CHECK_FALSE(table.fillHole(table.count() - 1)); // last row: nothing displaced
    checkI1I2(table);
  }
  CHECK(LifeCounter::alive == 0); // the destructor destroys live rows
}

TEST_CASE("columns aligned to 64, alignas(64) included") {
  oly::ecs::ComponentId componentIds[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<Wide>()};
  std::sort(std::begin(componentIds), std::end(componentIds));
  oly::ecs::detail::Table table(componentIds);

  table.pushUninitialized({0, 0});

  for (uint16_t column = 0; column < table.columnCount(); ++column) {
    CHECK(reinterpret_cast<uintptr_t>(table.column(column).data) % 64 == 0);
  }
}

TEST_CASE("std::string survives reallocation") {
  oly::ecs::ComponentId componentIds[] = {oly::ecs::componentId<std::string>()};
  oly::ecs::detail::Table table(componentIds);
  auto textFor = [](uint32_t i) { return std::string(40, 'a') + std::to_string(i); };

  for (uint32_t i = 0; i < 100; ++i) {
    auto row = table.pushUninitialized({i, 0});
    new (table.column(0).at(row)) std::string(textFor(i));
  }

  CHECK(table.capacity() == 128); // crossed 64
  for (uint32_t i = 0; i < 100; ++i) {
    CHECK(*static_cast<std::string*>(table.column(0).at(i)) == textFor(i));
  }
}
