import { Phase } from "@oly/core";
import type { ActionMap, Engine, EventBatch, HandlerContext, StateContext, SystemContext, TaskContext } from "@oly/core";

import { Chaser, Coin, CoinCollected, Collider, Dash, Enemy, EnemyDied, Health, Player, PlayerDied, Position, Velocity, Weapon, confirm, dash, gameOver, menu, move, pause, playing, shoot } from "../schema.js";
import { ArenaHeight, ArenaWidth, CoinInterval, CoinPoints, CoinRadius, DashDuration, DashRecharge, DashSpeed, EnemyHealth, EnemyRadius, EnemySpeed, KillPoints, PlayerHealth, PlayerRadius, WaveBaseEnemies, WaveInterval, WeaponInterval } from "./arena.js";
import { createRenderSystem } from "./render.js";
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

function pauseOnPress(context: SystemContext): void {
  if (context.actions.button(pause).pressed) {
    context.pause();
  }
}

function resumeOnPress(context: SystemContext): void {
  if (context.actions.button(pause).pressed) {
    context.resume();
  }
}

export function installGame(engine: Engine, options: GameOptions): ArenaScore {
  const score = createScore();

  bindActions(engine.actions);
  engine.addSystem(Phase.UpdateBefore, "startOnConfirm", startOnConfirm).inState(menu, gameOver);
  engine.addSystem(Phase.UpdateBefore, "pauseOnPress", pauseOnPress).inState(playing);
  engine.addSystem(Phase.UpdateBefore, "resumeOnPress", resumeOnPress).whilePaused().inState(playing);
  engine.addNativeSystem(Phase.UpdateBefore, "steerPlayer").inState(playing);
  engine.addNativeSystem(Phase.Update, "fireWeapon").inState(playing);
  engine.addNativeSystem(Phase.Update, "chasePlayer").inState(playing);
  engine.addNativeSystem(Phase.Update, "integrate").inState(playing);
  // In update, not in updateAfter: the destroys of out-of-bounds bullets are applied at the end of the phase, so collide sees no
  // bullet outside the arena (which it would destroy again) and no player beyond the wall.
  engine.addNativeSystem(Phase.Update, "confine").inState(playing);
  engine.addNativeSystem(Phase.UpdateAfter, "collide").inState(playing);

  engine.addNativeHandler(EnemyDied, "dropCoin");
  engine.on(EnemyDied, "countKill", (_context, batch) => countKill(score, batch));
  engine.addNativeHandler(EnemyDied, "destroyDead");
  engine.on(CoinCollected, "collectCoin", (context, batch) => collectCoin(context, score, batch));
  engine.on(PlayerDied, "endRound", (context) => endRound(context, score));

  engine.addSystem(Phase.Render, "render", createRenderSystem(options.drawer, score));

  engine.onEnter(playing, "startRound", (context) => startRound(context, score, options.random));
  // The pause applies to the whole engine, and only resumeOnPress (restricted to playing) lifts it: leaving playing while paused
  // would lock the next state.
  engine.onExit(playing, "resumeOnExit", (context) => context.resume());
  engine.requestState(menu);

  return score;
}

function startRound(context: StateContext, score: MutableScore, random: () => number): void {
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

  let wave = 0;
  context.schedule.every(WaveInterval, (task) => {
    wave += 1;
    spawnWave(task, random, wave);
  });
  context.schedule.every(CoinInterval, (task) => spawnLooseCoin(task, random));
}

function spawnWave(context: TaskContext, random: () => number, wave: number): void {
  for (let index = 0; index < WaveBaseEnemies + wave; ++index) {
    const side = Math.floor(random() * 4);
    const along = random();
    const horizontal = side === 0 || side === 2;
    const x = horizontal ? along * ArenaWidth : side === 1 ? ArenaWidth : 0;
    const y = horizontal ? (side === 0 ? 0 : ArenaHeight) : along * ArenaHeight;

    context.spawn([Enemy, Position, Velocity, Collider, Health, Chaser], {
      Position: { x, y },
      Collider: { radius: EnemyRadius },
      Health: { value: EnemyHealth },
      Chaser: { speed: EnemySpeed },
    });
  }
}

function spawnLooseCoin(context: TaskContext, random: () => number): void {
  context.spawn([Coin, Position, Collider], {
    Position: { x: CoinRadius + random() * (ArenaWidth - 2 * CoinRadius), y: CoinRadius + random() * (ArenaHeight - 2 * CoinRadius) },
    Collider: { radius: CoinRadius },
  });
}

// Two bullets in the same step produce two deaths of the same enemy in the batch (§4.4).
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
