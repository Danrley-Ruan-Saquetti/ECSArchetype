import assert from "node:assert/strict";
import { test } from "node:test";

import { action, component, defineSchema, event, state, tag } from "@oly/core";

import { emitHeader, emitRegistry } from "../dist/emit_cpp.js";

const P = component("P", { a: "u8", b: "f64", owner: "entity" });
const T = tag("T");
const schema = defineSchema([P, T]);

test("struct in declaration order", () => {
  assert.match(emitHeader(schema), /struct P \{\s*uint8_t a;\s*double b;\s*oly::ecs::Entity owner;\s*\};/);
});

test("size and offset static_asserts", () => {
  const header = emitHeader(schema);

  assert.match(header, /sizeof\(P\) == 24 && alignof\(P\) == 8/);
  assert.match(header, /offsetof\(P, b\) == 8/);
  assert.match(header, /offsetof\(P, owner\) == 16/);
  assert.match(header, /schema: layout of P diverges from TS/);
});

test("tag becomes an empty struct", () => {
  assert.match(emitHeader(schema), /struct T \{\};/);
});

test("registry with hash and ids in order", () => {
  const registry = emitRegistry(schema);

  assert.ok(registry.includes(`0x${schema.hash.toString(16)}u`));
  assert.match(registry, /oly::ecs::componentId<P>\(\),\s*oly::ecs::componentId<T>\(\)/);
  assert.match(registry, /namespace oly::bridge/);
});

const Hit = event("Hit", { target: "entity", amount: "f32" });
const Boom = event("Boom", { power: "u32" });
const schemaWithEvents = defineSchema([component("Q", { a: "u8" }), Hit, tag("U"), Boom]);

test("event becomes a struct with layout static_assert", () => {
  const header = emitHeader(schemaWithEvents);

  assert.match(header, /struct Hit \{\s*oly::ecs::Entity target;\s*float amount;\s*\};/);
  assert.match(header, /sizeof\(Hit\) == 12 && alignof\(Hit\) == 4/);
  assert.match(header, /offsetof\(Hit, amount\) == 8/);
  assert.match(header, /schema: layout of Hit diverges from TS/);
});

test("registry declares the events in schema order", () => {
  const registry = emitRegistry(schemaWithEvents);

  assert.ok(registry.includes('#include "oly/app/engine.hpp"'));
  assert.match(registry, /engine\.declareEvent<Hit>\("Hit"\);[\s\S]*engine\.declareEvent<Boom>\("Boom"\);/);
  assert.match(registry, /schemaEvents\(\)/);
});

test("schema without events generates an empty schemaEvents", () => {
  assert.match(emitRegistry(schema), /schemaEvents\(\) \{\s*return \{\};/);
});

const fire = action("fire", "button");
const move = action("move", "axis2d");
const schemaWithActions = defineSchema([component("R", { a: "u8" }), fire, move]);

test("header with actions declares actions::<name> with the handle of the kind", () => {
  const header = emitHeader(schemaWithActions);

  assert.ok(header.includes('#include "oly/input/action_map.hpp"'));
  assert.ok(header.includes("inline constexpr oly::input::ButtonAction fire{0};"));
  assert.ok(header.includes("inline constexpr oly::input::Axis2dAction move{1};"));
});

test("header without actions does not include the input module", () => {
  assert.ok(!emitHeader(schema).includes("oly/input"));
});

test("registry lists the actions in schema order", () => {
  const registry = emitRegistry(schemaWithActions);

  assert.ok(registry.indexOf('{"fire", oly::input::ActionKind::Button}') >= 0);
  assert.ok(registry.indexOf('{"fire", oly::input::ActionKind::Button}') < registry.indexOf('{"move", oly::input::ActionKind::Axis2d}'));
});

test("schema without actions generates an empty schemaActions", () => {
  assert.match(emitRegistry(schema), /schemaActions\(\) \{\s*return \{\};/);
});

const schemaWithStates = defineSchema([component("S", { a: "u8" }), state("menu"), state("playing")]);

test("header with states declares states::<name> with the StateId", () => {
  const header = emitHeader(schemaWithStates);

  assert.ok(header.includes('#include "oly/app/state.hpp"'));
  assert.ok(header.includes("namespace states {"));
  assert.ok(header.includes("inline constexpr oly::app::StateId menu{0};"));
  assert.ok(header.includes("inline constexpr oly::app::StateId playing{1};"));
});

test("header without states does not include oly/app/state", () => {
  assert.ok(!emitHeader(schema).includes("oly/app/state"));
});

test("registry lists the states in schema order", () => {
  const registry = emitRegistry(schemaWithStates);

  assert.ok(registry.indexOf('{"menu"}') >= 0);
  assert.ok(registry.indexOf('{"menu"}') < registry.indexOf('{"playing"}'));
  assert.match(registry, /schemaStates\(\)/);
});

test("schema without states generates an empty schemaStates", () => {
  assert.match(emitRegistry(schema), /schemaStates\(\) \{\s*return \{\};/);
});
