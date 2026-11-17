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

/* ============================================================================
 * 1. STATIC SNES WRAM BUFFERS (Bank $7E — zero malloc, tiny 65816 stacks!)
 * ============================================================================ */
static HearthState g_hearth;

/* VRAM Word Addresses (Non-overlapping 64 KB layout: 0x0000..0x7FFF words) */
#define VRAM_OBJ_GFX    0x0000u  /* 20 16x16 sprites -> 0x0000..0x05FF */
#define VRAM_BG0_GFX    0x2000u  /* UI box + 64 ASCII glyphs -> 0x2000..0x25FF */
#define VRAM_BG1_GFX    0x4000u  /* 34 terrain/battle tiles -> 0x4000..0x421F */
#define VRAM_BG0_MAP    0x6800u  /* 32x32 UI tilemap (1024 words = 2048 B) */
#define VRAM_BG1_MAP    0x7000u  /* 64x32 world tilemap (2048 words = 4096 B) */

/* Tilemap entry helper: vhopppcc cccccccc */
#define SNES_TILE(t, pal, prio) \
    ((u16)(((u16)(t) & 0x03FFu) | (((u16)(pal) & 7u) << 10) | (((u16)(prio) & 1u) << 13)))

/* UI Palette slots in BG CGRAM (4..6) */
#define PAL_UI_TEXT     4u  /* Warm cream on dark slate */
#define PAL_UI_GOLD     5u  /* Bright gold on dark slate */
#define PAL_UI_CYAN     6u  /* Arcane cyan on dark slate */

/* BG0 UI Box Tile Indices (at VRAM_BG0_GFX; ASCII font is at 32..95!) */
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
    SUI_BR    = 9
};

/* BG1 Terrain & Battle Tile Indices (at VRAM_BG1_GFX) */
enum {
    ST_EMPTY = 0,
    ST_GRASS,
    ST_GRASS_ALT,
    ST_FLOWERS,
    ST_DIRT,
    ST_COBBLE,
    ST_WATER,
    ST_FORD,
    ST_BRIDGE,
    ST_BRIDGE_RAIL,
    ST_TALL_GRASS,
    ST_BUSH,
    ST_TRUNK,
    ST_ROOF_TOP,
    ST_ROOF_BODY,
    ST_WALL_WOOD,
    ST_WALL_WIN,
    ST_WALL_DOOR,
    ST_WINDMILL_ROOF,
    ST_WINDMILL_WALL,
    ST_FLOWER_BED,
    ST_FENCE,
    ST_LAMP,
    ST_SIGN,
    ST_SHRINE_FLOOR,
    ST_SHRINE_RUNE,
    ST_SHRINE_PILLAR,
    ST_CANOPY,
    ST_ROOF_EAVE,
    ST_SHRINE_ARCH,
    ST_BATTLE_SKY_MEADOW,
    ST_BATTLE_SKY_SHRINE,
    ST_BATTLE_GROUND_MEADOW,
    ST_BATTLE_GROUND_SHRINE
};

#define SPR_TIMED_RING_SLOT 19u

/* Tilemap shadow buffers & pixel baker scratchpads in Bank $7E WRAM */
static u16 g_bg0_map[1024];
static u16 g_bg1_map[2048];
static u8  g_pix8[64];
static u8  g_pix16[256];
static u8  g_tile32[32];
static u16 g_pal16[16];
static char g_line[36];

static u8  g_bg0_dirty   = 0;
static u8  g_bg1_dirty_l = 0;
static u8  g_bg1_dirty_r = 0;
static s16 g_last_cam_mx = -999;
static s16 g_last_cam_my = -999;
static u8  g_last_battle = 255;
static u8  g_ui_layout   = 255;
static u16 g_ui_tick     = 0;

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
static void encode_and_upload_tile8(u16 vram_word_addr) {
    u8 y, x, mask, c;
    u8 bp0, bp1, bp2, bp3;
    u16 row_off;

    for (y = 0; y < 8u; y++) {
        bp0 = 0;
        bp1 = 0;
        bp2 = 0;
        bp3 = 0;
        row_off = (u16)((u16)y << 3);
        for (x = 0; x < 8u; x++) {
            mask = (u8)(0x80u >> x);
            c = g_pix8[row_off + x];
            if (c & 1u) bp0 |= mask;
            if (c & 2u) bp1 |= mask;
            if (c & 4u) bp2 |= mask;
            if (c & 8u) bp3 |= mask;
        }
        g_tile32[(u16)(y * 2u)]       = bp0;
        g_tile32[(u16)(y * 2u + 1u)]  = bp1;
        g_tile32[(u16)(16u + y * 2u)] = bp2;
        g_tile32[(u16)(17u + y * 2u)] = bp3;
    }
    dmaCopyVram(g_tile32, vram_word_addr, 32u);
}

static void upload_bg0_tile(u16 tile_idx) {
    encode_and_upload_tile8((u16)(VRAM_BG0_GFX + (tile_idx << 4)));
}

static void upload_bg1_tile(u16 tile_idx) {
    encode_and_upload_tile8((u16)(VRAM_BG1_GFX + (tile_idx << 4)));
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
}

/* ============================================================================
 * 5. BAKE BG1 TERRAIN, BG0 UI/FONT, AND 16x16 OBJ SPRITES
 * ============================================================================ */
static void bake_bg1_terrain_part1(void) {
    p8_fill(0); upload_bg1_tile(ST_EMPTY);

    /* Meadow grass + wildflower variants */
    p8_fill(4);
    g_pix8[10] = 5; g_pix8[18] = 3; g_pix8[46] = 5; g_pix8[54] = 3; g_pix8[33] = 3;
    upload_bg1_tile(ST_GRASS);

    p8_fill(4);
    g_pix8[13] = 3; g_pix8[21] = 5; g_pix8[42] = 3; g_pix8[51] = 5; g_pix8[31] = 3;
    upload_bg1_tile(ST_GRASS_ALT);

    p8_fill(4);
    g_pix8[18] = 15; g_pix8[19] = 8; g_pix8[26] = 3;
    g_pix8[45] = 14; g_pix8[46] = 8; g_pix8[53] = 3;
    upload_bg1_tile(ST_FLOWERS);

    /* Dirt road & cobblestone */
    p8_fill(7);
    g_pix8[9] = 8; g_pix8[21] = 6; g_pix8[43] = 8; g_pix8[54] = 6;
    upload_bg1_tile(ST_DIRT);

    p8_fill(9);
    p8_rect(0, 0, 7, 3, 10); p8_rect(1, 0, 5, 1, 11);
    p8_rect(0, 4, 3, 3, 10); p8_rect(4, 4, 4, 3, 10); p8_rect(5, 4, 2, 1, 11);
    upload_bg1_tile(ST_COBBLE);

    /* Creek water, pebble ford, bridge */
    p8_fill(12);
    p8_rect(1, 1, 3, 1, 13); p8_rect(2, 2, 2, 1, 14);
    p8_rect(4, 5, 3, 1, 13); p8_rect(5, 6, 2, 1, 14);
    upload_bg1_tile(ST_WATER);

    p8_fill(13);
    p8_rect(1, 2, 2, 2, 10); p8_rect(5, 5, 2, 2, 11);
    g_pix8[14] = 14; g_pix8[50] = 14;
    upload_bg1_tile(ST_FORD);

    p8_fill(7);
    p8_rect(0, 0, 8, 1, 6); p8_rect(0, 4, 8, 1, 6);
    p8_rect(0, 1, 8, 1, 8); p8_rect(0, 5, 8, 1, 8);
    upload_bg1_tile(ST_BRIDGE);

    p8_fill(12);
    p8_rect(0, 1, 8, 2, 6); p8_rect(0, 2, 8, 1, 8);
    p8_rect(1, 0, 2, 6, 6); p8_rect(5, 0, 2, 6, 6);
    upload_bg1_tile(ST_BRIDGE_RAIL);

    /* Tall grass, bush, tree trunk */
    p8_fill(3);
    p8_rect(1, 1, 1, 6, 5); p8_rect(3, 0, 1, 7, 4);
    p8_rect(5, 2, 1, 5, 5); p8_rect(7, 1, 1, 6, 4);
    upload_bg1_tile(ST_TALL_GRASS);

    p8_fill(2);
    p8_rect(1, 1, 6, 6, 3); p8_rect(2, 1, 4, 3, 4); p8_rect(2, 1, 2, 1, 5);
    upload_bg1_tile(ST_BUSH);

    p8_fill(3);
    p8_rect(2, 0, 4, 8, 6); p8_rect(3, 0, 2, 8, 7);
    upload_bg1_tile(ST_TRUNK);
}

static void bake_bg1_terrain_part2(void) {
    /* Cottage roofs & walls */
    p8_fill(15); p8_rect(0, 0, 8, 2, 6); p8_rect(0, 6, 8, 1, 1);
    upload_bg1_tile(ST_ROOF_TOP);

    p8_fill(15); p8_rect(0, 3, 8, 1, 6); p8_rect(0, 7, 8, 1, 1);
    upload_bg1_tile(ST_ROOF_BODY);

    p8_fill(8); p8_rect(0, 0, 8, 1, 6); p8_rect(0, 4, 8, 1, 7); p8_rect(0, 7, 8, 1, 6);
    upload_bg1_tile(ST_WALL_WOOD);

    p8_fill(8); p8_rect(1, 1, 6, 6, 6); p8_rect(2, 2, 4, 4, 14);
    p8_rect(3, 2, 1, 4, 6); p8_rect(2, 4, 4, 1, 6);
    upload_bg1_tile(ST_WALL_WIN);

    p8_fill(8); p8_rect(1, 0, 6, 8, 6); p8_rect(2, 1, 4, 7, 7); g_pix8[37] = 8;
    upload_bg1_tile(ST_WALL_DOOR);

    /* Windmill & hamlet props */
    p8_fill(12); p8_rect(0, 6, 8, 2, 9); p8_rect(1, 2, 6, 4, 13);
    upload_bg1_tile(ST_WINDMILL_ROOF);

    p8_fill(10); p8_rect(0, 0, 8, 1, 9); p8_rect(0, 4, 8, 1, 9);
    p8_rect(3, 1, 1, 3, 9); p8_rect(6, 5, 1, 3, 9);
    upload_bg1_tile(ST_WINDMILL_WALL);

    p8_fill(6); p8_rect(1, 1, 6, 6, 3); g_pix8[18] = 15; g_pix8[21] = 8; g_pix8[44] = 14;
    upload_bg1_tile(ST_FLOWER_BED);

    p8_fill(4); p8_rect(0, 2, 8, 2, 7); p8_rect(0, 5, 8, 2, 6);
    p8_rect(1, 1, 2, 7, 8); p8_rect(5, 1, 2, 7, 8);
    upload_bg1_tile(ST_FENCE);

    p8_fill(10); p8_rect(3, 3, 2, 5, 1); p8_rect(2, 1, 4, 3, 8); p8_rect(3, 2, 2, 1, 14);
    upload_bg1_tile(ST_LAMP);

    p8_fill(4); p8_rect(1, 1, 6, 4, 7); p8_rect(2, 2, 4, 2, 8); p8_rect(3, 5, 2, 3, 6);
    upload_bg1_tile(ST_SIGN);

    /* Ancient Shrine & Priority-1 Overhead Tiles (opaque backing) */
    p8_fill(9); p8_rect(1, 1, 6, 6, 10); p8_rect(1, 1, 5, 1, 11);
    upload_bg1_tile(ST_SHRINE_FLOOR);

    p8_fill(9); p8_rect(1, 1, 6, 6, 10); p8_rect(2, 2, 4, 4, 13); p8_rect(3, 3, 2, 2, 14);
    upload_bg1_tile(ST_SHRINE_RUNE);

    p8_fill(1); p8_rect(1, 0, 6, 8, 10); p8_rect(2, 0, 2, 8, 11); p8_rect(0, 0, 8, 2, 11);
    upload_bg1_tile(ST_SHRINE_PILLAR);

    p8_fill(4); p8_rect(0, 0, 8, 8, 2); p8_rect(1, 1, 6, 5, 3);
    p8_rect(2, 1, 4, 3, 4); p8_rect(3, 2, 2, 1, 5);
    upload_bg1_tile(ST_CANOPY);

    p8_fill(8); p8_rect(0, 2, 8, 6, 15); p8_rect(0, 7, 8, 1, 1);
    upload_bg1_tile(ST_ROOF_EAVE);

    p8_fill(9); p8_rect(0, 0, 8, 6, 10); p8_rect(0, 0, 8, 2, 11); p8_rect(0, 5, 8, 1, 1);
    upload_bg1_tile(ST_SHRINE_ARCH);

    /* Battle Arena Backdrops */
    p8_fill(13); p8_rect(0, 0, 8, 4, 12); g_pix8[51] = 14;
    upload_bg1_tile(ST_BATTLE_SKY_MEADOW);

    p8_fill(1); p8_rect(0, 4, 8, 4, 9); g_pix8[21] = 14;
    upload_bg1_tile(ST_BATTLE_SKY_SHRINE);

    p8_fill(3); p8_rect(0, 0, 8, 4, 4); g_pix8[18] = 5; g_pix8[53] = 2;
    upload_bg1_tile(ST_BATTLE_GROUND_MEADOW);

    p8_fill(9); p8_rect(0, 0, 8, 1, 11); p8_rect(0, 4, 8, 1, 1);
    upload_bg1_tile(ST_BATTLE_GROUND_SHRINE);
}

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

    /* Tiles 32..95: Opaque 5x7 ASCII font (bg=1 slate, fg=15 palette accent) */
    for (ch = 0; ch < 64u; ch++) {
        p8_fill(1);
        for (y = 0; y < 7u; y++) {
            bits = SNES_FONT_5X7[ch][y];
            for (x = 0; x < 5u; x++) {
                if (bits & (u8)(0x10u >> x)) {
                    g_pix8[(u16)(y * 8u + x + 1u)] = 15;
                }
            }
        }
        upload_bg0_tile((u16)(32u + ch));
    }
}

static void bake_obj_sprites_part1(void) {
    /* 0: SPR_HERO (Rowan) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 4); p16_rect(5, 1, 6, 2, 5);
    p16_rect(4, 4, 8, 4, 2); g_pix16[86] = 1; g_pix16[89] = 1;
    p16_rect(4, 8, 8, 2, 9); p16_rect(3, 9, 2, 3, 8);
    p16_rect(4, 10, 8, 4, 6); p16_rect(5, 10, 6, 2, 7);
    p16_rect(5, 14, 2, 2, 4); p16_rect(9, 14, 2, 2, 4);
    upload_obj_sprite16(SPR_HERO);

    /* 1: SPR_PIP */
    p16_clear();
    p16_rect(2, 4, 3, 4, 14); p16_rect(11, 4, 3, 4, 14);
    p16_rect(5, 4, 6, 6, 10); p16_rect(6, 5, 4, 4, 11);
    g_pix16[102] = 1; g_pix16[105] = 1;
    p16_rect(6, 2, 4, 2, 9);
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

    /* 4: SPR_NPC_ELDER */
    p16_clear();
    p16_rect(4, 1, 8, 3, 4); p16_rect(5, 4, 6, 3, 2);
    p16_rect(5, 7, 6, 3, 4);
    p16_rect(4, 10, 8, 5, 8); p16_rect(5, 10, 6, 4, 9);
    p16_rect(12, 4, 2, 11, 11);
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

    /* 19: Timed-Hit / Parry Target Ring */
    p16_clear();
    p16_rect(3, 1, 10, 2, 11); p16_rect(3, 13, 10, 2, 11);
    p16_rect(1, 3, 2, 10, 11); p16_rect(13, 3, 2, 10, 11);
    p16_rect(6, 6, 4, 4, 15);
    upload_obj_sprite16(SPR_TIMED_RING_SLOT);
}

/* ============================================================================
 * 6. BG1 SC_64x32 WORLD MAP & BATTLE ARENA STREAMING
 * ============================================================================ */
static u16 map_terrain_to_tile(u8 t) {
    switch (t) {
        case T_GRASS:          return ST_GRASS;
        case T_GRASS_ALT:      return ST_GRASS_ALT;
        case T_FLOWERS:        return ST_FLOWERS;
        case T_DIRT:           return ST_DIRT;
        case T_COBBLE:         return ST_COBBLE;
        case T_WATER:          return ST_WATER;
        case T_FORD:           return ST_FORD;
        case T_BRIDGE:         return ST_BRIDGE;
        case T_BRIDGE_RAIL_N:
        case T_BRIDGE_RAIL_S:  return ST_BRIDGE_RAIL;
        case T_TALL_GRASS:     return ST_TALL_GRASS;
        case T_BUSH:           return ST_BUSH;
        case T_TREE_TRUNK:     return ST_TRUNK;
        case T_ROOF_TOP:       return ST_ROOF_TOP;
        case T_ROOF_BODY:      return ST_ROOF_BODY;
        case T_WALL_WOOD:      return ST_WALL_WOOD;
        case T_WALL_WINDOW:    return ST_WALL_WIN;
        case T_WALL_DOOR:      return ST_WALL_DOOR;
        case T_WINDMILL_ROOF:  return ST_WINDMILL_ROOF;
        case T_WINDMILL_WALL:
        case T_WINDMILL_DOOR:  return ST_WINDMILL_WALL;
        case T_FLOWER_BED:     return ST_FLOWER_BED;
        case T_FENCE:          return ST_FENCE;
        case T_LANTERN_POST:   return ST_LAMP;
        case T_SIGNPOST:       return ST_SIGN;
        case T_SHRINE_FLOOR:   return ST_SHRINE_FLOOR;
        case T_SHRINE_RUNE:    return ST_SHRINE_RUNE;
        case T_SHRINE_PILLAR:  return ST_SHRINE_PILLAR;
        default:               return ST_GRASS;
    }
}

static void bg1_set_cell(u16 tx, u16 ty, u16 entry) {
    u16 x = (u16)(tx & 63u);
    u16 y = (u16)(ty & 31u);
    if (x < 32u) {
        g_bg1_map[(u16)((y << 5) + x)] = entry;
        g_bg1_dirty_l = 1;
    } else {
        g_bg1_map[(u16)(1024u + (y << 5) + (x - 32u))] = entry;
        g_bg1_dirty_r = 1;
    }
}

static void write_world_metatile(s16 mx, s16 my) {
    u16 entry, idx, tx0, tx1, ty0, ty1;
    u8 ov;

    if (mx < 0 || my < 0 || mx >= HEARTH_MAP_W || my >= HEARTH_MAP_H) {
        entry = SNES_TILE(ST_BUSH, 0u, 0u);
    } else {
        idx = (u16)((u16)my * HEARTH_MAP_W + (u16)mx);
        ov = g_hearth.map_bg2[idx];
        if (ov == O_ROOF_EAVE) {
            entry = SNES_TILE(ST_ROOF_EAVE, 0u, 1u);
        } else if (ov == O_SHRINE_ARCH) {
            entry = SNES_TILE(ST_SHRINE_ARCH, 0u, 1u);
        } else if (ov != O_NONE) {
            entry = SNES_TILE(ST_CANOPY, 0u, 1u);
        } else {
            entry = SNES_TILE(map_terrain_to_tile(g_hearth.map_bg1[idx]), 0u, 0u);
        }
    }

    tx0 = (u16)(((u16)(mx * 2)) & 63u);
    tx1 = (u16)((tx0 + 1u) & 63u);
    ty0 = (u16)(((u16)(my * 2)) & 31u);
    ty1 = (u16)((ty0 + 1u) & 31u);

    bg1_set_cell(tx0, ty0, entry);
    bg1_set_cell(tx1, ty0, entry);
    bg1_set_cell(tx0, ty1, entry);
    bg1_set_cell(tx1, ty1, entry);
}

static void build_full_world_bg1(s16 cam_x, s16 cam_y) {
    s16 mx0, my0, dy, dx;

    mx0 = (s16)((cam_x >> 4) - 2);
    my0 = (s16)((cam_y >> 4) - 1);
    for (dy = 0; dy < 16; dy++) {
        for (dx = 0; dx < 20; dx++) {
            write_world_metatile((s16)(mx0 + dx), (s16)(my0 + dy));
        }
    }
    g_last_cam_mx = (s16)(cam_x >> 4);
    g_last_cam_my = (s16)(cam_y >> 4);
}

static void stream_world_bg1(s16 cam_x, s16 cam_y) {
    s16 cmx, cmy, d;

    cmx = (s16)(cam_x >> 4);
    cmy = (s16)(cam_y >> 4);
    if (cmx == g_last_cam_mx && cmy == g_last_cam_my) return;

    if ((cmx - g_last_cam_mx) > 2 || (g_last_cam_mx - cmx) > 2 ||
        (cmy - g_last_cam_my) > 2 || (g_last_cam_my - cmy) > 2) {
        build_full_world_bg1(cam_x, cam_y);
        return;
    }

    while (g_last_cam_mx < cmx) {
        g_last_cam_mx++;
        for (d = -1; d < 15; d++) {
            write_world_metatile((s16)(g_last_cam_mx + 17), (s16)(g_last_cam_my + d));
        }
    }
    while (g_last_cam_mx > cmx) {
        g_last_cam_mx--;
        for (d = -1; d < 15; d++) {
            write_world_metatile((s16)(g_last_cam_mx - 2), (s16)(g_last_cam_my + d));
        }
    }
    while (g_last_cam_my < cmy) {
        g_last_cam_my++;
        for (d = -2; d < 18; d++) {
            write_world_metatile((s16)(g_last_cam_mx + d), (s16)(g_last_cam_my + 14));
        }
    }
    while (g_last_cam_my > cmy) {
        g_last_cam_my--;
        for (d = -2; d < 18; d++) {
            write_world_metatile((s16)(g_last_cam_mx + d), (s16)(g_last_cam_my - 1));
        }
    }
}

static void build_battle_bg1(u8 biome) {
    u16 sky_e, gnd_e, y, x;

    sky_e = SNES_TILE((biome == BIOME_SHRINE) ? ST_BATTLE_SKY_SHRINE : ST_BATTLE_SKY_MEADOW, 0u, 0u);
    gnd_e = SNES_TILE((biome == BIOME_SHRINE) ? ST_BATTLE_GROUND_SHRINE : ST_BATTLE_GROUND_MEADOW, 0u, 0u);

    for (y = 0; y < 32u; y++) {
        for (x = 0; x < 32u; x++) {
            g_bg1_map[(u16)((y << 5) + x)] = (y < 10u) ? sky_e : gnd_e;
        }
    }
    g_bg1_dirty_l = 1;
    g_bg1_dirty_r = 0;
}

/* ============================================================================
 * 7. BG0 NON-SCROLLING HUD, TITLE SCREEN, DIALOGUE & BATTLE UI
 * ============================================================================ */
static void bg0_clear_all(void) {
    hearth_memset(g_bg0_map, 0, 2048u);
    g_bg0_dirty = 1;
}

static void bg0_fill_rect(u16 x, u16 y, u16 w, u16 h, u16 entry) {
    u16 ry, rx;
    for (ry = y; ry < (u16)(y + h) && ry < 28u; ry++) {
        for (rx = x; rx < (u16)(x + w) && rx < 32u; rx++) {
            g_bg0_map[(u16)((ry << 5) + rx)] = entry;
        }
    }
    g_bg0_dirty = 1;
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
    g_bg0_dirty = 1;
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
    g_bg0_dirty = 1;
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
    char tmp[8];
    u16 n = 0;
    s32 v = val;

    if (v < 0) {
        if ((u16)(*pos + 1u) < 35u) g_line[(*pos)++] = '-';
        v = -v;
    }
    if (v == 0) {
        tmp[n++] = '0';
    } else {
        while (v > 0 && n < 7u) {
            tmp[n++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    while (n > 0 && (u16)(*pos + 1u) < 35u) {
        g_line[(*pos)++] = tmp[--n];
    }
    g_line[*pos] = '\0';
}

static void draw_title_ui(void) {
    bg0_clear_all();
    bg0_draw_box(1u, 2u, 30u, 24u);
    bg0_text(9u,  5u,  PAL_UI_GOLD, "PROJECT HEARTH");
    bg0_text(5u,  7u,  PAL_UI_CYAN, "16-BIT JRPG CORE (SNES)");
    bg0_text(3u,  10u, PAL_UI_TEXT, "DPAD : MOVE   B : DASH");
    bg0_text(3u,  12u, PAL_UI_TEXT, "A    : ACT / TIMED HIT");
    bg0_text(3u,  14u, PAL_UI_TEXT, "L / R: SWAP FAMILIAR");
    bg0_text(4u,  17u, PAL_UI_TEXT, "EXPLORE WINDMILL HAMLET");
    bg0_text(4u,  19u, PAL_UI_TEXT, "MEND HEARTS & SHRINE!");
    bg0_text(4u,  22u, PAL_UI_GOLD, "PRESS START OR A TO PLAY");
}

static void draw_explore_hud(const HearthPpuPacket *ppu) {
    u16 pos;
    const HearthPartyMember *m = &g_hearth.party[g_hearth.active_slot];

    line_reset(&pos);
    line_str(&pos, "HP:");
    line_int(&pos, m->hp);
    line_str(&pos, "/");
    line_int(&pos, m->max_hp);
    line_str(&pos, " M:");
    line_int(&pos, m->mp);
    if (ppu->interact_active && ppu->interact_label) {
        line_str(&pos, " [A]");
        line_str(&pos, ppu->interact_label);
    } else if (g_hearth.flag_guard_mended) {
        line_str(&pos, " SHRINE->");
    } else if (g_hearth.flag_has_courage) {
        line_str(&pos, " COURAGE!");
    } else {
        line_str(&pos, " HAMLET");
    }
    while (pos < 30u) g_line[pos++] = ' ';
    g_line[pos] = '\0';
    bg0_text(1u, 0u, PAL_UI_TEXT, g_line);
}

static void draw_dialogue_ui(void) {
    const HearthDialogue *d = &g_hearth.dialogue;
    u16 vis;
    u8 c;

    bg0_fill_rect(1u, 20u, 30u, 7u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
    bg0_text(2u, 20u, PAL_UI_GOLD, d->speaker ? d->speaker : "...");
    if (d->full_text) {
        vis = d->visible_chars;
        bg0_text_n(2u, 21u, PAL_UI_TEXT, d->full_text, (vis < 28u) ? vis : 28u);
        if (vis > 28u) {
            bg0_text_n(2u, 22u, PAL_UI_TEXT, d->full_text + 28,
                (u16)((vis - 28u < 28u) ? (vis - 28u) : 28u));
        }
        if (vis > 56u && d->choice_count == 0u) {
            bg0_text_n(2u, 23u, PAL_UI_TEXT, d->full_text + 56,
                (u16)((vis - 56u < 28u) ? (vis - 56u) : 28u));
        }
    }
    if (d->finished && d->choice_count > 0u) {
        for (c = 0; c < d->choice_count && c < 3u; c++) {
            u16 pal = (c == d->selected_choice) ? PAL_UI_GOLD : PAL_UI_TEXT;
            bg0_text(2u, (u16)(24u + c), pal, (c == d->selected_choice) ? ">" : " ");
            bg0_text_n(4u, (u16)(24u + c), pal, d->choices[c].label, 26u);
        }
    } else if (d->finished) {
        bg0_text(21u, 26u, PAL_UI_GOLD, "[A] NEXT");
    }
}

static void draw_battle_ui(void) {
    static const char *cmds[4] = { "STRIKE", "SKILL ", "GUARD ", "TONIC " };
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

    /* Row 20: Active Party Member */
    line_reset(&pos);
    line_str(&pos, pm->name);
    line_str(&pos, " H:");
    line_int(&pos, pm->hp);
    line_str(&pos, "/");
    line_int(&pos, pm->max_hp);
    line_str(&pos, " M:");
    line_int(&pos, pm->mp);
    bg0_text(2u, 20u, PAL_UI_TEXT, g_line);

    /* Row 21: Selected Enemy & Spell Locks */
    if (b->selected_enemy < b->enemy_count) {
        e = &b->enemies[b->selected_enemy];
        line_reset(&pos);
        line_str(&pos, e->name);
        line_str(&pos, " H:");
        line_int(&pos, e->hp);
        if (e->lock_count > 0u) {
            line_str(&pos, " L:");
            for (l = 0; l < e->lock_count; l++) {
                if (e->broken_locks[l]) line_str(&pos, ".");
                else if (e->locks[l] == LOCK_SWORD) line_str(&pos, "W");
                else if (e->locks[l] == LOCK_SUN)   line_str(&pos, "S");
                else line_str(&pos, "*");
            }
        }
        bg0_text(2u, 21u, PAL_UI_GOLD, g_line);
    }

    /* Rows 23..24: Timed-Hit prompt, Banner, or 2x2 Command Menu */
    if (b->ring.active) {
        bg0_text(4u, 23u, PAL_UI_GOLD,
            b->ring.is_parry ? ">> TAP A TO PARRY! <<" : ">> TAP A TIMED HIT! <<");
    } else if (b->banner_frames > 0u && b->banner[0] != '\0') {
        bg0_text_n(2u, 23u, PAL_UI_CYAN, b->banner, 28u);
    } else {
        for (c = 0; c < 4u; c++) {
            cx = (u16)(2u + (u16)(c & 1u) * 14u);
            cy = (u16)(23u + (u16)(c >> 1));
            pal = (c == b->selected_cmd) ? PAL_UI_GOLD : PAL_UI_TEXT;
            bg0_text(cx, cy, pal, (c == b->selected_cmd) ? ">" : " ");
            bg0_text((u16)(cx + 1u), cy, pal, cmds[c]);
        }
    }
    bg0_text(2u, 26u, PAL_UI_CYAN, "L/R:SWAP FAM  A:ACT");
}

static void update_ui_layer(const HearthPpuPacket *ppu) {
    u8 want = 0;

    if (g_hearth.mode == MODE_TITLE) {
        want = 1;
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
        if (want == 1u) {
            draw_title_ui();
        } else if (want == 0u) {
            bg0_clear_all();
            bg0_fill_rect(0u, 0u, 32u, 1u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
            bg0_fill_rect(0u, 1u, 32u, 1u, SNES_TILE(SUI_BOT,  PAL_UI_TEXT, 1u));
        } else {
            bg0_clear_all();
            bg0_draw_box(0u, 19u, 32u, 9u);
        }
        g_ui_tick = 0;
    }

    if (want == 1u) return;
    if ((g_ui_tick % 6u) != 0u) {
        g_ui_tick++;
        return;
    }
    g_ui_tick++;

    if (want == 0u)      draw_explore_hud(ppu);
    else if (want == 2u) draw_dialogue_ui();
    else                 draw_battle_ui();
}

/* ============================================================================
 * 8. SNES HARDWARE OAM SPRITE RENDERER
 * ============================================================================ */
static u8 sprite_obj_palette(u8 spr_id) {
    if (spr_id <= SPR_FAMILIAR_LUMINA) return 1u;
    if (spr_id <= SPR_NPC_GUARD || spr_id == SPR_WINDMILL_BLADES ||
        spr_id == SPR_WAYSTONE || spr_id == SPR_CHEST) return 2u;
    return 3u;
}

static void hide_oam_slot(u8 slot) {
    u16 id = (u16)((u16)slot << 2);
    oamSet(id, 255u, 224u, 0u, 0u, 0u, 0u, 0u);
    oamSetEx(id, OBJ_SMALL, OBJ_HIDE);
}

static void render_snes_sprites(const HearthPpuPacket *ppu) {
    u8 i, count, pal, prio, flip_x;
    const HearthOamSprite *o;
    s16 sx, sy;
    u16 id, tile;

    count = 0;

    if (g_hearth.mode != MODE_TITLE) {
        for (i = 0; i < ppu->oam_count && count < 48u; i++) {
            o = &ppu->oam[i];
            sx = (s16)(o->x - 8 + ppu->shake_x);
            sy = (s16)(o->y - ((o->layer == 2u) ? 8 : 16) + ppu->shake_y);

            if (sx < -15 || sx >= 256 || sy < -15 || sy >= 224) continue;
            if ((g_ui_layout == 2u || g_ui_layout == 3u) && sy >= 140) continue;
            if (g_ui_layout == 0u && sy < 10) continue;

            pal = sprite_obj_palette(o->sprite_id);
            tile = sprite_slot_to_tile(o->sprite_id);
            flip_x = (u8)((o->flags & OAM_FLAG_FLIP_X) ? 1u : 0u);
            prio = (u8)((o->layer == 2u) ? 3u : 2u);
            id = (u16)((u16)count << 2);

            oamSet(id, (u16)((u16)sx & 0x00FFu), (u16)((u16)sy & 0x00FFu),
                   prio, flip_x, 0u, tile, pal);
            oamSetEx(id, OBJ_SMALL, (u8)((sx < 0) ? OBJ_HIDE : OBJ_SHOW));
            count++;
        }

        /* Timed-Hit / Parry indicator ring sprite */
        if (g_hearth.mode == MODE_BATTLE && g_hearth.battle.ring.active && count < 50u) {
            sx = (s16)(g_hearth.battle.ring.x - 8);
            sy = (s16)(g_hearth.battle.ring.y - 24);
            if (sx >= 0 && sx < 248 && sy >= 0 && sy < 140) {
                id = (u16)((u16)count << 2);
                oamSet(id, (u16)sx, (u16)sy, 3u, 0u, 0u,
                       sprite_slot_to_tile(SPR_TIMED_RING_SLOT), 3u);
                oamSetEx(id, OBJ_SMALL, OBJ_SHOW);
                count++;
            }
        }
    }

    for (i = count; i < 52u; i++) {
        hide_oam_slot(i);
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
    g_bg1_dirty_l = 0;
    g_bg1_dirty_r = 0;
    g_bg0_dirty   = 0;
    setScreenOn();
}

static void flush_one_vblank_dma(void) {
    /* Stay well under SNES NTSC VBlank DMA budget: max 2048 bytes per frame! */
    if (g_bg1_dirty_l) {
        dmaCopyVram((u8 *)g_bg1_map, VRAM_BG1_MAP, 2048u);
        g_bg1_dirty_l = 0;
    } else if (g_bg1_dirty_r) {
        dmaCopyVram((u8 *)(g_bg1_map + 1024), (u16)(VRAM_BG1_MAP + 1024u), 2048u);
        g_bg1_dirty_r = 0;
    } else if (g_bg0_dirty) {
        dmaCopyVram((u8 *)g_bg0_map, VRAM_BG0_MAP, 1792u);
        g_bg0_dirty = 0;
    }
}

int main(void) {
    uint16_t pad;
    const HearthPpuPacket *ppu;
    u8 in_battle;
    u8 scene_switched;

    /* 1. Force Blank for full VRAM & CGRAM hardware upload */
    setScreenOff();

    upload_all_palettes();
    bake_bg1_terrain_part1();
    bake_bg1_terrain_part2();
    bake_bg0_ui_and_font();
    bake_obj_sprites_part1();
    bake_obj_sprites_part2();

    /* 2. Configure Mode 1 PPU Layers & 16x16 Hardware Sprites */
    bgSetGfxPtr(0, VRAM_BG0_GFX);
    bgSetMapPtr(0, VRAM_BG0_MAP, SC_32x32);
    bgSetGfxPtr(1, VRAM_BG1_GFX);
    bgSetMapPtr(1, VRAM_BG1_MAP, SC_64x32);
    oamInitGfxAttr(VRAM_OBJ_GFX, OBJ_SIZE16_L32);
    oamClear(0, 0);

    setMode(BG_MODE1, 0);
    bgSetDisable(2);

    /* 3. Boot Portable Engine Core in Native SNES 256x224 Mode */
    hearth_init(&g_hearth);
    g_hearth.tuning.ppu_mode = PPU_SNES_256x224;
    ppu = hearth_step_60hz(&g_hearth, 0u);
    g_last_battle = 0u;

    build_full_world_bg1(ppu->cam_x, ppu->cam_y);
    update_ui_layer(ppu);
    render_snes_sprites(ppu);

    bgSetScroll(0, 0, 0);
    bgSetScroll(1, (u16)ppu->cam_x, (u16)ppu->cam_y);

    flush_all_maps_force_blank();

    /* 4. Real 60Hz SNES Main Loop */
    while (1) {
        pad = read_snes_pad();
        ppu = hearth_step_60hz(&g_hearth, pad);

        in_battle = (u8)((g_hearth.mode == MODE_BATTLE ||
                          g_hearth.mode == MODE_BATTLE_VICTORY ||
                          g_hearth.mode == MODE_BATTLE_DEFEAT) ? 1u : 0u);
        scene_switched = 0;

        if (in_battle != g_last_battle) {
            g_last_battle = in_battle;
            scene_switched = 1;
            if (in_battle) {
                build_battle_bg1(g_hearth.battle.biome);
            } else {
                build_full_world_bg1(ppu->cam_x, ppu->cam_y);
            }
        } else if (!in_battle) {
            stream_world_bg1(ppu->cam_x, ppu->cam_y);
        }

        update_ui_layer(ppu);
        render_snes_sprites(ppu);

        if (scene_switched) {
            flush_all_maps_force_blank();
        } else {
            WaitForVBlank();
            flush_one_vblank_dma();
        }

        /* Lock BG0 HUD to (0,0) and hardware-scroll BG1 world */
        bgSetScroll(0, 0, 0);
        if (in_battle) {
            bgSetScroll(1, (u16)(-ppu->shake_x), (u16)(-ppu->shake_y));
        } else {
            bgSetScroll(1, (u16)(ppu->cam_x - ppu->shake_x), (u16)(ppu->cam_y - ppu->shake_y));
        }
    }

    return 0;
}
