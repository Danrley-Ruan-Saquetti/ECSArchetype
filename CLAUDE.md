# CLAUDE.md

Oly: 2D game engine in C++20 and TypeScript, on top of a single-threaded archetype ECS (study and benchmark project). C++ core compiled to WASM, C API `oly*`, thin TS layer.

## Documentation

Start with `docs/architecture/README.md`. It has the overview and the index of all documentation: architecture, glossary, commands, specs, plans and benchmarks.

## Rules that always apply

- Toolchain only in Docker, through `./spur <task>` from Git Bash (`./spur --list` shows the tasks; `./spur dev ...` for any other native C++ command, `./spur wasm ...` for WASM and TS). There is no C++ compiler on the host.
- Commits, comments and assert messages in English.
- Do not format `.cpp`/`.hpp` by hand: the `.claude/hooks/clang-format.sh` hook formats after each edit (it fails if Docker is stopped).
- Use the glossary names. No aliases or compatibility `using` with old names. Game concepts never in `engine/`; game names only in `engine/testing/fixture/` and `games/`.
- When changing `table`, `archetype`, `query*`, `command_buffer` or `world`: keep `checkInvariants` covering the change and run the `spec-conformance-reviewer` and `perf-reviewer` agents.
- New test: use the `add-test-case` skill.
- Any change that adds, changes or removes public API (the surfaces listed in `docs/development/guide-style.md`) updates the user guide `docs/guide/` in the same change, following that style.

## Code style

@docs/development/code-style.md
