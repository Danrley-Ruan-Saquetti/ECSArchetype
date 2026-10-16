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
    for b in benchmarks:
        if b.get("name") == target:
            return b["real_time"]
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--baseline", required=True)
    ap.add_argument("--max", required=True, type=float)
    args = ap.parse_args()

    with open(args.json, encoding="utf-8") as f:
        data = json.load(f)["benchmarks"]

    num = median(data, args.name)
    den = median(data, args.baseline)
    for label, value in (("name", num), ("baseline", den)):
        if value is None:
            print(f"benchmark não encontrado ({label})", file=sys.stderr)
            return 2
    ratio = num / den
    print(f"ratio={ratio:.3f}")
    return 0 if ratio <= args.max else 1


if __name__ == "__main__":
    sys.exit(main())
