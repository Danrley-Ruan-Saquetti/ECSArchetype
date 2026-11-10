import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { test } from "node:test";
import { fileURLToPath } from "node:url";

import ts from "typescript";

import { ContractError } from "@oly/core";

const declarationsEntry = fileURLToPath(new URL("../../dist/index.d.ts", import.meta.url));
const publicExportsPath = fileURLToPath(new URL("../../api/public-exports.txt", import.meta.url));

const program = ts.createProgram([declarationsEntry], {
  target: ts.ScriptTarget.ES2022,
  module: ts.ModuleKind.NodeNext,
  moduleResolution: ts.ModuleResolutionKind.NodeNext,
  skipLibCheck: true,
  noEmit: true,
  types: [],
});
const checker = program.getTypeChecker();
const entrySymbol = checker.getSymbolAtLocation(program.getSourceFile(declarationsEntry)!)!;
const exported = checker.getExportsOfModule(entrySymbol);

function memberNames(name: string): string[] {
  const symbol = exported.find((candidate) => candidate.name === name)!;
  const resolved = symbol.flags & ts.SymbolFlags.Alias ? checker.getAliasedSymbol(symbol) : symbol;

  return checker
    .getPropertiesOfType(checker.getDeclaredTypeOfSymbol(resolved))
    .map((property) => property.name)
    .filter((propertyName) => !propertyName.startsWith("__#") && !propertyName.startsWith("#"))
    .sort();
}

test("exports públicos batem com api/public-exports.txt", () => {
  const expected = readFileSync(publicExportsPath, "utf8").split("\n").filter((line) => line !== "");

  assert.deepEqual(exported.map((symbol) => symbol.name).sort(), expected);
});

test("World expõe só a API do glossário", () => {
  assert.deepEqual(memberNames("World"), [
    "add",
    "checkInvariants",
    "destroy",
    "dispose",
    "get",
    "handle",
    "has",
    "isAlive",
    "query",
    "remove",
    "set",
    "spawn",
  ]);
});

test("Query expõe só eachBlock", () => {
  assert.deepEqual(memberNames("Query"), ["eachBlock"]);
});

test("Block expõe só count, entity, read, write", () => {
  assert.deepEqual(memberNames("Block"), ["count", "entity", "read", "write"]);
});

test("Runtime expõe só schema e createWorld", () => {
  assert.deepEqual(memberNames("Runtime"), ["createWorld", "schema"]);
});

test("ContractError tem name ContractError", () => {
  assert.equal(new ContractError("x").name, "ContractError");
});
