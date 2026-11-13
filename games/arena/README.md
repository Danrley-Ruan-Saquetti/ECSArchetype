# Arena

Shooter that validates the engine (phase 7, §4.9 of the spec). C++ runs the per-entity gameplay; TS does render, HUD, state hooks and waves.

## How to run

```sh
WC="docker compose run --rm wasm"
$WC make games-test                                          # includes ts.arena.smoke
docker compose run --rm --service-ports web make serve G=arena   # open http://localhost:8000 in Chromium
$WC make game G=arena                                        # only assembles the static folder in build/wasm-games-release/web/arena/
```

`make serve` needs an open stdin (interactive terminal): esbuild exits when stdin closes.
The server serves `arena_game.wasm` as `application/wasm`.

## Manual checklist (Chromium)

Done by the agent with the Playwright MCP at `http://localhost:8000` (Chromium), on 2026-11-12. Checked: verified; `(no)`: not verified by the agent, left for the author.

- [x] The page opens with no console error and shows the menu.
- [x] Enter starts. (Click: covered only by the smoke test.)
- [x] WASD moves; the normalized diagonal is covered by the smoke test (not seen by eye).
- [x] Holding the left button shoots toward the mouse (bullets disappear; shot kills is covered by the smoke test).
- [x] Right click does not open the browser menu (`contextmenu` with `defaultPrevented`) and Space does not scroll the page.
- [x] Space dashes and the HUD shows the recharge (not seen by eye; covered by the smoke test).
- [x] Waves at the edges every 5 s chase the player.
- [x] Loose coins appear every 2 s.
- [x] Esc pauses and resumes with everything frozen.
- [x] Damage and game over: with the player standing still, 3 hits lead to "Game over — 0 points".
- [x] Enter plays again with a reset score and a clean arena.
- [ ] (no) Switching tabs and coming back without a jump: only simulated with a fake `visibilitychange`, no visible jump; still needs checking with a real tab.
- [ ] (no) F5 reloads; the game feel (initial numbers, not measured) needs someone who plays.
- [ ] (no) Dropping a coin on kill and scoring while playing; the Chromium profiler if it stutters.

## The phase list (§4.9)

**Missing from the engine (added)**
- `single`/`trySingle` in C++ and in TS: player position in `chasePlayer` and `fireWeapon`, health and dash in the HUD (commits "single and trySingle in the ECS Query" and "single and trySingle in @oly/core").
- Boundary lint in the `wasm-games*` presets: the games' `DEPENDS` was not checked (arena skeleton commit).

**Done in the game because it belongs to the game**
- Canvas 2D render through `Drawer`, brute-force collision, score, waves, randomness (`minstd_rand` with a fixed seed in C++, `random` injected in TS) and the initial numbers.

**Worked around**
- The right-click menu: `attachBrowserInput` only prevents it when `MouseRight` is bound to an action, and the arena does not use that button. `main.ts` prevents it on its own. The generic fix (an `attachBrowserInput` option) waits until a second game needs it; today only the arena does.
- The test TS projects emit to `web/dist/tests/` (not to `dist-tests/`) so that the relative imports `../schema.js` and `../src/game.js` exist at run time.
