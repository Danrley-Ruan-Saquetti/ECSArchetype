#include <doctest/doctest.h>

#include <algorithm>
#include <string>

#include "components.hpp"
#include "ecs/type_info.hpp"

TEST_CASE("componentId é estável, denso e ignora cvref") {
  auto a = ecs::componentId<A>();
  auto b = ecs::componentId<B>();

  CHECK(ecs::componentId<A>() == a);
  CHECK(ecs::componentId<const A&>() == a);
  CHECK(b != a);
  CHECK(std::max(a, b) < ecs::registeredTypeCount());
}

TEST_CASE("TypeInfo de trivial, não trivial e tag") {
  auto& ia = ecs::typeInfo(ecs::componentId<A>());
  CHECK(ia.size == sizeof(A));
  CHECK(ia.trivial);
  CHECK_FALSE(ia.tag);
  CHECK(ia.moveConstruct == nullptr);
  CHECK(ia.destroy == nullptr);
  CHECK(ia.name == "A");

  auto& is = ecs::typeInfo(ecs::componentId<std::string>());
  CHECK_FALSE(is.trivial);
  CHECK(is.moveConstruct != nullptr);
  CHECK(is.destroy != nullptr);

  auto& it = ecs::typeInfo(ecs::componentId<Tag>());
  CHECK(it.tag);
  CHECK(it.size == 0);

  CHECK(ecs::typeInfo(ecs::componentId<Wide>()).align == 64);
}
