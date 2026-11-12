// Casos de morte: cada violação de contrato deve disparar OLY_ASSERT, então só existe com asserts ligados.
// Uso: contract_death <caso>. Sai com 0 só se o assert disparar com todas as substrings exigidas.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "components.hpp"
#include "oly/core/assert.hpp"
#include "oly/ecs/query_by_id.hpp"
#include "oly/ecs/world.hpp"

namespace {
  const std::vector<const char*>* g_required = nullptr;

  void handler(const char* message) noexcept {
    std::fprintf(stderr, "%s\n", message);

    if (g_required) {
      for (const char* needle : *g_required) {
        if (!std::strstr(message, needle)) {
          std::fprintf(stderr, "mensagem sem a substring exigida: \"%s\"\n", needle);
          std::fflush(stderr);
          std::_Exit(2);
        }
      }
    }

    std::fflush(stderr);
    std::_Exit(0);
  }

  void addDuringEach() {
    oly::ecs::World world;
    auto entity = world.spawn(A{1});
    world.query<A>().each([&](A&) { world.add(entity, B{}); });
  }

  void getDeadEntity() {
    struct Position {
      float x;
    };

    oly::ecs::World world;
    auto entity = world.spawn(Position{1.f});
    world.destroy(entity);
    (void)world.get<Position>(entity);
  }

  void flushDuringEach() {
    oly::ecs::World world;
    world.spawn(A{1});
    auto commandBuffer = world.commands();
    world.query<A>().each([&](A&) { world.flush(commandBuffer); });
  }

  void commandAfterDestroy() {
    oly::ecs::World world;
    auto entity = world.spawn(A{1});
    auto commandBuffer = world.commands();
    commandBuffer.destroy(entity);
    commandBuffer.add(entity, A{});
    world.flush(commandBuffer);
  }

  void destroyDuringEachBlock() {
    oly::ecs::World world;
    auto entity = world.spawn(A{1});
    world.query<A>().eachBlock([&](std::span<A>) { world.destroy(entity); });
  }

  void queryByIdTagInRead() {
    oly::ecs::World world;
    world.queryById({.read = {oly::ecs::componentId<Tag>()}});
  }

  void queryByIdRepeatedComponent() {
    oly::ecs::World world;
    world.queryById({.read = {oly::ecs::componentId<A>()}, .write = {oly::ecs::componentId<A>()}});
  }

  void queryByIdUnknownComponent() {
    oly::ecs::World world;
    world.queryById({.read = {oly::ecs::registeredTypeCount() + 5}});
  }

  void queryByIdIterationOutOfOrder() {
    oly::ecs::World world;
    auto& query = world.queryById({.read = {oly::ecs::componentId<A>()}});
    std::optional<oly::ecs::QueryIteration> first(query.iterate());
    std::optional<oly::ecs::QueryIteration> second(query.iterate());
    first.reset();
  }

  void addDuringQueryByIdIteration() {
    oly::ecs::World world;
    auto entity = world.spawn(A{1});
    auto iteration = world.queryById({.read = {oly::ecs::componentId<A>()}}).iterate();
    world.add(entity, B{});
  }

  void getByIdMissing() {
    oly::ecs::World world;
    auto entity = world.spawn(A{1});
    world.getById(entity, oly::ecs::componentId<B>());
  }

  void spawnDuringEach() {
    oly::ecs::World world;
    world.spawn(A{1});
    world.query<A>().each([&](A&) { world.spawn(); });
  }

  void singleEmpty() {
    oly::ecs::World world;
    world.query<A>().single();
  }

  void singleMany() {
    oly::ecs::World world;
    world.spawn(A{1});
    world.spawn(A{2});
    world.query<A>().single();
  }

  void trySingleMany() {
    oly::ecs::World world;
    world.spawn(A{1});
    world.spawn(A{2});
    world.query<A>().trySingle();
  }

  struct Case {
    const char* name;
    void (*run)();
    std::vector<const char*> required;
  };
}

int main(int argumentCount, char** arguments) {
  static const std::vector<Case> cases = {
    {"add_during_each", addDuringEach, {"add", "e="}},
    {"get_dead_entity", getDeadEntity, {"get", "e=", "Position"}},
    {"flush_during_each", flushDuringEach, {"flush"}},
    {"command_after_destroy", commandAfterDestroy, {"after destroy"}},
    {"destroy_during_each_block", destroyDuringEachBlock, {"destroy"}},
    {"query_by_id_tag_in_read", queryByIdTagInRead, {"queryById", "tag"}},
    {"query_by_id_repeated_component", queryByIdRepeatedComponent, {"queryById", "repetido"}},
    {"query_by_id_unknown_component", queryByIdUnknownComponent, {"queryById", "desconhecido"}},
    {"query_by_id_iteration_out_of_order", queryByIdIterationOutOfOrder, {"iterate", "fora de ordem"}},
    {"add_during_query_by_id_iteration", addDuringQueryByIdIteration, {"add"}},
    {"get_by_id_missing", getByIdMissing, {"getById", "e="}},
    {"spawn_during_each", spawnDuringEach, {"spawn", "iteração"}},
    {"single_empty", singleEmpty, {"single:", "0 linhas"}},
    {"single_many", singleMany, {"single:", "2 linhas"}},
    {"try_single_many", trySingleMany, {"trySingle:", "2 linhas"}},
  };

  if (argumentCount != 2) {
    std::fprintf(stderr, "uso: contract_death <caso>\n");
    return 64;
  }

  for (const Case& testCase : cases) {
    if (std::strcmp(testCase.name, arguments[1]) != 0) {
      continue;
    }

    g_required = &testCase.required;
    oly::core::setAssertHandler(&handler);
    testCase.run();
    std::fprintf(stderr, "assert não disparou\n");
    return 1;
  }

  std::fprintf(stderr, "caso desconhecido: %s\n", arguments[1]);
  return 64;
}
