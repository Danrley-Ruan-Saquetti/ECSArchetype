import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, entityGeneration, entityIndex } from "@oly/core";
import type { World } from "@oly/core";

import { Enemy, Mixed, Player, Position, Velocity, loadFixture } from "./fixture.js";

const { runtime, fixture } = await loadFixture();

function withWorld(body: (world: World) => void): void {
  const world = runtime.createWorld();
  try {
    body(world);
    assert.equal(world.checkInvariants(), null);
  } finally {
    world.dispose();
  }
}

test("spawn com valores e get", () => {
  withWorld((world) => {
    const entity = world.spawn([Player, Position, Velocity], { Position: { x: 1.5, y: -2 } });

    assert.deepEqual(world.get(entity, Position), { x: 1.5, y: -2 });
    assert.deepEqual(world.get(entity, Velocity), { x: 0, y: 0 });
    assert.ok(world.has(entity, Player));
    assert.equal(world.has(entity, Enemy), false);
  });
});

test("set parcial", () => {
  withWorld((world) => {
    const entity = world.spawn([Position], { Position: { x: 1.5, y: -2 } });

    world.set(entity, Position, { y: 7 });

    assert.deepEqual(world.get(entity, Position), { x: 1.5, y: 7 });
  });
});

test("add de tag, add que atribui, remove", () => {
  withWorld((world) => {
    const entity = world.spawn([Position]);

    world.add(entity, Enemy);
    assert.ok(world.has(entity, Enemy));

    world.add(entity, Velocity, { x: 3, y: 4 });
    world.add(entity, Velocity, { x: 5, y: 6 });
    assert.deepEqual(world.get(entity, Velocity), { x: 5, y: 6 });

    world.remove(entity, Enemy);
    world.remove(entity, Velocity);
    assert.equal(world.has(entity, Enemy), false);
    assert.equal(world.has(entity, Velocity), false);
    assert.deepEqual(world.get(entity, Position), { x: 0, y: 0 });
  });
});

test("destroy e reuso do slot", () => {
  withWorld((world) => {
    const entity = world.spawn([Position]);

    world.destroy(entity);
    assert.equal(world.isAlive(entity), false);

    const reused = world.spawn([Position]);
    assert.equal(entityIndex(reused), entityIndex(entity));
    assert.equal(entityGeneration(reused), entityGeneration(entity) + 1);
  });
});

test("Mixed bate com o C++", () => {
  withWorld((world) => {
    const other = world.spawn([Player]);
    const entity = world.spawn([Mixed], { Mixed: { a: 200, b: 1.5, c: -3, owner: other } });

    assert.deepEqual(world.get(entity, Mixed), { a: 200, b: 1.5, c: -3, owner: other });
    assert.equal(
      fixture.fixtureMixedChecksum(world.handle, entityIndex(entity), entityGeneration(entity)),
      200 + 1.5 - 3 + entityIndex(other) + entityGeneration(other),
    );
  });
});

test("valor escrito pelo TS é visto pelo C++", () => {
  withWorld((world) => {
    for (const x of [1, 2, 3]) {
      world.spawn([Position], { Position: { x } });
    }

    assert.equal(fixture.fixtureSumPositionX(world.handle), 6);
  });
});

test("erros de contrato", () => {
  withWorld((world) => {
    const dead = world.spawn([Position]);
    world.destroy(dead);

    for (const [operation, action] of [
      ["get", () => world.get(dead, Position)],
      ["set", () => world.set(dead, Position, { x: 1 })],
      ["destroy", () => world.destroy(dead)],
      ["add", () => world.add(dead, Velocity)],
      ["remove", () => world.remove(dead, Position)],
    ] as const) {
      assert.throws(
        action,
        (error: unknown) => error instanceof ContractError && error.message.includes("e=") && error.message.includes(operation),
        operation,
      );
    }

    const alive = world.spawn([Position]);
    assert.throws(
      () => world.get(alive, Velocity),
      (error: unknown) => error instanceof ContractError && error.message.includes("type=Velocity"),
    );
    assert.throws(() => world.set(alive, Velocity, { x: 1 }), ContractError);
    assert.throws(() => world.spawn([Position], { Velocity: { x: 1 } } as never), ContractError);
    assert.throws(() => world.spawn([Position], { Position: { z: 1 } } as never), ContractError);

    assert.ok(world.spawn([Position]));
  });
});

test("dispose", () => {
  const world = runtime.createWorld();

  world.dispose();

  assert.throws(() => world.spawn([Position]), ContractError);
});
