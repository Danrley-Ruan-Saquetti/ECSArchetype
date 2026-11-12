import type { Bridge } from "./bridge.js";
import { buttonUsages, isButtonName, type ButtonName } from "./buttons.js";
import { ContractError } from "./errors.js";
import { actionIndexOf, type Runtime } from "./runtime.js";
import type { ActionKind, ActionType, Axis2dAction, ButtonAction, PointerAction } from "./schema.js";

export interface ButtonState {
  readonly pressed: boolean;
  readonly held: boolean;
  readonly released: boolean;
}

export interface Vector2 {
  readonly x: number;
  readonly y: number;
}

export interface PointerState {
  readonly x: number;
  readonly y: number;
  readonly valid: boolean;
}

export interface Axis2dBinding {
  readonly up: ButtonName;
  readonly down: ButtonName;
  readonly left: ButtonName;
  readonly right: ButtonName;
}

const ActionStateBytes = 12;
const FlagsOffset = 8;
const ActionPressed = 1;
const ActionHeld = 2;
const ActionReleased = 4;
const ActionValid = 8;

/**
 * Bindings and host input go to the C++ core, which keeps the state; reads come from the snapshot in the WASM heap,
 * without crossing the boundary. The same object serves the `Engine` and the contexts. Throws ContractError for a button outside the
 * W3C table, an action from another schema or of another kind, a non-finite pointer and a disposed `Engine`.
 */
export class ActionMap {
  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #engineHandle: number;
  readonly #snapshotAddress: number;
  readonly #assertUsable: (operation: string) => void;

  constructor(runtime: Runtime, bridge: Bridge, engineHandle: number, snapshotAddress: number, assertUsable: (operation: string) => void) {
    this.#runtime = runtime;
    this.#bridge = bridge;
    this.#engineHandle = engineHandle;
    this.#snapshotAddress = snapshotAddress;
    this.#assertUsable = assertUsable;
  }

  button(action: ButtonAction): ButtonState {
    const index = this.#indexOf("button", action, "button");
    const flags = this.#flagsOf(index);

    return {
      pressed: (flags & ActionPressed) !== 0,
      held: (flags & ActionHeld) !== 0,
      released: (flags & ActionReleased) !== 0,
    };
  }

  axis2d(action: Axis2dAction): Vector2 {
    const index = this.#indexOf("axis2d", action, "axis2d");
    const base = (this.#snapshotAddress + ActionStateBytes * index) >> 2;

    return { x: this.#bridge.heap.f32[base]!, y: this.#bridge.heap.f32[base + 1]! };
  }

  pointer(action: PointerAction): PointerState {
    const index = this.#indexOf("pointer", action, "pointer");
    const base = (this.#snapshotAddress + ActionStateBytes * index) >> 2;

    return {
      x: this.#bridge.heap.f32[base]!,
      y: this.#bridge.heap.f32[base + 1]!,
      valid: (this.#flagsOf(index) & ActionValid) !== 0,
    };
  }

  /** Binding the same button or axis to the action again does not duplicate it. Takes effect from the next capture. */
  bind(action: ButtonAction, button: ButtonName): void;
  bind(action: Axis2dAction, binding: Axis2dBinding): void;
  bind(action: ActionType, target: ButtonName | Axis2dBinding): void {
    if (typeof target === "string") {
      const index = this.#indexOf("bind", action, "button");
      this.#bridge.engineBindButton(this.#engineHandle, index, usageOf("bind", target));

      return;
    }

    const index = this.#indexOf("bind", action, "axis2d");
    this.#bridge.engineBindAxis2d(
      this.#engineHandle,
      index,
      usageOf("bind", target.up),
      usageOf("bind", target.down),
      usageOf("bind", target.left),
      usageOf("bind", target.right),
    );
  }

  clearBindings(action: ActionType): void {
    this.#assertUsable("clearBindings");
    const index = actionIndexOf(this.#runtime, action);

    this.#bridge.engineClearBindings(this.#engineHandle, index);
  }

  isBound(button: ButtonName): boolean {
    this.#assertUsable("isBound");

    return this.#bridge.engineIsBound(this.#engineHandle, usageOf("isBound", button));
  }

  buttonDown(button: ButtonName): void {
    this.#assertUsable("buttonDown");

    this.#bridge.engineButtonDown(this.#engineHandle, usageOf("buttonDown", button));
  }

  buttonUp(button: ButtonName): void {
    this.#assertUsable("buttonUp");

    this.#bridge.engineButtonUp(this.#engineHandle, usageOf("buttonUp", button));
  }

  pointerMove(x: number, y: number): void {
    this.#assertUsable("pointerMove");
    if (!Number.isFinite(x) || !Number.isFinite(y)) {
      throw new ContractError(`pointerMove x=${x} y=${y}: non-finite value`);
    }

    this.#bridge.enginePointerMove(this.#engineHandle, x, y);
  }

  releaseAll(): void {
    this.#assertUsable("releaseAll");

    this.#bridge.engineReleaseAll(this.#engineHandle);
  }

  #indexOf(operation: string, action: ActionType, expected: ActionKind): number {
    this.#assertUsable(operation);
    const index = actionIndexOf(this.#runtime, action);
    if (action.actionKind !== expected) {
      throw new ContractError(`${operation} action=${action.name}: action of kind ${action.actionKind}, expected ${expected}`);
    }

    return index;
  }

  #flagsOf(index: number): number {
    return this.#bridge.heap.u32[(this.#snapshotAddress + ActionStateBytes * index + FlagsOffset) >> 2]!;
  }
}

function usageOf(operation: string, button: ButtonName): number {
  if (!isButtonName(button)) {
    throw new ContractError(`${operation} button=${String(button)}: button outside the W3C table`);
  }

  return buttonUsages[button];
}
