// Só o que o core usa do ambiente; existe em node, navegador e worker.
declare class TextDecoder {
  decode(input?: Uint8Array): string;
}
