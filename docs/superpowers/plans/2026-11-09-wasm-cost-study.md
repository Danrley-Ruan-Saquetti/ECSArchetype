# Estudo de custo nativo vs WebAssembly — Plano de Implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compilar biblioteca, testes e benchmarks numa escada de variantes (GCC → Clang → 32 bits → WASM escalar → WASM SIMD, mais o ramo AVX2), medir o custo de cada degrau e explicá-lo pelo código gerado, num relatório versionado.

**Architecture:** Uma imagem Docker nova (`Dockerfile.wasm`, serviço `wasm`) com Emscripten 3.1.74, CMake 3.30.5 e um Clang nativo de LLVM próximo ao do emsdk; o serviço `dev` (GCC) não muda. As variantes são presets que herdam de um preset oculto de medição e mudam um fator cada (`CMAKE_CXX_COMPILER`, `CMAKE_CXX_FLAGS`, `toolchainFile`). Ajustes de portabilidade vivem no código e valem para GCC e Clang; o que é específico de WASM fica atrás de `if(EMSCRIPTEN)` no CMake. Um script no host roda as variantes intercaladas; um script Python monta a escada a partir dos JSONs.

**Tech Stack:** C++20, GCC 14 (V0), Clang ≈ LLVM 20 nativo, Emscripten 3.1.74 + node 20.18 (V8), CMake ≥ 3.25 com presets v6, doctest 2.4.11, Google Benchmark 1.8.3, Python 3 (`unittest`), `objdump`/`llvm-objdump`.

**Spec:** `docs/superpowers/specs/2026-11-06-wasm-cost-study-design.md` (complementa `2026-10-16-ecs-archetype-spec.md`). Seções `§N` abaixo são desse spec.

## Global Constraints

- Branch `feat/wasm-cost-study` (não `master`). Commits, comentários e mensagens em pt-BR, terminando com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Emscripten fixado em `emscripten/emsdk:3.1.74`; CMake da imagem nova `pip install cmake==3.30.5` (§3).
- A imagem `gcc:14` e o serviço `dev` continuam o caminho nativo e não mudam de comportamento; presets `debug-asan`, `release`, `relwithdebinfo` não mudam (§2, anti-objetivos).
- `-Wall -Wextra -Wpedantic -Werror` em todos os nossos alvos, em todas as variantes, inclusive Clang e Emscripten (§3).
- Variantes de medição: `CMAKE_BUILD_TYPE=Release` (`-O3`), `ECS_ENABLE_ASSERTS=0`, cada degrau muda um único fator (§4).
- WASM roda só em `node`, via `CMAKE_CROSSCOMPILING_EMULATOR`, para o `ctest` funcionar igual ao nativo (§3).
- Link WASM: `-sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1` (§5.6).
- `EntityRecord`: `static_assert(sizeof(EntityRecord) == sizeof(void*) + 8)`; NF-M1 vira "um ponteiro + 8 bytes por `EntityRecord` (16 B em 64 bits, 12 B em 32 bits)" (§5.1).
- Benchmarks: os quatro existentes sem mudar o que medem; protocolo `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`, JSON, medianas de `real_time` (§6).
- Comparação entre variantes sempre rotulada como "entre execuções" e com a dispersão entre rodadas (§6, anti-objetivos).
- Artefatos de estudo (JSONs, trechos de código gerado, relatório) em `bench/results/wasm/`, só texto (§7, §8).
- Scripts Python rodam com `python3 -I`.

### Como rodar comandos

```sh
DC="docker compose run --rm dev"     # GCC 14, inalterado
WC="docker compose run --rm wasm"    # Clang nativo + Emscripten (Task 1)
$WC make test PRESET=wasm-debug T=unit_world
```

### Decisões que o spec deixa em aberto (fixadas aqui)

- **Uma imagem só** para Clang nativo e WASM (`Dockerfile.wasm`), serviço `wasm`. V0 continua no serviço `dev`.
- **Clang nativo:** preferir o próprio `/emsdk/upstream/bin/clang++` (LLVM idêntico ao do Emscripten) se `--print-targets` listar `x86-64`; senão `clang-20` de `apt.llvm.org`. Em ambos os casos a imagem expõe `clang++` no `PATH` apontando para o escolhido, e os presets usam `clang++`. Biblioteca padrão nativa: a libstdc++ do `g++` da imagem (registrada no relatório; o WASM usa libc++).
- **Exceções no WASM:** `-fwasm-exceptions` (compilação e link) só nos executáveis doctest, via função CMake `ecs_wasm_exceptions(target)`. A biblioteca `ecs`, o `contract_death` e os benchmarks ficam sem exceções. Funciona porque `IterationGuard`/`each` são templates (compilados no TU do teste) e nada em `src/` captura exceções. Se o `wasm-ld` recusar a mistura, criar `ecs_without_exceptions` (mesmas fontes) só para o `contract_death` e compilar `ecs` com exceções.
- **Presets de variantes:** `native-clang` (V1), `native-clang-32` (V2), `native-clang-32-scalar` (par escalar de V3, `-m32 -fno-vectorize -fno-slp-vectorize`), `wasm-scalar` (V3), `wasm-simd` (V4), `native-clang-avx2` (VX). Testes: `wasm-debug` (Debug, asserts ligados, sem sanitizers). Sem `wasm-asan` (§5.7 o torna opcional).
- **Nomes de função no `.wasm`:** os presets WASM linkam com `--profiling-funcs` para o desmontador mostrar nomes; não muda o código gerado das funções.
- **`fuzz_vs_model` (§5.3):** incrementa antes, `++counter;` num comando próprio e depois `makeValue(ecs::Entity{uint32_t(counter), 0}, counter)`.

## Review Focus

1. **Variáveis de ambiente no `node`** (`ECS_FUZZ_SEED`/`ECS_FUZZ_OPS`): reproduzir uma semente em WASM deve dar o mesmo `seed=` impresso que no nativo — teste na Task 3.
2. **Um benchmark que falha só em WASM** (memória, `error_occurred` no JSON): a escada não pode mostrar número para ele como se tivesse rodado — `bench_ladder.py` recusa o JSON (Task 5).
3. **Benchmark ausente em alguma variante** ou nº de rodadas diferente entre variantes: a tabela mostra `—` no ausente e o script recusa rodadas desemparelhadas, em vez de travar ou parear errado (Task 5).
4. **Regressão silenciosa no GCC** pelas correções de portabilidade (sobretudo `DoNotOptimize`): os testes `debug-asan`/`release` passam e as razões NF-P1/NF-P2 do `bench_iterate` continuam dentro das metas (Task 2).
5. **Extração de código gerado pegando a função errada** (nome ambíguo, símbolo inexistente): o script falha com mensagem em vez de gravar arquivo vazio ou de outra função (Task 6).

---

### Task 1: Imagem `wasm`, Clang nativo e presets das variantes

**Files:**
- Create: `Dockerfile.wasm`
- Modify: `docker-compose.yaml` (serviço `wasm`)
- Modify: `CMakePresets.json` (presets novos; os atuais intactos)
- Modify: `CMakeLists.txt` (bloco `if(EMSCRIPTEN)` com as flags de link de §5.6)

**Interfaces:**
- Produces: serviço `wasm`; `clang++` nativo no `PATH`; presets configuráveis `native-clang`, `native-clang-32`, `native-clang-32-scalar`, `native-clang-avx2`, `wasm-scalar`, `wasm-simd`, `wasm-debug`, cada um com build e test preset de mesmo nome; binários em `build/<preset>/{tests,bench}/` (WASM: `<nome>.js` + `<nome>.wasm`).

- [ ] **Step 1: Criar o branch** `git switch -c feat/wasm-cost-study`.

- [ ] **Step 2: `Dockerfile.wasm`** `FROM emscripten/emsdk:3.1.74`; instala `make`, `g++`, `g++-multilib` (libstdc++ i386 para V2), `binutils`, `python3-pip`; `pip install cmake==3.30.5`; `WORKDIR /app`. Rodar `/emsdk/upstream/bin/clang++ --print-targets` dentro da imagem: se listar `x86-64`, criar o link `clang++` → esse binário; senão instalar `clang-20` de `apt.llvm.org` e linkar `clang++` → `clang++-20`.

- [ ] **Step 3: Serviço `wasm` no `docker-compose.yaml`** igual ao `dev` (mesmos volumes, `command: bash`), com `build: { context: ., dockerfile: Dockerfile.wasm }`.

- [ ] **Step 4: Verificar a imagem**

Run: `docker compose run --rm wasm sh -c 'cmake --version && clang++ --version && emcc --version && node --version && echo "int main(){}" | clang++ -m32 -x c++ - -o /tmp/a && /tmp/a && echo ok32'`
Expected: CMake 3.30.5, Clang 20.x, emcc 3.1.74, node v20.18.x, `ok32`.

- [ ] **Step 5: Presets.** Preset oculto `variant` herdando `base`: `CMAKE_BUILD_TYPE=Release`, `ECS_SANITIZE=OFF`, `ECS_BUILD_TESTS=ON`, `ECS_BUILD_BENCH=ON`, `ECS_ENABLE_ASSERTS=0`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`. Oculto `variant-clang` (`CMAKE_CXX_COMPILER=clang++`) e `variant-wasm` (`toolchainFile: /emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`, `CMAKE_EXE_LINKER_FLAGS=--profiling-funcs`). Os visíveis só acrescentam o seu fator:

| Preset | Herda | `CMAKE_CXX_FLAGS` | `CMAKE_EXE_LINKER_FLAGS` |
|---|---|---|---|
| `native-clang` | `variant-clang` | — | — |
| `native-clang-32` | `variant-clang` | `-m32` | `-m32` |
| `native-clang-32-scalar` | `variant-clang` | `-m32 -fno-vectorize -fno-slp-vectorize` | `-m32` |
| `native-clang-avx2` | `variant-clang` | `-mavx2 -mfma` | — |
| `wasm-scalar` | `variant-wasm` | — | (herdado) |
| `wasm-simd` | `variant-wasm` | `-msimd128` | (herdado) |
| `wasm-debug` | `base` + toolchain do `variant-wasm` | — | — |

`wasm-debug`: `CMAKE_BUILD_TYPE=Debug`, `ECS_SANITIZE=OFF`, `ECS_BUILD_TESTS=ON`, `ECS_BUILD_BENCH=OFF`. Build e test presets de mesmo nome para todos (test com `outputOnFailure`).

- [ ] **Step 6: `CMakeLists.txt`** logo após as `option(...)`: `if(EMSCRIPTEN) add_link_options(-sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1) endif()`.

- [ ] **Step 7: Configurar todas as variantes**

Run: `for p in native-clang native-clang-32 native-clang-32-scalar native-clang-avx2 wasm-scalar wasm-simd wasm-debug; do $WC cmake --preset $p || break; done`
Expected: todas configuram. Em `build/wasm-scalar/CMakeCache.txt`, `CMAKE_CROSSCOMPILING_EMULATOR` aponta para o `node` do emsdk (se não, definir no preset `variant-wasm`). Em `build/native-clang/compile_commands.json` e `build/wasm-simd/compile_commands.json` aparecem `-O3` e, no segundo, `-msimd128`.

- [ ] **Step 8: Confirmar que o nativo não mudou** `$DC make test` e `$DC make build PRESET=release` passam (o `docker-compose.yaml`/presets mexidos não afetam `dev`).

- [ ] **Step 9: Commit** `build: imagem wasm com Clang nativo e presets das variantes do estudo`.

---

### Task 2: Portabilidade para Clang (§5.1, §5.2, §5.3, §5.5)

**Files:**
- Modify: `include/ecs/entity.hpp:35`, `include/ecs/detail/assert.hpp:18-21`, `tests/fuzz_vs_model.cpp:29`
- Modify: `bench/flush.cpp`, `bench/fragmentation.cpp`, `bench/iterate.cpp`, `bench/transitions.cpp` (só as chamadas a `DoNotOptimize`)
- Modify: `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md:355` (texto da NF-M1)

**Interfaces:**
- Consumes: presets `native-clang`, `native-clang-32` (Task 1).
- Produces: código que compila sem aviso em GCC 14 e Clang 20, 64 e 32 bits.

- [ ] **Step 1: Ver falhar** `$WC make build PRESET=native-clang-32`
Expected: FAIL com o erro do `[[noreturn]]` depois de `__attribute__`, `-Wunsequenced` em `fuzz_vs_model.cpp`, `-Wdeprecated-declarations` nos benches e o `static_assert` NF-M1 (12 ≠ 16).

- [ ] **Step 2: Correções**
  - `assert.hpp`: `[[noreturn]]` antes do `__attribute__((format(printf, 4, 5)))`.
  - `entity.hpp`: `static_assert(sizeof(EntityRecord) == sizeof(void*) + 8, "NF-M1: um ponteiro + 8 bytes por EntityRecord");`.
  - `fuzz_vs_model.cpp`: a decisão "incrementa antes" das Global Constraints.
  - Benches: cada `DoNotOptimize` com argumento rvalue/const passa a receber um local não-const (`auto* world = setup.world.get(); benchmark::DoNotOptimize(world);`; idem `positions.data()`; `sum` já é lvalue — conferir se o aviso vem dele e, se não vier, não tocar).
  - Spec v1, NF-M1: "um ponteiro + 8 bytes por `EntityRecord` (16 B em 64 bits, 12 B em 32 bits) mais 8 bytes de `Entity` na tabela".

- [ ] **Step 3: Clang 64 e 32 bits passam**
Run: `$WC make test PRESET=native-clang && $WC make test PRESET=native-clang-32`
Expected: todos os testes passam, zero avisos.

- [ ] **Step 4: GCC sem regressão** (Review Focus 4)
Run: `$DC make test && $DC make test PRESET=release`, depois `$DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=build/iterate_check.json` e `bench_ratio.py` para `BM_IterateSingle_ECS/<N>` vs `BM_IterateSingle_SoA/<N>` com os limites NF-P1/NF-P2 registrados em `bench/results/README.md`.
Expected: testes passam; razões dentro das metas (JSON fica em `build/`, não é versionado).

- [ ] **Step 5: Commit** `fix: portabilidade para Clang e ponteiros de 32 bits`.

---

### Task 3: Testes em WASM (§5.4, §5.6, §5.7)

**Files:**
- Modify: `CMakeLists.txt` (função `ecs_wasm_exceptions`)
- Modify: `tests/CMakeLists.txt` (aplicar em `ecs_add_test`; `contract_death` sem)
- Create (só se o Step 4 falhar): `tools/wasm/node_env.js`

**Interfaces:**
- Consumes: preset `wasm-debug`, `wasm-scalar`, `wasm-simd` (Task 1); correções da Task 2.
- Produces: `ecs_wasm_exceptions(<target>)` — no-op fora do Emscripten; nele, `target_compile_options` e `target_link_options` `PRIVATE -fwasm-exceptions`.

- [ ] **Step 1: Ver falhar** `$WC make test PRESET=wasm-debug`
Expected: FAIL — `queries` (o `CHECK_THROWS` aborta sem suporte a exceções).

- [ ] **Step 2: Implementar `ecs_wasm_exceptions`** ao lado de `ecs_warnings` e chamá-la dentro de `ecs_add_test`. `contract_death` e benches não a chamam.

- [ ] **Step 3: Todos os testes passam**
Run: `$WC make test PRESET=wasm-debug && $WC make test PRESET=wasm-scalar && $WC make test PRESET=wasm-simd`
Expected: 10 suítes + 5 `contract_death.*` passam em `wasm-debug`; as 10 suítes passam nas duas de Release. Se o link misturando objetos com e sem `-fwasm-exceptions` falhar, aplicar o fallback das Global Constraints e repetir.

- [ ] **Step 4: Semente reproduzível** (Review Focus 1)
Run: `$WC sh -c 'ECS_FUZZ_SEED=7 ECS_FUZZ_OPS=1000 ctest --preset wasm-debug -R fuzz_vs_model -V' | grep "seed="`
Expected: `[fuzz_vs_model] seed=7 ops=1000`. Se mostrar o padrão (`seed=3781 ops=1000000`), criar `tools/wasm/node_env.js` com `Module.preRun = [() => Object.assign(ENV, process.env)];` e ligá-lo com `--pre-js` só nos testes WASM (dentro de `ecs_wasm_exceptions` ou função irmã), e repetir.

- [ ] **Step 5: Commit** `test: testes e contract_death rodando em WASM via node`.

---

### Task 4: Benchmarks em todas as variantes

**Files:** nenhum código novo esperado; ajustes só se algo falhar.

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: executáveis de benchmark em `build/release/bench/` (V0) e `build/<preset>/bench/` das seis variantes.

- [ ] **Step 1: Compilar** `$DC make build PRESET=release` e, para cada preset de variante, `$WC make build PRESET=<preset>`.
Expected: tudo compila sem aviso.

- [ ] **Step 2: Fumaça** — cada benchmark de cada variante com `--benchmark_min_time=0.01s --benchmark_format=json` (WASM: `node build/<preset>/bench/<bench>.js ...`).
Expected: nenhum resultado com `"error_occurred": true`; todos os nomes de §6 presentes (inclusive os de 1M entidades em WASM).

- [ ] **Step 3: Commit** só se algo precisou mudar (`fix: ...`).

---

### Task 5: Escada de custos — runner e tabela

**Files:**
- Create: `tools/bench_ladder.py`, `tools/test_bench_ladder.py`, `tools/run_ladder.sh`

**Interfaces:**
- Consumes: binários da Task 4.
- Produces:
  - `tools/run_ladder.sh <rodadas> <bench> [variantes...]` (roda no host, Git Bash/POSIX). Variantes padrão `V0 V1 V2 V2S V3 V4 VX`, rótulos fixos: `V0=release@dev`, `V1=native-clang`, `V2=native-clang-32`, `V2S=native-clang-32-scalar`, `V3=wasm-scalar`, `V4=wasm-simd`, `VX=native-clang-avx2` (os não-V0 no serviço `wasm`; WASM via `node <bench>.js`). Intercala: para k em 1..rodadas, para cada variante, roda com o protocolo de §6 e grava `bench/results/wasm/runs/<bench>/<variante>_r<k>.json`.
  - `tools/bench_ladder.py --run <VARIANTE>=<json> ... [--pair ECS=SoA] [--filter <regex>]` → Markdown no stdout. Funções: `load_medians(path) -> dict[str, float]` (nome sem `_median` → `real_time` em ns); `step_ratio(rounds_a, rounds_b, name) -> tuple[float, float, float] | None` (mediana, mín, máx das razões rodada a rodada); `render_markdown(...) -> str`.
  - Tabelas: (1) mediana entre rodadas por variante, com mín–máx; (2) degraus `V1/V0, V2/V1, V3/V2, V3/V2S, V4/V3, V4/V2, V4/V0, VX/V1`, cada um com mediana [mín–máx]; (3) com `--pair ECS=SoA`, para cada nome contendo `ECS`, a razão ECS/SoA por variante. Cabeçalho fixo: "Razões entre variantes vêm de execuções diferentes (intercaladas); ver dispersão."
  - Saída de erro: exit 2 se um JSON tem `error_occurred`, se um JSON não abre, ou se variantes têm nº de rodadas diferente.

- [ ] **Step 1: Testes** em `tools/test_bench_ladder.py` (`unittest`, fixtures JSON escritos em `tempfile`; o arquivo põe o próprio diretório no `sys.path`, já que `-I` o remove):
  - `test_median_across_rounds`: rodadas 10, 12, 11 ns → 11.
  - `test_time_unit_converted_to_ns`: `real_time: 2, time_unit: "us"` → 2000.
  - `test_step_ratio_pairs_rounds`: A [20, 30], B [10, 10] → (2.5, 2.0, 3.0).
  - `test_missing_benchmark_renders_dash`: nome só em V0 → célula `—` na coluna de V1 e degrau `V1/V0` com `—`.
  - `test_error_occurred_exits_2` e `test_unequal_rounds_exits_2` (via `main([...])`, conferindo o código de saída).
  - `test_pair_normalization`: `BM_X_ECS/1000`=30, `BM_X_SoA/1000`=10 → 3.00 na tabela 3.

- [ ] **Step 2: Ver falhar** `$WC python3 -I tools/test_bench_ladder.py` → FAIL (`ModuleNotFoundError: bench_ladder`).

- [ ] **Step 3: Implementar `tools/bench_ladder.py`** com a interface acima (só biblioteca padrão; ordem dos degraus fixa no código, degrau com variante ausente é omitido).

- [ ] **Step 4: Ver passar** `$WC python3 -I tools/test_bench_ladder.py` → OK.

- [ ] **Step 5: Implementar `tools/run_ladder.sh`** e testá-lo com `1 bench_iterate V0 V3` e `--benchmark_min_time=0.01s` repassado por variável `ECS_LADDER_EXTRA_ARGS` (o script anexa ao protocolo).
Expected: dois JSONs em `bench/results/wasm/runs/bench_iterate/`; `bench_ladder.py` sobre eles imprime as três tabelas. Apagar esses JSONs de teste.

- [ ] **Step 6: Commit** `feat: scripts da escada de custos entre variantes`.

---

### Task 6: Extração do código gerado (§7)

**Files:**
- Create: `tools/extract_codegen.sh`

**Interfaces:**
- Produces: `tools/extract_codegen.sh <binário> <regex-da-função> <saída>` — `.wasm` → `/emsdk/upstream/bin/llvm-objdump -d --demangle`; senão `objdump -d --no-show-raw-insn -M intel -C`. Grava só o corpo da função cujo nome demanglado casa com a regex. Exit 1 com "função não encontrada" se zero casarem, "regex ambígua: <nomes>" se mais de uma (Review Focus 5).

- [ ] **Step 1: Ver os casos de erro** — rodar com regex inexistente (`BM_NaoExiste`) e com uma ambígua (`BM_Iterate`) em `build/native-clang/bench/bench_iterate`. Expected: exit 1 com as mensagens acima, nenhum arquivo criado.

- [ ] **Step 2: Extrair os laços alvo** de V0 (no `dev`), V1, V2, V2S, V3, V4, VX para `bench/results/wasm/codegen/<variante>/<alvo>.txt`:
  - `iterate_ecs`: `BM_IterateSingle_ECS` (corpo do `eachBlock` inlinado) — `bench_iterate`
  - `iterate_soa`: `BM_IterateSingle_SoA` — `bench_iterate`
  - `transition`: `ecs::World::transitionTo` e a função de mover linha que ela chama (`moveRow` ou equivalente em `src/`) — `bench_transitions`
  - `flush`: `ecs::World::flush(ecs::CommandBuffer&)` — `bench_flush`
Expected: arquivos não vazios. Se uma função foi inteiramente inlinada numa variante, registrar no arquivo uma linha dizendo em qual função ela aparece e extrair essa.

- [ ] **Step 3: Commit** `feat: extração do código gerado dos laços quentes por variante`.

---

### Task 7: Medição, relatório e documentação (§6, §7, §8)

**Files:**
- Create: `bench/results/wasm/README.md`, `bench/results/wasm/runs/**.json`
- Modify: `bench/results/README.md` (linha na tabela de arquivos apontando para `wasm/`), `CLAUDE.md` (Build/Benchmarks: serviço `wasm`, presets, scripts), `Makefile` só se um alvo encurtar comandos de fato

- [ ] **Step 1: Medir** `tools/run_ladder.sh 3 <bench>` para `bench_iterate`, `bench_fragmented`, `bench_transitions`, `bench_flush`, numa mesma sessão, máquina ociosa.
Expected: 3 × 7 JSONs por bench, nenhum com `error_occurred`.

- [ ] **Step 2: Tabelas** `bench_ladder.py` por bench (`--pair ECS=SoA` em `bench_iterate`; em `bench_fragmented` usar a linha de base SoA dele) e coletar as razões intra-variante de §6 com `bench_ratio.py`.

- [ ] **Step 3: `bench/results/wasm/README.md`** com: máquina; versões exatas (GCC, Clang com origem, Emscripten, node e V8 de `node -p process.versions.v8`, libstdc++ vs libc++); as tabelas da escada separando tamanhos que cabem em cache dos limitados por memória; o ramo AVX2; a ressalva de execuções diferentes; para cada degrau relevante, os trechos lado a lado de `codegen/` e a causa apontada (vetorizou ou não, largura, desenrolamento, checagem de limites, `i64`/`i32`, chamadas não inlinadas). Uma afirmação de causa só entra com a variante que isola o fator e o trecho que a mostra (§2); o que não se explica fica marcado como não explicado. NF-P* em WASM: medidas e reportadas, não exigidas.

- [ ] **Step 4: Docs do projeto** — `CLAUDE.md` ganha `WC="docker compose run --rm wasm"`, os presets novos e os comandos de `run_ladder.sh`/`bench_ladder.py`/`extract_codegen.sh`; `bench/results/README.md` aponta para `wasm/README.md`.

- [ ] **Step 5: Verificação final**
Run: `$DC make test && $DC make test PRESET=release && $WC make test PRESET=wasm-debug && $WC make test PRESET=native-clang-32 && $WC python3 -I tools/test_bench_ladder.py`
Expected: tudo passa.

- [ ] **Step 6: Commit** `docs: relatório do estudo de custo nativo vs WASM`.
