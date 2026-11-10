---
name: bench-compare
description: Mede os benchmarks do ECS em Release, confere as metas de desempenho NF-P1, NF-P2 e NF-P5 como razões contra a linha de base medida na mesma execução e registra o resultado em docs/benchmarks/. Use quando o usuário pedir para medir, rodar os benchmarks, conferir as metas NF-P ou verificar se uma mudança trouxe regressão de desempenho.
argument-hint: "<prefixo> [iterate] [fragmented] [flush]"
disable-model-invocation: true
---

# bench-compare

Uma rodada completa: build Release → executar os benchmarks com repetições → calcular as razões de cada meta → comparar com a situação registrada → atualizar `docs/benchmarks/README.md`. Cada etapa tem um jeito fácil de errar sem perceber, e o erro só aparece como um número que parece plausível. Esta skill existe para que a rodada saia comparável com as anteriores.

## Fronteira

**Território** — define como uma medição vira veredito de meta: qual benchmark é dividido por qual linha de base, com qual limite, a partir de qual estatística, e como o resultado é registrado.

**Fora da fronteira** — os valores das metas (vêm da seção 7 da spec); o que fazer quando uma meta falha (investigar com profiling, mudar código); as metas que não são razões (NF-P3 vetorização, NF-P4 zero alocações); comparações entre variantes de compilação ou entre nativo e WASM; criar o commit, que é ato do usuário.

**O que não preciso saber** — o que cada benchmark mede por dentro e como o ECS é implementado. A skill compara medianas; entender por que uma razão mudou é outro trabalho, que começa depois do veredito.

**Contrato de borda** — recebo um prefixo para os arquivos de resultado (ex.: `r2`) e, opcionalmente, quais executáveis rodar; entrego os JSON em `docs/benchmarks/ecs/<prefixo>_<executável>.json`, uma tabela de razões com veredito por meta e o `README.md` de resultados atualizado, sem commit.

## A regra das razões

O ambiente (Docker sobre WSL2) é ruidoso: o mesmo benchmark já oscilou 2× entre execuções. Por isso toda meta é uma **razão entre dois benchmarks do mesmo JSON**, medidos na mesma execução, que sofrem o mesmo ruído e o cancelam.

- Veredito de meta: só pela razão de `tools/bench_ratio.py`, nunca por ns/entidade.
- Comparar com uma rodada anterior: compare **razão com razão** (ex.: a razão de NF-P5 a N = 100000 era ~0,85 e agora é X). Nunca compare ns de uma rodada com ns de outra; esses números não são comparáveis entre execuções.
- Números absolutos podem aparecer no README como contexto (custo por entidade, por exemplo), sempre identificados como da rodada em questão, nunca como critério.

## Tabela de metas

Os limites vêm da seção 7 de `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md`. Se a spec e esta tabela divergirem, a spec vence: avise o usuário e use o valor da spec.

| Meta | Executável | `--name` | `--baseline` | `--max` |
|---|---|---|---|---|
| NF-P1 | `ecs_bench_iterate` | `BM_IterateSingle_ECS/1000000` | `BM_IterateSingle_SoA/1000000` | `1.10` |
| NF-P2 | `ecs_bench_fragmented` | `BM_IterateFragmented/64` | `BM_IterateSingle_SoA/1000000` | `1.25` |
| NF-P5 (N = 1000) | `ecs_bench_flush` | `BM_FlushCoalesced/1000` | `BM_DirectAdds/1000` | `0.5` |
| NF-P5 (N = 100000) | `ecs_bench_flush` | `BM_FlushCoalesced/100000` | `BM_DirectAdds/100000` | `0.5` |

Pontos que costumam sair errados:

- **NF-P2 usa a linha de base do próprio `ecs_bench_fragmented`**, que tem a sua `BM_IterateSingle_SoA/1000000`. Dividir pela SoA do JSON de `ecs_bench_iterate` mistura duas execuções e anula o propósito da razão.
- **NF-P5 é "pelo menos 2× mais rápido"**, então o limite é `--max 0.5` (flush / adds diretos ≤ 0,5), não `2.0`.
- **NF-P5 a N = 100000 não é atendida** neste ambiente (~0,85, registrado no README). Uma falha ali é a situação conhecida; o que importa é se a razão piorou em relação à registrada.

## Fluxo

Todos os comandos rodam no container: `DC="docker compose run --rm dev"`.

### 1. Preparar

- Defina o prefixo a partir de `$ARGUMENTS`. Sem prefixo, pergunte: os arquivos de `docs/benchmarks/ecs/` são o histórico do projeto (`m0_…`, `m3_…`, `e1_…`, `r1_…`), e um nome genérico sobrescreveria um registro.
- Se `docs/benchmarks/ecs/<prefixo>_*.json` já existir, pare e pergunte antes de sobrescrever.
- Anote o commit (`git rev-parse --short HEAD`) e se a árvore está suja. O README registra o commit de referência; números de uma árvore suja precisam dizer isso.
- Sem argumento de executáveis, rode os três (`iterate`, `fragmented`, `flush`).

### 2. Build Release

```sh
$DC make build PRESET=release
```

Tem que ser o preset `release`: o padrão `debug-asan` liga ASan, UBSan e asserts, e qualquer razão medida nele é sem sentido. Se o build falhar, pare: não há o que medir.

### 3. Executar

Para cada executável escolhido:

```sh
$DC ./build/release/bench/ecs_bench_<executável> --benchmark_repetitions=10 --benchmark_report_aggregates_only=true --benchmark_out=docs/benchmarks/ecs/<prefixo>_<executável>.json
```

- As três flags são necessárias juntas: `bench_ratio.py` lê o agregado `<nome>_median`, que só existe com repetições. Sem `--benchmark_repetitions` ele sai com código 2 (nome não encontrado).
- `--benchmark_out` grava o JSON; a saída no console não serve de registro.
- Uma rodada leva minutos (`ecs_bench_fragmented` monta 1M entidades por caso). Use timeout longo ou execução em segundo plano, e rode um executável por vez: dois benchmarks em paralelo disputam cache e CPU e contaminam ambos.

### 4. Calcular as razões

Para cada linha da tabela de metas cujo executável foi rodado:

```sh
$DC python3 -I tools/bench_ratio.py --json docs/benchmarks/ecs/<prefixo>_<executável>.json --name <name> --baseline <baseline> --max <max>
```

Leia o código de saída, não só o texto:

- `0` — atende (`ratio <= max`).
- `1` — não atende.
- `2` — nome não encontrado. É erro da rodada (flags faltando, nome de benchmark mudou), não veredito. Confira os nomes com `--benchmark_list_tests` e corrija antes de seguir.

### 5. Confirmar uma falha

Antes de declarar que uma meta deixou de ser atendida (ou que uma razão piorou em relação ao registrado), repita a execução daquele executável inteiro e recalcule. O ruído do ambiente basta para empurrar uma razão no limite para o outro lado. Só uma falha que se repete é regressão; relate as duas razões.

Nunca afrouxe o `--max` para a meta passar.

### 6. Registrar em `docs/benchmarks/README.md`

- Uma linha por arquivo novo na tabela de arquivos, com o conteúdo e as metas que ele cobre.
- As razões obtidas por meta, com o commit de referência e a razão anteriormente registrada, quando houver.
- Se a situação de uma meta mudou (passou a atender, deixou de atender), atualize a seção de situação dela, como a de NF-P5.

Edite o que já existe em vez de duplicar seções. Não faça commit: proponha a mensagem (`bench: ...`, em pt-BR) e deixe o usuário decidir.

## Relatório final

Termine com uma tabela neste formato, seguida de uma ou duas frases sobre o que mudou em relação à rodada registrada:

| Meta | Razão | Limite | Anterior | Veredito |
|---|---|---|---|---|
| NF-P1 | 1,04 | ≤ 1,10 | 1,02 | atende |
| NF-P5 (N = 100000) | 0,87 | ≤ 0,50 | ~0,85 | não atende (situação conhecida) |

Inclua o commit medido, se a árvore estava suja e quais execuções foram repetidas.
