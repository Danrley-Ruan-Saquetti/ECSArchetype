import type { Bridge } from "./bridge.js";
import { packEntity, type Entity } from "./entity.js";
import { EngineContractError } from "./errors.js";
import { makeView, type ReadView, type WriteView } from "./fields.js";
import type { ComponentType, Fields, SchemaType } from "./schema.js";
import type { World } from "./world.js";

export interface QuerySpec {
  read?: readonly ComponentType[];
  write?: readonly ComponentType[];
  with?: readonly SchemaType[];
  without?: readonly SchemaType[];
}

const BlockMessage = "bloco usado fora do callback de eachBlock";

// Uma tabela do ECS: `count` linhas, as entidades e as colunas pedidas pela query.
// Só vale dentro do callback de eachBlock (a mudança estrutural invalidaria os endereços).
export class Block {
  readonly count: number;

  private valid = true;
  private readonly views = new Map<ComponentType, WriteView<Fields>>();

  constructor(
    private readonly bridge: Bridge,
    private readonly query: Query,
    private readonly entitiesAddress: number,
    private readonly columnAddresses: Uint32Array,
    count: number,
  ) {
    this.count = count;
  }

  invalidate(): void {
    this.valid = false;
  }

  entity(row: number): Entity {
    this.assertValid("entity");
    const position = (this.entitiesAddress >> 2) + row * 2;

    return packEntity(this.bridge.heap.u32[position]!, this.bridge.heap.u32[position + 1]!);
  }

  read<F extends Fields>(type: ComponentType<string, F>): ReadView<F> {
    this.assertValid("read", type);

    return this.viewOf("read", type) as unknown as ReadView<F>;
  }

  write<F extends Fields>(type: ComponentType<string, F>): WriteView<F> {
    this.assertValid("write", type);
    if (!this.query.writes(type)) {
      throw new EngineContractError(`write type=${type.name}: tipo não está em write na query (só em read ou fora dela)`);
    }

    return this.viewOf("write", type) as unknown as WriteView<F>;
  }

  private viewOf(operation: string, type: ComponentType): WriteView<Fields> {
    const cached = this.views.get(type);
    if (cached) {
      return cached;
    }

    const position = this.query.positionOf(type);
    if (position < 0) {
      throw new EngineContractError(`${operation} type=${type.name}: tipo não está na query`);
    }

    const view = makeView(this.bridge.heap, type.layout, this.columnAddresses[position]!);
    this.views.set(type, view);

    return view;
  }

  private assertValid(operation: string, type?: SchemaType): void {
    if (!this.valid) {
      throw new EngineContractError(`${operation}${type ? ` type=${type.name}` : ""}: ${BlockMessage}`);
    }
  }
}

export class Query {
  private readonly components: readonly ComponentType[];

  constructor(
    private readonly world: World,
    private readonly bridge: Bridge,
    private readonly handle: number,
    private readonly readTypes: readonly ComponentType[],
    private readonly writeTypes: readonly ComponentType[],
  ) {
    this.components = [...readTypes, ...writeTypes];
  }

  writes(type: ComponentType): boolean {
    return this.writeTypes.includes(type);
  }

  // Posição do tipo entre as colunas do descritor (read seguido de write), ou -1.
  positionOf(type: ComponentType): number {
    return this.components.indexOf(type);
  }

  eachBlock(callback: (block: Block) => void): void {
    this.world.assertUsable("eachBlock");

    const { blockCount, descriptors } = this.bridge.queryBegin(this.world.handle, this.handle);
    const stride = 2 + this.components.length;
    // Cópia: um eachBlock aninhado sobrescreve o buffer de descritores do lado C++.
    const start = descriptors >> 2;
    const copy = this.bridge.heap.u32.slice(start, start + blockCount * stride);

    this.world.iterationDepth++;
    let current: Block | null = null;
    try {
      for (let index = 0; index < blockCount; ++index) {
        const offset = index * stride;
        current = new Block(this.bridge, this, copy[offset + 1]!, copy.subarray(offset + 2, offset + stride), copy[offset]!);
        callback(current);
        current.invalidate();
      }
    } finally {
      current?.invalidate();
      this.world.iterationDepth--;
      this.bridge.queryEnd(this.world.handle, this.handle);
    }
  }
}
