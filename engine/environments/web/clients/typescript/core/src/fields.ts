import type { Heap } from "./bridge.js";
import { entityGeneration, entityIndex, packEntity } from "./entity.js";
import { scalarSize, type ComponentLayout, type FieldLayout, type ScalarType } from "./layout.js";
import type { Fields } from "./schema.js";

/** element = array[base + row * stride]; for `entity`, array is u32, `base` points to the index and `base + 1` to the generation. */
export interface RawField {
  readonly array: Float32Array | Float64Array | Int8Array | Uint8Array | Int16Array | Uint16Array | Int32Array | Uint32Array;
  readonly base: number;
  readonly stride: number;
}

export interface ReadField {
  get(row: number): number;
  /** Valid until the next call that allocates in the WASM (memory may grow and replace the buffer). */
  raw(): RawField;
}

export interface WriteField extends ReadField {
  set(row: number, value: number): void;
}

export type ReadView<F extends Fields> = { readonly [K in keyof F]: ReadField };
export type WriteView<F extends Fields> = { readonly [K in keyof F]: WriteField };

// One class per scalar type keeps each get/set monomorphic. The array is read from the Heap on every access:
// that way the view follows the growth of the WASM memory.
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

// `entity` is read as two u32 (index and generation), so the element is 4 B, not the scalar's 8 B.
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
