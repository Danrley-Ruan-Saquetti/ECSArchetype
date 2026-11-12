import { ContractError } from "./errors.js";
import { computeLayout, type ComponentLayout, type ScalarType } from "./layout.js";
import { schemaHash } from "./schema_hash.js";

export type Fields = Readonly<Record<string, ScalarType>>;

export type ValueOf<F extends Fields> = { [K in keyof F]: number };

export interface ComponentType<Name extends string = string, F extends Fields = Fields> {
  readonly kind: "component";
  readonly name: Name;
  readonly fields: F;
  readonly layout: ComponentLayout;
  /** -1 until the type enters a schema through defineSchema. */
  readonly schemaIndex: number;
}

export interface TagType<Name extends string = string> {
  readonly kind: "tag";
  readonly name: Name;
  /** -1 until the type enters a schema through defineSchema. */
  readonly schemaIndex: number;
}

export type SchemaType = ComponentType | TagType;

export interface EventType<Name extends string = string, F extends Fields = Fields> {
  readonly kind: "event";
  readonly name: Name;
  readonly fields: F;
  readonly layout: ComponentLayout;
  /** Position in `schema.events`; -1 until the event enters a schema through defineSchema. */
  readonly schemaIndex: number;
}

export type ActionKind = "button" | "axis2d" | "pointer";

export interface ActionType<Name extends string = string, Kind extends ActionKind = ActionKind> {
  readonly kind: "action";
  readonly actionKind: Kind;
  readonly name: Name;
  /** Position in `schema.actions`, which is also the ActionId; -1 until the action enters a schema through defineSchema. */
  readonly schemaIndex: number;
}

export type ButtonAction = ActionType<string, "button">;
export type Axis2dAction = ActionType<string, "axis2d">;
export type PointerAction = ActionType<string, "pointer">;

export interface State<Name extends string = string> {
  readonly kind: "state";
  readonly name: Name;
  /** Position in `schema.states`, which is also the StateId; -1 until the state enters a schema through defineSchema. */
  readonly schemaIndex: number;
}

export const MaxStates = 32;

export interface Schema {
  /** Components and tags; their index is the ComponentId's. */
  readonly types: readonly SchemaType[];
  /** Events in declaration order, which is the dispatch order. */
  readonly events: readonly EventType[];
  /** Input actions in declaration order. */
  readonly actions: readonly ActionType[];
  /** States in declaration order. */
  readonly states: readonly State[];
  readonly hash: number;
}

const typeNamePattern = /^[A-Z][A-Za-z0-9]*$/;
const fieldNamePattern = /^[a-z][A-Za-z0-9]*$/;
const actionNamePattern = fieldNamePattern;
const stateNamePattern = fieldNamePattern;
const actionKinds: readonly ActionKind[] = ["button", "axis2d", "pointer"];

const cppReservedWords: ReadonlySet<string> = new Set(
  `class struct int float double char long short unsigned signed bool void auto return operator template namespace new delete
  this default case switch register union enum const static virtual public private protected friend using typename do if else for
  while goto break continue try catch throw true false nullptr sizeof alignof export extern inline mutable volatile explicit
  concept requires and or not xor`.split(/\s+/),
);

function validateTypeName(name: string): void {
  if (!typeNamePattern.test(name)) {
    throw new ContractError(`invalid type name "${name}": use ${typeNamePattern}`);
  }
}

function validateFieldName(typeName: string, field: string): void {
  if (!fieldNamePattern.test(field) || cppReservedWords.has(field)) {
    throw new ContractError(
      `invalid field "${field}" in ${typeName}: use ${fieldNamePattern} and avoid C++ reserved words`,
    );
  }
}

function validateFields(name: string, fields: Fields, emptyMessage: string): void {
  validateTypeName(name);
  const fieldNames = Object.keys(fields);
  if (fieldNames.length === 0) {
    throw new ContractError(emptyMessage);
  }
  for (const field of fieldNames) {
    validateFieldName(name, field);
  }
}

/**
 * Throws ContractError if the name is not PascalCase, if any field is not camelCase or is a C++ reserved
 * word, or if there are no fields (use `tag`).
 */
export function component<const Name extends string, const F extends Fields>(
  name: Name,
  fields: F,
): ComponentType<Name, F> {
  validateFields(name, fields, `component ${name} has no fields: use tag("${name}")`);

  return { kind: "component", name, fields, layout: computeLayout(fields), schemaIndex: -1 };
}

/** Same validations as `component`; an event without data does not exist. */
export function event<const Name extends string, const F extends Fields>(name: Name, fields: F): EventType<Name, F> {
  validateFields(name, fields, `event ${name} has no fields: an event needs at least one field`);

  return { kind: "event", name, fields, layout: computeLayout(fields), schemaIndex: -1 };
}

/**
 * Declares an input action. The name becomes `actions::<name>` in C++, so it follows the field rules: camelCase and
 * not a C++ reserved word. Throws ContractError for an invalid name or an unknown `kind`.
 */
export function action<const Name extends string, const Kind extends ActionKind>(
  name: Name,
  kind: Kind,
): ActionType<Name, Kind> {
  if (!actionNamePattern.test(name) || cppReservedWords.has(name)) {
    throw new ContractError(
      `invalid action "${name}": use ${actionNamePattern} and avoid C++ reserved words`,
    );
  }
  if (!actionKinds.includes(kind)) {
    throw new ContractError(`action ${name}: unknown kind "${String(kind)}"; use button, axis2d or pointer`);
  }

  return { kind: "action", actionKind: kind, name, schemaIndex: -1 };
}

/**
 * Declares a game state. The name becomes `states::<name>` in C++, so it follows the field rules: camelCase and not
 * a C++ reserved word. Throws ContractError for an invalid name.
 */
export function state<const Name extends string>(name: Name): State<Name> {
  if (!stateNamePattern.test(name) || cppReservedWords.has(name)) {
    throw new ContractError(
      `invalid state "${name}": use ${stateNamePattern} and avoid C++ reserved words`,
    );
  }

  return { kind: "state", name, schemaIndex: -1 };
}

export function tag<const Name extends string>(name: Name): TagType<Name> {
  validateTypeName(name);

  return { kind: "tag", name, schemaIndex: -1 };
}

/**
 * Assigns `schemaIndex` in the order of each group (types, events, actions, states) and freezes what it received. Throws ContractError for
 * a repeated name (across types and events too) or for a type/event that already belongs to another schema.
 */
export function defineSchema(entries: readonly (SchemaType | EventType | ActionType | State)[]): Schema {
  const names = new Set<string>();
  for (const entry of entries) {
    if (names.has(entry.name)) {
      throw new ContractError(`defineSchema: repeated name "${entry.name}"`);
    }
    if (entry.schemaIndex >= 0) {
      throw new ContractError(`defineSchema: ${entry.name} already belongs to another schema`);
    }
    names.add(entry.name);
  }

  const types = entries.filter((entry): entry is SchemaType => entry.kind !== "event" && entry.kind !== "action" && entry.kind !== "state");
  const events = entries.filter((entry): entry is EventType => entry.kind === "event");
  const actions = entries.filter((entry): entry is ActionType => entry.kind === "action");
  const states = entries.filter((entry): entry is State => entry.kind === "state");
  if (states.length > MaxStates) {
    throw new ContractError(`defineSchema: ${states.length} states; the maximum is ${MaxStates}`);
  }

  for (const group of [types, events, actions, states]) {
    group.forEach((entry, index) => {
      (entry as { schemaIndex: number }).schemaIndex = index;
      Object.freeze(entry);
    });
  }

  const frozenTypes = Object.freeze([...types]);
  const frozenEvents = Object.freeze([...events]);
  const frozenActions = Object.freeze([...actions]);
  const frozenStates = Object.freeze([...states]);

  return Object.freeze({
    types: frozenTypes,
    events: frozenEvents,
    actions: frozenActions,
    states: frozenStates,
    hash: schemaHash(frozenTypes, frozenEvents, frozenActions, frozenStates),
  });
}
