import { ContractError } from "./errors.js";

/** generation * 2^32 + index. Com generation ≤ 2^21 − 1 o número cabe em 53 bits (inteiro seguro do JS). */
export type Entity = number;

export const MaxEntityGeneration = 0x1fffff;

const IndexRange = 2 ** 32;

export function packEntity(index: number, generation: number): Entity {
  if (generation > MaxEntityGeneration) {
    throw new ContractError(
      `packEntity e=${index}:${generation}: geração acima de ${MaxEntityGeneration} não cabe em um número JS`,
    );
  }

  return generation * IndexRange + index;
}

export function entityIndex(entity: Entity): number {
  return entity % IndexRange;
}

export function entityGeneration(entity: Entity): number {
  return Math.floor(entity / IndexRange);
}

export const NullEntity: Entity = packEntity(0xffffffff, 0);

export function formatEntity(entity: Entity): string {
  return `${entityIndex(entity)}:${entityGeneration(entity)}`;
}
