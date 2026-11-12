import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, Phase, state } from "@oly/core";
import type { Engine, Entity, State, StateContext } from "@oly/core";
import { moduleOf, scriptTaskCount } from "@oly/core/internal";

import { Ping, Position, Trace, loadFixture, menu, playing } from "./fixture.js";

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

function nothing(): void {}

function names(...states: (State | null)[]): (string | null)[] {
  return states.map((item) => item?.name ?? null);
}

test("requestState, engine.state and context.state; the last request wins", () => {
  withEngine((engine) => {
    const seen: (string | null)[] = [];
    engine.addSystem(Phase.Update, "watch", (context) => seen.push(context.state?.name ?? null));
    const step = stepper(engine);

    assert.equal(engine.state, null);

    engine.requestState(menu);
    engine.requestState(playing);

    assert.equal(engine.state, null);

    step();

    assert.equal(engine.state, playing);

    step();

    assert.deepEqual(seen, [null, "playing"]);
  });
});

test("C++ and TS hooks interleaved in registration order", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    const appendDigit = (digit: number) => (context: StateContext) => {
      context.set(entity, Trace, { value: context.get(entity, Trace).value * 10 + digit });
    };
    engine.onEnter(playing, "one", appendDigit(1));
    engine.addNativeOnEnter(playing, "appendEight");
    engine.onEnter(playing, "two", appendDigit(2));
    engine.requestState(playing);

    stepper(engine)();

    assert.equal(engine.world.get(entity, Trace).value, 182);
  });
});

test("fromState and toState in onExit and onEnter; null for the empty state", () => {
  withEngine((engine) => {
    const log: (string | null)[][] = [];
    const record = (label: string) => (context: StateContext) => {
      log.push([label, ...names(context.fromState, context.toState)]);
    };
    engine.onEnter(menu, "enterMenu", record("enterMenu"));
    engine.onExit(menu, "exitMenu", record("exitMenu"));
    engine.onEnter(playing, "enterPlaying", record("enterPlaying"));
    const step = stepper(engine);

    engine.requestState(menu);
    step();
    engine.requestState(playing);
    step();

    assert.deepEqual(log, [
      ["enterMenu", null, "menu"],
      ["exitMenu", "menu", "playing"],
      ["enterPlaying", "menu", "playing"],
    ]);
  });
});

test("inState from TS on a native system, a TS system and a handler", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    let handlerEvents = 0;
    engine.addNativeSystem(Phase.Update, "appendNine").inState(menu);
    engine.addSystem(Phase.Update, "appendSeven", (context) => {
      context.set(entity, Trace, { value: context.get(entity, Trace).value * 10 + 7 });
    }).inState(playing);
    engine.addSystem(Phase.UpdateBefore, "pinger", (context) => context.emit(Ping, { value: 1 }));
    engine.on(Ping, "counter", (_context, batch) => {
      handlerEvents += batch.count;
    }).inState(menu);
    const step = stepper(engine);

    step();

    assert.equal(engine.world.get(entity, Trace).value, 0);
    assert.equal(handlerEvents, 0);

    engine.requestState(menu);
    step();
    step();

    assert.equal(engine.world.get(entity, Trace).value, 9);
    assert.equal(handlerEvents, 1);

    engine.requestState(playing);
    step();
    step();

    assert.equal(engine.world.get(entity, Trace).value, 997);
  });
});

test("direct TS spawn in a hook and a handler is scoped; World.spawn outside a context is persistent", () => {
  withEngine((engine) => {
    let fromHook: Entity | undefined;
    let fromHandler: Entity | undefined;
    let emitted = false;
    engine.onEnter(menu, "spawn", (context) => {
      fromHook = context.spawn([Position]);
    });
    engine.addSystem(Phase.UpdateBefore, "emitOnce", (context) => {
      if (context.state === menu && !emitted) {
        emitted = true;
        context.emit(Ping, { value: 1 });
      }
    });
    engine.on(Ping, "spawn", (context) => {
      fromHandler = context.spawn([Position]);
    });
    const step = stepper(engine);
    engine.requestState(menu);
    step();
    step();
    const persistent = engine.world.spawn([Position]);

    assert.ok(engine.world.isAlive(fromHook!));
    assert.ok(engine.world.isAlive(fromHandler!));

    engine.requestState(playing);
    step();

    assert.ok(!engine.world.isAlive(fromHook!));
    assert.ok(!engine.world.isAlive(fromHandler!));
    assert.ok(engine.world.isAlive(persistent));
  });
});

test("persist in a TS system, in a direct context and in a task context", () => {
  withEngine((engine) => {
    let fromSystem: Entity | undefined;
    let fromHandler: Entity | undefined;
    let fromTask: Entity | undefined;
    let emitted = false;
    engine.addSystem(Phase.UpdateBefore, "persister", (context) => {
      if (context.state === menu && fromSystem === undefined) {
        fromSystem = context.spawn([Position]);
        context.persist(fromSystem);
      }
      if (context.state === menu && !emitted) {
        emitted = true;
        context.emit(Ping, { value: 1 });
      }
    });
    engine.on(Ping, "persister", (context) => {
      fromHandler = context.spawn([Position]);
      context.persist(fromHandler);
    });
    const step = stepper(engine);
    engine.requestState(menu);
    step();
    engine.schedule.after(0, (task) => {
      fromTask = task.spawn([Position]);
      task.persist(fromTask);
    });
    const kept = engine.schedule.after(100, nothing);
    const dropped = engine.schedule.after(100, nothing);
    engine.schedule.persist(kept);
    step();
    step();

    engine.requestState(playing);
    step();

    assert.ok(engine.world.isAlive(fromSystem!));
    assert.ok(engine.world.isAlive(fromHandler!));
    assert.ok(engine.world.isAlive(fromTask!));
    assert.ok(engine.schedule.isPending(kept));
    assert.ok(!engine.schedule.isPending(dropped));
    assert.ok(!engine.schedule.persist(dropped));
    engine.schedule.cancel(kept);
  });
});

test("closure map empty after the transition", () => {
  withEngine((engine) => {
    const step = stepper(engine);
    engine.requestState(menu);
    step();
    engine.schedule.every(1, nothing);
    engine.schedule.after(100, nothing);

    assert.equal(scriptTaskCount(engine.schedule), 2);

    engine.requestState(playing);
    step();

    assert.equal(scriptTaskCount(engine.schedule), 0);
  });
});

test("exception in TS onExit: state does not change and cleanup does not run; the next frame recovers", () => {
  withEngine((engine) => {
    let scoped: Entity | undefined;
    engine.onEnter(menu, "spawn", (context) => {
      scoped = context.spawn([Position]);
    });
    engine.onExit(menu, "boom", () => {
      throw new Error("boom");
    });
    const step = stepper(engine);
    engine.requestState(menu);
    step();

    engine.requestState(playing);

    assert.throws(() => step(), /boom/);
    assert.equal(engine.state, menu);
    assert.ok(engine.world.isAlive(scoped!));

    step();
    step();

    assert.equal(engine.state, menu);
  });
});

test("exception in TS onEnter: new state, and the following hooks do not run", () => {
  withEngine((engine) => {
    let secondCalls = 0;
    engine.onEnter(playing, "boom", () => {
      throw new Error("boom");
    });
    engine.onEnter(playing, "second", () => {
      ++secondCalls;
    });
    engine.requestState(playing);

    assert.throws(() => stepper(engine)(), /boom/);
    assert.equal(engine.state, playing);
    assert.equal(secondCalls, 0);
  });
});

test("memory grows inside the TS hook and the context stays valid", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 5 } });
    const module = moduleOf(runtime);
    let grew = false;
    let value = -1;
    engine.onEnter(menu, "grow", (context) => {
      const before = module.HEAPU8.buffer;
      for (let count = 0; count < 1_000_000 && module.HEAPU8.buffer === before; ++count) {
        context.spawn([Position, Trace]);
      }
      grew = module.HEAPU8.buffer !== before;
      value = context.get(entity, Trace).value;
    });
    engine.requestState(menu);

    stepper(engine)();

    assert.ok(grew, "WASM memory should have grown");
    assert.equal(value, 5);
  });
});

test("StateContext outside the hook call is ContractError", () => {
  withEngine((engine) => {
    let captured: StateContext | undefined;
    engine.onEnter(menu, "capture", (context) => {
      captured = context;
    });
    engine.requestState(menu);

    stepper(engine)();

    assert.throws(() => captured!.fromState, ContractError);
    assert.throws(() => captured!.toState, ContractError);
    assert.throws(() => captured!.requestState(playing), ContractError);
  });
});

test("errors: state from another schema, registration and inState during frame and after an interrupted frame, persist of a dead entity, unknown native hook", () => {
  withEngine((engine) => {
    const foreign = state("foreign");
    const duringFrame: unknown[] = [];
    const registration = engine.addNativeSystem(Phase.Update, "noop");
    let deadPersist: unknown;
    const dead = engine.world.spawn([Position]);
    engine.world.destroy(dead);
    let thrown = false;
    engine.addSystem(Phase.UpdateBefore, "violations", () => {
      for (const attempt of [
        () => engine.onEnter(menu, "late", nothing),
        () => engine.onExit(menu, "late", nothing),
        () => engine.addNativeOnEnter(menu, "appendEight"),
        () => registration.inState(menu),
      ]) {
        try {
          attempt();
        } catch (error) {
          duringFrame.push(error);
        }
      }
      if (!thrown) {
        thrown = true;
        throw new Error("boom");
      }
    });
    engine.schedule.after(0, (task) => {
      try {
        task.persist(dead);
      } catch (error) {
        deadPersist = error;
      }
    });
    const step = stepper(engine);

    assert.throws(() => engine.requestState(foreign), ContractError);
    assert.throws(() => engine.onEnter(foreign, "x", nothing), ContractError);
    assert.throws(() => registration.inState(foreign), ContractError);
    assert.throws(() => engine.addNativeOnEnter(menu, "doesNotExist"), ContractError);
    assert.throws(() => step(), /boom/);
    assert.equal(duringFrame.length, 4);
    for (const error of duringFrame) {
      assert.ok(error instanceof ContractError);
    }
    assert.throws(() => engine.onEnter(menu, "afterInterrupted", nothing), ContractError);
    assert.throws(() => registration.inState(menu), ContractError);

    step();
    step();

    assert.ok(deadPersist instanceof ContractError);
    assert.match((deadPersist as ContractError).message, /dead entity/);
  });
});
