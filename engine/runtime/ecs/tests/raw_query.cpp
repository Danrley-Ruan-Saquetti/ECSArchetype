#include <doctest/doctest.h>

#include <cstdint>

#include "components.hpp"
#include "oly/ecs/raw_query.hpp"
#include "oly/ecs/world.hpp"

TEST_CASE("rawQuery entrega colunas na ordem read seguida de write") {
  ecs::World world;
  std::vector<ecs::Entity> entities;
  for (uint64_t i = 0; i < 10; ++i) {
    entities.push_back(world.create(A{i}, B{i, 2 * i}));
  }
  CHECK_INVARIANTS(world);

  auto& query = world.rawQuery({.read = {ecs::componentId<A>()}, .write = {ecs::componentId<B>()}});
  CHECK(query.components().size() == 2);

  uint64_t sum = 0;
  query.eachBlock([&](ecs::Table& table, std::span<const uint16_t> columns) {
    const A* a = reinterpret_cast<const A*>(table.column(columns[0]).data);
    B* b = reinterpret_cast<B*>(table.column(columns[1]).data);
    for (uint32_t row = 0; row < table.count(); ++row) {
      sum += a[row].value;
      b[row].first += 100;
    }
  });

  CHECK(sum == 45);
  for (uint64_t i = 0; i < 10; ++i) {
    CHECK(world.get<B>(entities[i]).first == i + 100);
  }
  CHECK_INVARIANTS(world);
}

TEST_CASE("rawQuery é cacheada pela spec") {
  ecs::World world;
  const ecs::RawQuerySpec spec{.read = {ecs::componentId<A>()}, .write = {ecs::componentId<B>()}};

  CHECK(&world.rawQuery(spec) == &world.rawQuery(spec));

  auto& firstOrder = world.rawQuery({.read = {ecs::componentId<A>(), ecs::componentId<B>()}});
  auto& secondOrder = world.rawQuery({.read = {ecs::componentId<B>(), ecs::componentId<A>()}});
  CHECK(&firstOrder != &secondOrder);
}

TEST_CASE("rawQuery e Query<Ts...> com a mesma assinatura são instâncias distintas") {
  ecs::World world;
  world.create(A{1}, B{2, 3});

  auto& typed = world.query<const A, B>();
  auto& raw = world.rawQuery({.read = {ecs::componentId<A>()}, .write = {ecs::componentId<B>()}});
  CHECK(static_cast<void*>(&typed) != static_cast<void*>(&raw));

  int typedBlocks = 0, rawBlocks = 0;
  typed.eachBlock([&](std::span<const A>, std::span<B>) { ++typedBlocks; });
  raw.eachBlock([&](ecs::Table&, std::span<const uint16_t>) { ++rawBlocks; });
  CHECK(typedBlocks == 1);
  CHECK(rawBlocks == 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("rawQuery acompanha arquétipos criados depois") {
  ecs::World world;
  auto& query = world.rawQuery({.read = {ecs::componentId<A>()}});
  CHECK(query.matchedCount() == 0);

  world.create(A{1}, B{0, 0}, Tag{});
  CHECK(query.matchedCount() == 1);
  CHECK_INVARIANTS(world);

  uint32_t rows = 0;
  query.eachBlock([&](ecs::Table& table, std::span<const uint16_t>) { rows += table.count(); });
  CHECK(rows == 1);
}

TEST_CASE("rawQuery filtra por with e without") {
  ecs::World world;
  for (int i = 0; i < 3; ++i) {
    world.create(A{1}, Tag{});
  }
  for (int i = 0; i < 2; ++i) {
    world.create(A{1});
  }

  auto countRows = [&](ecs::RawQuery& query) {
    uint32_t rows = 0;
    query.eachBlock([&](ecs::Table& table, std::span<const uint16_t>) { rows += table.count(); });
    return rows;
  };

  CHECK(countRows(world.rawQuery({.read = {ecs::componentId<A>()}, .with = {ecs::componentId<Tag>()}})) == 3);
  CHECK(countRows(world.rawQuery({.read = {ecs::componentId<A>()}, .without = {ecs::componentId<Tag>()}})) == 2);
  CHECK_INVARIANTS(world);
}

TEST_CASE("rawQuery pula tabelas vazias") {
  ecs::World world;
  auto entity = world.create(A{1});
  auto& query = world.rawQuery({.read = {ecs::componentId<A>()}});
  world.destroy(entity);

  int calls = 0;
  query.eachBlock([&](ecs::Table&, std::span<const uint16_t>) { ++calls; });
  CHECK(calls == 0);
  CHECK_INVARIANTS(world);
}

TEST_CASE("beginIteration e endIteration marcam o World como iterando") {
  ecs::World world;
  auto& query = world.rawQuery({.read = {ecs::componentId<A>()}});

  query.beginIteration();
  CHECK(world.isIterating());
  CHECK(query.iterationDepth() == 1);

  query.beginIteration();
  CHECK(query.iterationDepth() == 2);

  query.endIteration();
  query.endIteration();
  CHECK_FALSE(world.isIterating());
  CHECK(query.iterationDepth() == 0);
  CHECK_INVARIANTS(world);
}
