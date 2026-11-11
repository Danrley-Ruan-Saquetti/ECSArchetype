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
  readonly button: number;
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
  addEventListener(type: "pointerup" | "pointermove", listener: (event: PointerInput) => void): void;
  removeEventListener(type: "keydown" | "keyup", listener: (event: KeyInput) => void): void;
  removeEventListener(type: "blur", listener: () => void): void;
  removeEventListener(type: "pointerup" | "pointermove", listener: (event: PointerInput) => void): void;
}

export interface InputEnvironment {
  readonly window: InputWindow;
}

export interface BrowserInput {
  detach(): void;
}

const mouseButtons: readonly (ButtonName | undefined)[] = ["MouseLeft", "MouseMiddle", "MouseRight"];

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
 * Entrega teclado e mouse ao alvo: teclas pelo `code` W3C, botões do mouse pelo canvas (soltar vale em qualquer
 * lugar da janela) e `releaseAll` no `blur`. A posição do ponteiro sai em pixels do canvas, não da tela. Eventos
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

  // Nada de pointerMove com NaN no snapshot: canvas oculto tem retângulo de largura ou altura 0.
  const moveToCanvasPosition = (event: PointerInput): boolean => {
    const rect = canvas.getBoundingClientRect();
    if (rect.width === 0 || rect.height === 0) {
      return false;
    }

    actions.pointerMove(((event.clientX - rect.left) * canvas.width) / rect.width, ((event.clientY - rect.top) * canvas.height) / rect.height);

    return true;
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
    const button = mouseButtons[event.button];
    if (button === undefined || !moveToCanvasPosition(event)) {
      return;
    }

    actions.buttonDown(button);
  };

  const onPointerUp = (event: PointerInput): void => {
    const button = mouseButtons[event.button];
    if (button !== undefined) {
      actions.buttonUp(button);
    }
  };

  const onPointerMove = (event: PointerInput): void => {
    moveToCanvasPosition(event);
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
      canvas.removeEventListener("pointerdown", onPointerDown);
      canvas.removeEventListener("contextmenu", onContextMenu);
    },
  };
}
