// Oly's Emscripten module: C API exports (prefix `_`) and the heap. Pointers and handles are `number`.
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
  _olySchemaActionCount(): number;
  _olySchemaActionKind(actionIndex: number): number;
  _olySchemaStateCount(): number;
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
  _olyWebHostInstallScriptInvoker(engine: number): void;
  _olyEngineEmit(engine: number, eventType: number, value: number): void;
  _olyEngineAddNativeHandler(engine: number, eventType: number, name: number): number;
  _olyEngineAddScriptHandler(engine: number, eventType: number, name: number): number;
  _olyEngineScheduleScriptTask(engine: number, delaySeconds: number, intervalSeconds: number, scriptId: number, out: number): void;
  _olyEngineCancelTask(engine: number, slot: number, generation: number): number;
  _olyEngineIsTaskPending(engine: number, slot: number, generation: number): number;
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
  _olyEngineRequestState(engine: number, state: number): void;
  _olyEngineState(engine: number): number;
  _olyEngineSetSystemStates(engine: number, systemId: number, stateMask: number): void;
  _olyEngineSetHandlerStates(engine: number, handlerId: number, stateMask: number): void;
  _olyEngineAddNativeStateHook(engine: number, when: number, state: number, name: number): number;
  _olyEngineAddScriptStateHook(engine: number, when: number, state: number, name: number): number;
  _olyDirectSpawn(engine: number, out: number): void;
  _olyContextPersist(engine: number, index: number, generation: number): void;
  _olyEnginePersistTask(engine: number, slot: number, generation: number): number;
  _olyEngineBindButton(engine: number, action: number, button: number): void;
  _olyEngineBindAxis2d(engine: number, action: number, up: number, down: number, left: number, right: number): void;
  _olyEngineClearBindings(engine: number, action: number): void;
  _olyEngineIsBound(engine: number, button: number): number;
  _olyEngineButtonDown(engine: number, button: number): void;
  _olyEngineButtonUp(engine: number, button: number): void;
  _olyEnginePointerMove(engine: number, x: number, y: number): void;
  _olyEngineReleaseAll(engine: number): void;
  _olyEngineActionSnapshot(engine: number): number;
  _olySystemSpawn(engine: number, out: number): void;
  _olySystemDestroy(engine: number, index: number, generation: number): void;
  _olySystemAdd(engine: number, index: number, generation: number, componentId: number, value: number): void;
  _olySystemRemove(engine: number, index: number, generation: number, componentId: number): void;

  // Called by the web host's script invoker (olyWebHostInstallScriptInvoker): the Runtime installs it once and dispatches by engine.
  olyInvokeScript?: (engine: number, kind: number, id: number, events: number, eventCount: number) => void;
}

export type OlyModuleFactory = () => Promise<OlyModule>;
