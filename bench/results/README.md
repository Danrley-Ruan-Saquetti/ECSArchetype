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
