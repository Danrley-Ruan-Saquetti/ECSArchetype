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
    std::unique_ptr<World> world = std::make_unique<World>();
    std::vector<ecs::Entity> entities;

    explicit Setup(int64_t entityCount, bool coalesced) {
      auto warm = world->create();
      if (coalesced) {
        auto commandBuffer = world->commands();
        commandBuffer.add(warm, Component<0>{0});
        commandBuffer.add(warm, Component<1>{0});
        commandBuffer.add(warm, Component<2>{0});
        world->flush(commandBuffer);
      } else {
        world->add(warm, Component<0>{0});
        world->add(warm, Component<1>{0});
        world->add(warm, Component<2>{0});
      }
      world->destroy(warm);

      entities.reserve(static_cast<size_t>(entityCount));
      for (int64_t i = 0; i < entityCount; ++i)
        entities.push_back(world->create());
    }
  };

  void record(ecs::CommandBuffer& commandBuffer, const std::vector<ecs::Entity>& entities) {
    for (ecs::Entity entity : entities) {
      commandBuffer.add(entity, Component<0>{1});
      commandBuffer.add(entity, Component<1>{2});
      commandBuffer.add(entity, Component<2>{3});
    }
  }

  // NF-P5 mede o flush: a gravação (3N comandos) fica fora do tempo; BM_RecordAndFlush a inclui.
  void BM_FlushCoalesced(benchmark::State& state) {
    const int64_t entityCount = state.range(0);

    for (auto _ : state) {
      state.PauseTiming();
      Setup setup(entityCount, true);
      std::optional<ecs::CommandBuffer> commandBuffer;
      commandBuffer.emplace(setup.world->commands());
      record(*commandBuffer, setup.entities);
      state.ResumeTiming();

      setup.world->flush(*commandBuffer);

      state.PauseTiming();
      commandBuffer.reset();
      benchmark::DoNotOptimize(setup.world.get());
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
      std::optional<ecs::CommandBuffer> commandBuffer;
      state.ResumeTiming();

      commandBuffer.emplace(setup.world->commands());
      record(*commandBuffer, setup.entities);
      setup.world->flush(*commandBuffer);

      state.PauseTiming();
      commandBuffer.reset();
      benchmark::DoNotOptimize(setup.world.get());
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

      for (ecs::Entity entity : setup.entities) {
        setup.world->add(entity, Component<0>{1});
        setup.world->add(entity, Component<1>{2});
        setup.world->add(entity, Component<2>{3});
      }

      state.PauseTiming();
      benchmark::DoNotOptimize(setup.world.get());
      state.ResumeTiming();
    }

    state.counters["ns_per_entity"] = benchmark::Counter(static_cast<double>(entityCount),
                                                         benchmark::Counter::kIsIterationInvariantRate | benchmark::Counter::kInvert);
  }
  BENCHMARK(BM_DirectAdds)->Arg(1000)->Arg(100000);
}

BENCHMARK_MAIN();
