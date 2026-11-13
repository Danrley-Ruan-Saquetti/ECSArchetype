import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { setContextActive } from "./context.js";
import { ContractError } from "./errors.js";
import type { Runtime } from "./runtime.js";
import { setTaskLastRun, TaskContext } from "./task_context.js";
import { setStructureLocked, type World } from "./world.js";

export type TaskFunction = (task: TaskContext) => void;

declare const taskHandleBrand: unique symbol;

/** Opaque value returned when scheduling; compared with `===`. */
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
 * Tasks on the game clock (the Engine's `time`), fired in step 2 of the fixed step and only in a non-paused step. The
 * same object in `engine.schedule` and in `context.schedule`. The timing rule lives in the C++ core; here are the
 * usage validation and the closures.
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

  /** Fires once, in the step whose start reaches `time + delaySeconds`. Delay finite and ≥ 0. */
  after(delaySeconds: number, callback: TaskFunction): TaskHandle {
    this.#assertUsable("after");
    if (!Number.isFinite(delaySeconds) || delaySeconds < 0) {
      throw new ContractError(`after: delay=${delaySeconds} negative or not finite`);
    }
    this.#assertCallback("after", callback);

    return this.#schedule(delaySeconds, 0, callback);
  }

  /** First firing at `time + intervalSeconds`, then every interval, without drift. Interval finite and > 0. */
  every(intervalSeconds: number, callback: TaskFunction): TaskHandle {
    this.#assertUsable("every");
    if (!Number.isFinite(intervalSeconds) || intervalSeconds <= 0) {
      throw new ContractError(`every: interval=${intervalSeconds} must be positive and finite`);
    }
    this.#assertCallback("every", callback);

    return this.#schedule(intervalSeconds, intervalSeconds, callback);
  }

  /** false if the task already finished or was cancelled. Cancelling an `every` discards the closure. */
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

  /** Removes the state scope of a pending task (true); false if it already finished or was cancelled. */
  persist(handle: TaskHandle): boolean {
    this.#assertUsable("persist");
    const ref = this.#refOf("persist", handle);

    return this.#bridge.enginePersistTask(this.#engineHandle, ref.slot, ref.generation);
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
      throw new ContractError(`task id=${scriptId}: not a TS task of this Engine`);
    }

    // A previous C++ task, handler or system may have grown the WASM memory.
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
      throw new ContractError(`${operation}: handle from another Engine or invalid`);
    }

    return ref;
  }

  #assertCallback(operation: string, callback: unknown): void {
    if (typeof callback !== "function") {
      throw new ContractError(`${operation}: the callback must be a function`);
    }
  }
}

/** Firing of a TS task by the core (OLY_SCRIPT_TASK). */
export function invokeScriptTask(scheduler: Scheduler, scriptId: number, lastRun: boolean): void {
  schedulerInternals.invoke(scheduler, scriptId, lastRun);
}

/** Discarding of a TS task by the core (OLY_SCRIPT_TASK_DISCARD); also valid with the Engine already disposed. */
export function discardScriptTask(scheduler: Scheduler, scriptId: number): void {
  schedulerInternals.discard(scheduler, scriptId);
}

export function scriptTaskCount(scheduler: Scheduler): number {
  return schedulerInternals.count(scheduler);
}
