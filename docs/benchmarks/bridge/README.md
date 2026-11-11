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

## Custo do loop — fase 2

Resultado bruto: `loop_fase2.json` (commit `fca8946`, 2026-11-10). Gerado por `engine/testing/bench/loop.ts`:

```sh
WC="docker compose run --rm wasm"
$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'
$WC node engine/testing/dist/bench/loop.js --module build/wasm-games-release/games/fixture_game.mjs \
  --out docs/benchmarks/bridge/loop_fase2.json --commit $(git rev-parse --short HEAD)
```

Ambiente: o mesmo das seções acima (Docker no WSL2, node 20.18, V8 11.3, Release com asserts desligados). Protocolo do `boundary.ts`:
aquecimento, 10 rodadas com a ordem das variantes girando, mediana. Um passo fixo por `frame` (`frame(k / 60)`), sem sistema de `Render`.
Sem meta numérica (§7).

### Sistemas vazios (ns por passo)

| K | zero | cpp (K × `noop`) | ts (K × sistema TS vazio) | ts / cpp | ns por chamada de volta |
|---:|---:|---:|---:|---:|---:|
| 1 | 90 | 94 | 135 | 1,43 | 40,3 |
| 4 | 108 | 119 | 315 | 2,66 | 49,1 |
| 16 | 100 | 181 | 985 | 5,44 | 50,2 |
| 64 | 86 | 407 | 2846 | 6,99 | 38,1 |

### Intercalar sistemas C++ e TS (K/2 de cada, ns por passo)

| K | alternado | agrupado | alternado / agrupado |
|---:|---:|---:|---:|
| 4 | 191 | 189 | 1,01 |
| 16 | 473 | 490 | 0,97 |
| 64 | 1797 | 1792 | 1,00 |

### Fronteira dentro do loop (ns por entidade)

| N | cpp-loop | ts-loop | ts-manual | ts-loop / ts-manual | ts-loop / cpp-loop |
|---:|---:|---:|---:|---:|---:|
| 1000 | 1,18 | 20,43 | 1,07 | 19,03 | 17,35 |
| 10000 | 0,80 | 11,20 | 5,69 | 1,97 | 14,05 |
| 100000 | 0,67 | 8,77 | 4,44 | 1,97 | 13,11 |
| 1000000 | 1,32 | 9,77 | 5,61 | 1,74 | 7,39 |

### Leitura

- Uma chamada de volta para um sistema TS vazio custa ~40–50 ns. O `frame` sem sistema custa ~90–110 ns; o laço da engine em si é barato.
- Intercalar não custa mais que o número de chamadas explica: `alternado` e `agrupado` empatam (razão ~1,0). Não vale agrupar sistemas TS por causa do custo da fronteira; a ordem das fases e do registro pode seguir a lógica do jogo.
- Um sistema TS com `eachBlock` e views, dentro do loop, fica na mesma faixa do `ts-engine` do `boundary.ts` a N ≥ 10000 (8–11 ns/entidade contra ~8 ns). A ~2× do `ts-manual` e 7–14× do `move` em C++, o que repete a conclusão da fase 1: laço quente fica no C++.
- A N = 1000 o `ts-loop` (20 ns/entidade) fica ~2× acima do `ts-engine` do `boundary.ts` (~10 ns). Não foi investigado; suspeitas: `context.query(...)` a cada passo e o aquecimento do JIT com 200 passos. Não leia a razão a N = 1000 como custo do passo (o `ts-manual` também é anômalo ali, como na fase 1).

## Custo dos eventos — fase 3

Resultado bruto: `events_fase3.json` (commit `3d331a8`, 2026-11-11). Gerado por `engine/testing/bench/events.ts`:

```sh
WC="docker compose run --rm wasm"
$WC sh -c 'make ts && cmake --preset wasm-games-release && cmake --build --preset wasm-games-release -j'
$WC node engine/testing/dist/bench/events.js --module build/wasm-games-release/games/fixture_game.mjs   --out docs/benchmarks/bridge/events_fase3.json --commit $(git rev-parse --short HEAD)
```

Ambiente: o mesmo das seções acima (Docker no WSL2, node 20.18, V8 11.3, Release com asserts desligados). Protocolo do `loop.ts`:
200 passos de aquecimento, 10 rodadas com a ordem das variantes girando, mediana, um passo fixo por `frame`. Cada amostra roda
~1 000 000 eventos (`ceil(1e6 / N)` passos). N eventos `Ping` por passo. O benchmark confere a soma de `value` lida pelos
handlers (N × passos) e sai com código 1 se divergir. Sem meta numérica (§7).

### Handler por lote (ns por evento)

Emissão sempre pelo sistema nativo `emitPings`. `none`: sem handler; `cpp`: `addNativeHandler(Ping, "sumPing")`; `ts`: handler TS
que soma `value` por `batch.read()`. A última coluna é `(ts − cpp) × N`: o custo extra do handler TS por lote, em ns por passo.

| N | none | cpp | ts | ts / cpp | (ts − cpp) ns/lote |
|---:|---:|---:|---:|---:|---:|
| 16 | 21,74 | 22,96 | 35,52 | 1,55 | 201 |
| 256 | 15,00 | 16,20 | 19,76 | 1,22 | 911 |
| 4096 | 14,52 | 14,50 | 18,98 | 1,31 | 18333 |

### Emissão (ns por evento)

Os dois com `sumPing`. `cpp`: `emitPings` nativo; `ts`: sistema TS com N × `context.emit(Ping, { value: 1 })`.

| N | cpp | ts | ts / cpp |
|---:|---:|---:|---:|
| 16 | 21,34 | 169,70 | 7,95 |
| 256 | 14,23 | 170,49 | 11,98 |
| 4096 | 14,20 | 172,41 | 12,14 |

### Leitura

- A chamada de volta de um handler TS custa ~150–200 ns por lote (N = 16: 201 ns, dos quais ~16 × 4 ns são leitura). É ~4× a
  chamada de um sistema TS vazio da fase 2 (~40–50 ns): além da travessia, cada lote cria o `EventBatch`, faz `sync()` e
  destrava e trava o `World`. Paga-se uma vez por handler por rodada, não por evento.
- Ler um evento pelas views custa ~4 ns por campo lido (N = 4096: (18,98 − 14,50) ≈ 4,5 ns/evento). O handler TS fica a
  1,2–1,6× do C++ no total do passo, porque a emissão e o despacho no C++ dominam.
- Emitir e despachar no C++ custa ~14 ns/evento sem handler. Não foi investigado (suspeitas: o `resize` da fila zera os bytes antes do
  `memcpy`, e `emit` passa por `emitById` em outro `.cpp`, sem inline); fica para quando um jogo mostrar o custo.
- Emitir do TS custa ~170 ns/evento, ~155 ns a mais que no C++ (uma travessia por evento, mais a validação e a escrita do
  valor na área de rascunho). Com dezenas de eventos por passo isso é poucos µs; não vale emissão em lote a partir do TS
  agora. Se um jogo emitir centenas por passo do TS, emitir do C++ ou criar a emissão em lote.
