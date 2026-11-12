# Native vs WebAssembly cost study — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compile the library, tests and benchmarks across a ladder of variants (GCC → Clang → 32 bits → scalar WASM → SIMD WASM, plus the AVX2 branch), measure the cost of each rung and explain it through the generated code, in a versioned report.

**Architecture:** A new Docker image (`Dockerfile.wasm`, `wasm` service) with Emscripten 3.1.74, CMake 3.30.5 and a native Clang with an LLVM close to emsdk's; the `dev` service (GCC) does not change. The variants are presets that inherit from a hidden measurement preset and change one factor each (`CMAKE_CXX_COMPILER`, `CMAKE_CXX_FLAGS`, `toolchainFile`). Portability fixes live in the code and apply to GCC and Clang; what is WASM-specific sits behind `if(EMSCRIPTEN)` in CMake. A script on the host runs the variants interleaved; a Python script builds the ladder from the JSONs.

**Tech Stack:** C++20, GCC 14 (V0), native Clang ≈ LLVM 20, Emscripten 3.1.74 + node 20.18 (V8), CMake ≥ 3.25 with presets v6, doctest 2.4.11, Google Benchmark 1.8.3, Python 3 (`unittest`), `objdump`/`llvm-objdump`.

**Spec:** `docs/superpowers/specs/2026-11-06-wasm-cost-study-design.md` (complements `2026-10-16-ecs-archetype-spec.md`). Sections `§N` below are from that spec.

## Global Constraints

- Branch `feat/wasm-cost-study` (not `master`). Commits, comments and messages in English, ending with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Emscripten pinned at `emscripten/emsdk:3.1.74`; CMake in the new image via `pip install cmake==3.30.5` (§3).
- The `gcc:14` image and the `dev` service remain the native path and do not change behaviour; presets `debug-asan`, `release`, `relwithdebinfo` do not change (§2, anti-goals).
- `-Wall -Wextra -Wpedantic -Werror` on all our targets, in all variants, including Clang and Emscripten (§3).
- Measurement variants: `CMAKE_BUILD_TYPE=Release` (`-O3`), `ECS_ENABLE_ASSERTS=0`, each rung changes a single factor (§4).
- WASM runs only on `node`, via `CMAKE_CROSSCOMPILING_EMULATOR`, so that `ctest` works the same as native (§3).
- WASM link: `-sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1` (§5.6).
- `EntityRecord`: `static_assert(sizeof(EntityRecord) == sizeof(void*) + 8)`; NF-M1 becomes "one pointer + 8 bytes per `EntityRecord` (16 B on 64 bits, 12 B on 32 bits)" (§5.1).
- Benchmarks: the four existing ones without changing what they measure; protocol `--benchmark_repetitions=10 --benchmark_report_aggregates_only=true`, JSON, medians of `real_time` (§6).
- Comparison between variants always labelled as "across runs" and with the spread between rounds (§6, anti-goals).
- Study artefacts (JSONs, generated code excerpts, report) in `bench/results/wasm/`, text only (§7, §8).
- Python scripts run with `python3 -I`.

### How to run commands

```sh
DC="docker compose run --rm dev"     # GCC 14, unchanged
WC="docker compose run --rm wasm"    # native Clang + Emscripten (Task 1)
$WC make test PRESET=wasm-debug T=unit_world
```

### Decisions the spec leaves open (fixed here)

- **A single image** for native Clang and WASM (`Dockerfile.wasm`), `wasm` service. V0 stays on the `dev` service.
- **Native Clang:** prefer `/emsdk/upstream/bin/clang++` itself (LLVM identical to Emscripten's) if `--print-targets` lists `x86-64`; otherwise `clang-20` from `apt.llvm.org`. In both cases the image exposes `clang++` on the `PATH` pointing to the chosen one, and the presets use `clang++`. Native standard library: the libstdc++ of the image's `g++` (recorded in the report; WASM uses libc++).
- **Exceptions in WASM:** `-fwasm-exceptions` (compile and link) only on the doctest executables, via the CMake function `ecs_wasm_exceptions(target)`. The `ecs` library, `contract_death` and the benchmarks stay without exceptions. This works because `IterationGuard`/`each` are templates (compiled in the test's TU) and nothing in `src/` catches exceptions. If `wasm-ld` refuses the mix, create `ecs_without_exceptions` (same sources) only for `contract_death` and compile `ecs` with exceptions.
- **Variant presets:** `native-clang` (V1), `native-clang-32` (V2), `native-clang-32-scalar` (scalar pair of V3, `-m32 -fno-vectorize -fno-slp-vectorize`), `wasm-scalar` (V3), `wasm-simd` (V4), `native-clang-avx2` (VX). Tests: `wasm-debug` (Debug, asserts on, no sanitizers). No `wasm-asan` (§5.7 makes it optional).
- **Function names in the `.wasm`:** the WASM presets link with `--profiling-funcs` so the disassembler shows names; it does not change the functions' generated code.
- **`fuzz_vs_model` (§5.3):** increment first, `++counter;` in its own statement and then `makeValue(ecs::Entity{uint32_t(counter), 0}, counter)`.

## Review Focus

1. **Environment variables in `node`** (`ECS_FUZZ_SEED`/`ECS_FUZZ_OPS`): reproducing a seed in WASM must give the same printed `seed=` as native — tested in Task 3.
2. **A benchmark that fails only in WASM** (memory, `error_occurred` in the JSON): the ladder must not show a number for it as if it had run — `bench_ladder.py` rejects the JSON (Task 5).
3. **Benchmark missing in some variant** or a different number of rounds between variants: the table shows `—` for the missing one and the script rejects unpaired rounds, instead of crashing or pairing wrongly (Task 5).
4. **Silent regression in GCC** from the portability fixes (above all `DoNotOptimize`): the `debug-asan`/`release` tests pass and the NF-P1/NF-P2 ratios of `bench_iterate` stay within targets (Task 2).
5. **Generated code extraction picking the wrong function** (ambiguous name, nonexistent symbol): the script fails with a message instead of writing an empty file or another function's (Task 6).

---

### Task 1: `wasm` image, native Clang and variant presets

**Files:**
- Create: `Dockerfile.wasm`
- Modify: `docker-compose.yaml` (`wasm` service)
- Modify: `CMakePresets.json` (new presets; the current ones intact)
- Modify: `CMakeLists.txt` (`if(EMSCRIPTEN)` block with the link flags from §5.6)

**Interfaces:**
- Produces: `wasm` service; native `clang++` on the `PATH`; configurable presets `native-clang`, `native-clang-32`, `native-clang-32-scalar`, `native-clang-avx2`, `wasm-scalar`, `wasm-simd`, `wasm-debug`, each with a build and test preset of the same name; binaries in `build/<preset>/{tests,bench}/` (WASM: `<name>.js` + `<name>.wasm`).

- [ ] **Step 1: Create the branch** `git switch -c feat/wasm-cost-study`.

- [ ] **Step 2: `Dockerfile.wasm`** `FROM emscripten/emsdk:3.1.74`; installs `make`, `g++`, `g++-multilib` (i386 libstdc++ for V2), `binutils`, `python3-pip`; `pip install cmake==3.30.5`; `WORKDIR /app`. Run `/emsdk/upstream/bin/clang++ --print-targets` inside the image: if it lists `x86-64`, create the link `clang++` → that binary; otherwise install `clang-20` from `apt.llvm.org` and link `clang++` → `clang++-20`.

- [ ] **Step 3: `wasm` service in `docker-compose.yaml`** same as `dev` (same volumes, `command: bash`), with `build: { context: ., dockerfile: Dockerfile.wasm }`.

- [ ] **Step 4: Verify the image**

Run: `docker compose run --rm wasm sh -c 'cmake --version && clang++ --version && emcc --version && node --version && echo "int main(){}" | clang++ -m32 -x c++ - -o /tmp/a && /tmp/a && echo ok32'`
Expected: CMake 3.30.5, Clang 20.x, emcc 3.1.74, node v20.18.x, `ok32`.

- [ ] **Step 5: Presets.** Hidden preset `variant` inheriting `base`: `CMAKE_BUILD_TYPE=Release`, `ECS_SANITIZE=OFF`, `ECS_BUILD_TESTS=ON`, `ECS_BUILD_BENCH=ON`, `ECS_ENABLE_ASSERTS=0`, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`. Hidden `variant-clang` (`CMAKE_CXX_COMPILER=clang++`) and `variant-wasm` (`toolchainFile: /emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake`, `CMAKE_EXE_LINKER_FLAGS=--profiling-funcs`). The visible ones only add their own factor:

| Preset | Inherits | `CMAKE_CXX_FLAGS` | `CMAKE_EXE_LINKER_FLAGS` |
|---|---|---|---|
| `native-clang` | `variant-clang` | — | — |
| `native-clang-32` | `variant-clang` | `-m32` | `-m32` |
| `native-clang-32-scalar` | `variant-clang` | `-m32 -fno-vectorize -fno-slp-vectorize` | `-m32` |
| `native-clang-avx2` | `variant-clang` | `-mavx2 -mfma` | — |
| `wasm-scalar` | `variant-wasm` | — | (inherited) |
| `wasm-simd` | `variant-wasm` | `-msimd128` | (inherited) |
| `wasm-debug` | `base` + toolchain from `variant-wasm` | — | — |

`wasm-debug`: `CMAKE_BUILD_TYPE=Debug`, `ECS_SANITIZE=OFF`, `ECS_BUILD_TESTS=ON`, `ECS_BUILD_BENCH=OFF`. Build and test presets of the same name for all (test with `outputOnFailure`).

- [ ] **Step 6: `CMakeLists.txt`** right after the `option(...)` calls: `if(EMSCRIPTEN) add_link_options(-sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1) endif()`.

- [ ] **Step 7: Configure all variants**

Run: `for p in native-clang native-clang-32 native-clang-32-scalar native-clang-avx2 wasm-scalar wasm-simd wasm-debug; do $WC cmake --preset $p || break; done`
Expected: all configure. In `build/wasm-scalar/CMakeCache.txt`, `CMAKE_CROSSCOMPILING_EMULATOR` points to emsdk's `node` (if not, set it in the `variant-wasm` preset). In `build/native-clang/compile_commands.json` and `build/wasm-simd/compile_commands.json`, `-O3` appears and, in the second one, `-msimd128`.

- [ ] **Step 8: Confirm native did not change** `$DC make test` and `$DC make build PRESET=release` pass (the touched `docker-compose.yaml`/presets do not affect `dev`).

- [ ] **Step 9: Commit** `build: wasm image with native Clang and the study's variant presets`.

---

### Task 2: Portability to Clang (§5.1, §5.2, §5.3, §5.5)

**Files:**
- Modify: `include/ecs/entity.hpp:35`, `include/ecs/detail/assert.hpp:18-21`, `tests/fuzz_vs_model.cpp:29`
- Modify: `bench/flush.cpp`, `bench/fragmentation.cpp`, `bench/iterate.cpp`, `bench/transitions.cpp` (only the `DoNotOptimize` calls)
- Modify: `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md:355` (NF-M1 text)

**Interfaces:**
- Consumes: presets `native-clang`, `native-clang-32` (Task 1).
- Produces: code that compiles without warnings on GCC 14 and Clang 20, 64 and 32 bits.

- [ ] **Step 1: See it fail** `$WC make build PRESET=native-clang-32`
Expected: FAIL with the `[[noreturn]]` after `__attribute__` error, `-Wunsequenced` in `fuzz_vs_model.cpp`, `-Wdeprecated-declarations` in the benches and the NF-M1 `static_assert` (12 ≠ 16).

- [ ] **Step 2: Fixes**
  - `assert.hpp`: `[[noreturn]]` before `__attribute__((format(printf, 4, 5)))`.
  - `entity.hpp`: `static_assert(sizeof(EntityRecord) == sizeof(void*) + 8, "NF-M1: one pointer + 8 bytes per EntityRecord");`.
  - `fuzz_vs_model.cpp`: the "increment first" decision from the Global Constraints.
  - Benches: each `DoNotOptimize` with an rvalue/const argument now receives a non-const local (`auto* world = setup.world.get(); benchmark::DoNotOptimize(world);`; likewise `positions.data()`; `sum` is already an lvalue — check whether the warning comes from it and, if not, do not touch it).
  - Spec v1, NF-M1: "one pointer + 8 bytes per `EntityRecord` (16 B on 64 bits, 12 B on 32 bits) plus 8 bytes of `Entity` in the table".

- [ ] **Step 3: Clang 64 and 32 bits pass**
Run: `$WC make test PRESET=native-clang && $WC make test PRESET=native-clang-32`
Expected: all tests pass, zero warnings.

- [ ] **Step 4: GCC without regression** (Review Focus 4)
Run: `$DC make test && $DC make test PRESET=release`, then `$DC ./build/release/bench/bench_iterate --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=build/iterate_check.json` and `bench_ratio.py` for `BM_IterateSingle_ECS/<N>` vs `BM_IterateSingle_SoA/<N>` with the NF-P1/NF-P2 limits recorded in `bench/results/README.md`.
Expected: tests pass; ratios within targets (the JSON stays in `build/`, not versioned).

- [ ] **Step 5: Commit** `fix: portability to Clang and 32-bit pointers`.

---

### Task 3: Tests in WASM (§5.4, §5.6, §5.7)

**Files:**
- Modify: `CMakeLists.txt` (function `ecs_wasm_exceptions`)
- Modify: `tests/CMakeLists.txt` (apply in `ecs_add_test`; not for `contract_death`)
- Create (only if Step 4 fails): `tools/wasm/node_env.js`

**Interfaces:**
- Consumes: presets `wasm-debug`, `wasm-scalar`, `wasm-simd` (Task 1); fixes from Task 2.
- Produces: `ecs_wasm_exceptions(<target>)` — no-op outside Emscripten; inside it, `target_compile_options` and `target_link_options` `PRIVATE -fwasm-exceptions`.

- [ ] **Step 1: See it fail** `$WC make test PRESET=wasm-debug`
Expected: FAIL — `queries` (`CHECK_THROWS` aborts without exception support).

- [ ] **Step 2: Implement `ecs_wasm_exceptions`** next to `ecs_warnings` and call it inside `ecs_add_test`. `contract_death` and the benches do not call it.

- [ ] **Step 3: All tests pass**
Run: `$WC make test PRESET=wasm-debug && $WC make test PRESET=wasm-scalar && $WC make test PRESET=wasm-simd`
Expected: 10 suites + 5 `contract_death.*` pass in `wasm-debug`; the 10 suites pass in both Release ones. If the link mixing objects with and without `-fwasm-exceptions` fails, apply the fallback from the Global Constraints and repeat.

- [ ] **Step 4: Reproducible seed** (Review Focus 1)
Run: `$WC sh -c 'ECS_FUZZ_SEED=7 ECS_FUZZ_OPS=1000 ctest --preset wasm-debug -R fuzz_vs_model -V' | grep "seed="`
Expected: `[fuzz_vs_model] seed=7 ops=1000`. If it shows the default (`seed=3781 ops=1000000`), create `tools/wasm/node_env.js` with `Module.preRun = [() => Object.assign(ENV, process.env)];` and wire it with `--pre-js` only in the WASM tests (inside `ecs_wasm_exceptions` or a sibling function), and repeat.

- [ ] **Step 5: Commit** `test: tests and contract_death running in WASM via node`.

---

### Task 4: Benchmarks in all variants

**Files:** no new code expected; adjustments only if something fails.

**Interfaces:**
- Consumes: Tasks 1–3.
- Produces: benchmark executables in `build/release/bench/` (V0) and `build/<preset>/bench/` for the six variants.

- [ ] **Step 1: Build** `$DC make build PRESET=release` and, for each variant preset, `$WC make build PRESET=<preset>`.
Expected: everything compiles without warnings.

- [ ] **Step 2: Smoke** — each benchmark of each variant with `--benchmark_min_time=0.01s --benchmark_format=json` (WASM: `node build/<preset>/bench/<bench>.js ...`).
Expected: no result with `"error_occurred": true`; all names from §6 present (including the 1M-entity ones in WASM).

- [ ] **Step 3: Commit** only if something had to change (`fix: ...`).

---

### Task 5: Cost ladder — runner and table

**Files:**
- Create: `tools/bench_ladder.py`, `tools/test_bench_ladder.py`, `tools/run_ladder.sh`

**Interfaces:**
- Consumes: binaries from Task 4.
- Produces:
  - `tools/run_ladder.sh <rounds> <bench> [variants...]` (runs on the host, Git Bash/POSIX). Default variants `V0 V1 V2 V2S V3 V4 VX`, fixed labels: `V0=release@dev`, `V1=native-clang`, `V2=native-clang-32`, `V2S=native-clang-32-scalar`, `V3=wasm-scalar`, `V4=wasm-simd`, `VX=native-clang-avx2` (the non-V0 ones on the `wasm` service; WASM via `node <bench>.js`). Interleaves: for k in 1..rounds, for each variant, runs with the §6 protocol and writes `bench/results/wasm/runs/<bench>/<variant>_r<k>.json`.
  - `tools/bench_ladder.py --run <VARIANT>=<json> ... [--pair ECS=SoA] [--filter <regex>]` → Markdown on stdout. Functions: `load_medians(path) -> dict[str, float]` (name without `_median` → `real_time` in ns); `step_ratio(rounds_a, rounds_b, name) -> tuple[float, float, float] | None` (median, min, max of the round-by-round ratios); `render_markdown(...) -> str`.
  - Tables: (1) median across rounds per variant, with min–max; (2) rungs `V1/V0, V2/V1, V3/V2, V3/V2S, V4/V3, V4/V2, V4/V0, VX/V1`, each with median [min–max]; (3) with `--pair ECS=SoA`, for each name containing `ECS`, the ECS/SoA ratio per variant. Fixed header: "Ratios between variants come from different runs (interleaved); see spread."
  - Error exit: exit 2 if a JSON has `error_occurred`, if a JSON does not open, or if variants have a different number of rounds.

- [ ] **Step 1: Tests** in `tools/test_bench_ladder.py` (`unittest`, JSON fixtures written to `tempfile`; the file puts its own directory on `sys.path`, since `-I` removes it):
  - `test_median_across_rounds`: rounds 10, 12, 11 ns → 11.
  - `test_time_unit_converted_to_ns`: `real_time: 2, time_unit: "us"` → 2000.
  - `test_step_ratio_pairs_rounds`: A [20, 30], B [10, 10] → (2.5, 2.0, 3.0).
  - `test_missing_benchmark_renders_dash`: name only in V0 → cell `—` in the V1 column and rung `V1/V0` with `—`.
  - `test_error_occurred_exits_2` and `test_unequal_rounds_exits_2` (via `main([...])`, checking the exit code).
  - `test_pair_normalization`: `BM_X_ECS/1000`=30, `BM_X_SoA/1000`=10 → 3.00 in table 3.

- [ ] **Step 2: See it fail** `$WC python3 -I tools/test_bench_ladder.py` → FAIL (`ModuleNotFoundError: bench_ladder`).

- [ ] **Step 3: Implement `tools/bench_ladder.py`** with the interface above (standard library only; rung order fixed in the code, a rung with a missing variant is omitted).

- [ ] **Step 4: See it pass** `$WC python3 -I tools/test_bench_ladder.py` → OK.

- [ ] **Step 5: Implement `tools/run_ladder.sh`** and test it with `1 bench_iterate V0 V3` and `--benchmark_min_time=0.01s` passed through the `ECS_LADDER_EXTRA_ARGS` variable (the script appends it to the protocol).
Expected: two JSONs in `bench/results/wasm/runs/bench_iterate/`; `bench_ladder.py` over them prints the three tables. Delete these test JSONs.

- [ ] **Step 6: Commit** `feat: cost ladder scripts across variants`.

---

### Task 6: Generated code extraction (§7)

**Files:**
- Create: `tools/extract_codegen.sh`

**Interfaces:**
- Produces: `tools/extract_codegen.sh <binary> <function-regex> <output>` — `.wasm` → `/emsdk/upstream/bin/llvm-objdump -d --demangle`; otherwise `objdump -d --no-show-raw-insn -M intel -C`. Writes only the body of the function whose demangled name matches the regex. Exit 1 with "function not found" if zero match, "ambiguous regex: <names>" if more than one (Review Focus 5).

- [ ] **Step 1: See the error cases** — run with a nonexistent regex (`BM_DoesNotExist`) and with an ambiguous one (`BM_Iterate`) on `build/native-clang/bench/bench_iterate`. Expected: exit 1 with the messages above, no file created.

- [ ] **Step 2: Extract the target loops** from V0 (on `dev`), V1, V2, V2S, V3, V4, VX into `bench/results/wasm/codegen/<variant>/<target>.txt`:
  - `iterate_ecs`: `BM_IterateSingle_ECS` (inlined `eachBlock` body) — `bench_iterate`
  - `iterate_soa`: `BM_IterateSingle_SoA` — `bench_iterate`
  - `transition`: `ecs::World::transitionTo` and the row-moving function it calls (`moveRow` or equivalent in `src/`) — `bench_transitions`
  - `flush`: `ecs::World::flush(ecs::CommandBuffer&)` — `bench_flush`
Expected: non-empty files. If a function was entirely inlined in a variant, record in the file a line saying which function it appears in and extract that one.

- [ ] **Step 3: Commit** `feat: extraction of the hot loops' generated code per variant`.

---

### Task 7: Measurement, report and documentation (§6, §7, §8)

**Files:**
- Create: `bench/results/wasm/README.md`, `bench/results/wasm/runs/**.json`
- Modify: `bench/results/README.md` (row in the files table pointing to `wasm/`), `CLAUDE.md` (Build/Benchmarks: `wasm` service, presets, scripts), `Makefile` only if a target actually shortens commands

- [ ] **Step 1: Measure** `tools/run_ladder.sh 3 <bench>` for `bench_iterate`, `bench_fragmented`, `bench_transitions`, `bench_flush`, in a single session, idle machine.
Expected: 3 × 7 JSONs per bench, none with `error_occurred`.

- [ ] **Step 2: Tables** `bench_ladder.py` per bench (`--pair ECS=SoA` on `bench_iterate`; on `bench_fragmented` use its SoA baseline) and collect the intra-variant ratios from §6 with `bench_ratio.py`.

- [ ] **Step 3: `bench/results/wasm/README.md`** with: machine; exact versions (GCC, Clang with its origin, Emscripten, node and V8 from `node -p process.versions.v8`, libstdc++ vs libc++); the ladder tables separating sizes that fit in cache from memory-bound ones; the AVX2 branch; the caveat about different runs; for each relevant rung, the side-by-side excerpts from `codegen/` and the identified cause (vectorized or not, width, unrolling, bounds checks, `i64`/`i32`, non-inlined calls). A causal claim only goes in with the variant that isolates the factor and the excerpt that shows it (§2); what cannot be explained stays marked as unexplained. NF-P* in WASM: measured and reported, not required.

- [ ] **Step 4: Project docs** — `CLAUDE.md` gains `WC="docker compose run --rm wasm"`, the new presets and the `run_ladder.sh`/`bench_ladder.py`/`extract_codegen.sh` commands; `bench/results/README.md` points to `wasm/README.md`.

- [ ] **Step 5: Final verification**
Run: `$DC make test && $DC make test PRESET=release && $WC make test PRESET=wasm-debug && $WC make test PRESET=native-clang-32 && $WC python3 -I tools/test_bench_ladder.py`
Expected: everything passes.

- [ ] **Step 6: Commit** `docs: report of the native vs WASM cost study`.
