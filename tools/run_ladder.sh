#!/usr/bin/env bash
# Runs the variant ladder interleaved (V0, V1, ..., V0, V1, ...) and writes one JSON per round and variant.
# Usage: tools/run_ladder.sh <rounds> <bench> [variants...]   (on the host; the binaries must already be built)
# OLY_LADDER_EXTRA_ARGS: extra arguments for the benchmark (e.g. --benchmark_min_time=0.01s, for testing only).
set -euo pipefail
# Git Bash on the Windows host would convert the POSIX paths passed to docker compose.
export MSYS_NO_PATHCONV=1

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <rounds> <bench> [variants...]" >&2
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
    *) echo "unknown variant: $1" >&2; return 1 ;;
  esac
}

# Rejects an unknown variant before the first round, not halfway up the ladder.
for variant in "${variants[@]}"; do
  command_for "$variant" > /dev/null
done

protocol="--benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_format=json ${OLY_LADDER_EXTRA_ARGS:-}"
for round in $(seq 1 "$rounds"); do
  for variant in "${variants[@]}"; do
    read -r service command <<< "$(command_for "$variant")"
    target="$output_directory/${variant}_r${round}.json"
    echo "round $round/$rounds: $variant ($bench)" >&2
    if ! docker compose run --rm -T "$service" sh -c "$command $protocol" > "$target.tmp"; then
      rm -f "$target.tmp"
      echo "failed: $variant round $round" >&2
      exit 1
    fi
    mv "$target.tmp" "$target"
  done
done
