import { writeScalar, type Bridge } from "./bridge.js";
import { formatEntity, type Entity } from "./entity.js";
import { ContractError } from "./errors.js";
import { componentIdOf, type Runtime } from "./runtime.js";
import type { ComponentLayout } from "./layout.js";
import type { ComponentType, EventType, SchemaType } from "./schema.js";

// Validações e escrita de valores compartilhadas por World e pelos contextos (mesmas regras e mensagens).

export function contractError(operation: string, entity: Entity | null, type: { readonly name: string } | null, reason: string): ContractError {
  const entityPart = entity === null ? "" : ` e=${formatEntity(entity)}`;
  const typePart = type === null ? "" : ` type=${type.name}`;

  return new ContractError(`${operation}${entityPart}${typePart}: ${reason}`);
}

export function assertKnownFields(operation: string, entity: Entity | null, type: ComponentType | EventType, value: Partial<Record<string, number>>): void {
  const known = new Set(type.layout.fields.map((field) => field.name));
  for (const name of Object.keys(value)) {
    if (!known.has(name)) {
      throw contractError(operation, entity, type, `campo desconhecido "${name}"`);
    }
  }
}

export function assertSpawnArguments(
  runtime: Runtime,
  types: readonly SchemaType[],
  valuesByName: Readonly<Record<string, Partial<Record<string, number>>>>,
): void {
  const seen = new Set<SchemaType>();
  for (const type of types) {
    componentIdOf(runtime, type);
    if (seen.has(type)) {
      throw new ContractError(`spawn type=${type.name}: tipo repetido em types`);
    }
    seen.add(type);
  }

  for (const [name, value] of Object.entries(valuesByName)) {
    const type = types.find((candidate) => candidate.name === name);
    if (type === undefined || type.kind !== "component") {
      throw new ContractError(`spawn type=${name}: valor para componente que não está em types`);
    }
    assertKnownFields("spawn", null, type, value);
  }
}

// Monta o valor na área de rascunho da ponte e devolve o endereço (0 para tag, que não tem layout).
export function writeValue(bridge: Bridge, layout: ComponentLayout | null, value: Partial<Record<string, number>> | undefined): number {
  if (layout === null) {
    return 0;
  }

  const address = bridge.valuePointer;
  bridge.heap.u8.fill(0, address, address + layout.size);
  for (const field of layout.fields) {
    const fieldValue = value?.[field.name];
    if (fieldValue !== undefined) {
      writeScalar(bridge.heap, field.type, address + field.offset, fieldValue);
    }
  }

  return address;
}
