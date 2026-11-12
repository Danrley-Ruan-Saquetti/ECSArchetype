import assert from "node:assert/strict";
import { test } from "node:test";

import type { ButtonName } from "@oly/core";

import { attachBrowserInput } from "../dist/index.js";
import type { InputCanvas, InputEnvironment, InputTarget } from "../dist/index.js";

type Listener = (event: never) => void;

class FakeEventSource {
  readonly listeners = new Map<string, Listener[]>();

  addEventListener(type: string, listener: Listener): void {
    this.listeners.set(type, [...(this.listeners.get(type) ?? []), listener]);
  }

  removeEventListener(type: string, listener: Listener): void {
    this.listeners.set(type, (this.listeners.get(type) ?? []).filter((candidate) => candidate !== listener));
  }

  dispatch(type: string, event: unknown = {}): void {
    for (const listener of this.listeners.get(type) ?? []) {
      (listener as (event: unknown) => void)(event);
    }
  }

  count(): number {
    let total = 0;
    for (const listeners of this.listeners.values()) {
      total += listeners.length;
    }

    return total;
  }
}

class FakeCanvas extends FakeEventSource implements InputCanvas {
  width = 400;
  height = 200;
  rect = { left: 10, top: 20, width: 200, height: 100 };

  getBoundingClientRect(): { left: number; top: number; width: number; height: number } {
    return this.rect;
  }
}

class RecordingTarget implements InputTarget {
  readonly calls: string[] = [];
  readonly bound = new Set<ButtonName>();

  readonly actions = {
    buttonDown: (button: ButtonName): void => {
      this.calls.push(`down ${button}`);
    },
    buttonUp: (button: ButtonName): void => {
      this.calls.push(`up ${button}`);
    },
    pointerMove: (x: number, y: number): void => {
      this.calls.push(`move ${x} ${y}`);
    },
    releaseAll: (): void => {
      this.calls.push("releaseAll");
    },
    isBound: (button: ButtonName): boolean => this.bound.has(button),
  };
}

function setup(): { target: RecordingTarget; canvas: FakeCanvas; window: FakeEventSource; detach: () => void } {
  const target = new RecordingTarget();
  const canvas = new FakeCanvas();
  const window = new FakeEventSource();
  const environment: InputEnvironment = { window };
  const input = attachBrowserInput(target, canvas, environment);

  return { target, canvas, window, detach: () => input.detach() };
}

function keyEvent(code: string): { code: string; prevented: boolean; preventDefault(): void } {
  const event = {
    code,
    prevented: false,
    preventDefault(): void {
      event.prevented = true;
    },
  };

  return event;
}

function pointerEvent(buttons: number, clientX = 0, clientY = 0): {
  buttons: number;
  clientX: number;
  clientY: number;
  prevented: boolean;
  preventDefault(): void;
} {
  const event = {
    buttons,
    clientX,
    clientY,
    prevented: false,
    preventDefault(): void {
      event.prevented = true;
    },
  };

  return event;
}

test("keydown/keyup become buttonDown/buttonUp by W3C code", () => {
  const { target, window } = setup();

  window.dispatch("keydown", keyEvent("KeyW"));
  window.dispatch("keyup", keyEvent("KeyW"));

  assert.deepEqual(target.calls, ["down KeyW", "up KeyW"]);
});

test("code outside the table is ignored", () => {
  const { target, window } = setup();

  window.dispatch("keydown", keyEvent("Unidentified"));
  window.dispatch("keyup", keyEvent("Unidentified"));

  assert.deepEqual(target.calls, []);
});

test("preventDefault only for a bound button", () => {
  const { target, window } = setup();
  target.bound.add("Space");
  const space = keyEvent("Space");
  const reload = keyEvent("F5");

  window.dispatch("keydown", space);
  window.dispatch("keydown", reload);

  assert.equal(space.prevented, true);
  assert.equal(reload.prevented, false);
});

test("blur on the window calls releaseAll", () => {
  const { target, window } = setup();

  window.dispatch("blur");

  assert.deepEqual(target.calls, ["releaseAll"]);
});

test("pointerdown on the canvas sends pointerMove then buttonDown", () => {
  const { target, canvas } = setup();

  canvas.dispatch("pointerdown", pointerEvent(1, 60, 70));

  assert.deepEqual(target.calls, ["move 100 100", "down MouseLeft"]);
});

test("pointerup and pointermove come from the window", () => {
  const { target, canvas, window } = setup();

  canvas.dispatch("pointerdown", pointerEvent(1, 60, 70));
  window.dispatch("pointermove", pointerEvent(1, 110, 70));
  window.dispatch("pointerup", pointerEvent(0, 500, 500));

  assert.deepEqual(target.calls, ["move 100 100", "down MouseLeft", "move 200 100", "up MouseLeft"]);
  assert.equal(canvas.count(), 2);
});

test("buttons bit 4 is MouseMiddle, 2 is MouseRight and bit 8 is ignored", () => {
  const { target, canvas, window } = setup();

  canvas.dispatch("pointerdown", pointerEvent(4, 10, 20));
  window.dispatch("pointerup", pointerEvent(0, 10, 20));
  canvas.dispatch("pointerdown", pointerEvent(2, 10, 20));
  window.dispatch("pointerup", pointerEvent(0, 10, 20));
  canvas.dispatch("pointerdown", pointerEvent(8, 10, 20));

  assert.deepEqual(target.calls, [
    "move 0 0",
    "down MouseMiddle",
    "up MouseMiddle",
    "move 0 0",
    "down MouseRight",
    "up MouseRight",
    "move 0 0",
  ]);
});

test("second button pressed and released in a chord arrives through pointermove", () => {
  const { target, canvas, window } = setup();

  canvas.dispatch("pointerdown", pointerEvent(1, 60, 70));
  window.dispatch("pointermove", pointerEvent(3, 60, 70));
  window.dispatch("pointermove", pointerEvent(2, 60, 70));
  window.dispatch("pointerup", pointerEvent(1, 60, 70));

  assert.deepEqual(target.calls, [
    "move 100 100",
    "down MouseLeft",
    "move 100 100",
    "down MouseRight",
    "move 100 100",
    "up MouseLeft",
    "up MouseRight",
  ]);
});

test("dragging from outside the canvas does not press a button", () => {
  const { target, window } = setup();

  window.dispatch("pointermove", pointerEvent(1, 500, 500));

  assert.deepEqual(target.calls, ["move 980 960"]);
});

test("pointercancel releases the pressed mouse buttons", () => {
  const { target, canvas, window } = setup();

  canvas.dispatch("pointerdown", pointerEvent(1, 60, 70));
  window.dispatch("pointercancel", pointerEvent(1, 60, 70));

  assert.deepEqual(target.calls, ["move 100 100", "down MouseLeft", "up MouseLeft"]);
});

test("contextmenu on the canvas is prevented only with MouseRight bound", () => {
  const { target, canvas } = setup();
  const unbound = pointerEvent(2);
  const bound = pointerEvent(2);

  canvas.dispatch("contextmenu", unbound);
  target.bound.add("MouseRight");
  canvas.dispatch("contextmenu", bound);

  assert.equal(unbound.prevented, false);
  assert.equal(bound.prevented, true);
});

test("canvas with size 0 ignores pointer events", () => {
  const { target, canvas, window } = setup();
  canvas.rect = { left: 0, top: 0, width: 0, height: 100 };

  canvas.dispatch("pointerdown", pointerEvent(1, 5, 5));
  window.dispatch("pointermove", pointerEvent(1, 5, 5));

  assert.deepEqual(target.calls, []);
});

test("detach removes the listeners and is idempotent", () => {
  const { target, canvas, window, detach } = setup();

  detach();
  detach();
  window.dispatch("keydown", keyEvent("KeyW"));
  canvas.dispatch("pointerdown", pointerEvent(1, 60, 70));

  assert.deepEqual(target.calls, []);
  assert.equal(window.count() + canvas.count(), 0);
});
