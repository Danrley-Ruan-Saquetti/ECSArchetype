# Build, testes e ferramentas

Os comandos do dia a dia estão em [../development/commands.md](../development/commands.md). Este arquivo explica a organização.

## Toolchain

Não há toolchain C++ no host. Tudo roda em Docker Compose:

| Serviço | Imagem | Uso |
|---|---|---|
| `dev` | `Dockerfile` (`gcc:14`, CMake, perf) | build e testes nativos |
| `perf` | `Dockerfile`, privilegiado | `perf stat` |
| `wasm` | `Dockerfile.wasm` (`emscripten/emsdk:3.1.74`, Clang do emsdk como compilador nativo, node 20.18, Python) | WASM, TS, estudo de custo |
| `web` | `Dockerfile.wasm`, porta 8000 | `make serve` |

## Presets

| Preset | Para quê |
|---|---|
| `debug-asan` (padrão) | testes com ASan + UBSan e asserts ligados |
| `release` | testes e benchmarks com `OLY_ENABLE_ASSERTS=0` |
| `relwithdebinfo` | benchmarks para `perf` |
| `native-clang`, `native-clang-32`, `native-clang-32-scalar`, `native-clang-avx2`, `wasm-scalar`, `wasm-simd` | escada do estudo de custo nativo × WASM |
| `wasm-debug` | testes do ECS em WASM |
| `wasm-games`, `wasm-games-release` | jogos, `bridge`, testes TS |

Cada degrau da escada do estudo de custo muda um único fator (compilador, largura de ponteiro, alvo WASM, SIMD).

Opções do CMake: `OLY_BUILD_TESTS`, `OLY_BUILD_BENCH`, `OLY_SANITIZE`, `OLY_WERROR`, `OLY_VEC_REPORT`, `OLY_ENABLE_ASSERTS`, `OLY_BUILD_GAMES`.

## Funções de build (`cmake/`)

| Função | O que faz |
|---|---|
| `oly_add_module` | cria `oly_<m>` (alias `oly::<m>`) e liga só os `DEPENDS` |
| `oly_add_test` | registra uma suíte doctest como executável e teste do ctest |
| `oly_add_contract_death` | registra os casos de morte de um módulo |
| `oly_add_bench` | registra um executável de benchmark |
| `oly_add_game` | roda o codegen e linka o módulo `.mjs`/`.wasm` de um jogo |
| `oly_warnings`, `oly_wasm_exceptions`, `oly_wasm_node_env` | avisos e ajustes do Emscripten |

## Testes

- **Uma suíte doctest = um executável**, registrado com `oly_add_test(MODULE <m> NAME <suíte> SOURCES ... [SUPPORT <dirs>])` no `tests/CMakeLists.txt` do módulo. Arquivo de teste novo precisa ser adicionado lá; as fontes de `src/*.cpp` entram por glob.
- **Nomes no ctest** levam o prefixo do módulo: `core.*`, `ecs.*`, `input.*`, `app.*`, `bridge.*`, `<módulo>.contract_death.<caso>`, `ts.<pacote>.<suíte>` (`ts.core.unit`, `ts.codegen.unit`, `ts.client.unit`, `ts.testing.wasm`, `ts.arena.smoke`) e `oly.lint.boundaries` / `oly.lint.boundaries_selftest`.
- **Testes de morte**: registrados com `oly_add_contract_death(MODULE <m> SOURCE <arq> CASES ...)`. Cada caso é um argumento de linha de comando. Um handler de assert confere substrings da mensagem e sai com 0; se o caso não dispara o assert, falha. Caso novo = função no arquivo + nome na lista `CASES`. Existem em `ecs`, `input`, `app` e `bridge`, só com asserts ligados.
- **Invariantes**: `checkInvariants(const World&, std::string* why)` verifica I-1 a I-11 (records, colunas, índices, arestas, queries, lista livre, modo iterando). Os testes a chamam depois de cada operação estrutural.
- **Fuzz contra modelo**: `fuzz_vs_model` compara o `World` com um mapa trivial. Os valores derivam da `Entity`, então trocar linhas aparece como valor errado. Semente e número de operações por `OLY_FUZZ_SEED`/`OLY_FUZZ_OPS`.
- **`LifeCounter`** conta construções − destruições e pega moved-from não destruído e double destroy.
- **Testes TS** rodam em node contra o WASM real, não contra um mock.

## Qualidade de código

- Avisos, formatação e regras de estilo: [../development/code-style.md](../development/code-style.md).
- O lint de fronteiras roda fora do Emscripten e nos presets `wasm-games*`, que têm Python no ambiente ([modules-and-boundaries.md](modules-and-boundaries.md)).

## Automação do Claude Code (`.claude/`)

- Skills do projeto: `add-test-case` (registrar suíte, `TEST_CASE` ou caso de `contract_death`) e `bench-compare` (só manual, `/bench-compare <prefixo>`: mede NF-P1/P2/P5 e grava em `docs/benchmarks/`).
- Agentes: `spec-conformance-reviewer` (spec, invariantes e cobertura de `checkInvariants`) e `perf-reviewer` (custo no caminho quente, conferido no assembly).
- Hook `PostToolUse` `.claude/hooks/clang-format.sh`: formata `.cpp`/`.hpp` após cada edição.
