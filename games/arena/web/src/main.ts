import { loadRuntime } from "@oly/core";
import { attachBrowserInput, moduleFactoryFromUrl, runInBrowser } from "@oly/web-host";

import schema from "../schema.js";
import { ArenaHeight, ArenaWidth } from "./arena.js";
import { installGame } from "./game.js";
import type { Drawer } from "./render.js";

const Font = "14px monospace";

function createCanvasDrawer(context: CanvasRenderingContext2D): Drawer {
  return {
    clear() {
      context.clearRect(0, 0, ArenaWidth, ArenaHeight);
    },
    circle(x, y, radius, color) {
      context.fillStyle = color;
      context.beginPath();
      context.arc(x, y, radius, 0, Math.PI * 2);
      context.fill();
    },
    rect(x, y, width, height, color) {
      context.fillStyle = color;
      context.fillRect(x, y, width, height);
    },
    text(x, y, content, color) {
      context.fillStyle = color;
      context.font = Font;
      context.fillText(content, x, y);
    },
  };
}

async function boot(): Promise<void> {
  const canvas = document.getElementById("arena");
  if (!(canvas instanceof HTMLCanvasElement)) {
    throw new Error('canvas with id "arena" not found on the page');
  }

  canvas.width = ArenaWidth;
  canvas.height = ArenaHeight;
  const context = canvas.getContext("2d");
  if (context === null) {
    throw new Error("the browser did not provide a 2D context for the canvas");
  }

  const runtime = await loadRuntime(moduleFactoryFromUrl("./arena_game.mjs"), schema);
  const engine = runtime.createEngine({ fixedHz: 60 });
  installGame(engine, { drawer: createCanvasDrawer(context), random: Math.random });
  // attachBrowserInput only prevents the right-click menu when MouseRight is bound to an action; the game does not use that button.
  canvas.addEventListener("contextmenu", (event) => event.preventDefault());
  attachBrowserInput(engine, canvas);
  runInBrowser(engine);
}

boot().catch((error: unknown) => {
  console.error(error);
  document.body.textContent = `Error starting the game: ${error instanceof Error ? error.message : String(error)}`;
});
