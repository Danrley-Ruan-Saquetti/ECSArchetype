export type ScalarType = "f32" | "f64" | "i8" | "u8" | "i16" | "u16" | "i32" | "u32" | "entity";

export interface FieldLayout {
  name: string;
  type: ScalarType;
  offset: number;
}

export interface ComponentLayout {
  size: number;
  alignment: number;
  fields: readonly FieldLayout[];
}

const scalarSizes: Readonly<Record<ScalarType, number>> = {
  f32: 4,
  f64: 8,
  i8: 1,
  u8: 1,
  i16: 2,
  u16: 2,
  i32: 4,
  u32: 4,
  entity: 8,
};

export function scalarSize(type: ScalarType): number {
  return scalarSizes[type];
}
