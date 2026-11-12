import { Bridge } from "./bridge.js";
import { Engine, invokeScript, type EngineConfig } from "./engine.js";
import { ContractError, SchemaMismatchError } from "./errors.js";
import type { OlyModule, OlyModuleFactory } from "./module.js";
import type { ActionKind, ActionType, EventType, Schema, SchemaType, State } from "./schema.js";
import { World } from "./world.js";

interface RuntimeInternals {
  bridgeOf(runtime: Runtime): Bridge;
  moduleOf(runtime: Runtime): OlyModule;
  componentIdOf(runtime: Runtime, type: SchemaType): number;
  eventIndexOf(runtime: Runtime, type: EventType): number;
  actionIndexOf(runtime: Runtime, action: ActionType): number;
  stateIndexOf(runtime: Runtime, state: State): number;
  stateAt(runtime: Runtime, index: number): State | null;
}

let runtimeInternals!: RuntimeInternals;

function hex(value: number): string {
  return `0x${(value >>> 0).toString(16)}`;
}

function firstDivergentEvent(module: OlyModule, schema: Schema): string | null {
  const count = Math.min(module._olySchemaEventCount(), schema.events.length);
  for (let index = 0; index < count; ++index) {
    const type = schema.events[index]!;
    if (module._olySchemaEventSize(index) !== type.layout.size || module._olySchemaEventAlignment(index) !== type.layout.alignment) {
      return type.name;
    }
  }

  return null;
}

const actionKindCodes: readonly ActionKind[] = ["button", "axis2d", "pointer"];

function firstDivergentAction(module: OlyModule, schema: Schema): string | null {
  const count = Math.min(module._olySchemaActionCount(), schema.actions.length);
  for (let index = 0; index < count; ++index) {
    const action = schema.actions[index]!;
    if (actionKindCodes.indexOf(action.actionKind) !== module._olySchemaActionKind(index)) {
      return action.name;
    }
  }

  return null;
}

function firstDivergentType(module: OlyModule, schema: Schema): string | null {
  return firstDivergentComponent(module, schema) ?? firstDivergentEvent(module, schema) ?? firstDivergentAction(module, schema);
}

function firstDivergentComponent(module: OlyModule, schema: Schema): string | null {
  const count = Math.min(module._olySchemaComponentCount(), schema.types.length);
  for (let index = 0; index < count; ++index) {
    const type = schema.types[index]!;
    const componentId = module._olySchemaComponentId(index);
    const expectedSize = type.kind === "component" ? type.layout.size : 0;
    const expectedAlignment = type.kind === "component" ? type.layout.alignment : 1;
    if (module._olyComponentSize(componentId) !== expectedSize || module._olyComponentAlignment(componentId) !== expectedAlignment) {
      return type.name;
    }
  }

  return null;
}

export class Runtime {
  readonly schema: Schema;

  readonly #module: OlyModule;
  readonly #bridge: Bridge;
  readonly #componentIds: number[];
  readonly #engines = new Map<number, Engine>();

  static {
    runtimeInternals = {
      bridgeOf: (runtime) => runtime.#bridge,
      moduleOf: (runtime) => runtime.#module,
      componentIdOf: (runtime, type) => {
        if (runtime.schema.types[type.schemaIndex] !== type) {
          throw new ContractError(`type=${type.name}: tipo não pertence ao esquema deste runtime`);
        }

        return runtime.#componentIds[type.schemaIndex]!;
      },
      eventIndexOf: (runtime, type) => {
        if (runtime.schema.events[type.schemaIndex] !== type) {
          throw new ContractError(`event=${type.name}: tipo não pertence ao esquema deste runtime`);
        }

        // O WASM declara os eventos do esquema na ordem dele: o índice no esquema é o EventTypeId.
        return type.schemaIndex;
      },
      actionIndexOf: (runtime, action) => {
        if (runtime.schema.actions[action.schemaIndex] !== action) {
          throw new ContractError(`action=${action.name}: ação não pertence ao esquema deste runtime`);
        }

        // O WASM declara as ações do esquema na ordem dele: o índice no esquema é o ActionId.
        return action.schemaIndex;
      },
      stateIndexOf: (runtime, state) => {
        if (runtime.schema.states[state.schemaIndex] !== state) {
          throw new ContractError(`state=${state.name}: estado não pertence ao esquema deste runtime`);
        }

        // O WASM declara os estados do esquema na ordem dele: o índice no esquema é o StateId.
        return state.schemaIndex;
      },
      stateAt: (runtime, index) => runtime.schema.states[index] ?? null,
    };
  }

  constructor(module: OlyModule, schema: Schema) {
    this.#module = module;
    this.schema = schema;
    this.#bridge = new Bridge(module, schema);
    this.#componentIds = schema.types.map((_, index) => module._olySchemaComponentId(index));

    module.olyInvokeScript = (engineHandle, kind, id, events, eventCount) => {
      const engine = this.#engines.get(engineHandle);
      if (engine === undefined) {
        throw new ContractError(`olyInvokeScript engine=${engineHandle}: Engine desconhecida ou já liberada`);
      }

      invokeScript(engine, kind, id, events, eventCount);
    };
  }

  createEngine(config: EngineConfig = {}): Engine {
    const fixedHz = config.fixedHz ?? 60;
    const maxStepsPerFrame = config.maxStepsPerFrame ?? 5;
    const maxEventRounds = config.maxEventRounds ?? 16;
    if (!Number.isFinite(fixedHz) || fixedHz <= 0) {
      throw new ContractError(`createEngine fixedHz=${fixedHz}: deve ser um número positivo`);
    }
    if (!Number.isInteger(maxStepsPerFrame) || maxStepsPerFrame < 1) {
      throw new ContractError(`createEngine maxStepsPerFrame=${maxStepsPerFrame}: deve ser um inteiro positivo`);
    }

    if (!Number.isInteger(maxEventRounds) || maxEventRounds < 1) {
      throw new ContractError(`createEngine maxEventRounds=${maxEventRounds}: deve ser um inteiro positivo`);
    }

    const handle = this.#bridge.engineCreate(fixedHz, maxStepsPerFrame, maxEventRounds);
    const world = new World(this, this.#bridge.engineWorld(handle), false);
    const engine = new Engine(this, handle, world, () => {
      this.#engines.delete(handle);
    });
    this.#engines.set(handle, engine);

    return engine;
  }

  createWorld(): World {
    return new World(this, this.#bridge.worldCreate());
  }
}

export function bridgeOf(runtime: Runtime): Bridge {
  return runtimeInternals.bridgeOf(runtime);
}

export function moduleOf(runtime: Runtime): OlyModule {
  return runtimeInternals.moduleOf(runtime);
}

export function componentIdOf(runtime: Runtime, type: SchemaType): number {
  return runtimeInternals.componentIdOf(runtime, type);
}

export function eventIndexOf(runtime: Runtime, type: EventType): number {
  return runtimeInternals.eventIndexOf(runtime, type);
}

export function actionIndexOf(runtime: Runtime, action: ActionType): number {
  return runtimeInternals.actionIndexOf(runtime, action);
}

export function stateIndexOf(runtime: Runtime, state: State): number {
  return runtimeInternals.stateIndexOf(runtime, state);
}

/** `null` para o estado vazio (0xFFFFFFFF), que nenhum esquema declara. */
export function stateAt(runtime: Runtime, index: number): State | null {
  return runtimeInternals.stateAt(runtime, index);
}

/**
 * Carrega o módulo WASM e confere que ele foi compilado com `schema`. Rejeita com SchemaMismatchError se o
 * hash, o nº de tipos ou o tamanho/alinhamento de algum tipo diferirem.
 */
export async function loadRuntime(factory: OlyModuleFactory, schema: Schema): Promise<Runtime> {
  const module = await factory();

  const wasmHash = module._olySchemaHash() >>> 0;
  const divergent = firstDivergentType(module, schema);
  const countMatches =
    module._olySchemaComponentCount() === schema.types.length && module._olySchemaEventCount() === schema.events.length &&
    module._olySchemaActionCount() === schema.actions.length && module._olySchemaStateCount() === schema.states.length;
  if (wasmHash !== schema.hash || !countMatches || divergent !== null) {
    throw new SchemaMismatchError(
      `esquema do WASM (${hex(wasmHash)}) difere do esquema TS (${hex(schema.hash)}); ` +
        `tipos no WASM: ${module._olySchemaComponentCount()}, no TS: ${schema.types.length}; ` +
        `eventos no WASM: ${module._olySchemaEventCount()}, no TS: ${schema.events.length}; ` +
        `ações no WASM: ${module._olySchemaActionCount()}, no TS: ${schema.actions.length}; ` +
        `estados no WASM: ${module._olySchemaStateCount()}, no TS: ${schema.states.length}; ` +
        `primeiro tipo, evento ou ação divergente: ${divergent ?? "nenhum por tamanho (campos ou ordem mudaram)"}; recompile o WASM`,
    );
  }

  return new Runtime(module, schema);
}
