# 7. Input

[← Events](06-events.md) · [Guide index](README.md) · Next: [Schedule →](08-schedule.md)

Gameplay never reads keys directly. It reads **actions** ("shoot", "move", "aim"). You **bind** physical buttons to actions, and you can change the bindings at any time. This gives you key rebinding, several keys per action and fake input in tests for free.

## Three kinds of actions

| Kind | Value | Typical use |
|---|---|---|
| `"button"` | `{ pressed, held, released }` | shoot, jump, pause, confirm |
| `"axis2d"` | `{ x, y }`, each in -1..1 | movement with WASD or arrows |
| `"pointer"` | `{ x, y, valid }` in canvas pixels | aim, click targets |

```ts
// schema.ts
export const move = action("move", "axis2d");
export const aim = action("aim", "pointer");
export const shoot = action("shoot", "button");
export const jump = action("jump", "button");
export const pause = action("pause", "button");
```

## Bind buttons

Buttons use the W3C `KeyboardEvent.code` names. A code is the **physical position** of the key, so WASD stays under your fingers on AZERTY, Dvorak or any layout.

```ts
engine.actions.bind(shoot, "MouseLeft");
engine.actions.bind(jump, "Space");
engine.actions.bind(jump, "KeyW");              // a second button for the same action
engine.actions.bind(pause, "Escape");
engine.actions.bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });
engine.actions.bind(move, { up: "ArrowUp", down: "ArrowDown", left: "ArrowLeft", right: "ArrowRight" });
```

Binding the same button again does nothing. Pointer actions need no binding: they follow the mouse.

Available names: `KeyA`..`KeyZ`, `Digit0`..`Digit9`, `F1`..`F12`, `Enter`, `Escape`, `Space`, `Tab`, `Backspace`, arrows (`ArrowUp`...), modifiers (`ShiftLeft`, `ControlLeft`, `AltLeft`, `MetaLeft` and the `Right` versions), punctuation (`Minus`, `Comma`, `Slash`...), the numpad (`Numpad0`..`Numpad9`, `NumpadAdd`...) and the mouse (`MouseLeft`, `MouseRight`, `MouseMiddle`). The full list is the `ButtonName` type; `isButtonName(text)` checks a string at run time.

## Read actions in systems

```ts
engine.addSystem(Phase.UpdateBefore, "readInput", (context) => {
  const jumpButton = context.actions.button(jump);
  const direction = context.actions.axis2d(move);
  const pointer = context.actions.pointer(aim);

  if (jumpButton.pressed) {
    // first step of the press
  }
  if (jumpButton.held) {
    // every step while it is down
  }
  if (jumpButton.released) {
    // the step it went up
  }
  if (pointer.valid) {
    // a pointer position has arrived at least once
  }
});
```

| Flag | True in |
|---|---|
| `pressed` | the one step where the action went down |
| `held` | every step while any bound button is down |
| `released` | the one step where the action went up |

Input is captured **once per fixed step**, at the start of the step. All systems in that step see the same values.

A tap shorter than one step still counts: that step shows `pressed` **and** `released`. It may not show `held`, so use `pressed` for one-shot actions (jump, confirm, pause) and `held` for continuous ones (shoot while holding, run).

### The 2D axis

The axis gives `-1`, `0` or `1` on each side, with `y` pointing **down** like the canvas. The diagonal is normalized, so moving diagonally is not faster:

| Keys down | Value |
|---|---|
| D | `{ x: 1, y: 0 }` |
| W | `{ x: 0, y: -1 }` |
| W + D | `{ x: 0.707, y: -0.707 }` |
| A + D | `{ x: 0, y: 0 }` |

```ts
const direction = context.actions.axis2d(move);
velocity.x.set(row, direction.x * speed);
velocity.y.set(row, direction.y * speed);
```

## Connect the browser

`attachBrowserInput` sends keyboard and mouse events to the engine:

```ts
import { attachBrowserInput } from "@oly/client";

const input = attachBrowserInput(engine, canvas);
// input.detach() removes all listeners
```

What it does:

- keys from `window`, by `code`;
- mouse presses only **on the canvas**, releases anywhere in the window;
- pointer position converted to **canvas pixels** (it handles CSS scaling);
- on `blur` (the window loses focus), releases every button so nothing stays stuck;
- calls `preventDefault` only for buttons bound to an action: Space does not scroll the page and Tab does not leave the game, but unbound keys keep their browser behaviour.

The right-click menu is prevented only when `MouseRight` is bound. If you do not bind it but still want no menu:

```ts
canvas.addEventListener("contextmenu", (event) => event.preventDefault());
```

## Rebind at run time

Bindings can change at any time, even inside a system. The change applies from the next capture.

```ts
function rebind(engine: Engine, action: ButtonAction, button: ButtonName): void {
  engine.actions.clearBindings(action);
  engine.actions.bind(action, button);
}

rebind(engine, jump, "KeyK");
```

A "press a key to rebind" screen:

```ts
window.addEventListener("keydown", function capture(event) {
  if (!isButtonName(event.code)) {
    return;
  }

  rebind(engine, jump, event.code);
  window.removeEventListener("keydown", capture);
});
```

Use `engine.actions.isBound(button)` to warn when a key is already in use.

## Deliver input yourself

The engine does not care where input comes from. Call these on `engine.actions` from a test, a replay, a touch overlay or a gamepad poller:

```ts
engine.actions.buttonDown("Space");
engine.actions.buttonUp("Space");
engine.actions.pointerMove(320, 180);
engine.actions.releaseAll();
```

A virtual joystick for touch screens can map to the same `move` action by pressing and releasing the four bound keys, or you can bind a dedicated set of keys and press those.

Key auto-repeat is ignored: holding a key gives one `pressed`.

## C++

Native systems read the same actions through the generated handles:

```cpp
OLY_SYSTEM(fireWeapon) {
  if (!context.actions().button(actions::shoot).held) {
    return;
  }

  const oly::input::PointerState aim = context.actions().pointer(actions::aim);
  if (aim.valid) {
    // shoot toward (aim.x, aim.y)
  }
}
```

In a C++-only game you declare and bind actions on the engine's `ActionMap`:

```cpp
using oly::input::Key;
using oly::input::MouseButton;

oly::input::ButtonAction jump = engine.actions().declareButton("jump");
oly::input::Axis2dAction move = engine.actions().declareAxis2d("move");
engine.actions().bind(jump, Key::Space);
engine.actions().bind(move, oly::input::Axis2dBinding{Key::W, Key::S, Key::A, Key::D});
engine.actions().bind(jump, MouseButton::Left);
```

## Next

[8. Schedule →](08-schedule.md): run code later, on the game clock.
