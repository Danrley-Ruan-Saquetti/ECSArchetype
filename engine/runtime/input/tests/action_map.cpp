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

TEST_CASE("pressed no passo da borda, held enquanto pressionado, released ao soltar") {
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

TEST_CASE("toque mais curto que um passo não se perde") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.buttonUp(Key::Space);
  f.map.capture();
  CHECK(f.state(f.jump) == ButtonState{true, false, true});

  f.map.capture();
  CHECK(f.state(f.jump) == idle);
}

TEST_CASE("fila vazia mantém o estado") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.capture();
  f.map.capture();
  f.map.capture();

  CHECK(f.state(f.jump).held);
  CHECK_FALSE(f.state(f.jump).pressed);
}

TEST_CASE("auto-repeat é ignorado") {
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

TEST_CASE("duas teclas na mesma ação: soltar uma não gera released") {
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

TEST_CASE("mesmo botão em duas ações: as duas reagem") {
  Fixture f;
  const ButtonAction confirm = f.map.declareButton("confirm");
  f.map.bind(f.jump, Key::Space);
  f.map.bind(confirm, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.capture();

  CHECK(f.state(f.jump).pressed);
  CHECK(f.state(confirm).pressed);
}

TEST_CASE("ligação repetida não duplica") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);
  f.map.bind(f.jump, Key::Space);

  f.map.buttonDown(Key::Space);
  f.map.buttonUp(Key::Space);
  f.map.capture();

  CHECK(f.state(f.jump) == ButtonState{true, false, true});
}

TEST_CASE("releaseAll solta tudo e o keyup atrasado é ignorado") {
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

TEST_CASE("eixo: WASD, anulação e diagonal normalizada") {
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

TEST_CASE("eixo com duas ligações: cada direção é OU") {
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

TEST_CASE("ponteiro: inválido até o primeiro pointerMove, depois a última posição") {
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

TEST_CASE("religar com o botão pressionado não gera bordas") {
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

TEST_CASE("religação vale só a partir da próxima captura") {
  Fixture f;
  f.map.bind(f.jump, Key::Space);
  f.map.buttonDown(Key::Space);

  CHECK_FALSE(f.state(f.jump).pressed);
}

TEST_CASE("isBound vale para botão e eixo") {
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

TEST_CASE("snapshot: um ActionState por ação na ordem de declaração com os bits") {
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
