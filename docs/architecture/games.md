# Games: `games/arena`

Arena is the validation game: a top-down 2D shooter (move, aim, shoot, dash, enemy waves, coins, menu → playing → game over). It is a client of the engine: if something generic could only be done with a hack outside the engine, the engine would be incomplete.

```
games/arena/
  schema.ts          the game's components, tags, events, actions and states
  native/            per-entity C++ gameplay: control, shooting, chasing, movement, collision
  src/game.ts        installGame(engine, { drawer, random }): registers everything, no DOM
  src/render.ts      render through a Drawer interface
  src/main.ts        browser shell: canvas, Drawer over Canvas 2D, runInBrowser, attachBrowserInput
  src/arena.ts       constants shared with native/arena.hpp
  tests/smoke.test.ts  script in node against the real WASM (ts.arena.smoke)
  web/index.html
  README.md          how to run, manual checklist and the phase list
```

## Split between the languages

- **C++**: everything that runs per entity (systems `steerPlayer`, `fireWeapon`, `chasePlayer`, `integrate`, `confine`, `collide`; handlers `dropCoin`, `destroyDead`).
- **TS**: render, HUD, key bindings, state hooks, waves and score. The boundary is used per system, per block and per batch.

The DOM-free game (`installGame`) is kept separate from the browser shell (`main.ts`). This allows the smoke test in node with a fake clock, input and `Drawer`. The game's `tsconfig.json` has no `lib: ["DOM"]`, so using the DOM outside `main.ts` is a compile error.

## Serving in the browser

`make game G=arena` assembles a static folder in `build/wasm-games-release/web/arena/` (`index.html`, esbuild's `main.js`, `.mjs` and `.wasm`). The `.mjs` does not go into the bundle: it is loaded at run time. `make serve G=arena`, in the `web` service, serves that folder on port 8000 ([build-and-tests.md](build-and-tests.md)).

The manual checklist and the list of what was missing in the engine, what belongs to the game and what was worked around are in `games/arena/README.md`.
