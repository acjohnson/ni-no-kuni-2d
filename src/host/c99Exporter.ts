/**
 * Project Hearth — Portable C99 Header & Retro Hardware Data Exporter
 *
 * Generates genuine compile-ready C99 struct headers and hardware-packed
 * ROM data tables targeting:
 *  - Sega Genesis / Mega Drive (SGDK — 9-bit BGR333 palettes, u8/u16)
 *  - Super Nintendo / SNES (PVSnesLib — 15-bit BGR555 palettes, ROM arrays)
 *  - Desktop / Portable C99 (SDL2 — RGBA8888 standard <stdint.h>)
 */

import {
  CutsceneOpExport,
  EngineTuning,
  MAP_H,
  MAP_W,
  WorldMapData,
} from '../core/types';

export type C99Target = 'SGDK_GENESIS' | 'PVSNESLIB_SNES' | 'SDL2_C99';

function hexToRgb(hex: string): [number, number, number] {
  const clean = hex.replace('#', '');
  const r = parseInt(clean.slice(0, 2), 16) || 0;
  const g = parseInt(clean.slice(2, 4), 16) || 0;
  const b = parseInt(clean.slice(4, 6), 16) || 0;
  return [r, g, b];
}

/** Sega Genesis VDP CRAM format: 0000 BBB0 GGG0 RRR0 (9-bit color) */
function toGenesisBgr333(hex: string): string {
  const [r, g, b] = hexToRgb(hex);
  const r3 = (r >> 5) & 7;
  const g3 = (g >> 5) & 7;
  const b3 = (b >> 5) & 7;
  const val = (b3 << 9) | (g3 << 5) | (r3 << 1);
  return '0x' + val.toString(16).toUpperCase().padStart(4, '0');
}

/** SNES PPU CGRAM format: 0 BBBBB GGGGG RRRRR (15-bit BGR555) */
function toSnesBgr555(hex: string): string {
  const [r, g, b] = hexToRgb(hex);
  const r5 = (r >> 3) & 31;
  const g5 = (g >> 3) & 31;
  const b5 = (b >> 3) & 31;
  const val = (b5 << 10) | (g5 << 5) | r5;
  return '0x' + val.toString(16).toUpperCase().padStart(4, '0');
}

function toRgba8888(hex: string): string {
  const [r, g, b] = hexToRgb(hex);
  const val = ((r << 24) | (g << 16) | (b << 8) | 0xff) >>> 0;
  return '0x' + val.toString(16).toUpperCase().padStart(8, '0') + 'u';
}

export function generateHearthTypesHeader(target: C99Target, tuning: EngineTuning): string {
  const targetBanner =
    target === 'SGDK_GENESIS'
      ? 'Sega Genesis / Mega Drive (SGDK / m68k-gcc)'
      : target === 'PVSNESLIB_SNES'
        ? 'Super Nintendo / SNES (PVSnesLib / 65816 C)'
        : 'Portable Desktop / Handheld C99 (SDL2 / Raylib / WASM)';

  return `/* ============================================================================
 * hearth_types.h — Project Hearth Platform-Agnostic 16-Bit Engine Core
 * Target Toolchain: ${targetBanner}
 * Generated directly from live engine registers & C99-mirrored data layout.
 * ============================================================================ */

#ifndef HEARTH_TYPES_H
#define HEARTH_TYPES_H

#include <stdint.h>

/* 16.8 Fixed-Point Integer Math (1 pixel = 256 subpixels, zero float drift) */
typedef int32_t fp16_t;
#define FP_SHIFT        8
#define FP_ONE          (1 << FP_SHIFT)
#define TO_FP(px)       ((fp16_t)((px) << FP_SHIFT))
#define FROM_FP(fp)     ((int16_t)((fp) >> FP_SHIFT))
#define FP_MUL(a, b)    ((fp16_t)(((int32_t)(a) * (int32_t)(b)) >> FP_SHIFT))

/* 12-Button Retro Gamepad Bitmask */
#define PAD_UP          (1u << 0)
#define PAD_DOWN        (1u << 1)
#define PAD_LEFT        (1u << 2)
#define PAD_RIGHT       (1u << 3)
#define PAD_A           (1u << 4)
#define PAD_B           (1u << 5)
#define PAD_X           (1u << 6)
#define PAD_Y           (1u << 7)
#define PAD_L           (1u << 8)
#define PAD_R           (1u << 9)
#define PAD_START       (1u << 10)
#define PAD_SELECT      (1u << 11)

/* Virtual PPU Hardware Budgets */
#define PPU_WIDTH       ${target === 'PVSNESLIB_SNES' ? 256 : 320}
#define PPU_HEIGHT      224
#define PPU_OAM_MAX     ${target === 'PVSNESLIB_SNES' ? 128 : 80}
#define MAP_TILE_W      ${MAP_W}
#define MAP_TILE_H      ${MAP_H}
#define TRAIL_LEN       48

/* Live Tuned Engine Feel Registers */
typedef struct {
    int16_t walk_speed_fp;        /* ${tuning.walkSpeedFp} (~${(tuning.walkSpeedFp / 256).toFixed(2)} px/frame) */
    int16_t dash_speed_fp;        /* ${tuning.dashSpeedFp} (~${(tuning.dashSpeedFp / 256).toFixed(2)} px/frame) */
    int16_t accel_fp;             /* ${tuning.accelFp} */
    uint8_t follow_dist_px;       /* ${tuning.followDistPx} px */
    uint8_t hitstop_normal_f;     /* ${tuning.hitstopNormalFrames} frames */
    uint8_t hitstop_crit_f;       /* ${tuning.hitstopCritFrames} frames */
    uint8_t hitstop_parry_f;      /* ${tuning.hitstopParryFrames} frames */
    uint8_t timed_hit_window_f;   /* +/-${tuning.timedHitWindowFrames} frames */
    uint8_t parry_window_f;       /* +/-${tuning.parryWindowFrames} frames */
} HearthTuning;

typedef struct {
    int16_t  x, y;
    uint16_t tile_id;
    uint8_t  palette_id;
    uint8_t  flags;      /* bit0: flipX, bit1: flashWhite, bit2: shadow */
    int16_t  sort_y;
} OamSprite;

typedef struct {
    fp16_t  fx, fy;
    fp16_t  vx, vy;
    uint8_t dir;         /* 0=D, 1=U, 2=L, 3=R */
    uint8_t anim_tick;
    uint8_t sprite_id;
    uint8_t aura_state;  /* 0=NONE, 1=RADIANT, 2=HOLLOW, 3=MENDED */
} HearthActor;

typedef enum {
    OP_WAIT = 0, OP_PAN_CAM, OP_CAM_FOLLOW, OP_FACE_ACTOR,
    OP_EMOTE, OP_SAY, OP_SFX, OP_SHAKE, OP_HEART_ORB, OP_SET_FLAG, OP_END
} CutsceneOpcode;

typedef struct {
    uint8_t opcode;
    int16_t arg0;
    int16_t arg1;
    uint8_t arg2;
} CutsceneInstr;

/* Core Platform Boundary — Zero OS/Hardware Calls Inside Core */
void hearth_init(const HearthTuning *tuning);
void hearth_step_60hz(uint16_t pad_bits, OamSprite *out_oam, uint8_t *out_oam_count);

#endif /* HEARTH_TYPES_H */
`;
}

export function generateHearthDataHeader(
  target: C99Target,
  map: WorldMapData,
  palettes: readonly (readonly string[])[],
  cutsceneOps: readonly CutsceneOpExport[]
): string {
  const palType = target === 'SDL2_C99' ? 'uint32_t' : 'uint16_t';
  const palComment =
    target === 'SGDK_GENESIS'
      ? 'Genesis VDP CRAM 9-bit BGR333 (0000BBB0GGG0RRR0)'
      : target === 'PVSNESLIB_SNES'
        ? 'SNES CGRAM 15-bit BGR555 (0BBBBBGGGGGRRRRR)'
        : 'Standard RGBA8888';

  const formatColor = (hex: string) =>
    target === 'SGDK_GENESIS'
      ? toGenesisBgr333(hex)
      : target === 'PVSNESLIB_SNES'
        ? toSnesBgr555(hex)
        : toRgba8888(hex);

  const palRows = palettes.map(
    (pal, idx) =>
      `    /* Sub-Palette ${idx} */\n    { ${pal.map(formatColor).join(', ')} }`
  ).join(',\n');

  const formatByteArray = (arr: Uint8Array, cols: number): string => {
    const lines: string[] = [];
    for (let y = 0; y < arr.length; y += cols) {
      const slice = Array.from(arr.slice(y, y + cols))
        .map((v) => v.toString().padStart(2, ' '))
        .join(',');
      lines.push(`    ${slice},`);
    }
    return lines.join('\n');
  };

  const opcodeNames = [
    'OP_WAIT',
    'OP_PAN_CAM',
    'OP_CAM_FOLLOW',
    'OP_FACE_ACTOR',
    'OP_EMOTE',
    'OP_SAY',
    'OP_SFX',
    'OP_SHAKE',
    'OP_HEART_ORB',
    'OP_SET_FLAG',
    'OP_END',
  ];

  const csLines = cutsceneOps.map((op) => {
    const code = opcodeNames[op.opcode] ?? 'OP_WAIT';
    return `    { ${code.padEnd(14, ' ')}, ${String(op.a0).padStart(3, ' ')}, ${String(op.a1).padStart(3, ' ')}, ${String(op.a2).padStart(3, ' ')} },`;
  }).join('\n');

  return `/* ============================================================================
 * hearth_data.h — Exported ROM Data Tables (Palettes, Tilemaps, Bytecode)
 * Palette Packing: ${palComment}
 * ============================================================================ */

#ifndef HEARTH_DATA_H
#define HEARTH_DATA_H

#include "hearth_types.h"

/* 4 x 16-Color Indexed Hardware Sub-Palettes */
static const ${palType} hearth_palettes[4][16] = {
${palRows}
};

/* BG1 Terrain & Architecture Metatile Map (${MAP_W} x ${MAP_H} = ${MAP_W * MAP_H} bytes) */
static const uint8_t hearth_map_bg1[${MAP_W * MAP_H}] = {
${formatByteArray(map.bg1, MAP_W)}
};

/* Collision Attribute Table (0=Walk, 1=Solid, 2=TallGrass, 3=ShallowFord, 4=Water) */
static const uint8_t hearth_map_col[${MAP_W * MAP_H}] = {
${formatByteArray(map.col, MAP_W)}
};

/* Intro Demo Cutscene Bytecode Script (${cutsceneOps.length} instructions) */
static const CutsceneInstr hearth_cutscene_intro[${cutsceneOps.length}] = {
${csLines}
};

#endif /* HEARTH_DATA_H */
`;
}
