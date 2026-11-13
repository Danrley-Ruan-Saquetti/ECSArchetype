import { pathToFileURL } from "node:url";

import type { OlyModuleFactory } from "@oly/core";
import { loadRuntime, type Runtime } from "@oly/core";

import schema from "../fixture/fixture.generated.js";

export interface Options {
  modulePath: string;
  outPath: string;
  sizes: number[];
  rounds: number;
  commit: string;
}

export function parseOptions(argv: string[], usage: string): Options {
  const values = new Map<string, string>();
  for (let index = 0; index < argv.length; index += 2) {
    values.set(argv[index]!, argv[index + 1] ?? "");
  }

  const modulePath = values.get("--module");
  const outPath = values.get("--out");
  if (!modulePath || !outPath) {
    console.error(usage);
    process.exit(1);
  }

  return {
    modulePath,
    outPath,
    sizes: (values.get("--sizes") ?? "1000,10000,100000,1000000").split(",").map(Number),
    rounds: Number(values.get("--rounds") ?? "10"),
    commit: values.get("--commit") ?? "",
  };
}

export async function loadBenchRuntime(modulePath: string): Promise<Runtime> {
  const factory: OlyModuleFactory = async () => {
    const loaded: { default: OlyModuleFactory } = await import(pathToFileURL(modulePath).href);

    return loaded.default();
  };

  return loadRuntime(factory, schema);
}

export function median(values: number[]): number {
  const sorted = [...values].sort((left, right) => left - right);
  const middle = sorted.length >> 1;

  return sorted.length % 2 === 1 ? sorted[middle]! : (sorted[middle - 1]! + sorted[middle]!) / 2;
}

// Same f32 arithmetic as the variants, so the accumulated value is checked without comparison error.
export function expectedPositionX(iterations: number, deltaTime: number): number {
  let value = 0;
  for (let iteration = 0; iteration < iterations; ++iteration) {
    value = Math.fround(value + deltaTime);
  }

  return value;
}

export function rotated<T>(items: T[], shift: number): T[] {
  return items.map((_, index) => items[(index + shift) % items.length]!);
}
