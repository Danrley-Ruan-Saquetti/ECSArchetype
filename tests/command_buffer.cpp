#include "ecs/command_buffer.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "ecs/world.hpp"
#include "model.hpp"

namespace {
  void cbAdd(ecs::CommandBuffer& cb, ecs::Entity e, ecs::ComponentId cid, uint64_t v) {
    if (cid == ecs::componentId<A>())
      cb.add(e, A{v});
    else if (cid == ecs::componentId<B>())
      cb.add(e, B{v, ~v});
    else if (cid == ecs::componentId<FS>())
      cb.add(e, fuzz::makeFS(v));
    else if (cid == ecs::componentId<Tag>())
      cb.add(e, Tag{});
    else
      cb.add(e, LifeCounter{v});
  }
}

TEST_CASE("spawn referenciado no mesmo buffer") {
  ecs::World w;
  auto cb = w.commands();

  auto e = cb.spawn();
  CHECK_FALSE(w.isAlive(e));

  cb.add(e, A{5});
  cb.add(e, FS{std::string(40, 'x')});
  w.flushSequential(cb);

  CHECK(w.get<A>(e).v == 5);
  CHECK(w.get<FS>(e).s == std::string(40, 'x'));
  CHECK_INV(w);
}

TEST_CASE("payload não trivial sobrevive ao crescimento do buffer") {
  ecs::World w;
  auto e = w.create();
  auto eShort = w.create();
  auto cb = w.commands();
  std::string big = "payload-que-nao-cabe-no-sso-0123456789";

  cb.add(e, FS{big});
  cb.add(eShort, FS{"abc"}); // SSO: um memcpy deixaria o ponteiro interno apontando para o bloco antigo
  for (int i = 0; i < 500; ++i)
    cb.add(w.create(), A{uint64_t(i)}); // força várias realocações
  w.flushSequential(cb);

  CHECK(w.get<FS>(e).s == big);
  CHECK(w.get<FS>(eShort).s == "abc");
  CHECK_INV(w);
}

TEST_CASE("payload alignas(64) fica alinhado") {
  ecs::World w;
  auto e = w.create();
  auto cb = w.commands();
  cb.add(e, A{1}); // desloca o offset do próximo registro
  cb.add(e, Wide{});

  int seen = 0;
  cb.forEach([&](const ecs::CommandView& v) {
    if (v.op == ecs::CommandOp::Add && v.cid == ecs::componentId<Wide>()) {
      ++seen;
      CHECK(reinterpret_cast<uintptr_t>(v.payload) % 64 == 0);
    }
  });
  CHECK(seen == 1);

  w.flushSequential(cb);
  CHECK(w.has<Wide>(e));
}

TEST_CASE("forEach reproduz a ordem de gravação") {
  ecs::World w;
  auto e = w.create();
  auto cb = w.commands();
  auto s = cb.spawn();
  cb.add(e, A{7});
  cb.remove<B>(e);
  cb.destroy(s);

  std::vector<ecs::CommandOp> ops;
  cb.forEach([&](const ecs::CommandView& v) { ops.push_back(v.op); });

  CHECK(cb.commandCount() == 4);
  CHECK_FALSE(cb.empty());
  CHECK(ops == std::vector<ecs::CommandOp>{ecs::CommandOp::Spawn, ecs::CommandOp::Add, ecs::CommandOp::Remove, ecs::CommandOp::Destroy});
}

TEST_CASE("buffer descartado sem flush não vaza") {
  LifeCounter::alive = 0;
  {
    ecs::World w;
    {
      auto cb = w.commands();
      auto s = cb.spawn();
      cb.add(s, LifeCounter{});
      cb.add(s, FS{std::string(64, 'y')});
    }
    CHECK(LifeCounter::alive == 0);

    auto e = w.create();
    CHECK(e.index == 0); // índice reservado voltou à freeList (geração + 1)
    CHECK(e.generation == 1);
  }
}

TEST_CASE("flushSequential ignora handles que não estão vivos nem reservados") {
  ecs::World w;
  auto e = w.create(A{1});
  w.destroy(e);
  auto cb = w.commands();
  cb.add(e, B{0, 0});
  cb.destroy(e);
  cb.remove<A>(e);

  w.flushSequential(cb);

  CHECK_FALSE(w.isAlive(e));
  CHECK_INV(w);
}

TEST_CASE("fuzz sequencial == aplicar direto") {
  ecs::World w;
  Model model;
  std::mt19937_64 rng(fuzzSeed() ^ 0xC0FFEE);
  std::vector<ecs::Entity> live, dead;
  uint64_t counter = 0, total = 0;
  LifeCounter::alive = 0;

  while (total < 20000) {
    auto cb = w.commands();
    const unsigned n = 1 + unsigned(rng() % 64);
    for (unsigned k = 0; k < n; ++k, ++total) {
      const unsigned r = unsigned(rng() % 100);
      if (r < 15 || live.empty()) {
        ecs::Entity e = cb.spawn();
        model[e];
        live.push_back(e);
      } else if (r < 25) {
        size_t i = rng() % live.size();
        ecs::Entity e = live[i];
        cb.destroy(e);
        model.erase(e);
        live[i] = live.back();
        live.pop_back();
        dead.push_back(e);
      } else if (r < 70) {
        ecs::Entity e = live[rng() % live.size()];
        ecs::ComponentId cid = fuzz::universe()[rng() % 5];
        uint64_t v = fuzz::isTag(cid) ? 0 : makeValue(e, ++counter);
        cbAdd(cb, e, cid, v);
        model[e][cid] = v;
      } else if (r < 95) {
        ecs::Entity e = live[rng() % live.size()];
        ecs::ComponentId cid = fuzz::universe()[rng() % 5];
        cb.removeRaw(e, cid);
        model[e].erase(cid);
      } else if (!dead.empty()) {
        ecs::Entity old = dead[rng() % dead.size()]; // obsoleto: ignorado no flush
        cbAdd(cb, old, ecs::componentId<A>(), 1);
        cb.removeRaw(old, ecs::componentId<B>());
      }
    }

    w.flushSequential(cb);
    CHECK(cb.empty());
    requireEqual(w, model);
    CHECK_INV(w);
  }

  int64_t lc = 0;
  for (auto& [e, comps] : model)
    lc += comps.count(ecs::componentId<LifeCounter>());
  CHECK(LifeCounter::alive == lc);
}

TEST_CASE("R-CB-4: add seguido de remove não deixa nada") {
  ecs::World w;
  auto e = w.create(B{1, 2});
  auto before = w.archetypeCount();
  auto cb = w.commands();
  cb.add(e, A{1});
  cb.remove<A>(e);

  w.flush(cb);

  CHECK_FALSE(w.has<A>(e));
  CHECK(w.get<B>(e).v == 1);
  CHECK(w.archetypeCount() == before); // permaneceu no arquétipo original
  CHECK_INV(w);
}

TEST_CASE("R-CB-4: remove seguido de add atribui o valor novo") {
  ecs::World w;
  auto e = w.create(A{1});
  auto cb = w.commands();
  cb.remove<A>(e);
  cb.add(e, A{9});

  w.flush(cb);

  CHECK(w.get<A>(e).v == 9);
  CHECK_INV(w);
}

TEST_CASE("R-CB-4: add repetido mantém o último valor") {
  ecs::World w;
  auto e = w.create();
  auto cb = w.commands();
  cb.add(e, A{1});
  cb.add(e, A{2});
  cb.add(e, FS{std::string(40, 'a')});
  cb.add(e, FS{std::string(40, 'b')});

  w.flush(cb);

  CHECK(w.get<A>(e).v == 2);
  CHECK(w.get<FS>(e).s == std::string(40, 'b'));
  CHECK_INV(w);
}

TEST_CASE("R-CB-4: add sobre T existente atribui") {
  LifeCounter::alive = 0;
  {
    ecs::World w;
    auto e = w.create(LifeCounter{1}, A{1});
    auto cb = w.commands();
    cb.add(e, LifeCounter{2});

    w.flush(cb);

    CHECK(w.get<LifeCounter>(e).v == 2);
    CHECK(LifeCounter::alive == 1);
    CHECK_INV(w);
  }
  CHECK(LifeCounter::alive == 0);
}

TEST_CASE("R-CB-4: destroy descarta comandos anteriores; spawn+destroy libera o índice") {
  LifeCounter::alive = 0;
  ecs::World w;
  w.create();
  auto e = w.create(A{1});
  auto cb = w.commands();
  cb.add(e, B{0, 0});
  cb.add(e, LifeCounter{1});
  cb.destroy(e);
  auto s = cb.spawn();
  cb.add(s, LifeCounter{2});
  cb.destroy(s);

  w.flush(cb);

  CHECK_FALSE(w.isAlive(e));
  CHECK_FALSE(w.isAlive(s));
  CHECK(LifeCounter::alive == 0);

  auto n = w.create();
  CHECK(n.index == s.index);
  CHECK(n.generation == s.generation + 1);
  CHECK_INV(w);
}

TEST_CASE("R-CB-3: uma única transição por entidade") {
  ecs::World w;
  auto e = w.create();
  auto cb = w.commands();
  cb.add(e, A{1});
  cb.add(e, B{2, 3});
  cb.add(e, Tag{});
  auto before = w.archetypeCount();

  w.flush(cb);

  CHECK(w.archetypeCount() == before + 1); // só {A,B,Tag}; nenhum intermediário
  CHECK(w.get<B>(e).w == 3);
  CHECK(w.has<Tag>(e));
  CHECK_INV(w);
}

TEST_CASE("flush: entidade com dados nasce direto no arquétipo final") {
  ecs::World w;
  auto cb = w.commands();
  auto s = cb.spawn();
  cb.add(s, A{1});
  cb.add(s, B{2, 3});
  auto before = w.archetypeCount();

  w.flush(cb);

  CHECK(w.archetypeCount() == before + 1);
  CHECK(w.get<A>(s).v == 1);
  CHECK_INV(w);
}

TEST_CASE("comandos para handle destruído diretamente são ignorados") {
  ecs::World w;
  auto e = w.create(A{1});
  auto cb = w.commands();
  cb.add(e, B{0, 0});
  w.destroy(e);
  auto reused = w.create(A{9}); // mesmo índice, geração nova
  CHECK(reused.index == e.index);

  w.flush(cb);

  CHECK_FALSE(w.has<B>(reused));
  CHECK(w.get<A>(reused).v == 9);
  CHECK_INV(w);
}

TEST_CASE("spawn+destroy no mesmo buffer devolve o índice com geração + 1") {
  ecs::World w;
  auto cb = w.commands();
  auto s = cb.spawn();
  cb.destroy(s);
  w.flush(cb);

  auto again = w.create();

  CHECK(again.index == s.index);
  CHECK(again.generation == s.generation + 1);
}

TEST_CASE("fuzz: flush coalescido == flushSequential") {
  LifeCounter::alive = 0;
  {
    ecs::World coalesced, sequential;
    std::vector<std::pair<ecs::Entity, ecs::Entity>> live; // handles lógicos
    std::mt19937_64 rng(fuzzSeed() ^ 0xBADC0DE);
    uint64_t counter = 0, total = 0;

    auto compare = [&] {
      for (auto& [e1, e2] : live) {
        REQUIRE(coalesced.isAlive(e1));
        REQUIRE(sequential.isAlive(e2));
        for (int i = 0; i < 5; ++i) {
          const auto cid = fuzz::universe()[i];
          uint64_t v1 = 0, v2 = 0;
          bool h1 = readFromWorld(coalesced, e1, cid, v1), h2 = readFromWorld(sequential, e2, cid, v2);
          if (h1 != h2 || v1 != v2)
            FAIL("divergência em " << ecs::typeInfo(cid).name << " e1=" << e1.index);
        }
      }
    };

    while (total < 30000) {
      auto cbA = coalesced.commands();
      auto cbB = sequential.commands();
      const unsigned n = 1 + unsigned(rng() % 64);
      std::vector<std::pair<ecs::Entity, ecs::Entity>> destroyedNow;
      for (unsigned k = 0; k < n; ++k, ++total) {
        const unsigned r = unsigned(rng() % 100);
        if (r < 15 || live.empty()) {
          live.emplace_back(cbA.spawn(), cbB.spawn());
        } else if (r < 25) {
          size_t i = rng() % live.size();
          auto [e1, e2] = live[i];
          cbA.destroy(e1);
          cbB.destroy(e2);
          destroyedNow.push_back(live[i]);
          live[i] = live.back(); // sem comandos depois de destroy
          live.pop_back();
        } else if (r < 70) {
          size_t i = rng() % live.size();
          auto [e1, e2] = live[i];
          ecs::ComponentId cid = fuzz::universe()[rng() % 5];
          uint64_t v = fuzz::isTag(cid) ? 0 : makeValue(ecs::Entity{uint32_t(i), 0}, ++counter);
          cbAdd(cbA, e1, cid, v);
          cbAdd(cbB, e2, cid, v);
        } else {
          size_t i = rng() % live.size();
          ecs::ComponentId cid = fuzz::universe()[rng() % 5];
          cbA.removeRaw(live[i].first, cid);
          cbB.removeRaw(live[i].second, cid);
        }
      }

      coalesced.flush(cbA);
      sequential.flushSequential(cbB);

      compare();
      CHECK_INV(coalesced);
      CHECK_INV(sequential);
      for (auto& [e1, e2] : destroyedNow) {
        CHECK_FALSE(coalesced.isAlive(e1));
        CHECK_FALSE(sequential.isAlive(e2));
      }
    }
  }
  CHECK(LifeCounter::alive == 0);
}

TEST_CASE("R-CB-3: comandos intercalados entre entidades ainda coalescem por entidade") {
  ecs::World w;
  auto e1 = w.create();
  auto e2 = w.create();
  auto cb = w.commands();
  cb.add(e1, A{1});
  cb.add(e2, A{2});
  cb.add(e1, B{3, 4}); // e1 volta fora do trecho consecutivo
  cb.add(e2, B{5, 6});
  cb.add(e1, A{7});
  auto before = w.archetypeCount();

  w.flush(cb);

  CHECK(w.archetypeCount() == before + 1); // só {A,B}
  CHECK(w.get<A>(e1).v == 7);
  CHECK(w.get<B>(e1).w == 4);
  CHECK(w.get<A>(e2).v == 2);
  CHECK(w.get<B>(e2).w == 6);
  CHECK_INV(w);
}

TEST_CASE("buffer reutilizado depois do flush agrupa de novo") {
  ecs::World w;
  auto e1 = w.create();
  auto e2 = w.create();
  auto cb = w.commands();
  cb.add(e1, A{1});
  cb.add(e2, A{2});
  cb.add(e1, B{0, 0}); // não consecutivo: o primeiro lote cai no caminho geral
  w.flush(cb);

  cb.add(e2, B{8, 9}); // segundo lote: o estado de agrupamento foi zerado pelo flush
  cb.add(e1, A{5});
  w.flush(cb);

  CHECK(w.get<A>(e1).v == 5);
  CHECK(w.get<B>(e2).w == 9);
  CHECK(w.get<A>(e2).v == 2);
  CHECK_INV(w);
}
