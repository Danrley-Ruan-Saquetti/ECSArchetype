---
name: bench-compare
description: Measures the ECS benchmarks in Release, checks the performance targets NF-P1, NF-P2 and NF-P5 as ratios against the baseline measured in the same run and records the result in docs/benchmarks/. Use when the user asks to measure, run the benchmarks, check the NF-P targets or verify whether a change introduced a performance regression.
argument-hint: "<prefix> [iterate] [fragmented] [flush]"
disable-model-invocation: true
---

# bench-compare

A full round: Release build → run the benchmarks with repetitions → compute the ratios for each target → compare with the recorded status → update `docs/benchmarks/README.md`. Each step has an easy way to go wrong without noticing, and the error only shows up as a number that looks plausible. This skill exists so that the round comes out comparable with the previous ones.

## Boundary

**Territory** — defines how a measurement becomes a target verdict: which benchmark is divided by which baseline, with which limit, from which statistic, and how the result is recorded.

**Outside the boundary** — the target values (they come from section 7 of the spec); what to do when a target fails (investigate with profiling, change code); the targets that are not ratios (NF-P3 vectorization, NF-P4 zero allocations); comparisons between build variants or between native and WASM; creating the commit, which is the user's act.

**What I do not need to know** — what each benchmark measures inside and how the ECS is implemented. The skill compares medians; understanding why a ratio changed is another job, which starts after the verdict.

**Edge contract** — I receive a prefix for the result files (e.g. `r2`) and, optionally, which executables to run; I deliver the JSON files in `docs/benchmarks/ecs/<prefix>_<executable>.json`, a ratio table with a verdict per target and the updated results `README.md`, without a commit.

## The ratio rule

The environment (Docker on WSL2) is noisy: the same benchmark has already swung 2× between runs. That is why every target is a **ratio between two benchmarks from the same JSON**, measured in the same run, which suffer the same noise and cancel it.

- Target verdict: only by the ratio from `tools/bench_ratio.py`, never by ns/entity.
- Comparing with a previous round: compare **ratio with ratio** (e.g. the NF-P5 ratio at N = 100000 was ~0.85 and now it is X). Never compare ns from one round with ns from another; those numbers are not comparable across runs.
- Absolute numbers may appear in the README as context (cost per entity, for example), always identified as belonging to the round in question, never as a criterion.

## Target table

The limits come from section 7 of `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md`. If the spec and this table diverge, the spec wins: warn the user and use the spec's value.

| Target | Executable | `--name` | `--baseline` | `--max` |
|---|---|---|---|---|
| NF-P1 | `ecs_bench_iterate` | `BM_IterateSingle_ECS/1000000` | `BM_IterateSingle_SoA/1000000` | `1.10` |
| NF-P2 | `ecs_bench_fragmented` | `BM_IterateFragmented/64` | `BM_IterateSingle_SoA/1000000` | `1.25` |
| NF-P5 (N = 1000) | `ecs_bench_flush` | `BM_FlushCoalesced/1000` | `BM_DirectAdds/1000` | `0.5` |
| NF-P5 (N = 100000) | `ecs_bench_flush` | `BM_FlushCoalesced/100000` | `BM_DirectAdds/100000` | `0.5` |

Points that often come out wrong:

- **NF-P2 uses the baseline of `ecs_bench_fragmented` itself**, which has its own `BM_IterateSingle_SoA/1000000`. Dividing by the SoA from the `ecs_bench_iterate` JSON mixes two runs and defeats the purpose of the ratio.
- **NF-P5 is "at least 2× faster"**, so the limit is `--max 0.5` (flush / direct adds ≤ 0.5), not `2.0`.
- **NF-P5 at N = 100000 is not met** in this environment (~0.85, recorded in the README). A failure there is the known status; what matters is whether the ratio got worse compared to the recorded one.

## Flow

All commands are `./spur` tasks, which run in the container.

### 1. Prepare

- Set the prefix from `$ARGUMENTS`. Without a prefix, ask: the files in `docs/benchmarks/ecs/` are the project history (`m0_…`, `m3_…`, `e1_…`, `r1_…`), and a generic name would overwrite a record.
- If `docs/benchmarks/ecs/<prefix>_*.json` already exists, stop and ask before overwriting.
- Note the commit (`git rev-parse --short HEAD`) and whether the tree is dirty. The README records the reference commit; numbers from a dirty tree must say so.
- With no executables argument, run all three (`iterate`, `fragmented`, `flush`).

### 2. Release build

```sh
PRESET=release ./spur build
```

It has to be the `release` preset: the default `debug-asan` enables ASan, UBSan and asserts, and any ratio measured with it is meaningless. If the build fails, stop: there is nothing to measure.

### 3. Run

For each chosen executable:

```sh
./spur dev ./build/release/bench/ecs_bench_<executable> --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=docs/benchmarks/ecs/<prefix>_<executable>.json
```

- The three flags are needed together: `bench_ratio.py` reads the `<name>_median` aggregate, which only exists with repetitions. Without `--benchmark_repetitions` it exits with code 2 (name not found).
- `--benchmark_out` writes the JSON; the console output does not serve as a record.
- A round takes minutes (`ecs_bench_fragmented` builds 1M entities per case). Use a long timeout or background execution, and run one executable at a time: two benchmarks in parallel compete for cache and CPU and contaminate both.

### 4. Compute the ratios

For each row of the target table whose executable was run:

```sh
./spur dev python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/<prefix>_<executable>.json --name <name> --baseline <baseline> --max <max>
```

Read the exit code, not just the text:

- `0` — met (`ratio <= max`).
- `1` — not met.
- `2` — name not found. It is an error of the round (missing flags, benchmark name changed), not a verdict. Check the names with `--benchmark_list_tests` and fix it before going on.

### 5. Confirm a failure

Before declaring that a target is no longer met (or that a ratio got worse compared to the recorded one), repeat the run of that whole executable and recompute. The environment's noise is enough to push a ratio at the limit to the other side. Only a failure that repeats is a regression; report both ratios.

Never loosen `--max` to make the target pass.

### 6. Record in `docs/benchmarks/README.md`

- One row per new file in the file table, with the content and the targets it covers.
- The ratios obtained per target, with the reference commit and the previously recorded ratio, when there is one.
- If a target's status changed (it became met, it stopped being met), update its status section, like the NF-P5 one.

Edit what already exists instead of duplicating sections. Do not commit: propose the message (`bench: ...`, in English) and let the user decide.

## Final report

End with a table in this format, followed by one or two sentences about what changed compared to the recorded round:

| Target | Ratio | Limit | Previous | Verdict |
|---|---|---|---|---|
| NF-P1 | 1.04 | ≤ 1.10 | 1.02 | met |
| NF-P5 (N = 100000) | 0.87 | ≤ 0.50 | ~0.85 | not met (known status) |

Include the measured commit, whether the tree was dirty and which runs were repeated.
