# ==============================================================================
# Project Hearth — Multi-Target Native C99 & Retro Console Build System
#
# Supported targets:
#   make               -> Auto-detect host OS and build native executable
#   make run           -> Build & launch native executable (Ubuntu/Linux/macOS)
#   make linux         -> Native Linux amd64 executable (SDL2, zero browser)
#   make core-test     -> Zero-dependency pure C99 automated verification test
#   make windows       -> Cross-compile Windows x64 .exe from Linux (MinGW-w64)
#   make macos         -> Native macOS executable (Homebrew SDL2)
#   make genesis       -> Cross-compile Sega Genesis .bin ROM (SGDK)
#   make snes          -> Cross-compile Super Nintendo .sfc ROM (PVSnesLib)
#   make clean         -> Remove build/ artifacts
#   make help          -> Print usage & toolchain setup instructions
# ==============================================================================

APP_NAME        := hearth
BUILD_DIR       := build

CORE_DIR        := engine/core
PLAT_DIR        := engine/platform
CORE_SRCS       := $(CORE_DIR)/hearth_data.c $(CORE_DIR)/hearth_core.c
CORE_HDRS       := $(CORE_DIR)/hearth_types.h $(CORE_DIR)/hearth_prng.h \
                   $(CORE_DIR)/hearth_data.h $(CORE_DIR)/hearth_core.h

SDL2_SRC        := $(PLAT_DIR)/sdl2_main.c
TEST_SRC        := $(PLAT_DIR)/headless_test.c
GENESIS_SRC     := $(PLAT_DIR)/genesis_main.c
SNES_SRC        := $(PLAT_DIR)/snes_main.c

# Strict portable C99 compiler flags
CFLAGS_COMMON   := -std=c99 -O2 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -I$(CORE_DIR)
CFLAGS_STRICT   := -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I$(CORE_DIR)

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

# Native Compilers
CC              ?= gcc

# Linux / macOS SDL2 flags (pkg-config first, fallback to sdl2-config)
SDL2_CFLAGS     := $(shell pkg-config --cflags sdl2 2>/dev/null || sdl2-config --cflags 2>/dev/null)
SDL2_LIBS       := $(shell pkg-config --libs sdl2 2>/dev/null || sdl2-config --libs 2>/dev/null)

# Windows MinGW-w64 cross-compiler settings (for cross-compiling from Ubuntu Noble)
MINGW_CC        ?= x86_64-w64-mingw32-gcc
MINGW_SDL2_DIR  ?= third_party/SDL2-mingw/x86_64-w64-mingw32

# Retro Console SDK paths (override via environment or CLI: `GDK=... make genesis`)
GDK             ?= $(HOME)/sgdk
PVSNESLIB_HOME  ?= $(HOME)/pvsneslib

.PHONY: all linux macos windows genesis snes core-test run deps-ubuntu clean help

# Default target builds for the current machine
all: $(HOST_TARGET)

# ------------------------------------------------------------------------------
# 1. NATIVE LINUX (Ubuntu Noble amd64 & other Linux distros)
# ------------------------------------------------------------------------------
linux: $(BUILD_DIR)/linux/$(APP_NAME)

$(BUILD_DIR)/linux/$(APP_NAME): $(CORE_SRCS) $(CORE_HDRS) $(SDL2_SRC)
	@if [ -z "$(SDL2_CFLAGS)" ] && [ ! -f /usr/include/SDL2/SDL.h ]; then \
		echo "ERROR: SDL2 development headers not found."; \
		echo "On Ubuntu Noble amd64, install them with:"; \
		echo "  sudo apt update && sudo apt install -y build-essential libsdl2-dev pkg-config"; \
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
# 3. WINDOWS x64 CROSS-COMPILE FROM LINUX (MinGW-w64)
# ------------------------------------------------------------------------------
windows: $(BUILD_DIR)/windows/$(APP_NAME).exe

$(BUILD_DIR)/windows/$(APP_NAME).exe: $(CORE_SRCS) $(CORE_HDRS) $(SDL2_SRC)
	@command -v $(MINGW_CC) >/dev/null 2>&1 || { \
		echo "ERROR: $(MINGW_CC) not found."; \
		echo "Install on Ubuntu Noble: sudo apt install -y mingw-w64"; \
		exit 1; \
	}
	@if [ ! -f "$(MINGW_SDL2_DIR)/include/SDL2/SDL.h" ]; then \
		echo "ERROR: MinGW SDL2 dev package not found at $(MINGW_SDL2_DIR)."; \
		echo "Download SDL2-devel-*-mingw.tar.gz from https://github.com/libsdl-org/SDL/releases"; \
		echo "Extract it to third_party/SDL2-mingw/ or pass MINGW_SDL2_DIR=/path/to/x86_64-w64-mingw32"; \
		exit 1; \
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
# 4. ZERO-DEPENDENCY PURE C99 CORE VERIFICATION TEST (Runs anywhere with gcc)
# ------------------------------------------------------------------------------
core-test: $(BUILD_DIR)/test/$(APP_NAME)_core_test
	@./$(BUILD_DIR)/test/$(APP_NAME)_core_test

$(BUILD_DIR)/test/$(APP_NAME)_core_test: $(CORE_SRCS) $(CORE_HDRS) $(TEST_SRC)
	@mkdir -p $(BUILD_DIR)/test
	$(CC) $(CFLAGS_STRICT) $(CORE_SRCS) $(TEST_SRC) -o $@

# ------------------------------------------------------------------------------
# 5. SEGA GENESIS / MEGA DRIVE ROM (SGDK — Motorola 68000)
# ------------------------------------------------------------------------------
genesis: $(CORE_SRCS) $(CORE_HDRS) $(GENESIS_SRC)
	@if [ ! -f "$(GDK)/makefile.gen" ]; then \
		echo "ERROR: SGDK not found at GDK=$(GDK)."; \
		echo "Install SGDK or run with SGDK's official Docker image:"; \
		echo "  git clone https://github.com/Stephane-D/SGDK.git \$$HOME/sgdk"; \
		echo "  # OR via Docker (no local 68k toolchain install needed):"; \
		echo "  make genesis-stage && docker run --rm -v \$$(pwd)/build/genesis:/m68k -t ghcr.io/stephane-d/sgdk:latest"; \
		exit 1; \
	fi
	@$(MAKE) --no-print-directory genesis-stage
	GDK="$(GDK)" $(MAKE) -C $(BUILD_DIR)/genesis -f "$(GDK)/makefile.gen"
	@echo "Built Sega Genesis ROM: $(BUILD_DIR)/genesis/out/rom.bin"

.PHONY: genesis-stage
genesis-stage:
	@rm -rf $(BUILD_DIR)/genesis/src
	@mkdir -p $(BUILD_DIR)/genesis/src/core $(BUILD_DIR)/genesis/src/platform $(BUILD_DIR)/genesis/res
	@cp $(CORE_SRCS) $(CORE_HDRS) $(BUILD_DIR)/genesis/src/core/
	@cp $(GENESIS_SRC) $(BUILD_DIR)/genesis/src/platform/main.c
	@echo "Staged SGDK project in $(BUILD_DIR)/genesis/"

# ------------------------------------------------------------------------------
# 6. SUPER NINTENDO (SNES) ROM (PVSnesLib — WDC 65816)
# ------------------------------------------------------------------------------
snes: $(CORE_SRCS) $(CORE_HDRS) $(SNES_SRC)
	@if [ ! -f "$(PVSNESLIB_HOME)/devkitsnes/snes_rules" ]; then \
		echo "ERROR: PVSnesLib not found at PVSNESLIB_HOME=$(PVSNESLIB_HOME)."; \
		echo "Download PVSnesLib from https://github.com/alekmaul/pvsneslib/releases"; \
		echo "Then run: PVSNESLIB_HOME=/path/to/pvsneslib make snes"; \
		exit 1; \
	fi
	@rm -rf $(BUILD_DIR)/snes
	@mkdir -p $(BUILD_DIR)/snes/core $(BUILD_DIR)/snes/platform
	@cp $(CORE_SRCS) $(CORE_HDRS) $(BUILD_DIR)/snes/core/
	@cp $(SNES_SRC) $(BUILD_DIR)/snes/platform/main.c
	@printf '%s\n' \
		'.MEMORYMAP' \
		'  SLOTSIZE $$8000' \
		'  DEFAULTSLOT 0' \
		'  SLOT 0 $$8000' \
		'  SLOT 1 $$0 $$2000' \
		'  SLOT 2 $$2000 $$E000' \
		'  SLOT 3 $$0 $$10000' \
		'.ENDME' \
		'.ROMBANKSIZE $$8000' \
		'.ROMBANKS 8' \
		'.SNESHEADER' \
		'  ID "HEAR"' \
		'  NAME "PROJECT HEARTH       "' \
		'  SLOWROM' \
		'  LOROM' \
		'  CARTRIDGETYPE $$00' \
		'  ROMSIZE $$08' \
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
		'  BRK EmptyHandler' \
		'  ABORT EmptyHandler' \
		'  NMI EmptyHandler' \
		'  RESET tcc__start' \
		'  IRQBRK EmptyHandler' \
		'.ENDEMUVECTOR' > $(BUILD_DIR)/snes/hdr.asm
	@printf '%s\n' \
		'export PVSNESLIB_HOME := $(PVSNESLIB_HOME)' \
		'include $$(PVSNESLIB_HOME)/devkitsnes/snes_rules' \
		'ROMNAME := hearth' \
		'CFILES := core/hearth_data.c core/hearth_core.c platform/main.c' \
		'all: $$(ROMNAME).sfc' > $(BUILD_DIR)/snes/Makefile
	$(MAKE) -C $(BUILD_DIR)/snes
	@echo "Built SNES ROM: $(BUILD_DIR)/snes/hearth.sfc"

# ------------------------------------------------------------------------------
# UTILITIES
# ------------------------------------------------------------------------------
run: $(HOST_TARGET)
	@./$(HOST_BIN)

deps-ubuntu:
	@echo "Installing Ubuntu Noble amd64 native build dependencies..."
	sudo apt-get update && sudo apt-get install -y build-essential libsdl2-dev pkg-config mingw-w64

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Project Hearth — Native C99 / Cross-Compile Makefile"
	@echo ""
	@echo "Host detected: $(UNAME_S) -> default target: $(HOST_TARGET)"
	@echo ""
	@echo "Targets:"
	@echo "  make              Build native executable for this OS ($(HOST_BIN))"
	@echo "  make run          Build and launch the native game window"
	@echo "  make core-test    Compile & run strict C99 core test suite (no SDL2 needed)"
	@echo "  make linux        Build Linux amd64 native SDL2 executable"
	@echo "  make windows      Cross-compile Windows x64 .exe via MinGW-w64"
	@echo "  make macos        Build macOS native SDL2 executable"
	@echo "  make genesis      Build Sega Genesis ROM (.bin) via SGDK"
	@echo "  make snes         Build Super Nintendo ROM (.sfc) via PVSnesLib"
	@echo "  make deps-ubuntu  Install Ubuntu Noble packages (gcc, make, libsdl2-dev, mingw-w64)"
	@echo "  make clean        Delete build/ directory"
