#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "common.hpp"
#include "oly/ecs/world.hpp"

namespace {
  using oly::ecs::World;

  // N entities in the root archetype, with the path's archetypes and edges already created by a
  // discarded warm-up entity, so that the measured time does not include that creation.
  struct Setup {
    std::unique_ptr<World> world = std::make_unique<World>();
    std::vector<oly::ecs::Entity> entities;

    explicit Setup(int64_t entityCount, bool coalesced) {
      auto warmupEntity = world->spawn();
      if (coalesced) {
        auto commandBuffer = world->commands();
        commandBuffer.add(warmupEntity, Component<0>{0});
        commandBuffer.add(warmupEntity, Component<1>{0});
        commandBuffer.add(warmupEntity, Component<2>{0});
        world->flush(commandBuffer);
      } else {
        world->add(warmupEntity, Component<0>{0});
        world->add(warmupEntity, Component<1>{0});
        world->add(warmupEntity, Component<2>{0});
      }
      world->destroy(warmupEntity);

      entities.reserve(static_cast<size_t>(entityCount));
      for (int64_t i = 0; i < entityCount; ++i) {
        entities.push_back(world->spawn());
      }
    }
  };

  void record(oly::ecs::CommandBuffer& commandBuffer, const std::vector<oly::ecs::Entity>& entities) {
    for (oly::ecs::Entity entity : entities) {
      commandBuffer.add(entity, Component<0>{1});
      commandBuffer.add(entity, Component<1>{2});
      commandBuffer.add(entity, Component<2>{3});
    }
  }

  // NF-P5 measures the flush: recording (3N commands) stays outside the timing; BM_RecordAndFlush includes it.
  void BM_FlushCoalesced(benchmark::State& state) {
    const int64_t entityCount = state.range(0);

    for (auto _ : state) {
      state.PauseTiming();
      Setup setup(entityCount, true);
      std::optional<oly::ecs::CommandBuffer> commandBuffer;
      commandBuffer.emplace(setup.world->commands());
      record(*commandBuffer, setup.entities);
      state.ResumeTiming();

      setup.world->flush(*commandBuffer);

      state.PauseTiming();
      commandBuffer.reset();
      auto* worldPointer = setup.world.get();
      benchmark::DoNotOptimize(worldPointer);
      state.ResumeTiming();
    }

    state.counters["ns_per_entity"] = benchmark::Counter(static_cast<double>(entityCount),
                                                         benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_FlushCoalesced)->Arg(1000)->Arg(100000);

  void BM_RecordAndFlush(benchmark::State& state) {
    const int64_t entityCount = state.range(0);

    for (auto _ : state) {
      state.PauseTiming();
      Setup setup(entityCount, true);
      std::optional<oly::ecs::CommandBuffer> commandBuffer;
      state.ResumeTiming();

      commandBuffer.emplace(setup.world->commands());
      record(*commandBuffer, setup.entities);
      setup.world->flush(*commandBuffer);

      state.PauseTiming();
      commandBuffer.reset();
      auto* worldPointer = setup.world.get();
      benchmark::DoNotOptimize(worldPointer);
      state.ResumeTiming();
    }

    state.counters["ns_per_entity"] = benchmark::Counter(static_cast<double>(entityCount),
                                                         benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_RecordAndFlush)->Arg(1000)->Arg(100000);

  void BM_DirectAdds(benchmark::State& state) {
    const int64_t entityCount = state.range(0);

    for (auto _ : state) {
      state.PauseTiming();
      Setup setup(entityCount, false);
      state.ResumeTiming();

      for (oly::ecs::Entity entity : setup.entities) {
        setup.world->add(entity, Component<0>{1});
        setup.world->add(entity, Component<1>{2});
        setup.world->add(entity, Component<2>{3});
      }

      state.PauseTiming();
      auto* worldPointer = setup.world.get();
      benchmark::DoNotOptimize(worldPointer);
      state.ResumeTiming();
    }

    state.counters["ns_per_entity"] = benchmark::Counter(static_cast<double>(entityCount),
                                                         benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_DirectAdds)->Arg(1000)->Arg(100000);
}

BENCHMARK_MAIN();
