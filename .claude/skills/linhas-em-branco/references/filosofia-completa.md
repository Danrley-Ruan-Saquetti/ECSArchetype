# Linhas em Branco: Filosofia e Princípios (documento completo)

Documento-fonte da skill. Consulte quando precisar de exemplos em uma linguagem específica, da tabela de níveis de agrupamento, das convenções de cada ecossistema ou do que formatadores conseguem automatizar.

## Sumário

- Premissa
- Princípios fundamentais
- Níveis de agrupamento
- Categorias recorrentes (dependências, constantes, membros, corpo de função, guardas, controle, testes)
- Convenções por linguagem
- Anti-padrões
- Linhas em branco e refatoração
- Automação: o que as ferramentas resolvem
- Checklist de revisão

## Premissa

Uma linha em branco é pontuação: ela separa **ideias**, não linhas. Funciona como a quebra de parágrafo num texto. Linhas que só fazem sentido juntas ficam coladas, e uma mudança de intenção ganha uma linha em branco.

Código é lido muito mais vezes do que é escrito. O leitor não lê linha a linha: primeiro varre a forma visual da função para entender sua estrutura, e só depois lê o detalhe do trecho que interessa. As linhas em branco são o que dá forma a essa primeira leitura.

O espaço vertical é, portanto, informação. Cada linha em branco afirma algo: "aqui termina um passo". Quando ela aparece onde não há mudança de passo, a afirmação é falsa e o leitor é enganado. Quando falta, o leitor precisa descobrir sozinho onde os passos começam e terminam.

Esses princípios valem para qualquer linguagem com sintaxe imperativa: C, C++, Java, C#, PHP, TypeScript, Python, Go, Rust. Os exemplos alternam entre linguagens de propósito, porque a ideia é a mesma em todas.

## Princípios fundamentais

Seis princípios cobrem quase todas as decisões. Os demais casos derivam deles.

1. **Coesão define o bloco.** Linhas que trabalham juntas para um único objetivo formam um bloco, e um bloco nunca é partido ao meio. Uma linha em branco aparece só onde o objetivo muda.
2. **O teste do título mental.** Se você consegue dar um nome curto a cada bloco ("valida a entrada", "monta a consulta", "grava o resultado"), o agrupamento está correto. Se um bloco precisa de dois nomes, ele é dois blocos. Se dois blocos levam o mesmo nome, eles são um só.
3. **Uma única intensidade.** Dentro de um arquivo, use no máximo uma linha em branco seguida. Duas linhas não comunicam "separação maior" de forma confiável, porque cada autor as usa de um jeito. A hierarquia maior é expressa pela estrutura da linguagem (funções, classes, módulos), não por mais espaço. A exceção é uma convenção formal da linguagem, como as duas linhas do PEP 8 entre definições de nível superior.
4. **Proximidade entre declaração e uso.** Uma variável fica colada às linhas que a consomem, e não num bloco de "declarações" afastado. Assim cada bloco carrega tudo de que precisa para ser lido sozinho.
5. **Delimitadores já separam.** Chaves, `begin/end` e a indentação já marcam o começo e o fim de um escopo. Uma linha em branco logo depois de abrir um bloco, ou logo antes de fechá-lo, repete essa informação e só gasta espaço.
6. **Coesão vence simetria.** O espaço não existe para deixar o código "arejado" ou visualmente equilibrado. Ele existe para comunicar estrutura. Um código denso e bem agrupado é melhor do que um código espaçado de forma uniforme.

## Níveis de agrupamento

A mesma regra se repete em escalas diferentes. Em cada nível, a linha em branco separa as unidades daquele nível, e a estrutura da linguagem cuida do nível acima.

| Nível | Unidade separada | O que a linha em branco comunica |
| --- | --- | --- |
| Arquivo | Cabeçalho, imports, constantes, tipos, implementação | Mudança de seção do módulo |
| Tipo (classe, struct) | Grupos de campos, construtores, métodos | Mudança de papel do membro |
| Função | Fases: guarda, preparação, processamento, resultado | Mudança de passo do algoritmo |
| Bloco interno | Sub-passos dentro de um laço ou condição | Mudança de sub-passo (raro; costuma indicar extração) |
| Expressão longa | Grupos de chaves num literal ou argumentos | Mudança de grupo semântico |

Quanto mais fundo o nível, mais rara deve ser a linha em branco. Um corpo de laço que precisa de três blocos internos está pedindo para virar uma função.

## Categorias recorrentes

As mesmas formas aparecem em todas as linguagens. Cada exemplo abaixo usa uma linguagem diferente, mas o padrão se transfere sem mudanças.

### Dependências: um grupo por origem

A ordem é: biblioteca padrão, depois dependências externas, depois o próprio projeto. A linha em branco diz ao leitor de onde vem cada coisa e quanto ele controla dela.

```cpp
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <spdlog/spdlog.h>

#include "ecs/world.hpp"
#include "physics/body.hpp"
```

### Constantes e configuração: um grupo por domínio

```python
MAX_ENTITIES = 100_000
CHUNK_SIZE = 16_384

GRAVITY = -9.81
FIXED_DT = 1 / 60
```

### Membros de um tipo: um grupo por papel

Campos de dados, estado de controle, construção e operações formam grupos distintos. Membros paralelos, como os arrays de um layout SoA, ficam colados porque representam uma única ideia.

```php
final class Inventory
{
    private array $itemIds = [];
    private array $quantities = [];

    private int $count = 0;

    public function __construct(private readonly int $capacity) {}

    public function add(int $itemId, int $qty): void
    {
        if ($this->count >= $this->capacity) {
            throw new OverflowException('Inventário cheio');
        }

        $i = $this->count++;
        $this->itemIds[$i] = $itemId;
        $this->quantities[$i] = $qty;
    }
}
```

### Corpo de função: guarda, preparação, processamento, resultado

A maioria das funções segue essa sequência de fases, e cada fase ocupa um bloco.

```ts
async function importLevel(path: string): Promise<Level> {
  if (!path.endsWith(".json")) throw new Error(`Formato inválido: ${path}`);

  const raw = await readFile(path, "utf8");
  const data = LevelSchema.parse(JSON.parse(raw));

  const entities = data.entities.map(toEntity);
  const tilemap = buildTilemap(data.tiles, data.width);

  return { name: data.name, entities, tilemap };
}
```

### Cláusulas de guarda: um bloco único

Várias guardas seguidas formam um só passo ("rejeitar casos inválidos") e por isso ficam coladas.

```python
def can_attack(attacker, target):
    if attacker is None or target is None:
        return False
    if not attacker.alive:
        return False
    if target.invulnerable:
        return False

    return distance(attacker, target) <= attacker.range
```

### Estruturas de controle: espaço antes e depois

Um laço ou condição com várias linhas marca uma mudança no fluxo, então ganha uma linha em branco antes e depois. Uma variável que existe só para aquele laço fica colada a ele.

O "antes" vale para o passo, não para a palavra-chave: o passo começa onde começam os seus preparativos. Declarações que existem só para alimentar a condição ou o laço — o valor esperado que o `if` compara, o acumulador que o `for` preenche, a variável não inicializada que uma cadeia `if/else` atribui, o `clear()` antes de reconstruir um vetor — fazem parte dele, e a linha em branco vai antes delas. Aplicar a regra mecanicamente, com uma linha antes de todo `if`/`for`, produz simetria visual e parte cada passo entre o preparativo e o uso. Pares inseparáveis (destruir uma linha e tapar o buraco, abrir e verificar) também vencem esta regra.

```cpp
// Errado: o preparativo ficou separado da comparação que é a sua razão de existir
const int16_t expected = column == NoColumn ? -1 : column;

if (mapping[i] != expected) {
    return false;
}

// Correto: calcular o esperado e comparar é um passo só
const int16_t expected = column == NoColumn ? -1 : column;
if (mapping[i] != expected) {
    return false;
}
```

Já uma declaração compartilhada por vários passos seguintes (um rótulo usado por várias guardas, um ponteiro lido por todo o resto da função) é uma etapa de preparação própria e pode ficar separada.

### Resultado: o `return` como etapa

O `return` final forma um bloco próprio quando o título do último passo não o inclui, ou seja, quando ele entrega o produto de passos anteriores. Fica colado quando é a conclusão do próprio passo: `return` dentro de uma guarda, função de um passo só, ou valor criado na linha anterior só para ser devolvido.

```cpp
Entity World::place(Archetype& archetype) {
    Entity entity = store_.allocate();
    EntityRecord& record = store_.record(entity.index);

    record.row = archetype.table.pushUninitialized(entity);
    record.archetype = &archetype;

    return entity;
}
```

Três etapas: obter identidade e slot, posicionar no arquétipo, entregar. O `record` fica com a alocação porque é uma consulta derivada direta da linha anterior; uma declaração que faz ponte entre dois passos fica no passo que a produz quando é uma consulta derivada, e no que a consome quando é montada para ele.

```cpp
void integrate(Motion& m, float dt) {
    for (std::size_t i = 0; i < m.count; ++i) {
        m.velY[i] += kGravity * dt;
    }

    for (std::size_t i = 0; i < m.count; ++i) {
        m.posX[i] += m.velX[i] * dt;
        m.posY[i] += m.velY[i] * dt;
    }

    m.dirty = true;
}
```

### Testes: Arrange, Act, Assert

Em testes, os blocos têm significado formal: são exatamente três. Se o bloco de ação tem mais de uma linha, o teste provavelmente verifica mais de um comportamento.

```php
public function testRemoveKeepsArrayDense(): void
{
    $pool = new Pool(4);
    $pool->spawn(1);
    $pool->spawn(2);

    $pool->remove(0);

    $this->assertSame(1, $pool->size());
    $this->assertSame(2, $pool->get(0));
}
```

## Convenções por linguagem

A filosofia é universal, mas cada ecossistema fixa alguns números. Quando existe um guia oficial ou um formatador padrão, ele prevalece sobre preferências pessoais, porque consistência dentro do ecossistema vale mais do que qualquer escolha individual.

| Linguagem | Referência | Regras de espaço vertical |
| --- | --- | --- |
| Python | PEP 8, Black, Ruff | 2 linhas entre definições de nível superior; 1 entre métodos; uso parcimonioso dentro de funções |
| PHP | PSR-12, PHP-CS-Fixer | 1 linha após `namespace` e após o bloco de `use`; `use` agrupados por tipo (classes, funções, constantes); nada de linha em branco logo após a chave de abertura |
| C++ | Google Style, clang-format | Minimizar espaço vertical; não iniciar nem terminar blocos com linha em branco; `MaxEmptyLinesToKeep: 1` |
| TypeScript / JS | Prettier, ESLint | Prettier reduz sequências a 1 linha; ESLint `padding-line-between-statements` e `lines-between-class-members` para regras finas |
| Go | gofmt | Reduz sequências a 1 linha; grupos de `import` separados por linha em branco |
| Rust | rustfmt | `blank_lines_upper_bound` padrão de 1 |

O Python é a principal exceção à regra da intensidade única: as duas linhas entre definições de nível superior são uma convenção formal, e não uma preferência do autor. Elas compensam a ausência de chaves, que nas outras linguagens já delimitam visualmente cada definição.

Valores conforme a documentação conhecida até 2026; confira o guia da versão que o seu projeto usa.

## Anti-padrões

Cada anti-padrão é uma linha em branco que afirma algo falso, ou a falta de uma que deveria estar lá.

- **Paredão.** Validação, I/O e cálculo seguidos, sem nenhuma separação. O leitor precisa reconstruir sozinho onde cada passo começa e termina.
- **Espaçamento uniforme.** Uma linha em branco entre todas as instruções. Se tudo está separado, nada está agrupado, e o espaço deixa de comunicar.
- **Moldura vazia.** Uma linha em branco logo após abrir um bloco ou antes de fechá-lo, o que duplica a informação da chave ou da indentação.
- **Bloco partido.** Um par que só faz sentido junto, separado por uma linha em branco: um `lock` e seu `unlock`, uma declaração e sua única utilização, uma abertura de arquivo e sua verificação de erro.
- **Espaço por sintaxe.** Uma linha em branco antes de todo `if`, `for` ou `return` porque "fica mais organizado", separando-o da declaração que o alimenta. É simetria, não estrutura: o critério deve ser a mudança de passo, não o tipo da instrução seguinte.
- **Hierarquia por quantidade.** Duas ou três linhas em branco para indicar uma "seção mais importante", fora de uma convenção formal da linguagem.
- **Comentários-título dentro de funções.** Linhas como `// --- Validação ---` dividindo um corpo longo. A necessidade de títulos indica que cada seção deveria ser uma função com esse nome.
- **Diff de espaço.** Alterar linhas em branco de código que você não está modificando. Isso polui o histórico e dificulta a revisão.

```python
# Bloco partido: abertura e verificação separadas
handle = open_device(path)

if handle is None:
    raise DeviceError(path)

# Correto: as duas linhas formam um único passo
handle = open_device(path)
if handle is None:
    raise DeviceError(path)
```

## Linhas em branco e refatoração

Um bloco bem delimitado é uma função que ainda não foi extraída. A linha em branco marca o ponto de corte, e o título mental do bloco já é o nome da nova função.

Isso cria um ciclo útil. Primeiro, agrupe o código em blocos coesos. Depois, quando uma função acumula muitos blocos (como regra prática, mais de 3 ou 4), extraia cada um. A função original vira uma sequência de chamadas nomeadas, e as linhas em branco dentro dela quase desaparecem, porque cada linha já é um passo inteiro.

```ts
// Antes: três blocos com títulos implícitos
function processOrder(order: Order): Receipt {
  if (!order.items.length) throw new EmptyOrderError();
  if (!order.customer.verified) throw new UnverifiedError();

  const subtotal = order.items.reduce((s, i) => s + i.price * i.qty, 0);
  const discount = subtotal > 500 ? subtotal * 0.1 : 0;
  const total = subtotal - discount;

  const receipt = { orderId: order.id, total, issuedAt: new Date() };
  repository.save(receipt);
  return receipt;
}

// Depois: cada bloco virou uma função com o nome do seu título
function processOrder(order: Order): Receipt {
  validate(order);
  const total = calculateTotal(order.items);
  return issueReceipt(order.id, total);
}
```

O caminho inverso também serve de diagnóstico. Se você não consegue decidir onde colocar as linhas em branco de uma função, provavelmente as responsabilidades dela estão misturadas. O problema não é de formatação, e sim de design.

## Automação: o que as ferramentas resolvem

Formatadores resolvem a parte **mecânica**, mas a parte **semântica** continua sendo uma decisão humana. Eles impõem limites (no máximo 1 linha seguida, nenhuma linha no início de um bloco, 2 linhas entre definições em Python). Não sabem, porém, onde termina um passo do algoritmo, porque isso depende do significado do código.

| Aspecto | Automatizável | Ferramentas |
| --- | --- | --- |
| Limite de linhas seguidas | Sim | Prettier, Black, gofmt, rustfmt, clang-format |
| Linhas no início e no fim de blocos | Sim | clang-format, PHP-CS-Fixer, ESLint |
| Espaço entre definições e membros | Sim | PEP 8 via Black/Ruff, ESLint `lines-between-class-members` |
| Grupos de imports | Sim | isort, ESLint `import/order`, clang-format `IncludeBlocks` |
| Linha antes de `return` ou depois de declarações | Parcialmente | ESLint `padding-line-between-statements` |
| Onde termina um passo lógico | Não | Revisão humana |

Regras do tipo "sempre uma linha em branco antes de `return`" devem ser adotadas com cuidado. Em funções de 2 ou 3 linhas elas fragmentam o código sem nenhum ganho, e funcionam melhor como heurística do que como regra obrigatória.

Configure o formatador no projeto e rode-o no CI. Assim as discussões de revisão ficam restritas ao que a ferramenta não consegue decidir.

## Checklist de revisão

- [ ] Cada bloco tem um título mental único e curto
- [ ] Nenhum bloco precisa de dois títulos, e nenhum par de blocos vizinhos compartilha o mesmo título
- [ ] No máximo 1 linha em branco seguida (exceto onde a convenção da linguagem define outro valor)
- [ ] Nenhuma linha em branco logo após abrir um bloco ou antes de fechá-lo
- [ ] Variáveis declaradas junto do bloco que as consome
- [ ] Pares inseparáveis (abrir/verificar, lock/unlock, declarar/usar) ficam colados
- [ ] Laços e condições de várias linhas têm espaço antes (dos seus preparativos) e depois
- [ ] O `return` final é separado só quando entrega o produto de passos anteriores
- [ ] Guardas consecutivas formam um único bloco
- [ ] Testes seguem exatamente três blocos: Arrange, Act, Assert
- [ ] Funções com mais de 3 ou 4 blocos foram avaliadas para extração
- [ ] O diff não altera linhas em branco de código não relacionado