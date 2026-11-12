# `ecs`: o ECS por arquétipos

Código em `engine/runtime/ecs/`. Spec: `docs/superpowers/specs/2026-10-16-ecs-archetype-spec.md`.

## Camadas

```
component → entity → detail/table → detail/archetype → query / command_buffer → world
```

Cada camada usa só as de baixo. Headers públicos: `world`, `entity`, `component`, `query`, `query_by_id`, `command_buffer` e `diagnostics`. Tudo o mais fica em `detail/`: `Table`, `Archetype`, `EntityStore`, cache de query, plano de comandos e os acessores de teste.

| Camada | Responsabilidade |
|---|---|
| `component` | `componentId<T>()` dá IDs densos por processo. `TypeInfo` guarda tamanho, alinhamento, se o tipo é trivial (caminho `memcpy`) ou tag (tamanho 0, sem coluna) e ponteiros de move e destroy. |
| `entity` | `Entity` = índice de 32 bits + geração de 32 bits. `EntityStore` guarda os records e a lista de índices livres. Ela fica abaixo do `World` para o `CommandBuffer` poder reservar índices sem conhecer o `World`. |
| `detail/table` | Armazenamento SoA: um único `count` e `capacity` para todas as colunas, cada coluna alinhada a 64 B, crescimento ×2. Não conhece entidades vivas, records nem queries. `pushUninit` deixa uma linha morta; `fillHole(r)` faz swap-remove e devolve a entidade deslocada. |
| `detail/archetype` | Dono de uma `Table`. Guarda `signature` (com as tags) e `columnTypes` (só componentes com dados); confundir os dois é a fonte clássica de coluna errada. Arestas `±ComponentId` memoizadas trazem pronto o mapeamento de colunas `sourceToDestination`. Arquétipos nunca são removidos, então os ponteiros são estáveis. |
| `query` | `Query<Ts...>` cacheada pelo `World`, com matching incremental. Filtros `With<...>` e `Without<...>` fazem parte da chave do cache. `each`, `eachBlock`, `single`, `trySingle`. |
| `query_by_id` | `QueryById`: a mesma query, type-erased por listas de `ComponentId` (`read`, `write`, `with`, `without`). É o que a ponte e o TS usam. |
| `command_buffer` | Grava mudanças estruturais adiadas. `spawn()` reserva o índice na hora. O `flush` agrupa por entidade e faz uma única transição por entidade até o arquétipo final. |
| `world` | Único dono dos records e único que chama `fillHole`, então a correção do record da entidade deslocada vive num só lugar. |
| `diagnostics` | Funções livres: `checkInvariants`, `stats`, `signatureLookupCount`, `archetypeCount`. |

## Índices do `World`

- `records_` (via `EntityStore`): `Entity.index → (arquétipo, linha, geração)`.
- `bySignature_`: assinatura → arquétipo. Só é consultado na primeira transição para um arquétipo.
- `componentIndex_[componentId]`: arquétipos que têm o componente e a coluna dele. `has`, `get` e a criação de queries usam esse índice, sem varrer todos os arquétipos.

Os dois índices só mudam em `findOrCreate`, que também registra o arquétipo novo nas queries existentes.

## Custos das operações

| Operação | Custo |
|---|---|
| `get<T>(entity)` | 3 indireções: record, `componentIndex_`, coluna |
| `add`/`remove` com aresta em cache | O(k) (k = componentes da entidade), zero alocações e zero lookups de hash |
| primeira transição para um arquétipo | O(k) + hash da assinatura |
| `query<Ts...>()` | O(arquétipos com o componente mais raro) na primeira vez; O(1) depois |
| `flush` | O(Σk), uma transição por entidade afetada |

## Templates e `ById`

Os templates de `world.hpp` são finos e delegam para funções não template por `ComponentId` em `world.cpp` (`addById`, `removeById` etc.), o que reduz tempo de compilação e tamanho de binário. O caminho quente (`get<T>`, `Query::each`, `eachBlock`) **não** delega: ele fica inline, porque uma indireção ali afetaria as metas NF-P1 e NF-P2 ([desempenho.md](desempenho.md)).

## Iteração e mudanças estruturais

- `each` e `eachBlock` incrementam `iterating_` (por um `IterationGuard`). Mudança estrutural direta com `iterating_ > 0` é erro de contrato.
- Mudanças durante a iteração passam pelo `CommandBuffer`.
- `QueryById::iterate()` devolve uma `QueryIteration` RAII, só movível, com descritores de bloco de um pool por nível de aninhamento. Não há alocação por iteração depois do aquecimento.

Regras de validade:

- Qualquer mudança estrutural invalida ponteiros, referências e spans de **qualquer** entidade.
- Só a `Entity` é um handle estável. Para guardar uma relação, guarde a `Entity` e chame `tryGet` ao usar.

## `CommandBuffer`

- O buffer é um bloco próprio alinhado a 64 B. Ao crescer, ele realoca cada payload pelo `TypeInfo`, porque copiar bytes de um tipo não trivial (ex.: `std::string` com SSO) seria comportamento indefinido.
- Regras de conflito (R-CB-4): `add` seguido de `remove` do mesmo tipo se anulam; `add` repetido mantém o último valor; `destroy` descarta os comandos anteriores da entidade; comando depois de `destroy` é erro de contrato.
- Um `destroy` direto de uma entidade com comandos pendentes é resolvido pela geração: o flush vê o handle obsoleto e ignora os comandos.

## Corretude

- As invariantes I-1 a I-11, o fuzz contra modelo e o `LifeCounter` estão em [build-e-testes.md](build-e-testes.md#testes).
- Ao mudar a lógica estrutural, mantenha `checkInvariants` cobrindo o que mudou, inclusive as caches de `QueryById` (que entram em `checkQueries`).
- Os ganchos só de teste (`flushSequential`, `setGeneration`, `CommandBuffer::forEach`) ficam em `detail/test_access.hpp` (`WorldTestAccess`, `EntityStoreTestAccess`, `CommandBufferTestAccess`). As classes públicas não têm nada "para teste".
