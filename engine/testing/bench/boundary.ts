import { writeFileSync } from "node:fs";

import type { Entity, Query, Runtime, World } from "@oly/core";
import { moduleOf, type OlyModule } from "@oly/core/internal";

import { Position, Velocity } from "../fixture/schema.js";
import { expectedPositionX, loadBenchRuntime, median, parseOptions, rotated } from "./common.js";

const DeltaTime = Math.fround(1 / 60);
const WarmupIterations = 50;
const TargetWork = 4_000_000;

type VariantName = "cpp" | "ts-engine" | "ts-engine-raw" | "ts-manual";

interface Variant {
  name: VariantName;
  step(): void;
  positionX(): number;
  dispose(): void;
}

interface FixtureModule extends OlyModule {
  _fixtureMove(world: number, deltaTime: number): void;
}

function spawnEntities(world: World, count: number): void {
  for (let index = 0; index < count; ++index) {
    world.spawn([Position, Velocity], { Position: { x: 0, y: 0 }, Velocity: { x: 1, y: 2 } });
  }
}

function firstEntity(query: Query): Entity {
  let first = -1;
  query.eachBlock((block) => {
    if (first < 0 && block.count > 0) {
      first = block.entity(0);
    }
  });

  return first;
}

function makeCpp(runtime: Runtime, count: number): Variant {
  const module = moduleOf(runtime) as FixtureModule;
  const world = runtime.createWorld();
  spawnEntities(world, count);
  const query = world.query({ read: [Velocity], write: [Position] });

  return {
    name: "cpp",
    step: () => module._fixtureMove(world.handle, DeltaTime),
    positionX: () => world.get(firstEntity(query), Position).x,
    dispose: () => world.dispose(),
  };
}

function makeTsEngine(runtime: Runtime, count: number): Variant {
  const world = runtime.createWorld();
  spawnEntities(world, count);
  const query = world.query({ read: [Velocity], write: [Position] });

  return {
    name: "ts-engine",
    step: () => {
      query.eachBlock((block) => {
        const velocity = block.read(Velocity);
        const position = block.write(Position);
        for (let row = 0; row < block.count; ++row) {
          position.x.set(row, position.x.get(row) + velocity.x.get(row) * DeltaTime);
          position.y.set(row, position.y.get(row) + velocity.y.get(row) * DeltaTime);
        }
      });
    },
    positionX: () => world.get(firstEntity(query), Position).x,
    dispose: () => world.dispose(),
  };
}

function makeTsEngineRaw(runtime: Runtime, count: number): Variant {
  const world = runtime.createWorld();
  spawnEntities(world, count);
  const query = world.query({ read: [Velocity], write: [Position] });

  return {
    name: "ts-engine-raw",
    step: () => {
      query.eachBlock((block) => {
        const velocity = block.read(Velocity);
        const position = block.write(Position);
        const positionX = position.x.raw();
        const positionY = position.y.raw();
        const velocityX = velocity.x.raw();
        const velocityY = velocity.y.raw();
        const rows = block.count;
        for (let row = 0; row < rows; ++row) {
          const indexX = positionX.base + row * positionX.stride;
          const indexY = positionY.base + row * positionY.stride;
          positionX.array[indexX] = positionX.array[indexX]! + velocityX.array[velocityX.base + row * velocityX.stride]! * DeltaTime;
          positionY.array[indexY] = positionY.array[indexY]! + velocityY.array[velocityY.base + row * velocityY.stride]! * DeltaTime;
        }
      });
    },
    positionX: () => world.get(firstEntity(query), Position).x,
    dispose: () => world.dispose(),
  };
}

function makeTsManual(count: number): Variant {
  const x = new Float32Array(count);
  const y = new Float32Array(count);
  const velocityX = new Float32Array(count).fill(1);
  const velocityY = new Float32Array(count).fill(2);

  return {
    name: "ts-manual",
    step: () => {
      for (let row = 0; row < count; ++row) {
        x[row] = x[row]! + velocityX[row]! * DeltaTime;
        y[row] = y[row]! + velocityY[row]! * DeltaTime;
      }
    },
    positionX: () => x[0]!,
    dispose: () => {},
  };
}

async function main(): Promise<void> {
  const options = parseOptions(process.argv.slice(2), "usage: node boundary.js --module <fixture_game.mjs> --out <json> [--sizes 1000,10000] [--rounds 10] [--commit <hash>]");
  const runtime = await loadBenchRuntime(options.modulePath);
  const results: { size: number; variant: VariantName; nsPerEntity: number[]; median: number; min: number; max: number }[] = [];
  const ratios: { size: number; tsEngineOverTsManual: number; tsEngineOverCpp: number; tsEngineRawOverTsManual: number }[] = [];
  const iterationsPerSize: Record<number, number> = {};
  let failed = false;

  for (const size of options.sizes) {
    const iterations = Math.ceil(TargetWork / size);
    iterationsPerSize[size] = iterations;
    const variants = [makeCpp(runtime, size), makeTsEngine(runtime, size), makeTsEngineRaw(runtime, size), makeTsManual(size)];
    const samples = new Map<VariantName, number[]>(variants.map((variant) => [variant.name, []]));
    const executed = new Map<VariantName, number>();

    for (const variant of variants) {
      for (let warmup = 0; warmup < WarmupIterations; ++warmup) {
        variant.step();
      }
      executed.set(variant.name, WarmupIterations);
    }

    for (let round = 0; round < options.rounds; ++round) {
      for (const variant of rotated(variants, round)) {
        const start = performance.now();
        for (let iteration = 0; iteration < iterations; ++iteration) {
          variant.step();
        }
        const elapsedMilliseconds = performance.now() - start;
        samples.get(variant.name)!.push((elapsedMilliseconds * 1e6) / (iterations * size));
        executed.set(variant.name, executed.get(variant.name)! + iterations);
      }
    }

    for (const variant of variants) {
      const expected = expectedPositionX(executed.get(variant.name)!, DeltaTime);
      const actual = variant.positionX();
      if (Math.abs(actual - expected) > 1e-3 * Math.abs(expected)) {
        console.error(`variant ${variant.name} N=${size}: x=${actual}, expected ${expected}`);
        failed = true;
      }
    }

    const medians = new Map<VariantName, number>();
    for (const variant of variants) {
      const values = samples.get(variant.name)!;
      const variantMedian = median(values);
      medians.set(variant.name, variantMedian);
      results.push({ size, variant: variant.name, nsPerEntity: values, median: variantMedian, min: Math.min(...values), max: Math.max(...values) });
      variant.dispose();
    }

    ratios.push({
      size,
      tsEngineOverTsManual: medians.get("ts-engine")! / medians.get("ts-manual")!,
      tsEngineOverCpp: medians.get("ts-engine")! / medians.get("cpp")!,
      tsEngineRawOverTsManual: medians.get("ts-engine-raw")! / medians.get("ts-manual")!,
    });
  }

  if (failed) {
    process.exit(1);
  }

  const meta = {
    date: new Date().toISOString(),
    commit: options.commit,
    node: process.versions.node,
    v8: process.versions.v8,
    sizes: options.sizes,
    rounds: options.rounds,
    iterationsPerSize,
  };
  writeFileSync(options.outPath, JSON.stringify({ meta, results, ratios }, null, 2));

  console.log("N\tts-engine/ts-manual\tts-engine/cpp\tts-engine-raw/ts-manual");
  for (const ratio of ratios) {
    console.log(`${ratio.size}\t${ratio.tsEngineOverTsManual.toFixed(2)}\t${ratio.tsEngineOverCpp.toFixed(2)}\t${ratio.tsEngineRawOverTsManual.toFixed(2)}`);
  }
}

await main();
