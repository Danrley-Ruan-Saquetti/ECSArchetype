---
name: comentarios-de-codigo
description: Filosofia e critérios para decidir quando escrever, manter, converter ou apagar comentários em código, docstrings, documentação de API e arquivos de configuração. Use sempre que for escrever ou editar código em qualquer linguagem, revisar um diff ou PR, refatorar, "limpar" um arquivo, documentar uma função ou API pública, ou mexer em configs (TOML, YAML, .env, JSON) — mesmo que o usuário não fale em "comentários". Use também quando o usuário pedir para comentar código, remover comentários, adicionar docstrings/JSDoc/Doxygen, ou perguntar se um comentário deve existir.
---

# Comentários em código

O código expressa **o que** acontece; o comentário existe só para o que não cabe na sintaxe. Nome, tipo, assinatura, estrutura, asserção e teste são verificados por alguma ferramenta. O comentário é o único artefato do repositório que ninguém checa: quando diverge da realidade, ele não quebra — mente em silêncio, com a mesma autoridade de quando era verdade.

Por isso todo comentário é uma de duas coisas:

1. **Falha de expressão do código** → corrija o código (renomeie, extraia, tipe) e apague o comentário.
2. **Informação que a linguagem não consegue carregar** → o comentário é o único lugar onde ela existe; escreva-o bem e preserve-o.

Confundir os dois produz tanto código poluído quanto código mudo. O objetivo não é "poucos comentários" nem "muitos": é zero comentários do tipo 1 e todos os do tipo 2.

Para a argumentação completa (assimetria de verificação, respostas a objeções), leia `references/filosofia-completa.md` quando precisar justificar uma decisão ao usuário ou resolver um caso que esta página não cobre.

## O teste de decisão

Antes de escrever um comentário, faça duas perguntas, nesta ordem:

**1. Essa informação pode ser movida para dentro do código?** Tente, nesta ordem:

| Forma | Elimina comentários sobre |
| --- | --- |
| Renomear | o que a variável/função é ou faz |
| Extrair função | o que um bloco faz (o comentário vira o nome) |
| Nomear constante | o significado de um literal (`86400` → `SECONDS_PER_DAY`) |
| Fortalecer o tipo | unidade, faixa, significado (`Milliseconds` em vez de `int`) |
| Enum | os valores possíveis de um campo |
| Asserção | invariantes que devem valer em execução |
| Teste com nome descritivo | comportamento esperado |

Se alguma serve, aplique e não comente.

**2. Se não pode, a informação existe em algum outro lugar do arquivo?** Se não existe, comente. Se já existe (está no código, no nome, no tipo), o comentário é eco — não escreva.

Quando nada da tabela serve, isso não é falha de quem escreve: é limite da linguagem.

## O que apagar (ou nunca escrever)

O teste comum: **se apagar o comentário não remove nenhuma informação do repositório, ele era eco.**

- **Paráfrase da linha** — `// incrementa o contador` acima de `++count`.
- **Paráfrase da função** — `// retorna true se é maior de idade` acima de `check(a)`. Isso pede `is_adult(age)`, não um comentário.
- **Redundância de assinatura** — `@param int x`, `@returns {string}` quando o tipo já está declarado.
- **Código comentado** — o controle de versão existe para isso.
- **Ruído cerimonial** — cabeçalho de autor/data, divisórias ASCII, `// fim do if`, `// construtor`, changelog inline.
- **Marcadores vazios** — `TODO`, `NOTE`, `IMPORTANTE` sem ação concreta, motivo ou ticket.

### Armadilhas específicas de agentes

Agentes tendem a produzir uma classe própria de comentários-eco. Não escreva:

- **Narração da edição** — `// alterado para usar o novo cache`, `// corrigido o bug`, `// adicionado tratamento de erro`. Isso descreve a mudança, não o código; pertence à mensagem de commit ou à resposta ao usuário, e fica falso no instante seguinte.
- **Comentários dirigidos ao usuário da conversa** — `// aqui você pode ajustar o valor`, `// conforme solicitado`. O leitor do arquivo não é o usuário desta sessão.
- **Passo a passo de tutorial** — `// Passo 1: ler o arquivo`, `// Passo 2: processar`. Se o fluxo precisa de rótulos, extraia funções com esses nomes.
- **Docstring em toda função privada** só porque o estilo "parece completo". Documentar tudo esconde o que importa.
- **Placeholders** — `// implementar lógica aqui`, `// ...resto do código`. Ou implemente, ou diga ao usuário o que falta.

## O que deve existir

Informação que não está em nenhum outro lugar do arquivo. Apagá-la é perda, não limpeza.

- **Razão da escolha** — por que esta solução e não a alternativa óbvia. Sem isso, alguém reintroduz a alternativa achando que melhorou o código.
  ```
  // Sequencial de propósito: a versão paralela perdeu nos testes porque o
  // custo de sincronização domina para lotes < 10k itens.
  ```
- **Número medido** — origem, condição e data. Número sem data não pode ser auditado nem invalidado.
  ```
  // 64 = ponto de saturação medido em 2026-03; acima disso a contenção de lock
  // anula o ganho. Refazer a medição se o pool de workers mudar.
  ```
- **Código deliberadamente menos legível** — SoA em vez de AoS, laço desenrolado, padding de alinhamento, busca linear onde caberia um mapa. Registre a medição que justifica: regressão de desempenho é a classe de bug que a CI não pega, e a forma "limpa" é exatamente o que um refatorador vai querer restaurar.
- **Restrição externa** — hardware, errata, protocolo, teto de serviço vizinho, regulação.
- **Invariante não local** — ordem exigida porque outro módulo faz busca binária, pareamento escrita/leitura em código concorrente, ordem obrigatória de chamadas. Prefira asserção; comente quando não der.
- **Workaround** — referência ao bug e condição de remoção. Sem critério de saída, vira permanente por omissão.
  ```
  // Workaround para o bug #4821 da lib X (quebra em 2.1.0, corrigido em 2.3.0).
  // Remover quando o piso de versão subir para 2.3.
  ```
- **Violação deliberada de convenção** — o comentário é o que separa decisão de descuido.

### Como escrever para durar

Quanto mais perto o comentário está do que o código já diz, mais rápido apodrece. Paráfrase quebra na próxima edição; razão sobrevive a reescritas inteiras.

- Escreva no nível da **decisão**, não da instrução.
- **Date** medições.
- Dê **critério de saída** ao que é temporário.
- **Aponte** para ticket/ADR/issue em vez de copiar o conteúdo.
- **Posicione no ponto de risco** — junto ao trecho que alguém vai querer "limpar", não no topo do arquivo.

## API pública é outra categoria

Documentação de interface pública é **contrato**, lida por quem nunca abrirá o arquivo (editor, autocompletar, docs geradas, `help()`). As regras de "apagar eco" não se aplicam do mesmo jeito: ali a documentação é a única superfície visível.

Documente o que a assinatura não diz:

- semântica de erro (o que lança, quando, o que o chamador deve fazer);
- contrato de nulo/vazio (ausência, erro, resultado legítimo?);
- efeitos colaterais (disco, rede, mutação de argumento, estado global);
- pré e pós-condições;
- garantias de desempenho/concorrência quando fazem parte do contrato (complexidade, thread-safety, reentrância);
- propriedade de recursos (quem libera o quê — crítico em C/C++);
- exemplo, quando o uso correto não é óbvio.

Dois limites: documente a interface **pública**, não cada função privada; e não repita o que o tipo já diz.

## Arquivos de configuração

Configuração não tem código para refatorar — só chave e valor. `max_connections` já tem o melhor nome possível, e ainda assim `64` não se explica: pode ser medição, limite de infra, acordo com outro sistema ou chute antigo, e é essa diferença que decide se alguém pode mexer.

Duas camadas:

1. **O que o campo é** (tipo, faixa, obrigatoriedade, default) → schema validado na carga (JSON Schema, tipo de config com validação). Isso é internalizar: falha na inicialização, não em produção.
2. **Por que este valor** → comentário com origem e condição de mudança.
   ```
   # 64 = teto do pool do banco em produção. Aumentar aqui sem aumentar lá
   # faz a aplicação enfileirar em vez de falhar rápido.
   max_connections = 64
   ```

JSON estrito não aceita comentários: a razão vai para o schema (`description`) ou para um arquivo de documentação vizinho. Valores por ambiente: a razão fica na fonte, não replicada em cada ambiente.

## Como agir em cada situação

**Escrevendo código novo.** Comece pelos nomes, tipos e estrutura. Só escreva comentário para o que sobrar depois do teste de decisão. Se você fez uma escolha não óbvia (algoritmo, estrutura de dados, layout de memória, constante), registre a razão no código — você é a única pessoa que sabe dela agora.

**Nunca invente a origem de um número ou a razão de uma escolha.** Um comentário com medição fabricada é pior que nenhum: tem toda a autoridade de um registro e nenhuma verdade. Se você não sabe de onde veio um valor que encontrou, não escreva uma justificativa plausível — pergunte ao usuário ou deixe um TODO acionável (`// TODO: origem de 64 desconhecida; medir saturação do pool antes de alterar`). Se você mesmo escolheu o valor sem medir, diga isso no comentário (`// valor inicial não medido; calibrar com benchmark de X`).

**Editando código existente.**
- Comentários tocados pela sua alteração precisam continuar verdadeiros. Atualize ou remova os que ficaram falsos; alterar código sem alterar o comentário deixa o arquivo inconsistente.
- Não faça uma limpeza ampla de comentários fora do escopo pedido. Se notar muitos comentários-eco, mencione ao usuário em vez de reescrever o arquivo inteiro sem ser pedido.

**Apagando ou "limpando" comentários.** Antes de apagar, pergunte: *onde mais essa informação está registrada?* Se você não consegue responder, não está limpando — está apagando a única cópia. Na dúvida sobre um comentário que parece explicar uma razão, medição ou restrição, preserve-o. Converter em código (nome, constante, tipo, asserção) é sempre melhor que apenas apagar.

**Revisando código (diff/PR).** Use o checklist:

- [ ] Comentários que descrevem o que o código faz foram removidos ou convertidos em nome, tipo, constante ou teste?
- [ ] Cada comentário restante sobreviveria a uma reescrita da implementação?
- [ ] Os comentários tocados pelo diff continuam verdadeiros?
- [ ] Código otimizado, estrutura não óbvia ou violação de convenção estão justificados?
- [ ] Números calibrados trazem origem e data?
- [ ] Workarounds trazem referência e condição de remoção?
- [ ] API pública nova/alterada documenta erros, efeitos colaterais e pré-condições?
- [ ] Sobrou código comentado, divisória, cabeçalho de autoria ou marcador vazio?

Ao apontar um problema, proponha a forma internalizada concreta (o novo nome, a constante, o tipo), não só "remova este comentário".

**Quando o usuário pede "comente este código".** Não adicione paráfrases linha a linha — isso produz exatamente o ruído descrito acima. Aplique o teste de decisão: melhore nomes onde o código não se explica, comente razões, medições, restrições e invariantes, e documente a API pública. Se o pedido for claramente didático (o usuário quer entender o código, não mantê-lo), explique na resposta da conversa em vez de poluir o arquivo — ou pergunte qual dos dois ele quer.

## Exemplo de conversão

Antes:
```cpp
// verifica se o buffer está cheio
// 4096 = tamanho máximo
bool check(const Buf& b) { return b.n >= 4096; }

for (...) {
    // processar entidades
    ...
}
```

Depois:
```cpp
// 4096 = tamanho de página do alvo; buffers maiores cruzam páginas e o
// flush perde ~30% de throughput (medido em 2026-05, x86-64, Linux 6.x).
constexpr std::size_t kMaxBufferBytes = 4096;

bool is_full(const Buffer& buffer) { return buffer.size >= kMaxBufferBytes; }

process_entities(world);
```

A paráfrase de `check` virou nome; o literal virou constante nomeada; o rótulo do laço virou função. O único comentário restante carrega o que nenhum nome expressa: de onde veio 4096 e quando revalidar.