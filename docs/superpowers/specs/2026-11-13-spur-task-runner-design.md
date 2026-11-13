# Oly — replace the Makefile with spur — design

Date: 2026-11-13. Base: `worktree-develop-2` at `a96068a`.
Migrated from the lightweight spec `.bamse-kit/specs/001-spur-task-runner/spec.md` (v1.0.0, 2026-11-13).

Marking: **[user decision]** was decided in the conversation; **[author decision]** was added during the migration and
the user may review it; everything else is carried over from the lightweight spec as approved there.

## 1. Intent

The `Makefile` is used only as a task runner: every target is `.PHONY`, nothing depends on timestamps. It pays make's
costs anyway:

- the test filter goes through a variable (`T='^ecs\.'`);
- the `ts` loop needs `$$`;
- the `game`/`serve` checks use `ifeq`/`$(error)`.

On the host every call also needs the `docker compose run --rm dev|wasm` prefix, and the right service depends on the
preset: a `wasm-*` preset in `dev` fails.

spur (https://github.com/esliph/spur, a task runner in one POSIX sh file) fixes the first set with positional
passthrough and one shell per task. A host-side dispatch helper fixes the second.

## 2. Goals, non-goals, anti-goals

**Goals**

- `./spur <task>` is the only way to run the toolchain tasks, and the same command works from the host (Git Bash) and
  inside a toolchain container.
- Arguments after the task name go verbatim to the underlying tool (`./spur test -R '^ecs\.'`).
- The host picks the right container by itself, from the task and the preset.
- Every `Makefile` target has a replacement, and the `Makefile` is removed.
- The living documentation and tooling cite `./spur`, not `make`.

**Non-goals**

- The historical plans and specs in `docs/superpowers/` keep their `make` commands.
- No tasks for the benchmarks or `tools/*.py`; they run through `./spur dev ...` / `./spur wasm ...`.
- The `perf` service and `tools/perf_fragmented.sh` stay as they are.
- The `.claude/hooks/clang-format.sh` hook keeps calling `docker compose` directly.
- Global installation of spur on the host.
- Support for PowerShell: spur needs a POSIX shell (Git Bash, WSL).

**Anti-goals** (these make the change a failure even if it works) **[author decision]**

- A task that behaves differently on the host and inside the container, beyond where it runs.
- A task whose chained calls run in different containers.
- A host requirement beyond Git Bash and Docker.
- A `Makefile` capability lost without a replacement or an explicit decision to drop it.

## 3. Approaches

No alternatives were compared. spur is the user's own task runner, and the user chose it **[user decision]**.

## 4. Design

### 4.1 Files that change

- Vendor `spur` v0.3.1 at the repository root, pinned to the tag, executable in the git index.
- Add a `Spurfile` that replaces every `Makefile` target (§4.4).
- Remove the `Makefile`. Its alias targets go with it: `bench` becomes `PRESET=release ./spur build`; `run` and `all`
  become `./spur test`.
- Mark each toolchain image with its name: `ENV OLY_TOOLCHAIN=dev` in `Dockerfile`, `ENV OLY_TOOLCHAIN=wasm` in
  `Dockerfile.wasm` (the `web` service uses the wasm image). The `make` package stays in `Dockerfile.wasm`: CMake's
  default generator is Unix Makefiles, so `cmake --build` still needs it.
- Force LF for `spur` and `Spurfile` in `.gitattributes`.
- Point the `build` and `run` compose services at `./spur build` and `./spur test`.
- Rewrite the `CLAUDE.md` toolchain rule in terms of `./spur <task>` from Git Bash (`./spur dev ...` /
  `./spur wasm ...` for anything else).
- Update the living documentation and tooling that cite `make`: `README.md`, `games/arena/README.md`,
  `docs/development/commands.md`, `docs/development/guide-style.md`, `docs/guide/`, `docs/architecture/`,
  `docs/benchmarks/bridge/README.md`, `.claude/agents/`, `.claude/skills/`, and the messages in
  `cmake/OlyGame.cmake` and `cmake/OlyOptions.cmake`.

### 4.2 Dispatch

A preamble helper decides where the current task runs:

```sh
_in IMAGE [compose run options] -- "$@"
```

- `OLY_TOOLCHAIN` equals `IMAGE`: returns, and the task runs here.
- `OLY_TOOLCHAIN` is set to another image: error, exit 64 (e.g. `./spur games-test` typed inside `dev`).
- `OLY_TOOLCHAIN` is unset (the host): runs, with `MSYS_NO_PATHCONV=1`,

  ```sh
  exec docker compose run --rm -e PRESET -e OLY_FUZZ_SEED -e OLY_FUZZ_OPS [options] SERVICE ./spur "$SPUR_TASK" "$@"
  ```

  The service is the image's own (`dev`, `wasm`), except for `serve`: it calls `_in` with image `wasm`, service `web`
  and `--service-ports`, so the mismatch check still compares against `wasm` inside the container.
- Runner flags (`-x`, `-n`) are not forwarded into the container: on the host they show only the dispatch. To trace a
  task, run it inside (`./spur dev ./spur -x test`).

Each task calls `_in` first, so the whole task, chained calls included, runs in one container. The marker lives in the
images, not in the compose file, so any container of those images (CI included) runs tasks directly. The `perf`
service is built from `Dockerfile`, so it gets `OLY_TOOLCHAIN=dev` and runs `dev` tasks directly too **[author
decision: noted during the migration, no change needed]**.

### 4.3 Image by preset

- `wasm*` and `native-clang*` presets go to `wasm`, the only image with `clang++` and `g++-multilib`.
- `schema-tool` goes to `wasm`: its build directory is shared with `ts`, which runs in `wasm`.
- Every other preset goes to `dev`.

### 4.4 Tasks

`PRESET` stays an environment variable, forwarded to the container by `_in`.

| Task | Does | Service |
|---|---|---|
| `configure` | `cmake --preset "$PRESET"` (default `debug-asan`) | by preset |
| `build` | `configure` + `cmake --build --preset "$PRESET" -j` | by preset |
| `test [ctest args]` | `build` + `ctest --preset "$PRESET" --no-tests=error "$@"` | by preset |
| `schema-tool` | builds `oly-schema` for the machine (preset `schema-tool`) | `wasm` |
| `ts` | `schema-tool`, generates every TS schema, `npm ci` when `node_modules` is missing, `npm run build`; npm is the one next to `node` (not on the emsdk image's PATH) | `wasm` |
| `games-test [ctest args]` | `ts` + `test` with `PRESET` default `wasm-games`; unlike the Makefile, an explicit `PRESET` is honoured (`PRESET=wasm-games-release ./spur games-test -R '^ts\.'`) | `wasm` |
| `game <game>` | assembles `build/<preset>/web/<game>/`, `PRESET` default `wasm-games-release` | `wasm` |
| `serve <game>` | `game` + esbuild serve on `0.0.0.0:8000` | `web` with `--service-ports` |
| `dev [cmd...]` | runs the command (default `bash`) in the `dev` image | `dev` |
| `wasm [cmd...]` | runs the command (default `bash`) in the `wasm` image | `wasm` |

Examples: `./spur test -R '^ecs\.'`, `PRESET=release ./spur build`, `./spur games-test -R '^ts\.'`,
`./spur serve arena`, `./spur dev ./build/release/bench/ecs_bench_iterate`.

## 5. Edge cases

- `game`/`serve` without a game argument: usage error, exit 64.
- `game`/`serve`/`games-test` with a `PRESET` outside `wasm-games*`: error, exit 64.
- `PRESET` unset on the host: the container must see it unset or empty; tasks default with `${PRESET:-...}`, so both
  work.
- Windows checkout with `core.autocrlf`: `.gitattributes` keeps `spur` and `Spurfile` LF, or `sh` fails in the
  container.
- Git Bash path conversion: `MSYS_NO_PATHCONV=1` on every `docker compose` call.
- Images built before the change lack `OLY_TOOLCHAIN`: `_in` would call `docker compose` inside the container and
  fail. The images must be rebuilt (`docker compose build`); the docs say so.
- `serve` needs an open stdin (esbuild exits when it closes): `docker compose run` keeps it attached.

## 6. Left to the builder

- Interactive `./spur dev` / `./spur wasm` from mintty (Git Bash's default terminal) may fail with "the input device is
  not a TTY". Test it during implementation; if it fails, document the workaround (`winpty`, or Windows Terminal) in
  `docs/development/commands.md`.
- The wording of each documentation update, following `docs/development/guide-style.md` where it applies.
