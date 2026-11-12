#!/usr/bin/env python3
"""Builds the cost ladder between variants from Google Benchmark JSONs.

Usage: bench_ladder.py --run V0=a.json --run V0=b.json --run V1=c.json ... [--pair ECS=SoA] [--filter REGEX]
Each --run is one round; the variants must have the same number of rounds. Markdown on stdout.
Exit 2 if a JSON does not open, has `error_occurred` or if the rounds do not match. Run with `python3 -I`.
"""
import argparse
import json
import re
import statistics
import sys

# Variants and steps of the WASM cost study: docs/superpowers/specs/2026-11-06-wasm-cost-study-design.md §4.
VARIANT_ORDER = ["V0", "V1", "V2", "V2S", "V3", "V4", "VX"]
STEPS = [("V1", "V0"), ("V2", "V1"), ("V3", "V2"), ("V3", "V2S"), ("V4", "V3"), ("V4", "V2"), ("V4", "V0"), ("VX", "V1")]

NANOSECONDS_PER_UNIT = {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}

DASH = "—"
HEADER = "Ratios between variants come from different runs (interleaved); see spread."


class LadderError(Exception):
    pass


def load_medians(path):
    """Name (without `_median`) -> real_time in ns."""
    try:
        with open(path, encoding="utf-8") as handle:
            benchmarks = json.load(handle)["benchmarks"]
    except (OSError, ValueError, KeyError) as error:
        raise LadderError(f"{path}: could not read the JSON ({error})")

    failed = [entry.get("name", "?") for entry in benchmarks if entry.get("error_occurred")]
    if failed:
        raise LadderError(f"{path}: benchmark with error_occurred: {', '.join(failed)}")

    medians = {}
    for entry in benchmarks:
        name = entry.get("name", "")
        if entry.get("aggregate_name") != "median" or not name.endswith("_median"):
            continue
        unit = entry.get("time_unit", "ns")
        if unit not in NANOSECONDS_PER_UNIT:
            raise LadderError(f"{path}: unknown time_unit '{unit}' in {name}")
        medians[name[: -len("_median")]] = entry["real_time"] * NANOSECONDS_PER_UNIT[unit]

    if not medians:
        raise LadderError(f"{path}: no median found (missing --benchmark_report_aggregates_only?)")
    return medians


def median_across_rounds(rounds, name):
    values = [round_[name] for round_ in rounds if name in round_]
    if len(values) != len(rounds):
        return None
    return statistics.median(values)


def spread_across_rounds(rounds, name):
    values = [round_[name] for round_ in rounds if name in round_]
    if len(values) != len(rounds):
        return None
    return statistics.median(values), min(values), max(values)


def ratio_of(rounds_a, name_a, rounds_b, name_b):
    """(median, min, max) of the round-by-round a/b ratios, or None if either side is missing."""
    ratios = []
    for round_a, round_b in zip(rounds_a, rounds_b):
        if name_a not in round_a or name_b not in round_b or round_b[name_b] == 0:
            return None
        ratios.append(round_a[name_a] / round_b[name_b])

    if not ratios:
        return None
    return statistics.median(ratios), min(ratios), max(ratios)


def step_ratio(rounds_a, rounds_b, name):
    return ratio_of(rounds_a, name, rounds_b, name)


def format_spread(spread, digits):
    if spread is None:
        return DASH
    middle, low, high = spread
    return f"{middle:.{digits}f} [{low:.{digits}f}–{high:.{digits}f}]"


def table(title, columns, rows):
    lines = [f"### {title}", "", "| Benchmark | " + " | ".join(columns) + " |", "|---|" + "---|" * len(columns)]
    for name, cells in rows:
        lines.append(f"| {name} | " + " | ".join(cells) + " |")

    return "\n".join(lines)


def ordered_variants(runs):
    known = [variant for variant in VARIANT_ORDER if variant in runs]
    return known + [variant for variant in runs if variant not in VARIANT_ORDER]


def benchmark_names(runs, variants, name_filter):
    names = []
    for variant in variants:
        for round_ in runs[variant]:
            for name in round_:
                if name not in names and (name_filter is None or re.search(name_filter, name)):
                    names.append(name)

    return names


def render_markdown(runs, pair=None, name_filter=None):
    variants = ordered_variants(runs)
    names = benchmark_names(runs, variants, name_filter)
    rounds_count = len(runs[variants[0]])
    sections = [HEADER]

    rows = [(name, [format_spread(spread_across_rounds(runs[variant], name), 1) for variant in variants]) for name in names]
    sections.append(table(f"Median real_time per variant (ns; median across {rounds_count} rounds [min–max])", variants, rows))

    steps = [(variant_a, variant_b) for variant_a, variant_b in STEPS if variant_a in runs and variant_b in runs]
    if steps:
        rows = [(name, [format_spread(step_ratio(runs[variant_a], runs[variant_b], name), 2) for variant_a, variant_b in steps]) for name in names]
        sections.append(table("Steps (round-by-round ratio: median [min–max])", [f"{variant_a}/{variant_b}" for variant_a, variant_b in steps], rows))

    if pair is not None:
        numerator, baseline = pair
        rows = []
        for name in names:
            if numerator in name:
                baseline_name = name.replace(numerator, baseline)
                rows.append((name, [format_spread(ratio_of(runs[variant], name, runs[variant], baseline_name), 2) for variant in variants]))
        sections.append(table(f"{numerator}/{baseline} ratio per variant (same round)", variants, rows))

    return "\n\n".join(sections) + "\n"


def parse_run(text):
    variant, separator, path = text.partition("=")
    if not separator or not variant or not path:
        raise argparse.ArgumentTypeError(f"expected VARIANT=file.json, got '{text}'")
    return variant, path


def parse_pair(text):
    numerator, separator, baseline = text.partition("=")
    if not separator or not numerator or not baseline:
        raise argparse.ArgumentTypeError(f"expected ECS=SoA, got '{text}'")
    return numerator, baseline


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="append", required=True, type=parse_run)
    parser.add_argument("--pair", type=parse_pair)
    parser.add_argument("--filter")
    arguments = parser.parse_args(argv)

    try:
        runs = {}
        for variant, path in arguments.run:
            runs.setdefault(variant, []).append(load_medians(path))

        counts = {variant: len(rounds) for variant, rounds in runs.items()}
        if len(set(counts.values())) != 1:
            raise LadderError(f"number of rounds differs between variants: {counts}")

        output = render_markdown(runs, arguments.pair, arguments.filter)
    except LadderError as error:
        print(f"bench_ladder: {error}", file=sys.stderr)
        return 2

    sys.stdout.write(output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
