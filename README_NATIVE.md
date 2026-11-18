# Project Hearth — Native C99 Engine & Multi-Platform Build Guide

**Project Hearth** is written in portable, zero-allocation **ANSI C89 / C99** inside `engine/core/` with thin hardware/OS hosts in `engine/platform/`. It compiles natively outside the browser via `make` on **Ubuntu Noble (24.04 amd64)** and builds **Windows x64**, **Sega Genesis**, and **Super Nintendo** binaries automatically straight from the `Makefile`.

---

## Quick Start on Ubuntu Noble (`amd64`)

```bash
sudo apt update
sudo apt install -y build-essential libsdl2-dev pkg-config

# Build and run native Linux binary (build/linux/hearth)
make run

# Verify pure C89 + C99 core engine tests (zero SDL2 required)
make core-test
```

---

## One-Command Cross-Compilation

### Sega Genesis / Mega Drive ROM (`build/genesis/hearth.bin`)
Uses Docker SGDK (`registry.gitlab.com/doragasu/docker-sgdk:v2.11`) automatically—no manual SGDK install required (falls back to a local `GDK` only if you explicitly set one):
```bash
make genesis
```

### Super Nintendo ROM (`build/snes/hearth.sfc`)
Automatically downloads and caches the latest official PVSnesLib Linux release into `.toolchains/pvsneslib/` on first run—no `PVSNESLIB_HOME` environment variable needed:
```bash
make snes
```

### Windows x64 (`build/windows/hearth.exe`)
Automatically downloads and caches the SDL2 MinGW dev package into `.toolchains/SDL2-mingw/` on first run:
```bash
sudo apt install -y mingw-w64
make windows
```

### Browser Studio Preview (`make wasm`, `make web`, `make web-run`)
Compiles `engine/core/` to `public/hearth_core.wasm` (auto-fetching Zig into `.toolchains/zig/`), and automatically uses a `node:22-slim` Docker container (caching dependencies in `.toolchains/web/`) when `npm` is not installed locally:
```bash
# Launch interactive browser preview on http://localhost:5173 (override with WEB_PORT=8080)
make web-run

# Or just build the static dist/ bundle + WASM core
make web
```

*(Run `make clean` to clean `build/` and `dist/` while keeping cached toolchains, or `make distclean` to remove `build/`, `dist/`, and `.toolchains/`.)*

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
