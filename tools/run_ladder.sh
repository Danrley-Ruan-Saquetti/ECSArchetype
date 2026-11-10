#!/usr/bin/env bash
# Roda a escada de variantes intercalada (V0, V1, ..., V0, V1, ...) e grava um JSON por rodada e variante.
# Uso: tools/run_ladder.sh <rodadas> <bench> [variantes...]   (no host; os binários já precisam estar compilados)
# OLY_LADDER_EXTRA_ARGS: argumentos extras para o benchmark (ex.: --benchmark_min_time=0.01s, só para teste).
set -euo pipefail
# Git Bash no host Windows converteria os caminhos POSIX passados ao docker compose.
export MSYS_NO_PATHCONV=1

if [ "$#" -lt 2 ]; then
  echo "uso: $0 <rodadas> <bench> [variantes...]" >&2
  exit 1
fi

rounds=$1
bench=$2
shift 2
variants=("$@")
if [ "${#variants[@]}" -eq 0 ]; then
  variants=(V0 V1 V2 V2S V3 V4 VX)
fi

cd "$(dirname "$0")/.."
output_directory="docs/benchmarks/wasm/runs/$bench"
mkdir -p "$output_directory"

command_for() {
  case $1 in
    V0) echo "dev ./build/release/bench/$bench" ;;
    V1) echo "wasm ./build/native-clang/bench/$bench" ;;
    V2) echo "wasm ./build/native-clang-32/bench/$bench" ;;
    V2S) echo "wasm ./build/native-clang-32-scalar/bench/$bench" ;;
    V3) echo "wasm node ./build/wasm-scalar/bench/$bench.js" ;;
    V4) echo "wasm node ./build/wasm-simd/bench/$bench.js" ;;
    VX) echo "wasm ./build/native-clang-avx2/bench/$bench" ;;
    *) echo "variante desconhecida: $1" >&2; return 1 ;;
  esac
}

# Recusa variante desconhecida antes da primeira rodada, não no meio da escada.
for variant in "${variants[@]}"; do
  command_for "$variant" > /dev/null
done

protocol="--benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_format=json ${OLY_LADDER_EXTRA_ARGS:-}"
for round in $(seq 1 "$rounds"); do
  for variant in "${variants[@]}"; do
    read -r service command <<< "$(command_for "$variant")"
    target="$output_directory/${variant}_r${round}.json"
    echo "rodada $round/$rounds: $variant ($bench)" >&2
    if ! docker compose run --rm -T "$service" sh -c "$command $protocol" > "$target.tmp"; then
      rm -f "$target.tmp"
      echo "falhou: $variant rodada $round" >&2
      exit 1
    fi
    mv "$target.tmp" "$target"
  done
done
