# Performance

Targets are **ratios** against a baseline measured in the same run, never absolute numbers, because the environment (WSL2 + Docker) is noisy.

## ECS targets

| Target | Requirement | Status |
|---|---|---|
| NF-P1 | iterate 1M entities in one archetype ≤ 1.10× the manual SoA | met (~0.82) |
| NF-P2 | the same across 64 archetypes ≤ 1.25× | met (~1.08) |
| NF-P3 | `eachBlock` inner loop vectorized | met |
| NF-P4 | `add`/`remove` with cached edge: zero allocations and zero hash | met |
| NF-P5 | flush of N coalesced adds ≥ 2× faster than direct adds | at the noise limit at N = 1000; **not met** at N = 100000 (the command buffer is read cold) |

## Boundary costs

No numeric target:

- The C++ loop inside WASM runs at ~0.6–1.3 ns/entity. The same loop in TS through the views is ~10–15× higher.
- The per-element `get`/`set` accessor is the main cost in TS; `raw()` access is within ~1.1–1.5× of a manual `Float32Array` at N ≥ 10⁴.
- A C++ → JS callback costs ~40–50 ns per TS system. Interleaving C++ and TS systems in the same phase costs no more than the number of calls explains.

Consequence for game authors: hot per-entity loops stay in C++; TS keeps what runs per system, per batch or a few times per step.

## Where the results live

| Folder | Contents |
|---|---|
| `docs/benchmarks/README.md` + `ecs/` | ECS targets, `perf stat` of the fragmented iteration |
| `docs/benchmarks/bridge/` | boundary, loop and events |
| `docs/benchmarks/wasm/` | runs (`runs/`) and generated code (`codegen/`) of the native × WASM ladder |

The ECS benchmarks live in `engine/runtime/ecs/bench/` (`ecs_bench_iterate`, `ecs_bench_transitions`, `ecs_bench_fragmented`, `ecs_bench_flush`). The boundary ones live in `engine/testing/bench/`. `tools/bench_ratio.py` computes the ratios.
