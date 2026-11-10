import assert from "node:assert/strict";
import { test } from "node:test";

import { EngineContractError, NullEntity, entityGeneration, entityIndex, formatEntity } from "@oly/core";
import { MaxEntityGeneration, packEntity } from "@oly/core/internal";

test("empacota e desempacota", () => {
  const entity = packEntity(5, 3);
  assert.equal(entity, 3 * 2 ** 32 + 5);
  assert.equal(entityIndex(entity), 5);
  assert.equal(entityGeneration(entity), 3);
});

test("geração máxima cabe em inteiro seguro", () => {
  assert.ok(Number.isSafeInteger(packEntity(0xffffffff, MaxEntityGeneration)));
  assert.equal(entityIndex(packEntity(0xffffffff, MaxEntityGeneration)), 0xffffffff);
  assert.equal(entityGeneration(packEntity(0xffffffff, MaxEntityGeneration)), MaxEntityGeneration);
});

test("geração acima do máximo é erro", () => {
  assert.throws(() => packEntity(0, MaxEntityGeneration + 1), EngineContractError);
});

test("NullEntity", () => {
  assert.equal(entityIndex(NullEntity), 0xffffffff);
  assert.equal(entityGeneration(NullEntity), 0);
});

test("formatEntity", () => {
  assert.equal(formatEntity(packEntity(5, 3)), "5:3");
});
