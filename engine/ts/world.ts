import { readScalar, writeScalar, type Bridge } from "./bridge.js";
import { formatEntity, type Entity } from "./entity.js";
import { EngineContractError } from "./errors.js";
import type { EngineRuntime } from "./runtime.js";
import type { ComponentType, Fields, SchemaType, ValueOf } from "./schema.js";

export type SpawnValues<Ts extends readonly SchemaType[]> = {
  [C in Extract<Ts[number], ComponentType> as C["name"]]?: Partial<ValueOf<C["fields"]>>;
};

export class World {
  readonly handle: number;
  // Aumentado por Query.eachBlock: mudança estrutural com iteração em curso é erro de contrato.
  iterationDepth = 0;

  private readonly runtime: EngineRuntime;
  private readonly bridge: Bridge;
  private disposed = false;

  constructor(runtime: EngineRuntime, handle: number) {
    this.runtime = runtime;
    this.bridge = runtime.bridge;
    this.handle = handle;
  }

  spawn<const Ts extends readonly SchemaType[]>(types: Ts, values?: SpawnValues<Ts>): Entity {
    this.assertStructuralChangeAllowed("spawn");

    const seen = new Set<SchemaType>();
    for (const type of types) {
      this.runtime.componentId(type);
      if (seen.has(type)) {
        throw new EngineContractError(`spawn type=${type.name}: tipo repetido em types`);
      }
      seen.add(type);
    }

    const valuesByName = (values ?? {}) as Readonly<Record<string, Partial<Record<string, number>>>>;
    for (const [name, value] of Object.entries(valuesByName)) {
      const type = types.find((candidate) => candidate.name === name);
      if (type === undefined || type.kind !== "component") {
        throw new EngineContractError(`spawn type=${name}: valor para componente que não está em types`);
      }
      this.assertKnownFields("spawn", null, type, value);
    }

    const entity = this.bridge.create(this.handle);
    for (const type of types) {
      this.addToEntity(entity, type, valuesByName[type.name]);
    }

    return entity;
  }

  destroy(entity: Entity): void {
    this.assertStructuralChangeAllowed("destroy", entity);
    this.assertAlive("destroy", entity);

    this.bridge.destroy(this.handle, entity);
  }

  isAlive(entity: Entity): boolean {
    this.assertUsable("isAlive");

    return this.bridge.isAlive(this.handle, entity);
  }

  has(entity: Entity, type: SchemaType): boolean {
    this.assertUsable("has");

    return this.bridge.has(this.handle, entity, this.runtime.componentId(type));
  }

  add<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value?: Partial<ValueOf<F>>): void;
  add(entity: Entity, type: SchemaType): void;
  add(entity: Entity, type: SchemaType, value?: Partial<Record<string, number>>): void {
    this.assertStructuralChangeAllowed("add", entity, type);
    this.assertAlive("add", entity, type);
    if (type.kind === "component") {
      this.assertKnownFields("add", entity, type, value ?? {});
    }

    this.addToEntity(entity, type, value);
  }

  remove(entity: Entity, type: SchemaType): void {
    this.assertStructuralChangeAllowed("remove", entity, type);
    this.assertAlive("remove", entity, type);

    this.bridge.remove(this.handle, entity, this.runtime.componentId(type));
  }

  get<F extends Fields>(entity: Entity, type: ComponentType<string, F>): ValueOf<F> {
    const address = this.addressOf("get", entity, type);
    const result: Record<string, number> = {};
    for (const field of type.layout.fields) {
      result[field.name] = readScalar(this.bridge.heap, field.type, address + field.offset);
    }

    return result as ValueOf<F>;
  }

  set<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value: Partial<ValueOf<F>>): void {
    const address = this.addressOf("set", entity, type);
    this.assertKnownFields("set", entity, type, value);

    for (const field of type.layout.fields) {
      const fieldValue = (value as Partial<Record<string, number>>)[field.name];
      if (fieldValue !== undefined) {
        writeScalar(this.bridge.heap, field.type, address + field.offset, fieldValue);
      }
    }
  }

  checkInvariants(): string | null {
    this.assertUsable("checkInvariants");

    return this.bridge.checkInvariants(this.handle);
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }

    this.disposed = true;
    this.bridge.worldDestroy(this.handle);
  }

  private addToEntity(entity: Entity, type: SchemaType, value: Partial<Record<string, number>> | undefined): void {
    const componentId = this.runtime.componentId(type);
    if (type.kind === "tag") {
      this.bridge.add(this.handle, entity, componentId, 0);
      return;
    }

    const address = this.bridge.valuePointer;
    this.bridge.heap.u8.fill(0, address, address + type.layout.size);
    for (const field of type.layout.fields) {
      const fieldValue = value?.[field.name];
      if (fieldValue !== undefined) {
        writeScalar(this.bridge.heap, field.type, address + field.offset, fieldValue);
      }
    }

    this.bridge.add(this.handle, entity, componentId, address);
  }

  private addressOf(operation: string, entity: Entity, type: ComponentType<string, Fields>): number {
    this.assertUsable(operation);
    this.assertAlive(operation, entity, type);

    const componentId = this.runtime.componentId(type);
    const address = this.bridge.get(this.handle, entity, componentId);
    if (address === 0) {
      throw this.contractError(operation, entity, type, "entidade não tem o componente");
    }

    return address;
  }

  private assertKnownFields(operation: string, entity: Entity | null, type: ComponentType, value: Partial<Record<string, number>>): void {
    const known = new Set(type.layout.fields.map((field) => field.name));
    for (const name of Object.keys(value)) {
      if (!known.has(name)) {
        throw this.contractError(operation, entity, type, `campo desconhecido "${name}"`);
      }
    }
  }

  private assertUsable(operation: string): void {
    if (this.disposed) {
      throw new EngineContractError(`${operation}: World já liberado`);
    }
  }

  private assertStructuralChangeAllowed(operation: string, entity: Entity | null = null, type: SchemaType | null = null): void {
    this.assertUsable(operation);
    if (this.iterationDepth > 0) {
      throw this.contractError(operation, entity, type, "mudança estrutural durante eachBlock");
    }
  }

  private assertAlive(operation: string, entity: Entity, type: SchemaType | null = null): void {
    if (!this.bridge.isAlive(this.handle, entity)) {
      throw this.contractError(operation, entity, type, "entidade morta");
    }
  }

  private contractError(operation: string, entity: Entity | null, type: SchemaType | null, reason: string): EngineContractError {
    const entityPart = entity === null ? "" : ` e=${formatEntity(entity)}`;
    const typePart = type === null ? "" : ` type=${type.name}`;

    return new EngineContractError(`${operation}${entityPart}${typePart}: ${reason}`);
  }
}
