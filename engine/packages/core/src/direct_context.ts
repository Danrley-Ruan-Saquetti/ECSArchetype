import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { assertContextActive, EngineContext, type ContextKind } from "./context.js";
import { formatEntity, type Entity } from "./entity.js";
import { ContractError } from "./errors.js";
import type { Runtime } from "./runtime.js";
import type { Scheduler } from "./scheduler.js";
import type { ComponentType, Fields, SchemaType, ValueOf } from "./schema.js";
import { spawnWith, type SpawnValues, type World } from "./world.js";

/**
 * Base do HandlerContext e do TaskContext: mudanças estruturais diretas (delegam ao World). Rodam fora de qualquer
 * iteração, então a entidade citada num evento ou numa tarefa pode já ter morrido (confira com isAlive).
 */
export class DirectContext extends EngineContext {
  readonly #world: World;
  readonly #bridge: Bridge;
  readonly #engineHandle: number;

  constructor(kind: ContextKind, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap, scheduler: Scheduler) {
    super(kind, name, runtime, bridge, engineHandle, world, actions, scheduler);
    this.#world = world;
    this.#bridge = bridge;
    this.#engineHandle = engineHandle;
  }

  /** Com estado ativo a entidade nasce com escopo e some na troca de estado, a menos que receba `persist`. */
  spawn<const Ts extends readonly SchemaType[]>(types: Ts, values?: SpawnValues<Ts>): Entity {
    assertContextActive(this, "spawn");

    return spawnWith(this.#world, types, values as Readonly<Record<string, Partial<Record<string, number>>>> | undefined, () => this.#bridge.directSpawn(this.#engineHandle));
  }

  /** Tira o escopo de estado da entidade. Lança ContractError para entidade morta. */
  persist(entity: Entity): void {
    assertContextActive(this, "persist");
    if (!this.#world.isAlive(entity)) {
      throw new ContractError(`persist entity=${formatEntity(entity)}: entidade morta`);
    }

    this.#bridge.contextPersist(this.#engineHandle, entity);
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
