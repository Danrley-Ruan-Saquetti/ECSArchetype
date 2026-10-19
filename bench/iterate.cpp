#include <benchmark/benchmark.h>

#include <cstdint>
#include <span>
#include <vector>

#include "common.hpp"
#include "ecs/world.hpp"

static void BM_IterateSingle_SoA(benchmark::State& state) {
  const auto n = static_cast<size_t>(state.range(0));
  std::vector<float3> pos(n, float3{0.f, 0.f, 0.f});
  std::vector<float3> vel(n, float3{1.f, 2.f, 3.f});
  for (auto _ : state) {
    float3* p = pos.data();
    const float3* v = vel.data();
    for (size_t i = 0; i < n; ++i) {
      p[i].x += v[i].x * kDt;
      p[i].y += v[i].y * kDt;
      p[i].z += v[i].z * kDt;
    }
    benchmark::DoNotOptimize(pos.data());
    benchmark::ClobberMemory();
  }
  state.counters["per_entity"] = benchmark::Counter(
    static_cast<double>(n), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}
BENCHMARK(BM_IterateSingle_SoA)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

static void BM_IterateSingle_ECS(benchmark::State& state) {
  const auto n = static_cast<size_t>(state.range(0));
  ecs::World w;
  for (size_t i = 0; i < n; ++i)
    w.create(Position{float3{0.f, 0.f, 0.f}}, Velocity{float3{1.f, 2.f, 3.f}});
  auto& q = w.query<Position, const Velocity>();
  for (auto _ : state) {
    q.eachBlock([](std::span<Position> pos, std::span<const Velocity> vel) {
      Position* p = pos.data();
      const Velocity* v = vel.data();
      const size_t count = pos.size();
      for (size_t i = 0; i < count; ++i) {
        p[i].v.x += v[i].v.x * kDt;
        p[i].v.y += v[i].v.y * kDt;
        p[i].v.z += v[i].v.z * kDt;
      }
    });
    benchmark::ClobberMemory();
  }
  state.counters["archetypes"] = w.archetypeCount();
  state.counters["entities_per_archetype"] = double(n) / (w.archetypeCount() - 1);
  state.counters["per_entity"] = benchmark::Counter(
    static_cast<double>(n), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}
BENCHMARK(BM_IterateSingle_ECS)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

BENCHMARK_MAIN();
