#include "oly/bridge/c_api.hpp"

#include <doctest/doctest.h>

#include <vector>

#include "components.hpp"
#include "oly/bridge/world_handle.hpp"

namespace {
  struct WorldFixture {
    EngineWorld* world = engineWorldCreate();

    ~WorldFixture() {
      engineWorldDestroy(world);
    }

    bool invariantsHold() {
      char message[256];
      const bool ok = engineCheckInvariants(world, message, sizeof(message)) == 1;
      CHECK_MESSAGE(ok, message);
      return ok;
    }

    EngineEntity create() {
      EngineEntity entity{};
      engineCreate(world, &entity);
      return entity;
    }
  };
}

TEST_CASE("schema exposto pela API C") {
  CHECK(engineSchemaHash() == 0x1234u);
  CHECK(engineSchemaComponentCount() == 3);
  CHECK(engineSchemaComponentId(1) == oly::ecs::componentId<B>());
  CHECK(engineComponentSize(oly::ecs::componentId<A>()) == 8);
  CHECK(engineComponentAlignment(oly::ecs::componentId<A>()) == 8);
  CHECK(engineComponentSize(oly::ecs::componentId<Tag>()) == 0);
}

TEST_CASE("create, add, get, has, remove e destroy por ID") {
  WorldFixture fixture;
  EngineWorld* world = fixture.world;
  const uint32_t idA = oly::ecs::componentId<A>();

  EngineEntity entity = fixture.create();
  CHECK(fixture.invariantsHold());

  A value{5};
  engineAdd(world, entity.index, entity.generation, idA, &value);
  CHECK(static_cast<A*>(engineGet(world, entity.index, entity.generation, idA))->value == 5);
  CHECK(engineHas(world, entity.index, entity.generation, idA) == 1);
  CHECK(fixture.invariantsHold());

  engineRemove(world, entity.index, entity.generation, idA);
  CHECK(engineHas(world, entity.index, entity.generation, idA) == 0);
  CHECK(engineGet(world, entity.index, entity.generation, idA) == nullptr);
  CHECK(fixture.invariantsHold());

  engineAdd(world, entity.index, entity.generation, oly::ecs::componentId<Tag>(), nullptr);
  CHECK(engineHas(world, entity.index, entity.generation, oly::ecs::componentId<Tag>()) == 1);
  CHECK(engineGet(world, entity.index, entity.generation, oly::ecs::componentId<Tag>()) == nullptr);

  engineDestroy(world, entity.index, entity.generation);
  CHECK(engineIsAlive(world, entity.index, entity.generation) == 0);
  CHECK(engineGet(world, entity.index, entity.generation, idA) == nullptr);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("engineQueryBegin entrega descritores por bloco") {
  WorldFixture fixture;
  EngineWorld* world = fixture.world;
  for (uint64_t i = 1; i <= 3; ++i) {
    world->world.create(A{i}, B{0, 0});
  }
  for (uint64_t i = 10; i <= 20; i += 10) {
    world->world.create(A{i}, B{0, 0}, Tag{});
  }

  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  EngineQuery* query = engineQuery(world, ids, 1, 1, 0, 0);

  const uintptr_t* descriptors = nullptr;
  const uint32_t blockCount = engineQueryBegin(world, query, &descriptors);
  CHECK(blockCount == 2);
  CHECK(world->world.isIterating());

  uint64_t sum = 0;
  uint32_t rows = 0;
  for (uint32_t block = 0; block < blockCount; ++block) {
    const uintptr_t* descriptor = descriptors + block * 4;
    const auto* entities = reinterpret_cast<const oly::ecs::Entity*>(descriptor[1]);
    const auto* a = reinterpret_cast<const A*>(descriptor[2]);
    CHECK(descriptor[3] != 0);
    for (uint32_t row = 0; row < descriptor[0]; ++row) {
      sum += a[row].value;
      CHECK(world->world.isAlive(entities[row]));
      ++rows;
    }
  }
  CHECK(rows == 5);
  CHECK(sum == 36);

  engineQueryEnd(world, query);
  CHECK_FALSE(world->world.isIterating());
  CHECK(fixture.invariantsHold());
}

TEST_CASE("engineQuery é cacheada") {
  WorldFixture fixture;
  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  const uint32_t withTag[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>(), oly::ecs::componentId<Tag>()};

  CHECK(engineQuery(fixture.world, ids, 1, 1, 0, 0) == engineQuery(fixture.world, ids, 1, 1, 0, 0));
  CHECK(engineQuery(fixture.world, withTag, 1, 1, 1, 0) != engineQuery(fixture.world, ids, 1, 1, 0, 0));
}

TEST_CASE("engineQueryBegin aninhado na mesma query") {
  WorldFixture fixture;
  EngineWorld* world = fixture.world;
  for (uint64_t i = 0; i < 4; ++i) {
    world->world.create(A{i}, B{0, 0});
  }

  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  EngineQuery* query = engineQuery(world, ids, 1, 1, 0, 0);

  const uintptr_t* outer = nullptr;
  const uint32_t outerBlocks = engineQueryBegin(world, query, &outer);
  std::vector<uintptr_t> outerCopy(outer, outer + outerBlocks * 4);

  const uintptr_t* inner = nullptr;
  CHECK(engineQueryBegin(world, query, &inner) == outerBlocks);
  engineQueryEnd(world, query);
  CHECK(world->world.isIterating());

  engineQueryEnd(world, query);
  CHECK_FALSE(world->world.isIterating());
  CHECK(outerCopy[0] == 4);
  CHECK(fixture.invariantsHold());
}
