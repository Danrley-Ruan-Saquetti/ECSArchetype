# Game engine sobre o ECS — design

Data: 2026-11-09. Evolui o pacote descrito em `2026-10-16-ecs-archetype-spec.md` (requisitos `R-*`, invariantes
`I-*`, metas `NF-*` continuam valendo para o ECS) e aproveita a infraestrutura de build WASM de
`2026-11-06-wasm-cost-study-design.md`.

Convenção deste documento: o que não vem marcado foi decidido pelo autor do projeto. Itens marcados
**[decisão do design]** foram escolhas feitas ao escrever o documento, sem confirmação explícita; podem ser revistas
no plano da fase correspondente sem voltar ao autor, desde que respeitem os objetivos e anti-objetivos.

## 1. Intenção

Transformar o ECS por arquétipos numa **game engine reaproveitável para jogos 2D no navegador**, com:

- um **núcleo em C++** (o ECS atual mais a infraestrutura da engine), compilado para WASM;
- um **complemento em TypeScript**, que é por onde o jogo usa a engine no navegador.

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
- Acesso TS a componentes **sem cópia**, por bloco de arquétipo.
- O jogo de validação jogável no navegador, de ponta a ponta.
- Custo da fronteira medido: o mesmo sistema escrito em TS (via engine) e em C++, e contra uma linha de base TS manual.

**Não-objetivos (por enquanto)**
- Render, colisão/física, áudio, carregamento de assets, câmera, UI. Ficam com o jogo.
- Threads, Web Workers, `SharedArrayBuffer`.
- Rede/multiplayer, editor, hot reload, serialização/save.
- Executar fora do navegador como produto (um build nativo existe só para testar o núcleo C++, §6).
- Suporte garantido a múltiplos navegadores: o alvo de validação é um navegador baseado em Chromium.
- Gamepad (as ações são desenhadas para aceitá-lo depois; teclado e mouse bastam agora).

**Anti-objetivos** (falha mesmo que funcione)
- Cruzar a fronteira WASM↔JS **por entidade** em caminhos quentes (iteração de sistemas, emissão de eventos por
  sistemas C++). A fronteira é cruzada por bloco, por lote ou por sistema.
- O layout de um componente divergir entre C++ e TS. Existe uma única fonte (o esquema, §4.1).
- Regredir o ECS: presets, testes, invariantes e números registrados em `bench/results/README.md` continuam valendo.
- Conceitos do jogo de validação vazarem para a engine.
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
 jogo (TS + C++ opcional)
 ───────────────────────────────────────────────
 pacote TS da engine      │  núcleo C++ da engine (WASM)
  boot, loop, rAF, input  │   fases, eventos, schedule, estados
  views sobre colunas     │   API C exportada (extern "C")
  sistemas/handlers TS    │   sistemas/handlers C++
 ───────────────────────────────────────────────
 ECS (type_info → entity → table → archetype → query/command_buffer → world)
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

**Ciclo de vida da aplicação** (controlado pela engine, igual para qualquer jogo):
- `boot`: carrega o WASM, cria o `World`, registra o esquema.
- `run`: o loop. Gameplay em **passo fixo** (**[decisão do design]** 60 Hz por padrão, configurável), acumulando o
  tempo real do `requestAnimationFrame`, com um limite de passos por quadro para evitar espiral de atraso. Render uma
  vez por quadro do `requestAnimationFrame`, recebendo o fator de interpolação entre passos.
- `pause`/`resume`: automático quando a aba fica oculta (`visibilitychange`) e disponível ao jogo
  (`engine.pause()`/`engine.resume()`, também pelo `context`). Pausado:
  - o relógio do jogo e o `schedule` congelam, e os sistemas de gameplay não rodam;
  - os passos fixos continuam acontecendo só para **capturar as ações** (§4.5) e rodar os sistemas registrados com
    `.whilePaused()` (ex.: um sistema que lê a ação `pause` e chama `resume`, ou o menu de pausa), seguidos do
    despacho de eventos e da transição de estado pendente;
  - o `render` continua.
  A pausa por aba oculta é independente da pausa do jogo: a aba volta e o jogo retoma sozinho, a menos que o jogo
  também tenha pausado. Durante a aba oculta nenhum passo roda (o navegador para o `requestAnimationFrame`), e o tempo
  acumulado é descartado ao voltar.
- `shutdown`: libera o `World` e os recursos da engine.

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
- Sistemas C++ e TS se misturam livremente na mesma fase. A orquestração (quem chama quem) fica com o construtor;
  a restrição é não cruzar a fronteira por entidade. Sistemas C++ consecutivos podem rodar numa única chamada.
- **Mudanças estruturais dentro de sistemas** (`spawn`, `destroy`, `add`, `remove`) são adiadas pelo `CommandBuffer`
  do ECS, aplicado **ao fim de cada fase**. **[decisão do design]** `spawn` devolve a `Entity` na hora (o ECS já
  reserva o índice). Alterar valores de componentes é direto.
- Um sistema pode ser restrito a estados (`inState("playing")`, §4.7); fora deles não roda.
- Um sistema registrado com `.whilePaused()` roda **só** enquanto a engine está pausada pelo jogo; os demais, só
  enquanto não está. As duas restrições (estado e pausa) se combinam.

### 4.4 Eventos

- Tipos de evento são declarados no esquema (§4.1); cada tipo tem uma fila.
- Sistemas (C++ ou TS) e handlers **emitem**; ninguém reage na hora da emissão.
- No passo 4 do loop, a engine **despacha** cada fila para os handlers registrados para aquele tipo, em C++ ou TS.
  Ordem: a de emissão. **[decisão do design]** Quando há vários tipos pendentes, despachar na ordem global de emissão
  ou tipo a tipo fica com o construtor, desde que seja determinística e documentada.
- O despacho acontece fora da iteração de queries, então **handlers fazem mudanças estruturais diretamente** (sem
  `CommandBuffer`).
- **Cascata no mesmo passo:** eventos emitidos durante o despacho são despachados em seguida, rodada após rodada,
  até as filas esvaziarem. Existe um **limite de rodadas** (configurável); ultrapassá-lo é erro de contrato
  (`ECS_ASSERT` no C++, erro equivalente no TS), com mensagem que nomeia o tipo de evento.
- As filas vivem na memória do WASM com o layout do esquema, para que sistemas C++ emitam sem cruzar a fronteira e
  handlers TS recebam um lote (views sobre a fila), não uma chamada por evento. **[decisão do design]**
- Eventos do passo são descartados após o despacho; não sobrevivem ao passo.
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
engine.actions({ move: axis2d({ up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" }),
                 aim: pointer(), shoot: button("Mouse0"), dash: button("Space") });
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
ENGINE_SYSTEM(move) {
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
| 2 | **Loop e fases** | `boot`/`run`/`pause`/`shutdown`, passo fixo + render por rAF, registro de sistemas C++ e TS nas fases, `CommandBuffer` por fase | sistemas das duas linguagens rodam na ordem esperada; pausa e `.whilePaused()` testados; loop testável com relógio falso (sem navegador) |
| 3 | **Eventos** | filas tipadas pelo esquema, emissão nos dois lados, despacho com cascata e limite | testes de ordem, cascata e estouro do limite (caso de morte no C++) |
| 4 | **Ações de input** | declaração de ações e ligações, snapshot por passo visível em C++ e TS | testes com entrada simulada, incluindo toque mais curto que um passo e sair da pausa por uma ação |
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
- Ambiente ruidoso (WSL2/Docker): só razões da mesma execução, como no ECS.
- Resultados versionados em `bench/results/` com registro no `README.md` correspondente.

## 8. O que fica a critério do construtor

- Estrutura de diretórios do núcleo C++ da engine, do pacote TS e do exemplo; nome do pacote.
- Ferramentas TS (bundler, dev server, test runner) e a imagem/serviço Docker que as roda.
- Formato do esquema e linguagem do gerador (§4.1).
- Mecanismo de orquestração entre sistemas C++ e TS dentro das fases (§4.3), respeitando os anti-objetivos.
- Representação de `Entity` no TS (§4.2).
- Detalhes da API C exportada e da API TS, desde que o uso fique próximo do esboço (§4.8).
- Ordem global vs por tipo no despacho de eventos (§4.4).
- Render e colisão do jogo de validação (canvas 2D é o suficiente).
