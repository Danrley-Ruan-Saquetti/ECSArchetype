import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, action, component, defineSchema, event, tag } from "@oly/core";
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

test("tipo fora de esquema tem schemaIndex -1", () => {
  assert.equal(component("Loose", { x: "f32" }).schemaIndex, -1);
  assert.equal(tag("LooseTag").schemaIndex, -1);
});

test("defineSchema atribui schemaIndex na ordem", () => {
  const first = component("First", { x: "f32" });
  const second = tag("Second");

  const schema = defineSchema([first, second]);

  assert.equal(first.schemaIndex, 0);
  assert.equal(second.schemaIndex, 1);
  assert.equal(schema.types.length, 2);
});

test("defineSchema rejeita nome repetido", () => {
  assert.throws(() => defineSchema([component("Dup", { x: "f32" }), tag("Dup")]), ContractError);
});

test("tipo já usado em outro esquema é erro", () => {
  const shared = component("Shared", { x: "f32" });
  defineSchema([shared]);

  assert.throws(() => defineSchema([shared]), ContractError);
});

test("fnv1a32 bate com os valores de referência do FNV-1a", () => {
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
});

test("defineSchema guarda o hash dos seus tipos", () => {
  const schema = defineSchema([component("H", { x: "f32" }), tag("T")]);

  assert.equal(schema.hash, schemaHash(schema.types));
});

test("event sem campos é erro", () => {
  assert.throws(() => event("Hit", {}), ContractError);
});

test("event valida nome e campos como component", () => {
  assert.throws(() => event("hit", { x: "f32" }), ContractError);
  assert.throws(() => event("Hit", { class: "f32" }), ContractError);
});

test("defineSchema separa tipos e eventos, cada um com o seu índice", () => {
  const typeA = component("A", { x: "f32" });
  const hit = event("Hit", { target: "entity", amount: "f32" });
  const typeT = tag("T");
  const boom = event("Boom", { power: "u32" });

  const schema = defineSchema([typeA, hit, typeT, boom]);

  assert.deepEqual(schema.types, [typeA, typeT]);
  assert.deepEqual(schema.events, [hit, boom]);
  assert.equal(hit.schemaIndex, 0);
  assert.equal(boom.schemaIndex, 1);
  assert.equal(typeA.schemaIndex, 0);
  assert.equal(typeT.schemaIndex, 1);
});

test("evento fora de esquema tem schemaIndex -1 e o layout dos campos", () => {
  const loose = event("Loose", { a: "u8", b: "f64" });

  assert.equal(loose.kind, "event");
  assert.equal(loose.schemaIndex, -1);
  assert.equal(loose.layout.size, 16);
});

test("nome repetido entre componente e evento é erro", () => {
  assert.throws(() => defineSchema([component("Dup", { x: "f32" }), event("Dup", { y: "f32" })]), ContractError);
});

test("evento já usado em outro esquema é erro", () => {
  const shared = event("SharedEvent", { x: "f32" });
  defineSchema([shared]);

  assert.throws(() => defineSchema([shared]), ContractError);
});

test("hash de esquema sem eventos não muda", () => {
  assert.equal(schemaHash([component("Position", { x: "f32", y: "f32" }), tag("Player")]), 0x837c124);
});

test("forma canônica acrescenta os eventos depois dos tipos", () => {
  assert.equal(
    canonicalSchema([component("Position", { x: "f32", y: "f32" })], [event("Hit", { target: "entity", amount: "f32" })]),
    "Position{x:f32,y:f32};!Hit{target:entity,amount:f32};",
  );
});

test("acrescentar um evento muda o hash", () => {
  const types = [component("A", { x: "f32" })];

  assert.notEqual(schemaHash(types), schemaHash(types, [event("E", { x: "f32" })]));
  assert.equal(defineSchema([component("A2", { x: "f32" }), event("E2", { x: "f32" })]).hash, schemaHash([component("A2", { x: "f32" })], [event("E2", { x: "f32" })]));
});

test("action valida o nome e o tipo", () => {
  assert.throws(() => action("Fire", "button"), ContractError);
  assert.throws(() => action("for", "button"), ContractError);
  assert.throws(() => action("fire", "trigger" as never), ContractError);
  assert.equal(action("fire", "button").schemaIndex, -1);
});

test("defineSchema separa as ações dos tipos e dos eventos", () => {
  const A = component("ActA", { x: "f32" });
  const fire = action("fire", "button");
  const Hit = event("ActHit", { x: "f32" });
  const move = action("move", "axis2d");

  const schema = defineSchema([A, fire, Hit, move]);

  assert.deepEqual(schema.actions, [fire, move]);
  assert.equal(fire.schemaIndex, 0);
  assert.equal(move.schemaIndex, 1);
  assert.equal(schema.types.length, 1);
  assert.equal(schema.events.length, 1);
});

test("nome de ação repetido entre ação e componente ou evento é erro", () => {
  assert.throws(() => defineSchema([component("Dup1", { x: "f32" }), action("Dup1", "button")]), ContractError);
  assert.throws(() => defineSchema([action("dup2", "button"), action("dup2", "pointer")]), ContractError);
});

test("acrescentar uma ação ou trocar o tipo dela muda o hash", () => {
  const base = [component("HashAct", { x: "f32" })];
  const withoutActions = defineSchema([...base]).hash;
  const button = defineSchema([component("HashAct2", { x: "f32" }), action("go", "button")]).hash;
  const axis = defineSchema([component("HashAct3", { x: "f32" }), action("go", "axis2d")]).hash;

  assert.notEqual(withoutActions, button);
  assert.notEqual(button, axis);
});
