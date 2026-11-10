export interface FrameTarget {
  frame(nowSeconds: number): void;
  setHidden(hidden: boolean): void;
}

export interface BrowserEnvironment {
  requestAnimationFrame(callback: (timestampMilliseconds: number) => void): number;
  cancelAnimationFrame(handle: number): void;
  readonly document: {
    readonly hidden: boolean;
    addEventListener(type: "visibilitychange", listener: () => void): void;
    removeEventListener(type: "visibilitychange", listener: () => void): void;
  };
}

export interface BrowserLoop {
  stop(): void;
}

// Os métodos do navegador exigem `this` = window; por isso as funções embrulhadas.
function globalEnvironment(): BrowserEnvironment {
  return {
    requestAnimationFrame: (callback) => globalThis.requestAnimationFrame(callback),
    cancelAnimationFrame: (handle) => globalThis.cancelAnimationFrame(handle),
    document: globalThis.document,
  };
}

/**
 * Chama `target.frame` a cada requestAnimationFrame (tempo em segundos) e `target.setHidden` a cada
 * visibilitychange, e já no início se o documento estiver oculto. `stop` cancela o rAF pendente e remove o
 * listener; chamá-lo de novo não faz nada.
 */
export function runInBrowser(
  target: FrameTarget,
  environment: BrowserEnvironment = globalEnvironment(),
): BrowserLoop {
  let pendingHandle = 0;
  let running = true;

  const onVisibilityChange = (): void => {
    target.setHidden(environment.document.hidden);
  };

  // O próximo rAF é pedido antes de `frame`: uma exceção em `frame` sobe para o
  // console do navegador sem interromper o laço.
  const onAnimationFrame = (timestampMilliseconds: number): void => {
    pendingHandle = environment.requestAnimationFrame(onAnimationFrame);
    target.frame(timestampMilliseconds / 1000);
  };

  environment.document.addEventListener("visibilitychange", onVisibilityChange);
  if (environment.document.hidden) {
    target.setHidden(true);
  }
  pendingHandle = environment.requestAnimationFrame(onAnimationFrame);

  return {
    stop(): void {
      if (!running) {
        return;
      }

      running = false;
      environment.cancelAnimationFrame(pendingHandle);
      environment.document.removeEventListener("visibilitychange", onVisibilityChange);
    },
  };
}
