#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "components.hpp"
#include "ecs/type_info.hpp"

TEST_CASE("componentId é estável, denso e ignora cvref") {
  auto idA = ecs::componentId<A>();
  auto idB = ecs::componentId<B>();

  CHECK(ecs::componentId<A>() == idA);
  CHECK(ecs::componentId<const A&>() == idA);
  CHECK(idB != idA);
  CHECK(std::max(idA, idB) < ecs::registeredTypeCount());
}

TEST_CASE("TypeInfo de trivial, não trivial e tag") {
  auto& infoA = ecs::typeInfo(ecs::componentId<A>());
  CHECK(infoA.size == sizeof(A));
  CHECK(infoA.trivial);
  CHECK_FALSE(infoA.tag);
  CHECK(infoA.moveConstruct == nullptr);
  CHECK(infoA.destroy == nullptr);
  CHECK(infoA.name == "A");

  auto& infoString = ecs::typeInfo(ecs::componentId<std::string>());
  CHECK_FALSE(infoString.trivial);
  CHECK(infoString.moveConstruct != nullptr);
  CHECK(infoString.destroy != nullptr);

  auto& infoTag = ecs::typeInfo(ecs::componentId<Tag>());
  CHECK(infoTag.tag);
  CHECK(infoTag.size == 0);

  CHECK(ecs::typeInfo(ecs::componentId<Wide>()).alignment == 64);
}
