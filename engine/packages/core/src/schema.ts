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
  /** -1 até o tipo entrar num esquema por defineSchema. */
  readonly schemaIndex: number;
}

export interface TagType<Name extends string = string> {
  readonly kind: "tag";
  readonly name: Name;
  /** -1 até o tipo entrar num esquema por defineSchema. */
  readonly schemaIndex: number;
}

export type SchemaType = ComponentType | TagType;

export interface EventType<Name extends string = string, F extends Fields = Fields> {
  readonly kind: "event";
  readonly name: Name;
  readonly fields: F;
  readonly layout: ComponentLayout;
  /** Posição em `schema.events`; -1 até o evento entrar num esquema por defineSchema. */
  readonly schemaIndex: number;
}

export interface Schema {
  /** Componentes e tags; o índice deles é o do ComponentId. */
  readonly types: readonly SchemaType[];
  /** Eventos na ordem de declaração, que é a ordem de despacho. */
  readonly events: readonly EventType[];
  readonly hash: number;
}

const typeNamePattern = /^[A-Z][A-Za-z0-9]*$/;
const fieldNamePattern = /^[a-z][A-Za-z0-9]*$/;

const cppReservedWords: ReadonlySet<string> = new Set(
  `class struct int float double char long short unsigned signed bool void auto return operator template namespace new delete
  this default case switch register union enum const static virtual public private protected friend using typename do if else for
  while goto break continue try catch throw true false nullptr sizeof alignof export extern inline mutable volatile explicit
  concept requires and or not xor`.split(/\s+/),
);

function validateTypeName(name: string): void {
  if (!typeNamePattern.test(name)) {
    throw new ContractError(`nome de tipo inválido "${name}": use ${typeNamePattern}`);
  }
}

function validateFieldName(typeName: string, field: string): void {
  if (!fieldNamePattern.test(field) || cppReservedWords.has(field)) {
    throw new ContractError(
      `campo inválido "${field}" em ${typeName}: use ${fieldNamePattern} e evite palavras reservadas do C++`,
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
 * Lança ContractError se o nome não for PascalCase, se algum campo não for camelCase ou for palavra reservada
 * do C++, ou se não houver campos (use `tag`).
 */
export function component<const Name extends string, const F extends Fields>(
  name: Name,
  fields: F,
): ComponentType<Name, F> {
  validateFields(name, fields, `componente ${name} sem campos: use tag("${name}")`);

  return { kind: "component", name, fields, layout: computeLayout(fields), schemaIndex: -1 };
}

/** Mesmas validações de `component`; um evento sem dados não existe. */
export function event<const Name extends string, const F extends Fields>(name: Name, fields: F): EventType<Name, F> {
  validateFields(name, fields, `evento ${name} sem campos: um evento precisa de ao menos um campo`);

  return { kind: "event", name, fields, layout: computeLayout(fields), schemaIndex: -1 };
}

export function tag<const Name extends string>(name: Name): TagType<Name> {
  validateTypeName(name);

  return { kind: "tag", name, schemaIndex: -1 };
}

/**
 * Atribui `schemaIndex` na ordem de cada grupo (tipos, eventos) e congela o que recebeu. Lança ContractError para
 * nome repetido (entre tipos e eventos também) ou para tipo/evento que já pertence a outro esquema.
 */
export function defineSchema(entries: readonly (SchemaType | EventType)[]): Schema {
  const names = new Set<string>();
  for (const entry of entries) {
    if (names.has(entry.name)) {
      throw new ContractError(`defineSchema: nome repetido "${entry.name}"`);
    }
    if (entry.schemaIndex >= 0) {
      throw new ContractError(`defineSchema: ${entry.name} já pertence a outro esquema`);
    }
    names.add(entry.name);
  }

  const types = entries.filter((entry): entry is SchemaType => entry.kind !== "event");
  const events = entries.filter((entry): entry is EventType => entry.kind === "event");
  for (const group of [types, events]) {
    group.forEach((entry, index) => {
      (entry as { schemaIndex: number }).schemaIndex = index;
      Object.freeze(entry);
    });
  }

  const frozenTypes = Object.freeze([...types]);
  const frozenEvents = Object.freeze([...events]);

  return Object.freeze({ types: frozenTypes, events: frozenEvents, hash: schemaHash(frozenTypes, frozenEvents) });
}
