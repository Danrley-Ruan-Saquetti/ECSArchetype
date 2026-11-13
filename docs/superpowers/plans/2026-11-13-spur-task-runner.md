# Replace the Makefile with spur Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `./spur <task>` runs every toolchain task, from Git Bash on the host or inside a toolchain container, and the `Makefile` is gone.

**Architecture:** spur v0.3.1 is vendored at the root and reads a `Spurfile`. A preamble helper `_in` makes each task either run where it is (the image's `OLY_TOOLCHAIN` marker matches), fail with 64 (another image), or re-run itself in the right compose service (host, marker unset). A temporary Python unittest with a fake `docker` on `PATH` validates the dispatch while the `Spurfile` is written; it lives in the git-ignored `build/` and is deleted at the end.

**Tech Stack:** POSIX sh (spur), Docker Compose, CMake/ctest, Python 3 `unittest`.

**Spec:** `docs/superpowers/specs/2026-11-13-spur-task-runner-design.md`

## Global Constraints

- spur is v0.3.1, pinned to the tag, a file at the repository root, executable in the git index.
- The `Spurfile` is POSIX sh: `./spur --check` must print nothing and exit 0.
- `_in` on the host runs `exec docker compose run --rm -e PRESET -e OLY_FUZZ_SEED -e OLY_FUZZ_OPS [options] SERVICE ./spur "$SPUR_TASK" "$@"` with `MSYS_NO_PATHCONV=1` exported.
- Exit 64 for a usage error, a wrong `PRESET` and a task typed in the wrong image.
- Image by preset: `wasm*`, `native-clang*` and `schema-tool` go to `wasm`; every other preset goes to `dev`.
- `PRESET` defaults: `debug-asan` (`configure`, `build`, `test`), `wasm-games` (`games-test`), `wasm-games-release` (`game`, `serve`).
- Untouched: `docs/superpowers/` (historical), `.claude/hooks/clang-format.sh`, the `perf` service, `tools/perf_fragmented.sh`, the `make` package in `Dockerfile.wasm`.
- Commits, comments and messages in English. Commit subjects short, with no spec, plan or task ids.
- Comments only for a reason, an external constraint or a non-local invariant; carry over the Makefile's three "why" comments (npm next to node, `schema-tool` built for the machine, generated schemas before `tsc`). No step labels, no narration of the edit.
- Blank lines in the `Spurfile` separate steps of a task, not lines.
- **[user decision]** The spur tests are not versioned: `build/spurfile-test/test_spurfile.py` exists only to validate the `Spurfile`, is never committed nor registered in CMake, and is deleted in Task 4.

## Review Focus

1. A ctest filter with regex characters or spaces (`-R '^ecs\.'`, `"two words"`) must reach `ctest` byte for byte through `docker compose run`. Checked in Task 1.
2. `./spur game` with no game, or `PRESET=release ./spur serve arena`, must fail on the host with 64 before any container starts. Checked in Task 2.
3. `./spur games-test` typed inside the `dev` container must exit 64 with a message naming both images, not try to call `docker` inside the container. Checked in Tasks 1 and 2.
4. An image built before this change has no `OLY_TOOLCHAIN`: inside it, `_in` finds no `docker`. The user expects a message that says to run `docker compose build`, not `docker: not found`. **[plan decision: exit 69, `EX_UNAVAILABLE`, outside spur's own 64–68]**. Checked in Task 1.
5. A Windows checkout with `core.autocrlf=true` must still give LF `spur` and `Spurfile`, or `sh` fails in the container. Checked in Task 1 with `git ls-files --eol`.

The temporary test sits in `build/` because that directory is git-ignored and mounted at `/app/build` in every service: the containers see it and `git add` cannot pick it up.

---

### Task 1: spur, image markers and the native tasks

**Files:**
- Create: `spur` (vendored), `Spurfile`
- Create, temporary and not committed: `build/spurfile-test/test_spurfile.py`
- Modify: `.gitattributes`, `Dockerfile`, `Dockerfile.wasm`

**Interfaces:**
- Produces, in the `Spurfile` preamble:
  - `_fail CODE MESSAGE` — prints `spur $SPUR_TASK: MESSAGE` to stderr and exits `CODE`.
  - `_image_for PRESET` — prints `wasm` or `dev` (rule in Global Constraints).
  - `_in IMAGE [--service SERVICE] [compose run options...] -- [task args...]` — returns when `OLY_TOOLCHAIN` = `IMAGE`; `_fail 64` when it is another image; `_fail 69` when it is unset and `docker` is not on `PATH`; otherwise the `exec` in Global Constraints. `SERVICE` defaults to `IMAGE`; `--service` is consumed by `_in`, the other options go before `SERVICE`.
- Produces tasks: `configure`, `build`, `test [ctest args]`, `dev [cmd...]`, `wasm [cmd...]`.
- Produces `build/spurfile-test/test_spurfile.py` with `run_spur(arguments, toolchain=None, preset=None, with_docker=True)` and `docker_arguments()` (list of lines the fake recorded, or `None`), and the constant `FORWARDED`. Task 2 adds tests to it.

- [ ] **Step 1: Write the failing test `build/spurfile-test/test_spurfile.py`**

Fixture: `setUp` creates a temporary directory with an executable `docker` that writes `MSYS_NO_PATHCONV=<value>` and then each argument on its own line to the file named by `OLY_TEST_DOCKER_RECORD`. `run_spur` runs `["sh", "spur", *arguments]` with `cwd` = repository root (three levels above the test file), `stdin=DEVNULL`, captured text output, and an environment copied from `os.environ` without `OLY_TOOLCHAIN`, `PRESET` and `MSYS_NO_PATHCONV`, then: `OLY_TOOLCHAIN=toolchain` and `PRESET=preset` when given, the fake directory first on `PATH` when `with_docker`.

```python
FORWARDED = ["compose", "run", "--rm", "-e", "PRESET", "-e", "OLY_FUZZ_SEED", "-e", "OLY_FUZZ_OPS"]


class SpurfileDispatchTest(unittest.TestCase):
    def test_host_dispatch_forwards_arguments_verbatim(self):
        result = self.run_spur(["test", "-R", "^ecs\\.", "two words"])

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.docker_arguments(), ["MSYS_NO_PATHCONV=1", *FORWARDED, "dev", "./spur", "test", "-R", "^ecs\\.", "two words"])

    def test_host_picks_the_image_by_preset(self):
        cases = [("debug-asan", "dev"), ("release", "dev"), ("relwithdebinfo", "dev"), ("wasm-debug", "wasm"),
                 ("wasm-games-release", "wasm"), ("native-clang-32", "wasm"), ("schema-tool", "wasm")]
        for preset, service in cases:
            with self.subTest(preset=preset):
                self.run_spur(["build"], preset=preset)

                self.assertEqual(self.docker_arguments()[1 + len(FORWARDED)], service)

    def test_runner_flags_stay_on_the_host(self):
        self.run_spur(["-x", "test"])

        self.assertEqual(self.docker_arguments()[-3:], ["dev", "./spur", "test"])

    def test_task_in_the_wrong_image_exits_64(self):
        result = self.run_spur(["test"], toolchain="wasm")

        self.assertEqual(result.returncode, 64)
        self.assertIn("dev", result.stderr)
        self.assertIn("wasm", result.stderr)
        self.assertIsNone(self.docker_arguments())

    def test_task_in_its_image_runs_here(self):
        result = self.run_spur(["dev", "echo", "hello"], toolchain="dev")

        self.assertEqual(result.stdout, "hello\n")
        self.assertIsNone(self.docker_arguments())

    def test_missing_docker_points_to_the_image_rebuild(self):
        result = self.run_spur(["test"], with_docker=False)

        self.assertEqual(result.returncode, 69)
        self.assertIn("docker compose build", result.stderr)
```

`run_spur` deletes the record file before each run, so `docker_arguments()` inside a `subTest` loop sees only that run.

- [ ] **Step 2: Run it to verify it fails**

Run: `MSYS_NO_PATHCONV=1 docker compose run --rm dev python3 -I build/spurfile-test/test_spurfile.py`
Expected: FAIL, every test, `sh: 0: cannot open spur` (no runner yet).

- [ ] **Step 3: Vendor spur and force LF**

```bash
curl -fsSL https://raw.githubusercontent.com/esliph/spur/v0.3.1/spur -o spur
git add spur && git update-index --chmod=+x spur
```

Add `spur text eol=lf` and `Spurfile text eol=lf` to `.gitattributes`.

- [ ] **Step 4: Mark the images**

`ENV OLY_TOOLCHAIN=dev` in `Dockerfile` and `ENV OLY_TOOLCHAIN=wasm` in `Dockerfile.wasm`, each after `WORKDIR /app`.

- [ ] **Step 5: Write the `Spurfile` preamble and the native tasks**

Preamble: `_fail`, `_image_for`, `_in` as in Interfaces. The 64 message names both images (`runs in the dev image, this is the wasm image`); the 69 message says the image predates `OLY_TOOLCHAIN` and to run `docker compose build`.

Each task opens with `_in ... -- "$@"`, so its chained calls run in the same container:

| Task | After `_in "$(_image_for "$preset")" -- "$@"`, with `preset=${PRESET:-debug-asan}` |
|---|---|
| `configure` | `cmake --preset "$preset"` |
| `build` | `PRESET=$preset spur configure`, then `cmake --build --preset "$preset" -j` |
| `test` | `PRESET=$preset spur build`, then `ctest --preset "$preset" --no-tests=error "$@"` |

`dev` and `wasm`: `_in dev -- "$@"` / `_in wasm -- "$@"`, then `[ "$#" -gt 0 ] || set -- bash` and `exec "$@"`. Each task has a `## description` for `--list`; `test`, `dev` and `wasm` have a `##` block with one example (`./spur test -R '^ecs\.'`, `./spur dev ./build/release/bench/ecs_bench_iterate`, `./spur dev ./spur -x test` to trace a task).

- [ ] **Step 6: Check the Spurfile and the line endings**

Run: `./spur --check && git add spur Spurfile .gitattributes && git ls-files --eol spur Spurfile`
Expected: no output from `--check`; both files `i/lf`.

- [ ] **Step 7: Run the test in the old image**

Run: `MSYS_NO_PATHCONV=1 docker compose run --rm dev python3 -I build/spurfile-test/test_spurfile.py`
Expected: `OK`, 6 tests (the test sets its own environment, so the old image is enough).

- [ ] **Step 8: Rebuild the images and run end to end from Git Bash**

Run: `docker compose build && ./spur test -R '^ecs\.' && PRESET=release ./spur build`
Expected: the `ecs` suites pass; the Release build ends without error.

Run: `./spur test`
Expected: every test passes, the same count as `docker compose run --rm dev make test`.

- [ ] **Step 9: Commit**

```bash
git add spur Spurfile .gitattributes Dockerfile Dockerfile.wasm
git commit -m "build: run the native toolchain tasks with spur"
```

### Task 2: WASM, TypeScript and game tasks

**Files:**
- Modify: `Spurfile`
- Modify, temporary and not committed: `build/spurfile-test/test_spurfile.py`

**Interfaces:**
- Consumes: `_fail`, `_in` (with `--service`), `run_spur`, `docker_arguments`, `FORWARDED` from Task 1.
- Produces tasks: `schema-tool`, `ts`, `games-test [ctest args]`, `game <game>`, `serve <game>`.

- [ ] **Step 1: Add the failing tests**

```python
    def test_wasm_tasks_dispatch_to_wasm(self):
        for task in ["schema-tool", "ts", "games-test"]:
            with self.subTest(task=task):
                self.run_spur([task])

                self.assertEqual(self.docker_arguments()[-3:], ["wasm", "./spur", task])

    def test_serve_uses_the_web_service_with_ports(self):
        self.run_spur(["serve", "arena"])

        self.assertEqual(self.docker_arguments(), ["MSYS_NO_PATHCONV=1", *FORWARDED, "--service-ports", "web", "./spur", "serve", "arena"])

    def test_game_and_serve_require_a_game(self):
        for task in ["game", "serve"]:
            with self.subTest(task=task):
                result = self.run_spur([task])

                self.assertEqual(result.returncode, 64)
                self.assertIsNone(self.docker_arguments())

    def test_web_tasks_reject_presets_outside_wasm_games(self):
        for arguments in [["game", "arena"], ["serve", "arena"], ["games-test"]]:
            with self.subTest(arguments=arguments):
                result = self.run_spur(arguments, preset="release")

                self.assertEqual(result.returncode, 64)
                self.assertIsNone(self.docker_arguments())

    def test_games_test_inside_dev_exits_64(self):
        result = self.run_spur(["games-test"], toolchain="dev")

        self.assertEqual(result.returncode, 64)
```

- [ ] **Step 2: Run them to verify they fail**

Run: `./spur dev python3 -I build/spurfile-test/test_spurfile.py`
Expected: the 5 new tests FAIL with spur's exit 67 (unknown task); the 6 from Task 1 pass.

- [ ] **Step 3: Write the tasks**

Translate `Makefile:30-77`; argument and preset checks come before `_in`, so the host fails before starting a container:

| Task | Checks, then `_in` | Body |
|---|---|---|
| `schema-tool` | `_in wasm -- "$@"` | `cmake --preset schema-tool`, `cmake --build --preset schema-tool -j` |
| `ts` | `_in wasm -- "$@"` | `spur schema-tool`; the two generation loops of `Makefile:39-40` with `build/schema-tool/bin/oly-schema`; `npm="$(dirname "$(command -v node)")/npm"`, `[ -d node_modules ] \|\| "$npm" ci --no-audit --no-fund`, `"$npm" run build` |
| `games-test` | `preset=${PRESET:-wasm-games}` must match `wasm-games*`; `_in wasm -- "$@"` | `spur ts`, `PRESET=$preset spur test "$@"` |
| `game` | `$1` required; `preset=${PRESET:-wasm-games-release}` must match `wasm-games*`; `_in wasm -- "$@"` | `spur ts`, `PRESET=$preset spur build`, then the folder, the two `cp` and the esbuild bundle of `Makefile:63-67` with `build/$preset/web/$1` |
| `serve` | same checks as `game`; `_in wasm --service web --service-ports -- "$@"` | `spur game "$1"`, then esbuild with `--servedir` and `--serve=0.0.0.0:8000` (`Makefile:71`) |

Usage messages: `usage: ./spur game <game>`; preset message: `needs a wasm-games* preset (got: $preset)`. `games-test` has a `##` block with `PRESET=wasm-games-release ./spur games-test -R '^ts\.'`; `serve` notes that esbuild stops when stdin closes.

- [ ] **Step 4: Run the tests**

Run: `./spur --check && ./spur dev python3 -I build/spurfile-test/test_spurfile.py`
Expected: no `--check` output; `OK`, 11 tests.

- [ ] **Step 5: Run the WASM tasks end to end**

Run: `./spur games-test && PRESET=wasm-games-release ./spur games-test -R '^ts\.' && ./spur game arena && ls build/wasm-games-release/web/arena`
Expected: both ctest runs pass; the folder holds `index.html`, `main.js`, `arena_game.mjs`, `arena_game.wasm`.

Run: `./spur serve arena`, open `http://localhost:8000`, stop with Ctrl+C.
Expected: the arena page loads.

- [ ] **Step 6: Commit**

```bash
git add Spurfile
git commit -m "build: run the WASM and game tasks with spur"
```

### Task 3: remove the Makefile

**Files:**
- Delete: `Makefile`
- Modify: `docker-compose.yaml:9,16`, `cmake/OlyGame.cmake:10`, `cmake/OlyOptions.cmake:10`, `CLAUDE.md` (first rule)

**Interfaces:**
- Consumes: every task from Tasks 1 and 2.

- [ ] **Step 1: Remove the Makefile and point the rest at spur**

- `git rm Makefile`.
- `docker-compose.yaml`: `command: ./spur build` (service `build`), `command: ./spur test` (service `run`).
- `cmake/OlyGame.cmake:10`: `run './spur schema-tool' (or './spur ts') before configuring with OLY_BUILD_GAMES=ON`.
- `cmake/OlyOptions.cmake:10`: `"Build the games (requires ./spur ts first)"`.
- `CLAUDE.md`, first rule, exact text:

```markdown
- Toolchain only in Docker, through `./spur <task>` from Git Bash (`./spur --list` shows the tasks; `./spur dev ...` for any other native C++ command, `./spur wasm ...` for WASM and TS). There is no C++ compiler on the host.
```

- [ ] **Step 2: Verify the compose services and the full suite**

Run: `docker compose run --rm build && ./spur test`
Expected: the `build` service builds `debug-asan`; every test passes.

- [ ] **Step 3: Commit**

```bash
git add -A Makefile docker-compose.yaml cmake/OlyGame.cmake cmake/OlyOptions.cmake CLAUDE.md
git commit -m "build: remove the Makefile"
```

### Task 4: documentation and tooling

**Files:**
- Modify: `README.md`, `games/arena/README.md`, `docs/development/commands.md`, `docs/development/guide-style.md:19`, `docs/guide/01-getting-started.md`, `docs/guide/11-testing.md`, `docs/guide/13-troubleshooting.md`, `docs/architecture/build-and-tests.md`, `docs/architecture/games.md`, `docs/architecture/repository.md:4`, `docs/architecture/schema-and-codegen.md:42`, `docs/benchmarks/bridge/README.md`, `.claude/agents/perf-reviewer.md`, `.claude/agents/spec-conformance-reviewer.md`, `.claude/skills/add-test-case/SKILL.md:163`, `.claude/skills/bench-compare/SKILL.md`

**Interfaces:**
- Consumes: the task names and arguments from Tasks 1 and 2.

- [ ] **Step 1: Rewrite the commands**

Drop the `DC=`/`WC=` variables and write each command in full. Mapping:

| Before | After |
|---|---|
| `$DC make test T='^ecs\.'` | `./spur test -R '^ecs\.'` |
| `$DC make build PRESET=release` | `PRESET=release ./spur build` |
| `$WC make ts` / `schema-tool` / `games-test` | `./spur ts` / `schema-tool` / `games-test` |
| `$WC make game G=arena` | `./spur game arena` |
| `docker compose run --rm --service-ports web make serve G=arena` | `./spur serve arena` |
| `$WC sh -c 'make ts && make test PRESET=wasm-games-release T=^ts\.'` | `PRESET=wasm-games-release ./spur games-test -R '^ts\.'` |
| `make game G=<game> PRESET=wasm-games` | `PRESET=wasm-games ./spur game <game>` |
| `$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build ...'` | `./spur ts && PRESET=wasm-games-release ./spur build` |
| `$DC <cmd>` / `$WC <cmd>` | `./spur dev <cmd>` / `./spur wasm <cmd>` |
| `Makefile` (file, targets) | `Spurfile` (tasks), runner `spur` |

`perf-reviewer.md:84` becomes `./spur dev sh -c 'PRESET=release ./spur build > /dev/null && tools/extract_codegen.sh ...'` with the rest of the line unchanged.

`docs/development/commands.md` and `README.md` also say: the host needs Git Bash and Docker; run `docker compose build` once after this change (images without `OLY_TOOLCHAIN` stop with exit 69); `-x` and `-n` given on the host show only the dispatch, so trace with `./spur dev ./spur -x test`. Follow `docs/development/guide-style.md` in `docs/guide/`.

- [ ] **Step 2: Test interactive use from mintty**

Ask the user to run `./spur dev` in Git Bash's default terminal (mintty) and report whether a shell opens.
Expected: a `bash` prompt in `/app`. If it fails with `the input device is not a TTY`, add the workaround that worked (`winpty ./spur dev`, or Windows Terminal) to `docs/development/commands.md`.

- [ ] **Step 3: Verify no living reference to make remains**

Run: `git grep -nE '\bmake (test|build|ts|game|serve|schema-tool|games-test|configure|run|bench|all)\b|Makefile|\$DC\b|\$WC\b|\bDC=|\bWC=' -- ':!docs/superpowers'`
Expected: no output.

- [ ] **Step 4: Run the spur tests one last time and delete them**

Run: `./spur dev python3 -I build/spurfile-test/test_spurfile.py && rm -r build/spurfile-test && git ls-files '*test_spurfile*'`
Expected: `OK`, 11 tests; the folder is gone; `git ls-files` lists nothing (the test was never versioned).

- [ ] **Step 5: Commit**

```bash
git add README.md games/arena/README.md docs/development docs/guide docs/architecture docs/benchmarks .claude/agents .claude/skills
git commit -m "docs: cite ./spur instead of make"
```
