#pragma once

#include <cstdint>

namespace oly::input {
  // Usages HID: página (16 bits altos) e usage (16 bits baixos). Teclado é a página 0x07.
  enum class Key : uint32_t {
    A = 0x0007'0004,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Digit0,
    Enter,
    Escape,
    Backspace,
    Tab,
    Space,
    Minus,
    Equal,
    BracketLeft,
    BracketRight,
    Backslash,
    Semicolon = 0x0007'0033,
    Quote,
    Backquote,
    Comma,
    Period,
    Slash,
    CapsLock,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    PrintScreen,
    ScrollLock,
    Pause,
    Insert,
    Home,
    PageUp,
    Delete,
    End,
    PageDown,
    ArrowRight,
    ArrowLeft,
    ArrowDown,
    ArrowUp,
    NumLock,
    NumpadDivide,
    NumpadMultiply,
    NumpadSubtract,
    NumpadAdd,
    NumpadEnter,
    Numpad1,
    Numpad2,
    Numpad3,
    Numpad4,
    Numpad5,
    Numpad6,
    Numpad7,
    Numpad8,
    Numpad9,
    Numpad0,
    NumpadDecimal,
    IntlBackslash,
    ContextMenu,
    ControlLeft = 0x0007'00E0,
    ShiftLeft,
    AltLeft,
    MetaLeft,
    ControlRight,
    ShiftRight,
    AltRight,
    MetaRight,
  };

  // Botões do mouse são a página 0x09.
  enum class MouseButton : uint32_t {
    Left = 0x0009'0001,
    Right,
    Middle,
  };

  struct PhysicalButton {
    uint32_t usage;

    constexpr PhysicalButton(Key key) noexcept
      : usage(static_cast<uint32_t>(key)) {}
    constexpr PhysicalButton(MouseButton button) noexcept
      : usage(static_cast<uint32_t>(button)) {}
    constexpr explicit PhysicalButton(uint32_t rawUsage) noexcept
      : usage(rawUsage) {}

    constexpr bool operator==(const PhysicalButton&) const noexcept = default;
  };
}
