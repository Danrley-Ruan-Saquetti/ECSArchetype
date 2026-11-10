#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "components.hpp"
#include "oly/core/assert.hpp"
#include "oly/ecs/table.hpp"
#include "oly/ecs/type_info.hpp"

namespace {
  uint16_t columnIndexOf(const ecs::Table& table, ecs::ComponentId id) {
    auto columnTypes = table.columnTypes();
    return static_cast<uint16_t>(std::lower_bound(columnTypes.begin(), columnTypes.end(), id) - columnTypes.begin());
  }

  void construct(ecs::Table& table, uint32_t row, uint64_t value) {
    new (table.column(columnIndexOf(table, ecs::componentId<A>())).at(row)) A{value};
    new (table.column(columnIndexOf(table, ecs::componentId<LifeCounter>())).at(row)) LifeCounter{value};
  }

  uint64_t valueA(ecs::Table& table, uint32_t row) {
    return static_cast<A*>(table.column(columnIndexOf(table, ecs::componentId<A>())).at(row))->value;
  }

  void checkI1I2(const ecs::Table& table) {
    CHECK(table.entities().size() == table.count());
    CHECK(table.capacity() >= table.count());
    CHECK(LifeCounter::alive == table.count());
  }
}

TEST_CASE("smoke: asserts ligados em debug") {
#ifdef NDEBUG
  CHECK(ECS_ENABLE_ASSERTS == 0);
#else
  CHECK(ECS_ENABLE_ASSERTS == 1);
#endif
  ECS_ASSERT(1 + 1 == 2, "op=%s", "smoke");
}

TEST_CASE("push, destroyRow, fillHole com swap e realocação") {
  LifeCounter::alive = 0;
  {
    ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<LifeCounter>()};
    std::sort(std::begin(ids), std::end(ids));
    ecs::Table table(ids);

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
    CHECK(valueA(table, 10) == 199); // valor acompanhou a entidade
    checkI1I2(table);

    table.destroyRow(table.count() - 1);
    CHECK_FALSE(table.fillHole(table.count() - 1)); // última linha: nada deslocado
    checkI1I2(table);
  }
  CHECK(LifeCounter::alive == 0); // destrutor destrói linhas vivas
}

TEST_CASE("colunas alinhadas a 64, inclusive alignas(64)") {
  ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<Wide>()};
  std::sort(std::begin(ids), std::end(ids));
  ecs::Table table(ids);

  table.pushUninitialized({0, 0});

  for (uint16_t column = 0; column < table.columnCount(); ++column) {
    CHECK(reinterpret_cast<uintptr_t>(table.column(column).data) % 64 == 0);
  }
}

TEST_CASE("std::string sobrevive à realocação") {
  ecs::ComponentId ids[] = {ecs::componentId<std::string>()};
  ecs::Table table(ids);
  auto make = [](uint32_t i) { return std::string(40, 'a') + std::to_string(i); };

  for (uint32_t i = 0; i < 100; ++i) {
    auto row = table.pushUninitialized({i, 0});
    new (table.column(0).at(row)) std::string(make(i));
  }

  CHECK(table.capacity() == 128); // cruzou 64
  for (uint32_t i = 0; i < 100; ++i) {
    CHECK(*static_cast<std::string*>(table.column(0).at(i)) == make(i));
  }
}
