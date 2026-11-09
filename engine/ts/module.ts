// Módulo Emscripten da engine: exports da API C (prefixo `_`) e o heap. Ponteiros e handles são `number`.
export interface EngineModule {
  HEAPU8: Uint8Array;
  _malloc(bytes: number): number;
  _free(pointer: number): void;

  _engineWorldCreate(): number;
  _engineWorldDestroy(world: number): void;
  _engineSchemaHash(): number;
  _engineSchemaComponentCount(): number;
  _engineSchemaComponentId(schemaIndex: number): number;
  _engineComponentSize(componentId: number): number;
  _engineComponentAlignment(componentId: number): number;
  _engineCreate(world: number, out: number): void;
  _engineDestroy(world: number, index: number, generation: number): void;
  _engineIsAlive(world: number, index: number, generation: number): number;
  _engineAdd(world: number, index: number, generation: number, componentId: number, value: number): void;
  _engineRemove(world: number, index: number, generation: number, componentId: number): void;
  _engineHas(world: number, index: number, generation: number, componentId: number): number;
  _engineGet(world: number, index: number, generation: number, componentId: number): number;
  _engineQuery(
    world: number,
    componentIds: number,
    readCount: number,
    writeCount: number,
    withCount: number,
    withoutCount: number,
  ): number;
  _engineQueryBegin(world: number, query: number, outDescriptors: number): number;
  _engineQueryEnd(world: number, query: number): void;
  _engineCheckInvariants(world: number, message: number, capacity: number): number;
}

export type EngineModuleFactory = () => Promise<EngineModule>;
