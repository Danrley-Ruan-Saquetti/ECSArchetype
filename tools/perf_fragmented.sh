#!/bin/sh
# Uso: perf_fragmented.sh <bench_fragmented> <saida>
# Para cada A roda BM_IterateFragmented/A sob `perf stat` e escreve uma linha CSV:
#   A,ns_per_entity,cycles,instructions,cache_misses_per_entity,l1d_misses_per_entity
# Os eventos são divididos por (iterações × 1M entidades). Eles incluem a montagem do mundo
# (fora do laço medido), então os valores por entidade são limites superiores.
set -eu
bin=$1
out=$2
tmp=$(mktemp -d)
echo "A,ns_per_entity,cycles,instructions,cache_misses_per_entity,l1d_misses_per_entity" >"$out"
for a in 1 8 64 512 4096; do
    perf stat -x, -e cycles,instructions,cache-misses,L1-dcache-load-misses -o "$tmp/perf.csv" \
        "$bin" --benchmark_filter="BM_IterateFragmented/$a\$" --benchmark_min_time=2s \
        --benchmark_out="$tmp/b.json" --benchmark_out_format=json >/dev/null
    python3 -I - "$a" "$tmp/perf.csv" "$tmp/b.json" >>"$out" <<'PY'
import json, sys
a, perf, js = sys.argv[1:4]
ev = {}
for line in open(perf):
    f = line.strip().split(",")
    if len(f) > 2 and not line.startswith("#"):
        if not f[0].replace(".", "").isdigit():
            sys.exit("evento não suportado: " + line.strip())
        ev[f[2].split(":")[0]] = float(f[0])
b = json.load(open(js))["benchmarks"][0]
n = b["iterations"] * 1_000_000
print("%s,%.4f,%d,%d,%.6f,%.6f" % (a, b["counters"]["per_entity"] if False else b["cpu_time"] / 1e6,
      ev["cycles"], ev["instructions"], ev["cache-misses"] / n, ev["L1-dcache-load-misses"] / n))
PY
done
rm -rf "$tmp"
