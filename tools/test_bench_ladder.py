import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bench_ladder


def write_json(directory, medians, unit="ns", error_name=None):
    benchmarks = []
    for name, value in medians.items():
        benchmarks.append({"name": name + "_mean", "aggregate_name": "mean", "real_time": value * 100, "time_unit": unit})
        benchmarks.append({"name": name + "_median", "aggregate_name": "median", "real_time": value, "time_unit": unit})
    if error_name is not None:
        benchmarks.append({"name": error_name, "error_occurred": True, "error_message": "sem memória"})
    path = tempfile.mktemp(suffix=".json", dir=directory)
    with open(path, "w", encoding="utf-8") as handle:
        json.dump({"benchmarks": benchmarks}, handle)
    return path


def run_main(arguments):
    output = io.StringIO()
    errors = io.StringIO()
    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
        code = bench_ladder.main(arguments)
    return code, output.getvalue(), errors.getvalue()


class BenchLadderTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)

    def rounds(self, name, values):
        return [bench_ladder.load_medians(write_json(self.directory.name, {name: value})) for value in values]

    def test_median_across_rounds(self):
        rounds = self.rounds("BM_X/1000", [10, 12, 11])
        self.assertEqual(bench_ladder.median_across_rounds(rounds, "BM_X/1000"), 11)

    def test_time_unit_converted_to_ns(self):
        path = write_json(self.directory.name, {"BM_X/1000": 2}, unit="us")
        self.assertEqual(bench_ladder.load_medians(path), {"BM_X/1000": 2000})

    def test_step_ratio_pairs_rounds(self):
        rounds_a = self.rounds("BM_X/1000", [20, 30])
        rounds_b = self.rounds("BM_X/1000", [10, 10])
        self.assertEqual(bench_ladder.step_ratio(rounds_a, rounds_b, "BM_X/1000"), (2.5, 2.0, 3.0))

    def test_missing_benchmark_renders_dash(self):
        version0 = write_json(self.directory.name, {"BM_X/1000": 10, "BM_Y/1000": 5})
        version1 = write_json(self.directory.name, {"BM_X/1000": 20})
        code, output, _ = run_main(["--run", "V0=" + version0, "--run", "V1=" + version1])
        self.assertEqual(code, 0)
        row = next(line for line in output.splitlines() if line.startswith("| BM_Y/1000"))
        self.assertEqual(row.count("—"), 1, row)
        step_row = [line for line in output.splitlines() if line.startswith("| BM_Y/1000")][1]
        self.assertIn("—", step_row)

    def test_error_occurred_exits_2(self):
        bad = write_json(self.directory.name, {"BM_X/1000": 10}, error_name="BM_Z/1000000")
        code, output, errors = run_main(["--run", "V0=" + bad])
        self.assertEqual(code, 2)
        self.assertEqual(output, "")
        self.assertIn("BM_Z/1000000", errors)

    def test_unreadable_json_exits_2(self):
        path = os.path.join(self.directory.name, "quebrado.json")
        with open(path, "w", encoding="utf-8") as handle:
            handle.write("{nao e json")
        code, _, errors = run_main(["--run", "V0=" + path])
        self.assertEqual(code, 2)
        self.assertIn("quebrado.json", errors)

    def test_unequal_rounds_exits_2(self):
        first = write_json(self.directory.name, {"BM_X/1000": 10})
        second = write_json(self.directory.name, {"BM_X/1000": 11})
        code, output, errors = run_main(
            ["--run", "V0=" + first, "--run", "V0=" + second, "--run", "V1=" + first])
        self.assertEqual(code, 2)
        self.assertEqual(output, "")
        self.assertIn("rodadas", errors)

    def test_pair_normalization(self):
        path = write_json(self.directory.name, {"BM_X_ECS/1000": 30, "BM_X_SoA/1000": 10})
        code, output, _ = run_main(["--run", "V0=" + path, "--pair", "ECS=SoA"])
        self.assertEqual(code, 0)
        table = output.split("### ")[-1]
        row = next(line for line in table.splitlines() if line.startswith("| BM_X_ECS/1000"))
        self.assertIn("3.00", row)


if __name__ == "__main__":
    unittest.main()
