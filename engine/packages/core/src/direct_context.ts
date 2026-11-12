import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { assertContextActive, EngineContext, type ContextKind } from "./context.js";
import type { Entity } from "./entity.js";
import type { Runtime } from "./runtime.js";
import type { Scheduler } from "./scheduler.js";
import type { ComponentType, Fields, SchemaType, ValueOf } from "./schema.js";
import type { SpawnValues, World } from "./world.js";

/**
 * Base do HandlerContext e do TaskContext: mudanças estruturais diretas (delegam ao World). Rodam fora de qualquer
 * iteração, então a entidade citada num evento ou numa tarefa pode já ter morrido (confira com isAlive).
 */
export class DirectContext extends EngineContext {
  readonly #world: World;

  constructor(kind: ContextKind, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap, scheduler: Scheduler) {
    super(kind, name, runtime, bridge, engineHandle, world, actions, scheduler);
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
