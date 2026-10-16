# ECS por Arquétipos v1 — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Entregar a biblioteca `ecs` (C++20, single-thread, por arquétipos) com testes, invariantes verificáveis e benchmarks que provem as metas NF-P1…NF-P5, fechando os marcos M0…M5 da spec.

**Architecture:** Biblioteca estática `ecs` em quatro camadas (`type_info → entity → table → archetype → query / command_buffer → world`). Templates finos em headers delegam para funções não template em `src/*.cpp` que recebem `ComponentId`. O `World` é o único dono dos records e o único que chama `fillHole`. Testes com doctest (um executável por suíte), benchmarks com Google Benchmark; tudo compila e roda dentro do container Docker do repositório.

**Tech Stack:** C++20, GCC 14 (imagem `gcc:14`), CMake ≥ 3.25 com presets, doctest 2.4.11, Google Benchmark 1.8.3 (ambos via `FetchContent`), Python 3 (só para comparar JSON de benchmark), `perf` (M5).

**Spec:** `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md` (cópia de `~/Downloads/spec-ecs-archetype.md`). Os IDs `R-*`, `I-*` e `NF-*` citados abaixo são dessa spec.

## Global Constraints

- C++20 (`std::span`, concepts, `std::bit_cast`); compilador-alvo GCC 13+ (usamos GCC 14); Clang 17+ desejável; MSVC não bloqueia.
- Namespace `ecs`; headers em `include/ecs/`; tipos `PascalCase`, funções `camelCase`.
- Um único `World` por thread, nenhum acesso concorrente.
- Componentes: `static_assert(std::is_nothrow_move_constructible_v<T>)` e `static_assert(alignof(T) <= 64)` (R-TI-3).
- Sem exceções no caminho quente. Violação de contrato → `ECS_ASSERT` em debug, UB em release. `ECS_ENABLE_ASSERTS=0` remove os asserts (NF-S4).
- Mensagem de `ECS_ASSERT` inclui operação, `Entity` (como `index:generation`) e nome do tipo quando houver (NF-S2).
- A biblioteca não tem dependências externas; doctest e Google Benchmark só em `tests/` e `bench/`, via `FetchContent` (NF-B3).
- CMake 3.25+, presets `debug-asan`, `release`, `relwithdebinfo` (NF-B1). Nossos alvos compilam com `-Wall -Wextra -Wpedantic -Werror` (NF-B2); dependências de terceiros não recebem essas flags.
- Todo código de teste roda limpo sob ASan + UBSan no preset `debug-asan` (NF-S1).
- Overhead por entidade: 16 bytes de `EntityRecord` + 8 bytes de `Entity` na tabela; nenhuma alocação por entidade (NF-M1). `static_assert(sizeof(EntityRecord) == 16)`.
- Colunas alinhadas a `max(info->align, 64)` (R-TB-3 / NF-M3). Crescimento ×2 com mínimo de 64 linhas (R-TB-2).
- Metas de desempenho são razões contra uma linha de base medida na mesma máquina, mediana de ≥ 10 repetições, resultados em JSON (`--benchmark_out`) em `bench/results/`.
- Commits terminam com a linha `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Trabalhar no branch `feat/ecs-v1` (não em `master`).

### Como rodar comandos

Todo comando de build/teste roda no container. Abreviação usada no plano:

```bash
DC="docker compose run --rm dev"
$DC make test PRESET=debug-asan T=unit_table   # configura, compila e roda ctest -R unit_table
```

Um caso doctest específico: `$DC ./build/debug-asan/tests/unit_table -tc="<nome do caso>"`.

### Decisões que a spec deixa em aberto (fixadas aqui)

- **Questão 1 (`create<Ts...>`)**: aceita forwarding; lvalues são copiados (`std::remove_cvref_t<Ts>(std::forward<Ts>(c))`). `static_assert` de tipos distintos (NF-S3).
- **Questão 2 (identidade da query)**: por tipo. Filtros são parâmetros de tipo na chamada: `world.query<Ts...>(ecs::With<Us...>{}, ecs::Without<Xs...>{})`. A spec mostra `Query& without(ComponentId...)` como método mutável; isso quebraria o cache (duas chamadas com filtros diferentes devolveriam a mesma instância), então os filtros entram na chave do cache. `Query` não tem métodos `with`/`without`.
- **Questão 3 (destroy direto com comandos pendentes)**: o flush detecta o handle obsoleto pela geração e ignora os comandos daquela entidade (opção mais barata).
- **Questão 4 (`get<Tag>`)**: erro de compilação.
- **`add<T>` sobre `T` existente** também exige `iterating_ == 0` (assert), mesmo não sendo estrutural; R-API-3 proíbe e o teste de morte "add durante each" depende disso.
- **Buffer do `CommandBuffer`**: não é `std::vector<std::byte>`. Crescer um `vector<byte>` copia bytes de payloads não triviais (ex.: `std::string` com SSO aponta para si mesma) — UB. O buffer é um bloco próprio alinhado a 64 que, ao crescer, percorre os registros e realoca cada payload com `TypeInfo` (memcpy se trivial, `moveConstruct`+`destroy` caso contrário). O layout de registro é o da spec.
- **`Archetype::mask`** (opcional na spec, R-AR-4): não implementado na v1; matching usa `std::includes`.
- **`Table::shrinkToFit`** (opcional, R-TB-6): não implementado.
- **Records e freeList** ficam numa classe `EntityStore` em `entity.hpp`, para que `CommandBuffer` (camada abaixo do `World`) possa reservar índices (R-CB-1) sem conhecer o `World`.
- **Benchmark "sem memcpy"**: em vez de uma flag na biblioteca, usa um wrapper `NonTrivial<T>` com move-constructor e destrutor definidos pelo usuário (mesmos bytes, caminho não trivial).
- **Teste de morte**: doctest não tem death tests. `contract_death` é um executável próprio: instala um handler de assert que imprime e chama `std::_Exit(0)`; se o caso terminar sem assert, retorna 1. Cada caso é um teste CTest separado (`contract_death <caso>`). Só é compilado com asserts ligados.

## Review Focus

1. **Payload não trivial sobrevivendo ao crescimento do `CommandBuffer`** — gravar um `add<std::string>` com string longa e depois centenas de comandos que forçam realocação; após o flush a string tem o valor original e ASan não reclama. Teste em Task 15.
2. **`add` com valor que aponta para dentro do próprio `World`** — `w.add<B>(e, w.get<B>(other))` onde `other` vive no arquétipo destino e a tabela destino realoca no `pushUninit`. Esperado: `e` recebe o valor de `other`, sem use-after-free. Mitigação da spec: construir o temporário antes da mudança estrutural. Teste em Task 7.
3. **Remoção da última linha / entidade deslocada é a própria** — `destroy`/`remove` na entidade que ocupa a última linha (inclusive a única do arquétipo): `fillHole` devolve `nullopt`, nenhum record é tocado indevidamente. Teste em Task 7.
4. **Handles obsoletos** — handle antigo após reciclagem do índice: `isAlive` falso, `tryGet` devolve `nullptr`, comandos de buffer para ele são ignorados no flush; slot com geração `UINT32_MAX` nunca é reutilizado. Testes em Tasks 4 e 16.
5. **Exceção lançada pelo lambda de `each`** — `iterating_` volta a 0 (guard RAII) e mudanças estruturais seguintes funcionam (I-11). Teste em Task 13.

---

## Mapa de arquivos

```
CMakeLists.txt  CMakePresets.json  Makefile  Dockerfile  docker-compose.yaml
include/ecs/
  detail/assert.hpp      ECS_ASSERT, setAssertHandler
  detail/type_name.hpp   typeName<T>() via __PRETTY_FUNCTION__
  detail/small_map.hpp   SmallMap<K,V> (vetor ordenado)
  detail/align.hpp       alignUp, alignedAlloc/alignedFree
  type_info.hpp          ComponentId, TypeInfo, componentId<T>(), typeInfo()
  entity.hpp             Entity, NullEntity, EntityRecord, EntityStore
  table.hpp              Column, Table, relocateValue, destroyValue
  archetype.hpp          ArchetypeId, NoColumn, Signature, SigHash, Edge, Archetype, buildEdge
  query.hpp              With, Without, QueryCacheBase, MatchedTable, Query<Ts...>
  command_buffer.hpp     CommandBuffer, CommandOp, NetComponentOp, EntityPlan
  world.hpp              World, WorldStats, ArchetypeStats
src/ assert.cpp type_info.cpp table.cpp archetype.cpp world.cpp command_buffer.cpp
tests/
  CMakeLists.txt
  support/ doctest_main.cpp components.hpp (LifeCounter e tipos de teste) alloc_counter.cpp model.hpp
  unit_type_info.cpp unit_entity.cpp unit_table.cpp unit_archetype.cpp unit_world.cpp
  invariants.cpp fuzz_vs_model.cpp nontrivial.cpp queries.cpp command_buffer.cpp contract_death.cpp
bench/ CMakeLists.txt common.hpp iterate.cpp transitions.cpp fragmentation.cpp flush.cpp results/
tools/ bench_ratio.py perf_fragmented.sh
```

`src/main.cpp` é removido (a biblioteca não tem executável).

---

## M0 · Infraestrutura

### Task 1: Build, presets, container e `ECS_ASSERT`

**Files:**
- Modify: `CMakeLists.txt` (reescrever), `Dockerfile`, `docker-compose.yaml`, `Makefile`, `.gitignore`
- Delete: `src/main.cpp`
- Create: `CMakePresets.json`, `include/ecs/detail/assert.hpp`, `src/assert.cpp`, `tests/CMakeLists.txt`, `tests/support/doctest_main.cpp`, `tests/unit_table.cpp`, `bench/CMakeLists.txt`

**Interfaces:**
- Produces:
  - Alvo CMake `ecs` (STATIC, `target_include_directories(ecs PUBLIC include)`, `cxx_std_20`).
  - Função CMake `ecs_add_test(<nome> <fontes...>)`: cria executável `<nome>` em `build/<preset>/tests/`, linka `ecs`, `doctest::doctest` e `tests/support/doctest_main.cpp`, registra `add_test(NAME <nome> ...)`.
  - Função CMake `ecs_add_bench(<nome> <fontes...>)`: executável em `build/<preset>/bench/`, linka `ecs` e `benchmark::benchmark`.
  - Opções: `ECS_BUILD_TESTS`, `ECS_BUILD_BENCH`, `ECS_SANITIZE` (adiciona `-fsanitize=address,undefined -fno-omit-frame-pointer` a compile e link), `ECS_WERROR`, `ECS_ENABLE_ASSERTS` (string; vazio = `0` se `NDEBUG`, senão `1`; passado como `target_compile_definitions(ecs PUBLIC ECS_ENABLE_ASSERTS=...)`).
  - `ECS_ASSERT(cond, fmt, ...)` (printf-style; expande para nada quando `ECS_ENABLE_ASSERTS == 0`, sem avaliar `cond`).
  - `namespace ecs { using AssertHandler = void(*)(const char* message) noexcept; AssertHandler setAssertHandler(AssertHandler) noexcept; }` — devolve o anterior. Handler padrão: escreve a mensagem em `stderr` e chama `std::abort()`.
  - `[[noreturn]] void ecs::detail::assertFail(const char* expr, const char* file, int line, const char* fmt, ...) noexcept` — formata `"ECS_ASSERT failed: <expr> | <msg> (<file>:<line>)"` num buffer de 512 bytes e chama o handler; se o handler retornar, `std::abort()`.
  - Makefile: `PRESET ?= debug-asan`, `T ?= .`; alvos `configure` (`cmake --preset $(PRESET)`), `build`, `test` (`ctest --preset $(PRESET) -R '$(T)' --output-on-failure`), `bench` (força `PRESET=release`).

- [ ] **Step 1: Criar o branch**

```bash
git checkout -b feat/ecs-v1
```

- [ ] **Step 2: Container**

`Dockerfile`: `FROM gcc:14`; instalar `cmake git python3 linux-perf` (`--no-install-recommends`). `docker-compose.yaml`: manter `build`/`run` e adicionar serviço `dev` (mesmos volumes, `command: bash`) e serviço `perf` igual a `dev` com `privileged: true`.

Run: `docker compose build dev && $DC cmake --version`
Expected: `cmake version 3.25` ou superior. Se for menor, instalar CMake do Kitware no Dockerfile antes de seguir.

- [ ] **Step 3: Escrever o smoke test**

```cpp
// tests/unit_table.cpp
#include <doctest/doctest.h>
#include <ecs/detail/assert.hpp>

TEST_CASE("smoke: asserts ligados em debug") {
#ifdef NDEBUG
    CHECK(ECS_ENABLE_ASSERTS == 0);
#else
    CHECK(ECS_ENABLE_ASSERTS == 1);
#endif
    ECS_ASSERT(1 + 1 == 2, "op=%s", "smoke");
}
```

- [ ] **Step 4: Rodar e ver falhar**

Run: `$DC make test PRESET=debug-asan T=unit_table`
Expected: FAIL na configuração (preset ou alvo inexistente).

- [ ] **Step 5: Implementar CMake, presets, Makefile, `assert.hpp`/`assert.cpp`**

Presets (todos com `binaryDir: build/${presetName}`, `CMAKE_CXX_STANDARD 20`, `ECS_WERROR ON`, e um `testPreset` de mesmo nome com `output.outputOnFailure`):

| preset | `CMAKE_BUILD_TYPE` | `ECS_SANITIZE` | `ECS_BUILD_TESTS` | `ECS_BUILD_BENCH` | `ECS_ENABLE_ASSERTS` |
| --- | --- | --- | --- | --- | --- |
| `debug-asan` | Debug | ON | ON | OFF | (padrão → 1) |
| `release` | Release | OFF | ON | ON | 0 |
| `relwithdebinfo` | RelWithDebInfo | OFF | OFF | ON | 0 |

doctest via `FetchContent` (tag `v2.4.11`), Google Benchmark (tag `v1.8.3`, com `BENCHMARK_ENABLE_TESTING OFF`, `BENCHMARK_ENABLE_GTEST_TESTS OFF`). `.gitignore` mantém `build`.

- [ ] **Step 6: Rodar e ver passar**

Run: `$DC make test PRESET=debug-asan T=unit_table && $DC make test PRESET=release T=unit_table`
Expected: `100% tests passed` nos dois.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "build: presets CMake, container dev e ECS_ASSERT"
```

---

### Task 2: Linha de base SoA e comparador de benchmarks

**Files:**
- Create: `bench/common.hpp`, `bench/iterate.cpp`, `tools/bench_ratio.py`, `bench/results/m0_iterate_single_soa.json`
- Modify: `bench/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `bench/common.hpp`: `struct float3 { float x, y, z; };` `struct Position { float3 v; };` `struct Velocity { float3 v; };` `inline constexpr float kDt = 1.0f / 60.0f;`
  - Benchmark `BM_IterateSingle_SoA` com `Arg` em {1000, 10000, 100000, 1000000}, `state.counters["per_entity"]` = `benchmark::Counter(n, kIsIterationInvariantRate | kInvert)` (tempo por entidade). Executável `bench_iterate`.
  - `tools/bench_ratio.py --json <arquivo> --name <benchmark> --baseline <benchmark> --max <razão>`: lê o agregado `<nome>_median` (`real_time`) de cada um, imprime `ratio=<x>` e sai com 0 se `ratio <= max`, 1 caso contrário, 2 se algum nome não existir. Rodar sempre com `python3 -I`.

- [ ] **Step 1: Escrever o benchmark SoA** — dois `std::vector<float3>` de tamanho `n`, loop `pos[i] += vel[i] * kDt` componente a componente, `benchmark::DoNotOptimize(pos.data())`.

- [ ] **Step 2: Rodar e gravar JSON**

Run: `$DC make build PRESET=release && $DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m0_iterate_single_soa.json`
Expected: 4 linhas `_median` para `BM_IterateSingle_SoA/<n>`.

- [ ] **Step 3: Verificar o comparador contra o próprio JSON**

Run: `$DC python3 -I tools/bench_ratio.py --json bench/results/m0_iterate_single_soa.json --name BM_IterateSingle_SoA/1000000 --baseline BM_IterateSingle_SoA/1000000 --max 1.0; echo $?`
Expected: `ratio=1.000`, exit 0. Com `--max 0.5`: exit 1. Com `--name inexistente`: exit 2.

- [ ] **Step 4: Commit** — `git add bench tools && git commit -m "bench: linha de base SoA de iterate_single (M0)"`

**Gate M0:** presets, FetchContent e baseline SoA em JSON — fechado por Tasks 1–2.

---

## M1 · Núcleo

### Task 3: `type_info`

**Files:**
- Create: `include/ecs/type_info.hpp`, `include/ecs/detail/type_name.hpp`, `src/type_info.cpp`, `tests/unit_type_info.cpp`, `tests/support/components.hpp`
- Modify: `tests/CMakeLists.txt`, `CMakeLists.txt` (fontes de `ecs`)

**Interfaces:**
- Produces:
  - `using ComponentId = uint32_t;` `struct TypeInfo` exatamente como na seção 4.1 da spec.
  - `template<class T> ComponentId componentId();` — `using U = std::remove_cvref_t<T>`; `static_assert`s de R-TI-3; `static const ComponentId id = detail::registerType(detail::makeTypeInfo<U>()); return id;` (R-TI-4). Para tags (`std::is_empty_v<U>`): `size = 0`, `tag = true`, `trivial = true`.
  - `const TypeInfo& typeInfo(ComponentId);` `uint32_t registeredTypeCount() noexcept;`
  - `ComponentId detail::registerType(const TypeInfo&);` — registro em `std::deque<TypeInfo>` dentro de função com `static` local (referências estáveis).
  - `template<class T> constexpr std::string_view detail::typeName();` — extraído de `__PRETTY_FUNCTION__` (GCC/Clang) / `__FUNCSIG__` (MSVC), ex.: `"Position"`.
  - `tests/support/components.hpp`: `struct A { uint64_t v; }; struct B { uint64_t v, w; }; struct Tag {}; struct alignas(64) Wide { float f[16]; };` e `struct LifeCounter { static inline int64_t alive = 0; uint64_t v = 0; /* ++ em todo ctor, -- no dtor, move noexcept */ };`

- [ ] **Step 1: Testes que falham**

```cpp
TEST_CASE("componentId é estável, denso e ignora cvref") {
    auto a = ecs::componentId<A>();
    CHECK(ecs::componentId<A>() == a);
    CHECK(ecs::componentId<const A&>() == a);
    auto b = ecs::componentId<B>();
    CHECK(b != a);
    CHECK(std::max(a, b) < ecs::registeredTypeCount());
}
TEST_CASE("TypeInfo de trivial, não trivial e tag") {
    auto& ia = ecs::typeInfo(ecs::componentId<A>());
    CHECK(ia.size == sizeof(A)); CHECK(ia.trivial); CHECK_FALSE(ia.tag);
    CHECK(ia.moveConstruct == nullptr); CHECK(ia.destroy == nullptr);
    auto& is = ecs::typeInfo(ecs::componentId<std::string>());
    CHECK_FALSE(is.trivial); CHECK(is.moveConstruct != nullptr); CHECK(is.destroy != nullptr);
    auto& it = ecs::typeInfo(ecs::componentId<Tag>());
    CHECK(it.tag); CHECK(it.size == 0);
    CHECK(ecs::typeInfo(ecs::componentId<Wide>()).align == 64);
    CHECK(ia.name == "A");
}
```

- [ ] **Step 2: Rodar e ver falhar** — `$DC make test T=unit_type_info` → FAIL (header inexistente).
- [ ] **Step 3: Implementar** conforme Interfaces.
- [ ] **Step 4: Rodar e ver passar** — `$DC make test T=unit_type_info` → PASS.
- [ ] **Step 5: Commit** — `feat(type_info): componentId<T> e TypeInfo`

---

### Task 4: `entity` e `EntityStore`

**Files:**
- Create: `include/ecs/entity.hpp`, `tests/unit_entity.cpp`

**Interfaces:**
- Consumes: nada além de `<cstdint>`; `struct Archetype;` declarado adiante.
- Produces:
  - `Entity`, `NullEntity`, `EntityRecord` exatamente como na seção 4.2; `static_assert(sizeof(EntityRecord) == 16)`; `struct EntityHash` (para `unordered_map<Entity, …>` nos testes).
  - `class EntityStore` (header-only):
    - `Entity allocate();` — pega do fim da `freeList` (LIFO) ou cria slot novo com geração 0; deixa `arch = nullptr` (estado reservado, R-EN-3); devolve `{index, generation}`.
    - `void release(uint32_t index) noexcept;` — `arch = nullptr`, `++generation`; se `generation == UINT32_MAX` o slot é aposentado (não volta à `freeList`), senão `freeList.push_back(index)` (R-EN-1).
    - `bool isAlive(Entity) const noexcept;` — fórmula de R-EN-2.
    - `bool isReserved(Entity) const noexcept;` — índice válido, geração igual e `arch == nullptr`.
    - `EntityRecord& record(uint32_t index) noexcept; const EntityRecord& record(uint32_t) const noexcept;`
    - `uint32_t slotCount() const noexcept; std::span<const uint32_t> freeList() const noexcept;`
    - `void setGenerationForTest(uint32_t index, uint32_t gen) noexcept;`

- [ ] **Step 1: Testes que falham**

```cpp
TEST_CASE("reciclagem incrementa geração e invalida handle antigo") {
    ecs::EntityStore s; ecs::Archetype* fake = reinterpret_cast<ecs::Archetype*>(0x10);
    auto e = s.allocate();
    CHECK(s.isReserved(e)); CHECK_FALSE(s.isAlive(e));
    s.record(e.index).arch = fake;
    CHECK(s.isAlive(e));
    s.release(e.index);
    CHECK_FALSE(s.isAlive(e));
    auto e2 = s.allocate();
    CHECK(e2.index == e.index); CHECK(e2.generation == e.generation + 1);
    s.record(e2.index).arch = fake;
    CHECK_FALSE(s.isAlive(e)); CHECK(s.isAlive(e2));
}
TEST_CASE("slot com geração UINT32_MAX é aposentado") {
    ecs::EntityStore s;
    auto e = s.allocate();
    s.setGenerationForTest(e.index, UINT32_MAX - 1);
    s.release(e.index);
    CHECK(s.freeList().empty());
    CHECK(s.allocate().index != e.index);
}
TEST_CASE("NullEntity nunca está vivo") { ecs::EntityStore s; CHECK_FALSE(s.isAlive(ecs::NullEntity)); }
```

- [ ] **Step 2: Ver falhar** — `$DC make test T=unit_entity` → FAIL.
- [ ] **Step 3: Implementar** conforme Interfaces.
- [ ] **Step 4: Ver passar** — PASS.
- [ ] **Step 5: Commit** — `feat(entity): Entity, EntityRecord e EntityStore geracional`

---

### Task 5: `Table`

**Files:**
- Create: `include/ecs/table.hpp`, `include/ecs/detail/align.hpp`, `src/table.cpp`
- Modify: `tests/unit_table.cpp`

**Interfaces:**
- Consumes: `TypeInfo`, `typeInfo()`, `Entity`.
- Produces:
  - `Column` e `Table` com a API pública exata da seção 4.3, mais `uint32_t capacity() const noexcept;` e `std::span<const ComponentId> columnTypes() const noexcept;`.
  - `void relocateValue(const TypeInfo&, void* dst, void* src) noexcept;` — `memcpy` se `trivial`, senão `moveConstruct` + `destroy`.
  - `void destroyValue(const TypeInfo&, void* p) noexcept;` — no-op se `trivial`.
  - `detail/align.hpp`: `constexpr size_t alignUp(size_t v, size_t a) noexcept;` `std::byte* alignedAlloc(size_t bytes, size_t align);` `void alignedFree(std::byte*, size_t align) noexcept;` (via `::operator new(bytes, std::align_val_t)`).
- Regras pinadas: crescimento `newCap = max(64, capacity*2)`; `reserve` também faz `entities_.reserve(newCap)` (necessário para NF-P4); trivial = um `memcpy` por coluna, não trivial = `moveConstruct`+`destroy` por elemento (R-TB-2); `fillHole` conforme R-TB-5 (usa `relocateValue` da última linha para `r`, `entities_[r] = entities_.back()`, `pop_back`, devolve a entidade deslocada ou `nullopt` se `r` era a última); construtor faz `ECS_ASSERT` de que nenhum tipo é tag.

- [ ] **Step 1: Testes que falham** (substituir o smoke; cada passo verifica I-1 e I-2)

```cpp
static void construct(ecs::Table& t, uint32_t row, uint64_t v);  // placement-new A{v} na col 0 e LifeCounter{v} na col 1
static void checkI1I2(const ecs::Table& t) {
    CHECK(t.entities().size() == t.count());
    CHECK(t.capacity() >= t.count());
    CHECK(LifeCounter::alive == t.count());
}
TEST_CASE("push, destroyRow, fillHole com swap e realocação") {
    LifeCounter::alive = 0;
    {
        ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<LifeCounter>()};
        std::sort(std::begin(ids), std::end(ids));
        ecs::Table t(ids);
        for (uint32_t i = 0; i < 200; ++i) { auto r = t.pushUninit({i, 0}); construct(t, r, i); checkI1I2(t); }
        CHECK(t.capacity() == 256);                       // 64 → 128 → 256
        t.destroyRow(10);
        auto moved = t.fillHole(10);
        REQUIRE(moved); CHECK(moved->index == 199);
        CHECK(t.entities()[10].index == 199);
        CHECK(valueA(t, 10) == 199);                      // valor acompanhou a entidade
        checkI1I2(t);
        t.destroyRow(t.count() - 1);
        CHECK_FALSE(t.fillHole(t.count() - 1));           // última linha: nada deslocado
        checkI1I2(t);
    }
    CHECK(LifeCounter::alive == 0);                       // destrutor destrói linhas vivas
}
TEST_CASE("colunas alinhadas a 64, inclusive alignas(64)") {
    ecs::ComponentId ids[] = {ecs::componentId<A>(), ecs::componentId<Wide>()};
    std::sort(std::begin(ids), std::end(ids));
    ecs::Table t(ids); t.pushUninit({0, 0});
    for (uint16_t c = 0; c < t.columnCount(); ++c)
        CHECK(reinterpret_cast<uintptr_t>(t.column(c).data) % 64 == 0);
}
TEST_CASE("std::string sobrevive à realocação") { /* 100 strings longas (>32 chars), conferir todas após crescer */ }
```

(Ordenar a coluna-alvo pelo índice retornado por `std::lower_bound` em `columnTypes()`; os helpers `construct`/`valueA` fazem isso.)

- [ ] **Step 2: Ver falhar** — `$DC make test T=unit_table` → FAIL.
- [ ] **Step 3: Implementar** `table.hpp`/`table.cpp`/`align.hpp`.
- [ ] **Step 4: Ver passar** — PASS, sem relatório de ASan.
- [ ] **Step 5: Commit** — `feat(table): tabela SoA com crescimento conjunto e fillHole`

---

### Task 6: `Archetype`, `Signature`, `SmallMap` e `buildEdge`

**Files:**
- Create: `include/ecs/archetype.hpp`, `include/ecs/detail/small_map.hpp`, `src/archetype.cpp`, `tests/unit_archetype.cpp`

**Interfaces:**
- Consumes: `Table`, `TypeInfo`.
- Produces:
  - `using ArchetypeId = uint32_t; using Signature = std::vector<ComponentId>; inline constexpr uint16_t NoColumn = UINT16_MAX;`
  - `struct SigHash { size_t operator()(const Signature&) const noexcept; };` — FNV-1a 64 sobre os IDs.
  - `template<class K, class V> class SmallMap` — vetor ordenado de `std::pair<K,V>`: `V* find(K) noexcept`, `const V* find(K) const noexcept`, `V& insert(K, V)` (`ECS_ASSERT` de chave ausente; pode invalidar referências), `size()`, `begin()/end()`, `capacityBytes()`.
  - `Edge` como na seção 4.4. `Archetype` como na seção 4.4 **sem** `mask`, com construtor `Archetype(ArchetypeId, Signature)` que deriva `columnTypes` (não tags) e constrói `table(columnTypes)`.
  - `Edge buildEdge(const Archetype& src, Archetype& dst, ComponentId added);` — merge O(k) das duas `columnTypes` (R-AR-2); `addedColumn` = coluna de `added` em `dst` se `added` tem dados e não está em `src`, senão `-1`. Para remoção ou salto arbitrário, passar `added = NoComponent` onde `inline constexpr ComponentId NoComponent = UINT32_MAX;`.
  - `uint16_t columnOf(const Archetype&, ComponentId) noexcept;` — `NoColumn` se ausente ou tag (busca binária em `columnTypes`).

- [ ] **Step 1: Testes que falham**

```cpp
TEST_CASE("columnTypes exclui tags; signature ordenada inclui") {
    ecs::Signature sig{ecs::componentId<A>(), ecs::componentId<Tag>(), ecs::componentId<B>()};
    std::sort(sig.begin(), sig.end());
    ecs::Archetype a(1, sig);
    CHECK(a.signature == sig);
    CHECK(a.columnTypes.size() == 2);
    CHECK(a.table.columnCount() == 2);
    CHECK(ecs::columnOf(a, ecs::componentId<Tag>()) == ecs::NoColumn);
}
TEST_CASE("buildEdge mapeia colunas por merge") {
    // src = {A, B}, dst = {A, B, C(dados)}: srcToDst == {0,1} na ordem de columnTypes, addedColumn == coluna de C
    // src = {A, B}, dst = {B}: srcToDst[col de A] == -1
}
TEST_CASE("SmallMap mantém ordem e encontra") { /* insere 5,1,3; begin()->first == 1; find(3) != nullptr; find(2) == nullptr */ }
TEST_CASE("SigHash difere para assinaturas diferentes") { CHECK(ecs::SigHash{}({1,2}) != ecs::SigHash{}({2,1,0})); }
```

(No teste de `buildEdge` declarar `struct C { uint32_t v; };` local e escrever os valores esperados a partir de `columnOf`.)

- [ ] **Step 2: Ver falhar** — `$DC make test T=unit_archetype` → FAIL.
- [ ] **Step 3: Implementar.**
- [ ] **Step 4: Ver passar.**
- [ ] **Step 5: Commit** — `feat(archetype): Archetype, SmallMap e buildEdge por merge`

---

### Task 7: `World` mínimo (sem arestas em cache)

**Files:**
- Create: `include/ecs/world.hpp`, `src/world.cpp`, `tests/unit_world.cpp`

**Interfaces:**
- Consumes: Tasks 3–6.
- Produces (API pública da seção 5, exceto `query`, `commands`, `flush`, `stats`, `checkInvariants`, que chegam depois):
  - `World(); ~World();` não copiável. Construtor cria o arquétipo raiz `id == 0` (R-AR-3).
  - `Entity create(); template<class... Ts> Entity create(Ts&&...); void destroy(Entity); bool isAlive(Entity) const noexcept;`
  - `template<class T> void add(Entity, T&&); template<class T> void remove(Entity); template<class T> bool has(Entity) const noexcept; template<class T> T& get(Entity); template<class T> T* tryGet(Entity) noexcept;`
  - Núcleo não template (públicos, usados por `flushSequential` e testes): `void addRaw(Entity, ComponentId, void* value);` (move de `value`; se já tem o componente: `destroyValue` + `moveConstruct`/`memcpy` no lugar; para tag `value` é ignorado), `void removeRaw(Entity, ComponentId);`, `void* getRaw(Entity, ComponentId) noexcept;` (`nullptr` se ausente/morto), `bool hasRaw(Entity, ComponentId) const noexcept;`
  - Privados com nomes fixos (usados por Tasks 8, 10, 16, 19): `Archetype* findOrCreate(Signature sig);` (lookup em `bySignature_`; se cria: `archetypes_.push_back`, insere em `bySignature_`, atualiza `componentIndex_[cid].insert(id, columnOf(...))` para todo `cid` da assinatura — R-WI-1 — e, a partir da Task 12, notifica `queries_`), `uint32_t moveEntity(Entity, EntityRecord&, Archetype& dst, const Edge&);` e os membros de 4.5 com `records_`/`freeList_` substituídos por `EntityStore store_;`.
  - `moveEntity` (pinado): `dstRow = dst.table.pushUninit(e)`; para cada coluna `c` de origem: se `srcToDst[c] >= 0` → `relocateValue` para `dst.column(srcToDst[c]).at(dstRow)`, senão `destroyValue`; `src.table.fillHole(srcRow)` e, se devolver entidade deslocada `d`, `store_.record(d.index).row = srcRow`; atualiza `rec = {&dst, dstRow, rec.generation}`; devolve `dstRow`. A coluna `edge.addedColumn` fica para o chamador construir.
  - `add<T>` (pinado): `using U = remove_cvref_t<T>`; `ECS_ASSERT(isAlive(e) && iterating_ == 0, "add e=%u:%u type=%s", …)`; para não tag, construir `U tmp(std::forward<T>(value))` **antes** de qualquer mudança estrutural e chamar `addRaw(e, cid, &tmp)`.
  - `get<T>`: `static_assert(!std::is_empty_v<U>)` (questão 4); custo conforme R-WI-2; `ECS_ASSERT(isAlive(e) && has, "get e=%u:%u type=%s", …)`.
  - `create<Ts...>`: `static_assert` de tipos distintos; assinatura ordenada; `findOrCreate` uma vez; `pushUninit`; placement-new de cada `T` na sua coluna (nasce direto no arquétipo final).
  - `destroy`: `destroyRow` + `fillHole` + atualização do deslocado + `store_.release`.
  - Na M1, transições usam `buildEdge(src, *findOrCreate(sig ± cid), cid)` a cada chamada (sem cache).

- [ ] **Step 1: Testes que falham** — um caso por linha da tabela da seção 5, mais os itens 2 e 3 do Review Focus:

```cpp
TEST_CASE("create() nasce vivo no arquétipo raiz") { ecs::World w; auto e = w.create(); CHECK(w.isAlive(e)); CHECK_FALSE(w.has<A>(e)); }
TEST_CASE("create<Ts...> nasce no arquétipo final com valores") {
    ecs::World w; auto e = w.create(A{1}, B{2, 3}, Tag{});
    CHECK(w.get<A>(e).v == 1); CHECK(w.get<B>(e).w == 3); CHECK(w.has<Tag>(e));
}
TEST_CASE("add sobre T existente atribui sem mudar arquétipo") {
    ecs::World w; auto e = w.create(A{1}); w.add(e, A{7}); CHECK(w.get<A>(e).v == 7);
}
TEST_CASE("remove de T ausente é no-op") { ecs::World w; auto e = w.create(A{1}); w.remove<B>(e); CHECK(w.get<A>(e).v == 1); }
TEST_CASE("destroy atualiza o record da entidade deslocada") {
    ecs::World w; auto e0 = w.create(A{0}); auto e1 = w.create(A{1}); auto e2 = w.create(A{2});
    w.destroy(e0);
    CHECK_FALSE(w.isAlive(e0)); CHECK(w.get<A>(e2).v == 2); CHECK(w.get<A>(e1).v == 1);
    CHECK(w.tryGet<A>(e0) == nullptr);
}
TEST_CASE("remove/destroy da última e da única linha") {   // Review Focus 3
    ecs::World w; auto a = w.create(A{1}); auto b = w.create(A{2});
    w.remove<A>(b); CHECK(w.get<A>(a).v == 1); CHECK_FALSE(w.has<A>(b));
    w.destroy(a);   CHECK(w.isAlive(b));
}
TEST_CASE("add com valor lido de outra entidade da tabela destino que realoca") {   // Review Focus 2
    ecs::World w;
    std::vector<ecs::Entity> full; for (uint64_t i = 0; i < 64; ++i) full.push_back(w.create(A{i}, B{i, i}));  // capacidade 64 cheia
    auto e = w.create(A{999});
    w.add(e, w.get<B>(full[5]));                  // destino {A,B} realoca no pushUninit
    CHECK(w.get<B>(e).v == 5); CHECK(w.get<B>(full[5]).w == 5);
}
TEST_CASE("tryGet em entidade morta e sem T devolve nullptr") { /* … */ }
TEST_CASE("tags: add/has/remove sem coluna") { /* add(e, Tag{}) → has; remove<Tag> → !has; colunas de dados intactas */ }
```

- [ ] **Step 2: Ver falhar** — `$DC make test T=unit_world` → FAIL.
- [ ] **Step 3: Implementar** `world.hpp`/`world.cpp` conforme Interfaces.
- [ ] **Step 4: Ver passar** — PASS sob ASan/UBSan.
- [ ] **Step 5: Commit** — `feat(world): create/destroy/add/remove/get com findOrCreate`

---

### Task 8: `checkInvariants()` (I-1…I-7, I-10) e suíte `invariants`

**Files:**
- Modify: `include/ecs/world.hpp`, `src/world.cpp`
- Create: `tests/invariants.cpp`

**Interfaces:**
- Produces: `bool World::checkInvariants(std::string* why = nullptr) const;` — compilado em todos os builds (spec: "só em debug" refere-se ao uso; tests release também a chamam). Em falha escreve em `*why` o ID da invariante e o objeto (ex.: `"I-3 e=4:1"`) e devolve `false`. Nesta task cobre I-1, I-3, I-4, I-5, I-6, I-7, I-10 e I-11 (`iterating_ == 0`). I-2 é verificada pelos testes via `LifeCounter`. Tasks 10 e 12 estendem com I-9 e I-8.
- Helper de teste em `tests/support/components.hpp`: `#define CHECK_INV(w) do { std::string why_; CHECK_MESSAGE((w).checkInvariants(&why_), why_); } while (0)`.

- [ ] **Step 1: Testes que falham** — sequências determinísticas com `CHECK_INV` após cada operação:

```cpp
TEST_CASE("sequência create/add/remove/destroy mantém invariantes") {
    ecs::World w; CHECK_INV(w);
    std::vector<ecs::Entity> es;
    for (uint64_t i = 0; i < 100; ++i) { es.push_back(w.create()); CHECK_INV(w); }
    for (uint64_t i = 0; i < 100; i += 2) { w.add(es[i], A{i}); CHECK_INV(w); }
    for (uint64_t i = 0; i < 100; i += 3) { w.add(es[i], Tag{}); CHECK_INV(w); }
    for (uint64_t i = 0; i < 100; i += 4) { w.remove<A>(es[i]); CHECK_INV(w); }
    for (uint64_t i = 0; i < 100; i += 5) { w.destroy(es[i]); CHECK_INV(w); }
    for (int i = 0; i < 20; ++i) { w.create(A{1}, LifeCounter{}); CHECK_INV(w); }   // reusa slots da freeList
}
TEST_CASE("I-2: LifeCounter bate com linhas vivas") { /* criar/remover/destruir com LifeCounter; CHECK(LifeCounter::alive == entidades com LifeCounter); 0 após ~World */ }
```

- [ ] **Step 2: Ver falhar** — `$DC make test T=invariants` → FAIL (método inexistente).
- [ ] **Step 3: Implementar.** I-7 compara `componentIndex_[c]` com o conjunto de arquétipos cuja assinatura contém `c` e o valor guardado com `columnOf`. I-6: `bySignature_.size() == archetypes_.size()` e cada `archetypes_[i]` é encontrado pela própria assinatura, com `id == i`.
- [ ] **Step 4: Ver passar.**
- [ ] **Step 5: Commit** — `feat(world): checkInvariants I-1..I-7, I-10, I-11`

---

### Task 9: `fuzz_vs_model` e `nontrivial`

**Files:**
- Create: `tests/support/model.hpp`, `tests/fuzz_vs_model.cpp`, `tests/nontrivial.cpp`

**Interfaces:**
- Produces (`model.hpp`, reutilizado em Tasks 15–16):
  - `struct FS { std::string s; };` (não trivial, valor sempre > 32 chars para sair do SSO).
  - Universo do fuzz: `A`, `B`, `FS`, `Tag`, `LifeCounter`. `using Model = std::unordered_map<ecs::Entity, std::map<ecs::ComponentId, uint64_t>, ecs::EntityHash>;` (tag guarda 0).
  - `uint64_t makeValue(ecs::Entity, uint64_t counter);` — mistura `index`, `generation` e contador (splitmix64), para que troca de linhas apareça como valor errado.
  - `void addToWorld(ecs::World&, ecs::Entity, ecs::ComponentId, uint64_t)`, `bool readFromWorld(const ecs::World&, ecs::Entity, ecs::ComponentId, uint64_t& out)` — despacho por `cid` para os 5 tipos.
  - `void requireEqual(ecs::World&, const Model&)` — para cada entidade do modelo: vivo, e para cada um dos 5 tipos `has` == presente no modelo e valor igual.
  - `uint64_t fuzzSeed()` (env `ECS_FUZZ_SEED`, padrão `0xEC5`) e `uint64_t fuzzOps()` (env `ECS_FUZZ_OPS`, padrão `1'000'000`).

- [ ] **Step 1: Escrever o fuzz**

Laço de `fuzzOps()` operações com `std::mt19937_64(seed)`; `MESSAGE("seed=" << seed)` no início. Pesos: create 20 %, destroy 10 %, add 30 %, remove 20 %, get 20 % (get compara `readFromWorld` com o modelo). Se vivos > 4096, destroy sobe para 40 %. A cada 1000 operações `requireEqual(w, model)`; no fim `CHECK_INV(w)` e `CHECK(LifeCounter::alive == <entradas LifeCounter no modelo>)`; após destruir o `World`, `LifeCounter::alive == 0`. Guardar também handles destruídos e, em ~1 % das operações, verificar `!w.isAlive(old)` e `w.tryGet<A>(old) == nullptr` (Review Focus 4).

- [ ] **Step 2: Escrever `nontrivial`**

Casos com `std::string`, `std::unique_ptr<int>`, `std::vector<int>` e `LifeCounter`: criar 300 entidades (força realocações), mover entre arquétipos com add/remove de `A`, destruir metade, conferir valores e `LifeCounter::alive` == contagem esperada; fim do escopo → `LifeCounter::alive == 0`.

- [ ] **Step 3: Rodar**

Run: `$DC make test PRESET=debug-asan T='fuzz_vs_model|nontrivial'`
Expected: PASS, `seed=` impresso, nenhum relatório de ASan/UBSan. Se algo falhar, depurar com superpowers:systematic-debugging antes de mexer no teste.

- [ ] **Step 4: Commit** — `test: fuzz contra modelo (1e6 ops) e suíte nontrivial`

**Gate M1:** núcleo, invariantes I-1…I-7/I-10, fuzz 1e6 e nontrivial limpos sob ASan/UBSan.

---

## M2 · Arestas e índice

### Task 10: Arestas em cache, I-9 e NF-P4

**Files:**
- Modify: `include/ecs/world.hpp`, `src/world.cpp`, `tests/unit_world.cpp`, `tests/invariants.cpp`
- Create: `tests/support/alloc_counter.cpp`, `tests/support/alloc_counter.hpp`
- Modify: `tests/CMakeLists.txt` (linkar `alloc_counter.cpp` só em `unit_world`)

**Interfaces:**
- Produces:
  - Privado `const Edge& World::edgeFor(Archetype& src, ComponentId cid, bool adding);` — procura em `src.addEdges`/`src.removeEdges`; na falta, `findOrCreate(src.signature ± cid)` + `buildEdge` e insere (R-AR-2). `add`/`remove` passam a usar isto.
  - `uint64_t World::signatureLookupCount() const noexcept;` — incrementado a cada consulta a `bySignature_`.
  - `checkInvariants` cobre I-9: para toda aresta `a --±c--> b`, `b.signature == a.signature ± c` e `srcToDst[i]` aponta para a coluna de `b` com o mesmo `ComponentId` (ou `-1` sse ausente em `b`); `addedColumn` correto.
  - `alloc_counter.hpp`: `size_t allocationCount() noexcept;` — contador incrementado por substituições globais de todas as formas de `operator new` (incluindo `align_val_t`).

- [ ] **Step 1: Teste NF-P4 que falha**

```cpp
TEST_CASE("NF-P4: add/remove com aresta em cache não aloca nem consulta hash") {
    struct C1 { uint64_t v; }; struct C2 { uint64_t v; }; struct C3 { uint64_t v; }; struct C4 { uint64_t v; };
    ecs::World w;
    auto e = w.create(C1{1}, C2{2}, C3{3});
    w.add(e, C4{4}); w.remove<C4>(e);           // aquece arestas e capacidade (64 linhas)
    auto allocs = allocationCount(); auto lookups = w.signatureLookupCount();
    for (int i = 0; i < 1000; ++i) { w.add(e, C4{uint64_t(i)}); w.remove<C4>(e); }
    CHECK(allocationCount() == allocs);
    CHECK(w.signatureLookupCount() == lookups);
    CHECK(w.get<C3>(e).v == 3);
}
```

Em `invariants.cpp`, `CHECK_INV` já passa a cobrir I-9 em todas as sequências.

- [ ] **Step 2: Ver falhar** — `$DC make test T=unit_world` → FAIL (lookups crescem a cada add).
- [ ] **Step 3: Implementar** `edgeFor`, contador e I-9.
- [ ] **Step 4: Ver passar** — `$DC make test T='unit_world|invariants|fuzz_vs_model'` → PASS.
- [ ] **Step 5: Commit** — `feat(world): arestas memoizadas, I-9 e NF-P4`

---

### Task 11: Benchmarks `transition` e `transition_cold`

**Files:**
- Create: `bench/transitions.cpp`, `bench/results/m2_transitions.json`
- Modify: `bench/common.hpp`, `bench/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `template<class T> struct NonTrivial { T v; NonTrivial(T x) noexcept; NonTrivial(NonTrivial&&) noexcept; NonTrivial& operator=(NonTrivial&&) noexcept; ~NonTrivial() {} };` em `common.hpp` — mesmos bytes, caminho não `memcpy`.
  - `template<int I> struct Comp { uint64_t v; };`
  - `BM_Transition<Trivial|NonTrivial>/k` com `k ∈ {2,4,8,16}`: entidades com `k` componentes `Comp<0..k-1>` (ou `NonTrivial<…>`), laço `add<Comp<31>>` + `remove<Comp<31>>`; reporta ns/operação, `archetypes` e `entities_per_archetype` como counters.
  - `BM_TransitionCold`: cada iteração cria um `World` novo (fora do tempo) e mede a primeira transição para arquétipo inédito.
  - `BM_RandomGet` (NF-P6, sem meta) e baseline `BM_RandomGet_VectorStruct`: 1M entidades com `Comp<0..3>`, `get<Comp<2>>` numa permutação aleatória fixa vs `std::vector<struct{4×uint64}>` indexado pela mesma permutação.
  - `BM_CreateDestroyChurn`: 100k entidades com 4 componentes; por iteração destrói 10 % aleatórios e cria 10 % novos; ns/operação.

- [ ] **Step 1: Escrever os benchmarks.**
- [ ] **Step 2: Rodar e salvar**

Run: `$DC make build PRESET=release && $DC ./build/release/bench/bench_transitions --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m2_transitions.json`
Expected: medianas para as 8 variações quentes, a fria, `random_get` (com baseline) e o churn; trivial ≤ não trivial para o mesmo `k` (se não, investigar o caminho `memcpy` em `relocateValue`).

- [ ] **Step 3: Commit** — `bench: transition e transition_cold (M2)`

**Gate M2:** arestas + I-9 (Task 10), `componentIndex_` com `get`/`tryGet`/`has` (já na Task 7), transições medidas e NF-P4 (Tasks 10–11). `random_get` e `create_destroy` (seção 8) também ficam medidos aqui.

---

## M3 · Queries

### Task 12: `Query<Ts...>` — cache, matching incremental e I-8

**Files:**
- Create: `include/ecs/query.hpp`, `tests/queries.cpp`
- Modify: `include/ecs/world.hpp`, `src/world.cpp`

**Interfaces:**
- Consumes: `Archetype`, `SmallMap`, `componentIndex_`.
- Produces:
  - `template<class... Us> struct With {}; template<class... Us> struct Without {};`
  - `class QueryCacheBase { public: virtual ~QueryCacheBase(); virtual void onArchetypeCreated(Archetype&) = 0; virtual bool matches(const Archetype&) const = 0; virtual bool isMatched(const Archetype*) const = 0; protected: uint32_t* iterating_; };`
  - `MatchedTable<Ts...>` como na seção 4.6.
  - `Query<Ts...>`: `static_assert` de que nenhum `std::remove_const_t<T>` é tag (R-QY-2/NF-S3); `static constexpr std::array<bool, sizeof...(Ts)> writes` = `!std::is_const_v<T>` (R-QY-1). Construtor `Query(uint32_t* iterating, std::vector<ComponentId> with, std::vector<ComponentId> without)` (`with` = ids de `Ts` ∪ `With`, ordenados). `void initialize(const std::vector<std::unique_ptr<Archetype>>& all, const SmallMap<ArchetypeId, uint16_t>* smallest);` — com `smallest != nullptr` percorre só ele (R-QY-3), senão todos. `size_t matchedCount() const noexcept;`
  - `matches(a)`: `std::includes(a.signature, with_)` e interseção vazia com `without_`. Colunas de `MatchedTable::cols` via `columnOf` no match (R-QY-4).
  - `World::query`: `template<class... Ts, class... Filters> Query<Ts...>& query(Filters...);` — chave `struct QueryKey { std::vector<uint64_t> ts /* (cid<<1)|isConst */; Signature with, without; auto operator<=>(const QueryKey&) const = default; };` em `std::map<QueryKey, std::unique_ptr<QueryCacheBase>> queryCache_`; `queries_` guarda os ponteiros; `c*` = componente de `with` com menor `componentIndex_[c].size()`. `findOrCreate` chama `q->onArchetypeCreated(a)` para cada query.
  - `checkInvariants` cobre I-8.

- [ ] **Step 1: Testes que falham**

```cpp
TEST_CASE("mesma instância para a mesma combinação; filtros mudam a chave") {
    ecs::World w;
    auto& q1 = w.query<A, const B>(); auto& q2 = w.query<A, const B>();
    CHECK(&q1 == &q2);
    CHECK(static_cast<void*>(&w.query<A, const B>(ecs::Without<Tag>{})) != static_cast<void*>(&q1));
    CHECK(static_cast<void*>(&w.query<A, B>()) != static_cast<void*>(&q1));   // constness entra na chave
}
TEST_CASE("arquétipos criados antes e depois da query; with/without de tag") {
    ecs::World w; w.create(A{1}); w.create(A{2}, Tag{});
    auto& all = w.query<A>(); auto& tagged = w.query<A>(ecs::With<Tag>{}); auto& untagged = w.query<A>(ecs::Without<Tag>{});
    w.create(A{3}, B{0, 0});  w.create(A{4}, B{0, 0}, Tag{});      // arquétipos novos depois
    CHECK(all.matchedCount() == 4); CHECK(tagged.matchedCount() == 2); CHECK(untagged.matchedCount() == 2);
    CHECK_INV(w);   // I-8
}
TEST_CASE("query criada antes de existir qualquer arquétipo com seus tipos") { /* matchedCount() == 0, depois create → 1 */ }
```

- [ ] **Step 2: Ver falhar** — `$DC make test T=queries` → FAIL.
- [ ] **Step 3: Implementar.**
- [ ] **Step 4: Ver passar** — `$DC make test T='queries|invariants'` → PASS.
- [ ] **Step 5: Commit** — `feat(query): Query<Ts...> com cache e matching incremental (I-8)`

---

### Task 13: `eachBlock`, `each` e modo iterando

**Files:**
- Modify: `include/ecs/query.hpp`, `include/ecs/world.hpp`, `tests/queries.cpp`

**Interfaces:**
- Produces:
  - `template<class F> void Query::eachBlock(F&& f);` — para cada `MatchedTable` com `count > 0`: `f(std::span<Ts>(ptr_i, count)...)`; `Ts` const → `std::span<const T>`.
  - `template<class F> void Query::each(F&& f);` — se `std::is_invocable_v<F, Entity, Ts&...>` chama `f(entity, comps...)`, senão `f(comps...)`; ponteiros de coluna resolvidos antes do laço interno.
  - Guard RAII `detail::IterationGuard` (incrementa `*iterating_` no construtor, decrementa no destrutor) usado por ambos (R-QY-5, Review Focus 5).
  - `World::isIterating() const noexcept` (`iterating_ > 0`) para testes.
  - `ECS_ASSERT(iterating_ == 0, "<op> e=%u:%u durante iteração", …)` em `create`, `destroy`, `add`, `remove` (death tests na Task 17).

- [ ] **Step 1: Testes que falham**

```cpp
TEST_CASE("each visita exatamente as entidades do modelo e permite escrita") {
    ecs::World w; std::set<uint32_t> expected;
    for (uint64_t i = 0; i < 300; ++i) { auto e = (i % 3) ? w.create(A{i}, B{i, 0}) : w.create(A{i}); if (i % 3) expected.insert(e.index); }
    w.create(B{0, 0});                                      // arquétipo sem A
    auto empty = w.create(A{0}, B{0, 0}, Tag{}); w.destroy(empty);   // arquétipo vazio é pulado
    std::set<uint32_t> seen;
    w.query<A, const B>().each([&](ecs::Entity e, A& a, const B&) { seen.insert(e.index); a.v += 1; });
    CHECK(seen == expected);
}
TEST_CASE("eachBlock entrega spans contíguos com count == tamanho da tabela") { /* soma de span.size() == entidades com A e B */ }
TEST_CASE("iterating_ aninha e volta a zero, inclusive com exceção") {   // Review Focus 5
    ecs::World w; w.create(A{1});
    w.query<A>().each([&](A&) { CHECK(w.isIterating()); w.query<A>().each([&](A&) { CHECK(w.isIterating()); }); });
    CHECK_FALSE(w.isIterating());
    CHECK_THROWS(w.query<A>().each([](A&) { throw 42; }));
    CHECK_FALSE(w.isIterating());
    w.create(A{2}); CHECK_INV(w);
}
```

Compile-fail de `w.query<Tag>()` não entra no CTest; registrar no commit que foi checado manualmente (`static_assert` dispara).

- [ ] **Step 2: Ver falhar** — `$DC make test T=queries` → FAIL.
- [ ] **Step 3: Implementar.**
- [ ] **Step 4: Ver passar.**
- [ ] **Step 5: Commit** — `feat(query): each/eachBlock e modo iterando`

---

### Task 14: Benchmarks NF-P1, NF-P2, NF-P3

**Files:**
- Modify: `bench/iterate.cpp`, `bench/CMakeLists.txt`, `CMakeLists.txt`
- Create: `bench/fragmentation.cpp`, `bench/results/m3_iterate.json`, `bench/results/m3_fragmented.json`, `bench/results/m3_vectorization.txt`

**Interfaces:**
- Produces:
  - `BM_IterateSingle_ECS/n` em `bench_iterate` (mesmos `n` do SoA): `world.query<Position, const Velocity>().eachBlock(...)` com o mesmo corpo de laço do SoA.
  - `bench_fragmented`: `BM_IterateFragmented/A` com `A ∈ {1, 8, 64, 512, 4096}`, 1M entidades distribuídas uniformemente; o arquétipo de cada entidade é `Position, Velocity` + o subconjunto de tags `Frag<0..11>` dado pelos bits de `i % A`. Counters `archetypes` e `entities_per_archetype`. Também `BM_IterateSingle_SoA/1000000` como baseline no mesmo binário.
  - Opção CMake `ECS_VEC_REPORT` (OFF por padrão): adiciona `-fopt-info-vec-optimized` (GCC) / `-Rpass=loop-vectorize` (Clang) aos alvos de bench.

- [ ] **Step 1: Escrever os benchmarks.**
- [ ] **Step 2: Medir e verificar NF-P1/NF-P2**

```bash
$DC make build PRESET=release
$DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m3_iterate.json
$DC python3 -I tools/bench_ratio.py --json bench/results/m3_iterate.json --name BM_IterateSingle_ECS/1000000 --baseline BM_IterateSingle_SoA/1000000 --max 1.10
$DC ./build/release/bench/bench_fragmented --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m3_fragmented.json
$DC python3 -I tools/bench_ratio.py --json bench/results/m3_fragmented.json --name BM_IterateFragmented/64 --baseline BM_IterateSingle_SoA/1000000 --max 1.25
```

Expected: ambos `exit 0`. Se não, perfilar antes de mudar código (não afrouxar a meta).

- [ ] **Step 3: Verificar NF-P3**

Run: `$DC sh -c "cmake --preset release -DECS_VEC_REPORT=ON && cmake --build --preset release --target bench_iterate 2>&1 | grep -E 'iterate.cpp:[0-9]+:[0-9]+: optimized: loop vectorized' | tee bench/results/m3_vectorization.txt"`
Expected: ao menos uma linha apontando para o laço interno do lambda do `eachBlock`. Se não vetorizar, copiar os ponteiros dos spans para variáveis locais `__restrict` dentro do lambda do benchmark e repetir; registrar a mudança no commit. Depois reconfigurar com `-DECS_VEC_REPORT=OFF`.

- [ ] **Step 4: Commit** — `bench: iterate ECS vs SoA e fragmentação (NF-P1..P3)`

**Gate M3:** Tasks 12–14.

---

## M4 · Command buffer

### Task 15: `CommandBuffer` (gravação, crescimento seguro, descarte) e flush sequencial

**Files:**
- Create: `include/ecs/command_buffer.hpp`, `src/command_buffer.cpp`, `tests/command_buffer.cpp`
- Modify: `include/ecs/world.hpp`, `src/world.cpp`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `EntityStore`, `TypeInfo`, `relocateValue`, `destroyValue`, `alignedAlloc`.
- Produces:
  - `enum class CommandOp : uint8_t { Spawn, Destroy, Add, Remove };`
  - Registro: cabeçalho de 24 bytes `[op:u8][pad:7][entity:u64][cid:u32][size:u32]`; payload em `alignUp(offset + 24, max(8, align))`; próximo registro em `alignUp(fim do payload, 8)`. Bloco alinhado a 64; crescimento ×2 (mínimo 1 KiB) percorrendo registros e usando `relocateValue` por payload.
  - `class CommandBuffer` (move-only):
    - `explicit CommandBuffer(EntityStore&)`; `~CommandBuffer()` destrói payloads não triviais e devolve ao `EntityStore` (`release`) os índices reservados ainda não materializados (R-CB-2).
    - `Entity spawn();` — `store.allocate()` imediato (R-CB-1).
    - `void destroy(Entity); template<class T> void add(Entity, T&& v); template<class T> void remove(Entity);` (`add` constrói `U` por move no payload).
    - `struct CommandView { CommandOp op; Entity e; ComponentId cid; void* payload; };` `template<class F> void forEach(F&&);` em ordem de gravação.
    - `size_t commandCount() const noexcept; bool empty() const noexcept; const EntityStore* store() const noexcept;`
    - `void clear() noexcept;` — destrói todos os payloads não triviais (inclusive moved-from), zera o buffer e esquece os spawns (já materializados ou liberados pelo flush).
  - `World::commands()` → `CommandBuffer{store_}`.
  - `void World::flushSequential(CommandBuffer&);` — oráculo sem coalescing: aplica cada comando na ordem via `create`-no-slot-reservado (Spawn: coloca a entidade reservada no arquétipo raiz), `destroy`, `addRaw` (com `moveConstruct` a partir do payload), `removeRaw`; comandos para handles nem vivos nem reservados são ignorados; depois `cb.clear()`. Assert `iterating_ == 0` e `cb.store() == &store_`.

- [ ] **Step 1: Testes que falham**

```cpp
TEST_CASE("spawn referenciado no mesmo buffer") {
    ecs::World w; auto cb = w.commands();
    auto e = cb.spawn(); CHECK_FALSE(w.isAlive(e));
    cb.add(e, A{5}); cb.add(e, FS{std::string(40, 'x')});
    w.flushSequential(cb);
    CHECK(w.get<A>(e).v == 5); CHECK(w.get<FS>(e).s == std::string(40, 'x')); CHECK_INV(w);
}
TEST_CASE("payload não trivial sobrevive ao crescimento do buffer") {   // Review Focus 1
    ecs::World w; auto e = w.create(); auto cb = w.commands();
    std::string big = "payload-que-nao-cabe-no-sso-0123456789";
    cb.add(e, FS{big});
    for (int i = 0; i < 500; ++i) cb.add(w.create(), A{uint64_t(i)});   // força várias realocações
    w.flushSequential(cb);
    CHECK(w.get<FS>(e).s == big);
}
TEST_CASE("payload alignas(64) fica alinhado") { /* add Wide; no forEach, uintptr_t(payload) % 64 == 0 */ }
TEST_CASE("buffer descartado sem flush não vaza") {
    LifeCounter::alive = 0;
    { ecs::World w; { auto cb = w.commands(); auto s = cb.spawn(); cb.add(s, LifeCounter{}); cb.add(s, FS{std::string(64, 'y')}); }
      CHECK(LifeCounter::alive == 0); CHECK(w.create().index == 0); }   // índice reservado voltou à freeList (geração +1)
}
TEST_CASE("fuzz sequencial == aplicar direto") {
    // Lotes aleatórios (seed fixa) de 1..64 comandos sobre spawn/destroy/add/remove dos tipos de model.hpp,
    // aplicados por flushSequential no World e um a um no Model; requireEqual após cada lote; 20k comandos no total.
}
```

- [ ] **Step 2: Ver falhar** — `$DC make test T=command_buffer` → FAIL.
- [ ] **Step 3: Implementar.**
- [ ] **Step 4: Ver passar** sob ASan/UBSan.
- [ ] **Step 5: Commit** — `feat(command_buffer): gravação, crescimento seguro e flush sequencial`

---

### Task 16: Flush coalescido (R-CB-3, R-CB-4)

**Files:**
- Modify: `include/ecs/command_buffer.hpp`, `src/command_buffer.cpp`, `include/ecs/world.hpp`, `src/world.cpp`, `tests/command_buffer.cpp`

**Interfaces:**
- Produces:
  - Passada 1 (em `command_buffer.cpp`): `struct NetComponentOp { ComponentId cid; bool present; void* payload; };` `struct EntityPlan { Entity e; bool spawned; bool destroyed; std::vector<NetComponentOp> ops; /* ordenado por cid */ };` `std::vector<EntityPlan> CommandBuffer::plan() const;` — agrupa por entidade preservando a ordem (ordenação estável de `(entity.index, seq)`); por `cid` vale o último comando: Add → `present = true, payload` do último Add; Remove → `present = false`. `Destroy` zera `ops` e marca `destroyed`; comandos depois de `Destroy` disparam `ECS_ASSERT(false, "command after destroy e=%u:%u")` e, sem asserts, são ignorados (R-CB-4).
  - Passada 2: `void World::flush(CommandBuffer&);` — para cada plano: descarta se o handle não está vivo nem reservado (questão 3); `destroyed` → `destroy(e)` se vivo, `store_.release` se reservado; senão assinatura final = assinatura atual (vazia se reservado) com `ops` aplicadas, `dst = findOrCreate(final)`, **uma** transição via aresta de salto `jumpEdge(src, dst)` (ou `pushUninit` direto se reservado), e para cada `present` com payload: se a coluna já existia na origem, `destroyValue` + `moveConstruct`/`memcpy`; senão constrói na coluna nova. No fim `cb.clear()` (R-CB-3).
  - Privado `const Edge& World::jumpEdge(Archetype& src, Archetype& dst);` — cache `std::unordered_map<uint64_t, Edge> jumpEdges_` com chave `(uint64_t(src.id) << 32) | dst.id`; I-9 verifica a consistência de `srcToDst` dessas arestas também.
  - `ECS_ASSERT(iterating_ == 0, "flush durante iteração")` (R-CB-5).

- [ ] **Step 1: Testes que falham**

```cpp
TEST_CASE("R-CB-4: add seguido de remove não deixa nada") { /* add A, remove A → !has<A>, entidade no arquétipo original */ }
TEST_CASE("R-CB-4: add repetido mantém o último valor") { /* add A{1}, add A{2} → get<A>.v == 2 */ }
TEST_CASE("R-CB-4: destroy descarta comandos anteriores; spawn+destroy libera o índice") { /* … */ }
TEST_CASE("R-CB-3: uma única transição por entidade") {
    ecs::World w; auto e = w.create(); auto cb = w.commands();
    cb.add(e, A{1}); cb.add(e, B{2, 3}); cb.add(e, Tag{});
    auto before = w.archetypeCount();
    w.flush(cb);
    CHECK(w.archetypeCount() == before + 1);   // só {A,B,Tag}; nenhum intermediário
}
TEST_CASE("comandos para handle destruído diretamente são ignorados") {   // Review Focus 4 / questão 3
    ecs::World w; auto e = w.create(A{1}); auto cb = w.commands(); cb.add(e, B{0, 0});
    w.destroy(e); auto reused = w.create(A{9});        // mesmo índice, geração nova
    w.flush(cb);
    CHECK_FALSE(w.has<B>(reused)); CHECK(w.get<A>(reused).v == 9);
}
TEST_CASE("fuzz: flush coalescido == flushSequential") {
    // Dois Worlds alimentados pela mesma semente; handles lógicos mapeados em std::vector<std::pair<Entity,Entity>>;
    // cada lote gravado em cbA (world1) e cbB (world2) com os mesmos comandos lógicos (sem comandos após destroy),
    // world1.flush(cbA), world2.flushSequential(cbB); comparar componentes por entidade lógica e CHECK_INV em ambos.
}
```

Adicionar `uint32_t World::archetypeCount() const noexcept` nesta task (usado aqui e em `stats()`).

- [ ] **Step 2: Ver falhar** — `$DC make test T=command_buffer` → FAIL.
- [ ] **Step 3: Implementar.**
- [ ] **Step 4: Ver passar** — `$DC make test T='command_buffer|invariants|fuzz'` → PASS.
- [ ] **Step 5: Commit** — `feat(command_buffer): flush coalescido com uma transição por entidade`

---

### Task 17: `contract_death` e I-11

**Files:**
- Create: `tests/contract_death.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `setAssertHandler`, `World`, `Query::each`, `CommandBuffer`.
- Produces: executável `contract_death` (sem doctest), compilado só quando `ECS_ENABLE_ASSERTS` resolve para 1. `main(argc, argv)` instala um handler que imprime a mensagem em `stderr`, verifica que ela contém as substrings exigidas pelo caso (senão `std::_Exit(2)`) e chama `std::_Exit(0)`; executa o caso nomeado em `argv[1]`; se retornar, imprime "assert não disparou" e devolve 1. Um `add_test(NAME contract_death.<caso> COMMAND contract_death <caso>)` por caso:

| caso | ação | substrings exigidas |
| --- | --- | --- |
| `add_during_each` | `w.add(e, B{})` dentro de `query<A>().each` | `add`, `e=` |
| `get_dead_entity` | `w.destroy(e); w.get<Position>(e)` (`struct Position { float x; }` local) | `get`, `e=`, `Position` |
| `flush_during_each` | `w.flush(cb)` dentro de `each` | `flush` |
| `command_after_destroy` | `cb.destroy(e); cb.add(e, A{}); w.flush(cb)` | `after destroy` |
| `destroy_during_each_block` | `w.destroy(e)` dentro de `eachBlock` | `destroy` |

- [ ] **Step 1: Escrever o executável e registrar os casos.**
- [ ] **Step 2: Rodar** — `$DC make test PRESET=debug-asan T=contract_death` → 5/5 PASS. Sanidade: trocar temporariamente um caso por código válido e ver o teste falhar com "assert não disparou"; desfazer.
- [ ] **Step 3: Rodar a suíte completa** — `$DC make test PRESET=debug-asan` e `$DC make test PRESET=release` → 100 % (no release, `contract_death` não existe).
- [ ] **Step 4: Commit** — `test: contract_death para modo iterando e flush`

---

### Task 18: Benchmark `flush_coalesced` (NF-P5)

**Files:**
- Create: `bench/flush.cpp`, `bench/results/m4_flush.json`
- Modify: `bench/CMakeLists.txt`

**Interfaces:**
- Produces: `bench_flush` com `BM_FlushCoalesced/N` e `BM_DirectAdds/N`, `N ∈ {1000, 100000}`: `N` entidades criadas no arquétipo raiz (fora do tempo, `PauseTiming`), 3 adds (`Comp<0>`, `Comp<1>`, `Comp<2>`) por entidade; o primeiro grava num `CommandBuffer` e faz `flush` dentro do tempo, o segundo faz `3N` `add` diretos. Ambos aquecem as arestas/arquétipos antes. Reporta ns/entidade.

- [ ] **Step 1: Escrever.**
- [ ] **Step 2: Medir e verificar**

```bash
$DC make build PRESET=release
$DC ./build/release/bench/bench_flush --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=bench/results/m4_flush.json
$DC python3 -I tools/bench_ratio.py --json bench/results/m4_flush.json --name BM_FlushCoalesced/100000 --baseline BM_DirectAdds/100000 --max 0.5
$DC python3 -I tools/bench_ratio.py --json bench/results/m4_flush.json --name BM_FlushCoalesced/1000 --baseline BM_DirectAdds/1000 --max 0.5
```

Expected: `exit 0` nos dois (≥ 2× mais rápido).

- [ ] **Step 3: Commit** — `bench: flush coalescido vs adds diretos (NF-P5)`

**Gate M4:** Tasks 15–18.

---

## M5 · Instrumentação

### Task 19: `stats()`

**Files:**
- Modify: `include/ecs/world.hpp`, `src/world.cpp`, `tests/unit_world.cpp`

**Interfaces:**
- Produces:

```cpp
struct ArchetypeStats {
    ArchetypeId id; Signature signature;
    uint32_t count, capacity, edgeCount;
    size_t columnBytes;     // capacity × Σ size das colunas + capacity × sizeof(Entity)
    size_t idleBytes;       // (capacity − count) × (Σ size + sizeof(Entity))
    size_t signatureBytes;  // capacity() de signature + columnTypes, em bytes
    size_t edgeBytes;       // capacityBytes() de addEdges/removeEdges + srcToDst
};
struct WorldStats {
    uint32_t archetypeCount; uint32_t liveEntities;
    std::vector<ArchetypeStats> archetypes;       // por id
    std::array<uint32_t, 33> histogram;           // [0] = arquétipos vazios; [i] = count em [2^(i-1), 2^i)
    uint64_t signatureLookups;
};
WorldStats World::stats() const;
```

- [ ] **Step 1: Teste que falha**

```cpp
TEST_CASE("stats: contagens, histograma e memória ociosa") {
    ecs::World w;
    for (uint64_t i = 0; i < 5; ++i) w.create(A{i});      // {A}: 5
    w.create(A{0}, B{0, 0});                              // {A,B}: 1
    auto s = w.stats();
    CHECK(s.archetypeCount == 3);                         // raiz, {A}, {A,B}
    CHECK(s.liveEntities == 6);
    CHECK(s.histogram[0] == 1);                           // raiz vazia
    CHECK(s.histogram[1] == 1);                           // count 1
    CHECK(s.histogram[3] == 1);                           // count 5 ∈ [4, 8)
    auto& a = *std::find_if(s.archetypes.begin(), s.archetypes.end(),
                            [](auto& x) { return x.signature == ecs::Signature{ecs::componentId<A>()}; });
    CHECK(a.capacity == 64);
    CHECK(a.idleBytes == (64 - 5) * (sizeof(A) + sizeof(ecs::Entity)));
}
```

- [ ] **Step 2: Ver falhar** — `$DC make test T=unit_world` → FAIL.
- [ ] **Step 3: Implementar.**
- [ ] **Step 4: Ver passar.**
- [ ] **Step 5: Commit** — `feat(world): stats() com histograma e memória ociosa (NF-M2)`

---

### Task 20: `iterate_fragmented` sob `perf stat`

**Files:**
- Create: `tools/perf_fragmented.sh`, `bench/results/m5_perf_fragmented.txt`, `bench/results/README.md`

**Interfaces:**
- Consumes: `bench_fragmented` (Task 14), preset `relwithdebinfo`, serviço `perf` (privileged).
- Produces: `tools/perf_fragmented.sh <bench_bin> <out>` — para cada `A ∈ {1, 8, 64, 512, 4096}` roda `perf stat -x, -e cycles,instructions,cache-misses,L1-dcache-load-misses <bin> --benchmark_filter='BM_IterateFragmented/A$' --benchmark_min_time=2s` e escreve uma linha CSV `A,ns_per_entity,cycles,instructions,cache_misses_per_entity,l1d_misses_per_entity` (eventos divididos por `iterações × 1M`). `README.md` em `bench/results/` lista cada JSON/TXT, o commit e a máquina em que foi medido, e a curva de ns e misses por entidade em função de `A`.

- [ ] **Step 1: Escrever o script** (POSIX sh, `set -eu`).
- [ ] **Step 2: Rodar**

Run: `docker compose run --rm perf sh -c "make build PRESET=relwithdebinfo && sh tools/perf_fragmented.sh build/relwithdebinfo/bench/bench_fragmented bench/results/m5_perf_fragmented.txt"`
Expected: 5 linhas CSV com contadores numéricos. Se `perf` não funcionar no kernel do Docker Desktop/WSL2 (`<not supported>` ou erro de versão), **parar e reportar ao usuário** com a saída exata; não inventar números.

- [ ] **Step 3: Escrever `bench/results/README.md`** com a tabela gerada.
- [ ] **Step 4: Commit** — `bench: perf stat de iterate_fragmented e registro de resultados (M5)`

**Gate M5:** Tasks 19–20. Fim da v1: rodar `$DC make test PRESET=debug-asan` e `$DC make test PRESET=release` uma última vez e anexar a saída ao PR.
