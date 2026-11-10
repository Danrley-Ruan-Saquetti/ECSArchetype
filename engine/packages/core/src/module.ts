// Módulo Emscripten da Oly: exports da API C (prefixo `_`) e o heap. Ponteiros e handles são `number`.
export interface OlyModule {
  HEAPU8: Uint8Array;
  _malloc(bytes: number): number;
  _free(pointer: number): void;

  _olyWorldCreate(): number;
  _olyWorldDestroy(world: number): void;
  _olySchemaHash(): number;
  _olySchemaComponentCount(): number;
  _olySchemaComponentId(schemaIndex: number): number;
  _olyComponentSize(componentId: number): number;
  _olyComponentAlignment(componentId: number): number;
  _olyEntitySpawn(world: number, out: number): void;
  _olyEntityDestroy(world: number, index: number, generation: number): void;
  _olyEntityIsAlive(world: number, index: number, generation: number): number;
  _olyComponentAdd(world: number, index: number, generation: number, componentId: number, value: number): void;
  _olyComponentRemove(world: number, index: number, generation: number, componentId: number): void;
  _olyComponentHas(world: number, index: number, generation: number, componentId: number): number;
  _olyComponentGet(world: number, index: number, generation: number, componentId: number): number;
  _olyQueryCreate(
    world: number,
    componentIds: number,
    readCount: number,
    writeCount: number,
    withCount: number,
    withoutCount: number,
  ): number;
  _olyQueryBegin(world: number, query: number, outBlockCount: number, outDescriptors: number): number;
  _olyQueryEnd(world: number, iteration: number): void;
  _olyWorldCheckInvariants(world: number, message: number, capacity: number): number;
}

export type OlyModuleFactory = () => Promise<OlyModule>;
