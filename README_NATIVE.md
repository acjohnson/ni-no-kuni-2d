# Project Hearth — Native C99 Engine & Multi-Platform Build Guide

**Project Hearth** is written in portable, zero-allocation **Pure C99** inside `engine/core/` with thin hardware/OS hosts in `engine/platform/`. It compiles natively outside the browser via `make` on **Ubuntu Noble (24.04 amd64)** and cross-compiles to **Windows x64**, **Sega Genesis (SGDK)**, and **Super Nintendo (PVSnesLib)**.

---

## Architecture

```text
engine/
  core/                    # 100% platform-agnostic C99 (zero SDL, zero malloc, zero float)
    hearth_types.h         # 16.8 fixed-point math, 12-bit retro pad, PPU/OAM packet structs
    hearth_prng.h          # Deterministic 16-bit Galois LFSR PRNG
    hearth_data.h / .c     # 4x16 master palettes (BGR333/BGR555 packers), 44x28 map, cutscenes
    hearth_core.h / .c     # 60Hz simulation: 8-way corner-sliding, Pip trail, cutscenes, combat
  platform/                # Thin OS & Retro Hardware Hosts
    sdl2_main.c            # Native Linux / Windows / macOS host (SDL2 software PPU + PCM synth)
    genesis_main.c         # Sega Genesis / Mega Drive host (SGDK / 68000)
    snes_main.c            # Super Nintendo host (PVSnesLib / 65816)
    headless_test.c        # Zero-dependency C99 verification suite
Makefile                   # Unified host-detecting & cross-compilation build system
```

---

## Quick Start on Ubuntu Noble (amd64)

### 1. Install build tools and SDL2
Only `libsdl2-dev` is required (no `SDL_ttf`, `SDL_image`, or `SDL_mixer` needed—the bitmap font, pixel PPU renderer, and 8-voice PCM audio synth are all built into C99):

```bash
sudo apt update
sudo apt install -y build-essential libsdl2-dev pkg-config
```

### 2. Build and Run Native Binary
```bash
make run
```
This compiles `build/linux/hearth` and opens the native 60Hz desktop window immediately.

### 3. Run the Zero-Dependency Pure C99 Core Tests
```bash
make core-test
```
Verifies that `sizeof(HearthState)` fits well inside the Sega Genesis 64 KB RAM budget, tests 8-way fixed-point movement, Pip breadcrumb tracking, bytecode cutscene execution, instant Familiar hot-swapping, timed hits, Spell Lock breaking, and parries under `-std=c99 -Wall -Wextra -Werror -pedantic`.

---

## Controls (Native Desktop Executable)

| Action | Keyboard | Gamepad (Auto-Detected) |
|---|---|---|
| Move (8-way + corner slide) | `WASD` / `Arrow Keys` | D-Pad / Left Stick |
| Confirm / Interact / Timed Hit / Parry | `Space` / `Z` / `Enter` | `A` (South) |
| Dash / Cancel | `Shift` / `X` | `B` (East) |
| Hot-Swap Familiar (Free Action in Battle) | `Q` / `E` or `[` / `]` | `L` / `R` Bumpers |
| Toggle Genesis `320x224` / SNES `256x224` | `F1` | — |
| Toggle CRT Scanlines | `F2` | — |
| Jump straight to Briar Warden Boss Battle | `F3` | — |
| Replay Intro Cutscene VM Demo | `F4` | — |
| Heal Party & Respawn Roaming Enemies | `F5` | — |
| Toggle Hitbox Overlay | `F6` | — |
| Mute Audio / Toggle Fullscreen | `M` / `F11` | — |

---

## Cross-Compilation from Ubuntu Noble

### Windows x64 (`hearth.exe`)
```bash
sudo apt install -y mingw-w64
# Download SDL2-devel-2.30.x-mingw.tar.gz from https://github.com/libsdl-org/SDL/releases
# Extract into third_party/SDL2-mingw/ (containing x86_64-w64-mingw32/)
make windows
# Output: build/windows/hearth.exe (+ SDL2.dll)
```

### Sega Genesis / Mega Drive ROM (`rom.bin` via SGDK)
Using a local SGDK installation:
```bash
GDK=/path/to/sgdk make genesis
```
Or using SGDK's official Docker image without installing a local m68k toolchain:
```bash
make genesis-stage
docker run --rm -v "$(pwd)/build/genesis:/m68k" -t ghcr.io/stephane-d/sgdk:latest
# Output: build/genesis/out/rom.bin (playable in BlastEm / Genesis Plus GX / real hardware)
```

### Super Nintendo ROM (`hearth.sfc` via PVSnesLib)
```bash
# Download PVSnesLib from https://github.com/alekmaul/pvsneslib/releases
PVSNESLIB_HOME=/path/to/pvsneslib make snes
# Output: build/snes/hearth.sfc (playable in Mesen / bsnes / real hardware)
```
