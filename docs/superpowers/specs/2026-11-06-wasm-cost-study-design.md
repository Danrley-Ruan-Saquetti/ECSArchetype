# Cost study: native ECS vs WebAssembly — design

Date: 2026-11-06. Complements `2026-10-16-ecs-archetype-spec.md`; does not change `R-*` requirements or `I-*` invariants
(NF-M1 is rewritten, see §5.1).

## 1. Intent

Understand **how much each factor costs** that separates the ECS compiled for native x86-64 from the same ECS compiled for
WebAssembly, and **why**, by combining two sources of evidence:

1. **Measurement** over a build matrix in which each variant changes a single factor relative to the previous one.
2. **Reading the generated code** (x86 assembly vs WASM) of the hot loops, explaining each cost step of the ladder.

It is a performance study, not a product: there is no JS API, no bindings (embind) and no browser use.

## 2. Goals, non-goals, anti-goals

**Goals**
- The library, the tests and the benchmarks compile and pass in WASM (Emscripten) inside Docker.
- Variant matrix (§4) built reproducibly by the project's presets/targets.
- Versioned report with the cost ladder and annotated excerpts of generated code.

**Non-goals**
- Running in browsers (Firefox/SpiderMonkey, Chrome, Safari). Only `node` (V8), in the container.
- Exposing the ECS to JavaScript; cost of the JS↔WASM boundary.
- Threads/`SharedArrayBuffer`, wasm64 (`MEMORY64`) as a variant.
- Meeting the `NF-P*` targets in WASM. They are **measured and reported** in WASM, not required.

**Anti-goals** (failure even if it works)
- Making the current native build worse: the existing presets, the tests and the numbers in `bench/results/README.md` must not
  regress. The portability fixes must not change behaviour on GCC.
- Comparing absolute times from different runs as if they came from the same run, without saying so
  (the WSL2 environment is noisy, see §6).
- Attributing a difference to a factor without the variant isolating that factor, or without evidence in the generated code.

## 3. Constraints

- Everything runs in Docker (there is no C++ toolchain on the host). The current image (`gcc:14`) remains the one for the native path.
- Emscripten is **pinned** at `emscripten/emsdk:3.1.74`, the version validated in the spike. That image ships CMake 3.22,
  and the project requires 3.25+, so the derived image needs a newer CMake (the spike used `pip install cmake==3.30.5`).
- WASM runs on `node` (the one shipped with emsdk: 20.18), via `CMAKE_CROSSCOMPILING_EMULATOR`
  (`emcmake` already sets it), so that `ctest` works the same as on native.
- `-Wall -Wextra -Wpedantic -Werror` stay on in all our targets and in all variants, including Clang.
- Project style and language (`CLAUDE.md`): commits, comments and messages in English.

## 4. The variant matrix

Each step changes **one** factor relative to the previous one. All measurement variants use Release, asserts
off (`ECS_ENABLE_ASSERTS=0`) and the same `-O` (the one from `CMAKE_BUILD_TYPE=Release`, `-O3` in both compilers).

| # | Variant | Compiler / target | Isolated factor (vs. previous step) |
|---|---|---|---|
| V0 | `native-gcc` | GCC 14, x86-64, SSE2 (default, no `-march`) | current baseline |
| V1 | `native-clang` | Clang, x86-64, SSE2 | compiler (GCC → Clang) |
| V2 | `native-clang-32` | Clang, 32-bit pointers (`-m32`), SSE2 | pointer/`size_t` width (e.g. `EntityRecord` 16 → 12 B) |
| V3 | `wasm-scalar` | Emscripten (Clang), wasm32, no `-msimd128` | WASM target (linear memory, V8 JIT, stack model) |
| V4 | `wasm-simd` | Emscripten, wasm32, `-msimd128` | SIMD128 vectorization in WASM |
| VX | `native-clang-avx2` | Clang, x86-64, `-mavx2 -mfma` | **side branch** of V1: how much native still leaves on the table with 256-bit vectors |

Design notes:
- The current native build vectorizes with SSE2 (128 bits), the same width as SIMD128. So the V1 → V4 difference is **not** vector
  width; the study must say what it is.
- V2 → V3 is the most faithful pair for "target cost": both have 32-bit pointers.
- V3 is scalar for WASM, but V2 has SSE2. The V2 → V3 comparison mixes target with SIMD. The report must make this
  explicit and also present V2 → V4 (same vector width, different targets). If cheap, a native variant
  with `-fno-vectorize -fno-slp-vectorize` can serve as the scalar pair for V3. Left to the builder's discretion.
- If `-m32` (V2) is not viable in the container (multilib/i386 of libc++/libstdc++ and Google Benchmark), the builder
  documents the reason in the report and the ladder jumps from V1 to V3, recording that the "pointer" factor is confounded with
  the "target" factor. `-mx32` is an acceptable alternative if that is what works.
- The native Clang compiler must be of a version close to that of Emscripten 3.1.74 (LLVM 20-dev), so that V1 → V4 does not
  mix very different LLVM versions. The exact version used is recorded in the report.

## 5. Portability prerequisites (found in the spike)

The spike (throwaway builds in `build/wasm-spike-*`) showed that, with the adjustments below, the 15 tests pass in WASM
and the benchmarks run. These adjustments become real changes, and all of them also apply to native Clang (V1).

### 5.1 `EntityRecord` and NF-M1

`include/ecs/entity.hpp:35` does `static_assert(sizeof(EntityRecord) == 16)`. On wasm32 (and with `-m32`) the record is 12 bytes.
Decision: the `static_assert` becomes `sizeof(EntityRecord) == sizeof(void*) + 8`, and the wording of NF-M1 in the spec becomes
"one pointer + 8 bytes per `EntityRecord` (16 B on 64-bit, 12 B on 32-bit)". The layout does not change.

### 5.2 Attribute order in `assertFail`

`include/ecs/detail/assert.hpp:18-21`: Clang rejects `[[noreturn]]` after `__attribute__((format(...)))`.
Put `[[noreturn]]` first.

### 5.3 `-Wunsequenced` in `fuzz_vs_model`

`tests/fuzz_vs_model.cpp:29`: `makeValue(ecs::Entity{uint32_t(counter), 0}, ++counter)` reads and increments `counter` in
arguments of the same call. Evaluation order is unspecified, so the value changes between compilers. Fix it by
moving the increment into its own statement. The chosen semantics (index before or after the increment) does not matter
for the test, which compares the `World` with the model using the same value, but it must be the same on every compiler.

### 5.4 `contract_death` and `std::_Exit` under WASM exceptions

Emscripten implements `exit`/`_Exit` by throwing an exception on the JS side. With `-fwasm-exceptions`, it crosses the
`noexcept` `handler` of `tests/contract_death.cpp` and becomes `std::terminate`. In the spike, compiling only `contract_death`
**without** exception support solved it, and the 5 cases passed. Decision: `contract_death` does not use doctest and is compiled
without the exceptions flag in WASM. The other tests use `-fwasm-exceptions`, because doctest catches exceptions.

### 5.5 Deprecated `DoNotOptimize` in the benchmarks

Clang emits `-Wdeprecated-declarations` for `benchmark::DoNotOptimize` with a `const&` argument (rvalue/temporary)
in `bench/flush.cpp`, `bench/fragmentation.cpp`, `bench/iterate.cpp`, `bench/transitions.cpp`. Fix by passing
non-const lvalues, without changing what is measured. GCC does not warn, but the fix applies to both.

### 5.6 WASM linker

Flags needed in the spike: `-sALLOW_MEMORY_GROWTH=1` (benchmarks with 1M entities) and `-sEXIT_RUNTIME=1`
(correct exit codes for `ctest`).

### 5.7 To be confirmed during implementation

- `getenv("ECS_FUZZ_SEED"/"ECS_FUZZ_OPS")` (`tests/support/model.hpp:108`): check whether `node` passes environment
  variables through to the program. If it does not, use `-sNODERAWFS`/`--pre-js` copying `process.env` into `ENV`.
  Reproducing a seed must work in WASM as it does on native.
- ASan/UBSan in WASM (`-fsanitize=address,undefined`): Emscripten supports it. A `wasm-asan` preset is desirable but
  not mandatory. If it is expensive (memory, fuzz time), a debug preset without sanitizers is enough.

## 6. Measurement

- Benchmarks: the four existing ones (`bench_iterate`, `bench_fragmented`, `bench_transitions`, `bench_flush`), without
  changing what they measure, in every variant.
- Protocol: `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`, JSON output. Medians.
- **Within a variant**, the ratios (ECS/SoA, flush/direct adds etc.) come from the same run, as today.
- **Across variants** there is no "same run". The cost of each step is presented in two ways:
  1. Ratio of median times between variants, with the explicit noise caveat. To reduce noise, the variants
     of a ladder run in sequence, interleaved (V0, V1, …, V0, V1, …), in the same session, and the report shows the
     spread across rounds.
  2. Ratio normalized by each variant's manual SoA baseline, when the benchmark has one.
- Always separate the sizes that fit in cache from the memory-bound ones. Spike (one run, a hint only):
  at 1k–100k, WASM SIMD was ~4× slower than native GCC; at 1M, they tied.
- `tools/bench_ratio.py` is reused for intra-variant ratios. Cross-variant comparisons get a new script in
  `tools/` (read several JSONs and build the ladder table).
- `perf` does not exist for WASM and is not required. Hardware counters stay only in the native variants, if they help
  explain a step.

## 7. Reading the generated code

- Target loops: the body of `eachBlock` in `BM_IterateSingle_ECS` and the SoA equivalent, the cached-edge transition
  (`transition`/`moveRow` used by `add`/`remove`) and the central loop of `flush`.
- Tools: `objdump -d --no-show-raw-insn -M intel` (or `-S` with `relwithdebinfo`) on native; `wasm-objdump -d` or
  `wasm2wat` (wabt; emsdk ships `llvm-objdump`, which also disassembles WASM) on WASM. To see what V8 actually generates
  from the WASM, `node --print-wasm-code` (if available in the node build) is welcome but optional.
- For each relevant step, the report shows the excerpts side by side and points out the cause (vectorized or not, width,
  unrolling, bounds checks, `i64`/`i32` conversions, indirect calls that were not inlined etc.).
- The extracted excerpts are versioned as text in `bench/results/wasm/` (not binaries).

## 8. Deliverables

- New presets in `CMakePresets.json` for the variants of §4 (indicative names: `native-clang`, `native-clang-32`,
  `native-clang-avx2`, `wasm-scalar`, `wasm-simd` and a WASM test preset). The current presets remain.
- New service(s) in `compose.yaml` for the Clang/WASM path. The current `dev` service (GCC) does not change behaviour.
- Commands documented in `CLAUDE.md` (Build/Benchmarks section) and in the `Makefile` if it makes sense.
- Result JSONs in `bench/results/wasm/` and a table/entry in `bench/results/README.md`.
- Versioned Markdown report (`bench/results/wasm/README.md` or `docs/`, at the builder's discretion) with:
  the machine and the versions (GCC, Clang, Emscripten, node), the cost ladder per benchmark and per size, the AVX2 branch and
  the explanations from the generated code.

## 9. What is left to the builder's discretion

- The organization of the Docker images (one new image derived from `emsdk` with native Clang + CMake, or two).
- How the presets are parameterized (toolchain file, `cacheVariables`, inheritance).
- The native scalar pair for V3 (§4) and the viability of `-m32` vs `-mx32`.
- The exact format of the tables and of the cross-variant comparison script.
- The `wasm-asan` preset (§5.7).
