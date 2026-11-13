import assert from "node:assert/strict";
import { test } from "node:test";

import { Bullet, Chaser, Coin, Collider, Dash, Enemy, Health, Invulnerable, Player, Position, Velocity, menu } from "../schema.generated.js";
import { ArenaHeight, ArenaWidth, PlayerRadius } from "../src/arena.js";
import { loadArena, withArena } from "./harness.js";
import type { ArenaTest } from "./harness.js";

const runtime = await loadArena();

test("installGame enters menu on the first step, with no player", () => {
  withArena(runtime, (arena) => {
    arena.step();

    assert.equal(arena.engine.state?.name, "menu");
    assert.equal(arena.count(Player), 0);
  });
});

test("Enter in menu starts: playing with one player in the center, health 3 and a reset score", () => {
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

test("click (MouseLeft) in menu also starts", () => {
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
  assert.ok(Math.abs(actual - expected) < 1e-3, `expected ${expected}, got ${actual}`);
}

test("WASD moves the player at 220 px/s; normalized diagonal", () => {
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

test("dash: impulse of 600 in the movement direction, ends in 0.15 s and recharges in 0.8 s", () => {
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

test("dash while standing goes toward the pointer; without a valid pointer it does not spend the recharge", () => {
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

test("chasePlayer points the enemy velocity at the player with the Chaser speed", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const enemy = spawnEnemy(arena, 780, 670, 90);

    arena.step();

    const velocity = arena.engine.world.get(enemy, Velocity);
    expectClose(velocity.x, -54);
    expectClose(velocity.y, -72);
  });
});

test("confine pins the player at ArenaWidth − radius and destroys the bullet outside the arena", () => {
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

test("shot toward the pointer kills the enemy with no leftover bullet or enemy", () => {
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

test("two bullets on the same enemy in the same step: one kill on the score and at most one coin", () => {
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

test("coin touching the player disappears and adds to the score", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const coin = arena.engine.world.spawn([Coin, Position, Collider], { Position: { x: 480, y: 270 }, Collider: { radius: 8 } });

    arena.step();

    assert.equal(arena.engine.world.isAlive(coin), false);
    assert.equal(arena.score.coins, 1);
    assert.equal(arena.score.score, 5);
  });
});

test("damage gives 1 s of invulnerability; health 0 leads to gameOver with no player", () => {
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

test("Esc pauses: clock, positions and dash freeze; Esc again resumes and the dash ends in the remaining time", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const player = arena.player();
    arena.hold("KeyD");
    arena.step();
    arena.tap("Space");

    arena.tap("Escape");
    const frozenTime = arena.engine.time;
    const frozenPosition = arena.engine.world.get(player, Position);
    arena.step(60);

    assert.equal(arena.engine.paused, true);
    assert.equal(arena.engine.time, frozenTime);
    assert.deepEqual(arena.engine.world.get(player, Position), frozenPosition);
    assert.equal(arena.engine.world.get(player, Dash).active, 1);

    arena.tap("Escape");
    arena.step(3);
    assert.equal(arena.engine.paused, false);
    assert.equal(arena.engine.world.get(player, Dash).active, 1);

    arena.step(10);
    assert.equal(arena.engine.world.get(player, Dash).active, 0);
  });
});

function isOnBorder(x: number, y: number): boolean {
  const margin = 2;

  return x <= margin || x >= ArenaWidth - margin || y <= margin || y >= ArenaHeight - margin;
}

test("waves every 5 s with 2 + n enemies at the edges; one loose coin every 2 s inside the arena", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    arena.engine.world.set(arena.player(), Health, { value: 1000 });

    arena.stepSeconds(4.9);
    assert.equal(arena.count(Enemy), 0);
    assert.equal(arena.count(Coin), 2);
    arena.engine.world.query({ read: [Position], with: [Coin] }).eachBlock((block) => {
      const position = block.read(Position);
      for (let row = 0; row < block.count; ++row) {
        assert.ok(position.x.get(row) >= 8 && position.x.get(row) <= ArenaWidth - 8);
        assert.ok(position.y.get(row) >= 8 && position.y.get(row) <= ArenaHeight - 8);
      }
    });

    for (let guard = 0; arena.count(Enemy) === 0 && guard < 60; ++guard) {
      arena.step();
    }
    assert.equal(arena.count(Enemy), 3);
    arena.engine.world.query({ read: [Position], with: [Enemy] }).eachBlock((block) => {
      const position = block.read(Position);
      for (let row = 0; row < block.count; ++row) {
        assert.ok(isOnBorder(position.x.get(row), position.y.get(row)), `enemy far from the edge: ${position.x.get(row)}, ${position.y.get(row)}`);
      }
    });

    arena.stepSeconds(5);
    assert.equal(arena.count(Enemy), 7);
  });
});

test("render: menu with no player, HUD in playing, pause and game over", () => {
  withArena(runtime, (arena) => {
    arena.step();
    assert.ok(arena.drawer.texts().includes("OLY ARENA"));
    assert.ok(arena.drawer.texts().includes("Enter or click to play"));
    assert.equal(arena.drawer.circles().length, 0);

    arena.tap("Enter");
    arena.step();
    assert.ok(arena.drawer.texts().includes("Health 3"));
    assert.ok(arena.drawer.texts().includes("Score 0"));
    assert.ok(arena.drawer.texts().includes("Dash ready"));
    assert.equal(arena.drawer.circles().length, arena.count(Collider));

    arena.tap("Escape");
    assert.ok(arena.drawer.texts().includes("Paused — Esc to continue"));
  });
});

test("script: menu → playing → wave → death → gameOver with an empty world → play again from scratch", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    arena.engine.world.set(arena.player(), Health, { value: 1 });

    for (let guard = 0; arena.engine.state?.name !== "gameOver" && guard < 60 * 30; ++guard) {
      arena.step();
    }

    assert.equal(arena.engine.state?.name, "gameOver");
    assert.equal(arena.count(), 0);
    assert.notEqual(arena.score.finalScore, null);
    assert.ok(arena.drawer.texts().includes(`Game over — ${arena.score.finalScore} points`));
    assert.ok(arena.drawer.texts().includes("Enter or click to play again"));

    arena.tap("Enter");
    arena.step();
    assert.equal(arena.engine.state?.name, "playing");
    assert.deepEqual(arena.score, { kills: 0, coins: 0, score: 0, finalScore: null });
    assert.equal(arena.count(Player), 1);
    assert.equal(arena.count(Enemy), 0);

    arena.stepSeconds(2.05);
    assert.equal(arena.count(Coin), 1);
  });
});

test("bullet outside the arena is discarded before collision: it does not hit the enemy at the edge", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    const enemy = spawnEnemy(arena, 0, 100);
    spawnBullet(arena, -1, 100);

    arena.step();

    assert.equal(arena.count(Bullet), 0);
    assert.equal(arena.engine.world.isAlive(enemy), true);
  });
});

test("player beyond the wall returns to the arena before collision: it does not take the coin outside", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    arena.engine.world.set(arena.player(), Position, { x: 1100, y: 270 });
    const coin = arena.engine.world.spawn([Coin, Position, Collider], { Position: { x: 1100, y: 270 }, Collider: { radius: 8 } });

    arena.step();

    assert.equal(arena.engine.world.isAlive(coin), true);
    assert.equal(arena.score.coins, 0);
  });
});

test("Esc on the step the player dies: gameOver without pause", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    arena.engine.world.set(arena.player(), Health, { value: 1 });
    spawnEnemy(arena, 480, 270);

    arena.tap("Escape");

    assert.equal(arena.engine.state?.name, "gameOver");
    assert.equal(arena.engine.paused, false);
  });
});

test("leaving paused playing through a host requestState lifts the pause", () => {
  withArena(runtime, (arena) => {
    startPlaying(arena);
    arena.tap("Escape");

    arena.engine.requestState(menu);
    arena.step();

    assert.equal(arena.engine.state?.name, "menu");
    assert.equal(arena.engine.paused, false);
  });
});
