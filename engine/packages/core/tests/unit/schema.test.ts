import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, component, defineSchema, tag } from "@oly/core";
import { canonicalSchema, fnv1a32, schemaHash } from "@oly/core/internal";

test("component sem campos é erro", () => {
  assert.throws(() => component("Empty", {}), /tag/);
});

test("nomes inválidos", () => {
  assert.throws(() => component("1abc", { x: "f32" }), ContractError);
  assert.throws(() => component("abc", { x: "f32" }), ContractError);
  assert.throws(() => component("Ok", { class: "f32" }), ContractError);
  assert.throws(() => component("Ok", { _x: "f32" }), ContractError);
  assert.throws(() => component("Ok", { X: "f32" }), ContractError);
  assert.throws(() => tag("bad name"), ContractError);
});

test("defineSchema atribui schemaIndex na ordem e rejeita nome repetido", () => {
  const first = component("First", { x: "f32" });
  const second = tag("Second");
  assert.equal(first.schemaIndex, -1);

  const schema = defineSchema([first, second]);
  assert.equal(first.schemaIndex, 0);
  assert.equal(second.schemaIndex, 1);
  assert.equal(schema.types.length, 2);

  assert.throws(() => defineSchema([component("Dup", { x: "f32" }), tag("Dup")]), ContractError);
});

test("tipo já usado em outro esquema é erro", () => {
  const shared = component("Shared", { x: "f32" });
  defineSchema([shared]);
  assert.throws(() => defineSchema([shared]), ContractError);
});

test("fnv1a32", () => {
  assert.equal(fnv1a32(""), 0x811c9dc5);
  assert.equal(fnv1a32("a"), 0xe40c292c);
});

test("forma canônica", () => {
  assert.equal(
    canonicalSchema([component("Position", { x: "f32", y: "f32" }), tag("Player")]),
    "Position{x:f32,y:f32};Player;",
  );
});

test("hash muda com ordem e com tipo de campo", () => {
  const base = schemaHash([component("A", { x: "f32" }), tag("B")]);
  assert.notEqual(base, schemaHash([tag("B"), component("A", { x: "f32" })]));
  assert.notEqual(base, schemaHash([component("A", { x: "f64" }), tag("B")]));

  const schema = defineSchema([component("H", { x: "f32" }), tag("T")]);
  assert.equal(schema.hash, schemaHash(schema.types));
});
