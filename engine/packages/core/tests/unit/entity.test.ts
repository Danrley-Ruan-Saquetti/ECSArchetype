import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, NullEntity, entityGeneration, entityIndex, formatEntity } from "@oly/core";
import { MaxEntityGeneration, packEntity } from "@oly/core/internal";

test("packs and unpacks", () => {
  const entity = packEntity(5, 3);

  assert.equal(entity, 3 * 2 ** 32 + 5);
  assert.equal(entityIndex(entity), 5);
  assert.equal(entityGeneration(entity), 3);
});

test("maximum generation fits in a safe integer", () => {
  const entity = packEntity(0xffffffff, MaxEntityGeneration);

  assert.ok(Number.isSafeInteger(entity));
  assert.equal(entityIndex(entity), 0xffffffff);
  assert.equal(entityGeneration(entity), MaxEntityGeneration);
});

test("generation above the maximum is an error", () => {
  assert.throws(() => packEntity(0, MaxEntityGeneration + 1), ContractError);
});

test("NullEntity has index 0xffffffff and generation 0", () => {
  assert.equal(entityIndex(NullEntity), 0xffffffff);
  assert.equal(entityGeneration(NullEntity), 0);
});

test("formatEntity writes index:generation", () => {
  assert.equal(formatEntity(packEntity(5, 3)), "5:3");
});
