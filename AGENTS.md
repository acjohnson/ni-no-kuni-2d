# Project Hearth (`ni-no-kuni-2d`) — Agent Instructions

Project Hearth is a multi-platform 16-bit JRPG built around a single portable, zero-allocation **ANSI C89/C99** simulation core (`engine/core/`) paired with thin platform hosts (`engine/platform/` and `src/`) targeting Native Desktop (SDL2), Sega Genesis (SGDK), Super Nintendo (PVSnesLib / WDC 65816), and WebAssembly + React/Vite.

---

## Build, Test & Verification Commands

### Core Tests & Map Audits
- `make core-test` — Compiles `engine/core/` under **both** strict ANSI C89 (`-std=c89 -Wall -Wextra -Werror -pedantic`, validating SNES `816-tcc` safety) and strict C99, then runs `engine/platform/headless_test.c` (checks `sizeof(HearthState) < 32768`, palette packers, story arcs, combat, shops, fishing; requires zero SDL2).
- `make map-audit` — Runs the 6-pass world, autotile, collision, portal, canopy, and pixel-seam audit (`map_audit_dump.c` + `engine/tools/map_audit.py`), writing reports and annotated PNGs to `build/map_audit/`.
- `make map-audit-vision` — Same as `make map-audit` with the optional Gemini Vision QA pass (`VISION=1`, supports `CHANGED_ONLY=1`; requires `GEMINI_API_KEY`).
- `make map-audit-bless` — Updates the verified pixel-diff baseline (`engine/tools/map_audit_baseline.json`).

### Native Desktop & Retro Console Builds
- `make` / `make linux` / `make macos` — Build native SDL2 binary (`build/<os>/hearth`).
- `make run` — Build and launch the native desktop executable.
- `make windows` — Cross-compile Windows x64 `.exe` via `x86_64-w64-mingw32-gcc` (auto-fetches SDL2 MinGW into `.toolchains/`).
- `make genesis` — Build Sega Genesis ROM (`build/genesis/hearth.bin`) via Docker SGDK (`doragasu/docker-sgdk:v2.11`) or local `$GDK`.
- `make snes` — Build SNES FastROM + SPC700 ROM (`build/snes/hearth.sfc`) via auto-fetched PVSnesLib (`.toolchains/pvsneslib/`) or `$PVSNESLIB_HOME`; also compiles and runs `engine/tools/gen_snes_audio.c` to generate `.it` audio modules.
- `make deps-ubuntu` — Install Ubuntu Noble native & cross-compile dependencies (`build-essential`, `libsdl2-dev`, `pkg-config`, `curl`, `unzip`, `mingw-w64`).

### Web / Browser Studio Builds
- `make wasm` — Compile `engine/core/` + `engine/platform/wasm_bridge.c` to freestanding `public/hearth_core.wasm` via Zig (`0.14.0`, auto-fetched into `.toolchains/zig/`).
- `make web` — Run `make wasm` and build the production Vite bundle into `dist/` (uses local `npm` or falls back to Docker `node:22-slim`).
- `make web-run` — Run `make wasm` and start the Vite dev server on `http://localhost:5173` (override with `WEB_PORT=...`).
- `npm run lint` — TypeScript typecheck (`tsc --noEmit`).
- `npm run dev` / `npm run build` / `npm run preview` — Direct Vite scripts (`npm run dev` uses port `3000`).

### Build Flags & Cleanup
- `DESIGNER_MODE=0` (default `1`) — Produces story-only release builds (`-DHEARTH_DESIGNER_MODE=0`), disabling Designer / Explore Evertale mode.
- `make clean` — Removes `build/` and `dist/` (preserves `.toolchains/`).
- `make distclean` — Removes `build/`, `dist/`, and `.toolchains/`.

### Asset Bakers (`engine/tools/`)
Run these whenever source art, tilesets, battle backgrounds, or Rowan sprites change so all 4 targets stay in sync:
- `python3 engine/tools/bake_chrono_tileset.py` — Emits `engine/platform/hearth_terrain_rom.h`, `engine/platform/hearth_terrain_snes.asm`, and `src/host/chronoTilesetData.ts`.
- `python3 engine/tools/bake_battle_bgs.py` — Emits `engine/platform/battle_bg_rom.h`, `engine/platform/battle_bg_snes.asm`, and `src/host/battleBgData.ts`.
- `python3 engine/tools/bake_cinema_cg.py` — Emits `engine/platform/cinema_cg_sdl2.h`, `engine/platform/cinema_cg_rom.h`, and `engine/platform/cinema_cg_snes.asm` (requires ImageMagick `convert`).
- `cc -std=c99 -O2 -Iengine/core engine/tools/bake_rowan_rom.c -o /tmp/bake_rowan_rom && /tmp/bake_rowan_rom` — Emits `engine/platform/rowan_snes_rom.asm` and `engine/platform/rowan_gen_rom.h` from `engine/platform/hearth_hero24.h`.

---

## Architecture Overview

```text
engine/
├── core/                  # Pure ANSI C89/C99 deterministic 60Hz simulation (zero OS/alloc/float)
│   ├── hearth_types.h     # Fixed-point 16.8 math, PAD_* masks, all enums & PPU/state structs
│   ├── hearth_prng.h      # Deterministic 16-bit Galois LFSR PRNG
│   ├── hearth_data.h/.c   # Palettes, 64 BG1 tiles, 25 zone builders, 256x192 globe streamer, scripts
│   └── hearth_core.h/.c   # HearthState (<32 KB) & hearth_step_60hz() -> HearthPpuPacket
├── platform/              # Thin OS/console renderers, audio drivers, and baked ROM tables
│   ├── sdl2_main.c        # Native Linux/macOS/Windows host (pure SDL2 + software PPU + PCM synth)
│   ├── genesis_main.c     # Sega Genesis host (SGDK VDP, Hardware SAT, SN76489 PSG)
│   ├── snes_main.c        # SNES host (PVSnesLib Mode 1 PPU, SPC700 BRR audio)
│   ├── wasm_bridge.c      # Freestanding wasm32 bridge -> public/hearth_core.wasm
│   ├── headless_test.c    # Zero-dependency C99 test suite (make core-test)
│   └── *_rom.h / *_snes.asm / cinema_cg_* / hearth_hero24.h / pixel_art_data.h
└── tools/                 # Python & C asset bakers, SNES .it audio generator, 6-pass map auditor
src/
├── core/types.ts          # C99-mirrored TypeScript types & constants (zero DOM/React)
├── host/                  # Browser host layer: wasmEngine.ts, ppuRenderer.ts, pixelAtlas.ts,
│                          # inputAdapter.ts, synthAudio.ts, c99Exporter.ts, baked TS art tables
├── components/            # React 19 + Tailwind v4 studio UI overlays & modals
└── App.tsx                # Loads /hearth_core.wasm and drives the 60Hz Canvas PPU loop
```

### Data Flow
1. Every host captures a 12-bit gamepad bitmask (`PAD_UP`..`PAD_SELECT`) and calls `hearth_step_60hz(&state, pad_bits)` at 60Hz.
2. `engine/core/` advances fixed-point physics, AI, dialogue/cutscene bytecode, turn-based timed-hit combat, shops, fishing, and 1024-step outdoor Time-of-Day progression, returning a flat `HearthPpuPacket` (camera scroll, OAM sprites, particles, floating text, SFX queue, active palette/BGM/ToD state).
3. Local zones use reusable `44x28` RAM tile buffers (`map_bg1`, `map_bg2`, `map_col`); the `256x192` toroidal Evertale Globe (`ZONE_EVERTALE_WORLD`) streams procedurally from ROM via `hearth_globe_cell()` / `hearth_globe_stream_*()` with zero map RAM overhead.
4. Each platform host renders `HearthPpuPacket` using its native PPU/VDP/SDL2/Canvas pipeline and drains `ppu.sfx_queue`.

---

## Coding Conventions & Hard Constraints

### `engine/core/` (Strict Portable C89 + C99)
- **Dual-standard compliance**: Code must compile cleanly under **both** `-std=c89 -Werror -pedantic` and `-std=c99 -Werror -pedantic`.
  - Declare all local variables at the top of blocks (C89).
  - Use `/* ... */` comments (no `//` comments in C89-compiled files).
  - Use `HEARTH_INLINE` instead of bare `inline`.
- **Zero dependencies**: Only include `<stdint.h>`, `<stdbool.h>`, `<stddef.h>` (guarded via `hearth_types.h` for SNES/SGDK). Never include `<stdio.h>`, `<stdlib.h>`, `<string.h>`, `<math.h>`, SDL, OS, or console headers inside `engine/core/`.
- **Zero heap allocation & zero floats**: No `malloc`/`free` and no floating-point math in simulation. Use `16.8` fixed-point (`fp16_t`, `TO_FP`, `FROM_FP`, `FP_MUL`, `FP_PX`), `hearth_memset()`, `HEARTH_SIN64`, and `HearthPrng`.
- **Hardware budgets**:
  - Keep `sizeof(HearthState) < 32768` bytes (enforced by `make core-test` for 64KB Genesis RAM / 128KB SNES WRAM).
  - Keep BG1 tile IDs within `T_TILE_COUNT = 64` and OAM sprites within `HEARTH_MAX_OAM = 64`.
- **SNES WRAM access macros**: In `hearth_core.c`, access state fields through `HS`, `HP`, `OAM_AT()`, and mark unused `st` with `ST_UNUSED(st)` so `HEARTH_TARGET_SNES` compiles to direct Bank `$7E` WRAM access while other targets dereference `st`.

### `engine/platform/` (Host-Specific Rules)
- **SNES (`snes_main.c`)**: Strictly ANSI C89. Keep per-function stack usage well below the WDC 65816 255-byte frame limit (place scratch buffers in static WRAM). Never exceed ~2 KB of VBlank DMA per frame.
- **Sega Genesis (`genesis_main.c`)**: Keep all VRAM tile indices strictly below tile `1344` (`0xA800`, 64x64 HSCROLL) and `1376` (`0xAC00`, SAT). Wrap bulk VDP tile/plane/CRAM uploads with `gen_vdp_begin_bulk()` / `gen_vdp_end_bulk()`.
- **WASM Bridge (`wasm_bridge.c` ↔ `src/host/wasmEngine.ts`)**: Keep `SCALAR_LEN`, all buffer strides (`OAM_STRIDE`, `PARTY_STRIDE`, `ENEMY_STRIDE`, `PARTICLE_STRIDE`, `FTEXT_STRIDE`, `FIELD_BOX_STRIDE`), and `WASM_EXPORT(...)` signatures strictly synchronized between C and TypeScript. Always run `make wasm` after editing `engine/core/` or `wasm_bridge.c`.
- **SDL2 (`sdl2_main.c`)**: Pure C99 + SDL2 only—do not add `SDL_ttf`, `SDL_image`, or `SDL_mixer` dependencies.

### Generated Assets
- **Do not hand-edit generated art/ROM files**: `hearth_terrain_rom.h`, `hearth_terrain_snes.asm`, `battle_bg_rom.h`, `battle_bg_snes.asm`, `cinema_cg_sdl2.h`, `cinema_cg_rom.h`, `cinema_cg_snes.asm`, `rowan_gen_rom.h`, `rowan_snes_rom.asm`, `src/host/chronoTilesetData.ts`, and `src/host/battleBgData.ts`. Edit the generator scripts in `engine/tools/` (or `hearth_hero24.h`) and re-bake.

### Frontend (`src/`)
- **No duplicated simulation logic**: Gameplay rules, collision, combat math, and story state belong exclusively in `engine/core/` and are consumed in the browser via `WasmHearthEngine`.
- Keep `src/core/types.ts` free of DOM/React imports and mirrored to `engine/core/hearth_types.h`.
- Preserve the `copy-unbundled-src-assets` plugin and `DISABLE_HMR` handling in `vite.config.ts`.

### Naming, Versioning & Commits
- **C naming**: `UPPER_SNAKE_CASE` for macros and enum constants (`HEARTH_*`, `ZONE_*`, `ZPAL_*`, `SPR_*`, `SFX_*`, `BGM_*`, `PAD_*`, `T_*`, `O_*`, `COL_*`, `BPHASE_*`, `OP_*`); `PascalCase` with `Hearth` prefix for typedefs (`HearthState`, `HearthOamSprite`); `snake_case` for functions/variables (`hearth_*` for public core API).
- **Versioning**: `VERSION` is the single source of truth injected by `Makefile` via `-DHEARTH_VERSION_RAW`; keep `HEARTH_VERSION` in `src/core/types.ts` and the fallback in `engine/core/hearth_types.h` aligned when bumping versions.
- **Commits**: Use Conventional Commits (e.g., `feat(core): ...`, `fix(genesis): ...`, `refactor: ...`).

---

## Pre-Completion Verification Checklist
- Edited `engine/core/`: run `make core-test` (and `make wasm` if browser parity is needed).
- Edited maps, portals, collision, or tilesets: run `make map-audit` (and `make core-test`).
- Edited `wasm_bridge.c` or `src/`: run `make wasm && npm run lint` (and `make web` for production bundle check).
- Edited a specific platform host (`sdl2_main.c`, `genesis_main.c`, `snes_main.c`): build that target (`make linux`, `make genesis`, `make snes`).
