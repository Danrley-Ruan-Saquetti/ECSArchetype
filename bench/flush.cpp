#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "common.hpp"
#include "ecs/world.hpp"

namespace {

  using ecs::World;

  // N entidades no arquétipo raiz (fora do tempo). Os arquétipos e as arestas são aquecidos com
  // uma entidade extra, que é destruída em seguida.
  struct Setup {
    std::unique_ptr<World> w = std::make_unique<World>();
    std::vector<ecs::Entity> es;
    explicit Setup(int64_t n, bool coalesced) {
      auto warm = w->create();
      if (coalesced) {
        auto cb = w->commands();
        cb.add(warm, Comp<0>{0});
        cb.add(warm, Comp<1>{0});
        cb.add(warm, Comp<2>{0});
        w->flush(cb);
      } else {
        w->add(warm, Comp<0>{0});
        w->add(warm, Comp<1>{0});
        w->add(warm, Comp<2>{0});
      }
      w->destroy(warm);
      es.reserve(static_cast<size_t>(n));
      for (int64_t i = 0; i < n; ++i)
        es.push_back(w->create());
    }
  };

  void record(ecs::CommandBuffer& cb, const std::vector<ecs::Entity>& es) {
    for (ecs::Entity e : es) {
      cb.add(e, Comp<0>{1});
      cb.add(e, Comp<1>{2});
      cb.add(e, Comp<2>{3});
    }
  }

  // NF-P5 mede o flush: a gravação (3N comandos) fica fora do tempo; BM_RecordAndFlush a inclui.
  void BM_FlushCoalesced(benchmark::State& state) {
    const int64_t n = state.range(0);
    for (auto _ : state) {
      state.PauseTiming();
      Setup s(n, true);
      std::optional<ecs::CommandBuffer> cb;
      cb.emplace(s.w->commands());
      record(*cb, s.es);
      state.ResumeTiming();
      s.w->flush(*cb);
      state.PauseTiming();
      cb.reset();
      benchmark::DoNotOptimize(s.w.get());
      state.ResumeTiming();
    }
    state.counters["ns_per_entity"] = benchmark::Counter(static_cast<double>(n),
                                                         benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_FlushCoalesced)->Arg(1000)->Arg(100000);

  void BM_RecordAndFlush(benchmark::State& state) {
    const int64_t n = state.range(0);
    for (auto _ : state) {
      state.PauseTiming();
      Setup s(n, true);
      std::optional<ecs::CommandBuffer> cb;
      state.ResumeTiming();
      cb.emplace(s.w->commands());
      record(*cb, s.es);
      s.w->flush(*cb);
      state.PauseTiming();
      cb.reset();
      benchmark::DoNotOptimize(s.w.get());
      state.ResumeTiming();
    }
    state.counters["ns_per_entity"] = benchmark::Counter(static_cast<double>(n),
                                                         benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_RecordAndFlush)->Arg(1000)->Arg(100000);

  void BM_DirectAdds(benchmark::State& state) {
    const int64_t n = state.range(0);
    for (auto _ : state) {
      state.PauseTiming();
      Setup s(n, false);
      state.ResumeTiming();
      for (ecs::Entity e : s.es) {
        s.w->add(e, Comp<0>{1});
        s.w->add(e, Comp<1>{2});
        s.w->add(e, Comp<2>{3});
      }
      state.PauseTiming();
      benchmark::DoNotOptimize(s.w.get());
      state.ResumeTiming();
    }
    state.counters["ns_per_entity"] = benchmark::Counter(static_cast<double>(n),
                                                         benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_DirectAdds)->Arg(1000)->Arg(100000);

} // namespace

BENCHMARK_MAIN();
