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
#   make wasm          -> WebAssembly C99 core for browser preview (auto-fetches Zig)
#   make web           -> Build browser preview bundle (make wasm + Node Docker/npm)
#   make web-run       -> Launch browser preview on localhost:5173 (Docker/npm)
#   make deps-ubuntu   -> Install Ubuntu Noble packages (gcc, SDL2, mingw-w64)
#   make clean         -> Remove build/ and dist/ (keeps cached .toolchains/)
#   make distclean     -> Remove build/, dist/, and .toolchains/
#   make help          -> Show all targets
# ==============================================================================

APP_NAME        := hearth
BUILD_DIR       := build
TOOLCHAIN_DIR   := .toolchains

# Single source-of-truth build version (VERSION file + optional short git commit hash)
HEARTH_VERSION  := $(strip $(shell cat VERSION 2>/dev/null || echo 0.6.0))
GIT_SHA         := $(strip $(shell git rev-parse --short=7 HEAD 2>/dev/null || true))
BUILD_VER_FULL  := v$(HEARTH_VERSION)$(if $(GIT_SHA), ($(GIT_SHA)),)

CORE_DIR        := engine/core
PLAT_DIR        := engine/platform
CORE_SRCS       := $(CORE_DIR)/hearth_data.c $(CORE_DIR)/hearth_core.c
CORE_HDRS       := $(CORE_DIR)/hearth_types.h $(CORE_DIR)/hearth_prng.h \
                   $(CORE_DIR)/hearth_data.h $(CORE_DIR)/hearth_core.h

SDL2_SRC        := $(PLAT_DIR)/sdl2_main.c
SDL2_CG_HDR     := $(PLAT_DIR)/cinema_cg_sdl2.h
ROM_CG_HDR      := $(PLAT_DIR)/cinema_cg_rom.h
HERO24_HDR      := $(PLAT_DIR)/hearth_hero24.h
ROWAN_GEN_HDR   := $(PLAT_DIR)/rowan_gen_rom.h
TERRAIN_ROM_HDR := $(PLAT_DIR)/hearth_terrain_rom.h
BATTLE_BG_ROM_HDR := $(PLAT_DIR)/battle_bg_rom.h
SNES_CG_ASM     := $(PLAT_DIR)/cinema_cg_snes.asm
SNES_ROWAN_ASM  := $(PLAT_DIR)/rowan_snes_rom.asm
SNES_TERRAIN_ASM := $(PLAT_DIR)/hearth_terrain_snes.asm
SNES_BATTLE_BG_ASM := $(PLAT_DIR)/battle_bg_snes.asm
TEST_SRC        := $(PLAT_DIR)/headless_test.c
GENESIS_SRC     := $(PLAT_DIR)/genesis_main.c
SNES_SRC        := $(PLAT_DIR)/snes_main.c
SNES_AUDIO_GEN  := engine/tools/gen_snes_audio.c
WASM_SRC        := $(PLAT_DIR)/wasm_bridge.c
WASM_OUT        := public/hearth_core.wasm
ZIG_VER         := 0.14.0
ZIG_CACHE       := $(TOOLCHAIN_DIR)/zig

# Optional compile-time Designer / Explore Evertale mode (1=enabled for dev, 0=release story-only)
DESIGNER_MODE   ?= 1
BUILD_MODE_LBL  := $(if $(filter 1,$(DESIGNER_MODE)),DESIGNER,RELEASE)
CFLAGS_VERSION  := -DHEARTH_VERSION_RAW=v$(HEARTH_VERSION)
CFLAGS_DESIGNER := -DHEARTH_DESIGNER_MODE=$(DESIGNER_MODE) $(CFLAGS_VERSION)

define PRINT_BUILD_BANNER
	@echo "========================================================================"
	@echo "  Project Hearth $(BUILD_VER_FULL)  |  Target: $(1)  |  Mode: $(BUILD_MODE_LBL)"
	@echo "========================================================================"
endef

# Strict portable compiler flags
CFLAGS_COMMON   := -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion $(CFLAGS_DESIGNER) -I$(CORE_DIR)
CFLAGS_STRICT   := -std=c99 -O2 -Wall -Wextra -Werror -pedantic $(CFLAGS_DESIGNER) -I$(CORE_DIR)
CFLAGS_C89      := -std=c89 -O2 -Wall -Wextra -Werror -pedantic $(CFLAGS_DESIGNER) -I$(CORE_DIR)

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
# (doragasu/docker-sgdk publishes versioned tags like v2.11, not :latest)
SGDK_IMAGE      ?= registry.gitlab.com/doragasu/docker-sgdk:v2.11

# Super Nintendo (PVSnesLib): auto-downloads & caches latest Linux release if unset
PVSNESLIB_CACHE := $(CURDIR)/$(TOOLCHAIN_DIR)/pvsneslib

# Browser Preview (Vite + WASM): uses local npm if present, or Docker Node automatically
NODE_IMAGE      ?= node:22-slim
WEB_PORT        ?= 5173
WEB_CACHE_DIR   := $(CURDIR)/$(TOOLCHAIN_DIR)/web

.PHONY: all linux macos windows genesis genesis-stage snes wasm web web-run core-test map-audit run deps-ubuntu clean distclean help

# Default target builds for the current machine
all: $(HOST_TARGET)

# ------------------------------------------------------------------------------
# 1. NATIVE LINUX (Ubuntu Noble amd64 & other Linux distros)
# ------------------------------------------------------------------------------
linux: $(BUILD_DIR)/linux/$(APP_NAME)

$(BUILD_DIR)/linux/$(APP_NAME): $(CORE_SRCS) $(CORE_HDRS) $(SDL2_SRC) $(SDL2_CG_HDR) $(HERO24_HDR) $(TERRAIN_ROM_HDR) $(BATTLE_BG_ROM_HDR)
	$(call PRINT_BUILD_BANNER,linux-sdl2)
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
	@echo "Built native Linux binary: $@ ($(BUILD_VER_FULL))"

# ------------------------------------------------------------------------------
# 2. NATIVE MACOS (Homebrew SDL2)
# ------------------------------------------------------------------------------
macos: $(BUILD_DIR)/macos/$(APP_NAME)

$(BUILD_DIR)/macos/$(APP_NAME): $(CORE_SRCS) $(CORE_HDRS) $(SDL2_SRC) $(SDL2_CG_HDR) $(HERO24_HDR) $(TERRAIN_ROM_HDR) $(BATTLE_BG_ROM_HDR)
	$(call PRINT_BUILD_BANNER,macos-sdl2)
	@if [ -z "$(SDL2_CFLAGS)" ]; then \
		echo "ERROR: SDL2 not found. Install via Homebrew: brew install sdl2 pkg-config"; \
		exit 1; \
	fi
	@mkdir -p $(BUILD_DIR)/macos
	$(CC) $(CFLAGS_COMMON) $(SDL2_CFLAGS) $(CORE_SRCS) $(SDL2_SRC) -o $@ $(SDL2_LIBS)
	@echo "Built native macOS binary: $@ ($(BUILD_VER_FULL))"

# ------------------------------------------------------------------------------
# 3. WINDOWS x64 CROSS-COMPILE FROM LINUX (MinGW-w64 + auto-fetched SDL2)
# ------------------------------------------------------------------------------
windows: $(BUILD_DIR)/windows/$(APP_NAME).exe

$(BUILD_DIR)/windows/$(APP_NAME).exe: $(CORE_SRCS) $(CORE_HDRS) $(SDL2_SRC) $(SDL2_CG_HDR) $(HERO24_HDR) $(TERRAIN_ROM_HDR) $(BATTLE_BG_ROM_HDR)
	$(call PRINT_BUILD_BANNER,windows-x64)
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
	@echo "Built Windows x64 executable: $@ ($(BUILD_VER_FULL))"

# ------------------------------------------------------------------------------
# 4. ZERO-DEPENDENCY CORE VERIFICATION SUITE (Checks BOTH strict C89 & C99!)
# ------------------------------------------------------------------------------
core-test: $(BUILD_DIR)/test/$(APP_NAME)_core_test
	$(call PRINT_BUILD_BANNER,core-test)
	@$(CC) $(CFLAGS_C89) -fsyntax-only $(CORE_SRCS)
	@echo "[PASS] engine/core compiles cleanly under strict ANSI C89 (SNES 816-tcc safe)"
	@./$(BUILD_DIR)/test/$(APP_NAME)_core_test

$(BUILD_DIR)/test/$(APP_NAME)_core_test: $(CORE_SRCS) $(CORE_HDRS) $(TEST_SRC)
	@mkdir -p $(BUILD_DIR)/test
	$(CC) $(CFLAGS_STRICT) $(CORE_SRCS) $(TEST_SRC) -o $@

# ------------------------------------------------------------------------------
# 4b. STANDALONE 6-PASS WORLD, AUTOTILE & PIXEL-SEAM AUDIT + ANNOTATED PNGs
# ------------------------------------------------------------------------------
map-audit: $(CORE_SRCS) $(CORE_HDRS) $(TERRAIN_ROM_HDR) engine/tools/map_audit_dump.c engine/tools/map_audit.py
	$(call PRINT_BUILD_BANNER,map-audit)
	@mkdir -p $(BUILD_DIR)/map_audit/raw
	$(CC) $(CFLAGS_COMMON) -Wno-unused-function $(CORE_SRCS) engine/tools/map_audit_dump.c -o $(BUILD_DIR)/map_audit/map_audit_dump
	@$(BUILD_DIR)/map_audit/map_audit_dump $(BUILD_DIR)/map_audit/raw
	@python3 engine/tools/map_audit.py $(BUILD_DIR)/map_audit/raw $(BUILD_DIR)/map_audit

# ------------------------------------------------------------------------------
# 5. SEGA GENESIS / MEGA DRIVE ROM (Implicit Docker SGDK — Zero Manual Setup!)
# ------------------------------------------------------------------------------
genesis-stage: $(CORE_SRCS) $(CORE_HDRS) $(GENESIS_SRC) $(ROM_CG_HDR) $(HERO24_HDR) $(ROWAN_GEN_HDR) $(TERRAIN_ROM_HDR) $(BATTLE_BG_ROM_HDR)
	@rm -rf $(BUILD_DIR)/genesis/src $(BUILD_DIR)/genesis/out
	@mkdir -p $(BUILD_DIR)/genesis/src $(BUILD_DIR)/genesis/res $(BUILD_DIR)/genesis/inc
	@cp $(CORE_SRCS) $(CORE_HDRS) $(ROM_CG_HDR) $(HERO24_HDR) $(ROWAN_GEN_HDR) $(TERRAIN_ROM_HDR) $(BATTLE_BG_ROM_HDR) $(BUILD_DIR)/genesis/src/
	@cp $(GENESIS_SRC) $(BUILD_DIR)/genesis/src/main.c

genesis: genesis-stage
	$(call PRINT_BUILD_BANNER,sega-genesis)
	@if [ -n "$(GDK)" ] && [ -f "$(GDK)/makefile.gen" ]; then \
		echo "Building Sega Genesis ROM using local SGDK at $(GDK)..."; \
		GDK="$(GDK)" $(MAKE) -C $(BUILD_DIR)/genesis -f "$(GDK)/makefile.gen" EXTRA_FLAGS="-DHEARTH_DESIGNER_MODE=$(DESIGNER_MODE) $(CFLAGS_VERSION)"; \
	else \
		command -v docker >/dev/null 2>&1 || { \
			echo "ERROR: Docker is required for automatic SGDK builds (or set GDK=/path/to/sgdk)."; \
			echo "Install Docker on Ubuntu Noble: sudo apt install -y docker.io"; \
			exit 1; \
		}; \
		echo "Building Sega Genesis ROM via Docker SGDK ($(SGDK_IMAGE))..."; \
		docker run --rm --user "$$(id -u):$$(id -g)" -e HOME=/tmp \
			-e EXTRA_FLAGS="-DHEARTH_DESIGNER_MODE=$(DESIGNER_MODE) $(CFLAGS_VERSION)" \
			-v "$(CURDIR)/$(BUILD_DIR)/genesis:/m68k" \
			$(SGDK_IMAGE); \
	fi
	@cp "$(BUILD_DIR)/genesis/out/rom.bin" "$(BUILD_DIR)/genesis/$(APP_NAME).bin"
	@echo "Built Sega Genesis ROM: $(BUILD_DIR)/genesis/$(APP_NAME).bin ($(BUILD_VER_FULL))"

# ------------------------------------------------------------------------------
# 6. SUPER NINTENDO (SNES) ROM (FastROM + SPC700 Audio + Auto-Fetched PVSnesLib)
# ------------------------------------------------------------------------------
snes: $(CORE_SRCS) $(CORE_HDRS) $(SNES_SRC) $(ROM_CG_HDR) $(HERO24_HDR) $(TERRAIN_ROM_HDR) $(BATTLE_BG_ROM_HDR) $(SNES_CG_ASM) $(SNES_ROWAN_ASM) $(SNES_TERRAIN_ASM) $(SNES_BATTLE_BG_ASM) $(SNES_AUDIO_GEN)
	$(call PRINT_BUILD_BANNER,snes-fastrom)
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
	mkdir -p "$(BUILD_DIR)/snes/src" "$(BUILD_DIR)/snes/res"; \
	$(CC) -std=c99 -O2 $(SNES_AUDIO_GEN) -o "$(BUILD_DIR)/snes/gen_snes_audio"; \
	"$(BUILD_DIR)/snes/gen_snes_audio" "$(BUILD_DIR)/snes/res"; \
	cp $(CORE_SRCS) $(CORE_HDRS) $(ROM_CG_HDR) $(HERO24_HDR) $(TERRAIN_ROM_HDR) $(BATTLE_BG_ROM_HDR) "$(BUILD_DIR)/snes/src/"; \
	cp $(SNES_SRC) "$(BUILD_DIR)/snes/src/main.c"; \
	DATA_OBJ="data.obj"; \
	if grep -qE '^[^#]*(SRC|dir)[^#]*\*\.asm' "$$PV_ROOT/devkitsnes/snes_rules" 2>/dev/null; then \
		cat $(SNES_CG_ASM) $(SNES_ROWAN_ASM) $(SNES_TERRAIN_ASM) $(SNES_BATTLE_BG_ASM) > "$(BUILD_DIR)/snes/src/data.asm"; \
		DATA_OBJ="src/data.obj"; \
	else \
		cat $(SNES_CG_ASM) $(SNES_ROWAN_ASM) $(SNES_TERRAIN_ASM) $(SNES_BATTLE_BG_ASM) > "$(BUILD_DIR)/snes/data.asm"; \
	fi; \
	printf '%s\n' \
		'export FASTROM := 1' \
		'export ROMNAME := hearth' \
		'export ROMTITLE := PROJECT HEARTH' \
		'export ROMSIZE := 0A' \
		'export ROMBANKS := 32' \
		'export SRAMSIZE := 00' \
		'export COUNTRY := 01' \
		'AUDIOFILES := res/hearth_sfx.it res/hearth_alderbrook.it res/hearth_wonder.it res/hearth_shadow.it res/hearth_lullaby.it res/hearth_hamlet.it res/hearth_battle.it res/hearth_boss.it' \
		'export SOUNDBANK := res/soundbank' \
		'SRC := src' \
		'CFLAGS += -DHEARTH_TARGET_SNES -DHEARTH_DESIGNER_MODE=$(DESIGNER_MODE) $(CFLAGS_VERSION) -Isrc -I.' \
		"include $${PV_ROOT}/devkitsnes/snes_rules" \
		'SMCONVFLAGS := -s -o $$(SOUNDBANK) -V -b 15 -f' \
		'musics: $$(SOUNDBANK).obj' \
		"$${DATA_OBJ}: hdr.asm" \
		'src/main.ps: $$(SOUNDBANK).asm' \
		'all: musics $$(ROMNAME).sfc' > "$(BUILD_DIR)/snes/Makefile"; \
	if ! grep -q "AUTOHDR" "$$PV_ROOT/devkitsnes/snes_rules" 2>/dev/null; then \
		printf '%s\n' \
			'.define FASTROM 1' \
			'.MEMORYMAP' \
			'  SLOTSIZE $$8000' \
			'  DEFAULTSLOT 0' \
			'  SLOT 0 $$8000' \
			'  SLOT 1 $$0 $$2000' \
			'  SLOT 2 $$2000 $$E000' \
			'  SLOT 3 $$0 $$10000' \
			'.ENDME' \
			'.ROMBANKSIZE $$8000' \
			'.ROMBANKS 32' \
			'.SNESHEADER' \
			'  ID "SNES"' \
			'  NAME "PROJECT HEARTH       "' \
			'  FASTROM' \
			'  LOROM' \
			'  CARTRIDGETYPE $$00' \
			'  ROMSIZE $$0A' \
			'  SRAMSIZE $$00' \
			'  COUNTRY $$01' \
			'  LICENSEECODE $$00' \
			'  VERSION $$00' \
			'.ENDSNES' \
			'.SNESNATIVEVECTOR' \
			'  COP EmptyHandler' \
			'  BRK EmptyHandler' \
			'  ABORT EmptyHandler' \
			'  NMI VBlank' \
			'  IRQ EmptyHandler' \
			'.ENDNATIVEVECTOR' \
			'.SNESEMUVECTOR' \
			'  COP EmptyHandler' \
			'  ABORT EmptyHandler' \
			'  NMI EmptyHandler' \
			'  RESET tcc__start' \
			'  IRQBRK EmptyHandler' \
			'.ENDEMUVECTOR' \
			'.BASE $$80' > "$(BUILD_DIR)/snes/hdr.asm"; \
	fi; \
	PVSNESLIB_HOME="$$PV_ROOT" $(MAKE) -C "$(BUILD_DIR)/snes" musics; \
	PVSNESLIB_HOME="$$PV_ROOT" $(MAKE) -C "$(BUILD_DIR)/snes"; \
	echo "Built SNES FastROM + SPC700 ROM: $(BUILD_DIR)/snes/hearth.sfc ($(BUILD_VER_FULL))"

# ------------------------------------------------------------------------------
# 7. FREESTANDING WEBASSEMBLY CORE FOR BROWSER STUDIO PREVIEW
# ------------------------------------------------------------------------------
wasm: $(WASM_OUT)

$(WASM_OUT): $(CORE_SRCS) $(CORE_HDRS) $(WASM_SRC)
	$(call PRINT_BUILD_BANNER,wasm32-core)
	@set -e; \
	ZIG_BIN=$$(command -v zig 2>/dev/null || true); \
	if [ -z "$$ZIG_BIN" ]; then \
		if [ -x "$(ZIG_CACHE)/zig" ]; then \
			ZIG_BIN="$(ZIG_CACHE)/zig"; \
		else \
			command -v curl >/dev/null 2>&1 || { echo "ERROR: curl required to auto-fetch Zig"; exit 1; }; \
			UNAME_M=$$(uname -m); \
			if [ "$$UNAME_S" = "Darwin" ]; then ZIG_OS="macos"; else ZIG_OS="linux"; fi; \
			if [ "$$UNAME_M" = "arm64" ] || [ "$$UNAME_M" = "aarch64" ]; then ZIG_ARCH="aarch64"; else ZIG_ARCH="x86_64"; fi; \
			ZIG_PKG="zig-$${ZIG_OS}-$${ZIG_ARCH}-$(ZIG_VER)"; \
			ZIG_URL="https://ziglang.org/download/$(ZIG_VER)/$${ZIG_PKG}.tar.xz"; \
			echo "Auto-fetching Zig $(ZIG_VER) ($${ZIG_OS}-$${ZIG_ARCH}) into $(ZIG_CACHE)/..."; \
			mkdir -p "$(ZIG_CACHE)"; \
			curl -fsSL "$$ZIG_URL" -o "$(ZIG_CACHE)/zig.tar.xz"; \
			if command -v xz >/dev/null 2>&1; then \
				tar -xJf "$(ZIG_CACHE)/zig.tar.xz" -C "$(ZIG_CACHE)" --strip-components=1; \
			elif command -v python3 >/dev/null 2>&1; then \
				python3 -c 'import tarfile, os; \
tf = tarfile.open("$(ZIG_CACHE)/zig.tar.xz", "r:xz"); \
root = tf.getmembers()[0].name.split("/")[0]; \
[setattr(m, "name", m.name[len(root)+1:]) or (tf.extract(m, "$(ZIG_CACHE)") if m.name else None) for m in tf.getmembers()]'; \
			else \
				echo "ERROR: Need either xz or python3 to extract Zig archive."; exit 1; \
			fi; \
			rm -f "$(ZIG_CACHE)/zig.tar.xz"; \
			ZIG_BIN="$(ZIG_CACHE)/zig"; \
		fi; \
	fi; \
	mkdir -p public; \
	"$$ZIG_BIN" cc -target wasm32-freestanding -O2 -Wall -Wextra -Werror -Wno-sign-conversion \
		-DHEARTH_DESIGNER_MODE=$(DESIGNER_MODE) $(CFLAGS_VERSION) \
		-nostdlib -Wl,--no-entry,--strip-all \
		-I$(CORE_DIR) $(CORE_SRCS) $(WASM_SRC) -o $@; \
	echo "Built WebAssembly C99 core: $@ ($$(wc -c < $@) bytes, $(BUILD_VER_FULL))"

web: wasm
	$(call PRINT_BUILD_BANNER,web-bundle)
	@set -e; \
	if [ -z "$(DOCKER_WEB)" ] && command -v npm >/dev/null 2>&1; then \
		if [ ! -d node_modules ]; then \
			npm install --no-audit --no-fund --no-package-lock --legacy-peer-deps; \
		fi; \
		npm run build; \
	else \
		command -v docker >/dev/null 2>&1 || { \
			echo "ERROR: Neither npm nor docker found. Install docker.io (or nodejs/npm)."; \
			exit 1; \
		}; \
		mkdir -p "$(WEB_CACHE_DIR)/app" "$(WEB_CACHE_DIR)/node_modules" "$(WEB_CACHE_DIR)/npm-cache" "$(CURDIR)/dist"; \
		cp package.json "$(WEB_CACHE_DIR)/package.json"; \
		echo "Building browser preview (C99 WASM + Vite) via Docker ($(NODE_IMAGE))..."; \
		docker run --rm --user "$$(id -u):$$(id -g)" \
			-e HOME=/tmp -e npm_config_cache=/work/npm-cache \
			-v "$(WEB_CACHE_DIR):/work" \
			-v "$(CURDIR):/work/app" \
			-w /work/app \
			$(NODE_IMAGE) sh -ec '\
				if [ ! -f /work/node_modules/.hearth_stamp ] || ! cmp -s /work/package.json /work/node_modules/.hearth_stamp; then \
					echo "Caching npm packages inside $(TOOLCHAIN_DIR)/web/node_modules/ (first run)..."; \
					npm install --prefix /work --no-audit --no-fund --no-package-lock --legacy-peer-deps; \
					cp /work/package.json /work/node_modules/.hearth_stamp; \
				fi; \
				/work/node_modules/.bin/vite build'; \
	fi
	@echo "Built browser preview bundle: dist/ ($(BUILD_VER_FULL))"

web-run: wasm
	$(call PRINT_BUILD_BANNER,web-run)
	@set -e; \
	if [ -z "$(DOCKER_WEB)" ] && command -v npm >/dev/null 2>&1; then \
		if [ ! -d node_modules ]; then \
			npm install --no-audit --no-fund --no-package-lock --legacy-peer-deps; \
		fi; \
		echo "Serving Project Hearth Browser Preview (C99 WASM) at http://localhost:$(WEB_PORT)"; \
		npx vite --port=$(WEB_PORT) --host=0.0.0.0; \
	else \
		command -v docker >/dev/null 2>&1 || { \
			echo "ERROR: Neither npm nor docker found. Install docker.io (or nodejs/npm)."; \
			exit 1; \
		}; \
		mkdir -p "$(WEB_CACHE_DIR)/app" "$(WEB_CACHE_DIR)/node_modules" "$(WEB_CACHE_DIR)/npm-cache"; \
		cp package.json "$(WEB_CACHE_DIR)/package.json"; \
		TTY_FLAG=""; if [ -t 0 ]; then TTY_FLAG="-it"; fi; \
		echo "Starting Browser Preview (C99 WASM) via Docker ($(NODE_IMAGE)) on http://localhost:$(WEB_PORT) ..."; \
		docker run --rm --init $$TTY_FLAG --user "$$(id -u):$$(id -g)" \
			-p "$(WEB_PORT):$(WEB_PORT)" \
			-e HOME=/tmp -e npm_config_cache=/work/npm-cache \
			-v "$(WEB_CACHE_DIR):/work" \
			-v "$(CURDIR):/work/app" \
			-w /work/app \
			$(NODE_IMAGE) sh -ec '\
				if [ ! -f /work/node_modules/.hearth_stamp ] || ! cmp -s /work/package.json /work/node_modules/.hearth_stamp; then \
					echo "Caching npm packages inside $(TOOLCHAIN_DIR)/web/node_modules/ (first run)..."; \
					npm install --prefix /work --no-audit --no-fund --no-package-lock --legacy-peer-deps; \
					cp /work/package.json /work/node_modules/.hearth_stamp; \
				fi; \
				/work/node_modules/.bin/vite --port=$(WEB_PORT) --host=0.0.0.0'; \
	fi

# ------------------------------------------------------------------------------
# UTILITIES
# ------------------------------------------------------------------------------
run: $(HOST_TARGET)
	@./$(HOST_BIN)

deps-ubuntu:
	@echo "Installing Ubuntu Noble amd64 native & cross-compile dependencies..."
	sudo apt-get update && sudo apt-get install -y build-essential libsdl2-dev pkg-config curl unzip mingw-w64

clean:
	rm -rf $(BUILD_DIR) dist

distclean:
	rm -rf $(BUILD_DIR) dist $(TOOLCHAIN_DIR)

help:
	@echo "Project Hearth $(BUILD_VER_FULL) — Native C99 & Multi-Platform Makefile"
	@echo ""
	@echo "Host detected: $(UNAME_S) -> default target: $(HOST_TARGET)  |  Mode: $(BUILD_MODE_LBL)"
	@echo ""
	@echo "Targets:"
	@echo "  make              Build native executable for this OS ($(HOST_BIN))"
	@echo "  make run          Build and launch the native game window"
	@echo "  make core-test    Compile & run strict C89/C99 core verification suite"
	@echo "  make map-audit    Run standalone 6-pass world/autotile/seam audit + annotated PNGs"
	@echo "  make linux        Build Linux amd64 native SDL2 executable"
	@echo "  make windows      Cross-compile Windows x64 .exe (auto-fetches SDL2 MinGW)"
	@echo "  make genesis      Build Sega Genesis ROM (.bin) via Docker SGDK automatically"
	@echo "  make snes         Build SNES ROM (.sfc) via auto-downloaded PVSnesLib"
	@echo "  make wasm         Build WebAssembly C99 core (public/hearth_core.wasm)"
	@echo "  make web          Build browser preview bundle (make wasm + Docker Node / npm)"
	@echo "  make web-run      Run browser preview on http://localhost:$(WEB_PORT) (Docker Node / npm)"
	@echo "  make macos        Build macOS native SDL2 executable"
	@echo "  make deps-ubuntu  Install Ubuntu Noble packages (gcc, SDL2, curl, unzip, mingw-w64)"
	@echo "  make clean        Remove build/ and dist/ (keeps cached .toolchains/)"
	@echo "  make distclean    Remove build/, dist/, and cached .toolchains/"
