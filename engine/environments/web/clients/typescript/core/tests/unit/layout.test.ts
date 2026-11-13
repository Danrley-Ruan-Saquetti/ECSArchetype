import assert from "node:assert/strict";
import { test } from "node:test";

import { computeLayout } from "@oly/core/internal";

function offsets(layout: ReturnType<typeof computeLayout>): number[] {
  return layout.fields.map((field) => field.offset);
}

test("two f32 sit together, without padding", () => {
  assert.deepEqual(computeLayout({ x: "f32", y: "f32" }), {
    size: 8,
    alignment: 4,
    fields: [
      { name: "x", type: "f32", offset: 0 },
      { name: "y", type: "f32", offset: 4 },
    ],
  });
});

test("padding before f32", () => {
  const layout = computeLayout({ a: "u8", b: "f32" });

  assert.deepEqual(offsets(layout), [0, 4]);
  assert.equal(layout.size, 8);
});

test("f64 aligns the struct to 8", () => {
  const layout = computeLayout({ a: "f32", b: "f64" });

  assert.deepEqual(offsets(layout), [0, 8]);
  assert.equal(layout.size, 16);
  assert.equal(layout.alignment, 8);
});

test("u16 between u8 aligns the struct to 2", () => {
  const layout = computeLayout({ a: "u8", b: "u16", c: "u8" });

  assert.deepEqual(offsets(layout), [0, 2, 4]);
  assert.equal(layout.size, 6);
  assert.equal(layout.alignment, 2);
});

test("entity has 8 bytes and alignment 4", () => {
  const layout = computeLayout({ owner: "entity", flag: "u8" });

  assert.deepEqual(offsets(layout), [0, 8]);
  assert.equal(layout.size, 12);
  assert.equal(layout.alignment, 4);
});
