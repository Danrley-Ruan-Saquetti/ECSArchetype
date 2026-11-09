#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "common.hpp"
#include "ecs/world.hpp"

namespace {
  template <int I>
  struct Fragment {};

  constexpr int kFragmentTags = 12; // 2^12 = 4096 combinações

  template <size_t... Is>
  std::array<ecs::ComponentId, sizeof...(Is)> fragmentIds(std::index_sequence<Is...>) {
    return {ecs::componentId<Fragment<int(Is)>>()...};
  }

  constexpr uint32_t kEntityCount = 1'000'000;

  // Baseline no mesmo binário.
  void BM_IterateSingle_SoA(benchmark::State& state) {
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

      auto* positionPointer = positions.data();
      benchmark::DoNotOptimize(positionPointer);
      benchmark::ClobberMemory();
    }

    state.counters["per_entity"] = benchmark::Counter(
      static_cast<double>(entityCount), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_IterateSingle_SoA)->Arg(kEntityCount);

  // 1M entidades distribuídas uniformemente em `archetypeCount` arquétipos: o arquétipo da entidade i é
  // {Position, Velocity} + as tags Fragment<j> cujos bits estão ligados em (i % archetypeCount).
  void BM_IterateFragmented(benchmark::State& state) {
    const auto archetypeCount = static_cast<uint32_t>(state.range(0));
    const auto tags = fragmentIds(std::make_index_sequence<kFragmentTags>{});
    ecs::World world;
    for (uint32_t i = 0; i < kEntityCount; ++i) {
      ecs::Entity entity = world.create(Position{float3{0.f, 0.f, 0.f}}, Velocity{float3{1.f, 2.f, 3.f}});
      const uint32_t mask = i % archetypeCount;
      for (int j = 0; j < kFragmentTags; ++j) {
        if (mask & (1u << j)) {
          world.addRaw(entity, tags[j], nullptr);
        }
      }
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

    state.counters["archetypes"] = static_cast<double>(query.matchedCount());
    state.counters["entities_per_archetype"] = double(kEntityCount) / double(query.matchedCount());
    state.counters["per_entity"] = benchmark::Counter(
      static_cast<double>(kEntityCount), benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_IterateFragmented)->Arg(1)->Arg(8)->Arg(64)->Arg(512)->Arg(4096);
}

BENCHMARK_MAIN();
