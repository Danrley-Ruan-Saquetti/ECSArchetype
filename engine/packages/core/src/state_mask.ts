// `>>> 0` devolve a máscara como uint32 (`1 << 31` é negativo em JS), o mesmo número que a API C lê.
export function stateMaskOf(indices: readonly number[]): number {
  let mask = 0;
  for (const index of indices) {
    mask |= 1 << index;
  }

  return mask >>> 0;
}
