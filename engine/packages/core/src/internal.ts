// Superfície interna: para os pacotes irmãos da engine (client, codegen, testing). Jogos não importam daqui.
export { Bridge, Heap, readScalar, writeScalar } from "./bridge.js";
export { MaxEntityGeneration, packEntity } from "./entity.js";
export { makeView } from "./fields.js";
export { computeLayout, scalarAlignment, scalarSize } from "./layout.js";
export type { OlyModule } from "./module.js";
export { actionIndexOf, bridgeOf, componentIdOf, eventIndexOf, moduleOf } from "./runtime.js";
export { canonicalSchema, fnv1a32, schemaHash } from "./schema_hash.js";
