# The `.oly` schema language

A `.oly` file describes the data layout of one game: its components, tags, events, actions and states. There is one file
per game, named `<game>.oly`, and it is the single source of the layout for every environment: `oly-schema` reads it and
emits the code of each client (C++, TypeScript, ...), so the layout is never written twice.

## Grammar

```
file        = { declaration } ;
declaration = component | event | tag | action | state ;
component   = "component" word "{" [ fields ] "}" ;
event       = "event" word "{" [ fields ] "}" ;
tag         = "tag" word ;
action      = "action" word ":" kind ;
state       = "state" word ;
fields      = field { "," field } [ "," ] ;
field       = word ":" scalar ;
scalar      = "f32" | "f64" | "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "entity" ;
kind        = "button" | "axis2d" | "pointer" ;
word        = ( letter | digit | "_" ) { letter | digit | "_" } ;
```

## Lexical rules

- Space, tab and line breaks separate tokens and are otherwise free, so a field list may span several lines.
- `#` starts a comment that runs to the end of the line.
- Keywords are recognized only at the start of a declaration, so a field may be named `state` or `tag`.
- A word is `[A-Za-z0-9_]+`: `1abc` is one word and reaches the naming rule, which rejects it with its own message.

## Naming rules and limits

| Rule | Applies to |
|---|---|
| PascalCase, `[A-Z][A-Za-z0-9]*` | component, tag and event names |
| camelCase, `[a-z][A-Za-z0-9]*`, and not a C++ reserved word | fields, actions and states |
| at least one field (use `tag` for none) | components and events |
| no repeated field inside a type | fields |
| no name repeated across the whole file | every declaration |
| at most 32 | states |

The names become C++ identifiers, hence the reserved-word rule. A target language may add its own restrictions in its
emitter; the language core knows none.

## Order

Each group keeps its declaration order, and that order is its id order: `ComponentId` for components and tags (one
group), dispatch order for events, `ActionId` for actions and `StateId` for states. Declarations of different groups
may be interleaved freely.

## Layout

Fields take natural alignment in declaration order. `entity` has 8 bytes and alignment 4. A component's size is rounded
up to its largest alignment.

## Hash

The schema hash is the 32-bit FNV-1a of the canonical form defined in `hash.hpp`. The host and the client compare it to
detect a client built from another schema.

## Errors

An error is reported as `<file>:<line>:<column>: <message>`, with 1-based line and column (the column counts bytes).
Only the first error is reported, and the build fails without writing any file.

## Corpus

`corpus/valid/` holds files that must parse. Each file in `corpus/invalid/` states its error on the first line, as
`# expect: <line>:<column>: <message>`, and has its declarations from line 2. A new rule adds a file there.
