import type { Fields } from "./schema.js";

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

// `entity` é oly::ecs::Entity: dois u32, alinhamento 4.
export function scalarAlignment(type: ScalarType): number {
  return type === "entity" ? 4 : scalarSizes[type];
}

function alignUp(value: number, alignment: number): number {
  return Math.ceil(value / alignment) * alignment;
}

export function computeLayout(fields: Fields): ComponentLayout {
  const layouts: FieldLayout[] = [];
  let offset = 0;
  let alignment = 1;
  for (const [name, type] of Object.entries(fields)) {
    const fieldAlignment = scalarAlignment(type);
    offset = alignUp(offset, fieldAlignment);
    layouts.push({ name, type, offset });
    offset += scalarSize(type);
    alignment = Math.max(alignment, fieldAlignment);
  }

  return { size: alignUp(offset, alignment), alignment, fields: layouts };
}
