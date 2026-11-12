# npm is the one next to node: in the emsdk image it is not on the PATH.
NPM = $(dir $(shell command -v node))npm

PRESET ?= debug-asan
T ?= .

configure:
	cmake --preset $(PRESET)
.PHONY: configure

build: configure
	cmake --build --preset $(PRESET) -j
.PHONY: build

test: build
	ctest --preset $(PRESET) -R '$(T)' --no-tests=error
.PHONY: test

bench:
	$(MAKE) build PRESET=release
.PHONY: bench

run: test
.PHONY: run

all: test

ts:
	([ -d node_modules ] || $(NPM) ci --no-audit --no-fund) && $(NPM) run build
.PHONY: ts

games-test: ts
	$(MAKE) test PRESET=wasm-games
.PHONY: games-test

# game/serve only work with the wasm-games* presets: without PRESET on the command line or in the environment, Release applies.
WEB_PRESET = $(if $(filter command line environment,$(origin PRESET)),$(PRESET),wasm-games-release)
WEB_DIR = build/$(WEB_PRESET)/web/$(G)
ESBUILD = node_modules/.bin/esbuild games/$(G)/dist/src/main.js --bundle --format=esm --platform=browser --target=es2022

ifneq ($(filter game serve,$(MAKECMDGOALS)),)
ifeq ($(G),)
$(error specify the game: make game G=<game>)
endif
ifeq ($(filter wasm-games%,$(WEB_PRESET)),)
$(error make game/serve require a wasm-games* preset (got: $(WEB_PRESET)))
endif
endif

game: ts
	$(MAKE) build PRESET=$(WEB_PRESET)
	mkdir -p $(WEB_DIR)
	cp games/$(G)/web/index.html $(WEB_DIR)/
	cp build/$(WEB_PRESET)/games/$(G)_game.mjs build/$(WEB_PRESET)/games/$(G)_game.wasm $(WEB_DIR)/
	$(ESBUILD) --outfile=$(WEB_DIR)/main.js
.PHONY: game

serve: game
	$(ESBUILD) --outfile=$(WEB_DIR)/main.js --servedir=$(WEB_DIR) --serve=0.0.0.0:8000
.PHONY: serve
