#include <benchmark/benchmark.h>

#include <cstdint>
#include <span>
#include <vector>

#include "common.hpp"
#include "ecs/world.hpp"

static void BM_IterateSingle_SoA(benchmark::State& state) {
  const auto entityCount = static_cast<size_t>(state.range(0));
  std::vector<float3> positions(entityCount, float3{0.f, 0.f, 0.f});
  std::vector<float3> velocities(entityCount, float3{1.f, 2.f, 3.f});

  for (auto _ : state) {
    float3* positionData = positions.data();
    const float3* velocityData = velocities.data();
    for (size_t i = 0; i < entityCount; ++i) {
      positionData[i].x += velocityData[i].x * kDeltaTime;
      positionData[i].y += velocityData[i].y * kDeltaTime;
      positionData[i].z += velocityData[i].z * kDeltaTime;
    }

    benchmark::DoNotOptimize(positions.data());
    benchmark::ClobberMemory();
  }

  state.counters["per_entity"] = benchmark::Counter(
    static_cast<double>(entityCount), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}
BENCHMARK(BM_IterateSingle_SoA)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

static void BM_IterateSingle_ECS(benchmark::State& state) {
  const auto entityCount = static_cast<size_t>(state.range(0));
  ecs::World world;
  for (size_t i = 0; i < entityCount; ++i) {
    world.create(Position{float3{0.f, 0.f, 0.f}}, Velocity{float3{1.f, 2.f, 3.f}});
  }

  auto& query = world.query<Position, const Velocity>();

  for (auto _ : state) {
    query.eachBlock([](std::span<Position> positions, std::span<const Velocity> velocities) {
      Position* positionData = positions.data();
      const Velocity* velocityData = velocities.data();
      const size_t count = positions.size();
      for (size_t i = 0; i < count; ++i) {
        positionData[i].value.x += velocityData[i].value.x * kDeltaTime;
        positionData[i].value.y += velocityData[i].value.y * kDeltaTime;
        positionData[i].value.z += velocityData[i].value.z * kDeltaTime;
      }
    });

    benchmark::ClobberMemory();
  }

  state.counters["archetypes"] = world.archetypeCount();
  state.counters["entities_per_archetype"] = double(entityCount) / (world.archetypeCount() - 1);
  state.counters["per_entity"] = benchmark::Counter(
    static_cast<double>(entityCount), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
}
BENCHMARK(BM_IterateSingle_ECS)->Arg(1000)->Arg(10000)->Arg(100000)->Arg(1000000);

BENCHMARK_MAIN();
