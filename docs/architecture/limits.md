# Known limitations and out of scope

- **Out of the engine's scope**: render, physics, audio, camera, UI, assets, networking, serialization, editor, threads and gamepad. Render and collision belong to the game ([games.md](games.md)).
- **No real native host** (window, SDL). The core is ready for one: the host only calls `frame`, `setHidden` and the input functions.
- **ECS v1**: no fixed-size chunks, no change detection, no enableable components, no sparse storage. `ComponentId` is only valid inside the process.
- **NF-P5** is not met at N = 100000 ([performance.md](performance.md)).
- **Two `Engine`s on the same `World`** share the scope tag: a state change in one cleans up the scoped entities of the other. `capi` avoids this by creating one `World` per `OlyEngine`.
- **Arena workarounds** (recorded in `games/arena/README.md`): the right-click menu is prevented by `main.ts` itself, because `attachBrowserInput` only prevents it when `MouseRight` is bound to an action.
