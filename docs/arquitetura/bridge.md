# `bridge`: a API C

O `bridge` (`engine/runtime/bridge/`) expõe a API C `extern "C"` (`c_api.cpp`, `engine_api.cpp`) e é o único lugar do núcleo que conhece o lado TS.

| Grupo | Funções |
|---|---|
| Mundo e esquema | `olyWorldCreate/Destroy/CheckInvariants`, `olySchemaHash`, `olySchemaComponentCount/ComponentId`, `olySchemaEvent*`, `olySchemaAction*`, `olySchemaStateCount`, `olyComponentSize/Alignment` |
| Entidades e componentes | `olyEntitySpawn/Destroy/IsAlive`, `olyComponentAdd/Remove/Has/Get` |
| Queries | `olyQueryCreate`, `olyQueryBegin`, `olyQueryEnd` |
| Engine e loop | `olyEngineCreate/Destroy/World`, `olyEngineFrame/SetHidden`, `olyEnginePause/Resume/IsPaused`, `olyEngineTime/FixedDeltaTime/Interpolation` |
| Sistemas | `olyEngineAddNativeSystem` (por nome), `olyEngineAddScriptSystem`, `olyEngineSetWhilePaused`, `olySystemSpawn/Destroy/Add/Remove` |
| Eventos | `olyEngineEmit`, `olyEngineAddNativeHandler`, `olyEngineAddScriptHandler` |
| Input | `olyEngineBindButton/BindAxis2d/ClearBindings/IsBound`, `olyEngineButtonDown/Up`, `olyEnginePointerMove`, `olyEngineReleaseAll`, `olyEngineActionSnapshot` |
| Schedule | `olyEngineScheduleScriptTask`, `olyEngineCancelTask`, `olyEngineIsTaskPending`, `olyEnginePersistTask` |
| Estados | `olyEngineRequestState`, `olyEngineState`, `olyEngineSetSystemStates/SetHandlerStates`, `olyEngineAddNativeStateHook/AddScriptStateHook`, `olyDirectSpawn`, `olyContextPersist` |

## Pontos de projeto

- **`OlyWorld` é opaco.** Ele guarda o `World` e uma pilha de slots de iteração. Os sistemas C++ do jogo chegam ao `World` por `oly::bridge::worldOf(OlyWorld*)`.
- **`olyQueryBegin` devolve uma `OlyIteration*`** com buffer próprio de descritores (`count`, endereço das entidades, endereço de cada coluna). Um `eachBlock` aninhado não sobrescreve o externo. `olyQueryEnd` precisa encerrar o slot do topo.
- **`OlyEngine`** guarda um `OlyWorld` e a `Engine` sobre ele (um `World` por `OlyEngine`). Na criação, declara os eventos, as ações e os estados do esquema na ordem dele, então o índice no esquema TS é o id no C++.
- **Uma única função JS importada**, `olyHostInvokeScript(engine, kind, id, events, eventCount)`, serve a todo código TS chamado pelo C++. Não há `addFunction` nem tabela de funções que cresce. O parâmetro `engine` permite várias engines por módulo WASM. O `kind` diz o que chamar e o sentido dos outros parâmetros:

  | `kind` | `id` | `events` / `eventCount` |
  |---|---|---|
  | `OLY_SCRIPT_SYSTEM` | sistema | nulo / 0; uma chamada por sistema por passo |
  | `OLY_SCRIPT_HANDLER` | handler | endereço da fila no heap / tamanho do lote; uma chamada por lote |
  | `OLY_SCRIPT_TASK` | `scriptId` | nulo / 1 na última execução, senão 0 |
  | `OLY_SCRIPT_TASK_DISCARD` | `scriptId` | nulo / 0 |
  | `OLY_SCRIPT_STATE_HOOK` | `hookId` | nulo / o estado do outro lado da troca (o de origem num `onEnter`, o de destino num `onExit`) |
- **Nomes nativos são resolvidos na criação.** `OLY_SYSTEM`, `OLY_HANDLER(name, EventType)` e `OLY_STATE_HOOK` registram funções por nome. Nome desconhecido devolve `OLY_INVALID_HANDLER`; tipo de evento errado, `OLY_HANDLER_TYPE_MISMATCH`.
- **Contexto atual.** `olyDirectSpawn` e `olyContextPersist` usam o contexto em que o TS está: direto num handler, tarefa ou gancho; adiado num sistema.
- No build nativo, `oly::bridge::setScriptInvoker` troca o invocador para os testes.

O lado TS que consome esta API está em [pacotes-ts.md](pacotes-ts.md). O contrato de link com o código gerado do jogo está em [esquema-e-codegen.md](esquema-e-codegen.md).
