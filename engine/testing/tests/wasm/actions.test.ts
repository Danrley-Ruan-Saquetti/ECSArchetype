import assert from "node:assert/strict";
import { test } from "node:test";

import { ContractError, Phase, action, defineSchema } from "@oly/core";
import type { ButtonName, ButtonState, Engine } from "@oly/core";
import { moduleOf } from "@oly/core/internal";

import { aim, fire, loadFixture, move, togglePause } from "./fixture.js";

const { runtime, fixture } = await loadFixture();

const StepSeconds = 1 / 60;

const idle: ButtonState = { pressed: false, held: false, released: false };

function stepper(engine: Engine): () => void {
  let step = 0;

  return () => {
    ++step;
    engine.frame(step * StepSeconds);
  };
}

function withEngine(body: (engine: Engine) => void): void {
  const engine = runtime.createEngine({ fixedHz: 60 });
  engine.frame(0);
  try {
    body(engine);
    assert.equal(engine.world.checkInvariants(), null);
  } finally {
    engine.dispose();
  }
}

test("antes da primeira captura tudo é falso e o ponteiro é inválido", () => {
  withEngine((engine) => {
    assert.deepEqual(engine.actions.button(fire), idle);
    assert.deepEqual(engine.actions.axis2d(move), { x: 0, y: 0 });
    assert.equal(engine.actions.pointer(aim).valid, false);
  });
});

test("sistema TS lê pressed, held e released", () => {
  withEngine((engine) => {
    const seen: ButtonState[] = [];
    engine.addSystem(Phase.Update, "reader", (context) => {
      seen.push(context.actions.button(fire));
    });
    engine.actions.bind(fire, "Space");
    const step = stepper(engine);

    engine.actions.buttonDown("Space");
    step();
    step();
    engine.actions.buttonUp("Space");
    step();

    assert.deepEqual(seen, [
      { pressed: true, held: true, released: false },
      { pressed: false, held: true, released: false },
      { pressed: false, held: false, released: true },
    ]);
  });
});

test("toque mais curto que um passo aparece num passo só", () => {
  withEngine((engine) => {
    const seen: ButtonState[] = [];
    engine.addSystem(Phase.Update, "reader", (context) => {
      seen.push(context.actions.button(fire));
    });
    engine.actions.bind(fire, "Space");
    const step = stepper(engine);

    engine.actions.buttonDown("Space");
    engine.actions.buttonUp("Space");
    step();
    step();

    assert.deepEqual(seen, [{ pressed: true, held: false, released: true }, idle]);
  });
});

test("sistemas C++ e TS veem o mesmo snapshot no mesmo passo", () => {
  withEngine((engine) => {
    let tsSawPressed = false;
    engine.addNativeSystem(Phase.Update, "countFirePresses");
    engine.addSystem(Phase.Update, "reader", (context) => {
      tsSawPressed = context.actions.button(fire).pressed;
    });
    engine.actions.bind(fire, "Space");
    fixture.fixtureTakeFirePressCount();

    engine.actions.buttonDown("Space");
    engine.actions.buttonUp("Space");
    stepper(engine)();

    assert.equal(fixture.fixtureTakeFirePressCount(), 1);
    assert.equal(tsSawPressed, true);
  });
});

test("eixo WASD com diagonal normalizada e ponteiro", () => {
  withEngine((engine) => {
    engine.actions.bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });
    const step = stepper(engine);

    engine.actions.buttonDown("KeyW");
    engine.actions.buttonDown("KeyD");
    engine.actions.pointerMove(10, 20);
    step();

    const axis = engine.actions.axis2d(move);
    assert.ok(Math.abs(axis.x - Math.fround(0.70710678)) < 1e-6);
    assert.ok(Math.abs(axis.y + Math.fround(0.70710678)) < 1e-6);
    assert.deepEqual(engine.actions.pointer(aim), { x: 10, y: 20, valid: true });
  });
});

test("sair da pausa por togglePause num sistema whilePaused", () => {
  withEngine((engine) => {
    engine
      .addSystem(Phase.Update, "toggle", (context) => {
        if (context.actions.button(togglePause).pressed) {
          context.resume();
        }
      })
      .whilePaused();
    engine.actions.bind(togglePause, "Escape");
    const step = stepper(engine);

    engine.pause();
    engine.actions.buttonDown("Escape");
    step();

    assert.equal(engine.paused, false);
  });
});

test("religar de dentro de um sistema vale na captura seguinte, sem bordas fantasmas", () => {
  withEngine((engine) => {
    const seen: ButtonState[] = [];
    let rebound = false;
    engine.addSystem(Phase.Update, "rebind", (context) => {
      seen.push(context.actions.button(fire));
      if (!rebound) {
        rebound = true;
        context.actions.bind(fire, "KeyQ");
      }
    });
    const step = stepper(engine);

    engine.actions.buttonDown("KeyQ");
    step();
    step();

    assert.deepEqual(seen, [idle, { pressed: false, held: true, released: false }]);
  });
});

test("memória cresce antes do sistema TS e a leitura continua certa", () => {
  withEngine((engine) => {
    const bufferBefore = moduleOf(runtime).HEAPU8.buffer;
    let pressed = false;
    engine.addNativeSystem(Phase.Update, "spawnBatch");
    engine.addSystem(Phase.UpdateAfter, "reader", (context) => {
      pressed = context.actions.button(fire).pressed;
    });
    engine.actions.bind(fire, "Space");

    engine.actions.buttonDown("Space");
    stepper(engine)();

    assert.notEqual(moduleOf(runtime).HEAPU8.buffer, bufferBefore);
    assert.equal(pressed, true);
  });
});

test("isBound e clearBindings", () => {
  withEngine((engine) => {
    engine.actions.bind(fire, "Space");
    engine.actions.bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });

    assert.equal(engine.actions.isBound("Space"), true);
    assert.equal(engine.actions.isBound("KeyD"), true);
    assert.equal(engine.actions.isBound("KeyQ"), false);

    engine.actions.clearBindings(fire);
    assert.equal(engine.actions.isBound("Space"), false);
  });
});

test("erros: botão fora da tabela, ação de outro esquema, tipo errado, ponteiro não finito, Engine liberada", () => {
  withEngine((engine) => {
    const foreign = defineSchema([action("fire", "button")]).actions[0]!;

    assert.throws(() => engine.actions.bind(fire, "w" as ButtonName), ContractError);
    assert.throws(() => engine.actions.buttonDown("Unidentified" as ButtonName), ContractError);
    assert.throws(() => engine.actions.bind(foreign as typeof fire, "Space"), ContractError);
    assert.throws(() => engine.actions.button(move as never), ContractError);
    assert.throws(() => engine.actions.bind(move as never, "Space"), ContractError);
    assert.throws(() => engine.actions.pointerMove(Number.NaN, 0), ContractError);
    assert.throws(() => engine.actions.pointerMove(0, Number.POSITIVE_INFINITY), ContractError);
  });

  const engine = runtime.createEngine();
  const actions = engine.actions;
  engine.dispose();

  assert.throws(() => engine.actions, ContractError);
  assert.throws(() => actions.buttonDown("Space"), ContractError);
  assert.throws(() => actions.button(fire), ContractError);
});

test("context.actions fora da chamada do sistema é ContractError", () => {
  withEngine((engine) => {
    let captured: { readonly actions: unknown } | undefined;
    engine.addSystem(Phase.Update, "capture", (context) => {
      captured = context;
    });
    stepper(engine)();

    assert.ok(captured);
    assert.throws(() => captured!.actions, ContractError);
  });
});
