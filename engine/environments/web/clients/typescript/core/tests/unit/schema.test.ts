import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, action, component, defineSchema, event, state, tag } from "@oly/core";
import { canonicalSchema, fnv1a32, schemaHash } from "@oly/core/internal";

test("component without fields is an error", () => {
  assert.throws(() => component("Empty", {}), /tag/);
});

test("invalid names", () => {
  assert.throws(() => component("1abc", { x: "f32" }), ContractError);
  assert.throws(() => component("abc", { x: "f32" }), ContractError);
  assert.throws(() => component("Ok", { class: "f32" }), ContractError);
  assert.throws(() => component("Ok", { _x: "f32" }), ContractError);
  assert.throws(() => component("Ok", { X: "f32" }), ContractError);
  assert.throws(() => tag("bad name"), ContractError);
});

test("type outside a schema has schemaIndex -1", () => {
  assert.equal(component("Loose", { x: "f32" }).schemaIndex, -1);
  assert.equal(tag("LooseTag").schemaIndex, -1);
});

test("defineSchema assigns schemaIndex in order", () => {
  const first = component("First", { x: "f32" });
  const second = tag("Second");

  const schema = defineSchema([first, second]);

  assert.equal(first.schemaIndex, 0);
  assert.equal(second.schemaIndex, 1);
  assert.equal(schema.types.length, 2);
});

test("defineSchema rejects a repeated name", () => {
  assert.throws(() => defineSchema([component("Dup", { x: "f32" }), tag("Dup")]), ContractError);
});

test("type already used in another schema is an error", () => {
  const shared = component("Shared", { x: "f32" });
  defineSchema([shared]);

  assert.throws(() => defineSchema([shared]), ContractError);
});

test("fnv1a32 matches the FNV-1a reference values", () => {
  assert.equal(fnv1a32(""), 0x811c9dc5);
  assert.equal(fnv1a32("a"), 0xe40c292c);
});

test("canonical form", () => {
  assert.equal(
    canonicalSchema([component("Position", { x: "f32", y: "f32" }), tag("Player")]),
    "Position{x:f32,y:f32};Player;",
  );
});

test("hash changes with order and with field type", () => {
  const base = schemaHash([component("A", { x: "f32" }), tag("B")]);

  assert.notEqual(base, schemaHash([tag("B"), component("A", { x: "f32" })]));
  assert.notEqual(base, schemaHash([component("A", { x: "f64" }), tag("B")]));
});

test("defineSchema stores the hash of its types", () => {
  const schema = defineSchema([component("H", { x: "f32" }), tag("T")]);

  assert.equal(schema.hash, schemaHash(schema.types));
});

test("event without fields is an error", () => {
  assert.throws(() => event("Hit", {}), ContractError);
});

test("event validates name and fields like component", () => {
  assert.throws(() => event("hit", { x: "f32" }), ContractError);
  assert.throws(() => event("Hit", { class: "f32" }), ContractError);
});

test("defineSchema separates types and events, each with its own index", () => {
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

test("event outside a schema has schemaIndex -1 and the fields layout", () => {
  const loose = event("Loose", { a: "u8", b: "f64" });

  assert.equal(loose.kind, "event");
  assert.equal(loose.schemaIndex, -1);
  assert.equal(loose.layout.size, 16);
});

test("name repeated between component and event is an error", () => {
  assert.throws(() => defineSchema([component("Dup", { x: "f32" }), event("Dup", { y: "f32" })]), ContractError);
});

test("event already used in another schema is an error", () => {
  const shared = event("SharedEvent", { x: "f32" });
  defineSchema([shared]);

  assert.throws(() => defineSchema([shared]), ContractError);
});

test("hash of a schema without events does not change", () => {
  assert.equal(schemaHash([component("Position", { x: "f32", y: "f32" }), tag("Player")]), 0x837c124);
});

test("canonical form appends the events after the types", () => {
  assert.equal(
    canonicalSchema([component("Position", { x: "f32", y: "f32" })], [event("Hit", { target: "entity", amount: "f32" })]),
    "Position{x:f32,y:f32};!Hit{target:entity,amount:f32};",
  );
});

test("adding an event changes the hash", () => {
  const types = [component("A", { x: "f32" })];

  assert.notEqual(schemaHash(types), schemaHash(types, [event("E", { x: "f32" })]));
  assert.equal(defineSchema([component("A2", { x: "f32" }), event("E2", { x: "f32" })]).hash, schemaHash([component("A2", { x: "f32" })], [event("E2", { x: "f32" })]));
});

test("action validates the name and the kind", () => {
  assert.throws(() => action("Fire", "button"), ContractError);
  assert.throws(() => action("for", "button"), ContractError);
  assert.throws(() => action("fire", "trigger" as never), ContractError);
  assert.equal(action("fire", "button").schemaIndex, -1);
});

test("defineSchema separates the actions from the types and the events", () => {
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

test("action name repeated between action and component or event is an error", () => {
  assert.throws(() => defineSchema([component("Dup1", { x: "f32" }), action("Dup1", "button")]), ContractError);
  assert.throws(() => defineSchema([action("dup2", "button"), action("dup2", "pointer")]), ContractError);
});

test("adding an action or changing its kind changes the hash", () => {
  const base = [component("HashAct", { x: "f32" })];
  const withoutActions = defineSchema([...base]).hash;
  const button = defineSchema([component("HashAct2", { x: "f32" }), action("go", "button")]).hash;
  const axis = defineSchema([component("HashAct3", { x: "f32" }), action("go", "axis2d")]).hash;

  assert.notEqual(withoutActions, button);
  assert.notEqual(button, axis);
});

test("state validates the name", () => {
  assert.throws(() => state("Menu"), ContractError);
  assert.throws(() => state("class"), ContractError);
  assert.throws(() => state("1menu"), ContractError);
  assert.equal(state("menu").schemaIndex, -1);
  assert.equal(state("menu").kind, "state");
});

test("defineSchema gives the state index in declaration order", () => {
  const A = component("StA", { x: "f32" });
  const menu = state("stMenu");
  const fire = action("stFire", "button");
  const playing = state("stPlaying");

  const schema = defineSchema([menu, A, fire, playing]);

  assert.deepEqual(schema.states, [menu, playing]);
  assert.equal(menu.schemaIndex, 0);
  assert.equal(playing.schemaIndex, 1);
  assert.equal(schema.types.length, 1);
  assert.equal(schema.actions.length, 1);
});

test("state name repeated between state, component and action is an error", () => {
  assert.throws(() => defineSchema([state("dupState1"), state("dupState1")]), ContractError);
  assert.throws(() => defineSchema([action("dupState2", "button"), state("dupState2")]), ContractError);
});

test("33 states is an error with the maximum in the message", () => {
  const states = Array.from({ length: 33 }, (_, index) => state(`many${index}`));

  assert.throws(() => defineSchema(states), /maximum/);
  assert.doesNotThrow(() => defineSchema(Array.from({ length: 32 }, (_, index) => state(`ok${index}`))));
});

test("canonical form appends the states after the actions", () => {
  assert.equal(canonicalSchema([], [], [action("go", "button")], [state("menu"), state("playing")]), "@go:button;#menu;#playing;");
});

test("adding or renaming a state changes the hash", () => {
  const base = schemaHash([], [], [], [state("a1")]);

  assert.notEqual(base, schemaHash([], [], [], []));
  assert.notEqual(base, schemaHash([], [], [], [state("a2")]));
});
