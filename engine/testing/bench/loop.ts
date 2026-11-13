import { writeFileSync } from "node:fs";

import { Phase } from "@oly/core";
import type { Engine, Entity, Runtime, SystemContext } from "@oly/core";

import { Position, Velocity } from "../fixture/fixture.generated.js";
import { expectedPositionX, loadBenchRuntime, median, parseOptions, rotated } from "./common.js";

const DeltaTime = Math.fround(1 / 60);
const FixedHz = 60;
const WarmupSteps = 200;
const EmptySteps = 100_000;
const TargetWork = 4_000_000;
const SystemCounts = [1, 4, 16, 64];
const InterleavingCounts = [4, 16, 64];

interface Case {
  name: string;
  step(): void;
  steps(): number;
  positionX(): number;
  dispose(): void;
}

interface Summary {
  median: number;
  min: number;
  max: number;
  samples: number[];
}

interface EmptySystemsRow {
  systems: number;
  zero: Summary;
  cpp: Summary;
  ts: Summary;
  tsOverCpp: number;
  nsPerCallback: number;
}

interface InterleavingRow {
  systems: number;
  alternating: Summary;
  grouped: Summary;
  alternatingOverGrouped: number;
}

interface BoundaryRow {
  size: number;
  nsPerEntity: Record<string, Summary>;
  tsLoopOverTsManual: number;
  tsLoopOverCppLoop: number;
}

function emptyTsSystem(): void {}

function firstEntity(engine: Engine): Entity {
  let first = -1;
  engine.world.query({ read: [Position] }).eachBlock((block) => {
    if (first < 0 && block.count > 0) {
      first = block.entity(0);
    }
  });

  return first;
}

function makeEngineCase(runtime: Runtime, name: string, setup: (engine: Engine) => void, entityCount = 0): Case {
  const engine = runtime.createEngine({ fixedHz: FixedHz, maxStepsPerFrame: 5 });
  for (let index = 0; index < entityCount; ++index) {
    engine.world.spawn([Position, Velocity], { Position: { x: 0, y: 0 }, Velocity: { x: 1, y: 2 } });
  }

  setup(engine);
  engine.frame(0);
  let executed = 0;

  return {
    name,
    step: () => {
      ++executed;
      engine.frame(executed / FixedHz);
    },
    steps: () => executed,
    positionX: () => (entityCount > 0 ? engine.world.get(firstEntity(engine), Position).x : 0),
    dispose: () => engine.dispose(),
  };
}

function moveTsSystem(context: SystemContext): void {
  context.query({ read: [Velocity], write: [Position] }).eachBlock((block) => {
    const velocity = block.read(Velocity);
    const position = block.write(Position);
    for (let row = 0; row < block.count; ++row) {
      position.x.set(row, position.x.get(row) + velocity.x.get(row) * DeltaTime);
      position.y.set(row, position.y.get(row) + velocity.y.get(row) * DeltaTime);
    }
  });
}

function makeManualCase(count: number): Case {
  const x = new Float32Array(count);
  const y = new Float32Array(count);
  const velocityX = new Float32Array(count).fill(1);
  const velocityY = new Float32Array(count).fill(2);
  let executed = 0;

  return {
    name: "ts-manual",
    step: () => {
      ++executed;
      for (let row = 0; row < count; ++row) {
        x[row] = x[row]! + velocityX[row]! * DeltaTime;
        y[row] = y[row]! + velocityY[row]! * DeltaTime;
      }
    },
    steps: () => executed,
    positionX: () => x[0]!,
    dispose: () => {},
  };
}

function summarize(samples: number[]): Summary {
  return { median: median(samples), min: Math.min(...samples), max: Math.max(...samples), samples };
}

function measureNanosecondsPerStep(cases: Case[], stepsPerSample: number, rounds: number): Map<string, Summary> {
  const samples = new Map<string, number[]>(cases.map((entry) => [entry.name, []]));

  for (const entry of cases) {
    for (let warmup = 0; warmup < WarmupSteps; ++warmup) {
      entry.step();
    }
  }

  for (let round = 0; round < rounds; ++round) {
    for (const entry of rotated(cases, round)) {
      const start = performance.now();
      for (let step = 0; step < stepsPerSample; ++step) {
        entry.step();
      }
      samples.get(entry.name)!.push(((performance.now() - start) * 1e6) / stepsPerSample);
    }
  }

  return new Map([...samples].map(([name, values]) => [name, summarize(values)]));
}

function verifyPositions(cases: Case[], label: string): boolean {
  let correct = true;
  for (const entry of cases) {
    const expected = expectedPositionX(entry.steps(), DeltaTime);
    const actual = entry.positionX();
    if (Math.abs(actual - expected) > 1e-3 * Math.abs(expected)) {
      console.error(`${label} ${entry.name}: x=${actual}, expected ${expected}`);
      correct = false;
    }
  }

  return correct;
}

function addNativeNoop(engine: Engine): void {
  engine.addNativeSystem(Phase.Update, "noop");
}

function addScriptNoop(engine: Engine): void {
  engine.addSystem(Phase.Update, "empty", emptyTsSystem);
}

function addGrouped(engine: Engine, native: number, script: number): void {
  for (let index = 0; index < native; ++index) {
    addNativeNoop(engine);
  }

  for (let index = 0; index < script; ++index) {
    addScriptNoop(engine);
  }
}

function addAlternating(engine: Engine, pairs: number): void {
  for (let index = 0; index < pairs; ++index) {
    addNativeNoop(engine);
    addScriptNoop(engine);
  }
}

function measureEmptySystems(runtime: Runtime, rounds: number): EmptySystemsRow[] {
  return SystemCounts.map((count) => {
    const cases = [
      makeEngineCase(runtime, "cpp", (engine) => addGrouped(engine, count, 0)),
      makeEngineCase(runtime, "ts", (engine) => addGrouped(engine, 0, count)),
      makeEngineCase(runtime, "zero", () => {}),
    ];
    const summaries = measureNanosecondsPerStep(cases, EmptySteps, rounds);
    cases.forEach((entry) => entry.dispose());

    const cpp = summaries.get("cpp")!;
    const script = summaries.get("ts")!;

    return {
      systems: count,
      zero: summaries.get("zero")!,
      cpp,
      ts: script,
      tsOverCpp: script.median / cpp.median,
      nsPerCallback: (script.median - cpp.median) / count,
    };
  });
}

function measureInterleaving(runtime: Runtime, rounds: number): InterleavingRow[] {
  return InterleavingCounts.map((count) => {
    const half = count / 2;
    const cases = [
      makeEngineCase(runtime, "alternating", (engine) => addAlternating(engine, half)),
      makeEngineCase(runtime, "grouped", (engine) => addGrouped(engine, half, half)),
    ];
    const summaries = measureNanosecondsPerStep(cases, EmptySteps, rounds);
    cases.forEach((entry) => entry.dispose());

    const alternating = summaries.get("alternating")!;
    const grouped = summaries.get("grouped")!;

    return { systems: count, alternating, grouped, alternatingOverGrouped: alternating.median / grouped.median };
  });
}

function measureBoundaryInLoop(runtime: Runtime, sizes: number[], rounds: number): { rows: BoundaryRow[]; correct: boolean } {
  let correct = true;
  const rows = sizes.map((size) => {
    const cases = [
      makeEngineCase(runtime, "cpp-loop", (engine) => void engine.addNativeSystem(Phase.Update, "move"), size),
      makeEngineCase(runtime, "ts-loop", (engine) => void engine.addSystem(Phase.Update, "move", moveTsSystem), size),
      makeManualCase(size),
    ];
    const summaries = measureNanosecondsPerStep(cases, Math.ceil(TargetWork / size), rounds);
    correct = verifyPositions(cases, `N=${size}`) && correct;
    cases.forEach((entry) => entry.dispose());

    const nsPerEntity = Object.fromEntries(
      [...summaries].map(([name, summary]) => [name, { ...summary, median: summary.median / size }]),
    );

    return {
      size,
      nsPerEntity,
      tsLoopOverTsManual: nsPerEntity["ts-loop"]!.median / nsPerEntity["ts-manual"]!.median,
      tsLoopOverCppLoop: nsPerEntity["ts-loop"]!.median / nsPerEntity["cpp-loop"]!.median,
    };
  });

  return { rows, correct };
}

function printTables(emptySystems: EmptySystemsRow[], interleaving: InterleavingRow[], boundary: BoundaryRow[]): void {
  console.log("K\tzero ns/step\tcpp\tts\tts/cpp\tns/callback");
  for (const row of emptySystems) {
    console.log(
      `${row.systems}\t${row.zero.median.toFixed(0)}\t${row.cpp.median.toFixed(0)}\t${row.ts.median.toFixed(0)}\t${row.tsOverCpp.toFixed(2)}\t${row.nsPerCallback.toFixed(1)}`,
    );
  }

  console.log("\nK\talternating\tgrouped\talternating/grouped");
  for (const row of interleaving) {
    console.log(`${row.systems}\t${row.alternating.median.toFixed(0)}\t${row.grouped.median.toFixed(0)}\t${row.alternatingOverGrouped.toFixed(2)}`);
  }

  console.log("\nN\tts-loop/ts-manual\tts-loop/cpp-loop");
  for (const row of boundary) {
    console.log(`${row.size}\t${row.tsLoopOverTsManual.toFixed(2)}\t${row.tsLoopOverCppLoop.toFixed(2)}`);
  }
}

async function main(): Promise<void> {
  const options = parseOptions(
    process.argv.slice(2),
    "usage: node loop.js --module <fixture_game.mjs> --out <json> [--sizes 1000,10000] [--rounds 10] [--commit <hash>]",
  );
  const runtime = await loadBenchRuntime(options.modulePath);

  const emptySystems = measureEmptySystems(runtime, options.rounds);
  const interleaving = measureInterleaving(runtime, options.rounds);
  const boundary = measureBoundaryInLoop(runtime, options.sizes, options.rounds);
  if (!boundary.correct) {
    process.exit(1);
  }

  const meta = {
    date: new Date().toISOString(),
    commit: options.commit,
    node: process.versions.node,
    v8: process.versions.v8,
    sizes: options.sizes,
    rounds: options.rounds,
  };
  writeFileSync(options.outPath, JSON.stringify({ meta, emptySystems, interleaving, boundaryInLoop: boundary.rows }, null, 2));

  printTables(emptySystems, interleaving, boundary.rows);
}

await main();
