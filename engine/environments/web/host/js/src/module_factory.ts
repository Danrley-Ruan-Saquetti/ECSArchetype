import type { OlyModuleFactory } from "@oly/core";

type EmscriptenFactory = (moduleArgument: { locateFile(path: string): string }) => Promise<Awaited<ReturnType<OlyModuleFactory>>>;

function importByHref(href: string): Promise<unknown> {
  return import(/* @vite-ignore */ href);
}

/**
 * Emscripten module (.mjs) factory from a URL, relative to the page when there is a `location`. The .wasm is
 * fetched next to the .mjs. The factory rejects with Error if the .mjs does not export the Emscripten factory as default.
 */
export function moduleFactoryFromUrl(url: string | URL, importModule: (href: string) => Promise<unknown> = importByHref): OlyModuleFactory {
  const base = globalThis.location?.href;
  const href = (base === undefined ? new URL(url) : new URL(url, base)).href;

  return async () => {
    const loaded = (await importModule(href)) as { default?: unknown };
    if (typeof loaded.default !== "function") {
      throw new Error(`${href}: the module does not export an Emscripten factory as default`);
    }

    return (loaded.default as EmscriptenFactory)({ locateFile: (path) => new URL(path, href).href });
  };
}
