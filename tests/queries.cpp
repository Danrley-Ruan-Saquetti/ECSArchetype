#include <doctest/doctest.h>

#include <cstdint>
#include <set>
#include <span>

#include "components.hpp"
#include "ecs/world.hpp"

TEST_CASE("mesma instância para a mesma combinação; filtros mudam a chave") {
  ecs::World w;

  auto& q1 = w.query<A, const B>();
  auto& q2 = w.query<A, const B>();

  CHECK(&q1 == &q2);
  CHECK(static_cast<void*>(&w.query<A, const B>(ecs::Without<Tag>{})) != static_cast<void*>(&q1));
  CHECK(static_cast<void*>(&w.query<A, B>()) != static_cast<void*>(&q1)); // constness entra na chave
}

TEST_CASE("arquétipos criados antes e depois da query; with/without de tag") {
  ecs::World w;
  w.create(A{1});
  w.create(A{2}, Tag{});
  auto& all = w.query<A>();
  auto& tagged = w.query<A>(ecs::With<Tag>{});
  auto& untagged = w.query<A>(ecs::Without<Tag>{});

  w.create(A{3}, B{0, 0});
  w.create(A{4}, B{0, 0}, Tag{}); // arquétipos novos depois

  CHECK(all.matchedCount() == 4);
  CHECK(tagged.matchedCount() == 2);
  CHECK(untagged.matchedCount() == 2);
  CHECK_INV(w); // I-8
}

TEST_CASE("query criada antes de existir qualquer arquétipo com seus tipos") {
  ecs::World w;
  auto& q = w.query<A, B>();
  CHECK(q.matchedCount() == 0);

  w.create(A{1}, B{2, 3});
  CHECK(q.matchedCount() == 1);

  w.create(A{1});
  CHECK(q.matchedCount() == 1);
  CHECK_INV(w);
}

TEST_CASE("Query guarda metadados de leitura/escrita por tipo") {
  using Q = ecs::Query<A, const B>;
  static_assert(Q::writes[0] == true);
  static_assert(Q::writes[1] == false);
  CHECK(Q::writes.size() == 2);
}

TEST_CASE("each visita exatamente as entidades do modelo e permite escrita") {
  ecs::World w;
  std::set<uint32_t> expected;
  std::vector<ecs::Entity> withB;
  for (uint64_t i = 0; i < 300; ++i) {
    auto e = (i % 3) ? w.create(A{i}, B{i, 0}) : w.create(A{i});
    if (i % 3) {
      expected.insert(e.index);
      withB.push_back(e);
    }
  }
  w.create(B{0, 0}); // arquétipo sem A
  auto empty = w.create(A{0}, B{0, 0}, Tag{});
  w.destroy(empty); // arquétipo vazio é pulado

  std::set<uint32_t> seen;
  size_t visits = 0;
  w.query<A, const B>().each([&](ecs::Entity e, A& a, const B&) {
    seen.insert(e.index);
    ++visits;
    a.v += 1000;
  });

  CHECK(seen == expected);
  CHECK(visits == expected.size()); // cada entidade exatamente uma vez
  for (auto e : withB)
    CHECK(w.get<A>(e).v >= 1000);
}

TEST_CASE("each sem entidade no lambda") {
  ecs::World w;
  for (uint64_t i = 0; i < 10; ++i)
    w.create(A{i});

  uint64_t sum = 0;
  w.query<const A>().each([&](const A& a) { sum += a.v; });

  CHECK(sum == 45);
}

TEST_CASE("eachBlock entrega spans contíguos com count == tamanho da tabela") {
  ecs::World w;
  size_t expected = 0;
  for (uint64_t i = 0; i < 200; ++i) {
    if (i % 2) {
      w.create(A{i}, B{i, i});
      ++expected;
    } else {
      w.create(A{i});
    }
  }

  size_t total = 0, blocks = 0;
  w.query<A, const B>().eachBlock([&](std::span<A> as, std::span<const B> bs) {
    CHECK(as.size() == bs.size());
    total += as.size();
    ++blocks;
    for (size_t i = 0; i < as.size(); ++i)
      CHECK(as[i].v == bs[i].v);
  });

  CHECK(total == expected);
  CHECK(blocks == 1);
}

TEST_CASE("iterating_ aninha e volta a zero, inclusive com exceção") { // Review Focus 5
  ecs::World w;
  w.create(A{1});

  w.query<A>().each([&](A&) {
    CHECK(w.isIterating());
    w.query<A>().each([&](A&) { CHECK(w.isIterating()); });
    CHECK(w.isIterating());
  });
  CHECK_FALSE(w.isIterating());

  CHECK_THROWS(w.query<A>().each([](A&) { throw 42; }));
  CHECK_FALSE(w.isIterating());

  w.create(A{2});
  CHECK_INV(w);
}
