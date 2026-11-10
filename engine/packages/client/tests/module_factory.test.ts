import assert from "node:assert/strict";
import { test } from "node:test";

import { moduleFactoryFromUrl } from "../dist/index.js";

const demoUrl = "https://x/games/demo.mjs";

test("importa o .mjs pela URL absoluta", async () => {
  const imported: string[] = [];
  const factory = moduleFactoryFromUrl(demoUrl, async (href) => {
    imported.push(href);
    return { default: async () => ({}) };
  });

  await factory();

  assert.deepEqual(imported, [demoUrl]);
});

test("locateFile resolve o .wasm ao lado do .mjs", async () => {
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

test("devolve o módulo criado pela fábrica", async () => {
  const created = { HEAPU8: new Uint8Array(1) };
  const factory = moduleFactoryFromUrl(demoUrl, async () => ({ default: async () => created }));

  const loaded = await factory();

  assert.equal(loaded, created as never);
});

test("default ausente é erro com a URL", async () => {
  const factory = moduleFactoryFromUrl(demoUrl, async () => ({}));

  await assert.rejects(factory(), (error: Error) => error.message.includes(demoUrl) && error.message.includes("não exporta uma fábrica Emscripten"));
});
