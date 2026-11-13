// Internal surface: for the engine's sibling packages (client, codegen, testing). Games do not import from here.
export { Bridge, Heap, readScalar, writeScalar } from "./bridge.js";
export { MaxEntityGeneration, packEntity } from "./entity.js";
export { makeView } from "./fields.js";
export { computeLayout, scalarAlignment, scalarSize } from "./layout.js";
export type { OlyModule } from "./module.js";
export { engineHandleOf } from "./engine.js";
export { scriptTaskCount } from "./scheduler.js";
export { buttonUsages, isButtonName } from "./buttons.js";
export { actionIndexOf, bridgeOf, componentIdOf, eventIndexOf, moduleOf, stateAt, stateIndexOf } from "./runtime.js";
export { canonicalSchema, fnv1a32, schemaHash } from "./schema_hash.js";
export { stateMaskOf } from "./state_mask.js";
