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
   * Faz o handler rodar só nos estados dados, conferidos a cada rodada; lista vazia tira a restrição. Lança
   * ContractError durante frame, depois de um frame interrompido e para estado de outro esquema.
   */
  inState(...states: State[]): HandlerRegistration;
}

export interface SystemRegistration {
  readonly id: number;
  /**
   * Faz o sistema rodar só com o jogo pausado: pausado, as fases de jogo rodam apenas os sistemas
   * whilePaused; sem pausa, eles não rodam. Lança ContractError para sistema da fase Render.
   */
  whilePaused(): SystemRegistration;
  /** Como `HandlerRegistration.inState`, para o sistema; vale em todas as fases, inclusive render. */
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

// Regras do loop vivem no núcleo C++; aqui só validação de uso e a chamada de volta dos sistemas TS.
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
  // Uma exceção desenrola o WASM sem destrutores: o núcleo C++ continua "em frame" até o próximo frame recuperar.
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

  /** Ligações, entrada do host e leitura das ações do esquema. O mesmo objeto de `context.actions`. */
  get actions(): ActionMap {
    this.#assertUsable("actions");

    return this.#actions;
  }

  /** Tarefas no relógio do jogo. O mesmo objeto de `context.schedule`. */
  get schedule(): Scheduler {
    this.#assertUsable("schedule");

    return this.#scheduler;
  }

  /**
   * Avança o relógio de passo fixo até `nowSeconds` e roda as fases. Lança ContractError se chamado de dentro
   * de um sistema ou com tempo não finito ou menor que o do frame anterior. Durante o frame, mudanças
   * estruturais diretas no `world` são erro; use as do SystemContext. Exceção de um sistema ou handler TS sobe
   * daqui; até o frame seguinte, registrar sistema ou handler é ContractError, e esse frame descarta os comandos e
   * os eventos pendentes.
   */
  frame(nowSeconds: number): void {
    this.#assertUsable("frame");
    if (this.#inFrame) {
      throw new ContractError("frame: chamado de dentro de um sistema ou handler (frame não é reentrante)");
    }
    if (!Number.isFinite(nowSeconds)) {
      throw new ContractError(`frame: tempo=${nowSeconds} não é um número finito`);
    }
    if (nowSeconds < this.#lastFrameTime) {
      throw new ContractError(`frame: tempo=${nowSeconds} menor que o do frame anterior (${this.#lastFrameTime})`);
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
   * Vale o último pedido antes do passo 5 (dentro ou fora de `frame`); a troca acontece lá, pausado ou não: `onExit`,
   * limpeza do escopo, `onEnter`. Um pedido feito num gancho vale no passo seguinte. Lança ContractError para estado
   * de outro esquema.
   */
  requestState(state: State): void {
    this.#assertUsable("requestState");

    this.#bridge.engineRequestState(this.#handle, stateIndexOf(this.#runtime, state));
  }

  /** `null` até a primeira troca. Só muda no passo 5. */
  get state(): State | null {
    this.#assertUsable("state");

    return stateAt(this.#runtime, this.#bridge.engineState(this.#handle));
  }

  /** Ganchos do mesmo estado rodam na ordem de registro, os C++ e os TS intercalados. Lança ContractError durante frame. */
  onEnter(state: State, name: string, hook: StateHookFunction): void {
    this.#addStateHook("onEnter", StateEnter, state, name, hook);
  }

  onExit(state: State, name: string, hook: StateHookFunction): void {
    this.#addStateHook("onExit", StateExit, state, name, hook);
  }

  /** Registra um gancho C++ declarado com OLY_STATE_HOOK. Lança ContractError se o nome não existe. */
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
      throw new ContractError(`addNativeSystem name=${name}: não há sistema nativo com esse nome (declare com OLY_SYSTEM)`);
    }

    return this.#registration(id, phase, name);
  }

  /**
   * Registra um handler TS: chamado uma vez por lote de `type` em cada rodada, na ordem de registro. Lança
   * ContractError durante frame.
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
      throw new ContractError(`addNativeHandler name=${name}: não há handler nativo com esse nome (declare com OLY_HANDLER)`);
    }
    if (id === HandlerTypeMismatch) {
      throw new ContractError(`addNativeHandler name=${name} event=${type.name}: o handler nativo é de outro tipo de evento`);
    }

    return this.#handlerRegistration(id, name);
  }

  dispose(): void {
    if (this.#disposed) {
      return;
    }
    if (this.#inFrame) {
      throw new ContractError("dispose: Engine em frame; libere depois do frame");
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
          throw new ContractError(`whilePaused system=${name}: não vale para sistema da fase render`);
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
      throw new ContractError(`${operation} ${subject}: restrição de estados durante frame`);
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
      throw new ContractError(`${operation} name=${name}: não há gancho de estado nativo com esse nome (declare com OLY_STATE_HOOK)`);
    }
  }

  #assertCanRegisterStateHook(operation: string, state: State, name: string): number {
    this.#assertUsable(operation);
    if (this.#inFrame) {
      throw new ContractError(`${operation} hook=${name}: registro durante frame`);
    }
    this.#assertNotInterrupted(operation, `hook=${name}`);

    return stateIndexOf(this.#runtime, state);
  }

  #invoke(systemId: number): void {
    const entry = this.#scriptSystems.get(systemId);
    if (entry === undefined) {
      throw new ContractError(`sistema id=${systemId}: não é um sistema TS desta Engine`);
    }

    // Um sistema C++ anterior pode ter crescido a memória do WASM.
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
      throw new ContractError(`handler id=${id}: não é um handler TS desta Engine`);
    }

    // Um handler ou sistema C++ anterior pode ter crescido a memória do WASM.
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
      throw new ContractError(`gancho id=${id}: não é um gancho de estado TS desta Engine`);
    }

    // Um gancho, sistema ou handler C++ anterior pode ter crescido a memória do WASM.
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
      throw new ContractError(`${operation} handler=${name}: registro durante frame`);
    }
    this.#assertNotInterrupted(operation, `handler=${name}`);

    return eventIndexOf(this.#runtime, type);
  }

  #assertNotInterrupted(operation: string, subject: string): void {
    if (this.#interrupted) {
      throw new ContractError(`${operation} ${subject}: o último frame foi interrompido por uma exceção; chame frame antes de registrar`);
    }
  }

  #assertCanRegister(operation: string, phase: Phase, name: string): void {
    this.#assertUsable(operation);
    if (this.#inFrame) {
      throw new ContractError(`${operation} system=${name}: registro durante frame`);
    }
    this.#assertNotInterrupted(operation, `system=${name}`);
    if (!Object.values(Phase).includes(phase)) {
      throw new ContractError(`${operation} system=${name}: phase=${String(phase)} inválida`);
    }
  }

  #assertUsable(operation: string): void {
    if (this.#disposed) {
      throw new ContractError(`${operation}: Engine já liberada`);
    }
  }
}

export function engineHandleOf(engine: Engine): number {
  return engineHandleOfInternal(engine);
}

export function invokeScript(engine: Engine, kind: number, id: number, events: number, eventCount: number): void {
  invokeScriptInternal(engine, kind, id, events, eventCount);
}
