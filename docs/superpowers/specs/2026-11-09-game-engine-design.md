# Game engine sobre o ECS — design

Data: 2026-11-09. Evolui o pacote descrito em `2026-10-16-ecs-archetype-spec.md` (requisitos `R-*`, invariantes
`I-*`, metas `NF-*` continuam valendo para o ECS) e aproveita a infraestrutura de build WASM de
`2026-11-06-wasm-cost-study-design.md`.

Revisão de 2026-11-10 (antes da fase 2): a engine passa a ser completa em C++, com o TypeScript como camada fina;
o loop sai do TS e vai para o C++ (§1, §2, §4, §4.3, §5, §7, §8). Nomes seguem a modularização
(`2026-11-09-oly-modularizacao-design.md`): prefixo `oly`, `OLY_ASSERT`, módulos em `engine/runtime/<m>/`.

Revisão de 2026-11-10 (antes da fase 3): §4.4 detalhado para os eventos — handlers recebem lotes por tipo,
despacho tipo a tipo dentro de cada rodada da cascata, fluxo de referência da morte de um inimigo, e o jogo (não a
engine) decide quando destruir a entidade. Medição da fase 3 no §7.

Revisão de 2026-11-11 (antes da fase 4): §4.5 detalhado para as ações de input — o host empurra eventos brutos,
botões físicos são usages do USB HID, ações são declaradas no esquema, módulo `input` próprio, sem interface de
plataforma (§4.3). Testes da fase 4 no §5 e ausência de benchmark justificada no §7.

Convenção deste documento: o que não vem marcado foi decidido pelo autor do projeto. Itens marcados
**[decisão do design]** foram escolhas feitas ao escrever o documento, sem confirmação explícita; podem ser revistas
no plano da fase correspondente sem voltar ao autor, desde que respeitem os objetivos e anti-objetivos.

## 1. Intenção

Transformar o ECS por arquétipos numa **game engine reaproveitável para jogos 2D no navegador**, com:

- um **núcleo em C++** (o ECS atual mais a infraestrutura da engine), compilado para WASM. **O núcleo é a engine
  inteira**: loop, fases, passo fixo, pausa, eventos, ações, schedule e estados vivem nele;
- um **complemento em TypeScript**, que é por onde o jogo usa a engine no navegador. É uma **camada fina**: carrega o
  WASM, repassa ao núcleo o tempo e os sinais do navegador, registra sistemas TS e dá acesso sem cópia aos
  componentes. Não contém lógica de engine que falte ao C++.

Motivo (do autor): quem quiser escrever um jogo só em C++, e no futuro rodá-lo em outro ambiente que não o navegador,
não pode encontrar a engine incompleta porque parte dela foi escrita em TypeScript.

Um sistema é um sistema, independente da linguagem: o jogo pode escrever cada sistema em C++ ou em TypeScript, e os
dois tipos convivem nas mesmas fases. Isso faz da **fronteira WASM↔JS uma peça central do design**, não um detalhe:
o acesso de sistemas TS aos componentes é feito por views sem cópia sobre a memória do WASM.

O projeto continua sendo também um projeto de estudo: custos de abstração e da fronteira são medidos, no mesmo
estilo das metas do ECS (razões contra uma linha de base da mesma execução).

### 1.1 Jogo de validação

Um jogo de tiro 2D com visão de cima, no navegador:

- o jogador tem uma arma com munição infinita, mira com o mouse e pode usar **dash** (com recarga);
- moedas aparecem pelo mapa e o jogador as coleta;
- inimigos perseguem o jogador para atacá-lo; o jogador foge e os mata;
- sequência de vida: menu → jogando → game over → jogar de novo.

O jogo é **cliente de validação** da engine: se algo de que ele precisa só puder ser feito com gambiarra fora da
engine (fora dos mecanismos de sistemas, eventos, ações, schedule e estados), a engine está incompleta. Render,
colisão e áudio do jogo são escritos no próprio jogo (§2).

## 2. Objetivos, não-objetivos, anti-objetivos

**Objetivos**
- Engine reaproveitável: nada no núcleo nem no pacote TS conhece conceitos do jogo de validação (inimigo, moeda, dash).
- Infraestrutura completa para: sistemas em fases fixas, mudanças estruturais seguras, eventos, ações de input,
  `schedule` de ações no tempo, ciclo de vida da aplicação e estados de jogo.
- Sistemas em C++ e em TypeScript com o mesmo modelo de uso.
- Engine completa em C++: um jogo só em C++ usa todos os mecanismos (loop, fases, pausa e, nas fases seguintes,
  eventos, ações, schedule e estados) pela API C++, sem depender de nada escrito em TS. O que é próprio do ambiente
  (tempo, quadro, visibilidade e, depois, input) entra no núcleo pelo **host** (§4.3), trocável.
- Acesso TS a componentes **sem cópia**, por bloco de arquétipo.
- O jogo de validação jogável no navegador, de ponta a ponta.
- Custo da fronteira medido: o mesmo sistema escrito em TS (via engine) e em C++, e contra uma linha de base TS manual.

**Não-objetivos (por enquanto)**
- Render, colisão/física, áudio, carregamento de assets, câmera, UI. Ficam com o jogo.
- Threads, Web Workers, `SharedArrayBuffer`.
- Rede/multiplayer, editor, hot reload, serialização/save.
- Executar fora do navegador como produto (um build nativo existe só para testar o núcleo C++, §6). A engine fica
  **pronta** para isso (núcleo completo e host trocável), mas nenhum host nativo de verdade (janela, input, SDL etc.)
  é entregue agora.
- Suporte garantido a múltiplos navegadores: o alvo de validação é um navegador baseado em Chromium.
- Gamepad (as ações são desenhadas para aceitá-lo depois; teclado e mouse bastam agora).

**Anti-objetivos** (falha mesmo que funcione)
- Cruzar a fronteira WASM↔JS **por entidade** em caminhos quentes (iteração de sistemas, emissão de eventos por
  sistemas C++). A fronteira é cruzada por bloco, por lote ou por sistema.
- O layout de um componente divergir entre C++ e TS. Existe uma única fonte (o esquema, §4.1).
- Regredir o ECS: presets, testes, invariantes e números registrados em `docs/benchmarks/README.md` continuam valendo.
- Conceitos do jogo de validação vazarem para a engine.
- Lógica de engine (decidir quando rodar passos, fases, pausa, eventos, schedule, estados) escrita só em TS. O TS
  pode ter conveniências de API, nunca comportamento que um jogo só em C++ não teria.
- Engine que só funciona com o jogo de validação (APIs moldadas para um caso só).

## 3. Restrições

- Toolchain só em Docker, como hoje. O caminho WASM usa a imagem Emscripten já fixada pelo estudo de custo
  (`emscripten/emsdk:3.1.74`, node 20.18). Ferramentas JS/TS (TypeScript, bundler, test runner) também rodam no
  container; o host não precisa de node.
- `-Wall -Wextra -Wpedantic -Werror` em todos os alvos C++; TypeScript em modo `strict`.
- Estilo e idioma do projeto (`CLAUDE.md`): commits, comentários e mensagens em pt-BR; nomes completos; camadas estritas.
- Camadas: a engine fica **acima** do `world` e só usa a API pública do ECS. O ECS não passa a conhecer a engine.
- **[decisão do design]** A API pública do ECS **pode ser estendida** com operações por `ComponentId` em tempo de
  execução, que o TS exige (as queries TS são montadas em tempo de execução): query type-erased por listas de IDs
  (`read`/`write`/`with`/`without`) com acesso a ponteiro de coluna e `count` por tabela, e `add`/`remove` por ID no
  `CommandBuffer` (o `World` já tem `addRaw`/`removeRaw`). As APIs template existentes passam a delegar para essas
  quando fizer sentido. Essa extensão é a primeira tarefa da fase 1 e vem com testes, `checkInvariants` e a
  reexecução dos benchmarks do ECS para provar que nada regrediu.
- Contratos de validade do ECS valem também no TS: qualquer mudança estrutural invalida ponteiros e spans;
  só `Entity` é handle estável.

## 4. Arquitetura

```
 jogo (TS e/ou C++)
 ─────────────────────────────────────────────────────────
 pacotes TS (camada fina)         │  núcleo C++ da engine (WASM ou nativo)
  boot (carrega o WASM)           │   loop: frame(agora) → passos fixos + render
  host do navegador: rAF,         │   fases, pausa, CommandBuffer por fase
   visibilitychange, input        │   eventos, schedule, estados (fases 3–6)
  views sobre colunas             │   API C exportada (extern "C")
  sistemas/handlers TS            │   sistemas/handlers C++
   (chamados de volta pelo C++)   │
 ─────────────────────────────────────────────────────────
 ECS (component → entity → table → archetype → query/command_buffer → world)
```

### 4.1 Esquema único de componentes e eventos

- Componentes e eventos são declarados num **esquema** de campos escalares (`f32`, `f64`, `i32`, `u32`, `u8`, `entity`
  etc.) e tags (componentes sem dados). Exemplo indicativo:
  `component("Position", { x: "f32", y: "f32" })`, `tag("Enemy")`, `event("EnemyDied", { entity: "entity", x: "f32", y: "f32" })`.
- Um gerador produz, a partir do esquema, o header C++ (structs triviais) e os descritores TS (nome, tamanho,
  alinhamento, campos com tipo e offset). Ele também verifica, no build, que `sizeof`/`alignof`/`offsetof` do C++
  batem com o descritor TS (ex.: `static_assert` gerados).
- **[decisão do design]** O esquema é escrito em TypeScript (ou JSON lido por um script node); o formato exato e a
  linguagem do gerador ficam com o construtor.
- Consequência: o WASM é compilado **por jogo** (engine + esquema do jogo + sistemas C++ do jogo). A engine é
  distribuída como biblioteca C++ + pacote TS + gerador, e o build do jogo os junta.
- Componentes que não cabem no esquema (strings, objetos JS, referências a imagens) vivem num armazenamento do lado TS
  indexado por `Entity`, fora do `World`. **[decisão do design]** Esse armazenamento é simples (mapa por tipo) e não é
  consultável por queries; sua limpeza acompanha a destruição da entidade.

### 4.2 Fronteira e acesso sem cópia

- O núcleo expõe uma **API C** (`extern "C"`, exportada pelo Emscripten); não usar embind. **[decisão do design]**
- Um sistema TS declara uma query (`read`, `write`, `with`, `without`) e itera com `eachBlock`: um callback por
  tabela não vazia, recebendo `count` e views tipadas sobre as colunas na memória linear do WASM. A fronteira é
  cruzada uma vez por bloco (obter ponteiros de coluna e `count`), nunca por entidade.
- **Layout das colunas: [decisão do design, importante]** a `Table` atual guarda cada componente como uma coluna de
  structs (`Position` = `x0 y0 x1 y1 …`), ou seja, SoA entre componentes e AoS dentro do componente. O design
  **mantém esse layout**. A view de um campo é um typed array sobre a coluna inteira com passo
  (`x[row] = coluna[row * passo + offsetDoCampo]`, em elementos do tipo do campo); o código gerado esconde o passo
  atrás de acessores. Separar os campos em colunas próprias (SoA por campo) mudaria a `Table` e a API C++ das queries,
  e fica como estudo futuro, motivado pelos benchmarks da fronteira.
- Alinhamento: como os structs são triviais e alinhados naturalmente, todo campo escalar tem offset múltiplo do seu
  tamanho e o tamanho do struct é múltiplo do alinhamento, então o typed array com passo é sempre válido.
- **Validade das views:** só dentro do callback de `eachBlock`. Crescimento da memória do WASM (`ALLOW_MEMORY_GROWTH`)
  desanexa o `ArrayBuffer`, e mudanças estruturais movem linhas; por isso views nunca são guardadas entre chamadas.
  Em build de debug, a engine detecta uso de view fora do callback quando for barato fazê-lo.
- `Entity` no TS: **[decisão do design]** representado como `number` (índice + geração empacotados de forma segura
  dentro de 53 bits, ou dois campos); a escolha fica com o construtor desde que comparar e guardar seja barato.
- Acesso pontual (`get(entity, Component)`, `single(...)`) existe para casos como "a posição do jogador"; cruza a
  fronteira por chamada e não deve ser usado em laço por entidade.

### 4.3 Loop e fases

**Quem conduz o loop.** O loop é do **núcleo C++**. Um módulo C++ novo (nome sugerido `app`, em
`engine/runtime/app/`, acima de `ecs`; **[decisão do design]** o nome) guarda o `World`, as fases e a ordem dos
sistemas, o relógio do jogo, o acumulador do passo fixo, o estado de pausa e o `CommandBuffer` por fase.

- **O host conduz o tempo; o núcleo decide o resto.** O host é quem conhece o ambiente. Ele chama `frame(agora)` uma
  vez por quadro (tempo em segundos, monotônico) e avisa quando o jogo fica oculto/visível (`setHidden`). O núcleo
  acumula o tempo, roda os passos fixos que couberem e a fase `render`. Hosts:
  - navegador: o pacote `@oly/client`, com `requestAnimationFrame` e `visibilitychange`, chamando a API C;
  - testes: um relógio falso, tanto no C++ nativo (doctest) quanto no TS em node (pela API C do WASM);
  - outros ambientes: ficam para depois (§2); a engine não pode depender de nada do navegador.
  Na fase 2, `frame` e `setHidden` bastam e **não** há interface de plataforma. Na fase 4 o input também chega
  por chamadas do host (eventos brutos empurrados, §4.5), então continua sem interface de plataforma.
  **[decisão do design]**
- **Sistemas C++** são funções registradas no núcleo e chamadas direto, sem fronteira. Um jogo só em C++ os registra
  pela API C++ (`addSystem(fase, nome, função)`). Para que o TS possa colocá-los na ordem das fases, sistemas C++ do
  jogo também podem ser registrados por **nome** com uma macro (ex.: `OLY_SYSTEM(move)`), e o TS os adiciona por esse
  nome (`addNativeSystem`).
- **Sistemas TS** são registrados pelo TS, que recebe um id do núcleo. Na hora de rodar, o núcleo chama o TS de volta
  por **uma única função JS importada** pelo módulo WASM, que escolhe o sistema pelo id (sem `addFunction` nem tabela
  de funções que cresce). **[decisão do design]** A fronteira é cruzada uma vez por sistema TS por passo, o que o
  anti-objetivo permite (por sistema, não por entidade). O mesmo mecanismo serve, nas fases seguintes, para handlers
  de evento, callbacks do schedule e ganchos de estado escritos em TS.
- Contrato da chamada de volta: enquanto um sistema TS roda, o núcleo não guarda ponteiros para dentro do `World`
  nem do heap através da chamada, porque o TS pode alocar no WASM (crescimento de memória) e mudar estrutura pelo
  `CommandBuffer`. Exceções lançadas por um sistema TS sobem para quem chamou `frame` e deixam a engine num estado
  definido (documentado no plano), sem corromper o `World`.

**Ciclo de vida da aplicação** (controlado pela engine, igual para qualquer jogo):
- `boot`: no navegador, o TS carrega o WASM; o núcleo cria o `World` e registra o esquema. Um jogo só em C++ cria a
  engine direto.
- `run`: o host passa a chamar `frame(agora)`. Gameplay em **passo fixo** (**[decisão do design]** 60 Hz por padrão,
  configurável), acumulando o tempo real entre chamadas, com um limite de passos por quadro para evitar espiral de
  atraso (o excedente é descartado). Render uma vez por `frame`, recebendo o fator de interpolação entre passos.
- `pause`/`resume`: automático quando a aba fica oculta (`visibilitychange`) e disponível ao jogo
  (`engine.pause()`/`engine.resume()`, também pelo `context`). Pausado:
  - o relógio do jogo e o `schedule` congelam, e os sistemas de gameplay não rodam;
  - os passos fixos continuam acontecendo só para **capturar as ações** (§4.5) e rodar os sistemas registrados com
    `.whilePaused()` (ex.: um sistema que lê a ação `pause` e chama `resume`, ou o menu de pausa), seguidos do
    despacho de eventos e da transição de estado pendente;
  - o `render` continua.
  A pausa por aba oculta é independente da pausa do jogo: a aba volta e o jogo retoma sozinho, a menos que o jogo
  também tenha pausado. Durante a aba oculta nenhum passo roda (o navegador para o `requestAnimationFrame`; o núcleo,
  avisado por `setHidden`, ignora `frame`), e o tempo acumulado é descartado ao voltar. Essa regra vive no núcleo;
  o host só repassa o sinal.
- `shutdown`: libera o `World` e os recursos da engine.

**Detalhes do loop** (**[decisão do design]**, revisáveis no plano da fase 2):
- `pause()`/`resume()` pedidos no meio de um passo valem a partir do próximo passo fixo.
- A fase `render` roda pausada ou não; `.whilePaused()` só se aplica às fases de gameplay.
- Pausado, o relógio do jogo congela, mas sistemas `.whilePaused()` recebem o `fixedDeltaTime` normal.
- Dentro de sistemas (C++ e TS), o contexto só oferece mudanças estruturais pelo `CommandBuffer` da fase; acesso
  estrutural direto ao `World` fica fora do loop. O `CommandBuffer` também é aplicado ao fim da fase `render`.
- O primeiro `frame`, e o primeiro depois da aba oculta, só registram o tempo: o acumulador começa em zero.
- Dependências: `app` fica acima de `ecs`; `bridge` passa a depender de `app` para expor o loop na API C. Pode haver
  mais de uma engine por instância do WASM; a chamada de volta única recebe qual engine chamou.
- Exceção em sistema TS: o C++ compila sem exceções, então um erro JS atravessa o WASM sem rodar destrutores. A
  engine marca o passo como interrompido, o `frame` seguinte se recupera, e isso tem teste.

**Fases fixas.** Cada sistema se registra em exatamente uma fase. As fases do gameplay são `updateBefore`, `update`,
`updateAfter`. Ordem de um passo fixo (**[decisão do design]** a ordem dos itens que não são fases do gameplay):

1. captura das ações de input (§4.5)
2. disparo das ações agendadas vencidas (§4.6)
3. `updateBefore` → `update` → `updateAfter`
4. despacho de eventos (§4.4)
5. transição de estado pendente, se houver (§4.7)

E, por quadro do `requestAnimationFrame`: fase `render` (uma só vez, após os passos fixos do quadro).

- Não existe fase de física: colisão e física do jogo se registram em `updateAfter`. **[decisão do design]**
- Dentro de uma fase, a ordem é a ordem de registro. **[decisão do design]** Não há grafo de dependências.
- Sistemas C++ e TS se misturam livremente na mesma fase, na ordem de registro; o núcleo percorre a fase e chama
  cada um (C++ direto, TS pela chamada de volta).
- **Mudanças estruturais dentro de sistemas** (`spawn`, `destroy`, `add`, `remove`) são adiadas pelo `CommandBuffer`
  do ECS, aplicado **ao fim de cada fase**. Sistemas C++ e TS escrevem no **mesmo** `CommandBuffer` da fase; o TS
  chega a ele pela API C (operações por `ComponentId`). **[decisão do design]** **[decisão do design]** `spawn` devolve a `Entity` na hora (o ECS já
  reserva o índice). Alterar valores de componentes é direto.
- Um sistema pode ser restrito a estados (`inState("playing")`, §4.7); fora deles não roda.
- Um sistema registrado com `.whilePaused()` roda **só** enquanto a engine está pausada pelo jogo; os demais, só
  enquanto não está. As duas restrições (estado e pausa) se combinam.

### 4.4 Eventos

Objetivo da fase (do autor): eventos escritos em C++ tanto quanto possível. TS entra só onde o navegador é preciso
(ex.: áudio). Um jogo só em C++ usa eventos inteiros pela API C++.

**Fluxo de referência** (aceito pelo autor como modelo; nomes são do jogo, não da engine):

1. Um sistema C++ de dano, em `updateAfter`, reduz a vida. Quando ela chega a zero, ele emite
   `EnemyDied { entity, x, y }` e **não** destrói a entidade.
2. No passo 4, os handlers de `EnemyDied` rodam na ordem de registro, cada um recebendo o lote de mortes do passo:
   `dropCoin` (C++, cria a moeda em `x, y`, pode emitir outro evento, que vira nova rodada da cascata),
   `addScore` (C++), `playDeathSound` (TS, único em TS porque o áudio é do navegador) e `destroyDead` (C++,
   registrado por último, destrói a entidade).
3. Enquanto os handlers rodam, o inimigo ainda está vivo e qualquer handler pode ler componentes dele. Ao fim do
   despacho, ele não existe mais.

Consequências para a engine:
- **Quem destrói é o jogo.** A engine garante a ordem dos handlers; "destruir no final" é registrar o handler que
  destrói por último. Não existe destruição automática associada a um evento.
- **A engine não tira eventos repetidos.** Duas balas que matam o mesmo inimigo no mesmo passo geram dois
  `EnemyDied` no mesmo lote; os handlers do jogo lidam com isso (ex.: `destroyDead` confere `isAlive`). Isso vale
  para qualquer handler que muda estrutura: uma entidade citada num evento pode já ter sido destruída por um handler
  anterior, de um lote anterior ou de uma rodada anterior.

Regras:
- Tipos de evento são declarados no esquema (§4.1) com `event(nome, campos)`, os mesmos tipos escalares dos
  componentes (inclusive `entity`); o gerador produz o struct C++ e o descritor TS, como para componentes. Eventos não
  são componentes: têm um id próprio e não entram no `World`. Cada tipo tem uma fila.
- **Declaração de tipos de evento na `Engine`, [decisão do design]:** o registro de tipos de evento (id denso,
  tamanho, alinhamento e nome de cada fila) vive no `app`, não no código gerado. Um tipo precisa ser declarado na
  `Engine` antes de ser emitido ou de receber handlers (algo como `engine.declareEvent<EnemyDied>()`; usar um tipo não
  declarado é erro de contrato), e a **ordem de declaração é a ordem de despacho**. Um jogo só em C++ declara os
  tipos à mão, sem esquema nem codegen; no caminho TS, o `bridge` declara os eventos do esquema na ordem do esquema.
  Assim a ordem de despacho é determinística e controlada pelo jogo nos dois caminhos.
- Sistemas (C++ ou TS) e handlers **emitem**; ninguém reage na hora da emissão.
- **Handler recebe um lote:** um handler é registrado para um tipo e é chamado **uma vez por lote**, com todos os
  eventos pendentes daquele tipo na ordem de emissão (C++: algo como `std::span<const EnemyDied>`; TS: views sobre a
  fila, com os mesmos acessores gerados dos blocos). **[decisão do design]** O handler percorre o lote; não há uma
  chamada por evento, nem em C++.
- **Ordem do despacho, [decisão do design]:** cada rodada tira um retrato de todas as filas não vazias e despacha
  **tipo a tipo**, na ordem de declaração na `Engine` (no caminho TS, a ordem do esquema); dentro de um tipo, cada handler na ordem de registro recebe o
  lote inteiro. Eventos emitidos durante a rodada vão para a rodada seguinte (não entram no lote em andamento, nem do
  mesmo tipo). Assim o lote que um handler está lendo nunca muda nem se move enquanto ele roda. A ordem global de
  emissão entre tipos diferentes **não** é preservada; isso fica documentado.
- O despacho acontece fora da iteração de queries, então **handlers fazem mudanças estruturais diretamente** (sem
  `CommandBuffer`), em C++ e em TS. Handlers podem iniciar queries e iterá-las; dentro dessa iteração valem as regras
  de sempre do ECS.
- **Handlers C++** se registram pela API C++ (`engine.on<EnemyDied>(nome, função)` ou equivalente) e, para que o TS
  possa encaixá-los na ordem, por nome com uma macro (ex.: `OLY_HANDLER(dropCoin, EnemyDied)`), que o TS adiciona
  com `addNativeHandler`, como `OLY_SYSTEM`/`addNativeSystem`. **Handlers TS** usam a mesma chamada de volta única dos
  sistemas TS (§4.3), uma vez por lote. Handlers C++ e TS de um mesmo tipo se misturam na ordem de registro.
- **Pausa, [decisão do design]:** handlers rodam em todo passo fixo, pausado ou não; não existe `.whilePaused()` para
  handlers. Pausado, só sistemas `.whilePaused()` rodam, então só eles (e seus handlers, em cascata) produzem eventos.
- **Cascata no mesmo passo:** eventos emitidos durante o despacho são despachados em seguida, rodada após rodada,
  até as filas esvaziarem. Existe um **limite de rodadas** (configurável no `EngineConfig`; **[decisão do design]**
  padrão 16); ultrapassá-lo é erro de contrato (`OLY_ASSERT` no C++, `ContractError` no TS antes de chegar ao
  assert, se o TS puder detectar; senão o assert), com mensagem que nomeia o(s) tipo(s) de evento ainda pendentes.
- As filas vivem na memória do WASM com o layout do esquema, para que sistemas C++ emitam sem cruzar a fronteira e
  handlers TS recebam um lote (views sobre a fila), não uma chamada por evento. **[decisão do design]**
- **Emissão a partir do TS, [decisão do design]:** pode cruzar a fronteira uma vez por evento (`context.emit(EnemyDied,
  {...})`); emitir de TS não é caminho quente do jogo de validação. Se o benchmark (§7) mostrar custo relevante, o
  construtor pode acumular no TS e enviar em lote, sem mudar a API.
- **Views de evento no TS e crescimento da memória:** um handler TS faz mudanças estruturais diretas, que podem
  alocar no WASM e desanexar o `ArrayBuffer`. As views do lote não podem ficar inválidas em silêncio no meio do
  handler: o construtor escolhe como (ex.: acessores que passam pela `Bridge` e se refazem após `sync()`, ou cópia
  do lote para o JS antes de chamar o handler), e a escolha tem teste que força crescimento de memória dentro de um
  handler. **[decisão do design]** o requisito; o mecanismo fica com o construtor.
- Contexto do handler: o mesmo tipo de acesso do `SystemContext` (tempo, pausa, `get`/`tryGet`/`has`/`isAlive`,
  queries, `emit`), mais mudanças estruturais **diretas** no lugar das adiadas. **[decisão do design]** Os nomes
  (`HandlerContext` ou outro) ficam com o construtor.
- **Exceção em handler TS:** mesma regra dos sistemas TS (§4.3): o passo é marcado como interrompido, e o `frame`
  seguinte descarta os eventos pendentes junto com os comandos pendentes e se recupera. O que handlers anteriores já
  fizeram fica.
- Eventos do passo são descartados após o despacho; não sobrevivem ao passo.
- Registro de handlers segue as regras do registro de sistemas: não pode acontecer durante `frame` (`OLY_ASSERT`).
- **Eventos emitidos fora das fases do gameplay** (**[decisão do design]**):
  - em ações agendadas (passo 2): entram na fila e são despachados no passo 4 do mesmo passo fixo;
  - em `onExit`/`onEnter` (passo 5): depois da transição, se houver eventos pendentes, a engine roda um novo despacho
    no mesmo passo fixo, com a mesma regra de cascata; as rodadas contam para o mesmo limite do passo;
  - na fase `render`: ficam na fila e são despachados no passo 4 do próximo passo fixo (render não é gameplay, mas
    perder um evento em silêncio seria pior).
- A limpeza de escopo de uma transição (§4.7) não emite eventos; a engine não tem eventos implícitos de destruição.

### 4.5 Ações de input ("comandos")

- O jogo declara **ações** e as liga a entradas: botão (`dash` → `Space`), eixo 2D (`move` → WASD), ponteiro
  (`aim` → posição do mouse em coordenadas do canvas). A tabela de ligações pode ser trocada sem mexer em sistemas.
- Sistemas consultam ações, nunca teclas: `pressed`, `held`, `released` para botões; vetor para eixos; posição para
  ponteiro.
- O estado é capturado **uma vez por passo fixo** e é o mesmo para sistemas C++ e TS (um snapshot escrito na memória
  do WASM). Um toque mais curto que um passo não pode se perder: `pressed` de um evento entre dois passos aparece no
  passo seguinte.
- Converter a posição do ponteiro para coordenadas do mundo é do jogo (não há câmera na engine).
- O `CommandBuffer` do ECS continua como mecanismo interno e não é exposto com esse nome pela engine.

**Módulo.** As ações vivem num módulo próprio, `input` (`engine/runtime/input/`, `DEPENDS core`, namespace
`oly::input`), que não conhece o ECS nem a `Engine`. O `app` passa a depender de `input`. Assim as regras de borda
são testáveis sem loop nem `World`, e o lint de fronteiras impede o input de depender do ECS. O `ActionMap` não é
uma interface de plataforma: o núcleo nunca pergunta nada ao host.

**Botões físicos.** Uma tecla ou um botão do mouse é um `PhysicalButton`: um `uint32_t` no formato de usage estendido
do USB HID, `(página << 16) | usage`. Teclado é a página `0x07` (`Key::W = 0x0007'001A`, `Key::Space =
0x0007'002C`), mouse é a página `0x09` (`MouseButton::Left = 0x0009'0001`, `Right = 0x0009'0002`, `Middle =
0x0009'0003`). `Key` e `MouseButton` são `enum class : uint32_t` com esses valores, convertidos implicitamente em
`PhysicalButton`. Os valores HID são os mesmos dos scancodes do SDL, então um host nativo futuro não precisa de tabela.
A identidade é a **posição física** da tecla (o `KeyboardEvent.code` do W3C, não o `key`): WASD fica no mesmo lugar em
qualquer layout de teclado.

**Entrada do host (eventos brutos empurrados).** O host chama, a qualquer hora (dentro ou fora de `frame`):
- `buttonDown(botão)` e `buttonUp(botão)`: entram numa fila de bordas, na ordem de chegada. Um `buttonDown` de um
  botão já pressionado (auto-repeat do teclado) é ignorado pelo núcleo; o host não filtra nada.
- `pointerMove(x, y)`: não entra na fila; só a última posição importa. Coordenadas em pixels do canvas, com `y`
  crescendo para baixo; a conversão das coordenadas do DOM é do host.
- `releaseAll()`: solta todos os botões pressionados (gera as bordas de soltura). O host chama no `blur` da janela,
  porque o `keyup` não chega quando o foco sai; a `Engine` chama em `setHidden(true)`.
A fronteira é cruzada uma vez por evento de input (dezenas por segundo), fora do caminho quente.
**[decisão do design]** A fila de bordas não tem limite: só cresce com eventos humanos e cada passo a esvazia.

**Captura (passo 1, em todo passo fixo, pausado ou não).** O núcleo consome a fila na ordem e monta o snapshot:
- **Botão.** Uma ação de botão tem zero ou mais ligações (ex.: `Space` e `ShiftLeft`). `held` é verdadeiro se algum
  botão ligado estiver pressionado no fim da captura; `pressed` é verdadeiro se a ação passou de solta a pressionada
  em algum momento da fila; `released`, o inverso. Um toque dentro de um passo dá `pressed` e `released` verdadeiros
  com `held` falso, e é assim que ele não se perde. Duas teclas na mesma ação: soltar uma enquanto a outra está
  pressionada não gera `released`.
- **As bordas valem por um passo.** Num `frame` com 3 passos, só o primeiro vê as bordas recebidas antes dele; num
  `frame` com 0 passos, a fila espera o próximo passo.
- **Eixo 2D.** Cada ligação tem 4 botões (`up`, `down`, `left`, `right`); pode haver várias (WASD e setas). O valor
  vem do estado pressionado no fim da captura: `x = right − left`, `y = down − up` (**[decisão do design]** `y` cresce
  para baixo, como no canvas, para `move` e `aim` usarem o mesmo sistema), teclas opostas se anulam e a diagonal é
  normalizada para comprimento 1. Eixo não tem bordas.
- **Ponteiro.** A última posição recebida e `valid`, falso até o primeiro `pointerMove`. Por enquanto o ponteiro não
  tem ligação: a fonte é o único ponteiro do host.
- **Ligações trocáveis a qualquer hora**, inclusive de dentro de um sistema (menu de configuração de teclas). A troca
  vale a partir da próxima captura, e o estado da ação é recalculado a partir dos botões físicos pressionados.

**API C++** (nomes normativos para o plano; detalhes de assinatura ficam com ele):

```cpp
struct ButtonAction  { ActionId id; };   // ids tipados: ler um eixo como botão não compila
struct Axis2dAction  { ActionId id; };
struct PointerAction { ActionId id; };

struct ButtonState   { bool pressed; bool held; bool released; };
struct Vector2       { float x; float y; };
struct PointerState  { float x; float y; bool valid; };
struct Axis2dBinding { PhysicalButton up, down, left, right; };

class ActionMap {
 public:
  ButtonAction  declareButton(std::string_view name);   // ActionId denso, na ordem de declaração
  Axis2dAction  declareAxis2d(std::string_view name);
  PointerAction declarePointer(std::string_view name);

  void bind(ButtonAction action, PhysicalButton button);
  void bind(Axis2dAction action, Axis2dBinding binding);
  void clearBindings(ActionId action);
  bool isBound(PhysicalButton button) const;

  void buttonDown(PhysicalButton button);
  void buttonUp(PhysicalButton button);
  void pointerMove(float x, float y);
  void releaseAll();

  void capture();

  ButtonState  button(ButtonAction action) const;
  Vector2      axis2d(Axis2dAction action) const;
  PointerState pointer(PointerAction action) const;
  std::span<const ActionState> snapshot() const;   // para a ponte
};
```

- A `Engine` guarda um `ActionMap`, exposto em `engine.actions()`; o host e o jogo declaram, ligam e entregam input
  pelo mesmo objeto. `captureActions()` (passo 1) chama `capture()`; `setHidden(true)` chama `releaseAll()`.
  `EngineContext` ganha `actions()`, então sistemas e handlers leem e religam pelo mesmo caminho
  (`context.actions().button(actions::dash).pressed`). **[decisão do design]** Sem leitores duplicados no contexto
  (`context.button(...)`), para a superfície do contexto não repetir a do `ActionMap`.
- Declarar ação durante `frame` é erro de contrato, como declarar evento. Ligar, entregar input e ler valem a qualquer
  hora.
- Um jogo só em C++ guarda o retorno de `declareButton` e liga com `engine.actions().bind(dash, Key::Space)`.

**Esquema e codegen.** Ações são declaradas no esquema, como os eventos: `action("dash", "button")`, com tipo
`"button"`, `"axis2d"` ou `"pointer"`, vai para `schema.actions` com `schemaIndex` na ordem. O nome é único entre tipos,
eventos e ações e precisa ser identificador C++ válido. O hash do esquema inclui as ações (nome e tipo), então um WASM
desatualizado dá `SchemaMismatchError`. O codegen gera `namespace actions { inline constexpr
oly::input::ButtonAction dash{0}; }` (o namespace evita colisão com nomes de componentes) e um `schemaActions()`
como o `schemaEvents()`; `olyEngineCreate` declara as ações do esquema na ordem do esquema, então o índice no esquema
é o `ActionId` nos dois lados e um nome errado num sistema nativo é erro de compilação. As **ligações** não entram no
esquema: são dados de tempo de execução.

**Snapshot e API C.** `ActionState` é `{ float x; float y; uint32_t flags; }` (12 bytes, um por ação, na ordem do
`ActionId`), com os bits `pressed`, `held`, `released` e `valid` em `flags`; o layout é conferido por `static_assert`.

```c
void olyEngineBindButton(OlyEngine*, uint32_t action, uint32_t button);
void olyEngineBindAxis2d(OlyEngine*, uint32_t action, uint32_t up, uint32_t down, uint32_t left, uint32_t right);
void olyEngineClearBindings(OlyEngine*, uint32_t action);
int32_t olyEngineIsBound(OlyEngine*, uint32_t button);
void olyEngineButtonDown(OlyEngine*, uint32_t button);
void olyEngineButtonUp(OlyEngine*, uint32_t button);
void olyEnginePointerMove(OlyEngine*, float x, float y);
void olyEngineReleaseAll(OlyEngine*);
const OlyActionState* olyEngineActionSnapshot(OlyEngine*);   // estável até olyEngineDestroy
```

O TS lê o snapshot por views da `Bridge` (refeitas após `sync()`), então ler uma ação não cruza a fronteira.

**`@oly/core`.**
- `action(name, kind)` devolve um handle tipado pelo tipo (`ButtonAction`, `Axis2dAction`, `PointerAction`):
  `context.actions.axis2d(dash)` é erro de tipo.
- Leitura em sistemas e handlers, com a forma do C++: `context.actions.button(dash)` → `{ pressed, held, released }`,
  `axis2d(move)` → `{ x, y }`, `pointer(aim)` → `{ x, y, valid }`. Cada leitura cria um objeto pequeno; são poucas
  por passo.
- Ligações: `engine.actions.bind(dash, "Space")`, `bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right:
  "KeyD" })`, `clearBindings(action)`. Os nomes são os do `KeyboardEvent.code` do W3C, mais `"MouseLeft"`,
  `"MouseRight"` e `"MouseMiddle"`, tipados como união literal (nome errado é erro de compilação). A tabela nome W3C →
  usage HID fica no `@oly/core` (só dados, sem DOM).
- Lado do host: `engine.actions.buttonDown(nome)`, `buttonUp(nome)`, `pointerMove(x, y)`, `releaseAll()`.

**`@oly/client`.** `attachBrowserInput(engine, canvas, environment?)` → `{ detach() }`, separado de `runInBrowser`
(loop e input mudam por motivos diferentes):
- teclado e `blur` na `window` (`blur` → `releaseAll`); `pointerdown` no canvas (clique fora dele não atira);
  `pointerup` e `pointermove` na `window` (soltar fora do canvas também chega);
- posição convertida para pixels do canvas com `getBoundingClientRect` e a razão `canvas.width / rect.width` (idem
  para a altura);
- códigos fora da tabela são ignorados;
- `preventDefault` só para botões ligados a alguma ação (`olyEngineIsBound`): `Space` não rola a página e o botão
  direito não abre o menu, mas `F5` e os atalhos do navegador continuam funcionando.

**Erros de contrato** (`OLY_ASSERT` no C++; no TS, `ContractError` antes de chegar ao assert): declarar ação durante
`frame` ou com nome repetido; `ActionId` fora do intervalo ou do tipo errado na API C (no C++ tipado, não compila);
botão com usage `0`; no TS, nome de botão fora da tabela e handle de ação de outro esquema.

**Tabela HID nos dois lados.** O enum C++ e a tabela TS copiam o mesmo padrão; um teste de cada lado confere as mesmas
amostras (letras, dígitos, setas, `Space`, modificadores, mouse) contra os valores do padrão.

### 4.6 Schedule

- Agendamento de ações no **relógio do jogo** (soma dos passos fixos não pausados): `after(segundos, callback)` e
  `every(segundos, callback)`, com handle para cancelar.
- Resolução de um passo fixo: uma ação vence no primeiro passo cujo tempo é ≥ o vencimento. Ações vencidas no mesmo
  passo rodam na ordem de vencimento e, em empate, de criação. `every` não acumula disparos atrasados além do que o
  limite de passos por quadro já permite. **[decisão do design]**
- Callbacks rodam no passo 2 do loop, fora de iteração, e podem fazer mudanças estruturais diretamente e emitir eventos.
- Callbacks em TS e em C++. **[decisão do design]**
- Exemplos do jogo: onda de inimigos a cada 5 s; dash disponível de novo após 0,8 s; moeda nova a cada 2 s.

### 4.7 Estados de jogo

- O jogo declara um conjunto plano de estados (ex.: `menu`, `playing`, `gameOver`) e o inicial. Não há estados
  aninhados nem pilha. **[decisão do design]**
- Ganchos `onEnter(estado)` e `onExit(estado)`; podem fazer mudanças estruturais e usar o `schedule`.
- Pedir transição (`context.setState("gameOver")`) não troca na hora: a troca acontece no passo 5 do loop
  (`onExit` do antigo, limpeza de escopo, `onEnter` do novo). Dois pedidos no mesmo passo: vale o último.
  **[decisão do design]**
- **Escopo:** entidades e ações agendadas criadas enquanto um estado está ativo (inclusive no `onEnter`) pertencem a
  ele por padrão e são destruídas/canceladas ao sair dele. Criar algo persistente exige pedir explicitamente.
  **[decisão do design]** Assim "jogar de novo" (`gameOver → playing`) começa limpo sem código de limpeza no jogo.
- Sistemas e handlers podem ser restritos a estados.

### 4.8 Uso indicativo

Esboço aceito como base de uso (nomes indicativos, não normativos):

```ts
const engine = await Engine.boot({ wasm: "game.wasm", fixedHz: 60 });
engine.states(["menu", "playing", "gameOver"], { initial: "menu" });
// move, aim, shoot e dash vêm do esquema: action("move", "axis2d"), action("aim", "pointer"), ...
engine.actions.bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });
engine.actions.bind(shoot, "MouseLeft");
engine.actions.bind(dash, "Space");
engine.onEnter("playing", (context) => {
  context.spawn([Player, Position, Velocity, Dash], { Dash: { ready: 1, speed: 600 } });
  context.schedule.every(5.0, spawnWave);
});
engine.addSystem(Phase.Update, "chasePlayer", chasePlayerSystem).inState("playing");
engine.addNativeSystem(Phase.Update, "move").inState("playing");
engine.on(EnemyDied, dropCoin);
engine.run();

function chasePlayerSystem(context: SystemContext) {
  const player = context.single(Position, { with: [Player] });
  context.query({ read: [Position], write: [Velocity], with: [Enemy] }).eachBlock((block) => {
    const position = block.read(Position);
    const velocity = block.write(Velocity);
    for (let row = 0; row < block.count; row++) {
      // acessores gerados escondem o passo da coluna (§4.2)
    }
  });
}
```

```cpp
OLY_SYSTEM(move) {
  const float deltaTime = context.fixedDeltaTime();
  context.query<Position, const Velocity>().each([&](Position& position, const Velocity& velocity) {
    position.x += velocity.x * deltaTime;
    position.y += velocity.y * deltaTime;
  });
}
```

## 5. Fases de implementação

Cada fase ganha um plano próprio em `docs/superpowers/plans/` quando começar. Cada fase termina com testes passando
(nativo e WASM onde se aplica) e não regride as anteriores nem o ECS.

| # | Fase | Entrega | Pronto quando |
|---|------|---------|---------------|
| 1 | **Ponte** | build WASM da engine com API C; pacote TS; esquema + gerador; spawn/destroy/get/set e `eachBlock` com views a partir do TS | um teste TS em node cria entidades, itera com views e o resultado bate com o lado C++; benchmark da fronteira (§7) registrado |
| 2 | **Loop e fases** | módulo C++ com o loop (`frame`, `setHidden`, passo fixo, render, pausa), registro de sistemas C++ (por função e por nome) e TS (chamada de volta) nas fases, `CommandBuffer` por fase compartilhado pelas duas linguagens; host do navegador em `@oly/client` (rAF + `visibilitychange`); passos 1, 2, 4 e 5 do loop como lugares vazios | um teste C++ nativo roda o loop inteiro sem TS, com relógio falso; sistemas das duas linguagens rodam na ordem esperada (teste TS em node contra o WASM); pausa, aba oculta e `.whilePaused()` testados; medições do §7 registradas |
| 3 | **Eventos** | `event` no esquema e no gerador; filas tipadas na memória do WASM; emissão nos dois lados; handlers C++ (por função e por nome) e TS por lote; despacho tipo a tipo com cascata e limite | teste C++ nativo do fluxo de referência (§4.4) sem TS, terminando com `checkInvariants` e o inimigo destruído; testes de ordem (tipo, registro, rodadas), cascata, estouro do limite (caso de morte no C++), evento duplicado, emissão na fase `render`, handlers C++ e TS misturados (teste TS em node contra o WASM), crescimento de memória dentro de handler TS e recuperação de exceção; medição do §7 registrada |
| 4 | **Ações de input** | módulo `input` (`ActionMap`, botões físicos HID, fila de bordas, captura no passo 1); `action` no esquema e no gerador; API C de ligações, entrada do host e snapshot; leitura pelos contextos em C++ e TS; `attachBrowserInput` em `@oly/client` | `input.unit` sem `Engine` cobrindo bordas, toque mais curto que um passo, várias ligações, auto-repeat, `releaseAll`, eixo (anulação e diagonal) e religação; no `app`, uma captura por passo (inclusive pausado), bordas só no primeiro passo do frame e sair da pausa por uma ação num sistema `.whilePaused()`; casos de morte novos; teste TS em node contra o WASM com sistema TS lendo ações, toque curto e saída da pausa; amostras da tabela HID iguais nos dois lados; `attachBrowserInput` testado com ambiente falso |
| 5 | **Schedule** | `after`/`every`/cancelar no relógio do jogo, congelando na pausa | testes de ordem de disparo e de pausa com relógio falso |
| 6 | **Estados** | estados, `onEnter`/`onExit`, `inState`, escopo de entidades e de ações agendadas | teste de `playing → gameOver → playing` sem sobras no `World` (`checkInvariants`) |
| 7 | **Jogo de validação** | o jogo de tiro em `examples/` (ou equivalente), com render em canvas e colisão simples feitos no jogo | jogável no navegador de ponta a ponta; lista do que precisou ser feito fora da engine, se algo |

Estados (6) depende do schedule (5) por causa do escopo das ações agendadas. Eventos, ações e schedule (3–5) dependem
só de 2 entre si; a ordem acima é a recomendada.

## 6. Testes e corretude

- Núcleo C++ da engine: testes doctest no estilo atual, rodando no preset nativo (`debug-asan`) e em WASM via node.
  Erros de contrato novos (cascata estourada, mudança estrutural direta durante iteração etc.) ganham casos em
  `contract_death`.
- `World::checkInvariants` continua sendo chamado nos testes após operações da engine que mudam estrutura.
- Pacote TS: testes em node no container (test runner a critério do construtor), contra o WASM real, não um mock.
- Loop, schedule e estados são testáveis sem navegador: relógio e fonte de input injetáveis.
- O jogo de validação é verificado manualmente no navegador; não se exige teste automatizado de navegador.

## 7. Medição

- Benchmark da fronteira (fase 1, repetido quando o loop existir): um sistema de movimento sobre N entidades em três
  versões, na mesma execução:
  1. C++ (sistema nativo);
  2. TS via engine (`eachBlock` + views com passo);
  3. TS manual (typed arrays próprios, sem engine) como linha de base.
  Reportar as razões (2)/(3) e (2)/(1) por tamanho, separando os que cabem em cache. **[decisão do design]** Sem meta
  numérica nesta etapa: os números são registrados e orientam a decisão sobre SoA por campo (§4.2).
- Custo do loop (fase 2), com K sistemas vazios e 0 entidades, na mesma execução:
  1. custo fixo de um passo com K sistemas C++ (linha de base) e com K sistemas TS: mede a chamada de volta C++→JS;
  2. K/2 sistemas C++ e K/2 TS **alternados** (`C,T,C,T…`) contra **agrupados** (`C…C,T…T`) na mesma fase: mede se
     intercalar linguagens custa mais que o número de chamadas de volta explica;
  3. o benchmark da fronteira (acima) repetido com os sistemas rodando dentro do loop.
  Sem meta numérica: os números dizem quanto custa ter o loop no C++ e orientam se vale agrupar sistemas TS.
- Custo dos eventos (fase 3, **[decisão do design]**), na mesma execução, para N eventos de um tipo por passo:
  1. um handler C++ contra um handler TS (custo da chamada de volta por lote e da leitura pelas views);
  2. emissão de N eventos por um sistema C++ contra por um sistema TS (custo da emissão pela fronteira).
  Sem meta numérica: os números dizem se vale emitir em lote a partir do TS.
- Ações de input (fase 4, **[decisão do design]**): sem benchmark. O input está fora do caminho quente: a fronteira é
  cruzada por evento de input (dezenas por segundo), a captura roda uma vez por passo sobre poucas ações e a leitura no
  TS é feita por views, sem cruzar a fronteira. Medir só faria sentido se um jogo tivesse centenas de ações.
- Ambiente ruidoso (WSL2/Docker): só razões da mesma execução, como no ECS.
- Resultados versionados em `docs/benchmarks/` com registro no `README.md` correspondente.

## 8. O que fica a critério do construtor

- Estrutura de diretórios do núcleo C++ da engine, do pacote TS e do exemplo; nome do pacote.
- Ferramentas TS (bundler, dev server, test runner) e a imagem/serviço Docker que as roda.
- Formato do esquema e linguagem do gerador (§4.1).
- Forma da função JS importada para a chamada de volta (`--js-library`, `EM_JS` ou equivalente) e da API C++ de
  registro de sistemas, desde que o loop fique no núcleo (§4.3).
- Representação de `Entity` no TS (§4.2).
- Detalhes da API C exportada e da API TS, desde que o uso fique próximo do esboço (§4.8).
- Ordem global vs por tipo no despacho de eventos (§4.4).
- Render e colisão do jogo de validação (canvas 2D é o suficiente).
