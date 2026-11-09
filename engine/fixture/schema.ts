import { component, defineSchema, tag } from "../ts/schema.js";

export const Position = component("Position", { x: "f32", y: "f32" });
export const Velocity = component("Velocity", { x: "f32", y: "f32" });
export const Mixed = component("Mixed", { a: "u8", b: "f64", c: "i16", owner: "entity" });
export const Player = tag("Player");
export const Enemy = tag("Enemy");

export default defineSchema([Position, Velocity, Mixed, Player, Enemy]);
