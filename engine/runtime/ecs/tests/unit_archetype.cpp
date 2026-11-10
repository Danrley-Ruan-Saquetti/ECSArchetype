#include <doctest/doctest.h>

#include <algorithm>

#include "components.hpp"
#include "oly/ecs/archetype.hpp"
#include "oly/core/small_map.hpp"

namespace {
  struct C {
    uint32_t value;
  };

  ecs::Signature sorted(ecs::Signature signature) {
    std::sort(signature.begin(), signature.end());
    return signature;
  }
}

TEST_CASE("columnTypes exclui tags; signature ordenada inclui") {
  ecs::Signature signature = sorted({ecs::componentId<A>(), ecs::componentId<Tag>(), ecs::componentId<B>()});

  ecs::Archetype archetype(1, signature);

  CHECK(archetype.signature == signature);
  CHECK(archetype.columnTypes.size() == 2);
  CHECK(archetype.table.columnCount() == 2);
  CHECK(ecs::columnOf(archetype, ecs::componentId<Tag>()) == ecs::NoColumn);
  CHECK(ecs::columnOf(archetype, ecs::componentId<A>()) != ecs::NoColumn);
  CHECK(ecs::columnOf(archetype, ecs::componentId<C>()) == ecs::NoColumn);
}

TEST_CASE("buildEdge mapeia colunas por merge") {
  const auto idA = ecs::componentId<A>(), idB = ecs::componentId<B>(), idC = ecs::componentId<C>();
  ecs::Archetype source(1, sorted({idA, idB}));
  {
    ecs::Archetype destination(2, sorted({idA, idB, idC}));
    auto edge = ecs::buildEdge(source, destination, idC);
    CHECK(edge.target == &destination);
    REQUIRE(edge.sourceToDestination.size() == 2);
    CHECK(edge.sourceToDestination[ecs::columnOf(source, idA)] == ecs::columnOf(destination, idA));
    CHECK(edge.sourceToDestination[ecs::columnOf(source, idB)] == ecs::columnOf(destination, idB));
    CHECK(edge.addedColumn == ecs::columnOf(destination, idC));
  }
  {
    ecs::Archetype destination(3, sorted({idB}));
    auto edge = ecs::buildEdge(source, destination, ecs::NoComponent);
    CHECK(edge.sourceToDestination[ecs::columnOf(source, idA)] == -1);
    CHECK(edge.sourceToDestination[ecs::columnOf(source, idB)] == 0);
    CHECK(edge.addedColumn == -1);
  }
  {
    ecs::Archetype destination(4, sorted({idA, idB, ecs::componentId<Tag>()}));
    auto edge = ecs::buildEdge(source, destination, ecs::componentId<Tag>());
    CHECK(edge.addedColumn == -1); // tag não tem coluna
    CHECK(edge.sourceToDestination[0] == 0);
    CHECK(edge.sourceToDestination[1] == 1);
  }
}

TEST_CASE("SmallMap mantém ordem e encontra") {
  ecs::SmallMap<uint32_t, int> map;

  map.insert(5, 50);
  map.insert(1, 10);
  map.insert(3, 30);

  CHECK(map.size() == 3);
  CHECK(map.begin()->first == 1);
  REQUIRE(map.find(3) != nullptr);
  CHECK(*map.find(3) == 30);
  CHECK(map.find(2) == nullptr);
  CHECK(map.capacityBytes() >= 3 * sizeof(std::pair<uint32_t, int>));
}

TEST_CASE("SignatureHash difere para assinaturas diferentes") {
  CHECK(ecs::SignatureHash{}({1, 2}) != ecs::SignatureHash{}({2, 1, 0}));
}
