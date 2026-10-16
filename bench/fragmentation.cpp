#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <ecs/world.hpp>
#include <span>
#include <utility>
#include <vector>

#include "common.hpp"

namespace {

template <int I>
struct Frag {};  // tag

constexpr int kFragTags = 12;  // 2^12 = 4096 combinações

template <size_t... Is>
std::array<ecs::ComponentId, sizeof...(Is)> fragIds(std::index_sequence<Is...>) {
    return {ecs::componentId<Frag<int(Is)>>()...};
}

constexpr uint32_t kN = 1'000'000;

// Baseline no mesmo binário.
void BM_IterateSingle_SoA(benchmark::State& state) {
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
BENCHMARK(BM_IterateSingle_SoA)->Arg(kN);

// 1M entidades distribuídas uniformemente em A arquétipos: o arquétipo da entidade i é
// {Position, Velocity} + as tags Frag<j> cujos bits estão ligados em (i % A).
void BM_IterateFragmented(benchmark::State& state) {
    const auto A = static_cast<uint32_t>(state.range(0));
    const auto tags = fragIds(std::make_index_sequence<kFragTags>{});
    ecs::World w;
    for (uint32_t i = 0; i < kN; ++i) {
        ecs::Entity e = w.create(Position{float3{0.f, 0.f, 0.f}}, Velocity{float3{1.f, 2.f, 3.f}});
        const uint32_t mask = i % A;
        for (int j = 0; j < kFragTags; ++j)
            if (mask & (1u << j)) w.addRaw(e, tags[j], nullptr);
    }
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
    state.counters["archetypes"] = static_cast<double>(q.matchedCount());
    state.counters["entities_per_archetype"] = double(kN) / double(q.matchedCount());
    state.counters["per_entity"] = benchmark::Counter(
        static_cast<double>(kN), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}
BENCHMARK(BM_IterateFragmented)->Arg(1)->Arg(8)->Arg(64)->Arg(512)->Arg(4096);

}  // namespace

BENCHMARK_MAIN();
