#include <doctest/doctest.h>

#include <cstdint>
#include <set>
#include <span>

#include "components.hpp"
#include "oly/ecs/world.hpp"

TEST_CASE("mesma instância para a mesma combinação; filtros mudam a chave") {
  ecs::World world;

  auto& firstQuery = world.query<A, const B>();
  auto& secondQuery = world.query<A, const B>();

  CHECK(&firstQuery == &secondQuery);
  CHECK(static_cast<void*>(&world.query<A, const B>(ecs::Without<Tag>{})) != static_cast<void*>(&firstQuery));
  CHECK(static_cast<void*>(&world.query<A, B>()) != static_cast<void*>(&firstQuery)); // constness entra na chave
}

TEST_CASE("arquétipos criados antes e depois da query; with/without de tag") {
  ecs::World world;
  world.create(A{1});
  world.create(A{2}, Tag{});
  auto& all = world.query<A>();
  auto& tagged = world.query<A>(ecs::With<Tag>{});
  auto& untagged = world.query<A>(ecs::Without<Tag>{});

  world.create(A{3}, B{0, 0});
  world.create(A{4}, B{0, 0}, Tag{}); // arquétipos novos depois

  CHECK(all.matchedCount() == 4);
  CHECK(tagged.matchedCount() == 2);
  CHECK(untagged.matchedCount() == 2);
  CHECK_INVARIANTS(world);
}

TEST_CASE("query criada antes de existir qualquer arquétipo com seus tipos") {
  ecs::World world;
  auto& query = world.query<A, B>();
  CHECK(query.matchedCount() == 0);

  world.create(A{1}, B{2, 3});
  CHECK(query.matchedCount() == 1);

  world.create(A{1});
  CHECK(query.matchedCount() == 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("Query guarda metadados de leitura/escrita por tipo") {
  using Q = ecs::Query<A, const B>;
  static_assert(Q::writes[0] == true);
  static_assert(Q::writes[1] == false);
  CHECK(Q::writes.size() == 2);
}

TEST_CASE("each visita exatamente as entidades do modelo e permite escrita") {
  ecs::World world;
  std::set<uint32_t> expected;
  std::vector<ecs::Entity> withB;
  for (uint64_t i = 0; i < 300; ++i) {
    auto entity = (i % 3) ? world.create(A{i}, B{i, 0}) : world.create(A{i});
    if (i % 3) {
      expected.insert(entity.index);
      withB.push_back(entity);
    }
  }

  world.create(B{0, 0}); // arquétipo sem A
  auto empty = world.create(A{0}, B{0, 0}, Tag{});
  world.destroy(empty); // arquétipo vazio é pulado

  std::set<uint32_t> seen;
  size_t visits = 0;
  world.query<A, const B>().each([&](ecs::Entity entity, A& componentA, const B&) {
    seen.insert(entity.index);
    ++visits;
    componentA.value += 1000;
  });

  CHECK(seen == expected);
  CHECK(visits == expected.size()); // cada entidade exatamente uma vez
  for (auto entity : withB) {
    CHECK(world.get<A>(entity).value >= 1000);
  }
}

TEST_CASE("each sem entidade no lambda") {
  ecs::World world;
  for (uint64_t i = 0; i < 10; ++i) {
    world.create(A{i});
  }

  uint64_t sum = 0;
  world.query<const A>().each([&](const A& componentA) { sum += componentA.value; });

  CHECK(sum == 45);
}

TEST_CASE("eachBlock entrega spans contíguos com count == tamanho da tabela") {
  ecs::World world;
  size_t expected = 0;
  for (uint64_t i = 0; i < 200; ++i) {
    if (i % 2) {
      world.create(A{i}, B{i, i});
      ++expected;
    } else {
      world.create(A{i});
    }
  }

  size_t total = 0, blocks = 0;
  world.query<A, const B>().eachBlock([&](std::span<A> componentsA, std::span<const B> componentsB) {
    CHECK(componentsA.size() == componentsB.size());
    total += componentsA.size();
    ++blocks;
    for (size_t i = 0; i < componentsA.size(); ++i) {
      CHECK(componentsA[i].value == componentsB[i].first);
    }
  });

  CHECK(total == expected);
  CHECK(blocks == 1);
}

TEST_CASE("iterating_ aninha e volta a zero, inclusive com exceção") {
  ecs::World world;
  world.create(A{1});

  world.query<A>().each([&](A&) {
    CHECK(world.isIterating());
    world.query<A>().each([&](A&) { CHECK(world.isIterating()); });
    CHECK(world.isIterating());
  });
  CHECK_FALSE(world.isIterating());

  CHECK_THROWS(world.query<A>().each([](A&) { throw 42; }));
  CHECK_FALSE(world.isIterating());

  world.create(A{2});
  CHECK_INVARIANTS(world);
}
