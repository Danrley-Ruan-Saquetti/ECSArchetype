---
name: blank-lines
description: Filosofia e regras de espaçamento vertical em código — onde colocar e onde remover linhas em branco para que elas separem ideias, não linhas. Use sempre que for escrever, editar, refatorar, formatar ou revisar código em qualquer linguagem imperativa (C, C++, Java, C#, PHP, TypeScript/JS, Python, Go, Rust), mesmo que o usuário não mencione linhas em branco. Use também quando o pedido envolver legibilidade, organização de funções, agrupamento de imports ou membros, estrutura de testes (Arrange/Act/Assert), configuração de formatadores (Prettier, Black, clang-format, gofmt, rustfmt, PHP-CS-Fixer) ou code review de estilo.
---

# Linhas em Branco

Uma linha em branco é pontuação: separa **ideias**, não linhas. Cada uma afirma "aqui termina um passo". Se aparece onde não há mudança de passo, engana o leitor; se falta onde há, obriga o leitor a reconstruir a estrutura sozinho. O leitor primeiro varre a forma visual da função e só depois lê o detalhe — as linhas em branco são o que dá forma a essa primeira leitura.

Para exemplos em várias linguagens, tabela de níveis de agrupamento, convenções por ecossistema e a seção de automação, leia `references/full-philosophy.md`. Este arquivo traz o que é preciso para aplicar a filosofia no dia a dia.

## Os seis princípios

1. **Coesão define o bloco.** Linhas que trabalham juntas para um único objetivo formam um bloco — um passo, ou etapa, do algoritmo — e um bloco nunca é partido ao meio. A linha em branco aparece só onde o objetivo muda, nunca por causa do tipo de instrução que vem a seguir (`if`, `for`, `return`).
2. **Teste do título mental.** Cada bloco deve aceitar um nome curto ("valida a entrada", "monta a consulta", "grava o resultado"). Bloco com dois nomes são dois blocos; dois blocos vizinhos com o mesmo nome são um só.
3. **Uma única intensidade.** No máximo uma linha em branco seguida. Hierarquia maior se expressa pela estrutura da linguagem (funções, classes, módulos), não por mais espaço. Exceção: convenções formais, como as 2 linhas do PEP 8 entre definições de nível superior em Python.
4. **Declaração perto do uso.** Variável fica colada às linhas que a consomem, para que cada bloco se leia sozinho.
5. **Delimitadores já separam.** Nada de linha em branco logo após abrir um bloco (`{`, `:`, `begin`) nem logo antes de fechá-lo — a chave ou a indentação já dizem isso.
6. **Coesão vence simetria.** Espaço comunica estrutura, não "areja" o código. Denso e bem agrupado é melhor que espaçado de forma uniforme.

## Como aplicar ao escrever código

Antes de digitar o corpo de uma função, pense nas fases. A maioria segue **guarda → preparação → processamento → resultado**, e cada fase é um bloco:

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

Regras recorrentes, todas derivadas dos princípios:

- **Imports:** um grupo por origem — biblioteca padrão, dependências externas, projeto — separados por uma linha em branco.
- **Constantes:** um grupo por domínio.
- **Membros de tipo:** um grupo por papel (dados, estado de controle, construção, operações). Membros paralelos, como os arrays de um layout SoA, ficam colados porque são uma ideia só.
- **Guardas consecutivas:** um único bloco ("rejeitar casos inválidos"), sem linhas entre elas.
- **Laços e condições de várias linhas:** linha em branco antes e depois — mas o "antes" fica antes dos **preparativos** deles. O passo começa onde começam as declarações que existem só para alimentar a condição ou o laço (o valor esperado que o `if` compara, o acumulador que o `for` preenche, a variável não inicializada que a cadeia `if/else` atribui, o `clear()` antes de reconstruir), não onde começa o `if`/`for`.
- **Resultado:** o `return` final é um bloco próprio quando o título do último passo não o inclui — ele entrega o produto de passos anteriores. Fica colado quando é a conclusão do próprio passo (`return` dentro de uma guarda, função de um passo só, valor criado na linha anterior só para ser devolvido).
- **Testes:** exatamente três blocos — Arrange, Act, Assert. Se o Act tem mais de uma linha, o teste provavelmente verifica mais de um comportamento.
- **Pares inseparáveis ficam colados:** abrir/verificar erro, `lock`/`unlock`, declarar/único uso, operação/sua contraparte (destruir a linha e tapar o buraco). Vencem a regra de laços e condições.

```cpp
Entity entity = store.allocate();
EntityRecord& record = store.record(entity.index);

record.row = table.pushUninitialized(entity);
record.archetype = &archetype;

return entity;
```

```cpp
// preparativo + condição: um passo só
const int16_t expected = column == NoColumn ? -1 : column;
if (mapping[i] != expected) {
  return false;
}
```

**Profundidade:** quanto mais interno o nível, mais rara a linha em branco. Um corpo de laço que precisa de vários blocos internos está pedindo para virar função.

## Como aplicar ao editar código existente

Aqui o risco maior não é errar o espaçamento, e sim poluir o diff.

- **Não altere linhas em branco de código que você não está modificando.** Diff de espaço dificulta revisão e suja o histórico, mesmo que o código antigo viole estes princípios. Se valer a pena corrigir, mencione ao usuário e proponha um commit separado.
- **Respeite o formatador do projeto.** Se houver `.clang-format`, `.prettierrc`, `pyproject.toml` (Black/Ruff), `.php-cs-fixer.php`, `rustfmt.toml` etc., ele prevalece sobre preferências pessoais — consistência dentro do ecossistema vale mais. Aplique esta filosofia só no que o formatador não decide: **onde termina um passo lógico**.
- **No código novo ou reescrito dentro da edição**, aplique os princípios normalmente.

## Linhas em branco como sinal de design

Um bloco bem delimitado é uma função que ainda não foi extraída: a linha em branco é o ponto de corte e o título mental é o nome da nova função. Use isso ativamente:

- Função com mais de 3–4 blocos → avalie extrair cada bloco. A função original vira uma sequência de chamadas nomeadas e as linhas em branco internas quase desaparecem.
- Vontade de escrever comentários-título (`// --- Validação ---`) dentro de uma função → é a mesma sinalização: cada seção quer ser uma função com aquele nome.
- Não consegue decidir onde pôr as linhas em branco → as responsabilidades estão misturadas. O problema é de design, não de formatação; diga isso em vez de apenas reformatar.

Não extraia por conta própria sem que o pedido envolva refatoração; sugira.

## Anti-padrões

| Anti-padrão | Por que é ruim |
| --- | --- |
| **Paredão** — validação, I/O e cálculo sem separação | O leitor reconstrói os passos sozinho |
| **Espaçamento uniforme** — linha entre toda instrução | Se tudo está separado, nada está agrupado |
| **Moldura vazia** — linha após `{` ou antes de `}` | Duplica a informação do delimitador |
| **Bloco partido** — par inseparável com linha no meio | Afirma uma mudança de passo que não existe |
| **Espaço por sintaxe** — linha antes de todo `if`/`for`/`return`, separando-o da declaração que o alimenta | Simetria visual no lugar de passos; parte o passo entre preparativo e uso |
| **Hierarquia por quantidade** — 2–3 linhas para "seção importante" | Cada autor lê de um jeito; use a estrutura da linguagem |
| **Comentários-título dentro de funções** | Indica funções não extraídas |
| **Diff de espaço** — mexer em linhas de código não relacionado | Polui o histórico e a revisão |

## Ao revisar código

Aponte problemas de espaçamento citando o princípio violado ("bloco partido: a abertura e a verificação de erro formam um passo só"), não como gosto pessoal. Priorize os que escondem estrutura (paredão, bloco partido, função com blocos demais) sobre os cosméticos (moldura vazia), e não peça mudanças que o formatador do projeto já resolveria.

Checklist:

- [ ] Cada bloco tem um título mental único e curto
- [ ] Nenhum bloco precisa de dois títulos; nenhum par vizinho compartilha o mesmo
- [ ] No máximo 1 linha em branco seguida (salvo convenção da linguagem)
- [ ] Nenhuma linha em branco logo após abrir ou antes de fechar um bloco
- [ ] Variáveis declaradas junto do bloco que as consome
- [ ] Pares inseparáveis colados
- [ ] Laços e condições de várias linhas com espaço antes (dos seus preparativos) e depois
- [ ] `return` final separado só quando entrega o produto de passos anteriores
- [ ] Guardas consecutivas num único bloco
- [ ] Testes com exatamente três blocos: Arrange, Act, Assert
- [ ] Funções com mais de 3–4 blocos avaliadas para extração
- [ ] O diff não altera linhas em branco de código não relacionado