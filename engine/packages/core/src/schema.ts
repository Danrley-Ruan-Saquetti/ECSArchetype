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
  readonly schemaIndex: number;
}

export interface TagType<Name extends string = string> {
  readonly kind: "tag";
  readonly name: Name;
  readonly schemaIndex: number;
}

export type SchemaType = ComponentType | TagType;

export interface Schema {
  readonly types: readonly SchemaType[];
  readonly hash: number;
}

const typeNamePattern = /^[A-Z][A-Za-z0-9]*$/;
const fieldNamePattern = /^[a-z][A-Za-z0-9]*$/;

const reservedWords: ReadonlySet<string> = new Set(
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
  if (!fieldNamePattern.test(field) || reservedWords.has(field)) {
    throw new ContractError(
      `campo inválido "${field}" em ${typeName}: use ${fieldNamePattern} e evite palavras reservadas do C++`,
    );
  }
}

export function component<const Name extends string, const F extends Fields>(
  name: Name,
  fields: F,
): ComponentType<Name, F> {
  validateTypeName(name);
  const fieldNames = Object.keys(fields);
  if (fieldNames.length === 0) {
    throw new ContractError(`componente ${name} sem campos: use tag("${name}")`);
  }
  for (const field of fieldNames) {
    validateFieldName(name, field);
  }

  return { kind: "component", name, fields, layout: computeLayout(fields), schemaIndex: -1 };
}

export function tag<const Name extends string>(name: Name): TagType<Name> {
  validateTypeName(name);

  return { kind: "tag", name, schemaIndex: -1 };
}

export function defineSchema(types: readonly SchemaType[]): Schema {
  const names = new Set<string>();
  for (const type of types) {
    if (names.has(type.name)) {
      throw new ContractError(`defineSchema: nome repetido "${type.name}"`);
    }
    if (type.schemaIndex >= 0) {
      throw new ContractError(`defineSchema: ${type.name} já pertence a outro esquema`);
    }
    names.add(type.name);
  }

  types.forEach((type, index) => {
    (type as { schemaIndex: number }).schemaIndex = index;
    Object.freeze(type);
  });

  const frozen = Object.freeze([...types]);

  return Object.freeze({ types: frozen, hash: schemaHash(frozen) });
}
