#pragma once

#include <cstdint>

#include "oly/bridge/c_api.hpp"

struct OlyEngine;

#define OLY_INVALID_SYSTEM 0xFFFFFFFFu
#define OLY_INVALID_HANDLER 0xFFFFFFFFu
#define OLY_HANDLER_TYPE_MISMATCH 0xFFFFFFFEu

// `kind` da chamada de volta única para o JS (olyHostInvokeScript).
#define OLY_SCRIPT_SYSTEM 0u
#define OLY_SCRIPT_HANDLER 1u
#define OLY_SCRIPT_TASK 2u
#define OLY_SCRIPT_TASK_DISCARD 3u
#define OLY_SCRIPT_STATE_HOOK 4u

#define OLY_NO_STATE 0xFFFFFFFFu
#define OLY_INVALID_STATE_HOOK 0xFFFFFFFFu
#define OLY_STATE_ENTER 0u
#define OLY_STATE_EXIT 1u

struct OlyTaskHandle {
  uint32_t slot;
  uint32_t generation;
};

// Espelho de oly::input::ActionState (12 B por ação): o JS lê o snapshot direto do heap.
struct OlyActionState {
  float x;
  float y;
  uint32_t flags;
};

#define OLY_ACTION_PRESSED 1u
#define OLY_ACTION_HELD 2u
#define OLY_ACTION_RELEASED 4u
#define OLY_ACTION_VALID 8u

extern "C" {
// fixedHz, maxStepsPerFrame e maxEventRounds precisam ser positivos (OLY_ASSERT). Declara, na ordem do esquema, todos
// os eventos de oly::bridge::schemaEvents(): o índice do evento no esquema é o seu tipo de evento (eventType).
// Depois dos eventos e das ações, declara os estados de oly::bridge::schemaStates(): o índice do estado no esquema é o
// seu StateId.
OLY_API OlyEngine* olyEngineCreate(double fixedHz, uint32_t maxStepsPerFrame, uint32_t maxEventRounds);
OLY_API void olyEngineDestroy(OlyEngine* engine);
// Pertence à engine: vale até olyEngineDestroy e nunca vai para olyWorldDestroy.
OLY_API OlyWorld* olyEngineWorld(OlyEngine* engine);

// Contrato do loop, da pausa e da aba oculta: o de oly::app::Engine (oly/app/engine.hpp).
OLY_API void olyEngineFrame(OlyEngine* engine, double nowSeconds);
OLY_API void olyEngineSetHidden(OlyEngine* engine, int32_t hidden);
OLY_API void olyEnginePause(OlyEngine* engine);
OLY_API void olyEngineResume(OlyEngine* engine);
OLY_API int32_t olyEngineIsPaused(OlyEngine* engine);
OLY_API double olyEngineTime(OlyEngine* engine);
OLY_API double olyEngineFixedDeltaTime(OlyEngine* engine);
OLY_API double olyEngineInterpolation(OlyEngine* engine);

// `phase` é um oly::app::Phase (fora do intervalo, OLY_ASSERT). Registrar durante olyEngineFrame é erro de contrato.
// OLY_INVALID_SYSTEM se não existe sistema nativo com esse nome (registrado por OLY_SYSTEM).
OLY_API uint32_t olyEngineAddNativeSystem(OlyEngine* engine, uint32_t phase, const char* name);
// O sistema é chamado pelo invocador de scripts (olyHostInvokeScript no WASM) com o id devolvido aqui.
OLY_API uint32_t olyEngineAddScriptSystem(OlyEngine* engine, uint32_t phase, const char* name);
OLY_API void olyEngineSetWhilePaused(OlyEngine* engine, uint32_t systemId, int32_t whilePaused);

// eventType < olySchemaEventCount() (OLY_ASSERT). Copia o evento (o tamanho do tipo) para a fila; vale dentro e fora
// de olyEngineFrame. Eventos emitidos são despachados no passo 4 do passo fixo, em lote por tipo.
OLY_API void olyEngineEmit(OlyEngine* engine, uint32_t eventType, const void* value);
// OLY_INVALID_HANDLER se não existe handler nativo com esse nome (registrado por OLY_HANDLER) e
// OLY_HANDLER_TYPE_MISMATCH se o handler nomeado é de outro tipo de evento. Registrar durante olyEngineFrame é erro.
OLY_API uint32_t olyEngineAddNativeHandler(OlyEngine* engine, uint32_t eventType, const char* name);
// O handler é chamado pelo invocador de scripts uma vez por lote, com o id devolvido aqui.
OLY_API uint32_t olyEngineAddScriptHandler(OlyEngine* engine, uint32_t eventType, const char* name);

// Tarefa de script no relógio do jogo. intervalSeconds 0 agenda um after; maior que 0, um every com o primeiro
// disparo em delaySeconds. Vale dentro e fora de olyEngineFrame. O invocador recebe OLY_SCRIPT_TASK a cada disparo
// (eventCount é 1 na última execução de um after) e OLY_SCRIPT_TASK_DISCARD, uma vez, quando a tarefa sai da fila sem
// a última execução (cancelamento ou olyEngineDestroy). Os asserts do Scheduler valem (atraso e intervalo inválidos).
OLY_API void olyEngineScheduleScriptTask(OlyEngine* engine, double delaySeconds, double intervalSeconds, uint32_t scriptId, OlyTaskHandle* out);
// 1 se a tarefa estava pendente; 0 se já terminou, foi cancelada ou o handle é de uma tarefa antiga do slot.
OLY_API int32_t olyEngineCancelTask(OlyEngine* engine, uint32_t slot, uint32_t generation);
OLY_API int32_t olyEngineIsTaskPending(OlyEngine* engine, uint32_t slot, uint32_t generation);

// Estados: olyEngineCreate declara os do esquema, então o índice no esquema é o StateId. O pedido vale dentro e fora de
// olyEngineFrame (o último vence) e a troca acontece no passo 5. Estado fora do intervalo é erro de contrato (OLY_ASSERT).
OLY_API void olyEngineRequestState(OlyEngine* engine, uint32_t state);
// OLY_NO_STATE até a primeira troca.
OLY_API uint32_t olyEngineState(OlyEngine* engine);
// Um bit por estado; stateMask 0 tira a restrição. Máscara com bit de estado não declarado é erro de contrato.
OLY_API void olyEngineSetSystemStates(OlyEngine* engine, uint32_t systemId, uint32_t stateMask);
OLY_API void olyEngineSetHandlerStates(OlyEngine* engine, uint32_t handlerId, uint32_t stateMask);
// `when` é OLY_STATE_ENTER ou OLY_STATE_EXIT (outro valor é erro de contrato). O nativo devolve OLY_INVALID_STATE_HOOK se
// não há gancho com esse nome (OLY_STATE_HOOK). O de script chama olyHostInvokeScript(engine, OLY_SCRIPT_STATE_HOOK,
// hookId, NULL, outro), onde `outro` é o estado do outro lado da troca: o de origem num OLY_STATE_ENTER e o de destino
// num OLY_STATE_EXIT. O lado do gancho é olyEngineState no momento da chamada.
OLY_API uint32_t olyEngineAddNativeStateHook(OlyEngine* engine, uint32_t when, uint32_t state, const char* name);
OLY_API uint32_t olyEngineAddScriptStateHook(OlyEngine* engine, uint32_t when, uint32_t state, const char* name);
// Só dentro de um handler, tarefa ou gancho de script; aplica o escopo do estado atual.
OLY_API void olyDirectSpawn(OlyEngine* engine, OlyEntity* out);
// Dentro de um sistema de script é adiado; num handler, tarefa ou gancho de script é direto (a entidade precisa estar viva).
OLY_API void olyContextPersist(OlyEngine* engine, uint32_t index, uint32_t generation);
// 1 se a tarefa estava pendente e perdeu o escopo; 0 se não está pendente.
OLY_API int32_t olyEnginePersistTask(OlyEngine* engine, uint32_t slot, uint32_t generation);

// Ações: olyEngineCreate declara, depois dos eventos, todas as de oly::bridge::schemaActions(); o índice da ação no
// esquema é o seu ActionId (action). `button` é um usage HID (página nos 16 bits altos). Ligar confere o intervalo
// e o tipo da ação (OLY_ASSERT); limpar confere só o intervalo. Valem dentro e fora de olyEngineFrame.
OLY_API void olyEngineBindButton(OlyEngine* engine, uint32_t action, uint32_t button);
OLY_API void olyEngineBindAxis2d(OlyEngine* engine, uint32_t action, uint32_t up, uint32_t down, uint32_t left, uint32_t right);
OLY_API void olyEngineClearBindings(OlyEngine* engine, uint32_t action);
OLY_API int32_t olyEngineIsBound(OlyEngine* engine, uint32_t button);
// Entrada do host: entra na fila e só vira estado de ação na captura do próximo passo.
OLY_API void olyEngineButtonDown(OlyEngine* engine, uint32_t button);
OLY_API void olyEngineButtonUp(OlyEngine* engine, uint32_t button);
OLY_API void olyEnginePointerMove(OlyEngine* engine, float x, float y);
OLY_API void olyEngineReleaseAll(OlyEngine* engine);
// Um OlyActionState por ação, na ordem do esquema; estável até olyEngineDestroy porque todas as ações são
// declaradas na criação.
OLY_API const OlyActionState* olyEngineActionSnapshot(OlyEngine* engine);

// Só valem dentro de um sistema de script (fora dele, OLY_ASSERT). Mudanças adiadas até o fim da fase.
OLY_API void olySystemSpawn(OlyEngine* engine, OlyEntity* out);
OLY_API void olySystemDestroy(OlyEngine* engine, uint32_t index, uint32_t generation);
OLY_API void olySystemAdd(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId, void* value);
OLY_API void olySystemRemove(OlyEngine* engine, uint32_t index, uint32_t generation, uint32_t componentId);
}
