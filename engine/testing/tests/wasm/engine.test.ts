import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, Phase } from "@oly/core";
import type { Engine, SystemContext } from "@oly/core";
import { moduleOf } from "@oly/core/internal";

import { Position, Trace, loadFixture } from "./fixture.js";

const { runtime, fixture } = await loadFixture();

const StepSeconds = 1 / 60;

function createEngine(): Engine {
  const engine = runtime.createEngine({ fixedHz: 60 });
  engine.frame(0);

  return engine;
}

function stepper(engine: Engine): () => void {
  let step = 0;

  return () => {
    ++step;
    engine.frame(step * StepSeconds);
  };
}

function withEngine(body: (engine: Engine) => void): void {
  const engine = createEngine();
  try {
    body(engine);
    assert.equal(engine.world.checkInvariants(), null);
  } finally {
    engine.dispose();
  }
}

function appendDigit(entity: number, digit: number): (context: SystemContext) => void {
  return (context) => {
    const trace = context.get(entity, Trace);
    context.set(entity, Trace, { value: trace.value * 10 + digit });
  };
}

test("TS and C++ systems in phase and registration order", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    engine.addSystem(Phase.Update, "u2", appendDigit(entity, 2));
    engine.addSystem(Phase.UpdateBefore, "b1", appendDigit(entity, 1));
    engine.addNativeSystem(Phase.Update, "appendNine");
    engine.addSystem(Phase.Update, "u3", appendDigit(entity, 3));

    stepper(engine)();

    assert.equal(engine.world.get(entity, Trace).value, 1293);
  });
});

test("frame(k/60) for k = 0..60 runs 60 steps", () => {
  const engine = runtime.createEngine({ fixedHz: 60 });
  try {
    let steps = 0;
    engine.addSystem(Phase.Update, "count", () => {
      ++steps;
    });

    for (let k = 0; k <= 60; ++k) {
      engine.frame(k / 60);
    }

    assert.equal(steps, 60);
    assert.equal(engine.world.checkInvariants(), null);
  } finally {
    engine.dispose();
  }
});

test("context.spawn is deferred and C++ sees the value", () => {
  withEngine((engine) => {
    let spawned = 0;
    let aliveInSameSystem = true;
    let xInNextPhase = -1;
    engine.addSystem(Phase.Update, "spawn", (context) => {
      spawned = context.spawn([Position], { Position: { x: 5 } });
      aliveInSameSystem = context.isAlive(spawned);
    });
    engine.addSystem(Phase.UpdateAfter, "observe", (context) => {
      xInNextPhase = context.get(spawned, Position).x;
    });

    stepper(engine)();

    assert.equal(aliveInSameSystem, false);
    assert.equal(xInNextPhase, 5);
    assert.equal(fixture.fixtureSumPositionX(engine.world.handle), 5);
  });
});

test("memory grows in a C++ system before a TS system", () => {
  withEngine((engine) => {
    const module = moduleOf(runtime);
    const bufferBefore = module.HEAPU8.buffer;
    let sum = 0;
    engine.addNativeSystem(Phase.Update, "spawnBatch");
    engine.addSystem(Phase.UpdateAfter, "sum", (context) => {
      context.query({ read: [Position] }).eachBlock((block) => {
        const positions = block.read(Position);
        for (let row = 0; row < block.count; ++row) {
          sum += positions.x.get(row);
        }
      });
    });

    stepper(engine)();

    assert.notEqual(module.HEAPU8.buffer, bufferBefore);
    assert.equal(sum, 400000);
  });
});

test("direct structural change on the World during frame is ContractError", () => {
  withEngine((engine) => {
    let message = "";
    engine.addSystem(Phase.Update, "direct", () => {
      try {
        engine.world.spawn([Position]);
      } catch (error) {
        assert.ok(error instanceof ContractError);
        message = error.message;
      }
    });

    stepper(engine)();

    assert.match(message, /context\.spawn/);
    assert.ok(engine.world.isAlive(engine.world.spawn([Position])));
  });
});

test("context used outside the call is ContractError", () => {
  withEngine((engine) => {
    let captured: SystemContext | null = null;
    engine.addSystem(Phase.Update, "capture", (context) => {
      captured = context;
    });

    stepper(engine)();

    assert.ok(captured !== null);
    const stale: SystemContext = captured;
    assert.throws(() => stale.spawn([Position]), ContractError);
    assert.throws(() => stale.isAlive(0), ContractError);
  });
});

test("addNativeSystem with an unknown name is ContractError with the name", () => {
  withEngine((engine) => {
    assert.throws(() => engine.addNativeSystem(Phase.Update, "doesNotExist"), (error: unknown) => {
      return error instanceof ContractError && error.message.includes("doesNotExist");
    });
  });
});

test("addSystem during frame is ContractError", () => {
  withEngine((engine) => {
    let thrown: unknown = null;
    engine.addSystem(Phase.Update, "adder", () => {
      try {
        engine.addSystem(Phase.Update, "other", () => {});
      } catch (error) {
        thrown = error;
      }
    });

    stepper(engine)();

    assert.ok(thrown instanceof ContractError);
  });
});

test("two engines in the same module receive only their own systems", () => {
  const first = createEngine();
  const second = createEngine();
  try {
    let firstCalls = 0;
    let secondCalls = 0;
    first.addSystem(Phase.Update, "first", () => {
      ++firstCalls;
    });
    second.addSystem(Phase.Update, "second", () => {
      ++secondCalls;
    });

    stepper(second)();

    assert.equal(firstCalls, 0);
    assert.equal(secondCalls, 1);
  } finally {
    first.dispose();
    second.dispose();
  }
});

test("engine.world.dispose() is ContractError; frame after engine.dispose() is ContractError", () => {
  const engine = createEngine();

  assert.throws(() => engine.world.dispose(), ContractError);

  engine.dispose();

  assert.throws(() => engine.frame(1), ContractError);
});
