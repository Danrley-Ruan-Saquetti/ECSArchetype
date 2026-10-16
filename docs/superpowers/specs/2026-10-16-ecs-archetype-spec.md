# Especificação — ECS por Arquétipos em C++

Oct 16, 2026 · @Danrley

## 1. Objetivo, premissas e escopo

A v1 entrega um ECS por arquétipos single-thread, em C++20. Sua iteração em lote deve ficar próxima de um SoA escrito à mão, e suas mudanças estruturais devem ter custo medido e previsível. O projeto serve a estudo e benchmark, não a produção, e a arquitetura deve permitir chunks, paralelismo e detecção de mudança sem alterar a API dos sistemas.

### Premissas

- Padrão C++20 (`std::span`, concepts, `std::bit_cast`). Compiladores-alvo: GCC 13+ e Clang 17+; MSVC é desejável, mas não bloqueia.
- Um único `World` por thread e nenhum acesso concorrente na v1.
- Componentes são tipos com move-constructor `noexcept`. A verificação é feita em tempo de compilação.
- Os IDs de componente valem só dentro do processo. Não há suporte a plugins nem a DLLs.
- Exceções não são usadas no caminho quente. Violações de contrato disparam `assert` em debug e têm comportamento indefinido em release.

### Entra na v1

| Item | Módulo |
| --- | --- |
| IDs de tipo em runtime, com `TypeInfo` e caminho `memcpy` para tipos triviais | `type_info` |
| Handles geracionais e reciclagem de índices | `entity` |
| Tabela SoA com colunas que crescem juntas | `table` |
| Arquétipos, com assinatura e grafo de transições | `archetype` |
| `create`, `destroy`, `add`, `remove`, `get`, `tryGet`, `has` e `isAlive` | `world` |
| Queries cacheadas com `with`, `without` e iteração por bloco e por entidade | `query` |
| Command buffer com IDs reservados e coalescing | `command_buffer` |
| Modo "iterando", que dispara `assert` em mudança estrutural direta | `world` |
| Instrumentação: número de arquétipos, entidades por arquétipo e histograma | `world` |

### Fora da v1

- Multithreading e scheduler de sistemas.
- Chunks de tamanho fixo.
- Detecção de mudança por versão.
- Componentes habilitáveis (bitset).
- Armazenamento híbrido em sparse set.
- Relacionamentos entre entidades, observers e eventos.
- Serialização, reflection e editor.
- Estabilidade de `ComponentId` entre execuções e entre DLLs.

Cada item de fora tem um ponto de extensão previsto na seção 10.

## 2. Glossário e convenções

| Termo | Definição nesta especificação |
| --- | --- |
| Entidade | Handle opaco de 64 bits: índice de 32 bits mais geração de 32 bits. Não guarda dados. |
| Componente | Tipo `T` registrado via `componentId<T>()`. Pode ter dados (`sizeof(T) > 0` e não vazio) ou ser uma tag (`std::is_empty_v<T>`). |
| `ComponentId` | Inteiro denso `uint32_t`, atribuído na ordem do primeiro uso. |
| Assinatura | `std::vector<ComponentId>` ordenado e sem duplicatas. Inclui as tags. |
| `columnTypes` | Subconjunto ordenado da assinatura que contém só os componentes com dados. |
| Arquétipo | Dono de uma `Table`. Tem assinatura única dentro do `World`. |
| Tabela | Armazenamento SoA: um `count` e um `capacity` únicos e N colunas de bytes. |
| Linha viva | Linha `r < count` cujas colunas contêm objetos construídos. |
| Linha morta | Faixa de memória sem objeto construído, nem mesmo moved-from. |
| Bloco | Faixa contígua de linhas entregue a um sistema como spans. Na v1, é uma tabela inteira. |
| Mudança estrutural | Qualquer operação que altera `count` ou assinatura: `create`, `destroy`, `add` e `remove`. |
| Aresta | Transição memoizada `arquétipo × (±ComponentId) → arquétipo`, com o mapeamento de colunas embutido. |

### Convenções

- Namespace `ecs`, headers em `include/ecs/`.
- Tipos em `PascalCase` e funções em `camelCase`.
- Complexidades são expressas em termos de: `k` = número de componentes da entidade, `A` = número de arquétipos, `Q` = número de queries, `n` = número de entidades e `m` = número de entidades num arquétipo.
- "Debug" significa `NDEBUG` não definido. Nesse modo, todas as invariantes da seção 6 podem ser verificadas sob demanda.

## 3. Arquitetura

A biblioteca tem sete módulos em quatro camadas, e cada camada depende só das que estão abaixo. A `Table` não conhece entidades vivas, queries nem records, o que permite trocá-la por chunks sem mexer nas camadas de cima.

&#91;embedded content: arquitetura em camadas · 7 módulos\]

As setas valem entre camadas: um módulo pode usar qualquer módulo de uma camada inferior, nunca de uma superior. O `World` é o único que conhece os records e o único que chama `fillHole`, então a atualização do record da entidade deslocada existe em um só lugar.

### Layout de diretórios

```
ecs/
  CMakeLists.txt  CMakePresets.json
  include/ecs/
    type_info.hpp      componentId<T>(), TypeInfo
    entity.hpp         Entity, EntityRecord
    table.hpp          Table, Column
    archetype.hpp      Archetype, Edge, Signature, SigHash
    query.hpp          Query<Ts...>, MatchedTable
    command_buffer.hpp CommandBuffer
    world.hpp          World (templates inline)
    detail/            SmallMap, assert, alinhamento
  src/
    table.cpp  archetype.cpp  world.cpp  command_buffer.cpp
  tests/
    unit_table.cpp  unit_world.cpp  invariants.cpp
    fuzz_vs_model.cpp  nontrivial.cpp  queries.cpp
    command_buffer.cpp  contract_death.cpp
  bench/
    iterate.cpp  transitions.cpp  fragmentation.cpp  flush.cpp
```

A parte não template (realocação da `Table`, `findOrCreate`, construção de arestas e flush) fica em `.cpp`. Os templates (`add<T>`, `get<T>`, `Query<Ts...>`) são finos e delegam para funções não template que recebem `ComponentId`, o que reduz o tempo de compilação e o tamanho do binário.

## 4. Especificação dos módulos

Cada módulo só conhece os que estão à sua esquerda na cadeia `type_info → entity → table → archetype → query / command_buffer → world`. A `Table` não sabe o que é uma entidade viva, uma query ou um record.

### 4.1 `type_info`

```cpp
using ComponentId = uint32_t;

struct TypeInfo {
    uint32_t size;            // 0 para tags
    uint32_t align;
    bool     trivial;         // is_trivially_copyable_v && is_trivially_destructible_v
    bool     tag;             // is_empty_v
    void (*moveConstruct)(void* dst, void* src) noexcept;  // nullptr se trivial
    void (*destroy)(void* p) noexcept;                     // nullptr se trivial
    std::string_view name;    // para diagnóstico
};

template<class T> ComponentId componentId();
const TypeInfo& typeInfo(ComponentId);
```

- **R-TI-1.** `componentId<T>()` devolve o mesmo valor em todas as chamadas do processo, e os IDs são densos a partir de 0.
- **R-TI-2.** `T` passa por `std::remove_cvref_t` antes do registro, então `componentId<const T&>() == componentId<T>()`.
- **R-TI-3.** `static_assert(std::is_nothrow_move_constructible_v<T>)` e `static_assert(alignof(T) <= 64)`.
- **R-TI-4.** O registro fica atrás de uma função com `static` local. A v1 não exige thread safety, mas registrar dentro de um `static` evita problemas com a ordem de inicialização.

### 4.2 `entity`

```cpp
struct Entity {
    uint32_t index;
    uint32_t generation;
    friend bool operator==(Entity, Entity) = default;
};
inline constexpr Entity NullEntity{UINT32_MAX, 0};

struct EntityRecord {
    Archetype* arch;        // nullptr = morto ou reservado (pendente)
    uint32_t   row;
    uint32_t   generation;  // geração atual do slot
};
```

- **R-EN-1.** Ao destruir, `generation` é incrementado e o índice vai para a `freeList`. Quando a geração atinge `UINT32_MAX`, o slot é aposentado e nunca mais reutilizado.
- **R-EN-2.** `isAlive(e)` equivale a `e.index < records.size() && records[e.index].generation == e.generation && records[e.index].arch != nullptr`.
- **R-EN-3.** Um índice reservado por command buffer tem `arch == nullptr` e a geração já definitiva. Ele não é considerado vivo até o flush.

### 4.3 `table`

```cpp
struct Column {
    const TypeInfo* info;
    std::byte*      data = nullptr;
    void* at(uint32_t r) noexcept { return data + size_t(r) * info->size; }
};

class Table {
public:
    explicit Table(std::span<const ComponentId> columnTypes);
    ~Table();                                    // destrói as linhas vivas e libera
    Table(Table&&) noexcept; Table& operator=(Table&&) noexcept;
    Table(const Table&) = delete;

    uint32_t count() const noexcept;
    uint32_t pushUninit(Entity e);               // linha nova MORTA no fim; pode realocar
    void     destroyRow(uint32_t r) noexcept;    // linha viva → morta
    std::optional<Entity> fillHole(uint32_t r) noexcept; // tapa linha morta com a última

    std::span<const Entity> entities() const noexcept;
    Column& column(uint16_t c) noexcept;
    uint16_t columnCount() const noexcept;
private:
    void reserve(uint32_t newCap);               // realoca TODAS as colunas juntas
    std::vector<Column> columns_;
    std::vector<Entity> entities_;               // size() == count
    uint32_t count_ = 0, capacity_ = 0;
};
```

- **R-TB-1.** Há um único `count` e um único `capacity` por tabela. Nenhuma coluna guarda contagem própria.
- **R-TB-2.** O crescimento é geométrico (×2, mínimo de 64 linhas). Na realocação, tipos triviais são copiados com um único `memcpy` por coluna; os demais usam `moveConstruct` seguido de `destroy` por elemento.
- **R-TB-3.** Cada coluna é alocada com `std::align_val_t(max(info->align, 64))`, alinhada à linha de cache para favorecer a vetorização.
- **R-TB-4.** `pushUninit` deixa a linha nova morta. O chamador precisa construir todas as colunas antes de qualquer outra operação na tabela.
- **R-TB-5.** `fillHole(r)` exige que a linha `r` esteja morta. Ele decrementa `count`; se `r` não era a última linha, realoca a última para `r` e devolve a entidade que foi deslocada.
- **R-TB-6.** A tabela não encolhe automaticamente. `shrinkToFit()` fica como API opcional.

### 4.4 `archetype`

```cpp
struct Edge {
    Archetype* target = nullptr;
    std::vector<int16_t> srcToDst;  // por coluna da origem: coluna no destino, ou -1
    int16_t addedColumn = -1;       // add de componente com dados: coluna no destino
};

struct Archetype {
    ArchetypeId              id;           // índice estável em World::archetypes_
    std::vector<ComponentId> signature;    // ordenada, inclui tags
    std::vector<ComponentId> columnTypes;  // ordenada, só dados
    Table                    table;
    SmallMap<ComponentId, Edge> addEdges, removeEdges;
    std::bitset<128>         mask;         // filtro rápido (opcional)
};
```

- **R-AR-1.** Os arquétipos vivem em `std::vector<std::unique_ptr<Archetype>>`, então o ponteiro é estável durante toda a vida do `World`. Na v1, arquétipos nunca são removidos.
- **R-AR-2.** Uma `Edge` é criada na primeira transição. O `srcToDst` é calculado por merge das duas `columnTypes` ordenadas, em O(k).
- **R-AR-3.** O arquétipo raiz (assinatura vazia) é criado no construtor do `World`, com `id == 0`.
- **R-AR-4.** `mask` só é usado enquanto o maior `ComponentId` for menor que 128. Acima disso, o teste cai para `std::includes`.

### 4.5 Índices do `World`

```cpp
std::vector<EntityRecord>                         records_;
std::vector<uint32_t>                             freeList_;
std::vector<std::unique_ptr<Archetype>>           archetypes_;
std::unordered_map<Signature, Archetype*, SigHash> bySignature_;
std::vector<SmallMap<ArchetypeId, uint16_t>>      componentIndex_;  // [cid]: arquétipo → coluna (NoColumn p/ tag)
std::vector<QueryCacheBase*>                      queries_;
uint32_t                                          iterating_ = 0;
```

- **R-WI-1.** `componentIndex_[cid]` lista todos os arquétipos que contêm `cid`. Ele é atualizado em `findOrCreate` e não precisa ser atualizado em nenhuma outra operação.
- **R-WI-2.** `get<T>(e)` custa um acesso a `records_[e.index]`, um lookup em `componentIndex_[cid]` e um acesso a `column.at(row)`.

### 4.6 `query`

```cpp
template<class... Ts>
struct MatchedTable {
    Archetype* arch;
    std::array<uint16_t, sizeof...(Ts)> cols;
};

template<class... Ts>
class Query : QueryCacheBase {
    std::vector<ComponentId> with_, without_;     // ordenados
    std::vector<MatchedTable<Ts...>> matched_;
public:
    Query& without(ComponentId...);
    template<class F> void eachBlock(F&& f);      // f(std::span<Ts>...)
    template<class F> void each(F&& f);           // f(Ts&...) ou f(Entity, Ts&...)
    void onArchetypeCreated(Archetype&) override; // matching incremental
};
```

- **R-QY-1.** `Ts` pode conter `const T`. Nesse caso, o span entregue é `std::span<const T>`. A constness é guardada como metadado de leitura e escrita, para uso futuro pelo scheduler.
- **R-QY-2.** Uma tag em `Ts` é um erro de compilação. Tags só podem aparecer no filtro (`with<Tag>()` e `without<Tag>()`).
- **R-QY-3.** Na criação, a query percorre `componentIndex_[c*]`, onde `c*` é o componente de `with_` com menos arquétipos. Ela não varre `archetypes_`.
- **R-QY-4.** `eachBlock` e `each` pulam arquétipos com `count == 0`. Os índices de coluna são resolvidos na hora do match; o loop interno não faz nenhum lookup.
- **R-QY-5.** Enquanto um `each` ou `eachBlock` está em andamento, `World::iterating_` fica maior que zero (com aninhamento permitido).
- **R-QY-6.** A vida da query é controlada pelo `World`, que devolve uma referência estável. A versão por RAII (desregistro no destrutor) fica para depois da v1.

### 4.7 `command_buffer`

O buffer é um `std::vector<std::byte>` com registros alinhados a 8 bytes:

```
[op:u8][pad][entity:u64][cid:u32][size:u32][payload: size bytes, alinhado]
op ∈ { Spawn, Destroy, Add, Remove }
```

- **R-CB-1.** `spawn()` reserva um índice imediatamente, conforme R-EN-3, e devolve um `Entity` que pode ser usado em comandos seguintes do mesmo buffer.
- **R-CB-2.** `add<T>(e, v)` constrói `T` por move dentro do payload. Se o buffer for descartado sem flush, os payloads não triviais são destruídos.
- **R-CB-3.** O flush é feito em duas passadas. A primeira agrupa os comandos por entidade, preservando a ordem, e calcula a assinatura final de cada uma. A segunda faz **uma única transição por entidade**, direto para o arquétipo final.
- **R-CB-4.** Semântica de conflitos, aplicada na ordem de gravação: `add` seguido de `remove` do mesmo `T` não deixa nada; `add` repetido mantém o último valor; `destroy` descarta os comandos anteriores da entidade; comandos após `destroy` são ignorados (com `assert` em debug).
- **R-CB-5.** Chamar `flush` com `iterating_ > 0` é erro de contrato.

## 5. API pública do `World`

Toda mudança estrutural direta exige `iterating_ == 0`. Durante uma iteração, as mudanças vão por `CommandBuffer`.

```cpp
class World {
public:
    Entity create();
    template<class... Ts> Entity create(Ts&&... comps);   // nasce direto no arquétipo final
    void   destroy(Entity e);
    bool   isAlive(Entity e) const noexcept;

    template<class T> void      add(Entity e, T&& value);    // ou atribui, se já tiver T
    template<class T> void      remove(Entity e);
    template<class T> bool      has(Entity e) const noexcept;
    template<class T> T&        get(Entity e);
    template<class T> T*        tryGet(Entity e) noexcept;

    template<class... Ts> Query<Ts...>& query();             // cacheada por (Ts..., filtros)
    CommandBuffer commands();
    void          flush(CommandBuffer&);

    WorldStats stats() const;                                // seção 4.5 / M5
    bool       checkInvariants() const;                      // seção 6, só em debug
};
```

| Operação | Pré-condição | Pós-condição | Custo |
| --- | --- | --- | --- |
| `create()` | — | Entidade viva no arquétipo raiz, com geração atual | O(1) amortizado |
| `create<Ts...>(…)` | `Ts` distintos | Entidade viva no arquétipo `{Ts…}`, sem passar pelos intermediários | O(\|Ts\|) + hash da assinatura na 1ª vez |
| `destroy(e)` | `isAlive(e)` | Componentes destruídos, geração incrementada, índice na `freeList`, record da entidade deslocada atualizado | O(k) |
| `add<T>(e, v)` | `isAlive(e)` | Se não tinha `T`: move para o arquétipo `sig ∪ {T}`. Se tinha: `get<T>(e) = std::move(v)`, sem mudança estrutural | O(k) com aresta em cache; O(k + hash) na 1ª vez |
| `remove<T>(e)` | `isAlive(e)` | Se tinha `T`: destrói `T` e move para `sig \ {T}`. Se não tinha: no-op | Igual a `add` |
| `has<T>(e)` | `isAlive(e)` | — | O(1), via `componentIndex_` |
| `get<T>(e)` | `isAlive(e) && has<T>(e)`; `T` não é tag | Referência válida até a próxima mudança estrutural em qualquer entidade | O(1), 3 indireções |
| `tryGet<T>(e)` | — | `nullptr` se a entidade estiver morta ou sem `T` | O(1) |
| `query<Ts...>()` | — | Mesma instância para a mesma combinação | O(A\_c\*) na 1ª vez; O(1) depois |
| `flush(cb)` | `iterating_ == 0` | Uma transição por entidade afetada | O(Σk) |

### Regras de validade de referências

- **R-API-1.** Ponteiros e referências obtidos de `get`, `tryGet` ou spans de `eachBlock` são invalidados por **qualquer** mudança estrutural no `World`, mesmo em outra entidade, porque a tabela pode realocar ou fazer swap.
- **R-API-2.** `Entity` é o único handle estável. Para guardar uma relação, guarde o `Entity` e chame `tryGet` ao usar.
- **R-API-3.** Escrever em componentes dentro de `each` é permitido; criar ou remover componentes, não.

## 6. Invariantes do sistema

As invariantes abaixo devem valer **entre** quaisquer duas chamadas públicas. `checkInvariants()` verifica todas, e os testes a chamam depois de cada operação.

| ID | Invariante | Pega qual bug |
| --- | --- | --- |
| I-1 | Para toda tabela, `entities().size() == count()`, e cada coluna tem capacidade para pelo menos `count()` linhas | Contagem divergente entre colunas |
| I-2 | Linhas `< count` estão vivas e linhas `>= count` estão mortas. Verificado com um componente contador de vida (construções − destruições == vivos) | Moved-from não destruído, double destroy |
| I-3 | Para todo `e` vivo, `records[e.index].arch->table.entities()[records[e.index].row] == e` | Record da entidade deslocada no swap não atualizado |
| I-4 | Para todo arquétipo `a` e toda linha `r`, `records[a.entities[r].index]` aponta para `(a, r)` | O mesmo bug, visto do lado da tabela |
| I-5 | `signature` e `columnTypes` estão ordenadas e sem duplicatas, e `columnTypes ⊆ signature`, contendo exatamente os não-tags | Coluna indexada errada por causa de tag |
| I-6 | `bySignature_` é bijetivo com `archetypes_` | Arquétipo duplicado |
| I-7 | `componentIndex_[c]` contém `a` se e só se `c ∈ a.signature`, e o índice de coluna guardado está correto | `get` lendo a coluna errada |
| I-8 | Para toda query `q` e todo arquétipo `a`, `a ∈ q.matched` se e só se `q.matches(a)` | Arquétipo novo não registrado nas queries |
| I-9 | Toda aresta `a --±c--> b` satisfaz `b.signature == a.signature ± c`, e seu `srcToDst` é consistente | Aresta stale ou mapeamento errado |
| I-10 | Índices na `freeList` têm `arch == nullptr`, e não há índices repetidos | Reuso duplo de slot |
| I-11 | Fora de `each`, `iterating_ == 0` | Contador vazado por retorno antecipado ou exceção |

I-2 exige instrumentação: em debug, um componente de teste `LifeCounter` incrementa e decrementa um contador global no construtor e no destrutor. O total precisa bater com o número de linhas vivas que contêm esse componente.

## 7. Requisitos não funcionais

As metas de desempenho são relativas a uma linha de base medida na mesma máquina, não a números absolutos. Cada meta é verificada por um benchmark da seção 8.

### Desempenho

| ID | Requisito | Meta |
| --- | --- | --- |
| NF-P1 | Iteração `Position += Velocity * dt` em 1M entidades num único arquétipo | Até 1,10× o tempo do SoA manual (dois `std::vector<float3>`) |
| NF-P2 | O mesmo, espalhado por 64 arquétipos de \~15k entidades | Até 1,25× o SoA manual |
| NF-P3 | O loop interno de `eachBlock` com tipos aritméticos é vetorizado | Confirmado por `-fopt-info-vec` (GCC) ou `-Rpass=loop-vectorize` (Clang) |
| NF-P4 | `add`/`remove` com aresta em cache, entidade com 4 componentes triviais | Zero alocações (com capacidade reservada) e zero lookups de hash |
| NF-P5 | Flush de N adds coalescidos (3 por entidade) | Pelo menos 2× mais rápido que 3N `add` diretos |
| NF-P6 | `get<T>(e)` com acesso aleatório | Medido e reportado; sem meta na v1 |

### Memória

- **NF-M1.** Overhead por entidade: 16 bytes de `EntityRecord` mais 8 bytes de `Entity` na tabela. Não há alocação por entidade.
- **NF-M2.** Overhead por arquétipo deve ser reportado por `stats()`: assinatura, arestas, colunas e capacidade ociosa.
- **NF-M3.** As colunas estão alinhadas a 64 bytes (R-TB-3).

### Segurança e diagnóstico

- **NF-S1.** Todo o código de teste roda limpo sob ASan e UBSan.
- **NF-S2.** Em debug, as pré-condições da seção 5 são verificadas por `ECS_ASSERT`, com mensagem que inclui a operação, o `Entity` e o nome do tipo.
- **NF-S3.** Erros de uso detectáveis em tempo de compilação viram `static_assert`: tag em `Ts` de query, tipo sem move `noexcept` e tipos repetidos em `create<Ts...>`.
- **NF-S4.** `ECS_ASSERT` é removível em release por macro (`ECS_ENABLE_ASSERTS=0`).

### Build

- **NF-B1.** CMake 3.25+, com presets `debug-asan`, `release` e `relwithdebinfo` (este último para profiling).
- **NF-B2.** Avisos com `-Wall -Wextra -Wpedantic -Werror` em CI.
- **NF-B3.** Dependências externas só em testes e benchmarks (doctest e Google Benchmark, via `FetchContent`). A biblioteca não tem dependências.

## 8. Estratégia de testes e benchmarks

O teste principal é um fuzz que compara o `World` com um modelo de referência trivialmente correto. Ele pega bugs de valor certo na entidade errada, que as invariantes estruturais sozinhas não pegam.

### Testes

| Suíte | O que faz | Critério |
| --- | --- | --- |
| `unit_table` | `pushUninit`, `destroyRow`, `fillHole` e realocação, com tipos triviais e não triviais | I-1 e I-2 após cada passo |
| `unit_world` | Um caso por linha da tabela da seção 5, incluindo `add` sobre `T` existente e `remove` de `T` ausente | Pós-condições exatas |
| `invariants` | Sequências curtas e determinísticas chamando `checkInvariants()` a cada operação | Todas as invariantes da seção 6 |
| `fuzz_vs_model` | 10⁶ operações aleatórias (create, destroy, add, remove, get), com semente fixa e reportada. O modelo é `std::unordered_map<Entity, std::map<ComponentId, Valor>>` | Igualdade de estado a cada 1.000 operações e `checkInvariants()` no fim |
| `nontrivial` | Componentes `std::string`, `std::unique_ptr<int>`, `std::vector<int>` e `LifeCounter` | Limpo sob ASan e UBSan; contador de vida zera no fim |
| `queries` | Arquétipos criados antes e depois da query, `without`, tags no filtro e arquétipos vazios | I-8; contagem de entidades visitadas igual à do modelo |
| `command_buffer` | Coalescing, conflitos de R-CB-4, `spawn` referenciado no mesmo buffer e buffer descartado sem flush | Estado igual ao de aplicar os comandos um a um; sem vazamento |
| `contract_death` | `add` durante `each`, `get` em entidade morta, `flush` durante iteração | O `assert` dispara (teste de morte em debug) |

No `fuzz_vs_model`, os valores dos componentes devem ser derivados do `Entity` e de um contador, para que uma troca de linhas apareça como valor errado.

### Benchmarks

| Benchmark | Variáveis | Métrica | Linha de base |
| --- | --- | --- | --- |
| `iterate_single` | n ∈ {10³, 10⁴, 10⁵, 10⁶} | ns/entidade | SoA manual com dois `std::vector` |
| `iterate_fragmented` | 1M entidades em A ∈ {1, 8, 64, 512, 4096} arquétipos | ns/entidade | `iterate_single` com o mesmo n |
| `transition` | k ∈ {2, 4, 8, 16}; componentes triviais e não triviais | ns/operação | Mesma operação sem o caminho `memcpy` |
| `transition_cold` | Primeira transição para arquétipos novos | ns/operação | `transition` |
| `flush_coalesced` | N ∈ {10³, 10⁵}, 3 adds por entidade | ns/entidade | 3N `add` diretos |
| `random_get` | Acesso a `get<T>` em ordem aleatória | ns/acesso | `std::vector<struct>` indexado |
| `create_destroy` | Churn de 10% das entidades por frame | ns/operação | — |

Cada benchmark reporta mediana e desvio de pelo menos 10 repetições, além de entidades por arquétipo e número de arquétipos. Os resultados são salvos em JSON (`--benchmark_out`) para comparar entre commits.

### Profiling

Os benchmarks quentes rodam sob `perf stat`, registrando ciclos, instruções, `cache-misses` e `L1-dcache-load-misses`. Em `iterate_fragmented`, a métrica de interesse é misses por entidade em função de A.

## 9. Marcos e critérios de aceitação

A v1 fecha em seis marcos sequenciais, e nenhum começa antes de o gate do anterior passar.

&#91;embedded content: marcos da v1 · 6 etapas, cada uma com seu gate\]

Os critérios abaixo são o gate de cada marco. Os IDs remetem às seções 6 e 7.

**M0 · Infraestrutura**

- [ ] Presets CMake `debug-asan`, `release` e `relwithdebinfo` (NF-B1)
- [ ] doctest e Google Benchmark via `FetchContent` (NF-B3)
- [ ] Baseline SoA de `iterate_single` medido e salvo em JSON

**M1 · Núcleo**

- [ ] `type_info`, `entity`, `table` e `World` mínimo, com `add`/`remove` via `findOrCreate` e sem arestas
- [ ] `checkInvariants()` cobre I-1 a I-7 e I-10
- [ ] `fuzz_vs_model` com 10⁶ operações passa
- [ ] Suíte `nontrivial` limpa sob ASan e UBSan (NF-S1)

**M2 · Arestas e índice**

- [ ] `Edge` com `srcToDst`; I-9 verificada
- [ ] `componentIndex_` com `get`, `tryGet` e `has`
- [ ] `transition` e `transition_cold` medidos, com e sem `memcpy`; NF-P4 atendido

**M3 · Queries**

- [ ] `Query<Ts...>` com matching incremental; I-8 verificada
- [ ] `eachBlock` e `each`; `static_assert` para tag em `Ts`
- [ ] NF-P1, NF-P2 e NF-P3 atendidos

**M4 · Command buffer**

- [ ] `CommandBuffer` sem coalescing, validado no fuzz
- [ ] Coalescing (R-CB-3) e conflitos (R-CB-4), comparados com a versão sem coalescing
- [ ] Modo iterando, I-11 e suíte `contract_death`
- [ ] NF-P5 atendido

**M5 · Instrumentação**

- [ ] `stats()` com número de arquétipos, entidades por arquétipo, histograma e memória ociosa (NF-M2)
- [ ] `iterate_fragmented` sob `perf stat`, com a curva de ns e misses por entidade registrada

## 10. Evoluções, riscos e questões em aberto

Os chunks são a próxima evolução depois da v1, porque `eachBlock` já isola os sistemas do formato do armazenamento.

### Pontos de extensão

| Evolução | Onde entra | O que muda | O que **não** muda |
| --- | --- | --- | --- |
| Chunks de 16 KB | `Table` vira uma lista de `Chunk`; `EntityRecord.row` passa a codificar `(chunk, linha)` | `Table`, `fillHole` e o iterador de blocos da `Query` | Assinatura dos sistemas (`eachBlock` com spans) |
| Iteração paralela | `Query::eachBlockParallel(pool, f)`, com um job por bloco | `Query` e uma flag de leitura/escrita por `Ts` | Armazenamento |
| Detecção de mudança | Versão `uint32_t` por coluna e por bloco, incrementada em acesso mutável | `Column`/`Chunk` e filtro `changed<T>()` na query | Transições |
| Componentes habilitáveis | Bitset por bloco e por tipo marcado | `Table` e iteração mascarada | Assinatura e arquétipos |
| Armazenamento híbrido | `TypeInfo::storage = Sparse`; o tipo fica fora da assinatura do arquétipo | `World::add/remove`, `componentIndex_` e queries mistas | Tabelas existentes |
| IDs estáveis entre DLLs | `componentId<T>()` passa a usar hash do nome do tipo, mapeado para um índice denso | Só `type_info` | Todo o resto |

### Riscos

| Risco | Sinal | Mitigação |
| --- | --- | --- |
| Fragmentação por tags e estados | `stats()` mostra muitos arquétipos com poucas entidades | Benchmark `iterate_fragmented` desde o M5; orientar estados frequentes para campos ou bitsets |
| Overhead de `componentIndex_` como `SmallMap` com muitos arquétipos | `has` e `get` lentos no `random_get` | Trocar por vetor denso `[cid][archId]` quando `A × C` couber em memória |
| Exceções em construtores de cópia usados por `add(const T&)` | Estado inconsistente após `throw` | Construir o valor temporário **antes** de qualquer mudança estrutural |
| Complexidade do coalescing (R-CB-3) | Bugs no fuzz de command buffer | Implementar primeiro sem coalescing e comparar as duas versões no fuzz |
| Explosão de arestas em `SmallMap` | Muita memória por arquétipo em mundos com muitos tipos | Medir no M5; um mapa global de arestas é a alternativa |

### Questões em aberto

- `create<Ts...>()` deve exigir que todos os `Ts` sejam movíveis, ou aceitar `const T&` com cópia?
- A query deve ser identificada pelo tipo (`query<Ts...>()` cacheada por template) ou por um builder em runtime? A especificação assume o primeiro modelo; o segundo é necessário para scripts.
- `destroy` direto de uma entidade que tem comandos num buffer pendente: o flush detecta o handle obsoleto pela geração e ignora os comandos, ou o `World` cancela os pendentes no momento do `destroy`? A especificação ainda não cobre o caso; a primeira opção é a mais barata.
- `get<T>` para tag: erro de compilação (como especificado) ou retorno de um `T&` estático?
