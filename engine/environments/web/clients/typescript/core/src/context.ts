import type { ActionMap } from "./action_map.js";
import type { Bridge } from "./bridge.js";
import type { Entity } from "./entity.js";
import { ContractError } from "./errors.js";
import type { Query, QuerySpec } from "./query.js";
import { eventIndexOf, stateAt, stateIndexOf, type Runtime } from "./runtime.js";
import type { Scheduler } from "./scheduler.js";
import type { ComponentType, EventType, Fields, SchemaType, State, ValueOf } from "./schema.js";
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
 * What SystemContext, HandlerContext, TaskContext and StateContext have in common. Valid only during the call of its system,
 * handler, task or hook; outside it, any use throws ContractError.
 */
export class EngineContext {
  #active = false;
  readonly #actions: ActionMap;
  readonly #scheduler: Scheduler;
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
          throw new ContractError(`${operation} ${label}=${context.#name}: ${className} used outside the ${callNoun} call`);
        }
      },
    };
  }

  constructor(kind: ContextKind, name: string, runtime: Runtime, bridge: Bridge, engineHandle: number, world: World, actions: ActionMap, scheduler: Scheduler) {
    this.#actions = actions;
    this.#scheduler = scheduler;
    this.#kind = kind;
    this.#name = name;
    this.#runtime = runtime;
    this.#bridge = bridge;
    this.#engineHandle = engineHandle;
    this.#world = world;
  }

  /** The same object as `Engine.actions`. */
  get actions(): ActionMap {
    contextInternals.assertActive(this, "actions");

    return this.#actions;
  }

  /** The same object as `Engine.schedule`. */
  get schedule(): Scheduler {
    contextInternals.assertActive(this, "schedule");

    return this.#scheduler;
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

  /** The last request before step 5 wins; the state change happens there. */
  requestState(state: State): void {
    contextInternals.assertActive(this, "requestState");

    this.#bridge.engineRequestState(this.#engineHandle, stateIndexOf(this.#runtime, state));
  }

  /** `null` until the first change. Only changes in step 5. */
  get state(): State | null {
    contextInternals.assertActive(this, "state");

    return stateAt(this.#runtime, this.#bridge.engineState(this.#engineHandle));
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

  /** Missing fields are 0. The event is dispatched in the next round of the current step. */
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
