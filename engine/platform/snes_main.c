/* ============================================================================
 * snes_main.c — Project Hearth Super Nintendo Host (PVSnesLib / WDC 65816)
 *
 * Strictly ANSI C89/C90 compatible with PVSnesLib's 816-tcc compiler and
 * WDC 65816 255-byte per-function stack frame limits.
 *
 * Full playable SNES PPU Mode 1 hardware renderer:
 *   - BG0 (4bpp, 32x32, prio 1): Fixed non-scrolling UI / Title / Dialogue / Battle HUD
 *   - BG1 (4bpp, 64x32, prio 0/1): Hardware-scrolling terrain + overhead canopies & battle arena
 *   - OBJ (4bpp, 16x16, prio 2/3): Procedural 16x16 sprites on 16-tile-wide VRAM sheet
 *   - Full 256-color CGRAM upload (BG palettes 0..6 + OBJ palettes 0..3)
 *   - Staggered VBlank DMA discipline (never exceeds 2 KB per VBlank frame)
 *
 * Build via top-level Makefile (auto-fetches PVSnesLib automatically!):
 *   make snes   -> build/snes/hearth.sfc
 * ============================================================================ */

#ifndef HEARTH_TARGET_SNES
#define HEARTH_TARGET_SNES 1
#endif

#include <snes.h>
#include "hearth_core.h"
#include "res/soundbank.h"
#include "cinema_cg_rom.h"
#define HEARTH_USE_PREBAKED_ROWAN 1
#include "hearth_hero24.h"
#define HEARTH_INCLUDE_SNES_TERRAIN 1
#include "hearth_terrain_rom.h"

extern char SOUNDBANK__;
extern char rowan_snes_tiles;
extern char snes_chrono_tiles;
extern char snes_chrono_anim_frames;

/* ============================================================================
 * 1. STATIC SNES WRAM BUFFERS (Bank $7E — zero malloc, tiny 65816 stacks!)
 * ============================================================================ */
HearthState g_hearth;

/* VRAM Word Addresses (Non-overlapping, 100% 4KB-word hardware-aligned 64 KB layout) */
#define VRAM_OBJ_GFX        0x0000u  /* 61 16x16 sprites -> 0x0000..0x0F9F (4KB aligned) */
#define VRAM_BG2_GFX        0x1000u  /* 2bpp Parallax Alpine Vista -> 0x1000..0x1FFF (4KB aligned! 512 2bpp tiles) */
#define VRAM_BG0_GFX        0x2000u  /* UI + Font + 384-tile Atlas -> 0x2000..0x3EFF (4KB aligned) */
#define VRAM_BG1_GFX        0x4000u  /* 512 4bpp tiles -> 0x4000..0x5FFF (Terrain OR 480-tile Lossless CG!) */
#define VRAM_CG_TILE_BASE   0u       /* Lossless 480-tile Cinema CG at BG1 tile 0..479 */
#define VRAM_CG_EMPTY_TILE  511u     /* Transparent letterbox tile during MODE_INTRO */
#define VRAM_BG2_MAP        0x6000u  /* 64x32 Parallax Alpine Vista tilemap (2048 words -> 0x6000..0x67FF) */
#define VRAM_BG0_MAP        0x6800u  /* 32x32 UI tilemap (1024 words = 2048 B) */
#define VRAM_BG1_MAP        0x7000u  /* 64x32 world tilemap (2048 words = 4096 B) */

/* Tilemap entry helpers: vhopppcc cccccccc */
#define SNES_TILE(t, pal, prio) \
    ((u16)(((u16)(t) & 0x03FFu) | (((u16)(pal) & 7u) << 10) | (((u16)(prio) & 1u) << 13)))
#define SNES_TILE_H(t, pal, prio) \
    ((u16)(SNES_TILE((t), (pal), (prio)) | 0x4000u))

/* UI Palette slots in BG CGRAM (4..7) */
#define PAL_UI_TEXT     4u  /* Warm cream on dark slate */
#define PAL_UI_GOLD     5u  /* Bright gold on dark slate */
#define PAL_UI_CYAN     6u  /* Arcane cyan on dark slate */
#define PAL_UI_ATLAS    7u  /* Dedicated 16-color World Atlas, Fishing & Battle Emblem palette */

/* BG0 UI Box, Pixel-Art Gauge/Lock, & Baked 24x16 World Atlas Tile Indices (at VRAM_BG0_GFX) */
enum {
    SUI_EMPTY = 0,
    SUI_FILL  = 1,
    SUI_TOP   = 2,
    SUI_BOT   = 3,
    SUI_LEFT  = 4,
    SUI_RIGHT = 5,
    SUI_TL    = 6,
    SUI_TR    = 7,
    SUI_BL    = 8,
    SUI_BR    = 9,
    /* True 4bpp Pixel-Art Fishing Gauge, Progress Bars & Battle Spell Lock Emblems (10..22) */
    SUI_FISH_CAP_L  = 10,
    SUI_FISH_CAP_R  = 11,
    SUI_FISH_WATER  = 12,
    SUI_FISH_ZONE_L = 13,
    SUI_FISH_ZONE_M = 14,
    SUI_FISH_ZONE_R = 15,
    SUI_BAR_BG      = 16,
    SUI_BAR_GRN     = 17,
    SUI_BAR_RED     = 18,
    SUI_LOCK_SWORD  = 19,
    SUI_LOCK_SUN    = 20,
    SUI_LOCK_STAR   = 21,
    SUI_LOCK_BROKEN = 22
};

#define SUI_ATLAS_BASE  112u /* 24x16 = 384 baked 4bpp World Atlas pixel tiles (112..495 -> 0x2700..0x3EFF) */
#define SUI_ATLAS_TW    24u
#define SUI_ATLAS_TH    16u

/* BG1 Terrain is pre-baked in ROM (snes_chrono_tiles: 415 4bpp tiles, 4 sub-palettes = 60 colors!) */

#define SPR_TIMED_RING_SLOT    49u  /* OBJ tile 0xC2 */
#define SPR_HERO_DOWN_1        50u  /* OBJ tile 0xC4 */
#define SPR_HERO_UP_0          51u  /* OBJ tile 0xC6 */
#define SPR_HERO_UP_1          52u  /* OBJ tile 0xC8 */
#define SPR_HERO_SIDE_0        53u  /* OBJ tile 0xCA */
#define SPR_HERO_SIDE_1        54u  /* OBJ tile 0xCC */
#define SPR_BATTLE_VFX_SLOT    55u  /* OBJ tile 0xCE */
#define SPR_ATLAS_HERO_SLOT    56u  /* OBJ tile 0xE0: Blinking Rowan Atlas Beacon */
#define SPR_ATLAS_RETICLE_SLOT 57u  /* OBJ tile 0xE2: Pulsing Designer Warp Reticle */
#define SPR_FISH_ICON_SLOT     58u  /* OBJ tile 0xE4: 16x16 Swimming Pixel Fish */
#define SPR_PIP_HOP_SLOT       59u  /* OBJ tile 0xE6: 16x16 Pip Mid-Air Stretch Hop */
#define SPR_PIP_SQUASH_SLOT    60u  /* OBJ tile 0xE8: 16x16 Pip Landing/Takeoff Squash */

/* Tilemap shadow buffers & pixel baker scratchpads in Bank $7E WRAM */
static u16 g_bg0_map[1024];
static u16 g_bg1_map[2048];
static u8  g_world_meta[HEARTH_MAX_MAP_CELLS];
static u8  g_pix8[64];
static u8  g_pix16[256];
static u8  g_pix24[384];
static u8  g_tile32[32];
static u16 g_pal16[16];
static char g_line[36];
static char g_intro_buf[128];
static u8  g_atlas_row3[3][64];
static u8  g_atlas_below[64];
static u8  g_lm_px[HEARTH_GLOBE_LANDMARK_COUNT];
static u8  g_lm_py[HEARTH_GLOBE_LANDMARK_COUNT];

/* Rowan 16x24 zero-CPU ROM VBlank DMA state (33 pre-baked 6-tile poses in rowan_snes_tiles):
 *   +0..+63 = top row (0x00, 0x01), +64..+127 = mid row (0x10, 0x11), +128..+191 = boot strip (0xC4, 0xC5) */
static u8  g_rowan_pose_idx   = 0u;
static u8  g_last_rowan_key   = 0xFFu;
static u8  g_rowan_dma_dirty  = 0u;
static u8  g_last_bright      = 15u;

static u8  g_bg0_dirty        = 0;
static u8  g_bg1_dirty_l      = 0;
static u8  g_full_pal_dirty   = 0;
static u8  g_bg2_enabled      = 0;
static u8  g_bg1_dirty_r      = 0;
static u8  g_bg1_row_dirty    = 0;
static u16 g_bg1_dirty_row_off = 0u;
static u8  g_env_dirty        = 0;
static u8  g_env_frame        = 255;
static u8  g_prev_oam_cnt     = 0;
static s16 g_last_cam_mx      = -999;
static s16 g_last_cam_my      = -999;
static u8  g_last_scene_kind  = 255;
static u8  g_last_intro_scene = 255;
static u8  g_last_pal_id      = 255;
static u8  g_pal_dirty        = 0;
static u8  g_last_map_rev     = 255;
static u8  g_ui_layout        = 255;
static u8  g_ui_tick          = 0;
static u16 g_last_hud_sig     = 0xFFFFu;
static u16 g_cur_bgm_mod      = 0xFFFFu;

static u8  g_atlas_baked      = 0u;
static u8  g_spc_booted       = 0u;
static u8  g_spc_playing      = 0u;
static u8  g_boot_stage       = 0u;

/* Scratch buffer in Bank $7E WRAM for fast strip-batched open-world globe streaming */
static u8 g_stream_meta[18];
/* 32x32 4bpp canvas in Bank $7E WRAM for procedural retro boot splash emblems */
static u8 g_splash_32[1024];

/* Windmill 16x16 OBJ: [frame][0..1 = top row T,T+1; 2..3 = bottom row T+16,T+17] */
static u8 g_anim_windmill[4][4][32];

/* Boot-strip left 8x8 OBJ tile index for tall 16x24 sprites (0 = standard 16x16 sprite).
 * Packed into freed 16x16 slots 50..54 & 61..62 (2 horizontal 16x8 strips per slot, all <= 0xFD). */
static const u8 SPR_BOOT_TILE_LUT[49] = {
    0xC4u, 0x00u, 0x00u, 0x00u, /* 0..3: Hero, Pip, Bramble, Lumina */
    0xC6u, 0xD6u, 0xC8u,        /* 4..6: Elder, Clara, Guard */
    0x00u, 0x00u, 0x00u, 0x00u, /* 7..10: Enemies */
    0x00u, 0x00u, 0x00u,        /* 11..13: Windmill, Waystone, Chest */
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, /* 14..18: Emotes + Orb */
    0x00u, 0x00u, 0x00u,        /* 19..21: Chest Open, Mouse, Pig */
    0xECu,                      /* 22: Shadow Grandma */
    0x00u,                      /* 23: Mido */
    0x00u, 0x00u, 0x00u, 0x00u, 0x00u, /* 24..28: Enemies */
    0x00u,                      /* 29: Hollow Oak */
    0x00u,                      /* 30: Bobber */
    0xFAu,                      /* 31: Alderbrook Townsfolk */
    0xD4u, 0xD8u, 0xCAu, 0xDAu, 0x00u, /* 32..36: Mara, Barnaby, Maple, Jasper, Loco */
    0xCCu, 0xDCu, 0xEAu,        /* 37..39: Tessa, Corin, Sentinel */
    0x00u,                      /* 40: Raft */
    0x00u, 0x00u,               /* 41..42: Desert Enemies */
    0x00u, 0x00u, 0x00u, 0x00u, /* 43..46: Fauna */
    0xFCu, 0x00u                /* 47..48: Shipwright Orin, Babana Stall */
};

/* ============================================================================
 * 2. COMPACT 5x7 UPPERCASE ASCII FONT (ASCII 32..95) -> OPAQUE 4bpp BG0 TILES
 * ============================================================================ */
static const u8 SNES_FONT_5X7[64][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* ' ' */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x04}, /* '!' */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* '"' */
    {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00}, /* '#' */
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, /* '$' */
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, /* '%' */
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, /* '&' */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ''' */
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, /* '(' */
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, /* ')' */
    {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00}, /* '*' */
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, /* '+' */
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, /* ',' */
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, /* '-' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x04}, /* '.' */
    {0x01,0x01,0x02,0x04,0x08,0x10,0x10}, /* '/' */
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* '0' */
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* '1' */
    {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* '2' */
    {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E}, /* '3' */
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* '4' */
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* '5' */
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* '6' */
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* '7' */
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* '8' */
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* '9' */
    {0x00,0x04,0x00,0x00,0x00,0x04,0x00}, /* ':' */
    {0x00,0x04,0x00,0x00,0x04,0x04,0x08}, /* ';' */
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, /* '<' */
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, /* '=' */
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, /* '>' */
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, /* '?' */
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, /* '@' */
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* 'A' */
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* 'B' */
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* 'C' */
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* 'D' */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* 'E' */
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* 'F' */
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* 'G' */
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* 'H' */
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* 'I' */
    {0x07,0x02,0x02,0x02,0x12,0x12,0x0C}, /* 'J' */
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* 'K' */
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* 'L' */
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* 'M' */
    {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* 'N' */
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* 'O' */
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* 'P' */
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* 'Q' */
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* 'R' */
    {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E}, /* 'S' */
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* 'T' */
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* 'U' */
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* 'V' */
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, /* 'W' */
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* 'X' */
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* 'Y' */
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* 'Z' */
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, /* '[' */
    {0x10,0x10,0x08,0x04,0x02,0x01,0x01}, /* '\' */
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, /* ']' */
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, /* '^' */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}  /* '_' */
};

/* ============================================================================
 * 3. SNES 4bpp PLANAR TILE & 16x16 OBJ SHEET BAKER (Called during Force Blank)
 * ============================================================================ */
static const u8 SNES_BIT_MASK[8] = {
    0x80u, 0x40u, 0x20u, 0x10u, 0x08u, 0x04u, 0x02u, 0x01u
};

static void encode_pix8_to_buf(u8 *dst32) {
    u8 y, x, mask, c;
    u8 bp0, bp1, bp2, bp3;
    const u8 *rp = g_pix8;
    u8 *p01 = dst32;
    u8 *p23 = dst32 + 16u;

    for (y = 0; y < 8u; y++) {
        bp0 = 0;
        bp1 = 0;
        bp2 = 0;
        bp3 = 0;
        for (x = 0; x < 8u; x++) {
            c = *rp++;
            if (c != 0u) {
                mask = SNES_BIT_MASK[x];
                if (c & 1u) bp0 |= mask;
                if (c & 2u) bp1 |= mask;
                if (c & 4u) bp2 |= mask;
                if (c & 8u) bp3 |= mask;
            }
        }
        *p01++ = bp0;
        *p01++ = bp1;
        *p23++ = bp2;
        *p23++ = bp3;
    }
}

static void encode_and_upload_tile8(u16 vram_word_addr) {
    encode_pix8_to_buf(g_tile32);
    dmaCopyVram(g_tile32, vram_word_addr, 32u);
}

static void upload_bg0_tile(u16 tile_idx) {
    encode_and_upload_tile8((u16)(VRAM_BG0_GFX + (tile_idx << 4)));
}

/* Instant single-DMA upload of the unified 415-tile Chrono/Mana 4bpp terrain bank from ROM! */
static void upload_chrono_terrain_vram(void) {
    dmaCopyVram((u8 *)&snes_chrono_tiles, VRAM_BG1_GFX, (u16)(SNES_CHRONO_TILE_COUNT * 32u));
}

/* Encode 8x8 2bpp tile (color indices 0..3) and upload to BG2 Parallax VRAM */
static void upload_bg2_tile_2bpp(u16 tile_idx) {
    u8 y, x, p0, p1, c, bit;
    const u8 *rp = g_pix8;
    u8 *dp = g_tile32;
    for (y = 0u; y < 8u; y++) {
        p0 = 0u; p1 = 0u;
        for (x = 0u; x < 8u; x++) {
            c = (u8)(*rp++ & 3u);
            if (c != 0u) {
                bit = SNES_BIT_MASK[x];
                if (c & 1u) p0 |= bit;
                if (c & 2u) p1 |= bit;
            }
        }
        *dp++ = p0;
        *dp++ = p1;
    }
    dmaCopyVram(g_tile32, (u16)(VRAM_BG2_GFX + (tile_idx << 3)), 16u);
}

static u16 sprite_slot_to_tile(u8 slot) {
    return (u16)((((u16)slot >> 3) << 5) + (((u16)slot & 7u) << 1));
}

static void copy_sub16_to_pix8(u8 ox, u8 oy) {
    u8 y, x;
    for (y = 0; y < 8u; y++) {
        for (x = 0; x < 8u; x++) {
            g_pix8[(u16)(y * 8u + x)] = g_pix16[(u16)((oy + y) * 16u + (ox + x))];
        }
    }
}

/* Upload 16x16 sprite onto SNES 16-tile-wide OBJ sheet: T, T+1, T+16, T+17 */
static void upload_obj_sprite16(u8 slot) {
    u16 t = sprite_slot_to_tile(slot);
    copy_sub16_to_pix8(0, 0);
    encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((t + 0u) << 4)));
    copy_sub16_to_pix8(8, 0);
    encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((t + 1u) << 4)));
    copy_sub16_to_pix8(0, 8);
    encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((t + 16u) << 4)));
    copy_sub16_to_pix8(8, 8);
    encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((t + 17u) << 4)));
}

static void copy_sub24_to_pix8(u8 ox, u8 oy) {
    u8 y, x;
    for (y = 0u; y < 8u; y++) {
        for (x = 0u; x < 8u; x++) {
            g_pix8[(u16)(y * 8u + x)] = g_pix24[(u16)((u16)(oy + y) * 16u + (u16)(ox + x))];
        }
    }
}

/* Upload 16x24 sprite during Force Blank: top 16x16 into slot, bottom 16x8 into boot_tile & boot_tile+1 */
static void upload_obj_sprite24_fb(u8 slot, u8 boot_tile) {
    u16 t = sprite_slot_to_tile(slot);
    u16 bt = (u16)boot_tile;
    copy_sub24_to_pix8(0u, 0u);  encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((t + 0u) << 4)));
    copy_sub24_to_pix8(8u, 0u);  encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((t + 1u) << 4)));
    copy_sub24_to_pix8(0u, 8u);  encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((t + 16u) << 4)));
    copy_sub24_to_pix8(8u, 8u);  encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((t + 17u) << 4)));
    copy_sub24_to_pix8(0u, 16u); encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((bt + 0u) << 4)));
    copy_sub24_to_pix8(8u, 16u); encode_and_upload_tile8((u16)(VRAM_OBJ_GFX + ((bt + 1u) << 4)));
}

/* Map (dir 0..3, pose 0..11) -> pre-baked ROM index (0..32) in ~5 CPU cycles */
static u8 snes_rowan_pose_idx(u8 dir, u8 pose) {
    static const u8 DIR_BASE[4] = { 0u, 7u, 14u, 21u };
    if (pose >= HERO_POSE_BTL_READY) {
        u8 bp = (u8)(pose - HERO_POSE_BTL_READY);
        return (u8)(28u + ((bp < 5u) ? bp : 0u));
    }
    return (u8)(DIR_BASE[dir & 3u] + pose);
}

/* Stream Rowan's pre-baked 16x24 pose directly from FastROM during VBlank (~1.1 scanlines, 0 CPU bake!) */
static void flush_rowan_vblank_dma(void) {
    u16 off = (u16)(((u16)g_rowan_pose_idx << 7) + ((u16)g_rowan_pose_idx << 6));
    u8 *src = ((u8 *)&rowan_snes_tiles) + off;
    dmaCopyVram(src,               (u16)(VRAM_OBJ_GFX + (0x00u << 4)), 64u);
    dmaCopyVram((u8 *)(src + 64u), (u16)(VRAM_OBJ_GFX + (0x10u << 4)), 64u);
    dmaCopyVram((u8 *)(src + 128u),(u16)(VRAM_OBJ_GFX + (0xC4u << 4)), 64u);
    g_rowan_dma_dirty = 0u;
}

static void p8_fill(u8 col) {
    hearth_memset(g_pix8, col, 64u);
}

static void p8_rect(u8 x, u8 y, u8 w, u8 h, u8 col) {
    u8 py, px;
    for (py = y; py < (u8)(y + h) && py < 8u; py++) {
        for (px = x; px < (u8)(x + w) && px < 8u; px++) {
            g_pix8[(u16)(py * 8u + px)] = col;
        }
    }
}

static void p16_clear(void) {
    hearth_memset(g_pix16, 0, 256u);
}

static void p16_rect(u8 x, u8 y, u8 w, u8 h, u8 col) {
    u8 py, px;
    for (py = y; py < (u8)(y + h) && py < 16u; py++) {
        for (px = x; px < (u8)(x + w) && px < 16u; px++) {
            g_pix16[(u16)(py * 16u + px)] = col;
        }
    }
}

/* ============================================================================
 * 4. CGRAM PALETTES (BG 0..6 + OBJ 0..3)
 * ============================================================================ */
static void upload_all_palettes(void) {
    u16 p, c;

    /* Upload 4 master palettes to BOTH BG CGRAM (0..63) and OBJ CGRAM (128..191) */
    for (p = 0; p < 4u; p++) {
        for (c = 0; c < 16u; c++) {
            g_pal16[c] = hearth_rgb_to_snes_bgr555(HEARTH_PALETTES_RGB[p][c]);
        }
        /* Backdrop color 0 = deep warm twilight slate (never harsh void black) */
        if (p == 0u) {
            g_pal16[0] = hearth_rgb_to_snes_bgr555(0x1D2824u);
        }
        dmaCopyCGram((u8 *)g_pal16, (u16)(p * 16u), 32u);
        dmaCopyCGram((u8 *)g_pal16, (u16)(128u + p * 16u), 32u);
    }

    /* BG Palette 4: UI Normal (1=dark slate, 7=gold border, 15=warm cream text) */
    for (c = 0; c < 16u; c++) g_pal16[c] = hearth_rgb_to_snes_bgr555(0x1D1B22u);
    g_pal16[0]  = 0u;
    g_pal16[1]  = hearth_rgb_to_snes_bgr555(0x1B1F2Eu);
    g_pal16[7]  = hearth_rgb_to_snes_bgr555(0xD8A24Au);
    g_pal16[15] = hearth_rgb_to_snes_bgr555(0xF6ECD4u);
    dmaCopyCGram((u8 *)g_pal16, (u16)(PAL_UI_TEXT * 16u), 32u);

    /* BG Palette 5: UI Gold Accent (15=bright goldenrod) */
    g_pal16[15] = hearth_rgb_to_snes_bgr555(0xFFE066u);
    dmaCopyCGram((u8 *)g_pal16, (u16)(PAL_UI_GOLD * 16u), 32u);

    /* BG Palette 6: UI Arcane Cyan Accent (15=sky cyan) */
    g_pal16[15] = hearth_rgb_to_snes_bgr555(0x74E4FFu);
    dmaCopyCGram((u8 *)g_pal16, (u16)(PAL_UI_CYAN * 16u), 32u);

    /* BG Palette 7: Dedicated 16-Color World Atlas, Fishing Gauge & Battle Emblem Palette */
    {
        static const uint32_t ATLAS_PAL_RGB[16] = {
            0x000000u, /* 0: Transparent */
            0x161322u, /* 1: Dark Slate BG / Pin Outline */
            0x1F4C82u, /* 2: Deep Cobalt Ocean (cell 0) */
            0x3EB4CCu, /* 3: Turquoise Shallows / Raft Channel (cell 1) */
            0xE87EA8u, /* 4: Coral Reef Shoals (cell 2) */
            0x5EA84Eu, /* 5: Emerald Heartland / Catch Zone (cell 3) */
            0xE4BC62u, /* 6: Sands of Babana / Gold Trim (cell 4) */
            0x56C49Au, /* 7: Suncrest Paradise / Catch Glow (cell 5) */
            0xE6F0FCu, /* 8: Frostmere North / Snow (cell 6) */
            0x226E3Au, /* 9: Canopy Wilds (cell 7) */
            0xD44824u, /* 10: Cinderpeak Caldera / Tension Red (cell 8) */
            0x4898C8u, /* 11: Aetheris Citadel / Water Mid (cell 9) */
            0xFFF1A8u, /* 12: Bright Gold (Port Pin / Sun Lock / Catch Highlight) */
            0x74D8F5u, /* 13: Sky Cyan (Vista Pin / Star Lock / Water Ripple) */
            0xFF9838u, /* 14: Warm Amber (Tension Mid / Bronze Frame) */
            0xF6ECD4u  /* 15: Warm Cream (Sword Blade / Crisp Gleam) */
        };
        for (c = 0u; c < 16u; c++) {
            g_pal16[c] = hearth_rgb_to_snes_bgr555(ATLAS_PAL_RGB[c]);
        }
        dmaCopyCGram((u8 *)g_pal16, (u16)(PAL_UI_ATLAS * 16u), 32u);
    }
    g_last_pal_id = ZPAL_HAMLET;
}

static void upload_zone_pal0(u8 pal_id) {
    u16 c;
    u8 pid;

    pid = (pal_id < ZPAL_COUNT) ? pal_id : 0u;
    /* Upload all 4 Chrono/Mana terrain sub-palettes (60 simultaneous colors in CGRAM 0..63)!
     * PAL1 (CGRAM 16..31) is mathematically harmonized with BG2 2bpp PAL4..PAL7 Alpine Vista! */
    for (c = 0u; c < 16u; c++) {
        g_pal16[c] = SNES_CHRONO_PALS[pid][0][c];
    }
    if (!g_hearth.is_world_map && g_hearth.ppu.has_mountain_vista) {
        g_pal16[0] = SNES_CHRONO_PALS[pid][1][2]; /* Seamless alpine mid-sky backdrop */
    } else {
        g_pal16[0] = SNES_CHRONO_PALS[pid][0][1]; /* Deep forest/shadow backdrop */
    }
    dmaCopyCGram((u8 *)g_pal16, 0u, 32u);
    dmaCopyCGram((u8 *)SNES_CHRONO_PALS[pid][1], 16u, 32u);
    dmaCopyCGram((u8 *)SNES_CHRONO_PALS[pid][2], 32u, 32u);
    dmaCopyCGram((u8 *)SNES_CHRONO_PALS[pid][3], 48u, 32u);
}

/* ============================================================================
 * 5. BAKE BG0 UI/FONT AND 16x16 OBJ SPRITES (BG1 Terrain streams straight from ROM!)
 * ============================================================================ */

static void bake_bg0_ui_and_font(void) {
    u16 ch;
    u8 y, x, bits;

    /* Tile 0: Transparent window cutout */
    p8_fill(0); upload_bg0_tile(SUI_EMPTY);

    /* Tiles 1..9: Dark slate panel with gold + cream storybook trim */
    p8_fill(1); upload_bg0_tile(SUI_FILL);

    p8_fill(1); p8_rect(0, 0, 8, 1, 7); p8_rect(0, 1, 8, 1, 15);
    upload_bg0_tile(SUI_TOP);

    p8_fill(1); p8_rect(0, 7, 8, 1, 7); p8_rect(0, 6, 8, 1, 15);
    upload_bg0_tile(SUI_BOT);

    p8_fill(1); p8_rect(0, 0, 1, 8, 7); p8_rect(1, 0, 1, 8, 15);
    upload_bg0_tile(SUI_LEFT);

    p8_fill(1); p8_rect(7, 0, 1, 8, 7); p8_rect(6, 0, 1, 8, 15);
    upload_bg0_tile(SUI_RIGHT);

    p8_fill(1); p8_rect(0, 0, 8, 2, 7); p8_rect(0, 0, 2, 8, 7);
    p8_rect(1, 1, 7, 1, 15); p8_rect(1, 1, 1, 7, 15);
    upload_bg0_tile(SUI_TL);

    p8_fill(1); p8_rect(0, 0, 8, 2, 7); p8_rect(6, 0, 2, 8, 7);
    p8_rect(0, 1, 7, 1, 15); p8_rect(6, 1, 1, 7, 15);
    upload_bg0_tile(SUI_TR);

    p8_fill(1); p8_rect(0, 6, 8, 2, 7); p8_rect(0, 0, 2, 8, 7);
    p8_rect(1, 6, 7, 1, 15); p8_rect(1, 0, 1, 7, 15);
    upload_bg0_tile(SUI_BL);

    p8_fill(1); p8_rect(0, 6, 8, 2, 7); p8_rect(6, 0, 2, 8, 7);
    p8_rect(0, 6, 7, 1, 15); p8_rect(6, 0, 1, 7, 15);
    upload_bg0_tile(SUI_BR);

    /* Tiles 10..22: True 4bpp Pixel-Art Fishing Gauge, Progress Bars & Battle Spell Locks (PAL_UI_ATLAS) */
    /* 10: SUI_FISH_CAP_L (Brass & Slate Rounded Left Endcap) */
    p8_fill(1);
    p8_rect(3, 0, 5, 1, 6);  p8_rect(3, 7, 5, 1, 6);
    p8_rect(2, 1, 6, 6, 14); p8_rect(4, 1, 4, 6, 6);
    p8_rect(5, 2, 3, 4, 2);  p8_rect(6, 2, 2, 2, 11);
    upload_bg0_tile(SUI_FISH_CAP_L);

    /* 11: SUI_FISH_CAP_R (Brass & Slate Rounded Right Endcap) */
    p8_fill(1);
    p8_rect(0, 0, 5, 1, 6);  p8_rect(0, 7, 5, 1, 6);
    p8_rect(0, 1, 6, 6, 14); p8_rect(0, 1, 4, 6, 6);
    p8_rect(0, 2, 3, 4, 2);  p8_rect(0, 2, 2, 2, 11);
    upload_bg0_tile(SUI_FISH_CAP_R);

    /* 12: SUI_FISH_WATER (Deep Shimmering Water Trough) */
    p8_fill(2);
    p8_rect(0, 0, 8, 1, 6);  p8_rect(0, 7, 8, 1, 6);
    p8_rect(0, 1, 8, 1, 1);  p8_rect(0, 6, 8, 1, 1);
    p8_rect(1, 2, 3, 1, 11); p8_rect(4, 4, 3, 1, 11);
    g_pix8[18] = 13; g_pix8[37] = 13;
    upload_bg0_tile(SUI_FISH_WATER);

    /* 13: SUI_FISH_ZONE_L (Glowing Emerald Catch Zone Left Edge) */
    p8_fill(5);
    p8_rect(0, 0, 8, 1, 12); p8_rect(0, 7, 8, 1, 12);
    p8_rect(0, 1, 2, 6, 12); p8_rect(2, 1, 6, 2, 7);
    p8_rect(2, 5, 6, 2, 9);  g_pix8[18] = 15;
    upload_bg0_tile(SUI_FISH_ZONE_L);

    /* 14: SUI_FISH_ZONE_M (Glowing Emerald Catch Zone Center) */
    p8_fill(5);
    p8_rect(0, 0, 8, 1, 12); p8_rect(0, 7, 8, 1, 12);
    p8_rect(0, 1, 8, 2, 7);  p8_rect(0, 5, 8, 2, 9);
    p8_rect(2, 2, 4, 1, 12);
    upload_bg0_tile(SUI_FISH_ZONE_M);

    /* 15: SUI_FISH_ZONE_R (Glowing Emerald Catch Zone Right Edge) */
    p8_fill(5);
    p8_rect(0, 0, 8, 1, 12); p8_rect(0, 7, 8, 1, 12);
    p8_rect(6, 1, 2, 6, 12); p8_rect(0, 1, 6, 2, 7);
    p8_rect(0, 5, 6, 2, 9);  g_pix8[21] = 15;
    upload_bg0_tile(SUI_FISH_ZONE_R);

    /* 16: SUI_BAR_BG (Empty Slim Progress/Tension Meter Slot) */
    p8_fill(1);
    p8_rect(0, 1, 8, 6, 2);
    p8_rect(0, 2, 8, 4, 1);
    upload_bg0_tile(SUI_BAR_BG);

    /* 17: SUI_BAR_GRN (Filled Emerald-Gold Catch Progress Bar) */
    p8_fill(1);
    p8_rect(0, 1, 8, 6, 9);
    p8_rect(0, 2, 8, 4, 5);
    p8_rect(0, 2, 8, 1, 7);
    p8_rect(1, 2, 6, 1, 12);
    upload_bg0_tile(SUI_BAR_GRN);

    /* 18: SUI_BAR_RED (Filled Crimson-Amber Tension Bar) */
    p8_fill(1);
    p8_rect(0, 1, 8, 6, 10);
    p8_rect(0, 2, 8, 3, 14);
    p8_rect(0, 2, 8, 1, 12);
    upload_bg0_tile(SUI_BAR_RED);

    /* 19: SUI_LOCK_SWORD (Pixel-Art Sword Emblem for Battle Locks) */
    p8_fill(1);
    p8_rect(1, 1, 6, 6, 2);
    g_pix8[14] = 15; g_pix8[21] = 15; g_pix8[28] = 15; g_pix8[35] = 15;
    g_pix8[13] = 8;  g_pix8[20] = 8;  g_pix8[27] = 8;
    g_pix8[34] = 12; g_pix8[43] = 12; g_pix8[42] = 6;  g_pix8[49] = 14;
    upload_bg0_tile(SUI_LOCK_SWORD);

    /* 20: SUI_LOCK_SUN (Pixel-Art Radiant Sun Emblem for Battle Locks) */
    p8_fill(1);
    p8_rect(1, 1, 6, 6, 2);
    p8_rect(2, 2, 4, 4, 14); p8_rect(3, 3, 2, 2, 12);
    g_pix8[11] = 12; g_pix8[12] = 12; g_pix8[51] = 12; g_pix8[52] = 12;
    g_pix8[25] = 12; g_pix8[33] = 12; g_pix8[30] = 12; g_pix8[38] = 12;
    upload_bg0_tile(SUI_LOCK_SUN);

    /* 21: SUI_LOCK_STAR (Pixel-Art Arcane Star Emblem for Battle Locks) */
    p8_fill(1);
    p8_rect(1, 1, 6, 6, 2);
    p8_rect(3, 1, 2, 6, 13); p8_rect(1, 3, 6, 2, 13);
    p8_rect(3, 3, 2, 2, 8);
    upload_bg0_tile(SUI_LOCK_STAR);

    /* 22: SUI_LOCK_BROKEN (Cracked Dim Socket for Shattered Battle Lock) */
    p8_fill(1);
    p8_rect(2, 2, 4, 4, 2);
    g_pix8[18] = 1; g_pix8[27] = 1; g_pix8[36] = 1; g_pix8[45] = 1;
    upload_bg0_tile(SUI_LOCK_BROKEN);

    /* Tiles 32..95: Direct 4bpp planar encoding for 5x7 ASCII font (bg=1, fg=15 -> bp0=0xFF, bp1=bp2=bp3=mask)
     * 50x faster than pixel-by-pixel encoding (~2ms on 65816 -> instant splash screen boot!) */
    for (ch = 0; ch < 64u; ch++) {
        u8 *p01 = g_tile32;
        u8 *p23 = g_tile32 + 16u;
        for (y = 0; y < 7u; y++) {
            bits = (u8)((SNES_FONT_5X7[ch][y] & 0x1Fu) << 2);
            *p01++ = 0xFFu;
            *p01++ = bits;
            *p23++ = bits;
            *p23++ = bits;
        }
        *p01++ = 0xFFu; *p01++ = 0x00u;
        *p23++ = 0x00u; *p23++ = 0x00u;
        dmaCopyVram(g_tile32, (u16)(VRAM_BG0_GFX + ((u16)(32u + ch) << 4)), 32u);
    }
    (void)x;
}

/* Bake full-color 192x128 (24x16 tile) 4bpp Pixel-Art World Atlas into BG0 VRAM (lazy on first open!).
 * Tile-level landmark culling skips the 14-pin inner check on ~93% of tiles (~12x faster!). */
static void bake_bg0_atlas_bitmap(void) {
    u16 ty, tx, gx, gy, gx0, gy0, tile_idx;
    u8 py, px, ax, ax0, rem3, rem3_0, sub, cell, col, i, ay0, ay_next, hit_cnt, h;
    u8 hit_lm[4];
    s16 dx, dy;

    /* Precompute exact pixel coordinates of all 14 globe landmarks on the 192x128 map */
    for (i = 0u; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
        u8 qy = (u8)(HEARTH_GLOBE_LANDMARKS[i].ty >> 2);
        u8 row3 = 0u;
        while (qy >= 3u) { qy = (u8)(qy - 3u); row3++; }
        g_lm_px[i] = (u8)((HEARTH_GLOBE_LANDMARKS[i].tx >> 2) * 3u + 1u);
        g_lm_py[i] = (u8)((row3 << 3) + ((qy == 0u) ? 1u : ((qy == 1u) ? 4u : 6u)));
    }

    tile_idx = SUI_ATLAS_BASE;
    for (ty = 0u; ty < SUI_ATLAS_TH; ty++) {
        gy0 = (u16)(ty << 3);
        ay0 = (u8)(ty + ty + ty);
        for (ax = 0u; ax < HEARTH_ATLAS_W; ax++) {
            g_atlas_row3[0][ax] = hearth_globe_atlas_cell(ax, ay0);
            g_atlas_row3[1][ax] = hearth_globe_atlas_cell(ax, (u8)(ay0 + 1u));
            g_atlas_row3[2][ax] = hearth_globe_atlas_cell(ax, (u8)(ay0 + 2u));
            ay_next = (u8)(ay0 + 3u);
            if (ay_next >= HEARTH_ATLAS_H) ay_next = 0u;
            g_atlas_below[ax] = hearth_globe_atlas_cell(ax, ay_next);
        }

        for (tx = 0u; tx < SUI_ATLAS_TW; tx++) {
            gx0 = (u16)(tx << 3);
            ax0 = 0u;
            rem3_0 = (u8)gx0;
            while (rem3_0 >= 3u) { rem3_0 = (u8)(rem3_0 - 3u); ax0++; }

            /* Pre-cull landmarks to this 8x8 tile bounding box */
            hit_cnt = 0u;
            for (i = 0u; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
                dx = (s16)g_lm_px[i] - (s16)gx0;
                if (dx < -2 || dx > 9) continue;
                dy = (s16)g_lm_py[i] - (s16)gy0;
                if (dy < -2 || dy > 9) continue;
                if (hit_cnt < 4u) hit_lm[hit_cnt++] = i;
            }

            for (py = 0u; py < 8u; py++) {
                u8 *rp = g_pix8 + ((u16)py << 3);
                gy = (u16)(gy0 + py);
                sub = (py < 3u) ? 0u : ((py < 6u) ? 1u : 2u);
                gx = gx0;
                ax = ax0;
                rem3 = rem3_0;

                for (px = 0u; px < 8u; px++) {
                    if (gx == 0u || gx == 191u || gy == 0u || gy == 127u) {
                        col = 6u; /* Gilded bronze outer map border */
                    } else {
                        cell = g_atlas_row3[sub][ax];
                        col = (u8)(cell + 2u);
                        /* Crisp 1px coastline shadow where land meets deep ocean to the south */
                        if (cell >= 3u && (py == 2u || py == 5u || py == 7u)) {
                            u8 sc = (sub < 2u) ? g_atlas_row3[(u8)(sub + 1u)][ax] : g_atlas_below[ax];
                            if (sc == 0u) col = 1u;
                        } else if (cell == 0u) {
                            /* Subtle toroidal graticule ticks on deep ocean */
                            if (((gx == 48u || gx == 96u || gx == 144u) && ((gy & 1u) == 0u)) ||
                                ((gy == 32u || gy == 64u || gy == 96u) && ((gx & 1u) == 0u))) {
                                col = 11u;
                            }
                        }

                        /* Stamp framed pixel-art landmark pins (only for landmarks touching this tile!) */
                        for (h = 0u; h < hit_cnt; h++) {
                            i = hit_lm[h];
                            dy = (s16)gy - (s16)g_lm_py[i];
                            if (dy < -2 || dy > 2) continue;
                            dx = (s16)gx - (s16)g_lm_px[i];
                            if (dx < -2 || dx > 2) continue;
                            if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
                                col = (dx == 0 && dy == 0) ? 15u
                                    : (HEARTH_GLOBE_LANDMARKS[i].enterable ? 12u : 13u);
                            } else if ((dx != -2 && dx != 2) || (dy != -2 && dy != 2)) {
                                col = 1u;
                            }
                        }
                    }

                    *rp++ = col;
                    gx++;
                    rem3++;
                    if (rem3 >= 3u) { rem3 = 0u; ax++; }
                }
            }
            upload_bg0_tile(tile_idx++);
        }
    }
}

static void bake_obj_sprites_part1(void) {
    /* 0: SPR_HERO (Rowan facing DOWN idle) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 4); p16_rect(5, 1, 6, 2, 5);
    p16_rect(4, 4, 8, 4, 2); g_pix16[86] = 1; g_pix16[89] = 1;
    p16_rect(4, 8, 8, 2, 9); p16_rect(3, 9, 2, 3, 8);
    p16_rect(3, 10, 10, 4, 6); p16_rect(5, 10, 6, 3, 7);
    g_pix16[195] = 2; g_pix16[204] = 2;
    p16_rect(5, 14, 2, 2, 4); p16_rect(9, 14, 2, 2, 4);
    upload_obj_sprite16(SPR_HERO);

    /* 1: SPR_PIP — Petite ~11x13 Wingless Garden-Gnome Pip (Idle / Grounded Pose, PAL1) */
    hearth_bake_pip_16x16(g_pix16, 0u);
    upload_obj_sprite16(SPR_PIP);

    /* 2: SPR_FAMILIAR_BRAMBLE */
    p16_clear();
    p16_rect(4, 2, 8, 4, 5); p16_rect(7, 0, 2, 2, 4);
    p16_rect(4, 6, 8, 6, 7); p16_rect(5, 7, 6, 4, 11);
    g_pix16[118] = 1; g_pix16[121] = 1;
    p16_rect(12, 6, 2, 6, 10);
    p16_rect(5, 12, 2, 3, 4); p16_rect(9, 12, 2, 3, 4);
    upload_obj_sprite16(SPR_FAMILIAR_BRAMBLE);

    /* 3: SPR_FAMILIAR_LUMINA */
    p16_clear();
    p16_rect(6, 2, 4, 10, 13); p16_rect(3, 5, 10, 4, 13);
    p16_rect(5, 4, 6, 6, 14); p16_rect(6, 5, 4, 4, 11);
    g_pix16[102] = 12; g_pix16[105] = 12;
    upload_obj_sprite16(SPR_FAMILIAR_LUMINA);

    /* 4: SPR_NPC_ELDER (Elder Wren - clear eyes, brows, white beard & golden staff) */
    p16_clear();
    p16_rect(4, 1, 8, 3, 15); p16_rect(5, 1, 6, 1, 4);
    p16_rect(4, 4, 8, 4, 2);
    g_pix16[69] = 15; g_pix16[70] = 15; g_pix16[73] = 15; g_pix16[74] = 15;
    g_pix16[86] = 1;  g_pix16[89] = 1;
    p16_rect(5, 7, 6, 3, 15); p16_rect(6, 9, 4, 2, 4);
    p16_rect(4, 10, 8, 5, 8); p16_rect(5, 11, 6, 4, 9);
    p16_rect(12, 3, 2, 12, 11); p16_rect(12, 2, 2, 2, 7);
    upload_obj_sprite16(SPR_NPC_ELDER);

    /* 5: SPR_NPC_CLARA */
    p16_clear();
    p16_rect(4, 1, 8, 4, 7); p16_rect(5, 4, 6, 3, 2);
    g_pix16[86] = 1; g_pix16[89] = 1;
    p16_rect(4, 8, 8, 6, 10); p16_rect(6, 9, 4, 5, 15);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_CLARA);

    /* 6: SPR_NPC_GUARD */
    p16_clear();
    p16_rect(4, 1, 8, 4, 5); p16_rect(5, 2, 6, 2, 4);
    p16_rect(5, 5, 6, 3, 2); g_pix16[102] = 1; g_pix16[105] = 1;
    p16_rect(4, 8, 8, 6, 8); p16_rect(5, 9, 6, 3, 4);
    p16_rect(12, 2, 2, 13, 5);
    upload_obj_sprite16(SPR_NPC_GUARD);

    /* 7: SPR_ENEMY_GLOOMCAP */
    p16_clear();
    p16_rect(2, 3, 12, 5, 3); p16_rect(4, 2, 8, 4, 4);
    p16_rect(4, 4, 2, 2, 12); p16_rect(10, 4, 2, 2, 12);
    p16_rect(5, 8, 6, 6, 5); g_pix16[166] = 10; g_pix16[169] = 10;
    upload_obj_sprite16(SPR_ENEMY_GLOOMCAP);

    /* 8: SPR_ENEMY_THORNLING */
    p16_clear();
    p16_rect(3, 3, 10, 8, 6); p16_rect(5, 4, 6, 6, 7);
    p16_rect(2, 2, 2, 3, 8); p16_rect(12, 2, 2, 3, 8);
    g_pix16[102] = 11; g_pix16[105] = 11;
    p16_rect(5, 11, 2, 4, 6); p16_rect(9, 11, 2, 4, 6);
    upload_obj_sprite16(SPR_ENEMY_THORNLING);

    /* 9: SPR_ENEMY_DUSK_MOTH */
    p16_clear();
    p16_rect(1, 3, 5, 8, 3); p16_rect(10, 3, 5, 8, 3);
    p16_rect(2, 4, 3, 5, 15); p16_rect(11, 4, 3, 5, 15);
    p16_rect(6, 4, 4, 9, 2); g_pix16[87] = 11; g_pix16[88] = 11;
    upload_obj_sprite16(SPR_ENEMY_DUSK_MOTH);
}

static void bake_obj_sprites_part2(void) {
    /* 10: SPR_BOSS_BRIAR */
    p16_clear();
    p16_rect(1, 1, 14, 14, 2); p16_rect(2, 2, 12, 11, 6);
    p16_rect(3, 1, 2, 3, 10); p16_rect(11, 1, 2, 3, 10);
    p16_rect(4, 5, 3, 2, 11); p16_rect(9, 5, 3, 2, 11);
    p16_rect(4, 9, 8, 2, 10);
    upload_obj_sprite16(SPR_BOSS_BRIAR);

    /* 11: SPR_WINDMILL_BLADES */
    p16_clear();
    p16_rect(7, 0, 2, 16, 12); p16_rect(0, 7, 16, 2, 12);
    p16_rect(6, 6, 4, 4, 7);
    upload_obj_sprite16(SPR_WINDMILL_BLADES);

    /* 12: SPR_WAYSTONE */
    p16_clear();
    p16_rect(4, 2, 8, 12, 5); p16_rect(5, 3, 6, 10, 4);
    p16_rect(6, 5, 4, 5, 9); p16_rect(7, 6, 2, 3, 15);
    upload_obj_sprite16(SPR_WAYSTONE);

    /* 13: SPR_CHEST */
    p16_clear();
    p16_rect(2, 5, 12, 9, 11); p16_rect(3, 6, 10, 7, 12);
    p16_rect(2, 8, 12, 2, 7); p16_rect(7, 8, 2, 3, 15);
    upload_obj_sprite16(SPR_CHEST);

    /* 14..17: Emotes */
    p16_clear(); p16_rect(4, 2, 8, 10, 12); p16_rect(7, 3, 2, 5, 10); p16_rect(7, 9, 2, 2, 10);
    upload_obj_sprite16(SPR_EMOTE_BANG);

    p16_clear(); p16_rect(4, 4, 3, 4, 10); p16_rect(9, 4, 3, 4, 10);
    p16_rect(5, 6, 6, 4, 10); p16_rect(7, 10, 2, 2, 10);
    upload_obj_sprite16(SPR_EMOTE_HEART);

    p16_clear(); p16_rect(5, 3, 6, 2, 11); p16_rect(9, 5, 2, 3, 11);
    p16_rect(7, 7, 2, 2, 11); p16_rect(7, 10, 2, 2, 11);
    upload_obj_sprite16(SPR_EMOTE_QUESTION);

    p16_clear(); p16_rect(7, 2, 2, 12, 11); p16_rect(2, 7, 12, 2, 11); p16_rect(5, 5, 6, 6, 12);
    upload_obj_sprite16(SPR_EMOTE_SPARK);

    /* 18: SPR_HEART_ORB */
    p16_clear();
    p16_rect(4, 4, 8, 8, 11); p16_rect(5, 5, 6, 6, 12);
    p16_rect(7, 2, 2, 12, 12); p16_rect(2, 7, 12, 2, 12);
    upload_obj_sprite16(SPR_HEART_ORB);

    /* 32: Timed-Hit / Parry Target Ring */
    p16_clear();
    p16_rect(3, 1, 10, 2, 11); p16_rect(3, 13, 10, 2, 11);
    p16_rect(1, 3, 2, 10, 11); p16_rect(13, 3, 2, 10, 11);
    p16_rect(6, 6, 4, 4, 15);
    upload_obj_sprite16(SPR_TIMED_RING_SLOT);
}

static void bake_obj_sprites_part3(void) {
    /* 19: SPR_CHEST_OPEN (Flipped-back lid, dark empty interior, brass latch) */
    p16_clear();
    p16_rect(2, 2, 12, 4, 11); p16_rect(3, 3, 10, 2, 12);
    p16_rect(2, 5, 12, 1, 7);
    p16_rect(3, 6, 10, 3, 1);
    p16_rect(2, 9, 12, 5, 11); p16_rect(3, 10, 10, 3, 12);
    p16_rect(2, 9, 12, 1, 7);  p16_rect(7, 9, 2, 2, 15);
    upload_obj_sprite16(SPR_CHEST_OPEN);

    /* 20: SPR_NPC_MOUSE */
    p16_clear();
    p16_rect(2, 1, 4, 4, 4);   p16_rect(10, 1, 4, 4, 4);
    p16_rect(3, 2, 2, 2, 10);  p16_rect(11, 2, 2, 2, 10);
    p16_rect(4, 4, 8, 4, 15);  p16_rect(5, 6, 6, 2, 2);
    g_pix16[86] = 1; g_pix16[89] = 1; g_pix16[119] = 10; g_pix16[120] = 10;
    p16_rect(4, 8, 8, 6, 8);   p16_rect(5, 9, 6, 4, 7);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_MOUSE);

    /* 21: SPR_NPC_PIG */
    p16_clear();
    p16_rect(3, 1, 3, 3, 10);  p16_rect(10, 1, 3, 3, 10);
    p16_rect(4, 3, 8, 5, 10);  p16_rect(5, 5, 6, 3, 2);
    g_pix16[70] = 1; g_pix16[73] = 1; g_pix16[103] = 1; g_pix16[104] = 1;
    p16_rect(3, 8, 10, 6, 5);  p16_rect(5, 9, 6, 4, 7);
    p16_rect(4, 14, 3, 2, 11); p16_rect(9, 14, 3, 2, 11);
    upload_obj_sprite16(SPR_NPC_PIG);

    /* 22: SPR_NPC_SHADOW_GRAN */
    p16_clear();
    p16_rect(3, 1, 10, 6, 2);  p16_rect(4, 2, 8, 4, 3);
    p16_rect(5, 4, 6, 3, 1);
    g_pix16[86] = 11; g_pix16[89] = 11;
    p16_rect(3, 7, 10, 8, 2);  p16_rect(5, 8, 6, 6, 3);
    p16_rect(2, 12, 2, 3, 4);  p16_rect(12, 12, 2, 3, 4);
    upload_obj_sprite16(SPR_NPC_SHADOW_GRAN);

    /* 23: SPR_PET_MILO (Mido — Amber Mountain Fox-Kit) */
    p16_clear();
    p16_rect(4, 1, 2, 3, 4);   p16_rect(10, 1, 2, 3, 4);
    p16_rect(5, 3, 6, 5, 10);  p16_rect(6, 5, 4, 3, 14);
    g_pix16[86] = 1; g_pix16[89] = 1; g_pix16[119] = 1;
    p16_rect(4, 8, 8, 2, 9);   g_pix16[151] = 11; g_pix16[152] = 11;
    p16_rect(4, 10, 8, 4, 10); p16_rect(6, 10, 4, 3, 14);
    p16_rect(12, 7, 3, 4, 10); p16_rect(13, 6, 2, 2, 14);
    p16_rect(5, 14, 2, 2, 4);  p16_rect(9, 14, 2, 2, 4);
    upload_obj_sprite16(SPR_PET_MILO);

    /* 24: SPR_ENEMY_RAT */
    p16_clear();
    p16_rect(3, 3, 3, 3, 3);   p16_rect(10, 3, 3, 3, 3);
    p16_rect(4, 5, 8, 7, 2);   p16_rect(5, 6, 6, 5, 4);
    g_pix16[118] = 10; g_pix16[121] = 10;
    p16_rect(12, 9, 3, 2, 5);
    p16_rect(5, 12, 2, 3, 3);  p16_rect(9, 12, 2, 3, 3);
    upload_obj_sprite16(SPR_ENEMY_RAT);
}

static void bake_obj_sprites_part4(void) {
    /* 25: SPR_ENEMY_SLIME */
    p16_clear();
    p16_rect(3, 6, 10, 8, 6);  p16_rect(4, 4, 8, 8, 7);
    p16_rect(5, 5, 3, 2, 15);
    g_pix16[134] = 11; g_pix16[137] = 11;
    p16_rect(2, 12, 12, 2, 6);
    upload_obj_sprite16(SPR_ENEMY_SLIME);

    /* 26: SPR_ENEMY_BOAR */
    p16_clear();
    p16_rect(4, 1, 8, 4, 2);   p16_rect(5, 2, 6, 2, 5);
    p16_rect(4, 5, 8, 4, 4);   g_pix16[102] = 11; g_pix16[105] = 11;
    g_pix16[132] = 12; g_pix16[139] = 12;
    p16_rect(3, 9, 10, 5, 3);  p16_rect(5, 10, 6, 3, 10);
    p16_rect(13, 1, 2, 14, 12);
    upload_obj_sprite16(SPR_ENEMY_BOAR);

    /* 27: SPR_BOSS_MUCKMAW */
    p16_clear();
    p16_rect(5, 0, 6, 3, 11);  g_pix16[6] = 12; g_pix16[9] = 12;
    p16_rect(1, 3, 14, 12, 6); p16_rect(2, 4, 12, 9, 7);
    p16_rect(3, 5, 3, 2, 11);  p16_rect(10, 5, 3, 2, 11);
    p16_rect(3, 9, 10, 3, 1);  p16_rect(4, 9, 2, 2, 12); p16_rect(10, 9, 2, 2, 12);
    upload_obj_sprite16(SPR_BOSS_MUCKMAW);

    /* 28: SPR_BOSS_IRONFANG */
    p16_clear();
    p16_rect(2, 0, 2, 4, 12);  p16_rect(12, 0, 2, 4, 12);
    p16_rect(3, 2, 10, 6, 2);  p16_rect(4, 3, 8, 4, 3);
    p16_rect(4, 5, 3, 2, 10);  p16_rect(9, 5, 3, 2, 10);
    p16_rect(2, 8, 12, 7, 10); p16_rect(4, 8, 8, 6, 2);
    p16_rect(3, 7, 2, 2, 12);  p16_rect(11, 7, 2, 2, 12);
    upload_obj_sprite16(SPR_BOSS_IRONFANG);

    /* 29: SPR_HOLLOW_OAK */
    p16_clear();
    p16_rect(1, 0, 14, 15, 11); p16_rect(2, 1, 12, 13, 12);
    p16_rect(5, 4, 6, 11, 1);
    p16_rect(6, 5, 4, 8, 9);    p16_rect(7, 6, 2, 6, 15);
    upload_obj_sprite16(SPR_HOLLOW_OAK);

    /* 30: SPR_BOBBER */
    p16_clear();
    p16_rect(7, 2, 2, 3, 11);
    p16_rect(5, 5, 6, 3, 9);
    p16_rect(5, 8, 6, 3, 14);
    p16_rect(3, 11, 10, 1, 13);
    upload_obj_sprite16(SPR_BOBBER);

    /* 31: SPR_NPC_TOWNSFOLK */
    p16_clear();
    p16_rect(4, 1, 8, 3, 11);  p16_rect(5, 4, 6, 4, 2);
    g_pix16[86] = 1; g_pix16[89] = 1;
    p16_rect(4, 8, 8, 6, 9);   p16_rect(5, 9, 6, 4, 7);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_TOWNSFOLK);
}

static void bake_obj_sprites_part5(void) {
    /* 32: SPR_NPC_MARA */
    p16_clear();
    p16_rect(5, 0, 6, 2, 15);  p16_rect(4, 2, 8, 3, 4);
    p16_rect(4, 5, 8, 3, 2);   g_pix16[102] = 1; g_pix16[105] = 1;
    p16_rect(3, 8, 10, 6, 10); p16_rect(5, 9, 6, 5, 15);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_MARA);

    /* 33: SPR_NPC_BARNABY */
    p16_clear();
    p16_rect(3, 1, 10, 3, 8);  p16_rect(6, 2, 4, 1, 7);
    p16_rect(4, 4, 8, 4, 2);   g_pix16[86] = 1; g_pix16[89] = 1;
    p16_rect(5, 7, 6, 1, 15);
    p16_rect(4, 8, 8, 6, 8);   p16_rect(7, 9, 2, 4, 7);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_BARNABY);

    /* 34: SPR_NPC_MAPLE */
    p16_clear();
    p16_rect(4, 1, 8, 2, 15);  p16_rect(4, 3, 8, 2, 11);
    p16_rect(5, 5, 6, 3, 2);   g_pix16[102] = 1; g_pix16[105] = 1;
    p16_rect(4, 8, 8, 6, 12);  p16_rect(6, 9, 4, 5, 15);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_MAPLE);

    /* 35: SPR_NPC_JASPER */
    p16_clear();
    p16_rect(4, 1, 8, 3, 10);  p16_rect(4, 4, 8, 3, 2);
    g_pix16[86] = 1; g_pix16[89] = 1;
    p16_rect(4, 7, 8, 2, 15);
    p16_rect(3, 9, 10, 5, 5);  p16_rect(5, 10, 6, 2, 10);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_JASPER);

    /* 36: SPR_LOCOMOTIVE */
    p16_clear();
    p16_rect(2, 1, 3, 4, 5);   p16_rect(1, 1, 5, 1, 7);
    p16_rect(1, 5, 9, 7, 5);   p16_rect(5, 3, 3, 2, 7);
    p16_rect(10, 2, 5, 10, 10); p16_rect(11, 4, 3, 3, 15);
    p16_rect(0, 9, 2, 4, 4);
    p16_rect(2, 12, 3, 3, 10); p16_rect(6, 12, 3, 3, 10); p16_rect(11, 12, 3, 3, 10);
    p16_rect(2, 13, 12, 1, 7);
    upload_obj_sprite16(SPR_LOCOMOTIVE);
}

static void bake_obj_sprites_part6(void) {
    /* 37: SPR_NPC_TESSA (Sunhill Baker Tessa) */
    p16_clear();
    p16_rect(3, 1, 10, 2, 10); p16_rect(4, 3, 8, 2, 11);
    p16_rect(5, 5, 6, 3, 2);   g_pix16[102] = 1; g_pix16[105] = 1;
    p16_rect(4, 8, 8, 6, 5);   p16_rect(6, 9, 4, 5, 15);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_TESSA);

    /* 38: SPR_NPC_CORIN (Sunhill Archivist Corin) */
    p16_clear();
    p16_rect(4, 1, 8, 3, 15);  p16_rect(4, 4, 8, 4, 2);
    g_pix16[85] = 9; g_pix16[86] = 9; g_pix16[89] = 9; g_pix16[90] = 9;
    p16_rect(3, 8, 10, 6, 8);  p16_rect(7, 9, 2, 4, 7);
    p16_rect(12, 3, 2, 12, 11);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_obj_sprite16(SPR_NPC_CORIN);

    /* 39: SPR_NPC_SENTINEL (Castle Aurelia Gate Sentinel) */
    p16_clear();
    p16_rect(7, 0, 2, 2, 7);   p16_rect(4, 2, 8, 3, 15);
    p16_rect(5, 5, 6, 3, 2);   g_pix16[102] = 1; g_pix16[105] = 1;
    p16_rect(3, 8, 10, 6, 8);  p16_rect(6, 9, 4, 4, 7);
    p16_rect(13, 1, 2, 14, 15);
    p16_rect(5, 14, 2, 2, 4);  p16_rect(9, 14, 2, 2, 4);
    upload_obj_sprite16(SPR_NPC_SENTINEL);
}

static void bake_obj_sprites_part7(void) {
    /* 40: SPR_RAFT (Cedar Log Raft with Cross-Lashing - PAL1) */
    p16_clear();
    p16_rect(1, 3, 14, 10, 4); p16_rect(2, 4, 12, 8, 5);
    p16_rect(1, 6, 14, 1, 10); p16_rect(1, 10, 14, 1, 10);
    p16_rect(4, 3, 1, 10, 1);  p16_rect(8, 3, 1, 10, 1); p16_rect(11, 3, 1, 10, 1);
    upload_obj_sprite16(SPR_RAFT);

    /* 41: SPR_ENEMY_SCORPION (Dune Scorpion - PAL3) */
    p16_clear();
    p16_rect(3, 7, 10, 6, 10); p16_rect(4, 8, 8, 4, 11);
    p16_rect(1, 5, 3, 4, 10);  p16_rect(12, 5, 3, 4, 10);
    p16_rect(6, 2, 4, 5, 10);  p16_rect(8, 1, 3, 2, 12);
    g_pix16[134] = 1; g_pix16[137] = 1;
    upload_obj_sprite16(SPR_ENEMY_SCORPION);

    /* 42: SPR_ENEMY_SANDWORM (Giant Sandworm - PAL3) */
    p16_clear();
    p16_rect(2, 2, 12, 13, 10); p16_rect(3, 3, 10, 10, 11);
    p16_rect(4, 4, 8, 5, 1);    p16_rect(5, 5, 2, 2, 12); p16_rect(9, 5, 2, 2, 12);
    p16_rect(2, 10, 12, 2, 6);  p16_rect(3, 13, 10, 2, 6);
    upload_obj_sprite16(SPR_ENEMY_SANDWORM);

    /* 43: SPR_FAUNA_CAMEL (Desert Camel - PAL2) */
    p16_clear();
    p16_rect(4, 4, 8, 6, 11);  p16_rect(6, 2, 4, 3, 12);
    p16_rect(11, 3, 4, 4, 11); g_pix16[62] = 1;
    p16_rect(4, 10, 2, 5, 11); p16_rect(10, 10, 2, 5, 11);
    upload_obj_sprite16(SPR_FAUNA_CAMEL);

    /* 44: SPR_FAUNA_PENGUIN (Frostmere Penguin - PAL2) */
    p16_clear();
    p16_rect(4, 2, 8, 12, 1);  p16_rect(5, 6, 6, 7, 15);
    p16_rect(6, 4, 4, 2, 7);   g_pix16[53] = 15; g_pix16[58] = 15;
    p16_rect(4, 14, 3, 2, 7);  p16_rect(9, 14, 3, 2, 7);
    upload_obj_sprite16(SPR_FAUNA_PENGUIN);

    /* 45: SPR_FAUNA_MONKEY (Canopy Monkey - PAL2) */
    p16_clear();
    p16_rect(2, 3, 3, 3, 11);  p16_rect(11, 3, 3, 3, 11);
    p16_rect(4, 2, 8, 6, 11);  p16_rect(5, 4, 6, 4, 2);
    g_pix16[70] = 1; g_pix16[73] = 1;
    p16_rect(4, 8, 8, 6, 11);  p16_rect(12, 7, 3, 4, 11);
    upload_obj_sprite16(SPR_FAUNA_MONKEY);

    /* 46: SPR_FAUNA_ROBOT (Aetheris Automaton - PAL2) */
    p16_clear();
    p16_rect(7, 0, 2, 2, 7);   p16_rect(4, 2, 8, 5, 5);
    p16_rect(5, 4, 6, 2, 9);   g_pix16[70] = 15; g_pix16[73] = 15;
    p16_rect(3, 7, 10, 6, 4);  p16_rect(5, 8, 6, 4, 9);
    p16_rect(5, 13, 2, 3, 5);  p16_rect(9, 13, 2, 3, 5);
    upload_obj_sprite16(SPR_FAUNA_ROBOT);

    /* 47: SPR_NPC_SHIPWRIGHT (Shipwright Orin - PAL2) */
    p16_clear();
    p16_rect(3, 1, 10, 3, 8);  p16_rect(4, 4, 8, 4, 2);
    g_pix16[86] = 1; g_pix16[89] = 1;
    p16_rect(4, 7, 8, 2, 11);
    p16_rect(3, 9, 10, 5, 9);  p16_rect(5, 9, 6, 5, 11);
    p16_rect(5, 14, 2, 2, 4);  p16_rect(9, 14, 2, 2, 4);
    upload_obj_sprite16(SPR_NPC_SHIPWRIGHT);

    /* 48: SPR_BABANA_STALL (Golden Babana Market Stall - PAL2) */
    p16_clear();
    p16_rect(1, 1, 14, 4, 7);  p16_rect(2, 2, 12, 2, 10);
    p16_rect(2, 5, 2, 10, 11); p16_rect(12, 5, 2, 10, 11);
    p16_rect(1, 9, 14, 5, 11); p16_rect(4, 7, 8, 3, 7);
    upload_obj_sprite16(SPR_BABANA_STALL);
}

static void bake_obj_sprites_hero_ext(void) {
    static const u8 TALL_NPCS[13] = {
        SPR_NPC_MARA, SPR_NPC_ELDER, SPR_NPC_CLARA, SPR_NPC_GUARD,
        SPR_NPC_BARNABY, SPR_NPC_MAPLE, SPR_NPC_JASPER, SPR_NPC_TESSA,
        SPR_NPC_CORIN, SPR_NPC_SENTINEL, SPR_NPC_TOWNSFOLK,
        SPR_NPC_SHADOW_GRAN, SPR_NPC_SHIPWRIGHT
    };
    u8 idx, sid;

    /* 55: SPR_BATTLE_VFX_SLOT (Solar / Starlight / Slash Choreography Burst) */
    p16_clear();
    p16_rect(7, 0, 2, 16, 11); p16_rect(0, 7, 16, 2, 11);
    p16_rect(3, 3, 10, 10, 15);
    p16_rect(5, 5, 6, 6, 12);
    p16_rect(2, 2, 2, 2, 10); p16_rect(12, 2, 2, 2, 10);
    p16_rect(2, 12, 2, 2, 10); p16_rect(12, 12, 2, 2, 10);
    upload_obj_sprite16(SPR_BATTLE_VFX_SLOT);

    /* 56: SPR_ATLAS_HERO_SLOT (Rowan Live Position Beacon: crisp white/gold frame + crimson core) */
    p16_clear();
    p16_rect(4, 4, 8, 8, 1);
    p16_rect(5, 5, 6, 6, 12);
    p16_rect(6, 6, 4, 4, 10);
    g_pix16[102] = 12;
    upload_obj_sprite16(SPR_ATLAS_HERO_SLOT);

    /* 57: SPR_ATLAS_RETICLE_SLOT (Designer Continent Targeting Corner Brackets) */
    p16_clear();
    p16_rect(1, 1, 5, 2, 1);  p16_rect(1, 1, 2, 5, 1);
    p16_rect(10, 1, 5, 2, 1); p16_rect(13, 1, 2, 5, 1);
    p16_rect(1, 13, 5, 2, 1); p16_rect(1, 10, 2, 5, 1);
    p16_rect(10, 13, 5, 2, 1); p16_rect(13, 10, 2, 5, 1);
    p16_rect(2, 2, 3, 1, 11); p16_rect(2, 2, 1, 3, 11);
    p16_rect(11, 2, 3, 1, 11); p16_rect(13, 2, 1, 3, 11);
    p16_rect(2, 13, 3, 1, 11); p16_rect(2, 11, 1, 3, 11);
    p16_rect(11, 13, 3, 1, 11); p16_rect(13, 11, 1, 3, 11);
    upload_obj_sprite16(SPR_ATLAS_RETICLE_SLOT);

    /* 58: SPR_FISH_ICON_SLOT (16x16 Pixel-Art Sun Carp / Meadow Trout facing RIGHT - PAL1) */
    p16_clear();
    p16_rect(0, 4, 3, 2, 1);  p16_rect(0, 10, 3, 2, 1); p16_rect(1, 5, 3, 6, 8);
    p16_rect(1, 5, 2, 2, 10); p16_rect(1, 9, 2, 2, 10);
    p16_rect(6, 2, 5, 2, 1);  p16_rect(7, 3, 3, 1, 10);
    p16_rect(7, 12, 3, 2, 1); g_pix16[200] = 10;
    p16_rect(4, 4, 10, 8, 1);
    p16_rect(4, 5, 10, 6, 9);
    p16_rect(5, 4, 8, 2, 8);
    p16_rect(5, 6, 7, 2, 10);
    p16_rect(6, 9, 6, 2, 11);
    p16_rect(14, 6, 2, 4, 1); p16_rect(13, 7, 2, 2, 9);
    g_pix16[107] = 14; g_pix16[108] = 1;
    g_pix16[118] = 11; g_pix16[120] = 11;
    upload_obj_sprite16(SPR_FISH_ICON_SLOT);

    /* 59 & 60: Petite ~11x13 Wingless Garden-Gnome Pip Hop & Landing Squash (PAL1) */
    hearth_bake_pip_16x16(g_pix16, 1u);
    upload_obj_sprite16(SPR_PIP_HOP_SLOT);
    hearth_bake_pip_16x16(g_pix16, 2u);
    upload_obj_sprite16(SPR_PIP_SQUASH_SLOT);

    /* Tall 16x24 Rowan initial pose streamed straight from pre-baked ROM (0 CPU baking!) */
    g_rowan_pose_idx = 0u;
    g_last_rowan_key = 0u;
    flush_rowan_vblank_dma();

    /* Tall 16x24 Adult NPCs: Grandma Mara + 12 key cast (upper 16x16 in own slot, lower 16x8 in boot strip) */
    for (idx = 0u; idx < 13u; idx++) {
        sid = TALL_NPCS[idx];
        hearth_bake_npc_16x24(g_pix24, sid, 0u);
        upload_obj_sprite24_fb(sid, SPR_BOOT_TILE_LUT[sid]);
    }
}

/* Helper: sample sweeping continuous 256px-wide alpine mountain skyline height & slope */
static u8 vista_mtn_y(u16 px, u8 *out_lit) {
    /* Three grand asymmetric snow-capped alpine peaks + shoulders looping seamlessly across 256px */
    static const u16 KNOT_X[12] = {
        0u, 22u, 48u, 76u, 102u, 130u, 158u, 180u, 206u, 228u, 244u, 256u
    };
    static const u8 KNOT_Y[12] = {
        38u, 26u, 14u, 32u, 22u, 8u, 30u, 18u, 36u, 20u, 32u, 38u
    };
    u8 k, y0, y1;
    u16 x0, x1, dx, x8;
    s16 dy, num;

    x8 = (u16)(px & 255u);
    for (k = 0u; k < 11u; k++) {
        if (x8 < KNOT_X[(u8)(k + 1u)]) break;
    }
    x0 = KNOT_X[k];
    x1 = KNOT_X[(u8)(k + 1u)];
    y0 = KNOT_Y[k];
    y1 = KNOT_Y[(u8)(k + 1u)];
    dx = (u16)(x1 - x0);
    if (dx == 0u) dx = 1u;
    dy = (s16)y1 - (s16)y0;
    /* Left-facing slopes catch warm morning/afternoon sun (out_lit = 1) */
    *out_lit = (dy < 0) ? 1u : 0u;
    num = (s16)((s16)(x8 - x0) * dy);
    return (u8)((s16)y0 + (num / (s16)dx));
}

/* Helper: sample rolling distant spruce treeline ridge across 256px */
static u8 vista_pine_y(u16 px) {
    u8 base = (u8)(50u + (((px >> 3) + (px >> 4)) & 3u));
    u8 tree = (u8)(px & 7u);
    u8 spire = (tree == 3u || tree == 4u) ? 5u
             : ((tree == 2u || tree == 5u) ? 3u
             : ((tree == 1u || tree == 6u) ? 1u : 0u));
    u8 vary = (u8)(((px * 5u) >> 3) & 3u);
    return (u8)(base - spire - vary);
}

/* Bake full-resolution 256x72px (32x9 tiles) 2bpp Mode-1 BG2 Hardware Parallax Alpine Vista!
 * Stored at 4KB-aligned VRAM_BG2_GFX (0x1000..0x1FFF, capacity 512 2bpp tiles).
 * Uses dedicated time-of-day palettes in CGRAM 16..31 (2bpp PAL4..PAL7) -> ZERO crimson red! */
static void bake_bg2_parallax_vista(void) {
    u16 tx, ty, lx, ly, px, py, next_t, off, half, col, entry, i;
    u8 my, pine_y, lit, c, snow_d, use_pal, has_mtn, has_pine, has_cloud;
    u8 *mtn_y_lut  = g_world_meta;
    u8 *mtn_lit_lut = g_world_meta + 256u;
    u8 *pine_y_lut = g_world_meta + 512u;

    /* Precompute 256-column mountain/pine skyline ONCE (256 divisions instead of 36,864 -> 140x faster boot!) */
    for (px = 0u; px < 256u; px++) {
        mtn_y_lut[px]  = vista_mtn_y(px, &lit);
        mtn_lit_lut[px] = lit;
        pine_y_lut[px] = vista_pine_y(px);
    }

    /* Shared solid/sky tiles 0..3 */
    p8_fill(1u); upload_bg2_tile_2bpp(0u); /* PAL4: Zenith sky */
    p8_fill(2u); upload_bg2_tile_2bpp(1u); /* PAL4: Mid cerulean sky */
    /* Tile 2: Smooth dithered zenith -> mid sky transition */
    for (ly = 0u; ly < 8u; ly++) {
        for (lx = 0u; lx < 8u; lx++) {
            c = (ly < 3u) ? 1u : ((ly >= 5u) ? 2u : ((((lx + ly) & 1u) == 0u) ? 1u : 2u));
            g_pix8[(u16)((ly << 3) + lx)] = c;
        }
    }
    upload_bg2_tile_2bpp(2u);
    /* Tile 3: Solid deep spruce forest valley base (PAL6) */
    p8_fill(1u); p8_rect(1u, 1u, 2u, 2u, 2u); p8_rect(5u, 4u, 2u, 2u, 2u);
    upload_bg2_tile_2bpp(3u);

    next_t = 4u;
    for (i = 0u; i < 2048u; i++) {
        g_bg1_map[i] = SNES_TILE(3u, 6u, 0u);
    }

    /* Paint sweeping 32x9 (256x72px) alpine skyline and duplicate seamlessly to 64x32 tilemap */
    for (ty = 0u; ty < 9u; ty++) {
        u16 py0 = (u16)(ty << 3);
        u16 py1 = (u16)(py0 + 7u);
        for (tx = 0u; tx < 32u; tx++) {
            u16 px0 = (u16)(tx << 3);
            has_mtn = 0u;
            has_pine = 0u;
            has_cloud = 0u;
            for (lx = 0u; lx < 8u; lx++) {
                px = (u16)(px0 + lx);
                my = mtn_y_lut[px];
                pine_y = pine_y_lut[px];
                if (py1 >= (u16)pine_y) has_pine = 1u;
                if (py1 >= (u16)my && py0 < (u16)pine_y) has_mtn = 1u;
                if (ty <= 1u && ((px >= 14u && px <= 46u) || (px >= 166u && px <= 198u))) {
                    u16 cx = (px < 100u) ? (u16)(px - 14u) : (u16)(px - 166u);
                    u8 top_c = (cx >= 10u && cx <= 22u) ? 6u : ((cx >= 4u && cx <= 28u) ? 9u : 11u);
                    if (py1 >= (u16)top_c && py0 <= 13u && (u16)top_c < (u16)my) has_cloud = 1u;
                }
            }

            if (!has_mtn && !has_pine && !has_cloud) {
                if (ty == 0u)      entry = SNES_TILE(0u, 4u, 0u);
                else if (ty == 1u) entry = SNES_TILE(2u, 4u, 0u);
                else               entry = SNES_TILE(1u, 4u, 0u);
            } else if (has_pine && !has_mtn && ty >= 7u) {
                /* Pine treeline silhouette tile (PAL6: 1=deep spruce, 2=lit spruce, 3=mountain base) */
                use_pal = 6u;
                for (ly = 0u; ly < 8u; ly++) {
                    py = (u16)(py0 + ly);
                    for (lx = 0u; lx < 8u; lx++) {
                        px = (u16)(px0 + lx);
                        pine_y = pine_y_lut[px];
                        if (py < (u16)pine_y) {
                            c = 3u; /* Hazy mountain base behind pine tips */
                        } else if (py <= (u16)(pine_y + 2u) && ((px & 7u) <= 3u)) {
                            c = 2u; /* Sunlit left edge of spruce spire */
                        } else {
                            c = 1u; /* Deep alpine spruce silhouette */
                        }
                        g_pix8[(u16)((ly << 3) + lx)] = c;
                    }
                }
                upload_bg2_tile_2bpp(next_t);
                entry = SNES_TILE(next_t, use_pal, 0u);
                next_t++;
            } else if (has_cloud && !has_mtn && !has_pine) {
                /* Soft cumulus cloud tile (PAL7: 1=zenith sky, 2=cloud shadow, 3=sunlit puff) */
                use_pal = 7u;
                for (ly = 0u; ly < 8u; ly++) {
                    py = (u16)(py0 + ly);
                    for (lx = 0u; lx < 8u; lx++) {
                        px = (u16)(px0 + lx);
                        c = 1u;
                        if ((px >= 14u && px <= 46u) || (px >= 166u && px <= 198u)) {
                            u16 cx = (px < 100u) ? (u16)(px - 14u) : (u16)(px - 166u);
                            u8 top_c = (cx >= 10u && cx <= 22u) ? 6u : ((cx >= 4u && cx <= 28u) ? 9u : 11u);
                            if (py >= (u16)top_c && py <= 13u) {
                                c = (py >= 12u) ? 2u : 3u;
                            }
                        }
                        g_pix8[(u16)((ly << 3) + lx)] = c;
                    }
                }
                upload_bg2_tile_2bpp(next_t);
                entry = SNES_TILE(next_t, use_pal, 0u);
                next_t++;
            } else if (has_pine) {
                /* Transition row where spruce silhouettes rise over shaded mountain flanks */
                use_pal = 6u;
                for (ly = 0u; ly < 8u; ly++) {
                    py = (u16)(py0 + ly);
                    for (lx = 0u; lx < 8u; lx++) {
                        px = (u16)(px0 + lx);
                        my = mtn_y_lut[px];
                        pine_y = pine_y_lut[px];
                        if (py < (u16)my) {
                            c = 0u; /* Seamless backdrop mid-sky */
                        } else if (py < (u16)pine_y) {
                            c = 3u; /* Shaded granite flank */
                        } else if (py <= (u16)(pine_y + 2u) && ((px & 7u) <= 3u)) {
                            c = 2u; /* Lit pine crest */
                        } else {
                            c = 1u; /* Deep spruce */
                        }
                        g_pix8[(u16)((ly << 3) + lx)] = c;
                    }
                }
                upload_bg2_tile_2bpp(next_t);
                entry = SNES_TILE(next_t, use_pal, 0u);
                next_t++;
            } else {
                /* Snow-capped alpine granite mountain tile (PAL5: 0=sky backdrop, 1=shadow, 2=lit, 3=snow) */
                use_pal = 5u;
                for (ly = 0u; ly < 8u; ly++) {
                    py = (u16)(py0 + ly);
                    for (lx = 0u; lx < 8u; lx++) {
                        px = (u16)(px0 + lx);
                        my = mtn_y_lut[px];
                        lit = mtn_lit_lut[px];
                        if (py < (u16)my) {
                            c = 0u; /* Transparent -> CGRAM[0] mid-sky seamlessly! */
                        } else {
                            snow_d = (u8)(py - (u16)my);
                            if (lit) {
                                /* Sunlit left slope: rich snowcap with organic snow gullies */
                                u8 snow_lim = (u8)(8u + (((px * 3u) >> 2) & 3u));
                                if (snow_d < snow_lim) c = 3u;
                                else if (snow_d < (u8)(snow_lim + 14u)) c = 2u;
                                else c = ((((px * 3u + py * 5u) & 7u) == 0u) ? 2u : 1u);
                            } else {
                                /* Shadowed right slope: crisp snow rim + blue-slate shadow + subtle crag */
                                u8 rim_lim = (u8)(3u + ((px >> 1) & 1u));
                                if (snow_d < rim_lim) c = 3u;
                                else if (snow_d < 8u && (((px + py) & 3u) == 0u)) c = 2u;
                                else c = 1u;
                            }
                        }
                        g_pix8[(u16)((ly << 3) + lx)] = c;
                    }
                }
                upload_bg2_tile_2bpp(next_t);
                entry = SNES_TILE(next_t, use_pal, 0u);
                next_t++;
            }

            /* Stamp into BOTH left (0..31) and right (32..63) 32x32 halves of SC_64x32 map */
            for (half = 0u; half < 2048u; half += 1024u) {
                col = tx;
                off = (u16)(half + (ty << 5) + col);
                g_bg1_map[off] = entry;
            }
        }
    }
    dmaCopyVram((u8 *)g_bg1_map, VRAM_BG2_MAP, 4096u);
}

/* BG1 animated terrain frames (32 tiles x 4 frames) are pre-baked in ROM (snes_chrono_anim_frames)! */

/* Pre-bake 4 rotational frames of the 16x16 spinning windmill sails */
static void bake_env_windmill_frames(void) {
    u8 f, i, d1;

    for (f = 0; f < 4u; f++) {
        p16_clear();
        if (f == 0u) {
            p16_rect(7, 0, 2, 16, 11); p16_rect(0, 7, 16, 2, 11);
            p16_rect(9, 1, 2, 5, 15);  p16_rect(5, 10, 2, 5, 15);
            p16_rect(10, 9, 5, 2, 15); p16_rect(1, 5, 5, 2, 15);
        } else if (f == 1u) {
            for (i = 0; i < 8u; i++) {
                d1 = (u8)(i >> 1);
                g_pix16[(u16)((7u - i) * 16u + (7u + d1))] = 11;
                g_pix16[(u16)((7u - i) * 16u + (8u + d1))] = 15;
                g_pix16[(u16)((8u + i) * 16u + (8u - d1))] = 11;
                g_pix16[(u16)((8u + i) * 16u + (7u - d1))] = 15;
                g_pix16[(u16)((7u + d1) * 16u + (8u + i))] = 11;
                g_pix16[(u16)((8u + d1) * 16u + (8u + i))] = 15;
                g_pix16[(u16)((8u - d1) * 16u + (7u - i))] = 11;
                g_pix16[(u16)((7u - d1) * 16u + (7u - i))] = 15;
            }
        } else if (f == 2u) {
            for (i = 1; i < 15u; i++) {
                g_pix16[(u16)(i * 16u + i)] = 11;
                if ((u8)(i + 1u) < 16u) g_pix16[(u16)(i * 16u + i + 1u)] = 15;
                g_pix16[(u16)(i * 16u + (15u - i))] = 11;
                g_pix16[(u16)((i - 1u) * 16u + (15u - i))] = 15;
            }
        } else {
            for (i = 0; i < 8u; i++) {
                d1 = (u8)(i >> 1);
                g_pix16[(u16)((7u - d1) * 16u + (8u + i))] = 11;
                g_pix16[(u16)((8u - d1) * 16u + (8u + i))] = 15;
                g_pix16[(u16)((8u + d1) * 16u + (7u - i))] = 11;
                g_pix16[(u16)((7u + d1) * 16u + (7u - i))] = 15;
                g_pix16[(u16)((7u - i) * 16u + (7u - d1))] = 11;
                g_pix16[(u16)((7u - i) * 16u + (8u - d1))] = 15;
                g_pix16[(u16)((8u + i) * 16u + (8u + d1))] = 11;
                g_pix16[(u16)((8u + i) * 16u + (7u + d1))] = 15;
            }
        }
        p16_rect(6, 6, 4, 4, 7);

        copy_sub16_to_pix8(0, 0); encode_pix8_to_buf(g_anim_windmill[f][0]);
        copy_sub16_to_pix8(8, 0); encode_pix8_to_buf(g_anim_windmill[f][1]);
        copy_sub16_to_pix8(0, 8); encode_pix8_to_buf(g_anim_windmill[f][2]);
        copy_sub16_to_pix8(8, 8); encode_pix8_to_buf(g_anim_windmill[f][3]);
    }
    /* Upload frame 0 immediately during boot Force Blank */
    {
        u16 t = sprite_slot_to_tile(SPR_WINDMILL_BLADES);
        dmaCopyVram(g_anim_windmill[0][0], (u16)(VRAM_OBJ_GFX + (t << 4)), 64u);
        dmaCopyVram(g_anim_windmill[0][2], (u16)(VRAM_OBJ_GFX + ((t + 16u) << 4)), 64u);
    }
}

/* ============================================================================
 * 6. BG1 SC_64x32 WORLD MAP & BATTLE ARENA STREAMING (148 Chrono Metatiles)
 * ============================================================================ */
static u16 snes_map_row_off(u16 my) {
    return (u16)((my << 5) + (my << 3) + (my << 2)); /* my * 44 */
}

static u8 snes_world_spatial_meta(u8 t, u16 wx, u16 wy) {
    u8 v;
    if (t >= CHRONO_META_COUNT) return (u8)T_GRASS;
    v = (u8)(((wx * 3u + wy * 5u) ^ (wx >> 1)) & 3u);
    if (t == (u8)T_GRASS && v > 0u) return (u8)(CM_GRASS_V1 + (v - 1u));
    if (t == (u8)T_WORLD_SAND && (v & 1u)) return (u8)CM_SAND_V1;
    if (t == (u8)T_WORLD_SNOW && (v & 1u)) return (u8)CM_SNOW_V1;
    if (t == (u8)T_WORLD_JUNGLE && (v & 1u)) return (u8)CM_JUNGLE_V1;
    if (t == (u8)T_WORLD_OCEAN && (v & 1u)) return (u8)CM_OCEAN_V1;
    return t;
}

static void bake_world_tile_cache(void) {
    u16 mx, my, mw, mh, row_off, idx;
    u8 ov, t, tn, ts, tw, te, has_vista;
    const u8 *p1;
    const u8 *p2;

    if (g_hearth.is_world_map) return;
    mw = (u16)(g_hearth.map_w ? g_hearth.map_w : HEARTH_MAP_W);
    mh = (u16)(g_hearth.map_h ? g_hearth.map_h : HEARTH_MAP_H);
    has_vista = g_hearth.has_mountain_vista ? 1u : 0u;
    p1 = g_hearth.map_bg1;
    p2 = g_hearth.map_bg2;

    for (my = 0u; my < mh; my++) {
        row_off = (u16)(my * mw);
        for (mx = 0u; mx < mw; mx++) {
            idx = (u16)(row_off + mx);
            t  = p1[idx];
            ov = p2[idx];
            if (ov != 0u && ov <= (u8)O_OAK_CROWN) {
                g_world_meta[idx] = (u8)(CM_OPQ_CANOPY_TL + (ov - 1u));
            } else if (has_vista && t == (u8)T_MTN_SKY) {
                g_world_meta[idx] = (u8)CM_EMPTY;
            } else {
                tn = (my > 0u) ? p1[(u16)(idx - mw)] : t;
                ts = ((u16)(my + 1u) < mh) ? p1[(u16)(idx + mw)] : t;
                tw = (mx > 0u) ? p1[(u16)(idx - 1u)] : t;
                te = ((u16)(mx + 1u) < mw) ? p1[(u16)(idx + 1u)] : t;
                g_world_meta[idx] = hearth_resolve_chrono_meta(t, ov, tn, ts, tw, te, mx, my, 0u);
            }
        }
    }
}

static void stream_world_col(s16 mx, s16 my_start) {
    u16 half_col, row_off, k, idx, idx_valid, step_w, wx, wy;
    s16 my, mw, mh, wy_s;
    u8 m;
    u16 *dst;
    const u8 *sp;

    half_col = (u16)(((u16)mx & 15u) << 1);
    if (((u16)mx & 16u) == 0u) {
        g_bg1_dirty_l = 1;
    } else {
        half_col = (u16)(1024u + half_col);
        g_bg1_dirty_r = 1;
    }
    row_off = (u16)(((u16)my_start & 15u) << 6);

    if (g_hearth.is_world_map) {
        wx = (u16)((u16)mx & 255u);
        wy_s = my_start;
        while (wy_s < 0) wy_s = (s16)(wy_s + HEARTH_GLOBE_H);
        while (wy_s >= HEARTH_GLOBE_H) wy_s = (s16)(wy_s - HEARTH_GLOBE_H);
        wy = (u16)wy_s;
        hearth_globe_stream_col(wx, wy, 16u, g_stream_meta);
        sp = g_stream_meta;
        for (k = 0u; k < 16u; k++) {
            m = snes_world_spatial_meta(*sp++, wx, wy);
            dst = g_bg1_map + (u16)(half_col + row_off);
            dst[0]  = SNES_META_TL[m];
            dst[1]  = SNES_META_TR[m];
            dst[32] = SNES_META_BL[m];
            dst[33] = SNES_META_BR[m];
            row_off = (u16)((row_off + 64u) & 1023u);
            wy++;
            if (wy >= (u16)HEARTH_GLOBE_H) wy = 0u;
        }
        return;
    }

    mw = (s16)(g_hearth.map_w ? g_hearth.map_w : HEARTH_MAP_W);
    mh = (s16)(g_hearth.map_h ? g_hearth.map_h : HEARTH_MAP_H);
    step_w = (u16)mw;

    if (mx < 0 || mx >= mw) {
        m = (u8)T_BUSH;
        for (k = 0u; k < 16u; k++) {
            dst = g_bg1_map + (u16)(half_col + row_off);
            dst[0]  = SNES_META_TL[m];
            dst[1]  = SNES_META_TR[m];
            dst[32] = SNES_META_BL[m];
            dst[33] = SNES_META_BR[m];
            row_off = (u16)((row_off + 64u) & 1023u);
        }
        return;
    }

    my = my_start;
    idx = 0u;
    idx_valid = 0u;
    for (k = 0u; k < 16u; k++, my++) {
        if (my < 0 || my >= mh) {
            m = (g_hearth.has_mountain_vista && my < 0) ? (u8)CM_EMPTY : (u8)T_BUSH;
        } else {
            if (!idx_valid) {
                idx = (u16)(snes_map_row_off((u16)my) + (u16)mx);
                idx_valid = 1u;
            }
            m = g_world_meta[idx];
            idx = (u16)(idx + step_w);
        }
        dst = g_bg1_map + (u16)(half_col + row_off);
        dst[0]  = SNES_META_TL[m];
        dst[1]  = SNES_META_TR[m];
        dst[32] = SNES_META_BL[m];
        dst[33] = SNES_META_BR[m];
        row_off = (u16)((row_off + 64u) & 1023u);
    }
}

static void stream_world_row(s16 mx_start, s16 my) {
    u16 row_off, off, k, idx, wx, wy;
    s16 mx, mw, mh, wy_s;
    u8 m;
    u16 *dst;
    const u8 *sp;

    row_off = (u16)(((u16)my & 15u) << 6);
    if (!g_bg1_dirty_l && !g_bg1_dirty_r) {
        if (!g_bg1_row_dirty || g_bg1_dirty_row_off == row_off) {
            /* Surgical 256-byte row-strip VBlank DMA (16x smaller than 4096B full-map DMA!) */
            g_bg1_row_dirty = 1u;
            g_bg1_dirty_row_off = row_off;
        } else {
            g_bg1_row_dirty = 0u;
            g_bg1_dirty_l = 1;
            g_bg1_dirty_r = 1;
        }
    } else {
        g_bg1_row_dirty = 0u;
        g_bg1_dirty_l = 1;
        g_bg1_dirty_r = 1;
    }

    mx = mx_start;
    off = (u16)(row_off + (((u16)mx & 15u) << 1));
    if (((u16)mx & 16u) != 0u) off = (u16)(off + 1024u);

    if (g_hearth.is_world_map) {
        wx = (u16)((u16)mx_start & 255u);
        wy_s = my;
        while (wy_s < 0) wy_s = (s16)(wy_s + HEARTH_GLOBE_H);
        while (wy_s >= HEARTH_GLOBE_H) wy_s = (s16)(wy_s - HEARTH_GLOBE_H);
        wy = (u16)wy_s;
        hearth_globe_stream_row(wx, wy, 18u, g_stream_meta);
        sp = g_stream_meta;
        for (k = 0u; k < 18u; k++) {
            m = snes_world_spatial_meta(*sp++, (u16)((wx + k) & 255u), wy);
            dst = g_bg1_map + off;
            dst[0]  = SNES_META_TL[m];
            dst[1]  = SNES_META_TR[m];
            dst[32] = SNES_META_BL[m];
            dst[33] = SNES_META_BR[m];
            off = (u16)(off + 2u);
            if ((off & 31u) == 0u) off = (u16)((off - 32u) ^ 1024u);
        }
        return;
    }

    mw = (s16)(g_hearth.map_w ? g_hearth.map_w : HEARTH_MAP_W);
    mh = (s16)(g_hearth.map_h ? g_hearth.map_h : HEARTH_MAP_H);

    if (my < 0 || my >= mh) {
        m = (g_hearth.has_mountain_vista && my < 0) ? (u8)CM_EMPTY : (u8)T_BUSH;
        for (k = 0u; k < 18u; k++) {
            dst = g_bg1_map + off;
            dst[0]  = SNES_META_TL[m];
            dst[1]  = SNES_META_TR[m];
            dst[32] = SNES_META_BL[m];
            dst[33] = SNES_META_BR[m];
            off = (u16)(off + 2u);
            if ((off & 31u) == 0u) off = (u16)((off - 32u) ^ 1024u);
        }
        return;
    }

    idx = snes_map_row_off((u16)my);
    if (mx >= 0 && (s16)(mx + 18) <= mw) {
        sp = g_world_meta + (u16)(idx + (u16)mx);
        for (k = 0u; k < 18u; k++) {
            m = *sp++;
            dst = g_bg1_map + off;
            dst[0]  = SNES_META_TL[m];
            dst[1]  = SNES_META_TR[m];
            dst[32] = SNES_META_BL[m];
            dst[33] = SNES_META_BR[m];
            off = (u16)(off + 2u);
            if ((off & 31u) == 0u) off = (u16)((off - 32u) ^ 1024u);
        }
        return;
    }

    for (k = 0u; k < 18u; k++, mx++) {
        m = (mx < 0 || mx >= mw) ? (u8)T_BUSH : g_world_meta[(u16)(idx + (u16)mx)];
        dst = g_bg1_map + off;
        dst[0]  = SNES_META_TL[m];
        dst[1]  = SNES_META_TR[m];
        dst[32] = SNES_META_BL[m];
        dst[33] = SNES_META_BR[m];
        off = (u16)(off + 2u);
        if ((off & 31u) == 0u) off = (u16)((off - 32u) ^ 1024u);
    }
}

static void build_full_world_bg1(s16 cam_x, s16 cam_y) {
    s16 mx0, my0, dy;

    mx0 = (s16)((cam_x >> 4) - 1);
    my0 = (s16)((cam_y >> 4) - 1);
    for (dy = 0; dy < 16; dy++) {
        stream_world_row(mx0, (s16)(my0 + dy));
    }
    g_bg1_dirty_l = 1;
    g_bg1_dirty_r = 1;
    g_bg1_row_dirty = 0u;
    g_last_cam_mx = (s16)(cam_x >> 4);
    g_last_cam_my = (s16)(cam_y >> 4);
}

static void stream_world_bg1(s16 cam_x, s16 cam_y) {
    s16 cmx, cmy, dx, dy;

    cmx = (s16)(cam_x >> 4);
    cmy = (s16)(cam_y >> 4);
    if (cmx == g_last_cam_mx && cmy == g_last_cam_my) return;

    dx = (s16)(cmx - g_last_cam_mx);
    dy = (s16)(cmy - g_last_cam_my);
    if (g_hearth.is_world_map) {
        if (dx > 128) dx = (s16)(dx - HEARTH_GLOBE_W);
        else if (dx < -128) dx = (s16)(dx + HEARTH_GLOBE_W);
        if (dy > 96) dy = (s16)(dy - HEARTH_GLOBE_H);
        else if (dy < -96) dy = (s16)(dy + HEARTH_GLOBE_H);
    }

    if (dx > 2 || dx < -2 || dy > 2 || dy < -2) {
        build_full_world_bg1(cam_x, cam_y);
        return;
    }

    /* Stagger diagonal tile-boundary crossings across 2 frames (at most 1 axis strip per frame).
     * The 16-pixel offscreen tile margin absorbs 1 frame cleanly while halving peak diagonal CPU! */
    if (dx > 0) {
        g_last_cam_mx++;
        if (g_hearth.is_world_map && g_last_cam_mx >= HEARTH_GLOBE_W) g_last_cam_mx = 0;
        stream_world_col((s16)(g_last_cam_mx + 16), (s16)(g_last_cam_my - 1));
        return;
    }
    if (dx < 0) {
        g_last_cam_mx--;
        if (g_hearth.is_world_map && g_last_cam_mx < 0) g_last_cam_mx = (s16)(HEARTH_GLOBE_W - 1);
        stream_world_col((s16)(g_last_cam_mx - 1), (s16)(g_last_cam_my - 1));
        return;
    }
    if (dy > 0) {
        g_last_cam_my++;
        if (g_hearth.is_world_map && g_last_cam_my >= HEARTH_GLOBE_H) g_last_cam_my = 0;
        stream_world_row((s16)(g_last_cam_mx - 1), (s16)(g_last_cam_my + 14));
        return;
    }
    if (dy < 0) {
        g_last_cam_my--;
        if (g_hearth.is_world_map && g_last_cam_my < 0) g_last_cam_my = (s16)(HEARTH_GLOBE_H - 1);
        stream_world_row((s16)(g_last_cam_mx - 1), (s16)(g_last_cam_my - 1));
        return;
    }
}

static void build_battle_bg1(u8 biome) {
    u16 my, mx, off;
    u8 dark_arena, m;
    u16 *dst;

    dark_arena = (biome == BIOME_SHRINE || biome == BIOME_SEWER || biome == BIOME_BOARHOLD) ? 1u : 0u;
    for (my = 0u; my < 16u; my++) {
        m = (my < 5u) ? (u8)(dark_arena ? CM_BATTLE_SKY_SHRINE : CM_BATTLE_SKY_MEADOW)
                      : (u8)(dark_arena ? CM_BATTLE_GND_SHRINE : CM_BATTLE_GND_MEADOW);
        for (mx = 0u; mx < 16u; mx++) {
            off = (u16)((my << 6) + (mx << 1));
            dst = g_bg1_map + off;
            dst[0]  = SNES_META_TL[m];
            dst[1]  = SNES_META_TR[m];
            dst[32] = SNES_META_BL[m];
            dst[33] = SNES_META_BR[m];
        }
    }
    g_bg1_dirty_l = 1;
    g_bg1_dirty_r = 0;
    g_bg1_row_dirty = 0u;
}

static u8 *snes_cg_tiles_ptr(u8 p) {
    switch (p) {
        case 1u: return (u8 *)&cg_plate1_tiles;
        case 2u: return (u8 *)&cg_plate2_tiles;
        case 3u: return (u8 *)&cg_plate3_tiles;
        case 4u: return (u8 *)&cg_plate4_tiles;
        case 5u: return (u8 *)&cg_plate5_tiles;
        case 6u: return (u8 *)&cg_plate6_tiles;
        case 7u: return (u8 *)&cg_plate7_tiles;
        case 8u: return (u8 *)&cg_plate8_tiles;
        case 9u: return (u8 *)&cg_plate9_tiles;
        default: return (u8 *)&cg_plate0_tiles;
    }
}

static const u16 *snes_cg_map_ptr(u8 p) {
    switch (p) {
        case 1u: return (const u16 *)&cg_plate1_map;
        case 2u: return (const u16 *)&cg_plate2_map;
        case 3u: return (const u16 *)&cg_plate3_map;
        case 4u: return (const u16 *)&cg_plate4_map;
        case 5u: return (const u16 *)&cg_plate5_map;
        case 6u: return (const u16 *)&cg_plate6_map;
        case 7u: return (const u16 *)&cg_plate7_map;
        case 8u: return (const u16 *)&cg_plate8_map;
        case 9u: return (const u16 *)&cg_plate9_map;
        default: return (const u16 *)&cg_plate0_map;
    }
}

static u8 *snes_cg_pal_ptr(u8 p) {
    switch (p) {
        case 1u: return (u8 *)&cg_plate1_pal;
        case 2u: return (u8 *)&cg_plate2_pal;
        case 3u: return (u8 *)&cg_plate3_pal;
        case 4u: return (u8 *)&cg_plate4_pal;
        case 5u: return (u8 *)&cg_plate5_pal;
        case 6u: return (u8 *)&cg_plate6_pal;
        case 7u: return (u8 *)&cg_plate7_pal;
        case 8u: return (u8 *)&cg_plate8_pal;
        case 9u: return (u8 *)&cg_plate9_pal;
        default: return (u8 *)&cg_plate0_pal;
    }
}

static void upload_cinema_cg_force_blank(u8 scene) {
    u8 p = (scene < CINEMA_ROM_PLATE_COUNT) ? scene : 0u;
    u16 byte_len = (u16)(CINEMA_ROM_TILE_COUNTS[p] << 5);
    dmaCopyVram(snes_cg_tiles_ptr(p),
                (u16)(VRAM_BG1_GFX + ((u16)VRAM_CG_TILE_BASE << 4)),
                byte_len);
    /* Upload pure transparent letterbox tile at VRAM_CG_EMPTY_TILE (511) */
    hearth_memset(g_tile32, 0u, 32u);
    dmaCopyVram(g_tile32, (u16)(VRAM_BG1_GFX + ((u16)VRAM_CG_EMPTY_TILE << 4)), 32u);
    /* Pier Solar-style 4-palette = 60-color CGRAM upload (BG PAL0..PAL3, leaving PAL4..PAL7 for BG0 UI!) */
    dmaCopyCGram(snes_cg_pal_ptr(p), 0u, 128u);
}

static void build_intro_bg1(u8 scene) {
    u8 p = (scene < CINEMA_ROM_PLATE_COUNT) ? scene : 0u;
    const u16 *mp = snes_cg_map_ptr(p);
    u16 empty_e = SNES_TILE(VRAM_CG_EMPTY_TILE, 0u, 0u);
    u16 i;

    for (i = 0u; i < 1024u; i++) {
        g_bg1_map[i] = empty_e;
    }
    /* Map 32x15 lossless Pier Solar multi-palette CG plate into BG1 rows 2..16 */
    for (i = 0u; i < CINEMA_ROM_MAP_LEN; i++) {
        g_bg1_map[(u16)(64u + i)] = mp[i];
    }
    g_bg1_dirty_l = 1;
    g_bg1_dirty_r = 0;
    g_bg1_row_dirty = 0u;
}

/* ============================================================================
 * 7. BG0 NON-SCROLLING HUD, TITLE SCREEN, DIALOGUE & BATTLE UI
 * ============================================================================ */
static void bg0_clear_all(void) {
    hearth_memset(g_bg0_map, 0, 2048u);
    g_bg0_dirty = 3u;
}

static void bg0_fill_rect(u16 x, u16 y, u16 w, u16 h, u16 entry) {
    u16 ry, rx;
    for (ry = y; ry < (u16)(y + h) && ry < 28u; ry++) {
        for (rx = x; rx < (u16)(x + w) && rx < 32u; rx++) {
            g_bg0_map[(u16)((ry << 5) + rx)] = entry;
        }
    }
    if (y >= 20u) {
        if (g_bg0_dirty < 2u) g_bg0_dirty = 2u;
    } else if (y == 0u && h == 1u) {
        if (g_bg0_dirty == 0u) g_bg0_dirty = 1u;
    } else {
        g_bg0_dirty = 3u;
    }
}

static void bg0_draw_box(u16 x, u16 y, u16 w, u16 h) {
    u16 fill_e = SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u);
    u16 top_e  = SNES_TILE(SUI_TOP,  PAL_UI_TEXT, 1u);
    u16 bot_e  = SNES_TILE(SUI_BOT,  PAL_UI_TEXT, 1u);
    u16 l_e    = SNES_TILE(SUI_LEFT, PAL_UI_TEXT, 1u);
    u16 r_e    = SNES_TILE(SUI_RIGHT,PAL_UI_TEXT, 1u);

    bg0_fill_rect(x, y, w, h, fill_e);
    if (w < 2u || h < 2u) return;
    bg0_fill_rect((u16)(x + 1u), y, (u16)(w - 2u), 1u, top_e);
    bg0_fill_rect((u16)(x + 1u), (u16)(y + h - 1u), (u16)(w - 2u), 1u, bot_e);
    bg0_fill_rect(x, (u16)(y + 1u), 1u, (u16)(h - 2u), l_e);
    bg0_fill_rect((u16)(x + w - 1u), (u16)(y + 1u), 1u, (u16)(h - 2u), r_e);

    g_bg0_map[(u16)((y << 5) + x)]                         = SNES_TILE(SUI_TL, PAL_UI_TEXT, 1u);
    g_bg0_map[(u16)((y << 5) + x + w - 1u)]                = SNES_TILE(SUI_TR, PAL_UI_TEXT, 1u);
    g_bg0_map[(u16)(((y + h - 1u) << 5) + x)]              = SNES_TILE(SUI_BL, PAL_UI_TEXT, 1u);
    g_bg0_map[(u16)(((y + h - 1u) << 5) + x + w - 1u)]     = SNES_TILE(SUI_BR, PAL_UI_TEXT, 1u);
    g_bg0_dirty = 3u;
}

static void bg0_text_n(u16 x, u16 y, u16 pal, const char *s, u16 max_c) {
    u16 i;
    char c;
    if (!s || y >= 28u) return;
    for (i = 0; i < max_c && s[i] != '\0' && (u16)(x + i) < 32u; i++) {
        c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c < 32 || c > 95) c = ' ';
        g_bg0_map[(u16)((y << 5) + x + i)] = SNES_TILE((u16)c, pal, 1u);
    }
    if (y >= 20u) {
        if (g_bg0_dirty < 2u) g_bg0_dirty = 2u;
    } else if (y == 0u) {
        if (g_bg0_dirty == 0u) g_bg0_dirty = 1u;
    } else {
        g_bg0_dirty = 3u;
    }
}

static void bg0_text(u16 x, u16 y, u16 pal, const char *s) {
    bg0_text_n(x, y, pal, s, 32u);
}

static void line_reset(u16 *pos) {
    *pos = 0;
    g_line[0] = '\0';
}

static void line_str(u16 *pos, const char *src) {
    if (!src) return;
    while (*src && (u16)(*pos + 1u) < 35u) {
        g_line[(*pos)++] = *src++;
    }
    g_line[*pos] = '\0';
}

static void line_int(u16 *pos, s16 val) {
    u16 v, d, started, p;

    p = *pos;
    if (val < 0) {
        if ((u16)(p + 1u) < 35u) g_line[p++] = '-';
        v = (u16)(-val);
    } else {
        v = (u16)val;
    }
    if (v == 0u) {
        if ((u16)(p + 1u) < 35u) g_line[p++] = '0';
    } else {
        started = 0u;
        if (v >= 10000u) {
            d = 0u; while (v >= 10000u) { v -= 10000u; d++; }
            if ((u16)(p + 1u) < 35u) g_line[p++] = (char)('0' + d);
            started = 1u;
        }
        if (started || v >= 1000u) {
            d = 0u; while (v >= 1000u) { v -= 1000u; d++; }
            if ((u16)(p + 1u) < 35u) g_line[p++] = (char)('0' + d);
            started = 1u;
        }
        if (started || v >= 100u) {
            d = 0u; while (v >= 100u) { v -= 100u; d++; }
            if ((u16)(p + 1u) < 35u) g_line[p++] = (char)('0' + d);
            started = 1u;
        }
        if (started || v >= 10u) {
            d = 0u; while (v >= 10u) { v -= 10u; d++; }
            if ((u16)(p + 1u) < 35u) g_line[p++] = (char)('0' + d);
        }
        if ((u16)(p + 1u) < 35u) g_line[p++] = (char)('0' + v);
    }
    g_line[p] = '\0';
    *pos = p;
}

static void draw_title_cursor(void) {
#if HEARTH_DESIGNER_MODE
    static const char *opts[3] = {
        "NEW STORY            ",
        "EXPLORE EVERTALE     ",
        "REPLAY OPENING CINEMA"
    };
    u8 cnt = 3u;
#else
    static const char *opts[2] = {
        "NEW STORY            ",
        "REPLAY OPENING CINEMA"
    };
    u8 cnt = 2u;
#endif
    u8 i;
    u16 pal, y;

    for (i = 0; i < cnt; i++) {
        pal = (i == g_hearth.title_cursor) ? PAL_UI_GOLD : PAL_UI_TEXT;
        y = (u16)(16u + ((u16)i << 1));
        bg0_text(4u, y, pal, (i == g_hearth.title_cursor) ? ">" : " ");
        bg0_text(6u, y, pal, opts[i]);
    }
    g_bg0_dirty = 3u;
}

static void draw_title_ui(void) {
    bg0_clear_all();
    bg0_draw_box(1u, 2u, 30u, 24u);
    bg0_text(9u,  4u,  PAL_UI_GOLD, "PROJECT HEARTH");
    bg0_text(5u,  6u,  PAL_UI_CYAN, "16-BIT JRPG CORE (SNES)");
    bg0_text(3u,  9u,  PAL_UI_TEXT, "DPAD : MOVE   B : DASH");
    bg0_text(3u,  11u, PAL_UI_TEXT, "A    : ACT / TIMED HIT");
    bg0_text(3u,  13u, PAL_UI_TEXT, "SEL/ST: ATLAS L/R:SWAP");
    draw_title_cursor();
    bg0_text(4u,  23u, PAL_UI_CYAN, "DPAD:PICK  START/A:GO");
    bg0_text(24u, 24u, PAL_UI_TEXT, HEARTH_VERSION_STR);
}

static u16 put_hud_u16(u16 col, u16 v) {
    u16 d;
    if (v >= 100u) {
        d = 0u;
        while (v >= 100u) { v -= 100u; d++; }
        g_bg0_map[col++] = (u16)(0x3030u + d);
        d = 0u;
        while (v >= 10u) { v -= 10u; d++; }
        g_bg0_map[col++] = (u16)(0x3030u + d);
        g_bg0_map[col++] = (u16)(0x3030u + v);
    } else if (v >= 10u) {
        d = 0u;
        while (v >= 10u) { v -= 10u; d++; }
        g_bg0_map[col++] = (u16)(0x3030u + d);
        g_bg0_map[col++] = (u16)(0x3030u + v);
    } else {
        g_bg0_map[col++] = (u16)(0x3030u + v);
    }
    return col;
}

static u16 put_hud_str(u16 col, const char *s) {
    u16 ch;
    while (*s != '\0' && col < 31u) {
        ch = (u16)(u8)(*s++);
        if (ch >= (u16)'a' && ch <= (u16)'z') ch = (u16)(ch - 32u);
        else if (ch < 32u || ch > 95u) ch = 32u;
        g_bg0_map[col++] = (u16)(ch | 0x3000u);
    }
    return col;
}

static void draw_explore_hud(const HearthPpuPacket *ppu) {
    const HearthPartyMember *m = &g_hearth.party[g_hearth.active_slot];
    u16 col = 1u;
    (void)ppu;

    g_bg0_map[col++] = (u16)('H' | 0x3000u);
    g_bg0_map[col++] = (u16)(':' | 0x3000u);
    col = put_hud_u16(col, (u16)((m->hp > 0) ? m->hp : 0));
    g_bg0_map[col++] = (u16)(' ' | 0x3000u);
    g_bg0_map[col++] = (u16)('M' | 0x3000u);
    g_bg0_map[col++] = (u16)(':' | 0x3000u);
    col = put_hud_u16(col, (u16)((m->mp > 0) ? m->mp : 0));
    g_bg0_map[col++] = (u16)(' ' | 0x3000u);
    col = put_hud_u16(col, g_hearth.guilders);
    g_bg0_map[col++] = (u16)('G' | 0x3000u);

    if (g_hearth.ppu.interact_active && g_hearth.ppu.interact_label) {
        col = put_hud_str(col, " [A]");
        col = put_hud_str(col, g_hearth.ppu.interact_label);
    } else if (g_hearth.ppu.zone_name) {
        col = put_hud_str(col, " ");
        col = put_hud_str(col, g_hearth.ppu.zone_name);
    }
    while (col < 31u) {
        g_bg0_map[col++] = 0x3020u;
    }
    if (g_bg0_dirty == 0u) g_bg0_dirty = 1u;
}

static void draw_dialogue_ui(void) {
    const HearthDialogue *d = &g_hearth.dialogue;
    u16 vis, start, take, last_sp, row, max_row;
    u8 c, show_choices;

    show_choices = (u8)((d->finished && d->next_text == NULL && d->choice_count > 0u) ? 1u : 0u);
    bg0_fill_rect(1u, 20u, 30u, 7u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
    bg0_text(2u, 20u, PAL_UI_GOLD, d->speaker ? d->speaker : "...");
    if (d->full_text) {
        vis = d->visible_chars;
        start = 0u;
        max_row = show_choices ? 23u : 24u;
        for (row = 21u; row <= max_row && start < vis; row++) {
            while (start < vis && d->full_text[start] == ' ') start++;
            if (start >= vis) break;
            take = 0u;
            last_sp = 0u;
            while ((u16)(start + take) < vis && take < 28u && d->full_text[start + take] != '\0') {
                if (d->full_text[start + take] == ' ') last_sp = take;
                take++;
            }
            if (take == 28u && (u16)(start + take) < vis &&
                d->full_text[start + take] != ' ' && d->full_text[start + take] != '\0' && last_sp > 0u) {
                take = last_sp;
            }
            bg0_text_n(2u, row, PAL_UI_TEXT, d->full_text + start, take);
            start = (u16)(start + take);
        }
    }
    if (show_choices) {
        for (c = 0; c < d->choice_count && c < 3u; c++) {
            u16 pal = (c == d->selected_choice) ? PAL_UI_GOLD : PAL_UI_TEXT;
            bg0_text(2u, (u16)(24u + c), pal, (c == d->selected_choice) ? ">" : " ");
            bg0_text_n(4u, (u16)(24u + c), pal, d->choices[c].label, 26u);
        }
    } else if (d->finished) {
        bg0_text(21u, 26u, PAL_UI_GOLD, (d->next_text != NULL) ? "[A] MORE" : "[A] NEXT");
    }
}

static void draw_intro_ui(void) {
    const HearthIntroState *in = &g_hearth.intro;
    const char *s;
    const char *hdr;
    u16 total, vis, start, end, cut, row;

    bg0_fill_rect(0u, 0u, 32u, 1u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
    bg0_text_n(1u, 0u, PAL_UI_GOLD, in->title ? in->title : "PROLOGUE", 30u);

    bg0_fill_rect(1u, 20u, 30u, 7u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
    hdr = (in->speaker && in->speaker[0] != '\0') ? in->speaker : (in->title ? in->title : "PROLOGUE");
    bg0_text_n(2u, 20u, PAL_UI_GOLD, hdr, 28u);

    /* Join line1 + space + line2 in Bank $7E scratch buffer and word-wrap across rows 21..24 */
    total = 0u;
    if (in->line1) {
        s = in->line1;
        while (*s && total < 125u) g_intro_buf[total++] = *s++;
    }
    if (in->line2 && total < 125u) {
        if (total > 0u) g_intro_buf[total++] = ' ';
        s = in->line2;
        while (*s && total < 126u) g_intro_buf[total++] = *s++;
    }
    g_intro_buf[total] = '\0';

    vis = in->chars;
    if (vis > total) vis = total;
    start = 0u;
    for (row = 21u; row <= 24u && start < vis; row++) {
        while (start < vis && g_intro_buf[start] == ' ') start++;
        if (start >= vis) break;
        end = (u16)(start + 28u);
        if (end >= vis) {
            cut = vis;
        } else {
            cut = end;
            while (cut > start && g_intro_buf[cut] != ' ') cut--;
            if (cut == start) cut = end;
        }
        bg0_text_n(2u, row, PAL_UI_TEXT, g_intro_buf + start, (u16)(cut - start));
        start = cut;
    }

    bg0_text(2u, 26u, PAL_UI_CYAN,
             in->auto_play ? "A:NEXT  Y:AUTO ON  ST:SKIP"
                           : "A:NEXT  Y:AUTO OFF ST:SKIP");
    g_bg0_dirty = 3u;
}

static void draw_battle_ui(void) {
    const HearthBattleState *b = &g_hearth.battle;
    const HearthPartyMember *pm = &g_hearth.party[b->active_slot];
    const HearthBattleEnemy *e;
    u16 pos, cx, cy, pal;
    u8 l, c;

    bg0_fill_rect(1u, 20u, 30u, 7u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
    if (g_hearth.mode == MODE_BATTLE_VICTORY) {
        bg0_text(5u, 21u, PAL_UI_GOLD, "VICTORY! HEART STEADY");
        bg0_text(4u, 24u, PAL_UI_TEXT, "PRESS A OR START");
        return;
    }
    if (g_hearth.mode == MODE_BATTLE_DEFEAT) {
        bg0_text(5u, 21u, PAL_UI_GOLD, "RETREAT TO WAYSTONE");
        bg0_text(4u, 24u, PAL_UI_TEXT, "PRESS A OR START");
        return;
    }

    /* Row 20: Active Party Member, Slot (1/3), HP & MP */
    line_reset(&pos);
    line_str(&pos, pm->name);
    line_str(&pos, " ");
    line_int(&pos, (s16)(b->active_slot + 1u));
    line_str(&pos, "/3 ");
    line_int(&pos, pm->hp);
    line_str(&pos, "/");
    line_int(&pos, pm->max_hp);
    line_str(&pos, " M");
    line_int(&pos, pm->mp);
    bg0_text_n(1u, 20u, PAL_UI_TEXT, g_line, 30u);

    /* Row 21: Selected Target Enemy, HP & Spell Locks */
    if (b->selected_enemy < b->enemy_count) {
        e = &b->enemies[b->selected_enemy];
        line_reset(&pos);
        line_str(&pos, "TGT:");
        line_str(&pos, e->name);
        line_str(&pos, " ");
        line_int(&pos, e->hp);
        bg0_text_n(1u, 21u, PAL_UI_GOLD, g_line, 24u);
        if (e->lock_count > 0u) {
            u16 lx = (u16)(2u + pos);
            if (lx > 27u) lx = 27u;
            for (l = 0; l < e->lock_count && (u16)(lx + l) < 31u; l++) {
                u16 ltile = e->broken_locks[l] ? SUI_LOCK_BROKEN
                          : ((e->locks[l] == LOCK_SWORD) ? SUI_LOCK_SWORD
                          : ((e->locks[l] == LOCK_SUN) ? SUI_LOCK_SUN : SUI_LOCK_STAR));
                g_bg0_map[(u16)((21u << 5) + lx + l)] = SNES_TILE(ltile, PAL_UI_ATLAS, 1u);
            }
        }
    }

    /* Rows 23..24: Flashing Timed-Hit Prompt, Action Banner, or Real Move Menu */
    if (b->ring.active) {
        pal = ((((u16)g_hearth.frame_count) & 4u) != 0u) ? PAL_UI_GOLD : PAL_UI_CYAN;
        bg0_text(3u, 23u, pal,
            b->ring.is_parry ? ">> PRESS A TO PARRY! <<" : ">> PRESS A TIMED HIT! <<");
    } else if (b->banner_frames > 0u && b->banner[0] != '\0') {
        bg0_text_n(2u, 23u, PAL_UI_CYAN, b->banner, 28u);
    } else {
        for (c = 0; c < 4u; c++) {
            cx = (c & 1u) ? 15u : 1u;
            cy = (u16)(23u + (u16)(c >> 1));
            pal = (c == b->selected_cmd) ? PAL_UI_GOLD : PAL_UI_TEXT;
            line_reset(&pos);
            if (c == 0u) {
                line_str(&pos, pm->basic_name);
            } else if (c == 1u) {
                line_str(&pos, pm->skill_name);
                line_str(&pos, " ");
                line_int(&pos, (s16)pm->skill_cost);
                line_str(&pos, "M");
            } else if (c == 2u) {
                line_str(&pos, "GUARD +6M");
            } else {
                line_str(&pos, "TONIC x");
                line_int(&pos, (s16)g_hearth.potions);
            }
            bg0_text(cx, cy, pal, (c == b->selected_cmd) ? ">" : " ");
            bg0_text_n((u16)(cx + 1u), cy, pal, g_line, (c & 1u) ? 15u : 13u);
        }
    }
    bg0_text(3u, 26u, PAL_UI_CYAN, "L/R:SWAP  U/D:TGT  A:ACT");
}

static void draw_shop_ui(void) {
    const HearthShopState *sh = &g_hearth.shop;
    u16 pos, pal;
    u8 count, start, r, idx;

    bg0_fill_rect(1u, 20u, 30u, 7u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
    line_reset(&pos);
    line_str(&pos, (sh->tab == 0u) ? "[BUY] SELL " : " BUY [SELL] ");
    line_int(&pos, (s16)g_hearth.guilders);
    line_str(&pos, "G");
    bg0_text_n(2u, 20u, PAL_UI_GOLD, g_line, 28u);

    count = (sh->tab == 0u) ? (u8)HEARTH_SHOP_ITEM_COUNT : (u8)HEARTH_FISH_SPECIES;
    start = (sh->cursor >= 3u) ? (u8)(sh->cursor - 2u) : 0u;
    for (r = 0u; r < 3u && (u8)(start + r) < count; r++) {
        idx = (u8)(start + r);
        pal = (idx == sh->cursor) ? PAL_UI_GOLD : PAL_UI_TEXT;
        line_reset(&pos);
        line_str(&pos, (idx == sh->cursor) ? ">" : " ");
        if (sh->tab == 0u) {
            line_str(&pos, HEARTH_SHOP_ITEMS[idx].name);
            line_str(&pos, " ");
            line_int(&pos, (s16)HEARTH_SHOP_ITEMS[idx].price);
            line_str(&pos, "G");
        } else {
            line_str(&pos, HEARTH_FISH_TABLE[idx].name);
            line_str(&pos, " x");
            line_int(&pos, (s16)g_hearth.fish_bag[idx]);
            line_str(&pos, " ");
            line_int(&pos, (s16)HEARTH_FISH_TABLE[idx].sell_price);
            line_str(&pos, "G");
        }
        bg0_text_n(2u, (u16)(21u + r), pal, g_line, 28u);
    }
    if (sh->msg_timer > 0u && sh->msg[0] != '\0') {
        bg0_text_n(2u, 25u, PAL_UI_CYAN, sh->msg, 28u);
    } else {
        bg0_text(2u, 25u, PAL_UI_TEXT, "L/R:TAB  A:BUY/SELL  B:EXIT");
    }
    bg0_text_n(2u, 26u, PAL_UI_GOLD, sh->title ? sh->title : "MARKET", 28u);
}

static void draw_fishing_ui(void) {
    const HearthFishingState *fs = &g_hearth.fishing;
    u16 pos, pal, row_off, tile;
    u8 k, bi, bw, lo, hi, pfill, tfill;

    bg0_fill_rect(1u, 20u, 30u, 7u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
    if (fs->phase == FISH_PHASE_CAST || fs->phase == FISH_PHASE_WAIT_BITE) {
        bg0_text(2u, 20u, PAL_UI_GOLD, "FISHING - WAIT FOR BITE...");
        bg0_text(2u, 22u, PAL_UI_TEXT, "PRESS A ON BOBBER SPLASH!");
        bg0_text(2u, 26u, PAL_UI_CYAN, "B:LEAVE PIER");
    } else if (fs->phase == FISH_PHASE_BITE_ALERT) {
        pal = ((((u16)g_hearth.frame_count) & 4u) != 0u) ? PAL_UI_GOLD : PAL_UI_CYAN;
        bg0_text(3u, 22u, pal, "BITE! PRESS A TO HOOK!");
        bg0_text(2u, 26u, PAL_UI_TEXT, "HOLD A TO REEL ONCE HOOKED");
    } else if (fs->phase == FISH_PHASE_REELING) {
        bg0_text(2u, 20u, PAL_UI_GOLD, "REEL! HOLD A TO RAISE ZONE");

        /* True 4bpp Pixel-Art Water Trough & Glowing Emerald Catch Zone (Row 22, cols 5..26) */
        bi = (u8)(((u16)fs->bar_pos * 51u) >> 8);
        bw = (u8)(((u16)fs->bar_half_w * 51u) >> 8);
        if (bw < 2u) bw = 2u;
        lo = (bi > bw) ? (u8)(bi - bw) : 0u;
        hi = (u8)(bi + bw);
        if (hi > 19u) hi = 19u;
        row_off = (u16)(22u << 5);
        g_bg0_map[(u16)(row_off + 5u)]  = SNES_TILE(SUI_FISH_CAP_L, PAL_UI_ATLAS, 1u);
        for (k = 0u; k < 20u; k++) {
            if (k >= lo && k <= hi) {
                tile = (k == lo) ? SUI_FISH_ZONE_L : ((k == hi) ? SUI_FISH_ZONE_R : SUI_FISH_ZONE_M);
            } else {
                tile = SUI_FISH_WATER;
            }
            g_bg0_map[(u16)(row_off + 6u + k)] = SNES_TILE(tile, PAL_UI_ATLAS, 1u);
        }
        g_bg0_map[(u16)(row_off + 26u)] = SNES_TILE(SUI_FISH_CAP_R, PAL_UI_ATLAS, 1u);

        /* True 4bpp Pixel-Art Catch Progress & Line Tension Bars (Row 24) */
        pfill = (u8)(((u16)fs->progress * 21u) >> 8);
        tfill = (u8)(((u16)fs->tension  * 21u) >> 8);
        if (fs->progress > 0u && pfill == 0u) pfill = 1u;
        if (fs->tension  > 0u && tfill == 0u) tfill = 1u;
        bg0_text(2u, 24u, PAL_UI_TEXT, "CATCH");
        row_off = (u16)(24u << 5);
        for (k = 0u; k < 8u; k++) {
            g_bg0_map[(u16)(row_off + 8u + k)] = SNES_TILE((k < pfill) ? SUI_BAR_GRN : SUI_BAR_BG, PAL_UI_ATLAS, 1u);
        }
        bg0_text(17u, 24u, PAL_UI_TEXT, "TENS");
        for (k = 0u; k < 8u; k++) {
            g_bg0_map[(u16)(row_off + 22u + k)] = SNES_TILE((k < tfill) ? SUI_BAR_RED : SUI_BAR_BG, PAL_UI_ATLAS, 1u);
        }
        bg0_text(2u, 26u, PAL_UI_CYAN, "KEEP FISH IN GREEN  B:QUIT");
        if (g_bg0_dirty < 2u) g_bg0_dirty = 2u;
    } else if (fs->phase == FISH_PHASE_CAUGHT) {
        line_reset(&pos);
        line_str(&pos, "CAUGHT ");
        line_str(&pos, HEARTH_FISH_TABLE[fs->species % HEARTH_FISH_SPECIES].name);
        line_str(&pos, "!");
        bg0_text_n(5u, 21u, PAL_UI_GOLD, g_line, 25u);
        line_reset(&pos);
        line_str(&pos, "WEIGHT: ");
        line_int(&pos, (s16)fs->weight_oz);
        line_str(&pos, " OZ");
        bg0_text_n(5u, 23u, PAL_UI_TEXT, g_line, 25u);
        bg0_text(2u, 26u, PAL_UI_CYAN, "A:CONTINUE");
    } else {
        bg0_text(3u, 22u, PAL_UI_GOLD, "LINE SNAPPED! IT ESCAPED");
        bg0_text(2u, 26u, PAL_UI_CYAN, "A:CONTINUE");
    }
}

static void draw_atlas_ui(void) {
    static const char *VEH_LBL[6] = { "FOOT", "RAFT", "SHIP", "BALLOON", "AIRSHIP", "STARSHIP" };
    u16 rx, ry, pos, tile_idx, row_off;

    bg0_draw_box(0u, 0u, 32u, 27u);
    bg0_text(6u, 1u, PAL_UI_GOLD, "EVERTALE WORLD ATLAS");

    /* Stamp pre-baked 24x16 (192x128 px) 4bpp Pixel-Art World Map onto BG0 cols 4..27, rows 3..18 */
    tile_idx = SUI_ATLAS_BASE;
    for (ry = 0u; ry < SUI_ATLAS_TH; ry++) {
        row_off = (u16)(((ry + 3u) << 5) + 4u);
        for (rx = 0u; rx < SUI_ATLAS_TW; rx++) {
            g_bg0_map[(u16)(row_off + rx)] = SNES_TILE(tile_idx++, PAL_UI_ATLAS, 1u);
        }
    }

#if HEARTH_DESIGNER_MODE
    if (g_hearth.designer_sandbox) {
        line_reset(&pos);
        line_str(&pos, "< ");
        line_str(&pos, HEARTH_REGION_NAMES[(g_hearth.atlas_cursor < REGION_COUNT) ? g_hearth.atlas_cursor : 0u]);
        line_str(&pos, " >");
        bg0_text_n(2u, 20u, PAL_UI_GOLD, g_line, 28u);

        line_reset(&pos);
        line_str(&pos, "VESSEL: ");
        line_str(&pos, VEH_LBL[g_hearth.active_vehicle < 6u ? g_hearth.active_vehicle : 0u]);
        line_str(&pos, " (UP/DN)");
        bg0_text_n(2u, 22u, PAL_UI_CYAN, g_line, 28u);

        bg0_text(2u, 24u, PAL_UI_TEXT, "L/R:REGION   A:WARP THERE");
        bg0_text(2u, 25u, PAL_UI_CYAN, "START/SEL/B: CLOSE ATLAS");
    } else
#endif
    {
        line_reset(&pos);
        line_str(&pos, HEARTH_REGION_NAMES[(g_hearth.world_region_id < REGION_COUNT) ? g_hearth.world_region_id : 0u]);
        bg0_text_n(2u, 20u, PAL_UI_GOLD, g_line, 28u);

        line_reset(&pos);
        line_str(&pos, "TRAVEL: ");
        line_str(&pos, VEH_LBL[g_hearth.active_vehicle < 6u ? g_hearth.active_vehicle : 0u]);
        bg0_text_n(2u, 22u, PAL_UI_CYAN, g_line, 28u);

        bg0_text(2u, 24u, PAL_UI_TEXT, "GOLD:PORT/TOWN  CYAN:VISTA");
        bg0_text(2u, 25u, PAL_UI_CYAN, "START/SEL/B: CLOSE ATLAS");
    }
    g_bg0_dirty = 3u;
}

/* Native 16-bit HUD dirty signature (zero 32-bit shifts or high bit-shifts on WDC 65816) */
static u16 compute_hud_sig(u8 want, const HearthPpuPacket *ppu) {
    u16 sig = (u16)(((u16)g_hearth.mode << 4) | (u16)want);
    (void)ppu;
    if (want == 7u) {
        return (u16)(sig ^ ((u16)g_hearth.world_region_id << 4) ^
                     ((u16)g_hearth.atlas_cursor << 8) ^
                     ((u16)g_hearth.active_vehicle << 12));
    }
    if (want == 0u) {
        const HearthPartyMember *m = &g_hearth.party[g_hearth.active_slot];
        sig = (u16)(sig + (u16)m->hp + (u16)(m->mp << 5) + g_hearth.guilders);
        sig ^= (u16)(((u16)g_hearth.zone_id << 8) ^ ((u16)g_hearth.world_region_id << 12));
        if (g_hearth.ppu.interact_active && g_hearth.ppu.interact_label) {
            sig ^= (u16)(0x4000u ^ ((u16)(u8)g_hearth.ppu.interact_label[0] << 8) ^ (u16)(u8)g_hearth.ppu.interact_label[1]);
        }
        return sig;
    }
    if (want == 1u) {
        return (u16)(sig ^ ((u16)g_hearth.title_cursor << 8));
    }
    if (want == 4u) {
        return (u16)(sig ^ ((u16)g_hearth.intro.beat << 9) ^ (g_hearth.intro.chars << 1) ^ (u16)(g_hearth.intro.auto_play ? 0x8000u : 0u));
    }
    if (want == 5u) {
        const HearthShopState *sh = &g_hearth.shop;
        sig ^= (u16)(((u16)sh->tab << 8) ^ ((u16)sh->cursor << 2) ^ g_hearth.guilders);
        if (sh->msg_timer > 0u) sig ^= 0x8000u;
        return sig;
    }
    if (want == 6u) {
        const HearthFishingState *fs = &g_hearth.fishing;
        sig ^= (u16)(((u16)fs->phase << 8) ^ ((u16)fs->progress << 1) ^ ((u16)fs->tension << 6) ^ ((u16)fs->bar_pos << 4));
        if (fs->phase == FISH_PHASE_BITE_ALERT && ((((u16)g_hearth.frame_count) & 4u) != 0u)) sig ^= 0x4000u;
        return sig;
    }
    if (want == 2u) {
        const HearthDialogue *d = &g_hearth.dialogue;
        sig ^= (u16)((d->visible_chars << 4) ^ ((u16)d->selected_choice << 8) ^ d->full_len);
        if (d->finished) sig ^= 0x4000u;
        if (d->next_text != NULL) sig ^= 0x2000u;
        sig ^= (u16)((u16)d->choice_count << 2);
    } else {
        const HearthBattleState *b = &g_hearth.battle;
        const HearthPartyMember *pm = &g_hearth.party[b->active_slot];
        sig = (u16)(sig + (u16)pm->hp + ((u16)pm->mp << 4));
        sig ^= (u16)(((u16)b->active_slot << 8) ^ ((u16)b->selected_cmd << 2) ^
                     ((u16)b->selected_enemy << 4));
        if (b->ring.active) {
            sig ^= 0x4000u;
            if ((((u16)g_hearth.frame_count) & 4u) != 0u) sig ^= 0x2000u;
        }
        if (b->banner_frames > 0u) sig ^= 0x8000u;
        if (b->selected_enemy < b->enemy_count) {
            sig = (u16)(sig + ((u16)b->enemies[b->selected_enemy].hp << 2));
            if (b->enemies[b->selected_enemy].broken_locks[0]) sig ^= 0x0040u;
            if (b->enemies[b->selected_enemy].broken_locks[1]) sig ^= 0x0080u;
        }
    }
    return sig;
}

static void update_ui_layer(const HearthPpuPacket *ppu) {
    u8 want = 0;
    u16 sig;

    if (g_hearth.atlas_open) {
        want = 7;
    } else if (g_hearth.mode == MODE_INTRO) {
        want = 4;
    } else if (g_hearth.mode == MODE_TITLE) {
        want = 1;
    } else if (g_hearth.mode == MODE_SHOP) {
        want = 5;
    } else if (g_hearth.mode == MODE_FISHING) {
        want = 6;
    } else if (g_hearth.mode == MODE_BATTLE ||
               g_hearth.mode == MODE_BATTLE_VICTORY ||
               g_hearth.mode == MODE_BATTLE_DEFEAT) {
        want = 3;
    } else if (g_hearth.dialogue.active) {
        want = 2;
    } else {
        want = 0;
    }

    if (want != g_ui_layout) {
        g_ui_layout = want;
        g_last_hud_sig = 0xFFFFu;
        if (want == 7u) {
            bg0_clear_all();
            if (!g_atlas_baked) {
                setScreenOff();
                bake_bg0_atlas_bitmap();
                g_atlas_baked = 1u;
                draw_atlas_ui();
                dmaCopyVram((u8 *)g_bg0_map, VRAM_BG0_MAP, 2048u);
                g_bg0_dirty = 0u;
                setScreenOn();
            } else {
                draw_atlas_ui();
            }
            g_last_hud_sig = compute_hud_sig(want, ppu);
            g_ui_tick = 0;
            return;
        } else if (want == 1u) {
            draw_title_ui();
            g_last_hud_sig = compute_hud_sig(want, ppu);
        } else if (want == 0u) {
            bg0_clear_all();
            bg0_fill_rect(0u, 0u, 32u, 1u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
            bg0_fill_rect(0u, 1u, 32u, 1u, SNES_TILE(SUI_BOT,  PAL_UI_TEXT, 1u));
            g_bg0_dirty = 3u;
        } else {
            bg0_clear_all();
            bg0_draw_box(0u, 19u, 32u, 9u);
            g_bg0_dirty = 3u;
        }
        g_ui_tick = 0;
        if (want == 1u) return;
    }

    /* Fast dirty-check avoids redundant 65816 string formatting & BG0 VBlank DMA */
    sig = compute_hud_sig(want, ppu);
    if (sig == g_last_hud_sig) return;

    /* Throttle rapid typewriter updates to 15Hz; fishing catch zone glides at 30Hz (fish sprite is 60Hz) */
    if ((want == 2u && !g_hearth.dialogue.finished) ||
        (want == 4u && g_hearth.intro.chars > 0u && g_hearth.intro.scene == g_last_intro_scene)) {
        if (++g_ui_tick < 4u) return;
        g_ui_tick = 0;
    } else if (want == 6u && g_hearth.fishing.phase == FISH_PHASE_REELING) {
        if (++g_ui_tick < 2u) return;
        g_ui_tick = 0;
    }
    g_last_hud_sig = sig;

    if (want == 7u)      draw_atlas_ui();
    else if (want == 1u) draw_title_cursor();
    else if (want == 0u) draw_explore_hud(ppu);
    else if (want == 2u) draw_dialogue_ui();
    else if (want == 4u) draw_intro_ui();
    else if (want == 5u) draw_shop_ui();
    else if (want == 6u) draw_fishing_ui();
    else                 draw_battle_ui();
}

/* ============================================================================
 * 8. SNES HARDWARE OAM SPRITE RENDERER (Direct oamMemory[] Shadow Writes)
 * ============================================================================ */
/* Pre-combined 16-bit word: ((0x20 | (pal << 1)) << 8) | tile (49 core sprites) */
static const u16 SPR_TILE_ATTR_LUT[49] = {
    0x2200u, 0x2202u, 0x2204u, 0x2206u, /* 0..3: Hero, Pip, Bramble, Lumina (PAL1) */
    0x2408u, 0x240Au, 0x240Cu,          /* 4..6: Elder, Clara, Guard (PAL2) */
    0x260Eu, 0x2620u, 0x2622u, 0x2624u, /* 7..10: Gloomcap, Thornling, DuskMoth, Briar (PAL3) */
    0x2426u, 0x2428u, 0x242Au,          /* 11..13: Windmill, Waystone, Chest Closed (PAL2) */
    0x262Cu, 0x262Eu, 0x2640u, 0x2642u, 0x2644u, /* 14..18: Emotes + Heart Orb (PAL3) */
    0x2446u, 0x2448u, 0x244Au,          /* 19..21: Chest Open, Mouse, Pig (PAL2) */
    0x264Cu,                            /* 22: Shadow Grandma (PAL3) */
    0x224Eu,                            /* 23: Mido Fox-Kit (PAL1) */
    0x2660u, 0x2662u, 0x2664u, 0x2666u, 0x2668u, /* 24..28: Rat, Slime, Boar, Muckmaw, Ironfang (PAL3) */
    0x246Au,                            /* 29: Hollow Oak (PAL2) */
    0x226Cu,                            /* 30: Fishing Bobber (PAL1) */
    0x246Eu,                            /* 31: Alderbrook Townsfolk (PAL2) */
    0x2480u, 0x2482u, 0x2484u, 0x2486u, 0x2488u, /* 32..36: Mara, Barnaby, Maple, Jasper, Locomotive (PAL2) */
    0x248Au, 0x248Cu, 0x248Eu,          /* 37..39: Tessa, Corin, Sentinel (PAL2) */
    0x22A0u,                            /* 40: Cedar Raft (PAL1) */
    0x26A2u, 0x26A4u,                   /* 41..42: Dune Scorpion, Giant Sandworm (PAL3) */
    0x24A6u, 0x24A8u, 0x24AAu, 0x24ACu, /* 43..46: Camel, Penguin, Monkey, Robot (PAL2) */
    0x24AEu, 0x24C0u                    /* 47..48: Shipwright Orin, Babana Stall (PAL2) */
};

static const u8 HI_TAIL_MASK[4] = { 0x00u, 0x54u, 0x50u, 0x40u };

/* Emit one SNES OAM entry under OBJ_SIZE8_L16 (is_large=1 -> 16x16, is_large=0 -> 8x8) */
#define EMIT_SNES_OAM(ox_, oy_, otile_, oattr_, is_large_) do { \
    oamMemory[oidx]               = (u8)(ox_); \
    oamMemory[(u16)(oidx + 1u)]   = (u8)(oy_); \
    oamMemory[(u16)(oidx + 2u)]   = (u8)(otile_); \
    oamMemory[(u16)(oidx + 3u)]   = (u8)(oattr_); \
    oidx = (u16)(oidx + 4u); \
    if ((ox_) < 0) hi_byte |= hi_mask; \
    if (is_large_) hi_byte |= (u16)(hi_mask << 1); \
    hi_mask <<= 2; \
    if (hi_mask >= 0x0100u) { \
        oamMemory[hi_idx++] = (u8)hi_byte; \
        hi_byte = 0u; \
        hi_mask = 1u; \
    } \
    count++; \
} while (0)

static void render_snes_sprites(const HearthPpuPacket *ppu) {
    u16 i, n, count, sid, ta, flg, oidx, hi_idx, hi_byte, hi_mask, prev_cnt, end_lane;
    s16 sx, sy, sy_bot, sx_r, shx, shy, min_y, max_y, min_y_tall, max_y_tall;
    u16 prog, rem, rpal;
    u8 boot_tile, hdir, hpose, rkey, hflip, bt_l, bt_r, battr;
    const HearthOamSprite *o;
    (void)ppu;

    count = 0u;
    oidx = 0u;
    hi_idx = 512u;
    hi_byte = 0u;
    hi_mask = 1u;

    if (g_hearth.mode != MODE_TITLE && !g_hearth.atlas_open) {
        shx = (s16)g_hearth.ppu.shake_x;
        shy = (s16)g_hearth.ppu.shake_y;
        min_y      = (g_ui_layout == 0u) ? 10 : -15;
        max_y      = (g_ui_layout >= 2u) ? 140 : 224;
        min_y_tall = (g_ui_layout == 0u) ? 0  : -23;
        max_y_tall = (g_ui_layout >= 2u) ? 132 : 224;
        n = (u16)g_hearth.ppu.oam_count;
        if (n > 48u) n = 48u;
        o = g_hearth.ppu.oam;

        for (i = 0u; i < n && count < 118u; i++, o++) {
            flg = (u16)o->flags;
            sid = (u16)o->sprite_id;
            if (sid >= 49u) sid = 0u;
            boot_tile = SPR_BOOT_TILE_LUT[sid];

            if (boot_tile != 0u) {
                /* Tall 16x24 JRPG character: 1x 16x16 (OBJ_LARGE) + 2x 8x8 boot tiles (OBJ_SMALL) */
                sy = (s16)(o->y - 24 + shy);
                if (sy < min_y_tall || sy >= max_y_tall) continue;
                sx = (s16)(o->x - 8 + shx);
                if (sx < -15 || sx >= 256) continue;

                if (sid == 0u) {
                    hdir = (u8)((flg >> 3) & 3u);
                    hpose = hearth_compute_hero_pose(&g_hearth);
                    rkey = (u8)((hdir << 4) | (hpose & 0x0Fu));
                    if (rkey != g_last_rowan_key) {
                        g_last_rowan_key = rkey;
                        g_rowan_pose_idx = snes_rowan_pose_idx(hdir, hpose);
                        g_rowan_dma_dirty = 1u;
                    }
                    ta = 0x2200u;
                    hflip = 0u;
                } else {
                    ta = SPR_TILE_ATTR_LUT[sid];
                    hflip = (u8)((flg & OAM_FLAG_FLIP_X) ? 1u : 0u);
                    if (hflip) ta |= 0x4000u;
                }
                if (flg & OAM_FLAG_LAYER2) ta |= 0x1000u;

                EMIT_SNES_OAM(sx, sy, (u8)ta, (u8)(ta >> 8), 1u);

                sy_bot = (s16)(sy + 16);
                battr = (u8)(ta >> 8);
                bt_l = hflip ? (u8)(boot_tile + 1u) : boot_tile;
                bt_r = hflip ? boot_tile : (u8)(boot_tile + 1u);
                if (sy_bot >= -7 && sy_bot < 224) {
                    if (sx >= -7 && sx < 256 && count < 120u) {
                        EMIT_SNES_OAM(sx, sy_bot, bt_l, battr, 0u);
                    }
                    sx_r = (s16)(sx + 8);
                    if (sx_r >= -7 && sx_r < 256 && count < 120u) {
                        EMIT_SNES_OAM(sx_r, sy_bot, bt_r, battr, 0u);
                    }
                }
            } else {
                /* Standard 16x16 sprite (OBJ_LARGE under OBJ_SIZE8_L16) */
                sy = (s16)(o->y - ((flg & OAM_FLAG_LAYER2) ? 8 : 16) + shy);
                if (sy < min_y || sy >= max_y) continue;
                sx = (s16)(o->x - 8 + shx);
                if (sx < -15 || sx >= 256) continue;

                if (sid == 1u) {
                    /* Petite 3-Pose Bouncy Garden-Gnome Pip: Idle (0x02), Hop (0xE6), Squash (0xE8) */
                    if ((flg & OAM_FLAG_WALK_BIT) != 0u) {
                        ta = ((flg & OAM_FLAG_STEP_BIT) != 0u) ? 0x22E6u : 0x22E8u;
                    } else {
                        ta = 0x2202u;
                    }
                    if (flg & OAM_FLAG_FLIP_X) ta |= 0x4000u;
                } else {
                    ta = SPR_TILE_ATTR_LUT[sid];
                    if (flg & OAM_FLAG_FLIP_X) ta |= 0x4000u;
                }
                if (flg & OAM_FLAG_LAYER2) ta |= 0x1000u;

                EMIT_SNES_OAM(sx, sy, (u8)ta, (u8)(ta >> 8), 1u);
            }
        }

        /* Battle Choreography VFX burst sprite (16x16 OBJ_LARGE) */
        if (g_hearth.mode == MODE_BATTLE && g_hearth.battle.vfx_kind != BVFX_NONE && count < 122u) {
            rem = (g_hearth.battle.vfx_dur > 0u) ? g_hearth.battle.vfx_dur : 1u;
            prog = (u16)(((u16)g_hearth.battle.vfx_timer << 4) / rem);
            if (prog > 16u) prog = 16u;
            sx = (s16)(g_hearth.battle.vfx_x + (((g_hearth.battle.vfx_tx - g_hearth.battle.vfx_x) * (s16)prog) >> 4) - 8);
            sy = (s16)(g_hearth.battle.vfx_y + (((g_hearth.battle.vfx_ty - g_hearth.battle.vfx_y) * (s16)prog) >> 4) - 16);
            rpal = (u16)(0x36u | ((((u16)g_hearth.frame_count) & 2u) ? 0x40u : 0x00u)
                               | ((((u16)g_hearth.frame_count) & 4u) ? 0x80u : 0x00u));
            if (sx >= 0 && sx < 248 && sy >= 0 && sy < 140) {
                EMIT_SNES_OAM(sx, sy, 0xCEu, (u8)rpal, 1u);
            }
        }

        /* Converging & flashing Timed-Hit / Parry indicator ring sprite (16x16 OBJ_LARGE) */
        if (g_hearth.mode == MODE_BATTLE && g_hearth.battle.ring.active && count < 124u) {
            prog = g_hearth.battle.ring.progress_fp;
            rem = (prog < 256u) ? (u16)(256u - prog) : 0u;
            rpal = (prog >= g_hearth.battle.ring.sweet_start_fp)
                ? ((((u16)g_hearth.frame_count & 2u) != 0u) ? 0x32u : 0x34u)
                : 0x36u;
            sx = (s16)(g_hearth.battle.ring.x - 8);
            sy = (s16)(g_hearth.battle.ring.y - 20 - (s16)(rem >> 4));
            if (sx >= 0 && sx < 248 && sy >= 0 && sy < 140) {
                EMIT_SNES_OAM(sx, sy, 0xC2u, (u8)rpal, 1u);
            }
        }

        /* 60fps Hardware Drifting Biome Atmosphere Particles (8x8 OBJ: leaves, petals, fireflies, snow) */
        if (g_hearth.mode == MODE_EXPLORE && count < 118u) {
            u8 ap, p_pal, pid;
            u16 fc16;
            pid = ppu->terrain_pal_id;
            fc16 = (u16)g_hearth.frame_count;
            p_pal = (pid == 1u || pid == 6u || pid == 16u) ? 0x30u
                  : (pid == 14u) ? 0x34u
                  : (pid == 7u || pid == 8u || pid == 3u || pid == 5u) ? 0x36u : 0x32u;
            for (ap = 0u; ap < 6u; ap++) {
                u16 seed = (u16)((ap * 43u + 17u) & 255u);
                s16 ax, ay;
                if (pid == 7u || pid == 8u || pid == 3u || pid == 5u) {
                    if (((fc16 + (u16)(ap * 19u)) & 31u) >= 20u) continue;
                    ax = (s16)(((seed * 3u + (fc16 >> 2) - (u16)(ppu->cam_x >> 1)) & 255u));
                    ay = (s16)((((seed * 5u + ((fc16 >> 3) ^ (u16)(ap << 3)) - (u16)(ppu->cam_y >> 1)) & 127u) + 24u));
                } else {
                    ax = (s16)(((seed * 5u + (fc16 >> 1) - (u16)(ppu->cam_x >> 1)) & 255u));
                    ay = (s16)((((seed * 3u + ((fc16 * (u16)(1u + (ap & 1u))) >> 2) - (u16)(ppu->cam_y >> 1)) & 191u) + 16u));
                }
                if (ax < 248 && ay < 208) {
                    EMIT_SNES_OAM(ax, ay, 0x42u, p_pal, 0u);
                }
            }
        }

        /* 60fps Pixel-Art Swimming / Trophy Fish Hardware Sprite (16x16 OBJ_LARGE) */
        if (g_hearth.mode == MODE_FISHING && count < 126u) {
            if (g_hearth.fishing.phase == FISH_PHASE_REELING) {
                sx = (s16)(40 + (s16)(((u16)g_hearth.fishing.fish_pos * 205u) >> 7));
                sy = 172;
                rpal = (g_hearth.fishing.fish_vel < 0) ? 0x72u : 0x32u;
                EMIT_SNES_OAM(sx, sy, 0xE4u, (u8)rpal, 1u);
            } else if (g_hearth.fishing.phase == FISH_PHASE_CAUGHT) {
                sx = 16;
                sy = (s16)(170 + ((((u16)g_hearth.frame_count) & 16u) ? 1 : 0));
                EMIT_SNES_OAM(sx, sy, 0xE4u, 0x32u, 1u);
            }
        }
    } else if (g_hearth.atlas_open) {
        /* 60fps Hardware Sprites floating over the 192x128 Pixel-Art World Atlas (16x16 OBJ_LARGE) */
        u16 htx = (u16)((u16)(FP_PX(g_hearth.hero_fx) >> 4) & 255u);
        s16 hty_s = (s16)(FP_PX(g_hearth.hero_fy) >> 4);
        while (hty_s < 0) hty_s = (s16)(hty_s + HEARTH_GLOBE_H);
        while (hty_s >= HEARTH_GLOBE_H) hty_s = (s16)(hty_s - HEARTH_GLOBE_H);
        sx = (s16)(24 + (s16)(((htx + htx + htx) >> 2)));
        sy = (s16)(16 + (s16)(((u16)hty_s * 171u) >> 8));
        rpal = ((((u16)g_hearth.frame_count) & 8u) != 0u) ? 0x36u : 0x32u;
        EMIT_SNES_OAM(sx, sy, 0xE0u, (u8)rpal, 1u);

#if HEARTH_DESIGNER_MODE
        if (g_hearth.designer_sandbox) {
            static const u16 WARP_RX[REGION_COUNT] = { 104u, 132u,  52u, 128u, 212u,  32u, 220u };
            static const u16 WARP_RY[REGION_COUNT] = {  84u, 155u, 147u,  22u, 126u,  62u,  62u };
            u8 cid = (g_hearth.atlas_cursor < REGION_COUNT) ? g_hearth.atlas_cursor : 0u;
            u16 wtx = WARP_RX[cid];
            sx = (s16)(24 + (s16)(((wtx + wtx + wtx) >> 2)));
            sy = (s16)(16 + (s16)((WARP_RY[cid] * 171u) >> 8));
            rpal = ((((u16)g_hearth.frame_count) & 8u) != 0u) ? 0x32u : 0x36u;
            EMIT_SNES_OAM(sx, sy, 0xE2u, (u8)rpal, 1u);
        }
#endif
    }

    /* Delta OAM hide: clear previously used slots (x=255, y=240, x8=1, size=0 -> 8x8 offscreen) */
    prev_cnt = (u16)g_prev_oam_cnt;
    for (i = count; i < prev_cnt; i++) {
        oamMemory[oidx]             = 0xFFu;
        oamMemory[(u16)(oidx + 1u)] = 0xF0u;
        oidx = (u16)(oidx + 4u);
        hi_byte |= hi_mask;
        hi_mask <<= 2;
        if (hi_mask >= 0x0100u) {
            oamMemory[hi_idx++] = (u8)hi_byte;
            hi_byte = 0u;
            hi_mask = 1u;
        }
    }

    /* Flush any partial 4-sprite high-table byte cleanly (unused upper lanes keep x8=1, size=0) */
    end_lane = (u16)(((count > prev_cnt) ? count : prev_cnt) & 3u);
    if (end_lane != 0u) {
        oamMemory[hi_idx] = (u8)(hi_byte | (u16)HI_TAIL_MASK[end_lane]);
    }
    g_prev_oam_cnt = (u8)count;
}
#undef EMIT_SNES_OAM

/* ============================================================================
 * 8b. SONY SPC700 MUSIC & SOUND EFFECTS DRIVER (SNESMod Soundbank)
 * ============================================================================ */
static u16 resolve_snes_bgm_mod(u8 bgm_id) {
    if (bgm_id == BGM_ALDERBROOK || bgm_id == BGM_JUNGLE) return (u16)MOD_HEARTH_ALDERBROOK;
    if (bgm_id == BGM_CINEMA_WONDER || bgm_id == BGM_DESERT || bgm_id == BGM_PARADISE) return (u16)MOD_HEARTH_WONDER;
    if (bgm_id == BGM_CINEMA_LULLABY || bgm_id == BGM_FROST) return (u16)MOD_HEARTH_LULLABY;
    if (bgm_id == BGM_SHADOW || bgm_id == BGM_SEWERS) return (u16)MOD_HEARTH_SHADOW;
    if (bgm_id == BGM_BATTLE)         return (u16)MOD_HEARTH_BATTLE;
    if (bgm_id == BGM_BOSS || bgm_id == BGM_BOARHOLD ||
        bgm_id == BGM_VOLCANO || bgm_id == BGM_TECH) return (u16)MOD_HEARTH_BOSS;
    return (u16)MOD_HEARTH_HAMLET;
}

static void load_snes_music(u16 mod_id) {
    u8 j;
    if (mod_id == g_cur_bgm_mod) return;
    g_cur_bgm_mod = mod_id;
    spcStop();
    spcLoad(mod_id);
    for (j = 0; j < 6u; j++) {
        spcLoadEffect(j);
    }
    spcSetModuleVolume(168u);
    spcPlay(0u);
    g_spc_playing = 1u;
}

static void play_snes_sfx(uint8_t sfx) {
    /* hearth_sfx.it samples @ 16kHz -> pitch=4 plays at exact 16kHz */
    switch (sfx) {
        case SFX_STEP:
        case SFX_GRASS_RUSTLE:
            spcEffect(4u, 2u, 0x68u);
            break;
        case SFX_DASH:
        case SFX_WATER_SPLASH:
            spcEffect(5u, 2u, 0xA8u);
            break;
        case SFX_CURSOR:
            spcEffect(4u, 0u, 0xC8u);
            break;
        case SFX_CANCEL:
            spcEffect(3u, 0u, 0xC8u);
            break;
        case SFX_TEXT_HERO:
            spcEffect(4u, 0u, 0x78u);
            break;
        case SFX_TEXT_PIP:
            spcEffect(5u, 0u, 0x88u);
            break;
        case SFX_TEXT_ELDER:
            spcEffect(3u, 0u, 0x78u);
            break;
        case SFX_CONFIRM:
        case SFX_EMOTE:
        case SFX_ENEMY_ALERT:
        case SFX_COIN:
        case SFX_FISH_BITE:
            spcEffect(4u, 1u, 0xE8u);
            break;
        case SFX_FISH_REEL:
            spcEffect(4u, 0u, 0xA8u);
            break;
        case SFX_HIT_NORMAL:
        case SFX_HURT:
            spcEffect(4u, 3u, 0xF8u);
            break;
        case SFX_HIT_CRITICAL:
        case SFX_PARRY_CLANG:
        case SFX_LOCK_BREAK:
        case SFX_LOCK_ALL_BREAK:
            spcEffect(4u, 4u, 0xF8u);
            break;
        case SFX_HEAL:
        case SFX_HEART_TAKE:
        case SFX_HEART_GIVE:
        case SFX_SUMMON_ARC:
        case SFX_SPELL_CAST:
        case SFX_CHEST_OPEN:
        case SFX_VICTORY_FANFARE:
        case SFX_BATTLE_SWOOP:
        case SFX_FISH_CATCH:
        case SFX_PORTAL:
            spcEffect(4u, 5u, 0xE8u);
            break;
        default:
            break;
    }
}

/* ============================================================================
 * 9. INPUT & MAIN 60Hz GAME LOOP
 * ============================================================================ */
static uint16_t read_snes_pad(void) {
    u16 raw = padsCurrent(0);
    uint16_t bits = 0;

    if (raw & KEY_UP)     bits |= PAD_UP;
    if (raw & KEY_DOWN)   bits |= PAD_DOWN;
    if (raw & KEY_LEFT)   bits |= PAD_LEFT;
    if (raw & KEY_RIGHT)  bits |= PAD_RIGHT;
    if (raw & KEY_A)      bits |= PAD_A;
    if (raw & KEY_B)      bits |= PAD_B;
    if (raw & KEY_X)      bits |= PAD_X;
    if (raw & KEY_Y)      bits |= PAD_Y;
    if (raw & KEY_L)      bits |= PAD_L;
    if (raw & KEY_R)      bits |= PAD_R;
    if (raw & KEY_START)  bits |= PAD_START;
    if (raw & KEY_SELECT) bits |= PAD_SELECT;
    return bits;
}

static void flush_all_maps_force_blank(void) {
    setScreenOff();
    dmaCopyVram((u8 *)g_bg1_map, VRAM_BG1_MAP, 4096u);
    dmaCopyVram((u8 *)g_bg0_map, VRAM_BG0_MAP, 2048u);
    g_bg1_dirty_l   = 0;
    g_bg1_dirty_r   = 0;
    g_bg1_row_dirty = 0u;
    g_bg0_dirty     = 0;
    setScreenOn();
}

static void flush_env_vblank_dma(void) {
    u8 af = g_env_frame;
    u16 anim_bytes = (u16)(SNES_CHRONO_ANIM_COUNT * 32u); /* 1024 B: 32 Chrono/Mana animated 8x8 tiles */

    /* Single contiguous 1024-byte ROM DMA updates tiles 1..32 (water, foam shores, flowers, fire, lava!) */
    dmaCopyVram((u8 *)&snes_chrono_anim_frames + (u16)((u16)af * anim_bytes),
                (u16)(VRAM_BG1_GFX + 16u), anim_bytes);
    if (g_hearth.has_windmill) {
        u16 t = sprite_slot_to_tile(SPR_WINDMILL_BLADES);
        dmaCopyVram(g_anim_windmill[af][0], (u16)(VRAM_OBJ_GFX + (t << 4)),         64u);
        dmaCopyVram(g_anim_windmill[af][2], (u16)(VRAM_OBJ_GFX + ((t + 16u) << 4)), 64u);
    }
    g_env_dirty = 0;
}

static void flush_one_vblank_dma(void) {
    /* Stay strictly under SNES NTSC VBlank DMA budget (~2.6 KB max per frame)! */
    if (g_pal_dirty) {
        upload_zone_pal0(g_last_pal_id);
        g_pal_dirty = 0u;
    }
    if (g_rowan_dma_dirty) {
        flush_rowan_vblank_dma();
    }
    if (g_bg1_dirty_l) {
        dmaCopyVram((u8 *)g_bg1_map, VRAM_BG1_MAP, 2048u);
        g_bg1_dirty_l = 0;
    } else if (g_bg1_dirty_r) {
        dmaCopyVram((u8 *)(g_bg1_map + 1024), (u16)(VRAM_BG1_MAP + 1024u), 2048u);
        g_bg1_dirty_r = 0;
    } else {
        if (g_bg1_row_dirty) {
            /* Surgical 128B + 128B = 256B metatile row strip across both SC_64x32 halves */
            u16 roff = g_bg1_dirty_row_off;
            dmaCopyVram((u8 *)(g_bg1_map + roff), (u16)(VRAM_BG1_MAP + roff), 128u);
            dmaCopyVram((u8 *)(g_bg1_map + 1024u + roff), (u16)(VRAM_BG1_MAP + 1024u + roff), 128u);
            g_bg1_row_dirty = 0u;
        }
        if (g_bg0_dirty == 1u) {
            /* Explore HUD row 0 only (64 bytes -> 0.4 scanlines) */
            dmaCopyVram((u8 *)g_bg0_map, VRAM_BG0_MAP, 64u);
            g_bg0_dirty = 0u;
        } else if (g_bg0_dirty == 2u) {
            /* Dialogue / Battle box rows 20..26 only (448 bytes -> 2.7 scanlines) */
            dmaCopyVram((u8 *)(g_bg0_map + 640u), (u16)(VRAM_BG0_MAP + 640u), 448u);
            g_bg0_dirty = 0u;
            return;
        } else if (g_bg0_dirty >= 3u) {
            dmaCopyVram((u8 *)g_bg0_map, VRAM_BG0_MAP, 1792u);
            g_bg0_dirty = 0u;
            return;
        }
        if (g_env_dirty) {
            flush_env_vblank_dma();
        }
    }
}

/* ============================================================================
 * 9b. FAST BOOT STAGING & RETRO 16-BIT CONSOLE SPLASH SCREENS
 * ============================================================================ */
#define SUI_SPLASH_BASE    SUI_ATLAS_BASE /* Temporary BG0 VRAM 112..171 (overwritten on lazy Atlas open) */
#define SUI_SP_RULE        (SUI_SPLASH_BASE + 0u)
#define SUI_SP_CAP_L       (SUI_SPLASH_BASE + 1u)
#define SUI_SP_CAP_R       (SUI_SPLASH_BASE + 2u)
#define SUI_SP_STAR_A      (SUI_SPLASH_BASE + 3u)
#define SUI_SP_STAR_B      (SUI_SPLASH_BASE + 4u)
#define SUI_SP_STAR_C      (SUI_SPLASH_BASE + 5u)
#define SUI_SP_CORNER      (SUI_SPLASH_BASE + 6u)
#define SUI_SP_CREST_BASE  (SUI_SPLASH_BASE + 8u)  /* 16 tiles (4x4 = 32x32 px) active splash crest */
#define SUI_SP_FLAME_ALT   (SUI_SPLASH_BASE + 24u) /* 4 tiles (2x2) alternate flickering lantern flame */

static void sp32_clear(void) {
    hearth_memset(g_splash_32, 0, 1024u);
}

static void sp32_rect(u8 x, u8 y, u8 w, u8 h, u8 col) {
    u8 py, px;
    for (py = y; py < (u8)(y + h) && py < 32u; py++) {
        u8 *row = g_splash_32 + ((u16)py << 5);
        for (px = x; px < (u8)(x + w) && px < 32u; px++) {
            row[px] = col;
        }
    }
}

static void sp32_disc(s16 cx, s16 cy, s16 r, u8 col) {
    s16 y, x, dx, dy, r2;
    r2 = (s16)(r * r + r);
    for (y = (s16)(cy - r); y <= (s16)(cy + r); y++) {
        if (y < 0 || y >= 32) continue;
        dy = (s16)(y - cy);
        for (x = (s16)(cx - r); x <= (s16)(cx + r); x++) {
            if (x < 0 || x >= 32) continue;
            dx = (s16)(x - cx);
            if ((s16)(dx * dx + dy * dy) <= r2) {
                g_splash_32[(u16)(((u16)y << 5) + (u16)x)] = col;
            }
        }
    }
}

static void sp32_upload_tile(u8 tx, u8 ty, u16 bg0_tile_idx) {
    u8 y, x;
    u16 sy = (u16)((u16)ty << 3);
    u16 sx = (u16)((u16)tx << 3);
    for (y = 0u; y < 8u; y++) {
        const u8 *sr = g_splash_32 + (((u16)(sy + y)) << 5) + sx;
        u8 *dr = g_pix8 + ((u16)y << 3);
        for (x = 0u; x < 8u; x++) dr[x] = sr[x];
    }
    upload_bg0_tile(bg0_tile_idx);
}

static void sp32_upload_4x4(u16 base_tile) {
    u8 ty, tx;
    u16 t = base_tile;
    for (ty = 0u; ty < 4u; ty++) {
        for (tx = 0u; tx < 4u; tx++) {
            sp32_upload_tile(tx, ty, t++);
        }
    }
}

static void bake_snes_splash_decor_tiles(void) {
    /* Horizontal gilded/silver divider rule */
    p8_fill(0u);
    p8_rect(0u, 3u, 8u, 1u, 11u);
    p8_rect(0u, 4u, 8u, 1u, 2u);
    upload_bg0_tile(SUI_SP_RULE);

    /* Left diamond endcap flourish */
    p8_fill(0u);
    g_pix8[27] = 11u; g_pix8[28] = 15u; g_pix8[29] = 11u; g_pix8[30] = 11u; g_pix8[31] = 11u;
    g_pix8[19] = 11u; g_pix8[35] = 11u;
    g_pix8[36] = 2u;  g_pix8[37] = 2u;  g_pix8[38] = 2u;  g_pix8[39] = 2u;
    upload_bg0_tile(SUI_SP_CAP_L);

    /* Right diamond endcap flourish */
    p8_fill(0u);
    g_pix8[24] = 11u; g_pix8[25] = 11u; g_pix8[26] = 11u; g_pix8[27] = 15u; g_pix8[28] = 11u;
    g_pix8[20] = 11u; g_pix8[36] = 11u;
    g_pix8[32] = 2u;  g_pix8[33] = 2u;  g_pix8[34] = 2u;  g_pix8[35] = 2u;
    upload_bg0_tile(SUI_SP_CAP_R);

    /* Twinkling 4-point star frames A / B / C */
    p8_fill(0u);
    g_pix8[27] = 15u; g_pix8[28] = 15u;
    g_pix8[19] = 11u; g_pix8[35] = 11u; g_pix8[26] = 11u; g_pix8[29] = 11u;
    upload_bg0_tile(SUI_SP_STAR_A);

    p8_fill(0u);
    p8_rect(3u, 1u, 2u, 6u, 11u);
    p8_rect(1u, 3u, 6u, 2u, 11u);
    p8_rect(3u, 3u, 2u, 2u, 15u);
    upload_bg0_tile(SUI_SP_STAR_B);

    p8_fill(0u);
    g_pix8[27] = 11u;
    upload_bg0_tile(SUI_SP_STAR_C);

    /* Storybook corner flourish (mirrorable via H/V flip) */
    p8_fill(0u);
    p8_rect(1u, 1u, 7u, 1u, 11u);
    p8_rect(1u, 1u, 1u, 7u, 11u);
    p8_rect(2u, 2u, 5u, 1u, 2u);
    p8_rect(2u, 2u, 1u, 5u, 2u);
    g_pix8[9] = 15u;
    upload_bg0_tile(SUI_SP_CORNER);
}

static void upload_snes_splash_pal(u8 splash_id) {
    static const uint32_t SP0_RGB[16] = {
        0x0A0F24u, /* 0: Midnight indigo backdrop */
        0x192344u, /* 1: Deep badge shadow */
        0x4A5B82u, /* 2: Bevel slate */
        0xB62638u, /* 3: Crimson shadow (Red orb) */
        0xF44354u, /* 4: Crimson bright (Red orb) */
        0xC99218u, /* 5: Sunburst shadow (Yellow orb) */
        0xFFE048u, /* 6: Sunburst bright (Yellow orb) */
        0x1D8C48u, /* 7: Emerald shadow (Green orb) */
        0x44DC78u, /* 8: Emerald bright (Green orb) */
        0x2256B8u, /* 9: Cobalt shadow (Blue orb) */
        0x4A98FFu, /* 10: Cobalt bright (Blue orb) */
        0xB8C8E8u, /* 11: Chrome silver trim */
        0x7892C4u, /* 12: Dim steel */
        0x74E4FFu, /* 13: Electric cyan */
        0xFFD866u, /* 14: Warm gold */
        0xFFFFFFu  /* 15: Pure specular gleam */
    };
    static const uint32_t SP1_RGB[16] = {
        0x141020u, /* 0: Warm twilight plum backdrop */
        0x221C32u, /* 1: Wrought iron dark */
        0x524662u, /* 2: Wrought iron rim */
        0x98361Cu, /* 3: Ember deep orange */
        0xE46824u, /* 4: Lantern flame orange */
        0xFFB438u, /* 5: Lantern glow amber */
        0xFFE878u, /* 6: Bright candle gold */
        0x6E4C2Au, /* 7: Aged bronze */
        0xB88642u, /* 8: Warm brass */
        0x362C46u, /* 9: Iron highlight */
        0xD89844u, /* 10: Gold filigree */
        0xF0C060u, /* 11: Gilded rule */
        0x786A88u, /* 12: Muted lavender */
        0x7CE0F8u, /* 13: Arcane cyan */
        0xF6ECD4u, /* 14: Parchment cream */
        0xFFFDF0u  /* 15: Flame core white */
    };
    static const uint32_t SP2_RGB[16] = {
        0x0E1D1Cu, /* 0: Deep forest-midnight backdrop */
        0x18302Cu, /* 1: Pine shadow */
        0x38664Au, /* 2: Laurel shadow */
        0x9C2434u, /* 3: Heart ruby shadow */
        0xE84452u, /* 4: Heart ruby bright */
        0xFF9468u, /* 5: Heart warm glow */
        0xFFE484u, /* 6: Radiant gold */
        0x2E8C54u, /* 7: Oak leaf mid */
        0x64CC74u, /* 8: Oak leaf highlight */
        0x7A5028u, /* 9: Acorn cap bronze */
        0xC88A3Cu, /* 10: Acorn gold */
        0xE8B854u, /* 11: Gilded trim */
        0x5C847Cu, /* 12: Sage muted */
        0x7CE4F8u, /* 13: Starlight cyan */
        0xF8EED8u, /* 14: Storybook cream */
        0xFFFFFFu  /* 15: Starlight gleam */
    };
    const uint32_t *src = (splash_id == 0u) ? SP0_RGB : ((splash_id == 1u) ? SP1_RGB : SP2_RGB);
    u16 bg_c, c;

    for (c = 0u; c < 16u; c++) {
        g_pal16[c] = hearth_rgb_to_snes_bgr555(src[c]);
    }
    bg_c = g_pal16[0];
    /* Backdrop color 0 */
    dmaCopyCGram((u8 *)g_pal16, 0u, 2u);
    /* PAL_UI_ATLAS (BG PAL 7): full 16-color crest palette */
    dmaCopyCGram((u8 *)g_pal16, (u16)(PAL_UI_ATLAS * 16u), 32u);

    /* PAL_UI_TEXT (BG PAL 4): seamless text on backdrop -> white/cream */
    for (c = 0u; c < 16u; c++) g_pal16[c] = bg_c;
    g_pal16[0]  = 0u;
    g_pal16[1]  = bg_c;
    g_pal16[15] = hearth_rgb_to_snes_bgr555((splash_id == 0u) ? 0xF2F6FFu : 0xF6ECD4u);
    dmaCopyCGram((u8 *)g_pal16, (u16)(PAL_UI_TEXT * 16u), 32u);

    /* PAL_UI_GOLD (BG PAL 5): bright gold highlight text */
    g_pal16[15] = hearth_rgb_to_snes_bgr555((splash_id == 0u) ? 0xFFE058u : 0xFFD860u);
    dmaCopyCGram((u8 *)g_pal16, (u16)(PAL_UI_GOLD * 16u), 32u);

    /* PAL_UI_CYAN (BG PAL 6): sky cyan / subtle accent text */
    g_pal16[15] = hearth_rgb_to_snes_bgr555((splash_id == 0u) ? 0x74DCFFu : 0x7CE4F8u);
    dmaCopyCGram((u8 *)g_pal16, (u16)(PAL_UI_CYAN * 16u), 32u);

    /* PAL 3 (CGRAM 48..63): muted footer prompt text */
    g_pal16[15] = hearth_rgb_to_snes_bgr555(src[12]);
    dmaCopyCGram((u8 *)g_pal16, 48u, 32u);
}

static void bake_snes_crest(u8 splash_id) {
    sp32_clear();
    if (splash_id == 0u) {
        /* Iconic Super 16-Bit 4-Color Tilted Jewel Crest in sleek beveled housing */
        sp32_disc(16, 16, 14, 1u);
        sp32_disc(16, 16, 13, 11u);
        sp32_disc(16, 16, 12, 2u);
        sp32_disc(16, 16, 11, 1u);
        /* Top-Left: Emerald Green orb */
        sp32_disc(11, 11, 4, 7u);
        sp32_disc(10, 10, 3, 8u);
        g_splash_32[9u * 32u + 9u] = 15u;
        /* Top-Right: Cobalt Blue orb */
        sp32_disc(21, 12, 4, 9u);
        sp32_disc(20, 11, 3, 10u);
        g_splash_32[10u * 32u + 19u] = 15u;
        /* Bottom-Left: Sunburst Yellow orb */
        sp32_disc(11, 20, 4, 5u);
        sp32_disc(10, 19, 3, 6u);
        g_splash_32[18u * 32u + 9u] = 15u;
        /* Bottom-Right: Ruby Crimson orb */
        sp32_disc(21, 21, 4, 3u);
        sp32_disc(20, 20, 3, 4u);
        g_splash_32[19u * 32u + 19u] = 15u;
        /* Center chrome diamond cross-sparkle */
        sp32_rect(15u, 14u, 2u, 4u, 11u);
        sp32_rect(14u, 15u, 4u, 2u, 11u);
        sp32_rect(15u, 15u, 2u, 2u, 15u);
        sp32_upload_4x4(SUI_SP_CREST_BASE);
    } else if (splash_id == 1u) {
        /* Hearthworks Wrought-Iron Lantern & Animated Flickering Flame */
        sp32_disc(16, 17, 13, 1u);
        /* Top brass hanging ring + peaked roof */
        sp32_rect(14u, 2u, 4u, 2u, 8u);
        sp32_rect(15u, 1u, 2u, 1u, 11u);
        sp32_rect(11u, 4u, 10u, 2u, 2u);
        sp32_rect(8u, 6u, 16u, 2u, 9u);
        sp32_rect(6u, 8u, 20u, 2u, 2u);
        sp32_rect(7u, 8u, 18u, 1u, 11u);
        /* Lantern glass chamber & warm glow */
        sp32_rect(9u, 10u, 14u, 14u, 3u);
        sp32_rect(10u, 11u, 12u, 12u, 4u);
        sp32_rect(11u, 12u, 10u, 10u, 5u);
        /* Wrought-iron side pillars & bottom hearth pedestal */
        sp32_rect(8u, 10u, 2u, 14u, 2u);
        sp32_rect(22u, 10u, 2u, 14u, 2u);
        sp32_rect(6u, 24u, 20u, 2u, 2u);
        sp32_rect(7u, 24u, 18u, 1u, 11u);
        sp32_rect(9u, 26u, 14u, 3u, 1u);
        sp32_rect(10u, 27u, 12u, 1u, 8u);
        /* Flame Frame A (tall centered teardrop) */
        sp32_disc(16, 19, 4, 6u);
        sp32_rect(14u, 14u, 4u, 4u, 6u);
        sp32_rect(15u, 12u, 2u, 3u, 6u);
        sp32_disc(16, 19, 2, 15u);
        sp32_rect(15u, 15u, 2u, 3u, 15u);
        sp32_upload_4x4(SUI_SP_CREST_BASE);

        /* Flame Frame B (gentle swaying breeze -> upload center 2x2 tiles to SUI_SP_FLAME_ALT) */
        sp32_rect(10u, 11u, 12u, 12u, 4u);
        sp32_rect(11u, 12u, 10u, 10u, 5u);
        sp32_disc(16, 19, 4, 6u);
        sp32_rect(15u, 14u, 4u, 4u, 6u);
        sp32_rect(16u, 11u, 2u, 3u, 6u);
        sp32_disc(16, 19, 2, 15u);
        sp32_rect(16u, 15u, 2u, 3u, 15u);
        sp32_upload_tile(1u, 1u, (u16)(SUI_SP_FLAME_ALT + 0u));
        sp32_upload_tile(2u, 1u, (u16)(SUI_SP_FLAME_ALT + 1u));
        sp32_upload_tile(1u, 2u, (u16)(SUI_SP_FLAME_ALT + 2u));
        sp32_upload_tile(2u, 2u, (u16)(SUI_SP_FLAME_ALT + 3u));
    } else {
        /* Project Hearth Storybook Crest: Radiant Ruby Heart-Ember & Twin Oak Laurel */
        sp32_disc(16, 16, 14, 1u);
        /* Twin emerald oak-leaf laurel arc */
        sp32_disc(9, 21, 5, 2u);  sp32_disc(9, 20, 4, 7u);  sp32_disc(8, 19, 2, 8u);
        sp32_disc(23, 21, 5, 2u); sp32_disc(23, 20, 4, 7u); sp32_disc(24, 19, 2, 8u);
        sp32_disc(16, 25, 4, 9u); sp32_disc(16, 24, 3, 10u); sp32_rect(15u, 23u, 2u, 2u, 6u);
        /* Radiant golden halo */
        sp32_disc(16, 13, 9, 11u);
        sp32_disc(16, 13, 8, 1u);
        /* Ruby Heart-Ember */
        sp32_disc(13, 11, 4, 3u); sp32_disc(19, 11, 4, 3u);
        sp32_rect(10u, 12u, 12u, 3u, 4u);
        sp32_rect(12u, 15u, 8u, 2u, 4u);
        sp32_rect(14u, 17u, 4u, 2u, 4u);
        sp32_rect(15u, 19u, 2u, 1u, 3u);
        sp32_disc(13, 11, 3, 4u); sp32_disc(19, 11, 3, 4u);
        sp32_disc(13, 10, 1, 5u);
        g_splash_32[9u * 32u + 12u] = 15u;
        g_splash_32[10u * 32u + 12u] = 15u;
        /* Tiny golden crown sparkle above heart */
        sp32_rect(15u, 2u, 2u, 4u, 6u);
        sp32_rect(14u, 3u, 4u, 2u, 6u);
        sp32_rect(15u, 3u, 2u, 2u, 15u);
        sp32_upload_4x4(SUI_SP_CREST_BASE);
    }
}

static void draw_snes_splash_layout(u8 splash_id) {
    u16 y, x, t;
    u16 rule_e = SNES_TILE(SUI_SP_RULE, PAL_UI_ATLAS, 1u);
    u16 capl_e = SNES_TILE(SUI_SP_CAP_L, PAL_UI_ATLAS, 1u);
    u16 capr_e = SNES_TILE(SUI_SP_CAP_R, PAL_UI_ATLAS, 1u);

    bg0_clear_all();

    /* Stamp 4x4 (32x32) pixel-art crest centered at cols 14..17, rows 6..9 */
    t = SUI_SP_CREST_BASE;
    for (y = 0u; y < 4u; y++) {
        for (x = 0u; x < 4u; x++) {
            g_bg0_map[(u16)(((6u + y) << 5) + 14u + x)] = SNES_TILE(t++, PAL_UI_ATLAS, 1u);
        }
    }

    /* Gilded / silver decorative divider rule on row 15 */
    g_bg0_map[(u16)((15u << 5) + 5u)] = capl_e;
    for (x = 6u; x < 26u; x++) g_bg0_map[(u16)((15u << 5) + x)] = rule_e;
    g_bg0_map[(u16)((15u << 5) + 26u)] = capr_e;

    if (splash_id == 0u) {
        bg0_text(6u,  13u, PAL_UI_TEXT, "SUPER 16-BIT EDITION");
        bg0_text(5u,  17u, PAL_UI_GOLD, "MODE 1 FASTROM * 60FPS");
        bg0_text(3u,  19u, PAL_UI_CYAN, "SONY SPC700 DIGITAL AUDIO");
    } else if (splash_id == 1u) {
        bg0_text(4u,  13u, PAL_UI_GOLD, "HEARTHWORKS INTERACTIVE");
        bg0_text(2u,  17u, PAL_UI_TEXT, "16-BIT STORYBOOK RPG ENGINE");
        bg0_text(10u, 19u, PAL_UI_CYAN, "BUILD " HEARTH_VERSION_STR);
    } else {
        /* Storybook corner flourishes */
        g_bg0_map[(u16)((3u << 5) + 2u)]   = SNES_TILE(SUI_SP_CORNER, PAL_UI_ATLAS, 1u);
        g_bg0_map[(u16)((3u << 5) + 29u)]  = SNES_TILE_H(SUI_SP_CORNER, PAL_UI_ATLAS, 1u);
        g_bg0_map[(u16)((22u << 5) + 2u)]  = (u16)(SNES_TILE(SUI_SP_CORNER, PAL_UI_ATLAS, 1u) | 0x8000u);
        g_bg0_map[(u16)((22u << 5) + 29u)] = (u16)(SNES_TILE_H(SUI_SP_CORNER, PAL_UI_ATLAS, 1u) | 0x8000u);
        bg0_text(9u,  13u, PAL_UI_GOLD, "PROJECT HEARTH");
        bg0_text(1u,  17u, PAL_UI_TEXT, "TALES OF ALDERBROOK & EVERTALE");
        bg0_text(3u,  19u, PAL_UI_CYAN, "A HANDCRAFTED 16-BIT JOURNEY");
    }

    bg0_text(6u, 24u, 3u, "PRESS START TO SKIP");
}

static void snes_splash_anim_tick(u8 splash_id, u8 tick) {
    static const u16 STAR_T[4] = { SUI_SP_STAR_A, SUI_SP_STAR_B, SUI_SP_STAR_A, SUI_SP_STAR_C };
    u8 phase = (u8)((tick >> 3) & 3u);
    u16 s0 = SNES_TILE(STAR_T[phase], PAL_UI_ATLAS, 1u);
    u16 s1 = SNES_TILE(STAR_T[(phase + 2u) & 3u], PAL_UI_ATLAS, 1u);

    /* Twinkling stars flanking the 32x32 crest (rows 6 & 9) */
    g_bg0_map[(u16)((6u << 5) + 11u)] = s0;
    g_bg0_map[(u16)((9u << 5) + 20u)] = s0;
    g_bg0_map[(u16)((9u << 5) + 11u)] = s1;
    g_bg0_map[(u16)((6u << 5) + 20u)] = s1;

    if (splash_id == 1u) {
        /* Gently flickering flame inside the Hearthworks Lantern (rows 7..8, cols 15..16) */
        if ((tick & 8u) != 0u) {
            g_bg0_map[(u16)((7u << 5) + 15u)] = SNES_TILE((u16)(SUI_SP_FLAME_ALT + 0u), PAL_UI_ATLAS, 1u);
            g_bg0_map[(u16)((7u << 5) + 16u)] = SNES_TILE((u16)(SUI_SP_FLAME_ALT + 1u), PAL_UI_ATLAS, 1u);
            g_bg0_map[(u16)((8u << 5) + 15u)] = SNES_TILE((u16)(SUI_SP_FLAME_ALT + 2u), PAL_UI_ATLAS, 1u);
            g_bg0_map[(u16)((8u << 5) + 16u)] = SNES_TILE((u16)(SUI_SP_FLAME_ALT + 3u), PAL_UI_ATLAS, 1u);
        } else {
            g_bg0_map[(u16)((7u << 5) + 15u)] = SNES_TILE((u16)(SUI_SP_CREST_BASE + 5u), PAL_UI_ATLAS, 1u);
            g_bg0_map[(u16)((7u << 5) + 16u)] = SNES_TILE((u16)(SUI_SP_CREST_BASE + 6u), PAL_UI_ATLAS, 1u);
            g_bg0_map[(u16)((8u << 5) + 15u)] = SNES_TILE((u16)(SUI_SP_CREST_BASE + 9u), PAL_UI_ATLAS, 1u);
            g_bg0_map[(u16)((8u << 5) + 16u)] = SNES_TILE((u16)(SUI_SP_CREST_BASE + 10u), PAL_UI_ATLAS, 1u);
        }
    }
}

/* Execute staged cold-boot work progressively so nothing is ever skipped if player presses START */
static void snes_run_boot_stage(u8 target_stage) {
    if (g_boot_stage < 1u && target_stage >= 1u) {
        /* Stage 1: Windmill frames + SPC700 IPL boot (terrain metatiles are pre-baked in ROM!) */
        bake_env_windmill_frames();
        if (!g_spc_booted) {
            spcBoot();
            spcSetBank((u8 *)&SOUNDBANK__);
            g_spc_booted = 1u;
        }
        g_boot_stage = 1u;
    }
    if (g_boot_stage < 2u && target_stage >= 2u) {
        /* Stage 2: Force-blank OBJ sprites Part 1..4 */
        bake_obj_sprites_part1();
        bake_obj_sprites_part2();
        bake_obj_sprites_part3();
        bake_obj_sprites_part4();
        g_boot_stage = 2u;
    }
    if (g_boot_stage < 3u && target_stage >= 3u) {
        /* Stage 3: Force-blank OBJ sprites Part 5..7, tall NPCs & BG2 Parallax Vista */
        bake_obj_sprites_part5();
        bake_obj_sprites_part6();
        bake_obj_sprites_part7();
        bake_obj_sprites_hero_ext();
        bake_bg2_parallax_vista();
        g_boot_stage = 3u;
    }
}

static void run_snes_boot_splashes(void) {
    u8 sp, f, b, skipped;
    uint16_t pad, prev_pad;

    skipped = 0u;
    prev_pad = read_snes_pad();

    for (sp = 0u; sp < 3u && !skipped; sp++) {
        setScreenOff();
        if (sp == 1u) {
            snes_run_boot_stage(2u);
        } else if (sp == 2u) {
            snes_run_boot_stage(3u);
        }
        upload_snes_splash_pal(sp);
        bake_snes_crest(sp);
        draw_snes_splash_layout(sp);
        snes_splash_anim_tick(sp, 0u);
        dmaCopyVram((u8 *)g_bg0_map, VRAM_BG0_MAP, 1792u);
        bgSetScroll(0, 0, 0);
        WaitForVBlank();

        /* Smooth 15-frame fade-in (brightness 0 -> 15) */
        for (b = 1u; b <= 15u; b++) {
            if (g_spc_booted) spcProcess();
            WaitForVBlank();
            setBrightness(b);
            pad = read_snes_pad();
            if ((pad & (uint16_t)(~prev_pad)) & (PAD_START | PAD_A | PAD_B)) {
                skipped = 1u;
                break;
            }
            prev_pad = pad;
        }
        if (skipped) break;

        /* Hide heavy WRAM/SPC700 initialization while Splash 1 & Splash 2 gleam at full brightness! */
        if (sp == 0u) {
            snes_run_boot_stage(1u);
        } else if (sp == 1u && g_spc_booted && g_cur_bgm_mod == 0xFFFFu) {
            /* Preload Opening Cinema overture + 6 SFX while Hearthworks Lantern is visible! */
            u8 j;
            g_cur_bgm_mod = (u16)MOD_HEARTH_WONDER;
            spcStop();
            spcLoad((u16)MOD_HEARTH_WONDER);
            for (j = 0u; j < 6u; j++) spcLoadEffect(j);
            spcSetModuleVolume(168u);
            play_snes_sfx(SFX_CONFIRM);
        } else if (sp == 2u && g_spc_booted && !g_spc_playing) {
            /* Gently swell the opening wonder overture on the Project Hearth card! */
            spcPlay(0u);
            g_spc_playing = 1u;
        }

        /* Hold & animate splash (~1.6s, 96 frames, instantly skippable!) */
        for (f = 0u; f < 96u; f++) {
            if ((f & 7u) == 0u) {
                snes_splash_anim_tick(sp, f);
            }
            if (g_spc_booted) spcProcess();
            WaitForVBlank();
            if ((f & 7u) == 0u) {
                /* Surgical 256B DMA of rows 6..9 only -> 1.5 scanlines! */
                dmaCopyVram((u8 *)(g_bg0_map + (6u << 5)), (u16)(VRAM_BG0_MAP + (6u << 5)), 256u);
            }
            pad = read_snes_pad();
            if ((pad & (uint16_t)(~prev_pad)) & (PAD_START | PAD_A | PAD_B)) {
                skipped = 1u;
                break;
            }
            prev_pad = pad;
        }

        /* Smooth 15-frame fade-out (brightness 15 -> 0) */
        for (b = 14u; b > 0u; b--) {
            if (g_spc_booted) spcProcess();
            WaitForVBlank();
            setBrightness(b);
            if (!skipped) {
                pad = read_snes_pad();
                if ((pad & (uint16_t)(~prev_pad)) & (PAD_START | PAD_A | PAD_B)) {
                    skipped = 1u;
                    break;
                }
                prev_pad = pad;
            }
        }
        if (g_spc_booted) spcProcess();
        WaitForVBlank();
        setBrightness(0u);
    }
}

int main(void) {
    uint16_t pad;
    const HearthPpuPacket *ppu;
    u8 scene_kind;
    u8 scene_switched;
    u8 i, af;
    u16 want_mod;

    /* 1. Ultra-Fast Force Blank Setup (< 50ms!) -> show Retro Splash 1 IMMEDIATELY! */
    setScreenOff();
    bgSetGfxPtr(0, VRAM_BG0_GFX);
    bgSetMapPtr(0, VRAM_BG0_MAP, SC_32x32);
    bgSetGfxPtr(1, VRAM_BG1_GFX);
    bgSetMapPtr(1, VRAM_BG1_MAP, SC_64x32);
    bgSetGfxPtr(2, VRAM_BG2_GFX);
    bgSetMapPtr(2, VRAM_BG2_MAP, SC_64x32);
    oamInitGfxAttr(VRAM_OBJ_GFX, OBJ_SIZE8_L16);
    oamClear(0, 0);

    setMode(BG_MODE1, 0);
    bgSetDisable(1);
    bgSetDisable(2);
    g_bg2_enabled = 0u;

    /* Fast-bake ONLY UI/font + tiny splash decor tiles (~20ms) before Splash 1 appears! */
    bake_bg0_ui_and_font();
    bake_snes_splash_decor_tiles();

    /* 2. Cycle 3 Skippable Retro 16-Bit Splash Screens (overlapping heavy asset & SPC700 boot!) */
    run_snes_boot_splashes();

    /* 3. Finish any remaining boot assets (instant if all 3 splashes played, fast if skipped!) */
    setScreenOff();
    snes_run_boot_stage(3u);
    upload_all_palettes();
    bgSetEnable(1);

    /* 4. Boot Portable Engine Core in Native SNES 256x224 Mode */
    hearth_init(&g_hearth);
    g_hearth.prev_pad = read_snes_pad(); /* Prevent skip button from skipping Opening Cinema! */
    bake_world_tile_cache();
    g_last_map_rev = g_hearth.map_revision;
    g_hearth.tuning.ppu_mode = PPU_SNES_256x224;
    ppu = hearth_step_60hz(&g_hearth, g_hearth.prev_pad);
    g_last_pal_id = ppu->terrain_pal_id;
    upload_zone_pal0(g_last_pal_id);
    g_prev_oam_cnt = 0u;
    g_env_frame = 0u;
    g_ui_layout = 255u;

    if (g_hearth.mode == MODE_INTRO) {
        /* BG1 is immediately filled by the 480-tile lossless Cinema CG; BG1 terrain bakes lazily on exit! */
        g_last_scene_kind = 2u;
        g_last_intro_scene = g_hearth.intro.scene;
        upload_cinema_cg_force_blank(g_hearth.intro.scene);
        build_intro_bg1(g_hearth.intro.scene);
        bgSetScroll(1, 0u, 0u);
    } else {
        upload_chrono_terrain_vram();
        g_last_scene_kind = 0u;
        g_last_intro_scene = 255u;
        build_full_world_bg1(ppu->cam_x, ppu->cam_y);
        bgSetScroll(1, (u16)ppu->cam_x, (u16)ppu->cam_y);
        if (ppu->has_mountain_vista) {
            bgSetEnable(2);
            g_bg2_enabled = 1u;
            bgSetScroll(2, (u16)(ppu->cam_x >> 2), (u16)(ppu->cam_y >> 3));
        }
    }
    update_ui_layer(ppu);
    render_snes_sprites(ppu);

    bgSetScroll(0, 0, 0);
    flush_all_maps_force_blank();
    g_last_bright = 15u;

    /* Start opening BGM immediately (already preloaded in SPC700 if splashes played!) */
    want_mod = resolve_snes_bgm_mod(ppu->bgm_id);
    if (want_mod != g_cur_bgm_mod) {
        load_snes_music(want_mod);
    } else if (g_spc_booted && !g_spc_playing) {
        spcPlay(0u);
        g_spc_playing = 1u;
    }

    /* 5. Real 60Hz SNES FastROM Main Loop */
    while (1) {
        pad = read_snes_pad();
        ppu = hearth_step_60hz(&g_hearth, pad);

        if (g_hearth.mode == MODE_INTRO) {
            scene_kind = 2u;
        } else if (g_hearth.mode == MODE_BATTLE ||
                   g_hearth.mode == MODE_BATTLE_VICTORY ||
                   g_hearth.mode == MODE_BATTLE_DEFEAT) {
            scene_kind = 1u;
        } else {
            scene_kind = 0u;
        }
        scene_switched = 0;

        if (scene_kind != 2u && ppu->terrain_pal_id != g_last_pal_id) {
            g_last_pal_id = ppu->terrain_pal_id;
            g_pal_dirty = 1u;
        }

        if (g_hearth.map_revision != g_last_map_rev) {
            g_last_map_rev = g_hearth.map_revision;
            bake_world_tile_cache();
            scene_switched = 1;
            if (scene_kind == 0u) {
                build_full_world_bg1(ppu->cam_x, ppu->cam_y);
            }
        }

        if (scene_kind != g_last_scene_kind ||
            (scene_kind == 2u && g_hearth.intro.scene != g_last_intro_scene)) {
            if (g_last_scene_kind == 2u && scene_kind != 2u) {
                g_last_pal_id = ppu->terrain_pal_id;
                g_pal_dirty = 1u;
                g_full_pal_dirty = 1u; /* Restore BG PAL0..PAL3 overwritten by 60-color CG! */
            }
            g_last_scene_kind = scene_kind;
            g_last_intro_scene = g_hearth.intro.scene;
            scene_switched = 1;
            if (scene_kind == 2u) {
                build_intro_bg1(g_hearth.intro.scene);
            } else if (scene_kind == 1u) {
                build_battle_bg1(g_hearth.battle.biome);
            } else {
                build_full_world_bg1(ppu->cam_x, ppu->cam_y);
            }
        } else if (scene_kind == 0u) {
            stream_world_bg1(ppu->cam_x, ppu->cam_y);
            /* Trigger ~7.5 Hz VBlank CHR environmental tile animation */
            af = (u8)((((u16)g_hearth.frame_count) >> 3) & 3u);
            if (af != g_env_frame) {
                g_env_frame = af;
                g_env_dirty = 1;
            }
        }

        update_ui_layer(ppu);
        render_snes_sprites(ppu);

        /* Trigger SPC700 gameplay SFX */
        for (i = 0; i < ppu->sfx_count; i++) {
            play_snes_sfx(ppu->sfx_queue[i]);
        }

        want_mod = resolve_snes_bgm_mod(ppu->bgm_id);
        if (scene_switched) {
            u8 want_bg2 = (scene_kind == 0u && ppu->has_mountain_vista) ? 1u : 0u;
            u8 want_bright = (ppu->fade_level < 15u) ? (u8)(15u - ppu->fade_level) : 0u;
            setScreenOff();
            if (scene_kind == 2u) {
                upload_cinema_cg_force_blank(g_hearth.intro.scene);
                g_pal_dirty = 0u;
                g_full_pal_dirty = 0u;
            } else if (g_full_pal_dirty) {
                /* Restore Chrono/Mana 415-tile BG1 terrain from ROM (single 4ms DMA!) + 60-color palettes */
                upload_chrono_terrain_vram();
                upload_all_palettes();
                upload_zone_pal0(g_last_pal_id);
                g_full_pal_dirty = 0u;
                g_pal_dirty = 0u;
            } else if (g_pal_dirty || want_bg2 != g_bg2_enabled) {
                upload_zone_pal0(g_last_pal_id);
                g_pal_dirty = 0u;
            }
            if (want_bg2 != g_bg2_enabled) {
                if (want_bg2) bgSetEnable(2);
                else bgSetDisable(2);
                g_bg2_enabled = want_bg2;
            }
            if (g_rowan_dma_dirty) {
                flush_rowan_vblank_dma();
            }
            dmaCopyVram((u8 *)g_bg1_map, VRAM_BG1_MAP, 4096u);
            dmaCopyVram((u8 *)g_bg0_map, VRAM_BG0_MAP, 2048u);
            g_bg1_dirty_l   = 0;
            g_bg1_dirty_r   = 0;
            g_bg1_row_dirty = 0u;
            g_bg0_dirty     = 0;
            g_env_dirty     = 0;
            /* Latch exact hardware scroll BEFORE revealing display -> zero 1-frame scroll jump! */
            if (scene_kind != 0u) {
                bgSetScroll(1, (u16)(-ppu->shake_x), (u16)(-ppu->shake_y));
            } else {
                bgSetScroll(1, (u16)(ppu->cam_x - ppu->shake_x), (u16)(ppu->cam_y - ppu->shake_y));
                if (g_bg2_enabled) {
                    bgSetScroll(2,
                        (u16)((ppu->cam_x >> 2) - ppu->shake_x),
                        (u16)((ppu->cam_y >> 3) - ppu->shake_y));
                }
            }
            WaitForVBlank();
            setBrightness(want_bright);
            g_last_bright = want_bright;
            /* Stream SPC700 music only once display is visible (never hang on black screen!) */
            if (want_bright > 0u && want_mod != g_cur_bgm_mod) {
                load_snes_music(want_mod);
            }
        } else {
            u8 want_bright = (ppu->fade_level < 15u) ? (u8)(15u - ppu->fade_level) : 0u;
            if (ppu->fade_level == 0u && want_mod != g_cur_bgm_mod) {
                load_snes_music(want_mod);
            }
            spcProcess();
            WaitForVBlank();
            /* Latch BG1/BG2 hardware scroll immediately at scanline 225 before DMA */
            if (scene_kind != 0u) {
                bgSetScroll(1, (u16)(-ppu->shake_x), (u16)(-ppu->shake_y));
            } else {
                bgSetScroll(1, (u16)(ppu->cam_x - ppu->shake_x), (u16)(ppu->cam_y - ppu->shake_y));
                if (g_bg2_enabled) {
                    bgSetScroll(2,
                        (u16)((ppu->cam_x >> 2) - ppu->shake_x),
                        (u16)((ppu->cam_y >> 3) - ppu->shake_y));
                }
            }
            flush_one_vblank_dma();
            if (want_bright != g_last_bright) {
                setBrightness(want_bright);
                g_last_bright = want_bright;
            }
        }
    }

    return 0;
}
