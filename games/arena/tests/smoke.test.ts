import assert from "node:assert/strict";
import { test } from "node:test";

import { Health, Player, Position } from "../schema.js";
import { loadArena, withArena } from "./harness.js";

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
