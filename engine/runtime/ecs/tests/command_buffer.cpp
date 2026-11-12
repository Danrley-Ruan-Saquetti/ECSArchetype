#include "oly/ecs/command_buffer.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "model.hpp"
#include "oly/ecs/detail/test_access.hpp"
#include "oly/ecs/world.hpp"

namespace {
  void addToCommandBuffer(oly::ecs::CommandBuffer& commandBuffer, oly::ecs::Entity entity, oly::ecs::ComponentId componentId, uint64_t value) {
    if (componentId == oly::ecs::componentId<A>()) {
      commandBuffer.add(entity, A{value});
    } else if (componentId == oly::ecs::componentId<B>()) {
      commandBuffer.add(entity, B{value, ~value});
    } else if (componentId == oly::ecs::componentId<FuzzState>()) {
      commandBuffer.add(entity, fuzz::makeFuzzState(value));
    } else if (componentId == oly::ecs::componentId<Tag>()) {
      commandBuffer.add(entity, Tag{});
    } else {
      commandBuffer.add(entity, LifeCounter{value});
    }
  }
}

TEST_CASE("spawn referenced in the same buffer") {
  oly::ecs::World world;
  auto commandBuffer = world.commands();
  auto entity = commandBuffer.spawn();
  CHECK_FALSE(world.isAlive(entity));

  commandBuffer.add(entity, A{5});
  commandBuffer.add(entity, FuzzState{std::string(40, 'x')});
  oly::ecs::detail::WorldTestAccess::flushSequential(world, commandBuffer);

  CHECK(world.get<A>(entity).value == 5);
  CHECK(world.get<FuzzState>(entity).text == std::string(40, 'x'));
  CHECK_INVARIANTS(world);
}

TEST_CASE("non-trivial payload survives buffer growth") {
  oly::ecs::World world;
  auto entity = world.spawn();
  auto shortEntity = world.spawn();
  auto commandBuffer = world.commands();
  std::string big = "payload-that-does-not-fit-in-sso-0123456789";

  commandBuffer.add(entity, FuzzState{big});
  commandBuffer.add(shortEntity, FuzzState{"abc"}); // SSO: a memcpy would leave the internal pointer pointing into the old block
  for (int i = 0; i < 500; ++i) {
    commandBuffer.add(world.spawn(), A{uint64_t(i)}); // forces several reallocations
  }
  oly::ecs::detail::WorldTestAccess::flushSequential(world, commandBuffer);

  CHECK(world.get<FuzzState>(entity).text == big);
  CHECK(world.get<FuzzState>(shortEntity).text == "abc");
  CHECK_INVARIANTS(world);
}

TEST_CASE("alignas(64) payload stays aligned") {
  oly::ecs::World world;
  auto entity = world.spawn();
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, A{1}); // shifts the offset of the next record
  commandBuffer.add(entity, Wide{});

  int seen = 0;
  oly::ecs::detail::CommandBufferTestAccess::forEach(commandBuffer, [&](const oly::ecs::detail::CommandView& command) {
    if (command.operation == oly::ecs::detail::CommandOperation::Add && command.componentId == oly::ecs::componentId<Wide>()) {
      ++seen;
      CHECK(reinterpret_cast<uintptr_t>(command.payload) % 64 == 0);
    }
  });
  CHECK(seen == 1);

  oly::ecs::detail::WorldTestAccess::flushSequential(world, commandBuffer);
  CHECK(world.has<Wide>(entity));
}

TEST_CASE("forEach replays the recording order") {
  oly::ecs::World world;
  auto entity = world.spawn();
  auto commandBuffer = world.commands();
  auto spawned = commandBuffer.spawn();
  commandBuffer.add(entity, A{7});
  commandBuffer.remove<B>(entity);
  commandBuffer.destroy(spawned);

  std::vector<oly::ecs::detail::CommandOperation> operations;
  oly::ecs::detail::CommandBufferTestAccess::forEach(commandBuffer, [&](const oly::ecs::detail::CommandView& command) { operations.push_back(command.operation); });

  CHECK(commandBuffer.commandCount() == 4);
  CHECK_FALSE(commandBuffer.empty());
  CHECK(operations == std::vector<oly::ecs::detail::CommandOperation>{oly::ecs::detail::CommandOperation::Spawn, oly::ecs::detail::CommandOperation::Add, oly::ecs::detail::CommandOperation::Remove, oly::ecs::detail::CommandOperation::Destroy});
}

TEST_CASE("buffer discarded without flush does not leak") {
  LifeCounter::alive = 0;
  {
    oly::ecs::World world;
    {
      auto commandBuffer = world.commands();
      auto spawned = commandBuffer.spawn();
      commandBuffer.add(spawned, LifeCounter{});
      commandBuffer.add(spawned, FuzzState{std::string(64, 'y')});
    }
    CHECK(LifeCounter::alive == 0);

    auto entity = world.spawn();
    CHECK(entity.index == 0); // reserved index went back to the freeList (generation + 1)
    CHECK(entity.generation == 1);
  }
}

TEST_CASE("flushSequential ignores handles that are neither alive nor reserved") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});
  world.destroy(entity);
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, B{0, 0});
  commandBuffer.destroy(entity);
  commandBuffer.remove<A>(entity);

  oly::ecs::detail::WorldTestAccess::flushSequential(world, commandBuffer);

  CHECK_FALSE(world.isAlive(entity));
  CHECK_INVARIANTS(world);
}

TEST_CASE("sequential fuzz == applying directly") {
  oly::ecs::World world;
  Model model;
  std::mt19937_64 generator(fuzzSeed() ^ 0xC0FFEE);
  std::vector<oly::ecs::Entity> live, dead;
  uint64_t counter = 0, total = 0;
  LifeCounter::alive = 0;

  while (total < 20000) {
    auto commandBuffer = world.commands();
    const unsigned batchSize = 1 + unsigned(generator() % 64);
    for (unsigned commandIndex = 0; commandIndex < batchSize; ++commandIndex, ++total) {
      const unsigned roll = unsigned(generator() % 100);
      if (roll < 15 || live.empty()) {
        oly::ecs::Entity entity = commandBuffer.spawn();
        model[entity];
        live.push_back(entity);
      } else if (roll < 25) {
        size_t i = generator() % live.size();
        oly::ecs::Entity entity = live[i];
        commandBuffer.destroy(entity);
        model.erase(entity);
        live[i] = live.back();
        live.pop_back();
        dead.push_back(entity);
      } else if (roll < 70) {
        oly::ecs::Entity entity = live[generator() % live.size()];
        oly::ecs::ComponentId componentId = fuzz::universe()[generator() % 5];
        uint64_t value = fuzz::isTag(componentId) ? 0 : makeValue(entity, ++counter);
        addToCommandBuffer(commandBuffer, entity, componentId, value);
        model[entity][componentId] = value;
      } else if (roll < 95) {
        oly::ecs::Entity entity = live[generator() % live.size()];
        oly::ecs::ComponentId componentId = fuzz::universe()[generator() % 5];
        commandBuffer.removeById(entity, componentId);
        model[entity].erase(componentId);
      } else if (!dead.empty()) {
        oly::ecs::Entity old = dead[generator() % dead.size()]; // stale: ignored on flush
        addToCommandBuffer(commandBuffer, old, oly::ecs::componentId<A>(), 1);
        commandBuffer.removeById(old, oly::ecs::componentId<B>());
      }
    }

    oly::ecs::detail::WorldTestAccess::flushSequential(world, commandBuffer);
    CHECK(commandBuffer.empty());
    requireEqual(world, model);
    CHECK_INVARIANTS(world);
  }

  int64_t lifeCounters = 0;
  for (auto& [entity, components] : model) {
    lifeCounters += components.count(oly::ecs::componentId<LifeCounter>());
  }
  CHECK(LifeCounter::alive == lifeCounters);
}

TEST_CASE("R-CB-4: add followed by remove leaves nothing") {
  oly::ecs::World world;
  auto entity = world.spawn(B{1, 2});
  auto before = oly::ecs::archetypeCount(world);
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, A{1});
  commandBuffer.remove<A>(entity);

  world.flush(commandBuffer);

  CHECK_FALSE(world.has<A>(entity));
  CHECK(world.get<B>(entity).first == 1);
  CHECK(oly::ecs::archetypeCount(world) == before); // stayed in the original archetype
  CHECK_INVARIANTS(world);
}

TEST_CASE("R-CB-4: remove followed by add assigns the new value") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});
  auto commandBuffer = world.commands();
  commandBuffer.remove<A>(entity);
  commandBuffer.add(entity, A{9});

  world.flush(commandBuffer);

  CHECK(world.get<A>(entity).value == 9);
  CHECK_INVARIANTS(world);
}

TEST_CASE("R-CB-4: repeated add keeps the last value") {
  oly::ecs::World world;
  auto entity = world.spawn();
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, A{1});
  commandBuffer.add(entity, A{2});
  commandBuffer.add(entity, FuzzState{std::string(40, 'a')});
  commandBuffer.add(entity, FuzzState{std::string(40, 'b')});

  world.flush(commandBuffer);

  CHECK(world.get<A>(entity).value == 2);
  CHECK(world.get<FuzzState>(entity).text == std::string(40, 'b'));
  CHECK_INVARIANTS(world);
}

TEST_CASE("R-CB-4: add over an existing T assigns") {
  LifeCounter::alive = 0;
  {
    oly::ecs::World world;
    auto entity = world.spawn(LifeCounter{1}, A{1});
    auto commandBuffer = world.commands();
    commandBuffer.add(entity, LifeCounter{2});

    world.flush(commandBuffer);

    CHECK(world.get<LifeCounter>(entity).value == 2);
    CHECK(LifeCounter::alive == 1);
    CHECK_INVARIANTS(world);
  }
  CHECK(LifeCounter::alive == 0);
}

TEST_CASE("R-CB-4: destroy discards earlier commands; spawn+destroy releases the index") {
  LifeCounter::alive = 0;
  oly::ecs::World world;
  world.spawn();
  auto entity = world.spawn(A{1});
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, B{0, 0});
  commandBuffer.add(entity, LifeCounter{1});
  commandBuffer.destroy(entity);
  auto spawned = commandBuffer.spawn();
  commandBuffer.add(spawned, LifeCounter{2});
  commandBuffer.destroy(spawned);

  world.flush(commandBuffer);

  CHECK_FALSE(world.isAlive(entity));
  CHECK_FALSE(world.isAlive(spawned));
  CHECK(LifeCounter::alive == 0);

  auto created = world.spawn();
  CHECK(created.index == spawned.index);
  CHECK(created.generation == spawned.generation + 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("R-CB-3: a single transition per entity") {
  oly::ecs::World world;
  auto entity = world.spawn();
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, A{1});
  commandBuffer.add(entity, B{2, 3});
  commandBuffer.add(entity, Tag{});
  auto before = oly::ecs::archetypeCount(world);

  world.flush(commandBuffer);

  CHECK(oly::ecs::archetypeCount(world) == before + 1); // only {A,B,Tag}; no intermediate one
  CHECK(world.get<B>(entity).second == 3);
  CHECK(world.has<Tag>(entity));
  CHECK_INVARIANTS(world);
}

TEST_CASE("flush: entity with data is born directly in the final archetype") {
  oly::ecs::World world;
  auto commandBuffer = world.commands();
  auto spawned = commandBuffer.spawn();
  commandBuffer.add(spawned, A{1});
  commandBuffer.add(spawned, B{2, 3});
  auto before = oly::ecs::archetypeCount(world);

  world.flush(commandBuffer);

  CHECK(oly::ecs::archetypeCount(world) == before + 1);
  CHECK(world.get<A>(spawned).value == 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("commands for a directly destroyed handle are ignored") {
  oly::ecs::World world;
  auto entity = world.spawn(A{1});
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, B{0, 0});
  world.destroy(entity);
  auto reused = world.spawn(A{9}); // same index, new generation
  CHECK(reused.index == entity.index);

  world.flush(commandBuffer);

  CHECK_FALSE(world.has<B>(reused));
  CHECK(world.get<A>(reused).value == 9);
  CHECK_INVARIANTS(world);
}

TEST_CASE("spawn+destroy in the same buffer returns the index with generation + 1") {
  oly::ecs::World world;
  auto commandBuffer = world.commands();
  auto spawned = commandBuffer.spawn();
  commandBuffer.destroy(spawned);
  world.flush(commandBuffer);

  auto again = world.spawn();

  CHECK(again.index == spawned.index);
  CHECK(again.generation == spawned.generation + 1);
}

TEST_CASE("fuzz: coalesced flush == flushSequential") {
  LifeCounter::alive = 0;
  {
    oly::ecs::World coalesced, sequential;
    std::vector<std::pair<oly::ecs::Entity, oly::ecs::Entity>> live; // logical handles
    std::mt19937_64 generator(fuzzSeed() ^ 0xBADC0DE);
    uint64_t counter = 0, total = 0;

    auto compare = [&] {
      for (auto& [coalescedEntity, sequentialEntity] : live) {
        REQUIRE(coalesced.isAlive(coalescedEntity));
        REQUIRE(sequential.isAlive(sequentialEntity));
        for (int i = 0; i < 5; ++i) {
          const auto componentId = fuzz::universe()[i];
          uint64_t coalescedValue = 0, sequentialValue = 0;
          bool coalescedHas = readFromWorld(coalesced, coalescedEntity, componentId, coalescedValue), sequentialHas = readFromWorld(sequential, sequentialEntity, componentId, sequentialValue);
          if (coalescedHas != sequentialHas || coalescedValue != sequentialValue) {
            FAIL("divergence in " << oly::ecs::typeInfo(componentId).name << " e1=" << coalescedEntity.index);
          }
        }
      }
    };

    while (total < 30000) {
      auto coalescedCommands = coalesced.commands();
      auto sequentialCommands = sequential.commands();
      const unsigned batchSize = 1 + unsigned(generator() % 64);
      std::vector<std::pair<oly::ecs::Entity, oly::ecs::Entity>> destroyedNow;
      for (unsigned commandIndex = 0; commandIndex < batchSize; ++commandIndex, ++total) {
        const unsigned roll = unsigned(generator() % 100);
        if (roll < 15 || live.empty()) {
          live.emplace_back(coalescedCommands.spawn(), sequentialCommands.spawn());
        } else if (roll < 25) {
          size_t i = generator() % live.size();
          auto [coalescedEntity, sequentialEntity] = live[i];
          coalescedCommands.destroy(coalescedEntity);
          sequentialCommands.destroy(sequentialEntity);
          destroyedNow.push_back(live[i]);
          live[i] = live.back(); // no commands after destroy
          live.pop_back();
        } else if (roll < 70) {
          size_t i = generator() % live.size();
          auto [coalescedEntity, sequentialEntity] = live[i];
          oly::ecs::ComponentId componentId = fuzz::universe()[generator() % 5];
          uint64_t value = fuzz::isTag(componentId) ? 0 : makeValue(oly::ecs::Entity{uint32_t(i), 0}, ++counter);
          addToCommandBuffer(coalescedCommands, coalescedEntity, componentId, value);
          addToCommandBuffer(sequentialCommands, sequentialEntity, componentId, value);
        } else {
          size_t i = generator() % live.size();
          oly::ecs::ComponentId componentId = fuzz::universe()[generator() % 5];
          coalescedCommands.removeById(live[i].first, componentId);
          sequentialCommands.removeById(live[i].second, componentId);
        }
      }

      coalesced.flush(coalescedCommands);
      oly::ecs::detail::WorldTestAccess::flushSequential(sequential, sequentialCommands);

      compare();
      CHECK_INVARIANTS(coalesced);
      CHECK_INVARIANTS(sequential);
      for (auto& [coalescedEntity, sequentialEntity] : destroyedNow) {
        CHECK_FALSE(coalesced.isAlive(coalescedEntity));
        CHECK_FALSE(sequential.isAlive(sequentialEntity));
      }
    }
  }
  CHECK(LifeCounter::alive == 0);
}

TEST_CASE("R-CB-3: commands interleaved across entities still coalesce per entity") {
  oly::ecs::World world;
  auto first = world.spawn();
  auto second = world.spawn();
  auto commandBuffer = world.commands();
  commandBuffer.add(first, A{1});
  commandBuffer.add(second, A{2});
  commandBuffer.add(first, B{3, 4}); // first comes back outside the consecutive run
  commandBuffer.add(second, B{5, 6});
  commandBuffer.add(first, A{7});
  auto before = oly::ecs::archetypeCount(world);

  world.flush(commandBuffer);

  CHECK(oly::ecs::archetypeCount(world) == before + 1); // only {A,B}
  CHECK(world.get<A>(first).value == 7);
  CHECK(world.get<B>(first).second == 4);
  CHECK(world.get<A>(second).value == 2);
  CHECK(world.get<B>(second).second == 6);
  CHECK_INVARIANTS(world);
}

TEST_CASE("buffer reused after flush groups again") {
  oly::ecs::World world;
  auto first = world.spawn();
  auto second = world.spawn();
  auto commandBuffer = world.commands();
  commandBuffer.add(first, A{1});
  commandBuffer.add(second, A{2});
  commandBuffer.add(first, B{0, 0}); // not consecutive: the first batch takes the general path
  world.flush(commandBuffer);

  commandBuffer.add(second, B{8, 9}); // second batch: the grouping state was reset by the flush
  commandBuffer.add(first, A{5});
  world.flush(commandBuffer);

  CHECK(world.get<A>(first).value == 5);
  CHECK(world.get<B>(second).second == 9);
  CHECK(world.get<A>(second).value == 2);
  CHECK_INVARIANTS(world);
}

TEST_CASE("addById records a trivial component by ID") {
  oly::ecs::World world;
  auto entity = world.spawn();
  auto commandBuffer = world.commands();

  A value{7};
  commandBuffer.addById(entity, oly::ecs::componentId<A>(), &value);
  world.flush(commandBuffer);

  CHECK(world.get<A>(entity).value == 7);
  CHECK_INVARIANTS(world);
}

TEST_CASE("addById of a tag ignores the value") {
  oly::ecs::World world;
  auto entity = world.spawn();
  auto commandBuffer = world.commands();

  commandBuffer.addById(entity, oly::ecs::componentId<Tag>(), nullptr);
  world.flush(commandBuffer);

  CHECK(world.has<Tag>(entity));
  CHECK_INVARIANTS(world);
}

TEST_CASE("addById moves a non-trivial type") {
  const int64_t aliveBefore = LifeCounter::alive;
  {
    oly::ecs::World world;
    auto entity = world.spawn();
    auto commandBuffer = world.commands();
    {
      LifeCounter source{9};
      commandBuffer.addById(entity, oly::ecs::componentId<LifeCounter>(), &source);
    }
    world.flush(commandBuffer);

    CHECK(world.get<LifeCounter>(entity).value == 9);
    world.destroy(entity);
    CHECK_INVARIANTS(world);
  }

  CHECK(LifeCounter::alive == aliveBefore);
}

TEST_CASE("addById coalesces the same as add<T> on flush") {
  oly::ecs::World byIdWorld, typedWorld;
  auto byIdEntity = byIdWorld.spawn();
  auto typedEntity = typedWorld.spawn();

  auto byIdBuffer = byIdWorld.commands();
  A aValue{1};
  B bValue{2, 3};
  byIdBuffer.addById(byIdEntity, oly::ecs::componentId<A>(), &aValue);
  byIdBuffer.removeById(byIdEntity, oly::ecs::componentId<A>());
  byIdBuffer.addById(byIdEntity, oly::ecs::componentId<B>(), &bValue);
  byIdWorld.flush(byIdBuffer);

  auto typedBuffer = typedWorld.commands();
  typedBuffer.add(typedEntity, A{1});
  typedBuffer.remove<A>(typedEntity);
  typedBuffer.add(typedEntity, B{2, 3});
  typedWorld.flush(typedBuffer);

  CHECK_FALSE(byIdWorld.has<A>(byIdEntity));
  CHECK_FALSE(typedWorld.has<A>(typedEntity));
  CHECK(byIdWorld.get<B>(byIdEntity).second == typedWorld.get<B>(typedEntity).second);
  CHECK_INVARIANTS(byIdWorld);
  CHECK_INVARIANTS(typedWorld);
}
