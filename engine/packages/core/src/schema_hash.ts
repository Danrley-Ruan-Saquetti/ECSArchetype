import type { EventType, SchemaType } from "./schema.js";

// FNV-1a 32 bits sobre as unidades de código do texto; resultado sem sinal.
export function fnv1a32(text: string): number {
  let hash = 0x811c9dc5;
  for (let position = 0; position < text.length; ++position) {
    hash = Math.imul(hash ^ text.charCodeAt(position), 0x01000193) >>> 0;
  }

  return hash;
}

// Forma canônica: `Nome{campo:tipo,...};Tag;` na ordem de declaração, seguida de `!Nome{campo:tipo,...};` por evento.
export function canonicalSchema(types: readonly SchemaType[], events: readonly EventType[] = []): string {
  let text = "";
  for (const type of types) {
    if (type.kind === "tag") {
      text += `${type.name};`;
    } else {
      const fields = type.layout.fields.map((field) => `${field.name}:${field.type}`).join(",");
      text += `${type.name}{${fields}};`;
    }
  }
  for (const event of events) {
    const fields = event.layout.fields.map((field) => `${field.name}:${field.type}`).join(",");
    text += `!${event.name}{${fields}};`;
  }

  return text;
}

export function schemaHash(types: readonly SchemaType[], events: readonly EventType[] = []): number {
  return fnv1a32(canonicalSchema(types, events));
}
