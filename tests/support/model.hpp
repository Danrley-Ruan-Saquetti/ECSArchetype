#pragma once

// Modelo de referência trivialmente correto e utilitários para fuzz (reutilizado em Tasks 15-16).
#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <unordered_map>

#include "components.hpp"
#include "ecs/world.hpp"

struct FS {
  std::string s; // não trivial; valor sempre > 32 chars para sair do SSO
};

// tag guarda 0
using Model = std::unordered_map<ecs::Entity, std::map<ecs::ComponentId, uint64_t>, ecs::EntityHash>;

namespace fuzz {

  inline const std::string kFsPrefix = "fuzz-state-padding-0123456789abcdef-";

  inline FS makeFS(uint64_t v) {
    return FS{kFsPrefix + std::to_string(v)};
  }

  // Universo do fuzz: A, B, FS, Tag, LifeCounter.
  inline const ecs::ComponentId* universe() {
    static const ecs::ComponentId ids[5] = {ecs::componentId<A>(), ecs::componentId<B>(), ecs::componentId<FS>(),
                                            ecs::componentId<Tag>(), ecs::componentId<LifeCounter>()};
    return ids;
  }
  inline bool isTag(ecs::ComponentId cid) {
    return cid == ecs::componentId<Tag>();
  }

} // namespace fuzz

// Mistura index, generation e contador (splitmix64): troca de linhas aparece como valor errado.
inline uint64_t makeValue(ecs::Entity e, uint64_t counter) {
  uint64_t x = (uint64_t(e.index) << 32 | e.generation) ^ (counter * 0x9e3779b97f4a7c15ull);
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}

inline void addToWorld(ecs::World& w, ecs::Entity e, ecs::ComponentId cid, uint64_t v) {
  if (cid == ecs::componentId<A>())
    w.add(e, A{v});
  else if (cid == ecs::componentId<B>())
    w.add(e, B{v, ~v});
  else if (cid == ecs::componentId<FS>())
    w.add(e, fuzz::makeFS(v));
  else if (cid == ecs::componentId<Tag>())
    w.add(e, Tag{});
  else
    w.add(e, LifeCounter{v});
}

// Devolve false se a entidade não tem o componente. Tag devolve 0.
inline bool readFromWorld(ecs::World& w, ecs::Entity e, ecs::ComponentId cid, uint64_t& out) {
  if (!w.hasRaw(e, cid))
    return false;
  if (cid == ecs::componentId<A>()) {
    out = w.get<A>(e).v;
  } else if (cid == ecs::componentId<B>()) {
    auto& b = w.get<B>(e);
    out = (b.w == ~b.v) ? b.v : ~uint64_t(0); // w corrompido vira valor impossível de casar
  } else if (cid == ecs::componentId<FS>()) {
    const std::string& s = w.get<FS>(e).s;
    out = s.compare(0, fuzz::kFsPrefix.size(), fuzz::kFsPrefix) == 0 ? std::strtoull(s.c_str() + fuzz::kFsPrefix.size(), nullptr, 10) : ~uint64_t(0);
  } else if (cid == ecs::componentId<Tag>()) {
    out = 0;
  } else {
    out = w.get<LifeCounter>(e).v;
  }
  return true;
}

inline void requireEqual(ecs::World& w, const Model& model) {
  for (const auto& [e, comps] : model) {
    if (!w.isAlive(e))
      FAIL("entidade do modelo morta no World: e=" << e.index << ":" << e.generation);
    for (int i = 0; i < 5; ++i) {
      const ecs::ComponentId cid = fuzz::universe()[i];
      auto it = comps.find(cid);
      uint64_t got = 0;
      const bool has = readFromWorld(w, e, cid, got);
      if (has != (it != comps.end()))
        FAIL("presença diverge: e=" << e.index << ":" << e.generation << " tipo=" << ecs::typeInfo(cid).name);
      if (has && got != it->second)
        FAIL("valor diverge: e=" << e.index << ":" << e.generation << " tipo=" << ecs::typeInfo(cid).name
                                 << " esperado=" << it->second << " obtido=" << got);
    }
  }
}

inline uint64_t fuzzSeed() {
  const char* s = std::getenv("ECS_FUZZ_SEED");
  return s ? std::strtoull(s, nullptr, 0) : 0xEC5ull;
}
inline uint64_t fuzzOps() {
  const char* s = std::getenv("ECS_FUZZ_OPS");
  return s ? std::strtoull(s, nullptr, 0) : 1'000'000ull;
}
