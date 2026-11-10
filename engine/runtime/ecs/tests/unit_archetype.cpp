#include <doctest/doctest.h>

#include <algorithm>

#include "components.hpp"
#include "oly/ecs/archetype.hpp"
#include "oly/core/small_map.hpp"

namespace {
  struct C {
    uint32_t value;
  };

  oly::ecs::Signature sorted(oly::ecs::Signature signature) {
    std::sort(signature.begin(), signature.end());
    return signature;
  }
}

TEST_CASE("columnTypes exclui tags; signature ordenada inclui") {
  oly::ecs::Signature signature = sorted({oly::ecs::componentId<A>(), oly::ecs::componentId<Tag>(), oly::ecs::componentId<B>()});

  oly::ecs::Archetype archetype(1, signature);

  CHECK(archetype.signature == signature);
  CHECK(archetype.columnTypes.size() == 2);
  CHECK(archetype.table.columnCount() == 2);
  CHECK(oly::ecs::columnOf(archetype, oly::ecs::componentId<Tag>()) == oly::ecs::NoColumn);
  CHECK(oly::ecs::columnOf(archetype, oly::ecs::componentId<A>()) != oly::ecs::NoColumn);
  CHECK(oly::ecs::columnOf(archetype, oly::ecs::componentId<C>()) == oly::ecs::NoColumn);
}

TEST_CASE("buildEdge mapeia colunas por merge") {
  const auto idA = oly::ecs::componentId<A>(), idB = oly::ecs::componentId<B>(), idC = oly::ecs::componentId<C>();
  oly::ecs::Archetype source(1, sorted({idA, idB}));
  {
    oly::ecs::Archetype destination(2, sorted({idA, idB, idC}));
    auto edge = oly::ecs::buildEdge(source, destination, idC);
    CHECK(edge.target == &destination);
    REQUIRE(edge.sourceToDestination.size() == 2);
    CHECK(edge.sourceToDestination[oly::ecs::columnOf(source, idA)] == oly::ecs::columnOf(destination, idA));
    CHECK(edge.sourceToDestination[oly::ecs::columnOf(source, idB)] == oly::ecs::columnOf(destination, idB));
    CHECK(edge.addedColumn == oly::ecs::columnOf(destination, idC));
  }
  {
    oly::ecs::Archetype destination(3, sorted({idB}));
    auto edge = oly::ecs::buildEdge(source, destination, oly::ecs::NoComponent);
    CHECK(edge.sourceToDestination[oly::ecs::columnOf(source, idA)] == -1);
    CHECK(edge.sourceToDestination[oly::ecs::columnOf(source, idB)] == 0);
    CHECK(edge.addedColumn == -1);
  }
  {
    oly::ecs::Archetype destination(4, sorted({idA, idB, oly::ecs::componentId<Tag>()}));
    auto edge = oly::ecs::buildEdge(source, destination, oly::ecs::componentId<Tag>());
    CHECK(edge.addedColumn == -1); // tag não tem coluna
    CHECK(edge.sourceToDestination[0] == 0);
    CHECK(edge.sourceToDestination[1] == 1);
  }
}

TEST_CASE("SmallMap mantém ordem e encontra") {
  oly::core::SmallMap<uint32_t, int> map;

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
  CHECK(oly::ecs::SignatureHash{}({1, 2}) != oly::ecs::SignatureHash{}({2, 1, 0}));
}
