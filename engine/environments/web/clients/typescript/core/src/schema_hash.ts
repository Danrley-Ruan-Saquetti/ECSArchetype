import type { ActionType, EventType, SchemaType, State } from "./schema.js";

// 32-bit FNV-1a over the text's code units; unsigned result.
export function fnv1a32(text: string): number {
  let hash = 0x811c9dc5;
  for (let position = 0; position < text.length; ++position) {
    hash = Math.imul(hash ^ text.charCodeAt(position), 0x01000193) >>> 0;
  }

  return hash;
}

// Canonical form: `Name{field:type,...};Tag;` in declaration order, followed by `!Name{field:type,...};` per event, `@name:type;` per action and `#name;` per state.
export function canonicalSchema(
  types: readonly SchemaType[],
  events: readonly EventType[] = [],
  actions: readonly ActionType[] = [],
  states: readonly State[] = [],
): string {
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
  for (const action of actions) {
    text += `@${action.name}:${action.actionKind};`;
  }
  for (const state of states) {
    text += `#${state.name};`;
  }

  return text;
}

export function schemaHash(
  types: readonly SchemaType[],
  events: readonly EventType[] = [],
  actions: readonly ActionType[] = [],
  states: readonly State[] = [],
): number {
  return fnv1a32(canonicalSchema(types, events, actions, states));
}
