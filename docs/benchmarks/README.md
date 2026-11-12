# Benchmark results

Folders: `ecs/` (ECS module), `bridge/` (WASM↔TS boundary), `wasm/` (variant ladder). The executables `bench_iterate`, `bench_transitions`, `bench_fragmented` and `bench_flush` are now called `ecs_bench_*`.

Machine: Docker container (gcc:14) on WSL2/Windows 11, 12 CPUs at ~3.6 GHz, L1d 32 KiB, L2 512 KiB, L3 16 MiB.
Measurements are noisy in this environment (`BM_DirectAdds` has already swung from 60 to 120 ns/entity between runs);
always compare ratios measured in the same run. Reference commit for the numbers below: `1b1e8ce`.

| File | Contents | Target |
|---|---|---|
| `ecs/m0_iterate_single_soa.json` | SoA baseline (task 2) | — |
| `ecs/m2_transitions.json` | Transitions with cached and cold edges (task 11) | NF-P4 |
| `ecs/m3_iterate.json` | ECS vs SoA iteration (task 14) | NF-P1, NF-P2 |
| `ecs/m3_fragmented.json` | Fragmented iteration (task 14) | NF-P3 |
| `ecs/m3_vectorization.txt` | Vectorization report of the iteration loop | — |
| `ecs/m4_flush.json` | Coalesced flush vs direct adds (task 18) | NF-P5 |
| `ecs/m5_perf_fragmented.txt` | `perf stat` of `BM_IterateFragmented/A` (task 20) | — |
| `ecs/e1_iterate.json`, `ecs/e1_fragmented.json`, `ecs/e1_flush.json` | Re-run after `RawQuery`/`addRaw` (engine phase 1, task 2) | NF-P1, NF-P2, NF-P5 |
| `bridge/boundary_phase1.json` | The engine's WASM↔TS boundary; see `bridge/README.md` (engine phase 1, task 8) | — |
| `ecs/r1_iterate.json`, `ecs/r1_fragmented.json`, `ecs/r1_flush.json` | Re-run after the modularization into Oly modules (task 10) | NF-P1, NF-P2, NF-P5 |
| `bridge/boundary_oly.json` | WASM↔TS boundary after the modularization; comparison in `bridge/README.md` | — |
| `bridge/loop_phase2.json` | Cost of the phase 2 loop: empty systems, interleaving and boundary inside the loop; see `bridge/README.md` | — |
| `bridge/events_phase3.json` | Cost of the phase 3 events: per-batch handler (C++ and TS) and emission from C++ and from TS; see `bridge/README.md` | — |

## NF-P5 — status

`BM_FlushCoalesced/1000` / `BM_DirectAdds/1000` ≈ 0.50 (at the limit); `BM_FlushCoalesced/100000` / `BM_DirectAdds/100000` ≈ 0.85:
**the 2× target is not met at N = 100000 in this environment.** The flush costs ~29 ns/entity at N = 1000 and ~50 ns at
N = 100000; the difference is the command buffer (96 B per entity, 9.6 MB) read cold.

## `iterate_fragmented` under `perf stat` (1M entities, `relwithdebinfo`)

The events are divided by iterations × 1M and include building the world, so the per-entity values are upper bounds.

| A (archetypes) | ns/entity | cache-misses/entity | L1d-misses/entity |
|---:|---:|---:|---:|
| 1 | 1.28 | 0.021 | 0.59 |
| 8 | 1.35 | 0.024 | 0.57 |
| 64 | 1.15 | 0.025 | 0.59 |
| 512 | 1.32 | 0.071 | 0.70 |
| 4096 | 2.54 | 0.275 | 0.80 |

Up to 512 archetypes (2048 entities per block) the per-entity cost is practically flat; at 4096 (244 entities per block)
the time doubles and last-level cache misses rise ~13× relative to A = 1. Raw data in `ecs/m5_perf_fragmented.txt`.

## Re-run after `RawQuery`/`addRaw` (engine phase 1)

Ratios of the medians in `ecs/e1_*.json`: `BM_IterateSingle_ECS/1000000` / SoA = 0.82 (NF-P1 ≤ 1.10);
`BM_IterateFragmented/64` / SoA = 0.71 (NF-P2 ≤ 1.25); `BM_FlushCoalesced/1000` / `BM_DirectAdds/1000` = 0.44 (NF-P5 ≤ 0.5);
`BM_FlushCoalesced/100000` / `BM_DirectAdds/100000` = 0.95 (already documented ~0.85; target still not met at N = 100000).

Noise: full runs of the same binary gave ECS/SoA at 1M between 0.82 and 1.31 (the runs filtered at 1M gave 0.86, 0.99 and 0.96),
and the flush at N = 1000 between 0.44 and 0.59. To separate noise from regression, `master`'s `bench_flush` (now `ecs_bench_flush`) was measured right after, next to the
branch: 0.55 / 0.59 / 0.57 on `master` against 0.51 / 0.44 / 0.59 on the branch. The flush path did not change, so the range
~0.45–0.6 at N = 1000 is the precision of the environment, not an effect of this phase. The recorded files are representative runs, not the best
of a draw: the values above show the spread.

`BM_IterateSingle_RawQuery/N` / `BM_IterateSingle_ECS/N`: 1.005 (1000), 1.010 (10000), 0.902 (100000), 1.043 (1000000) — the type-erased
query costs the same as the template one in the block loop.

## Re-run after the modularization (Oly, r1)

Ratios of the medians in `ecs/r1_*.json` (`ecs_bench_*`, `release` preset): `BM_IterateSingle_ECS/1000000` / SoA = 0.82 (NF-P1 ≤ 1.10);
`BM_IterateFragmented/64` / SoA = 1.08 (NF-P2 ≤ 1.25); `BM_FlushCoalesced/1000` / `BM_DirectAdds/1000` = 0.53 (NF-P5 ≤ 0.5);
`BM_FlushCoalesced/100000` / `BM_DirectAdds/100000` = 0.90 (the target is still not met at N = 100000).

NF-P5 at N = 1000 went above 0.5 in three consecutive runs (0.52, 0.57, 0.53). To separate noise from regression, the `bench_flush` of the commit
before the refactoring (`1ad7e0e`) was measured interleaved with the current `ecs_bench_flush`, in the same environment: 0.53 / 0.45 / 0.53 before against
0.50 / 0.53 / 0.51 after. The ranges overlap, so it is the environment's noise (already seen at 0.44–0.59), not an effect of the modularization.
`BM_IterateSingle_QueryById/1000000` / `BM_IterateSingle_ECS/1000000` = 1.15 in this run (1.04 in phase 1); this ratio has no target.
