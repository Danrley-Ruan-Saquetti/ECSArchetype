import type { Bridge } from "./bridge.js";
import type { Entity } from "./entity.js";
import { ContractError } from "./errors.js";
import type { Query, QuerySpec } from "./query.js";
import { eventIndexOf, type Runtime } from "./runtime.js";
import type { ComponentType, EventType, Fields, SchemaType, ValueOf } from "./schema.js";
import { assertKnownFields, writeValue } from "./values.js";
import type { World } from "./world.js";

export interface ContextKind {
  readonly label: string;
  readonly className: string;
  readonly callNoun: string;
}

interface ContextInternals {
  setActive(context: EngineContext, active: boolean): void;
  assertActive(context: EngineContext, operation: string): void;
}

let contextInternals!: ContextInternals;

/**
 * O que o SystemContext e o HandlerContext têm em comum. Válido só durante a chamada do seu sistema ou handler;
 * fora dela, qualquer uso lança ContractError.
 */
export class EngineContext {
  #active = false;
  readonly #kind: ContextKind;
  readonly #name: string;
  readonly #runtime: Runtime;
  readonly #bridge: Bridge;
  readonly #engineHandle: number;
  readonly #world: World;

  static {
    contextInternals = {
      setActive: (context, active) => {
        context.#active = active;
      },
      assertActive: (context, operation) => {
        if (!context.#active) {
          const { label, className, callNoun } = context.#kind;
          throw new ContractError(`${operation} ${label}=${context.#name}: ${className} usado fora da chamada do ${callNoun}`);
        }
      },
    };
  }

  constructor(kind: ContextKind, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World) {
    this.#kind = kind;
    this.#name = name;
    this.#runtime = runtime;
    this.#bridge = bridge;
    this.#engineHandle = engineHandle;
    this.#world = world;
  }

  get fixedDeltaTime(): number {
    contextInternals.assertActive(this, "fixedDeltaTime");

    return this.#bridge.engineFixedDeltaTime(this.#engineHandle);
  }

  get time(): number {
    contextInternals.assertActive(this, "time");

    return this.#bridge.engineTime(this.#engineHandle);
  }

  get paused(): boolean {
    contextInternals.assertActive(this, "paused");

    return this.#bridge.engineIsPaused(this.#engineHandle);
  }

  pause(): void {
    contextInternals.assertActive(this, "pause");

    this.#bridge.enginePause(this.#engineHandle);
  }

  resume(): void {
    contextInternals.assertActive(this, "resume");

    this.#bridge.engineResume(this.#engineHandle);
  }

  query(spec: QuerySpec): Query {
    contextInternals.assertActive(this, "query");

    return this.#world.query(spec);
  }

  get<F extends Fields>(entity: Entity, type: ComponentType<string, F>): ValueOf<F> {
    contextInternals.assertActive(this, "get");

    return this.#world.get(entity, type);
  }

  set<F extends Fields>(entity: Entity, type: ComponentType<string, F>, value: Partial<ValueOf<F>>): void {
    contextInternals.assertActive(this, "set");

    this.#world.set(entity, type, value);
  }

  has(entity: Entity, type: SchemaType): boolean {
    contextInternals.assertActive(this, "has");

    return this.#world.has(entity, type);
  }

  isAlive(entity: Entity): boolean {
    contextInternals.assertActive(this, "isAlive");

    return this.#world.isAlive(entity);
  }

  /** Campos ausentes valem 0. O evento é despachado na rodada seguinte do passo em curso. */
  emit<F extends Fields>(type: EventType<string, F>, value?: Partial<ValueOf<F>>): void {
    contextInternals.assertActive(this, "emit");

    const eventType = eventIndexOf(this.#runtime, type);
    const fieldValues = value as Partial<Record<string, number>> | undefined;
    assertKnownFields("emit", null, type, fieldValues ?? {});

    this.#bridge.engineEmit(this.#engineHandle, eventType, writeValue(this.#bridge, type.layout, fieldValues));
  }
}

export function setContextActive(context: EngineContext, active: boolean): void {
  contextInternals.setActive(context, active);
}

export function assertContextActive(context: EngineContext, operation: string): void {
  contextInternals.assertActive(context, operation);
}
