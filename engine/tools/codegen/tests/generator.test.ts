import assert from "node:assert/strict";
import { test } from "node:test";

import { component, defineSchema, event, tag } from "@oly/core";

import { emitHeader, emitRegistry } from "../dist/emit_cpp.js";

const P = component("P", { a: "u8", b: "f64", owner: "entity" });
const T = tag("T");
const schema = defineSchema([P, T]);

test("struct na ordem de declaração", () => {
  assert.match(emitHeader(schema), /struct P \{\s*uint8_t a;\s*double b;\s*oly::ecs::Entity owner;\s*\};/);
});

test("static_asserts de tamanho e offsets", () => {
  const header = emitHeader(schema);

  assert.match(header, /sizeof\(P\) == 24 && alignof\(P\) == 8/);
  assert.match(header, /offsetof\(P, b\) == 8/);
  assert.match(header, /offsetof\(P, owner\) == 16/);
  assert.match(header, /esquema: layout de P diverge do TS/);
});

test("tag vira struct vazio", () => {
  assert.match(emitHeader(schema), /struct T \{\};/);
});

test("registro com hash e ids na ordem", () => {
  const registry = emitRegistry(schema);

  assert.ok(registry.includes(`0x${schema.hash.toString(16)}u`));
  assert.match(registry, /oly::ecs::componentId<P>\(\),\s*oly::ecs::componentId<T>\(\)/);
  assert.match(registry, /namespace oly::bridge/);
});

const Hit = event("Hit", { target: "entity", amount: "f32" });
const Boom = event("Boom", { power: "u32" });
const schemaWithEvents = defineSchema([component("Q", { a: "u8" }), Hit, tag("U"), Boom]);

test("evento vira struct com static_assert de layout", () => {
  const header = emitHeader(schemaWithEvents);

  assert.match(header, /struct Hit \{\s*oly::ecs::Entity target;\s*float amount;\s*\};/);
  assert.match(header, /sizeof\(Hit\) == 12 && alignof\(Hit\) == 4/);
  assert.match(header, /offsetof\(Hit, amount\) == 8/);
  assert.match(header, /esquema: layout de Hit diverge do TS/);
});

test("registro declara os eventos na ordem do esquema", () => {
  const registry = emitRegistry(schemaWithEvents);

  assert.ok(registry.includes('#include "oly/app/engine.hpp"'));
  assert.match(registry, /engine\.declareEvent<Hit>\("Hit"\);[\s\S]*engine\.declareEvent<Boom>\("Boom"\);/);
  assert.match(registry, /schemaEvents\(\)/);
});

test("esquema sem eventos gera schemaEvents vazio", () => {
  assert.match(emitRegistry(schema), /schemaEvents\(\) \{\s*return \{\};/);
});
