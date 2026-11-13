// `>>> 0` returns the mask as uint32 (`1 << 31` is negative in JS), the same number the C API reads.
export function stateMaskOf(indices: readonly number[]): number {
  let mask = 0;
  for (const index of indices) {
    mask |= 1 << index;
  }

  return mask >>> 0;
}
