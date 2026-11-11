import { writeFileSync } from "node:fs";

import { Phase } from "@oly/core";
import type { Engine, EventBatch, HandlerContext, Runtime, SystemContext } from "@oly/core";
import { moduleOf, type OlyModule } from "@oly/core/internal";

import { Ping } from "../fixture/schema.js";
import { loadBenchRuntime, median, parseOptions, rotated } from "./common.js";

const FixedHz = 60;
const WarmupSteps = 200;
const TargetEvents = 1_000_000;
const EventCounts = [16, 256, 4096];

interface FixtureModule extends OlyModule {
  _fixtureSetPingCount(count: number): void;
  _fixtureTakePingSum(): number;
}

interface Case {
  name: string;
  step(): void;
  steps(): number;
  dispose(): void;
}

interface Summary {
  median: number;
  min: number;
  max: number;
  samples: number[];
}

interface HandlerRow {
  events: number;
  nsPerEvent: Record<string, Summary>;
  tsOverCpp: number;
  nsPerBatch: number;
}

interface EmissionRow {
  events: number;
  nsPerEvent: Record<string, Summary>;
  tsOverCpp: number;
}

function summarize(samples: number[]): Summary {
  return { median: median(samples), min: Math.min(...samples), max: Math.max(...samples), samples };
}

function makeEngineCase(runtime: Runtime, name: string, setup: (engine: Engine) => void): Case {
  const engine = runtime.createEngine({ fixedHz: FixedHz, maxStepsPerFrame: 5 });
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
    dispose: () => engine.dispose(),
  };
}

function measureNanosecondsPerEvent(cases: Case[], eventCount: number, rounds: number): Map<string, Summary> {
  const stepsPerSample = Math.ceil(TargetEvents / eventCount);
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
      samples.get(entry.name)!.push(((performance.now() - start) * 1e6) / (stepsPerSample * eventCount));
    }
  }

  return new Map([...samples].map(([name, values]) => [name, summarize(values)]));
}

function checkSum(label: string, actual: number, expected: number): boolean {
  if (actual !== expected) {
    console.error(`${label}: soma ${actual}, esperado ${expected}`);
    return false;
  }

  return true;
}

function addNativeEmitter(engine: Engine): void {
  engine.addNativeSystem(Phase.Update, "emitPings");
}

function measureHandler(runtime: Runtime, module: FixtureModule, rounds: number): { rows: HandlerRow[]; correct: boolean } {
  let correct = true;
  const rows = EventCounts.map((eventCount) => {
    module._fixtureSetPingCount(eventCount);
    module._fixtureTakePingSum();
    let scriptSum = 0;
    const sumInScript = (_context: HandlerContext, batch: EventBatch<typeof Ping.fields>) => {
      const view = batch.read();
      for (let index = 0; index < batch.count; ++index) {
        scriptSum += view.value.get(index);
      }
    };

    const cases = [
      makeEngineCase(runtime, "none", addNativeEmitter),
      makeEngineCase(runtime, "cpp", (engine) => {
        addNativeEmitter(engine);
        engine.addNativeHandler(Ping, "sumPing");
      }),
      makeEngineCase(runtime, "ts", (engine) => {
        addNativeEmitter(engine);
        engine.on(Ping, "sumPing", sumInScript);
      }),
    ];
    const summaries = measureNanosecondsPerEvent(cases, eventCount, rounds);
    const [, cppCase, scriptCase] = cases;
    correct = checkSum(`handler N=${eventCount} cpp`, module._fixtureTakePingSum(), eventCount * cppCase!.steps()) && correct;
    correct = checkSum(`handler N=${eventCount} ts`, scriptSum, eventCount * scriptCase!.steps()) && correct;
    cases.forEach((entry) => entry.dispose());

    const cpp = summaries.get("cpp")!;
    const script = summaries.get("ts")!;

    return {
      events: eventCount,
      nsPerEvent: Object.fromEntries(summaries),
      tsOverCpp: script.median / cpp.median,
      nsPerBatch: (script.median - cpp.median) * eventCount,
    };
  });

  return { rows, correct };
}

function measureEmission(runtime: Runtime, module: FixtureModule, rounds: number): { rows: EmissionRow[]; correct: boolean } {
  let correct = true;
  const rows = EventCounts.map((eventCount) => {
    module._fixtureSetPingCount(eventCount);
    module._fixtureTakePingSum();
    const emitInScript = (context: SystemContext) => {
      for (let index = 0; index < eventCount; ++index) {
        context.emit(Ping, { value: 1 });
      }
    };

    const cases = [
      makeEngineCase(runtime, "cpp", (engine) => {
        addNativeEmitter(engine);
        engine.addNativeHandler(Ping, "sumPing");
      }),
      makeEngineCase(runtime, "ts", (engine) => {
        engine.addSystem(Phase.Update, "emitPings", emitInScript);
        engine.addNativeHandler(Ping, "sumPing");
      }),
    ];
    const summaries = measureNanosecondsPerEvent(cases, eventCount, rounds);
    const totalSteps = cases.reduce((total, entry) => total + entry.steps(), 0);
    correct = checkSum(`emissão N=${eventCount}`, module._fixtureTakePingSum(), eventCount * totalSteps) && correct;
    cases.forEach((entry) => entry.dispose());

    return {
      events: eventCount,
      nsPerEvent: Object.fromEntries(summaries),
      tsOverCpp: summaries.get("ts")!.median / summaries.get("cpp")!.median,
    };
  });

  return { rows, correct };
}

function printTables(handler: HandlerRow[], emission: EmissionRow[]): void {
  console.log("N\tnone ns/evento\tcpp\tts\tts/cpp\t(ts-cpp) ns/lote");
  for (const row of handler) {
    const ns = row.nsPerEvent;
    console.log(
      `${row.events}\t${ns["none"]!.median.toFixed(2)}\t${ns["cpp"]!.median.toFixed(2)}\t${ns["ts"]!.median.toFixed(2)}\t${row.tsOverCpp.toFixed(2)}\t${row.nsPerBatch.toFixed(0)}`,
    );
  }

  console.log("\nN\temissão cpp ns/evento\tts\tts/cpp");
  for (const row of emission) {
    console.log(`${row.events}\t${row.nsPerEvent["cpp"]!.median.toFixed(2)}\t${row.nsPerEvent["ts"]!.median.toFixed(2)}\t${row.tsOverCpp.toFixed(2)}`);
  }
}

async function main(): Promise<void> {
  const options = parseOptions(process.argv.slice(2), "uso: node events.js --module <fixture_game.mjs> --out <json> [--rounds 10] [--commit <hash>]");
  const runtime = await loadBenchRuntime(options.modulePath);
  const module = moduleOf(runtime) as FixtureModule;

  const handler = measureHandler(runtime, module, options.rounds);
  const emission = measureEmission(runtime, module, options.rounds);
  if (!handler.correct || !emission.correct) {
    process.exit(1);
  }

  const meta = {
    date: new Date().toISOString(),
    commit: options.commit,
    node: process.versions.node,
    v8: process.versions.v8,
    sizes: EventCounts,
    rounds: options.rounds,
  };
  writeFileSync(options.outPath, JSON.stringify({ meta, handler: handler.rows, emission: emission.rows }, null, 2));

  printTables(handler.rows, emission.rows);
}

await main();
