import { Phase } from "@oly/core";
import type { ActionMap, Engine, StateContext, SystemContext } from "@oly/core";

import { Collider, Dash, Health, Player, Position, Velocity, Weapon, confirm, dash, gameOver, menu, move, pause, playing, shoot } from "../schema.js";
import { ArenaHeight, ArenaWidth, CoinPoints, DashDuration, DashRecharge, DashSpeed, KillPoints, PlayerHealth, PlayerRadius, WeaponInterval } from "./arena.js";
import type { Drawer } from "./render.js";

export interface GameOptions {
  readonly drawer: Drawer;
  readonly random: () => number;
}

export interface ArenaScore {
  readonly kills: number;
  readonly coins: number;
  readonly score: number;
  readonly finalScore: number | null;
}

interface MutableScore {
  kills: number;
  coins: number;
  readonly score: number;
  finalScore: number | null;
}

function createScore(): MutableScore {
  return {
    kills: 0,
    coins: 0,
    finalScore: null,
    get score(): number {
      return KillPoints * this.kills + CoinPoints * this.coins;
    },
  };
}

function bindActions(actions: ActionMap): void {
  actions.bind(move, { up: "KeyW", down: "KeyS", left: "KeyA", right: "KeyD" });
  actions.bind(shoot, "MouseLeft");
  actions.bind(dash, "Space");
  actions.bind(pause, "Escape");
  actions.bind(confirm, "Enter");
  actions.bind(confirm, "MouseLeft");
}

function startOnConfirm(context: SystemContext): void {
  if (context.actions.button(confirm).pressed) {
    context.requestState(playing);
  }
}

export function installGame(engine: Engine, options: GameOptions): ArenaScore {
  const score = createScore();

  bindActions(engine.actions);
  engine.addSystem(Phase.UpdateBefore, "startOnConfirm", startOnConfirm).inState(menu, gameOver);
  engine.onEnter(playing, "startRound", (context) => startRound(context, score));
  engine.requestState(menu);

  return score;
}

function startRound(context: StateContext, score: MutableScore): void {
  score.kills = 0;
  score.coins = 0;
  score.finalScore = null;

  context.spawn([Player, Position, Velocity, Collider, Health, Weapon, Dash], {
    Position: { x: ArenaWidth / 2, y: ArenaHeight / 2 },
    Collider: { radius: PlayerRadius },
    Health: { value: PlayerHealth },
    Weapon: { interval: WeaponInterval, cooldown: 0 },
    Dash: { speed: DashSpeed, duration: DashDuration, recharge: DashRecharge, ready: 1, active: 0 },
  });
}
