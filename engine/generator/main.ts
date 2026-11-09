import { mkdirSync, writeFileSync } from "node:fs";
import { join, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import type { Schema } from "../ts/schema.js";
import { emitHeader, emitRegistry } from "./emit_cpp.js";

function argumentValue(name: string): string {
  const position = process.argv.indexOf(name);
  const value = position >= 0 ? process.argv[position + 1] : undefined;
  if (value === undefined) {
    console.error(`uso: node main.js --schema <schema.js> --out <diretório>  (falta ${name})`);
    process.exit(1);
  }

  return value;
}

function isSchema(value: unknown): value is Schema {
  const candidate = value as Partial<Schema> | null;

  return typeof candidate === "object" && candidate !== null && Array.isArray(candidate.types) && typeof candidate.hash === "number";
}

const schemaPath = resolve(argumentValue("--schema"));
const outputDirectory = resolve(argumentValue("--out"));

const loaded: { default?: unknown } = await import(pathToFileURL(schemaPath).href);
if (!isSchema(loaded.default)) {
  console.error(`${schemaPath}: o export default deve ser um Schema (use defineSchema)`);
  process.exit(1);
}

mkdirSync(outputDirectory, { recursive: true });
writeFileSync(join(outputDirectory, "schema.generated.hpp"), emitHeader(loaded.default));
writeFileSync(join(outputDirectory, "schema_registry.generated.cpp"), emitRegistry(loaded.default));
