// Violação do contrato de uso da engine (entidade morta, componente ausente, bloco fora do callback...).
export class EngineContractError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "EngineContractError";
  }
}

// O WASM carregado foi compilado com outro esquema que o do TS.
export class SchemaMismatchError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "SchemaMismatchError";
  }
}
