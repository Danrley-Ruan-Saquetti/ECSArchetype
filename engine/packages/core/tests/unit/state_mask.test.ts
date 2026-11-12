import assert from "node:assert/strict";
import { test } from "node:test";

import { stateMaskOf } from "@oly/core/internal";

test("lista vazia dá máscara 0", () => {
  assert.equal(stateMaskOf([]), 0);
});

test("um bit por índice", () => {
  assert.equal(stateMaskOf([0, 2]), 0b101);
});

test("o índice 31 dá 0x80000000 e não um número negativo", () => {
  assert.equal(stateMaskOf([31]), 0x80000000);
  assert.equal(stateMaskOf([0, 31]), 0x80000001);
});
