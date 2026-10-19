#include "ecs/command_buffer.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "ecs/world.hpp"
#include "model.hpp"

namespace {
  void addToCommandBuffer(ecs::CommandBuffer& commandBuffer, ecs::Entity entity, ecs::ComponentId componentId, uint64_t value) {
    if (componentId == ecs::componentId<A>())
      commandBuffer.add(entity, A{value});
    else if (componentId == ecs::componentId<B>())
      commandBuffer.add(entity, B{value, ~value});
    else if (componentId == ecs::componentId<FuzzState>())
      commandBuffer.add(entity, fuzz::makeFuzzState(value));
    else if (componentId == ecs::componentId<Tag>())
      commandBuffer.add(entity, Tag{});
    else
      commandBuffer.add(entity, LifeCounter{value});
  }
}

TEST_CASE("spawn referenciado no mesmo buffer") {
  ecs::World world;
  auto commandBuffer = world.commands();

  auto entity = commandBuffer.spawn();
  CHECK_FALSE(world.isAlive(entity));

  commandBuffer.add(entity, A{5});
  commandBuffer.add(entity, FuzzState{std::string(40, 'x')});
  world.flushSequential(commandBuffer);

  CHECK(world.get<A>(entity).value == 5);
  CHECK(world.get<FuzzState>(entity).text == std::string(40, 'x'));
  CHECK_INVARIANTS(world);
}

TEST_CASE("payload não trivial sobrevive ao crescimento do buffer") {
  ecs::World world;
  auto entity = world.create();
  auto shortEntity = world.create();
  auto commandBuffer = world.commands();
  std::string big = "payload-que-nao-cabe-no-sso-0123456789";

  commandBuffer.add(entity, FuzzState{big});
  commandBuffer.add(shortEntity, FuzzState{"abc"}); // SSO: um memcpy deixaria o ponteiro interno apontando para o bloco antigo
  for (int i = 0; i < 500; ++i)
    commandBuffer.add(world.create(), A{uint64_t(i)}); // força várias realocações
  world.flushSequential(commandBuffer);

  CHECK(world.get<FuzzState>(entity).text == big);
  CHECK(world.get<FuzzState>(shortEntity).text == "abc");
  CHECK_INVARIANTS(world);
}

TEST_CASE("payload alignas(64) fica alinhado") {
  ecs::World world;
  auto entity = world.create();
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, A{1}); // desloca o offset do próximo registro
  commandBuffer.add(entity, Wide{});

  int seen = 0;
  commandBuffer.forEach([&](const ecs::CommandView& command) {
    if (command.operation == ecs::CommandOperation::Add && command.componentId == ecs::componentId<Wide>()) {
      ++seen;
      CHECK(reinterpret_cast<uintptr_t>(command.payload) % 64 == 0);
    }
  });
  CHECK(seen == 1);

  world.flushSequential(commandBuffer);
  CHECK(world.has<Wide>(entity));
}

TEST_CASE("forEach reproduz a ordem de gravação") {
  ecs::World world;
  auto entity = world.create();
  auto commandBuffer = world.commands();
  auto spawned = commandBuffer.spawn();
  commandBuffer.add(entity, A{7});
  commandBuffer.remove<B>(entity);
  commandBuffer.destroy(spawned);

  std::vector<ecs::CommandOperation> operations;
  commandBuffer.forEach([&](const ecs::CommandView& command) { operations.push_back(command.operation); });

  CHECK(commandBuffer.commandCount() == 4);
  CHECK_FALSE(commandBuffer.empty());
  CHECK(operations == std::vector<ecs::CommandOperation>{ecs::CommandOperation::Spawn, ecs::CommandOperation::Add, ecs::CommandOperation::Remove, ecs::CommandOperation::Destroy});
}

TEST_CASE("buffer descartado sem flush não vaza") {
  LifeCounter::alive = 0;
  {
    ecs::World world;
    {
      auto commandBuffer = world.commands();
      auto spawned = commandBuffer.spawn();
      commandBuffer.add(spawned, LifeCounter{});
      commandBuffer.add(spawned, FuzzState{std::string(64, 'y')});
    }
    CHECK(LifeCounter::alive == 0);

    auto entity = world.create();
    CHECK(entity.index == 0); // índice reservado voltou à freeList (geração + 1)
    CHECK(entity.generation == 1);
  }
}

TEST_CASE("flushSequential ignora handles que não estão vivos nem reservados") {
  ecs::World world;
  auto entity = world.create(A{1});
  world.destroy(entity);
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, B{0, 0});
  commandBuffer.destroy(entity);
  commandBuffer.remove<A>(entity);

  world.flushSequential(commandBuffer);

  CHECK_FALSE(world.isAlive(entity));
  CHECK_INVARIANTS(world);
}

TEST_CASE("fuzz sequencial == aplicar direto") {
  ecs::World world;
  Model model;
  std::mt19937_64 generator(fuzzSeed() ^ 0xC0FFEE);
  std::vector<ecs::Entity> live, dead;
  uint64_t counter = 0, total = 0;
  LifeCounter::alive = 0;

  while (total < 20000) {
    auto commandBuffer = world.commands();
    const unsigned batchSize = 1 + unsigned(generator() % 64);
    for (unsigned commandIndex = 0; commandIndex < batchSize; ++commandIndex, ++total) {
      const unsigned roll = unsigned(generator() % 100);
      if (roll < 15 || live.empty()) {
        ecs::Entity entity = commandBuffer.spawn();
        model[entity];
        live.push_back(entity);
      } else if (roll < 25) {
        size_t i = generator() % live.size();
        ecs::Entity entity = live[i];
        commandBuffer.destroy(entity);
        model.erase(entity);
        live[i] = live.back();
        live.pop_back();
        dead.push_back(entity);
      } else if (roll < 70) {
        ecs::Entity entity = live[generator() % live.size()];
        ecs::ComponentId componentId = fuzz::universe()[generator() % 5];
        uint64_t value = fuzz::isTag(componentId) ? 0 : makeValue(entity, ++counter);
        addToCommandBuffer(commandBuffer, entity, componentId, value);
        model[entity][componentId] = value;
      } else if (roll < 95) {
        ecs::Entity entity = live[generator() % live.size()];
        ecs::ComponentId componentId = fuzz::universe()[generator() % 5];
        commandBuffer.removeRaw(entity, componentId);
        model[entity].erase(componentId);
      } else if (!dead.empty()) {
        ecs::Entity old = dead[generator() % dead.size()]; // obsoleto: ignorado no flush
        addToCommandBuffer(commandBuffer, old, ecs::componentId<A>(), 1);
        commandBuffer.removeRaw(old, ecs::componentId<B>());
      }
    }

    world.flushSequential(commandBuffer);
    CHECK(commandBuffer.empty());
    requireEqual(world, model);
    CHECK_INVARIANTS(world);
  }

  int64_t lifeCounters = 0;
  for (auto& [entity, components] : model)
    lifeCounters += components.count(ecs::componentId<LifeCounter>());
  CHECK(LifeCounter::alive == lifeCounters);
}

TEST_CASE("R-CB-4: add seguido de remove não deixa nada") {
  ecs::World world;
  auto entity = world.create(B{1, 2});
  auto before = world.archetypeCount();
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, A{1});
  commandBuffer.remove<A>(entity);

  world.flush(commandBuffer);

  CHECK_FALSE(world.has<A>(entity));
  CHECK(world.get<B>(entity).first == 1);
  CHECK(world.archetypeCount() == before); // permaneceu no arquétipo original
  CHECK_INVARIANTS(world);
}

TEST_CASE("R-CB-4: remove seguido de add atribui o valor novo") {
  ecs::World world;
  auto entity = world.create(A{1});
  auto commandBuffer = world.commands();
  commandBuffer.remove<A>(entity);
  commandBuffer.add(entity, A{9});

  world.flush(commandBuffer);

  CHECK(world.get<A>(entity).value == 9);
  CHECK_INVARIANTS(world);
}

TEST_CASE("R-CB-4: add repetido mantém o último valor") {
  ecs::World world;
  auto entity = world.create();
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, A{1});
  commandBuffer.add(entity, A{2});
  commandBuffer.add(entity, FuzzState{std::string(40, 'a')});
  commandBuffer.add(entity, FuzzState{std::string(40, 'b')});

  world.flush(commandBuffer);

  CHECK(world.get<A>(entity).value == 2);
  CHECK(world.get<FuzzState>(entity).text == std::string(40, 'b'));
  CHECK_INVARIANTS(world);
}

TEST_CASE("R-CB-4: add sobre T existente atribui") {
  LifeCounter::alive = 0;
  {
    ecs::World world;
    auto entity = world.create(LifeCounter{1}, A{1});
    auto commandBuffer = world.commands();
    commandBuffer.add(entity, LifeCounter{2});

    world.flush(commandBuffer);

    CHECK(world.get<LifeCounter>(entity).value == 2);
    CHECK(LifeCounter::alive == 1);
    CHECK_INVARIANTS(world);
  }
  CHECK(LifeCounter::alive == 0);
}

TEST_CASE("R-CB-4: destroy descarta comandos anteriores; spawn+destroy libera o índice") {
  LifeCounter::alive = 0;
  ecs::World world;
  world.create();
  auto entity = world.create(A{1});
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, B{0, 0});
  commandBuffer.add(entity, LifeCounter{1});
  commandBuffer.destroy(entity);
  auto spawned = commandBuffer.spawn();
  commandBuffer.add(spawned, LifeCounter{2});
  commandBuffer.destroy(spawned);

  world.flush(commandBuffer);

  CHECK_FALSE(world.isAlive(entity));
  CHECK_FALSE(world.isAlive(spawned));
  CHECK(LifeCounter::alive == 0);

  auto created = world.create();
  CHECK(created.index == spawned.index);
  CHECK(created.generation == spawned.generation + 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("R-CB-3: uma única transição por entidade") {
  ecs::World world;
  auto entity = world.create();
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, A{1});
  commandBuffer.add(entity, B{2, 3});
  commandBuffer.add(entity, Tag{});
  auto before = world.archetypeCount();

  world.flush(commandBuffer);

  CHECK(world.archetypeCount() == before + 1); // só {A,B,Tag}; nenhum intermediário
  CHECK(world.get<B>(entity).second == 3);
  CHECK(world.has<Tag>(entity));
  CHECK_INVARIANTS(world);
}

TEST_CASE("flush: entidade com dados nasce direto no arquétipo final") {
  ecs::World world;
  auto commandBuffer = world.commands();
  auto spawned = commandBuffer.spawn();
  commandBuffer.add(spawned, A{1});
  commandBuffer.add(spawned, B{2, 3});
  auto before = world.archetypeCount();

  world.flush(commandBuffer);

  CHECK(world.archetypeCount() == before + 1);
  CHECK(world.get<A>(spawned).value == 1);
  CHECK_INVARIANTS(world);
}

TEST_CASE("comandos para handle destruído diretamente são ignorados") {
  ecs::World world;
  auto entity = world.create(A{1});
  auto commandBuffer = world.commands();
  commandBuffer.add(entity, B{0, 0});
  world.destroy(entity);
  auto reused = world.create(A{9}); // mesmo índice, geração nova
  CHECK(reused.index == entity.index);

  world.flush(commandBuffer);

  CHECK_FALSE(world.has<B>(reused));
  CHECK(world.get<A>(reused).value == 9);
  CHECK_INVARIANTS(world);
}

TEST_CASE("spawn+destroy no mesmo buffer devolve o índice com geração + 1") {
  ecs::World world;
  auto commandBuffer = world.commands();
  auto spawned = commandBuffer.spawn();
  commandBuffer.destroy(spawned);
  world.flush(commandBuffer);

  auto again = world.create();

  CHECK(again.index == spawned.index);
  CHECK(again.generation == spawned.generation + 1);
}

TEST_CASE("fuzz: flush coalescido == flushSequential") {
  LifeCounter::alive = 0;
  {
    ecs::World coalesced, sequential;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> live; // handles lógicos
    std::mt19937_64 generator(fuzzSeed() ^ 0xBADC0DE);
    uint64_t counter = 0, total = 0;

    auto compare = [&] {
      for (auto& [coalescedEntity, sequentialEntity] : live) {
        REQUIRE(coalesced.isAlive(coalescedEntity));
        REQUIRE(sequential.isAlive(sequentialEntity));
        for (int i = 0; i < 5; ++i) {
          const auto componentId = fuzz::universe()[i];
          uint64_t coalescedValue = 0, sequentialValue = 0;
          bool coalescedHas = readFromWorld(coalesced, coalescedEntity, componentId, coalescedValue), sequentialHas = readFromWorld(sequential, sequentialEntity, componentId, sequentialValue);
          if (coalescedHas != sequentialHas || coalescedValue != sequentialValue)
            FAIL("divergência em " << ecs::typeInfo(componentId).name << " e1=" << coalescedEntity.index);
        }
      }
    };

    while (total < 30000) {
      auto coalescedCommands = coalesced.commands();
      auto sequentialCommands = sequential.commands();
      const unsigned batchSize = 1 + unsigned(generator() % 64);
      std::vector<std::pair<ecs::Entity, ecs::Entity>> destroyedNow;
      for (unsigned commandIndex = 0; commandIndex < batchSize; ++commandIndex, ++total) {
        const unsigned roll = unsigned(generator() % 100);
        if (roll < 15 || live.empty()) {
          live.emplace_back(coalescedCommands.spawn(), sequentialCommands.spawn());
        } else if (roll < 25) {
          size_t i = generator() % live.size();
          auto [coalescedEntity, sequentialEntity] = live[i];
          coalescedCommands.destroy(coalescedEntity);
          sequentialCommands.destroy(sequentialEntity);
          destroyedNow.push_back(live[i]);
          live[i] = live.back(); // sem comandos depois de destroy
          live.pop_back();
        } else if (roll < 70) {
          size_t i = generator() % live.size();
          auto [coalescedEntity, sequentialEntity] = live[i];
          ecs::ComponentId componentId = fuzz::universe()[generator() % 5];
          uint64_t value = fuzz::isTag(componentId) ? 0 : makeValue(ecs::Entity{uint32_t(i), 0}, ++counter);
          addToCommandBuffer(coalescedCommands, coalescedEntity, componentId, value);
          addToCommandBuffer(sequentialCommands, sequentialEntity, componentId, value);
        } else {
          size_t i = generator() % live.size();
          ecs::ComponentId componentId = fuzz::universe()[generator() % 5];
          coalescedCommands.removeRaw(live[i].first, componentId);
          sequentialCommands.removeRaw(live[i].second, componentId);
        }
      }

      coalesced.flush(coalescedCommands);
      sequential.flushSequential(sequentialCommands);

      compare();
      CHECK_INVARIANTS(coalesced);
      CHECK_INVARIANTS(sequential);
      for (auto& [coalescedEntity, sequentialEntity] : destroyedNow) {
        CHECK_FALSE(coalesced.isAlive(coalescedEntity));
        CHECK_FALSE(sequential.isAlive(sequentialEntity));
      }
    }
  }
  CHECK(LifeCounter::alive == 0);
}

TEST_CASE("R-CB-3: comandos intercalados entre entidades ainda coalescem por entidade") {
  ecs::World world;
  auto first = world.create();
  auto second = world.create();
  auto commandBuffer = world.commands();
  commandBuffer.add(first, A{1});
  commandBuffer.add(second, A{2});
  commandBuffer.add(first, B{3, 4}); // first volta fora do trecho consecutivo
  commandBuffer.add(second, B{5, 6});
  commandBuffer.add(first, A{7});
  auto before = world.archetypeCount();

  world.flush(commandBuffer);

  CHECK(world.archetypeCount() == before + 1); // só {A,B}
  CHECK(world.get<A>(first).value == 7);
  CHECK(world.get<B>(first).second == 4);
  CHECK(world.get<A>(second).value == 2);
  CHECK(world.get<B>(second).second == 6);
  CHECK_INVARIANTS(world);
}

TEST_CASE("buffer reutilizado depois do flush agrupa de novo") {
  ecs::World world;
  auto first = world.create();
  auto second = world.create();
  auto commandBuffer = world.commands();
  commandBuffer.add(first, A{1});
  commandBuffer.add(second, A{2});
  commandBuffer.add(first, B{0, 0}); // não consecutivo: o primeiro lote cai no caminho geral
  world.flush(commandBuffer);

  commandBuffer.add(second, B{8, 9}); // segundo lote: o estado de agrupamento foi zerado pelo flush
  commandBuffer.add(first, A{5});
  world.flush(commandBuffer);

  CHECK(world.get<A>(first).value == 5);
  CHECK(world.get<B>(second).second == 9);
  CHECK(world.get<A>(second).value == 2);
  CHECK_INVARIANTS(world);
}
