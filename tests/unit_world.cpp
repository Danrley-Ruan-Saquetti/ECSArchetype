#include <doctest/doctest.h>

#include <cstdint>
#include <ecs/world.hpp>
#include <vector>

#include "components.hpp"

TEST_CASE("create() nasce vivo no arquétipo raiz") {
    ecs::World w;
    auto e = w.create();
    CHECK(w.isAlive(e));
    CHECK_FALSE(w.has<A>(e));
}
TEST_CASE("create<Ts...> nasce no arquétipo final com valores") {
    ecs::World w;
    auto e = w.create(A{1}, B{2, 3}, Tag{});
    CHECK(w.get<A>(e).v == 1);
    CHECK(w.get<B>(e).w == 3);
    CHECK(w.has<Tag>(e));
}
TEST_CASE("create<Ts...> copia lvalues") {
    ecs::World w;
    A a{4};
    auto e = w.create(a);
    a.v = 5;
    CHECK(w.get<A>(e).v == 4);
}
TEST_CASE("add sobre T existente atribui sem mudar arquétipo") {
    ecs::World w;
    auto e = w.create(A{1});
    w.add(e, A{7});
    CHECK(w.get<A>(e).v == 7);
}
TEST_CASE("add de T ausente move para o arquétipo com T") {
    ecs::World w;
    auto e = w.create(A{1});
    w.add(e, B{2, 3});
    CHECK(w.get<A>(e).v == 1);
    CHECK(w.get<B>(e).w == 3);
}
TEST_CASE("remove de T ausente é no-op") {
    ecs::World w;
    auto e = w.create(A{1});
    w.remove<B>(e);
    CHECK(w.get<A>(e).v == 1);
}
TEST_CASE("remove de T presente") {
    ecs::World w;
    auto e = w.create(A{1}, B{2, 3});
    w.remove<A>(e);
    CHECK_FALSE(w.has<A>(e));
    CHECK(w.get<B>(e).v == 2);
}
TEST_CASE("destroy atualiza o record da entidade deslocada") {
    ecs::World w;
    auto e0 = w.create(A{0});
    auto e1 = w.create(A{1});
    auto e2 = w.create(A{2});
    w.destroy(e0);
    CHECK_FALSE(w.isAlive(e0));
    CHECK(w.get<A>(e2).v == 2);
    CHECK(w.get<A>(e1).v == 1);
    CHECK(w.tryGet<A>(e0) == nullptr);
}
TEST_CASE("remove/destroy da última e da única linha") {  // Review Focus 3
    ecs::World w;
    auto a = w.create(A{1});
    auto b = w.create(A{2});
    w.remove<A>(b);
    CHECK(w.get<A>(a).v == 1);
    CHECK_FALSE(w.has<A>(b));
    w.destroy(a);
    CHECK(w.isAlive(b));
}
TEST_CASE("add com valor lido de outra entidade da tabela destino que realoca") {  // Review Focus 2
    ecs::World w;
    std::vector<ecs::Entity> full;
    for (uint64_t i = 0; i < 64; ++i) full.push_back(w.create(A{i}, B{i, i}));  // capacidade 64 cheia
    auto e = w.create(A{999});
    w.add(e, w.get<B>(full[5]));  // destino {A,B} realoca no pushUninit
    CHECK(w.get<B>(e).v == 5);
    CHECK(w.get<B>(full[5]).w == 5);
}
TEST_CASE("tryGet em entidade morta e sem T devolve nullptr") {
    ecs::World w;
    auto e = w.create(A{1});
    CHECK(w.tryGet<B>(e) == nullptr);
    REQUIRE(w.tryGet<A>(e) != nullptr);
    CHECK(w.tryGet<A>(e)->v == 1);
    w.destroy(e);
    CHECK(w.tryGet<A>(e) == nullptr);
    CHECK_FALSE(w.has<A>(e));
}
TEST_CASE("tags: add/has/remove sem coluna") {
    ecs::World w;
    auto e = w.create(A{1});
    w.add(e, Tag{});
    CHECK(w.has<Tag>(e));
    CHECK(w.get<A>(e).v == 1);
    w.remove<Tag>(e);
    CHECK_FALSE(w.has<Tag>(e));
    CHECK(w.get<A>(e).v == 1);
}
TEST_CASE("handle antigo após reciclagem do índice") {  // Review Focus 4
    ecs::World w;
    auto e = w.create(A{1});
    w.destroy(e);
    auto e2 = w.create(A{2});
    CHECK(e2.index == e.index);
    CHECK_FALSE(w.isAlive(e));
    CHECK(w.tryGet<A>(e) == nullptr);
    CHECK(w.get<A>(e2).v == 2);
}
