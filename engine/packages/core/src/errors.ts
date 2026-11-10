// Violação do contrato de uso da Oly (entidade morta, componente ausente, bloco fora do callback...).
export class ContractError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ContractError";
  }
}

// O WASM carregado foi compilado com outro esquema que o do TS.
export class SchemaMismatchError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "SchemaMismatchError";
  }
}
