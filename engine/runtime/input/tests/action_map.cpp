#include "oly/input/action_map.hpp"

#include <doctest/doctest.h>

namespace {
  using namespace oly::input;

  struct Fixture {
    ActionMap map;
    ButtonAction jump = map.declareButton("jump");

    ButtonState state(ButtonAction action) {
      return map.button(action);
    }
  };

  constexpr ButtonState pressedHeld{true, true, false};
  constexpr ButtonState heldOnly{false, true, false};
  constexpr ButtonState releasedOnly{false, false, true};
  constexpr ButtonState idle{false, false, false};
}

TEST_CASE("pressed on the edge step, held while pressed, released on release") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.capture();
  CHECK(f.state(f.jump) == pressedHeld);

  f.map.capture();
  CHECK(f.state(f.jump) == heldOnly);

  f.map.buttonUp(Key::Space);
  f.map.capture();
  CHECK(f.state(f.jump) == releasedOnly);

  f.map.capture();
  CHECK(f.state(f.jump) == idle);
}

TEST_CASE("a tap shorter than a step is not lost") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.buttonUp(Key::Space);
  f.map.capture();
  CHECK(f.state(f.jump) == ButtonState{true, false, true});

  f.map.capture();
  CHECK(f.state(f.jump) == idle);
}

TEST_CASE("empty queue keeps the state") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.capture();
  f.map.capture();
  f.map.capture();

  CHECK(f.state(f.jump).held);
  CHECK_FALSE(f.state(f.jump).pressed);
}

TEST_CASE("auto-repeat is ignored") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.buttonDown(Key::Space);
  f.map.buttonDown(Key::Space);
  f.map.capture();
  CHECK(f.state(f.jump).pressed);

  f.map.buttonUp(Key::Space);
  f.map.capture();
  CHECK(f.state(f.jump).released);
  CHECK_FALSE(f.state(f.jump).held);
}

TEST_CASE("two keys on the same action: releasing one does not produce released") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);
  f.map.bind(f.jump, Key::ShiftLeft);

  f.map.buttonDown(Key::Space);
  f.map.buttonDown(Key::ShiftLeft);
  f.map.capture();
  CHECK(f.state(f.jump) == pressedHeld);

  f.map.buttonUp(Key::Space);
  f.map.capture();
  CHECK_FALSE(f.state(f.jump).released);
  CHECK(f.state(f.jump).held);

  f.map.buttonUp(Key::ShiftLeft);
  f.map.capture();
  CHECK(f.state(f.jump).released);
}

TEST_CASE("same button on two actions: both react") {
  Fixture f;
  const ButtonAction confirm = f.map.declareButton("confirm");
  f.map.bind(f.jump, Key::Space);
  f.map.bind(confirm, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.capture();

  CHECK(f.state(f.jump).pressed);
  CHECK(f.state(confirm).pressed);
}

TEST_CASE("repeated binding does not duplicate") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);
  f.map.bind(f.jump, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.buttonUp(Key::Space);
  f.map.capture();

  CHECK(f.state(f.jump) == ButtonState{true, false, true});
}

TEST_CASE("releaseAll releases everything and the late keyup is ignored") {
  Fixture f;
  f.map.bind(f.jump, Key::W);

  f.map.buttonDown(Key::W);
  f.map.capture();
  f.map.releaseAll();
  f.map.buttonUp(Key::W);
  f.map.capture();
  CHECK(f.state(f.jump).released);

  f.map.capture();
  CHECK_FALSE(f.state(f.jump).released);
  CHECK_FALSE(f.state(f.jump).held);
}

TEST_CASE("axis: WASD, cancellation and normalized diagonal") {
  ActionMap map;
  const Axis2dAction move = map.declareAxis2d("move");
  map.bind(move, Axis2dBinding{Key::W, Key::S, Key::A, Key::D});

  map.buttonDown(Key::W);
  map.capture();
  CHECK(map.axis2d(move).x == doctest::Approx(0.0f));
  CHECK(map.axis2d(move).y == doctest::Approx(-1.0f));

  map.buttonDown(Key::S);
  map.capture();
  CHECK(map.axis2d(move).y == doctest::Approx(0.0f));

  map.buttonUp(Key::S);
  map.buttonDown(Key::D);
  map.capture();
  CHECK(map.axis2d(move).x == doctest::Approx(0.70710677f));
  CHECK(map.axis2d(move).y == doctest::Approx(-0.70710677f));
}

TEST_CASE("axis with two bindings: each direction is OR") {
  ActionMap map;
  const Axis2dAction move = map.declareAxis2d("move");
  map.bind(move, Axis2dBinding{Key::W, Key::S, Key::A, Key::D});
  map.bind(move, Axis2dBinding{Key::ArrowUp, Key::ArrowDown, Key::ArrowLeft, Key::ArrowRight});

  map.buttonDown(Key::A);
  map.buttonDown(Key::ArrowRight);
  map.capture();
  CHECK(map.axis2d(move).x == doctest::Approx(0.0f));
  CHECK(map.axis2d(move).y == doctest::Approx(0.0f));

  map.buttonUp(Key::A);
  map.buttonUp(Key::ArrowRight);
  map.buttonDown(Key::W);
  map.buttonDown(Key::ArrowUp);
  map.capture();
  CHECK(map.axis2d(move).x == doctest::Approx(0.0f));
  CHECK(map.axis2d(move).y == doctest::Approx(-1.0f));
}

TEST_CASE("pointer: invalid until the first pointerMove, then the last position") {
  ActionMap map;
  const PointerAction aim = map.declarePointer("aim");

  map.capture();
  CHECK_FALSE(map.pointer(aim).valid);

  map.pointerMove(1.0f, 2.0f);
  map.pointerMove(3.0f, 4.0f);
  map.capture();
  CHECK(map.pointer(aim).x == 3.0f);
  CHECK(map.pointer(aim).y == 4.0f);
  CHECK(map.pointer(aim).valid);
}

TEST_CASE("rebinding with the button pressed produces no edges") {
  Fixture f;

  f.map.buttonDown(Key::Space);
  f.map.capture();

  f.map.bind(f.jump, Key::Space);
  f.map.capture();
  CHECK(f.state(f.jump) == heldOnly);

  f.map.clearBindings(f.jump.id);
  f.map.capture();
  CHECK(f.state(f.jump) == idle);
}

TEST_CASE("rebinding only takes effect from the next capture") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);
  f.map.buttonDown(Key::Space);

  CHECK_FALSE(f.state(f.jump).pressed);
}

TEST_CASE("isBound works for button and axis") {
  ActionMap map;
  const ButtonAction jump = map.declareButton("jump");
  const Axis2dAction move = map.declareAxis2d("move");
  map.bind(jump, Key::Space);
  map.bind(move, Axis2dBinding{Key::W, Key::S, Key::A, Key::D});

  CHECK(map.isBound(Key::Space));
  CHECK(map.isBound(Key::D));
  CHECK_FALSE(map.isBound(Key::Q));

  map.clearBindings(jump.id);
  CHECK_FALSE(map.isBound(Key::Space));
}

TEST_CASE("snapshot: one ActionState per action in declaration order with the bits") {
  ActionMap map;
  const ButtonAction jump = map.declareButton("jump");
  const Axis2dAction move = map.declareAxis2d("move");
  const PointerAction aim = map.declarePointer("aim");
  map.bind(jump, Key::Space);
  map.bind(move, Axis2dBinding{Key::W, Key::S, Key::A, Key::D});

  CHECK(map.actionCount() == 3);
  CHECK(map.kind(aim.id) == ActionKind::Pointer);
  CHECK(map.name(move.id) == "move");

  map.buttonDown(Key::Space);
  map.buttonDown(Key::D);
  map.pointerMove(5.0f, 6.0f);
  map.capture();

  const auto snapshot = map.snapshot();
  REQUIRE(snapshot.size() == 3);
  CHECK(snapshot[0].flags == (ActionPressed | ActionHeld));
  CHECK(snapshot[1].x == 1.0f);
  CHECK(snapshot[1].flags == 0u);
  CHECK(snapshot[2].flags == ActionValid);
  CHECK(snapshot[2].x == 5.0f);
  CHECK(snapshot[2].y == 6.0f);
}
