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

test("public exports match api/public-exports.txt", () => {
  const expected = readFileSync(publicExportsPath, "utf8").split(/\r?\n/).filter((line) => line !== "");

  assert.deepEqual(exported.map((symbol) => symbol.name).sort(), expected);
});

test("World exposes only the glossary API", () => {
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

test("Query exposes only eachBlock, single and trySingle", () => {
  assert.deepEqual(memberNames("Query"), ["eachBlock", "single", "trySingle"]);
});

test("Block exposes only count, entity, read, write", () => {
  assert.deepEqual(memberNames("Block"), ["count", "entity", "read", "write"]);
});

test("Runtime exposes only schema, createWorld and createEngine", () => {
  assert.deepEqual(memberNames("Runtime"), ["createEngine", "createWorld", "schema"]);
});

test("Engine exposes only the glossary API", () => {
  assert.deepEqual(memberNames("Engine"), [
    "actions",
    "addNativeHandler",
    "addNativeOnEnter",
    "addNativeOnExit",
    "addNativeSystem",
    "addSystem",
    "dispose",
    "fixedDeltaTime",
    "frame",
    "on",
    "onEnter",
    "onExit",
    "pause",
    "paused",
    "requestState",
    "resume",
    "schedule",
    "setHidden",
    "state",
    "time",
    "world",
  ]);
});

test("SystemContext exposes only the glossary API", () => {
  assert.deepEqual(memberNames("SystemContext"), [
    "actions",
    "add",
    "destroy",
    "emit",
    "fixedDeltaTime",
    "get",
    "has",
    "interpolation",
    "isAlive",
    "name",
    "pause",
    "paused",
    "persist",
    "phase",
    "query",
    "remove",
    "requestState",
    "resume",
    "schedule",
    "set",
    "spawn",
    "state",
    "time",
  ]);
});

test("HandlerContext exposes only the glossary API", () => {
  assert.deepEqual(memberNames("HandlerContext"), [
    "actions",
    "add",
    "destroy",
    "emit",
    "eventType",
    "fixedDeltaTime",
    "get",
    "has",
    "isAlive",
    "name",
    "pause",
    "paused",
    "persist",
    "query",
    "remove",
    "requestState",
    "resume",
    "schedule",
    "set",
    "spawn",
    "state",
    "time",
  ]);
});

test("TaskContext exposes only the glossary API", () => {
  assert.deepEqual(memberNames("TaskContext"), [
    "actions",
    "add",
    "destroy",
    "emit",
    "fixedDeltaTime",
    "get",
    "handle",
    "has",
    "isAlive",
    "lastRun",
    "pause",
    "paused",
    "persist",
    "query",
    "remove",
    "requestState",
    "resume",
    "schedule",
    "set",
    "spawn",
    "state",
    "time",
  ]);
});

test("StateContext exposes only the glossary API", () => {
  assert.deepEqual(memberNames("StateContext"), [
    "actions",
    "add",
    "destroy",
    "emit",
    "fixedDeltaTime",
    "fromState",
    "get",
    "has",
    "isAlive",
    "pause",
    "paused",
    "persist",
    "query",
    "remove",
    "requestState",
    "resume",
    "schedule",
    "set",
    "spawn",
    "state",
    "time",
    "toState",
  ]);
});

test("Scheduler exposes only after, every, cancel, isPending and persist", () => {
  assert.deepEqual(memberNames("Scheduler"), ["after", "cancel", "every", "isPending", "persist"]);
});

test("ActionMap exposes only binding, host input and reading", () => {
  assert.deepEqual(memberNames("ActionMap"), [
    "axis2d",
    "bind",
    "button",
    "buttonDown",
    "buttonUp",
    "clearBindings",
    "isBound",
    "pointer",
    "pointerMove",
    "releaseAll",
  ]);
});

test("EventBatch exposes only type, count and read", () => {
  assert.deepEqual(memberNames("EventBatch"), ["count", "read", "type"]);
});

test("ContractError has name ContractError", () => {
  assert.equal(new ContractError("x").name, "ContractError");
});

test("SingleRow exposes only entity and get", () => {
  assert.deepEqual(memberNames("SingleRow"), ["entity", "get"]);
});
