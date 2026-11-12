# Code style

## Language

Commits, comments and assert messages are in English.

## Formatting

- `.clang-format`: Google base, indentation 2, `NamespaceIndentation: All`, no column limit.
- A Claude Code `PostToolUse` hook (`.claude/hooks/clang-format.sh`) runs `clang-format -i` in the `dev` container after every edit of a `.cpp`/`.hpp`. Do not format by hand. If Docker is stopped, the hook fails.
- `-Wall -Wextra -Wpedantic -Werror` on every target (`oly_warnings`): a warning breaks the build. TS in `strict`.

## C++ rules

- Braces on every `if`/`for`/`while`/`else`, even with a single line.
- Full names, no abbreviations (`entity`, `componentId`, `sourceToDestination`, not `e`, `cid`, `srcToDst`).
- Types in `PascalCase`, functions and variables in `camelCase`, private members with a `_` suffix.
- Comments only for the why or for non-obvious contracts; nothing that repeats the code. If a comment explains the what, prefer a name or an extracted function.
- Blank lines separate ideas (logical blocks), not individual lines.
- One `namespace` block per file, without reopening the same namespace further down.
- In a `class`/`struct`, the attributes (data members, including private and static ones) go at the top, before constructors and methods. In a `class` this means opening with a `private:` section of data only (and the nested types they use), then `public:` and, finally, `private:` with the helper methods. Moving blocks of members must not change their relative order (initialization order, `-Wreorder`).
- `#include` project headers with quotes (`"oly/ecs/..."`), after the standard library headers.
- `OLY_ASSERT` messages include the operation, the `Entity` and the type name.
