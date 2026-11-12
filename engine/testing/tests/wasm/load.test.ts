import assert from "node:assert/strict";
import { test } from "node:test";

import { SchemaMismatchError, action, component, defineSchema, event, loadRuntime, tag } from "@oly/core";

import { fixtureFactory, loadFixture } from "./fixture.js";

test("loads with the fixture schema", async () => {
  const { runtime } = await loadFixture();
  const world = runtime.createWorld();

  assert.ok(world);
  assert.equal(world.checkInvariants(), null);
  world.dispose();
});

test("mismatched schema is rejected", async () => {
  const otherSchema = defineSchema([
    component("Position", { x: "f64", y: "f32" }),
    component("Velocity", { x: "f32", y: "f32" }),
    component("Mixed", { a: "u8", b: "f64", c: "i16", owner: "entity" }),
    tag("Player"),
    tag("Enemy"),
  ]);

  await assert.rejects(
    loadRuntime(fixtureFactory(), otherSchema),
    (error: unknown) =>
      error instanceof SchemaMismatchError && /0x[0-9a-f]+.*0x[0-9a-f]+/s.test(error.message) && /Position/.test(error.message),
  );
});

function fixtureTypes() {
  return [
    component("Position", { x: "f32", y: "f32" }),
    component("Velocity", { x: "f32", y: "f32" }),
    component("Mixed", { a: "u8", b: "f64", c: "i16", owner: "entity" }),
    component("Trace", { value: "f64" }),
    tag("Player"),
    tag("Enemy"),
  ];
}

test("TS schema with one more event than the WASM is rejected with the event count", async () => {
  const withExtraEvent = defineSchema([
    ...fixtureTypes(),
    event("Ping", { target: "entity", value: "f32" }),
    event("Echo", { depth: "u32" }),
    event("Extra", { x: "f32" }),
  ]);

  await assert.rejects(
    loadRuntime(fixtureFactory(), withExtraEvent),
    (error: unknown) => error instanceof SchemaMismatchError && /events in WASM: 2, in TS: 3/.test(error.message),
  );
});

test("event with a size different from the WASM is rejected with the event name", async () => {
  const withWiderPing = defineSchema([...fixtureTypes(), event("Ping", { target: "entity", value: "f64" }), event("Echo", { depth: "u32" })]);

  await assert.rejects(
    loadRuntime(fixtureFactory(), withWiderPing),
    (error: unknown) => error instanceof SchemaMismatchError && /events in WASM: 2, in TS: 2/.test(error.message) && /divergent type, event or action: Ping/.test(error.message),
  );
});

test("extra action in TS is rejected with the action count", async () => {
  const withAction = defineSchema([
    ...fixtureTypes(),
    event("Ping", { target: "entity", value: "f32" }),
    event("Echo", { depth: "u32" }),
    action("fire", "button"),
    action("move", "axis2d"),
    action("aim", "pointer"),
    action("togglePause", "button"),
    action("extra", "button"),
  ]);

  await assert.rejects(
    loadRuntime(fixtureFactory(), withAction),
    (error: unknown) => error instanceof SchemaMismatchError && /actions in WASM: 4, in TS: 5/.test(error.message),
  );
});

test("action with a kind different from the WASM is rejected with the action name", async () => {
  const withWrongKind = defineSchema([
    ...fixtureTypes(),
    event("Ping", { target: "entity", value: "f32" }),
    event("Echo", { depth: "u32" }),
    action("fire", "axis2d"),
    action("move", "axis2d"),
    action("aim", "pointer"),
    action("togglePause", "button"),
  ]);

  await assert.rejects(
    loadRuntime(fixtureFactory(), withWrongKind),
    (error: unknown) => error instanceof SchemaMismatchError && /actions in WASM: 4, in TS: 4/.test(error.message) && /divergent type, event or action: fire/.test(error.message),
  );
});
