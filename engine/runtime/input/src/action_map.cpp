#include "oly/input/action_map.hpp"

#include <algorithm>
#include <cmath>

#include "oly/core/assert.hpp"

namespace oly::input {
  namespace {
    constexpr float diagonalScale = 0.70710678f;

    bool contains(const std::vector<PhysicalButton>& buttons, PhysicalButton button) {
      return std::find(buttons.begin(), buttons.end(), button) != buttons.end();
    }

    void erase(std::vector<PhysicalButton>& buttons, PhysicalButton button) {
      buttons.erase(std::remove(buttons.begin(), buttons.end(), button), buttons.end());
    }

    bool axisUses(const Axis2dBinding& binding, PhysicalButton button) {
      return binding.up == button || binding.down == button || binding.left == button || binding.right == button;
    }

    float axisValue(bool positive, bool negative) {
      return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
    }
  }

  ActionId ActionMap::declare(ActionKind kind, std::string_view name) {
    OLY_ASSERT(!declarationsLocked_, "declare: ação '%.*s' declarada durante frame", static_cast<int>(name.size()),
               name.data());

    for ([[maybe_unused]] const Declaration& declaration : declarations_) {
      OLY_ASSERT(declaration.name != name, "declare: ação '%.*s' já declarada", static_cast<int>(name.size()),
                 name.data());
    }

    declarations_.push_back(Declaration{kind, std::string(name), {}, {}});
    snapshot_.push_back(ActionState{0.0f, 0.0f, 0u});

    return static_cast<ActionId>(declarations_.size() - 1);
  }

  ButtonAction ActionMap::declareButton(std::string_view name) {
    return ButtonAction{declare(ActionKind::Button, name)};
  }

  Axis2dAction ActionMap::declareAxis2d(std::string_view name) {
    return Axis2dAction{declare(ActionKind::Axis2d, name)};
  }

  PointerAction ActionMap::declarePointer(std::string_view name) {
    return PointerAction{declare(ActionKind::Pointer, name)};
  }

  void ActionMap::bind(ButtonAction action, PhysicalButton button) {
    checkKind("bind", action.id, ActionKind::Button);
    OLY_ASSERT(button.usage != 0, "bind: usage 0 inválido (ação '%s')", declarations_[action.id].name.c_str());

    std::vector<PhysicalButton>& buttons = declarations_[action.id].buttons;
    if (!contains(buttons, button)) {
      buttons.push_back(button);
    }
  }

  void ActionMap::bind(Axis2dAction action, const Axis2dBinding& binding) {
    checkKind("bind", action.id, ActionKind::Axis2d);
    OLY_ASSERT(binding.up.usage != 0 && binding.down.usage != 0 && binding.left.usage != 0 && binding.right.usage != 0,
               "bind: usage 0 inválido (ação '%s')", declarations_[action.id].name.c_str());

    std::vector<Axis2dBinding>& axes = declarations_[action.id].axes;
    if (std::find(axes.begin(), axes.end(), binding) == axes.end()) {
      axes.push_back(binding);
    }
  }

  void ActionMap::clearBindings(ActionId action) {
    checkAction("clearBindings", action);

    declarations_[action].buttons.clear();
    declarations_[action].axes.clear();
  }

  bool ActionMap::isBound(PhysicalButton button) const {
    for (const Declaration& declaration : declarations_) {
      if (contains(declaration.buttons, button)) {
        return true;
      }

      for (const Axis2dBinding& binding : declaration.axes) {
        if (axisUses(binding, button)) {
          return true;
        }
      }
    }

    return false;
  }

  void ActionMap::buttonDown(PhysicalButton button) {
    OLY_ASSERT(button.usage != 0, "buttonDown: usage 0 inválido");

    if (contains(hostPressed_, button)) {
      return;
    }

    hostPressed_.push_back(button);
    edges_.push_back(Edge{button, true});
  }

  void ActionMap::buttonUp(PhysicalButton button) {
    OLY_ASSERT(button.usage != 0, "buttonUp: usage 0 inválido");

    if (!contains(hostPressed_, button)) {
      return;
    }

    erase(hostPressed_, button);
    edges_.push_back(Edge{button, false});
  }

  void ActionMap::pointerMove(float x, float y) {
    OLY_ASSERT(std::isfinite(x) && std::isfinite(y), "pointerMove: valor não finito (x=%f, y=%f)", x, y);

    pointerX_ = x;
    pointerY_ = y;
    pointerValid_ = true;
  }

  void ActionMap::releaseAll() {
    for (const PhysicalButton button : hostPressed_) {
      edges_.push_back(Edge{button, false});
    }

    hostPressed_.clear();
  }

  void ActionMap::capture() {
    for (std::size_t index = 0; index < declarations_.size(); ++index) {
      const Declaration& declaration = declarations_[index];
      ActionState& state = snapshot_[index];

      if (declaration.kind == ActionKind::Button) {
        state.flags = anyPressed(declaration.buttons) ? ActionHeld : 0u;
      }
    }

    for (const Edge& edge : edges_) {
      if (edge.down) {
        capturedPressed_.push_back(edge.button);
      } else {
        erase(capturedPressed_, edge.button);
      }

      for (std::size_t index = 0; index < declarations_.size(); ++index) {
        const Declaration& declaration = declarations_[index];

        if (declaration.kind != ActionKind::Button || !contains(declaration.buttons, edge.button)) {
          continue;
        }

        ActionState& state = snapshot_[index];
        const bool held = (state.flags & ActionHeld) != 0;
        const bool now = anyPressed(declaration.buttons);

        if (!held && now) {
          state.flags |= ActionPressed;
        }

        if (held && !now) {
          state.flags |= ActionReleased;
        }

        state.flags = now ? (state.flags | ActionHeld) : (state.flags & ~ActionHeld);
      }
    }

    edges_.clear();

    for (std::size_t index = 0; index < declarations_.size(); ++index) {
      const Declaration& declaration = declarations_[index];
      ActionState& state = snapshot_[index];

      if (declaration.kind == ActionKind::Axis2d) {
        bool up = false;
        bool down = false;
        bool left = false;
        bool right = false;

        for (const Axis2dBinding& binding : declaration.axes) {
          up = up || contains(capturedPressed_, binding.up);
          down = down || contains(capturedPressed_, binding.down);
          left = left || contains(capturedPressed_, binding.left);
          right = right || contains(capturedPressed_, binding.right);
        }

        float x = axisValue(right, left);
        float y = axisValue(down, up);

        if (x != 0.0f && y != 0.0f) {
          x *= diagonalScale;
          y *= diagonalScale;
        }

        state = ActionState{x, y, 0u};
      } else if (declaration.kind == ActionKind::Pointer) {
        state = ActionState{pointerX_, pointerY_, pointerValid_ ? ActionValid : 0u};
      }
    }
  }

  ButtonState ActionMap::button(ButtonAction action) const {
    checkKind("button", action.id, ActionKind::Button);

    const uint32_t flags = snapshot_[action.id].flags;
    return ButtonState{(flags & ActionPressed) != 0, (flags & ActionHeld) != 0, (flags & ActionReleased) != 0};
  }

  Vector2 ActionMap::axis2d(Axis2dAction action) const {
    checkKind("axis2d", action.id, ActionKind::Axis2d);

    return Vector2{snapshot_[action.id].x, snapshot_[action.id].y};
  }

  PointerState ActionMap::pointer(PointerAction action) const {
    checkKind("pointer", action.id, ActionKind::Pointer);

    const ActionState& state = snapshot_[action.id];
    return PointerState{state.x, state.y, (state.flags & ActionValid) != 0};
  }

  std::span<const ActionState> ActionMap::snapshot() const noexcept {
    return snapshot_;
  }

  uint32_t ActionMap::actionCount() const noexcept {
    return static_cast<uint32_t>(declarations_.size());
  }

  ActionKind ActionMap::kind(ActionId action) const {
    checkAction("kind", action);

    return declarations_[action].kind;
  }

  std::string_view ActionMap::name(ActionId action) const {
    checkAction("name", action);

    return declarations_[action].name;
  }

  void ActionMap::setDeclarationsLocked(bool locked) noexcept {
    declarationsLocked_ = locked;
  }

  void ActionMap::checkAction([[maybe_unused]] const char* operation, [[maybe_unused]] ActionId action) const {
    OLY_ASSERT(action < declarations_.size(), "%s: ação %u fora do intervalo (%zu declaradas)", operation, action,
               declarations_.size());
  }

  void ActionMap::checkKind(const char* operation, ActionId action, [[maybe_unused]] ActionKind expected) const {
    checkAction(operation, action);
    OLY_ASSERT(declarations_[action].kind == expected, "%s: ação '%s' tem tipo %u, esperado %u", operation,
               declarations_[action].name.c_str(), static_cast<unsigned>(declarations_[action].kind),
               static_cast<unsigned>(expected));
  }

  bool ActionMap::anyPressed(const std::vector<PhysicalButton>& buttons) const {
    for (const PhysicalButton button : buttons) {
      if (contains(capturedPressed_, button)) {
        return true;
      }
    }

    return false;
  }
}
