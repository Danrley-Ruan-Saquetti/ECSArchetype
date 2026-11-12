import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { setContextActive } from "./context.js";
import { ContractError } from "./errors.js";
import type { Runtime } from "./runtime.js";
import { setTaskLastRun, TaskContext } from "./task_context.js";
import { setStructureLocked, type World } from "./world.js";

export type TaskFunction = (task: TaskContext) => void;

declare const taskHandleBrand: unique symbol;

/** Valor opaco devolvido ao agendar; compara-se com `===`. */
export interface TaskHandle {
  readonly [taskHandleBrand]: true;
}

interface ScriptTask {
  readonly callback: TaskFunction;
  readonly context: TaskContext;
}

interface TaskRef {
  readonly slot: number;
  readonly generation: number;
}

interface SchedulerInternals {
  invoke(scheduler: Scheduler, scriptId: number, lastRun: boolean): void;
  discard(scheduler: Scheduler, scriptId: number): void;
  count(scheduler: Scheduler): number;
}

let schedulerInternals!: SchedulerInternals;

/**
 * Tarefas no relógio do jogo (`time` da Engine), disparadas no passo 2 do passo fixo e só em passo não pausado. O
 * mesmo objeto em `engine.schedule` e em `context.schedule`. A regra de tempo vive no núcleo C++; aqui ficam a
 * validação de uso e as closures.
 */
export class Scheduler {
  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #engineHandle: number;
  readonly #world: World;
  readonly #actions: ActionMap;
  readonly #assertUsable: (operation: string) => void;
  readonly #tasks = new Map<number, ScriptTask>();
  readonly #refs = new WeakMap<TaskHandle, TaskRef>();
  #nextScriptId = 0;

  static {
    schedulerInternals = {
      invoke: (scheduler, scriptId, lastRun) => {
        scheduler.#invoke(scriptId, lastRun);
      },
      discard: (scheduler, scriptId) => {
        scheduler.#tasks.delete(scriptId);
      },
      count: (scheduler) => scheduler.#tasks.size,
    };
  }

  constructor(runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap, assertUsable: (operation: string) => void) {
    this.#runtime = runtime;
    this.#bridge = bridge;
    this.#engineHandle = engineHandle;
    this.#world = world;
    this.#actions = actions;
    this.#assertUsable = assertUsable;
  }

  /** Dispara uma vez, no passo cujo início alcança `time + delaySeconds`. Atraso finito e ≥ 0. */
  after(delaySeconds: number, callback: TaskFunction): TaskHandle {
    this.#assertUsable("after");
    if (!Number.isFinite(delaySeconds) || delaySeconds < 0) {
      throw new ContractError(`after: atraso=${delaySeconds} negativo ou não finito`);
    }
    this.#assertCallback("after", callback);

    return this.#schedule(delaySeconds, 0, callback);
  }

  /** Primeiro disparo em `time + intervalSeconds`, depois a cada intervalo, sem deriva. Intervalo finito e > 0. */
  every(intervalSeconds: number, callback: TaskFunction): TaskHandle {
    this.#assertUsable("every");
    if (!Number.isFinite(intervalSeconds) || intervalSeconds <= 0) {
      throw new ContractError(`every: intervalo=${intervalSeconds} deve ser positivo e finito`);
    }
    this.#assertCallback("every", callback);

    return this.#schedule(intervalSeconds, intervalSeconds, callback);
  }

  /** false se a tarefa já terminou ou foi cancelada. Cancelar uma `every` descarta a closure. */
  cancel(handle: TaskHandle): boolean {
    this.#assertUsable("cancel");
    const ref = this.#refOf("cancel", handle);

    return this.#bridge.engineCancelTask(this.#engineHandle, ref.slot, ref.generation);
  }

  isPending(handle: TaskHandle): boolean {
    this.#assertUsable("isPending");
    const ref = this.#refOf("isPending", handle);

    return this.#bridge.engineIsTaskPending(this.#engineHandle, ref.slot, ref.generation);
  }

  #schedule(delaySeconds: number, intervalSeconds: number, callback: TaskFunction): TaskHandle {
    const scriptId = this.#nextScriptId++;
    const ref = this.#bridge.engineScheduleScriptTask(this.#engineHandle, delaySeconds, intervalSeconds, scriptId);
    const handle = Object.freeze({}) as TaskHandle;
    this.#refs.set(handle, ref);
    const context = new TaskContext(scriptId, handle, this.#runtime, this.#bridge, this.#engineHandle, this.#world, this.#actions, this);
    this.#tasks.set(scriptId, { callback, context });

    return handle;
  }

  #invoke(scriptId: number, lastRun: boolean): void {
    const task = this.#tasks.get(scriptId);
    if (task === undefined) {
      throw new ContractError(`tarefa id=${scriptId}: não é uma tarefa TS desta Engine`);
    }

    // Uma tarefa, handler ou sistema C++ anterior pode ter crescido a memória do WASM.
    this.#bridge.sync();
    setTaskLastRun(task.context, lastRun);
    setContextActive(task.context, true);
    setStructureLocked(this.#world, false);
    try {
      task.callback(task.context);
    } finally {
      if (lastRun) {
        this.#tasks.delete(scriptId);
      }
      setContextActive(task.context, false);
      setStructureLocked(this.#world, true);
    }
  }

  #refOf(operation: string, handle: TaskHandle): TaskRef {
    const ref = this.#refs.get(handle);
    if (ref === undefined) {
      throw new ContractError(`${operation}: handle de outra Engine ou inválido`);
    }

    return ref;
  }

  #assertCallback(operation: string, callback: unknown): void {
    if (typeof callback !== "function") {
      throw new ContractError(`${operation}: o callback precisa ser uma função`);
    }
  }
}

/** Disparo de uma tarefa TS pelo núcleo (OLY_SCRIPT_TASK). */
export function invokeScriptTask(scheduler: Scheduler, scriptId: number, lastRun: boolean): void {
  schedulerInternals.invoke(scheduler, scriptId, lastRun);
}

/** Descarte de uma tarefa TS pelo núcleo (OLY_SCRIPT_TASK_DISCARD); vale também com a Engine já liberada. */
export function discardScriptTask(scheduler: Scheduler, scriptId: number): void {
  schedulerInternals.discard(scheduler, scriptId);
}

export function scriptTaskCount(scheduler: Scheduler): number {
  return schedulerInternals.count(scheduler);
}
