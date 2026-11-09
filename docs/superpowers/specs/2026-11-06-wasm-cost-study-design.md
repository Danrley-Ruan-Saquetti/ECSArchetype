# Estudo de custo: ECS nativo vs WebAssembly — design

Data: 2026-11-06. Complementa `2026-10-16-ecs-archetype-spec.md`; não altera requisitos `R-*` nem invariantes `I-*`
(a NF-M1 é reescrita, ver §5.1).

## 1. Intenção

Entender **quanto custa cada fator** que separa o ECS compilado para x86-64 nativo do mesmo ECS compilado para
WebAssembly, e **por quê**, combinando duas fontes de evidência:

1. **Medição** numa matriz de builds em que cada variante muda um único fator em relação à anterior.
2. **Leitura do código gerado** (assembly x86 vs WASM) dos laços quentes, explicando cada salto de custo da escada.

É um estudo de desempenho, não um produto: não há API para JS, nem bindings (embind), nem uso em navegador.

## 2. Objetivos, não-objetivos, anti-objetivos

**Objetivos**
- A biblioteca, os testes e os benchmarks compilam e passam em WASM (Emscripten) dentro do Docker.
- Matriz de variantes (§4) construída de forma reproduzível por presets/targets do projeto.
- Relatório versionado com a escada de custos e trechos comentados de código gerado.

**Não-objetivos**
- Executar em navegadores (Firefox/SpiderMonkey, Chrome, Safari). Só `node` (V8), no container.
- Expor o ECS para JavaScript; custo da fronteira JS↔WASM.
- Threads/`SharedArrayBuffer`, wasm64 (`MEMORY64`) como variante.
- Atender as metas `NF-P*` em WASM. Elas são **medidas e reportadas** em WASM, não exigidas.

**Anti-objetivos** (falha mesmo que funcione)
- Piorar o build nativo atual: os presets existentes, os testes e os números de `bench/results/README.md` não podem
  regredir. As correções de portabilidade não podem mudar o comportamento no GCC.
- Comparar tempos absolutos de execuções diferentes como se fossem da mesma execução, sem dizer isso
  (o ambiente WSL2 é ruidoso, ver §6).
- Atribuir uma diferença a um fator sem que a variante isole aquele fator, ou sem evidência no código gerado.

## 3. Restrições

- Tudo roda em Docker (não há toolchain C++ no host). A imagem atual (`gcc:14`) continua sendo a do caminho nativo.
- O Emscripten é **fixado** em `emscripten/emsdk:3.1.74`, a versão validada no spike. Essa imagem traz CMake 3.22,
  e o projeto exige 3.25+, então a imagem derivada precisa de um CMake mais novo (o spike usou `pip install cmake==3.30.5`).
- A execução de WASM é feita por `node` (o que vem no emsdk: 20.18), via `CMAKE_CROSSCOMPILING_EMULATOR`
  (o `emcmake` já o define), para que `ctest` funcione igual ao nativo.
- `-Wall -Wextra -Wpedantic -Werror` continuam ligados em todos os nossos alvos e em todas as variantes, inclusive Clang.
- Estilo e idioma do projeto (`CLAUDE.md`): commits, comentários e mensagens em pt-BR.

## 4. A matriz de variantes

Cada degrau muda **um** fator em relação ao anterior. Todas as variantes de medição usam Release, asserts
desligados (`ECS_ENABLE_ASSERTS=0`) e o mesmo `-O` (o do `CMAKE_BUILD_TYPE=Release`, `-O3` em ambos os compiladores).

| # | Variante | Compilador / alvo | Fator isolado (vs. degrau anterior) |
|---|---|---|---|
| V0 | `native-gcc` | GCC 14, x86-64, SSE2 (padrão, sem `-march`) | linha de base atual |
| V1 | `native-clang` | Clang, x86-64, SSE2 | compilador (GCC → Clang) |
| V2 | `native-clang-32` | Clang, ponteiros de 32 bits (`-m32`), SSE2 | largura de ponteiro/`size_t` (ex.: `EntityRecord` 16 → 12 B) |
| V3 | `wasm-scalar` | Emscripten (Clang), wasm32, sem `-msimd128` | alvo WASM (memória linear, JIT V8, modelo de pilha) |
| V4 | `wasm-simd` | Emscripten, wasm32, `-msimd128` | vetorização SIMD128 no WASM |
| VX | `native-clang-avx2` | Clang, x86-64, `-mavx2 -mfma` | **ramo lateral** de V1: o quanto o nativo ainda deixa na mesa com vetores de 256 bits |

Observações de desenho:
- O nativo atual vetoriza com SSE2 (128 bits), a mesma largura do SIMD128. Logo a diferença V1 → V4 **não** é largura
  de vetor; o estudo deve dizer o que é.
- V2 → V3 é o par mais fiel para "custo do alvo": ambos têm ponteiros de 32 bits.
- V3 é escalar para o WASM, mas V2 tem SSE2. A comparação V2 → V3 mistura alvo com SIMD. O relatório deve deixar isso
  explícito e apresentar também V2 → V4 (mesma largura de vetor, alvos diferentes). Se for barato, uma variante
  nativa com `-fno-vectorize -fno-slp-vectorize` pode servir de par escalar para V3. Fica a critério do construtor.
- Se `-m32` (V2) não for viável no container (multilib/i386 do libc++/libstdc++ e Google Benchmark), o construtor
  documenta o motivo no relatório e a escada pula de V1 para V3, registrando que o fator "ponteiro" fica confundido com
  o fator "alvo". `-mx32` é alternativa aceitável se for o que funcionar.
- O compilador Clang nativo deve ser de versão próxima à do Emscripten 3.1.74 (LLVM 20-dev), para que V1 → V4 não
  misture versões muito diferentes de LLVM. A versão exata usada é registrada no relatório.

## 5. Pré-requisitos de portabilidade (encontrados no spike)

O spike (builds descartáveis em `build/wasm-spike-*`) mostrou que, com os ajustes abaixo, os 15 testes passam em WASM
e os benchmarks rodam. Esses ajustes viram mudanças reais, e todas também valem para o Clang nativo (V1).

### 5.1 `EntityRecord` e a NF-M1

`include/ecs/entity.hpp:35` faz `static_assert(sizeof(EntityRecord) == 16)`. No wasm32 (e em `-m32`) o record tem 12 bytes.
Decisão: o `static_assert` passa a ser `sizeof(EntityRecord) == sizeof(void*) + 8`, e a redação da NF-M1 na spec passa
a ser "um ponteiro + 8 bytes por `EntityRecord` (16 B em 64 bits, 12 B em 32 bits)". O layout não muda.

### 5.2 Ordem de atributos em `assertFail`

`include/ecs/detail/assert.hpp:18-21`: o Clang rejeita `[[noreturn]]` depois de `__attribute__((format(...)))`.
Colocar `[[noreturn]]` primeiro.

### 5.3 `-Wunsequenced` em `fuzz_vs_model`

`tests/fuzz_vs_model.cpp:29`: `makeValue(ecs::Entity{uint32_t(counter), 0}, ++counter)` lê e incrementa `counter` em
argumentos da mesma chamada. A ordem de avaliação é indeterminada, então o valor muda entre compiladores. Corrigir
separando o incremento num comando próprio. A semântica escolhida (índice antes ou depois do incremento) é indiferente
para o teste, que compara o `World` com o modelo usando o mesmo valor, mas deve ser a mesma em todos os compiladores.

### 5.4 `contract_death` e `std::_Exit` sob exceções WASM

O Emscripten implementa `exit`/`_Exit` lançando uma exceção do lado JS. Com `-fwasm-exceptions`, ela atravessa o
`handler` `noexcept` de `tests/contract_death.cpp` e vira `std::terminate`. No spike, compilar só o `contract_death`
**sem** suporte a exceções resolveu, e os 5 casos passaram. Decisão: o `contract_death` não usa doctest e é compilado
sem flag de exceções no WASM. Os demais testes usam `-fwasm-exceptions`, porque o doctest captura exceções.

### 5.5 `DoNotOptimize` obsoleto nos benchmarks

O Clang emite `-Wdeprecated-declarations` para `benchmark::DoNotOptimize` com argumento `const&` (rvalue/temporário)
em `bench/flush.cpp`, `bench/fragmentation.cpp`, `bench/iterate.cpp`, `bench/transitions.cpp`. Corrigir passando
lvalues não-const, sem mudar o que é medido. O GCC não avisa, mas a correção vale para os dois.

### 5.6 Linker do WASM

Flags necessárias no spike: `-sALLOW_MEMORY_GROWTH=1` (benchmarks com 1M entidades) e `-sEXIT_RUNTIME=1`
(códigos de saída corretos para o `ctest`).

### 5.7 A confirmar durante a implementação

- `getenv("ECS_FUZZ_SEED"/"ECS_FUZZ_OPS")` (`tests/support/model.hpp:108`): verificar se o `node` repassa as variáveis
  de ambiente ao programa. Se não repassar, usar `-sNODERAWFS`/`--pre-js` copiando `process.env` para `ENV`.
  Reproduzir uma semente precisa funcionar em WASM como no nativo.
- ASan/UBSan em WASM (`-fsanitize=address,undefined`): o Emscripten suporta. Um preset `wasm-asan` é desejável, mas
  não obrigatório. Se custar caro (memória, tempo do fuzz), basta um preset debug sem sanitizers.

## 6. Medição

- Benchmarks: os quatro existentes (`bench_iterate`, `bench_fragmented`, `bench_transitions`, `bench_flush`), sem
  alterar o que medem, em todas as variantes.
- Protocolo: `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`, saída JSON. Medianas.
- **Dentro de uma variante**, as razões (ECS/SoA, flush/adds diretos etc.) saem da mesma execução, como hoje.
- **Entre variantes** não há "mesma execução". O custo de cada degrau é apresentado de duas formas:
  1. Razão de tempos medianos entre variantes, com a ressalva explícita de ruído. Para reduzir o ruído, as variantes
     de uma escada rodam em sequência, intercaladas (V0, V1, …, V0, V1, …), na mesma sessão, e o relatório mostra a
     dispersão entre as rodadas.
  2. Razão normalizada pela linha de base SoA manual de cada variante, quando o benchmark tem uma.
- Separar sempre os tamanhos que cabem no cache dos limitados pela memória. Spike (uma execução, indício apenas):
  a 1k–100k, o WASM SIMD ficou ~4× mais lento que o GCC nativo; a 1M, empataram.
- `tools/bench_ratio.py` é reusado para razões intra-variante. Comparações entre variantes ganham um script novo em
  `tools/` (ler vários JSONs e montar a tabela da escada).
- `perf` não existe para WASM e não é exigido. Contadores de hardware ficam só nas variantes nativas, se ajudarem a
  explicar um degrau.

## 7. Leitura do código gerado

- Laços alvo: o corpo de `eachBlock` em `BM_IterateSingle_ECS` e o equivalente SoA, a transição com aresta em cache
  (`transition`/`moveRow` usados por `add`/`remove`) e o laço central do `flush`.
- Ferramentas: `objdump -d --no-show-raw-insn -M intel` (ou `-S` com `relwithdebinfo`) no nativo; `wasm-objdump -d` ou
  `wasm2wat` (wabt; o emsdk traz `llvm-objdump`, que também desmonta WASM) no WASM. Para ver o que o V8 gera de fato
  a partir do WASM, `node --print-wasm-code` (se disponível na build do node) é bem-vindo, mas opcional.
- Para cada degrau relevante, o relatório mostra o trecho lado a lado e aponta a causa (vetorizou ou não, largura,
  desenrolamento, checagem de limites, conversões `i64`/`i32`, chamadas indiretas que não sofreram inline etc.).
- Os trechos extraídos ficam versionados como texto em `bench/results/wasm/` (não binários).

## 8. Entregáveis

- Presets novos em `CMakePresets.json` para as variantes de §4 (nomes indicativos: `native-clang`, `native-clang-32`,
  `native-clang-avx2`, `wasm-scalar`, `wasm-simd` e um preset de testes WASM). Os presets atuais permanecem.
- Serviço(s) novo(s) no `compose.yaml` para o caminho Clang/WASM. O serviço `dev` atual (GCC) não muda de comportamento.
- Comandos documentados no `CLAUDE.md` (seção Build/Benchmarks) e no `Makefile` se fizer sentido.
- JSONs de resultado em `bench/results/wasm/` e uma tabela/registro no `bench/results/README.md`.
- Relatório em Markdown versionado (`bench/results/wasm/README.md` ou `docs/`, a critério do construtor) com:
  a máquina e as versões (GCC, Clang, Emscripten, node), a escada de custos por benchmark e por tamanho, o ramo AVX2 e
  as explicações pelo código gerado.

## 9. O que fica a critério do construtor

- A organização das imagens Docker (uma imagem nova derivada do `emsdk` com Clang nativo + CMake, ou duas).
- Como os presets são parametrizados (toolchain file, `cacheVariables`, herança).
- O par nativo escalar para V3 (§4) e a viabilidade de `-m32` vs `-mx32`.
- O formato exato das tabelas e do script de comparação entre variantes.
- O preset `wasm-asan` (§5.7).
