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

// The browser methods require `this` = window; hence the wrapped functions.
function globalEnvironment(): BrowserEnvironment {
  return {
    requestAnimationFrame: (callback) => globalThis.requestAnimationFrame(callback),
    cancelAnimationFrame: (handle) => globalThis.cancelAnimationFrame(handle),
    document: globalThis.document,
  };
}

/**
 * Calls `target.frame` on every requestAnimationFrame (time in seconds) and `target.setHidden` on every
 * visibilitychange, and right at the start if the document is hidden. `stop` cancels the pending rAF and removes the
 * listener; calling it again does nothing.
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

  // The next rAF is requested before `frame`: an exception in `frame` goes up to the
  // browser console without breaking the loop.
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
