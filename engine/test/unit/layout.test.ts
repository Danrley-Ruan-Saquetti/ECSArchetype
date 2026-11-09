import assert from "node:assert/strict";
import { test } from "node:test";

import { computeLayout } from "../../ts/layout.js";

function offsets(layout: ReturnType<typeof computeLayout>): number[] {
  return layout.fields.map((field) => field.offset);
}

test("Position f32,f32", () => {
  assert.deepEqual(computeLayout({ x: "f32", y: "f32" }), {
    size: 8,
    alignment: 4,
    fields: [
      { name: "x", type: "f32", offset: 0 },
      { name: "y", type: "f32", offset: 4 },
    ],
  });
});

test("padding antes de f32", () => {
  const layout = computeLayout({ a: "u8", b: "f32" });
  assert.deepEqual(offsets(layout), [0, 4]);
  assert.equal(layout.size, 8);
});

test("f64 alinha o struct a 8", () => {
  const layout = computeLayout({ a: "f32", b: "f64" });
  assert.deepEqual(offsets(layout), [0, 8]);
  assert.equal(layout.size, 16);
  assert.equal(layout.alignment, 8);
});

test("u8,u16,u8", () => {
  const layout = computeLayout({ a: "u8", b: "u16", c: "u8" });
  assert.deepEqual(offsets(layout), [0, 2, 4]);
  assert.equal(layout.size, 6);
  assert.equal(layout.alignment, 2);
});

test("entity tem 8 bytes e alinhamento 4", () => {
  const layout = computeLayout({ owner: "entity", flag: "u8" });
  assert.deepEqual(offsets(layout), [0, 8]);
  assert.equal(layout.size, 12);
  assert.equal(layout.alignment, 4);
});
