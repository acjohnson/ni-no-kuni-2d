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

extern char SOUNDBANK__;

/* ============================================================================
 * 1. STATIC SNES WRAM BUFFERS (Bank $7E — zero malloc, tiny 65816 stacks!)
 * ============================================================================ */
HearthState g_hearth;

/* VRAM Word Addresses (Non-overlapping 64 KB layout: 0x0000..0x7FFF words) */
#define VRAM_OBJ_GFX      0x0000u  /* 26 16x16 sprites -> 0x0000..0x073F */
#define VRAM_BG0_GFX      0x2000u  /* UI box + 64 ASCII glyphs -> 0x2000..0x25FF */
#define VRAM_BG1_GFX      0x4000u  /* 34 terrain tiles (0..33) + 340 CG tiles (64..403 -> 0x4400..0x593F) */
#define VRAM_CG_TILE_BASE 64u      /* Non-overlapping BG1 tile slot for Cinema CG plates */
#define VRAM_BG0_MAP      0x6800u  /* 32x32 UI tilemap (1024 words = 2048 B) */
#define VRAM_BG1_MAP      0x7000u  /* 64x32 world tilemap (2048 words = 4096 B) */

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

#define SPR_TIMED_RING_SLOT  32u  /* OBJ tile 0x80 */
#define SPR_HERO_DOWN_1      33u  /* OBJ tile 0x82 */
#define SPR_HERO_UP_0        34u  /* OBJ tile 0x84 */
#define SPR_HERO_UP_1        35u  /* OBJ tile 0x86 */
#define SPR_HERO_SIDE_0      36u  /* OBJ tile 0x88 */
#define SPR_HERO_SIDE_1      37u  /* OBJ tile 0x8A */
#define SPR_BATTLE_VFX_SLOT  38u  /* OBJ tile 0x8C */

/* Tilemap shadow buffers & pixel baker scratchpads in Bank $7E WRAM */
static u16 g_bg0_map[1024];
static u16 g_bg1_map[2048];
static u16 g_world_tile[HEARTH_MAP_CELLS];
static u8  g_pix8[64];
static u8  g_pix16[256];
static u8  g_tile32[32];
static u16 g_pal16[16];
static char g_line[36];
static char g_intro_buf[128];

static u8  g_bg0_dirty        = 0;
static u8  g_bg1_dirty_l      = 0;
static u8  g_bg1_dirty_r      = 0;
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

/* Pre-baked 4bpp SNES planar tile buffers for zero-CPU VBlank environmental animation */
static u8 g_anim_grass[4][32];
static u8 g_anim_grass_alt[4][32];
static u8 g_anim_flowers[4][32];
static u8 g_anim_water[4][32];
static u8 g_anim_ford[4][32];
static u8 g_anim_tgrass[4][32];
static u8 g_anim_rune[4][32];
/* Windmill 16x16 OBJ: [frame][0..1 = top row T,T+1; 2..3 = bottom row T+16,T+17] */
static u8 g_anim_windmill[4][4][32];

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
static void encode_pix8_to_buf(u8 *dst32) {
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
        dst32[(u16)(y * 2u)]       = bp0;
        dst32[(u16)(y * 2u + 1u)]  = bp1;
        dst32[(u16)(16u + y * 2u)] = bp2;
        dst32[(u16)(17u + y * 2u)] = bp3;
    }
}

static void encode_and_upload_tile8(u16 vram_word_addr) {
    encode_pix8_to_buf(g_tile32);
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
    g_last_pal_id = ZPAL_HAMLET;
}

static void upload_zone_pal0(u8 pal_id) {
    u16 c;
    u8 pid;

    pid = (pal_id < ZPAL_COUNT) ? pal_id : 0u;
    for (c = 0u; c < 16u; c++) {
        g_pal16[c] = hearth_rgb_to_snes_bgr555(HEARTH_ZONE_PALETTES_RGB[pid][c]);
    }
    g_pal16[0] = hearth_rgb_to_snes_bgr555(HEARTH_ZONE_PALETTES_RGB[pid][1]);
    dmaCopyCGram((u8 *)g_pal16, 0u, 32u);
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
    /* 0: SPR_HERO (Rowan facing DOWN idle) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 4); p16_rect(5, 1, 6, 2, 5);
    p16_rect(4, 4, 8, 4, 2); g_pix16[86] = 1; g_pix16[89] = 1;
    p16_rect(4, 8, 8, 2, 9); p16_rect(3, 9, 2, 3, 8);
    p16_rect(3, 10, 10, 4, 6); p16_rect(5, 10, 6, 3, 7);
    g_pix16[195] = 2; g_pix16[204] = 2;
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

    /* 23: SPR_PET_MILO */
    p16_clear();
    p16_rect(3, 4, 3, 4, 4);   p16_rect(10, 4, 3, 4, 4);
    p16_rect(5, 3, 6, 5, 10);  p16_rect(6, 4, 4, 3, 11);
    g_pix16[86] = 1; g_pix16[89] = 1; g_pix16[119] = 1;
    p16_rect(5, 8, 6, 2, 9);
    p16_rect(4, 10, 8, 4, 10); p16_rect(12, 8, 2, 3, 11);
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

static void bake_obj_sprites_hero_ext(void) {
    /* 20: SPR_HERO_DOWN_1 (Rowan facing DOWN walking stride) */
    p16_clear();
    p16_rect(4, 0, 8, 4, 4); p16_rect(5, 0, 6, 2, 5);
    p16_rect(4, 3, 8, 4, 2); g_pix16[70] = 1; g_pix16[73] = 1;
    p16_rect(4, 7, 8, 2, 9); p16_rect(2, 8, 2, 3, 8);
    p16_rect(3, 9, 10, 4, 6); p16_rect(5, 9, 6, 3, 7);
    g_pix16[179] = 2; g_pix16[204] = 2;
    p16_rect(4, 13, 3, 3, 4); p16_rect(10, 13, 2, 2, 4);
    upload_obj_sprite16(SPR_HERO_DOWN_1);

    /* 21: SPR_HERO_UP_0 (Rowan facing UP idle - back of hair & red scarf) */
    p16_clear();
    p16_rect(4, 1, 8, 6, 4); p16_rect(5, 1, 6, 3, 5);
    p16_rect(4, 7, 8, 2, 9);
    p16_rect(3, 9, 10, 5, 6); p16_rect(5, 9, 6, 3, 7);
    p16_rect(6, 9, 4, 3, 8);
    p16_rect(5, 14, 2, 2, 4); p16_rect(9, 14, 2, 2, 4);
    upload_obj_sprite16(SPR_HERO_UP_0);

    /* 22: SPR_HERO_UP_1 (Rowan facing UP walking stride) */
    p16_clear();
    p16_rect(4, 0, 8, 6, 4); p16_rect(5, 0, 6, 3, 5);
    p16_rect(4, 6, 8, 2, 9);
    p16_rect(3, 8, 10, 5, 6); p16_rect(5, 8, 6, 3, 7);
    p16_rect(7, 8, 3, 4, 8);
    p16_rect(4, 13, 2, 2, 4); p16_rect(9, 13, 3, 3, 4);
    upload_obj_sprite16(SPR_HERO_UP_1);

    /* 23: SPR_HERO_SIDE_0 (Rowan facing RIGHT idle; hardware H-flipped for LEFT) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 4); p16_rect(5, 1, 6, 2, 5);
    p16_rect(4, 4, 3, 3, 4);
    p16_rect(7, 4, 5, 4, 2); g_pix16[90] = 1;
    p16_rect(4, 8, 8, 2, 9); p16_rect(2, 8, 3, 3, 8);
    p16_rect(5, 10, 6, 4, 6); p16_rect(6, 10, 4, 3, 7);
    g_pix16[202] = 2;
    p16_rect(6, 14, 4, 2, 4);
    upload_obj_sprite16(SPR_HERO_SIDE_0);

    /* 24: SPR_HERO_SIDE_1 (Rowan facing RIGHT walking stride; H-flipped for LEFT) */
    p16_clear();
    p16_rect(4, 0, 8, 4, 4); p16_rect(5, 0, 6, 2, 5);
    p16_rect(4, 3, 3, 3, 4);
    p16_rect(7, 3, 5, 4, 2); g_pix16[74] = 1;
    p16_rect(4, 7, 8, 2, 9); p16_rect(1, 7, 4, 3, 8);
    p16_rect(5, 9, 6, 4, 6); p16_rect(6, 9, 4, 3, 7);
    g_pix16[187] = 2;
    p16_rect(4, 13, 3, 3, 4); p16_rect(9, 13, 3, 3, 4);
    upload_obj_sprite16(SPR_HERO_SIDE_1);

    /* 25: SPR_BATTLE_VFX_SLOT (Solar / Starlight / Slash Choreography Burst) */
    p16_clear();
    p16_rect(7, 0, 2, 16, 11); p16_rect(0, 7, 16, 2, 11);
    p16_rect(3, 3, 10, 10, 15);
    p16_rect(5, 5, 6, 6, 12);
    p16_rect(2, 2, 2, 2, 10); p16_rect(12, 2, 2, 2, 10);
    p16_rect(2, 12, 2, 2, 10); p16_rect(12, 12, 2, 2, 10);
    upload_obj_sprite16(SPR_BATTLE_VFX_SLOT);
}

/* Pre-bake 4 animated frames for meadow grass, flowers, flowing water, ford,
 * swaying tall grass, and pulsing shrine runes into Bank $7E WRAM. */
static void bake_env_bg_frames(void) {
    static const s8 sway_dx[4] = { 0, 1, 0, -1 };
    u8 f, s1, s2, k;
    s8 dx;

    for (f = 0; f < 4u; f++) {
        dx = sway_dx[f];

        /* Sunlit meadow grass breeze */
        p8_fill(4);
        g_pix8[(u16)(8u + (u16)((2 + dx) & 7))]  = 5; g_pix8[18] = 3;
        g_pix8[(u16)(40u + (u16)((6 - dx) & 7))] = 5; g_pix8[54] = 3;
        g_pix8[33] = 3;
        encode_pix8_to_buf(g_anim_grass[f]);

        /* Alternate meadow grass breeze */
        p8_fill(4);
        g_pix8[(u16)(8u + (u16)((5 - dx) & 7))]  = 5; g_pix8[21] = 3;
        g_pix8[(u16)(40u + (u16)((2 + dx) & 7))] = 5; g_pix8[51] = 3;
        g_pix8[31] = 3;
        encode_pix8_to_buf(g_anim_grass_alt[f]);

        /* Wildflowers bobbing gently */
        p8_fill(4);
        g_pix8[(u16)(16u + (u16)((2 + dx) & 7))] = 15;
        g_pix8[(u16)(16u + (u16)((3 + dx) & 7))] = 8;
        g_pix8[26] = 3;
        g_pix8[(u16)(40u + (u16)((5 - dx) & 7))] = 14;
        g_pix8[(u16)(40u + (u16)((6 - dx) & 7))] = 8;
        g_pix8[53] = 3;
        encode_pix8_to_buf(g_anim_flowers[f]);

        /* Flowing sunlit creek water */
        p8_fill(12);
        s1 = (u8)((f << 1) & 7u);
        s2 = (u8)(((f << 1) + 4u) & 7u);
        for (k = 0; k < 3u; k++) {
            g_pix8[(u16)(8u  + ((s1 + k) & 7u))] = 13;
            g_pix8[(u16)(40u + ((s2 + k) & 7u))] = 13;
        }
        g_pix8[(u16)(16u + ((s1 + 1u) & 7u))] = 14;
        g_pix8[(u16)(16u + ((s1 + 2u) & 7u))] = 14;
        g_pix8[(u16)(48u + ((s2 + 1u) & 7u))] = 14;
        g_pix8[(u16)(48u + ((s2 + 2u) & 7u))] = 14;
        g_pix8[(u16)(24u + ((s2 + 3u) & 7u))] = 13;
        encode_pix8_to_buf(g_anim_water[f]);

        /* Shallow pebble ford shimmering current */
        p8_fill(13);
        p8_rect(1, 2, 2, 2, 10); p8_rect(5, 5, 2, 2, 11);
        g_pix8[(u16)(8u  + ((s1 + 1u) & 7u))] = 14;
        g_pix8[(u16)(8u  + ((s1 + 2u) & 7u))] = 14;
        g_pix8[(u16)(48u + ((s1 + 5u) & 7u))] = 14;
        g_pix8[(u16)(32u + ((s1 + 3u) & 7u))] = 12;
        encode_pix8_to_buf(g_anim_ford[f]);

        /* Tall wildgrass swaying tips */
        p8_fill(3);
        g_pix8[(u16)(8u  + (u16)((1 + dx) & 7))] = 5;
        g_pix8[(u16)(16u + (u16)((1 + dx) & 7))] = 5; p8_rect(1, 3, 1, 4, 4);
        g_pix8[(u16)(0u  + (u16)((3 + dx) & 7))] = 5;
        g_pix8[(u16)(8u  + (u16)((3 + dx) & 7))] = 4; p8_rect(3, 2, 1, 5, 4);
        g_pix8[(u16)(16u + (u16)((5 + dx) & 7))] = 5;
        g_pix8[(u16)(24u + (u16)((5 + dx) & 7))] = 5; p8_rect(5, 4, 1, 3, 4);
        g_pix8[(u16)(8u  + (u16)((7 + dx) & 7))] = 5;
        g_pix8[(u16)(16u + (u16)((7 + dx) & 7))] = 4; p8_rect(7, 3, 1, 4, 4);
        encode_pix8_to_buf(g_anim_tgrass[f]);

        /* Ancient Shrine pulsing arcane rune */
        p8_fill(9); p8_rect(1, 1, 6, 6, 10);
        if (f == 0u) {
            p8_rect(3, 3, 2, 2, 13);
        } else if (f == 1u || f == 3u) {
            p8_rect(2, 2, 4, 4, 13); p8_rect(3, 3, 2, 2, 14);
        } else {
            p8_rect(2, 2, 4, 4, 14); p8_rect(3, 3, 2, 2, 15);
        }
        encode_pix8_to_buf(g_anim_rune[f]);
    }
}

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
 * 6. BG1 SC_64x32 WORLD MAP & BATTLE ARENA STREAMING
 * ============================================================================ */
static const u16 TERRAIN_TILE_LUT[28] = {
    ST_GRASS,          /* 0: T_GRASS */
    ST_GRASS_ALT,      /* 1: T_GRASS_ALT */
    ST_FLOWERS,        /* 2: T_FLOWERS */
    ST_TALL_GRASS,     /* 3: T_TALL_GRASS */
    ST_DIRT,           /* 4: T_DIRT */
    ST_COBBLE,         /* 5: T_COBBLE */
    ST_WATER,          /* 6: T_WATER */
    ST_FORD,           /* 7: T_FORD */
    ST_BRIDGE,         /* 8: T_BRIDGE */
    ST_BRIDGE_RAIL,    /* 9: T_BRIDGE_RAIL_N */
    ST_BRIDGE_RAIL,    /* 10: T_BRIDGE_RAIL_S */
    ST_FENCE,          /* 11: T_FENCE */
    ST_TRUNK,          /* 12: T_TREE_TRUNK */
    ST_BUSH,           /* 13: T_BUSH */
    ST_WALL_WOOD,      /* 14: T_WALL_WOOD */
    ST_WALL_WIN,       /* 15: T_WALL_WINDOW */
    ST_WALL_DOOR,      /* 16: T_WALL_DOOR */
    ST_ROOF_BODY,      /* 17: T_ROOF_BODY */
    ST_ROOF_TOP,       /* 18: T_ROOF_TOP */
    ST_WINDMILL_WALL,  /* 19: T_WINDMILL_WALL */
    ST_WINDMILL_WALL,  /* 20: T_WINDMILL_DOOR */
    ST_WINDMILL_ROOF,  /* 21: T_WINDMILL_ROOF */
    ST_SHRINE_FLOOR,   /* 22: T_SHRINE_FLOOR */
    ST_SHRINE_RUNE,    /* 23: T_SHRINE_RUNE */
    ST_SHRINE_PILLAR,  /* 24: T_SHRINE_PILLAR */
    ST_SIGN,           /* 25: T_SIGNPOST */
    ST_LAMP,           /* 26: T_LANTERN_POST */
    ST_FLOWER_BED      /* 27: T_FLOWER_BED */
};

static void bake_world_tile_cache(void) {
    u16 idx, ov, t;
    for (idx = 0u; idx < (u16)HEARTH_MAP_CELLS; idx++) {
        ov = (u16)g_hearth.map_bg2[idx];
        if (ov == 0u) {
            t = (u16)g_hearth.map_bg1[idx];
            g_world_tile[idx] = (t < 28u) ? TERRAIN_TILE_LUT[t] : (u16)ST_GRASS;
        } else if (ov == O_ROOF_EAVE) {
            g_world_tile[idx] = SNES_TILE(ST_ROOF_EAVE, 0u, 1u);
        } else if (ov == O_SHRINE_ARCH) {
            g_world_tile[idx] = SNES_TILE(ST_SHRINE_ARCH, 0u, 1u);
        } else {
            g_world_tile[idx] = SNES_TILE(ST_CANOPY, 0u, 1u);
        }
    }
}

static void stream_world_col(s16 mx, s16 my_start) {
    u16 half_col, row_off, off, k, entry, idx, idx_valid;
    s16 my;

    half_col = (u16)(((u16)mx & 15u) << 1);
    if (((u16)mx & 16u) == 0u) {
        g_bg1_dirty_l = 1;
    } else {
        half_col = (u16)(1024u + half_col);
        g_bg1_dirty_r = 1;
    }
    row_off = (u16)(((u16)my_start & 15u) << 6);

    if (mx < 0 || mx >= HEARTH_MAP_W) {
        entry = (u16)ST_BUSH;
        for (k = 0u; k < 16u; k++) {
            off = (u16)(half_col + row_off);
            g_bg1_map[off]              = entry;
            g_bg1_map[(u16)(off + 1u)]  = entry;
            g_bg1_map[(u16)(off + 32u)] = entry;
            g_bg1_map[(u16)(off + 33u)] = entry;
            row_off = (u16)((row_off + 64u) & 1023u);
        }
        return;
    }

    my = my_start;
    idx = 0u;
    idx_valid = 0u;
    for (k = 0u; k < 16u; k++, my++) {
        if (my < 0 || my >= HEARTH_MAP_H) {
            entry = (u16)ST_BUSH;
        } else {
            if (!idx_valid) {
                idx = (u16)(((u16)my << 5) + ((u16)my << 3) + ((u16)my << 2) + (u16)mx);
                idx_valid = 1u;
            }
            entry = g_world_tile[idx];
            idx += (u16)HEARTH_MAP_W;
        }
        off = (u16)(half_col + row_off);
        g_bg1_map[off]              = entry;
        g_bg1_map[(u16)(off + 1u)]  = entry;
        g_bg1_map[(u16)(off + 32u)] = entry;
        g_bg1_map[(u16)(off + 33u)] = entry;
        row_off = (u16)((row_off + 64u) & 1023u);
    }
}

static void stream_world_row(s16 mx_start, s16 my) {
    u16 row_off, off, k, entry, idx;
    s16 mx;

    row_off = (u16)(((u16)my & 15u) << 6);
    g_bg1_dirty_l = 1;
    g_bg1_dirty_r = 1;

    mx = mx_start;
    off = (u16)(row_off + (((u16)mx & 15u) << 1));
    if (((u16)mx & 16u) != 0u) off = (u16)(off + 1024u);

    if (my < 0 || my >= HEARTH_MAP_H) {
        entry = (u16)ST_BUSH;
        for (k = 0u; k < 18u; k++) {
            g_bg1_map[off]              = entry;
            g_bg1_map[(u16)(off + 1u)]  = entry;
            g_bg1_map[(u16)(off + 32u)] = entry;
            g_bg1_map[(u16)(off + 33u)] = entry;
            off = (u16)(off + 2u);
            if ((off & 31u) == 0u) off = (u16)((off - 32u) ^ 1024u);
        }
        return;
    }

    idx = (u16)(((u16)my << 5) + ((u16)my << 3) + ((u16)my << 2));
    if (mx >= 0 && (s16)(mx + 18) <= HEARTH_MAP_W) {
        /* Fast interior path: zero per-tile bounds checks */
        idx = (u16)(idx + (u16)mx);
        for (k = 0u; k < 18u; k++) {
            entry = g_world_tile[idx++];
            g_bg1_map[off]              = entry;
            g_bg1_map[(u16)(off + 1u)]  = entry;
            g_bg1_map[(u16)(off + 32u)] = entry;
            g_bg1_map[(u16)(off + 33u)] = entry;
            off = (u16)(off + 2u);
            if ((off & 31u) == 0u) off = (u16)((off - 32u) ^ 1024u);
        }
        return;
    }

    for (k = 0u; k < 18u; k++, mx++) {
        entry = (mx < 0 || mx >= HEARTH_MAP_W) ? (u16)ST_BUSH : g_world_tile[(u16)(idx + (u16)mx)];
        g_bg1_map[off]              = entry;
        g_bg1_map[(u16)(off + 1u)]  = entry;
        g_bg1_map[(u16)(off + 32u)] = entry;
        g_bg1_map[(u16)(off + 33u)] = entry;
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
    g_last_cam_mx = (s16)(cam_x >> 4);
    g_last_cam_my = (s16)(cam_y >> 4);
}

static void stream_world_bg1(s16 cam_x, s16 cam_y) {
    s16 cmx, cmy;

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
        stream_world_col((s16)(g_last_cam_mx + 16), (s16)(g_last_cam_my - 1));
    }
    while (g_last_cam_mx > cmx) {
        g_last_cam_mx--;
        stream_world_col((s16)(g_last_cam_mx - 1), (s16)(g_last_cam_my - 1));
    }
    while (g_last_cam_my < cmy) {
        g_last_cam_my++;
        stream_world_row((s16)(g_last_cam_mx - 1), (s16)(g_last_cam_my + 14));
    }
    while (g_last_cam_my > cmy) {
        g_last_cam_my--;
        stream_world_row((s16)(g_last_cam_mx - 1), (s16)(g_last_cam_my - 1));
    }
}

static void build_battle_bg1(u8 biome) {
    u16 sky_e, gnd_e, y, x;
    u8 dark_arena;

    dark_arena = (biome == BIOME_SHRINE || biome == BIOME_SEWER || biome == BIOME_BOARHOLD) ? 1u : 0u;
    sky_e = SNES_TILE(dark_arena ? ST_BATTLE_SKY_SHRINE : ST_BATTLE_SKY_MEADOW, 0u, 0u);
    gnd_e = SNES_TILE(dark_arena ? ST_BATTLE_GROUND_SHRINE : ST_BATTLE_GROUND_MEADOW, 0u, 0u);

    for (y = 0; y < 32u; y++) {
        for (x = 0; x < 32u; x++) {
            g_bg1_map[(u16)((y << 5) + x)] = (y < 10u) ? sky_e : gnd_e;
        }
    }
    g_bg1_dirty_l = 1;
    g_bg1_dirty_r = 0;
}

static u8 *snes_cg_tiles_ptr(u8 p) {
    switch (p) {
        case 1u: return (u8 *)&cg_plate1_tiles;
        case 2u: return (u8 *)&cg_plate2_tiles;
        case 3u: return (u8 *)&cg_plate3_tiles;
        case 4u: return (u8 *)&cg_plate4_tiles;
        case 5u: return (u8 *)&cg_plate5_tiles;
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
        default: return (u8 *)&cg_plate0_pal;
    }
}

static void upload_cinema_cg_force_blank(u8 scene) {
    u8 p = (scene < CINEMA_ROM_PLATE_COUNT) ? scene : 0u;
    u16 byte_len = (u16)(CINEMA_ROM_TILE_COUNTS[p] << 5);
    dmaCopyVram(snes_cg_tiles_ptr(p),
                (u16)(VRAM_BG1_GFX + ((u16)VRAM_CG_TILE_BASE << 4)),
                byte_len);
    dmaCopyCGram(snes_cg_pal_ptr(p), 0u, 32u);
}

static void build_intro_bg1(u8 scene) {
    u8 p = (scene < CINEMA_ROM_PLATE_COUNT) ? scene : 0u;
    const u16 *mp = snes_cg_map_ptr(p);
    u16 empty_e = SNES_TILE(ST_EMPTY, 0u, 0u);
    u16 i;

    for (i = 0u; i < 1024u; i++) {
        g_bg1_map[i] = empty_e;
    }
    /* Map 32x15 pre-encoded quantized anime CG plate into BG1 rows 2..16 */
    for (i = 0u; i < CINEMA_ROM_MAP_LEN; i++) {
        g_bg1_map[(u16)(64u + i)] = mp[i];
    }
    g_bg1_dirty_l = 1;
    g_bg1_dirty_r = 0;
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
    static const char *opts[3] = {
        "NEW STORY (AGE 5->10)",
        "EXPLORE EVERTALE     ",
        "REPLAY OPENING CINEMA"
    };
    u8 i;
    u16 pal, y;

    for (i = 0; i < 3u; i++) {
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
    bg0_text(3u,  13u, PAL_UI_TEXT, "L / R: SWAP FAMILIAR");
    draw_title_cursor();
    bg0_text(4u,  23u, PAL_UI_CYAN, "DPAD:PICK  START/A:GO");
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
        if (e->lock_count > 0u) {
            line_str(&pos, " [");
            for (l = 0; l < e->lock_count; l++) {
                if (e->broken_locks[l]) line_str(&pos, ".");
                else if (e->locks[l] == LOCK_SWORD) line_str(&pos, "W");
                else if (e->locks[l] == LOCK_SUN)   line_str(&pos, "S");
                else line_str(&pos, "*");
            }
            line_str(&pos, "]");
        }
        bg0_text_n(1u, 21u, PAL_UI_GOLD, g_line, 30u);
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

    count = (sh->tab == 0u) ? 5u : (u8)HEARTH_FISH_SPECIES;
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
    u16 pos, pal;
    u8 k, fi, bi, bw, d;

    bg0_fill_rect(1u, 20u, 30u, 7u, SNES_TILE(SUI_FILL, PAL_UI_TEXT, 1u));
    if (fs->phase == FISH_PHASE_CAST || fs->phase == FISH_PHASE_WAIT_BITE) {
        bg0_text(2u, 20u, PAL_UI_GOLD, "FISHING - WAIT FOR BITE...");
        bg0_text(2u, 22u, PAL_UI_TEXT, "PRESS A ON SPLASH!");
        bg0_text(2u, 26u, PAL_UI_CYAN, "B:LEAVE PIER");
    } else if (fs->phase == FISH_PHASE_BITE_ALERT) {
        pal = ((((u16)g_hearth.frame_count) & 4u) != 0u) ? PAL_UI_GOLD : PAL_UI_CYAN;
        bg0_text(3u, 22u, pal, ">> BITE! PRESS A NOW! <<");
        bg0_text(2u, 26u, PAL_UI_TEXT, "HOLD A TO REEL ONCE HOOKED");
    } else if (fs->phase == FISH_PHASE_REELING) {
        fi = (u8)((fs->fish_pos * 20) / 100);
        bi = (u8)((fs->bar_pos * 20) / 100);
        bw = (u8)((fs->bar_half_w * 20) / 100);
        if (bw < 2u) bw = 2u;
        g_line[0] = '[';
        for (k = 0u; k < 20u; k++) {
            d = (k > bi) ? (u8)(k - bi) : (u8)(bi - k);
            if (k == fi) g_line[1u + k] = 'O';
            else if (d <= bw) g_line[1u + k] = '=';
            else g_line[1u + k] = '-';
        }
        g_line[21] = ']';
        g_line[22] = '\0';
        bg0_text(2u, 20u, PAL_UI_GOLD, "REEL! HOLD A TO RAISE ZONE");
        bg0_text(4u, 22u, PAL_UI_CYAN, g_line);
        line_reset(&pos);
        line_str(&pos, "CATCH:");
        line_int(&pos, (s16)fs->progress);
        line_str(&pos, "%  TENS:");
        line_int(&pos, (s16)fs->tension);
        line_str(&pos, "%");
        bg0_text_n(2u, 24u, PAL_UI_TEXT, g_line, 28u);
        bg0_text(2u, 26u, PAL_UI_CYAN, "KEEP O IN ===   B:RELEASE");
    } else if (fs->phase == FISH_PHASE_CAUGHT) {
        line_reset(&pos);
        line_str(&pos, "CAUGHT ");
        line_str(&pos, HEARTH_FISH_TABLE[fs->species % HEARTH_FISH_SPECIES].name);
        line_str(&pos, "!");
        bg0_text_n(2u, 21u, PAL_UI_GOLD, g_line, 28u);
        line_reset(&pos);
        line_str(&pos, "WEIGHT: ");
        line_int(&pos, (s16)fs->weight_oz);
        line_str(&pos, " OZ");
        bg0_text_n(2u, 23u, PAL_UI_TEXT, g_line, 28u);
        bg0_text(2u, 26u, PAL_UI_CYAN, "A:CONTINUE");
    } else {
        bg0_text(3u, 22u, PAL_UI_GOLD, "LINE SNAPPED! IT ESCAPED");
        bg0_text(2u, 26u, PAL_UI_CYAN, "A:CONTINUE");
    }
}

/* Native 16-bit HUD dirty signature (zero 32-bit shifts or high bit-shifts on WDC 65816) */
static u16 compute_hud_sig(u8 want, const HearthPpuPacket *ppu) {
    u16 sig = (u16)(((u16)g_hearth.mode << 4) | (u16)want);
    (void)ppu;
    if (want == 0u) {
        const HearthPartyMember *m = &g_hearth.party[g_hearth.active_slot];
        sig = (u16)(sig + (u16)m->hp + (u16)(m->mp << 5) + g_hearth.guilders);
        sig ^= (u16)((u16)g_hearth.zone_id << 8);
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
        sig ^= (u16)(((u16)fs->phase << 8) ^ ((u16)fs->progress << 1) ^ (u16)fs->fish_pos ^ ((u16)fs->bar_pos << 4));
        if (fs->phase == FISH_PHASE_BITE_ALERT && ((((u16)g_hearth.frame_count) & 4u) != 0u)) sig ^= 0x4000u;
        return sig;
    }
    if (want == 2u) {
        const HearthDialogue *d = &g_hearth.dialogue;
        sig ^= (u16)((d->visible_chars << 4) ^ ((u16)d->selected_choice << 8));
        if (d->finished) sig ^= 0x4000u;
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

    if (g_hearth.mode == MODE_INTRO) {
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
        if (want == 1u) {
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

    /* Throttle rapid typewriter updates to 15Hz; menu/HUD changes apply immediately */
    if ((want == 2u && !g_hearth.dialogue.finished) ||
        (want == 4u && g_hearth.intro.chars > 0u && g_hearth.intro.scene == g_last_intro_scene) ||
        (want == 6u && g_hearth.fishing.phase == FISH_PHASE_REELING)) {
        if (++g_ui_tick < 4u) return;
        g_ui_tick = 0;
    }
    g_last_hud_sig = sig;

    if (want == 1u)      draw_title_cursor();
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
/* Pre-combined 16-bit word: ((0x20 | (pal << 1)) << 8) | tile (32 core sprites) */
static const u16 SPR_TILE_ATTR_LUT[32] = {
    0x2200u, 0x2202u, 0x2204u, 0x2206u, /* 0..3: Hero, Pip, Bramble, Lumina (PAL1) */
    0x2408u, 0x240Au, 0x240Cu,          /* 4..6: Elder, Clara, Guard (PAL2) */
    0x260Eu, 0x2620u, 0x2622u, 0x2624u, /* 7..10: Gloomcap, Thornling, DuskMoth, Briar (PAL3) */
    0x2426u, 0x2428u, 0x242Au,          /* 11..13: Windmill, Waystone, Chest Closed (PAL2) */
    0x262Cu, 0x262Eu, 0x2640u, 0x2642u, 0x2644u, /* 14..18: Emotes + Heart Orb (PAL3) */
    0x2446u, 0x2448u, 0x244Au,          /* 19..21: Chest Open, Mouse, Pig (PAL2) */
    0x264Cu,                            /* 22: Shadow Grandma (PAL3) */
    0x224Eu,                            /* 23: Milo Puppy (PAL1) */
    0x2660u, 0x2662u, 0x2664u, 0x2666u, 0x2668u, /* 24..28: Rat, Slime, Boar, Muckmaw, Ironfang (PAL3) */
    0x246Au,                            /* 29: Hollow Oak (PAL2) */
    0x226Cu,                            /* 30: Fishing Bobber (PAL1) */
    0x246Eu                             /* 31: Alderbrook Townsfolk (PAL2) */
};

static const u16 HERO_TA_LUT[8] = {
    0x2200u, /* 0: DOWN  step 0 (slot 0,  tile 0x00) */
    0x2284u, /* 1: UP    step 0 (slot 34, tile 0x84) */
    0x6288u, /* 2: LEFT  step 0 (slot 36, tile 0x88 + H-flip) */
    0x2288u, /* 3: RIGHT step 0 (slot 36, tile 0x88) */
    0x2282u, /* 4: DOWN  step 1 (slot 33, tile 0x82) */
    0x2286u, /* 5: UP    step 1 (slot 35, tile 0x86) */
    0x628Au, /* 6: LEFT  step 1 (slot 37, tile 0x8A + H-flip) */
    0x228Au  /* 7: RIGHT step 1 (slot 37, tile 0x8A) */
};

static const u8 HI_TAIL_MASK[4] = { 0x00u, 0x54u, 0x50u, 0x40u };

static void render_snes_sprites(const HearthPpuPacket *ppu) {
    u16 i, n, count, sid, ta, flg, oidx, hi_idx, hi_byte, hi_mask, prev_cnt, end_lane;
    s16 sx, sy, shx, shy, min_y, max_y;
    u16 prog, rem, rpal;
    (void)ppu;

    count = 0u;
    oidx = 0u;
    hi_idx = 512u;
    hi_byte = 0u;
    hi_mask = 1u;

    if (g_hearth.mode != MODE_TITLE) {
        shx = (s16)g_hearth.ppu.shake_x;
        shy = (s16)g_hearth.ppu.shake_y;
        min_y = (g_ui_layout == 0u) ? 10 : -15;
        max_y = (g_ui_layout >= 2u) ? 140 : 224;
        n = (u16)g_hearth.ppu.oam_count;
        if (n > 48u) n = 48u;

        for (i = 0u; i < n; i++) {
            flg = (u16)g_hearth.ppu.oam[i].flags;
            sy = (s16)(g_hearth.ppu.oam[i].y - ((flg & OAM_FLAG_LAYER2) ? 8 : 16) + shy);
            if (sy < min_y || sy >= max_y) continue;
            sx = (s16)(g_hearth.ppu.oam[i].x - 8 + shx);
            if (sx < -15 || sx >= 256) continue;

            sid = (u16)g_hearth.ppu.oam[i].sprite_id;
            if (sid == 0u) {
                ta = HERO_TA_LUT[(flg >> 3) & 7u];
            } else {
                if (sid >= 32u) sid = 0u;
                ta = SPR_TILE_ATTR_LUT[sid];
                if (flg & OAM_FLAG_FLIP_X) ta |= 0x4000u;
            }
            if (flg & OAM_FLAG_LAYER2) ta |= 0x1000u;

            oamMemory[oidx]               = (u8)sx;
            oamMemory[(u16)(oidx + 1u)]   = (u8)sy;
            oamMemory[(u16)(oidx + 2u)]   = (u8)ta;
            oamMemory[(u16)(oidx + 3u)]   = (u8)(ta >> 8);
            oidx = (u16)(oidx + 4u);

            if (sx < 0) hi_byte |= hi_mask;
            hi_mask <<= 2;
            if (hi_mask >= 0x0100u) {
                oamMemory[hi_idx++] = (u8)hi_byte;
                hi_byte = 0u;
                hi_mask = 1u;
            }
            count++;
        }

        /* Battle Choreography VFX burst sprite (Solar Meteor / Star Bolt / Staff Arc / Guard) */
        if (g_hearth.mode == MODE_BATTLE && g_hearth.battle.vfx_kind != BVFX_NONE && count < 49u) {
            rem = (g_hearth.battle.vfx_dur > 0u) ? g_hearth.battle.vfx_dur : 1u;
            prog = (u16)(((u16)g_hearth.battle.vfx_timer << 4) / rem);
            if (prog > 16u) prog = 16u;
            sx = (s16)(g_hearth.battle.vfx_x + (((g_hearth.battle.vfx_tx - g_hearth.battle.vfx_x) * (s16)prog) >> 4) - 8);
            sy = (s16)(g_hearth.battle.vfx_y + (((g_hearth.battle.vfx_ty - g_hearth.battle.vfx_y) * (s16)prog) >> 4) - 16);
            rpal = (u16)(0x36u | ((((u16)g_hearth.frame_count) & 2u) ? 0x40u : 0x00u)
                               | ((((u16)g_hearth.frame_count) & 4u) ? 0x80u : 0x00u));
            if (sx >= 0 && sx < 248 && sy >= 0 && sy < 140) {
                oamMemory[oidx]             = (u8)sx;
                oamMemory[(u16)(oidx + 1u)] = (u8)sy;
                oamMemory[(u16)(oidx + 2u)] = 0x8Cu;
                oamMemory[(u16)(oidx + 3u)] = (u8)rpal;
                oidx = (u16)(oidx + 4u);
                hi_mask <<= 2;
                if (hi_mask >= 0x0100u) {
                    oamMemory[hi_idx++] = (u8)hi_byte;
                    hi_byte = 0u;
                    hi_mask = 1u;
                }
                count++;
            }
        }

        /* Converging & flashing Timed-Hit / Parry indicator ring sprite */
        if (g_hearth.mode == MODE_BATTLE && g_hearth.battle.ring.active && count < 50u) {
            prog = g_hearth.battle.ring.progress_fp;
            rem = (prog < 256u) ? (u16)(256u - prog) : 0u;
            rpal = (prog >= g_hearth.battle.ring.sweet_start_fp)
                ? ((((u16)g_hearth.frame_count & 2u) != 0u) ? 0x32u : 0x34u)
                : 0x36u;
            sx = (s16)(g_hearth.battle.ring.x - 8);
            sy = (s16)(g_hearth.battle.ring.y - 20 - (s16)(rem >> 4));
            if (sx >= 0 && sx < 248 && sy >= 0 && sy < 140) {
                oamMemory[oidx]             = (u8)sx;
                oamMemory[(u16)(oidx + 1u)] = (u8)sy;
                oamMemory[(u16)(oidx + 2u)] = 0x80u;
                oamMemory[(u16)(oidx + 3u)] = (u8)rpal;
                oidx = (u16)(oidx + 4u);
                hi_mask <<= 2;
                if (hi_mask >= 0x0100u) {
                    oamMemory[hi_idx++] = (u8)hi_byte;
                    hi_byte = 0u;
                    hi_mask = 1u;
                }
                count++;
            }
        }
    }

    /* Delta OAM hide: clear previously used slots (x=255, y=240, x8=1, size=0) */
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

/* ============================================================================
 * 8b. SONY SPC700 MUSIC & SOUND EFFECTS DRIVER (SNESMod Soundbank)
 * ============================================================================ */
static u16 resolve_snes_bgm_mod(u8 bgm_id) {
    if (bgm_id == BGM_ALDERBROOK)     return (u16)MOD_HEARTH_ALDERBROOK;
    if (bgm_id == BGM_CINEMA_WONDER)  return (u16)MOD_HEARTH_WONDER;
    if (bgm_id == BGM_CINEMA_LULLABY) return (u16)MOD_HEARTH_LULLABY;
    if (bgm_id == BGM_SHADOW || bgm_id == BGM_SEWERS) return (u16)MOD_HEARTH_SHADOW;
    if (bgm_id == BGM_BATTLE)         return (u16)MOD_HEARTH_BATTLE;
    if (bgm_id == BGM_BOSS || bgm_id == BGM_BOARHOLD) return (u16)MOD_HEARTH_BOSS;
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
    g_bg1_dirty_l = 0;
    g_bg1_dirty_r = 0;
    g_bg0_dirty   = 0;
    setScreenOn();
}

static void flush_env_vblank_dma(void) {
    u8 af = g_env_frame;
    u16 t = sprite_slot_to_tile(SPR_WINDMILL_BLADES);

    /* 7 BG1 tiles (224 B) + 4 Windmill OBJ tiles (128 B) = 352 bytes total! */
    dmaCopyVram(g_anim_grass[af],       (u16)(VRAM_BG1_GFX + ((u16)ST_GRASS << 4)),       32u);
    dmaCopyVram(g_anim_grass_alt[af],   (u16)(VRAM_BG1_GFX + ((u16)ST_GRASS_ALT << 4)),   32u);
    dmaCopyVram(g_anim_flowers[af],     (u16)(VRAM_BG1_GFX + ((u16)ST_FLOWERS << 4)),     32u);
    dmaCopyVram(g_anim_water[af],       (u16)(VRAM_BG1_GFX + ((u16)ST_WATER << 4)),       32u);
    dmaCopyVram(g_anim_ford[af],        (u16)(VRAM_BG1_GFX + ((u16)ST_FORD << 4)),        32u);
    dmaCopyVram(g_anim_tgrass[af],      (u16)(VRAM_BG1_GFX + ((u16)ST_TALL_GRASS << 4)),  32u);
    dmaCopyVram(g_anim_rune[af],        (u16)(VRAM_BG1_GFX + ((u16)ST_SHRINE_RUNE << 4)), 32u);
    dmaCopyVram(g_anim_windmill[af][0], (u16)(VRAM_OBJ_GFX + (t << 4)),                   64u);
    dmaCopyVram(g_anim_windmill[af][2], (u16)(VRAM_OBJ_GFX + ((t + 16u) << 4)),           64u);
    g_env_dirty = 0;
}

static void flush_one_vblank_dma(void) {
    /* Stay well under SNES NTSC VBlank DMA budget: max 2048 bytes per frame! */
    if (g_pal_dirty) {
        upload_zone_pal0(g_last_pal_id);
        g_pal_dirty = 0u;
    }
    if (g_bg1_dirty_l) {
        dmaCopyVram((u8 *)g_bg1_map, VRAM_BG1_MAP, 2048u);
        g_bg1_dirty_l = 0;
    } else if (g_bg1_dirty_r) {
        dmaCopyVram((u8 *)(g_bg1_map + 1024), (u16)(VRAM_BG1_MAP + 1024u), 2048u);
        g_bg1_dirty_r = 0;
    } else {
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

int main(void) {
    uint16_t pad;
    const HearthPpuPacket *ppu;
    u8 scene_kind;
    u8 scene_switched;
    u8 i, af;
    u16 want_mod;

    /* 1. Boot Sony SPC700 Sound Engine & register soundbank */
    spcBoot();
    spcSetBank((u8 *)&SOUNDBANK__);

    /* 2. Force Blank for full VRAM & CGRAM hardware upload */
    setScreenOff();

    upload_all_palettes();
    bake_bg1_terrain_part1();
    bake_bg1_terrain_part2();
    bake_bg0_ui_and_font();
    bake_obj_sprites_part1();
    bake_obj_sprites_part2();
    bake_obj_sprites_part3();
    bake_obj_sprites_part4();
    bake_obj_sprites_hero_ext();
    bake_env_bg_frames();
    bake_env_windmill_frames();

    /* 3. Configure Mode 1 PPU Layers & 16x16 Hardware Sprites */
    bgSetGfxPtr(0, VRAM_BG0_GFX);
    bgSetMapPtr(0, VRAM_BG0_MAP, SC_32x32);
    bgSetGfxPtr(1, VRAM_BG1_GFX);
    bgSetMapPtr(1, VRAM_BG1_MAP, SC_64x32);
    oamInitGfxAttr(VRAM_OBJ_GFX, OBJ_SIZE16_L32);
    oamClear(0, 0);

    setMode(BG_MODE1, 0);
    bgSetDisable(2);

    /* 4. Boot Portable Engine Core in Native SNES 256x224 Mode */
    hearth_init(&g_hearth);
    bake_world_tile_cache();
    g_last_map_rev = g_hearth.map_revision;
    g_hearth.tuning.ppu_mode = PPU_SNES_256x224;
    ppu = hearth_step_60hz(&g_hearth, 0u);
    g_last_pal_id = ppu->terrain_pal_id;
    upload_zone_pal0(g_last_pal_id);
    g_prev_oam_cnt = 0u;
    g_env_frame = 0u;

    if (g_hearth.mode == MODE_INTRO) {
        g_last_scene_kind = 2u;
        g_last_intro_scene = g_hearth.intro.scene;
        upload_cinema_cg_force_blank(g_hearth.intro.scene);
        build_intro_bg1(g_hearth.intro.scene);
        bgSetScroll(1, 0u, 0u);
    } else {
        g_last_scene_kind = 0u;
        g_last_intro_scene = 255u;
        build_full_world_bg1(ppu->cam_x, ppu->cam_y);
        bgSetScroll(1, (u16)ppu->cam_x, (u16)ppu->cam_y);
    }
    update_ui_layer(ppu);
    render_snes_sprites(ppu);

    bgSetScroll(0, 0, 0);

    /* Load Alderbrook Overture ("Steam & Pine") + 6 gameplay SFX before revealing display */
    load_snes_music(resolve_snes_bgm_mod(ppu->bgm_id));
    flush_all_maps_force_blank();

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
            setScreenOff();
            if (scene_kind == 2u) {
                upload_cinema_cg_force_blank(g_hearth.intro.scene);
                g_pal_dirty = 0u;
            } else if (g_pal_dirty) {
                upload_zone_pal0(g_last_pal_id);
                g_pal_dirty = 0u;
            }
            load_snes_music(want_mod);
            dmaCopyVram((u8 *)g_bg1_map, VRAM_BG1_MAP, 4096u);
            dmaCopyVram((u8 *)g_bg0_map, VRAM_BG0_MAP, 2048u);
            g_bg1_dirty_l = 0;
            g_bg1_dirty_r = 0;
            g_bg0_dirty   = 0;
            g_env_dirty   = 0;
            setScreenOn();
        } else {
            if (want_mod != g_cur_bgm_mod) {
                load_snes_music(want_mod);
            }
            spcProcess();
            WaitForVBlank();
            flush_one_vblank_dma();
        }

        /* Hardware-scroll BG1 world (BG0 locked to 0,0 at init) */
        if (scene_kind == 2u) {
            bgSetScroll(1, (u16)(-ppu->shake_x), (u16)(-ppu->shake_y));
        } else if (scene_kind == 1u) {
            bgSetScroll(1, (u16)(-ppu->shake_x), (u16)(-ppu->shake_y));
        } else {
            bgSetScroll(1, (u16)(ppu->cam_x - ppu->shake_x), (u16)(ppu->cam_y - ppu->shake_y));
        }
    }

    return 0;
}
