import { entityGeneration, entityIndex, packEntity, type Entity } from "./entity.js";
import type { ScalarType } from "./layout.js";
import type { OlyModule } from "./module.js";
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

// Área de rascunho: OlyEntity de saída (8 B), ponteiro de descritores (4 B), nº de blocos (4 B), valor de um
// componente ou evento (`valueCapacity` B) e, por último, a mensagem de checkInvariants.
const OutputOffset = 0;
const DescriptorsOffset = 8;
const BlockCountOffset = 12;
const ValueOffset = 16;
const MessageCapacity = 256;

const InitialIdCapacity = 32;
const InitialStringCapacity = 64;

// Único lugar que chama exports do WASM. Toda chamada que pode alocar no WASM termina em `sync()`.
export class Bridge {
  readonly module: OlyModule;
  readonly heap: Heap;

  private scratch: number;
  private valueCapacity: number;
  private idsPointer: number;
  private idsCapacity = InitialIdCapacity;
  private stringPointer: number;
  private stringCapacity = InitialStringCapacity;

  constructor(module: OlyModule, schema: Schema) {
    this.module = module;
    this.heap = new Heap(module.HEAPU8.buffer as ArrayBuffer);

    this.valueCapacity = Math.max(
      8,
      ...schema.types.map((type) => (type.kind === "component" ? type.layout.size : 0)),
      ...schema.events.map((type) => type.layout.size),
    );
    this.scratch = this.allocate(ValueOffset + this.valueCapacity + MessageCapacity);
    this.idsPointer = this.allocate(this.idsCapacity * 4);
    this.stringPointer = this.allocate(this.stringCapacity);
  }

  // Área de rascunho para montar o valor de um componente ou evento (zerada pelo chamador).
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
    const world = this.module._olyWorldCreate() >>> 0;
    this.sync();

    return world;
  }

  worldDestroy(world: number): void {
    this.module._olyWorldDestroy(world);
  }

  spawn(world: number): Entity {
    this.module._olyEntitySpawn(world, this.scratch + OutputOffset);
    this.sync();

    const base = (this.scratch + OutputOffset) >> 2;
    return packEntity(this.heap.u32[base]!, this.heap.u32[base + 1]!);
  }

  destroy(world: number, entity: Entity): void {
    this.module._olyEntityDestroy(world, entityIndex(entity), entityGeneration(entity));
    this.sync();
  }

  isAlive(world: number, entity: Entity): boolean {
    return this.module._olyEntityIsAlive(world, entityIndex(entity), entityGeneration(entity)) === 1;
  }

  add(world: number, entity: Entity, componentId: number, valuePointer: number): void {
    this.module._olyComponentAdd(world, entityIndex(entity), entityGeneration(entity), componentId, valuePointer);
    this.sync();
  }

  remove(world: number, entity: Entity, componentId: number): void {
    this.module._olyComponentRemove(world, entityIndex(entity), entityGeneration(entity), componentId);
    this.sync();
  }

  has(world: number, entity: Entity, componentId: number): boolean {
    return this.module._olyComponentHas(world, entityIndex(entity), entityGeneration(entity), componentId) === 1;
  }

  // Endereço do componente, ou 0 se a entidade está morta, o componente ausente ou é tag.
  get(world: number, entity: Entity, componentId: number): number {
    return this.module._olyComponentGet(world, entityIndex(entity), entityGeneration(entity), componentId) >>> 0;
  }

  queryCreate(world: number, ids: readonly number[], readCount: number, writeCount: number, withCount: number, withoutCount: number): number {
    if (ids.length > this.idsCapacity) {
      this.free(this.idsPointer);
      this.idsCapacity = Math.max(ids.length, this.idsCapacity * 2);
      this.idsPointer = this.allocate(this.idsCapacity * 4);
    }

    const base = this.idsPointer >> 2;
    ids.forEach((id, position) => {
      this.heap.u32[base + position] = id;
    });

    const query = this.module._olyQueryCreate(world, this.idsPointer, readCount, writeCount, withCount, withoutCount) >>> 0;
    this.sync();

    return query;
  }

  queryBegin(world: number, query: number): { iteration: number; blockCount: number; descriptors: number } {
    const iteration = this.module._olyQueryBegin(world, query, this.scratch + BlockCountOffset, this.scratch + DescriptorsOffset) >>> 0;
    this.sync();

    return {
      iteration,
      blockCount: this.heap.u32[(this.scratch + BlockCountOffset) >> 2]!,
      descriptors: this.heap.u32[(this.scratch + DescriptorsOffset) >> 2]!,
    };
  }

  queryEnd(world: number, iteration: number): void {
    this.module._olyQueryEnd(world, iteration);
  }

  engineCreate(fixedHz: number, maxStepsPerFrame: number, maxEventRounds: number): number {
    const engine = this.module._olyEngineCreate(fixedHz, maxStepsPerFrame, maxEventRounds) >>> 0;
    this.sync();

    return engine;
  }

  engineDestroy(engine: number): void {
    this.module._olyEngineDestroy(engine);
  }

  engineWorld(engine: number): number {
    return this.module._olyEngineWorld(engine) >>> 0;
  }

  // Um sistema TS pode lançar: a exceção JS atravessa o WASM e a memória pode ter crescido no meio.
  engineFrame(engine: number, nowSeconds: number): void {
    try {
      this.module._olyEngineFrame(engine, nowSeconds);
    } finally {
      this.sync();
    }
  }

  engineSetHidden(engine: number, hidden: boolean): void {
    this.module._olyEngineSetHidden(engine, hidden ? 1 : 0);
  }

  enginePause(engine: number): void {
    this.module._olyEnginePause(engine);
  }

  engineResume(engine: number): void {
    this.module._olyEngineResume(engine);
  }

  engineIsPaused(engine: number): boolean {
    return this.module._olyEngineIsPaused(engine) === 1;
  }

  engineTime(engine: number): number {
    return this.module._olyEngineTime(engine);
  }

  engineFixedDeltaTime(engine: number): number {
    return this.module._olyEngineFixedDeltaTime(engine);
  }

  engineInterpolation(engine: number): number {
    return this.module._olyEngineInterpolation(engine);
  }

  // 0xFFFFFFFF (OLY_INVALID_SYSTEM) se o nome nativo não existe.
  addNativeSystem(engine: number, phase: number, name: string): number {
    const id = this.module._olyEngineAddNativeSystem(engine, phase, this.writeString(name)) >>> 0;
    this.sync();

    return id;
  }

  addScriptSystem(engine: number, phase: number, name: string): number {
    const id = this.module._olyEngineAddScriptSystem(engine, phase, this.writeString(name)) >>> 0;
    this.sync();

    return id;
  }

  // 0xFFFFFFFF (OLY_INVALID_HANDLER) se o nome nativo não existe; 0xFFFFFFFE (OLY_HANDLER_TYPE_MISMATCH) se é de outro tipo.
  addNativeHandler(engine: number, eventType: number, name: string): number {
    const id = this.module._olyEngineAddNativeHandler(engine, eventType, this.writeString(name)) >>> 0;
    this.sync();

    return id;
  }

  addScriptHandler(engine: number, eventType: number, name: string): number {
    const id = this.module._olyEngineAddScriptHandler(engine, eventType, this.writeString(name)) >>> 0;
    this.sync();

    return id;
  }

  // Copia o valor para a fila de eventos do WASM, que pode crescer a memória.
  engineEmit(engine: number, eventType: number, valuePointer: number): void {
    this.module._olyEngineEmit(engine, eventType, valuePointer);
    this.sync();
  }

  setWhilePaused(engine: number, systemId: number, whilePaused: boolean): void {
    this.module._olyEngineSetWhilePaused(engine, systemId, whilePaused ? 1 : 0);
  }

  engineBindButton(engine: number, action: number, usage: number): void {
    this.module._olyEngineBindButton(engine, action, usage);
    this.sync();
  }

  engineBindAxis2d(engine: number, action: number, up: number, down: number, left: number, right: number): void {
    this.module._olyEngineBindAxis2d(engine, action, up, down, left, right);
    this.sync();
  }

  engineClearBindings(engine: number, action: number): void {
    this.module._olyEngineClearBindings(engine, action);
  }

  engineIsBound(engine: number, usage: number): boolean {
    return this.module._olyEngineIsBound(engine, usage) === 1;
  }

  engineButtonDown(engine: number, usage: number): void {
    this.module._olyEngineButtonDown(engine, usage);
    this.sync();
  }

  engineButtonUp(engine: number, usage: number): void {
    this.module._olyEngineButtonUp(engine, usage);
    this.sync();
  }

  enginePointerMove(engine: number, x: number, y: number): void {
    this.module._olyEnginePointerMove(engine, x, y);
    this.sync();
  }

  engineReleaseAll(engine: number): void {
    this.module._olyEngineReleaseAll(engine);
    this.sync();
  }

  // Estável até engineDestroy: o núcleo declara todas as ações do esquema na criação.
  engineActionSnapshot(engine: number): number {
    return this.module._olyEngineActionSnapshot(engine) >>> 0;
  }

  systemSpawn(engine: number): Entity {
    this.module._olySystemSpawn(engine, this.scratch + OutputOffset);
    this.sync();

    const base = (this.scratch + OutputOffset) >> 2;
    return packEntity(this.heap.u32[base]!, this.heap.u32[base + 1]!);
  }

  systemDestroy(engine: number, entity: Entity): void {
    this.module._olySystemDestroy(engine, entityIndex(entity), entityGeneration(entity));
    this.sync();
  }

  systemAdd(engine: number, entity: Entity, componentId: number, valuePointer: number): void {
    this.module._olySystemAdd(engine, entityIndex(entity), entityGeneration(entity), componentId, valuePointer);
    this.sync();
  }

  systemRemove(engine: number, entity: Entity, componentId: number): void {
    this.module._olySystemRemove(engine, entityIndex(entity), entityGeneration(entity), componentId);
    this.sync();
  }

  // UTF-8 + NUL numa área de rascunho que cresce; o ponteiro vale até a próxima chamada.
  writeString(text: string): number {
    const bytes = new TextEncoder().encode(text);
    if (bytes.length + 1 > this.stringCapacity) {
      this.free(this.stringPointer);
      this.stringCapacity = Math.max(bytes.length + 1, this.stringCapacity * 2);
      this.stringPointer = this.allocate(this.stringCapacity);
    }

    this.heap.u8.set(bytes, this.stringPointer);
    this.heap.u8[this.stringPointer + bytes.length] = 0;

    return this.stringPointer;
  }

  checkInvariants(world: number): string | null {
    const message = this.scratch + ValueOffset + this.valueCapacity;
    const consistent = this.module._olyWorldCheckInvariants(world, message, MessageCapacity) === 1;
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
