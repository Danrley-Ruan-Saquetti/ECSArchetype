#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "alloc_counter.hpp"
#include "components.hpp"
#include "oly/ecs/world.hpp"

TEST_CASE("spawn() nasce vivo no arquétipo raiz") {
  oly::ecs::World world;

  auto entity = world.spawn();

  CHECK(world.isAlive(entity));
  CHECK_FALSE(world.has<A>(entity));
}

TEST_CASE("spawn(components...) nasce no arquétipo final com valores") {
  oly::ecs::World world;

  auto entity = world.spawn(A{1}, B{2, 3}, Tag{});

  CHECK(world.get<A>(entity).value == 1);
  CHECK(world.get<B>(entity).second == 3);
  CHECK(world.has<Tag>(entity));
}

TEST_CASE("spawn(components...) copia lvalues") {
  oly::ecs::World world;
  A componentA{4};

  auto entity = world.spawn(componentA);
  componentA.value = 5;

  CHECK(world.get<A>(entity).value == 4);
}

TEST_CASE("add sobre T existente atribui sem mudar arquétipo") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});

  world.add(entity, A{7});

  CHECK(world.get<A>(entity).value == 7);
}

TEST_CASE("add de T ausente move para o arquétipo com T") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});

  world.add(entity, B{2, 3});

  CHECK(world.get<A>(entity).value == 1);
  CHECK(world.get<B>(entity).second == 3);
}

TEST_CASE("remove de T ausente é no-op") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});

  world.remove<B>(entity);

  CHECK(world.get<A>(entity).value == 1);
}

TEST_CASE("remove de T presente") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1}, B{2, 3});

  world.remove<A>(entity);

  CHECK_FALSE(world.has<A>(entity));
  CHECK(world.get<B>(entity).first == 2);
}

TEST_CASE("destroy atualiza o record da entidade deslocada") {
  oly::ecs::World world;
  auto first = world.spawn(A{0});
  auto second = world.spawn(A{1});
  auto third = world.spawn(A{2});

  world.destroy(first);

  CHECK_FALSE(world.isAlive(first));
  CHECK(world.get<A>(third).value == 2);
  CHECK(world.get<A>(second).value == 1);
  CHECK(world.tryGet<A>(first) == nullptr);
}

TEST_CASE("remove/destroy da última e da única linha") {
  oly::ecs::World world;
  auto first = world.spawn(A{1});
  auto second = world.spawn(A{2});

  world.remove<A>(second);
  CHECK(world.get<A>(first).value == 1);
  CHECK_FALSE(world.has<A>(second));

  world.destroy(first);
  CHECK(world.isAlive(second));
}

TEST_CASE("add com valor lido de outra entidade da tabela destino que realoca") {
  oly::ecs::World world;
  std::vector<oly::ecs::Entity> full;
  for (uint64_t i = 0; i < 64; ++i) {
    full.push_back(world.spawn(A{i}, B{i, i})); // capacidade 64 cheia
  }

  auto entity = world.spawn(A{999});

  world.add(entity, world.get<B>(full[5])); // destino {A,B} realoca no pushUninitialized

  CHECK(world.get<B>(entity).first == 5);
  CHECK(world.get<B>(full[5]).second == 5);
}

TEST_CASE("tryGet em entidade morta e sem T devolve nullptr") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});
  CHECK(world.tryGet<B>(entity) == nullptr);
  REQUIRE(world.tryGet<A>(entity) != nullptr);
  CHECK(world.tryGet<A>(entity)->value == 1);

  world.destroy(entity);

  CHECK(world.tryGet<A>(entity) == nullptr);
  CHECK_FALSE(world.has<A>(entity));
}

TEST_CASE("tags: add/has/remove sem coluna") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});

  world.add(entity, Tag{});
  CHECK(world.has<Tag>(entity));
  CHECK(world.get<A>(entity).value == 1);

  world.remove<Tag>(entity);
  CHECK_FALSE(world.has<Tag>(entity));
  CHECK(world.get<A>(entity).value == 1);
}

TEST_CASE("handle antigo após reciclagem do índice") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});
  world.destroy(entity);

  auto recycled = world.spawn(A{2});

  CHECK(recycled.index == entity.index);
  CHECK_FALSE(world.isAlive(entity));
  CHECK(world.tryGet<A>(entity) == nullptr);
  CHECK(world.get<A>(recycled).value == 2);
}

TEST_CASE("NF-P4: add/remove com aresta em cache não aloca nem consulta hash") {
  struct C1 {
    uint64_t value;
  };
  struct C2 {
    uint64_t value;
  };
  struct C3 {
    uint64_t value;
  };
  struct C4 {
    uint64_t value;
  };

  oly::ecs::World world;
  auto entity = world.spawn(C1{1}, C2{2}, C3{3});
  world.add(entity, C4{4});
  world.remove<C4>(entity); // aquece arestas e capacidade (64 linhas)
  auto allocationsBefore = allocationCount();
  auto lookupsBefore = oly::ecs::signatureLookupCount(world);

  for (int i = 0; i < 1000; ++i) {
    world.add(entity, C4{uint64_t(i)});
    world.remove<C4>(entity);
  }

  auto allocationsAfter = allocationCount();
  auto lookupsAfter = oly::ecs::signatureLookupCount(world);
  CHECK(allocationsAfter == allocationsBefore);
  CHECK(lookupsAfter == lookupsBefore);
  CHECK(world.get<C3>(entity).value == 3);
}

TEST_CASE("archetypeCount conta a raiz e os arquétipos criados") {
  oly::ecs::World world;
  CHECK(oly::ecs::archetypeCount(world) == 1);

  world.spawn(A{1});
  CHECK(oly::ecs::archetypeCount(world) == 2);

  world.spawn(A{2});
  CHECK(oly::ecs::archetypeCount(world) == 2);

  world.spawn(A{3}, B{0, 0});
  CHECK(oly::ecs::archetypeCount(world) == 3);
}

TEST_CASE("stats: contagens, histograma e memória ociosa") {
  oly::ecs::World world;
  for (uint64_t i = 0; i < 5; ++i) {
    world.spawn(A{i}); // {A}: 5
  }
  world.spawn(A{0}, B{0, 0}); // {A,B}: 1

  auto stats = oly::ecs::stats(world);

  CHECK(stats.archetypeCount == 3); // raiz, {A}, {A,B}
  CHECK(stats.liveEntities == 6);
  CHECK(stats.histogram[0] == 1); // raiz vazia
  CHECK(stats.histogram[1] == 1); // count 1
  CHECK(stats.histogram[3] == 1); // count 5 ∈ [4, 8)

  auto& archetypeA = *std::find_if(stats.archetypes.begin(), stats.archetypes.end(),
                                   [](auto& archetype) { return archetype.signature == oly::ecs::detail::Signature{oly::ecs::componentId<A>()}; });
  CHECK(archetypeA.capacity == 64);
  CHECK(archetypeA.idleBytes == (64 - 5) * (sizeof(A) + sizeof(oly::ecs::Entity)));
  CHECK(archetypeA.columnBytes == 64 * (sizeof(A) + sizeof(oly::ecs::Entity)));
}
