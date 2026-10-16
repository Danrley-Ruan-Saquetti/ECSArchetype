#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <ecs/world.hpp>
#include <random>
#include <vector>

#include "model.hpp"

TEST_CASE("fuzz: World == modelo de referência") {
  const uint64_t seed = fuzzSeed();
  const uint64_t ops = fuzzOps();
  MESSAGE("seed=" << seed << " ops=" << ops);
  std::fprintf(stderr, "[fuzz_vs_model] seed=%llu ops=%llu\n", (unsigned long long)seed, (unsigned long long)ops);

  LifeCounter::alive = 0;
  {
    ecs::World w;
    std::mt19937_64 rng(seed);
    Model model;
    std::vector<ecs::Entity> live, dead;
    uint64_t counter = 0;

    auto pickIndex = [&] { return size_t(rng() % live.size()); };
    auto pickCid = [&] { return fuzz::universe()[rng() % 5]; };

    auto createOne = [&] {
      const uint64_t v = makeValue(ecs::Entity{uint32_t(counter), 0}, ++counter);
      ecs::Entity e;
      std::map<ecs::ComponentId, uint64_t> comps;
      switch (rng() % 6) {
      case 0:
        e = w.create();
        break;
      case 1:
        e = w.create(A{v});
        comps[ecs::componentId<A>()] = v;
        break;
      case 2:
        e = w.create(A{v}, B{v, ~v});
        comps[ecs::componentId<A>()] = v;
        comps[ecs::componentId<B>()] = v;
        break;
      case 3:
        e = w.create(fuzz::makeFS(v));
        comps[ecs::componentId<FS>()] = v;
        break;
      case 4:
        e = w.create(A{v}, Tag{});
        comps[ecs::componentId<A>()] = v;
        comps[ecs::componentId<Tag>()] = 0;
        break;
      default:
        e = w.create(LifeCounter{v}, fuzz::makeFS(v), B{v, ~v});
        comps[ecs::componentId<LifeCounter>()] = v;
        comps[ecs::componentId<FS>()] = v;
        comps[ecs::componentId<B>()] = v;
        break;
      }
      model[e] = std::move(comps);
      live.push_back(e);
    };

    for (uint64_t op = 1; op <= ops; ++op) {
      const unsigned destroyWeight = live.size() > 4096 ? 40 : 10;
      unsigned r = unsigned(rng() % (20 + destroyWeight + 30 + 20 + 20));
      if (r < 20) {
        createOne();
      } else if (r < 20 + destroyWeight) {
        if (!live.empty()) {
          size_t i = pickIndex();
          ecs::Entity e = live[i];
          w.destroy(e);
          model.erase(e);
          live[i] = live.back();
          live.pop_back();
          if (dead.size() < 1024)
            dead.push_back(e);
          else
            dead[rng() % dead.size()] = e;
        }
      } else if (r < 20 + destroyWeight + 30) {
        if (!live.empty()) {
          ecs::Entity e = live[pickIndex()];
          ecs::ComponentId cid = pickCid();
          uint64_t v = fuzz::isTag(cid) ? 0 : makeValue(e, ++counter);
          addToWorld(w, e, cid, v);
          model[e][cid] = v;
        }
      } else if (r < 20 + destroyWeight + 30 + 20) {
        if (!live.empty()) {
          ecs::Entity e = live[pickIndex()];
          ecs::ComponentId cid = pickCid();
          w.removeRaw(e, cid);
          model[e].erase(cid);
        }
      } else {
        if (!live.empty()) {
          ecs::Entity e = live[pickIndex()];
          ecs::ComponentId cid = pickCid();
          auto& comps = model[e];
          auto it = comps.find(cid);
          uint64_t got = 0;
          bool has = readFromWorld(w, e, cid, got);
          if (has != (it != comps.end()) || (has && got != it->second))
            FAIL("get diverge no op " << op << " e=" << e.index << ":" << e.generation);
        }
      }
      // Review Focus 4: handles obsoletos
      if (!dead.empty() && rng() % 100 == 0) {
        ecs::Entity old = dead[rng() % dead.size()];
        if (w.isAlive(old) || w.tryGet<A>(old) != nullptr)
          FAIL("handle obsoleto vivo: e=" << old.index);
      }
      if (op % 1000 == 0)
        requireEqual(w, model);
      if (op % 100000 == 0)
        CHECK_INV(w);
    }

    requireEqual(w, model);
    CHECK_INV(w);
    int64_t lc = 0;
    for (auto& [e, comps] : model)
      lc += comps.count(ecs::componentId<LifeCounter>());
    CHECK(LifeCounter::alive == lc);
  }
  CHECK(LifeCounter::alive == 0);
}
