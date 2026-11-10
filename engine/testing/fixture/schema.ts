import { component, defineSchema, event, tag } from "@oly/core";

export const Position = component("Position", { x: "f32", y: "f32" });
export const Velocity = component("Velocity", { x: "f32", y: "f32" });
export const Mixed = component("Mixed", { a: "u8", b: "f64", c: "i16", owner: "entity" });
export const Trace = component("Trace", { value: "f64" });
export const Player = tag("Player");
export const Enemy = tag("Enemy");

export const Ping = event("Ping", { target: "entity", value: "f32" });
export const Echo = event("Echo", { depth: "u32" });

export default defineSchema([Position, Velocity, Mixed, Trace, Player, Enemy, Ping, Echo]);
