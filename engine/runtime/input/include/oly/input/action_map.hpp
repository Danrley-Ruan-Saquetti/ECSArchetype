#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "oly/input/physical_button.hpp"

namespace oly::input {
  using ActionId = uint32_t;

  enum class ActionKind : uint8_t { Button,
                                    Axis2d,
                                    Pointer };

  struct ButtonAction {
    ActionId id;
  };

  struct Axis2dAction {
    ActionId id;
  };

  struct PointerAction {
    ActionId id;
  };

  struct ButtonState {
    bool pressed;
    bool held;
    bool released;

    constexpr bool operator==(const ButtonState&) const noexcept = default;
  };

  struct Vector2 {
    float x;
    float y;
  };

  struct PointerState {
    float x;
    float y;
    bool valid;
  };

  struct Axis2dBinding {
    PhysicalButton up;
    PhysicalButton down;
    PhysicalButton left;
    PhysicalButton right;

    constexpr bool operator==(const Axis2dBinding&) const noexcept = default;
  };

  constexpr uint32_t ActionPressed = 1u << 0;
  constexpr uint32_t ActionHeld = 1u << 1;
  constexpr uint32_t ActionReleased = 1u << 2;
  constexpr uint32_t ActionValid = 1u << 3;

  // Layout read by TS straight from the WASM heap: 12 B per action, without crossing the boundary.
  struct ActionState {
    float x;
    float y;
    uint32_t flags;
  };

  static_assert(sizeof(ActionState) == 12);
  static_assert(offsetof(ActionState, x) == 0);
  static_assert(offsetof(ActionState, y) == 4);
  static_assert(offsetof(ActionState, flags) == 8);

  class ActionMap {
   private:
    struct Declaration {
      ActionKind kind;
      std::string name;
      std::vector<PhysicalButton> buttons;
      std::vector<Axis2dBinding> axes;
    };

    struct Edge {
      PhysicalButton button;
      bool down;
    };

    std::vector<Declaration> declarations_;
    std::vector<ActionState> snapshot_;
    std::vector<Edge> edges_;
    // Host state: changes at call time and decides what enters the queue.
    std::vector<PhysicalButton> hostPressed_;
    // State replayed by the capture; at the end of every capture it equals the host state.
    std::vector<PhysicalButton> capturedPressed_;
    float pointerX_ = 0.0f;
    float pointerY_ = 0.0f;
    bool pointerValid_ = false;
    bool declarationsLocked_ = false;

   public:
    ActionId declare(ActionKind kind, std::string_view name);
    ButtonAction declareButton(std::string_view name);
    Axis2dAction declareAxis2d(std::string_view name);
    PointerAction declarePointer(std::string_view name);

    void bind(ButtonAction action, PhysicalButton button);
    void bind(Axis2dAction action, const Axis2dBinding& binding);
    void clearBindings(ActionId action);
    bool isBound(PhysicalButton button) const;

    void buttonDown(PhysicalButton button);
    void buttonUp(PhysicalButton button);
    void pointerMove(float x, float y);
    void releaseAll();

    void capture();

    ButtonState button(ButtonAction action) const;
    Vector2 axis2d(Axis2dAction action) const;
    PointerState pointer(PointerAction action) const;
    std::span<const ActionState> snapshot() const noexcept;

    uint32_t actionCount() const noexcept;
    ActionKind kind(ActionId action) const;
    std::string_view name(ActionId action) const;
    void setDeclarationsLocked(bool locked) noexcept;

   private:
    void checkAction(const char* operation, ActionId action) const;
    void checkKind(const char* operation, ActionId action, ActionKind expected) const;
    bool anyPressed(const std::vector<PhysicalButton>& buttons) const;
  };
}
