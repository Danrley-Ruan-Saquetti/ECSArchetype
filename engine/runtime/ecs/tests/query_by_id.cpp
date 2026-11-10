#include <doctest/doctest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include "alloc_counter.hpp"
#include "components.hpp"
#include "oly/ecs/query_by_id.hpp"
#include "oly/ecs/world.hpp"

TEST_CASE("eachBlock entrega count, entidades e colunas na ordem read, write") {
  oly::ecs::World world;
  std::vector<oly::ecs::Entity> entities;
  for (uint64_t i = 0; i < 10; ++i) {
    entities.push_back(world.spawn(A{i}, B{i, 2 * i}));
  }
  CHECK_INVARIANTS(world);

  auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}, .write = {oly::ecs::componentId<B>()}});
  CHECK(query.components().size() == 2);

  uint64_t sum = 0;
  query.eachBlock([&](const oly::ecs::BlockById& block) {
    const A* a = static_cast<const A*>(block.column(0));
    B* b = static_cast<B*>(block.column(1));
    CHECK(block.entities().size() == block.count());
    for (uint32_t row = 0; row < block.count(); ++row) {
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

TEST_CASE("queryById é cacheada pela spec") {
  oly::ecs::World world;
  const oly::ecs::QueryByIdSpec spec{.read = {oly::ecs::componentId<A>()}, .write = {oly::ecs::componentId<B>()}};

  CHECK(&world.queryById(spec) == &world.queryById(spec));

  auto& firstOrder = world.queryById({.read = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()}});
  auto& secondOrder = world.queryById({.read = {oly::ecs::componentId<B>(), oly::ecs::componentId<A>()}});
  CHECK(&firstOrder != &secondOrder);
}

TEST_CASE("QueryById e Query<Ts...> com a mesma assinatura são instâncias distintas") {
  oly::ecs::World world;
  world.spawn(A{1}, B{2, 3});

  auto& typed = world.query<const A, B>();
  auto& byId = world.queryById({.read = {oly::ecs::componentId<A>()}, .write = {oly::ecs::componentId<B>()}});
  CHECK(static_cast<void*>(&typed) != static_cast<void*>(&byId));

  int typedBlocks = 0, byIdBlocks = 0;
  typed.eachBlock([&](std::span<const A>, std::span<B>) { ++typedBlocks; });
  byId.eachBlock([&](const oly::ecs::BlockById&) { ++byIdBlocks; });
  CHECK(typedBlocks == 1);
  CHECK(byIdBlocks == 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("queryById acompanha arquétipos criados depois") {
  oly::ecs::World world;
  auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}});
  CHECK(query.matchedCount() == 0);

  world.spawn(A{1}, B{0, 0}, Tag{});
  CHECK(query.matchedCount() == 1);
  CHECK_INVARIANTS(world);

  uint32_t rows = 0;
  query.eachBlock([&](const oly::ecs::BlockById& block) { rows += block.count(); });
  CHECK(rows == 1);
}

TEST_CASE("queryById filtra por with e without") {
  oly::ecs::World world;
  for (int i = 0; i < 3; ++i) {
    world.spawn(A{1}, Tag{});
  }
  for (int i = 0; i < 2; ++i) {
    world.spawn(A{1});
  }

  auto countRows = [&](oly::ecs::QueryById& query) {
    uint32_t rows = 0;
    query.eachBlock([&](const oly::ecs::BlockById& block) { rows += block.count(); });
    return rows;
  };

  CHECK(countRows(world.queryById({.read = {oly::ecs::componentId<A>()}, .with = {oly::ecs::componentId<Tag>()}})) == 3);
  CHECK(countRows(world.queryById({.read = {oly::ecs::componentId<A>()}, .without = {oly::ecs::componentId<Tag>()}})) == 2);
  CHECK_INVARIANTS(world);
}

TEST_CASE("queryById pula tabelas vazias") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});
  auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}});
  world.destroy(entity);

  int calls = 0;
  query.eachBlock([&](const oly::ecs::BlockById&) { ++calls; });
  CHECK(calls == 0);
  CHECK_INVARIANTS(world);
}

TEST_CASE("iterate marca o World como iterando até o destrutor") {
  oly::ecs::World world;
  auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}});

  {
    oly::ecs::QueryIteration iteration = query.iterate();
    CHECK(world.isIterating());
  }

  CHECK_FALSE(world.isIterating());
  CHECK_INVARIANTS(world);
}

TEST_CASE("iterate aninhado mantém os blocos de cada nível") {
  oly::ecs::World world;
  for (uint64_t i = 0; i < 5; ++i) {
    world.spawn(A{i});
    world.spawn(A{i}, B{i, i});
  }
  auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}});

  oly::ecs::QueryIteration outer = query.iterate();
  const std::vector<uintptr_t> before(outer.descriptors().begin(), outer.descriptors().end());
  {
    oly::ecs::QueryIteration inner = query.iterate();
    CHECK(inner.blockCount() == outer.blockCount());
    CHECK(inner.descriptors().data() != outer.descriptors().data());
  }
  const std::vector<uintptr_t> after(outer.descriptors().begin(), outer.descriptors().end());

  CHECK(outer.blockCount() == 2);
  CHECK(outer.descriptors().size() == outer.blockCount() * (2 + query.components().size()));
  CHECK(before == after);
}

TEST_CASE("QueryIteration movida encerra a iteração uma vez só") {
  oly::ecs::World world;
  world.spawn(A{1});
  auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}});

  {
    oly::ecs::QueryIteration first = query.iterate();
    oly::ecs::QueryIteration second = std::move(first);
    CHECK(world.isIterating());
    CHECK(second.blockCount() == 1);
  }

  CHECK_FALSE(world.isIterating());
  {
    oly::ecs::QueryIteration again = query.iterate();
    CHECK(world.isIterating());
  }

  CHECK_FALSE(world.isIterating());
  CHECK_INVARIANTS(world);
}

TEST_CASE("exceção no callback de eachBlock encerra a iteração") {
  oly::ecs::World world;
  world.spawn(A{1});
  auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}});

  CHECK_THROWS_AS(query.eachBlock([](const oly::ecs::BlockById&) { throw std::runtime_error("falha"); }), std::runtime_error);

  CHECK_FALSE(world.isIterating());
  world.spawn(A{2});
  CHECK_INVARIANTS(world);
}

TEST_CASE("iterate em regime não aloca") {
  oly::ecs::World world;
  for (uint64_t i = 0; i < 100; ++i) {
    world.spawn(A{i});
    world.spawn(A{i}, B{i, i});
  }
  auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}});

  {
    oly::ecs::QueryIteration warmup = query.iterate();
  }

  const size_t allocationsBefore = allocationCount();
  {
    oly::ecs::QueryIteration iteration = query.iterate();
    CHECK(iteration.blockCount() == 2);
  }

  CHECK(allocationCount() == allocationsBefore);
}

TEST_CASE("getById e tryGetById") {
  oly::ecs::World world;
  auto entity = world.spawn(A{7}, Tag{});
  auto dead = world.spawn(A{1});
  world.destroy(dead);

  CHECK(world.tryGetById(entity, oly::ecs::componentId<B>()) == nullptr);
  CHECK(world.tryGetById(entity, oly::ecs::componentId<Tag>()) == nullptr);
  CHECK(world.tryGetById(dead, oly::ecs::componentId<A>()) == nullptr);
  CHECK(world.getById(entity, oly::ecs::componentId<A>()) == &world.get<A>(entity));
  CHECK(world.hasById(entity, oly::ecs::componentId<Tag>()));
}

TEST_CASE("QueryIteration não aceita atribuição por movimento") {
  static_assert(!std::is_move_assignable_v<oly::ecs::QueryIteration>);
  static_assert(!std::is_copy_assignable_v<oly::ecs::QueryIteration>);
}
