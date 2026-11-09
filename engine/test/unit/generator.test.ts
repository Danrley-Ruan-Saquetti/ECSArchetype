import assert from "node:assert/strict";
import { test } from "node:test";

import { emitHeader, emitRegistry } from "../../generator/emit_cpp.js";
import { component, defineSchema, tag } from "../../ts/schema.js";

const P = component("P", { a: "u8", b: "f64", owner: "entity" });
const T = tag("T");
const schema = defineSchema([P, T]);

test("struct na ordem de declaração", () => {
  assert.match(emitHeader(schema), /struct P \{\s*uint8_t a;\s*double b;\s*ecs::Entity owner;\s*\};/);
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
  assert.match(registry, /ecs::componentId<P>\(\),\s*ecs::componentId<T>\(\)/);
});
