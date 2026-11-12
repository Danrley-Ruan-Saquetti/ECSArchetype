import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, Phase, entityGeneration, entityIndex } from "@oly/core";
import type { Engine, Entity, Scheduler, TaskContext } from "@oly/core";
import { engineHandleOf, moduleOf, scriptTaskCount } from "@oly/core/internal";

import { Ping, Position, Trace, Velocity, Mixed, loadFixture } from "./fixture.js";

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

function appendDigit(entity: Entity, digit: number): (task: TaskContext) => void {
  return (task) => {
    const trace = task.get(entity, Trace);
    task.set(entity, Trace, { value: trace.value * 10 + digit });
  };
}

function nothing(): void {}

test("after e every do engine.schedule disparam no passo do vencimento", () => {
  withEngine((engine) => {
    const afterTimes: number[] = [];
    const everyTimes: number[] = [];
    engine.schedule.after(2 * StepSeconds, (task) => afterTimes.push(task.time));
    engine.schedule.every(0.025, (task) => everyTimes.push(task.time));
    const step = stepper(engine);

    for (let count = 0; count < 60; ++count) {
      step();
    }

    assert.equal(afterTimes.length, 1);
    assert.ok(Math.abs(afterTimes[0]! - 2 * StepSeconds) < 1e-9);
    assert.equal(everyTimes.length, 39);
  });
});

test("tarefas C++ e TS intercaladas por vencimento e criação", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    const handle = engineHandleOf(engine);
    const index = entityIndex(entity);
    const generation = entityGeneration(entity);
    fixture.fixtureScheduleDigit(handle, 0, index, generation, 1);
    engine.schedule.after(0, appendDigit(entity, 2));
    fixture.fixtureScheduleDigit(handle, StepSeconds, index, generation, 3);
    engine.schedule.after(StepSeconds, appendDigit(entity, 4));
    const step = stepper(engine);

    step();
    step();

    assert.equal(engine.world.get(entity, Trace).value, 1234);
  });
});

test("tarefa TS faz mudanças diretas e o evento emitido é despachado no mesmo passo", () => {
  withEngine((engine) => {
    let taskTime = -1;
    let handlerTime = -2;
    let handlerValue = -1;
    let created: Entity | undefined;
    engine.on(Ping, "ping", (context, batch) => {
      handlerTime = context.time;
      handlerValue = batch.read().value.get(0);
    });
    engine.schedule.after(0, (task) => {
      created = task.spawn([Position]);
      taskTime = task.time;
      task.emit(Ping, { value: 7 });
    });

    stepper(engine)();

    assert.ok(created !== undefined && engine.world.isAlive(created));
    assert.equal(handlerValue, 7);
    assert.equal(handlerTime, taskTime);
  });
});

test("cancel, isPending e every que cancela a si mesma", () => {
  withEngine((engine) => {
    let afterRuns = 0;
    let everyRuns = 0;
    const pending = engine.schedule.after(10, () => {
      ++afterRuns;
    });

    assert.equal(engine.schedule.isPending(pending), true);
    assert.equal(engine.schedule.cancel(pending), true);
    assert.equal(engine.schedule.isPending(pending), false);
    assert.equal(engine.schedule.cancel(pending), false);

    const self = engine.schedule.every(StepSeconds, (task) => {
      ++everyRuns;
      task.schedule.cancel(task.handle);
    });
    const step = stepper(engine);
    for (let count = 0; count < 4; ++count) {
      step();
    }

    assert.equal(afterRuns, 0);
    assert.equal(everyRuns, 1);
    assert.equal(engine.schedule.isPending(self), false);
  });
});

test("a mesma closure agendada duas vezes: cancelar um handle não afeta o outro", () => {
  withEngine((engine) => {
    let runs = 0;
    const closure = (): void => {
      ++runs;
    };
    const first = engine.schedule.after(StepSeconds, closure);
    const second = engine.schedule.after(StepSeconds, closure);

    assert.equal(engine.schedule.cancel(first), true);
    assert.equal(engine.schedule.isPending(second), true);
    const step = stepper(engine);
    step();
    step();

    assert.equal(runs, 1);
    assert.equal(scriptTaskCount(engine.schedule), 0);
  });
});

test("exceção em after TS chega a quem chamou frame; o frame seguinte se recupera e a closure some", () => {
  withEngine((engine) => {
    engine.schedule.after(StepSeconds, () => {
      throw new Error("boom");
    });
    const step = stepper(engine);

    step();

    assert.throws(step, /boom/);
    assert.doesNotThrow(step);
    assert.equal(scriptTaskCount(engine.schedule), 0);
  });
});

test("every TS que lança continua pendente e roda de novo depois da recuperação", () => {
  withEngine((engine) => {
    let runs = 0;
    const handle = engine.schedule.every(StepSeconds, () => {
      ++runs;
      if (runs === 1) {
        throw new Error("boom");
      }
    });
    const step = stepper(engine);
    step();

    assert.throws(step, /boom/);
    for (let count = 0; count < 4; ++count) {
      step();
    }

    assert.ok(runs > 1, `a tarefa deveria rodar de novo (runs=${runs})`);
    assert.equal(engine.schedule.isPending(handle), true);
    assert.equal(scriptTaskCount(engine.schedule), 1);
  });
});

test("memória cresce dentro da tarefa TS e o contexto continua válido", () => {
  withEngine((engine) => {
    const entity = engine.world.spawn([Trace], { Trace: { value: 0 } });
    const module = moduleOf(runtime);
    let grew = false;
    let value = -1;
    engine.schedule.after(0, (task) => {
      const before = module.HEAPU8.buffer;
      for (let count = 0; count < 1_000_000 && module.HEAPU8.buffer === before; ++count) {
        task.spawn([Position, Velocity, Mixed, Trace]);
      }
      grew = module.HEAPU8.buffer !== before;
      task.set(entity, Trace, { value: 5 });
      value = task.get(entity, Trace).value;
    });

    stepper(engine)();

    assert.ok(grew, "a memória do WASM deveria ter crescido");
    assert.equal(value, 5);
  });
});

test("mapa de closures termina vazio em todos os caminhos", () => {
  withEngine((engine) => {
    const schedule = engine.schedule;
    const step = stepper(engine);

    schedule.after(StepSeconds, nothing);
    const cancelled = schedule.after(10, nothing);
    schedule.cancel(cancelled);
    schedule.every(StepSeconds, (task) => {
      task.schedule.cancel(task.handle);
    });
    schedule.after(2 * StepSeconds, () => {
      throw new Error("boom");
    });
    step();
    step();
    assert.throws(step, /boom/);
    step();

    assert.equal(scriptTaskCount(schedule), 0);
  });

  const engine = createEngine();
  const schedule = engine.schedule;
  schedule.every(1, nothing);
  schedule.after(10, nothing);
  assert.equal(scriptTaskCount(schedule), 2);

  engine.dispose();

  assert.equal(scriptTaskCount(schedule), 0);
});

test("context.schedule e engine.schedule são o mesmo objeto", () => {
  withEngine((engine) => {
    let fromSystem: Scheduler | undefined;
    let fromTask: Scheduler | undefined;
    engine.addSystem(Phase.Update, "capture", (context) => {
      fromSystem = context.schedule;
    });
    engine.schedule.after(0, (task) => {
      fromTask = task.schedule;
    });

    stepper(engine)();

    assert.equal(fromSystem, engine.schedule);
    assert.equal(fromTask, engine.schedule);
  });
});

test("erros: atraso negativo ou NaN, intervalo 0, callback que não é função, handle de outra Engine, Engine liberada", () => {
  withEngine((engine) => {
    assert.throws(() => engine.schedule.after(-1, nothing), ContractError);
    assert.throws(() => engine.schedule.after(Number.NaN, nothing), ContractError);
    assert.throws(() => engine.schedule.every(0, nothing), ContractError);
    assert.throws(() => engine.schedule.every(Number.POSITIVE_INFINITY, nothing), ContractError);
    assert.throws(() => engine.schedule.after(1, "não é função" as never), ContractError);

    const other = createEngine();
    try {
      const foreign = other.schedule.after(1, nothing);

      assert.throws(() => engine.schedule.cancel(foreign), ContractError);
      assert.throws(() => engine.schedule.isPending(foreign), ContractError);
    } finally {
      other.dispose();
    }

    assert.equal(scriptTaskCount(engine.schedule), 0);
  });

  const released = createEngine();
  const schedule = released.schedule;
  released.dispose();

  assert.throws(() => released.schedule, ContractError);
  assert.throws(() => schedule.after(1, nothing), ContractError);
});

test("TaskContext fora da chamada da tarefa é ContractError", () => {
  withEngine((engine) => {
    let captured: TaskContext | undefined;
    engine.schedule.after(0, (task) => {
      captured = task;
    });

    stepper(engine)();

    assert.ok(captured !== undefined);
    assert.throws(() => captured!.time, ContractError);
    assert.throws(() => captured!.handle, ContractError);
    assert.throws(() => captured!.schedule, ContractError);
  });
});
