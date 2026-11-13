import assert from "node:assert/strict";
import { test } from "node:test";

import type { ValueOf } from "@oly/core";
import { actionType, componentType, eventType, schemaOf, stateType, tagType } from "@oly/core/generated";

const P = componentType("P", { a: "u8", b: "f64" }, 0, { size: 16, alignment: 8, offsets: { a: 0, b: 8 } });
const T = tagType("T", 1);
const E = eventType("E", { target: "entity", amount: "f32" }, 0, { size: 12, alignment: 4, offsets: { target: 0, amount: 8 } });
const go = actionType("go", "button", 0);
const menu = stateType("menu", 0);

test("componentType builds the layout in field order from the generator's offsets", () => {
  assert.deepEqual(P.layout, {
    size: 16,
    alignment: 8,
    fields: [
      { name: "a", type: "u8", offset: 0 },
      { name: "b", type: "f64", offset: 8 },
    ],
  });
  assert.equal(P.schemaIndex, 0);
  assert.ok(Object.isFrozen(P));
});

test("schemaOf groups the entries by kind, in order, and keeps the hash", () => {
  const schema = schemaOf([P, T, E, go, menu], 0x1234);

  assert.deepEqual(schema.types, [P, T]);
  assert.deepEqual(schema.events, [E]);
  assert.deepEqual(schema.actions, [go]);
  assert.deepEqual(schema.states, [menu]);
  assert.equal(schema.hash, 0x1234);
  assert.ok(Object.isFrozen(schema.types));
});

test("the generated constants keep literal names, fields and kinds", () => {
  const name: "P" = P.name;
  const kind: "button" = go.actionKind;
  const value: ValueOf<typeof P.fields> = { a: 1, b: 2 };
  // @ts-expect-error: a field the schema does not declare must be a compile error, so the field names stay literal.
  const unknownField: ValueOf<typeof P.fields> = { a: 1, b: 2, c: 3 };

  assert.deepEqual([name, kind, value.a, unknownField.a], ["P", "button", 1, 1]);
});
