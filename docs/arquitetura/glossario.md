# Glossário

O mesmo verbo vale para o mesmo conceito em C++, na API C e no TS.

| Conceito | C++ (`oly::ecs`, `oly::app`, `oly::input`) | API C | TS (`@oly/core`) |
|---|---|---|---|
| criar entidade | `World::spawn()`, `spawn(components...)`, `CommandBuffer::spawn()` | `olyEntitySpawn`, `olyDirectSpawn` (handler, tarefa e gancho TS, com o escopo do estado) | `World.spawn` |
| destruir / viva | `destroy`, `isAlive` | `olyEntityDestroy`, `olyEntityIsAlive` | `destroy`, `isAlive` |
| adicionar / remover / ter | `add`, `remove`, `has` | `olyComponentAdd/Remove/Has` | `add`, `remove`, `has` |
| ler (existe por contrato) / se existir | `get` -> `T&` / `tryGet` -> `T*` | `olyComponentGet` | `get` -> cópia |
| query / iterar | `query<Ts...>`, `each`, `eachBlock` | `olyQueryCreate`, `olyQueryBegin/End` | `query`, `eachBlock` |
| ler a única linha de uma query (exatamente uma / no máximo uma) | `Query::single()` -> `tuple<Entity, Ts&...>` / `trySingle()` -> `optional` | (`olyQueryBegin/End`) | `query.single()` / `trySingle()` -> `SingleRow` (cópia) |
| passo de jogo (fixo) / aba oculta | `Engine::frame(nowSeconds)`, `setHidden` | `olyEngineFrame`, `olyEngineSetHidden` | `Engine.frame`, `setHidden` |
| pausar / retomar | `Engine::pause`, `resume`, `paused`; `SystemContext::pause/resume` | `olyEnginePause/Resume/IsPaused` | `pause`, `resume`, `paused` |
| registrar sistema | `Engine::addSystem(phase, name, função)`, `OLY_SYSTEM(name)` (por nome) | `olyEngineAddNativeSystem` (por nome), `olyEngineAddScriptSystem` | `addSystem`, `addNativeSystem` |
| sistema roda pausado | `Engine::setWhilePaused` | `olyEngineSetWhilePaused` | `SystemRegistration.whilePaused()` |
| mudança estrutural adiada (dentro de sistema) | `SystemContext::spawn/destroy/add/remove` | `olySystemSpawn/Destroy/Add/Remove` | `context.spawn/destroy/add/remove` |
| declarar evento (ordem = ordem de despacho) | `Engine::declareEvent<T>(name)` | (eventos do esquema, na criação da `OlyEngine`) | `event(name, fields)` no esquema |
| emitir evento | `Engine::emit`, `SystemContext/HandlerContext::emit` | `olyEngineEmit` | `context.emit` |
| declarar ação | `ActionMap::declareButton/declareAxis2d/declarePointer` | (ações do esquema, na criação da `OlyEngine`) | `action(name, kind)` no esquema |
| ligar ação a botão físico | `ActionMap::bind`, `clearBindings`, `isBound` | `olyEngineBindButton/BindAxis2d`, `olyEngineClearBindings`, `olyEngineIsBound` | `engine.actions.bind/clearBindings/isBound` |
| entregar input do host | `buttonDown`, `buttonUp`, `pointerMove`, `releaseAll` | `olyEngineButtonDown/Up`, `olyEnginePointerMove`, `olyEngineReleaseAll` | `engine.actions.buttonDown/buttonUp/pointerMove/releaseAll` |
| ler ação no passo | `ActionMap::button/axis2d/pointer` (`context.actions()`) | `olyEngineActionSnapshot` (lido sem cruzar) | `context.actions.button/axis2d/pointer` |
| registrar handler (lote por rodada) | `Engine::on<T>(name, função)` | `olyEngineAddScriptHandler` | `Engine.on` |
| handler por nome | `OLY_HANDLER(name, EventType)` | `olyEngineAddNativeHandler` | `addNativeHandler` |
| agendar tarefa (relógio do jogo) | `Scheduler::after/every` (`engine.schedule()`, `context.schedule()`) | `olyEngineScheduleScriptTask` | `schedule.after/every` |
| cancelar / consultar tarefa | `Scheduler::cancel/isPending` | `olyEngineCancelTask`, `olyEngineIsTaskPending` | `schedule.cancel/isPending` |
| declarar estado (ordem = StateId) | `Engine::declareState(name)` | (estados do esquema, na criação da `OlyEngine`) | `state(name)` no esquema |
| pedir / ler estado | `Engine::requestState`, `state`; `EngineContext::requestState/state` | `olyEngineRequestState`, `olyEngineState` | `engine.requestState`, `engine.state`, `context.requestState/state` |
| gancho de estado | `Engine::onEnter/onExit(state, name, função)`, `OLY_STATE_HOOK(name)` (por nome) | `olyEngineAddNativeStateHook` (por nome), `olyEngineAddScriptStateHook` | `onEnter`, `onExit`, `addNativeOnEnter/addNativeOnExit` |
| restringir a estados | `Engine::setSystemStates/setHandlerStates` (e `*StateMask`) | `olyEngineSetSystemStates/SetHandlerStates` | `registration.inState(...states)` |
| persistir (tirar o escopo de estado) | `SystemContext/DirectContext::persist`, `Scheduler::persist` | `olyContextPersist`, `olyEnginePersistTask` | `context.persist`, `schedule.persist` |
| variante por ID | sufixo `ById`: `addById`, `removeById`, `hasById`, `getById`, `tryGetById`, `queryById` | (toda por ID) | — |

## Regras de nome

- O prefixo `oly` aparece só na API C (`oly*`, `OlyWorld`, `OLY_API`) e no que é interno à ponte (`OlyModule`). As APIs C++ e TS não repetem `oly` nos nomes. O motivo está em [modulos-e-fronteiras.md](modulos-e-fronteiras.md#regras-de-vocabulário).
- `create` e "Raw" não existem como nomes. Uma entidade nasce por `spawn`; a variante por `ComponentId` usa o sufixo `ById`.
- Não há aliases nem `using` de compatibilidade com nomes antigos.
