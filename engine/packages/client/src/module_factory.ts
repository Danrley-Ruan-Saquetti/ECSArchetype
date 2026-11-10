import type { OlyModuleFactory } from "@oly/core";

type EmscriptenFactory = (moduleArgument: { locateFile(path: string): string }) => Promise<Awaited<ReturnType<OlyModuleFactory>>>;

function importByHref(href: string): Promise<unknown> {
  return import(/* @vite-ignore */ href);
}

/**
 * Fábrica do módulo Emscripten (.mjs) a partir de uma URL, relativa à página quando há `location`. O .wasm é
 * buscado ao lado do .mjs. A fábrica rejeita com Error se o .mjs não exportar a fábrica Emscripten como default.
 */
export function moduleFactoryFromUrl(url: string | URL, importModule: (href: string) => Promise<unknown> = importByHref): OlyModuleFactory {
  const base = globalThis.location?.href;
  const href = (base === undefined ? new URL(url) : new URL(url, base)).href;

  return async () => {
    const loaded = (await importModule(href)) as { default?: unknown };
    if (typeof loaded.default !== "function") {
      throw new Error(`${href}: o módulo não exporta uma fábrica Emscripten como default`);
    }

    return (loaded.default as EmscriptenFactory)({ locateFile: (path) => new URL(path, href).href });
  };
}
