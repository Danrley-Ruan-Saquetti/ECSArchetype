import type { ComponentLayout, ScalarType } from "./layout.js";

export type Fields = Readonly<Record<string, ScalarType>>;

export type ValueOf<F extends Fields> = { [K in keyof F]: number };

export interface ComponentType<Name extends string = string, F extends Fields = Fields> {
  readonly kind: "component";
  readonly name: Name;
  readonly fields: F;
  readonly layout: ComponentLayout;
  /** Position in `schema.types`, which is also the ComponentId order in the WASM. */
  readonly schemaIndex: number;
}

export interface TagType<Name extends string = string> {
  readonly kind: "tag";
  readonly name: Name;
  /** Position in `schema.types`, which is also the ComponentId order in the WASM. */
  readonly schemaIndex: number;
}

export type SchemaType = ComponentType | TagType;

export interface EventType<Name extends string = string, F extends Fields = Fields> {
  readonly kind: "event";
  readonly name: Name;
  readonly fields: F;
  readonly layout: ComponentLayout;
  /** Position in `schema.events`, which is the dispatch order. */
  readonly schemaIndex: number;
}

export type ActionKind = "button" | "axis2d" | "pointer";

export interface ActionType<Name extends string = string, Kind extends ActionKind = ActionKind> {
  readonly kind: "action";
  readonly actionKind: Kind;
  readonly name: Name;
  /** Position in `schema.actions`, which is also the ActionId. */
  readonly schemaIndex: number;
}

export type ButtonAction = ActionType<string, "button">;
export type Axis2dAction = ActionType<string, "axis2d">;
export type PointerAction = ActionType<string, "pointer">;

export interface State<Name extends string = string> {
  readonly kind: "state";
  readonly name: Name;
  /** Position in `schema.states`, which is also the StateId. */
  readonly schemaIndex: number;
}

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
