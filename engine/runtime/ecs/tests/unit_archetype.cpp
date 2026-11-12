#include <doctest/doctest.h>

#include <algorithm>

#include "components.hpp"
#include "oly/ecs/detail/archetype.hpp"
#include "oly/core/small_map.hpp"

namespace {
  struct C {
    uint32_t value;
  };

  oly::ecs::detail::Signature sorted(oly::ecs::detail::Signature signature) {
    std::sort(signature.begin(), signature.end());
    return signature;
  }
}

TEST_CASE("columnTypes excludes tags; sorted signature includes them") {
  oly::ecs::detail::Signature signature = sorted({oly::ecs::componentId<A>(), oly::ecs::componentId<Tag>(), oly::ecs::componentId<B>()});

  oly::ecs::detail::Archetype archetype(1, signature);

  CHECK(archetype.signature == signature);
  CHECK(archetype.columnTypes.size() == 2);
  CHECK(archetype.table.columnCount() == 2);
  CHECK(oly::ecs::detail::columnOf(archetype, oly::ecs::componentId<Tag>()) == oly::ecs::detail::NoColumn);
  CHECK(oly::ecs::detail::columnOf(archetype, oly::ecs::componentId<A>()) != oly::ecs::detail::NoColumn);
  CHECK(oly::ecs::detail::columnOf(archetype, oly::ecs::componentId<C>()) == oly::ecs::detail::NoColumn);
}

TEST_CASE("buildEdge maps columns by merge") {
  const auto idA = oly::ecs::componentId<A>(), idB = oly::ecs::componentId<B>(), idC = oly::ecs::componentId<C>();
  oly::ecs::detail::Archetype source(1, sorted({idA, idB}));
  {
    oly::ecs::detail::Archetype destination(2, sorted({idA, idB, idC}));
    auto edge = oly::ecs::detail::buildEdge(source, destination, idC);
    CHECK(edge.target == &destination);
    REQUIRE(edge.sourceToDestination.size() == 2);
    CHECK(edge.sourceToDestination[oly::ecs::detail::columnOf(source, idA)] == oly::ecs::detail::columnOf(destination, idA));
    CHECK(edge.sourceToDestination[oly::ecs::detail::columnOf(source, idB)] == oly::ecs::detail::columnOf(destination, idB));
    CHECK(edge.addedColumn == oly::ecs::detail::columnOf(destination, idC));
  }
  {
    oly::ecs::detail::Archetype destination(3, sorted({idB}));
    auto edge = oly::ecs::detail::buildEdge(source, destination, oly::ecs::detail::NoComponent);
    CHECK(edge.sourceToDestination[oly::ecs::detail::columnOf(source, idA)] == -1);
    CHECK(edge.sourceToDestination[oly::ecs::detail::columnOf(source, idB)] == 0);
    CHECK(edge.addedColumn == -1);
  }
  {
    oly::ecs::detail::Archetype destination(4, sorted({idA, idB, oly::ecs::componentId<Tag>()}));
    auto edge = oly::ecs::detail::buildEdge(source, destination, oly::ecs::componentId<Tag>());
    CHECK(edge.addedColumn == -1); // a tag has no column
    CHECK(edge.sourceToDestination[0] == 0);
    CHECK(edge.sourceToDestination[1] == 1);
  }
}

TEST_CASE("SmallMap keeps order and finds") {
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

TEST_CASE("SignatureHash differs for different signatures") {
  CHECK(oly::ecs::detail::SignatureHash{}({1, 2}) != oly::ecs::detail::SignatureHash{}({2, 1, 0}));
}
