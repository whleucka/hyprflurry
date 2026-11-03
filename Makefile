# Thin wrapper around CMake so the usual make targets work.

BUILD_DIR ?= build
BUILD_TYPE ?= Release
PREFIX ?= /usr/local
GENERATOR := $(shell command -v ninja >/dev/null 2>&1 && echo Ninja || echo "Unix Makefiles")

.PHONY: all configure run debug install uninstall clean

all: configure
	@cmake --build $(BUILD_DIR)

configure:
	@test -f $(BUILD_DIR)/CMakeCache.txt || \
		cmake -S . -B $(BUILD_DIR) -G "$(GENERATOR)" \
			-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_INSTALL_PREFIX=$(PREFIX)

run: all
	./$(BUILD_DIR)/hyprflurry -c ./hyprflurry.conf

debug:
	@$(MAKE) --no-print-directory BUILD_DIR=build-debug BUILD_TYPE=Debug

install: all
	cmake --install $(BUILD_DIR)

uninstall:
	xargs rm -f < $(BUILD_DIR)/install_manifest.txt

clean:
	rm -rf build build-debug
