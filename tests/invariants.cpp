#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "components.hpp"
#include "ecs/world.hpp"

TEST_CASE("sequência create/add/remove/destroy mantém invariantes") {
  ecs::World w;
  CHECK_INV(w);

  std::vector<ecs::Entity> es;
  for (uint64_t i = 0; i < 100; ++i) {
    es.push_back(w.create());
    CHECK_INV(w);
  }
  for (uint64_t i = 0; i < 100; i += 2) {
    w.add(es[i], A{i});
    CHECK_INV(w);
  }
  for (uint64_t i = 0; i < 100; i += 3) {
    w.add(es[i], Tag{});
    CHECK_INV(w);
  }
  for (uint64_t i = 0; i < 100; i += 4) {
    w.remove<A>(es[i]);
    CHECK_INV(w);
  }
  for (uint64_t i = 0; i < 100; i += 5) {
    w.destroy(es[i]);
    CHECK_INV(w);
  }
  for (int i = 0; i < 20; ++i) {
    w.create(A{1}, LifeCounter{}); // reusa slots da freeList
    CHECK_INV(w);
  }
}

TEST_CASE("I-2: LifeCounter bate com linhas vivas") {
  LifeCounter::alive = 0;
  {
    ecs::World w;
    constexpr int N = 60;
    std::vector<ecs::Entity> es;
    std::vector<bool> hasLC(N, false), dead(N, false);
    auto expected = [&] {
      int64_t n = 0;
      for (int i = 0; i < N; ++i)
        n += (hasLC[i] && !dead[i]);
      return n;
    };
    for (int i = 0; i < N; ++i)
      es.push_back(w.create(A{uint64_t(i)}));

    for (int i = 0; i < N; i += 2) {
      w.add(es[i], LifeCounter{uint64_t(i)});
      hasLC[i] = true;
    }
    CHECK(LifeCounter::alive == expected());

    for (int i = 0; i < N; i += 4) {
      w.remove<LifeCounter>(es[i]);
      hasLC[i] = false;
    }
    CHECK(LifeCounter::alive == expected());

    for (int i = 0; i < N; i += 3) {
      w.destroy(es[i]);
      dead[i] = true;
    }
    CHECK(LifeCounter::alive == expected());
    CHECK_INV(w);
  }
  CHECK(LifeCounter::alive == 0);
}
