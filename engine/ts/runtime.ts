import { Bridge } from "./bridge.js";
import { EngineContractError, SchemaMismatchError } from "./errors.js";
import type { EngineModule, EngineModuleFactory } from "./module.js";
import type { Schema, SchemaType } from "./schema.js";
import { World } from "./world.js";

function hex(value: number): string {
  return `0x${(value >>> 0).toString(16)}`;
}

function firstDivergentType(module: EngineModule, schema: Schema): string | null {
  const count = Math.min(module._engineSchemaComponentCount(), schema.types.length);
  for (let index = 0; index < count; ++index) {
    const type = schema.types[index]!;
    const componentId = module._engineSchemaComponentId(index);
    const expectedSize = type.kind === "component" ? type.layout.size : 0;
    const expectedAlignment = type.kind === "component" ? type.layout.alignment : 1;
    if (module._engineComponentSize(componentId) !== expectedSize || module._engineComponentAlignment(componentId) !== expectedAlignment) {
      return type.name;
    }
  }

  return null;
}

export class EngineRuntime {
  readonly schema: Schema;
  readonly module: EngineModule;
  readonly bridge: Bridge;

  private readonly componentIds: number[];

  constructor(module: EngineModule, schema: Schema) {
    this.module = module;
    this.schema = schema;
    this.bridge = new Bridge(module, schema);
    this.componentIds = schema.types.map((_, index) => module._engineSchemaComponentId(index));
  }

  componentId(type: SchemaType): number {
    if (this.schema.types[type.schemaIndex] !== type) {
      throw new EngineContractError(`type=${type.name}: tipo não pertence ao esquema deste runtime`);
    }

    return this.componentIds[type.schemaIndex]!;
  }

  createWorld(): World {
    return new World(this, this.bridge.worldCreate());
  }
}

export async function loadEngine(factory: EngineModuleFactory, schema: Schema): Promise<EngineRuntime> {
  const module = await factory();

  const wasmHash = module._engineSchemaHash() >>> 0;
  const divergent = firstDivergentType(module, schema);
  const countMatches = module._engineSchemaComponentCount() === schema.types.length;
  if (wasmHash !== schema.hash || !countMatches || divergent !== null) {
    throw new SchemaMismatchError(
      `esquema do WASM (${hex(wasmHash)}) difere do esquema TS (${hex(schema.hash)}); ` +
        `tipos no WASM: ${module._engineSchemaComponentCount()}, no TS: ${schema.types.length}; ` +
        `primeiro tipo divergente: ${divergent ?? "nenhum por tamanho (campos ou ordem mudaram)"}; recompile o WASM`,
    );
  }

  return new EngineRuntime(module, schema);
}
