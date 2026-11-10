import type { Bridge } from "./bridge.js";
import { assertContextActive, EngineContext } from "./context.js";
import type { Entity } from "./entity.js";
import type { Phase } from "./engine.js";
import { componentIdOf, type Runtime } from "./runtime.js";
import type { ComponentType, Fields, SchemaType, ValueOf } from "./schema.js";
import { assertKnownFields, assertSpawnArguments, writeValue } from "./values.js";
import type { SpawnValues, World } from "./world.js";

export type SystemFunction = (context: SystemContext) => void;

/**
 * Válido só durante a chamada do seu sistema; fora dela, qualquer uso lança ContractError. Mudanças estruturais
 * (spawn/destroy/add/remove) são adiadas até o fim da fase.
 */
export class SystemContext extends EngineContext {
  readonly phase: Phase;
  readonly name: string;

  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #engineHandle: number;

  constructor(phase: Phase, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World) {
    super({ label: "system", className: "SystemContext", callNoun: "sistema" }, name, runtime, bridge, engineHandle, world);
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

  /** A entidade só fica viva ao fim da fase; o handle já pode ser usado nos comandos adiados. */
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
