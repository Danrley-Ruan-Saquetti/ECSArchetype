import type { Bridge } from "./bridge.js";
import { ContractError } from "./errors.js";
import { bridgeOf, type Runtime } from "./runtime.js";
import { setContextActive, SystemContext, type SystemFunction } from "./system_context.js";
import { releaseOwnedWorld, setStructureLocked, type World } from "./world.js";

export const Phase = { UpdateBefore: 0, Update: 1, UpdateAfter: 2, Render: 3 } as const;
export type Phase = (typeof Phase)[keyof typeof Phase];

export interface EngineConfig {
  fixedHz?: number;
  maxStepsPerFrame?: number;
}

export interface SystemRegistration {
  readonly id: number;
  // Roda o sistema também com o jogo pausado (e só então). Não vale para a fase Render.
  whilePaused(): SystemRegistration;
}

const InvalidSystem = 0xffffffff;

interface ScriptSystem {
  readonly function: SystemFunction;
  readonly context: SystemContext;
}

let invokeSystemInternal!: (engine: Engine, systemId: number) => void;

// Regras do loop vivem no núcleo C++; aqui só validação de uso e a chamada de volta dos sistemas TS.
export class Engine {
  readonly world: World;

  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #handle: number;
  readonly #onDispose: () => void;
  readonly #scriptSystems = new Map<number, ScriptSystem>();
  #inFrame = false;
  #disposed = false;
  #lastFrameTime = -Infinity;

  static {
    invokeSystemInternal = (engine, systemId) => engine.#invoke(systemId);
  }

  constructor(runtime: Runtime, handle: number, world: World, onDispose: () => void) {
    this.#runtime = runtime;
    this.#bridge = bridgeOf(runtime);
    this.#handle = handle;
    this.world = world;
    this.#onDispose = onDispose;
  }

  frame(nowSeconds: number): void {
    this.#assertUsable("frame");
    if (this.#inFrame) {
      throw new ContractError("frame: chamado de dentro de um sistema (frame não é reentrante)");
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
    const context = new SystemContext(phase, name, this.#runtime, this.#bridge, this.#handle, this.world);
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
    };

    return registration;
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

  #assertCanRegister(operation: string, phase: Phase, name: string): void {
    this.#assertUsable(operation);
    if (this.#inFrame) {
      throw new ContractError(`${operation} system=${name}: registro durante frame`);
    }
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

export function invokeSystem(engine: Engine, systemId: number): void {
  invokeSystemInternal(engine, systemId);
}
