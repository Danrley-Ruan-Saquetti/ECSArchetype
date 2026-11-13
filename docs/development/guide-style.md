# Guide style

How to write and maintain the user guide in `docs/guide/`. The guide teaches how to **use** the engine to make a game. The architecture docs (`docs/architecture/`) explain how the engine is **built** and why. Keep the two apart: the guide links to the architecture for the "why" and does not repeat it.

## When the guide must change

Every change that adds, changes or removes something in the **public API** updates the guide in the same change (same commit or same PR). A public API change without a guide update is incomplete.

The public API is:

| Surface | Where it is defined |
|---|---|
| `@oly/core` exports | `engine/environments/web/clients/typescript/core/api/public-exports.txt` |
| `@oly/client` exports | `engine/packages/client/src/index.ts` |
| C++ public headers | `engine/runtime/<module>/include/oly/<module>/*.hpp`, except `detail/` |
| registration macros | `OLY_SYSTEM`, `OLY_HANDLER`, `OLY_STATE_HOOK` |
| the C API | `engine/runtime/capi/include/oly/capi/c_api.hpp` |
| the schema language and the generated C++ | `engine/environments/web/clients/typescript/core/src/schema.ts`, `engine/tools/codegen/` |
| how a game is built | `cmake/OlyGame.cmake` (`oly_add_game`), the `game`, `serve` and `games-test` targets of the `Makefile` |
| behaviour a game relies on | loop order, deferred vs. direct changes, event rounds, scope rules, validity of views, error messages |

What to update:

| The change | Update |
|---|---|
| new function, method, option or macro | the page of its topic, the [cheat sheet](../guide/cheat-sheet.md) and, if it enables a new pattern, a [recipe](../guide/recipes.md) |
| renamed or changed signature | every example that uses it (search the whole `docs/guide/`) and the cheat sheet |
| removed | every mention; replace examples that depended on it |
| new or changed error | the table in [Troubleshooting](../guide/13-troubleshooting.md) |
| changed behaviour (order, timing, scope) | the page that describes it and any recipe that relied on it |
| new page | the index tables in `docs/guide/README.md` and the navigation lines of the neighbouring pages |

Before finishing, search `docs/guide/` for the old name and check the links you touched.

## Audience and tone

- The reader wants to make a game. They know TypeScript; C++ is optional.
- Start each page and each section with what the reader can do, then how.
- Explain the contract the reader depends on: what is deferred or direct, what stays valid, what throws. Leave internal mechanisms to the architecture docs.

## Language and writing

- English, like the rest of the repository.
- Simple words, active voice, one idea per sentence, short paragraphs.
- Use the glossary names ([glossary](../architecture/glossary.md)). Never an alias or an old name.
- Prefer a table to a paragraph when comparing two or more options.
- No filler: no "simply", "just", "easily", no restating the heading.

## Game-agnostic

The guide does not reference any specific example game. More games will live in `games/`, and all of them are references.

- Point to `games/` in general: "each game has its own `README.md`".
- Write examples inline. Do not link to a game's file for code the reader needs.
- Use generic names: `Player`, `Enemy`, `Position`, `WorldWidth`, `<game>`. Not names that belong to one game.
- What is specific to one game goes in that game's `README.md`.

## Files and navigation

```
docs/guide/
  README.md                 index: what you can build, learning path, references, example games, conventions
  NN-topic.md               learning path, read in order (01-getting-started.md ... 13-troubleshooting.md)
  recipes.md                ready patterns, one section each
  cheat-sheet.md            every public name on one page
```

- Numbered pages form the learning path. Each one builds on the previous ones. Unnumbered pages are references.
- File names are lowercase `kebab-case`.
- Each numbered page starts with `# N. Title` and a navigation line right under it:

  ```markdown
  [← Previous title](NN-previous.md) · [Guide index](README.md) · Next: [Next title →](NN-next.md)
  ```

- Each numbered page ends with a `## Next` section: one line with the link and what the next page teaches.
- Links are relative. Anchors follow GitHub's rule: lowercase, punctuation removed, spaces become `-` (`## C++ tasks and payloads` → `#c-tasks-and-payloads`).

## Page structure

1. Title and navigation line.
2. One or two sentences: what this is and why the reader needs it.
3. The smallest working example.
4. Sections from the common case to the rare one.
5. Rules and pitfalls next to the feature they belong to, not in a separate list at the end.
6. `## Next`.

## Examples

- **TypeScript first**, then C++ when C++ is the better fit or differs in a way that matters.
- **Real signatures only.** Check each call against the source before writing it. An example that does not compile is worse than none.
- **Complete enough to copy**: imports when they are not obvious, the schema entries an example needs, no `...` inside the logic that matters.
- **Follow the code style** ([code-style.md](code-style.md)): full names, braces on every `if`/`for`, blank lines between steps.
- **Comments only for the why**, the same rule as in the code. No comments that narrate the example step by step: put that in the prose before the block.
- **Commands** use `$WC` (`docker compose run --rm wasm`) and `$DC` (`docker compose run --rm dev`), as in [commands.md](commands.md).
- Show the wrong way only when the mistake is common, and always next to the right way.

## Recipes

- One problem per section, named by what the reader wants (`## Ability with a cooldown`), listed in the table at the top.
- Each recipe shows the schema entries it needs, then the code, then one or two lines on why it is built that way or when to choose another tool.

## Cheat sheet

- Every public name, grouped by topic, with the shortest correct signature.
- No explanations beyond a few words: link to the page instead.
