import type { SchemaType } from "./schema.js";

// FNV-1a 32 bits sobre as unidades de código do texto; resultado sem sinal.
export function fnv1a32(text: string): number {
  let hash = 0x811c9dc5;
  for (let position = 0; position < text.length; ++position) {
    hash = Math.imul(hash ^ text.charCodeAt(position), 0x01000193) >>> 0;
  }

  return hash;
}

// Forma canônica: `Nome{campo:tipo,...};Tag;` na ordem de declaração.
export function canonicalSchema(types: readonly SchemaType[]): string {
  let text = "";
  for (const type of types) {
    if (type.kind === "tag") {
      text += `${type.name};`;
    } else {
      const fields = type.layout.fields.map((field) => `${field.name}:${field.type}`).join(",");
      text += `${type.name}{${fields}};`;
    }
  }

  return text;
}

export function schemaHash(types: readonly SchemaType[]): number {
  return fnv1a32(canonicalSchema(types));
}
