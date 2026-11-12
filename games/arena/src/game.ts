import { Phase } from "@oly/core";
import type { ActionMap, Engine, EventBatch, HandlerContext, StateContext, SystemContext } from "@oly/core";

import { CoinCollected, Collider, Dash, EnemyDied, Health, Player, PlayerDied, Position, Velocity, Weapon, confirm, dash, gameOver, menu, move, pause, playing, shoot } from "../schema.js";
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
  engine.addNativeSystem(Phase.UpdateBefore, "steerPlayer").inState(playing);
  engine.addNativeSystem(Phase.Update, "fireWeapon").inState(playing);
  engine.addNativeSystem(Phase.Update, "chasePlayer").inState(playing);
  engine.addNativeSystem(Phase.Update, "integrate").inState(playing);
  engine.addNativeSystem(Phase.UpdateAfter, "collide").inState(playing);
  engine.addNativeSystem(Phase.UpdateAfter, "confine").inState(playing);

  engine.addNativeHandler(EnemyDied, "dropCoin");
  engine.on(EnemyDied, "countKill", (_context, batch) => countKill(score, batch));
  engine.addNativeHandler(EnemyDied, "destroyDead");
  engine.on(CoinCollected, "collectCoin", (context, batch) => collectCoin(context, score, batch));
  engine.on(PlayerDied, "endRound", (context) => endRound(context, score));

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

// Duas balas no mesmo passo geram duas mortes do mesmo inimigo no lote (§4.4).
function countKill(score: MutableScore, batch: EventBatch<typeof EnemyDied.fields>): void {
  const enemies = batch.read().entity;
  const counted = new Set<number>();
  for (let index = 0; index < batch.count; ++index) {
    const enemy = enemies.get(index);
    if (!counted.has(enemy)) {
      counted.add(enemy);
      score.kills += 1;
    }
  }
}

function collectCoin(context: HandlerContext, score: MutableScore, batch: EventBatch<typeof CoinCollected.fields>): void {
  const coins = batch.read().coin;
  for (let index = 0; index < batch.count; ++index) {
    const coin = coins.get(index);
    if (context.isAlive(coin)) {
      context.destroy(coin);
      score.coins += 1;
    }
  }
}

function endRound(context: HandlerContext, score: MutableScore): void {
  score.finalScore = score.score;
  context.requestState(gameOver);
}
