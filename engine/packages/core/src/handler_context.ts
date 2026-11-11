import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { assertContextActive, EngineContext } from "./context.js";
import type { Entity } from "./entity.js";
import type { EventBatch } from "./event_batch.js";
import type { Runtime } from "./runtime.js";
import type { ComponentType, EventType, Fields, SchemaType, ValueOf } from "./schema.js";
import type { SpawnValues, World } from "./world.js";

export type HandlerFunction<F extends Fields = Fields> = (context: HandlerContext, batch: EventBatch<F>) => void;

/**
 * Válido só durante a chamada do seu handler. Mudanças estruturais são diretas (delegam ao World): o despacho
 * acontece fora de qualquer iteração, então a entidade citada num evento pode já ter morrido (confira com isAlive).
 */
export class HandlerContext extends EngineContext {
  readonly eventType: EventType;
  readonly name: string;

  readonly #world: World;

  constructor(eventType: EventType, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap) {
    super({ label: "handler", className: "HandlerContext", callNoun: "handler" }, name, runtime, bridge, engineHandle, world, actions);
    this.eventType = eventType;
    this.name = name;
    this.#world = world;
  }

  spawn<const Ts extends readonly SchemaType[]>(types: Ts, values?: SpawnValues<Ts>): Entity {
    assertContextActive(this, "spawn");

    return this.#world.spawn(types, values);
  }

  destroy(entity: Entity): void {
    assertContextActive(this, "destroy");

    this.#world.destroy(entity);
  }

  add<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value?: Partial<ValueOf<F>>): void;
  add(entity: Entity, type: SchemaType): void;
  add(entity: Entity, type: SchemaType, value?: Partial<Record<string, number>>): void {
    assertContextActive(this, "add");

    if (type.kind === "component") {
      this.#world.add(entity, type, value);
    } else {
      this.#world.add(entity, type);
    }
  }

  remove(entity: Entity, type: SchemaType): void {
    assertContextActive(this, "remove");

    this.#world.remove(entity, type);
  }
}
