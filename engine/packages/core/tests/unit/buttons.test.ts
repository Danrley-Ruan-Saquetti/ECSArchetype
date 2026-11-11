import assert from "node:assert/strict";
import { test } from "node:test";

import { buttonUsages, isButtonName } from "@oly/core/internal";

test("amostras da tabela W3C para HID", () => {
  const samples: Readonly<Record<string, number>> = {
    KeyA: 0x0007_0004,
    KeyW: 0x0007_001a,
    KeyZ: 0x0007_001d,
    Digit1: 0x0007_001e,
    Digit0: 0x0007_0027,
    Enter: 0x0007_0028,
    Escape: 0x0007_0029,
    Space: 0x0007_002c,
    F1: 0x0007_003a,
    F12: 0x0007_0045,
    ArrowRight: 0x0007_004f,
    ArrowLeft: 0x0007_0050,
    ArrowDown: 0x0007_0051,
    ArrowUp: 0x0007_0052,
    ControlLeft: 0x0007_00e0,
    ShiftLeft: 0x0007_00e1,
    AltLeft: 0x0007_00e2,
    MetaLeft: 0x0007_00e3,
    ShiftRight: 0x0007_00e5,
    MouseLeft: 0x0009_0001,
    MouseRight: 0x0009_0002,
    MouseMiddle: 0x0009_0003,
  };

  for (const [name, usage] of Object.entries(samples)) {
    assert.equal(buttonUsages[name as keyof typeof buttonUsages], usage, name);
  }
});

test("isButtonName aceita só nomes W3C da tabela", () => {
  assert.equal(isButtonName("KeyW"), true);
  assert.equal(isButtonName("w"), false);
  assert.equal(isButtonName("Unidentified"), false);
  assert.equal(isButtonName("toString"), false);
});
