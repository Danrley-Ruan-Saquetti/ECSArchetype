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
#include "ecs/world.hpp"

namespace {

  const std::vector<const char*>* g_required = nullptr;

  void handler(const char* message) noexcept {
    std::fprintf(stderr, "%s\n", message);
    if (g_required)
      for (const char* needle : *g_required)
        if (!std::strstr(message, needle)) {
          std::fprintf(stderr, "mensagem sem a substring exigida: \"%s\"\n", needle);
          std::fflush(stderr);
          std::_Exit(2);
        }
    std::fflush(stderr);
    std::_Exit(0);
  }

  void addDuringEach() {
    ecs::World w;
    auto e = w.create(A{1});
    w.query<A>().each([&](A&) { w.add(e, B{}); });
  }

  void getDeadEntity() {
    struct Position {
      float x;
    };
    ecs::World w;
    auto e = w.create(Position{1.f});
    w.destroy(e);
    (void)w.get<Position>(e);
  }

  void flushDuringEach() {
    ecs::World w;
    w.create(A{1});
    auto cb = w.commands();
    w.query<A>().each([&](A&) { w.flush(cb); });
  }

  void commandAfterDestroy() {
    ecs::World w;
    auto e = w.create(A{1});
    auto cb = w.commands();
    cb.destroy(e);
    cb.add(e, A{});
    w.flush(cb);
  }

  void destroyDuringEachBlock() {
    ecs::World w;
    auto e = w.create(A{1});
    w.query<A>().eachBlock([&](std::span<A>) { w.destroy(e); });
  }

  struct Case {
    const char* name;
    void (*run)();
    std::vector<const char*> required;
  };

} // namespace

int main(int argc, char** argv) {
  static const std::vector<Case> cases = {
    {"add_during_each", addDuringEach, {"add", "e="}},
    {"get_dead_entity", getDeadEntity, {"get", "e=", "Position"}},
    {"flush_during_each", flushDuringEach, {"flush"}},
    {"command_after_destroy", commandAfterDestroy, {"after destroy"}},
    {"destroy_during_each_block", destroyDuringEachBlock, {"destroy"}},
  };
  if (argc != 2) {
    std::fprintf(stderr, "uso: contract_death <caso>\n");
    return 64;
  }
  for (const Case& c : cases) {
    if (std::strcmp(c.name, argv[1]) != 0)
      continue;
    g_required = &c.required;
    ecs::setAssertHandler(&handler);
    c.run();
    std::fprintf(stderr, "assert não disparou\n");
    return 1;
  }
  std::fprintf(stderr, "caso desconhecido: %s\n", argv[1]);
  return 64;
}
