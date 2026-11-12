# Jogos: `games/arena`

O arena é o jogo de validação: um tiro 2D com visão de cima (mover, mirar, atirar, dash, ondas de inimigos, moedas, menu → jogando → fim de jogo). Ele é cliente da engine: se algo genérico só pudesse ser feito com gambiarra fora da engine, a engine estaria incompleta.

```
games/arena/
  schema.ts          componentes, tags, eventos, ações e estados do jogo
  native/            gameplay C++ por entidade: controle, tiro, perseguição, movimento, colisão
  src/game.ts        installGame(engine, { drawer, random }): registra tudo, sem DOM
  src/render.ts      render por uma interface Drawer
  src/main.ts        casca do navegador: canvas, Drawer sobre Canvas 2D, runInBrowser, attachBrowserInput
  src/arena.ts       constantes compartilhadas com native/arena.hpp
  tests/smoke.test.ts  roteiro em node contra o WASM real (ts.arena.smoke)
  web/index.html
  README.md          como rodar, checklist manual e a lista da fase
```

## Divisão entre as linguagens

- **C++**: tudo o que roda por entidade (sistemas `steerPlayer`, `fireWeapon`, `chasePlayer`, `integrate`, `confine`, `collide`; handlers `dropCoin`, `destroyDead`).
- **TS**: render, HUD, ligações de teclas, ganchos de estado, ondas e placar. A fronteira é usada por sistema, por bloco e por lote.

O jogo sem DOM (`installGame`) fica separado da casca do navegador (`main.ts`). Isso permite o teste de fumaça em node com relógio, input e `Drawer` falsos. O `tsconfig.json` do jogo não tem `lib: ["DOM"]`, então usar DOM fora do `main.ts` é erro de compilação.

## Servir no navegador

`make game G=arena` monta uma pasta estática em `build/wasm-games-release/web/arena/` (`index.html`, `main.js` do esbuild, `.mjs` e `.wasm`). O `.mjs` não entra no bundle: é carregado em tempo de execução. `make serve G=arena`, no serviço `web`, serve essa pasta na porta 8000 ([build-e-testes.md](build-e-testes.md)).

O checklist manual e a lista do que faltava na engine, do que é do jogo e do que foi contornado ficam em `games/arena/README.md`.
