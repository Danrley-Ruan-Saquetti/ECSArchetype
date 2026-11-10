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
  _olySchemaEventCount(): number;
  _olySchemaEventSize(eventIndex: number): number;
  _olySchemaEventAlignment(eventIndex: number): number;
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

  _olyEngineCreate(fixedHz: number, maxStepsPerFrame: number, maxEventRounds: number): number;
  _olyEngineDestroy(engine: number): void;
  _olyEngineWorld(engine: number): number;
  _olyEngineFrame(engine: number, nowSeconds: number): void;
  _olyEngineSetHidden(engine: number, hidden: number): void;
  _olyEnginePause(engine: number): void;
  _olyEngineResume(engine: number): void;
  _olyEngineIsPaused(engine: number): number;
  _olyEngineTime(engine: number): number;
  _olyEngineFixedDeltaTime(engine: number): number;
  _olyEngineInterpolation(engine: number): number;
  _olyEngineAddNativeSystem(engine: number, phase: number, name: number): number;
  _olyEngineAddScriptSystem(engine: number, phase: number, name: number): number;
  _olyEngineSetWhilePaused(engine: number, systemId: number, whilePaused: number): void;
  _olySystemSpawn(engine: number, out: number): void;
  _olySystemDestroy(engine: number, index: number, generation: number): void;
  _olySystemAdd(engine: number, index: number, generation: number, componentId: number, value: number): void;
  _olySystemRemove(engine: number, index: number, generation: number, componentId: number): void;

  // Chamada de volta do núcleo (olyHostInvokeScript): o Runtime instala uma vez e despacha por engine.
  olyInvokeScript?: (engine: number, kind: number, id: number, events: number, eventCount: number) => void;
}

export type OlyModuleFactory = () => Promise<OlyModule>;
