BUILD_DIR=build

configure:
	mkdir -p $(BUILD_DIR) && cd $(BUILD_DIR) && cmake ..
.PHONY: configure

build: configure
	cd $(BUILD_DIR) && cmake --build .
.PHONY: build

run: build
	./$(BUILD_DIR)/bin/ECSArchetype
.PHONY: run

all: run
