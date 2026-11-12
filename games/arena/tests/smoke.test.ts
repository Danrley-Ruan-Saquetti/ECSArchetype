import assert from "node:assert/strict";
import { test } from "node:test";

import { Bullet, Chaser, Coin, Collider, Dash, Enemy, Health, Invulnerable, Player, Position, Velocity } from "../schema.js";
import { ArenaWidth, PlayerRadius } from "../src/arena.js";
import { loadArena, withArena } from "./harness.js";
import type { ArenaTest } from "./harness.js";

const runtime = await loadArena();

test("installGame entra em menu no primeiro passo, sem jogador", () => {
  withArena(runtime, (arena) => {
    arena.step();

    assert.equal(arena.engine.state?.name, "menu");
    assert.equal(arena.count(Player), 0);
  });
});

test("Enter em menu começa: playing com um jogador no centro, vida 3 e placar zerado", () => {
  withArena(runtime, (arena) => {
    arena.step();

    arena.tap("Enter");
    arena.step();

    assert.equal(arena.engine.state?.name, "playing");
    const player = arena.player();
    assert.deepEqual(arena.engine.world.get(player, Position), { x: 480, y: 270 });
    assert.equal(arena.engine.world.get(player, Health).value, 3);
    assert.deepEqual(arena.score, { kills: 0, coins: 0, score: 0, finalScore: null });
  });
});

test("clique (MouseLeft) em menu também começa", () => {
  withArena(runtime, (arena) => {
    arena.step();

    arena.tap("MouseLeft");
    arena.step();

    assert.equal(arena.engine.state?.name, "playing");
    assert.equal(arena.count(Player), 1);
  });
});

function startPlaying(arena: ArenaTest): void {
  arena.step();
  arena.tap("Enter");
  arena.step();
}

function spawnEnemy(arena: ArenaTest, x: number, y: number, speed = 0, health = 1): number {
  return arena.engine.world.spawn([Enemy, Position, Velocity, Collider, Health, Chaser], {
    Position: { x, y },
    Collider: { radius: 12 },
    Health: { value: health },
    Chaser: { speed },
  });
}

function spawnBullet(arena: ArenaTest, x: number, y: number): number {
  return arena.engine.world.spawn([Bullet, Position, Velocity, Collider], { Position: { x, y }, Collider: { radius: 3 } });
}

function expectClose(actual: number, expected: number): void {
  assert.ok(Math.abs(actual - expected) < 1e-3, `esperava ${expected}, veio ${actual}`);
}

test("WASD move o jogador a 220 px/s; diagonal normalizada", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const player = arena.player();

    arena.hold("KeyD");
    arena.step();
    assert.equal(arena.engine.world.get(player, Velocity).x, 220);

    arena.hold("KeyS");
    arena.step();
    const velocity = arena.engine.world.get(player, Velocity);
    expectClose(Math.hypot(velocity.x, velocity.y), 220);
  });
});

test("dash: impulso de 600 na direção do movimento, fim em 0,15 s e recarga em 0,8 s", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const player = arena.player();
    arena.hold("KeyD");
    arena.step();

    arena.tap("Space");
    assert.equal(arena.engine.world.get(player, Dash).active, 1);
    assert.equal(arena.engine.world.get(player, Dash).ready, 0);
    assert.equal(arena.engine.world.get(player, Velocity).x, 600);

    arena.stepSeconds(0.2);
    assert.equal(arena.engine.world.get(player, Dash).active, 0);
    assert.equal(arena.engine.world.get(player, Velocity).x, 220);

    arena.stepSeconds(0.3);
    assert.equal(arena.engine.world.get(player, Dash).ready, 0);

    arena.stepSeconds(0.4);
    assert.equal(arena.engine.world.get(player, Dash).ready, 1);
  });
});

test("dash parado vai para o ponteiro; sem ponteiro válido não gasta a recarga", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const player = arena.player();

    arena.tap("Space");
    assert.equal(arena.engine.world.get(player, Dash).ready, 1);
    assert.equal(arena.engine.world.get(player, Dash).active, 0);

    arena.engine.actions.pointerMove(580, 270);
    arena.tap("Space");
    assert.equal(arena.engine.world.get(player, Dash).active, 1);
    assert.equal(arena.engine.world.get(player, Dash).ready, 0);
    assert.equal(arena.engine.world.get(player, Velocity).x, 600);
  });
});

test("chasePlayer aponta a velocidade do inimigo para o jogador com a velocidade do Chaser", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const enemy = spawnEnemy(arena, 780, 670, 90);

    arena.step();

    const velocity = arena.engine.world.get(enemy, Velocity);
    expectClose(velocity.x, -54);
    expectClose(velocity.y, -72);
  });
});

test("confine prende o jogador em ArenaWidth − raio e destrói a bala fora da arena", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const player = arena.player();

    arena.engine.actions.pointerMove(900, 270);
    arena.hold("MouseLeft");
    arena.step();
    arena.release("MouseLeft");
    assert.equal(arena.count(Bullet), 1);
    arena.stepSeconds(2);
    assert.equal(arena.count(Bullet), 0);

    arena.engine.world.set(player, Position, { x: 5000, y: -50 });
    arena.step();
    assert.deepEqual(arena.engine.world.get(player, Position), { x: ArenaWidth - PlayerRadius, y: PlayerRadius });
  });
});

test("tiro na direção do ponteiro mata o inimigo sem sobra de bala nem de inimigo", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const enemy = spawnEnemy(arena, 580, 270);

    arena.engine.actions.pointerMove(680, 270);
    arena.hold("MouseLeft");
    arena.step();
    arena.release("MouseLeft");
    arena.stepSeconds(0.5);

    assert.equal(arena.score.kills, 1);
    assert.equal(arena.engine.world.isAlive(enemy), false);
    assert.equal(arena.count(Bullet), 0);
  });
});

test("duas balas no mesmo inimigo no mesmo passo: uma morte no placar e no máximo uma moeda", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const enemy = spawnEnemy(arena, 200, 100);
    spawnBullet(arena, 200, 100);
    spawnBullet(arena, 201, 100);

    arena.step();

    assert.equal(arena.score.kills, 1);
    assert.equal(arena.engine.world.isAlive(enemy), false);
    assert.equal(arena.count(Bullet), 0);
    assert.ok(arena.count(Coin) <= 1);
  });
});

test("moeda encostada no jogador some e soma no placar", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const coin = arena.engine.world.spawn([Coin, Position, Collider], { Position: { x: 480, y: 270 }, Collider: { radius: 8 } });

    arena.step();

    assert.equal(arena.engine.world.isAlive(coin), false);
    assert.equal(arena.score.coins, 1);
    assert.equal(arena.score.score, 5);
  });
});

test("dano dá 1 s de invulnerabilidade; vida 0 leva a gameOver sem jogador", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const player = arena.player();
    spawnEnemy(arena, 480, 270);

    arena.step();
    assert.equal(arena.engine.world.get(player, Health).value, 2);
    assert.equal(arena.engine.world.has(player, Invulnerable), true);

    arena.stepSeconds(0.5);
    assert.equal(arena.engine.world.get(player, Health).value, 2);

    arena.stepSeconds(0.6);
    assert.equal(arena.engine.world.get(player, Health).value, 1);

    arena.stepSeconds(1.1);

    assert.equal(arena.engine.state?.name, "gameOver");
    assert.equal(arena.count(Player), 0);
    assert.equal(arena.score.finalScore, arena.score.score);
  });
});
