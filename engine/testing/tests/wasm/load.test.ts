import assert from "node:assert/strict";
import { test } from "node:test";

import { SchemaMismatchError } from "@oly/core";
import { loadEngine } from "@oly/core";
import { component, defineSchema, tag } from "@oly/core";
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
    loadEngine(fixtureFactory(), otherSchema),
    (error: unknown) =>
      error instanceof SchemaMismatchError && /0x[0-9a-f]+.*0x[0-9a-f]+/s.test(error.message) && /Position/.test(error.message),
  );
});
