---
name: blank-lines
description: Philosophy and rules for vertical spacing in code — where to add and where to remove blank lines so that they separate ideas, not lines. Use whenever you write, edit, refactor, format or review code in any imperative language (C, C++, Java, C#, PHP, TypeScript/JS, Python, Go, Rust), even if the user does not mention blank lines. Also use when the request involves readability, function organization, grouping of imports or members, test structure (Arrange/Act/Assert), formatter configuration (Prettier, Black, clang-format, gofmt, rustfmt, PHP-CS-Fixer) or style code review.
---

# Blank Lines

A blank line is punctuation: it separates **ideas**, not lines. Each one states "a step ends here". If it appears where there is no change of step, it misleads the reader; if it is missing where there is one, it forces the reader to rebuild the structure alone. The reader first scans the visual shape of the function and only then reads the detail — blank lines are what give shape to that first reading.

For examples in several languages, the table of grouping levels, per-ecosystem conventions and the automation section, read `references/full-philosophy.md`. This file holds what you need to apply the philosophy day to day.

## The six principles

1. **Cohesion defines the block.** Lines that work together toward a single goal form a block — one step of the algorithm — and a block is never split in half. The blank line appears only where the goal changes, never because of the kind of statement that comes next (`if`, `for`, `return`).
2. **Mental title test.** Each block must accept a short name ("validates the input", "builds the query", "writes the result"). A block with two names is two blocks; two neighbouring blocks with the same name are one.
3. **A single intensity.** At most one blank line in a row. Larger hierarchy is expressed by the language's structure (functions, classes, modules), not by more space. Exception: formal conventions, such as PEP 8's 2 lines between top-level definitions in Python.
4. **Declaration near use.** A variable stays next to the lines that consume it, so each block reads on its own.
5. **Delimiters already separate.** No blank line right after opening a block (`{`, `:`, `begin`) nor right before closing it — the brace or the indentation already say that.
6. **Cohesion beats symmetry.** Space communicates structure, it does not "air out" the code. Dense and well grouped is better than uniformly spaced.

## How to apply when writing code

Before typing a function body, think about the phases. Most follow **guard → preparation → processing → result**, and each phase is a block:

```ts
async function importLevel(path: string): Promise<Level> {
  if (!path.endsWith(".json")) throw new Error(`Invalid format: ${path}`);

  const raw = await readFile(path, "utf8");
  const data = LevelSchema.parse(JSON.parse(raw));

  const entities = data.entities.map(toEntity);
  const tilemap = buildTilemap(data.tiles, data.width);

  return { name: data.name, entities, tilemap };
}
```

Recurring rules, all derived from the principles:

- **Imports:** one group per origin — standard library, external dependencies, project — separated by a blank line.
- **Constants:** one group per domain.
- **Type members:** one group per role (data, control state, construction, operations). Parallel members, such as the arrays of an SoA layout, stay together because they are a single idea.
- **Consecutive guards:** a single block ("reject invalid cases"), with no lines between them.
- **Multi-line loops and conditions:** blank line before and after — but the "before" goes before their **preparations**. The step starts where the declarations that exist only to feed the condition or the loop start (the expected value the `if` compares, the accumulator the `for` fills, the uninitialized variable the `if/else` chain assigns, the `clear()` before rebuilding), not where the `if`/`for` starts.
- **Result:** the final `return` is a block of its own when the title of the last step does not include it — it delivers the product of earlier steps. It stays attached when it is the conclusion of its own step (`return` inside a guard, a single-step function, a value created on the previous line only to be returned).
- **Tests:** exactly three blocks — Arrange, Act, Assert. If the Act has more than one line, the test probably checks more than one behaviour.
- **Inseparable pairs stay together:** open/check error, `lock`/`unlock`, declare/single use, operation/its counterpart (destroy the row and fill the hole). They beat the loops and conditions rule.

```cpp
Entity entity = store.allocate();
EntityRecord& record = store.record(entity.index);

record.row = table.pushUninitialized(entity);
record.archetype = &archetype;

return entity;
```

```cpp
// preparation + condition: a single step
const int16_t expected = column == NoColumn ? -1 : column;
if (mapping[i] != expected) {
  return false;
}
```

**Depth:** the deeper the level, the rarer the blank line. A loop body that needs several inner blocks is asking to become a function.

## How to apply when editing existing code

Here the bigger risk is not getting the spacing wrong, but polluting the diff.

- **Do not change blank lines in code you are not modifying.** A whitespace diff makes review harder and dirties the history, even if the old code violates these principles. If it is worth fixing, mention it to the user and propose a separate commit.
- **Respect the project's formatter.** If there is a `.clang-format`, `.prettierrc`, `pyproject.toml` (Black/Ruff), `.php-cs-fixer.php`, `rustfmt.toml` etc., it prevails over personal preferences — consistency within the ecosystem is worth more. Apply this philosophy only to what the formatter does not decide: **where a logical step ends**.
- **In new or rewritten code within the edit**, apply the principles normally.

## Blank lines as a design signal

A well-delimited block is a function that has not been extracted yet: the blank line is the cut point and the mental title is the name of the new function. Use this actively:

- Function with more than 3–4 blocks → consider extracting each block. The original function becomes a sequence of named calls and the inner blank lines almost disappear.
- Urge to write title comments (`// --- Validation ---`) inside a function → it is the same signal: each section wants to be a function with that name.
- Cannot decide where to put the blank lines → the responsibilities are mixed. The problem is design, not formatting; say so instead of just reformatting.

Do not extract on your own unless the request involves refactoring; suggest it.

## Anti-patterns

| Anti-pattern | Why it is bad |
| --- | --- |
| **Wall** — validation, I/O and computation without separation | The reader rebuilds the steps alone |
| **Uniform spacing** — a line between every statement | If everything is separated, nothing is grouped |
| **Empty frame** — line after `{` or before `}` | Duplicates the delimiter's information |
| **Split block** — inseparable pair with a line in the middle | States a change of step that does not exist |
| **Space by syntax** — line before every `if`/`for`/`return`, separating it from the declaration that feeds it | Visual symmetry instead of steps; splits the step between preparation and use |
| **Hierarchy by quantity** — 2–3 lines for an "important section" | Each author reads it differently; use the language's structure |
| **Title comments inside functions** | Indicates functions not extracted |
| **Whitespace diff** — touching lines of unrelated code | Pollutes the history and the review |

## When reviewing code

Point out spacing problems by citing the violated principle ("split block: the opening and the error check form a single step"), not as personal taste. Prioritize the ones that hide structure (wall, split block, function with too many blocks) over the cosmetic ones (empty frame), and do not ask for changes the project's formatter would already handle.

Checklist:

- [ ] Each block has a single, short mental title
- [ ] No block needs two titles; no neighbouring pair shares the same one
- [ ] At most 1 blank line in a row (unless a language convention says otherwise)
- [ ] No blank line right after opening or before closing a block
- [ ] Variables declared next to the block that consumes them
- [ ] Inseparable pairs kept together
- [ ] Multi-line loops and conditions with space before (their preparations) and after
- [ ] Final `return` separated only when it delivers the product of earlier steps
- [ ] Consecutive guards in a single block
- [ ] Tests with exactly three blocks: Arrange, Act, Assert
- [ ] Functions with more than 3–4 blocks considered for extraction
- [ ] The diff does not change blank lines in unrelated code