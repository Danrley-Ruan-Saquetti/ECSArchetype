# Desempenho

As metas são **razões** contra uma linha de base medida na mesma execução, nunca números absolutos, porque o ambiente (WSL2 + Docker) é ruidoso.

## Metas do ECS

| Meta | Requisito | Situação |
|---|---|---|
| NF-P1 | iterar 1M entidades num arquétipo ≤ 1,10× o SoA manual | atendida (~0,82) |
| NF-P2 | o mesmo em 64 arquétipos ≤ 1,25× | atendida (~1,08) |
| NF-P3 | laço interno de `eachBlock` vetorizado | atendida |
| NF-P4 | `add`/`remove` com aresta em cache: zero alocações e zero hash | atendida |
| NF-P5 | flush de N adds coalescidos ≥ 2× mais rápido que adds diretos | no limite do ruído a N = 1000; **não atendida** a N = 100000 (o buffer de comandos é lido a frio) |

## Custos da fronteira

Sem meta numérica:

- O laço em C++ dentro do WASM roda a ~0,6–1,3 ns/entidade. O mesmo laço em TS pelas views fica ~10–15× acima.
- O acessor `get`/`set` por elemento é o custo principal no TS; o acesso `raw()` fica a ~1,1–1,5× de um `Float32Array` manual a N ≥ 10⁴.
- Uma chamada de volta C++ → JS custa ~40–50 ns por sistema TS. Intercalar sistemas C++ e TS na mesma fase não custa mais que o número de chamadas explica.

Consequência para quem escreve jogos: laços quentes por entidade ficam em C++; o TS fica com o que roda por sistema, por lote ou poucas vezes por passo.

## Onde ficam os resultados

| Pasta | Conteúdo |
|---|---|
| `docs/benchmarks/README.md` + `ecs/` | metas do ECS, `perf stat` da iteração fragmentada |
| `docs/benchmarks/bridge/` | fronteira, loop e eventos |
| `docs/benchmarks/wasm/` | execuções (`runs/`) e código gerado (`codegen/`) da escada nativo × WASM |

Os benchmarks do ECS ficam em `engine/runtime/ecs/bench/` (`ecs_bench_iterate`, `ecs_bench_transitions`, `ecs_bench_fragmented`, `ecs_bench_flush`). Os da fronteira ficam em `engine/testing/bench/`. `tools/bench_ratio.py` calcula as razões.
