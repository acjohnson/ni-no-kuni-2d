# ==============================================================================
# Project Hearth — Zero-Config Multi-Target Native & Retro Console Makefile
#
# Targets:
#   make               -> Auto-detect host OS and build native executable
#   make run           -> Build & launch native executable
#   make linux         -> Native Linux amd64 executable (SDL2, zero browser)
#   make core-test     -> Zero-dependency C89 + C99 core verification suite
#   make windows       -> Cross-compile Windows x64 .exe (auto-fetches SDL2 MinGW)
#   make genesis       -> Build Sega Genesis .bin ROM (implicit Docker SGDK)
#   make snes          -> Build Super Nintendo .sfc ROM (auto-fetches PVSnesLib)
#   make macos         -> Native macOS executable (Homebrew SDL2)
#   make deps-ubuntu   -> Install Ubuntu Noble packages (gcc, SDL2, mingw-w64)
#   make clean         -> Remove build/ (keeps cached .toolchains/)
#   make distclean     -> Remove both build/ and .toolchains/
#   make help          -> Show all targets
# ==============================================================================

APP_NAME        := hearth
BUILD_DIR       := build
TOOLCHAIN_DIR   := .toolchains

CORE_DIR        := engine/core
PLAT_DIR        := engine/platform
CORE_SRCS       := $(CORE_DIR)/hearth_data.c $(CORE_DIR)/hearth_core.c
CORE_HDRS       := $(CORE_DIR)/hearth_types.h $(CORE_DIR)/hearth_prng.h \
                   $(CORE_DIR)/hearth_data.h $(CORE_DIR)/hearth_core.h

SDL2_SRC        := $(PLAT_DIR)/sdl2_main.c
TEST_SRC        := $(PLAT_DIR)/headless_test.c
GENESIS_SRC     := $(PLAT_DIR)/genesis_main.c
SNES_SRC        := $(PLAT_DIR)/snes_main.c

# Strict portable compiler flags
CFLAGS_COMMON   := -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -I$(CORE_DIR)
CFLAGS_STRICT   := -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I$(CORE_DIR)
CFLAGS_C89      := -std=c89 -O2 -Wall -Wextra -Werror -pedantic -I$(CORE_DIR)

# Detect Host OS
UNAME_S         := $(shell uname -s 2>/dev/null || echo Unknown)
ifeq ($(UNAME_S),Linux)
    HOST_TARGET := linux
    HOST_BIN    := $(BUILD_DIR)/linux/$(APP_NAME)
else ifeq ($(UNAME_S),Darwin)
    HOST_TARGET := macos
    HOST_BIN    := $(BUILD_DIR)/macos/$(APP_NAME)
else
    HOST_TARGET := windows
    HOST_BIN    := $(BUILD_DIR)/windows/$(APP_NAME).exe
endif

CC              ?= gcc

# Linux / macOS SDL2 flags (pkg-config first, fallback to sdl2-config)
SDL2_CFLAGS     := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL2_LIBS       := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)

# Windows MinGW-w64 cross-compile settings (auto-downloads SDL2 MinGW dev if absent)
MINGW_CC        ?= x86_64-w64-mingw32-gcc
SDL2_MINGW_VER  ?= 2.30.10
SDL2_MINGW_URL  ?= https://github.com/libsdl-org/SDL/releases/download/release-$(SDL2_MINGW_VER)/SDL2-devel-$(SDL2_MINGW_VER)-mingw.tar.gz
MINGW_SDL2_DIR  ?= $(TOOLCHAIN_DIR)/SDL2-mingw/SDL2-$(SDL2_MINGW_VER)/x86_64-w64-mingw32

# Sega Genesis (SGDK): uses local GDK if installed, otherwise Docker SGDK implicitly
SGDK_IMAGE      ?= registry.gitlab.com/doragasu/docker-sgdk:latest

# Super Nintendo (PVSnesLib): auto-downloads & caches latest Linux release if unset
PVSNESLIB_CACHE := $(CURDIR)/$(TOOLCHAIN_DIR)/pvsneslib

.PHONY: all linux macos windows genesis genesis-stage snes core-test run deps-ubuntu clean distclean help

# Default target builds for the current machine
all: $(HOST_TARGET)

# ------------------------------------------------------------------------------
# 1. NATIVE LINUX (Ubuntu Noble amd64 & other Linux distros)
# ------------------------------------------------------------------------------
linux: $(BUILD_DIR)/linux/$(APP_NAME)

$(BUILD_DIR)/linux/$(APP_NAME): $(CORE_SRCS) $(CORE_HDRS) $(SDL2_SRC)
	@if [ -z "$(SDL2_CFLAGS)" ] && [ ! -f /usr/include/SDL2/SDL.h ]; then \
		echo "ERROR: SDL2 development headers not found."; \
		echo "On Ubuntu Noble amd64, run: make deps-ubuntu"; \
		echo "  (or: sudo apt update && sudo apt install -y build-essential libsdl2-dev pkg-config)"; \
		exit 1; \
	fi
	@mkdir -p $(BUILD_DIR)/linux
	$(CC) $(CFLAGS_COMMON) $(if $(SDL2_CFLAGS),$(SDL2_CFLAGS),-I/usr/include/SDL2) \
		$(CORE_SRCS) $(SDL2_SRC) \
		-o $@ $(if $(SDL2_LIBS),$(SDL2_LIBS),-lSDL2)
	@echo "Built native Linux binary: $@"

# ------------------------------------------------------------------------------
# 2. NATIVE MACOS (Homebrew SDL2)
# ------------------------------------------------------------------------------
macos: $(BUILD_DIR)/macos/$(APP_NAME)

$(BUILD_DIR)/macos/$(APP_NAME): $(CORE_SRCS) $(CORE_HDRS) $(SDL2_SRC)
	@if [ -z "$(SDL2_CFLAGS)" ]; then \
		echo "ERROR: SDL2 not found. Install via Homebrew: brew install sdl2 pkg-config"; \
		exit 1; \
	fi
	@mkdir -p $(BUILD_DIR)/macos
	$(CC) $(CFLAGS_COMMON) $(SDL2_CFLAGS) $(CORE_SRCS) $(SDL2_SRC) -o $@ $(SDL2_LIBS)
	@echo "Built native macOS binary: $@"

# ------------------------------------------------------------------------------
# 3. WINDOWS x64 CROSS-COMPILE FROM LINUX (MinGW-w64 + auto-fetched SDL2)
# ------------------------------------------------------------------------------
windows: $(BUILD_DIR)/windows/$(APP_NAME).exe

$(BUILD_DIR)/windows/$(APP_NAME).exe: $(CORE_SRCS) $(CORE_HDRS) $(SDL2_SRC)
	@command -v $(MINGW_CC) >/dev/null 2>&1 || { \
		echo "ERROR: $(MINGW_CC) not found. Install with: sudo apt install -y mingw-w64"; \
		exit 1; \
	}
	@if [ ! -f "$(MINGW_SDL2_DIR)/include/SDL2/SDL.h" ]; then \
		echo "Fetching SDL2 $(SDL2_MINGW_VER) MinGW dev package into $(TOOLCHAIN_DIR)/SDL2-mingw/..."; \
		mkdir -p $(TOOLCHAIN_DIR)/SDL2-mingw; \
		curl -fsSL "$(SDL2_MINGW_URL)" | tar -xz -C $(TOOLCHAIN_DIR)/SDL2-mingw; \
	fi
	@mkdir -p $(BUILD_DIR)/windows
	$(MINGW_CC) $(CFLAGS_COMMON) \
		-I$(MINGW_SDL2_DIR)/include \
		$(CORE_SRCS) $(SDL2_SRC) \
		-L$(MINGW_SDL2_DIR)/lib -lSDL2 -static-libgcc -mwindows \
		-o $@
	@if [ -f "$(MINGW_SDL2_DIR)/bin/SDL2.dll" ]; then \
		cp "$(MINGW_SDL2_DIR)/bin/SDL2.dll" "$(BUILD_DIR)/windows/SDL2.dll"; \
	fi
	@echo "Built Windows x64 executable: $@"

# ------------------------------------------------------------------------------
# 4. ZERO-DEPENDENCY CORE VERIFICATION SUITE (Checks BOTH strict C89 & C99!)
# ------------------------------------------------------------------------------
core-test: $(BUILD_DIR)/test/$(APP_NAME)_core_test
	@$(CC) $(CFLAGS_C89) -fsyntax-only $(CORE_SRCS)
	@echo "[PASS] engine/core compiles cleanly under strict ANSI C89 (SNES 816-tcc safe)"
	@./$(BUILD_DIR)/test/$(APP_NAME)_core_test

$(BUILD_DIR)/test/$(APP_NAME)_core_test: $(CORE_SRCS) $(CORE_HDRS) $(TEST_SRC)
	@mkdir -p $(BUILD_DIR)/test
	$(CC) $(CFLAGS_STRICT) $(CORE_SRCS) $(TEST_SRC) -o $@

# ------------------------------------------------------------------------------
# 5. SEGA GENESIS / MEGA DRIVE ROM (Implicit Docker SGDK — Zero Manual Setup!)
# ------------------------------------------------------------------------------
genesis-stage: $(CORE_SRCS) $(CORE_HDRS) $(GENESIS_SRC)
	@rm -rf $(BUILD_DIR)/genesis/src
	@mkdir -p $(BUILD_DIR)/genesis/src $(BUILD_DIR)/genesis/res
	@cp $(CORE_SRCS) $(CORE_HDRS) $(BUILD_DIR)/genesis/src/
	@cp $(GENESIS_SRC) $(BUILD_DIR)/genesis/src/main.c

genesis: genesis-stage
	@if [ -n "$(GDK)" ] && [ -f "$(GDK)/makefile.gen" ]; then \
		echo "Building Sega Genesis ROM using local SGDK at $(GDK)..."; \
		GDK="$(GDK)" $(MAKE) -C $(BUILD_DIR)/genesis -f "$(GDK)/makefile.gen"; \
	else \
		command -v docker >/dev/null 2>&1 || { \
			echo "ERROR: Docker is required for automatic SGDK builds (or set GDK=/path/to/sgdk)."; \
			echo "Install Docker on Ubuntu Noble: sudo apt install -y docker.io"; \
			exit 1; \
		}; \
		echo "Building Sega Genesis ROM via Docker SGDK ($(SGDK_IMAGE))..."; \
		docker run --rm --user "$$(id -u):$$(id -g)" \
			-v "$(CURDIR)/$(BUILD_DIR)/genesis:/m68k" \
			$(SGDK_IMAGE); \
	fi
	@echo "Built Sega Genesis ROM: $(BUILD_DIR)/genesis/out/rom.bin"

# ------------------------------------------------------------------------------
# 6. SUPER NINTENDO (SNES) ROM (Auto-Fetches PVSnesLib — Zero Manual Setup!)
# ------------------------------------------------------------------------------
snes: $(CORE_SRCS) $(CORE_HDRS) $(SNES_SRC)
	@set -e; \
	PV_ROOT=""; \
	if [ -n "$(PVSNESLIB_HOME)" ] && [ -f "$(PVSNESLIB_HOME)/devkitsnes/snes_rules" ]; then \
		PV_ROOT="$(PVSNESLIB_HOME)"; \
	else \
		RULES_PATH=$$(find "$(PVSNESLIB_CACHE)" -maxdepth 4 -path '*/devkitsnes/snes_rules' 2>/dev/null | head -n 1 || true); \
		if [ -z "$$RULES_PATH" ]; then \
			command -v curl >/dev/null 2>&1 || { echo "ERROR: curl required (sudo apt install -y curl unzip)"; exit 1; }; \
			command -v unzip >/dev/null 2>&1 || { echo "ERROR: unzip required (sudo apt install -y curl unzip)"; exit 1; }; \
			echo "Downloading latest PVSnesLib Linux release into $(TOOLCHAIN_DIR)/pvsneslib/..."; \
			mkdir -p "$(PVSNESLIB_CACHE)"; \
			API_JSON=$$(curl -fsSL "https://api.github.com/repos/alekmaul/pvsneslib/releases/latest"); \
			DL_URL=$$(printf '%s\n' "$$API_JSON" | grep -Eo 'https://[^"]*64b_linux[^"]*\.zip' | head -n 1 || true); \
			if [ -z "$$DL_URL" ]; then \
				DL_URL=$$(printf '%s\n' "$$API_JSON" | grep -Eo 'https://[^"]*linux[^"]*\.zip' | head -n 1 || true); \
			fi; \
			if [ -z "$$DL_URL" ]; then \
				echo "ERROR: Could not locate PVSnesLib Linux zip from GitHub API."; \
				exit 1; \
			fi; \
			echo "Fetching $$DL_URL ..."; \
			curl -fsSL "$$DL_URL" -o "$(PVSNESLIB_CACHE)/pvsneslib.zip"; \
			unzip -oq "$(PVSNESLIB_CACHE)/pvsneslib.zip" -d "$(PVSNESLIB_CACHE)"; \
			rm -f "$(PVSNESLIB_CACHE)/pvsneslib.zip"; \
			RULES_PATH=$$(find "$(PVSNESLIB_CACHE)" -maxdepth 4 -path '*/devkitsnes/snes_rules' | head -n 1); \
		fi; \
		PV_ROOT=$$(dirname "$$(dirname "$$RULES_PATH")"); \
		chmod +x "$$PV_ROOT"/devkitsnes/bin/* "$$PV_ROOT"/devkitsnes/tools/* 2>/dev/null || true; \
	fi; \
	echo "Using PVSnesLib at $$PV_ROOT"; \
	rm -rf "$(BUILD_DIR)/snes"; \
	mkdir -p "$(BUILD_DIR)/snes/src"; \
	cp $(CORE_SRCS) $(CORE_HDRS) "$(BUILD_DIR)/snes/src/"; \
	cp $(SNES_SRC) "$(BUILD_DIR)/snes/src/main.c"; \
	printf '%s\n' \
		'export ROMNAME := hearth' \
		'export ROMTITLE := PROJECT HEARTH' \
		'export ROMSIZE := 08' \
		'export SRAMSIZE := 00' \
		'export COUNTRY := 01' \
		'SRC := src' \
		'CFLAGS += -DHEARTH_TARGET_SNES -Isrc' \
		"include $${PV_ROOT}/devkitsnes/snes_rules" \
		'all: $$(ROMNAME).sfc' > "$(BUILD_DIR)/snes/Makefile"; \
	if ! grep -q "AUTOHDR" "$$PV_ROOT/devkitsnes/snes_rules" 2>/dev/null; then \
		SAMPLE_HDR=$$(find "$$PV_ROOT" -name "hdr.asm" | head -n 1 || true); \
		if [ -n "$$SAMPLE_HDR" ]; then cp "$$SAMPLE_HDR" "$(BUILD_DIR)/snes/hdr.asm"; fi; \
	fi; \
	PVSNESLIB_HOME="$$PV_ROOT" $(MAKE) -C "$(BUILD_DIR)/snes"; \
	echo "Built SNES ROM: $(BUILD_DIR)/snes/hearth.sfc"

# ------------------------------------------------------------------------------
# UTILITIES
# ------------------------------------------------------------------------------
run: $(HOST_TARGET)
	@./$(HOST_BIN)

deps-ubuntu:
	@echo "Installing Ubuntu Noble amd64 native & cross-compile dependencies..."
	sudo apt-get update && sudo apt-get install -y build-essential libsdl2-dev pkg-config curl unzip mingw-w64

clean:
	rm -rf $(BUILD_DIR)

distclean:
	rm -rf $(BUILD_DIR) $(TOOLCHAIN_DIR)

help:
	@echo "Project Hearth — Native C99 & Multi-Platform Makefile"
	@echo ""
	@echo "Host detected: $(UNAME_S) -> default target: $(HOST_TARGET)"
	@echo ""
	@echo "Targets:"
	@echo "  make              Build native executable for this OS ($(HOST_BIN))"
	@echo "  make run          Build and launch the native game window"
	@echo "  make core-test    Compile & run strict C89/C99 core verification suite"
	@echo "  make linux        Build Linux amd64 native SDL2 executable"
	@echo "  make windows      Cross-compile Windows x64 .exe (auto-fetches SDL2 MinGW)"
	@echo "  make genesis      Build Sega Genesis ROM (.bin) via Docker SGDK automatically"
	@echo "  make snes         Build SNES ROM (.sfc) via auto-downloaded PVSnesLib"
	@echo "  make macos        Build macOS native SDL2 executable"
	@echo "  make deps-ubuntu  Install Ubuntu Noble packages (gcc, SDL2, curl, unzip, mingw-w64)"
	@echo "  make clean        Remove build/ (keeps cached .toolchains/)"
	@echo "  make distclean    Remove build/ and cached .toolchains/"
