import assert from "node:assert/strict";
import { test } from "node:test";

import { SchemaMismatchError, component, defineSchema, event, loadRuntime, tag } from "@oly/core";

import { fixtureFactory, loadFixture } from "./fixture.js";

test("carrega com o esquema do fixture", async () => {
  const { runtime } = await loadFixture();
  const world = runtime.createWorld();

  assert.ok(world);
  assert.equal(world.checkInvariants(), null);
  world.dispose();
});

test("esquema divergente é rejeitado", async () => {
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

test("esquema TS com um evento a mais que o WASM é rejeitado com a contagem de eventos", async () => {
  const withExtraEvent = defineSchema([
    component("Position", { x: "f32", y: "f32" }),
    component("Velocity", { x: "f32", y: "f32" }),
    component("Mixed", { a: "u8", b: "f64", c: "i16", owner: "entity" }),
    component("Trace", { value: "f64" }),
    tag("Player"),
    tag("Enemy"),
    event("Extra", { x: "f32" }),
  ]);

  await assert.rejects(
    loadRuntime(fixtureFactory(), withExtraEvent),
    (error: unknown) => error instanceof SchemaMismatchError && /eventos no WASM: 0, no TS: 1/.test(error.message),
  );
});
