# Oly

Oly é uma engine de jogos em C++20 e TypeScript. O núcleo é um ECS (*Entity Component System*) por arquétipos, single-thread, escrito em C++ e compilado para WebAssembly. O jogo usa esse núcleo pelo TypeScript, no navegador ou no Node.

É um projeto de estudo. O objetivo é medir como a representação dos dados (SoA, arquétipos, colunas alinhadas) e as decisões de arquitetura afetam o custo de execução. Por isso cada meta de desempenho tem um benchmark e um resultado registrado.

> **Estado:** em desenvolvimento. O ECS, a ponte C/WASM e os pacotes TypeScript base já funcionam e têm testes. Ainda não há renderização, entrada, áudio nem um jogo de exemplo. Os pacotes ainda não estão publicados no npm. A API pode mudar.

## O que já existe

| Parte | Onde | O que faz |
|---|---|---|
| `oly::core` | `engine/runtime/core` | Utilitários base: `OLY_ASSERT`, alinhamento, `small_map`, nome de tipo. |
| `oly::ecs` | `engine/runtime/ecs` | O ECS: `World`, `Entity`, queries cacheadas, `CommandBuffer`, diagnóstico de invariantes. |
| `oly::bridge` | `engine/runtime/bridge` | API C (`oly*`) que expõe o ECS ao WebAssembly. |
| `@oly/core` | `engine/packages/core` | API TypeScript: esquema, `World`, `Query`, `Block`. |
| `@oly/client` | `engine/packages/client` | Carrega o módulo WASM por URL no navegador. |
| `@oly/codegen` | `engine/tools/codegen` | Gera os structs C++ a partir do esquema TypeScript. |

## Como o ECS funciona

- Cada combinação de componentes forma um **arquétipo**. Cada arquétipo guarda as entidades numa tabela SoA: uma coluna contígua por componente, alinhada a 64 bytes.
- Adicionar ou remover um componente move a entidade para outro arquétipo. As transições ficam em cache como arestas do grafo de arquétipos.
- Uma **query** é cacheada pelo `World` e só visita os arquétipos que combinam com ela. Arquétipos novos entram nas queries existentes de forma incremental.
- Mudanças estruturais durante uma iteração são erro de contrato. Para isso existe o **`CommandBuffer`**: ele grava as mudanças e o `flush` aplica uma única transição por entidade.
- Só o `Entity` (índice + geração) é um handle estável. Ponteiros e views valem até a próxima mudança estrutural.

## Exemplos

### C++

```cpp
#include "oly/ecs/world.hpp"

struct Position { float x, y; };
struct Velocity { float x, y; };
struct Frozen {};  // tag: componente sem dados

oly::ecs::World world;
oly::ecs::Entity player = world.spawn(Position{0, 0}, Velocity{1, 2});

// Itera só entidades com Position e Velocity e sem a tag Frozen.
world.query<Position, const Velocity>(oly::ecs::Without<Frozen>{})
    .each([](Position& position, const Velocity& velocity) {
      position.x += velocity.x;
      position.y += velocity.y;
    });

// Mudanças estruturais durante a iteração passam pelo CommandBuffer.
oly::ecs::CommandBuffer commands = world.commands();
world.query<const Position>().each([&](oly::ecs::Entity entity, const Position& position) {
  if (position.x > 100) {
    commands.destroy(entity);
  }
});
world.flush(commands);
```

### TypeScript

O jogo declara o esquema em TypeScript. O `@oly/codegen` gera o C++ equivalente, que entra no módulo WASM do jogo.

```ts
import { component, defineSchema, loadRuntime, tag } from "@oly/core";
import { moduleFactoryFromUrl } from "@oly/client";

const Position = component("Position", { x: "f32", y: "f32" });
const Velocity = component("Velocity", { x: "f32", y: "f32" });
const Enemy = tag("Enemy");
const schema = defineSchema([Position, Velocity, Enemy]);

// Lança SchemaMismatchError se o WASM foi gerado com outro esquema.
const runtime = await loadRuntime(moduleFactoryFromUrl("/game.mjs"), schema);
const world = runtime.createWorld();

const enemy = world.spawn([Position, Velocity, Enemy], {
  Position: { x: 0, y: 0 },
  Velocity: { x: 1, y: 2 },
});

world.query({ read: [Velocity], write: [Position] }).eachBlock((block) => {
  const velocity = block.read(Velocity);
  const position = block.write(Position);
  for (let row = 0; row < block.count; ++row) {
    position.x.set(row, position.x.get(row) + velocity.x.get(row));
    position.y.set(row, position.y.get(row) + velocity.y.get(row));
  }
});

console.log(world.get(enemy, Position)); // { x: 1, y: 2 }
world.dispose();
```

As views de `block.read` e `block.write` acessam a memória do WASM diretamente, sem cópia, e só valem dentro do callback. O TypeScript valida o uso antes de chamar o C++ e lança `ContractError` quando o uso é inválido.

Um exemplo completo, com sistemas em C++ chamados a partir do TypeScript, está em [`engine/testing/fixture/`](engine/testing/fixture/) e nos testes de [`engine/testing/tests/wasm/`](engine/testing/tests/wasm/).

## Compilar e testar

O único requisito no host é o **Docker** com Docker Compose. As imagens trazem o GCC 14, o CMake, o `perf`, o Emscripten e o Node.

```sh
# C++ nativo: compila e roda todos os testes com ASan + UBSan.
docker compose run --rm dev make test

# Só um módulo ou uma suíte (T é uma regex do ctest).
docker compose run --rm dev make test T='^ecs\.'

# ECS em WebAssembly + pacotes TypeScript + testes de integração.
docker compose run --rm wasm make games-test
```

O build fica em `build/<preset>/`. Os presets estão em [`CMakePresets.json`](CMakePresets.json). Os principais são `debug-asan` (padrão), `release` e `wasm-games`.

## Benchmarks

As metas de desempenho são razões contra uma linha de base medida na mesma execução, por exemplo: "iterar no ECS custa no máximo 1,10× o custo de um laço SoA escrito à mão".

```sh
docker compose run --rm dev make build PRESET=release
docker compose run --rm dev ./build/release/bench/ecs_bench_iterate
```

Os resultados registrados e a situação de cada meta estão em [`docs/benchmarks/README.md`](docs/benchmarks/README.md).

## Estrutura do repositório

```
engine/runtime/   módulos C++ (core, ecs, bridge)
engine/packages/  pacotes TypeScript (@oly/core, @oly/client)
engine/tools/     @oly/codegen
engine/testing/   fixture, testes de integração WASM e benchmark da fronteira
cmake/            funções de build (oly_add_module, oly_add_game, ...)
tools/            scripts de benchmark e de lint
docs/benchmarks/  resultados de benchmark
```
