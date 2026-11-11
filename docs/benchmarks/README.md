# Resultados de benchmark

Pastas: `ecs/` (módulo ECS), `bridge/` (fronteira WASM↔TS), `wasm/` (escada de variantes). Os executáveis `bench_iterate`, `bench_transitions`, `bench_fragmented` e `bench_flush` hoje se chamam `ecs_bench_*`.

Máquina: container Docker (gcc:14) em WSL2/Windows 11, 12 CPUs a ~3,6 GHz, L1d 32 KiB, L2 512 KiB, L3 16 MiB.
Medições são ruidosas neste ambiente (os `BM_DirectAdds` já oscilaram de 60 a 120 ns/entidade entre execuções);
compare sempre razões medidas na mesma execução. Commit de referência dos números abaixo: `1b1e8ce`.

| Arquivo | Conteúdo | Meta |
|---|---|---|
| `ecs/m0_iterate_single_soa.json` | Linha de base SoA (task 2) | — |
| `ecs/m2_transitions.json` | Transições com arestas em cache e a frio (task 11) | NF-P4 |
| `ecs/m3_iterate.json` | Iteração ECS vs SoA (task 14) | NF-P1, NF-P2 |
| `ecs/m3_fragmented.json` | Iteração fragmentada (task 14) | NF-P3 |
| `ecs/m3_vectorization.txt` | Relatório de vetorização do laço de iteração | — |
| `ecs/m4_flush.json` | Flush coalescido vs adds diretos (task 18) | NF-P5 |
| `ecs/m5_perf_fragmented.txt` | `perf stat` de `BM_IterateFragmented/A` (task 20) | — |
| `ecs/e1_iterate.json`, `ecs/e1_fragmented.json`, `ecs/e1_flush.json` | Reexecução após `RawQuery`/`addRaw` (engine fase 1, task 2) | NF-P1, NF-P2, NF-P5 |
| `bridge/boundary_fase1.json` | Fronteira WASM↔TS da engine; ver `bridge/README.md` (engine fase 1, task 8) | — |
| `ecs/r1_iterate.json`, `ecs/r1_fragmented.json`, `ecs/r1_flush.json` | Reexecução após a modularização em módulos Oly (task 10) | NF-P1, NF-P2, NF-P5 |
| `bridge/boundary_oly.json` | Fronteira WASM↔TS após a modularização; comparação em `bridge/README.md` | — |
| `bridge/loop_fase2.json` | Custo do loop da fase 2: sistemas vazios, intercalação e fronteira dentro do loop; ver `bridge/README.md` | — |
| `bridge/events_fase3.json` | Custo dos eventos da fase 3: handler por lote (C++ e TS) e emissão do C++ e do TS; ver `bridge/README.md` | — |

## NF-P5 — situação

`BM_FlushCoalesced/1000` / `BM_DirectAdds/1000` ≈ 0,50 (no limite); `BM_FlushCoalesced/100000` / `BM_DirectAdds/100000` ≈ 0,85:
**a meta de 2× não é atendida a N = 100000 neste ambiente.** O flush custa ~29 ns/entidade a N = 1000 e ~50 ns a
N = 100000; a diferença é o buffer de comandos (96 B por entidade, 9,6 MB) lido a frio.

## `iterate_fragmented` sob `perf stat` (1M entidades, `relwithdebinfo`)

Os eventos são divididos por iterações × 1M e incluem a montagem do mundo, então os valores por entidade são limites superiores.

| A (arquétipos) | ns/entidade | cache-misses/entidade | L1d-misses/entidade |
|---:|---:|---:|---:|
| 1 | 1,28 | 0,021 | 0,59 |
| 8 | 1,35 | 0,024 | 0,57 |
| 64 | 1,15 | 0,025 | 0,59 |
| 512 | 1,32 | 0,071 | 0,70 |
| 4096 | 2,54 | 0,275 | 0,80 |

Até 512 arquétipos (2048 entidades por bloco) o custo por entidade é praticamente plano; em 4096 (244 entidades por bloco)
o tempo dobra e os cache-misses de último nível sobem ~13× em relação a A = 1. Dados brutos em `ecs/m5_perf_fragmented.txt`.

## Reexecução após `RawQuery`/`addRaw` (engine fase 1)

Razões das medianas nos `ecs/e1_*.json`: `BM_IterateSingle_ECS/1000000` / SoA = 0,82 (NF-P1 ≤ 1,10);
`BM_IterateFragmented/64` / SoA = 0,71 (NF-P2 ≤ 1,25); `BM_FlushCoalesced/1000` / `BM_DirectAdds/1000` = 0,44 (NF-P5 ≤ 0,5);
`BM_FlushCoalesced/100000` / `BM_DirectAdds/100000` = 0,95 (já documentado ~0,85; meta continua não atendida a N = 100000).

Ruído: execuções completas do mesmo binário deram ECS/SoA a 1M entre 0,82 e 1,31 (as execuções filtradas em 1M deram 0,86, 0,99 e 0,96),
e o flush a N = 1000 entre 0,44 e 0,59. Para separar ruído de regressão, o `bench_flush` (hoje `ecs_bench_flush`) do `master` foi medido em seguida, ao lado do
branch: 0,55 / 0,59 / 0,57 no `master` contra 0,51 / 0,44 / 0,59 no branch. O caminho do flush não mudou, então a faixa
~0,45–0,6 a N = 1000 é a precisão do ambiente, não efeito desta fase. Os arquivos registrados são execuções representativas, não as melhores
de um sorteio: os valores acima mostram a dispersão.

`BM_IterateSingle_RawQuery/N` / `BM_IterateSingle_ECS/N`: 1,005 (1000), 1,010 (10000), 0,902 (100000), 1,043 (1000000) — a query
type-erased custa o mesmo que a template no laço de blocos.

## Reexecução após a modularização (Oly, r1)

Razões das medianas nos `ecs/r1_*.json` (`ecs_bench_*`, preset `release`): `BM_IterateSingle_ECS/1000000` / SoA = 0,82 (NF-P1 ≤ 1,10);
`BM_IterateFragmented/64` / SoA = 1,08 (NF-P2 ≤ 1,25); `BM_FlushCoalesced/1000` / `BM_DirectAdds/1000` = 0,53 (NF-P5 ≤ 0,5);
`BM_FlushCoalesced/100000` / `BM_DirectAdds/100000` = 0,90 (a meta continua não atendida a N = 100000).

NF-P5 a N = 1000 saiu de 0,5 em três execuções seguidas (0,52, 0,57, 0,53). Para separar ruído de regressão, o `bench_flush` do commit
anterior à refatoração (`1ad7e0e`) foi medido intercalado com o `ecs_bench_flush` atual, no mesmo ambiente: 0,53 / 0,45 / 0,53 antes contra
0,50 / 0,53 / 0,51 depois. As faixas se sobrepõem, então é o ruído do ambiente (já visto em 0,44–0,59), não efeito da modularização.
`BM_IterateSingle_QueryById/1000000` / `BM_IterateSingle_ECS/1000000` = 1,15 nesta execução (1,04 na fase 1); essa razão não tem meta.
