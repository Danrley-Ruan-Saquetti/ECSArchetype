import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, Phase } from "@oly/core";
import type { Engine } from "@oly/core";

import { Position, loadFixture } from "./fixture.js";

const { runtime } = await loadFixture();

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

test("paused: only whilePaused runs, time freezes, render continues", () => {
  withEngine((engine) => {
    const calls: string[] = [];
    engine.addSystem(Phase.Update, "u", () => calls.push("u"));
    engine.addSystem(Phase.Update, "p", () => calls.push("p")).whilePaused();
    engine.addSystem(Phase.Render, "r", () => calls.push("r"));
    const step = stepper(engine);

    engine.pause();
    step();
    step();

    assert.deepEqual(calls, ["p", "r", "p", "r"]);
    assert.equal(engine.time, 0);
    assert.equal(engine.paused, true);

    engine.resume();
    calls.length = 0;
    step();

    assert.deepEqual(calls, ["u", "r"]);
    assert.ok(engine.time > 0);
  });
});

test("whilePaused system calls context.resume() and the next step runs gameplay", () => {
  withEngine((engine) => {
    const calls: string[] = [];
    engine
      .addSystem(Phase.Update, "p", (context) => {
        calls.push("p");
        context.resume();
      })
      .whilePaused();
    engine.addSystem(Phase.Update, "u", () => calls.push("u"));
    const step = stepper(engine);

    engine.pause();
    step();
    step();

    assert.deepEqual(calls, ["p", "u"]);
    assert.equal(engine.paused, false);
  });
});

test("whilePaused() on a Render system is ContractError", () => {
  withEngine((engine) => {
    const registration = engine.addSystem(Phase.Render, "r", () => {});

    assert.throws(() => registration.whilePaused(), ContractError);
  });
});

test("setHidden(true) ignores frames; after setHidden(false) the 1st frame only records the time", () => {
  withEngine((engine) => {
    const calls: string[] = [];
    engine.addSystem(Phase.Update, "u", () => calls.push("u"));
    engine.addSystem(Phase.Render, "r", () => calls.push("r"));

    engine.setHidden(true);
    engine.frame(1);
    assert.deepEqual(calls, []);

    engine.setHidden(false);
    engine.frame(2);
    assert.deepEqual(calls, []);

    engine.frame(2 + StepSeconds);
    assert.deepEqual(calls, ["u", "r"]);
  });
});

test("exception inside eachBlock in a TS system reaches the frame caller and the next frame recovers", () => {
  withEngine((engine) => {
    engine.world.spawn([Position]);
    let calls = 0;
    let pending = -1;
    engine.addSystem(Phase.Update, "thrower", (context) => {
      ++calls;
      if (calls > 1) {
        return;
      }

      pending = context.spawn([Position]);
      context.query({ read: [Position] }).eachBlock(() => {
        throw new Error("boom");
      });
    });
    const step = stepper(engine);

    assert.throws(() => step(), /boom/);
    assert.equal(engine.world.checkInvariants(), null);
    assert.equal(engine.world.isAlive(pending), false);
    assert.ok(engine.world.isAlive(engine.world.spawn([Position])));

    step();
    assert.equal(calls, 1);

    step();
    assert.equal(calls, 2);
  });
});

test("frame called from inside a system is ContractError", () => {
  withEngine((engine) => {
    let thrown: unknown = null;
    engine.addSystem(Phase.Update, "reentrant", () => {
      try {
        engine.frame(10);
      } catch (error) {
        thrown = error;
      }
    });

    stepper(engine)();

    assert.ok(thrown instanceof ContractError);
  });
});

test("frame with a time lower than the previous one or NaN is ContractError", () => {
  withEngine((engine) => {
    engine.frame(1);

    assert.throws(() => engine.frame(0.5), ContractError);
    assert.throws(() => engine.frame(Number.NaN), ContractError);
    assert.throws(() => engine.frame(Number.POSITIVE_INFINITY), ContractError);
  });
});
