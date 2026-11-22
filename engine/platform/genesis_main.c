/* ============================================================================
 * genesis_main.c — Project Hearth Sega Genesis / Mega Drive Host (SGDK v2.11)
 *
 * Full playable 16-bit Sega Genesis VDP + Hardware SAT + SN76489 PSG host:
 *   - Procedural 4bpp VRAM terrain metatiles (BG_A ground + BG_B canopies)
 *   - Procedural 4bpp 16x16 hardware sprites (column-major 2x2 SAT entries)
 *   - True 64x64 hardware plane scrolling with incremental tile streaming
 *   - Non-scrolling Hardware WINDOW plane for Title, HUD, Dialogue & Battle UI
 *   - Decaying 4-channel SN76489 PSG driver (Pastoral/Battle BGM + crisp SFX)
 *
 * Build via top-level Makefile (implicit Docker SGDK v2.11):
 *   make genesis   -> build/genesis/hearth.bin (and out/rom.bin)
 * ============================================================================ */

#include <genesis.h>
#include "hearth_core.h"
#include "cinema_cg_rom.h"

/* Placed statically in Sega Genesis 64KB 68000 Work RAM (zero malloc!) */
static HearthState g_hearth;

/* ============================================================================
 * VRAM TILE INDEX ALLOCATION (All well below TILE_FONT_INDEX)
 * ============================================================================ */
#define VRAM_BG_BASE    ((u16)(TILE_USER_INDEX))
#define VRAM_UI_BASE    ((u16)(VRAM_BG_BASE + 64u))
#define VRAM_SPR_BASE   ((u16)(VRAM_UI_BASE + 16u))
#define VRAM_FONT_BASE  ((u16)(VRAM_SPR_BASE + 184u))
#define VRAM_CG_BASE    ((u16)(VRAM_FONT_BASE + 80u))

/* BG Tile IDs (offsets from VRAM_BG_BASE — 55 tiles total, < 64) */
enum {
    GT_EMPTY = 0,
    GT_GRASS,
    GT_GRASS_ALT,
    GT_FLOWERS,
    GT_DIRT,
    GT_COBBLE,
    GT_WATER,
    GT_FORD,
    GT_BRIDGE,
    GT_BRIDGE_RAIL,
    GT_TALL_GRASS,
    GT_BUSH,
    GT_TRUNK,
    GT_ROOF_TOP,
    GT_ROOF_BODY,
    GT_WALL_WOOD,
    GT_WALL_WIN,
    GT_WALL_DOOR,
    GT_WINDMILL_ROOF,
    GT_WINDMILL_WALL,
    GT_FLOWER_BED,
    GT_FENCE,
    GT_LAMP,
    GT_SIGN,
    GT_SHRINE_FLOOR,
    GT_SHRINE_RUNE,
    GT_SHRINE_PILLAR,
    GT_CANOPY,
    GT_ROOF_EAVE,
    GT_SHRINE_ARCH,
    GT_BATTLE_SKY_MEADOW,
    GT_BATTLE_SKY_SHRINE,
    GT_BATTLE_GROUND_MEADOW,
    GT_BATTLE_GROUND_SHRINE,
    /* Milestone 2: Ground-Up Autumn Mountain & Cottage Interior Tiles */
    GT_MTN_SKY,
    GT_CLIFF_WALL,
    GT_CLIFF_LEDGE,
    GT_BOULDER,
    GT_AUTUMN_GRASS,
    GT_ROCKY_PATH,
    GT_RAPIDS,
    GT_RAIL_TRACK,
    GT_MAPLE_TRUNK,
    GT_PINE_TRUNK,
    GT_WOOD_FLOOR,
    GT_HEARTH_RUG,
    GT_FIREPLACE,
    GT_BED_QUILT,
    GT_ATTIC_CHEST,
    GT_HOLLOW_OAK,
    GT_STONE_STAIRS,
    GT_INTERIOR_WALL,
    GT_MAPLE_CANOPY,
    GT_PINE_TOP,
    GT_OAK_CROWN,
    GT_COUNT
};

/* UI Box Tile IDs (offsets from VRAM_UI_BASE) */
enum {
    GUI_FILL = 0,
    GUI_TOP,
    GUI_BOT,
    GUI_LEFT,
    GUI_RIGHT,
    GUI_TL,
    GUI_TR,
    GUI_BL,
    GUI_BR
};

/* Sprite VRAM Slots (37 core sprites + 7 host internal slots = 44 x 4 = 176 tiles) */
#define SPR_TIMED_RING_SLOT  37u
#define SPR_HERO_DOWN_1      38u
#define SPR_HERO_UP_0        39u
#define SPR_HERO_UP_1        40u
#define SPR_HERO_SIDE_0      41u
#define SPR_HERO_SIDE_1      42u
#define SPR_BATTLE_VFX_SLOT  43u

/* ============================================================================
 * 1. COMPACT 5x7 UPPERCASE ASCII FONT (ASCII 32..95) -> OPAQUE 4bpp VRAM
 * ============================================================================ */
static const u8 GEN_FONT_5X7[64][7] = {
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
 * 2. 4bpp PACKED GENESIS TILE & 16x16 SPRITE BAKER
 * ============================================================================ */
static u8 g_pix8[64];
static u8 g_pix16[256];

static void pack_pix8_to_buf(u32 dst[8]) {
    for (u16 y = 0; y < 8; y++) {
        u32 row = 0;
        for (u16 x = 0; x < 8; x++) {
            row = (row << 4) | (u32)(g_pix8[y * 8 + x] & 0x0Fu);
        }
        dst[y] = row;
    }
}

static void upload_pix8_to_vram(u16 tile_idx) {
    u32 tile[8];
    pack_pix8_to_buf(tile);
    VDP_loadTileData(tile, tile_idx, 1, CPU);
}

/* Genesis 2x2 (16x16) hardware sprites are COLUMN-MAJOR: TL, BL, TR, BR */
static void pack_pix16_sprite_to_buf(u32 dst[32]) {
    static const u8 ox[4] = { 0, 0, 8, 8 };
    static const u8 oy[4] = { 0, 8, 0, 8 };
    for (u16 t = 0; t < 4; t++) {
        for (u16 y = 0; y < 8; y++) {
            u32 row = 0;
            for (u16 x = 0; x < 8; x++) {
                u8 c = g_pix16[(oy[t] + y) * 16 + (ox[t] + x)] & 0x0Fu;
                row = (row << 4) | (u32)c;
            }
            dst[t * 8u + y] = row;
        }
    }
}

static void upload_pix16_sprite_to_vram(u16 base_tile) {
    u32 tiles[32];
    pack_pix16_sprite_to_buf(tiles);
    VDP_loadTileData(tiles, base_tile, 4, CPU);
}

/* Persistent RAM buffers for tear-free VBlank DMA_QUEUE environmental animations */
static u32 g_anim_grass[4][8];
static u32 g_anim_grass_alt[4][8];
static u32 g_anim_flowers[4][8];
static u32 g_anim_water[4][8];
static u32 g_anim_ford[4][8];
static u32 g_anim_tgrass[4][8];
static u32 g_anim_rune[4][8];
static u32 g_anim_rapids[4][8];
static u32 g_anim_windmill[4][32];
static u8  g_last_env_frame = 255;

static void p8_fill(u8 col) {
    hearth_memset(g_pix8, col, 64);
}

static void p8_rect(u8 x, u8 y, u8 w, u8 h, u8 col) {
    for (u8 py = y; py < y + h && py < 8; py++) {
        for (u8 px = x; px < x + w && px < 8; px++) {
            g_pix8[py * 8 + px] = col;
        }
    }
}

static void p16_clear(void) {
    hearth_memset(g_pix16, 0, 256);
}

static void p16_rect(u8 x, u8 y, u8 w, u8 h, u8 col) {
    for (u8 py = y; py < y + h && py < 16; py++) {
        for (u8 px = x; px < x + w && px < 16; px++) {
            g_pix16[py * 16 + px] = col;
        }
    }
}

static void bake_bg_and_ui_tiles(void) {
    /* 0: Transparent */
    p8_fill(0); upload_pix8_to_vram(VRAM_BG_BASE + GT_EMPTY);

    /* 1: Sunlit meadow grass */
    p8_fill(4);
    g_pix8[1*8+2] = 5; g_pix8[2*8+2] = 3;
    g_pix8[5*8+6] = 5; g_pix8[6*8+6] = 3;
    g_pix8[4*8+1] = 3;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_GRASS);

    /* 2: Alternate meadow grass */
    p8_fill(4);
    g_pix8[1*8+5] = 3; g_pix8[2*8+5] = 5;
    g_pix8[5*8+2] = 3; g_pix8[6*8+3] = 5;
    g_pix8[3*8+7] = 3;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_GRASS_ALT);

    /* 3: Wildflowers */
    p8_fill(4);
    g_pix8[2*8+2] = 15; g_pix8[2*8+3] = 8; g_pix8[3*8+2] = 3;
    g_pix8[5*8+5] = 14; g_pix8[5*8+6] = 8; g_pix8[6*8+5] = 3;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_FLOWERS);

    /* 4: Dirt road */
    p8_fill(7);
    g_pix8[1*8+1] = 8; g_pix8[2*8+5] = 6; g_pix8[5*8+3] = 8; g_pix8[6*8+6] = 6;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_DIRT);

    /* 5: Hamlet cobblestone */
    p8_fill(9);
    p8_rect(0, 0, 7, 3, 10); p8_rect(1, 0, 5, 1, 11);
    p8_rect(0, 4, 3, 3, 10); p8_rect(4, 4, 4, 3, 10); p8_rect(5, 4, 2, 1, 11);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_COBBLE);

    /* 6: Sparkling creek water */
    p8_fill(12);
    p8_rect(1, 1, 3, 1, 13); p8_rect(2, 2, 2, 1, 14);
    p8_rect(4, 5, 3, 1, 13); p8_rect(5, 6, 2, 1, 14);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_WATER);

    /* 7: Shallow pebble ford */
    p8_fill(13);
    p8_rect(1, 2, 2, 2, 10); p8_rect(5, 5, 2, 2, 11);
    g_pix8[1*8+6] = 14; g_pix8[6*8+2] = 14;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_FORD);

    /* 8: Timber bridge deck */
    p8_fill(7);
    p8_rect(0, 0, 8, 1, 6); p8_rect(0, 4, 8, 1, 6);
    p8_rect(0, 1, 8, 1, 8); p8_rect(0, 5, 8, 1, 8);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BRIDGE);

    /* 9: Bridge rail */
    p8_fill(12);
    p8_rect(0, 1, 8, 2, 6); p8_rect(0, 2, 8, 1, 8);
    p8_rect(1, 0, 2, 6, 6); p8_rect(5, 0, 2, 6, 6);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BRIDGE_RAIL);

    /* 10: Tall wildgrass */
    p8_fill(3);
    p8_rect(1, 1, 1, 6, 5); p8_rect(3, 0, 1, 7, 4);
    p8_rect(5, 2, 1, 5, 5); p8_rect(7, 1, 1, 6, 4);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_TALL_GRASS);

    /* 11: Forest bush border */
    p8_fill(2);
    p8_rect(1, 1, 6, 6, 3); p8_rect(2, 1, 4, 3, 4); p8_rect(2, 1, 2, 1, 5);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BUSH);

    /* 12: Tree trunk */
    p8_fill(3);
    p8_rect(2, 0, 4, 8, 6); p8_rect(3, 0, 2, 8, 7);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_TRUNK);

    /* 13-14: Cottage roof */
    p8_fill(15); p8_rect(0, 0, 8, 2, 6); p8_rect(0, 6, 8, 1, 1);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_ROOF_TOP);
    p8_fill(15); p8_rect(0, 3, 8, 1, 6); p8_rect(0, 7, 8, 1, 1);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_ROOF_BODY);

    /* 15-17: Cottage wall, window, door */
    p8_fill(8); p8_rect(0, 0, 8, 1, 6); p8_rect(0, 4, 8, 1, 7); p8_rect(0, 7, 8, 1, 6);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_WALL_WOOD);
    p8_fill(8); p8_rect(1, 1, 6, 6, 6); p8_rect(2, 2, 4, 4, 14); p8_rect(3, 2, 1, 4, 6); p8_rect(2, 4, 4, 1, 6);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_WALL_WIN);
    p8_fill(8); p8_rect(1, 0, 6, 8, 6); p8_rect(2, 1, 4, 7, 7); g_pix8[4*8+5] = 8;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_WALL_DOOR);

    /* 18-19: Stone windmill */
    p8_fill(12); p8_rect(0, 6, 8, 2, 9); p8_rect(1, 2, 6, 4, 13);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_WINDMILL_ROOF);
    p8_fill(10); p8_rect(0, 0, 8, 1, 9); p8_rect(0, 4, 8, 1, 9); p8_rect(3, 1, 1, 3, 9); p8_rect(6, 5, 1, 3, 9);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_WINDMILL_WALL);

    /* 20-23: Props (Flower bed, fence, lantern, sign) */
    p8_fill(6); p8_rect(1, 1, 6, 6, 3); g_pix8[2*8+2] = 15; g_pix8[2*8+5] = 8; g_pix8[5*8+4] = 14;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_FLOWER_BED);
    p8_fill(4); p8_rect(0, 2, 8, 2, 7); p8_rect(0, 5, 8, 2, 6); p8_rect(1, 1, 2, 7, 8); p8_rect(5, 1, 2, 7, 8);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_FENCE);
    p8_fill(10); p8_rect(3, 3, 2, 5, 1); p8_rect(2, 1, 4, 3, 8); p8_rect(3, 2, 2, 1, 14);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_LAMP);
    p8_fill(4); p8_rect(1, 1, 6, 4, 7); p8_rect(2, 2, 4, 2, 8); p8_rect(3, 5, 2, 3, 6);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_SIGN);

    /* 24-26: Ancient Shrine */
    p8_fill(9); p8_rect(1, 1, 6, 6, 10); p8_rect(1, 1, 5, 1, 11);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_SHRINE_FLOOR);
    p8_fill(9); p8_rect(1, 1, 6, 6, 10); p8_rect(2, 2, 4, 4, 13); p8_rect(3, 3, 2, 2, 14);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_SHRINE_RUNE);
    p8_fill(1); p8_rect(1, 0, 6, 8, 10); p8_rect(2, 0, 2, 8, 11); p8_rect(0, 0, 8, 2, 11);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_SHRINE_PILLAR);

    /* 27-29: Overhead BG_B tiles */
    p8_fill(0); p8_rect(1, 1, 6, 6, 2); p8_rect(1, 1, 6, 5, 3); p8_rect(2, 1, 4, 3, 4); p8_rect(3, 2, 2, 1, 5);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_CANOPY);
    p8_fill(0); p8_rect(0, 4, 8, 4, 15); p8_rect(0, 7, 8, 1, 1);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_ROOF_EAVE);
    p8_fill(0); p8_rect(0, 0, 8, 6, 10); p8_rect(0, 0, 8, 2, 11); p8_rect(0, 5, 8, 1, 1);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_SHRINE_ARCH);

    /* 30-33: Battle Arena Backdrop */
    p8_fill(13); p8_rect(0, 0, 8, 4, 12); g_pix8[6*8+3] = 14;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BATTLE_SKY_MEADOW);
    p8_fill(1); p8_rect(0, 4, 8, 4, 9); g_pix8[2*8+5] = 14;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BATTLE_SKY_SHRINE);
    p8_fill(3); p8_rect(0, 0, 8, 4, 4); g_pix8[2*8+2] = 5; g_pix8[6*8+5] = 2;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BATTLE_GROUND_MEADOW);
    p8_fill(9); p8_rect(0, 0, 8, 1, 11); p8_rect(0, 4, 8, 1, 1);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BATTLE_GROUND_SHRINE);

    /* 34..54: Ground-Up Autumn Mountain & Cottage Interior Tiles */
    /* GT_MTN_SKY: Distant Snow-Dusted Alpine Peaks & Horizon Sky */
    p8_fill(13); p8_rect(0, 0, 8, 2, 12);
    p8_rect(2, 2, 4, 2, 14); p8_rect(3, 1, 2, 1, 14); /* Snow cap */
    p8_rect(1, 4, 6, 4, 9);  p8_rect(0, 6, 8, 2, 2);  /* Granite peak + autumn ridge */
    upload_pix8_to_vram(VRAM_BG_BASE + GT_MTN_SKY);

    /* GT_CLIFF_WALL & GT_CLIFF_LEDGE */
    p8_fill(9); p8_rect(0, 0, 7, 3, 10); p8_rect(1, 0, 5, 1, 11);
    p8_rect(0, 3, 8, 1, 1); p8_rect(1, 4, 6, 3, 10); p8_rect(0, 7, 8, 1, 1);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_CLIFF_WALL);
    p8_fill(9); p8_rect(0, 0, 8, 3, 3); p8_rect(1, 0, 6, 2, 4); p8_rect(2, 0, 3, 1, 5);
    p8_rect(0, 3, 8, 2, 11); p8_rect(0, 5, 8, 1, 1); p8_rect(1, 6, 6, 2, 10);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_CLIFF_LEDGE);

    /* GT_BOULDER, GT_AUTUMN_GRASS, GT_ROCKY_PATH */
    p8_fill(4); p8_rect(1, 2, 6, 5, 9); p8_rect(2, 2, 4, 4, 10); p8_rect(2, 2, 3, 2, 11); g_pix8[2*8+2] = 3;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BOULDER);
    p8_fill(4); g_pix8[1*8+2] = 5; g_pix8[5*8+6] = 3; g_pix8[2*8+5] = 15; g_pix8[6*8+2] = 8; g_pix8[4*8+3] = 15;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_AUTUMN_GRASS);
    p8_fill(7); p8_rect(1, 1, 3, 2, 10); g_pix8[1*8+1] = 11; p8_rect(4, 5, 3, 2, 10); g_pix8[5*8+5] = 11;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_ROCKY_PATH);

    /* GT_RAPIDS & GT_RAIL_TRACK */
    p8_fill(12); p8_rect(1, 1, 4, 1, 13); p8_rect(2, 2, 3, 1, 14); p8_rect(3, 5, 4, 1, 14);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_RAPIDS);
    p8_fill(9); p8_rect(1, 0, 2, 8, 6); p8_rect(5, 0, 2, 8, 6);
    p8_rect(0, 1, 8, 2, 1); p8_rect(0, 1, 8, 1, 11);
    p8_rect(0, 5, 8, 2, 1); p8_rect(0, 5, 8, 1, 11);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_RAIL_TRACK);

    /* GT_MAPLE_TRUNK & GT_PINE_TRUNK */
    p8_fill(4); p8_rect(2, 0, 4, 8, 6); p8_rect(3, 0, 2, 7, 7); g_pix8[6*8+1] = 15; g_pix8[7*8+6] = 8;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_MAPLE_TRUNK);
    p8_fill(3); p8_rect(3, 4, 2, 4, 6); p8_rect(1, 0, 6, 5, 2); p8_rect(2, 1, 4, 3, 3);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_PINE_TRUNK);

    /* GT_WOOD_FLOOR, GT_HEARTH_RUG, GT_FIREPLACE, GT_BED_QUILT, GT_ATTIC_CHEST */
    p8_fill(7); p8_rect(0, 3, 8, 1, 6); p8_rect(0, 7, 8, 1, 6); p8_rect(1, 1, 4, 1, 8); p8_rect(4, 5, 3, 1, 8);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_WOOD_FLOOR);
    p8_fill(15); p8_rect(1, 1, 6, 6, 8); p8_rect(2, 2, 4, 4, 15); p8_rect(3, 3, 2, 2, 14);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_HEARTH_RUG);
    p8_fill(9); p8_rect(0, 0, 8, 2, 11); p8_rect(2, 2, 4, 6, 1); p8_rect(2, 4, 4, 3, 15); p8_rect(3, 5, 2, 2, 8);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_FIREPLACE);
    p8_fill(6); p8_rect(1, 1, 6, 2, 14); p8_rect(1, 3, 3, 2, 15); p8_rect(4, 3, 3, 2, 13); p8_rect(1, 5, 6, 2, 8);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_BED_QUILT);
    p8_fill(7); p8_rect(1, 1, 6, 6, 6); p8_rect(2, 2, 4, 2, 15); p8_rect(2, 4, 4, 2, 8); g_pix8[4*8+3] = 14;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_ATTIC_CHEST);

    /* GT_HOLLOW_OAK, GT_STONE_STAIRS, GT_INTERIOR_WALL */
    p8_fill(6); p8_rect(1, 0, 6, 8, 7); p8_rect(2, 2, 4, 6, 1); p8_rect(3, 3, 2, 4, 14);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_HOLLOW_OAK);
    p8_fill(9); p8_rect(0, 0, 8, 1, 11); p8_rect(0, 1, 8, 2, 10); p8_rect(0, 3, 8, 1, 1);
    p8_rect(0, 4, 8, 1, 11); p8_rect(0, 5, 8, 2, 10); p8_rect(0, 7, 8, 1, 1);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_STONE_STAIRS);
    p8_fill(8); p8_rect(0, 0, 8, 1, 6); p8_rect(0, 6, 8, 2, 6); p8_rect(3, 1, 2, 5, 7);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_INTERIOR_WALL);

    /* BG_B Overhead Autumn Canopies: GT_MAPLE_CANOPY, GT_PINE_TOP, GT_OAK_CROWN */
    p8_fill(0); p8_rect(1, 1, 6, 6, 6); p8_rect(1, 1, 6, 5, 15); p8_rect(2, 2, 4, 3, 8); g_pix8[2*8+3] = 14;
    upload_pix8_to_vram(VRAM_BG_BASE + GT_MAPLE_CANOPY);
    p8_fill(0); p8_rect(3, 0, 2, 2, 3); p8_rect(2, 2, 4, 3, 2); p8_rect(1, 5, 6, 3, 2); p8_rect(2, 5, 3, 2, 3);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_PINE_TOP);
    p8_fill(0); p8_rect(0, 1, 8, 6, 6); p8_rect(1, 1, 6, 5, 15); p8_rect(2, 2, 4, 3, 8); p8_rect(3, 2, 2, 2, 14);
    upload_pix8_to_vram(VRAM_BG_BASE + GT_OAK_CROWN);

    /* UI Box Tiles (PAL2: 1=dark slate, 7=gold, 15=warm cream) */
    p8_fill(1); upload_pix8_to_vram(VRAM_UI_BASE + GUI_FILL);
    p8_fill(1); p8_rect(0, 0, 8, 1, 7); p8_rect(0, 1, 8, 1, 15); upload_pix8_to_vram(VRAM_UI_BASE + GUI_TOP);
    p8_fill(1); p8_rect(0, 7, 8, 1, 7); p8_rect(0, 6, 8, 1, 15); upload_pix8_to_vram(VRAM_UI_BASE + GUI_BOT);
    p8_fill(1); p8_rect(0, 0, 1, 8, 7); p8_rect(1, 0, 1, 8, 15); upload_pix8_to_vram(VRAM_UI_BASE + GUI_LEFT);
    p8_fill(1); p8_rect(7, 0, 1, 8, 7); p8_rect(6, 0, 1, 8, 15); upload_pix8_to_vram(VRAM_UI_BASE + GUI_RIGHT);
    p8_fill(1); p8_rect(0, 0, 8, 2, 7); p8_rect(0, 0, 2, 8, 7); p8_rect(1, 1, 7, 1, 15); p8_rect(1, 1, 1, 7, 15);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_TL);
    p8_fill(1); p8_rect(0, 0, 8, 2, 7); p8_rect(6, 0, 2, 8, 7); p8_rect(0, 1, 7, 1, 15); p8_rect(6, 1, 1, 7, 15);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_TR);
    p8_fill(1); p8_rect(0, 6, 8, 2, 7); p8_rect(0, 0, 2, 8, 7); p8_rect(1, 6, 7, 1, 15); p8_rect(1, 0, 1, 7, 15);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_BL);
    p8_fill(1); p8_rect(0, 6, 8, 2, 7); p8_rect(6, 0, 2, 8, 7); p8_rect(0, 6, 7, 1, 15); p8_rect(6, 0, 1, 7, 15);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_BR);

    /* Opaque 8x8 ASCII Font (bg=1 dark slate, fg=15 warm cream in PAL2, or fg=10/11 gold in PAL1) */
    for (u16 ch = 0; ch < 64; ch++) {
        p8_fill(1);
        for (u8 y = 0; y < 7; y++) {
            u8 bits = GEN_FONT_5X7[ch][y];
            for (u8 x = 0; x < 5; x++) {
                if (bits & (1u << (4 - x))) {
                    g_pix8[y * 8 + (x + 1)] = 15;
                }
            }
        }
        upload_pix8_to_vram(VRAM_FONT_BASE + ch);
    }
}

static void bake_all_sprites(void) {
    /* 0: SPR_HERO (Rowan facing DOWN idle - PAL1: forest tunic, red scarf, chestnut hair) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 4); p16_rect(5, 1, 6, 2, 5);      /* Hair */
    p16_rect(4, 4, 8, 4, 2); g_pix16[5*16+6]=1; g_pix16[5*16+9]=1; /* Face & eyes */
    p16_rect(4, 8, 8, 2, 9); p16_rect(3, 9, 2, 3, 8);      /* Red scarf */
    p16_rect(3, 10, 10, 4, 6); p16_rect(5, 10, 6, 3, 7);   /* Green tunic + sleeves */
    g_pix16[12*16+3]=2; g_pix16[12*16+12]=2;               /* Hands */
    p16_rect(5, 14, 2, 2, 4); p16_rect(9, 14, 2, 2, 4);    /* Boots */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_HERO * 4);

    /* 1: SPR_PIP (Hovering golden sprite companion - PAL1) */
    p16_clear();
    p16_rect(2, 4, 3, 4, 14); p16_rect(11, 4, 3, 4, 14);   /* Wings */
    p16_rect(5, 4, 6, 6, 10); p16_rect(6, 5, 4, 4, 11);    /* Body */
    g_pix16[6*16+6]=1; g_pix16[6*16+9]=1;                  /* Eyes */
    p16_rect(6, 2, 4, 2, 9);                               /* Cap */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_PIP * 4);

    /* 2: SPR_FAMILIAR_BRAMBLE (Acorn Knight - PAL1) */
    p16_clear();
    p16_rect(4, 2, 8, 4, 5); p16_rect(7, 0, 2, 2, 4);      /* Acorn cap */
    p16_rect(4, 6, 8, 6, 7); p16_rect(5, 7, 6, 4, 11);     /* Body */
    g_pix16[7*16+6]=1; g_pix16[7*16+9]=1;
    p16_rect(12, 6, 2, 6, 10);                             /* Shield/blade */
    p16_rect(5, 12, 2, 3, 4); p16_rect(9, 12, 2, 3, 4);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_FAMILIAR_BRAMBLE * 4);

    /* 3: SPR_FAMILIAR_LUMINA (Starlight Spirit - PAL1) */
    p16_clear();
    p16_rect(6, 2, 4, 10, 13); p16_rect(3, 5, 10, 4, 13);
    p16_rect(5, 4, 6, 6, 14); p16_rect(6, 5, 4, 4, 11);
    g_pix16[6*16+6]=12; g_pix16[6*16+9]=12;
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_FAMILIAR_LUMINA * 4);

    /* 4: SPR_NPC_ELDER (Elder Wren - PAL2, clear face & eyes!) */
    p16_clear();
    p16_rect(4, 1, 8, 3, 15); p16_rect(5, 1, 6, 1, 4);     /* Silver hair crown */
    p16_rect(4, 4, 8, 4, 2);                               /* Warm face */
    g_pix16[4*16+5]=15; g_pix16[4*16+6]=15;                /* Silver brows */
    g_pix16[4*16+9]=15; g_pix16[4*16+10]=15;
    g_pix16[5*16+6]=1; g_pix16[5*16+9]=1;                  /* Crisp dark eyes */
    p16_rect(5, 7, 6, 3, 15); p16_rect(6, 9, 4, 2, 4);     /* White wizard beard */
    p16_rect(4, 10, 8, 5, 8); p16_rect(5, 11, 6, 4, 9);    /* Blue robe */
    p16_rect(12, 3, 2, 12, 11); p16_rect(12, 2, 2, 2, 7);  /* Staff & gold orb */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_ELDER * 4);

    /* 5: SPR_NPC_CLARA (Clara with golden courage apron - PAL2) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 7); p16_rect(5, 4, 6, 3, 2);
    g_pix16[5*16+6]=1; g_pix16[5*16+9]=1;
    p16_rect(4, 8, 8, 6, 10); p16_rect(6, 9, 4, 5, 15);    /* Rose dress & apron */
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_CLARA * 4);

    /* 6: SPR_NPC_GUARD (Guard Bram - PAL2) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 5); p16_rect(5, 2, 6, 2, 4);      /* Steel helm */
    p16_rect(5, 5, 6, 3, 2); g_pix16[6*16+6]=1; g_pix16[6*16+9]=1;
    p16_rect(4, 8, 8, 6, 8); p16_rect(5, 9, 6, 3, 4);      /* Armor */
    p16_rect(12, 2, 2, 13, 5);                             /* Spear */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_GUARD * 4);

    /* 7: SPR_ENEMY_GLOOMCAP (Purple Mushroom - PAL3) */
    p16_clear();
    p16_rect(2, 3, 12, 5, 3); p16_rect(4, 2, 8, 4, 4);
    p16_rect(4, 4, 2, 2, 12); p16_rect(10, 4, 2, 2, 12);   /* Cap spots */
    p16_rect(5, 8, 6, 6, 5); g_pix16[10*16+6]=10; g_pix16[10*16+9]=10;
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ENEMY_GLOOMCAP * 4);

    /* 8: SPR_ENEMY_THORNLING (Briar Imp - PAL3) */
    p16_clear();
    p16_rect(3, 3, 10, 8, 6); p16_rect(5, 4, 6, 6, 7);
    p16_rect(2, 2, 2, 3, 8); p16_rect(12, 2, 2, 3, 8);
    g_pix16[6*16+6]=11; g_pix16[6*16+9]=11;                /* Glowing eyes */
    p16_rect(5, 11, 2, 4, 6); p16_rect(9, 11, 2, 4, 6);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ENEMY_THORNLING * 4);

    /* 9: SPR_ENEMY_DUSK_MOTH (Dusk Moth - PAL3) */
    p16_clear();
    p16_rect(1, 3, 5, 8, 3); p16_rect(10, 3, 5, 8, 3);
    p16_rect(2, 4, 3, 5, 15); p16_rect(11, 4, 3, 5, 15);
    p16_rect(6, 4, 4, 9, 2); g_pix16[5*16+7]=11; g_pix16[5*16+8]=11;
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ENEMY_DUSK_MOTH * 4);

    /* 10: SPR_BOSS_BRIAR (Briar Warden Boss - PAL3) */
    p16_clear();
    p16_rect(1, 1, 14, 14, 2); p16_rect(2, 2, 12, 11, 6);
    p16_rect(3, 1, 2, 3, 10); p16_rect(11, 1, 2, 3, 10);   /* Crown horns */
    p16_rect(4, 5, 3, 2, 11); p16_rect(9, 5, 3, 2, 11);    /* Fiery eyes */
    p16_rect(4, 9, 8, 2, 10);                              /* Maw */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_BOSS_BRIAR * 4);

    /* 11: SPR_WINDMILL_BLADES (PAL2) */
    p16_clear();
    p16_rect(7, 0, 2, 16, 12); p16_rect(0, 7, 16, 2, 12);
    p16_rect(6, 6, 4, 4, 7);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_WINDMILL_BLADES * 4);

    /* 12: SPR_WAYSTONE (PAL2) */
    p16_clear();
    p16_rect(4, 2, 8, 12, 5); p16_rect(5, 3, 6, 10, 4);
    p16_rect(6, 5, 4, 5, 9); p16_rect(7, 6, 2, 3, 15);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_WAYSTONE * 4);

    /* 13: SPR_CHEST (PAL2) */
    p16_clear();
    p16_rect(2, 5, 12, 9, 11); p16_rect(3, 6, 10, 7, 12);
    p16_rect(2, 8, 12, 2, 7); p16_rect(7, 8, 2, 3, 15);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_CHEST * 4);

    /* 14..17: Emotes (!, Heart, ?, Spark) (PAL3) */
    p16_clear(); p16_rect(4, 2, 8, 10, 12); p16_rect(7, 3, 2, 5, 10); p16_rect(7, 9, 2, 2, 10);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_EMOTE_BANG * 4);
    p16_clear(); p16_rect(4, 4, 3, 4, 10); p16_rect(9, 4, 3, 4, 10); p16_rect(5, 6, 6, 4, 10); p16_rect(7, 10, 2, 2, 10);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_EMOTE_HEART * 4);
    p16_clear(); p16_rect(5, 3, 6, 2, 11); p16_rect(9, 5, 2, 3, 11); p16_rect(7, 7, 2, 2, 11); p16_rect(7, 10, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_EMOTE_QUESTION * 4);
    p16_clear(); p16_rect(7, 2, 2, 12, 11); p16_rect(2, 7, 12, 2, 11); p16_rect(5, 5, 6, 6, 12);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_EMOTE_SPARK * 4);

    /* 18: SPR_HEART_ORB (Golden Courage Orb - PAL3) */
    p16_clear();
    p16_rect(4, 4, 8, 8, 11); p16_rect(5, 5, 6, 6, 12);
    p16_rect(7, 2, 2, 12, 12); p16_rect(2, 7, 12, 2, 12);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_HEART_ORB * 4);

    /* 19: SPR_CHEST_OPEN (Opened Treasure Chest with flipped lid & empty interior - PAL2) */
    p16_clear();
    p16_rect(2, 2, 12, 4, 11); p16_rect(3, 3, 10, 2, 12);  /* Flipped-back lid */
    p16_rect(2, 5, 12, 1, 7);                              /* Brass rim */
    p16_rect(3, 6, 10, 3, 1);                              /* Dark hollow interior */
    p16_rect(2, 9, 12, 5, 11); p16_rect(3, 10, 10, 3, 12); /* Lower chest front */
    p16_rect(2, 9, 12, 1, 7); p16_rect(7, 9, 2, 2, 15);    /* Open brass clasp */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_CHEST_OPEN * 4);

    /* 20: SPR_NPC_MOUSE (Whiskerburgh Anthropomorphic Mouse - PAL2) */
    p16_clear();
    p16_rect(2, 1, 4, 4, 4); p16_rect(10, 1, 4, 4, 4);     /* Round mouse ears */
    p16_rect(3, 2, 2, 2, 10); p16_rect(11, 2, 2, 2, 10);   /* Pink inner ears */
    p16_rect(4, 4, 8, 4, 15); p16_rect(5, 6, 6, 2, 2);     /* Snout & cheeks */
    g_pix16[5*16+6]=1; g_pix16[5*16+9]=1; g_pix16[7*16+7]=10; g_pix16[7*16+8]=10;
    p16_rect(4, 8, 8, 6, 8); p16_rect(5, 9, 6, 4, 7);      /* Market vest */
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_MOUSE * 4);

    /* 21: SPR_NPC_PIG (Boarhold Anthropomorphic Pig - PAL2) */
    p16_clear();
    p16_rect(3, 1, 3, 3, 10); p16_rect(10, 1, 3, 3, 10);   /* Pig ears */
    p16_rect(4, 3, 8, 5, 10); p16_rect(5, 5, 6, 3, 2);     /* Face & snout */
    g_pix16[4*16+6]=1; g_pix16[4*16+9]=1;                  /* Eyes */
    g_pix16[6*16+7]=1; g_pix16[6*16+8]=1;                  /* Snout nostrils */
    p16_rect(3, 8, 10, 6, 5); p16_rect(5, 9, 6, 4, 7);     /* Apron / jerkin */
    p16_rect(4, 14, 3, 2, 11); p16_rect(9, 14, 3, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_PIG * 4);

    /* 22: SPR_NPC_SHADOW_GRAN (Twisted Shadow of Grandmother - PAL3) */
    p16_clear();
    p16_rect(3, 1, 10, 6, 2); p16_rect(4, 2, 8, 4, 3);     /* Dark violet hood */
    p16_rect(5, 4, 6, 3, 1);                               /* Hollow shadowed face */
    g_pix16[5*16+6]=11; g_pix16[5*16+9]=11;                /* Uncanny glowing eyes */
    p16_rect(3, 7, 10, 8, 2); p16_rect(5, 8, 6, 6, 3);     /* Tattered cloak */
    p16_rect(2, 12, 2, 3, 4); p16_rect(12, 12, 2, 3, 4);   /* Shadow wisps */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_SHADOW_GRAN * 4);

    /* 23: SPR_PET_MILO (Rowan's Puppy Milo with red ribbon - PAL1) */
    p16_clear();
    p16_rect(3, 4, 3, 4, 4); p16_rect(10, 4, 3, 4, 4);     /* Floppy ears */
    p16_rect(5, 3, 6, 5, 10); p16_rect(6, 4, 4, 3, 11);    /* Golden pup head */
    g_pix16[5*16+6]=1; g_pix16[5*16+9]=1; g_pix16[7*16+7]=1; /* Eyes & nose */
    p16_rect(5, 8, 6, 2, 9);                               /* Red ribbon collar */
    p16_rect(4, 10, 8, 4, 10); p16_rect(12, 8, 2, 3, 11);  /* Body & wagging tail */
    p16_rect(5, 14, 2, 2, 4); p16_rect(9, 14, 2, 2, 4);    /* Paws */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_PET_MILO * 4);

    /* 24: SPR_ENEMY_RAT (Gutter Rat - PAL3) */
    p16_clear();
    p16_rect(3, 3, 3, 3, 3); p16_rect(10, 3, 3, 3, 3);     /* Rat ears */
    p16_rect(4, 5, 8, 7, 2); p16_rect(5, 6, 6, 5, 4);      /* Fur body */
    g_pix16[7*16+6]=10; g_pix16[7*16+9]=10;                /* Red eyes */
    p16_rect(12, 9, 3, 2, 5);                              /* Tail */
    p16_rect(5, 12, 2, 3, 3); p16_rect(9, 12, 2, 3, 3);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ENEMY_RAT * 4);

    /* 25: SPR_ENEMY_SLIME (Mire Slime - PAL3) */
    p16_clear();
    p16_rect(3, 6, 10, 8, 6); p16_rect(4, 4, 8, 8, 7);     /* Ooze dome */
    p16_rect(5, 5, 3, 2, 15);                              /* Specular highlight */
    g_pix16[8*16+6]=11; g_pix16[8*16+9]=11;                /* Bright eyes */
    p16_rect(2, 12, 12, 2, 6);                             /* Puddle base */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ENEMY_SLIME * 4);

    /* 26: SPR_ENEMY_BOAR (Boar Halberdier - PAL3) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 2); p16_rect(5, 2, 6, 2, 5);      /* Iron helm */
    p16_rect(4, 5, 8, 4, 4); g_pix16[6*16+6]=11; g_pix16[6*16+9]=11;
    g_pix16[8*16+4]=12; g_pix16[8*16+11]=12;               /* Ivory tusks */
    p16_rect(3, 9, 10, 5, 3); p16_rect(5, 10, 6, 3, 10);   /* Crimson armor */
    p16_rect(13, 1, 2, 14, 12);                            /* Halberd */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ENEMY_BOAR * 4);

    /* 27: SPR_BOSS_MUCKMAW (King Muckmaw Sewer Boss - PAL3) */
    p16_clear();
    p16_rect(5, 0, 6, 3, 11); g_pix16[0*16+6]=12; g_pix16[0*16+9]=12; /* Gold crown */
    p16_rect(1, 3, 14, 12, 6); p16_rect(2, 4, 12, 9, 7);   /* Hulking ooze toad */
    p16_rect(3, 5, 3, 2, 11); p16_rect(10, 5, 3, 2, 11);   /* Glowing eyes */
    p16_rect(3, 9, 10, 3, 1); p16_rect(4, 9, 2, 2, 12); p16_rect(10, 9, 2, 2, 12); /* Maw */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_BOSS_MUCKMAW * 4);

    /* 28: SPR_BOSS_IRONFANG (Lord Ironfang Arc Boss - PAL3) */
    p16_clear();
    p16_rect(2, 0, 2, 4, 12); p16_rect(12, 0, 2, 4, 12);   /* Great horns */
    p16_rect(3, 2, 10, 6, 2); p16_rect(4, 3, 8, 4, 3);     /* Dark iron visor */
    p16_rect(4, 5, 3, 2, 10); p16_rect(9, 5, 3, 2, 10);    /* Crimson eyes */
    p16_rect(2, 8, 12, 7, 10); p16_rect(4, 8, 8, 6, 2);    /* Cape & breastplate */
    p16_rect(3, 7, 2, 2, 12); p16_rect(11, 7, 2, 2, 12);   /* Ironfang tusks */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_BOSS_IRONFANG * 4);

    /* 29: SPR_HOLLOW_OAK (Ancient Hollow Oak Tree Gate - PAL2) */
    p16_clear();
    p16_rect(1, 0, 14, 15, 11); p16_rect(2, 1, 12, 13, 12);/* Gnarled oak bark */
    p16_rect(5, 4, 6, 11, 1);                              /* Deep hollow doorway */
    p16_rect(6, 5, 4, 8, 9); p16_rect(7, 6, 2, 6, 15);     /* Shimmering Evertale light */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_HOLLOW_OAK * 4);

    /* 30: SPR_BOBBER (Fishing Bobber - PAL1) */
    p16_clear();
    p16_rect(7, 2, 2, 3, 11);                              /* Tip stem */
    p16_rect(5, 5, 6, 3, 9);                               /* Red top half */
    p16_rect(5, 8, 6, 3, 14);                              /* White bottom half */
    p16_rect(3, 11, 10, 1, 13);                            /* Water ripple */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_BOBBER * 4);

    /* 31: SPR_NPC_TOWNSFOLK (Alderbrook Villager - PAL2) */
    p16_clear();
    p16_rect(4, 1, 8, 3, 11); p16_rect(5, 4, 6, 4, 2);     /* Brown cap & warm face */
    g_pix16[5*16+6]=1; g_pix16[5*16+9]=1;                  /* Crisp dark eyes */
    p16_rect(4, 8, 8, 6, 9); p16_rect(5, 9, 6, 4, 7);      /* Cozy sweater */
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_TOWNSFOLK * 4);

    /* 32: SPR_NPC_MARA (Grandma Mara — Silver Bun, Burgundy Shawl, Cream Apron - PAL2) */
    p16_clear();
    p16_rect(5, 0, 6, 2, 15); p16_rect(4, 2, 8, 3, 4);
    p16_rect(4, 5, 8, 3, 2); g_pix16[6*16+6]=1; g_pix16[6*16+9]=1;
    p16_rect(3, 8, 10, 6, 10); p16_rect(5, 9, 6, 5, 15);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_MARA * 4);

    /* 33: SPR_NPC_BARNABY (Stationmaster Barnaby — Navy Cap & Brass Badge - PAL2) */
    p16_clear();
    p16_rect(3, 1, 10, 3, 8); p16_rect(6, 2, 4, 1, 7);
    p16_rect(4, 4, 8, 4, 2); g_pix16[5*16+6]=1; g_pix16[5*16+9]=1;
    p16_rect(5, 7, 6, 1, 15);
    p16_rect(4, 8, 8, 6, 8); p16_rect(7, 9, 2, 4, 7);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_BARNABY * 4);

    /* 34: SPR_NPC_MAPLE (Baker Maple — Kerchief & Flour Apron - PAL2) */
    p16_clear();
    p16_rect(4, 1, 8, 2, 15); p16_rect(4, 3, 8, 2, 11);
    p16_rect(5, 5, 6, 3, 2); g_pix16[6*16+6]=1; g_pix16[6*16+9]=1;
    p16_rect(4, 8, 8, 6, 12); p16_rect(6, 9, 4, 5, 15);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_MAPLE * 4);

    /* 35: SPR_NPC_JASPER (Old Woodsman Jasper — Knit Cap & Silver Beard - PAL2) */
    p16_clear();
    p16_rect(4, 1, 8, 3, 10); p16_rect(4, 4, 8, 3, 2);
    g_pix16[5*16+6]=1; g_pix16[5*16+9]=1;
    p16_rect(4, 7, 8, 2, 15);
    p16_rect(3, 9, 10, 5, 5); p16_rect(5, 10, 6, 2, 10);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_JASPER * 4);

    /* 36: SPR_LOCOMOTIVE (Alderbrook Mountain Steam Engine - PAL2) */
    p16_clear();
    p16_rect(2, 1, 3, 4, 5); p16_rect(1, 1, 5, 1, 7);       /* Smokestack & brass lip */
    p16_rect(1, 5, 9, 7, 5); p16_rect(5, 3, 3, 2, 7);       /* Iron boiler & brass dome */
    p16_rect(10, 2, 5, 10, 10); p16_rect(11, 4, 3, 3, 15);  /* Cab & glowing window */
    p16_rect(0, 9, 2, 4, 4);                                /* Cowcatcher */
    p16_rect(2, 12, 3, 3, 10); p16_rect(6, 12, 3, 3, 10); p16_rect(11, 12, 3, 3, 10);
    p16_rect(2, 13, 12, 1, 7);                              /* Coupling rod */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_LOCOMOTIVE * 4);

    /* 32: Timed-Hit / Parry Ring Marker (PAL3) */
    p16_clear();
    p16_rect(3, 1, 10, 2, 11); p16_rect(3, 13, 10, 2, 11);
    p16_rect(1, 3, 2, 10, 11); p16_rect(13, 3, 2, 10, 11);
    p16_rect(6, 6, 4, 4, 15);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_TIMED_RING_SLOT * 4);

    /* 20: SPR_HERO_DOWN_1 (Rowan facing DOWN walking stride - PAL1) */
    p16_clear();
    p16_rect(4, 0, 8, 4, 4); p16_rect(5, 0, 6, 2, 5);
    p16_rect(4, 3, 8, 4, 2); g_pix16[4*16+6]=1; g_pix16[4*16+9]=1;
    p16_rect(4, 7, 8, 2, 9); p16_rect(2, 8, 2, 3, 8);
    p16_rect(3, 9, 10, 4, 6); p16_rect(5, 9, 6, 3, 7);
    g_pix16[11*16+3]=2; g_pix16[12*16+12]=2;
    p16_rect(4, 13, 3, 3, 4); p16_rect(10, 13, 2, 2, 4);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_HERO_DOWN_1 * 4);

    /* 21: SPR_HERO_UP_0 (Rowan facing UP idle - back of hair & red scarf - PAL1) */
    p16_clear();
    p16_rect(4, 1, 8, 6, 4); p16_rect(5, 1, 6, 3, 5);      /* Back of hair */
    p16_rect(4, 7, 8, 2, 9); p16_rect(6, 9, 4, 3, 8);      /* Scarf tail on back */
    p16_rect(3, 9, 10, 5, 6); p16_rect(5, 9, 6, 3, 7);     /* Green tunic back */
    p16_rect(6, 9, 4, 3, 8);                               /* Scarf over tunic */
    p16_rect(5, 14, 2, 2, 4); p16_rect(9, 14, 2, 2, 4);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_HERO_UP_0 * 4);

    /* 22: SPR_HERO_UP_1 (Rowan facing UP walking stride - PAL1) */
    p16_clear();
    p16_rect(4, 0, 8, 6, 4); p16_rect(5, 0, 6, 3, 5);
    p16_rect(4, 6, 8, 2, 9);
    p16_rect(3, 8, 10, 5, 6); p16_rect(5, 8, 6, 3, 7);
    p16_rect(7, 8, 3, 4, 8);                               /* Swaying scarf */
    p16_rect(4, 13, 2, 2, 4); p16_rect(9, 13, 3, 3, 4);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_HERO_UP_1 * 4);

    /* 23: SPR_HERO_SIDE_0 (Rowan facing RIGHT idle; hardware H-flipped for LEFT - PAL1) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 4); p16_rect(5, 1, 6, 2, 5);      /* Hair crown */
    p16_rect(4, 4, 3, 3, 4);                               /* Back hair */
    p16_rect(7, 4, 5, 4, 2); g_pix16[5*16+10]=1;           /* Side profile face & eye */
    p16_rect(4, 8, 8, 2, 9); p16_rect(2, 8, 3, 3, 8);      /* Streaming scarf behind */
    p16_rect(5, 10, 6, 4, 6); p16_rect(6, 10, 4, 3, 7);    /* Tunic side */
    g_pix16[12*16+10]=2;                                   /* Forward hand */
    p16_rect(6, 14, 4, 2, 4);                              /* Boots together */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_HERO_SIDE_0 * 4);

    /* 24: SPR_HERO_SIDE_1 (Rowan facing RIGHT walking stride; H-flipped for LEFT - PAL1) */
    p16_clear();
    p16_rect(4, 0, 8, 4, 4); p16_rect(5, 0, 6, 2, 5);
    p16_rect(4, 3, 3, 3, 4);
    p16_rect(7, 3, 5, 4, 2); g_pix16[4*16+10]=1;
    p16_rect(4, 7, 8, 2, 9); p16_rect(1, 7, 4, 3, 8);      /* Scarf fluttering back */
    p16_rect(5, 9, 6, 4, 6); p16_rect(6, 9, 4, 3, 7);
    g_pix16[11*16+11]=2;                                   /* Swinging hand */
    p16_rect(4, 13, 3, 3, 4); p16_rect(9, 13, 3, 3, 4);    /* Wide stride boots */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_HERO_SIDE_1 * 4);

    /* 25: SPR_BATTLE_VFX_SLOT (Star / Solar / Slash Choreography Burst - PAL3) */
    p16_clear();
    p16_rect(7, 0, 2, 16, 11); p16_rect(0, 7, 16, 2, 11);
    p16_rect(3, 3, 10, 10, 15);
    p16_rect(5, 5, 6, 6, 12);
    p16_rect(2, 2, 2, 2, 10); p16_rect(12, 2, 2, 2, 10);
    p16_rect(2, 12, 2, 2, 10); p16_rect(12, 12, 2, 2, 10);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_BATTLE_VFX_SLOT * 4);
}

/* Pre-bake 4 animated frames for meadow grass, flowers, water, ford, tall grass,
 * pulsing shrine runes, and continuously spinning 16x16 windmill sails. */
static void bake_env_anim_frames(void) {
    static const s8 sway_dx[4] = { 0, 1, 0, -1 };

    for (u8 f = 0; f < 4u; f++) {
        s8 dx = sway_dx[f];

        /* 1. Sunlit meadow grass breeze */
        p8_fill(4);
        g_pix8[1*8 + ((2 + dx) & 7)] = 5; g_pix8[2*8 + 2] = 3;
        g_pix8[5*8 + ((6 - dx) & 7)] = 5; g_pix8[6*8 + 6] = 3;
        g_pix8[4*8 + 1] = 3;
        pack_pix8_to_buf(g_anim_grass[f]);

        /* 2. Alternate meadow grass breeze */
        p8_fill(4);
        g_pix8[1*8 + ((5 - dx) & 7)] = 5; g_pix8[2*8 + 5] = 3;
        g_pix8[5*8 + ((2 + dx) & 7)] = 5; g_pix8[6*8 + 3] = 3;
        g_pix8[3*8 + 7] = 3;
        pack_pix8_to_buf(g_anim_grass_alt[f]);

        /* 3. Wildflowers bobbing gently in the breeze */
        p8_fill(4);
        g_pix8[2*8 + ((2 + dx) & 7)] = 15; g_pix8[2*8 + ((3 + dx) & 7)] = 8; g_pix8[3*8 + 2] = 3;
        g_pix8[5*8 + ((5 - dx) & 7)] = 14; g_pix8[5*8 + ((6 - dx) & 7)] = 8; g_pix8[6*8 + 5] = 3;
        pack_pix8_to_buf(g_anim_flowers[f]);

        /* 4. Flowing sunlit creek water (ripples drift smoothly rightward) */
        p8_fill(12);
        {
            u8 s1 = (u8)((f * 2u) & 7u);
            u8 s2 = (u8)((f * 2u + 4u) & 7u);
            for (u8 k = 0; k < 3u; k++) {
                g_pix8[1*8 + ((s1 + k) & 7u)] = 13;
                g_pix8[5*8 + ((s2 + k) & 7u)] = 13;
            }
            g_pix8[2*8 + ((s1 + 1u) & 7u)] = 14;
            g_pix8[2*8 + ((s1 + 2u) & 7u)] = 14;
            g_pix8[6*8 + ((s2 + 1u) & 7u)] = 14;
            g_pix8[6*8 + ((s2 + 2u) & 7u)] = 14;
            g_pix8[3*8 + ((s2 + 3u) & 7u)] = 13;
        }
        pack_pix8_to_buf(g_anim_water[f]);

        /* 5. Shallow pebble ford with shimmering current */
        p8_fill(13);
        p8_rect(1, 2, 2, 2, 10); p8_rect(5, 5, 2, 2, 11);
        g_pix8[1*8 + ((f * 2u + 1u) & 7u)] = 14;
        g_pix8[1*8 + ((f * 2u + 2u) & 7u)] = 14;
        g_pix8[6*8 + ((f * 2u + 5u) & 7u)] = 14;
        g_pix8[4*8 + ((f * 2u + 3u) & 7u)] = 12;
        pack_pix8_to_buf(g_anim_ford[f]);

        /* 6. Tall wildgrass swaying tips */
        p8_fill(3);
        g_pix8[1*8 + ((1 + dx) & 7)] = 5; g_pix8[2*8 + ((1 + dx) & 7)] = 5; p8_rect(1, 3, 1, 4, 4);
        g_pix8[0*8 + ((3 + dx) & 7)] = 5; g_pix8[1*8 + ((3 + dx) & 7)] = 4; p8_rect(3, 2, 1, 5, 4);
        g_pix8[2*8 + ((5 + dx) & 7)] = 5; g_pix8[3*8 + ((5 + dx) & 7)] = 5; p8_rect(5, 4, 1, 3, 4);
        g_pix8[1*8 + ((7 + dx) & 7)] = 5; g_pix8[2*8 + ((7 + dx) & 7)] = 4; p8_rect(7, 3, 1, 4, 4);
        pack_pix8_to_buf(g_anim_tgrass[f]);

        /* 7. Ancient Shrine rune stone pulsing with arcane cyan light */
        p8_fill(9); p8_rect(1, 1, 6, 6, 10);
        if (f == 0u) {
            p8_rect(3, 3, 2, 2, 13);
        } else if (f == 1u || f == 3u) {
            p8_rect(2, 2, 4, 4, 13); p8_rect(3, 3, 2, 2, 14);
        } else {
            p8_rect(2, 2, 4, 4, 14); p8_rect(3, 3, 2, 2, 15);
        }
        pack_pix8_to_buf(g_anim_rune[f]);

        /* 7b. Mountain Whitewater Rapids (scrolling downward riffles) */
        p8_fill(12);
        {
            u8 r1 = (u8)((f * 2u) & 7u);
            u8 r2 = (u8)((f * 2u + 4u) & 7u);
            p8_rect(1, r1, 3, 1, 13);
            g_pix8[r1 * 8u + 2u] = 14;
            p8_rect(4, r2, 3, 1, 14);
            g_pix8[((r2 + 1u) & 7u) * 8u + 5u] = 13;
        }
        pack_pix8_to_buf(g_anim_rapids[f]);

        /* 8. Windmill Blades (4 smooth rotational frames: 0°, 22.5°, 45°, 67.5°) */
        p16_clear();
        if (f == 0u) {
            /* Vertical / Horizontal '+' sails */
            p16_rect(7, 0, 2, 16, 11); p16_rect(0, 7, 16, 2, 11);
            p16_rect(9, 1, 2, 5, 15);  p16_rect(5, 10, 2, 5, 15);
            p16_rect(10, 9, 5, 2, 15); p16_rect(1, 5, 5, 2, 15);
        } else if (f == 1u) {
            /* ~22.5 deg clockwise */
            for (u8 i = 0; i < 8u; i++) {
                u8 dx1 = (u8)(i >> 1);
                g_pix16[(7 - i) * 16 + (7 + dx1)] = 11; g_pix16[(7 - i) * 16 + (8 + dx1)] = 15;
                g_pix16[(8 + i) * 16 + (8 - dx1)] = 11; g_pix16[(8 + i) * 16 + (7 - dx1)] = 15;
                g_pix16[(7 + dx1) * 16 + (8 + i)] = 11; g_pix16[(8 + dx1) * 16 + (8 + i)] = 15;
                g_pix16[(8 - dx1) * 16 + (7 - i)] = 11; g_pix16[(7 - dx1) * 16 + (7 - i)] = 15;
            }
        } else if (f == 2u) {
            /* Diagonal 'X' 45 deg sails */
            for (u8 i = 1; i < 15u; i++) {
                g_pix16[i * 16 + i] = 11;
                if (i + 1u < 16u) g_pix16[i * 16 + (i + 1u)] = 15;
                g_pix16[i * 16 + (15u - i)] = 11;
                if (i > 0u) g_pix16[(i - 1u) * 16 + (15u - i)] = 15;
            }
        } else {
            /* ~67.5 deg clockwise */
            for (u8 i = 0; i < 8u; i++) {
                u8 dy1 = (u8)(i >> 1);
                g_pix16[(7 - dy1) * 16 + (8 + i)] = 11; g_pix16[(8 - dy1) * 16 + (8 + i)] = 15;
                g_pix16[(8 + dy1) * 16 + (7 - i)] = 11; g_pix16[(7 + dy1) * 16 + (7 - i)] = 15;
                g_pix16[(7 - i) * 16 + (7 - dy1)] = 11; g_pix16[(7 - i) * 16 + (8 - dy1)] = 15;
                g_pix16[(8 + i) * 16 + (8 + dy1)] = 11; g_pix16[(8 + i) * 16 + (7 + dy1)] = 15;
            }
        }
        p16_rect(6, 6, 4, 4, 7); /* Golden hub */
        pack_pix16_sprite_to_buf(g_anim_windmill[f]);
    }
    /* Upload frame 0 immediately to VRAM so boot frame matches animation */
    VDP_loadTileData(g_anim_windmill[0], VRAM_SPR_BASE + SPR_WINDMILL_BLADES * 4u, 4, CPU);
}

static void tick_env_animations(u32 frame_count) {
    u8 af = (u8)((frame_count >> 3) & 3u); /* Smooth ~7.5 Hz environmental cycle */
    if (af == g_last_env_frame) return;
    g_last_env_frame = af;

    /* Queue all 11 tile updates (352 bytes total) atomically into VBlank DMA_QUEUE */
    VDP_loadTileData(g_anim_grass[af],     VRAM_BG_BASE + GT_GRASS,      1, DMA_QUEUE);
    VDP_loadTileData(g_anim_grass_alt[af], VRAM_BG_BASE + GT_GRASS_ALT,  1, DMA_QUEUE);
    VDP_loadTileData(g_anim_flowers[af],   VRAM_BG_BASE + GT_FLOWERS,    1, DMA_QUEUE);
    VDP_loadTileData(g_anim_water[af],     VRAM_BG_BASE + GT_WATER,      1, DMA_QUEUE);
    VDP_loadTileData(g_anim_ford[af],      VRAM_BG_BASE + GT_FORD,       1, DMA_QUEUE);
    VDP_loadTileData(g_anim_tgrass[af],    VRAM_BG_BASE + GT_TALL_GRASS, 1, DMA_QUEUE);
    VDP_loadTileData(g_anim_rune[af],      VRAM_BG_BASE + GT_SHRINE_RUNE,1, DMA_QUEUE);
    VDP_loadTileData(g_anim_rapids[af],    VRAM_BG_BASE + GT_RAPIDS,     1, DMA_QUEUE);
    VDP_loadTileData(g_anim_windmill[af],  VRAM_SPR_BASE + SPR_WINDMILL_BLADES * 4u, 4, DMA_QUEUE);
}

/* ============================================================================
 * 3. HARDWARE MAP TILE MAPPING & 64x64 SCROLL STREAMING
 * ============================================================================ */
static u16 map_bg1_to_vram_tile(u8 t) {
    switch (t) {
        case T_GRASS:          return VRAM_BG_BASE + GT_GRASS;
        case T_GRASS_ALT:      return VRAM_BG_BASE + GT_GRASS_ALT;
        case T_FLOWERS:        return VRAM_BG_BASE + GT_FLOWERS;
        case T_DIRT:           return VRAM_BG_BASE + GT_DIRT;
        case T_COBBLE:         return VRAM_BG_BASE + GT_COBBLE;
        case T_WATER:          return VRAM_BG_BASE + GT_WATER;
        case T_FORD:           return VRAM_BG_BASE + GT_FORD;
        case T_BRIDGE:         return VRAM_BG_BASE + GT_BRIDGE;
        case T_BRIDGE_RAIL_N:
        case T_BRIDGE_RAIL_S:  return VRAM_BG_BASE + GT_BRIDGE_RAIL;
        case T_TALL_GRASS:     return VRAM_BG_BASE + GT_TALL_GRASS;
        case T_BUSH:           return VRAM_BG_BASE + GT_BUSH;
        case T_TREE_TRUNK:     return VRAM_BG_BASE + GT_TRUNK;
        case T_ROOF_TOP:       return VRAM_BG_BASE + GT_ROOF_TOP;
        case T_ROOF_BODY:      return VRAM_BG_BASE + GT_ROOF_BODY;
        case T_WALL_WOOD:      return VRAM_BG_BASE + GT_WALL_WOOD;
        case T_WALL_WINDOW:    return VRAM_BG_BASE + GT_WALL_WIN;
        case T_WALL_DOOR:      return VRAM_BG_BASE + GT_WALL_DOOR;
        case T_WINDMILL_ROOF:  return VRAM_BG_BASE + GT_WINDMILL_ROOF;
        case T_WINDMILL_WALL:
        case T_WINDMILL_DOOR:  return VRAM_BG_BASE + GT_WINDMILL_WALL;
        case T_FLOWER_BED:     return VRAM_BG_BASE + GT_FLOWER_BED;
        case T_FENCE:          return VRAM_BG_BASE + GT_FENCE;
        case T_LANTERN_POST:   return VRAM_BG_BASE + GT_LAMP;
        case T_SIGNPOST:       return VRAM_BG_BASE + GT_SIGN;
        case T_SHRINE_FLOOR:   return VRAM_BG_BASE + GT_SHRINE_FLOOR;
        case T_SHRINE_RUNE:    return VRAM_BG_BASE + GT_SHRINE_RUNE;
        case T_SHRINE_PILLAR:  return VRAM_BG_BASE + GT_SHRINE_PILLAR;
        case T_MTN_SKY:        return VRAM_BG_BASE + GT_MTN_SKY;
        case T_CLIFF_WALL:     return VRAM_BG_BASE + GT_CLIFF_WALL;
        case T_CLIFF_LEDGE:    return VRAM_BG_BASE + GT_CLIFF_LEDGE;
        case T_BOULDER:        return VRAM_BG_BASE + GT_BOULDER;
        case T_AUTUMN_GRASS:   return VRAM_BG_BASE + GT_AUTUMN_GRASS;
        case T_ROCKY_PATH:     return VRAM_BG_BASE + GT_ROCKY_PATH;
        case T_RAPIDS:         return VRAM_BG_BASE + GT_RAPIDS;
        case T_RAIL_TRACK:     return VRAM_BG_BASE + GT_RAIL_TRACK;
        case T_MAPLE_TRUNK:    return VRAM_BG_BASE + GT_MAPLE_TRUNK;
        case T_PINE_TRUNK:     return VRAM_BG_BASE + GT_PINE_TRUNK;
        case T_WOOD_FLOOR:     return VRAM_BG_BASE + GT_WOOD_FLOOR;
        case T_HEARTH_RUG:     return VRAM_BG_BASE + GT_HEARTH_RUG;
        case T_FIREPLACE:      return VRAM_BG_BASE + GT_FIREPLACE;
        case T_BED_QUILT:      return VRAM_BG_BASE + GT_BED_QUILT;
        case T_ATTIC_CHEST:    return VRAM_BG_BASE + GT_ATTIC_CHEST;
        case T_HOLLOW_OAK:     return VRAM_BG_BASE + GT_HOLLOW_OAK;
        case T_STONE_STAIRS:   return VRAM_BG_BASE + GT_STONE_STAIRS;
        case T_INTERIOR_WALL:  return VRAM_BG_BASE + GT_INTERIOR_WALL;
        default:               return VRAM_BG_BASE + GT_GRASS;
    }
}

static u16 map_bg2_to_vram_tile(u8 t) {
    if (t == O_NONE) return VRAM_BG_BASE + GT_EMPTY;
    if (t == O_ROOF_EAVE) return VRAM_BG_BASE + GT_ROOF_EAVE;
    if (t == O_SHRINE_ARCH) return VRAM_BG_BASE + GT_SHRINE_ARCH;
    if (t == O_MAPLE_TL || t == O_MAPLE_TR) return VRAM_BG_BASE + GT_MAPLE_CANOPY;
    if (t == O_PINE_TOP) return VRAM_BG_BASE + GT_PINE_TOP;
    if (t == O_OAK_CROWN) return VRAM_BG_BASE + GT_OAK_CROWN;
    return VRAM_BG_BASE + GT_CANOPY;
}

static void write_metatile(s16 mx, s16 my) {
    u16 a_attr, b_attr;
    if (mx < 0 || my < 0 || mx >= HEARTH_MAP_W || my >= HEARTH_MAP_H) {
        a_attr = TILE_ATTR_FULL(PAL0, 0, 0, 0, VRAM_BG_BASE + GT_BUSH);
        b_attr = TILE_ATTR_FULL(PAL0, 0, 0, 0, VRAM_BG_BASE + GT_EMPTY);
    } else {
        u16 idx = (u16)(my * HEARTH_MAP_W + mx);
        u16 a_tile = map_bg1_to_vram_tile(g_hearth.map_bg1[idx]);
        u16 b_tile = map_bg2_to_vram_tile(g_hearth.map_bg2[idx]);
        a_attr = TILE_ATTR_FULL(PAL0, 0, 0, 0, a_tile);
        b_attr = TILE_ATTR_FULL(PAL0, (b_tile != VRAM_BG_BASE + GT_EMPTY) ? 1 : 0, 0, 0, b_tile);
    }
    u16 tx0 = (u16)((mx * 2) & 63);
    u16 tx1 = (u16)((tx0 + 1) & 63);
    u16 ty0 = (u16)((my * 2) & 63);
    u16 ty1 = (u16)((ty0 + 1) & 63);
    VDP_setTileMapXY(BG_A, a_attr, tx0, ty0);
    VDP_setTileMapXY(BG_A, a_attr, tx1, ty0);
    VDP_setTileMapXY(BG_A, a_attr, tx0, ty1);
    VDP_setTileMapXY(BG_A, a_attr, tx1, ty1);
    VDP_setTileMapXY(BG_B, b_attr, tx0, ty0);
    VDP_setTileMapXY(BG_B, b_attr, tx1, ty0);
    VDP_setTileMapXY(BG_B, b_attr, tx0, ty1);
    VDP_setTileMapXY(BG_B, b_attr, tx1, ty1);
}

static s16 g_last_cam_mx = -999;
static s16 g_last_cam_my = -999;
static u8  g_last_scene_mode = 255;
static u8  g_last_intro_scene = 255;
static u8  g_last_pal_id = 255;
static u8  g_last_map_rev = 255;

static void draw_full_world_viewport(s16 cam_x, s16 cam_y) {
    s16 mx0 = (s16)((cam_x >> 4) - 2);
    s16 my0 = (s16)((cam_y >> 4) - 2);
    for (s16 dy = 0; dy < 18; dy++) {
        for (s16 dx = 0; dx < 24; dx++) {
            write_metatile((s16)(mx0 + dx), (s16)(my0 + dy));
        }
    }
    g_last_cam_mx = (s16)(cam_x >> 4);
    g_last_cam_my = (s16)(cam_y >> 4);
}

static void stream_world_viewport(s16 cam_x, s16 cam_y) {
    s16 cmx = (s16)(cam_x >> 4);
    s16 cmy = (s16)(cam_y >> 4);
    if (cmx == g_last_cam_mx && cmy == g_last_cam_my) return;
    if ((cmx - g_last_cam_mx) > 3 || (g_last_cam_mx - cmx) > 3 ||
        (cmy - g_last_cam_my) > 3 || (g_last_cam_my - cmy) > 3) {
        draw_full_world_viewport(cam_x, cam_y);
        return;
    }
    while (g_last_cam_mx < cmx) {
        g_last_cam_mx++;
        for (s16 dy = -2; dy < 16; dy++) write_metatile((s16)(g_last_cam_mx + 21), (s16)(g_last_cam_my + dy));
    }
    while (g_last_cam_mx > cmx) {
        g_last_cam_mx--;
        for (s16 dy = -2; dy < 16; dy++) write_metatile((s16)(g_last_cam_mx - 2), (s16)(g_last_cam_my + dy));
    }
    while (g_last_cam_my < cmy) {
        g_last_cam_my++;
        for (s16 dx = -2; dx < 22; dx++) write_metatile((s16)(g_last_cam_mx + dx), (s16)(g_last_cam_my + 15));
    }
    while (g_last_cam_my > cmy) {
        g_last_cam_my--;
        for (s16 dx = -2; dx < 22; dx++) write_metatile((s16)(g_last_cam_mx + dx), (s16)(g_last_cam_my - 2));
    }
}

static void draw_battle_backdrop(u8 biome) {
    u8 dark_arena = (biome == BIOME_SHRINE || biome == BIOME_SEWER || biome == BIOME_BOARHOLD) ? 1u : 0u;
    u16 sky_t = dark_arena ? (VRAM_BG_BASE + GT_BATTLE_SKY_SHRINE) : (VRAM_BG_BASE + GT_BATTLE_SKY_MEADOW);
    u16 gnd_t = dark_arena ? (VRAM_BG_BASE + GT_BATTLE_GROUND_SHRINE) : (VRAM_BG_BASE + GT_BATTLE_GROUND_MEADOW);
    VDP_clearPlane(BG_B, TRUE);
    VDP_fillTileMapRect(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, sky_t), 0, 0, 40, 10);
    VDP_fillTileMapRect(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, gnd_t), 0, 10, 40, 18);
    VDP_setHorizontalScrollVSync(BG_A, 0);
    VDP_setVerticalScrollVSync(BG_A, 0);
    VDP_setHorizontalScrollVSync(BG_B, 0);
    VDP_setVerticalScrollVSync(BG_B, 0);
}

static void draw_intro_backdrop(u8 scene) {
    u8 p = (scene < CINEMA_ROM_PLATE_COUNT) ? scene : 0u;
    const uint16_t *mp = CINEMA_ROM_MAP_PTRS[p];

    VDP_clearPlane(BG_B, TRUE);
    VDP_clearPlane(BG_A, TRUE);

    /* Upload quantized 16-color BGR333 plate palette to PAL0 & 4bpp tiles to VRAM_CG_BASE */
    PAL_setColors(0, CINEMA_ROM_PALS[p], 16, CPU);
    VDP_loadTileData(CINEMA_GEN_TILE_PTRS[p], VRAM_CG_BASE, CINEMA_ROM_TILE_COUNTS[p], DMA);

    /* Center 32x15 illustrated anime CG plate horizontally on 40x28 Genesis screen (x=4..35, y=2..16) */
    for (u16 ty = 0; ty < CINEMA_ROM_TH; ty++) {
        for (u16 tx = 0; tx < CINEMA_ROM_TW; tx++) {
            u16 tid = mp[(u16)(ty * CINEMA_ROM_TW + tx)];
            VDP_setTileMapXY(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, (u16)(VRAM_CG_BASE + tid)),
                             (u16)(4u + tx), (u16)(2u + ty));
        }
    }

    VDP_setHorizontalScrollVSync(BG_A, 0);
    VDP_setVerticalScrollVSync(BG_A, 0);
    VDP_setHorizontalScrollVSync(BG_B, 0);
    VDP_setVerticalScrollVSync(BG_B, 0);
}

/* ============================================================================
 * 4. NON-SCROLLING HARDWARE WINDOW HUD, DIALOGUE & BATTLE UI
 * ============================================================================ */
static void win_fill_box(u16 x, u16 y, u16 w, u16 h) {
    VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), x, y, w, h);
    if (w < 2 || h < 2) return;
    VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_TOP),  (u16)(x + 1), y, (u16)(w - 2), 1);
    VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_BOT),  (u16)(x + 1), (u16)(y + h - 1), (u16)(w - 2), 1);
    VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_LEFT), x, (u16)(y + 1), 1, (u16)(h - 2));
    VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_RIGHT),(u16)(x + w - 1), (u16)(y + 1), 1, (u16)(h - 2));
    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_TL), x, y);
    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_TR), (u16)(x + w - 1), y);
    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_BL), x, (u16)(y + h - 1));
    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_BR), (u16)(x + w - 1), (u16)(y + h - 1));
}

static void win_text_n(u16 x, u16 y, u16 pal, const char *s, u16 max_chars) {
    if (!s) return;
    for (u16 i = 0; i < max_chars && s[i] != '\0' && (x + i) < 40u; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c < 32 || c > 95) c = ' ';
        VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(pal, 1, 0, 0, (u16)(VRAM_FONT_BASE + (u16)(c - 32))), (u16)(x + i), y);
    }
}

static void win_text(u16 x, u16 y, u16 pal, const char *s) {
    win_text_n(x, y, pal, s, 40u);
}

static void append_str(char *dst, u16 *pos, u16 cap, const char *src) {
    if (!src) return;
    while (*src && (u16)(*pos + 1u) < cap) dst[(*pos)++] = *src++;
    dst[*pos] = '\0';
}

static void append_int(char *dst, u16 *pos, u16 cap, s16 val) {
    char tmp[8];
    u16 n = 0;
    s32 v = val;
    if (v < 0) {
        if ((u16)(*pos + 1u) < cap) dst[(*pos)++] = '-';
        v = -v;
    }
    if (v == 0) tmp[n++] = '0';
    else { while (v > 0 && n < 7u) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } }
    while (n > 0 && (u16)(*pos + 1u) < cap) dst[(*pos)++] = tmp[--n];
    dst[*pos] = '\0';
}

static u8  g_win_layout = 255;
static u16 g_hud_tick = 0;
static u8  g_last_title_cursor = 255;

static void draw_genesis_title_cursor(void) {
    static const char *opts[3] = {
        "NEW STORY (AGE 5->10) ",
        "EXPLORE EVERTALE      ",
        "REPLAY OPENING CINEMA "
    };
    for (u8 i = 0; i < 3u; i++) {
        u16 pal = (i == g_hearth.title_cursor) ? PAL3 : PAL2;
        u16 y = (u16)(16u + i * 2u);
        win_text(7, y, pal, (i == g_hearth.title_cursor) ? ">" : " ");
        win_text(9, y, pal, opts[i]);
    }
    g_last_title_cursor = g_hearth.title_cursor;
}

static void update_window_ui(const HearthPpuPacket *ppu) {
    u8 want_layout = 0;
    if (g_hearth.mode == MODE_INTRO) want_layout = 4;
    else if (g_hearth.mode == MODE_TITLE) want_layout = 1;
    else if (g_hearth.mode == MODE_SHOP) want_layout = 5;
    else if (g_hearth.mode == MODE_FISHING) want_layout = 6;
    else if (g_hearth.mode == MODE_BATTLE || g_hearth.mode == MODE_BATTLE_VICTORY || g_hearth.mode == MODE_BATTLE_DEFEAT) want_layout = 3;
    else if (g_hearth.dialogue.active) want_layout = 2;
    else want_layout = 0;

    if (want_layout != g_win_layout) {
        g_win_layout = want_layout;
        g_last_title_cursor = 255;
        if (want_layout == 1) {
            VDP_clearPlane(WINDOW, TRUE);
            VDP_setWindowFullScreen();
            win_fill_box(2, 2, 36, 24);
            win_text(13, 4,  PAL3, "PROJECT HEARTH");
            win_text(8,  6,  PAL2, "16-BIT JRPG CORE (GENESIS)");
            win_text(5,  9,  PAL2, "DPAD  : MOVE    B : DASH");
            win_text(5,  11, PAL2, "A / C : ACT / TIMED HIT / PARRY");
            win_text(5,  13, PAL2, "X / Z : SWAP FAMILIAR IN BATTLE");
            draw_genesis_title_cursor();
            win_text(7,  23, PAL3, "DPAD:PICK  START OR A:BEGIN");
        } else if (want_layout == 0) {
            VDP_setWindowOnTop(2);
            VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 0, 0, 40, 2);
            VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_BOT),  0, 1, 40, 1);
        } else {
            VDP_setWindowOnBottom(8);
            win_fill_box(0, 20, 40, 8);
        }
        g_hud_tick = 0;
    }

    if (want_layout == 1) {
        if (g_hearth.title_cursor != g_last_title_cursor) {
            draw_genesis_title_cursor();
        }
        return;
    }

    /* Throttle text redraws to ~15Hz (every 4 frames) for crisp responsiveness & light DMA */
    if ((g_hud_tick & 3u) != 0u) {
        g_hud_tick++;
        return;
    }
    g_hud_tick++;

    if (want_layout == 4) {
        /* Illustrated Opening Cinema Narrative Box */
        const HearthIntroState *in = &g_hearth.intro;
        char buf[128];
        u16 total = 0u, vis, start = 0u;
        const char *s;
        const char *hdr = (in->speaker && in->speaker[0] != '\0')
                        ? in->speaker
                        : (in->title ? in->title : "PROLOGUE");
        VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 1, 21, 38, 6);
        win_text_n(2, 21, PAL3, hdr, 36u);
        if (in->line1) {
            s = in->line1;
            while (*s && total < 125u) buf[total++] = *s++;
        }
        if (in->line2 && total < 125u) {
            if (total > 0u) buf[total++] = ' ';
            s = in->line2;
            while (*s && total < 126u) buf[total++] = *s++;
        }
        buf[total] = '\0';
        vis = (in->chars > total) ? total : in->chars;
        for (u16 row = 22u; row <= 24u && start < vis; row++) {
            u16 end, cut;
            while (start < vis && buf[start] == ' ') start++;
            if (start >= vis) break;
            end = (u16)(start + 36u);
            if (end >= vis) {
                cut = vis;
            } else {
                cut = end;
                while (cut > start && buf[cut] != ' ') cut--;
                if (cut == start) cut = end;
            }
            win_text_n(2, row, PAL2, buf + start, (u16)(cut - start));
            start = cut;
        }
        win_text(2, 26, PAL3,
                 in->auto_play ? "A:NEXT   Y:AUTO:ON    START:SKIP  "
                               : "A:NEXT   Y:AUTO:OFF   START:SKIP  ");
    } else if (want_layout == 0) {
        char line[40];
        u16 pos = 0;
        const HearthPartyMember *m = &g_hearth.party[g_hearth.active_slot];
        append_str(line, &pos, sizeof(line), "HP:");
        append_int(line, &pos, sizeof(line), m->hp);
        append_str(line, &pos, sizeof(line), " MP:");
        append_int(line, &pos, sizeof(line), m->mp);
        append_str(line, &pos, sizeof(line), " ");
        append_int(line, &pos, sizeof(line), (s16)g_hearth.guilders);
        append_str(line, &pos, sizeof(line), "G");
        if (ppu->interact_active && ppu->interact_label) {
            append_str(line, &pos, sizeof(line), " [A]");
            append_str(line, &pos, sizeof(line), ppu->interact_label);
        } else if (ppu->zone_name) {
            append_str(line, &pos, sizeof(line), " ");
            append_str(line, &pos, sizeof(line), ppu->zone_name);
        }
        while (pos < 38u) line[pos++] = ' ';
        line[pos] = '\0';
        win_text(1, 0, PAL2, line);
    } else if (want_layout == 2) {
        const HearthDialogue *d = &g_hearth.dialogue;
        VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 1, 21, 38, 6);
        win_text(2, 21, PAL3, d->speaker ? d->speaker : "...");
        if (d->full_text) {
            u16 vis = d->visible_chars;
            win_text_n(2, 22, PAL2, d->full_text, (vis < 36u) ? vis : 36u);
            if (vis > 36u) win_text_n(2, 23, PAL2, d->full_text + 36, (u16)((vis - 36u < 36u) ? (vis - 36u) : 36u));
            if (vis > 72u && d->choice_count == 0) {
                win_text_n(2, 24, PAL2, d->full_text + 72, (u16)((vis - 72u < 36u) ? (vis - 72u) : 36u));
            }
        }
        if (d->finished && d->choice_count > 0) {
            for (u8 c = 0; c < d->choice_count && c < 3u; c++) {
                win_text(2, (u16)(24 + c), (c == d->selected_choice) ? PAL3 : PAL2,
                    (c == d->selected_choice) ? ">" : " ");
                win_text(4, (u16)(24 + c), (c == d->selected_choice) ? PAL3 : PAL2, d->choices[c].label);
            }
        } else if (d->finished) {
            win_text(28, 26, PAL3, "[A] NEXT");
        }
    } else if (want_layout == 3) {
        const HearthBattleState *b = &g_hearth.battle;
        const HearthPartyMember *pm = &g_hearth.party[b->active_slot];
        char line[40];
        u16 pos;

        VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 1, 21, 38, 6);
        if (g_hearth.mode == MODE_BATTLE_VICTORY) {
            win_text(10, 22, PAL3, "VICTORY! HEART STEADY!");
            win_text(7,  24, PAL2, "PRESS A OR START TO RETURN");
            return;
        }
        if (g_hearth.mode == MODE_BATTLE_DEFEAT) {
            win_text(9, 22, PAL3, "RETREATING TO WAYSTONE");
            win_text(7, 24, PAL2, "PRESS A OR START TO CONTINUE");
            return;
        }

        /* Row 21: Active Fighter, Slot Index, HP/MP, & Familiar Swap Cue */
        pos = 0;
        append_str(line, &pos, sizeof(line), pm->name);
        append_str(line, &pos, sizeof(line), " ");
        append_int(line, &pos, sizeof(line), (s16)(b->active_slot + 1u));
        append_str(line, &pos, sizeof(line), "/3 HP:");
        append_int(line, &pos, sizeof(line), pm->hp);
        append_str(line, &pos, sizeof(line), "/");
        append_int(line, &pos, sizeof(line), pm->max_hp);
        append_str(line, &pos, sizeof(line), " MP:");
        append_int(line, &pos, sizeof(line), pm->mp);
        win_text_n(2, 21, PAL2, line, 36u);

        /* Row 22: Selected Enemy HP, Spell Locks, and Charge Countdown */
        if (b->selected_enemy < b->enemy_count) {
            const HearthBattleEnemy *e = &b->enemies[b->selected_enemy];
            pos = 0;
            append_str(line, &pos, sizeof(line), "TGT:");
            append_str(line, &pos, sizeof(line), e->name);
            append_str(line, &pos, sizeof(line), " HP:");
            append_int(line, &pos, sizeof(line), e->hp);
            if (e->lock_count > 0) {
                append_str(line, &pos, sizeof(line), " [");
                for (u8 l = 0; l < e->lock_count; l++) {
                    if (e->broken_locks[l]) append_str(line, &pos, sizeof(line), ".");
                    else if (e->locks[l] == LOCK_SWORD) append_str(line, &pos, sizeof(line), "W");
                    else if (e->locks[l] == LOCK_SUN)   append_str(line, &pos, sizeof(line), "S");
                    else append_str(line, &pos, sizeof(line), "*");
                }
                append_str(line, &pos, sizeof(line), "]");
            }
            win_text_n(2, 22, PAL3, line, 36u);
        }

        /* Rows 24-25: Timed-Hit/Parry Callout, Choreography Banner, or Real Move Menu */
        if (b->ring.active) {
            u16 rpal = ((g_hearth.frame_count & 4u) != 0u) ? PAL3 : PAL2;
            win_text(6, 24, rpal, b->ring.is_parry
                ? ">>> PRESS A NOW TO PARRY! <<<"
                : ">>> PRESS A FOR TIMED HIT! <<<");
        } else if (b->banner_frames > 0 && b->banner[0] != '\0') {
            win_text_n(3, 24, PAL3, b->banner, 34u);
        } else {
            char opt[20];
            for (u8 c = 0; c < 4u; c++) {
                u16 cx = (u16)(2u + (c & 1u) * 19u);
                u16 cy = (u16)(24u + (c >> 1));
                u16 cpal = (c == b->selected_cmd) ? PAL3 : PAL2;
                pos = 0;
                if (c == 0u) {
                    append_str(opt, &pos, sizeof(opt), pm->basic_name);
                } else if (c == 1u) {
                    append_str(opt, &pos, sizeof(opt), pm->skill_name);
                    append_str(opt, &pos, sizeof(opt), " ");
                    append_int(opt, &pos, sizeof(opt), (s16)pm->skill_cost);
                    append_str(opt, &pos, sizeof(opt), "M");
                } else if (c == 2u) {
                    append_str(opt, &pos, sizeof(opt), "GUARD +6MP");
                } else {
                    append_str(opt, &pos, sizeof(opt), "TONIC x");
                    append_int(opt, &pos, sizeof(opt), (s16)g_hearth.potions);
                }
                win_text(cx, cy, cpal, (c == b->selected_cmd) ? ">" : " ");
                win_text_n((u16)(cx + 1u), cy, cpal, opt, 17u);
            }
        }
        win_text(2, 26, PAL2, "DPAD:PICK  A:ACT  X/Z:SWAP FAMILIAR");
    } else if (want_layout == 5) {
        /* Interactive Merchant Shop (Buy Supplies / Sell Caught Fish) */
        const HearthShopState *sh = &g_hearth.shop;
        char line[40];
        u16 pos = 0;
        u8 count = (sh->tab == 0u) ? 5u : (u8)HEARTH_FISH_SPECIES;
        u8 start = (sh->cursor >= 3u) ? (u8)(sh->cursor - 2u) : 0u;

        VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 1, 21, 38, 6);
        append_str(line, &pos, sizeof(line), sh->tab == 0u ? "[BUY] SELL  " : " BUY [SELL] ");
        append_int(line, &pos, sizeof(line), (s16)g_hearth.guilders);
        append_str(line, &pos, sizeof(line), "G");
        win_text_n(2, 21, PAL3, line, 36u);

        for (u8 r = 0; r < 3u && (u8)(start + r) < count; r++) {
            u8 idx = (u8)(start + r);
            u16 pal = (idx == sh->cursor) ? PAL3 : PAL2;
            pos = 0;
            append_str(line, &pos, sizeof(line), (idx == sh->cursor) ? ">" : " ");
            if (sh->tab == 0u) {
                append_str(line, &pos, sizeof(line), HEARTH_SHOP_ITEMS[idx].name);
                append_str(line, &pos, sizeof(line), " - ");
                append_int(line, &pos, sizeof(line), (s16)HEARTH_SHOP_ITEMS[idx].price);
                append_str(line, &pos, sizeof(line), "G");
            } else {
                append_str(line, &pos, sizeof(line), HEARTH_FISH_TABLE[idx].name);
                append_str(line, &pos, sizeof(line), " x");
                append_int(line, &pos, sizeof(line), (s16)g_hearth.fish_bag[idx]);
                append_str(line, &pos, sizeof(line), " (");
                append_int(line, &pos, sizeof(line), (s16)HEARTH_FISH_TABLE[idx].sell_price);
                append_str(line, &pos, sizeof(line), "G)");
            }
            win_text_n(2, (u16)(22u + r), pal, line, 36u);
        }
        if (sh->msg_timer > 0u && sh->msg[0] != '\0') {
            win_text_n(2, 25, PAL3, sh->msg, 36u);
        } else {
            win_text(2, 25, PAL2, "L/R:TAB  U/D:PICK  A:BUY/SELL  B:OUT");
        }
        win_text(2, 26, PAL2, sh->title ? sh->title : "EVERTALE MARKET");
    } else if (want_layout == 6) {
        /* Zelda / Sea-of-Stars Timed-Reel Fishing Gauge */
        const HearthFishingState *fs = &g_hearth.fishing;
        char line[40];
        u16 pos = 0;
        VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 1, 21, 38, 6);
        if (fs->phase == FISH_PHASE_CAST || fs->phase == FISH_PHASE_WAIT_BITE) {
            win_text(2, 21, PAL3, "FISHING PIER - WAIT FOR A BITE...");
            win_text(2, 23, PAL2, "WATCH THE BOBBER! PRESS A ON SPLASH");
            win_text(2, 26, PAL2, "B:PACK UP ROD");
        } else if (fs->phase == FISH_PHASE_BITE_ALERT) {
            u16 bpal = ((g_hearth.frame_count & 4u) != 0u) ? PAL3 : PAL2;
            win_text(6, 22, bpal, ">>> BITE! PRESS A TO HOOK! <<<");
            win_text(2, 26, PAL2, "HOLD A TO REEL ONCE HOOKED!");
        } else if (fs->phase == FISH_PHASE_REELING) {
            char bar[28];
            u8 fi = (u8)((fs->fish_pos * 24) / 100);
            u8 bi = (u8)((fs->bar_pos * 24) / 100);
            u8 bw = (u8)((fs->bar_half_w * 24) / 100);
            if (bw < 2u) bw = 2u;
            bar[0] = '[';
            for (u8 k = 0; k < 24u; k++) {
                u8 d = (k > bi) ? (u8)(k - bi) : (u8)(bi - k);
                if (k == fi) bar[1u + k] = 'O';
                else if (d <= bw) bar[1u + k] = '=';
                else bar[1u + k] = '-';
            }
            bar[25] = ']';
            bar[26] = '\0';
            win_text(2, 21, PAL3, "REELING! HOLD A TO RAISE ZONE");
            win_text(6, 23, PAL3, bar);
            append_str(line, &pos, sizeof(line), "CATCH:");
            append_int(line, &pos, sizeof(line), (s16)fs->progress);
            append_str(line, &pos, sizeof(line), "%   TENSION:");
            append_int(line, &pos, sizeof(line), (s16)fs->tension);
            append_str(line, &pos, sizeof(line), "%");
            win_text_n(2, 25, PAL2, line, 36u);
            win_text(2, 26, PAL2, "KEEP [O] INSIDE [===]  B:RELEASE");
        } else if (fs->phase == FISH_PHASE_CAUGHT) {
            append_str(line, &pos, sizeof(line), "CAUGHT: ");
            append_str(line, &pos, sizeof(line), HEARTH_FISH_TABLE[fs->species % HEARTH_FISH_SPECIES].name);
            append_str(line, &pos, sizeof(line), " (");
            append_int(line, &pos, sizeof(line), (s16)fs->weight_oz);
            append_str(line, &pos, sizeof(line), " OZ)");
            win_text_n(2, 22, PAL3, line, 36u);
            win_text(2, 25, PAL2, "SELL IT AT WHISKERBURGH MARKET!");
            win_text(2, 26, PAL3, "A:CONTINUE");
        } else {
            win_text(6, 22, PAL3, "THE LINE SNAPPED! IT GOT AWAY.");
            win_text(2, 26, PAL2, "A:TRY AGAIN");
        }
    }
}

/* ============================================================================
 * 5. HARDWARE SPRITE TABLE (SAT) UPLOAD
 * ============================================================================ */
static u16 sprite_palette(u8 spr_id) {
    if (spr_id <= SPR_FAMILIAR_LUMINA || spr_id == SPR_PET_MILO || spr_id == SPR_BOBBER) return PAL1;
    if (spr_id <= SPR_NPC_GUARD || spr_id == SPR_WINDMILL_BLADES || spr_id == SPR_WAYSTONE ||
        spr_id == SPR_CHEST || spr_id == SPR_CHEST_OPEN || spr_id == SPR_NPC_MOUSE ||
        spr_id == SPR_NPC_PIG || spr_id == SPR_HOLLOW_OAK || spr_id == SPR_NPC_TOWNSFOLK ||
        (spr_id >= SPR_NPC_MARA && spr_id <= SPR_LOCOMOTIVE)) return PAL2;
    return PAL3;
}

static void render_sprites(const HearthPpuPacket *ppu) {
    u16 count = 0;

    if (g_hearth.mode == MODE_TITLE) {
        VDP_setSpriteFull(0, -32, -32, SPRITE_SIZE(2, 2), 0, 0);
        VDP_updateSprites(1, DMA_QUEUE);
        return;
    }

    for (u8 i = 0; i < ppu->oam_count && count < 58u; i++) {
        const HearthOamSprite *o = &ppu->oam[i];
        s16 sx = (s16)(o->x - 8 + ppu->shake_x);
        s16 sy = (s16)(o->y - ((o->layer == 2) ? 8 : 16) + ppu->shake_y);
        u16 pal, tile;
        u8 flip_x;

        if (sx < -16 || sx >= 320 || sy < -16 || sy >= 224) continue;
        /* Keep sprites behind bottom dialogue/battle/intro/shop/fishing window */
        if (g_win_layout >= 2u && sy >= 148) continue;
        if (g_win_layout == 0u && sy < 8) continue;

        pal = sprite_palette(o->sprite_id);
        flip_x = (o->flags & OAM_FLAG_FLIP_X) ? 1u : 0u;
        if (o->sprite_id == SPR_HERO) {
            u8 step = (u8)(o->frame & 1u);
            u8 slot = SPR_HERO;
            if (o->dir == DIR_DOWN) {
                slot = step ? SPR_HERO_DOWN_1 : SPR_HERO;
                flip_x = 0u;
            } else if (o->dir == DIR_UP) {
                slot = step ? SPR_HERO_UP_1 : SPR_HERO_UP_0;
                flip_x = 0u;
            } else if (o->dir == DIR_LEFT) {
                slot = step ? SPR_HERO_SIDE_1 : SPR_HERO_SIDE_0;
                flip_x = 1u;
            } else {
                slot = step ? SPR_HERO_SIDE_1 : SPR_HERO_SIDE_0;
                flip_x = 0u;
            }
            tile = (u16)(VRAM_SPR_BASE + (u16)slot * 4u);
        } else {
            tile = (u16)(VRAM_SPR_BASE + (u16)o->sprite_id * 4u);
        }
        VDP_setSpriteFull(count, sx, sy, SPRITE_SIZE(2, 2),
            TILE_ATTR_FULL(pal, 0, 0, flip_x, tile), (u8)(count + 1u));
        count++;
    }

    /* Battle Choreography VFX Burst Sprite (Solar Meteor / Star Bolt / Staff Arc / Guard) */
    if (g_hearth.mode == MODE_BATTLE && g_hearth.battle.vfx_kind != BVFX_NONE && count < 60u) {
        const HearthBattleState *b = &g_hearth.battle;
        u16 dur = (b->vfx_dur > 0u) ? b->vfx_dur : 1u;
        s32 t256 = ((s32)b->vfx_timer * 256) / (s32)dur;
        s16 vx = (s16)(b->vfx_x + (((s32)(b->vfx_tx - b->vfx_x) * t256) >> 8) - 8);
        s16 vy = (s16)(b->vfx_y + (((s32)(b->vfx_ty - b->vfx_y) * t256) >> 8) - 16);
        u8 vflip = (u8)((g_hearth.frame_count >> 1) & 1u);
        u8 hflip = (u8)((g_hearth.frame_count >> 2) & 1u);
        if (vx >= -16 && vx < 320 && vy >= -16 && vy < 148) {
            VDP_setSpriteFull(count, vx, vy, SPRITE_SIZE(2, 2),
                TILE_ATTR_FULL(PAL3, 1, vflip, hflip, VRAM_SPR_BASE + SPR_BATTLE_VFX_SLOT * 4u),
                (u8)(count + 1u));
            count++;
        }
    }

    /* Battle Timed-Hit / Parry indicator sprite */
    if (g_hearth.mode == MODE_BATTLE && g_hearth.battle.ring.active && count < 62u) {
        s16 rx = (s16)(g_hearth.battle.ring.x - 8);
        s16 ry = (s16)(g_hearth.battle.ring.y - 24);
        VDP_setSpriteFull(count, rx, ry, SPRITE_SIZE(2, 2),
            TILE_ATTR_FULL(PAL3, 1, 0, 0, VRAM_SPR_BASE + SPR_TIMED_RING_SLOT * 4u),
            (u8)(count + 1u));
        count++;
    }

    if (count == 0) {
        VDP_setSpriteFull(0, -32, -32, SPRITE_SIZE(2, 2), 0, 0);
        VDP_updateSprites(1, DMA_QUEUE);
    } else {
        /* Terminate hardware SAT chain cleanly and flush atomically during VBlank */
        VDP_setSpriteFull(count, -32, -32, SPRITE_SIZE(1, 1), 0, 0);
        VDP_updateSprites((u16)(count + 1u), DMA_QUEUE);
    }
}

/* ============================================================================
 * 6. CLEAN SN76489 PSG SFX + MULTI-ZONE BGM SEQUENCER
 *    - Z80 halted at boot so 68000 has exclusive PSG bus ownership
 *    - Mute-before-latch inside SYS_disableInts() eliminates 2-byte pitch blips
 *    - Fixed noise clock dividers (no TONE3 cross-talk with SFX channel 2)
 * ============================================================================ */
static u8  g_psg_env[4]   = { 15, 15, 15, 15 };
static u8  g_psg_decay[4] = { 4, 4, 2, 2 };
static u8  g_psg_cnt[4]   = { 0, 0, 0, 0 };
static u8  g_bgm_step     = 0;
static u8  g_bgm_timer    = 0;
static u8  g_last_bgm_id  = 255;

/* Windmill Hamlet (3/4 Pastoral Waltz) */
static const u16 PSG_HAMLET_LEAD[24] = {
    392, 494, 587, 587, 659, 587,  494, 494, 392, 0,   440, 494,
    523, 523, 659, 587, 523, 494,  440, 440, 494, 523, 587, 392
};
static const u16 PSG_HAMLET_BASS[24] = {
    196, 294, 294, 165, 247, 247,  196, 294, 294, 147, 220, 220,
    262, 196, 196, 147, 220, 220,  220, 165, 147, 220, 196, 196
};
/* Alderbrook Overture — "Steam & Pine" (32-step 6/8 Waltz, C Major) */
static const u16 PSG_ALDER_LEAD[32] = {
    330, 392, 523, 494, 440, 392, 330, 0,
    349, 440, 392, 349, 330, 294, 262, 0,
    330, 392, 523, 587, 659, 587, 523, 440,
    392, 440, 494, 587, 523, 523, 0,   0
};
static const u16 PSG_ALDER_BASS[32] = {
    262, 196, 196, 220, 165, 165, 220, 196,
    175, 262, 262, 196, 147, 262, 196, 196,
    262, 196, 196, 220, 165, 175, 262, 220,
    196, 147, 196, 247, 262, 196, 262, 0
};
/* "Beyond the Hollow Oak" — Cinema Wonder (32 steps, D Major) */
static const u16 PSG_WONDER_LEAD[32] = {
    587, 740, 880, 784, 740, 659, 587, 494,
    587, 440, 0,   440, 494, 587, 740, 659,
    740, 784, 880, 988, 880, 740, 659, 740,
    784, 740, 587, 494, 440, 554, 587, 0
};
static const u16 PSG_WONDER_BASS[32] = {
    294, 220, 196, 294, 247, 185, 220, 165,
    196, 294, 294, 220, 220, 165, 294, 220,
    294, 220, 196, 294, 220, 165, 247, 185,
    196, 294, 220, 165, 294, 220, 294, 0
};
/* "Lantern Lullaby" — Cinema Comfort & Pip's Window (32 steps, F Major) */
static const u16 PSG_LULLABY_LEAD[32] = {
    440, 523, 587, 523, 440, 392, 440, 349,
    0,   466, 587, 523, 440, 392, 440, 0,
    523, 698, 659, 587, 523, 587, 523, 440,
    392, 440, 466, 440, 392, 330, 349, 0
};
static const u16 PSG_LULLABY_BASS[32] = {
    175, 262, 233, 175, 262, 196, 175, 262,
    233, 175, 220, 262, 147, 220, 262, 196,
    175, 262, 262, 196, 233, 175, 262, 196,
    147, 220, 262, 196, 175, 262, 175, 0
};
/* "The Hollow Mirror" — Shadow Grandmother (32 steps, C Minor) */
static const u16 PSG_SHADOW_LEAD[32] = {
    523, 622, 587, 494, 523, 415, 392, 0,
    622, 740, 698, 587, 622, 587, 523, 0,
    784, 740, 698, 622, 587, 523, 494, 392,
    415, 523, 494, 587, 523, 523, 0,   0
};
static const u16 PSG_SHADOW_BASS[32] = {
    131, 131, 247, 247, 208, 208, 196, 196,
    131, 131, 175, 175, 208, 196, 131, 0,
    131, 131, 247, 247, 208, 208, 196, 196,
    175, 175, 196, 196, 131, 131, 0,   0
};
/* Whiskerburgh Mouse Market (Bouncy Ragtime) */
static const u16 PSG_MOUSE_LEAD[16] = {
    587, 659, 740, 880, 740, 659, 587, 494,
    523, 587, 659, 784, 659, 587, 523, 440
};
static const u16 PSG_MOUSE_BASS[16] = {
    294, 220, 294, 220, 247, 196, 247, 196,
    262, 196, 262, 196, 220, 165, 220, 196
};
/* Sunken Sewers / Shadow Mood (Suspenseful Minor) */
static const u16 PSG_SEWER_LEAD[16] = {
    330, 0,   392, 370, 330, 0,   294, 247,
    330, 0,   440, 392, 370, 330, 247, 0
};
static const u16 PSG_SEWER_BASS[16] = {
    165, 165, 196, 165, 147, 147, 165, 147,
    131, 131, 165, 131, 123, 147, 165, 123
};
/* Boarhold Citadel (Sturdy Brass March) */
static const u16 PSG_BOAR_LEAD[16] = {
    392, 392, 466, 523, 587, 523, 466, 392,
    440, 440, 523, 587, 659, 587, 523, 440
};
static const u16 PSG_BOAR_BASS[16] = {
    196, 147, 196, 147, 233, 175, 233, 175,
    220, 165, 220, 165, 262, 196, 220, 165
};
/* Battle & Boss Theme */
static const u16 PSG_BATTLE_LEAD[16] = {
    440, 523, 659, 880, 784, 659, 587, 659,
    523, 659, 784, 880, 784, 659, 587, 494
};
static const u16 PSG_BATTLE_BASS[16] = {
    220, 220, 262, 220, 196, 196, 220, 196,
    262, 262, 294, 262, 165, 196, 220, 247
};

static void psg_trigger_tone(u8 ch, u16 hz, u8 start_env, u8 decay_frames) {
    if (hz < 110u) return;
    SYS_disableInts();
    PSG_setEnvelope(ch, PSG_ENVELOPE_MIN);
    PSG_setFrequency(ch, hz);
    PSG_setEnvelope(ch, start_env);
    SYS_enableInts();
    g_psg_env[ch]   = start_env;
    g_psg_decay[ch] = decay_frames;
    g_psg_cnt[ch]   = 0;
}

static void psg_trigger_noise(u8 type, u8 freq, u8 start_env, u8 decay_frames) {
    SYS_disableInts();
    PSG_setEnvelope(3, PSG_ENVELOPE_MIN);
    PSG_setNoise(type, freq);
    PSG_setEnvelope(3, start_env);
    SYS_enableInts();
    g_psg_env[3]   = start_env;
    g_psg_decay[3] = decay_frames;
    g_psg_cnt[3]   = 0;
}

static void play_genesis_sfx(uint8_t sfx) {
    switch (sfx) {
        case SFX_STEP:
        case SFX_GRASS_RUSTLE:
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK2, 11, 1);
            break;
        case SFX_DASH:
        case SFX_WATER_SPLASH:
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK4, 8, 2);
            break;
        case SFX_CURSOR:
            psg_trigger_tone(2, 660, 7, 1);
            break;
        case SFX_CONFIRM:
        case SFX_EMOTE:
            psg_trigger_tone(2, 880, 6, 2);
            break;
        case SFX_CANCEL:
            psg_trigger_tone(2, 330, 7, 2);
            break;
        case SFX_TEXT_HERO:
        case SFX_TEXT_PIP:
        case SFX_TEXT_ELDER:
            psg_trigger_tone(2, (sfx == SFX_TEXT_PIP) ? 740 : 420, 10, 1);
            break;
        case SFX_HIT_NORMAL:
        case SFX_HURT:
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK8, 5, 2);
            psg_trigger_tone(2, 220, 6, 2);
            break;
        case SFX_HIT_CRITICAL:
        case SFX_PARRY_CLANG:
        case SFX_LOCK_BREAK:
        case SFX_LOCK_ALL_BREAK:
            psg_trigger_tone(2, 1047, 4, 3);
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK2, 6, 2);
            break;
        case SFX_COIN:
            psg_trigger_tone(2, 1175, 5, 2);
            break;
        case SFX_FISH_BITE:
            psg_trigger_tone(2, 988, 4, 3);
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK4, 6, 2);
            break;
        case SFX_FISH_REEL:
            psg_trigger_tone(2, 523, 8, 1);
            break;
        case SFX_FISH_CATCH:
        case SFX_PORTAL:
        case SFX_HEAL:
        case SFX_HEART_TAKE:
        case SFX_HEART_GIVE:
        case SFX_CHEST_OPEN:
        case SFX_VICTORY_FANFARE:
            psg_trigger_tone(2, 784, 5, 4);
            break;
        default:
            break;
    }
}

static void psg_tick_60hz(u8 bgm_id) {
    /* 0. Cleanly reset BGM voices whenever active track changes */
    if (bgm_id != g_last_bgm_id) {
        g_last_bgm_id = bgm_id;
        g_bgm_step = 0;
        g_bgm_timer = 0;
        SYS_disableInts();
        PSG_setEnvelope(0, PSG_ENVELOPE_MIN);
        PSG_setEnvelope(1, PSG_ENVELOPE_MIN);
        SYS_enableInts();
        g_psg_env[0] = PSG_ENVELOPE_MIN;
        g_psg_env[1] = PSG_ENVELOPE_MIN;
    }

    /* 1. Decay all 4 PSG channels smoothly back to silence (PSG_ENVELOPE_MIN = 15) */
    for (u8 ch = 0; ch < 4u; ch++) {
        if (g_psg_env[ch] < PSG_ENVELOPE_MIN) {
            g_psg_cnt[ch]++;
            if (g_psg_cnt[ch] >= g_psg_decay[ch]) {
                g_psg_cnt[ch] = 0;
                g_psg_env[ch]++;
                SYS_disableInts();
                PSG_setEnvelope(ch, g_psg_env[ch]);
                SYS_enableInts();
            }
        }
    }

    /* 2. Step clean 2-channel PSG BGM (Ch 0 lead, Ch 1 bass) */
    if (g_bgm_timer > 0) {
        g_bgm_timer--;
        return;
    }
    if (bgm_id == BGM_BATTLE || bgm_id == BGM_BOSS) {
        u8 idx = (u8)(g_bgm_step & 15u);
        psg_trigger_tone(0, PSG_BATTLE_LEAD[idx], 8, 3);
        psg_trigger_tone(1, PSG_BATTLE_BASS[idx], 10, 3);
        g_bgm_timer = (bgm_id == BGM_BOSS) ? 9 : 11;
    } else if (bgm_id == BGM_ALDERBROOK) {
        u8 idx = (u8)(g_bgm_step & 31u);
        if (PSG_ALDER_LEAD[idx] > 0) psg_trigger_tone(0, PSG_ALDER_LEAD[idx], 9, 5);
        if (PSG_ALDER_BASS[idx] > 0) psg_trigger_tone(1, PSG_ALDER_BASS[idx], 11, 5);
        g_bgm_timer = 15;
    } else if (bgm_id == BGM_CINEMA_WONDER) {
        u8 idx = (u8)(g_bgm_step & 31u);
        if (PSG_WONDER_LEAD[idx] > 0) psg_trigger_tone(0, PSG_WONDER_LEAD[idx], 8, 5);
        if (PSG_WONDER_BASS[idx] > 0) psg_trigger_tone(1, PSG_WONDER_BASS[idx], 11, 5);
        g_bgm_timer = 14;
    } else if (bgm_id == BGM_CINEMA_LULLABY) {
        u8 idx = (u8)(g_bgm_step & 31u);
        if (PSG_LULLABY_LEAD[idx] > 0) psg_trigger_tone(0, PSG_LULLABY_LEAD[idx], 9, 6);
        if (PSG_LULLABY_BASS[idx] > 0) psg_trigger_tone(1, PSG_LULLABY_BASS[idx], 11, 6);
        g_bgm_timer = 17;
    } else if (bgm_id == BGM_SHADOW) {
        u8 idx = (u8)(g_bgm_step & 31u);
        if (PSG_SHADOW_LEAD[idx] > 0) psg_trigger_tone(0, PSG_SHADOW_LEAD[idx], 8, 6);
        if (PSG_SHADOW_BASS[idx] > 0) psg_trigger_tone(1, PSG_SHADOW_BASS[idx], 10, 6);
        g_bgm_timer = 17;
    } else if (bgm_id == BGM_WHISKERBURGH) {
        u8 idx = (u8)(g_bgm_step & 15u);
        psg_trigger_tone(0, PSG_MOUSE_LEAD[idx], 8, 3);
        psg_trigger_tone(1, PSG_MOUSE_BASS[idx], 10, 3);
        g_bgm_timer = 12;
    } else if (bgm_id == BGM_SEWERS) {
        u8 idx = (u8)(g_bgm_step & 15u);
        if (PSG_SEWER_LEAD[idx] > 0) psg_trigger_tone(0, PSG_SEWER_LEAD[idx], 9, 5);
        if (PSG_SEWER_BASS[idx] > 0) psg_trigger_tone(1, PSG_SEWER_BASS[idx], 11, 5);
        g_bgm_timer = 18;
    } else if (bgm_id == BGM_BOARHOLD) {
        u8 idx = (u8)(g_bgm_step & 15u);
        psg_trigger_tone(0, PSG_BOAR_LEAD[idx], 8, 3);
        psg_trigger_tone(1, PSG_BOAR_BASS[idx], 10, 4);
        g_bgm_timer = 13;
    } else {
        u8 idx = (u8)(g_bgm_step % 24u);
        if (PSG_HAMLET_LEAD[idx] > 0) psg_trigger_tone(0, PSG_HAMLET_LEAD[idx], 9, 4);
        if (PSG_HAMLET_BASS[idx] > 0) psg_trigger_tone(1, PSG_HAMLET_BASS[idx], 11, 4);
        g_bgm_timer = 15;
    }
    g_bgm_step++;
}

/* ============================================================================
 * 7. INPUT & MAIN LOOP
 * ============================================================================ */
static uint16_t read_genesis_pad(void) {
    u16 raw = JOY_readJoypad(JOY_1);
    uint16_t bits = 0;

    if (raw & BUTTON_UP)    bits |= PAD_UP;
    if (raw & BUTTON_DOWN)  bits |= PAD_DOWN;
    if (raw & BUTTON_LEFT)  bits |= PAD_LEFT;
    if (raw & BUTTON_RIGHT) bits |= PAD_RIGHT;
    if (raw & BUTTON_A)     bits |= PAD_A;
    if (raw & BUTTON_B)     bits |= PAD_B;
    if (raw & BUTTON_C)     bits |= PAD_A;      /* C also confirms for 3-button pads */
    if (raw & BUTTON_X)     bits |= PAD_L;      /* 6-Button Pad L Hot-Swap */
    if (raw & BUTTON_Z)     bits |= PAD_R;      /* 6-Button Pad R Hot-Swap */
    if (raw & BUTTON_Y)     bits |= PAD_Y;
    if (raw & BUTTON_START) bits |= PAD_START;
    if (raw & BUTTON_MODE)  bits |= PAD_SELECT;
    return bits;
}

static void upload_zone_pal0_to_cram(u8 pal_id, TransferMethod tm) {
    u16 cram[16];
    u8 pid = (pal_id < ZPAL_COUNT) ? pal_id : 0u;
    for (u16 c = 0; c < 16u; c++) {
        cram[c] = hearth_rgb_to_genesis_bgr333(HEARTH_ZONE_PALETTES_RGB[pid][c]);
    }
    PAL_setColors(0, cram, 16, tm);
    g_last_pal_id = pid;
}

static void upload_palettes_to_cram(void) {
    u16 cram[16];
    for (u16 p = 0; p < 4u; p++) {
        for (u16 c = 0; c < 16u; c++) {
            cram[c] = hearth_rgb_to_genesis_bgr333(HEARTH_PALETTES_RGB[p][c]);
        }
        PAL_setColors((u16)(p * 16u), cram, 16, CPU);
    }
    g_last_pal_id = ZPAL_HAMLET;
}

int main(bool hard_reset) {
    (void)hard_reset;

    /* Halt Z80 PSG bus contention before initializing SN76489 hardware! */
    Z80_loadDriver(Z80_DRIVER_NULL, TRUE);
    PSG_reset();
    VDP_setScreenWidth320();
    VDP_setScreenHeight224();
    VDP_setPlaneSize(64, 64, TRUE);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_resetSprites();

    upload_palettes_to_cram();
    VDP_setBackgroundColor(33); /* PAL2[1] warm slate backdrop */

    bake_bg_and_ui_tiles();
    bake_all_sprites();
    bake_env_anim_frames();

    hearth_init(&g_hearth);
    g_hearth.tuning.ppu_mode = PPU_GENESIS_320x224;
    {
        const HearthPpuPacket *boot_ppu = hearth_step_60hz(&g_hearth, 0u);
        g_last_map_rev = g_hearth.map_revision;
        upload_zone_pal0_to_cram(boot_ppu->terrain_pal_id, CPU);
        if (g_hearth.mode == MODE_INTRO) {
            g_last_scene_mode = 2u;
            g_last_intro_scene = g_hearth.intro.scene;
            draw_intro_backdrop(g_hearth.intro.scene);
        } else {
            g_last_scene_mode = 0u;
            draw_full_world_viewport(boot_ppu->cam_x, boot_ppu->cam_y);
            VDP_setHorizontalScrollVSync(BG_A, (s16)(-boot_ppu->cam_x));
            VDP_setVerticalScrollVSync(BG_A,   (s16)( boot_ppu->cam_y));
            VDP_setHorizontalScrollVSync(BG_B, (s16)(-boot_ppu->cam_x));
            VDP_setVerticalScrollVSync(BG_B,   (s16)( boot_ppu->cam_y));
        }
        update_window_ui(boot_ppu);
        render_sprites(boot_ppu);
        SYS_doVBlankProcess();
    }

    while (TRUE) {
        uint16_t pad = read_genesis_pad();
        const HearthPpuPacket *ppu = hearth_step_60hz(&g_hearth, pad);
        u8 scene_mode = (g_hearth.mode == MODE_INTRO) ? 2u
                      : (g_hearth.mode == MODE_BATTLE ||
                         g_hearth.mode == MODE_BATTLE_VICTORY ||
                         g_hearth.mode == MODE_BATTLE_DEFEAT) ? 1u : 0u;
        u8 map_changed = (g_hearth.map_revision != g_last_map_rev) ? 1u : 0u;

        if (scene_mode != 2u && ppu->terrain_pal_id != g_last_pal_id) {
            upload_zone_pal0_to_cram(ppu->terrain_pal_id, DMA_QUEUE);
        }

        if (scene_mode != g_last_scene_mode || map_changed ||
            (scene_mode == 2u && g_hearth.intro.scene != g_last_intro_scene)) {
            g_last_scene_mode = scene_mode;
            g_last_intro_scene = g_hearth.intro.scene;
            g_last_map_rev = g_hearth.map_revision;
            VDP_setEnable(FALSE);
            if (scene_mode == 2u) {
                g_last_pal_id = 255u; /* Force zone PAL0 restore upon leaving cinema */
                draw_intro_backdrop(g_hearth.intro.scene);
            } else if (scene_mode == 1u) {
                upload_zone_pal0_to_cram(ppu->terrain_pal_id, CPU);
                draw_battle_backdrop(g_hearth.battle.biome);
            } else {
                upload_zone_pal0_to_cram(ppu->terrain_pal_id, CPU);
                draw_full_world_viewport(ppu->cam_x, ppu->cam_y);
            }
            VDP_setEnable(TRUE);
        }

        if (scene_mode == 0u) {
            stream_world_viewport(ppu->cam_x, ppu->cam_y);
            tick_env_animations(g_hearth.frame_count);
            /* VSync scroll updates latch atomically in VBlank -> 0% screen tearing */
            VDP_setHorizontalScrollVSync(BG_A, (s16)(-(ppu->cam_x - ppu->shake_x)));
            VDP_setVerticalScrollVSync(BG_A,   (s16)( (ppu->cam_y - ppu->shake_y)));
            VDP_setHorizontalScrollVSync(BG_B, (s16)(-(ppu->cam_x - ppu->shake_x)));
            VDP_setVerticalScrollVSync(BG_B,   (s16)( (ppu->cam_y - ppu->shake_y)));
        } else if (scene_mode == 2u) {
            VDP_setHorizontalScrollVSync(BG_A, (s16)ppu->shake_x);
            VDP_setVerticalScrollVSync(BG_A,   (s16)ppu->shake_y);
        } else {
            VDP_setHorizontalScrollVSync(BG_A, (s16)ppu->shake_x);
            VDP_setVerticalScrollVSync(BG_A,   (s16)ppu->shake_y);
        }

        update_window_ui(ppu);
        render_sprites(ppu);

        for (uint8_t i = 0; i < ppu->sfx_count; i++) {
            play_genesis_sfx(ppu->sfx_queue[i]);
        }
        psg_tick_60hz(ppu->bgm_id);

        SYS_doVBlankProcess();
    }

    return 0;
}
