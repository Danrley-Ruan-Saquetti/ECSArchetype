import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_boundaries


class CheckBoundariesTest(unittest.TestCase):
    def setUp(self):
        self.temporaryDirectory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporaryDirectory.cleanup)
        self.root = self.temporaryDirectory.name

    def write(self, relativePath, content):
        path = os.path.join(self.root, relativePath)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(content)

    def run_check(self, modules, games=()):
        manifest = {
            "modules": [{"name": name, "directory": directory, "depends": depends} for name, directory, depends in modules],
            "games": [{"name": name, "directory": directory, "depends": depends} for name, directory, depends in games],
        }
        manifestPath = os.path.join(self.root, "manifest.json")
        with open(manifestPath, "w", encoding="utf-8") as handle:
            json.dump(manifest, handle)

        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            code = check_boundaries.main(["--root", self.root, "--manifest", manifestPath])
        return code, output.getvalue()

    def test_declared_include_passes(self):
        self.write("engine/runtime/ecs/src/x.cpp", '#include "oly/core/assert.hpp"\n')

        code, output = self.run_check([("core", "engine/runtime/core", []), ("ecs", "engine/runtime/ecs", ["core"])])

        self.assertEqual(code, 0)
        self.assertEqual(output, "")

    def test_undeclared_include_fails(self):
        self.write("engine/runtime/capi/src/x.cpp", '#include <cstdint>\n\n#include "oly/core/assert.hpp"\n')

        code, output = self.run_check([("ecs", "engine/runtime/ecs", []), ("capi", "engine/runtime/capi", ["ecs"])])

        self.assertEqual(code, 1)
        self.assertIn("engine/runtime/capi/src/x.cpp:3:", output)
        self.assertIn("without declaring core", output)

    def test_detail_from_outside_fails(self):
        self.write("engine/runtime/capi/src/x.cpp", '#include "oly/ecs/detail/table.hpp"\n')

        code, output = self.run_check([("ecs", "engine/runtime/ecs", []), ("capi", "engine/runtime/capi", ["ecs"])])

        self.assertEqual(code, 1)
        self.assertIn("oly/ecs/detail is internal to module ecs", output)

    def test_detail_in_module_tests_passes(self):
        self.write("engine/runtime/ecs/tests/t.cpp", '#include "oly/ecs/detail/table.hpp"\n')

        code, output = self.run_check([("ecs", "engine/runtime/ecs", [])])

        self.assertEqual(code, 0)
        self.assertEqual(output, "")

    def test_game_uses_game_depends(self):
        self.write("engine/fixture/systems.cpp", '#include "oly/core/assert.hpp"\n')

        code, output = self.run_check(
            [("core", "engine/runtime/core", []), ("ecs", "engine/runtime/ecs", ["core"]), ("capi", "engine/runtime/capi", ["ecs"])],
            games=[("fixture_game", "engine/fixture", ["ecs", "capi"])],
        )

        self.assertEqual(code, 1)
        self.assertIn("engine/fixture/systems.cpp:1:", output)

    def test_internal_outside_engine_fails(self):
        self.write("games/x/src/a.ts", 'import { Bridge } from "@oly/core/internal";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("games/x/src/a.ts:1: @oly/core/internal can only be used inside engine/", output)

    def test_internal_inside_engine_passes(self):
        self.write("engine/testing/tests/a.ts", 'import { Bridge } from "@oly/core/internal";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 0)
        self.assertEqual(output, "")

    def test_core_importing_web_host_fails(self):
        self.write("engine/environments/web/clients/typescript/core/src/a.ts", 'import { moduleFactoryFromUrl } from "@oly/web-host";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("engine/environments/web/clients/typescript/core/src/a.ts:1: @oly/core cannot depend on @oly/web-host", output)

    def test_relative_across_packages_fails(self):
        self.write("engine/environments/web/host/js/package.json", "{}")
        self.write("engine/environments/web/clients/typescript/core/package.json", "{}")
        self.write("engine/environments/web/host/js/src/a.ts", 'import { World } from "../../../clients/typescript/core/src/world.js";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("engine/environments/web/host/js/src/a.ts:1: relative import crosses packages; use the package name", output)


if __name__ == "__main__":
    unittest.main()
