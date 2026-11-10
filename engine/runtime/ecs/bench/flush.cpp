#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "common.hpp"
#include "oly/ecs/world.hpp"

namespace {
  using oly::ecs::World;

  // N entidades no arquétipo raiz (fora do tempo). Os arquétipos e as arestas são aquecidos com
  // uma entidade extra, que é destruída em seguida.
  struct Setup {
    std::unique_ptr<World> world = std::make_unique<World>();
    std::vector<oly::ecs::Entity> entities;

    explicit Setup(int64_t entityCount, bool coalesced) {
      auto warm = world->spawn();
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

  // NF-P5 mede o flush: a gravação (3N comandos) fica fora do tempo; BM_RecordAndFlush a inclui.
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
