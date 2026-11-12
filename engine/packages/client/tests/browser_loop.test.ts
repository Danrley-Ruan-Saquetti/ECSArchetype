import assert from "node:assert/strict";
import { test } from "node:test";

import { runInBrowser } from "../dist/index.js";
import type { BrowserEnvironment, FrameTarget } from "../dist/index.js";

class FakeBrowser implements BrowserEnvironment {
  readonly callbacks = new Map<number, (timestampMilliseconds: number) => void>();
  readonly cancelled: number[] = [];
  listener: (() => void) | null = null;
  removedListeners = 0;
  hidden = false;
  nextHandle = 1;

  get document(): BrowserEnvironment["document"] {
    return {
      hidden: this.hidden,
      addEventListener: (_type, listener) => {
        this.listener = listener;
      },
      removeEventListener: (_type, listener) => {
        if (this.listener === listener) {
          this.listener = null;
          ++this.removedListeners;
        }
      },
    };
  }

  requestAnimationFrame(callback: (timestampMilliseconds: number) => void): number {
    const handle = this.nextHandle++;
    this.callbacks.set(handle, callback);

    return handle;
  }

  cancelAnimationFrame(handle: number): void {
    this.cancelled.push(handle);
    this.callbacks.delete(handle);
  }

  tick(timestampMilliseconds: number): void {
    const [handle, callback] = [...this.callbacks][0]!;
    this.callbacks.delete(handle);
    callback(timestampMilliseconds);
  }
}

class RecordingTarget implements FrameTarget {
  readonly frames: number[] = [];
  readonly hiddenCalls: boolean[] = [];
  onFrame: (() => void) | null = null;

  frame(nowSeconds: number): void {
    this.frames.push(nowSeconds);
    this.onFrame?.();
  }

  setHidden(hidden: boolean): void {
    this.hiddenCalls.push(hidden);
  }
}

function setup(): { target: RecordingTarget; browser: FakeBrowser } {
  return { target: new RecordingTarget(), browser: new FakeBrowser() };
}

test("each rAF calls frame with the timestamp in seconds", () => {
  const { target, browser } = setup();
  runInBrowser(target, browser);

  browser.tick(1500);
  browser.tick(1516.5);

  assert.deepEqual(target.frames, [1.5, 1.5165]);
});

test("visibilitychange forwards setHidden with document.hidden", () => {
  const { target, browser } = setup();
  runInBrowser(target, browser);

  browser.hidden = true;
  browser.listener!();
  browser.hidden = false;
  browser.listener!();

  assert.deepEqual(target.hiddenCalls, [true, false]);
});

test("document already hidden at start calls setHidden(true)", () => {
  const { target, browser } = setup();
  browser.hidden = true;

  runInBrowser(target, browser);

  assert.deepEqual(target.hiddenCalls, [true]);
});

test("stop cancels the pending rAF and removes the listener", () => {
  const { target, browser } = setup();
  const loop = runInBrowser(target, browser);

  loop.stop();

  assert.equal(browser.callbacks.size, 0);
  assert.equal(browser.cancelled.length, 1);
  assert.equal(browser.removedListeners, 1);
});

test("exception in frame does not stop the loop: the next rAF was already requested", () => {
  const { target, browser } = setup();
  runInBrowser(target, browser);
  target.onFrame = () => {
    throw new Error("boom");
  };

  assert.throws(() => browser.tick(1000), /boom/);
  assert.equal(browser.callbacks.size, 1);

  target.onFrame = null;
  browser.tick(1016);

  assert.deepEqual(target.frames, [1, 1.016]);
});
