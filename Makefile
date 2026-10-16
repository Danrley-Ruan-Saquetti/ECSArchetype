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
