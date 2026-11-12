#!/usr/bin/env python3
"""Compares the median (real_time) of two benchmarks in a Google Benchmark JSON.

Output: `ratio=<x>`; exit 0 if ratio <= --max, 1 if greater, 2 if a name does not exist.
Always run with `python3 -I`.
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
            print(f"benchmark not found ({label})", file=sys.stderr)
            return 2

    ratio = measured / baseline
    print(f"ratio={ratio:.3f}")
    return 0 if ratio <= arguments.max else 1


if __name__ == "__main__":
    sys.exit(main())
