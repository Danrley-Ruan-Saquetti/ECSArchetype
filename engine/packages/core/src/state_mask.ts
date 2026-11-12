// `>>> 0` porque `1 << 31` é negativo em JS e a API C lê a máscara como uint32_t.
export function stateMaskOf(indices: readonly number[]): number {
  let mask = 0;
  for (const index of indices) {
    mask |= 1 << index;
  }

  return mask >>> 0;
}
