# Arena

Jogo de tiro de validação da engine (fase 7, §4.9 do spec). O C++ faz o gameplay por entidade; o TS faz render, HUD, ganchos de estado e ondas.

## Como rodar

```sh
WC="docker compose run --rm wasm"
$WC make games-test                                          # inclui ts.arena.smoke
docker compose run --rm --service-ports web make serve G=arena   # abra http://localhost:8000 no Chromium
$WC make game G=arena                                        # só monta a pasta estática em build/wasm-games-release/web/arena/
```

O `make serve` precisa de stdin aberto (terminal interativo): o esbuild encerra quando o stdin fecha.
O servidor entrega `arena_game.wasm` como `application/wasm`.

## Checklist manual (Chromium)

Feito pelo agente com o MCP do Playwright em `http://localhost:8000` (Chromium), em 2026-11-12. Marcado: conferido; `(não)`: não conferido pelo agente, falta o autor.

- [x] A página abre sem erro no console e mostra o menu.
- [x] Enter começa. (Clique: coberto só pelo teste de fumaça.)
- [x] WASD move; a diagonal normalizada é coberta pelo teste de fumaça (não vista a olho).
- [x] Segurar o botão esquerdo atira na direção do mouse (balas somem; tiro mata é coberto pelo teste de fumaça).
- [x] Clique direito não abre o menu do navegador (`contextmenu` com `defaultPrevented`) e Space não rola a página.
- [x] Space faz dash e o HUD mostra a recarga (não vista a olho; coberto pelo teste de fumaça).
- [x] Ondas nas bordas a cada 5 s perseguem o jogador.
- [x] Moedas soltas aparecem a cada 2 s.
- [x] Esc pausa e retoma com tudo congelado.
- [x] Dano e fim de jogo: com o jogador parado, 3 danos levam a "Fim de jogo — 0 pontos".
- [x] Enter joga de novo com placar zerado e arena limpa.
- [ ] (não) Trocar de aba e voltar sem salto: só simulado com `visibilitychange` falso, sem salto visível; falta conferir com aba de verdade.
- [ ] (não) F5 recarrega; a sensação do jogo (números iniciais, sem medição) precisa de quem joga.
- [ ] (não) Soltar moeda ao matar e pontuar jogando; o profiler do Chromium se travar.

## A lista da fase (§4.9)

**Faltava na engine (adicionado)**
- `single`/`trySingle` em C++ e em TS: posição do jogador em `chasePlayer` e `fireWeapon`, vida e dash no HUD (commits "single e trySingle no Query do ECS" e "single e trySingle no @oly/core").
- Lint de fronteiras nos presets `wasm-games*`: o `DEPENDS` dos jogos não era conferido (commit do esqueleto do arena).

**Feito no jogo por ser do jogo**
- Render em Canvas 2D por `Drawer`, colisão por força bruta, placar, ondas, aleatoriedade (`minstd_rand` com semente fixa no C++, `random` injetado no TS) e os números iniciais.

**Contornado**
- O menu do clique direito: `attachBrowserInput` só o impede quando `MouseRight` está ligado a uma ação, e o arena não usa esse botão. O `main.ts` impede por conta própria. A correção genérica (uma opção do `attachBrowserInput`) fica para quando um segundo jogo pedir; hoje só o arena precisa.
- Os projetos TS dos testes emitem em `dist/tests/` (não em `dist-tests/`) para que os imports relativos `../schema.js` e `../src/game.js` existam em tempo de execução.
