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

    def test_include_declarado_passa(self):
        self.write("engine/runtime/ecs/src/x.cpp", '#include "oly/core/assert.hpp"\n')

        code, output = self.run_check([("core", "engine/runtime/core", []), ("ecs", "engine/runtime/ecs", ["core"])])

        self.assertEqual(code, 0)
        self.assertEqual(output, "")

    def test_include_nao_declarado_falha(self):
        self.write("engine/runtime/bridge/src/x.cpp", '#include <cstdint>\n\n#include "oly/core/assert.hpp"\n')

        code, output = self.run_check([("ecs", "engine/runtime/ecs", []), ("bridge", "engine/runtime/bridge", ["ecs"])])

        self.assertEqual(code, 1)
        self.assertIn("engine/runtime/bridge/src/x.cpp:3:", output)
        self.assertIn("sem declarar core", output)

    def test_detail_de_fora_falha(self):
        self.write("engine/runtime/bridge/src/x.cpp", '#include "oly/ecs/detail/table.hpp"\n')

        code, output = self.run_check([("ecs", "engine/runtime/ecs", []), ("bridge", "engine/runtime/bridge", ["ecs"])])

        self.assertEqual(code, 1)
        self.assertIn("oly/ecs/detail é interno ao módulo ecs", output)

    def test_detail_nos_testes_do_modulo_passa(self):
        self.write("engine/runtime/ecs/tests/t.cpp", '#include "oly/ecs/detail/table.hpp"\n')

        code, output = self.run_check([("ecs", "engine/runtime/ecs", [])])

        self.assertEqual(code, 0)
        self.assertEqual(output, "")

    def test_jogo_usa_os_depends_do_jogo(self):
        self.write("engine/fixture/systems.cpp", '#include "oly/core/assert.hpp"\n')

        code, output = self.run_check(
            [("core", "engine/runtime/core", []), ("ecs", "engine/runtime/ecs", ["core"]), ("bridge", "engine/runtime/bridge", ["ecs"])],
            games=[("fixture_game", "engine/fixture", ["ecs", "bridge"])],
        )

        self.assertEqual(code, 1)
        self.assertIn("engine/fixture/systems.cpp:1:", output)


    def test_internal_fora_de_engine_falha(self):
        self.write("games/x/src/a.ts", 'import { Bridge } from "@oly/core/internal";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("games/x/src/a.ts:1: @oly/core/internal só pode ser usado dentro de engine/", output)

    def test_internal_dentro_de_engine_passa(self):
        self.write("engine/testing/tests/a.ts", 'import { Bridge } from "@oly/core/internal";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 0)
        self.assertEqual(output, "")

    def test_core_importando_client_falha(self):
        self.write("engine/packages/core/src/a.ts", 'import { moduleFactoryFromUrl } from "@oly/client";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("engine/packages/core/src/a.ts:1: @oly/core não pode depender de @oly/client", output)

    def test_relativo_entre_pacotes_falha(self):
        self.write("engine/packages/client/package.json", "{}")
        self.write("engine/packages/core/package.json", "{}")
        self.write("engine/packages/client/src/a.ts", 'import { World } from "../../core/src/world.js";\n')

        code, output = self.run_check([])

        self.assertEqual(code, 1)
        self.assertIn("engine/packages/client/src/a.ts:1: import relativo atravessa pacotes; use o nome do pacote", output)


if __name__ == "__main__":
    unittest.main()
