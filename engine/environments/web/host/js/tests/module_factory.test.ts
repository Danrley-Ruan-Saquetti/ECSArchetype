import assert from "node:assert/strict";
import { test } from "node:test";

import { moduleFactoryFromUrl } from "../dist/index.js";

const demoUrl = "https://x/games/demo.mjs";

test("imports the .mjs by its absolute URL", async () => {
  const imported: string[] = [];
  const factory = moduleFactoryFromUrl(demoUrl, async (href) => {
    imported.push(href);
    return { default: async () => ({}) };
  });

  await factory();

  assert.deepEqual(imported, [demoUrl]);
});

test("locateFile resolves the .wasm next to the .mjs", async () => {
  let received: { locateFile(path: string): string } | undefined;
  const factory = moduleFactoryFromUrl(new URL(demoUrl), async () => ({
    default: async (moduleArgument: { locateFile(path: string): string }) => {
      received = moduleArgument;
      return {};
    },
  }));

  await factory();

  assert.equal(received!.locateFile("demo.wasm"), "https://x/games/demo.wasm");
});

test("returns the module created by the factory", async () => {
  const created = { HEAPU8: new Uint8Array(1) };
  const factory = moduleFactoryFromUrl(demoUrl, async () => ({ default: async () => created }));

  const loaded = await factory();

  assert.equal(loaded, created as never);
});

test("missing default is an error with the URL", async () => {
  const factory = moduleFactoryFromUrl(demoUrl, async () => ({}));

  await assert.rejects(factory(), (error: Error) => error.message.includes(demoUrl) && error.message.includes("does not export an Emscripten factory"));
});
