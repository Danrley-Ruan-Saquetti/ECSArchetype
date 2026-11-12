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
 * Válido só durante a chamada do seu gancho; fora dela, qualquer uso lança ContractError. Roda no passo 5, fora de
 * qualquer iteração, com mudanças estruturais diretas.
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
    super({ label: "hook", className: "StateContext", callNoun: "gancho" }, name, runtime, bridge, engineHandle, world, actions, scheduler);
  }

  /** `null` na primeira troca. */
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
