#include <doctest/doctest.h>

#include <cstdint>
#include <ecs/entity.hpp>

TEST_CASE("reciclagem incrementa geração e invalida handle antigo") {
    ecs::EntityStore s;
    ecs::Archetype* fake = reinterpret_cast<ecs::Archetype*>(0x10);
    auto e = s.allocate();
    CHECK(s.isReserved(e));
    CHECK_FALSE(s.isAlive(e));
    s.record(e.index).arch = fake;
    CHECK(s.isAlive(e));
    s.release(e.index);
    CHECK_FALSE(s.isAlive(e));
    auto e2 = s.allocate();
    CHECK(e2.index == e.index);
    CHECK(e2.generation == e.generation + 1);
    s.record(e2.index).arch = fake;
    CHECK_FALSE(s.isAlive(e));
    CHECK(s.isAlive(e2));
}
TEST_CASE("slot com geração UINT32_MAX é aposentado") {
    ecs::EntityStore s;
    auto e = s.allocate();
    s.setGenerationForTest(e.index, UINT32_MAX - 1);
    s.release(e.index);
    CHECK(s.freeList().empty());
    CHECK(s.allocate().index != e.index);
}
TEST_CASE("NullEntity nunca está vivo") {
    ecs::EntityStore s;
    CHECK_FALSE(s.isAlive(ecs::NullEntity));
}
