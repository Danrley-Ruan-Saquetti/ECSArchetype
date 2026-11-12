import type { QuerySpec, SystemContext, SystemFunction } from "@oly/core";

import { Bullet, Coin, Collider, Dash, Enemy, Health, Invulnerable, Player, Position, gameOver, menu, playing } from "../schema.js";
import { ArenaHeight, ArenaWidth } from "./arena.js";
import type { ArenaScore } from "./game.js";

export interface Drawer {
  clear(): void;
  circle(x: number, y: number, radius: number, color: string): void;
  rect(x: number, y: number, width: number, height: number, color: string): void;
  text(x: number, y: number, content: string, color: string): void;
}

const Colors = {
  background: "#101418",
  player: "#4fc3f7",
  playerInvulnerable: "#ffffff",
  enemy: "#ef5350",
  bullet: "#ffee58",
  coin: "#ffd54f",
  text: "#e0e0e0",
} as const;

const HudMargin = 16;
const HudLineHeight = 24;

type Filter = Pick<QuerySpec, "with" | "without">;

function drawCircles(context: SystemContext, drawer: Drawer, filter: Filter, color: string): void {
  context.query({ read: [Position, Collider], ...filter }).eachBlock((block) => {
    const position = block.read(Position);
    const collider = block.read(Collider);
    for (let row = 0; row < block.count; ++row) {
      drawer.circle(position.x.get(row), position.y.get(row), collider.radius.get(row), color);
    }
  });
}

function drawCentered(drawer: Drawer, lines: readonly string[]): void {
  lines.forEach((line, index) => {
    drawer.text(ArenaWidth / 2 - line.length * 4, ArenaHeight / 2 + index * HudLineHeight, line, Colors.text);
  });
}

function drawHud(context: SystemContext, drawer: Drawer, score: ArenaScore): void {
  const player = context.query({ read: [Health, Dash], with: [Player] }).trySingle();
  if (player === null) {
    return;
  }

  const dashLabel = player.get(Dash).ready === 1 ? "Dash pronto" : "Dash recarregando";
  drawer.text(HudMargin, HudMargin + HudLineHeight, `Vida ${player.get(Health).value}`, Colors.text);
  drawer.text(HudMargin, HudMargin + 2 * HudLineHeight, `Pontos ${score.score}`, Colors.text);
  drawer.text(HudMargin, HudMargin + 3 * HudLineHeight, dashLabel, Colors.text);
}

// Sem interpolação: desenha a posição do último passo.
export function createRenderSystem(drawer: Drawer, score: ArenaScore): SystemFunction {
  return (context) => {
    drawer.clear();
    drawer.rect(0, 0, ArenaWidth, ArenaHeight, Colors.background);
    drawCircles(context, drawer, { with: [Coin] }, Colors.coin);
    drawCircles(context, drawer, { with: [Bullet] }, Colors.bullet);
    drawCircles(context, drawer, { with: [Enemy] }, Colors.enemy);
    drawCircles(context, drawer, { with: [Player], without: [Invulnerable] }, Colors.player);
    drawCircles(context, drawer, { with: [Player, Invulnerable] }, Colors.playerInvulnerable);

    const stateName = context.state?.name;
    if (stateName === menu.name) {
      drawCentered(drawer, ["OLY ARENA", "Enter ou clique para jogar"]);
    } else if (stateName === gameOver.name) {
      drawCentered(drawer, [`Fim de jogo — ${score.finalScore ?? score.score} pontos`, "Enter ou clique para jogar de novo"]);
    } else if (stateName === playing.name) {
      drawHud(context, drawer, score);
      if (context.paused) {
        drawCentered(drawer, ["Pausado — Esc para continuar"]);
      }
    }
  };
}
