# Benchmark da fronteira WASM↔TS — engine fase 1

> Os comandos abaixo usam os caminhos atuais; a fase 1 os rodou com `make engine-ts` e o preset `wasm-engine-release`.

Resultado bruto: `boundary_fase1.json` (commit `e8b564c`, 2026-11-09). Gerado por `engine/testing/bench/boundary.ts`:

```sh
WC="docker compose run --rm wasm"
$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'
$WC node engine/testing/dist/bench/boundary.js --module build/wasm-games-release/games/fixture_game.mjs \
  --out docs/benchmarks/bridge/boundary_fase1.json --commit $(git rev-parse --short HEAD)
```

## Ambiente

AMD Ryzen 5 5600GT (12 threads), WSL2/Docker, Emscripten 3.1.74 (Release, `ECS_ENABLE_ASSERTS=0`), node 20.18.0, V8 11.3.244.8-node.23.
Cache: L1d 32 KiB, L2 512 KiB, L3 16 MiB. Ambiente ruidoso: valem as razões e a dispersão, não os números absolutos.

## Protocolo

Mesma operação nas quatro variantes, `position += velocity * deltaTime` (`deltaTime = fround(1/60)`), sobre N entidades `Position {0, 0}` e
`Velocity {1, 2}` num único arquétipo, um `World` por variante:

1. `cpp` — `fixtureMove(world, deltaTime)` (uma chamada pela fronteira);
2. `ts-engine` — `eachBlock` + `get`/`set` (uso normal);
3. `ts-engine-raw` — `eachBlock` + `raw()` (separa o custo do acessor do custo do passo);
4. `ts-manual` — quatro `Float32Array` próprios, sem engine (linha de base).

Por N: 50 iterações de aquecimento por variante e 10 rodadas intercaladas (a ordem das variantes gira a cada rodada); cada medida =
`ceil(4e6 / N)` iterações cronometradas com `performance.now()`, em ns/entidade. Mediana por variante; razões sobre as medianas. Ao final,
`x` de cada variante é conferido contra a simulação f32 do acumulado (tolerância relativa 1e-3); divergência aborta com exit 1.

## Resultados (ns/entidade, mediana [mín–máx] de 10 rodadas)

| N | conjunto de trabalho (engine) | cabe em | cpp | ts-engine | ts-engine-raw | ts-manual |
|---:|---:|---|---:|---:|---:|---:|
| 1000 | 24 KB | L1d | 0,71 [0,68–1,04] | 8,42 [8,19–11,21] | 6,00 [5,61–6,20] | 0,94 [0,92–1,22] |
| 10000 | 240 KB | L2 | 0,62 [0,61–0,89] | 8,29 [7,75–8,81] | 4,60 [4,43–5,98] | 4,14 [4,09–4,29] |
| 100000 | 2,4 MB | L3 | 0,63 [0,62–0,68] | 9,89 [9,21–10,89] | 6,29 [5,74–7,39] | 5,28 [5,13–6,57] |
| 1000000 | 24 MB | não cabe (> L3) | 1,12 [0,97–1,48] | 10,70 [10,35–11,47] | 7,55 [7,07–8,47] | 5,22 [5,11–5,90] |

Conjunto de trabalho da engine = N × (8 + 8 + 8) B (`Position` lida e escrita + `Velocity` lida); o `ts-manual` toca 16 B/entidade.

| N | ts-engine / ts-manual | ts-engine / cpp | ts-engine-raw / ts-manual |
|---:|---:|---:|---:|
| 1000 | 8,93 | 11,90 | 6,37 |
| 10000 | 2,01 | 13,43 | 1,11 |
| 100000 | 1,87 | 15,75 | 1,19 |
| 1000000 | 2,05 | 9,59 | 1,45 |

## Leitura

- A fronteira em si é barata: `eachBlock` cruza uma vez por chamada, e o custo está no laço. O C++ em WASM roda a ~0,6–1,1 ns/entidade;
  o TS por view fica 10–16× acima disso. Esse é o custo de manter o laço do jogo em TS.
- O acessor `get`/`set` custa caro: `ts-engine-raw` é ~1,4–1,8× mais rápido que `ts-engine` (por exemplo 4,6 vs 8,3 ns a N = 10000). Com `raw()`
  o laço fica a 1,1–1,45× do `Float32Array` manual a N ≥ 10000, ou seja, o passo de coluna de structs (AoS dentro do componente) custa pouco quando
  o conjunto não cabe em L1; o que sobra de diferença a N = 1M é o passo 2 em vez de 1 e os 24 B contra 16 B por entidade.
- A N = 1000 as razões contra `ts-manual` (8,9 e 6,4) são infladas pelo próprio `ts-manual` (0,94 ns, laço totalmente em L1 e vetorizado pelo JIT),
  enquanto a N ≥ 10000 o `ts-manual` salta para ~4–5 ns, que não foi investigado (suspeita: comportamento do JIT/tamanho do laço, não da engine). Por isso
  a razão a N = 1000 não deve ser lida como custo do passo.
- Sobre a decisão SoA-por-campo (§4.2): o ganho de ter cada campo como typed array com passo é visível no `raw()` (próximo do manual) e some no
  acessor com chamada de método por elemento. Para laços quentes em TS, o caminho é `raw()` ou deixar o laço no C++; o `get`/`set` serve para
  código de jogo menos quente. Não há meta numérica nesta fase (§7).

## Após a modularização (`boundary_oly.json`)

Mesmo protocolo, executado com `OlyWorld`/`olyQueryBegin` e `@oly/core`; os rótulos `ts-engine` e `ts-engine-raw` ficam para comparar com a fase 1. Sem meta numérica.

| N | ts-engine / ts-manual (fase 1 → Oly) | ts-engine / cpp | ts-engine-raw / ts-manual |
|---:|---:|---:|---:|
| 1000 | 8,93 → 9,34 | 11,90 → 12,85 | 6,37 → 5,41 |
| 10000 | 2,01 → 2,17 | 13,43 → 13,79 | 1,11 → 1,22 |
| 100000 | 1,87 → 1,70 | 15,75 → 14,42 | 1,19 → 1,07 |
| 1000000 | 2,05 → 1,94 | 9,59 → 10,48 | 1,45 → 1,38 |

As razões ficam na mesma faixa da fase 1 (diferenças dentro da dispersão de ±15% do ambiente); a ponte nova não mudou o custo do laço.
