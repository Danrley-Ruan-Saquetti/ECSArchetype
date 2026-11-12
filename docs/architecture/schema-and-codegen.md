# Esquema, codegen e o módulo WASM de cada jogo

## O esquema

O jogo declara tudo num `schema.ts`:

```ts
component("Position", { x: "f32", y: "f32" })
tag("Enemy")
event("EnemyDied", { entity: "entity", x: "f32", y: "f32" })
action("move", "axis2d")
state("playing")
```

- Tipos escalares: `f32 f64 i8 u8 i16 u16 i32 u32 entity`. Não há inteiros de 64 bits (exigiriam `BigInt`).
- Campos na ordem de declaração, alinhamento natural. Um componente vira uma struct trivial; a coluna guarda structs (SoA entre componentes, AoS dentro do componente).
- O layout é calculado pelo próprio `@oly/core` (`computeLayout`). O gerador usa a mesma função. Não existe arquivo TS gerado que possa divergir.

## O fluxo de build

```
schema.ts ──tsc──► schema.js ──oly-codegen──► header C++ (structs, ids, static_assert, hash)
                                                   │
sistemas C++ do jogo (OLY_SYSTEM, OLY_HANDLER) ────┤
oly::bridge + app + input + ecs + core ────────────┴──► emcc ──► <jogo>_game.mjs + .wasm
```

- `oly_add_game` (`cmake/OlyGame.cmake`) junta tudo. O WASM é compilado **por jogo**.
- O código gerado tem `static_assert` de `sizeof`, `alignof` e `offsetof`, e namespaces `actions::` e `states::` com os ids. Um nome errado num sistema nativo é erro de compilação.
- **Hash do esquema**: FNV-1a 32 bits de uma forma canônica (componentes, tags, eventos, ações e estados). `loadRuntime` confere o hash e os tamanhos e lança `SchemaMismatchError` se o WASM foi gerado com outro esquema.
- Link: `-sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=node,web`, memória que cresce. O resultado sai em `build/<preset>/games/<jogo>_game.{mjs,wasm}`.

Veja também: [bridge.md](bridge.md) (como a `OlyEngine` declara o esquema na criação) e [ts-packages.md](ts-packages.md#olycodegen-e-olytesting) (o pacote do gerador).
