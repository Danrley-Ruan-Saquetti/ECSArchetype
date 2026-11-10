#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "components.hpp"
#include "oly/ecs/world.hpp"

namespace {
  std::string longString(uint64_t i) {
    return std::string(48, 'z') + std::to_string(i);
  }
}

TEST_CASE("string, unique_ptr, vector e LifeCounter sobrevivem a realocação e transições") {
  LifeCounter::alive = 0;
  {
    oly::ecs::World world;
    std::vector<oly::ecs::Entity> entities;
    for (uint64_t i = 0; i < 300; ++i) {
      entities.push_back(world.spawn(longString(i), std::make_unique<int>(int(i)), std::vector<int>(i % 7 + 1, int(i)),
                                      LifeCounter{i}));
    }
    CHECK(LifeCounter::alive == 300);

    for (uint64_t i = 0; i < 300; i += 2) {
      world.add(entities[i], A{i});
    }
    for (uint64_t i = 0; i < 300; i += 4) {
      world.remove<A>(entities[i]);
    }
    CHECK_INVARIANTS(world);

    for (uint64_t i = 1; i < 300; i += 2) {
      world.destroy(entities[i]);
    }
    CHECK(LifeCounter::alive == 150);

    for (uint64_t i = 0; i < 300; i += 2) {
      CHECK(world.get<std::string>(entities[i]) == longString(i));
      CHECK(*world.get<std::unique_ptr<int>>(entities[i]) == int(i));
      CHECK(world.get<std::vector<int>>(entities[i]).size() == i % 7 + 1);
      CHECK(world.get<LifeCounter>(entities[i]).value == i);
      CHECK(world.has<A>(entities[i]) == (i % 4 != 0));
      if (i % 4 != 0) {
        CHECK(world.get<A>(entities[i]).value == i);
      }
    }
    for (uint64_t i = 1; i < 300; i += 2) {
      CHECK_FALSE(world.isAlive(entities[i]));
    }
    CHECK_INVARIANTS(world);
  }
  CHECK(LifeCounter::alive == 0);
}

TEST_CASE("remover componente não trivial destrói o valor") {
  LifeCounter::alive = 0;
  {
    oly::ecs::World world;
    auto entity = world.spawn(LifeCounter{1}, A{2});
    CHECK(LifeCounter::alive == 1);

    world.remove<LifeCounter>(entity);
    CHECK(LifeCounter::alive == 0);
    CHECK(world.get<A>(entity).value == 2);

    world.add(entity, LifeCounter{3});
    world.add(entity, LifeCounter{4}); // atribui sobre o existente
    CHECK(LifeCounter::alive == 1);
    CHECK(world.get<LifeCounter>(entity).value == 4);
  }
  CHECK(LifeCounter::alive == 0);
}
