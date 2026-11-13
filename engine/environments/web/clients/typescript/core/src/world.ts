import { readScalar, writeScalar, type Bridge } from "./bridge.js";
import { type Entity } from "./entity.js";
import { ContractError } from "./errors.js";
import { bridgeOf, componentIdOf, type Runtime } from "./runtime.js";
import type { ComponentType, Fields, SchemaType, ValueOf } from "./schema.js";
import { Query, type QuerySpec } from "./query.js";
import { assertKnownFields, assertSpawnArguments, contractError, writeValue } from "./values.js";

export type SpawnValues<Ts extends readonly SchemaType[]> = {
  [C in Extract<Ts[number], ComponentType> as C["name"]]?: Partial<ValueOf<C["fields"]>>;
};

interface WorldInternals {
  assertUsable(world: World, operation: string): void;
  enterIteration(world: World): void;
  leaveIteration(world: World): void;
  setStructureLocked(world: World, locked: boolean): void;
  release(world: World): void;
  spawnWith(world: World, types: readonly SchemaType[], values: SpawnValuesByName | undefined, create: () => Entity): Entity;
}

type SpawnValuesByName = Readonly<Record<string, Partial<Record<string, number>>>>;

let worldInternals!: WorldInternals;

/**
 * Throws ContractError for use after `dispose`, a dead entity, a missing component in `get`/`set`, an unknown
 * field and a direct structural change during eachBlock or during `Engine.frame`.
 */
export class World {
  readonly handle: number;

  // Incremented by Query.eachBlock: a structural change with an iteration in progress is a contract error.
  #iterationDepth = 0;
  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #owned: boolean;
  #disposed = false;
  // Set by the Engine during frame: structural changes go through the SystemContext (deferred).
  #structureLocked = false;
  readonly #queries = new Map<string, Query>();

  static {
    worldInternals = {
      assertUsable: (world, operation) => world.#assertUsable(operation),
      enterIteration: (world) => {
        ++world.#iterationDepth;
      },
      leaveIteration: (world) => {
        --world.#iterationDepth;
      },
      setStructureLocked: (world, locked) => {
        world.#structureLocked = locked;
      },
      release: (world) => {
        world.#disposed = true;
      },
      spawnWith: (world, types, values, create) => world.#spawnWith(types, values, create),
    };
  }

  // `owned` false: the World belongs to an Engine, which disposes it along with itself.
  constructor(runtime: Runtime, handle: number, owned = true) {
    this.#runtime = runtime;
    this.#bridge = bridgeOf(runtime);
    this.#owned = owned;
    this.handle = handle;
  }

  spawn<const Ts extends readonly SchemaType[]>(types: Ts, values?: SpawnValues<Ts>): Entity {
    return this.#spawnWith(types, values as SpawnValuesByName | undefined, () => this.#bridge.spawn(this.handle));
  }

  destroy(entity: Entity): void {
    this.#assertStructuralChangeAllowed("destroy", entity);
    this.#assertAlive("destroy", entity);

    this.#bridge.destroy(this.handle, entity);
  }

  isAlive(entity: Entity): boolean {
    this.#assertUsable("isAlive");

    return this.#bridge.isAlive(this.handle, entity);
  }

  has(entity: Entity, type: SchemaType): boolean {
    this.#assertUsable("has");

    return this.#bridge.has(this.handle, entity, componentIdOf(this.#runtime, type));
  }

  add<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value?: Partial<ValueOf<F>>): void;
  add(entity: Entity, type: SchemaType): void;
  add(entity: Entity, type: SchemaType, value?: Partial<Record<string, number>>): void {
    this.#assertStructuralChangeAllowed("add", entity, type);
    this.#assertAlive("add", entity, type);
    if (type.kind === "component") {
      assertKnownFields("add", entity, type, value ?? {});
    }

    this.#addToEntity(entity, type, value);
  }

  remove(entity: Entity, type: SchemaType): void {
    this.#assertStructuralChangeAllowed("remove", entity, type);
    this.#assertAlive("remove", entity, type);

    this.#bridge.remove(this.handle, entity, componentIdOf(this.#runtime, type));
  }

  get<F extends Fields>(entity: Entity, type: ComponentType<string, F>): ValueOf<F> {
    const address = this.#addressOf("get", entity, type);
    const result: Record<string, number> = {};
    for (const field of type.layout.fields) {
      result[field.name] = readScalar(this.#bridge.heap, field.type, address + field.offset);
    }

    return result as ValueOf<F>;
  }

  set<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value: Partial<ValueOf<F>>): void {
    const address = this.#addressOf("set", entity, type);
    assertKnownFields("set", entity, type, value);

    for (const field of type.layout.fields) {
      const fieldValue = (value as Partial<Record<string, number>>)[field.name];
      if (fieldValue !== undefined) {
        writeScalar(this.#bridge.heap, field.type, address + field.offset, fieldValue);
      }
    }
  }

  /**
   * Memoized by spec: the same combination (with the read/write order) returns the same Query. Throws
   * ContractError for a tag in read/write or a type repeated in read/write.
   */
  query(spec: QuerySpec): Query {
    this.#assertUsable("query");

    const read = spec.read ?? [];
    const write = spec.write ?? [];
    const withTypes = spec.with ?? [];
    const withoutTypes = spec.without ?? [];

    const seen = new Set<SchemaType>();
    for (const type of [...read, ...write] as SchemaType[]) {
      componentIdOf(this.#runtime, type);
      if (type.kind === "tag") {
        throw new ContractError(`query type=${type.name}: a tag cannot go in read or write; use with/without`);
      }
      if (seen.has(type)) {
        throw new ContractError(`query type=${type.name}: type repeated in read/write`);
      }
      seen.add(type);
    }

    const filterIndexes = (types: readonly SchemaType[]): number[] => {
      types.forEach((type) => componentIdOf(this.#runtime, type));

      return [...new Set(types.map((type) => type.schemaIndex))].sort((first, second) => first - second);
    };
    const withIndexes = filterIndexes(withTypes);
    const withoutIndexes = filterIndexes(withoutTypes);

    const key = [read, write].map((types) => types.map((type) => type.schemaIndex).join(",")).join("|") + `|${withIndexes}|${withoutIndexes}`;
    const cached = this.#queries.get(key);
    if (cached) {
      return cached;
    }

    const ids = [...read, ...write, ...withIndexes.map((index) => this.#runtime.schema.types[index]!), ...withoutIndexes.map((index) => this.#runtime.schema.types[index]!)].map(
      (type) => componentIdOf(this.#runtime, type),
    );
    const handle = this.#bridge.queryCreate(this.handle, ids, read.length, write.length, withIndexes.length, withoutIndexes.length);
    const query = new Query(this, this.#bridge, handle, read, write);
    this.#queries.set(key, query);

    return query;
  }

  checkInvariants(): string | null {
    this.#assertUsable("checkInvariants");

    return this.#bridge.checkInvariants(this.handle);
  }

  dispose(): void {
    if (this.#disposed) {
      return;
    }
    if (!this.#owned) {
      throw new ContractError("dispose: World belongs to the Engine; use engine.dispose()");
    }
    if (this.#iterationDepth > 0) {
      throw new ContractError("dispose: World in iteration (eachBlock); dispose after the loop");
    }

    this.#disposed = true;
    this.#bridge.worldDestroy(this.handle);
  }

  #addToEntity(entity: Entity, type: SchemaType, value: Partial<Record<string, number>> | undefined): void {
    const componentId = componentIdOf(this.#runtime, type);
    const address = writeValue(this.#bridge, type.kind === "tag" ? null : type.layout, value);

    this.#bridge.add(this.handle, entity, componentId, address);
  }

  #addressOf(operation: string, entity: Entity, type: ComponentType<string, Fields>): number {
    this.#assertUsable(operation);
    this.#assertAlive(operation, entity, type);

    const componentId = componentIdOf(this.#runtime, type);
    const address = this.#bridge.get(this.handle, entity, componentId);
    if (address === 0) {
      throw contractError(operation, entity, type, "entity does not have the component");
    }

    return address;
  }

  #assertUsable(operation: string): void {
    if (this.#disposed) {
      throw new ContractError(`${operation}: World already disposed`);
    }
  }

  #spawnWith(types: readonly SchemaType[], values: SpawnValuesByName | undefined, create: () => Entity): Entity {
    this.#assertStructuralChangeAllowed("spawn");

    const valuesByName = values ?? {};
    assertSpawnArguments(this.#runtime, types, valuesByName);

    const entity = create();
    for (const type of types) {
      this.#addToEntity(entity, type, valuesByName[type.name]);
    }

    return entity;
  }

  #assertStructuralChangeAllowed(operation: string, entity: Entity | null = null, type: SchemaType | null = null): void {
    this.#assertUsable(operation);
    if (this.#structureLocked) {
      throw contractError(operation, entity, type, "direct structural change during frame; use context.spawn/destroy/add/remove");
    }
    if (this.#iterationDepth > 0) {
      throw contractError(operation, entity, type, "structural change during eachBlock");
    }
  }

  #assertAlive(operation: string, entity: Entity, type: SchemaType | null = null): void {
    if (!this.#bridge.isAlive(this.handle, entity)) {
      throw contractError(operation, entity, type, "dead entity");
    }
  }
}

export function assertWorldUsable(world: World, operation: string): void {
  worldInternals.assertUsable(world, operation);
}

export function enterIteration(world: World): void {
  worldInternals.enterIteration(world);
}

export function leaveIteration(world: World): void {
  worldInternals.leaveIteration(world);
}

// What World.spawn does, with the entity creation chosen by the caller (the direct context creates through the core,
// which applies the state scope).
export function spawnWith(world: World, types: readonly SchemaType[], values: SpawnValuesByName | undefined, create: () => Entity): Entity {
  return worldInternals.spawnWith(world, types, values, create);
}

export function setStructureLocked(world: World, locked: boolean): void {
  worldInternals.setStructureLocked(world, locked);
}

// The Engine that owns the World disposes it along with the OlyEngine: here it only marks it as disposed.
export function releaseOwnedWorld(world: World): void {
  worldInternals.release(world);
}
