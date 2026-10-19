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
  auto mk(uint64_t v) {
    if constexpr (NT)
      return NonTrivial<Comp<I>>{Comp<I>{v}};
    else
      return Comp<I>{v};
  }

  template <bool NT, size_t... Is>
  ecs::Entity createK(ecs::World& w, uint64_t v, std::index_sequence<Is...>) {
    return w.create(mk<NT, int(Is)>(v)...);
  }

  constexpr int kExtra = 31; // componente que entra e sai

  template <bool NT, int K>
  void BM_Transition(benchmark::State& state) {
    constexpr int E = 1024;
    ecs::World w;
    std::vector<ecs::Entity> es;
    for (int i = 0; i < E; ++i)
      es.push_back(createK<NT>(w, i, std::make_index_sequence<K>{}));
    using X = decltype(mk<NT, kExtra>(0));
    for (auto e : es) { // aquece arestas e capacidades
      w.add(e, mk<NT, kExtra>(1));
      w.remove<X>(e);
    }
    for (auto _ : state) {
      for (auto e : es) {
        w.add(e, mk<NT, kExtra>(2));
        w.remove<X>(e);
      }
    }
    state.counters["ns_per_op"] = benchmark::Counter(2.0 * E, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
    state.counters["archetypes"] = w.archetypeCount();
    state.counters["entities_per_archetype"] = double(E) / w.archetypeCount();
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
      auto w = std::make_unique<ecs::World>();
      auto e = w->create(Comp<0>{1}, Comp<1>{2}, Comp<2>{3}, Comp<3>{4});
      state.ResumeTiming();
      w->add(e, Comp<kExtra>{5});
      state.PauseTiming();
      w.reset();
      state.ResumeTiming();
    }
  }
  BENCHMARK(BM_TransitionCold);

  struct Rec4 {
    uint64_t a, b, c, d;
  };

  std::vector<uint32_t> permutation(uint32_t n) {
    std::vector<uint32_t> p(n);
    std::iota(p.begin(), p.end(), 0u);
    std::mt19937 rng(1234);
    std::shuffle(p.begin(), p.end(), rng);
    return p;
  }

  constexpr uint32_t kGetN = 1'000'000;

  // NF-P6: sem meta.
  void BM_RandomGet(benchmark::State& state) {
    ecs::World w;
    std::vector<ecs::Entity> es;
    es.reserve(kGetN);
    for (uint32_t i = 0; i < kGetN; ++i)
      es.push_back(w.create(Comp<0>{i}, Comp<1>{i}, Comp<2>{i}, Comp<3>{i}));
    const auto perm = permutation(kGetN);
    for (auto _ : state) {
      uint64_t sum = 0;
      for (uint32_t i : perm)
        sum += w.get<Comp<2>>(es[i]).v;
      benchmark::DoNotOptimize(sum);
    }
    state.counters["per_get"] = benchmark::Counter(kGetN, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_RandomGet);

  void BM_RandomGet_VectorStruct(benchmark::State& state) {
    std::vector<Rec4> v(kGetN);
    for (uint32_t i = 0; i < kGetN; ++i)
      v[i] = Rec4{i, i, i, i};
    const auto perm = permutation(kGetN);
    for (auto _ : state) {
      uint64_t sum = 0;
      for (uint32_t i : perm)
        sum += v[i].c;
      benchmark::DoNotOptimize(sum);
    }
    state.counters["per_get"] = benchmark::Counter(kGetN, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_RandomGet_VectorStruct);

  // Churn: 10% das entidades destruídas e 10% novas por iteração.
  void BM_CreateDestroyChurn(benchmark::State& state) {
    constexpr uint32_t N = 100'000, K = N / 10;
    ecs::World w;
    std::vector<ecs::Entity> es;
    es.reserve(N);
    for (uint32_t i = 0; i < N; ++i)
      es.push_back(w.create(Comp<0>{i}, Comp<1>{i}, Comp<2>{i}, Comp<3>{i}));
    std::mt19937 rng(99);
    for (auto _ : state) {
      for (uint32_t k = 0; k < K; ++k) {
        const uint32_t i = rng() % N;
        w.destroy(es[i]);
        es[i] = w.create(Comp<0>{k}, Comp<1>{k}, Comp<2>{k}, Comp<3>{k});
      }
    }
    state.counters["ns_per_op"] = benchmark::Counter(2.0 * K, benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_CreateDestroyChurn);

} // namespace

BENCHMARK_MAIN();
