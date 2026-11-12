import { packEntity, type Entity } from "./entity.js";
import { ContractError } from "./errors.js";
import { readScalar, type Bridge } from "./bridge.js";
import { makeView, type ReadView, type WriteView } from "./fields.js";
import type { ComponentType, Fields, SchemaType, ValueOf } from "./schema.js";
import { assertWorldUsable, enterIteration, leaveIteration, type World } from "./world.js";

export interface QuerySpec {
  read?: readonly ComponentType[];
  write?: readonly ComponentType[];
  with?: readonly SchemaType[];
  without?: readonly SchemaType[];
}

const BlockMessage = "bloco usado fora do callback de eachBlock";

// Descritor de bloco de olyQueryBegin, em u32 no wasm32: [count, endereço das entidades, endereço de cada coluna...].
const DescriptorHeaderLength = 2;

let invalidateBlock!: (block: Block) => void;

/**
 * Uma tabela do ECS: `count` linhas, as entidades e as colunas pedidas pela query. O bloco e as views de
 * `read`/`write` só valem dentro do callback de eachBlock (a mudança estrutural invalidaria os endereços).
 * Fora dele, os métodos do bloco lançam ContractError; as views não são conferidas.
 */
export class Block {
  readonly count: number;

  #valid = true;
  readonly #bridge: Bridge;
  readonly #components: readonly ComponentType[];
  readonly #writeTypes: readonly ComponentType[];
  readonly #entitiesAddress: number;
  readonly #columnAddresses: Uint32Array;
  readonly #views = new Map<ComponentType, WriteView<Fields>>();

  static {
    invalidateBlock = (block) => {
      block.#valid = false;
    };
  }

  constructor(
    bridge: Bridge,
    components: readonly ComponentType[],
    writeTypes: readonly ComponentType[],
    entitiesAddress: number,
    columnAddresses: Uint32Array,
    count: number,
  ) {
    this.#bridge = bridge;
    this.#components = components;
    this.#writeTypes = writeTypes;
    this.#entitiesAddress = entitiesAddress;
    this.#columnAddresses = columnAddresses;
    this.count = count;
  }

  entity(row: number): Entity {
    this.#assertValid("entity");

    const position = (this.#entitiesAddress >> 2) + row * 2;

    return packEntity(this.#bridge.heap.u32[position]!, this.#bridge.heap.u32[position + 1]!);
  }

  read<F extends Fields>(type: ComponentType<string, F>): ReadView<F> {
    this.#assertValid("read", type);

    return this.#viewOf("read", type) as unknown as ReadView<F>;
  }

  write<F extends Fields>(type: ComponentType<string, F>): WriteView<F> {
    this.#assertValid("write", type);
    if (!this.#writeTypes.includes(type)) {
      throw new ContractError(`write type=${type.name}: tipo não está em write na query (só em read ou fora dela)`);
    }

    return this.#viewOf("write", type) as unknown as WriteView<F>;
  }

  #viewOf(operation: string, type: ComponentType): WriteView<Fields> {
    const cached = this.#views.get(type);
    if (cached) {
      return cached;
    }

    // Posição do tipo entre as colunas do descritor (read seguido de write).
    const position = this.#components.indexOf(type);
    if (position < 0) {
      throw new ContractError(`${operation} type=${type.name}: tipo não está na query`);
    }

    const view = makeView(this.#bridge.heap, type.layout, this.#columnAddresses[position]!);
    this.#views.set(type, view);

    return view;
  }

  #assertValid(operation: string, type?: SchemaType): void {
    if (!this.#valid) {
      throw new ContractError(`${operation}${type ? ` type=${type.name}` : ""}: ${BlockMessage}`);
    }
  }
}


let makeSingleRow!: (entity: Entity, values: ReadonlyMap<ComponentType, unknown>) => SingleRow;

/**
 * A única linha de uma query: a entidade e uma cópia dos componentes de `read` e `write`, feita no momento
 * da consulta. Mudar a cópia não muda o World (use `world.set`).
 */
export class SingleRow {
  readonly entity: Entity;

  readonly #values: ReadonlyMap<ComponentType, unknown>;

  static {
    makeSingleRow = (entity, values) => new SingleRow(entity, values);
  }

  private constructor(entity: Entity, values: ReadonlyMap<ComponentType, unknown>) {
    this.entity = entity;
    this.#values = values;
  }

  get<F extends Fields>(type: ComponentType<string, F>): ValueOf<F> {
    const value = this.#values.get(type);
    if (value === undefined) {
      throw new ContractError(`get type=${type.name}: tipo não está na query`);
    }

    return value as ValueOf<F>;
  }
}

export class Query {
  readonly #world: World;
  readonly #bridge: Bridge;
  readonly #handle: number;
  readonly #writeTypes: readonly ComponentType[];
  readonly #components: readonly ComponentType[];

  constructor(
    world: World,
    bridge: Bridge,
    handle: number,
    readTypes: readonly ComponentType[],
    writeTypes: readonly ComponentType[],
  ) {
    this.#world = world;
    this.#bridge = bridge;
    this.#handle = handle;
    this.#writeTypes = writeTypes;
    this.#components = [...readTypes, ...writeTypes];
  }

  /** Durante o callback, mudança estrutural direta no World (spawn/destroy/add/remove) lança ContractError. */
  eachBlock(callback: (block: Block) => void): void {
    assertWorldUsable(this.#world, "eachBlock");

    const { iteration, blockCount, descriptors } = this.#bridge.queryBegin(this.#world.handle, this.#handle);
    const stride = DescriptorHeaderLength + this.#components.length;
    const start = descriptors >> 2;
    // Cópia: a memória do WASM pode crescer durante o callback e invalidar a view sobre o heap.
    const copy = this.#bridge.heap.u32.slice(start, start + blockCount * stride);

    enterIteration(this.#world);
    let current: Block | null = null;
    try {
      for (let index = 0; index < blockCount; ++index) {
        const offset = index * stride;
        current = new Block(this.#bridge, this.#components, this.#writeTypes, copy[offset + 1]!, copy.subarray(offset + DescriptorHeaderLength, offset + stride), copy[offset]!);
        callback(current);
        invalidateBlock(current);
      }
    } finally {
      if (current !== null) {
        invalidateBlock(current);
      }
      leaveIteration(this.#world);
      this.#bridge.queryEnd(this.#world.handle, iteration);
    }
  }

  /** Exatamente uma linha entre todas as tabelas casadas; senão ContractError. Cruza a fronteira duas vezes (Begin/End). */
  single(): SingleRow {
    return this.#readSingle("single", true)!;
  }

  /** `null` sem linhas; mais de uma é ContractError. */
  trySingle(): SingleRow | null {
    return this.#readSingle("trySingle", false);
  }

  #readSingle(operation: string, required: boolean): SingleRow | null {
    assertWorldUsable(this.#world, operation);

    const { iteration, blockCount, descriptors } = this.#bridge.queryBegin(this.#world.handle, this.#handle);
    const stride = DescriptorHeaderLength + this.#components.length;
    const start = descriptors >> 2;
    let rowCount = 0;
    let row: SingleRow | null = null;
    try {
      const words = this.#bridge.heap.u32;
      let firstBlockOffset = -1;
      for (let index = 0; index < blockCount; ++index) {
        const count = words[start + index * stride]!;
        if (count > 0 && firstBlockOffset < 0) {
          firstBlockOffset = start + index * stride;
        }

        rowCount += count;
      }

      if (rowCount === 1) {
        row = this.#copyFirstRow(firstBlockOffset);
      }
    } finally {
      this.#bridge.queryEnd(this.#world.handle, iteration);
    }

    if (rowCount > 1 || (rowCount === 0 && required)) {
      const expected = required ? "1" : "0 ou 1";
      throw new ContractError(`${operation}: query com ${rowCount} linhas; esperava ${expected}`);
    }

    return row;
  }

  #copyFirstRow(blockOffset: number): SingleRow {
    const heap = this.#bridge.heap;
    const entityPosition = heap.u32[blockOffset + 1]! >> 2;
    const values = new Map<ComponentType, unknown>();
    this.#components.forEach((type, position) => {
      const columnAddress = heap.u32[blockOffset + DescriptorHeaderLength + position]!;
      const value: Record<string, number> = {};
      for (const field of type.layout.fields) {
        value[field.name] = readScalar(heap, field.type, columnAddress + field.offset);
      }

      values.set(type, value);
    });

    return makeSingleRow(packEntity(heap.u32[entityPosition]!, heap.u32[entityPosition + 1]!), values);
  }
}
