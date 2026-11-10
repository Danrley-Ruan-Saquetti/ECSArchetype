#!/bin/sh
# Uso: perf_fragmented.sh <ecs_bench_fragmented> <saída>
# Para cada A roda BM_IterateFragmented/A sob `perf stat` e escreve uma linha CSV:
#   A,ns_per_entity,cycles,instructions,cache_misses_per_entity,l1d_misses_per_entity
# Os eventos são divididos por (iterações × 1M entidades). Eles incluem a montagem do mundo
# (fora do laço medido), então os valores por entidade são limites superiores.
set -eu

binary=$1
output=$2
temporary=$(mktemp -d)

echo "A,ns_per_entity,cycles,instructions,cache_misses_per_entity,l1d_misses_per_entity" >"$output"
for archetype_count in 1 8 64 512 4096; do
    perf stat -x, -e cycles,instructions,cache-misses,L1-dcache-load-misses -o "$temporary/perf.csv" \
        "$binary" --benchmark_filter="BM_IterateFragmented/$archetype_count\$" --benchmark_min_time=2s \
        --benchmark_out="$temporary/benchmark.json" --benchmark_out_format=json >/dev/null
    python3 -I - "$archetype_count" "$temporary/perf.csv" "$temporary/benchmark.json" >>"$output" <<'PY'
import json
import sys

ENTITY_COUNT = 1_000_000  # kEntityCount de engine/runtime/ecs/bench/fragmentation.cpp

archetype_count, perf_path, benchmark_path = sys.argv[1:4]

events = {}
for line in open(perf_path):
    fields = line.strip().split(",")
    if len(fields) > 2 and not line.startswith("#"):
        if not fields[0].replace(".", "").isdigit():
            sys.exit("evento não suportado: " + line.strip())
        events[fields[2].split(":")[0]] = float(fields[0])

benchmark = json.load(open(benchmark_path))["benchmarks"][0]
entities = benchmark["iterations"] * ENTITY_COUNT
print("%s,%.4f,%d,%d,%.6f,%.6f" % (archetype_count, benchmark["cpu_time"] / ENTITY_COUNT,
      events["cycles"], events["instructions"], events["cache-misses"] / entities, events["L1-dcache-load-misses"] / entities))
PY
done

rm -rf "$temporary"
