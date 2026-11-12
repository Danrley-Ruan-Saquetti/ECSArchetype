#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "oly/ecs/world.hpp"
#include "model.hpp"

TEST_CASE("fuzz: World == reference model") {
  const uint64_t seed = fuzzSeed();
  const uint64_t operationCount = fuzzOperationCount();
  MESSAGE("seed=" << seed << " ops=" << operationCount);
  std::fprintf(stderr, "[fuzz_vs_model] seed=%llu ops=%llu\n", (unsigned long long)seed, (unsigned long long)operationCount);

  LifeCounter::alive = 0;
  {
    oly::ecs::World world;
    std::mt19937_64 generator(seed);
    Model model;
    std::vector<oly::ecs::Entity> live, dead;
    uint64_t counter = 0;

    auto pickIndex = [&] { return size_t(generator() % live.size()); };
    auto pickComponentId = [&] { return fuzz::universe()[generator() % 5]; };

    auto spawnRandomEntity = [&] {
      ++counter;
      const uint64_t value = makeValue(oly::ecs::Entity{uint32_t(counter), 0}, counter);
      oly::ecs::Entity entity;
      std::map<oly::ecs::ComponentId, uint64_t> components;
      switch (generator() % 6) {
      case 0:
        entity = world.spawn();
        break;
      case 1:
        entity = world.spawn(A{value});
        components[oly::ecs::componentId<A>()] = value;
        break;
      case 2:
        entity = world.spawn(A{value}, B{value, ~value});
        components[oly::ecs::componentId<A>()] = value;
        components[oly::ecs::componentId<B>()] = value;
        break;
      case 3:
        entity = world.spawn(fuzz::makeFuzzState(value));
        components[oly::ecs::componentId<FuzzState>()] = value;
        break;
      case 4:
        entity = world.spawn(A{value}, Tag{});
        components[oly::ecs::componentId<A>()] = value;
        components[oly::ecs::componentId<Tag>()] = 0;
        break;
      default:
        entity = world.spawn(LifeCounter{value}, fuzz::makeFuzzState(value), B{value, ~value});
        components[oly::ecs::componentId<LifeCounter>()] = value;
        components[oly::ecs::componentId<FuzzState>()] = value;
        components[oly::ecs::componentId<B>()] = value;
        break;
      }

      model[entity] = std::move(components);
      live.push_back(entity);
    };

    for (uint64_t operation = 1; operation <= operationCount; ++operation) {
      const unsigned destroyWeight = live.size() > 4096 ? 40 : 10;
      unsigned roll = unsigned(generator() % (20 + destroyWeight + 30 + 20 + 20));
      if (roll < 20) {
        spawnRandomEntity();
      } else if (roll < 20 + destroyWeight) {
        if (!live.empty()) {
          size_t i = pickIndex();
          oly::ecs::Entity entity = live[i];
          world.destroy(entity);
          model.erase(entity);
          live[i] = live.back();
          live.pop_back();
          if (dead.size() < 1024) {
            dead.push_back(entity);
          } else {
            dead[generator() % dead.size()] = entity;
          }
        }
      } else if (roll < 20 + destroyWeight + 30) {
        if (!live.empty()) {
          oly::ecs::Entity entity = live[pickIndex()];
          oly::ecs::ComponentId componentId = pickComponentId();
          uint64_t value = fuzz::isTag(componentId) ? 0 : makeValue(entity, ++counter);
          addToWorld(world, entity, componentId, value);
          model[entity][componentId] = value;
        }
      } else if (roll < 20 + destroyWeight + 30 + 20) {
        if (!live.empty()) {
          oly::ecs::Entity entity = live[pickIndex()];
          oly::ecs::ComponentId componentId = pickComponentId();
          world.removeById(entity, componentId);
          model[entity].erase(componentId);
        }
      } else {
        if (!live.empty()) {
          oly::ecs::Entity entity = live[pickIndex()];
          oly::ecs::ComponentId componentId = pickComponentId();
          auto& components = model[entity];
          auto it = components.find(componentId);
          uint64_t got = 0;
          bool has = readFromWorld(world, entity, componentId, got);
          if (has != (it != components.end()) || (has && got != it->second)) {
            FAIL("get diverges at op " << operation << " e=" << entity.index << ":" << entity.generation);
          }
        }
      }

      if (!dead.empty() && generator() % 100 == 0) {
        oly::ecs::Entity old = dead[generator() % dead.size()];
        if (world.isAlive(old) || world.tryGet<A>(old) != nullptr) {
          FAIL("stale handle alive: e=" << old.index);
        }
      }

      if (operation % 1000 == 0) {
        requireEqual(world, model);
      }
      if (operation % 100000 == 0) {
        CHECK_INVARIANTS(world);
      }
    }

    requireEqual(world, model);
    CHECK_INVARIANTS(world);

    int64_t lifeCounters = 0;
    for (auto& [entity, components] : model) {
      lifeCounters += components.count(oly::ecs::componentId<LifeCounter>());
    }
    CHECK(LifeCounter::alive == lifeCounters);
  }
  CHECK(LifeCounter::alive == 0);
}
