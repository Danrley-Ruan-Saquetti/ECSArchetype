import assert from "node:assert/strict";
import { test } from "node:test";

import { stateMaskOf } from "@oly/core/internal";

test("empty list gives mask 0", () => {
  assert.equal(stateMaskOf([]), 0);
});

test("one bit per index", () => {
  assert.equal(stateMaskOf([0, 2]), 0b101);
});

test("index 31 gives 0x80000000 and not a negative number", () => {
  assert.equal(stateMaskOf([31]), 0x80000000);
  assert.equal(stateMaskOf([0, 31]), 0x80000001);
});
