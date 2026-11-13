import { action, component, defineSchema, event, state, tag } from "@oly/core";

export const Position = component("Position", { x: "f32", y: "f32" });
export const Velocity = component("Velocity", { x: "f32", y: "f32" });
export const Collider = component("Collider", { radius: "f32" });
export const Health = component("Health", { value: "i32" });
export const Chaser = component("Chaser", { speed: "f32" });
export const Weapon = component("Weapon", { interval: "f32", cooldown: "f32" });
export const Dash = component("Dash", { speed: "f32", duration: "f32", recharge: "f32", ready: "u8", active: "u8" });
export const Player = tag("Player");
export const Enemy = tag("Enemy");
export const Bullet = tag("Bullet");
export const Coin = tag("Coin");
export const Invulnerable = tag("Invulnerable");

export const EnemyDied = event("EnemyDied", { entity: "entity", x: "f32", y: "f32" });
export const CoinCollected = event("CoinCollected", { coin: "entity" });
export const PlayerDied = event("PlayerDied", { entity: "entity" });

export const move = action("move", "axis2d");
export const aim = action("aim", "pointer");
export const shoot = action("shoot", "button");
export const dash = action("dash", "button");
export const pause = action("pause", "button");
export const confirm = action("confirm", "button");

export const menu = state("menu");
export const playing = state("playing");
export const gameOver = state("gameOver");

export default defineSchema([
  Position,
  Velocity,
  Collider,
  Health,
  Chaser,
  Weapon,
  Dash,
  Player,
  Enemy,
  Bullet,
  Coin,
  Invulnerable,
  EnemyDied,
  CoinCollected,
  PlayerDied,
  move,
  aim,
  shoot,
  dash,
  pause,
  confirm,
  menu,
  playing,
  gameOver,
]);
