import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { assertContextActive } from "./context.js";
import { DirectContext } from "./direct_context.js";
import type { Runtime } from "./runtime.js";
import type { Scheduler, TaskHandle } from "./scheduler.js";
import type { World } from "./world.js";

let setLastRunInternal!: (context: TaskContext, lastRun: boolean) => void;

/**
 * Válido só durante a chamada da sua tarefa; fora dela, qualquer uso lança ContractError. Roda no passo 2, fora de
 * qualquer iteração, com mudanças estruturais diretas.
 */
export class TaskContext extends DirectContext {
  readonly #handle: TaskHandle;
  #lastRun = false;

  static {
    setLastRunInternal = (context, lastRun) => {
      context.#lastRun = lastRun;
    };
  }

  constructor(scriptId: number, handle: TaskHandle, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap, scheduler: Scheduler) {
    super({ label: "task", className: "TaskContext", callNoun: "tarefa" }, String(scriptId), runtime, bridge, engineHandle, world, actions, scheduler);
    this.#handle = handle;
  }

  get handle(): TaskHandle {
    assertContextActive(this, "handle");

    return this.#handle;
  }

  /** Verdadeiro no disparo de um after: a tarefa não roda mais. */
  get lastRun(): boolean {
    assertContextActive(this, "lastRun");

    return this.#lastRun;
  }
}

export function setTaskLastRun(context: TaskContext, lastRun: boolean): void {
  setLastRunInternal(context, lastRun);
}
