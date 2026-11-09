// Testes de morte: violações de contrato devem disparar ECS_ASSERT (só compilado com asserts ligados).
// Uso: contract_death <caso>. O handler confere as substrings exigidas e sai com 0; se o caso
// terminar sem assert, imprime "assert não disparou" e devolve 1.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "components.hpp"
#include "ecs/detail/assert.hpp"
#include "ecs/raw_query.hpp"
#include "ecs/world.hpp"

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
    ecs::World world;
    auto entity = world.create(A{1});
    world.query<A>().each([&](A&) { world.add(entity, B{}); });
  }

  void getDeadEntity() {
    struct Position {
      float x;
    };

    ecs::World world;
    auto entity = world.create(Position{1.f});
    world.destroy(entity);
    (void)world.get<Position>(entity);
  }

  void flushDuringEach() {
    ecs::World world;
    world.create(A{1});
    auto commandBuffer = world.commands();
    world.query<A>().each([&](A&) { world.flush(commandBuffer); });
  }

  void commandAfterDestroy() {
    ecs::World world;
    auto entity = world.create(A{1});
    auto commandBuffer = world.commands();
    commandBuffer.destroy(entity);
    commandBuffer.add(entity, A{});
    world.flush(commandBuffer);
  }

  void destroyDuringEachBlock() {
    ecs::World world;
    auto entity = world.create(A{1});
    world.query<A>().eachBlock([&](std::span<A>) { world.destroy(entity); });
  }

  void rawQueryTagInRead() {
    ecs::World world;
    world.rawQuery({.read = {ecs::componentId<Tag>()}});
  }

  void rawQueryRepeatedComponent() {
    ecs::World world;
    world.rawQuery({.read = {ecs::componentId<A>()}, .write = {ecs::componentId<A>()}});
  }

  void rawQueryUnknownComponent() {
    ecs::World world;
    world.rawQuery({.read = {ecs::registeredTypeCount() + 5}});
  }

  void rawQueryEndWithoutBegin() {
    ecs::World world;
    world.rawQuery({.read = {ecs::componentId<A>()}}).endIteration();
  }

  void addDuringRawIteration() {
    ecs::World world;
    auto entity = world.create(A{1});
    world.rawQuery({.read = {ecs::componentId<A>()}}).beginIteration();
    world.add(entity, B{});
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
    {"raw_query_tag_in_read", rawQueryTagInRead, {"rawQuery", "tag"}},
    {"raw_query_repeated_component", rawQueryRepeatedComponent, {"rawQuery", "repetido"}},
    {"raw_query_unknown_component", rawQueryUnknownComponent, {"rawQuery", "desconhecido"}},
    {"raw_query_end_without_begin", rawQueryEndWithoutBegin, {"endIteration"}},
    {"add_during_raw_iteration", addDuringRawIteration, {"add"}},
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
    ecs::setAssertHandler(&handler);
    testCase.run();
    std::fprintf(stderr, "assert não disparou\n");
    return 1;
  }

  std::fprintf(stderr, "caso desconhecido: %s\n", arguments[1]);
  return 64;
}
