#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "components.hpp"
#include "oly/ecs/component.hpp"

TEST_CASE("componentId é estável, denso e ignora cvref") {
  auto idA = oly::ecs::componentId<A>();
  auto idB = oly::ecs::componentId<B>();

  CHECK(oly::ecs::componentId<A>() == idA);
  CHECK(oly::ecs::componentId<const A&>() == idA);
  CHECK(idB != idA);
  CHECK(std::max(idA, idB) < oly::ecs::registeredTypeCount());
}

TEST_CASE("TypeInfo de trivial, não trivial e tag") {
  auto& infoA = oly::ecs::typeInfo(oly::ecs::componentId<A>());
  CHECK(infoA.size == sizeof(A));
  CHECK(infoA.trivial);
  CHECK_FALSE(infoA.tag);
  CHECK(infoA.moveConstruct == nullptr);
  CHECK(infoA.destroy == nullptr);
  CHECK(infoA.name == "A");

  auto& infoString = oly::ecs::typeInfo(oly::ecs::componentId<std::string>());
  CHECK_FALSE(infoString.trivial);
  CHECK(infoString.moveConstruct != nullptr);
  CHECK(infoString.destroy != nullptr);

  auto& infoTag = oly::ecs::typeInfo(oly::ecs::componentId<Tag>());
  CHECK(infoTag.tag);
  CHECK(infoTag.size == 0);

  CHECK(oly::ecs::typeInfo(oly::ecs::componentId<Wide>()).alignment == 64);
}
