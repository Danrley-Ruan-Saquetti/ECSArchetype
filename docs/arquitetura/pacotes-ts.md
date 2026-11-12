# Pacotes TypeScript

npm workspaces na raiz, um único `package-lock.json`, `tsconfig.base.json` (`strict`, ES2022, `NodeNext`) e project references. Cada pacote compila para o próprio `dist/` (testes em `dist-tests/`). Versões fixas: TypeScript 5.6.3, `@types/node` 20.17.6 e esbuild 0.24.2. Testes com `node:test`.

## `@oly/core`

A engine no lado TS (`engine/packages/core/`), sem DOM nem node (`types: []`): roda em node, no navegador e em worker. O que depende do ambiente entra por injeção (a fábrica do módulo WASM, o tempo, o input).

- **Esquema**: `defineSchema`, `component`, `tag`, `event`, `action`, `state`.
- **Runtime**: `loadRuntime(factory, schema)` → `Runtime` (`schema`, `createWorld()`).
- **Mundo**: `World`, `Query` (`eachBlock`, `single`, `trySingle`), `Block` (`read`, `write`), `Entity`.
- **Engine**: `Engine`, `SystemContext`, `HandlerContext`, `TaskContext`, `StateContext`, `EventBatch`, `Scheduler`, `ActionMap`.
- **Erros**: `ContractError`, `SchemaMismatchError`.
- **Interno** (`@oly/core/internal`, só para `engine/`): `Bridge`, `bridgeOf`, `moduleOf`, `componentIdOf`, `Heap`, layout e hash.

A lista pública exata é `engine/packages/core/api/public-exports.txt` ([modulos-e-fronteiras.md](modulos-e-fronteiras.md#como-as-fronteiras-são-garantidas)).

Regras de implementação:

- **Só a `Bridge` chama exports do WASM.** Toda chamada que pode alocar passa por ela, e ela faz `sync()`: se a memória cresceu, o `ArrayBuffer` antigo foi desanexado e as views do heap são recriadas.
- **Views sem cópia.** `Block.read/write` e `EventBatch.read` devolvem campos que leem o heap pelo endereço da coluna e o passo da struct. Elas valem só dentro do callback.
- **O TS valida antes de chamar o C++.** Uso inválido lança `ContractError` com operação, entidade e tipo. Com asserts desligados no Release, chegar ao `OLY_ASSERT` seria comportamento indefinido.
- **`Entity` é um `number`**: `generation × 2³² + index`, com geração até `0x1FFFFF` para caber em 53 bits.
- **`World` travado durante `frame`.** Mudança estrutural direta num sistema TS é `ContractError`; o sistema usa `context.spawn/destroy/add/remove`.
- Leituras pontuais (`get`, `single`) cruzam a fronteira um número constante de vezes por chamada e devolvem cópias.

## `@oly/client`

Camada fina do navegador (`engine/packages/client/`). Ela implementa com APIs do DOM as portas que o `core` recebe por injeção e não tem regra de engine:

- `moduleFactoryFromUrl(url)`: carrega o `.mjs` do Emscripten de um jogo.
- `runInBrowser(engine)`: laço por `requestAnimationFrame` e `visibilitychange` → `setHidden`.
- `attachBrowserInput(engine, canvas)`: teclado e `blur` na `window`, `pointerdown` no canvas, `pointerup`/`pointermove` na `window`, posição convertida para pixels do canvas. `preventDefault` só para botões ligados a alguma ação.

Loop e input ficam separados porque mudam por motivos diferentes.

## `@oly/codegen` e `@oly/testing`

- `@oly/codegen` (`engine/tools/codegen/`): o bin `oly-codegen --schema <schema.js> --out <dir>`, chamado pelo `oly_add_game` ([esquema-e-codegen.md](esquema-e-codegen.md)).
- `@oly/testing` (`engine/testing/`, privado): o jogo `fixture` (`engine/testing/fixture/`, único lugar da engine com nomes de jogo), os testes de integração contra o WASM real (`tests/wasm/`) e os benchmarks da fronteira, do loop e dos eventos (`bench/`). Ele fica fora do `@oly/core` para não criar ciclo nas project references nem pôr nomes de jogo no pacote.
