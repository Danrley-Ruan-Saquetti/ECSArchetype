#include <benchmark/benchmark.h>

#include <cstdint>
#include <vector>

#include "common.hpp"

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

BENCHMARK_MAIN();
