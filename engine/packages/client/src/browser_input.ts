import { isButtonName } from "@oly/core";
import type { ActionMap, ButtonName } from "@oly/core";

export interface InputTarget {
  readonly actions: Pick<ActionMap, "buttonDown" | "buttonUp" | "pointerMove" | "releaseAll" | "isBound">;
}

export interface KeyInput {
  readonly code: string;
  preventDefault(): void;
}

export interface PointerInput {
  readonly buttons: number;
  readonly clientX: number;
  readonly clientY: number;
  preventDefault(): void;
}

export interface InputCanvas {
  readonly width: number;
  readonly height: number;
  getBoundingClientRect(): { left: number; top: number; width: number; height: number };
  addEventListener(type: "pointerdown" | "contextmenu", listener: (event: PointerInput) => void): void;
  removeEventListener(type: "pointerdown" | "contextmenu", listener: (event: PointerInput) => void): void;
}

export interface InputWindow {
  addEventListener(type: "keydown" | "keyup", listener: (event: KeyInput) => void): void;
  addEventListener(type: "blur", listener: () => void): void;
  addEventListener(type: "pointerup" | "pointermove" | "pointercancel", listener: (event: PointerInput) => void): void;
  removeEventListener(type: "keydown" | "keyup", listener: (event: KeyInput) => void): void;
  removeEventListener(type: "blur", listener: () => void): void;
  removeEventListener(type: "pointerup" | "pointermove" | "pointercancel", listener: (event: PointerInput) => void): void;
}

export interface InputEnvironment {
  readonly window: InputWindow;
}

export interface BrowserInput {
  detach(): void;
}

// Bits de PointerEvent.buttons. O botão intermediário de um acorde só chega por aqui: pointerdown dispara quando o
// primeiro botão é apertado e pointerup quando o último é solto.
const mouseButtonBits: readonly (readonly [number, ButtonName])[] = [
  [1, "MouseLeft"],
  [2, "MouseRight"],
  [4, "MouseMiddle"],
];

// Os métodos do navegador exigem `this` = window; por isso as funções embrulhadas.
function globalEnvironment(): InputEnvironment {
  const target = globalThis as unknown as InputWindow;

  return {
    window: {
      addEventListener: (type: string, listener: unknown) => {
        (target.addEventListener as (type: string, listener: unknown) => void)(type, listener);
      },
      removeEventListener: (type: string, listener: unknown) => {
        (target.removeEventListener as (type: string, listener: unknown) => void)(type, listener);
      },
    } as InputWindow,
  };
}

/**
 * Entrega teclado e mouse ao alvo: teclas pelo `code` W3C, botões do mouse pela máscara `buttons` (apertar vale só
 * no canvas, soltar em qualquer lugar da janela) e `releaseAll` no `blur`. A posição do ponteiro sai em pixels do canvas, não da tela. Eventos
 * de ponteiro com o canvas sem tamanho são ignorados. `detach` remove os listeners; chamá-lo de novo não faz nada.
 */
export function attachBrowserInput(
  target: InputTarget,
  canvas: InputCanvas,
  environment: InputEnvironment = globalEnvironment(),
): BrowserInput {
  const { actions } = target;
  const { window } = environment;
  let attached = true;

  const heldMouseButtons = new Set<ButtonName>();

  // Devolve null com o canvas sem tamanho (oculto): não há como converter sem dividir por zero.
  const moveToCanvasPosition = (event: PointerInput): { inside: boolean } | null => {
    const rect = canvas.getBoundingClientRect();
    if (rect.width === 0 || rect.height === 0) {
      return null;
    }

    const x = ((event.clientX - rect.left) * canvas.width) / rect.width;
    const y = ((event.clientY - rect.top) * canvas.height) / rect.height;
    actions.pointerMove(x, y);

    return { inside: x >= 0 && x < canvas.width && y >= 0 && y < canvas.height };
  };

  // Aperto só vale dentro do canvas; soltar vale em qualquer lugar da janela.
  const syncMouseButtons = (buttons: number, allowPress: boolean): void => {
    for (const [bit, button] of mouseButtonBits) {
      const down = (buttons & bit) !== 0;
      if (down && allowPress && !heldMouseButtons.has(button)) {
        heldMouseButtons.add(button);
        actions.buttonDown(button);
      } else if (!down && heldMouseButtons.delete(button)) {
        actions.buttonUp(button);
      }
    }
  };

  const onKeyDown = (event: KeyInput): void => {
    if (!isButtonName(event.code)) {
      return;
    }
    if (actions.isBound(event.code)) {
      event.preventDefault();
    }

    actions.buttonDown(event.code);
  };

  const onKeyUp = (event: KeyInput): void => {
    if (isButtonName(event.code)) {
      actions.buttonUp(event.code);
    }
  };

  const onBlur = (): void => {
    actions.releaseAll();
  };

  const onPointerDown = (event: PointerInput): void => {
    if (moveToCanvasPosition(event) !== null) {
      syncMouseButtons(event.buttons, true);
    }
  };

  const onPointerUp = (event: PointerInput): void => {
    syncMouseButtons(event.buttons, false);
  };

  const onPointerMove = (event: PointerInput): void => {
    const position = moveToCanvasPosition(event);
    syncMouseButtons(event.buttons, position?.inside === true);
  };

  const onPointerCancel = (): void => {
    syncMouseButtons(0, false);
  };

  // O menu do botão direito vem deste evento, não do pointerdown.
  const onContextMenu = (event: PointerInput): void => {
    if (actions.isBound("MouseRight")) {
      event.preventDefault();
    }
  };

  window.addEventListener("keydown", onKeyDown);
  window.addEventListener("keyup", onKeyUp);
  window.addEventListener("blur", onBlur);
  window.addEventListener("pointerup", onPointerUp);
  window.addEventListener("pointermove", onPointerMove);
  window.addEventListener("pointercancel", onPointerCancel);
  canvas.addEventListener("pointerdown", onPointerDown);
  canvas.addEventListener("contextmenu", onContextMenu);

  return {
    detach(): void {
      if (!attached) {
        return;
      }

      attached = false;
      window.removeEventListener("keydown", onKeyDown);
      window.removeEventListener("keyup", onKeyUp);
      window.removeEventListener("blur", onBlur);
      window.removeEventListener("pointerup", onPointerUp);
      window.removeEventListener("pointermove", onPointerMove);
      window.removeEventListener("pointercancel", onPointerCancel);
      canvas.removeEventListener("pointerdown", onPointerDown);
      canvas.removeEventListener("contextmenu", onContextMenu);
    },
  };
}
