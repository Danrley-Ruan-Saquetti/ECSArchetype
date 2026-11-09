import { entityGeneration, entityIndex, packEntity, type Entity } from "./entity.js";
import type { ScalarType } from "./layout.js";
import type { EngineModule } from "./module.js";
import type { Schema } from "./schema.js";

// Views sobre o buffer inteiro do WASM. O buffer muda quando a memória cresce: `refresh` recria as views.
export class Heap {
  buffer!: ArrayBuffer;
  u8!: Uint8Array;
  i8!: Int8Array;
  u16!: Uint16Array;
  i16!: Int16Array;
  u32!: Uint32Array;
  i32!: Int32Array;
  f32!: Float32Array;
  f64!: Float64Array;

  constructor(buffer: ArrayBuffer) {
    this.refresh(buffer);
  }

  refresh(buffer: ArrayBuffer): void {
    this.buffer = buffer;
    this.u8 = new Uint8Array(buffer);
    this.i8 = new Int8Array(buffer);
    this.u16 = new Uint16Array(buffer);
    this.i16 = new Int16Array(buffer);
    this.u32 = new Uint32Array(buffer);
    this.i32 = new Int32Array(buffer);
    this.f32 = new Float32Array(buffer);
    this.f64 = new Float64Array(buffer);
  }
}

export function readScalar(heap: Heap, type: ScalarType, address: number): number {
  switch (type) {
    case "f32":
      return heap.f32[address >> 2]!;
    case "f64":
      return heap.f64[address >> 3]!;
    case "i8":
      return heap.i8[address]!;
    case "u8":
      return heap.u8[address]!;
    case "i16":
      return heap.i16[address >> 1]!;
    case "u16":
      return heap.u16[address >> 1]!;
    case "i32":
      return heap.i32[address >> 2]!;
    case "u32":
      return heap.u32[address >> 2]!;
    case "entity":
      return packEntity(heap.u32[address >> 2]!, heap.u32[(address >> 2) + 1]!);
  }
}

export function writeScalar(heap: Heap, type: ScalarType, address: number, value: number): void {
  switch (type) {
    case "f32":
      heap.f32[address >> 2] = value;
      return;
    case "f64":
      heap.f64[address >> 3] = value;
      return;
    case "i8":
      heap.i8[address] = value;
      return;
    case "u8":
      heap.u8[address] = value;
      return;
    case "i16":
      heap.i16[address >> 1] = value;
      return;
    case "u16":
      heap.u16[address >> 1] = value;
      return;
    case "i32":
      heap.i32[address >> 2] = value;
      return;
    case "u32":
      heap.u32[address >> 2] = value;
      return;
    case "entity":
      heap.u32[address >> 2] = entityIndex(value);
      heap.u32[(address >> 2) + 1] = entityGeneration(value);
      return;
  }
}

const OutputOffset = 0; // EngineEntity (8 B) e depois o ponteiro de descritores (4 B)
const DescriptorsOffset = 8;
const ValueOffset = 16;
const MessageCapacity = 256;
const InitialIdCapacity = 32;

// Único lugar que chama exports do WASM. Toda chamada que pode alocar no WASM termina em `sync()`.
export class Bridge {
  readonly module: EngineModule;
  readonly heap: Heap;

  private scratch: number;
  private valueCapacity: number;
  private idsPointer: number;
  private idsCapacity = InitialIdCapacity;

  constructor(module: EngineModule, schema: Schema) {
    this.module = module;
    this.heap = new Heap(module.HEAPU8.buffer as ArrayBuffer);

    this.valueCapacity = Math.max(8, ...schema.types.map((type) => (type.kind === "component" ? type.layout.size : 0)));
    this.scratch = this.allocate(ValueOffset + this.valueCapacity + MessageCapacity);
    this.idsPointer = this.allocate(this.idsCapacity * 4);
  }

  // Área de rascunho para montar o valor de um componente (zerada pelo chamador).
  get valuePointer(): number {
    return this.scratch + ValueOffset;
  }

  get valueCapacityBytes(): number {
    return this.valueCapacity;
  }

  sync(): void {
    const buffer = this.module.HEAPU8.buffer as ArrayBuffer;
    if (buffer !== this.heap.buffer) {
      this.heap.refresh(buffer);
    }
  }

  allocate(bytes: number): number {
    const pointer = this.module._malloc(bytes) >>> 0;
    this.sync();

    return pointer;
  }

  free(pointer: number): void {
    this.module._free(pointer);
  }

  worldCreate(): number {
    const world = this.module._engineWorldCreate() >>> 0;
    this.sync();

    return world;
  }

  worldDestroy(world: number): void {
    this.module._engineWorldDestroy(world);
  }

  create(world: number): Entity {
    this.module._engineCreate(world, this.scratch + OutputOffset);
    this.sync();
    const base = (this.scratch + OutputOffset) >> 2;

    return packEntity(this.heap.u32[base]!, this.heap.u32[base + 1]!);
  }

  destroy(world: number, entity: Entity): void {
    this.module._engineDestroy(world, entityIndex(entity), entityGeneration(entity));
    this.sync();
  }

  isAlive(world: number, entity: Entity): boolean {
    return this.module._engineIsAlive(world, entityIndex(entity), entityGeneration(entity)) === 1;
  }

  add(world: number, entity: Entity, componentId: number, valuePointer: number): void {
    this.module._engineAdd(world, entityIndex(entity), entityGeneration(entity), componentId, valuePointer);
    this.sync();
  }

  remove(world: number, entity: Entity, componentId: number): void {
    this.module._engineRemove(world, entityIndex(entity), entityGeneration(entity), componentId);
    this.sync();
  }

  has(world: number, entity: Entity, componentId: number): boolean {
    return this.module._engineHas(world, entityIndex(entity), entityGeneration(entity), componentId) === 1;
  }

  // Endereço do componente, ou 0 se a entidade está morta, o componente ausente ou é tag.
  get(world: number, entity: Entity, componentId: number): number {
    return this.module._engineGet(world, entityIndex(entity), entityGeneration(entity), componentId) >>> 0;
  }

  query(world: number, ids: readonly number[], readCount: number, writeCount: number, withCount: number, withoutCount: number): number {
    if (ids.length > this.idsCapacity) {
      this.free(this.idsPointer);
      this.idsCapacity = Math.max(ids.length, this.idsCapacity * 2);
      this.idsPointer = this.allocate(this.idsCapacity * 4);
    }

    const base = this.idsPointer >> 2;
    ids.forEach((id, position) => {
      this.heap.u32[base + position] = id;
    });

    const query = this.module._engineQuery(world, this.idsPointer, readCount, writeCount, withCount, withoutCount) >>> 0;
    this.sync();

    return query;
  }

  queryBegin(world: number, query: number): { blockCount: number; descriptors: number } {
    const blockCount = this.module._engineQueryBegin(world, query, this.scratch + DescriptorsOffset) >>> 0;
    this.sync();

    return { blockCount, descriptors: this.heap.u32[(this.scratch + DescriptorsOffset) >> 2]! };
  }

  queryEnd(world: number, query: number): void {
    this.module._engineQueryEnd(world, query);
  }

  checkInvariants(world: number): string | null {
    const message = this.scratch + ValueOffset + this.valueCapacity;
    const consistent = this.module._engineCheckInvariants(world, message, MessageCapacity) === 1;
    this.sync();
    if (consistent) {
      return null;
    }

    let end = message;
    while (this.heap.u8[end] !== 0) {
      ++end;
    }

    return new TextDecoder().decode(this.heap.u8.subarray(message, end));
  }
}
