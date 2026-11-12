import { pathToFileURL } from "node:url";

import { loadRuntime } from "@oly/core";
import type { OlyModuleFactory, Runtime } from "@oly/core";
import { moduleOf, type OlyModule } from "@oly/core/internal";

import { Echo, Enemy, Mixed, Ping, Player, Position, Trace, Velocity, aim, fire, gameOver, menu, move, playing, togglePause } from "../../fixture/schema.js";
import schema from "../../fixture/schema.js";

export { Echo, Enemy, Mixed, Ping, Player, Position, Trace, Velocity, aim, fire, gameOver, menu, move, playing, schema, togglePause };

export interface FixtureExports {
  fixtureMove(world: number, deltaTime: number): void;
  fixtureSumPositionX(world: number): number;
  fixtureMixedChecksum(world: number, index: number, generation: number): number;
  fixtureSetPingCount(count: number): void;
  fixtureTakePingSum(): number;
  fixtureTakeFirePressCount(): number;
  fixtureScheduleDigit(engine: number, delaySeconds: number, index: number, generation: number, digit: number): void;
}

interface FixtureModule extends OlyModule {
  _fixtureMove(world: number, deltaTime: number): void;
  _fixtureSumPositionX(world: number): number;
  _fixtureMixedChecksum(world: number, index: number, generation: number): number;
  _fixtureSetPingCount(count: number): void;
  _fixtureTakePingSum(): number;
  _fixtureTakeFirePressCount(): number;
  _fixtureScheduleDigit(engine: number, delaySeconds: number, index: number, generation: number, digit: number): void;
}

export function fixtureFactory(): OlyModuleFactory {
  const modulePath = process.env.OLY_FIXTURE_MODULE;
  if (!modulePath) {
    throw new Error("set OLY_FIXTURE_MODULE to the path of fixture_game.mjs (ctest already does this)");
  }

  return async () => {
    const loaded: { default: OlyModuleFactory } = await import(pathToFileURL(modulePath).href);

    return loaded.default();
  };
}

export async function loadFixture(): Promise<{ runtime: Runtime; fixture: FixtureExports }> {
  const runtime = await loadRuntime(fixtureFactory(), schema);
  const module = moduleOf(runtime) as FixtureModule;

  return {
    runtime,
    fixture: {
      fixtureMove: (world, deltaTime) => module._fixtureMove(world, deltaTime),
      fixtureSumPositionX: (world) => module._fixtureSumPositionX(world),
      fixtureMixedChecksum: (world, index, generation) => module._fixtureMixedChecksum(world, index, generation),
      fixtureSetPingCount: (count) => module._fixtureSetPingCount(count),
      fixtureTakePingSum: () => module._fixtureTakePingSum(),
      fixtureTakeFirePressCount: () => module._fixtureTakeFirePressCount(),
      fixtureScheduleDigit: (engine, delaySeconds, index, generation, digit) => module._fixtureScheduleDigit(engine, delaySeconds, index, generation, digit),
    },
  };
}
