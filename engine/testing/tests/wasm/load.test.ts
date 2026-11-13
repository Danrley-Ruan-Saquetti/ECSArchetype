import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, SchemaMismatchError, loadRuntime } from "@oly/core";
import type { OlyModuleFactory } from "@oly/core";

import extraActionSchema from "../../fixture/schemas/extra_action.generated.js";
import extraEventSchema from "../../fixture/schemas/extra_event.generated.js";
import fireAxis2dSchema from "../../fixture/schemas/fire_axis2d.generated.js";
import positionF64Schema from "../../fixture/schemas/position_f64.generated.js";
import widerPingSchema from "../../fixture/schemas/wider_ping.generated.js";
import { fixtureFactory, loadFixture, schema } from "./fixture.js";

test("loads with the fixture schema", async () => {
  const { runtime } = await loadFixture();
  const world = runtime.createWorld();

  assert.ok(world);
  assert.equal(world.checkInvariants(), null);
  world.dispose();
});

test("mismatched schema is rejected", async () => {
  await assert.rejects(
    loadRuntime(fixtureFactory(), positionF64Schema),
    (error: unknown) =>
      error instanceof SchemaMismatchError && /0x[0-9a-f]+.*0x[0-9a-f]+/s.test(error.message) && /Position/.test(error.message),
  );
});

test("a module without the web host's invoker is rejected at load", async () => {
  const factory = fixtureFactory();
  const withoutWebHost: OlyModuleFactory = async () => {
    const module = await factory();
    Reflect.deleteProperty(module, "_olyWebHostInstallScriptInvoker");

    return module;
  };

  await assert.rejects(
    loadRuntime(withoutWebHost, schema),
    (error: unknown) => error instanceof ContractError && /olyWebHostInstallScriptInvoker/.test(error.message) && /oly::web_host/.test(error.message),
  );
});

test("TS schema with one more event than the WASM is rejected with the event count", async () => {
  await assert.rejects(
    loadRuntime(fixtureFactory(), extraEventSchema),
    (error: unknown) => error instanceof SchemaMismatchError && /events in WASM: 2, in TS: 3/.test(error.message),
  );
});

test("event with a size different from the WASM is rejected with the event name", async () => {
  await assert.rejects(
    loadRuntime(fixtureFactory(), widerPingSchema),
    (error: unknown) => error instanceof SchemaMismatchError && /events in WASM: 2, in TS: 2/.test(error.message) && /divergent type, event or action: Ping/.test(error.message),
  );
});

test("extra action in TS is rejected with the action count", async () => {
  await assert.rejects(
    loadRuntime(fixtureFactory(), extraActionSchema),
    (error: unknown) => error instanceof SchemaMismatchError && /actions in WASM: 4, in TS: 5/.test(error.message),
  );
});

test("action with a kind different from the WASM is rejected with the action name", async () => {
  await assert.rejects(
    loadRuntime(fixtureFactory(), fireAxis2dSchema),
    (error: unknown) => error instanceof SchemaMismatchError && /actions in WASM: 4, in TS: 4/.test(error.message) && /divergent type, event or action: fire/.test(error.message),
  );
});
