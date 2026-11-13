/** Violation of the Oly usage contract (dead entity, missing component, block outside the callback...). */
export class ContractError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ContractError";
  }
}

/** The loaded WASM was compiled with a schema other than the TS one; recompile the WASM. */
export class SchemaMismatchError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "SchemaMismatchError";
  }
}
