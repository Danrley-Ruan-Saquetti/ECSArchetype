#include <doctest/doctest.h>

#include <cstdint>
#include <ecs/world.hpp>

#include "components.hpp"

TEST_CASE("mesma instância para a mesma combinação; filtros mudam a chave") {
    ecs::World w;
    auto& q1 = w.query<A, const B>();
    auto& q2 = w.query<A, const B>();
    CHECK(&q1 == &q2);
    CHECK(static_cast<void*>(&w.query<A, const B>(ecs::Without<Tag>{})) != static_cast<void*>(&q1));
    CHECK(static_cast<void*>(&w.query<A, B>()) != static_cast<void*>(&q1));  // constness entra na chave
}

TEST_CASE("arquétipos criados antes e depois da query; with/without de tag") {
    ecs::World w;
    w.create(A{1});
    w.create(A{2}, Tag{});
    auto& all = w.query<A>();
    auto& tagged = w.query<A>(ecs::With<Tag>{});
    auto& untagged = w.query<A>(ecs::Without<Tag>{});
    w.create(A{3}, B{0, 0});
    w.create(A{4}, B{0, 0}, Tag{});  // arquétipos novos depois
    CHECK(all.matchedCount() == 4);
    CHECK(tagged.matchedCount() == 2);
    CHECK(untagged.matchedCount() == 2);
    CHECK_INV(w);  // I-8
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
