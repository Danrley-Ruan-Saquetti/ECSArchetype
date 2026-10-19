#include <doctest/doctest.h>

#include "ecs/detail/assert.hpp"

TEST_CASE("smoke: asserts ligados em debug") {
#ifdef NDEBUG
  CHECK(ECS_ENABLE_ASSERTS == 0);
#else
  CHECK(ECS_ENABLE_ASSERTS == 1);
#endif
  ECS_ASSERT(1 + 1 == 2, "op=%s", "smoke");
}

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "components.hpp"
#include "ecs/table.hpp"
#include "ecs/type_info.hpp"

namespace {
  uint16_t colOf(const ecs::Table& t, ecs::ComponentId id) {
    auto ct = t.columnTypes();
    return static_cast<uint16_t>(std::lower_bound(ct.begin(), ct.end(), id) - ct.begin());
  }
  // placement-new de A{v} e LifeCounter{v} nas respectivas colunas
  void construct(ecs::Table& t, uint32_t row, uint64_t v) {
    new (t.column(colOf(t, ecs::componentId<A>())).at(row)) A{v};
    new (t.column(colOf(t, ecs::componentId<LifeCounter>())).at(row)) LifeCounter{v};
  }
  uint64_t valueA(ecs::Table& t, uint32_t row) {
    return static_cast<A*>(t.column(colOf(t, ecs::componentId<A>())).at(row))->v;
  }
  void checkI1I2(const ecs::Table& t) {
    CHECK(t.entities().size() == t.count());
    CHECK(t.capacity() >= t.count());
    CHECK(LifeCounter::alive == t.count());
  }
} // namespace

TEST_CASE("push, destroyRow, fillHole com swap e realocação") {
  LifeCounter::alive = 0;
  {
    ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<LifeCounter>()};
    std::sort(std::begin(ids), std::end(ids));
    ecs::Table t(ids);
    for (uint32_t i = 0; i < 200; ++i) {
      auto r = t.pushUninit({i, 0});
      construct(t, r, i);
      checkI1I2(t);
    }
    CHECK(t.capacity() == 256); // 64 → 128 → 256
    t.destroyRow(10);
    auto moved = t.fillHole(10);
    REQUIRE(moved);
    CHECK(moved->index == 199);
    CHECK(t.entities()[10].index == 199);
    CHECK(valueA(t, 10) == 199); // valor acompanhou a entidade
    checkI1I2(t);
    t.destroyRow(t.count() - 1);
    CHECK_FALSE(t.fillHole(t.count() - 1)); // última linha: nada deslocado
    checkI1I2(t);
  }
  CHECK(LifeCounter::alive == 0); // destrutor destrói linhas vivas
}

TEST_CASE("colunas alinhadas a 64, inclusive alignas(64)") {
  ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<Wide>()};
  std::sort(std::begin(ids), std::end(ids));
  ecs::Table t(ids);
  t.pushUninit({0, 0});
  for (uint16_t c = 0; c < t.columnCount(); ++c)
    CHECK(reinterpret_cast<uintptr_t>(t.column(c).data) % 64 == 0);
}

TEST_CASE("std::string sobrevive à realocação") {
  ecs::ComponentId ids[] = {ecs::componentId<std::string>()};
  ecs::Table t(ids);
  auto make = [](uint32_t i) { return std::string(40, 'a') + std::to_string(i); };
  for (uint32_t i = 0; i < 100; ++i) {
    auto r = t.pushUninit({i, 0});
    new (t.column(0).at(r)) std::string(make(i));
  }
  CHECK(t.capacity() == 128); // cruzou 64
  for (uint32_t i = 0; i < 100; ++i)
    CHECK(*static_cast<std::string*>(t.column(0).at(i)) == make(i));
}
