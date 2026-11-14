# Main architectural decisions

| Decision | Reason | Accepted cost |
|---|---|---|
| Archetype ECS with SoA tables | iteration close to a hand-written SoA | transitions move the entity between tables |
| Memoized `±ComponentId` edges with column mapping | transition with no hash and no allocation | memory per archetype |
| Archetypes never removed | stable pointers, queries without invalidation | empty archetypes remain |
| `detail/` + lint instead of pimpl | hot templates inline | boundary enforced by the lint, not by the compiler |
| Separate template API and `ById` API | the hot path pays no indirection | two implementations to maintain |
| `CommandBuffer` with one transition per entity | cheap batched flush | coalescing complexity (covered by fuzz) |
| Loop and every engine rule in C++ | a C++-only game does not end up with an incomplete engine | TS needs the callback |
| A per-engine script invoker set through the C API; the web host installs the JS one | the core knows no environment; a managed client passes its own handle in `userData` | one function pointer and one `userData` per engine; dispatch by `kind` in each client |
| `.oly` as the single source + C++ generator + hash; each client gets generated code | one schema for every environment and language; layout and hash computed once | the TS schema API and the TS generator are gone; `./spur ts` builds `oly-schema` first |
| Column of structs, strided view | does not change the `Table` or the query API | strided accessor in TS |
| `Engine` does not own the `World` | the ECS stays below and independent of the engine | two `Engine`s on the same `World` share the scope tag |
| State scope through an internal tag | cleanup by query, cheap `persist` | twin archetypes with and without the tag |
| Schedule as a min-heap in `app`, not as entities | tasks do not show up in the game's queries | task scope implemented separately |
| Input in its own module, without a platform interface | edge rules testable without `Engine`; the host only pushes events | — |
| `@oly/core` without DOM, `@oly/web-host` with DOM | the TS core runs in node, browser and worker | two packages |

The details of each decision are in the module files ([README.md](README.md#index)) and in the specs.
