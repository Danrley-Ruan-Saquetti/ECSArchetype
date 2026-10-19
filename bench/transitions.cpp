#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <numeric>
#include <random>
#include <utility>
#include <vector>

#include "common.hpp"
#include "ecs/world.hpp"

namespace {
  template <bool NT, int I>
  auto makeComponent(uint64_t value) {
    if constexpr (NT) {
      return NonTrivial<Component<I>>{Component<I>{value}};
    } else {
      return Component<I>{value};
    }
  }

  template <bool NT, size_t... Is>
  ecs::Entity createWithComponents(ecs::World& world, uint64_t value, std::index_sequence<Is...>) {
    return world.create(makeComponent<NT, int(Is)>(value)...);
  }

  constexpr int kExtra = 31; // componente que entra e sai

  template <bool NT, int K>
  void BM_Transition(benchmark::State& state) {
    constexpr int kEntityCount = 1024;
    ecs::World world;
    std::vector<ecs::Entity> entities;
    for (int i = 0; i < kEntityCount; ++i) {
      entities.push_back(createWithComponents<NT>(world, i, std::make_index_sequence<K>{}));
    }

    using X = decltype(makeComponent<NT, kExtra>(0));
    for (auto entity : entities) { // aquece arestas e capacidades
      world.add(entity, makeComponent<NT, kExtra>(1));
      world.remove<X>(entity);
    }

    for (auto _ : state) {
      for (auto entity : entities) {
        world.add(entity, makeComponent<NT, kExtra>(2));
        world.remove<X>(entity);
      }
    }

    state.counters["ns_per_op"] = benchmark::Counter(2.0 * kEntityCount, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
    state.counters["archetypes"] = world.archetypeCount();
    state.counters["entities_per_archetype"] = double(kEntityCount) / world.archetypeCount();
  }
  BENCHMARK_TEMPLATE2(BM_Transition, false, 2)->Name("BM_Transition_Trivial/2");
  BENCHMARK_TEMPLATE2(BM_Transition, false, 4)->Name("BM_Transition_Trivial/4");
  BENCHMARK_TEMPLATE2(BM_Transition, false, 8)->Name("BM_Transition_Trivial/8");
  BENCHMARK_TEMPLATE2(BM_Transition, false, 16)->Name("BM_Transition_Trivial/16");
  BENCHMARK_TEMPLATE2(BM_Transition, true, 2)->Name("BM_Transition_NonTrivial/2");
  BENCHMARK_TEMPLATE2(BM_Transition, true, 4)->Name("BM_Transition_NonTrivial/4");
  BENCHMARK_TEMPLATE2(BM_Transition, true, 8)->Name("BM_Transition_NonTrivial/8");
  BENCHMARK_TEMPLATE2(BM_Transition, true, 16)->Name("BM_Transition_NonTrivial/16");

  // Primeira transição para um arquétipo inédito (inclui criar arquétipo e aresta).
  void BM_TransitionCold(benchmark::State& state) {
    for (auto _ : state) {
      state.PauseTiming();
      auto world = std::make_unique<ecs::World>();
      auto entity = world->create(Component<0>{1}, Component<1>{2}, Component<2>{3}, Component<3>{4});
      state.ResumeTiming();

      world->add(entity, Component<kExtra>{5});

      state.PauseTiming();
      world.reset();
      state.ResumeTiming();
    }
  }
  BENCHMARK(BM_TransitionCold);

  struct Record4 {
    uint64_t first, second, third, fourth;
  };

  std::vector<uint32_t> permutation(uint32_t count) {
    std::vector<uint32_t> indices(count);
    std::iota(indices.begin(), indices.end(), 0u);
    std::mt19937 generator(1234);
    std::shuffle(indices.begin(), indices.end(), generator);
    return indices;
  }

  constexpr uint32_t kGetCount = 1'000'000;

  // NF-P6: sem meta.
  void BM_RandomGet(benchmark::State& state) {
    ecs::World world;
    std::vector<ecs::Entity> entities;
    entities.reserve(kGetCount);
    for (uint32_t i = 0; i < kGetCount; ++i) {
      entities.push_back(world.create(Component<0>{i}, Component<1>{i}, Component<2>{i}, Component<3>{i}));
    }

    const auto shuffledIndices = permutation(kGetCount);

    for (auto _ : state) {
      uint64_t sum = 0;
      for (uint32_t i : shuffledIndices) {
        sum += world.get<Component<2>>(entities[i]).value;
      }
      benchmark::DoNotOptimize(sum);
    }

    state.counters["per_get"] = benchmark::Counter(kGetCount, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_RandomGet);

  void BM_RandomGet_VectorStruct(benchmark::State& state) {
    std::vector<Record4> records(kGetCount);
    for (uint32_t i = 0; i < kGetCount; ++i) {
      records[i] = Record4{i, i, i, i};
    }

    const auto shuffledIndices = permutation(kGetCount);

    for (auto _ : state) {
      uint64_t sum = 0;
      for (uint32_t i : shuffledIndices) {
        sum += records[i].third;
      }
      benchmark::DoNotOptimize(sum);
    }

    state.counters["per_get"] = benchmark::Counter(kGetCount, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_RandomGet_VectorStruct);

  // Churn: 10% das entidades destruídas e 10% novas por iteração.
  void BM_CreateDestroyChurn(benchmark::State& state) {
    constexpr uint32_t kEntityCount = 100'000, kChurnPerIteration = kEntityCount / 10;
    ecs::World world;
    std::vector<ecs::Entity> entities;
    entities.reserve(kEntityCount);
    for (uint32_t i = 0; i < kEntityCount; ++i) {
      entities.push_back(world.create(Component<0>{i}, Component<1>{i}, Component<2>{i}, Component<3>{i}));
    }

    std::mt19937 generator(99);
    for (auto _ : state) {
      for (uint32_t churn = 0; churn < kChurnPerIteration; ++churn) {
        const uint32_t i = generator() % kEntityCount;
        world.destroy(entities[i]);
        entities[i] = world.create(Component<0>{churn}, Component<1>{churn}, Component<2>{churn}, Component<3>{churn});
      }
    }

    state.counters["ns_per_op"] = benchmark::Counter(2.0 * kChurnPerIteration, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_CreateDestroyChurn);
}

BENCHMARK_MAIN();
