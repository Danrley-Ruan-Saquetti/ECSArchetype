# `input`: ações

`oly::input::ActionMap` (`engine/runtime/input/`) não conhece ECS nem `Engine`. Assim as regras de borda são testáveis sem loop nem `World`.

- **Ações**: botão, eixo 2D e ponteiro, com `ActionId` denso na ordem de declaração e handles tipados (`ButtonAction`, `Axis2dAction`, `PointerAction`): ler um eixo como botão não compila.
- **Botões físicos**: `PhysicalButton` é um usage do USB HID, `(página << 16) | usage`. Teclado = página `0x07`, mouse = página `0x09`. A identidade é a posição física da tecla (o `KeyboardEvent.code` do W3C), então WASD fica no mesmo lugar em qualquer layout. Os valores são os mesmos dos scancodes do SDL.
- **Entrada do host**: `buttonDown`, `buttonUp`, `pointerMove` e `releaseAll`, a qualquer hora. As bordas entram numa fila. Auto-repeat e `keyup` que chega depois de um `releaseAll` são ignorados.
- **Captura**: uma vez por passo fixo, também pausado. As bordas viram `pressed`/`released` por um passo só. Um toque mais curto que um passo aparece como `pressed` e `released` juntos, então não se perde. O eixo normaliza a diagonal e tem `y` para baixo, como o canvas.
- **Ligações**: trocáveis a qualquer hora, inclusive de dentro de um sistema. A troca vale na próxima captura.
- **Snapshot**: `ActionState { float x; float y; uint32_t flags; }`, 12 B por ação. O endereço é estável na `OlyEngine`, então o TS lê as ações por views do heap, sem cruzar a fronteira ([bridge.md](bridge.md)).

O host não é uma interface de plataforma: o núcleo nunca pergunta nada ao host, só recebe os eventos que ele empurra. No navegador, quem empurra é o `attachBrowserInput` ([pacotes-ts.md](pacotes-ts.md#olyclient)).
