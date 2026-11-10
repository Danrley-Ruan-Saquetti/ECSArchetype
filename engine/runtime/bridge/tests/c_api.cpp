#include "oly/bridge/c_api.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "components.hpp"
#include "oly/bridge/world.hpp"

namespace {
  struct WorldFixture {
    OlyWorld* world = olyWorldCreate();

    ~WorldFixture() {
      olyWorldDestroy(world);
    }

    bool invariantsHold() {
      char message[256];
      const bool ok = olyWorldCheckInvariants(world, message, sizeof(message)) == 1;
      CHECK_MESSAGE(ok, message);
      return ok;
    }

    OlyEntity spawn() {
      OlyEntity entity{};
      olyEntitySpawn(world, &entity);
      return entity;
    }

    oly::ecs::World& ecs() {
      return oly::bridge::worldOf(world);
    }
  };
}

TEST_CASE("schema exposto pela API C") {
  CHECK(olySchemaHash() == 0x1234u);
  CHECK(olySchemaComponentCount() == 3);
  CHECK(olySchemaComponentId(1) == oly::ecs::componentId<B>());
  CHECK(olyComponentSize(oly::ecs::componentId<A>()) == 8);
  CHECK(olyComponentAlignment(oly::ecs::componentId<A>()) == 8);
  CHECK(olyComponentSize(oly::ecs::componentId<Tag>()) == 0);
}

TEST_CASE("spawn, add, get, has, remove e destroy por ID") {
  WorldFixture fixture;
  OlyWorld* world = fixture.world;
  const uint32_t idA = oly::ecs::componentId<A>();

  OlyEntity entity = fixture.spawn();
  CHECK(fixture.invariantsHold());

  A value{5};
  olyComponentAdd(world, entity.index, entity.generation, idA, &value);
  CHECK(static_cast<A*>(olyComponentGet(world, entity.index, entity.generation, idA))->value == 5);
  CHECK(olyComponentHas(world, entity.index, entity.generation, idA) == 1);
  CHECK(fixture.invariantsHold());

  olyComponentRemove(world, entity.index, entity.generation, idA);
  CHECK(olyComponentHas(world, entity.index, entity.generation, idA) == 0);
  CHECK(olyComponentGet(world, entity.index, entity.generation, idA) == nullptr);
  CHECK(fixture.invariantsHold());

  olyComponentAdd(world, entity.index, entity.generation, oly::ecs::componentId<Tag>(), nullptr);
  CHECK(olyComponentHas(world, entity.index, entity.generation, oly::ecs::componentId<Tag>()) == 1);
  CHECK(olyComponentGet(world, entity.index, entity.generation, oly::ecs::componentId<Tag>()) == nullptr);

  olyEntityDestroy(world, entity.index, entity.generation);
  CHECK(olyEntityIsAlive(world, entity.index, entity.generation) == 0);
  CHECK(olyComponentGet(world, entity.index, entity.generation, idA) == nullptr);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyQueryBegin entrega descritores por bloco") {
  WorldFixture fixture;
  OlyWorld* world = fixture.world;
  for (uint64_t i = 1; i <= 3; ++i) {
    fixture.ecs().spawn(A{i}, B{0, 0});
  }
  for (uint64_t i = 10; i <= 20; i += 10) {
    fixture.ecs().spawn(A{i}, B{0, 0}, Tag{});
  }

  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  OlyQuery* query = olyQueryCreate(world, ids, 1, 1, 0, 0);

  uint32_t blockCount = 0;
  const uintptr_t* descriptors = nullptr;
  OlyIteration* iteration = olyQueryBegin(world, query, &blockCount, &descriptors);
  CHECK(blockCount == 2);
  CHECK(fixture.ecs().isIterating());

  uint64_t sum = 0;
  uint32_t rows = 0;
  for (uint32_t block = 0; block < blockCount; ++block) {
    const uintptr_t* descriptor = descriptors + block * 4;
    const auto* entities = reinterpret_cast<const oly::ecs::Entity*>(descriptor[1]);
    const auto* a = reinterpret_cast<const A*>(descriptor[2]);
    CHECK(descriptor[3] != 0);
    for (uint32_t row = 0; row < descriptor[0]; ++row) {
      sum += a[row].value;
      CHECK(fixture.ecs().isAlive(entities[row]));
      ++rows;
    }
  }
  CHECK(rows == 5);
  CHECK(sum == 36);

  olyQueryEnd(world, iteration);
  CHECK_FALSE(fixture.ecs().isIterating());
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyQueryCreate é cacheada") {
  WorldFixture fixture;
  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  const uint32_t withTag[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>(), oly::ecs::componentId<Tag>()};

  CHECK(olyQueryCreate(fixture.world, ids, 1, 1, 0, 0) == olyQueryCreate(fixture.world, ids, 1, 1, 0, 0));
  CHECK(olyQueryCreate(fixture.world, withTag, 1, 1, 1, 0) != olyQueryCreate(fixture.world, ids, 1, 1, 0, 0));
}

TEST_CASE("olyQueryBegin aninhado preserva os descritores do externo") {
  WorldFixture fixture;
  OlyWorld* world = fixture.world;
  for (uint64_t i = 0; i < 4; ++i) {
    fixture.ecs().spawn(A{i}, B{0, 0});
  }
  fixture.ecs().spawn(A{9}, B{0, 0}, Tag{});

  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  OlyQuery* query = olyQueryCreate(world, ids, 1, 1, 0, 0);

  uint32_t outerBlocks = 0;
  const uintptr_t* outer = nullptr;
  OlyIteration* outerIteration = olyQueryBegin(world, query, &outerBlocks, &outer);
  std::vector<uintptr_t> outerCopy(outer, outer + outerBlocks * 4);

  uint32_t innerBlocks = 0;
  const uintptr_t* inner = nullptr;
  OlyIteration* innerIteration = olyQueryBegin(world, query, &innerBlocks, &inner);
  CHECK(innerBlocks == outerBlocks);
  CHECK(inner != outer);
  olyQueryEnd(world, innerIteration);
  CHECK(fixture.ecs().isIterating());

  CHECK(std::memcmp(outer, outerCopy.data(), outerCopy.size() * sizeof(uintptr_t)) == 0);
  olyQueryEnd(world, outerIteration);
  CHECK_FALSE(fixture.ecs().isIterating());
  CHECK(fixture.invariantsHold());
}

TEST_CASE("worldOf dá acesso ao World do handle") {
  WorldFixture fixture;
  OlyEntity handle = fixture.spawn();

  CHECK(oly::bridge::worldOf(fixture.world).isAlive(oly::ecs::Entity{handle.index, handle.generation}));
}

TEST_CASE("iteração repetida reaproveita o slot") {
  WorldFixture fixture;
  fixture.ecs().spawn(A{1});
  const uint32_t ids[] = {oly::ecs::componentId<A>()};
  OlyQuery* query = olyQueryCreate(fixture.world, ids, 1, 0, 0, 0);

  uint32_t blockCount = 0;
  const uintptr_t* descriptors = nullptr;
  OlyIteration* first = olyQueryBegin(fixture.world, query, &blockCount, &descriptors);
  olyQueryEnd(fixture.world, first);
  OlyIteration* second = olyQueryBegin(fixture.world, query, &blockCount, &descriptors);
  olyQueryEnd(fixture.world, second);

  CHECK(first == second);
}
