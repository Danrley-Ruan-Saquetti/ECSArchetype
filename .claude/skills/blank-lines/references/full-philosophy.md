# Blank Lines: Philosophy and Principles (full document)

Source document of the skill. Consult it when you need examples in a specific language, the table of grouping levels, each ecosystem's conventions or what formatters can automate.

## Contents

- Premise
- Fundamental principles
- Grouping levels
- Recurring categories (dependencies, constants, members, function body, guards, control, tests)
- Conventions by language
- Anti-patterns
- Blank lines and refactoring
- Automation: what the tools solve
- Review checklist

## Premise

A blank line is punctuation: it separates **ideas**, not lines. It works like a paragraph break in a text. Lines that only make sense together stay together, and a change of intent gets a blank line.

Code is read far more often than it is written. The reader does not read line by line: they first scan the visual shape of the function to understand its structure, and only then read the detail of the part that matters. Blank lines are what give shape to that first reading.

Vertical space is therefore information. Each blank line states something: "a step ends here". When it appears where there is no change of step, the statement is false and the reader is misled. When it is missing, the reader has to find out alone where the steps start and end.

These principles hold for any language with imperative syntax: C, C++, Java, C#, PHP, TypeScript, Python, Go, Rust. The examples alternate between languages on purpose, because the idea is the same in all of them.

## Fundamental principles

Six principles cover almost every decision. The remaining cases derive from them.

1. **Cohesion defines the block.** Lines that work together toward a single goal form a block, and a block is never split in half. A blank line appears only where the goal changes.
2. **The mental title test.** If you can give a short name to each block ("validates the input", "builds the query", "writes the result"), the grouping is correct. If a block needs two names, it is two blocks. If two blocks take the same name, they are one.
3. **A single intensity.** Within a file, use at most one blank line in a row. Two lines do not reliably communicate "bigger separation", because each author uses them differently. Larger hierarchy is expressed by the language's structure (functions, classes, modules), not by more space. The exception is a formal language convention, such as PEP 8's two lines between top-level definitions.
4. **Proximity between declaration and use.** A variable stays next to the lines that consume it, and not in a distant "declarations" block. That way each block carries everything it needs to be read on its own.
5. **Delimiters already separate.** Braces, `begin/end` and indentation already mark the start and end of a scope. A blank line right after opening a block, or right before closing it, repeats that information and only wastes space.
6. **Cohesion beats symmetry.** Space does not exist to make the code "airy" or visually balanced. It exists to communicate structure. Dense, well-grouped code is better than uniformly spaced code.

## Grouping levels

The same rule repeats at different scales. At each level, the blank line separates the units of that level, and the language's structure takes care of the level above.

| Level | Unit separated | What the blank line communicates |
| --- | --- | --- |
| File | Header, imports, constants, types, implementation | Change of module section |
| Type (class, struct) | Groups of fields, constructors, methods | Change of member role |
| Function | Phases: guard, preparation, processing, result | Change of algorithm step |
| Inner block | Sub-steps inside a loop or condition | Change of sub-step (rare; usually signals extraction) |
| Long expression | Groups of keys in a literal or arguments | Change of semantic group |

The deeper the level, the rarer the blank line should be. A loop body that needs three inner blocks is asking to become a function.

## Recurring categories

The same shapes appear in every language. Each example below uses a different language, but the pattern transfers unchanged.

### Dependencies: one group per origin

The order is: standard library, then external dependencies, then the project itself. The blank line tells the reader where each thing comes from and how much of it they control.

```cpp
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <spdlog/spdlog.h>

#include "ecs/world.hpp"
#include "physics/body.hpp"
```

### Constants and configuration: one group per domain

```python
MAX_ENTITIES = 100_000
CHUNK_SIZE = 16_384

GRAVITY = -9.81
FIXED_DT = 1 / 60
```

### Members of a type: one group per role

Data fields, control state, construction and operations form distinct groups. Parallel members, such as the arrays of an SoA layout, stay together because they represent a single idea.

```php
final class Inventory
{
    private array $itemIds = [];
    private array $quantities = [];

    private int $count = 0;

    public function __construct(private readonly int $capacity) {}

    public function add(int $itemId, int $qty): void
    {
        if ($this->count >= $this->capacity) {
            throw new OverflowException('Inventory full');
        }

        $i = $this->count++;
        $this->itemIds[$i] = $itemId;
        $this->quantities[$i] = $qty;
    }
}
```

### Function body: guard, preparation, processing, result

Most functions follow this sequence of phases, and each phase takes one block.

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

### Guard clauses: a single block

Several guards in a row form a single step ("reject invalid cases") and so they stay together.

```python
def can_attack(attacker, target):
    if attacker is None or target is None:
        return False
    if not attacker.alive:
        return False
    if target.invulnerable:
        return False

    return distance(attacker, target) <= attacker.range
```

### Control structures: space before and after

A multi-line loop or condition marks a change in flow, so it gets a blank line before and after. A variable that exists only for that loop stays next to it.

The "before" applies to the step, not to the keyword: the step starts where its preparations start. Declarations that exist only to feed the condition or the loop — the expected value the `if` compares, the accumulator the `for` fills, the uninitialized variable an `if/else` chain assigns, the `clear()` before rebuilding a vector — are part of it, and the blank line goes before them. Applying the rule mechanically, with a line before every `if`/`for`, produces visual symmetry and splits each step between the preparation and the use. Inseparable pairs (destroying a row and filling the hole, opening and checking) also beat this rule.

```cpp
// Wrong: the preparation was separated from the comparison that is its reason to exist
const int16_t expected = column == NoColumn ? -1 : column;

if (mapping[i] != expected) {
    return false;
}

// Correct: computing the expected value and comparing is a single step
const int16_t expected = column == NoColumn ? -1 : column;
if (mapping[i] != expected) {
    return false;
}
```

On the other hand, a declaration shared by several following steps (a label used by several guards, a pointer read by the whole rest of the function) is a preparation step of its own and can stay separate.

### Result: the `return` as a step

The final `return` forms a block of its own when the title of the last step does not include it, that is, when it delivers the product of earlier steps. It stays attached when it is the conclusion of its own step: `return` inside a guard, a single-step function, or a value created on the previous line only to be returned.

```cpp
Entity World::place(Archetype& archetype) {
    Entity entity = store_.allocate();
    EntityRecord& record = store_.record(entity.index);

    record.row = archetype.table.pushUninitialized(entity);
    record.archetype = &archetype;

    return entity;
}
```

Three steps: get identity and slot, place in the archetype, deliver. The `record` stays with the allocation because it is a lookup derived directly from the previous line; a declaration that bridges two steps stays in the step that produces it when it is a derived lookup, and in the one that consumes it when it is built for that step.

```cpp
void integrate(Motion& m, float dt) {
    for (std::size_t i = 0; i < m.count; ++i) {
        m.velY[i] += kGravity * dt;
    }

    for (std::size_t i = 0; i < m.count; ++i) {
        m.posX[i] += m.velX[i] * dt;
        m.posY[i] += m.velY[i] * dt;
    }

    m.dirty = true;
}
```

### Tests: Arrange, Act, Assert

In tests, the blocks have a formal meaning: there are exactly three. If the act block has more than one line, the test probably checks more than one behaviour.

```php
public function testRemoveKeepsArrayDense(): void
{
    $pool = new Pool(4);
    $pool->spawn(1);
    $pool->spawn(2);

    $pool->remove(0);

    $this->assertSame(1, $pool->size());
    $this->assertSame(2, $pool->get(0));
}
```

## Conventions by language

The philosophy is universal, but each ecosystem fixes a few numbers. When there is an official guide or a standard formatter, it prevails over personal preferences, because consistency within the ecosystem is worth more than any individual choice.

| Language | Reference | Vertical spacing rules |
| --- | --- | --- |
| Python | PEP 8, Black, Ruff | 2 lines between top-level definitions; 1 between methods; sparing use inside functions |
| PHP | PSR-12, PHP-CS-Fixer | 1 line after `namespace` and after the `use` block; `use` grouped by kind (classes, functions, constants); no blank line right after the opening brace |
| C++ | Google Style, clang-format | Minimize vertical space; do not start or end blocks with a blank line; `MaxEmptyLinesToKeep: 1` |
| TypeScript / JS | Prettier, ESLint | Prettier reduces runs to 1 line; ESLint `padding-line-between-statements` and `lines-between-class-members` for fine-grained rules |
| Go | gofmt | Reduces runs to 1 line; `import` groups separated by a blank line |
| Rust | rustfmt | `blank_lines_upper_bound` default of 1 |

Python is the main exception to the single-intensity rule: the two lines between top-level definitions are a formal convention, not an author preference. They make up for the absence of braces, which in the other languages already visually delimit each definition.

Values according to the documentation known up to 2026; check the guide for the version your project uses.

## Anti-patterns

Each anti-pattern is a blank line that states something false, or the absence of one that should be there.

- **Wall.** Validation, I/O and computation in a row, with no separation at all. The reader has to rebuild alone where each step starts and ends.
- **Uniform spacing.** A blank line between every statement. If everything is separated, nothing is grouped, and the space stops communicating.
- **Empty frame.** A blank line right after opening a block or before closing it, which duplicates the information of the brace or the indentation.
- **Split block.** A pair that only makes sense together, separated by a blank line: a `lock` and its `unlock`, a declaration and its single use, a file open and its error check.
- **Space by syntax.** A blank line before every `if`, `for` or `return` because "it looks more organized", separating it from the declaration that feeds it. It is symmetry, not structure: the criterion must be the change of step, not the kind of the next statement.
- **Hierarchy by quantity.** Two or three blank lines to signal a "more important section", outside a formal language convention.
- **Title comments inside functions.** Lines like `// --- Validation ---` dividing a long body. The need for titles indicates that each section should be a function with that name.
- **Whitespace diff.** Changing blank lines in code you are not modifying. It pollutes the history and makes review harder.

```python
# Split block: opening and check separated
handle = open_device(path)

if handle is None:
    raise DeviceError(path)

# Correct: the two lines form a single step
handle = open_device(path)
if handle is None:
    raise DeviceError(path)
```

## Blank lines and refactoring

A well-delimited block is a function that has not been extracted yet. The blank line marks the cut point, and the block's mental title is already the name of the new function.

This creates a useful cycle. First, group the code into cohesive blocks. Then, when a function accumulates many blocks (as a rule of thumb, more than 3 or 4), extract each one. The original function becomes a sequence of named calls, and the blank lines inside it almost disappear, because each line is already a whole step.

```ts
// Before: three blocks with implicit titles
function processOrder(order: Order): Receipt {
  if (!order.items.length) throw new EmptyOrderError();
  if (!order.customer.verified) throw new UnverifiedError();

  const subtotal = order.items.reduce((s, i) => s + i.price * i.qty, 0);
  const discount = subtotal > 500 ? subtotal * 0.1 : 0;
  const total = subtotal - discount;

  const receipt = { orderId: order.id, total, issuedAt: new Date() };
  repository.save(receipt);
  return receipt;
}

// After: each block became a function named after its title
function processOrder(order: Order): Receipt {
  validate(order);
  const total = calculateTotal(order.items);
  return issueReceipt(order.id, total);
}
```

The reverse path also works as a diagnosis. If you cannot decide where to put a function's blank lines, its responsibilities are probably mixed. The problem is not formatting, but design.

## Automation: what the tools solve

Formatters solve the **mechanical** part, but the **semantic** part remains a human decision. They enforce limits (at most 1 line in a row, no line at the start of a block, 2 lines between definitions in Python). They do not know, however, where a step of the algorithm ends, because that depends on the meaning of the code.

| Aspect | Automatable | Tools |
| --- | --- | --- |
| Limit of lines in a row | Yes | Prettier, Black, gofmt, rustfmt, clang-format |
| Lines at the start and end of blocks | Yes | clang-format, PHP-CS-Fixer, ESLint |
| Space between definitions and members | Yes | PEP 8 via Black/Ruff, ESLint `lines-between-class-members` |
| Import groups | Yes | isort, ESLint `import/order`, clang-format `IncludeBlocks` |
| Line before `return` or after declarations | Partially | ESLint `padding-line-between-statements` |
| Where a logical step ends | No | Human review |

Rules like "always a blank line before `return`" should be adopted with care. In 2- or 3-line functions they fragment the code with no gain, and they work better as a heuristic than as a mandatory rule.

Configure the formatter in the project and run it in CI. That way review discussions are limited to what the tool cannot decide.

## Review checklist

- [ ] Each block has a single, short mental title
- [ ] No block needs two titles, and no pair of neighbouring blocks shares the same title
- [ ] At most 1 blank line in a row (except where the language convention defines another value)
- [ ] No blank line right after opening a block or before closing it
- [ ] Variables declared next to the block that consumes them
- [ ] Inseparable pairs (open/check, lock/unlock, declare/use) stay together
- [ ] Multi-line loops and conditions have space before (their preparations) and after
- [ ] The final `return` is separated only when it delivers the product of earlier steps
- [ ] Consecutive guards form a single block
- [ ] Tests follow exactly three blocks: Arrange, Act, Assert
- [ ] Functions with more than 3 or 4 blocks were considered for extraction
- [ ] The diff does not change blank lines in unrelated code