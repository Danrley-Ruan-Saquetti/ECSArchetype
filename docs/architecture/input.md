# `input`: actions

`oly::input::ActionMap` (`engine/runtime/input/`) knows neither the ECS nor the `Engine`. That way the edge rules are testable without a loop or a `World`.

- **Actions**: button, 2D axis and pointer, with a dense `ActionId` in declaration order and typed handles (`ButtonAction`, `Axis2dAction`, `PointerAction`): reading an axis as a button does not compile.
- **Physical buttons**: `PhysicalButton` is a USB HID usage, `(page << 16) | usage`. Keyboard = page `0x07`, mouse = page `0x09`. The identity is the physical position of the key (the W3C `KeyboardEvent.code`), so WASD stays in the same place on any layout. The values are the same as SDL scancodes.
- **Host input**: `buttonDown`, `buttonUp`, `pointerMove` and `releaseAll`, at any time. Edges go into a queue. Auto-repeat and a `keyup` that arrives after a `releaseAll` are ignored.
- **Capture**: once per fixed step, also while paused. Edges become `pressed`/`released` for a single step. A tap shorter than a step shows up as `pressed` and `released` together, so it is not lost. The axis normalizes the diagonal and has `y` pointing down, like the canvas.
- **Bindings**: can be changed at any time, including from inside a system. The change takes effect on the next capture.
- **Snapshot**: `ActionState { float x; float y; uint32_t flags; }`, 12 B per action. The address is stable in the `OlyEngine`, so TS reads the actions through heap views, without crossing the boundary ([bridge.md](bridge.md)).

The host is not a platform interface: the core never asks the host anything, it only receives the events the host pushes. In the browser, `attachBrowserInput` does the pushing ([ts-packages.md](ts-packages.md#olyweb-host)).
