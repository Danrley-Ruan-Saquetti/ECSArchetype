import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { assertContextActive, EngineContext } from "./context.js";
import type { Entity } from "./entity.js";
import type { Phase } from "./engine.js";
import { componentIdOf, type Runtime } from "./runtime.js";
import type { Scheduler } from "./scheduler.js";
import type { ComponentType, Fields, SchemaType, ValueOf } from "./schema.js";
import { assertKnownFields, assertSpawnArguments, writeValue } from "./values.js";
import type { SpawnValues, World } from "./world.js";

export type SystemFunction = (context: SystemContext) => void;

/**
 * Valid only during the call of its system; outside it, any use throws ContractError. Structural changes
 * (spawn/destroy/add/remove) are deferred until the end of the phase.
 */
export class SystemContext extends EngineContext {
  readonly phase: Phase;
  readonly name: string;

  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #engineHandle: number;

  constructor(phase: Phase, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap, scheduler: Scheduler) {
    super({ label: "system", className: "SystemContext", callNoun: "system" }, name, runtime, bridge, engineHandle, world, actions, scheduler);
    this.phase = phase;
    this.name = name;
    this.#runtime = runtime;
    this.#bridge = bridge;
    this.#engineHandle = engineHandle;
  }

  get interpolation(): number {
    assertContextActive(this, "interpolation");

    return this.#bridge.engineInterpolation(this.#engineHandle);
  }

  /** The entity only becomes alive at the end of the phase; the handle can already be used in the deferred commands. */
  spawn<const Ts extends readonly SchemaType[]>(types: Ts, values?: SpawnValues<Ts>): Entity {
    assertContextActive(this, "spawn");

    const valuesByName = (values ?? {}) as Readonly<Record<string, Partial<Record<string, number>>>>;
    assertSpawnArguments(this.#runtime, types, valuesByName);

    const entity = this.#bridge.systemSpawn(this.#engineHandle);
    for (const type of types) {
      this.#queueAdd(entity, type, valuesByName[type.name]);
    }

    return entity;
  }

  /** Deferred until the end of the phase, like remove: removes the entity's state scope, so it survives the change. */
  persist(entity: Entity): void {
    assertContextActive(this, "persist");

    this.#bridge.contextPersist(this.#engineHandle, entity);
  }

  destroy(entity: Entity): void {
    assertContextActive(this, "destroy");

    this.#bridge.systemDestroy(this.#engineHandle, entity);
  }

  add<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value?: Partial<ValueOf<F>>): void;
  add(entity: Entity, type: SchemaType): void;
  add(entity: Entity, type: SchemaType, value?: Partial<Record<string, number>>): void {
    assertContextActive(this, "add");
    if (type.kind === "component") {
      assertKnownFields("add", entity, type, value ?? {});
    }

    this.#queueAdd(entity, type, value);
  }

  remove(entity: Entity, type: SchemaType): void {
    assertContextActive(this, "remove");

    this.#bridge.systemRemove(this.#engineHandle, entity, componentIdOf(this.#runtime, type));
  }

  #queueAdd(entity: Entity, type: SchemaType, value: Partial<Record<string, number>> | undefined): void {
    const componentId = componentIdOf(this.#runtime, type);
    const address = writeValue(this.#bridge, type.kind === "tag" ? null : type.layout, value);

    this.#bridge.systemAdd(this.#engineHandle, entity, componentId, address);
  }
}
