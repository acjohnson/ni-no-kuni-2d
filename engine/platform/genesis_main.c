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

/* Placed statically in Sega Genesis 64KB 68000 Work RAM (zero malloc!) */
static HearthState g_hearth;

/* ============================================================================
 * VRAM TILE INDEX ALLOCATION (All well below TILE_FONT_INDEX)
 * ============================================================================ */
#define VRAM_BG_BASE    ((u16)(TILE_USER_INDEX))
#define VRAM_UI_BASE    ((u16)(VRAM_BG_BASE + 40u))
#define VRAM_SPR_BASE   ((u16)(VRAM_UI_BASE + 16u))
#define VRAM_FONT_BASE  ((u16)(VRAM_SPR_BASE + 80u))

/* BG Tile IDs (offsets from VRAM_BG_BASE) */
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

/* Sprite VRAM Slots (20 sprites x 4 tiles each = 80 tiles) */
#define SPR_TIMED_RING_SLOT 19u

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

static void upload_pix8_to_vram(u16 tile_idx) {
    u32 tile[8];
    for (u16 y = 0; y < 8; y++) {
        u32 row = 0;
        for (u16 x = 0; x < 8; x++) {
            row = (row << 4) | (u32)(g_pix8[y * 8 + x] & 0x0Fu);
        }
        tile[y] = row;
    }
    VDP_loadTileData(tile, tile_idx, 1, CPU);
}

/* Genesis 2x2 (16x16) hardware sprites are COLUMN-MAJOR: TL, BL, TR, BR */
static void upload_pix16_sprite_to_vram(u16 base_tile) {
    u32 tiles[4][8];
    static const u8 ox[4] = { 0, 0, 8, 8 };
    static const u8 oy[4] = { 0, 8, 0, 8 };
    for (u16 t = 0; t < 4; t++) {
        for (u16 y = 0; y < 8; y++) {
            u32 row = 0;
            for (u16 x = 0; x < 8; x++) {
                u8 c = g_pix16[(oy[t] + y) * 16 + (ox[t] + x)] & 0x0Fu;
                row = (row << 4) | (u32)c;
            }
            tiles[t][y] = row;
        }
    }
    VDP_loadTileData(&tiles[0][0], base_tile, 4, CPU);
}

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
    /* 0: SPR_HERO (Rowan - PAL1: forest tunic, red scarf, brown hair) */
    p16_clear();
    p16_rect(4, 1, 8, 4, 4); p16_rect(5, 1, 6, 2, 5);      /* Hair */
    p16_rect(4, 4, 8, 4, 2); g_pix16[5*16+6]=1; g_pix16[5*16+9]=1; /* Face & eyes */
    p16_rect(4, 8, 8, 2, 9); p16_rect(3, 9, 2, 3, 8);      /* Red scarf */
    p16_rect(4, 10, 8, 4, 6); p16_rect(5, 10, 6, 2, 7);    /* Green tunic */
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

    /* 4: SPR_NPC_ELDER (Elder Wren - PAL2) */
    p16_clear();
    p16_rect(4, 1, 8, 3, 4); p16_rect(5, 4, 6, 3, 2);
    p16_rect(5, 7, 6, 3, 4);                               /* Beard */
    p16_rect(4, 10, 8, 5, 8); p16_rect(5, 10, 6, 4, 9);    /* Blue robe */
    p16_rect(12, 4, 2, 11, 11);                            /* Staff */
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

    /* 19: Timed-Hit / Parry Ring Marker (PAL3) */
    p16_clear();
    p16_rect(3, 1, 10, 2, 11); p16_rect(3, 13, 10, 2, 11);
    p16_rect(1, 3, 2, 10, 11); p16_rect(13, 3, 2, 10, 11);
    p16_rect(6, 6, 4, 4, 15);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_TIMED_RING_SLOT * 4);
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
        default:               return VRAM_BG_BASE + GT_GRASS;
    }
}

static u16 map_bg2_to_vram_tile(u8 t) {
    if (t == O_NONE) return VRAM_BG_BASE + GT_EMPTY;
    if (t == O_ROOF_EAVE) return VRAM_BG_BASE + GT_ROOF_EAVE;
    if (t == O_SHRINE_ARCH) return VRAM_BG_BASE + GT_SHRINE_ARCH;
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
static u8  g_last_scene_battle = 255;

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
    u16 sky_t = (biome == BIOME_SHRINE) ? (VRAM_BG_BASE + GT_BATTLE_SKY_SHRINE) : (VRAM_BG_BASE + GT_BATTLE_SKY_MEADOW);
    u16 gnd_t = (biome == BIOME_SHRINE) ? (VRAM_BG_BASE + GT_BATTLE_GROUND_SHRINE) : (VRAM_BG_BASE + GT_BATTLE_GROUND_MEADOW);
    VDP_clearPlane(BG_B, TRUE);
    VDP_fillTileMapRect(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, sky_t), 0, 0, 40, 10);
    VDP_fillTileMapRect(BG_A, TILE_ATTR_FULL(PAL0, 0, 0, 0, gnd_t), 0, 10, 40, 18);
    VDP_setHorizontalScroll(BG_A, 0);
    VDP_setVerticalScroll(BG_A, 0);
    VDP_setHorizontalScroll(BG_B, 0);
    VDP_setVerticalScroll(BG_B, 0);
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

static void update_window_ui(const HearthPpuPacket *ppu) {
    u8 want_layout = 0;
    if (g_hearth.mode == MODE_TITLE) want_layout = 1;
    else if (g_hearth.mode == MODE_BATTLE || g_hearth.mode == MODE_BATTLE_VICTORY || g_hearth.mode == MODE_BATTLE_DEFEAT) want_layout = 3;
    else if (g_hearth.dialogue.active) want_layout = 2;
    else want_layout = 0;

    if (want_layout != g_win_layout) {
        g_win_layout = want_layout;
        if (want_layout == 1) {
            VDP_clearPlane(WINDOW, TRUE);
            VDP_setWindowFullScreen();
            win_fill_box(2, 3, 36, 22);
            win_text(13, 6,  PAL2, "PROJECT HEARTH");
            win_text(8,  8,  PAL3, "16-BIT JRPG CORE (GENESIS)");
            win_text(5,  11, PAL2, "DPAD  : MOVE    B : DASH");
            win_text(5,  13, PAL2, "A     : ACT / TIMED HIT / PARRY");
            win_text(5,  15, PAL2, "X / Z : HOT-SWAP FAMILIAR");
            win_text(7,  18, PAL2, "EXPLORE WINDMILL HAMLET &");
            win_text(7,  20, PAL2, "SHATTER THE BRIAR WARDEN!");
            win_text(8,  23, PAL3, "PRESS START OR A TO BEGIN");
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

    /* Throttle text redraws to ~10Hz (every 6 frames) for crisp responsiveness & light DMA */
    if ((g_hud_tick % 6u) != 0u && want_layout != 1) {
        g_hud_tick++;
        return;
    }
    g_hud_tick++;

    if (want_layout == 0) {
        char line[40];
        u16 pos = 0;
        const HearthPartyMember *m = &g_hearth.party[g_hearth.active_slot];
        append_str(line, &pos, sizeof(line), "HP:");
        append_int(line, &pos, sizeof(line), m->hp);
        append_str(line, &pos, sizeof(line), "/");
        append_int(line, &pos, sizeof(line), m->max_hp);
        append_str(line, &pos, sizeof(line), " MP:");
        append_int(line, &pos, sizeof(line), m->mp);
        if (ppu->interact_active && ppu->interact_label) {
            append_str(line, &pos, sizeof(line), " [A]");
            append_str(line, &pos, sizeof(line), ppu->interact_label);
        } else if (g_hearth.flag_guard_mended) {
            append_str(line, &pos, sizeof(line), " SHRINE->");
        } else if (g_hearth.flag_has_courage) {
            append_str(line, &pos, sizeof(line), " COURAGE!");
        } else {
            append_str(line, &pos, sizeof(line), " HAMLET");
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
            win_text(11, 22, PAL3, "VICTORY! HEART STEADY!");
            win_text(9,  24, PAL2, "PRESS A OR START TO RETURN");
            return;
        }
        if (g_hearth.mode == MODE_BATTLE_DEFEAT) {
            win_text(10, 22, PAL3, "RETREATING TO WAYSTONE");
            win_text(9,  24, PAL2, "PRESS A OR START TO CONTINUE");
            return;
        }

        /* Row 21: Active Party Member HP/MP */
        pos = 0;
        append_str(line, &pos, sizeof(line), pm->name);
        append_str(line, &pos, sizeof(line), " HP:");
        append_int(line, &pos, sizeof(line), pm->hp);
        append_str(line, &pos, sizeof(line), "/");
        append_int(line, &pos, sizeof(line), pm->max_hp);
        append_str(line, &pos, sizeof(line), " MP:");
        append_int(line, &pos, sizeof(line), pm->mp);
        win_text(2, 21, PAL2, line);

        /* Row 22: Selected Enemy HP + Spell Locks */
        if (b->selected_enemy < b->enemy_count) {
            const HearthBattleEnemy *e = &b->enemies[b->selected_enemy];
            pos = 0;
            append_str(line, &pos, sizeof(line), e->name);
            append_str(line, &pos, sizeof(line), " HP:");
            append_int(line, &pos, sizeof(line), e->hp);
            if (e->lock_count > 0) {
                append_str(line, &pos, sizeof(line), " L:");
                for (u8 l = 0; l < e->lock_count; l++) {
                    if (e->broken_locks[l]) append_str(line, &pos, sizeof(line), ".");
                    else if (e->locks[l] == LOCK_SWORD) append_str(line, &pos, sizeof(line), "W");
                    else if (e->locks[l] == LOCK_SUN)   append_str(line, &pos, sizeof(line), "S");
                    else append_str(line, &pos, sizeof(line), "*");
                }
            }
            win_text(2, 22, PAL3, line);
        }

        /* Row 24-25: Command Menu or Timed-Hit Banner */
        if (b->ring.active) {
            win_text(9, 24, PAL3, b->ring.is_parry ? ">>> TAP A TO PARRY! <<<" : ">>> TAP A TIMED HIT! <<<");
        } else if (b->banner_frames > 0 && b->banner[0] != '\0') {
            win_text(4, 24, PAL3, b->banner);
        } else {
            static const char *cmds[4] = { "STRIKE", "SKILL ", "GUARD ", "TONIC " };
            for (u8 c = 0; c < 4u; c++) {
                u16 cx = (u16)(2u + (c & 1u) * 18u);
                u16 cy = (u16)(24u + (c >> 1));
                win_text(cx, cy, (c == b->selected_cmd) ? PAL3 : PAL2, (c == b->selected_cmd) ? ">" : " ");
                win_text((u16)(cx + 1u), cy, (c == b->selected_cmd) ? PAL3 : PAL2, cmds[c]);
            }
        }
        win_text(2, 26, PAL2, "X/Z:SWAP FAMILIAR  A:CONFIRM");
    }
}

/* ============================================================================
 * 5. HARDWARE SPRITE TABLE (SAT) UPLOAD
 * ============================================================================ */
static u16 sprite_palette(u8 spr_id) {
    if (spr_id <= SPR_FAMILIAR_LUMINA) return PAL1;
    if (spr_id <= SPR_NPC_GUARD || spr_id == SPR_WINDMILL_BLADES || spr_id == SPR_WAYSTONE || spr_id == SPR_CHEST) return PAL2;
    return PAL3;
}

static void render_sprites(const HearthPpuPacket *ppu) {
    u16 count = 0;

    if (g_hearth.mode == MODE_TITLE) {
        VDP_setSpriteFull(0, -32, -32, SPRITE_SIZE(2, 2), 0, 0);
        VDP_updateSprites(1, DMA);
        return;
    }

    for (u8 i = 0; i < ppu->oam_count && count < 60u; i++) {
        const HearthOamSprite *o = &ppu->oam[i];
        s16 sx = (s16)(o->x - 8 + ppu->shake_x);
        s16 sy = (s16)(o->y - ((o->layer == 2) ? 8 : 16) + ppu->shake_y);
        u16 pal, tile;
        u8 flip_x;

        if (sx < -16 || sx >= 320 || sy < -16 || sy >= 224) continue;
        /* Keep sprites behind bottom dialogue/battle window */
        if ((g_win_layout == 2 || g_win_layout == 3) && sy >= 148) continue;
        if (g_win_layout == 0 && sy < 8) continue;

        pal = sprite_palette(o->sprite_id);
        tile = (u16)(VRAM_SPR_BASE + (u16)o->sprite_id * 4u);
        flip_x = (o->flags & OAM_FLAG_FLIP_X) ? 1u : 0u;
        VDP_setSpriteFull(count, sx, sy, SPRITE_SIZE(2, 2),
            TILE_ATTR_FULL(pal, 0, 0, flip_x, tile), (u8)(count + 1u));
        count++;
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
        VDP_updateSprites(1, DMA);
    } else {
        /* Patch last sprite link to 0 to terminate SAT cleanly */
        const HearthOamSprite *last = &ppu->oam[0];
        (void)last;
        VDP_setSpriteFull(count, -32, -32, SPRITE_SIZE(1, 1), 0, 0);
        VDP_updateSprites((u16)(count + 1u), DMA);
    }
}

/* ============================================================================
 * 6. DECAYING SN76489 PSG SFX + PASTORAL / BATTLE BGM SEQUENCER (ZERO STUCK BEEP!)
 * ============================================================================ */
static u8  g_psg_env[4]   = { 15, 15, 15, 15 };
static u8  g_psg_decay[4] = { 4, 4, 2, 2 };
static u8  g_psg_cnt[4]   = { 0, 0, 0, 0 };
static u8  g_bgm_step     = 0;
static u8  g_bgm_timer    = 0;

/* Gentle pastoral 3/4 waltz melody & bass (Hz, 0 = rest) */
static const u16 PSG_HAMLET_LEAD[24] = {
    392, 494, 587, 587, 659, 587,  494, 494, 392, 0,   440, 494,
    523, 523, 659, 587, 523, 494,  440, 440, 494, 523, 587, 392
};
static const u16 PSG_HAMLET_BASS[24] = {
    196, 294, 294, 165, 247, 247,  196, 294, 294, 147, 220, 220,
    262, 196, 196, 147, 220, 220,  220, 165, 147, 220, 196, 196
};
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
    PSG_setFrequency(ch, hz);
    PSG_setEnvelope(ch, start_env);
    g_psg_env[ch]   = start_env;
    g_psg_decay[ch] = decay_frames;
    g_psg_cnt[ch]   = 0;
}

static void psg_trigger_noise(u8 type, u8 freq, u8 start_env, u8 decay_frames) {
    PSG_setNoise(type, freq);
    PSG_setEnvelope(3, start_env);
    g_psg_env[3]   = start_env;
    g_psg_decay[3] = decay_frames;
    g_psg_cnt[3]   = 0;
}

static void play_genesis_sfx(uint8_t sfx) {
    switch (sfx) {
        case SFX_STEP:
        case SFX_GRASS_RUSTLE:
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK2, 10, 1);
            break;
        case SFX_DASH:
        case SFX_WATER_SPLASH:
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK4, 7, 2);
            break;
        case SFX_CURSOR:
            psg_trigger_tone(2, 660, 6, 1);
            break;
        case SFX_CONFIRM:
        case SFX_EMOTE:
            psg_trigger_tone(2, 880, 5, 2);
            break;
        case SFX_CANCEL:
            psg_trigger_tone(2, 330, 6, 2);
            break;
        case SFX_TEXT_HERO:
        case SFX_TEXT_PIP:
        case SFX_TEXT_ELDER:
            psg_trigger_tone(2, (sfx == SFX_TEXT_PIP) ? 740 : 420, 9, 1);
            break;
        case SFX_HIT_NORMAL:
        case SFX_HURT:
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_TONE3, 4, 2);
            psg_trigger_tone(2, 220, 5, 2);
            break;
        case SFX_HIT_CRITICAL:
        case SFX_PARRY_CLANG:
        case SFX_LOCK_BREAK:
        case SFX_LOCK_ALL_BREAK:
            psg_trigger_tone(2, 1047, 3, 3);
            psg_trigger_noise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_CLOCK2, 5, 2);
            break;
        case SFX_HEAL:
        case SFX_HEART_TAKE:
        case SFX_HEART_GIVE:
        case SFX_VICTORY_FANFARE:
            psg_trigger_tone(2, 784, 4, 4);
            break;
        default:
            break;
    }
}

static void psg_tick_60hz(void) {
    /* 1. Decay all 4 PSG channels smoothly back to silence (PSG_ENVELOPE_MIN = 15) */
    for (u8 ch = 0; ch < 4u; ch++) {
        if (g_psg_env[ch] < PSG_ENVELOPE_MIN) {
            g_psg_cnt[ch]++;
            if (g_psg_cnt[ch] >= g_psg_decay[ch]) {
                g_psg_cnt[ch] = 0;
                g_psg_env[ch]++;
                PSG_setEnvelope(ch, g_psg_env[ch]);
            }
        }
    }

    /* 2. Step gentle 2-channel PSG BGM (Ch 0 lead, Ch 1 bass) */
    if (g_bgm_timer > 0) {
        g_bgm_timer--;
        return;
    }
    if (g_hearth.mode == MODE_BATTLE) {
        u8 idx = (u8)(g_bgm_step & 15u);
        psg_trigger_tone(0, PSG_BATTLE_LEAD[idx], 7, 3);
        psg_trigger_tone(1, PSG_BATTLE_BASS[idx], 9, 3);
        g_bgm_timer = 10;
    } else {
        u8 idx = (u8)(g_bgm_step % 24u);
        if (PSG_HAMLET_LEAD[idx] > 0) psg_trigger_tone(0, PSG_HAMLET_LEAD[idx], 8, 4);
        if (PSG_HAMLET_BASS[idx] > 0) psg_trigger_tone(1, PSG_HAMLET_BASS[idx], 10, 4);
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

static void upload_palettes_to_cram(void) {
    u16 cram[16];
    for (u16 p = 0; p < 4u; p++) {
        for (u16 c = 0; c < 16u; c++) {
            cram[c] = hearth_rgb_to_genesis_bgr333(HEARTH_PALETTES_RGB[p][c]);
        }
        PAL_setColors((u16)(p * 16u), cram, 16, CPU);
    }
}

int main(bool hard_reset) {
    (void)hard_reset;

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

    hearth_init(&g_hearth);
    g_hearth.tuning.ppu_mode = PPU_GENESIS_320x224;
    {
        const HearthPpuPacket *boot_ppu = hearth_step_60hz(&g_hearth, 0u);
        g_last_scene_battle = 0u;
        draw_full_world_viewport(boot_ppu->cam_x, boot_ppu->cam_y);
        VDP_setHorizontalScroll(BG_A, (s16)(-boot_ppu->cam_x));
        VDP_setVerticalScroll(BG_A,   (s16)( boot_ppu->cam_y));
        VDP_setHorizontalScroll(BG_B, (s16)(-boot_ppu->cam_x));
        VDP_setVerticalScroll(BG_B,   (s16)( boot_ppu->cam_y));
        update_window_ui(boot_ppu);
        render_sprites(boot_ppu);
    }

    while (TRUE) {
        uint16_t pad = read_genesis_pad();
        const HearthPpuPacket *ppu = hearth_step_60hz(&g_hearth, pad);
        u8 is_battle = (g_hearth.mode == MODE_BATTLE ||
                        g_hearth.mode == MODE_BATTLE_VICTORY ||
                        g_hearth.mode == MODE_BATTLE_DEFEAT) ? 1u : 0u;

        if (is_battle != g_last_scene_battle) {
            g_last_scene_battle = is_battle;
            if (is_battle) {
                draw_battle_backdrop(g_hearth.battle.biome);
            } else {
                draw_full_world_viewport(ppu->cam_x, ppu->cam_y);
            }
        }

        if (!is_battle) {
            stream_world_viewport(ppu->cam_x, ppu->cam_y);
            VDP_setHorizontalScroll(BG_A, (s16)(-(ppu->cam_x - ppu->shake_x)));
            VDP_setVerticalScroll(BG_A,   (s16)( (ppu->cam_y - ppu->shake_y)));
            VDP_setHorizontalScroll(BG_B, (s16)(-(ppu->cam_x - ppu->shake_x)));
            VDP_setVerticalScroll(BG_B,   (s16)( (ppu->cam_y - ppu->shake_y)));
        } else {
            VDP_setHorizontalScroll(BG_A, (s16)ppu->shake_x);
            VDP_setVerticalScroll(BG_A,   (s16)ppu->shake_y);
        }

        update_window_ui(ppu);
        render_sprites(ppu);

        for (uint8_t i = 0; i < ppu->sfx_count; i++) {
            play_genesis_sfx(ppu->sfx_queue[i]);
        }
        psg_tick_60hz();

        SYS_doVBlankProcess();
    }

    return 0;
}
