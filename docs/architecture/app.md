# `app`: a `Engine`

Código em `engine/runtime/app/`. Spec: `docs/superpowers/specs/2026-11-09-game-engine-design.md`.

`oly::app::Engine` guarda o relógio de passo fixo, os sistemas de cada fase, a pausa, a aba oculta, as filas de eventos, o `Scheduler`, os estados, um `ActionMap` ([input.md](input.md)) e um `CommandBuffer`. Ela **não** é dona do `World`: recebe `World&` no construtor.

`EngineConfig`: `fixedHz = 60`, `maxStepsPerFrame = 5`, `maxEventRounds = 16`.

## O loop

O host chama `frame(nowSeconds)` uma vez por quadro e `setHidden(bool)` quando a aba muda de visibilidade. O núcleo decide o resto.

```
frame(agora):
  se o frame anterior foi interrompido por exceção → recupera (ver "Recuperação de exceção")
  se a aba está oculta → retorna
  primeiro frame (ou primeiro depois de oculta/recuperação) → só registra o tempo
  acumulador += agora − anterior
  enquanto couber um passo e passos < maxStepsPerFrame:
    passo fixo
  excedente além do limite → descartado (evita a espiral de atraso)
  render (uma vez), com interpolation = acumulador / passo

passo fixo:
  1. captura das ações                       (pausado ou não)
  2. tarefas agendadas vencidas              (só sem pausa)
  3. updateBefore → update → updateAfter     (CommandBuffer aplicado ao fim de cada fase)
  4. despacho de eventos, em rodadas
  5. troca de estado pendente                (pausado ou não)
  relógio avança (só sem pausa)
```

- `time()` = passos não pausados concluídos × `fixedDeltaTime`. Dentro de um passo, é o início do passo; fora de `frame` e na fase `render`, é o início do próximo passo. Não há soma acumulada de `double`.
- Não existe fase de física. Colisão e física do jogo ficam em `updateAfter`.
- Dentro de uma fase, a ordem é a ordem de registro. Não há grafo de dependências entre sistemas.

## Sistemas e contextos

Um sistema é um ponteiro de função mais `userData`. Assim o `app` não conhece TS: o `bridge` registra um sistema TS como uma função C++ comum ([bridge.md](bridge.md)).

Registro: `addSystem(phase, name, function)` ou, por nome, a macro `OLY_SYSTEM(name)`, que o TS adiciona com `addNativeSystem`. Sistemas C++ e TS se misturam na mesma fase, na ordem de registro.

Hierarquia de contextos:

```
EngineContext      tempo, pausa, query/get/has/isAlive, emit, actions, schedule, state, requestState
├── SystemContext  mudanças adiadas (CommandBuffer), interpolation, persist adiado
└── DirectContext  mudanças diretas no World, persist direto
    ├── HandlerContext  + lote de eventos
    ├── TaskContext     + handle, lastRun, payload<P>()
    └── StateContext    + fromState, toState
```

Sistemas não fazem mudança estrutural direta, porque podem estar dentro de uma iteração. Handlers, tarefas e ganchos rodam fora de iteração, então mudam o `World` direto.

## Pausa e aba oculta

- `pause()`/`resume()` valem a partir do próximo passo fixo.
- Pausado: o relógio e o schedule congelam; só rodam os sistemas marcados `whilePaused`. A captura, o despacho de eventos, a troca de estado e o `render` continuam.
- Aba oculta: nenhum passo roda, `releaseAll()` é chamado e o tempo acumulado é descartado ao voltar. A aba oculta é independente da pausa do jogo.

## Eventos

- `declareEvent<T>(name)` dá o `EventTypeId` na ordem de declaração, que é a ordem de despacho. `T` é trivialmente copiável.
- Cada tipo tem duas filas de bytes: `pending` (recebe `emit`, a qualquer hora) e `dispatching` (o lote da rodada).
- **Rodada**: troca as filas de todo tipo com pendência e chama cada handler do tipo uma vez com o lote inteiro, tipo a tipo. O que os handlers emitem vai para a rodada seguinte do mesmo passo. O lote que um handler lê nunca muda nem se move enquanto ele roda.
- As rodadas continuam até as filas esvaziarem ou estourar `maxEventRounds` (`OLY_ASSERT`; sem asserts, descarta tudo e o passo segue).
- A engine não tira eventos repetidos nem destrói nada sozinha. O jogo registra por último o handler que destrói e confere `isAlive`.
- Evento sem handler some no fim do passo. Evento emitido no `render` ou fora de `frame` é despachado no próximo passo.

## Schedule

- `Scheduler::after/every/cancel/isPending`, no relógio do jogo. Vencimento = `time()` + atraso.
- A fila (`detail::TaskQueue`) é um min-heap por (vencimento, sequência) sobre slots com geração e cancelamento preguiçoso. Ela não conhece a `Engine`.
- Tarefas vencidas rodam no passo 2, só sem pausa. Uma tarefa criada durante o passo 2 só roda a partir do passo seguinte, então `after(0)` dentro de uma tarefa não vira laço.
- `every` não deriva e dispara no máximo uma vez por passo.
- O payload é copiado (trivialmente copiável, até 32 B, alinhamento até 8). `payload<P>()` confere o tipo.
- O descarte opcional roda uma única vez quando a tarefa sai da fila sem a última execução (cancelamento, troca de estado, destruição da `Engine`). Ele nunca roda depois do último disparo de um `after` (`lastRun()`): uma exceção JS desenrola o WASM sem rodar o que vem depois, e a closure TS ficaria presa.

## Estados

- Estados planos, sem aninhamento nem pilha. `declareState` dá o `StateId` na ordem de declaração; no máximo 32, para a restrição ser uma máscara `uint32_t`. A engine começa em `NoState`.
- `requestState` vale a qualquer hora; o último pedido antes do passo 5 vence. Não existe estado inicial especial: o jogo pede o primeiro estado antes de rodar.
- **Troca (passo 5)**: `onExit` do estado que sai → limpeza do escopo → estado novo → `onEnter` do que entra → novo despacho de eventos, que conta no limite de rodadas do passo. Um pedido feito num gancho vale no passo seguinte.
- **`inState`**: sistemas e handlers podem ser restritos por máscara, em todas as fases. Sem estado ativo, quem tem restrição não roda.
- **Escopo**: com estado ativo, todo `spawn` de contexto põe a tag interna `detail::StateScoped`, e toda tarefa agendada é marcada como `scoped`. A troca destrói as entidades com a tag (por uma `QueryById` criada uma vez) e cancela as tarefas com escopo. Assim "jogar de novo" começa limpo sem código de limpeza no jogo. `World::spawn` direto é sempre persistente. `persist` tira o escopo (adiado no `SystemContext`, direto nos demais contextos). O que `onExit` cria é limpo na mesma troca; o que `onEnter` cria fica até a troca seguinte.
- Custo aceito do escopo por tag: arquétipos gêmeos (com e sem a tag) quando o jogo cria a mesma combinação nos dois modos.

## Recuperação de exceção

Um sistema TS que lança desenrola o WASM sem rodar destrutores. O `frame` seguinte vê `inFrame_` ainda ligado e recupera:

- descarta os comandos pendentes, as filas de eventos e o pedido de estado;
- devolve ao heap as tarefas criadas durante o passo 2 interrompido;
- reinicia o relógio do frame (o próximo `frame` só registra o tempo).

O que as fases anteriores já aplicaram fica. As tarefas agendadas sobrevivem. Uma exceção num `onExit` deixa o estado e o escopo como estavam; num `onEnter`, o estado já é o novo.
