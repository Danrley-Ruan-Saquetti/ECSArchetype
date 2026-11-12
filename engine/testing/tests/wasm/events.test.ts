import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, Phase } from "@oly/core";
import type { Engine, HandlerContext, SystemContext } from "@oly/core";
import { moduleOf } from "@oly/core/internal";

import { Echo, Mixed, Ping, Position, Trace, Velocity, loadFixture } from "./fixture.js";

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

function appendDigit(entity: number, digit: number): (context: HandlerContext) => void {
  return (context) => {
    const trace = context.get(entity, Trace);
    context.set(entity, Trace, { value: trace.value * 10 + digit });
  };
}

test("TS and C++ Ping handlers in registration order", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    engine.addSystem(Phase.Update, "emit", (context) => context.emit(Ping, { target: entity, value: 1 }));
    engine.on(Ping, "h1", appendDigit(entity, 1));
    engine.addNativeHandler(Ping, "appendNineOnPing");
    engine.on(Ping, "h2", appendDigit(entity, 2));

    stepper(engine)();

    assert.equal(engine.world.get(entity, Trace).value, 192);
  });
});

test("types dispatched in schema order, not in emission order", () => {
  withEngine((engine) => {
    const calls: string[] = [];
    engine.addSystem(Phase.Update, "emit", (context) => {
      context.emit(Echo, { depth: 1 });
      context.emit(Ping, { value: 1 });
    });
    engine.on(Echo, "echo", () => {
      calls.push("Echo");
    });
    engine.on(Ping, "ping", () => {
      calls.push("Ping");
    });

    stepper(engine)();

    assert.deepEqual(calls, ["Ping", "Echo"]);
  });
});

test("batch: count and values through the views, entity included", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    const seen = { count: 0, values: [] as number[], target: -1 };
    engine.addSystem(Phase.Update, "emit", (context) => {
      for (const value of [1, 2, 3]) {
        context.emit(Ping, { target: entity, value });
      }
    });
    engine.on(Ping, "read", (_context, batch) => {
      const view = batch.read();
      seen.count = batch.count;
      for (let index = 0; index < batch.count; ++index) {
        seen.values.push(view.value.get(index));
      }
      seen.target = view.target.get(0);
    });

    stepper(engine)();

    assert.equal(seen.count, 3);
    assert.deepEqual(seen.values, [1, 2, 3]);
    assert.equal(seen.target, entity);
  });
});

test("event emitted by a TS handler goes to the next round of the same step", () => {
  withEngine((engine) => {
    const pingCounts: number[] = [];
    const echoDepths: number[] = [];
    let first = true;
    engine.addSystem(Phase.Update, "emit", (context) => context.emit(Ping, { value: 7 }));
    engine.on(Ping, "ping", (context, batch) => {
      pingCounts.push(batch.count);
      if (first) {
        first = false;
        context.emit(Echo, { depth: 1 });
        context.emit(Ping, { value: 99 });
        assert.equal(batch.count, 1);
        assert.equal(batch.read().value.get(0), 7);
      }
    });
    engine.on(Echo, "echo", (_context, batch) => {
      echoDepths.push(batch.read().depth.get(0));
    });

    stepper(engine)();

    assert.deepEqual(pingCounts, [1, 1]);
    assert.deepEqual(echoDepths, [1]);
  });
});

test("TS handler destroys directly; repeated event does not break", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    engine.addSystem(Phase.Update, "emit", (context) => {
      context.emit(Ping, { target: entity });
      context.emit(Ping, { target: entity });
    });
    engine.on(Ping, "kill", (context, batch) => {
      const view = batch.read();
      for (let index = 0; index < batch.count; ++index) {
        const target = view.target.get(index);
        if (context.isAlive(target)) {
          context.destroy(target);
        }
      }
    });

    stepper(engine)();

    assert.equal(engine.world.isAlive(entity), false);
  });
});

test("event emitted in Render is dispatched in the next step", () => {
  withEngine((engine) => {
    let calls = 0;
    engine.addSystem(Phase.Render, "emit", (context) => context.emit(Ping, { value: 1 }));
    engine.on(Ping, "count", () => {
      ++calls;
    });
    const step = stepper(engine);

    step();
    assert.equal(calls, 0);

    step();
    assert.equal(calls, 1);
  });
});

test("memory grows inside the TS handler and the batch stays readable", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    const module = moduleOf(runtime);
    let grew = false;
    let value = -1;
    let target = -1;
    engine.addSystem(Phase.Update, "emit", (context) => context.emit(Ping, { target: entity, value: 42 }));
    engine.on(Ping, "grow", (context, batch) => {
      // The view is created before the growth and reused after it: it must not hold the old buffer.
      const view = batch.read();
      assert.equal(view.value.get(0), 42);
      const before = module.HEAPU8.buffer;
      for (let count = 0; count < 1_000_000 && module.HEAPU8.buffer === before; ++count) {
        context.spawn([Position, Velocity, Mixed, Trace]);
      }
      grew = module.HEAPU8.buffer !== before;
      value = view.value.get(0);
      target = view.target.get(0);
    });

    stepper(engine)();

    assert.ok(grew, "WASM memory should have grown");
    assert.equal(value, 42);
    assert.equal(target, entity);
  });
});

test("exception in a TS handler reaches the frame caller and the next frame discards the pending events", () => {
  withEngine((engine) => {
    let echoes = 0;
    let thrown = false;
    let armed = true;
    engine.addSystem(Phase.UpdateBefore, "arm", (context) => {
      if (armed) {
        armed = false;
        context.emit(Ping, { value: 1 });
      }
    });
    engine.on(Ping, "thrower", (context) => {
      context.emit(Echo, { depth: 1 });
      if (!thrown) {
        thrown = true;
        throw new Error("boom");
      }
    });
    engine.on(Echo, "echo", () => {
      ++echoes;
    });

    assert.throws(() => engine.frame(StepSeconds), /boom/);
    assert.equal(engine.world.checkInvariants(), null);

    engine.frame(StepSeconds);
    engine.frame(2 * StepSeconds);
    assert.equal(echoes, 0);

    armed = true;
    engine.frame(3 * StepSeconds);
    assert.equal(echoes, 1);
  });
});

test("registration after an interrupted frame is ContractError until the next frame", () => {
  withEngine((engine) => {
    engine.addSystem(Phase.UpdateBefore, "arm", (context) => context.emit(Ping, { value: 1 }));
    let thrown = false;
    engine.on(Ping, "thrower", () => {
      if (!thrown) {
        thrown = true;
        throw new Error("boom");
      }
    });

    assert.throws(() => engine.frame(StepSeconds), /boom/);

    const interrupted = (error: unknown) => error instanceof ContractError && /interrupted/.test(error.message);
    assert.throws(() => engine.on(Echo, "late", () => {}), interrupted);
    assert.throws(() => engine.addNativeHandler(Ping, "sumPing"), interrupted);
    assert.throws(() => engine.addSystem(Phase.Update, "late", () => {}), interrupted);
    assert.throws(() => engine.addNativeSystem(Phase.Update, "noop"), interrupted);

    engine.frame(StepSeconds);
    let echoes = 0;
    engine.on(Echo, "late", () => {
      ++echoes;
    });
    engine.addSystem(Phase.Update, "emitEcho", (context) => context.emit(Echo, { depth: 1 }));
    engine.frame(2 * StepSeconds);
    assert.equal(echoes, 1);
  });
});

test("direct structural change on the World inside the handler is allowed; inside the system it is still forbidden", () => {
  withEngine((engine) => {
    let handlerSpawned = -1;
    let systemError: unknown = null;
    engine.addSystem(Phase.Update, "emit", (context) => {
      try {
        engine.world.spawn([Position]);
      } catch (error) {
        systemError = error;
      }
      context.emit(Ping, { value: 1 });
    });
    engine.on(Ping, "spawn", () => {
      handlerSpawned = engine.world.spawn([Position]);
    });

    stepper(engine)();

    assert.ok(systemError instanceof ContractError);
    assert.ok(engine.world.isAlive(handlerSpawned));
  });
});

test("emit with an unknown field, with an event from another schema or outside the call is ContractError", () => {
  withEngine((engine) => {
    let captured: SystemContext | null = null;
    const errors: unknown[] = [];
    engine.addSystem(Phase.Update, "bad", (context) => {
      captured = context;
      try {
        context.emit(Ping, { nope: 1 } as never);
      } catch (error) {
        errors.push(error);
      }
    });

    stepper(engine)();

    assert.equal(errors.length, 1);
    assert.ok(errors[0] instanceof ContractError);
    assert.ok(captured !== null);
    const stale: SystemContext = captured;
    assert.throws(() => stale.emit(Ping, { value: 1 }), ContractError);
  });
});

test("addNativeHandler with an unknown name or one of another type is ContractError with the names", () => {
  withEngine((engine) => {
    assert.throws(() => engine.addNativeHandler(Ping, "doesNotExist"), (error: unknown) => {
      return error instanceof ContractError && error.message.includes("doesNotExist");
    });
    assert.throws(() => engine.addNativeHandler(Echo, "appendNineOnPing"), (error: unknown) => {
      return error instanceof ContractError && error.message.includes("appendNineOnPing") && error.message.includes("Echo");
    });
  });
});

test("on during frame is ContractError; batch.read() and context after the handler are ContractError", () => {
  withEngine((engine) => {
    let duringFrame: unknown = null;
    let staleContext: HandlerContext | null = null;
    let staleRead: (() => unknown) | null = null;
    engine.addSystem(Phase.Update, "emit", (context) => context.emit(Ping, { value: 1 }));
    engine.on(Ping, "capture", (context, batch) => {
      staleContext = context;
      staleRead = () => batch.read();
      try {
        engine.on(Echo, "late", () => {});
      } catch (error) {
        duringFrame = error;
      }
    });

    stepper(engine)();

    assert.ok(duringFrame instanceof ContractError);
    assert.ok(staleContext !== null && staleRead !== null);
    const context: HandlerContext = staleContext;
    const read: () => unknown = staleRead;
    assert.throws(() => context.isAlive(0), ContractError);
    assert.throws(read, ContractError);
  });
});

test("event without a handler is discarded at the end of the step", () => {
  withEngine((engine) => {
    let calls = 0;
    engine.addSystem(Phase.Update, "emit", (context) => context.emit(Ping, { value: 1 }));
    const step = stepper(engine);

    step();
    engine.on(Ping, "late", () => {
      ++calls;
    });
    step();

    assert.equal(calls, 1);
  });
});

test("maxEventRounds that is not a positive integer is ContractError in createEngine", () => {
  for (const maxEventRounds of [0, -1, 1.5, Number.NaN]) {
    assert.throws(() => runtime.createEngine({ maxEventRounds }), (error: unknown) => error instanceof ContractError && /maxEventRounds/.test(error.message));
  }

  const engine = runtime.createEngine({ maxEventRounds: 3 });
  engine.dispose();
});
