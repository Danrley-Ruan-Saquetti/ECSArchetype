#include <doctest/doctest.h>

#include <type_traits>

#include "components.hpp"
#include "oly/ecs/detail/archetype.hpp"
#include "oly/ecs/diagnostics.hpp"
#include "oly/ecs/world.hpp"

namespace {
  template <class W>
  concept HasMemberStats = requires(W& world) { world.stats(); };
  template <class W>
  concept HasMemberCheckInvariants = requires(W& world) { world.checkInvariants(nullptr); };
  template <class W, class C>
  concept HasFlushSequential = requires(W& world, C& commands) { world.flushSequential(commands); };
  template <class C>
  concept HasPlan = requires(C& commands) { commands.plan(); };
  template <class C>
  concept HasForEach = requires(C& commands) { commands.forEach([](auto&) {}); };
  template <class C>
  concept HasStore = requires(C& commands) { commands.store(); };
  template <class Q>
  concept HasOnArchetypeCreated = requires(Q& query, oly::ecs::detail::Archetype& archetype) { query.onArchetypeCreated(archetype); };
}

TEST_CASE("World e CommandBuffer não expõem diagnóstico nem ganchos de teste") {
  static_assert(!HasMemberStats<oly::ecs::World>);
  static_assert(!HasMemberCheckInvariants<oly::ecs::World>);
  static_assert(!HasFlushSequential<oly::ecs::World, oly::ecs::CommandBuffer>);
  static_assert(!HasPlan<oly::ecs::CommandBuffer> && !HasForEach<oly::ecs::CommandBuffer> && !HasStore<oly::ecs::CommandBuffer>);
  static_assert(!HasOnArchetypeCreated<oly::ecs::Query<A>>);
  static_assert(!std::is_constructible_v<oly::ecs::CommandBuffer, oly::ecs::detail::EntityStore&>);
}

TEST_CASE("diagnóstico por funções livres") {
  oly::ecs::World world;
  world.spawn(A{1});

  CHECK(oly::ecs::checkInvariants(world));
  CHECK(oly::ecs::archetypeCount(world) == 2);
  CHECK(oly::ecs::stats(world).liveEntities == 1);
  CHECK(oly::ecs::signatureLookupCount(world) > 0);
}
