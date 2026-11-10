import { pathToFileURL } from "node:url";

import { Enemy, Mixed, Player, Position, Velocity } from "../../fixture/schema.js";
import schema from "../../fixture/schema.js";
import { loadEngine } from "../../ts/runtime.js";
import type { OlyModule, OlyModuleFactory } from "../../ts/module.js";
import type { EngineRuntime } from "../../ts/runtime.js";

export { Enemy, Mixed, Player, Position, Velocity, schema };

export interface FixtureExports {
  fixtureMove(world: number, deltaTime: number): void;
  fixtureSumPositionX(world: number): number;
  fixtureMixedChecksum(world: number, index: number, generation: number): number;
}

interface FixtureModule extends OlyModule {
  _fixtureMove(world: number, deltaTime: number): void;
  _fixtureSumPositionX(world: number): number;
  _fixtureMixedChecksum(world: number, index: number, generation: number): number;
}

export function fixtureFactory(): OlyModuleFactory {
  const modulePath = process.env.ENGINE_FIXTURE_MODULE;
  if (!modulePath) {
    throw new Error("defina ENGINE_FIXTURE_MODULE com o caminho de fixture_game.mjs (o ctest já faz isso)");
  }

  return async () => {
    const loaded: { default: OlyModuleFactory } = await import(pathToFileURL(modulePath).href);

    return loaded.default();
  };
}

export async function loadFixture(): Promise<{ runtime: EngineRuntime; fixture: FixtureExports }> {
  const runtime = await loadEngine(fixtureFactory(), schema);
  const module = runtime.module as FixtureModule;

  return {
    runtime,
    fixture: {
      fixtureMove: (world, deltaTime) => module._fixtureMove(world, deltaTime),
      fixtureSumPositionX: (world) => module._fixtureSumPositionX(world),
      fixtureMixedChecksum: (world, index, generation) => module._fixtureMixedChecksum(world, index, generation),
    },
  };
}
