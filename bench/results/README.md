# Resultados de benchmark

Máquina: container Docker (gcc:14) em WSL2/Windows 11, 12 CPUs a ~3,6 GHz, L1d 32 KiB, L2 512 KiB, L3 16 MiB.
Medições são ruidosas neste ambiente (os `BM_DirectAdds` já oscilaram de 60 a 120 ns/entidade entre execuções);
compare sempre razões medidas na mesma execução. Commit de referência dos números abaixo: `1b1e8ce`.

| Arquivo | Conteúdo | Meta |
|---|---|---|
| `m0_iterate_single_soa.json` | Linha de base SoA (task 2) | — |
| `m2_transitions.json` | Transições com arestas em cache e a frio (task 11) | NF-P4 |
| `m3_iterate.json` | Iteração ECS vs SoA (task 14) | NF-P1, NF-P2 |
| `m3_fragmented.json` | Iteração fragmentada (task 14) | NF-P3 |
| `m3_vectorization.txt` | Relatório de vetorização do laço de iteração | — |
| `m4_flush.json` | Flush coalescido vs adds diretos (task 18) | NF-P5 |
| `m5_perf_fragmented.txt` | `perf stat` de `BM_IterateFragmented/A` (task 20) | — |
| `e1_iterate.json`, `e1_fragmented.json`, `e1_flush.json` | Reexecução após `RawQuery`/`addRaw` (engine fase 1, task 2) | NF-P1, NF-P2, NF-P5 |
| `engine/boundary_fase1.json` | Fronteira WASM↔TS da engine; ver `engine/README.md` (engine fase 1, task 8) | — |

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
o tempo dobra e os cache-misses de último nível sobem ~13× em relação a A = 1. Dados brutos em `m5_perf_fragmented.txt`.

## Reexecução após `RawQuery`/`addRaw` (engine fase 1)

Razões das medianas nos `e1_*.json`: `BM_IterateSingle_ECS/1000000` / SoA = 0,82 (NF-P1 ≤ 1,10);
`BM_IterateFragmented/64` / SoA = 0,71 (NF-P2 ≤ 1,25); `BM_FlushCoalesced/1000` / `BM_DirectAdds/1000` = 0,44 (NF-P5 ≤ 0,5);
`BM_FlushCoalesced/100000` / `BM_DirectAdds/100000` = 0,95 (já documentado ~0,85; meta continua não atendida a N = 100000).

Ruído: execuções completas do mesmo binário deram ECS/SoA a 1M entre 0,82 e 1,31 (as execuções filtradas em 1M deram 0,86, 0,99 e 0,96),
e o flush a N = 1000 entre 0,44 e 0,59. Para separar ruído de regressão, o `bench_flush` do `master` foi medido em seguida, ao lado do
branch: 0,55 / 0,59 / 0,57 no `master` contra 0,51 / 0,44 / 0,59 no branch. O caminho do flush não mudou, então a faixa
~0,45–0,6 a N = 1000 é a precisão do ambiente, não efeito desta fase. Os arquivos registrados são execuções representativas, não as melhores
de um sorteio: os valores acima mostram a dispersão.

`BM_IterateSingle_RawQuery/N` / `BM_IterateSingle_ECS/N`: 1,005 (1000), 1,010 (10000), 0,902 (100000), 1,043 (1000000) — a query
type-erased custa o mesmo que a template no laço de blocos.
