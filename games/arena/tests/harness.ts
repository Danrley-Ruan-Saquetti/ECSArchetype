import { pathToFileURL } from "node:url";

import { loadRuntime } from "@oly/core";
import type { ButtonName, Engine, Entity, OlyModuleFactory, Runtime, SchemaType } from "@oly/core";

import schema, { Player } from "../schema.js";
import { installGame } from "../src/game.js";
import type { ArenaScore } from "../src/game.js";
import type { Drawer } from "../src/render.js";

const StepsPerSecond = 60;

export interface Circle {
  readonly x: number;
  readonly y: number;
  readonly radius: number;
  readonly color: string;
}

/** Guarda as chamadas feitas desde o último `clear`, que é o que um quadro desenhou. */
export class RecordingDrawer implements Drawer {
  readonly #texts: string[] = [];
  readonly #circles: Circle[] = [];

  clear(): void {
    this.#texts.length = 0;
    this.#circles.length = 0;
  }

  circle(x: number, y: number, radius: number, color: string): void {
    this.#circles.push({ x, y, radius, color });
  }

  rect(): void {}

  text(_x: number, _y: number, content: string): void {
    this.#texts.push(content);
  }

  texts(): readonly string[] {
    return this.#texts;
  }

  circles(): readonly Circle[] {
    return this.#circles;
  }
}

export function sequenceRandom(values: readonly number[]): () => number {
  let index = 0;

  return () => values[index++ % values.length]!;
}

export interface ArenaTest {
  readonly engine: Engine;
  readonly score: ArenaScore;
  readonly drawer: RecordingDrawer;
  step(count?: number): void;
  stepSeconds(seconds: number): void;
  /** Toque curto: dá `pressed` mas não `held` (§4.5). Para atirar use hold, step e release. */
  tap(button: ButtonName): void;
  hold(button: ButtonName): void;
  release(button: ButtonName): void;
  /** Sem tipos, conta todas as entidades. */
  count(...types: SchemaType[]): number;
  player(): Entity;
}

function arenaFactory(): OlyModuleFactory {
  const modulePath = process.env.OLY_ARENA_MODULE;
  if (!modulePath) {
    throw new Error("defina OLY_ARENA_MODULE com o caminho de arena_game.mjs (o ctest já faz isso)");
  }

  return async () => {
    const loaded: { default: OlyModuleFactory } = await import(pathToFileURL(modulePath).href);

    return loaded.default();
  };
}

export function loadArena(): Promise<Runtime> {
  return loadRuntime(arenaFactory(), schema);
}

export function withArena(runtime: Runtime, body: (arena: ArenaTest) => void, random: () => number = sequenceRandom([0.5, 0.25, 0.75, 0.1, 0.9])): void {
  const engine = runtime.createEngine({ fixedHz: StepsPerSecond });
  const drawer = new RecordingDrawer();
  const score = installGame(engine, { drawer, random });
  engine.frame(0);
  let stepNumber = 0;

  const arena: ArenaTest = {
    engine,
    score,
    drawer,
    step(count = 1) {
      for (let index = 0; index < count; ++index) {
        ++stepNumber;
        engine.frame(stepNumber / StepsPerSecond);
        const broken = engine.world.checkInvariants();
        if (broken !== null) {
          throw new Error(`invariante quebrada no passo ${stepNumber}: ${broken}`);
        }
      }
    },
    stepSeconds(seconds) {
      arena.step(Math.round(seconds * StepsPerSecond));
    },
    tap(button) {
      engine.actions.buttonDown(button);
      engine.actions.buttonUp(button);
      arena.step();
    },
    hold(button) {
      engine.actions.buttonDown(button);
    },
    release(button) {
      engine.actions.buttonUp(button);
    },
    count(...types) {
      let total = 0;
      engine.world.query({ with: types }).eachBlock((block) => {
        total += block.count;
      });

      return total;
    },
    player() {
      return engine.world.query({ with: [Player] }).single().entity;
    },
  };

  try {
    body(arena);
  } finally {
    engine.dispose();
  }
}
