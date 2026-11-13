import type { ComponentLayout } from "./layout.js";
import type { ActionKind, ActionType, ComponentType, EventType, Fields, Schema, SchemaType, State, TagType } from "./schema.js";

// Constructors for the code oly-schema generates (`*.generated.ts`); the boundary lint keeps hand-written code out.
// They trust their input: oly-schema has validated the names and computed the layout. Properties keep a fixed order
// (kind, name, fields, layout, schemaIndex), so the hot path sees the same object shapes.

export interface GeneratedLayout<F extends Fields> {
  readonly size: number;
  readonly alignment: number;
  readonly offsets: { readonly [K in keyof F]: number };
}

function layoutOf<F extends Fields>(fields: F, layout: GeneratedLayout<F>): ComponentLayout {
  const offsets = layout.offsets as Readonly<Record<string, number>>;

  return Object.freeze({
    size: layout.size,
    alignment: layout.alignment,
    fields: Object.freeze(Object.entries(fields).map(([name, type]) => Object.freeze({ name, type, offset: offsets[name]! }))),
  });
}

export function componentType<const Name extends string, const F extends Fields>(
  name: Name,
  fields: F,
  schemaIndex: number,
  layout: GeneratedLayout<F>,
): ComponentType<Name, F> {
  return Object.freeze({ kind: "component", name, fields, layout: layoutOf(fields, layout), schemaIndex });
}

export function tagType<const Name extends string>(name: Name, schemaIndex: number): TagType<Name> {
  return Object.freeze({ kind: "tag", name, schemaIndex });
}

export function eventType<const Name extends string, const F extends Fields>(
  name: Name,
  fields: F,
  schemaIndex: number,
  layout: GeneratedLayout<F>,
): EventType<Name, F> {
  return Object.freeze({ kind: "event", name, fields, layout: layoutOf(fields, layout), schemaIndex });
}

export function actionType<const Name extends string, const Kind extends ActionKind>(
  name: Name,
  kind: Kind,
  schemaIndex: number,
): ActionType<Name, Kind> {
  return Object.freeze({ kind: "action", actionKind: kind, name, schemaIndex });
}

export function stateType<const Name extends string>(name: Name, schemaIndex: number): State<Name> {
  return Object.freeze({ kind: "state", name, schemaIndex });
}

export function schemaOf(entries: readonly (SchemaType | EventType | ActionType | State)[], hash: number): Schema {
  return Object.freeze({
    types: Object.freeze(entries.filter((entry): entry is SchemaType => entry.kind === "component" || entry.kind === "tag")),
    events: Object.freeze(entries.filter((entry): entry is EventType => entry.kind === "event")),
    actions: Object.freeze(entries.filter((entry): entry is ActionType => entry.kind === "action")),
    states: Object.freeze(entries.filter((entry): entry is State => entry.kind === "state")),
    hash,
  });
}
