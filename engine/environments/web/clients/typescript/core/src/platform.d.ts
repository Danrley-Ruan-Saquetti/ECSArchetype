// Only what core uses from the environment; it exists in node, browser and worker.
declare class TextDecoder {
  decode(input?: Uint8Array): string;
}
declare class TextEncoder {
  encode(input?: string): Uint8Array;
}
