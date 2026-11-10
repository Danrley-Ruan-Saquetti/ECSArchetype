#!/usr/bin/env python3
"""Compara a mediana (real_time) de dois benchmarks de um JSON do Google Benchmark.

Saída: `ratio=<x>`; exit 0 se ratio <= --max, 1 se maior, 2 se algum nome não existir.
Rodar sempre com `python3 -I`.
"""
import argparse
import json
import sys


def median(benchmarks, name):
    target = name + "_median"
    for benchmark in benchmarks:
        if benchmark.get("name") == target:
            return benchmark["real_time"]
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--max", required=True, type=float)
    arguments = parser.parse_args()

    with open(arguments.json, encoding="utf-8") as handle:
        benchmarks = json.load(handle)["benchmarks"]

    measured = median(benchmarks, arguments.name)
    baseline = median(benchmarks, arguments.baseline)
    for label, value in (("name", measured), ("baseline", baseline)):
        if value is None:
            print(f"benchmark não encontrado ({label})", file=sys.stderr)
            return 2

    ratio = measured / baseline
    print(f"ratio={ratio:.3f}")
    return 0 if ratio <= arguments.max else 1


if __name__ == "__main__":
    sys.exit(main())
