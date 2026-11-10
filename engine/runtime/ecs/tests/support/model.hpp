#pragma once

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>

#include "components.hpp"
#include "oly/ecs/world.hpp"

struct FuzzState {
  std::string text; // não trivial; valor sempre > 32 chars para sair do SSO
};

// tag guarda 0
using Model = std::unordered_map<oly::ecs::Entity, std::map<oly::ecs::ComponentId, uint64_t>, oly::ecs::EntityHash>;

namespace fuzz {
  inline const std::string kFuzzStatePrefix = "fuzz-state-padding-0123456789abcdef-";

  inline FuzzState makeFuzzState(uint64_t value) {
    return FuzzState{kFuzzStatePrefix + std::to_string(value)};
  }

  inline const oly::ecs::ComponentId* universe() {
    static const oly::ecs::ComponentId ids[5] = {oly::ecs::componentId<A>(), oly::ecs::componentId<B>(), oly::ecs::componentId<FuzzState>(),
                                            oly::ecs::componentId<Tag>(), oly::ecs::componentId<LifeCounter>()};
    return ids;
  }

  inline bool isTag(oly::ecs::ComponentId componentId) {
    return componentId == oly::ecs::componentId<Tag>();
  }
}

// Mistura index, generation e contador (splitmix64): troca de linhas aparece como valor errado.
inline uint64_t makeValue(oly::ecs::Entity entity, uint64_t counter) {
  uint64_t hash = (uint64_t(entity.index) << 32 | entity.generation) ^ (counter * 0x9e3779b97f4a7c15ull);
  hash += 0x9e3779b97f4a7c15ull;
  hash = (hash ^ (hash >> 30)) * 0xbf58476d1ce4e5b9ull;
  hash = (hash ^ (hash >> 27)) * 0x94d049bb133111ebull;
  return hash ^ (hash >> 31);
}

inline void addToWorld(oly::ecs::World& world, oly::ecs::Entity entity, oly::ecs::ComponentId componentId, uint64_t value) {
  if (componentId == oly::ecs::componentId<A>()) {
    world.add(entity, A{value});
  } else if (componentId == oly::ecs::componentId<B>()) {
    world.add(entity, B{value, ~value});
  } else if (componentId == oly::ecs::componentId<FuzzState>()) {
    world.add(entity, fuzz::makeFuzzState(value));
  } else if (componentId == oly::ecs::componentId<Tag>()) {
    world.add(entity, Tag{});
  } else {
    world.add(entity, LifeCounter{value});
  }
}

// Devolve false se a entidade não tem o componente. Tag devolve 0.
inline bool readFromWorld(oly::ecs::World& world, oly::ecs::Entity entity, oly::ecs::ComponentId componentId, uint64_t& out) {
  if (!world.hasById(entity, componentId)) {
    return false;
  }

  if (componentId == oly::ecs::componentId<A>()) {
    out = world.get<A>(entity).value;
  } else if (componentId == oly::ecs::componentId<B>()) {
    auto& componentB = world.get<B>(entity);
    out = (componentB.second == ~componentB.first) ? componentB.first : ~uint64_t(0); // second corrompido vira valor impossível de casar
  } else if (componentId == oly::ecs::componentId<FuzzState>()) {
    const std::string& text = world.get<FuzzState>(entity).text;
    out = text.compare(0, fuzz::kFuzzStatePrefix.size(), fuzz::kFuzzStatePrefix) == 0 ? std::strtoull(text.c_str() + fuzz::kFuzzStatePrefix.size(), nullptr, 10) : ~uint64_t(0);
  } else if (componentId == oly::ecs::componentId<Tag>()) {
    out = 0;
  } else {
    out = world.get<LifeCounter>(entity).value;
  }

  return true;
}

inline void requireEqual(oly::ecs::World& world, const Model& model) {
  for (const auto& [entity, components] : model) {
    if (!world.isAlive(entity)) {
      FAIL("entidade do modelo morta no World: e=" << entity.index << ":" << entity.generation);
    }

    for (int i = 0; i < 5; ++i) {
      const oly::ecs::ComponentId componentId = fuzz::universe()[i];
      auto it = components.find(componentId);
      uint64_t got = 0;
      const bool has = readFromWorld(world, entity, componentId, got);
      if (has != (it != components.end())) {
        FAIL("presença diverge: e=" << entity.index << ":" << entity.generation << " tipo=" << oly::ecs::typeInfo(componentId).name);
      }
      if (has && got != it->second) {
        FAIL("valor diverge: e=" << entity.index << ":" << entity.generation << " tipo=" << oly::ecs::typeInfo(componentId).name
                                 << " esperado=" << it->second << " obtido=" << got);
      }
    }
  }
}

inline uint64_t fuzzSeed() {
  const char* environmentValue = std::getenv("OLY_FUZZ_SEED");
  return environmentValue ? std::strtoull(environmentValue, nullptr, 0) : 0xEC5ull;
}

inline uint64_t fuzzOperationCount() {
  const char* environmentValue = std::getenv("OLY_FUZZ_OPS");
  return environmentValue ? std::strtoull(environmentValue, nullptr, 0) : 1'000'000ull;
}
