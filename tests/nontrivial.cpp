#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "components.hpp"
#include "ecs/world.hpp"

namespace {
  std::string longStr(uint64_t i) {
    return std::string(48, 'z') + std::to_string(i);
  }
}

TEST_CASE("string, unique_ptr, vector e LifeCounter sobrevivem a realocação e transições") {
  LifeCounter::alive = 0;
  {
    ecs::World w;
    std::vector<ecs::Entity> es;
    for (uint64_t i = 0; i < 300; ++i)
      es.push_back(w.create(longStr(i), std::make_unique<int>(int(i)), std::vector<int>(i % 7 + 1, int(i)),
                            LifeCounter{i}));
    CHECK(LifeCounter::alive == 300);

    for (uint64_t i = 0; i < 300; i += 2)
      w.add(es[i], A{i});
    for (uint64_t i = 0; i < 300; i += 4)
      w.remove<A>(es[i]);
    CHECK_INV(w);

    for (uint64_t i = 1; i < 300; i += 2)
      w.destroy(es[i]);
    CHECK(LifeCounter::alive == 150);

    for (uint64_t i = 0; i < 300; i += 2) {
      CHECK(w.get<std::string>(es[i]) == longStr(i));
      CHECK(*w.get<std::unique_ptr<int>>(es[i]) == int(i));
      CHECK(w.get<std::vector<int>>(es[i]).size() == i % 7 + 1);
      CHECK(w.get<LifeCounter>(es[i]).v == i);
      CHECK(w.has<A>(es[i]) == (i % 4 != 0));
      if (i % 4 != 0)
        CHECK(w.get<A>(es[i]).v == i);
    }
    for (uint64_t i = 1; i < 300; i += 2)
      CHECK_FALSE(w.isAlive(es[i]));
    CHECK_INV(w);
  }
  CHECK(LifeCounter::alive == 0);
}

TEST_CASE("remover componente não trivial destrói o valor") {
  LifeCounter::alive = 0;
  {
    ecs::World w;
    auto e = w.create(LifeCounter{1}, A{2});
    CHECK(LifeCounter::alive == 1);

    w.remove<LifeCounter>(e);
    CHECK(LifeCounter::alive == 0);
    CHECK(w.get<A>(e).v == 2);

    w.add(e, LifeCounter{3});
    w.add(e, LifeCounter{4}); // atribui sobre o existente
    CHECK(LifeCounter::alive == 1);
    CHECK(w.get<LifeCounter>(e).v == 4);
  }
  CHECK(LifeCounter::alive == 0);
}
