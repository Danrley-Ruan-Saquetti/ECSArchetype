#include "oly/bridge/c_api.hpp"

#include <doctest/doctest.h>

#include <cstring>
#include <vector>

#include "components.hpp"
#include "oly/bridge/world.hpp"
#include "test_events.hpp"

namespace {
  // 2 + readCount + writeCount para a query read A, write B: [count, entidades, coluna A, coluna B].
  constexpr uint32_t DescriptorStrideReadAWriteB = 4;

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
  CHECK(olyComponentSize(oly::ecs::componentId<A>()) == sizeof(A));
  CHECK(olyComponentAlignment(oly::ecs::componentId<A>()) == alignof(A));
  CHECK(olyComponentSize(oly::ecs::componentId<Tag>()) == 0);
}

TEST_CASE("olyComponentAdd por ID e olyComponentGet devolve o valor") {
  WorldFixture fixture;
  const OlyEntity entity = fixture.spawn();
  const uint32_t idA = oly::ecs::componentId<A>();
  A value{5};

  olyComponentAdd(fixture.world, entity.index, entity.generation, idA, &value);

  CHECK(static_cast<A*>(olyComponentGet(fixture.world, entity.index, entity.generation, idA))->value == 5);
  CHECK(olyComponentHas(fixture.world, entity.index, entity.generation, idA) == 1);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyComponentRemove por ID tira o componente") {
  WorldFixture fixture;
  const OlyEntity entity = fixture.spawn();
  const uint32_t idA = oly::ecs::componentId<A>();
  A value{5};
  olyComponentAdd(fixture.world, entity.index, entity.generation, idA, &value);

  olyComponentRemove(fixture.world, entity.index, entity.generation, idA);

  CHECK(olyComponentHas(fixture.world, entity.index, entity.generation, idA) == 0);
  CHECK(olyComponentGet(fixture.world, entity.index, entity.generation, idA) == nullptr);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("tag adicionada por ID existe mas não tem dados") {
  WorldFixture fixture;
  const OlyEntity entity = fixture.spawn();
  const uint32_t idTag = oly::ecs::componentId<Tag>();

  olyComponentAdd(fixture.world, entity.index, entity.generation, idTag, nullptr);

  CHECK(olyComponentHas(fixture.world, entity.index, entity.generation, idTag) == 1);
  CHECK(olyComponentGet(fixture.world, entity.index, entity.generation, idTag) == nullptr);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyEntityDestroy mata a entidade e seus componentes deixam de ser acessíveis") {
  WorldFixture fixture;
  const OlyEntity entity = fixture.spawn();
  const uint32_t idA = oly::ecs::componentId<A>();
  A value{5};
  olyComponentAdd(fixture.world, entity.index, entity.generation, idA, &value);

  olyEntityDestroy(fixture.world, entity.index, entity.generation);

  CHECK(olyEntityIsAlive(fixture.world, entity.index, entity.generation) == 0);
  CHECK(olyComponentGet(fixture.world, entity.index, entity.generation, idA) == nullptr);
  CHECK(fixture.invariantsHold());
}

TEST_CASE("olyQueryBegin entrega descritores por bloco") {
  WorldFixture fixture;
  for (uint64_t value = 1; value <= 3; ++value) {
    fixture.ecs().spawn(A{value}, B{0, 0});
  }
  for (uint64_t value = 10; value <= 20; value += 10) {
    fixture.ecs().spawn(A{value}, B{0, 0}, Tag{});
  }
  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  OlyQuery* query = olyQueryCreate(fixture.world, ids, 1, 1, 0, 0);
  uint32_t blockCount = 0;
  const uintptr_t* descriptors = nullptr;

  OlyIteration* iteration = olyQueryBegin(fixture.world, query, &blockCount, &descriptors);

  CHECK(blockCount == 2);
  CHECK(fixture.ecs().isIterating());
  uint64_t sum = 0;
  uint32_t rows = 0;
  for (uint32_t block = 0; block < blockCount; ++block) {
    const uintptr_t* descriptor = descriptors + block * DescriptorStrideReadAWriteB;
    const auto* entities = reinterpret_cast<const oly::ecs::Entity*>(descriptor[1]);
    const auto* columnA = reinterpret_cast<const A*>(descriptor[2]);
    CHECK(descriptor[3] != 0);
    for (uint32_t row = 0; row < descriptor[0]; ++row) {
      sum += columnA[row].value;
      CHECK(fixture.ecs().isAlive(entities[row]));
      ++rows;
    }
  }
  CHECK(rows == 5);
  CHECK(sum == 36);
  olyQueryEnd(fixture.world, iteration);
}

TEST_CASE("olyQueryEnd encerra a iteração") {
  WorldFixture fixture;
  fixture.ecs().spawn(A{1}, B{0, 0});
  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  OlyQuery* query = olyQueryCreate(fixture.world, ids, 1, 1, 0, 0);
  uint32_t blockCount = 0;
  const uintptr_t* descriptors = nullptr;
  OlyIteration* iteration = olyQueryBegin(fixture.world, query, &blockCount, &descriptors);

  olyQueryEnd(fixture.world, iteration);

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
  for (uint64_t value = 0; value < 4; ++value) {
    fixture.ecs().spawn(A{value}, B{0, 0});
  }
  fixture.ecs().spawn(A{9}, B{0, 0}, Tag{});
  const uint32_t ids[] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>()};
  OlyQuery* query = olyQueryCreate(fixture.world, ids, 1, 1, 0, 0);
  uint32_t outerBlocks = 0;
  const uintptr_t* outer = nullptr;
  OlyIteration* outerIteration = olyQueryBegin(fixture.world, query, &outerBlocks, &outer);
  const std::vector<uintptr_t> outerCopy(outer, outer + outerBlocks * DescriptorStrideReadAWriteB);
  uint32_t innerBlocks = 0;
  const uintptr_t* inner = nullptr;

  OlyIteration* innerIteration = olyQueryBegin(fixture.world, query, &innerBlocks, &inner);
  olyQueryEnd(fixture.world, innerIteration);

  CHECK(innerBlocks == outerBlocks);
  CHECK(inner != outer);
  CHECK(fixture.ecs().isIterating());
  CHECK(std::memcmp(outer, outerCopy.data(), outerCopy.size() * sizeof(uintptr_t)) == 0);
  olyQueryEnd(fixture.world, outerIteration);
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

TEST_CASE("eventos do esquema expostos pela API C") {
  CHECK(olySchemaEventCount() == 1);
  CHECK(olySchemaEventSize(0) == sizeof(BridgeTestEvent));
  CHECK(olySchemaEventAlignment(0) == alignof(BridgeTestEvent));
}

TEST_CASE("ações do esquema expostas pela API C") {
  CHECK(olySchemaActionCount() == 3);
  CHECK(olySchemaActionKind(0) == OLY_ACTION_BUTTON);
  CHECK(olySchemaActionKind(1) == OLY_ACTION_AXIS2D);
  CHECK(olySchemaActionKind(2) == OLY_ACTION_POINTER);
}
