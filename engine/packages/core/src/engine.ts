import { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import { setContextActive } from "./context.js";
import { ContractError } from "./errors.js";
import { EventBatch, endBatch } from "./event_batch.js";
import { HandlerContext, type HandlerFunction } from "./handler_context.js";
import { bridgeOf, eventIndexOf, stateAt, stateIndexOf, type Runtime } from "./runtime.js";
import { discardScriptTask, invokeScriptTask, Scheduler } from "./scheduler.js";
import type { EventType, Fields, State } from "./schema.js";
import { setStateContextSides, StateContext, type StateHookFunction } from "./state_context.js";
import { stateMaskOf } from "./state_mask.js";
import { SystemContext, type SystemFunction } from "./system_context.js";
import { releaseOwnedWorld, setStructureLocked, type World } from "./world.js";

export const Phase = { UpdateBefore: 0, Update: 1, UpdateAfter: 2, Render: 3 } as const;
export type Phase = (typeof Phase)[keyof typeof Phase];

export interface EngineConfig {
  fixedHz?: number;
  maxStepsPerFrame?: number;
  maxEventRounds?: number;
}

export interface HandlerRegistration {
  readonly id: number;
  /**
   * Makes the handler run only in the given states, checked every round; an empty list removes the restriction. Throws
   * ContractError during a frame, after an interrupted frame and for a state from another schema.
   */
  inState(...states: State[]): HandlerRegistration;
}

export interface SystemRegistration {
  readonly id: number;
  /**
   * Makes the system run only while the game is paused: when paused, the game phases run only the
   * whilePaused systems; when not paused, those do not run. Throws ContractError for a system in the Render phase.
   */
  whilePaused(): SystemRegistration;
  /** Like `HandlerRegistration.inState`, for the system; applies in every phase, including render. */
  inState(...states: State[]): SystemRegistration;
}

const InvalidSystem = 0xffffffff;
const InvalidHandler = 0xffffffff;
const HandlerTypeMismatch = 0xfffffffe;
const ScriptHandler = 1;
const ScriptTask = 2;
const ScriptTaskDiscard = 3;
const ScriptStateHook = 4;
const InvalidStateHook = 0xffffffff;
const StateEnter = 0;
const StateExit = 1;

interface ScriptSystem {
  readonly function: SystemFunction;
  readonly context: SystemContext;
}

interface ScriptHandlerEntry {
  readonly function: HandlerFunction;
  readonly context: HandlerContext;
  readonly type: EventType;
}

interface ScriptStateHookEntry {
  readonly function: StateHookFunction;
  readonly context: StateContext;
  readonly entering: boolean;
}

let engineHandleOfInternal!: (engine: Engine) => number;
let invokeScriptInternal!: (engine: Engine, kind: number, id: number, events: number, eventCount: number) => void;

// Loop rules live in the C++ core; here only usage validation and the callback into TS systems.
export class Engine {
  readonly world: World;

  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #actions: ActionMap;
  readonly #scheduler: Scheduler;
  readonly #handle: number;
  readonly #onDispose: () => void;
  readonly #scriptSystems = new Map<number, ScriptSystem>();
  readonly #scriptHandlers = new Map<number, ScriptHandlerEntry>();
  readonly #scriptStateHooks = new Map<number, ScriptStateHookEntry>();
  #inFrame = false;
  // An exception unwinds the WASM without destructors: the C++ core stays "in frame" until the next frame recovers.
  #interrupted = false;
  #disposed = false;
  #lastFrameTime = -Infinity;

  static {
    engineHandleOfInternal = (engine) => engine.#handle;
    invokeScriptInternal = (engine, kind, id, events, eventCount) => {
      if (kind === ScriptHandler) {
        engine.#invokeHandler(id, events, eventCount);
      } else if (kind === ScriptTask) {
        invokeScriptTask(engine.#scheduler, id, eventCount !== 0);
      } else if (kind === ScriptTaskDiscard) {
        discardScriptTask(engine.#scheduler, id);
      } else if (kind === ScriptStateHook) {
        engine.#invokeStateHook(id, eventCount);
      } else {
        engine.#invoke(id);
      }
    };
  }

  constructor(runtime: Runtime, handle: number, world: World, onDispose: () => void) {
    this.#runtime = runtime;
    this.#bridge = bridgeOf(runtime);
    this.#handle = handle;
    this.world = world;
    this.#onDispose = onDispose;
    this.#actions = new ActionMap(runtime, this.#bridge, handle, this.#bridge.engineActionSnapshot(handle), (operation) => {
      this.#assertUsable(operation);
    });
    this.#scheduler = new Scheduler(runtime, this.#bridge, handle, world, this.#actions, (operation) => {
      this.#assertUsable(operation);
    });
  }

  /** Bindings, host input and reading of the schema actions. The same object as `context.actions`. */
  get actions(): ActionMap {
    this.#assertUsable("actions");

    return this.#actions;
  }

  /** Tasks on the game clock. The same object as `context.schedule`. */
  get schedule(): Scheduler {
    this.#assertUsable("schedule");

    return this.#scheduler;
  }

  /**
   * Advances the fixed-step clock up to `nowSeconds` and runs the phases. Throws ContractError if called from inside
   * a system or with a time that is not finite or is less than the previous frame's. During the frame, direct structural
   * changes on the `world` are an error; use the SystemContext ones. An exception from a TS system, handler, task or
   * state hook propagates from here; until the next frame, registering a system, handler or hook and calling `inState` is
   * ContractError, and that frame discards the pending commands, events and state request (a `requestState`
   * made in that interval is lost too).
   */
  frame(nowSeconds: number): void {
    this.#assertUsable("frame");
    if (this.#inFrame) {
      throw new ContractError("frame: called from inside a system or handler (frame is not reentrant)");
    }
    if (!Number.isFinite(nowSeconds)) {
      throw new ContractError(`frame: time=${nowSeconds} is not a finite number`);
    }
    if (nowSeconds < this.#lastFrameTime) {
      throw new ContractError(`frame: time=${nowSeconds} less than the previous frame's (${this.#lastFrameTime})`);
    }

    this.#lastFrameTime = nowSeconds;
    this.#inFrame = true;
    setStructureLocked(this.world, true);
    try {
      this.#bridge.engineFrame(this.#handle, nowSeconds);
      this.#interrupted = false;
    } catch (error) {
      this.#interrupted = true;
      throw error;
    } finally {
      setStructureLocked(this.world, false);
      this.#inFrame = false;
    }
  }

  setHidden(hidden: boolean): void {
    this.#assertUsable("setHidden");

    this.#bridge.engineSetHidden(this.#handle, hidden);
  }

  pause(): void {
    this.#assertUsable("pause");

    this.#bridge.enginePause(this.#handle);
  }

  resume(): void {
    this.#assertUsable("resume");

    this.#bridge.engineResume(this.#handle);
  }

  /**
   * The last request before step 5 wins (inside or outside `frame`); the change happens there, paused or not: `onExit`,
   * scope cleanup, `onEnter`. A request made in a hook takes effect in the next step. Throws ContractError for a state
   * from another schema.
   */
  requestState(state: State): void {
    this.#assertUsable("requestState");

    this.#bridge.engineRequestState(this.#handle, stateIndexOf(this.#runtime, state));
  }

  /** `null` until the first change. Only changes in step 5. */
  get state(): State | null {
    this.#assertUsable("state");

    return stateAt(this.#runtime, this.#bridge.engineState(this.#handle));
  }

  /** Hooks of the same state run in registration order, C++ and TS interleaved. Throws ContractError during a frame. */
  onEnter(state: State, name: string, hook: StateHookFunction): void {
    this.#addStateHook("onEnter", StateEnter, state, name, hook);
  }

  onExit(state: State, name: string, hook: StateHookFunction): void {
    this.#addStateHook("onExit", StateExit, state, name, hook);
  }

  /** Registers a C++ hook declared with OLY_STATE_HOOK. Throws ContractError if the name does not exist. */
  addNativeOnEnter(state: State, name: string): void {
    this.#addNativeStateHook("addNativeOnEnter", StateEnter, state, name);
  }

  addNativeOnExit(state: State, name: string): void {
    this.#addNativeStateHook("addNativeOnExit", StateExit, state, name);
  }

  get paused(): boolean {
    this.#assertUsable("paused");

    return this.#bridge.engineIsPaused(this.#handle);
  }

  get time(): number {
    this.#assertUsable("time");

    return this.#bridge.engineTime(this.#handle);
  }

  get fixedDeltaTime(): number {
    this.#assertUsable("fixedDeltaTime");

    return this.#bridge.engineFixedDeltaTime(this.#handle);
  }

  addSystem(phase: Phase, name: string, system: SystemFunction): SystemRegistration {
    this.#assertCanRegister("addSystem", phase, name);

    const id = this.#bridge.addScriptSystem(this.#handle, phase, name);
    const context = new SystemContext(phase, name, this.#runtime, this.#bridge, this.#handle, this.world, this.#actions, this.#scheduler);
    this.#scriptSystems.set(id, { function: system, context });

    return this.#registration(id, phase, name);
  }

  addNativeSystem(phase: Phase, name: string): SystemRegistration {
    this.#assertCanRegister("addNativeSystem", phase, name);

    const id = this.#bridge.addNativeSystem(this.#handle, phase, name);
    if (id === InvalidSystem) {
      throw new ContractError(`addNativeSystem name=${name}: no native system with that name (declare it with OLY_SYSTEM)`);
    }

    return this.#registration(id, phase, name);
  }

  /**
   * Registers a TS handler: called once per batch of `type` in each round, in registration order. Throws
   * ContractError during a frame.
   */
  on<F extends Fields>(type: EventType<string, F>, name: string, handler: HandlerFunction<F>): HandlerRegistration {
    const eventType = this.#assertCanRegisterHandler("on", type, name);

    const id = this.#bridge.addScriptHandler(this.#handle, eventType, name);
    const context = new HandlerContext(type, name, this.#runtime, this.#bridge, this.#handle, this.world, this.#actions, this.#scheduler);
    this.#scriptHandlers.set(id, { function: handler as unknown as HandlerFunction, context, type });

    return this.#handlerRegistration(id, name);
  }

  addNativeHandler(type: EventType, name: string): HandlerRegistration {
    const eventType = this.#assertCanRegisterHandler("addNativeHandler", type, name);

    const id = this.#bridge.addNativeHandler(this.#handle, eventType, name);
    if (id === InvalidHandler) {
      throw new ContractError(`addNativeHandler name=${name}: no native handler with that name (declare it with OLY_HANDLER)`);
    }
    if (id === HandlerTypeMismatch) {
      throw new ContractError(`addNativeHandler name=${name} event=${type.name}: the native handler is for another event type`);
    }

    return this.#handlerRegistration(id, name);
  }

  dispose(): void {
    if (this.#disposed) {
      return;
    }
    if (this.#inFrame) {
      throw new ContractError("dispose: Engine in frame; dispose after the frame");
    }

    this.#disposed = true;
    releaseOwnedWorld(this.world);
    this.#bridge.engineDestroy(this.#handle);
    this.#onDispose();
  }

  #registration(id: number, phase: Phase, name: string): SystemRegistration {
    const registration: SystemRegistration = {
      id,
      whilePaused: () => {
        this.#assertUsable("whilePaused");
        if (phase === Phase.Render) {
          throw new ContractError(`whilePaused system=${name}: not valid for a system in the render phase`);
        }

        this.#bridge.setWhilePaused(this.#handle, id, true);

        return registration;
      },
      inState: (...states) => {
        this.#restrictToStates("inState", `system=${name}`, states, (mask) => {
          this.#bridge.engineSetSystemStates(this.#handle, id, mask);
        });

        return registration;
      },
    };

    return registration;
  }

  #handlerRegistration(id: number, name: string): HandlerRegistration {
    const registration: HandlerRegistration = {
      id,
      inState: (...states) => {
        this.#restrictToStates("inState", `handler=${name}`, states, (mask) => {
          this.#bridge.engineSetHandlerStates(this.#handle, id, mask);
        });

        return registration;
      },
    };

    return registration;
  }

  #restrictToStates(operation: string, subject: string, states: readonly State[], apply: (mask: number) => void): void {
    this.#assertUsable(operation);
    if (this.#inFrame) {
      throw new ContractError(`${operation} ${subject}: state restriction during frame`);
    }
    this.#assertNotInterrupted(operation, subject);

    apply(stateMaskOf(states.map((state) => stateIndexOf(this.#runtime, state))));
  }

  #addStateHook(operation: string, when: number, state: State, name: string, hook: StateHookFunction): void {
    const index = this.#assertCanRegisterStateHook(operation, state, name);

    const id = this.#bridge.engineAddScriptStateHook(this.#handle, when, index, name);
    const context = new StateContext(name, this.#runtime, this.#bridge, this.#handle, this.world, this.#actions, this.#scheduler);
    this.#scriptStateHooks.set(id, { function: hook, context, entering: when === StateEnter });
  }

  #addNativeStateHook(operation: string, when: number, state: State, name: string): void {
    const index = this.#assertCanRegisterStateHook(operation, state, name);

    const id = this.#bridge.engineAddNativeStateHook(this.#handle, when, index, name);
    if (id === InvalidStateHook) {
      throw new ContractError(`${operation} name=${name}: no native state hook with that name (declare it with OLY_STATE_HOOK)`);
    }
  }

  #assertCanRegisterStateHook(operation: string, state: State, name: string): number {
    this.#assertUsable(operation);
    if (this.#inFrame) {
      throw new ContractError(`${operation} hook=${name}: registration during frame`);
    }
    this.#assertNotInterrupted(operation, `hook=${name}`);

    return stateIndexOf(this.#runtime, state);
  }

  #invoke(systemId: number): void {
    const entry = this.#scriptSystems.get(systemId);
    if (entry === undefined) {
      throw new ContractError(`system id=${systemId}: not a TS system of this Engine`);
    }

    // A previous C++ system may have grown the WASM memory.
    this.#bridge.sync();
    setContextActive(entry.context, true);
    try {
      entry.function(entry.context);
    } finally {
      setContextActive(entry.context, false);
    }
  }

  #invokeHandler(id: number, address: number, count: number): void {
    const entry = this.#scriptHandlers.get(id);
    if (entry === undefined) {
      throw new ContractError(`handler id=${id}: not a TS handler of this Engine`);
    }

    // A previous C++ handler or system may have grown the WASM memory.
    this.#bridge.sync();
    const batch = new EventBatch(entry.type, count, address, this.#bridge);
    setContextActive(entry.context, true);
    setStructureLocked(this.world, false);
    try {
      entry.function(entry.context, batch);
    } finally {
      endBatch(batch);
      setContextActive(entry.context, false);
      setStructureLocked(this.world, true);
    }
  }

  #invokeStateHook(id: number, otherSide: number): void {
    const entry = this.#scriptStateHooks.get(id);
    if (entry === undefined) {
      throw new ContractError(`hook id=${id}: not a TS state hook of this Engine`);
    }

    // A previous C++ hook, system or handler may have grown the WASM memory.
    this.#bridge.sync();
    const current = stateAt(this.#runtime, this.#bridge.engineState(this.#handle));
    const other = stateAt(this.#runtime, otherSide >>> 0);
    if (entry.entering) {
      setStateContextSides(entry.context, other, current);
    } else {
      setStateContextSides(entry.context, current, other);
    }
    setContextActive(entry.context, true);
    setStructureLocked(this.world, false);
    try {
      entry.function(entry.context);
    } finally {
      setContextActive(entry.context, false);
      setStructureLocked(this.world, true);
    }
  }

  #assertCanRegisterHandler(operation: string, type: EventType, name: string): number {
    this.#assertUsable(operation);
    if (this.#inFrame) {
      throw new ContractError(`${operation} handler=${name}: registration during frame`);
    }
    this.#assertNotInterrupted(operation, `handler=${name}`);

    return eventIndexOf(this.#runtime, type);
  }

  #assertNotInterrupted(operation: string, subject: string): void {
    if (this.#interrupted) {
      throw new ContractError(`${operation} ${subject}: the last frame was interrupted by an exception; call frame before registering`);
    }
  }

  #assertCanRegister(operation: string, phase: Phase, name: string): void {
    this.#assertUsable(operation);
    if (this.#inFrame) {
      throw new ContractError(`${operation} system=${name}: registration during frame`);
    }
    this.#assertNotInterrupted(operation, `system=${name}`);
    if (!Object.values(Phase).includes(phase)) {
      throw new ContractError(`${operation} system=${name}: invalid phase=${String(phase)}`);
    }
  }

  #assertUsable(operation: string): void {
    if (this.#disposed) {
      throw new ContractError(`${operation}: Engine already disposed`);
    }
  }
}

export function engineHandleOf(engine: Engine): number {
  return engineHandleOfInternal(engine);
}

export function invokeScript(engine: Engine, kind: number, id: number, events: number, eventCount: number): void {
  invokeScriptInternal(engine, kind, id, events, eventCount);
}
