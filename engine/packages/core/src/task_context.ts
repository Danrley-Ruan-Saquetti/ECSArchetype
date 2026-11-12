import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { assertContextActive } from "./context.js";
import { DirectContext } from "./direct_context.js";
import type { Runtime } from "./runtime.js";
import type { Scheduler, TaskHandle } from "./scheduler.js";
import type { World } from "./world.js";

let setLastRunInternal!: (context: TaskContext, lastRun: boolean) => void;

/**
 * Valid only during the call of its task; outside it, any use throws ContractError. Runs in step 2, outside
 * any iteration, with direct structural changes.
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
    super({ label: "task", className: "TaskContext", callNoun: "task" }, String(scriptId), runtime, bridge, engineHandle, world, actions, scheduler);
    this.#handle = handle;
  }

  get handle(): TaskHandle {
    assertContextActive(this, "handle");

    return this.#handle;
  }

  /** True on the firing of an after: the task does not run again. */
  get lastRun(): boolean {
    assertContextActive(this, "lastRun");

    return this.#lastRun;
  }
}

export function setTaskLastRun(context: TaskContext, lastRun: boolean): void {
  setLastRunInternal(context, lastRun);
}
