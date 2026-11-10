#include <doctest/doctest.h>

#include <cstdint>

#include "oly/ecs/entity.hpp"

TEST_CASE("reciclagem incrementa geração e invalida handle antigo") {
  oly::ecs::EntityStore store;
  oly::ecs::Archetype* fake = reinterpret_cast<oly::ecs::Archetype*>(0x10);

  auto entity = store.allocate();
  CHECK(store.isReserved(entity));
  CHECK_FALSE(store.isAlive(entity));

  store.record(entity.index).archetype = fake;
  CHECK(store.isAlive(entity));

  store.release(entity.index);
  CHECK_FALSE(store.isAlive(entity));

  auto recycled = store.allocate();
  CHECK(recycled.index == entity.index);
  CHECK(recycled.generation == entity.generation + 1);

  store.record(recycled.index).archetype = fake;
  CHECK_FALSE(store.isAlive(entity));
  CHECK(store.isAlive(recycled));
}

TEST_CASE("slot com geração UINT32_MAX é aposentado") {
  oly::ecs::EntityStore store;
  auto entity = store.allocate();
  store.setGenerationForTest(entity.index, UINT32_MAX - 1);

  store.release(entity.index);

  CHECK(store.freeList().empty());
  CHECK(store.allocate().index != entity.index);
}

TEST_CASE("NullEntity nunca está vivo") {
  oly::ecs::EntityStore store;
  CHECK_FALSE(store.isAlive(oly::ecs::NullEntity));
}
