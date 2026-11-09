# Comentários em Código: Princípios e Critérios

## O princípio central

O código expressa o que acontece; o comentário existe apenas para o que não cabe na sintaxe. Essa é a linha divisória, e ela não é uma questão de estilo ou de gosto — é uma questão do que a linguagem é capaz de representar.

Nome, tipo, assinatura, estrutura e teste são formas de expressar comportamento. Todas são verificadas por alguma ferramenta: compilador, interpretador, type checker, linter, suíte de testes. Quando a informação cabe em uma dessas formas, colocá-la em um comentário é uma escolha pior, porque troca uma declaração verificada por uma frase que ninguém checa.

O problema é que existe uma classe inteira de informação que nenhuma linguagem consegue representar:

- **Por que esta solução e não a alternativa óbvia.** O código registra o caminho escolhido; ele é estruturalmente incapaz de registrar os caminhos rejeitados.
- **De onde veio um número.** Um limite medido, um valor calibrado, uma constante derivada de hardware ou de um sistema externo.
- **O que amarra este trecho a algo distante.** Invariantes entre partes do sistema que nenhum tipo local consegue expressar.
- **O que é deliberado e parece erro.** Violações de convenção feitas de propósito, workarounds de bugs de terceiros.

Nenhum renomeamento resolve esses casos, porque não há o que renomear. A informação simplesmente não existe no arquivo até alguém escrevê-la.

Daí o enunciado operacional do princípio: **todo comentário é ou uma falha de expressão do código, ou uma informação que a linguagem não tem como carregar.** O primeiro caso se corrige refatorando. O segundo se resolve comentando. Confundir os dois é o que produz tanto o código poluído quanto o código mudo.

## A assimetria da verificação

O comentário é o único artefato do repositório sem checagem mecânica, e é isso que o torna perigoso. Tudo o mais quebra quando diverge da realidade: um tipo errado não compila, um teste desatualizado falha, uma chamada com aridade errada explode. O comentário não quebra. Ele mente em silêncio, indefinidamente, com a mesma aparência de autoridade que tinha no dia em que era verdade.

Isso cria uma assimetria prática de custo. Um comentário correto economiza alguns minutos de quem lê. Um comentário desatualizado custa horas, porque o leitor confia nele, parte de uma premissa falsa e só descobre o erro depois de depurar o sintoma errado. O valor esperado de um comentário, portanto, depende fortemente da probabilidade de ele continuar verdadeiro.

E essa probabilidade varia muito conforme o tipo de comentário:

| Tipo de informação | Acopla-se a quê | Risco de apodrecer |
| --- | --- | --- |
| Paráfrase da linha de código | A cada caractere do código | Altíssimo |
| Descrição de assinatura | À assinatura | Alto |
| Invariante local | À implementação próxima | Médio |
| Razão da escolha, trade-off | À decisão, não ao código | Baixo |
| Restrição externa (hardware, protocolo, sistema vizinho) | Ao mundo fora do repositório | Baixo |
| Número medido com data e contexto | A uma medição datada | Baixo, se datado |

O padrão é claro: **quanto mais próximo o comentário está do que o código já diz, mais rápido ele apodrece.** A paráfrase quebra a cada edição. A razão da escolha sobrevive a refatorações inteiras, porque descreve algo que continua verdadeiro mesmo quando cada linha ao redor mudou.

Disso saem duas consequências que orientam todo o resto deste documento:

1. **Comentário é último recurso, nunca primeira opção.** Antes de escrever um, esgote as formas verificáveis de expressar a mesma coisa.
2. **Comentário que sobrevive é comentário que não depende dos detalhes do código.** Escreva no nível da decisão, não no nível da instrução.

## Comentários que devem ser deletados

Todo comentário que descreve o que o código faz é duplicação não verificada e deve sair. Abaixo, os padrões recorrentes, em qualquer linguagem.

**Paráfrase da linha.** O comentário repete a instrução em português.

```
// incrementa o contador
++count;
```

Não há informação nova. Delete.

**Paráfrase da função.** O comentário descreve o comportamento que o nome deveria descrever.

```
// retorna true se o usuário é maior de idade
bool check(int a) { return a >= 18; }
```

Isso não pede comentário, pede `is_adult(age)`. Comentários dessa família são sintoma de que uma extração de função, uma constante nomeada, um enum ou um tipo não foi feito. O comentário está compensando um nome ruim — corrija o nome.

**Redundância de assinatura.** Blocos de documentação que repetem o que o sistema de tipos já declara (`@param int $x` acima de um parâmetro já tipado como `int`, `@returns {string}` acima de um retorno já tipado) são duplicação pura em qualquer linguagem com tipos na assinatura. O verificador checa a assinatura e ignora o comentário; quando os dois divergem, o comentário é que está errado, e ninguém avisa.

**Código comentado.** O histórico de versões existe exatamente para isso. Código comentado não compila, não é testado, não é refatorado junto com o resto e envelhece até ninguém mais saber se ainda faz sentido. Delete sempre.

**Ruído cerimonial.** Cabeçalhos com nome do autor e data de criação (o histórico sabe), divisórias de ASCII separando seções de um arquivo (se o arquivo precisa de divisórias, ele precisa ser dividido em arquivos), `// fim do if`, `// construtor`, changelog inline no topo do arquivo.

**Marcadores vazios.** `// NOTE:`, `// IMPORTANTE:` ou `// TODO` sem conteúdo acionável — sem o que fazer, sem por quê, sem referência a um ticket. Um TODO que ninguém consegue executar é só uma confissão de dúvida.

**O teste comum a todos:** se apagar o comentário não remove nenhuma informação do repositório, ele não era informação — era eco.

## Comentários que devem existir

São os que registram informação que não existe em nenhum outro lugar do arquivo. Apagá-los é perda de informação, não limpeza.

**Razão da escolha.** O código mostra a solução adotada e esconde as rejeitadas. Quando a alternativa óbvia foi descartada por um motivo não óbvio, esse motivo precisa estar ali — senão a próxima pessoa reintroduz a alternativa achando que está melhorando o código.

```
// Processamento sequencial de propósito: a versão paralela foi testada e
// perdeu, porque o custo de sincronização domina para lotes < 10k itens.
```

**Número medido.** Toda constante calibrada empiricamente carrega uma medição que o valor sozinho não comunica. Registre o número, a condição em que foi medido e quando — um número sem data é um número sem validade.

```
// 64 = ponto de saturação medido em 2026-03; acima disso a contenção de lock
// anula o ganho. Refazer a medição se o pool de workers mudar.
```

**Código que parece pior do que a alternativa ingênua.** Otimização costuma ser deliberadamente menos legível: uma estrutura decomposta em arrays paralelos, um laço desenrolado, padding para alinhamento, uma busca linear onde caberia um mapa. Sem o registro da medição, alguém "limpa" a estrutura, volta para a forma legível e causa uma regressão de desempenho que nenhum teste pega — perda de desempenho é justamente a classe de regressão que a integração contínua não detecta.

**Restrição externa.** Limites de hardware, errata de processador, comportamento de um protocolo, um teto imposto por um serviço vizinho, uma exigência regulatória. São fatos que vivem fora do repositório e que nenhum artefato local pode expressar.

**Invariante não local.** Uma propriedade que precisa valer entre partes distantes do sistema e que nenhum tipo local captura: a ordem exigida de um vetor porque outro módulo faz busca binária nele, o pareamento entre uma escrita e uma leitura em código concorrente, a ordem obrigatória de duas chamadas. Quando possível, transforme a invariante em asserção executável; quando não for possível, comente.

**Workaround.** Um desvio causado por bug de terceiro, versão específica de dependência ou comportamento divergente entre plataformas. Inclua a referência ao bug e a condição de remoção — um workaround sem critério de saída vira permanente por omissão.

```
// Workaround para o bug #4821 da lib X (quebra em 2.1.0, corrigido em 2.3.0).
// Remover quando o piso de versão subir para 2.3.
```

**Violação deliberada de convenção.** Quando o código quebra de propósito um padrão do projeto, o comentário é o que separa "decisão" de "descuido". Sem ele, o leitor precisa escolher entre consertar algo que não está quebrado e deixar passar algo que está.

## O teste de decisão

Antes de escrever um comentário, pergunte se a informação pode ser movida para dentro do código. Se puder, mova — você troca uma frase não verificada por uma declaração que alguma ferramenta checa. Se não puder, o comentário é o único lugar onde essa informação existe.



As formas de internalizar, na ordem em que valem a pena tentar:

1. **Renomear.** O caso mais comum. Um nome preciso dispensa a frase que o explicava.
2. **Extrair função ou método.** Um bloco que precisava de um comentário introdutório vira uma função com nome, e o comentário vira a assinatura.
3. **Nomear a constante.** `86400` com um comentário ao lado vira `SEGUNDOS_POR_DIA`.
4. **Fortalecer o tipo.** Trocar primitivos por tipos específicos elimina comentários sobre unidade, faixa e significado — um tipo próprio para milissegundos torna impossível passar segundos por engano.
5. **Usar enum em vez de valores mágicos.** Os estados possíveis passam a ser enumeráveis pelo compilador, não listados num comentário.
6. **Escrever uma asserção.** Uma invariante verificada em execução vale mais que a mesma invariante escrita em prosa.
7. **Escrever um teste.** Um teste com nome descritivo documenta comportamento e falha quando o comportamento muda.

Quando nenhuma dessas formas serve, não é falha sua: é um limite da linguagem. Aí o comentário deixa de ser poluição e passa a ser o único veículo possível.

## API pública é outra categoria

Documentação de interface pública não é comentário de implementação — é contrato, e as regras acima não se aplicam a ela. A diferença é o leitor: o comentário de implementação é lido por quem abre o arquivo; a documentação de API é lida por quem **nunca vai abrir o arquivo**.

Isso muda tudo. Quem chama sua função vê a documentação no editor, na página gerada, no autocompletar, no `help()` do interpretador. Para essa pessoa, a documentação não é um acréscimo ao código — ela é a única superfície visível. Ser contra isso não é ser contra poluição, é ser contra documentar a API.

O que pertence a esse contrato e não cabe na assinatura:

- **Semântica de erro.** O que a função lança, em que condições, e o que o chamador deve fazer a respeito.
- **Contrato de nulidade e vazio.** O que significa um retorno vazio: ausência, erro silencioso, resultado legítimo.
- **Efeitos colaterais.** Escrita em disco, chamada de rede, mutação de argumento, uso de estado global.
- **Pré-condições e pós-condições.** O que precisa valer antes e o que passa a valer depois.
- **Garantias de desempenho e concorrência.** Complexidade assintótica quando ela faz parte do contrato, segurança para uso concorrente, reentrância.
- **Propriedade de recursos.** Quem libera o quê — crítico em linguagens com gerenciamento manual de memória.
- **Exemplo de uso**, quando a forma correta de chamar não é óbvia pela assinatura.

Duas restrições de bom senso continuam valendo. Primeiro, documente a **interface pública**, não cada função privada: documentar tudo produz o mesmo ruído que documentar nada, e esconde o que importa. Segundo, não repita a assinatura — se o tipo já diz, a documentação deve dizer o que o tipo não diz.

## Arquivos de configuração

Configuração é o terreno onde a tese do "código autoexplicativo" falha de forma mais evidente, porque ali não há código. Não há função para extrair, não há tipo para fortalecer, não há teste que explique a escolha. Há apenas pares de chave e valor, e o valor é arbitrário até alguém dizer de onde veio.

A chave `max_connections` já tem o melhor nome possível. Ainda assim, `64` não se explica: o número veio de uma medição, de um limite de infraestrutura, de um acordo com um sistema vizinho ou de um chute que ninguém revisitou — e a diferença entre esses quatro casos é exatamente o que decide se alguém pode mexer nele.

Configurações concentram justamente a informação de menor densidade sintática e maior risco: números calibrados, tetos impostos por terceiros, acoplamentos invisíveis entre serviços, valores que só fazem sentido em um ambiente específico.

**A resposta correta tem duas partes, e só uma delas é comentário.**

Primeiro, mova a *descrição do campo* para um schema validado na carga — JSON Schema com descrições, um validador de esquema tipado, um tipo de configuração com valores padrão explícitos. Isso é internalizar no sentido da seção anterior: transforma documentação em algo verificável, que falha na inicialização em vez de falhar em produção. Ganhos diretos: tipo, faixa válida, obrigatoriedade e valor padrão deixam de ser prosa.

Segundo, aceite que o schema cobre *o que o campo é* e não cobre *por que este valor*. Essa parte continua sendo comentário, e deve registrar origem e condição de mudança:

```
# 64 = teto do pool do serviço de banco em produção. Aumentar aqui sem
# aumentar lá faz a aplicação enfileirar em vez de falhar rápido.
max_connections = 64
```

Duas notas práticas. JSON puro não aceita comentário algum — é uma das razões pelas quais ecossistemas migraram para formatos que aceitam (TOML, YAML, variantes comentáveis de JSON) em configuração destinada a humanos; quando o formato é JSON estrito, a razão precisa ir para um arquivo vizinho de documentação ou para o schema. E valores que mudam por ambiente pedem a razão no arquivo de origem, não replicada em cada ambiente.

## Objeções comuns

**"Isso está no histórico de versões."** Está, e mesmo assim não chega a quem precisa. O histórico degrada: reformatação em massa, renomeação de arquivo, movimentação de função entre módulos e mudanças de estilo apagam a trilha. Mais importante que a degradação técnica é a comportamental — quase ninguém investiga o histórico antes de deletar três linhas que parecem feias. A proximidade física é o que faz a informação chegar no momento da decisão. O histórico é um arquivo morto consultável; o comentário é um aviso no local do risco.

**"Está documentado na decisão de arquitetura."** Documentos de decisão servem para escolhas estruturais: qual banco, qual protocolo, qual fronteira entre serviços. Eles não servem, e não escalam, para explicar por que esta linha tem um ajuste de um ou por que esta constante vale 64. Granularidade errada. Quando existe um documento relevante, a prática certa é um comentário curto que aponta para ele, não a ausência de comentário.

**"Código bom não precisa de comentário."** Verdadeiro para o que o código consegue dizer, falso para o resto. Essa objeção trata "o que o código faz" como se fosse toda a informação relevante, quando a informação cara é tipicamente a outra: a razão, a medição, a restrição. Nenhuma qualidade de código faz aparecer no arquivo uma alternativa que foi testada e descartada.

**"Comentário é desculpa para código ruim."** Frequentemente é, e essa é a parte legítima da objeção. A resposta correta é a disciplina do teste de decisão: se dá para internalizar, internalize — o comentário nunca deve ser o caminho mais fácil para evitar uma refatoração. Mas a existência do abuso não invalida o uso.

**"Comentários ficam desatualizados, então é melhor não ter."** O argumento prova demais. Testes também ficam desatualizados, e a conclusão não é abandonar testes. A conclusão correta é escrever comentários que apodrecem devagar (razão, não paráfrase) e tratá-los como parte do código na hora de alterar e de revisar.

**"A pessoa pode perguntar para quem escreveu."** Não pode. Em dois anos, quem escreveu ou não está mais no projeto ou não lembra. Código é lido muito mais vezes do que é escrito, e quase sempre por alguém sem acesso ao contexto original — inclusive você mesmo.

## Manutenção e revisão

Um comentário é parte do código que ele acompanha: alterar um sem alterar o outro é deixar o arquivo em estado inconsistente. Essa é a única regra de manutenção que importa, e ela precisa valer na revisão, não na boa vontade.

**Como escrever para durar**

- Escreva no nível da decisão, não da instrução. "Sequencial porque a sincronização domina abaixo de 10k itens" sobrevive a refatorações; "itera sobre a lista" não sobrevive à próxima edição.
- Date o que é medição. Um número sem data não tem como ser auditado nem invalidado.
- Dê critério de saída ao que é temporário. Workaround sem condição de remoção vira permanente por omissão.
- Prefira apontar a duplicar. Referencie o ticket, o documento ou o relatório de bug em vez de copiar o conteúdo deles.
- Posicione no ponto de risco. O comentário protege o trecho que alguém vai querer "limpar" — ele precisa estar lá, não no topo do arquivo.

**Na revisão de código**

- [ ] Todo comentário que descreve o que o código faz foi removido ou convertido em nome, tipo, constante ou teste?
- [ ] Todo comentário restante sobreviveria a uma reescrita da implementação?
- [ ] Os comentários tocados por esta alteração continuam verdadeiros?
- [ ] Código otimizado, estrutura não óbvia ou violação deliberada de convenção estão justificados?
- [ ] Números calibrados trazem origem e data?
- [ ] Workarounds trazem referência e condição de remoção?
- [ ] A interface pública nova ou alterada documenta erros, efeitos colaterais e pré-condições?
- [ ] Sobrou código comentado, divisória, cabeçalho de autoria ou marcador vazio?

**Regra de bolso para quem vai deletar um comentário:** se você não consegue dizer onde mais aquela informação está registrada, você não está limpando o arquivo — está apagando a única cópia.