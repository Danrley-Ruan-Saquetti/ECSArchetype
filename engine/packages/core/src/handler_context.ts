import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { DirectContext } from "./direct_context.js";
import type { EventBatch } from "./event_batch.js";
import type { Runtime } from "./runtime.js";
import type { Scheduler } from "./scheduler.js";
import type { EventType, Fields } from "./schema.js";
import type { World } from "./world.js";

export type HandlerFunction<F extends Fields = Fields> = (context: HandlerContext, batch: EventBatch<F>) => void;

/**
 * Válido só durante a chamada do seu handler. Mudanças estruturais são diretas (DirectContext): o despacho
 * acontece fora de qualquer iteração, então a entidade citada num evento pode já ter morrido (confira com isAlive).
 */
export class HandlerContext extends DirectContext {
  readonly eventType: EventType;
  readonly name: string;

  constructor(eventType: EventType, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap, scheduler: Scheduler) {
    super({ label: "handler", className: "HandlerContext", callNoun: "handler" }, name, runtime, bridge, engineHandle, world, actions, scheduler);
    this.eventType = eventType;
    this.name = name;
  }
}
