import { Bridge } from "./bridge.js";
import { Engine, invokeSystem, type EngineConfig } from "./engine.js";
import { ContractError, SchemaMismatchError } from "./errors.js";
import type { OlyModule, OlyModuleFactory } from "./module.js";
import type { Schema, SchemaType } from "./schema.js";
import { World } from "./world.js";

interface RuntimeInternals {
  bridgeOf(runtime: Runtime): Bridge;
  moduleOf(runtime: Runtime): OlyModule;
  componentIdOf(runtime: Runtime, type: SchemaType): number;
}

let runtimeInternals!: RuntimeInternals;

function hex(value: number): string {
  return `0x${(value >>> 0).toString(16)}`;
}

function firstDivergentType(module: OlyModule, schema: Schema): string | null {
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
    };
  }

  constructor(module: OlyModule, schema: Schema) {
    this.#module = module;
    this.schema = schema;
    this.#bridge = new Bridge(module, schema);
    this.#componentIds = schema.types.map((_, index) => module._olySchemaComponentId(index));

    module.olyInvokeScript = (engineHandle, systemId) => {
      const engine = this.#engines.get(engineHandle);
      if (engine === undefined) {
        throw new ContractError(`olyInvokeScript engine=${engineHandle}: Engine desconhecida ou já liberada`);
      }

      invokeSystem(engine, systemId);
    };
  }

  createEngine(config: EngineConfig = {}): Engine {
    const fixedHz = config.fixedHz ?? 60;
    const maxStepsPerFrame = config.maxStepsPerFrame ?? 5;
    if (!Number.isFinite(fixedHz) || fixedHz <= 0) {
      throw new ContractError(`createEngine fixedHz=${fixedHz}: deve ser um número positivo`);
    }
    if (!Number.isInteger(maxStepsPerFrame) || maxStepsPerFrame < 1) {
      throw new ContractError(`createEngine maxStepsPerFrame=${maxStepsPerFrame}: deve ser um inteiro positivo`);
    }

    const handle = this.#bridge.engineCreate(fixedHz, maxStepsPerFrame);
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

/**
 * Carrega o módulo WASM e confere que ele foi compilado com `schema`. Rejeita com SchemaMismatchError se o
 * hash, o nº de tipos ou o tamanho/alinhamento de algum tipo diferirem.
 */
export async function loadRuntime(factory: OlyModuleFactory, schema: Schema): Promise<Runtime> {
  const module = await factory();

  const wasmHash = module._olySchemaHash() >>> 0;
  const divergent = firstDivergentType(module, schema);
  const countMatches = module._olySchemaComponentCount() === schema.types.length;
  if (wasmHash !== schema.hash || !countMatches || divergent !== null) {
    throw new SchemaMismatchError(
      `esquema do WASM (${hex(wasmHash)}) difere do esquema TS (${hex(schema.hash)}); ` +
        `tipos no WASM: ${module._olySchemaComponentCount()}, no TS: ${schema.types.length}; ` +
        `primeiro tipo divergente: ${divergent ?? "nenhum por tamanho (campos ou ordem mudaram)"}; recompile o WASM`,
    );
  }

  return new Runtime(module, schema);
}
