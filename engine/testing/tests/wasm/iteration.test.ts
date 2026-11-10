import assert from "node:assert/strict";
import { test } from "node:test";

import type { World } from "@oly/core";
import { bridgeOf } from "@oly/core/internal";
import { Position, Velocity, loadFixture } from "./fixture.js";

const { runtime } = await loadFixture();

function withWorld(body: (world: World) => void): void {
  const world = runtime.createWorld();
  try {
    body(world);
    assert.equal(world.checkInvariants(), null);
  } finally {
    world.dispose();
  }
}

test("exceção no callback de eachBlock encerra a iteração", () => {
  withWorld((world) => {
    world.spawn([Position]);

    assert.throws(
      () =>
        world.query({ read: [Position] }).eachBlock(() => {
          throw new Error("x");
        }),
      /x/,
    );

    assert.ok(world.spawn([Position]));
    assert.equal(world.checkInvariants(), null);
  });
});

test("eachBlock aninhado em outra query mantém o bloco externo", () => {
  withWorld((world) => {
    for (let i = 0; i < 8; ++i) {
      world.spawn([Position, Velocity], { Position: { x: i }, Velocity: { x: 100 + i } });
    }
    const positions = world.query({ read: [Position] });
    const velocities = world.query({ read: [Velocity] });

    positions.eachBlock((outer) => {
      const outerPosition = outer.read(Position);
      const before: number[] = [];
      for (let row = 0; row < outer.count; ++row) {
        before.push(outerPosition.x.get(row));
      }

      velocities.eachBlock((inner) => {
        assert.ok(inner.count > 0);
      });

      for (let row = 0; row < outer.count; ++row) {
        assert.equal(outerPosition.x.get(row), before[row]);
        assert.equal(world.get(outer.entity(row), Position).x, before[row]);
      }
    });
  });
});

test("query criada dentro de eachBlock (memória pode crescer) não corrompe o bloco externo", () => {
  withWorld((world) => {
    const entities: number[] = [];
    for (let i = 0; i < 16; ++i) {
      entities.push(world.spawn([Position, Velocity], { Position: { x: i * 3 }, Velocity: { x: i } }));
    }

    world.query({ read: [Position] }).eachBlock((outer) => {
      const bridge = bridgeOf(runtime);
      const pointer = bridge.allocate(64 * 1024 * 1024);

      const created = world.query({ read: [Position, Velocity] });
      created.eachBlock((inner) => {
        assert.equal(inner.count, 16);
      });
      bridge.free(pointer);

      const position = outer.read(Position);
      for (let row = 0; row < outer.count; ++row) {
        assert.equal(position.x.get(row), world.get(outer.entity(row), Position).x);
        assert.equal(position.x.get(row), entities.indexOf(outer.entity(row)) * 3);
      }
    });
  });
});
