import type { Bridge } from "./bridge.js";
import { ContractError } from "./errors.js";
import { makeView } from "./fields.js";
import type { ReadView } from "./fields.js";
import type { EventType, Fields } from "./schema.js";

let endBatchInternal!: (batch: EventBatch) => void;

/**
 * O lote de eventos de um tipo que um handler recebe, na ordem de emissão. As views leem a fila direto da memória
 * do WASM (sem cópia) e valem só durante a chamada do handler; depois dela, `read` lança ContractError.
 */
export class EventBatch<F extends Fields = Fields> {
  readonly type: EventType;
  readonly count: number;

  #view: ReadView<F> | null = null;
  #live = true;
  readonly #bridge: Bridge;
  readonly #address: number;

  static {
    endBatchInternal = (batch) => {
      batch.#live = false;
    };
  }

  constructor(type: EventType, count: number, address: number, bridge: Bridge) {
    this.type = type;
    this.count = count;
    this.#address = address;
    this.#bridge = bridge;
  }

  read(): ReadView<F> {
    if (!this.#live) {
      throw new ContractError(`read type=${this.type.name}: EventBatch usado fora da chamada do handler`);
    }

    this.#view ??= makeView(this.#bridge.heap, this.type.layout, this.#address) as unknown as ReadView<F>;

    return this.#view;
  }
}

export function endBatch(batch: EventBatch): void {
  endBatchInternal(batch);
}
