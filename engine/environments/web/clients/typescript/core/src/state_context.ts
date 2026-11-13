import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { assertContextActive } from "./context.js";
import { DirectContext } from "./direct_context.js";
import type { Runtime } from "./runtime.js";
import type { Scheduler } from "./scheduler.js";
import type { State } from "./schema.js";
import type { World } from "./world.js";

export type StateHookFunction = (context: StateContext) => void;

let setSidesInternal!: (context: StateContext, fromState: State | null, toState: State | null) => void;

/**
 * Valid only during the call of its hook; outside it, any use throws ContractError. Runs in step 5, outside
 * any iteration, with direct structural changes.
 */
export class StateContext extends DirectContext {
  #fromState: State | null = null;
  #toState: State | null = null;

  static {
    setSidesInternal = (context, fromState, toState) => {
      context.#fromState = fromState;
      context.#toState = toState;
    };
  }

  constructor(name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap, scheduler: Scheduler) {
    super({ label: "hook", className: "StateContext", callNoun: "hook" }, name, runtime, bridge, engineHandle, world, actions, scheduler);
  }

  /** `null` on the first change. */
  get fromState(): State | null {
    assertContextActive(this, "fromState");

    return this.#fromState;
  }

  get toState(): State | null {
    assertContextActive(this, "toState");

    return this.#toState;
  }
}

export function setStateContextSides(context: StateContext, fromState: State | null, toState: State | null): void {
  setSidesInternal(context, fromState, toState);
}
