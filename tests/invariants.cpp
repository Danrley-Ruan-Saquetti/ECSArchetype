#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "components.hpp"
#include "ecs/world.hpp"

TEST_CASE("sequência create/add/remove/destroy mantém invariantes") {
  ecs::World world;
  CHECK_INVARIANTS(world);

  std::vector<ecs::Entity> entities;
  for (uint64_t i = 0; i < 100; ++i) {
    entities.push_back(world.create());
    CHECK_INVARIANTS(world);
  }
  for (uint64_t i = 0; i < 100; i += 2) {
    world.add(entities[i], A{i});
    CHECK_INVARIANTS(world);
  }
  for (uint64_t i = 0; i < 100; i += 3) {
    world.add(entities[i], Tag{});
    CHECK_INVARIANTS(world);
  }
  for (uint64_t i = 0; i < 100; i += 4) {
    world.remove<A>(entities[i]);
    CHECK_INVARIANTS(world);
  }
  for (uint64_t i = 0; i < 100; i += 5) {
    world.destroy(entities[i]);
    CHECK_INVARIANTS(world);
  }
  for (int i = 0; i < 20; ++i) {
    world.create(A{1}, LifeCounter{}); // reusa slots da freeList
    CHECK_INVARIANTS(world);
  }
}

TEST_CASE("I-2: LifeCounter bate com linhas vivas") {
  LifeCounter::alive = 0;
  {
    ecs::World world;
    constexpr int kEntityCount = 60;
    std::vector<ecs::Entity> entities;
    std::vector<bool> hasLifeCounter(kEntityCount, false), dead(kEntityCount, false);
    auto expected = [&] {
      int64_t count = 0;
      for (int i = 0; i < kEntityCount; ++i) {
        count += (hasLifeCounter[i] && !dead[i]);
      }
      return count;
    };
    for (int i = 0; i < kEntityCount; ++i) {
      entities.push_back(world.create(A{uint64_t(i)}));
    }

    for (int i = 0; i < kEntityCount; i += 2) {
      world.add(entities[i], LifeCounter{uint64_t(i)});
      hasLifeCounter[i] = true;
    }
    CHECK(LifeCounter::alive == expected());

    for (int i = 0; i < kEntityCount; i += 4) {
      world.remove<LifeCounter>(entities[i]);
      hasLifeCounter[i] = false;
    }
    CHECK(LifeCounter::alive == expected());

    for (int i = 0; i < kEntityCount; i += 3) {
      world.destroy(entities[i]);
      dead[i] = true;
    }
    CHECK(LifeCounter::alive == expected());
    CHECK_INVARIANTS(world);
  }
  CHECK(LifeCounter::alive == 0);
}
