#include <doctest/doctest.h>

#include <algorithm>
#include <ecs/archetype.hpp>
#include <ecs/detail/small_map.hpp>

#include "components.hpp"

namespace {
struct C {
    uint32_t v;
};
ecs::Signature sorted(ecs::Signature s) {
    std::sort(s.begin(), s.end());
    return s;
}
}  // namespace

TEST_CASE("columnTypes exclui tags; signature ordenada inclui") {
    ecs::Signature sig = sorted({ecs::componentId<A>(), ecs::componentId<Tag>(), ecs::componentId<B>()});
    ecs::Archetype a(1, sig);
    CHECK(a.signature == sig);
    CHECK(a.columnTypes.size() == 2);
    CHECK(a.table.columnCount() == 2);
    CHECK(ecs::columnOf(a, ecs::componentId<Tag>()) == ecs::NoColumn);
    CHECK(ecs::columnOf(a, ecs::componentId<A>()) != ecs::NoColumn);
    CHECK(ecs::columnOf(a, ecs::componentId<C>()) == ecs::NoColumn);
}

TEST_CASE("buildEdge mapeia colunas por merge") {
    const auto ia = ecs::componentId<A>(), ib = ecs::componentId<B>(), ic = ecs::componentId<C>();
    ecs::Archetype src(1, sorted({ia, ib}));
    {
        ecs::Archetype dst(2, sorted({ia, ib, ic}));
        auto e = ecs::buildEdge(src, dst, ic);
        CHECK(e.target == &dst);
        REQUIRE(e.srcToDst.size() == 2);
        CHECK(e.srcToDst[ecs::columnOf(src, ia)] == ecs::columnOf(dst, ia));
        CHECK(e.srcToDst[ecs::columnOf(src, ib)] == ecs::columnOf(dst, ib));
        CHECK(e.addedColumn == ecs::columnOf(dst, ic));
    }
    {
        ecs::Archetype dst(3, sorted({ib}));
        auto e = ecs::buildEdge(src, dst, ecs::NoComponent);
        CHECK(e.srcToDst[ecs::columnOf(src, ia)] == -1);
        CHECK(e.srcToDst[ecs::columnOf(src, ib)] == 0);
        CHECK(e.addedColumn == -1);
    }
    {
        ecs::Archetype dst(4, sorted({ia, ib, ecs::componentId<Tag>()}));
        auto e = ecs::buildEdge(src, dst, ecs::componentId<Tag>());
        CHECK(e.addedColumn == -1);  // tag não tem coluna
        CHECK(e.srcToDst[0] == 0);
        CHECK(e.srcToDst[1] == 1);
    }
}

TEST_CASE("SmallMap mantém ordem e encontra") {
    ecs::SmallMap<uint32_t, int> m;
    m.insert(5, 50);
    m.insert(1, 10);
    m.insert(3, 30);
    CHECK(m.size() == 3);
    CHECK(m.begin()->first == 1);
    REQUIRE(m.find(3) != nullptr);
    CHECK(*m.find(3) == 30);
    CHECK(m.find(2) == nullptr);
    CHECK(m.capacityBytes() >= 3 * sizeof(std::pair<uint32_t, int>));
}

TEST_CASE("SigHash difere para assinaturas diferentes") {
    CHECK(ecs::SigHash{}({1, 2}) != ecs::SigHash{}({2, 1, 0}));
}
