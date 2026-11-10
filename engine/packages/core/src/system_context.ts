import type { Bridge } from "./bridge.js";
import type { Entity } from "./entity.js";
import { ContractError } from "./errors.js";
import type { Phase } from "./engine.js";
import type { Query, QuerySpec } from "./query.js";
import { componentIdOf, type Runtime } from "./runtime.js";
import type { ComponentType, Fields, SchemaType, ValueOf } from "./schema.js";
import { assertKnownFields, assertSpawnArguments, writeComponentValue } from "./values.js";
import type { SpawnValues, World } from "./world.js";

export type SystemFunction = (context: SystemContext) => void;

export let setContextActive!: (context: SystemContext, active: boolean) => void;

// Válido só durante a chamada do seu sistema. Mudanças estruturais (spawn/destroy/add/remove) são adiadas até o fim da fase.
export class SystemContext {
  readonly phase: Phase;
  readonly name: string;

  #active = false;
  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #engineHandle: number;
  readonly #world: World;

  static {
    setContextActive = (context, active) => {
      context.#active = active;
    };
  }

  constructor(phase: Phase, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World) {
    this.phase = phase;
    this.name = name;
    this.#runtime = runtime;
    this.#bridge = bridge;
    this.#engineHandle = engineHandle;
    this.#world = world;
  }

  get fixedDeltaTime(): number {
    this.#assertActive("fixedDeltaTime");

    return this.#bridge.engineFixedDeltaTime(this.#engineHandle);
  }

  get time(): number {
    this.#assertActive("time");

    return this.#bridge.engineTime(this.#engineHandle);
  }

  get interpolation(): number {
    this.#assertActive("interpolation");

    return this.#bridge.engineInterpolation(this.#engineHandle);
  }

  get paused(): boolean {
    this.#assertActive("paused");

    return this.#bridge.engineIsPaused(this.#engineHandle);
  }

  pause(): void {
    this.#assertActive("pause");
    this.#bridge.enginePause(this.#engineHandle);
  }

  resume(): void {
    this.#assertActive("resume");
    this.#bridge.engineResume(this.#engineHandle);
  }

  query(spec: QuerySpec): Query {
    this.#assertActive("query");

    return this.#world.query(spec);
  }

  get<F extends Fields>(entity: Entity, type: ComponentType<string, F>): ValueOf<F> {
    this.#assertActive("get");

    return this.#world.get(entity, type);
  }

  set<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value: Partial<ValueOf<F>>): void {
    this.#assertActive("set");
    this.#world.set(entity, type, value);
  }

  has(entity: Entity, type: SchemaType): boolean {
    this.#assertActive("has");

    return this.#world.has(entity, type);
  }

  isAlive(entity: Entity): boolean {
    this.#assertActive("isAlive");

    return this.#world.isAlive(entity);
  }

  // A entidade só fica viva ao fim da fase; o handle já pode ser usado nos comandos adiados.
  spawn<const Ts extends readonly SchemaType[]>(types: Ts, values?: SpawnValues<Ts>): Entity {
    this.#assertActive("spawn");

    const valuesByName = (values ?? {}) as Readonly<Record<string, Partial<Record<string, number>>>>;
    assertSpawnArguments(this.#runtime, types, valuesByName);

    const entity = this.#bridge.systemSpawn(this.#engineHandle);
    for (const type of types) {
      this.#queueAdd(entity, type, valuesByName[type.name]);
    }

    return entity;
  }

  destroy(entity: Entity): void {
    this.#assertActive("destroy");
    this.#bridge.systemDestroy(this.#engineHandle, entity);
  }

  add<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value?: Partial<ValueOf<F>>): void;
  add(entity: Entity, type: SchemaType): void;
  add(entity: Entity, type: SchemaType, value?: Partial<Record<string, number>>): void {
    this.#assertActive("add");
    if (type.kind === "component") {
      assertKnownFields("add", entity, type, value ?? {});
    }

    this.#queueAdd(entity, type, value);
  }

  remove(entity: Entity, type: SchemaType): void {
    this.#assertActive("remove");
    this.#bridge.systemRemove(this.#engineHandle, entity, componentIdOf(this.#runtime, type));
  }

  #queueAdd(entity: Entity, type: SchemaType, value: Partial<Record<string, number>> | undefined): void {
    const componentId = componentIdOf(this.#runtime, type);
    const address = writeComponentValue(this.#bridge, type, value);

    this.#bridge.systemAdd(this.#engineHandle, entity, componentId, address);
  }

  #assertActive(operation: string): void {
    if (!this.#active) {
      throw new ContractError(`${operation} system=${this.name}: SystemContext usado fora da chamada do sistema`);
    }
  }
}
