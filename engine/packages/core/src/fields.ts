import type { Heap } from "./bridge.js";
import { entityGeneration, entityIndex, packEntity } from "./entity.js";
import { scalarSize, type ComponentLayout, type FieldLayout, type ScalarType } from "./layout.js";
import type { Fields } from "./schema.js";

/** elemento = array[base + row * stride]; para `entity`, array é u32, `base` aponta para o index e `base + 1` para a generation. */
export interface RawField {
  readonly array: Float32Array | Float64Array | Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array;
  readonly base: number;
  readonly stride: number;
}

export interface ReadField {
  get(row: number): number;
  /** Válido até a próxima chamada que aloque no WASM (a memória pode crescer e trocar o buffer). */
  raw(): RawField;
}

export interface WriteField extends ReadField {
  set(row: number, value: number): void;
}

export type ReadView<F extends Fields> = { readonly [K in keyof F]: ReadField };
export type WriteView<F extends Fields> = { readonly [K in keyof F]: WriteField };

// Uma classe por tipo escalar mantém cada get/set monomórfico. O array é lido do Heap a cada acesso:
// assim a view segue o crescimento da memória do WASM.
class F32Field implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    return this.heap.f32[this.base + row * this.stride]!;
  }

  set(row: number, value: number): void {
    this.heap.f32[this.base + row * this.stride] = value;
  }

  raw(): RawField {
    return { array: this.heap.f32, base: this.base, stride: this.stride };
  }
}

class F64Field implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    return this.heap.f64[this.base + row * this.stride]!;
  }

  set(row: number, value: number): void {
    this.heap.f64[this.base + row * this.stride] = value;
  }

  raw(): RawField {
    return { array: this.heap.f64, base: this.base, stride: this.stride };
  }
}

class I8Field implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    return this.heap.i8[this.base + row * this.stride]!;
  }

  set(row: number, value: number): void {
    this.heap.i8[this.base + row * this.stride] = value;
  }

  raw(): RawField {
    return { array: this.heap.i8, base: this.base, stride: this.stride };
  }
}

class U8Field implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    return this.heap.u8[this.base + row * this.stride]!;
  }

  set(row: number, value: number): void {
    this.heap.u8[this.base + row * this.stride] = value;
  }

  raw(): RawField {
    return { array: this.heap.u8, base: this.base, stride: this.stride };
  }
}

class I16Field implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    return this.heap.i16[this.base + row * this.stride]!;
  }

  set(row: number, value: number): void {
    this.heap.i16[this.base + row * this.stride] = value;
  }

  raw(): RawField {
    return { array: this.heap.i16, base: this.base, stride: this.stride };
  }
}

class U16Field implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    return this.heap.u16[this.base + row * this.stride]!;
  }

  set(row: number, value: number): void {
    this.heap.u16[this.base + row * this.stride] = value;
  }

  raw(): RawField {
    return { array: this.heap.u16, base: this.base, stride: this.stride };
  }
}

class I32Field implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    return this.heap.i32[this.base + row * this.stride]!;
  }

  set(row: number, value: number): void {
    this.heap.i32[this.base + row * this.stride] = value;
  }

  raw(): RawField {
    return { array: this.heap.i32, base: this.base, stride: this.stride };
  }
}

class U32Field implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    return this.heap.u32[this.base + row * this.stride]!;
  }

  set(row: number, value: number): void {
    this.heap.u32[this.base + row * this.stride] = value;
  }

  raw(): RawField {
    return { array: this.heap.u32, base: this.base, stride: this.stride };
  }
}

class EntityField implements WriteField {
  constructor(
    private readonly heap: Heap,
    private readonly base: number,
    private readonly stride: number,
  ) {}

  get(row: number): number {
    const position = this.base + row * this.stride;

    return packEntity(this.heap.u32[position]!, this.heap.u32[position + 1]!);
  }

  set(row: number, value: number): void {
    const position = this.base + row * this.stride;
    this.heap.u32[position] = entityIndex(value);
    this.heap.u32[position + 1] = entityGeneration(value);
  }

  raw(): RawField {
    return { array: this.heap.u32, base: this.base, stride: this.stride };
  }
}

// `entity` é lido como dois u32 (index e generation), então o elemento tem 4 B, não os 8 B do escalar.
function elementSize(type: ScalarType): number {
  return type === "entity" ? 4 : scalarSize(type);
}

function makeField(heap: Heap, layout: ComponentLayout, field: FieldLayout, columnAddress: number): WriteField {
  const size = elementSize(field.type);
  const base = (columnAddress + field.offset) / size;
  const stride = layout.size / size;

  switch (field.type) {
    case "f32":
      return new F32Field(heap, base, stride);
    case "f64":
      return new F64Field(heap, base, stride);
    case "i8":
      return new I8Field(heap, base, stride);
    case "u8":
      return new U8Field(heap, base, stride);
    case "i16":
      return new I16Field(heap, base, stride);
    case "u16":
      return new U16Field(heap, base, stride);
    case "i32":
      return new I32Field(heap, base, stride);
    case "u32":
      return new U32Field(heap, base, stride);
    case "entity":
      return new EntityField(heap, base, stride);
  }
}

export function makeView(heap: Heap, layout: ComponentLayout, columnAddress: number): WriteView<Fields> {
  const view: Record<string, WriteField> = {};
  for (const field of layout.fields) {
    view[field.name] = makeField(heap, layout, field, columnAddress);
  }

  return view;
}
