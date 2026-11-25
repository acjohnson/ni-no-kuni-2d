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
#define HEARTH_USE_PREBAKED_ROWAN 1
#include "hearth_hero24.h"
#include "rowan_gen_rom.h"
#define HEARTH_INCLUDE_GENESIS_TERRAIN 1
#include "hearth_terrain_rom.h"

/* Placed statically in Sega Genesis 64KB 68000 Work RAM (zero malloc!) */
static HearthState g_hearth;

/* ============================================================================
 * VRAM TILE INDEX ALLOCATION (Strictly below SGDK 64x64 HSCROLL=1344 & SAT=1376!)
 *   Chrono/Mana 4bpp BG terrain (392t + 1 atmosphere mote): 16..415  (400 tiles)
 *   UI boxes + fishing gauge + battle lock badges:         416..439  (24 tiles)
 *   16x16 Hardware Sprites (61 slots x 4 tiles):           440..683  (244 tiles)
 *   Opaque 5x7 ASCII Font (64 glyphs):                     684..747  (64 tiles)
 *   Shared Full-Screen/Vista Block (mutually exclusive):   748..1227 (480 tiles)
 *     - Boot Splashes (40t) / Cinema CG (<=480t) / Vista (<=256t) / World Atlas (384t)
 *   Tall 16x24 Sprites (Rowan + 13 Adult NPCs x 6):       1228..1311 (84 tiles)
 *   Headroom below 64x64 HSCROLL (0xA800 = tile 1344):     32 tiles (1.0 KB) free!
 * ============================================================================ */
/* Pin strictly to tile 16 (0x0200) independent of SGDK LEGACY_FONT_LOCATION so tiles
 * 16..1311 (0x0200..0xA3FF) NEVER overlap 64x64 HSCROLL (0xA800 = 1344) or SAT (0xAC00 = 1376)! */
#define VRAM_BG_BASE       16u
#define VRAM_BG_MOTE_TILE  ((u16)(VRAM_BG_BASE + GEN_CHRONO_TILE_COUNT))
#define VRAM_UI_BASE       ((u16)(VRAM_BG_BASE + 400u))
#define VRAM_SPR_BASE      ((u16)(VRAM_UI_BASE + 24u))
#define VRAM_FONT_BASE     ((u16)(VRAM_SPR_BASE + 244u))
#define VRAM_ATLAS_BASE    ((u16)(VRAM_FONT_BASE + 64u))
#define VRAM_CG_BASE       VRAM_ATLAS_BASE
#define VRAM_VISTA_BASE    VRAM_ATLAS_BASE
#define VRAM_TALL_SPR_BASE ((u16)(VRAM_ATLAS_BASE + 480u))

/* Nestable atomic VDP bulk-upload guard: prevents _VINT from clobbering VDP control latches
 * during multi-millisecond CPU tile/plane/CRAM uploads and restores autoInc=2 after VRAM fills. */
static u8 g_vdp_bulk_depth = 0u;

static void gen_vdp_begin_bulk(void) {
    if (g_vdp_bulk_depth == 0u) {
        VDP_waitDMACompletion();
        SYS_disableInts();
        VDP_setEnable(FALSE);
        VDP_setAutoInc(2);
    }
    g_vdp_bulk_depth++;
}

static void gen_vdp_end_bulk(void) {
    if (g_vdp_bulk_depth > 0u) {
        g_vdp_bulk_depth--;
        if (g_vdp_bulk_depth == 0u) {
            VDP_waitDMACompletion();
            VDP_setAutoInc(2);
            VDP_setEnable(TRUE);
            SYS_enableInts();
        }
    }
}

static u8 g_bgb_meta_used[32][4];

static void gen_clear_plane(VDPPlane plane) {
    VDP_clearPlane(plane, TRUE);
    VDP_setAutoInc(2);
    if (plane == BG_B) {
        hearth_memset(g_bgb_meta_used, 0, sizeof(g_bgb_meta_used));
    }
}

#define GEN_ATLAS_TW    24u
#define GEN_ATLAS_TH    16u

/* UI Box & Pixel-Art Minigame/Battle Tile IDs (offsets from VRAM_UI_BASE, 22 < 32) */
enum {
    GUI_FILL = 0,
    GUI_TOP,
    GUI_BOT,
    GUI_LEFT,
    GUI_RIGHT,
    GUI_TL,
    GUI_TR,
    GUI_BL,
    GUI_BR,
    GUI_FISH_CAP_L,
    GUI_FISH_CAP_R,
    GUI_FISH_WATER,
    GUI_FISH_ZONE_L,
    GUI_FISH_ZONE_M,
    GUI_FISH_ZONE_R,
    GUI_BAR_BG,
    GUI_BAR_GRN,
    GUI_BAR_RED,
    GUI_LOCK_SWORD,
    GUI_LOCK_SUN,
    GUI_LOCK_STAR,
    GUI_LOCK_BROKEN
};

/* Sprite VRAM Slots (49 core sprites + 12 host internal slots = 61 x 4 = 244 tiles) */
#define SPR_TIMED_RING_SLOT    49u
#define SPR_HERO_DOWN_1        50u
#define SPR_HERO_UP_0          51u
#define SPR_HERO_UP_1          52u
#define SPR_HERO_SIDE_0        53u
#define SPR_HERO_SIDE_1        54u
#define SPR_BATTLE_VFX_SLOT    55u
#define SPR_ATLAS_HERO_SLOT    56u
#define SPR_ATLAS_RETICLE_SLOT 57u
#define SPR_FISH_ICON_SLOT     58u
#define SPR_PIP_HOP_SLOT       59u
#define SPR_PIP_SQUASH_SLOT    60u

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

/* 128KB-boundary-safe VBlank DMA_QUEUE ROM & staged WRAM tile uploaders:
 *  - Every tile upload uses the hardware-proven DMA_QUEUE + SYS_doVBlankProcess() path.
 *  - Never crosses a 68000 0x20000 DMA bank wrap; straddling tiles copy via WRAM g_dma_edge.
 *  - Caps each VBlank burst at <= 64 ROM tiles (2,048 B) or <= 32 staged tiles (1,024 B). */
static u32 g_dma_edge[4][8];
static u8  g_dma_edge_slot = 0u;
static u32 g_dma_stage[32u * 8u];
static u16 g_dma_stage_base = 0u;
static u16 g_dma_stage_cnt  = 0u;

static void gen_queue_rom_tiles_dma(const u32 *src, u16 dst_tile, u16 count) {
    while (count > 0u) {
        u32 bank_off = (u32)((unsigned long)src & 0x1FFFFUL);
        u32 rem_bytes = 0x20000u - bank_off;
        u16 safe_tiles = (u16)(rem_bytes >> 5);
        if (safe_tiles == 0u) {
            /* Single 32-byte tile straddles a 128KB ROM boundary -> copy to WRAM staging slot */
            u32 *slot = g_dma_edge[g_dma_edge_slot & 3u];
            g_dma_edge_slot++;
            for (u16 k = 0u; k < 8u; k++) slot[k] = src[k];
            VDP_loadTileData(slot, dst_tile, 1u, DMA_QUEUE);
            src += 8u;
            dst_tile++;
            count--;
        } else {
            u16 chunk = (count < safe_tiles) ? count : safe_tiles;
            VDP_loadTileData(src, dst_tile, chunk, DMA_QUEUE);
            src += (u32)chunk * 8u;
            dst_tile = (u16)(dst_tile + chunk);
            count = (u16)(count - chunk);
        }
    }
}

static void gen_upload_rom_tiles_dma(const u32 *src, u16 dst_tile, u16 count) {
    /* Must ONLY be called with interrupts enabled (g_vdp_bulk_depth == 0)! */
    while (count > 0u) {
        u16 chunk = (count > 64u) ? 64u : count;
        gen_queue_rom_tiles_dma(src, dst_tile, chunk);
        SYS_doVBlankProcess();
        src += (u32)chunk * 8u;
        dst_tile = (u16)(dst_tile + chunk);
        count = (u16)(count - chunk);
    }
}

static void gen_dma_stage_flush(void) {
    if (g_dma_stage_cnt > 0u) {
        VDP_loadTileData(g_dma_stage, g_dma_stage_base, g_dma_stage_cnt, DMA_QUEUE);
        SYS_doVBlankProcess();
        g_dma_stage_cnt = 0u;
    }
}

static void gen_dma_stage_push(u16 dst_tile, const u32 *tiles_u32, u16 count) {
    /* Must ONLY be called with interrupts enabled (g_vdp_bulk_depth == 0)! */
    while (count > 0u) {
        u16 chunk = (count > 32u) ? 32u : count;
        if (g_dma_stage_cnt > 0u &&
            ((u16)(g_dma_stage_base + g_dma_stage_cnt) != dst_tile ||
             (u16)(g_dma_stage_cnt + chunk) > 32u)) {
            gen_dma_stage_flush();
        }
        if (g_dma_stage_cnt == 0u) {
            g_dma_stage_base = dst_tile;
        }
        {
            u32 *dst_p = &g_dma_stage[(u32)g_dma_stage_cnt * 8u];
            u32 words = (u32)chunk * 8u;
            for (u32 wi = 0u; wi < words; wi++) dst_p[wi] = tiles_u32[wi];
        }
        g_dma_stage_cnt = (u16)(g_dma_stage_cnt + chunk);
        tiles_u32 += (u32)chunk * 8u;
        dst_tile = (u16)(dst_tile + chunk);
        count = (u16)(count - chunk);
        if (g_dma_stage_cnt >= 32u) {
            gen_dma_stage_flush();
        }
    }
}

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
    gen_dma_stage_push(tile_idx, tile, 1u);
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
    gen_dma_stage_push(base_tile, tiles, 4u);
}

/* Genesis 2x3 (16x24) tall hardware sprites are COLUMN-MAJOR:
 * Tile 0=(0,0), Tile 1=(0,8), Tile 2=(0,16), Tile 3=(8,0), Tile 4=(8,8), Tile 5=(8,16) */
static u8  g_pix24[384];
static u8  g_last_rowan_key = 0xFFu;

static u8 gen_rowan_pose_idx(u8 dir, u8 pose) {
    static const u8 DIR_BASE[4] = { 0u, 7u, 14u, 21u };
    if (pose >= HERO_POSE_BTL_READY) {
        u8 bp = (u8)(pose - HERO_POSE_BTL_READY);
        return (u8)(28u + ((bp < 5u) ? bp : 0u));
    }
    return (u8)(DIR_BASE[dir & 3u] + pose);
}

static void pack_pix16x24_sprite_to_buf(const u8 *src384, u32 dst[48]) {
    static const u8 ox[6] = { 0, 0, 0, 8, 8, 8 };
    static const u8 oy[6] = { 0, 8, 16, 0, 8, 16 };
    for (u16 t = 0; t < 6u; t++) {
        for (u16 y = 0; y < 8u; y++) {
            u32 row = 0;
            for (u16 x = 0; x < 8u; x++) {
                u8 c = src384[(u16)(oy[t] + y) * 16u + (u16)(ox[t] + x)] & 0x0Fu;
                row = (row << 4) | (u32)c;
            }
            dst[t * 8u + y] = row;
        }
    }
}

/* Map tall 16x24 character sprite_ids to VRAM_TALL_SPR_BASE slots 0..13 (0xFF = standard 16x16) */
static u8 genesis_tall_slot(u8 sprite_id) {
    switch (sprite_id) {
        case SPR_HERO:            return 0u;
        case SPR_NPC_MARA:        return 1u;
        case SPR_NPC_ELDER:       return 2u;
        case SPR_NPC_CLARA:       return 3u;
        case SPR_NPC_GUARD:       return 4u;
        case SPR_NPC_BARNABY:     return 5u;
        case SPR_NPC_MAPLE:       return 6u;
        case SPR_NPC_JASPER:      return 7u;
        case SPR_NPC_TESSA:       return 8u;
        case SPR_NPC_CORIN:       return 9u;
        case SPR_NPC_SENTINEL:    return 10u;
        case SPR_NPC_TOWNSFOLK:   return 11u;
        case SPR_NPC_SHADOW_GRAN: return 12u;
        case SPR_NPC_SHIPWRIGHT:  return 13u;
        default:                  return 0xFFu;
    }
}

/* Persistent RAM buffer for rotating 16x16 windmill blades (Chrono terrain anim streams from ROM!) */
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

static void bake_bg_terrain_tiles(void) {
    /* 128KB-boundary-safe VBlank DMA_QUEUE upload of all 399 pre-baked Chrono/Mana 4bpp tiles */
    gen_dma_stage_flush();
    gen_upload_rom_tiles_dma(GEN_CHRONO_TILES, VRAM_BG_BASE, GEN_CHRONO_TILE_COUNT);

    /* Crisp 8x8 organic drifting petal / maple leaf / firefly mote for hardware SAT atmosphere */
    p8_fill(0);
    g_pix8[2 * 8 + 3] = 14; g_pix8[2 * 8 + 4] = 15;
    g_pix8[3 * 8 + 2] = 14; g_pix8[3 * 8 + 3] = 15; g_pix8[3 * 8 + 4] = 15; g_pix8[3 * 8 + 5] = 13;
    g_pix8[4 * 8 + 2] = 13; g_pix8[4 * 8 + 3] = 14; g_pix8[4 * 8 + 4] = 14;
    g_pix8[5 * 8 + 3] = 12;
    upload_pix8_to_vram(VRAM_BG_MOTE_TILE);
    gen_dma_stage_flush();
}

static void bake_ui_and_font_tiles(void) {
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

    /* True 4bpp Pixel-Art Fishing Gauge, Progress/Tension Bars & Battle Lock Icons */
    /* GUI_FISH_CAP_L (PAL2: dark slate 1, bronze 6, gold 7, cream 15, ocean 8) */
    p8_fill(1);
    p8_rect(2, 0, 6, 8, 6); p8_rect(3, 1, 5, 6, 7); p8_rect(4, 2, 4, 4, 8);
    g_pix8[1*8+3] = 15; g_pix8[1*8+4] = 15;
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_FISH_CAP_L);

    /* GUI_FISH_CAP_R (PAL2) */
    p8_fill(1);
    p8_rect(0, 0, 6, 8, 6); p8_rect(0, 1, 5, 6, 7); p8_rect(0, 2, 4, 4, 8);
    g_pix8[1*8+4] = 15; g_pix8[1*8+3] = 15;
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_FISH_CAP_R);

    /* GUI_FISH_WATER (PAL2: framed azure water trough with shimmer highlights) */
    p8_fill(8);
    p8_rect(0, 0, 8, 1, 6); p8_rect(0, 7, 8, 1, 6);
    p8_rect(0, 1, 8, 1, 1); p8_rect(0, 6, 8, 1, 1);
    p8_rect(1, 2, 3, 1, 9); p8_rect(5, 4, 3, 1, 9);
    g_pix8[2*8+2] = 15;
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_FISH_WATER);

    /* GUI_FISH_ZONE_L / M / R (PAL3: glowing emerald catch zone with golden rim) */
    p8_fill(7);
    p8_rect(0, 0, 8, 1, 11); p8_rect(0, 7, 8, 1, 11);
    p8_rect(0, 1, 1, 6, 12); p8_rect(1, 1, 7, 1, 8); p8_rect(1, 6, 7, 1, 6);
    p8_rect(2, 2, 5, 2, 8);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_FISH_ZONE_L);

    p8_fill(7);
    p8_rect(0, 0, 8, 1, 11); p8_rect(0, 7, 8, 1, 11);
    p8_rect(0, 1, 8, 1, 8);  p8_rect(0, 6, 8, 1, 6);
    p8_rect(1, 2, 6, 2, 8);  p8_rect(3, 3, 2, 1, 12);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_FISH_ZONE_M);

    p8_fill(7);
    p8_rect(0, 0, 8, 1, 11); p8_rect(0, 7, 8, 1, 11);
    p8_rect(7, 1, 1, 6, 12); p8_rect(0, 1, 7, 1, 8); p8_rect(0, 6, 7, 1, 6);
    p8_rect(1, 2, 5, 2, 8);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_FISH_ZONE_R);

    /* GUI_BAR_BG (PAL2: recessed dark slate bar track) */
    p8_fill(1);
    p8_rect(0, 1, 8, 6, 11); p8_rect(0, 2, 8, 4, 1);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_BAR_BG);

    /* GUI_BAR_GRN (PAL3: glossy emerald catch progress segment) */
    p8_fill(1);
    p8_rect(0, 1, 8, 6, 6); p8_rect(0, 2, 8, 4, 7); p8_rect(0, 2, 7, 1, 8);
    g_pix8[2*8+1] = 12; g_pix8[2*8+2] = 12;
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_BAR_GRN);

    /* GUI_BAR_RED (PAL3: glossy crimson tension meter segment) */
    p8_fill(1);
    p8_rect(0, 1, 8, 6, 9); p8_rect(0, 2, 8, 4, 10); p8_rect(0, 2, 7, 1, 11);
    g_pix8[2*8+1] = 12; g_pix8[2*8+2] = 12;
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_BAR_RED);

    /* GUI_LOCK_SWORD (PAL2: 8x8 steel blade & golden hilt badge) */
    p8_fill(1);
    p8_rect(0, 0, 8, 1, 6); p8_rect(0, 7, 8, 1, 6);
    p8_rect(0, 0, 1, 8, 6); p8_rect(7, 0, 1, 8, 6);
    g_pix8[1*8+6] = 15; g_pix8[2*8+5] = 4; g_pix8[3*8+4] = 4; g_pix8[4*8+3] = 5;
    g_pix8[3*8+2] = 7;  g_pix8[4*8+2] = 7; g_pix8[5*8+3] = 7; g_pix8[5*8+4] = 7;
    g_pix8[6*8+1] = 7;  g_pix8[5*8+2] = 11;
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_LOCK_SWORD);

    /* GUI_LOCK_SUN (PAL3: 8x8 radiant solar emblem badge) */
    p8_fill(1);
    p8_rect(0, 0, 8, 1, 9); p8_rect(0, 7, 8, 1, 9);
    p8_rect(0, 0, 1, 8, 9); p8_rect(7, 0, 1, 8, 9);
    p8_rect(3, 3, 2, 2, 12);
    g_pix8[1*8+3] = 11; g_pix8[1*8+4] = 11; g_pix8[6*8+3] = 11; g_pix8[6*8+4] = 11;
    g_pix8[3*8+1] = 11; g_pix8[4*8+1] = 11; g_pix8[3*8+6] = 11; g_pix8[4*8+6] = 11;
    g_pix8[2*8+2] = 10; g_pix8[2*8+5] = 10; g_pix8[5*8+2] = 10; g_pix8[5*8+5] = 10;
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_LOCK_SUN);

    /* GUI_LOCK_STAR (PAL3: 8x8 arcane cyan starlight emblem badge) */
    p8_fill(1);
    p8_rect(0, 0, 8, 1, 13); p8_rect(0, 7, 8, 1, 13);
    p8_rect(0, 0, 1, 8, 13); p8_rect(7, 0, 1, 8, 13);
    p8_rect(3, 1, 2, 6, 15); p8_rect(1, 3, 6, 2, 15);
    p8_rect(3, 3, 2, 2, 12);
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_LOCK_STAR);

    /* GUI_LOCK_BROKEN (PAL2: dimmed shattered lock socket) */
    p8_fill(1);
    p8_rect(0, 0, 8, 1, 11); p8_rect(0, 7, 8, 1, 11);
    p8_rect(0, 0, 1, 8, 11); p8_rect(7, 0, 1, 8, 11);
    g_pix8[2*8+2] = 5; g_pix8[3*8+3] = 5; g_pix8[4*8+4] = 5; g_pix8[5*8+5] = 5;
    g_pix8[2*8+5] = 5; g_pix8[5*8+2] = 5;
    upload_pix8_to_vram(VRAM_UI_BASE + GUI_LOCK_BROKEN);

    /* Fast direct-packed 8x8 ASCII Font (bg=1 dark slate, fg=15 warm cream in PAL2/PAL3) */
    for (u16 ch = 0u; ch < 64u; ch++) {
        u32 tile[8];
        for (u8 y = 0u; y < 7u; y++) {
            u8 bits = GEN_FONT_5X7[ch][y];
            u32 row = 0x11111111u;
            if (bits & 0x10u) row |= 0x0E000000u;
            if (bits & 0x08u) row |= 0x00E00000u;
            if (bits & 0x04u) row |= 0x000E0000u;
            if (bits & 0x02u) row |= 0x0000E000u;
            if (bits & 0x01u) row |= 0x00000E00u;
            tile[y] = row;
        }
        tile[7] = 0x11111111u;
        gen_dma_stage_push((u16)(VRAM_FONT_BASE + ch), tile, 1u);
    }
    gen_dma_stage_flush();

    /* Sweeping 4bpp Alpine Parallax Vista tiles are baked dynamically into VRAM_VISTA_BASE
     * whenever entering exploration after boot or after an illustrated cinema sequence. */
}

/* Dedicated 16-Color Full-Color World Atlas Palette (Loaded into PAL0 while Atlas is open; 0 = VDP transparent) */
static const uint32_t ATLAS_PAL_RGB[16] = {
    0x000000u, /* 0: VDP Transparent (unused in Atlas bitmap) */
    0x161322u, /* 1: Coastline / Pin Dark Shadow */
    0x1F4C82u, /* 2: Deep Cobalt Ocean (cell 0) */
    0x3EB4CCu, /* 3: Turquoise Shallows / Raft Channel (cell 1) */
    0xE87EA8u, /* 4: Coral Reef Shoals (cell 2) */
    0x5EA84Eu, /* 5: Evertale Heartland (cell 3) */
    0xE4BC62u, /* 6: Sands of Babana (cell 4) */
    0x56C49Au, /* 7: Suncrest Paradise (cell 5) */
    0xE6F0FCu, /* 8: Frostmere North (cell 6) */
    0x226E3Au, /* 9: Canopy Wilds (cell 7) */
    0xD44824u, /* 10: Cinderpeak Caldera (cell 8) */
    0x4898C8u, /* 11: Aetheris Citadel (cell 9) */
    0xC8963Eu, /* 12: Gilded Bronze Map Frame */
    0xFFF1A8u, /* 13: Golden Enterable Town/Port Pin */
    0x74D8F5u, /* 14: Sky Cyan Landmark Vista Pin */
    0x2B629Cu  /* 15: Subtle Ocean Graticule Line */
};

static u8 g_atlas_baked = 0u;
static u8 g_vista_vram_dirty = 1u;

/* Fast zero-DIVU 24x16 (192x128 px) 4bpp Pixel-Art Evertale World Atlas baker (lazy-baked on first Atlas open) */
static void bake_genesis_atlas_bitmap(void) {
    static u8 row3[3][HEARTH_ATLAS_W];
    static u8 below[HEARTH_ATLAS_W];
    static u8 px_ax[192];
    static u8 px_mod3[192];
    static u8 px_grat[192];
    s16 lm_px[HEARTH_GLOBE_LANDMARK_COUNT];
    s16 lm_py[HEARTH_GLOBE_LANDMARK_COUNT];
    u8  lm_col[HEARTH_GLOBE_LANDMARK_COUNT];
    u16 tile_idx = 0u;

    /* Precompute px/3, px%3, and px%24==0 via pure additions (eliminates 98,304 68000 DIVU calls!) */
    {
        u8 cur_ax = 0u;
        u8 cur_m3 = 0u;
        u8 cur_m24 = 0u;
        for (u16 px = 0u; px < 192u; px++) {
            px_ax[px] = (cur_ax < HEARTH_ATLAS_W) ? cur_ax : (u8)(HEARTH_ATLAS_W - 1u);
            px_mod3[px] = cur_m3;
            px_grat[px] = (cur_m24 == 0u) ? 1u : 0u;
            cur_m3++;
            if (cur_m3 == 3u) { cur_m3 = 0u; cur_ax++; }
            cur_m24++;
            if (cur_m24 == 24u) { cur_m24 = 0u; }
        }
    }

    /* Precompute landmark pixel centers once (eliminates 344,064 inner-loop MULU calls!) */
    for (u8 i = 0u; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
        const HearthGlobeLandmark *lm = &HEARTH_GLOBE_LANDMARKS[i];
        lm_px[i]  = (s16)(((u16)lm->tx * 3u) >> 2);
        lm_py[i]  = (s16)(((u16)lm->ty * 171u) >> 8);
        lm_col[i] = lm->enterable ? 13u : 14u;
    }

    for (u16 ty = 0u; ty < GEN_ATLAS_TH; ty++) {
        u8 ay0 = (u8)(ty * 3u);
        u8 ay_next = (u8)((ay0 + 3u < HEARTH_ATLAS_H) ? (ay0 + 3u) : (HEARTH_ATLAS_H - 1u));
        s16 ty0 = (s16)(ty << 3);
        for (u8 ax = 0u; ax < HEARTH_ATLAS_W; ax++) {
            row3[0][ax] = hearth_globe_atlas_cell(ax, ay0);
            row3[1][ax] = hearth_globe_atlas_cell(ax, (u8)(ay0 + 1u));
            row3[2][ax] = hearth_globe_atlas_cell(ax, (u8)(ay0 + 2u));
            below[ax]   = hearth_globe_atlas_cell(ax, ay_next);
        }

        for (u16 tx = 0u; tx < GEN_ATLAS_TW; tx++) {
            s16 tx0 = (s16)(tx << 3);
            u8 hit_lm[4];
            u8 hit_cnt = 0u;

            /* Tile-level 8x8 bounding-box cull for 5x5 landmark pins (~93% of tiles skip inner loop!) */
            for (u8 i = 0u; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
                s16 ldx = (s16)(lm_px[i] - tx0);
                s16 ldy = (s16)(lm_py[i] - ty0);
                if (ldx >= -2 && ldx <= 9 && ldy >= -2 && ldy <= 9 && hit_cnt < 4u) {
                    hit_lm[hit_cnt++] = i;
                }
            }

            for (u8 ly = 0u; ly < 8u; ly++) {
                u8 py = (u8)((ty << 3) + ly);
                u8 sub = (ly < 3u) ? 0u : ((ly < 5u) ? 1u : 2u);
                u8 py_grat = (py == 0u || py == 24u || py == 48u ||
                              py == 72u || py == 96u || py == 120u) ? 1u : 0u;
                u8 py_even = ((py & 1u) == 0u) ? 1u : 0u;
                u8 is_row_seam = (ly == 3u || ly == 5u) ? 1u : 0u;

                for (u8 lx = 0u; lx < 8u; lx++) {
                    u8 px = (u8)((tx << 3) + lx);
                    u8 ax = px_ax[px];
                    u8 cell = row3[sub][ax];
                    u8 col = (u8)((cell < 10u ? cell : 0u) + 2u);

                    /* Subtle latitude/longitude graticule across deep ocean */
                    if (cell == 0u && ((px_grat[px] && py_even) || (py_grat && ((px & 1u) == 0u)))) {
                        col = 15u;
                    }
                    /* Crisp 1px coastal shadow along south/east continent shoreline */
                    if (cell < 3u) {
                        if (is_row_seam && sub > 0u && row3[sub - 1u][ax] >= 3u) {
                            col = 1u;
                        } else if (px_mod3[px] == 0u && ax > 0u && row3[sub][ax - 1u] >= 3u) {
                            col = 1u;
                        }
                    } else if (ly == 7u && below[ax] < 3u) {
                        col = 1u;
                    }

                    /* Crisp 5x5 framed landmark pins (only evaluated on intersecting tiles) */
                    for (u8 h = 0u; h < hit_cnt; h++) {
                        u8 li = hit_lm[h];
                        s16 dx = (s16)px - lm_px[li];
                        s16 dy = (s16)py - lm_py[li];
                        if (dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2) {
                            if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) {
                                col = lm_col[li];
                            } else if ((dx != -2 && dx != 2) || (dy != -2 && dy != 2)) {
                                col = 1u;
                            }
                        }
                    }

                    /* Gilded bronze outer border around the 192x128 World Atlas */
                    if (px == 0u || px == 191u || py == 0u || py == 127u) {
                        col = 12u;
                    }
                    g_pix8[(u16)ly * 8u + lx] = col;
                }
            }
            upload_pix8_to_vram((u16)(VRAM_ATLAS_BASE + tile_idx));
            tile_idx++;
        }
    }
    gen_dma_stage_flush();
    g_atlas_baked = 1u;
    g_vista_vram_dirty = 1u; /* Shares 480-tile block with Alpine Parallax Vista */
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

    /* 1: SPR_PIP (Petite ~11x13 Wingless Garden-Gnome Pip — Pose 0: Grounded Idle - PAL1) */
    hearth_bake_pip_16x16(g_pix16, 0u);
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

    /* 23: SPR_PET_MILO (Mido — Amber Mountain Fox-Kit with Crimson Scarf & Bell - PAL1) */
    p16_clear();
    p16_rect(4, 1, 2, 3, 4); p16_rect(10, 1, 2, 3, 4);     /* Pointed dark-tipped fox ears */
    p16_rect(5, 3, 6, 5, 10); p16_rect(6, 5, 4, 3, 14);    /* Amber fox head + cream snout */
    g_pix16[5*16+6]=1; g_pix16[5*16+9]=1; g_pix16[7*16+7]=1;
    p16_rect(4, 8, 8, 2, 9); g_pix16[9*16+7]=11; g_pix16[9*16+8]=11; /* Crimson scarf & brass bell */
    p16_rect(4, 10, 8, 4, 10); p16_rect(6, 10, 4, 3, 14);  /* Body & cream chest */
    p16_rect(12, 7, 3, 4, 10); p16_rect(13, 6, 2, 2, 14);  /* Bushy cream-tipped fox tail */
    p16_rect(5, 14, 2, 2, 4); p16_rect(9, 14, 2, 2, 4);    /* Dark fox paws */
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

    /* 37: SPR_NPC_TESSA (Sunhill Market Baker Tessa - PAL2) */
    p16_clear();
    p16_rect(3, 1, 10, 2, 10); p16_rect(4, 3, 8, 2, 11);
    p16_rect(5, 5, 6, 3, 2); g_pix16[6*16+6]=1; g_pix16[6*16+9]=1;
    p16_rect(4, 8, 8, 6, 5); p16_rect(6, 9, 4, 5, 15);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_TESSA * 4);

    /* 38: SPR_NPC_CORIN (Sunhill Archivist Corin - PAL2) */
    p16_clear();
    p16_rect(4, 1, 8, 3, 15); p16_rect(4, 4, 8, 4, 2);
    g_pix16[5*16+5]=9; g_pix16[5*16+6]=9; g_pix16[5*16+9]=9; g_pix16[5*16+10]=9;
    p16_rect(3, 8, 10, 6, 8); p16_rect(7, 9, 2, 4, 7);
    p16_rect(12, 3, 2, 12, 11);
    p16_rect(5, 14, 2, 2, 11); p16_rect(9, 14, 2, 2, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_CORIN * 4);

    /* 39: SPR_NPC_SENTINEL (Castle Aurelia Gate Sentinel - PAL2) */
    p16_clear();
    p16_rect(7, 0, 2, 2, 7); p16_rect(4, 2, 8, 3, 15);
    p16_rect(5, 5, 6, 3, 2); g_pix16[6*16+6]=1; g_pix16[6*16+9]=1;
    p16_rect(3, 8, 10, 6, 8); p16_rect(6, 9, 4, 4, 7);
    p16_rect(13, 1, 2, 14, 15);
    p16_rect(5, 14, 2, 2, 4); p16_rect(9, 14, 2, 2, 4);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_SENTINEL * 4);

    /* 40: SPR_RAFT (Cedar Log Raft - PAL1) */
    p16_clear();
    p16_rect(1, 3, 14, 10, 4); p16_rect(2, 4, 12, 8, 5);
    p16_rect(1, 6, 14, 1, 10); p16_rect(1, 10, 14, 1, 10);
    p16_rect(4, 3, 1, 10, 1);  p16_rect(8, 3, 1, 10, 1); p16_rect(11, 3, 1, 10, 1);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_RAFT * 4);

    /* 41: SPR_ENEMY_SCORPION (Dune Scorpion - PAL3) */
    p16_clear();
    p16_rect(3, 7, 10, 6, 10); p16_rect(4, 8, 8, 4, 11);
    p16_rect(1, 5, 3, 4, 10);  p16_rect(12, 5, 3, 4, 10);
    p16_rect(6, 2, 4, 5, 10);  p16_rect(8, 1, 3, 2, 12);
    g_pix16[8*16+6]=1; g_pix16[8*16+9]=1;
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ENEMY_SCORPION * 4);

    /* 42: SPR_ENEMY_SANDWORM (Giant Sandworm - PAL3) */
    p16_clear();
    p16_rect(2, 2, 12, 13, 10); p16_rect(3, 3, 10, 10, 11);
    p16_rect(4, 4, 8, 5, 1);    p16_rect(5, 5, 2, 2, 12); p16_rect(9, 5, 2, 2, 12);
    p16_rect(2, 10, 12, 2, 6);  p16_rect(3, 13, 10, 2, 6);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ENEMY_SANDWORM * 4);

    /* 43: SPR_FAUNA_CAMEL (Desert Camel - PAL2) */
    p16_clear();
    p16_rect(4, 4, 8, 6, 11);  p16_rect(6, 2, 4, 3, 12);
    p16_rect(11, 3, 4, 4, 11); g_pix16[3*16+14]=1;
    p16_rect(4, 10, 2, 5, 11); p16_rect(10, 10, 2, 5, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_FAUNA_CAMEL * 4);

    /* 44: SPR_FAUNA_PENGUIN (Frostmere Penguin - PAL2) */
    p16_clear();
    p16_rect(4, 2, 8, 12, 1);  p16_rect(5, 6, 6, 7, 15);
    p16_rect(6, 4, 4, 2, 7);   g_pix16[3*16+5]=15; g_pix16[3*16+10]=15;
    p16_rect(4, 14, 3, 2, 7);  p16_rect(9, 14, 3, 2, 7);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_FAUNA_PENGUIN * 4);

    /* 45: SPR_FAUNA_MONKEY (Canopy Monkey - PAL2) */
    p16_clear();
    p16_rect(2, 3, 3, 3, 11);  p16_rect(11, 3, 3, 3, 11);
    p16_rect(4, 2, 8, 6, 11);  p16_rect(5, 4, 6, 4, 2);
    g_pix16[4*16+6]=1; g_pix16[4*16+9]=1;
    p16_rect(4, 8, 8, 6, 11);  p16_rect(12, 7, 3, 4, 11);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_FAUNA_MONKEY * 4);

    /* 46: SPR_FAUNA_ROBOT (Aetheris Automaton - PAL2) */
    p16_clear();
    p16_rect(7, 0, 2, 2, 7);   p16_rect(4, 2, 8, 5, 5);
    p16_rect(5, 4, 6, 2, 9);   g_pix16[4*16+6]=15; g_pix16[4*16+9]=15;
    p16_rect(3, 7, 10, 6, 4);  p16_rect(5, 8, 6, 4, 9);
    p16_rect(5, 13, 2, 3, 5);  p16_rect(9, 13, 2, 3, 5);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_FAUNA_ROBOT * 4);

    /* 47: SPR_NPC_SHIPWRIGHT (Shipwright Orin - PAL2) */
    p16_clear();
    p16_rect(3, 1, 10, 3, 8);  p16_rect(4, 4, 8, 4, 2);
    g_pix16[5*16+6]=1; g_pix16[5*16+9]=1;
    p16_rect(4, 7, 8, 2, 11);
    p16_rect(3, 9, 10, 5, 9);  p16_rect(5, 9, 6, 5, 11);
    p16_rect(5, 14, 2, 2, 4);  p16_rect(9, 14, 2, 2, 4);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_NPC_SHIPWRIGHT * 4);

    /* 48: SPR_BABANA_STALL (Golden Babana Stall - PAL2) */
    p16_clear();
    p16_rect(1, 1, 14, 4, 7);  p16_rect(2, 2, 12, 2, 10);
    p16_rect(2, 5, 2, 10, 11); p16_rect(12, 5, 2, 10, 11);
    p16_rect(1, 9, 14, 5, 11); p16_rect(4, 7, 8, 3, 7);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_BABANA_STALL * 4);

    /* 49: Timed-Hit / Parry Ring Marker (PAL3) */
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

    /* 55: SPR_BATTLE_VFX_SLOT (Star / Solar / Slash Choreography Burst - PAL3) */
    p16_clear();
    p16_rect(7, 0, 2, 16, 11); p16_rect(0, 7, 16, 2, 11);
    p16_rect(3, 3, 10, 10, 15);
    p16_rect(5, 5, 6, 6, 12);
    p16_rect(2, 2, 2, 2, 10); p16_rect(12, 2, 2, 2, 10);
    p16_rect(2, 12, 2, 2, 10); p16_rect(12, 12, 2, 2, 10);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_BATTLE_VFX_SLOT * 4);

    /* 56: SPR_ATLAS_HERO_SLOT (Blinking Compass Beacon for Rowan on World Atlas - PAL1/PAL3) */
    p16_clear();
    p16_rect(7, 2, 2, 12, 1);  p16_rect(2, 7, 12, 2, 1);
    p16_rect(4, 4, 8, 8, 1);   p16_rect(5, 5, 6, 6, 9);
    p16_rect(6, 6, 4, 4, 11);  g_pix16[7*16+7] = 14; g_pix16[7*16+8] = 14;
    g_pix16[3*16+7] = 11; g_pix16[3*16+8] = 11;
    g_pix16[12*16+7] = 11; g_pix16[12*16+8] = 11;
    g_pix16[7*16+3] = 11; g_pix16[8*16+3] = 11;
    g_pix16[7*16+12] = 11; g_pix16[8*16+12] = 11;
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ATLAS_HERO_SLOT * 4);

    /* 57: SPR_ATLAS_RETICLE_SLOT (Designer Continent Warp Reticle Corners - PAL3/PAL1) */
    p16_clear();
    p16_rect(1, 1, 5, 2, 11);  p16_rect(1, 1, 2, 5, 11);
    p16_rect(10, 1, 5, 2, 11); p16_rect(13, 1, 2, 5, 11);
    p16_rect(1, 13, 5, 2, 11); p16_rect(1, 10, 2, 5, 11);
    p16_rect(10, 13, 5, 2, 11); p16_rect(13, 10, 2, 5, 11);
    g_pix16[2*16+2] = 15; g_pix16[2*16+13] = 15;
    g_pix16[13*16+2] = 15; g_pix16[13*16+13] = 15;
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_ATLAS_RETICLE_SLOT * 4);

    /* 58: SPR_FISH_ICON_SLOT (16x16 Storybook Swimming & Trophy Fish facing RIGHT - PAL1) */
    p16_clear();
    p16_rect(1, 5, 3, 6, 12);  g_pix16[4*16+1] = 13; g_pix16[11*16+1] = 13; /* Forked tail fin */
    g_pix16[7*16+1] = 0;       g_pix16[8*16+1] = 0;
    p16_rect(4, 4, 8, 8, 13);  p16_rect(5, 3, 6, 10, 13);                   /* Shimmering body */
    p16_rect(6, 2, 4, 2, 12);                                               /* Dorsal fin */
    p16_rect(12, 5, 3, 6, 13); p16_rect(14, 6, 2, 4, 14);                   /* Nose & snout */
    p16_rect(6, 5, 6, 2, 14);                                               /* Silver dorsal highlight */
    p16_rect(5, 9, 7, 3, 10);  p16_rect(6, 10, 5, 2, 11);                   /* Golden belly */
    p16_rect(7, 12, 3, 2, 9);                                               /* Ventral fin */
    g_pix16[6*16+12] = 1;      g_pix16[6*16+13] = 14;                       /* Eye & glint */
    g_pix16[7*16+10] = 12;     g_pix16[8*16+10] = 12;                       /* Gill arc */
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_FISH_ICON_SLOT * 4);

    /* 59 & 60: Petite ~11x13 Wingless Garden-Gnome Pip Hop & Landing Squash (PAL1) */
    hearth_bake_pip_16x16(g_pix16, 1u);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_PIP_HOP_SLOT * 4);
    hearth_bake_pip_16x16(g_pix16, 2u);
    upload_pix16_sprite_to_vram(VRAM_SPR_BASE + SPR_PIP_SQUASH_SLOT * 4);

    /* Tall 16x24 Hardware Sprites (SPRITE_SIZE(2,3), 6 column-major tiles each):
     * Slot 0 = Rowan (dynamically updated via VBlank DMA_QUEUE on pose change),
     * Slots 1..13 = Grandma Mara + 12 key adult NPCs! */
    {
        static const u8 TALL_NPCS[13] = {
            SPR_NPC_MARA, SPR_NPC_ELDER, SPR_NPC_CLARA, SPR_NPC_GUARD,
            SPR_NPC_BARNABY, SPR_NPC_MAPLE, SPR_NPC_JASPER, SPR_NPC_TESSA,
            SPR_NPC_CORIN, SPR_NPC_SENTINEL, SPR_NPC_TOWNSFOLK,
            SPR_NPC_SHADOW_GRAN, SPR_NPC_SHIPWRIGHT
        };
        u32 tmp_tiles[48];
        gen_dma_stage_flush();
        gen_upload_rom_tiles_dma(ROWAN_GEN_TILES[0], VRAM_TALL_SPR_BASE, 6u);
        g_last_rowan_key = 0u;

        for (u8 i = 0; i < 13u; i++) {
            hearth_bake_npc_16x24(g_pix24, TALL_NPCS[i], 0u);
            pack_pix16x24_sprite_to_buf(g_pix24, tmp_tiles);
            gen_dma_stage_push((u16)(VRAM_TALL_SPR_BASE + (u16)(i + 1u) * 6u), tmp_tiles, 6u);
        }
        gen_dma_stage_flush();
    }
}

/* Pre-bake 4 smooth rotational frames for 16x16 windmill sails (terrain anims stream from ROM!) */
static void bake_env_anim_frames(void) {
    for (u8 f = 0; f < 4u; f++) {
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
    gen_dma_stage_push((u16)(VRAM_SPR_BASE + SPR_WINDMILL_BLADES * 4u), g_anim_windmill[0], 4u);
    gen_dma_stage_flush();
}

static void tick_env_animations(u32 frame_count) {
    u8 af = (u8)((frame_count >> 3) & 3u); /* Smooth ~7.5 Hz environmental cycle */
    if (af == g_last_env_frame) return;
    g_last_env_frame = af;

    /* Stream 30 animated Chrono water/flower/grass/rune/shoreline tiles (960 B) + 4 windmill tiles */
    gen_queue_rom_tiles_dma((const u32 *)GEN_CHRONO_ANIM[af], (u16)(VRAM_BG_BASE + 1u), GEN_CHRONO_ANIM_COUNT);
    VDP_loadTileData(g_anim_windmill[af], VRAM_SPR_BASE + SPR_WINDMILL_BLADES * 4u, 4, DMA_QUEUE);
}

/* ============================================================================
 * 3. CHRONO/MANA 188-METATILE AUTOTILER, PAL3 PARALLAX VISTA & 64x64 STREAMING
 * ============================================================================ */
static u8  g_gen_bg_meta[HEARTH_MAX_MAP_CELLS];
static u16 g_vista_map[8][32];

static void bake_genesis_zone_metas(void) {
    if (g_hearth.is_world_map) return;
    u16 mw = (u16)(g_hearth.map_w ? g_hearth.map_w : HEARTH_MAP_W);
    u16 mh = (u16)(g_hearth.map_h ? g_hearth.map_h : HEARTH_MAP_H);
    const u8 *p1 = g_hearth.map_bg1;
    const u8 *p2 = g_hearth.map_bg2;

    for (u16 my = 0u; my < mh; my++) {
        u16 row_off = (u16)(my * mw);
        u16 n_off = (my > 0u) ? (u16)((my - 1u) * mw) : row_off;
        u16 s_off = ((u16)(my + 1u) < mh) ? (u16)((my + 1u) * mw) : row_off;
        for (u16 mx = 0u; mx < mw; mx++) {
            u16 mx_l = (mx > 0u) ? (u16)(mx - 1u) : mx;
            u16 mx_r = ((u16)(mx + 1u) < mw) ? (u16)(mx + 1u) : mx;
            u16 idx = (u16)(row_off + mx);
            u8 t   = p1[idx];
            u8 ov  = p2[idx];
            u8 tn  = p1[(u16)(n_off + mx)];
            u8 ts  = p1[(u16)(s_off + mx)];
            u8 tw  = p1[(u16)(row_off + mx_l)];
            u8 te  = p1[(u16)(row_off + mx_r)];
            u8 tnw = p1[(u16)(n_off + mx_l)];
            u8 tne = p1[(u16)(n_off + mx_r)];
            u8 tsw = p1[(u16)(s_off + mx_l)];
            u8 tse = p1[(u16)(s_off + mx_r)];
            g_gen_bg_meta[idx] = hearth_resolve_chrono_meta(t, ov, tn, ts, tw, te,
                                                            tnw, tne, tsw, tse, mx, my, 0u);
        }
    }
}

static inline u16 gen_meta_attr(u16 raw) {
    return (u16)(raw + VRAM_BG_BASE);
}

/* Sweeping continuous 256px-wide alpine mountain skyline height & slope (1:1 parity with SNES) */
static u8 gen_vista_mtn_y(u16 px, u8 *out_lit) {
    static const u16 KNOT_X[12] = {
        0u, 22u, 48u, 76u, 102u, 130u, 158u, 180u, 206u, 228u, 244u, 256u
    };
    static const u8 KNOT_Y[12] = {
        38u, 26u, 14u, 32u, 22u, 8u, 30u, 18u, 36u, 20u, 32u, 38u
    };
    u16 x8 = (u16)(px & 255u);
    u8 k;
    for (k = 0u; k < 11u; k++) {
        if (x8 < KNOT_X[k + 1u]) break;
    }
    u16 x0 = KNOT_X[k], x1 = KNOT_X[k + 1u];
    u8 y0 = KNOT_Y[k], y1 = KNOT_Y[k + 1u];
    u16 dx = (u16)(x1 - x0);
    if (dx == 0u) dx = 1u;
    s16 dy = (s16)y1 - (s16)y0;
    *out_lit = (dy < 0) ? 1u : 0u;
    return (u8)((s16)y0 + (((s16)(x8 - x0) * dy) / (s16)dx));
}

static u8 gen_vista_pine_y(u16 px) {
    u8 base = (u8)(50u + (((px >> 3) + (px >> 4)) & 3u));
    u8 tree = (u8)(px & 7u);
    u8 spire = (tree == 3u || tree == 4u) ? 5u
             : ((tree == 2u || tree == 5u) ? 3u
             : ((tree == 1u || tree == 6u) ? 1u : 0u));
    u8 vary = (u8)(((px * 5u) >> 3) & 3u);
    return (u8)(base - spire - vary);
}

/* Bake full-resolution 256x64px (32x8 tiles) 4bpp Alpine Parallax Vista into VRAM_VISTA_BASE!
 * Uses PAL3 Chrono sky/snow/rock indices (2..4 sky, 5 snow/clouds, 6..9 rock, 1/10 foothills)
 * so the skyline shifts seamlessly across Morning, Afternoon, Dusk & Night! */
static void bake_genesis_parallax_vista(void) {
    static u8 mtn_y_lut[256];
    static u8 mtn_lit_lut[256];
    static u8 pine_y_lut[256];

    for (u16 px = 0u; px < 256u; px++) {
        u8 lit = 0u;
        mtn_y_lut[px]  = gen_vista_mtn_y(px, &lit);
        mtn_lit_lut[px] = lit;
        pine_y_lut[px] = gen_vista_pine_y(px);
    }

    /* Shared tiles 0..3: Zenith sky (2), Mid sky (3), Dithered gradient (2/3), Deep foothill (10/6) */
    p8_fill(2u); upload_pix8_to_vram((u16)(VRAM_VISTA_BASE + 0u));
    p8_fill(3u); upload_pix8_to_vram((u16)(VRAM_VISTA_BASE + 1u));
    for (u16 ly = 0u; ly < 8u; ly++) {
        for (u16 lx = 0u; lx < 8u; lx++) {
            g_pix8[ly * 8u + lx] = (ly < 3u) ? 2u : ((ly >= 5u) ? 3u : ((((lx + ly) & 1u) == 0u) ? 2u : 3u));
        }
    }
    upload_pix8_to_vram((u16)(VRAM_VISTA_BASE + 2u));
    p8_fill(10u); p8_rect(1, 1, 2, 2, 6); p8_rect(5, 4, 2, 2, 6);
    upload_pix8_to_vram((u16)(VRAM_VISTA_BASE + 3u));

    u16 next_t = 4u;
    for (u16 ty = 0u; ty < 8u; ty++) {
        u16 py0 = (u16)(ty << 3);
        u16 py1 = (u16)(py0 + 7u);
        for (u16 tx = 0u; tx < 32u; tx++) {
            u16 px0 = (u16)(tx << 3);
            u8 has_mtn = 0u, has_pine = 0u, has_cloud = 0u, all_pine = 1u;
            for (u16 lx = 0u; lx < 8u; lx++) {
                u16 px = (u16)(px0 + lx);
                u8 my = mtn_y_lut[px];
                u8 pine_y = pine_y_lut[px];
                if (py1 >= (u16)pine_y) has_pine = 1u;
                if (py0 < (u16)pine_y) {
                    all_pine = 0u;
                    if (py1 >= (u16)my) has_mtn = 1u;
                    if (ty <= 1u && ((px >= 14u && px <= 46u) || (px >= 166u && px <= 198u))) {
                        u16 cx = (px < 100u) ? (u16)(px - 14u) : (u16)(px - 166u);
                        u8 top_c = (cx >= 10u && cx <= 22u) ? 6u : ((cx >= 4u && cx <= 28u) ? 9u : 11u);
                        if (py1 >= (u16)top_c && py0 <= 13u && (u16)top_c < (u16)my) has_cloud = 1u;
                    }
                }
            }

            if (all_pine) {
                g_vista_map[ty][tx] = (u16)(VRAM_VISTA_BASE + 3u);
            } else if (!has_mtn && !has_pine && !has_cloud) {
                u16 st = (ty == 0u) ? 0u : ((ty == 1u) ? 2u : 1u);
                g_vista_map[ty][tx] = (u16)(VRAM_VISTA_BASE + st);
            } else {
                for (u16 ly = 0u; ly < 8u; ly++) {
                    u16 py = (u16)(py0 + ly);
                    for (u16 lx = 0u; lx < 8u; lx++) {
                        u16 px = (u16)(px0 + lx);
                        u8 my = mtn_y_lut[px];
                        u8 lit = mtn_lit_lut[px];
                        u8 pine_y = pine_y_lut[px];
                        u8 col;
                        if (py >= (u16)pine_y) {
                            col = (py <= (u16)(pine_y + 2u) && ((px & 7u) <= 3u)) ? 6u : 10u;
                        } else if (py >= (u16)my) {
                            u8 snow_d = (u8)(py - (u16)my);
                            if (lit) {
                                u8 snow_lim = (u8)(8u + (((px * 3u) >> 2) & 3u));
                                if (snow_d < snow_lim) col = 5u;
                                else if (snow_d < (u8)(snow_lim + 4u)) col = 9u;
                                else if (snow_d < (u8)(snow_lim + 16u)) col = 8u;
                                else col = ((((px * 3u + py * 5u) & 7u) == 0u) ? 8u : 7u);
                            } else {
                                u8 rim_lim = (u8)(3u + ((px >> 1) & 1u));
                                if (snow_d < rim_lim) col = 9u;
                                else if (snow_d < 8u && (((px + py) & 3u) == 0u)) col = 7u;
                                else col = 6u;
                            }
                        } else {
                            /* Smooth Chrono alpine sky gradient + billowy sunlit clouds */
                            col = (py < 11u) ? 2u
                                : ((py < 13u) ? ((((px + py) & 1u) == 0u) ? 2u : 3u)
                                : ((py < 24u) ? 3u : ((((px + py) & 1u) == 0u) ? 3u : 4u)));
                            if (py >= 5u && py <= 14u &&
                                ((px >= 14u && px <= 46u) || (px >= 166u && px <= 198u))) {
                                u16 cx = (px < 100u) ? (u16)(px - 14u) : (u16)(px - 166u);
                                u8 top_c = (cx >= 10u && cx <= 22u) ? 6u : ((cx >= 4u && cx <= 28u) ? 9u : 11u);
                                if (py >= (u16)top_c && py <= 13u) col = (py >= 12u) ? 9u : 5u;
                            }
                        }
                        g_pix8[ly * 8u + lx] = col;
                    }
                }
                if (next_t < 480u) {
                    upload_pix8_to_vram((u16)(VRAM_VISTA_BASE + next_t));
                    g_vista_map[ty][tx] = (u16)(VRAM_VISTA_BASE + next_t);
                    next_t++;
                } else {
                    g_vista_map[ty][tx] = (u16)(VRAM_VISTA_BASE + 1u);
                }
            }
        }
    }
    gen_dma_stage_flush();
    g_vista_vram_dirty = 0u;
    g_atlas_baked = 0u; /* Shares 480-tile block with World Atlas */
}

static void draw_genesis_parallax_vista(void) {
    /* Stamp seamless 64x64 sweeping 4bpp alpine skyline onto BG_B (PAL3) via proven VDP_setTileMapXY
     * (Callers ensure bake_genesis_parallax_vista() has already completed outside bulk lock!) */
    VDP_setAutoInc(2);
    for (u16 y = 0u; y < 8u; y++) {
        for (u16 x = 0u; x < 32u; x++) {
            u16 attr = TILE_ATTR_FULL(PAL3, 0, 0, 0, g_vista_map[y][x]);
            VDP_setTileMapXY(BG_B, attr, x, y);
            VDP_setTileMapXY(BG_B, attr, (u16)(x + 32u), y);
        }
    }
    VDP_fillTileMapRect(BG_B, TILE_ATTR_FULL(PAL3, 0, 0, 0, (u16)(VRAM_VISTA_BASE + 3u)), 0, 8, 64, 56);
}

/* 32x32 toroidal WRAM cache for Evertale World Map cells: eliminates 8x redundant
 * hearth_globe_cell() neighbor lookups per scrolled metatile -> solid 60fps & zero PSG lag! */
static u8  g_globe_bg1[32][32];
static u8  g_globe_bg2[32][32];
static u16 g_globe_tag[32][32];

static void gen_invalidate_globe_cache(void) {
    hearth_memset(g_globe_tag, 0xFF, sizeof(g_globe_tag));
}

static inline void gen_globe_cell_cached(u16 wx, u16 wy, u8 *out_bg1, u8 *out_bg2) {
    u16 cx = (u16)(wx & 31u);
    u16 cy = (u16)(wy & 31u);
    u16 tag = (u16)(((wx & 255u) << 8) | (wy & 255u));
    if (g_globe_tag[cy][cx] != tag) {
        u8 b1 = 0u, b2 = 0u;
        hearth_globe_cell(wx, wy, &b1, &b2, NULL, NULL);
        g_globe_bg1[cy][cx] = b1;
        g_globe_bg2[cy][cx] = b2;
        g_globe_tag[cy][cx] = tag;
    }
    if (out_bg1) *out_bg1 = g_globe_bg1[cy][cx];
    if (out_bg2) *out_bg2 = g_globe_bg2[cy][cx];
}

static void resolve_metatile_attrs(s16 mx, s16 my, u8 use_parallax,
                                   u16 *a_tl, u16 *a_tr, u16 *a_bl, u16 *a_br,
                                   u16 *b_tl, u16 *b_tr, u16 *b_bl, u16 *b_br) {
    u16 emp = gen_meta_attr(GEN_META_TL[CM_EMPTY]);

    if (g_hearth.is_world_map) {
        u16 wx = (u16)((u16)mx & 255u);
        s16 wy_s = my;
        u8 bg1, bg2, m1;
        while (wy_s < 0) wy_s = (s16)(wy_s + HEARTH_GLOBE_H);
        while (wy_s >= HEARTH_GLOBE_H) wy_s = (s16)(wy_s - HEARTH_GLOBE_H);
        u16 wy = (u16)wy_s;
        gen_globe_cell_cached(wx, wy, &bg1, &bg2);
        if (bg1 == (u8)T_WORLD_OCEAN || bg1 == (u8)T_WORLD_SHALLOW ||
            bg1 == (u8)T_WORLD_BEACH || bg1 == (u8)T_WORLD_SAND ||
            bg1 == (u8)T_WATER || bg1 == (u8)T_FORD) {
            u16 wx_w = (u16)((wx - 1u) & 255u);
            u16 wx_e = (u16)((wx + 1u) & 255u);
            u16 wy_n = (wy == 0u) ? (u16)(HEARTH_GLOBE_H - 1u) : (u16)(wy - 1u);
            u16 wy_d = ((u16)(wy + 1u) >= (u16)HEARTH_GLOBE_H) ? 0u : (u16)(wy + 1u);
            u8 tn, ts, tw, te, tnw, tne, tsw, tse;
            gen_globe_cell_cached(wx, wy_n, &tn, NULL);
            gen_globe_cell_cached(wx, wy_d, &ts, NULL);
            gen_globe_cell_cached(wx_w, wy, &tw, NULL);
            gen_globe_cell_cached(wx_e, wy, &te, NULL);
            gen_globe_cell_cached(wx_w, wy_n, &tnw, NULL);
            gen_globe_cell_cached(wx_e, wy_n, &tne, NULL);
            gen_globe_cell_cached(wx_w, wy_d, &tsw, NULL);
            gen_globe_cell_cached(wx_e, wy_d, &tse, NULL);
            m1 = hearth_resolve_chrono_meta(bg1, bg2, tn, ts, tw, te,
                                            tnw, tne, tsw, tse, wx, wy, 1u);
        } else {
            m1 = hearth_resolve_chrono_meta(bg1, bg2, bg1, bg1, bg1, bg1,
                                            bg1, bg1, bg1, bg1, wx, wy, 1u);
        }
        *a_tl = gen_meta_attr(GEN_META_TL[m1]);
        *a_tr = gen_meta_attr(GEN_META_TR[m1]);
        *a_bl = gen_meta_attr(GEN_META_BL[m1]);
        *a_br = gen_meta_attr(GEN_META_BR[m1]);
        if (bg2 != 0u && bg2 <= (u8)O_MAPLE_TOP) {
            u8 mo = (u8)(CM_OV_CANOPY_TL + (bg2 - 1u));
            *b_tl = gen_meta_attr(GEN_META_TL[mo]);
            *b_tr = gen_meta_attr(GEN_META_TR[mo]);
            *b_bl = gen_meta_attr(GEN_META_BL[mo]);
            *b_br = gen_meta_attr(GEN_META_BR[mo]);
        } else {
            *b_tl = emp; *b_tr = emp; *b_bl = emp; *b_br = emp;
        }
    } else {
        s16 mw = (s16)(g_hearth.map_w ? g_hearth.map_w : HEARTH_MAP_W);
        s16 mh = (s16)(g_hearth.map_h ? g_hearth.map_h : HEARTH_MAP_H);
        if (mx < 0 || my < 0 || mx >= mw || my >= mh) {
            u8 em = (use_parallax && my < 4) ? (u8)CM_EMPTY : (u8)T_BUSH;
            *a_tl = gen_meta_attr(GEN_META_TL[em]);
            *a_tr = gen_meta_attr(GEN_META_TR[em]);
            *a_bl = gen_meta_attr(GEN_META_BL[em]);
            *a_br = gen_meta_attr(GEN_META_BR[em]);
            *b_tl = emp; *b_tr = emp; *b_bl = emp; *b_br = emp;
        } else {
            u16 idx = (u16)(my * mw + mx);
            u8 bg1 = g_hearth.map_bg1[idx];
            u8 bg2 = g_hearth.map_bg2[idx];
            u8 m1  = g_gen_bg_meta[idx];
            if (use_parallax) {
                /* BG_B holds 60fps sweeping alpine mountains; BG_A renders foreground & canopies */
                if (bg1 == (u8)T_MTN_SKY) {
                    if (bg2 != 0u && bg2 <= (u8)O_MAPLE_TOP) {
                        u8 mo = (u8)(CM_OV_CANOPY_TL + (bg2 - 1u));
                        *a_tl = gen_meta_attr(GEN_META_TL[mo]);
                        *a_tr = gen_meta_attr(GEN_META_TR[mo]);
                        *a_bl = gen_meta_attr(GEN_META_BL[mo]);
                        *a_br = gen_meta_attr(GEN_META_BR[mo]);
                    } else {
                        *a_tl = emp; *a_tr = emp; *a_bl = emp; *a_br = emp;
                    }
                } else if (bg2 != 0u && bg2 <= (u8)O_MAPLE_TOP) {
                    u8 mo = (u8)(CM_OPQ_CANOPY_TL + (bg2 - 1u));
                    *a_tl = gen_meta_attr(GEN_META_TL[mo]);
                    *a_tr = gen_meta_attr(GEN_META_TR[mo]);
                    *a_bl = gen_meta_attr(GEN_META_BL[mo]);
                    *a_br = gen_meta_attr(GEN_META_BR[mo]);
                } else {
                    *a_tl = gen_meta_attr(GEN_META_TL[m1]);
                    *a_tr = gen_meta_attr(GEN_META_TR[m1]);
                    *a_bl = gen_meta_attr(GEN_META_BL[m1]);
                    *a_br = gen_meta_attr(GEN_META_BR[m1]);
                }
                *b_tl = 0u; *b_tr = 0u; *b_bl = 0u; *b_br = 0u;
            } else {
                *a_tl = gen_meta_attr(GEN_META_TL[m1]);
                *a_tr = gen_meta_attr(GEN_META_TR[m1]);
                *a_bl = gen_meta_attr(GEN_META_BL[m1]);
                *a_br = gen_meta_attr(GEN_META_BR[m1]);
                if (bg2 != 0u && bg2 <= (u8)O_MAPLE_TOP) {
                    u8 mo = (u8)(CM_OV_CANOPY_TL + (bg2 - 1u));
                    *b_tl = gen_meta_attr(GEN_META_TL[mo]);
                    *b_tr = gen_meta_attr(GEN_META_TR[mo]);
                    *b_bl = gen_meta_attr(GEN_META_BL[mo]);
                    *b_br = gen_meta_attr(GEN_META_BR[mo]);
                } else {
                    *b_tl = emp; *b_tr = emp; *b_bl = emp; *b_br = emp;
                }
            }
        }
    }
}

static void write_metatile(s16 mx, s16 my) {
    u16 a_tl, a_tr, a_bl, a_br;
    u16 emp = gen_meta_attr(GEN_META_TL[CM_EMPTY]);
    u16 b_tl = emp, b_tr = emp, b_bl = emp, b_br = emp;
    u8 use_parallax = (!g_hearth.is_world_map && g_hearth.ppu.has_mountain_vista) ? 1u : 0u;
    resolve_metatile_attrs(mx, my, use_parallax, &a_tl, &a_tr, &a_bl, &a_br, &b_tl, &b_tr, &b_bl, &b_br);
    u16 tx0 = (u16)((mx * 2) & 63);
    u16 tx1 = (u16)((tx0 + 1) & 63);
    u16 ty0 = (u16)((my * 2) & 63);
    u16 ty1 = (u16)((ty0 + 1) & 63);
    VDP_setTileMapXY(BG_A, a_tl, tx0, ty0);
    VDP_setTileMapXY(BG_A, a_tr, tx1, ty0);
    VDP_setTileMapXY(BG_A, a_bl, tx0, ty1);
    VDP_setTileMapXY(BG_A, a_br, tx1, ty1);
    if (!use_parallax) {
        u8 b_has = (b_tl != emp || b_tr != emp || b_bl != emp || b_br != emp) ? 1u : 0u;
        u8 bx = (u8)((tx0 >> 1) & 31u);
        u8 by = (u8)((ty0 >> 1) & 31u);
        u8 mask = (u8)(1u << (bx & 7u));
        u8 was_used = (g_bgb_meta_used[by][bx >> 3] & mask) ? 1u : 0u;
        if (b_has) {
            VDP_setTileMapXY(BG_B, b_tl, tx0, ty0);
            VDP_setTileMapXY(BG_B, b_tr, tx1, ty0);
            VDP_setTileMapXY(BG_B, b_bl, tx0, ty1);
            VDP_setTileMapXY(BG_B, b_br, tx1, ty1);
            g_bgb_meta_used[by][bx >> 3] |= mask;
        } else if (was_used) {
            VDP_setTileMapXY(BG_B, emp, tx0, ty0);
            VDP_setTileMapXY(BG_B, emp, tx1, ty0);
            VDP_setTileMapXY(BG_B, emp, tx0, ty1);
            VDP_setTileMapXY(BG_B, emp, tx1, ty1);
            g_bgb_meta_used[by][bx >> 3] &= (u8)(~mask);
        }
    }
}

static s16 g_last_cam_mx = -999;
static s16 g_last_cam_my = -999;
static u8  g_last_scene_mode = 255;
static u8  g_last_intro_scene = 255;
static u8  g_last_pal_id = 255;
static u8  g_last_pal3_mode = 255;
static u8  g_last_map_rev = 255;
static u8  g_cinema_pals_active = 0;

static void draw_full_world_viewport(s16 cam_x, s16 cam_y) {
    s16 mx0 = (s16)((cam_x >> 4) - 2);
    s16 my0 = (s16)((cam_y >> 4) - 2);
    u8 use_parallax = (!g_hearth.is_world_map && g_hearth.ppu.has_mountain_vista) ? 1u : 0u;
    u16 emp = gen_meta_attr(GEN_META_TL[CM_EMPTY]);

    if (use_parallax) {
        draw_genesis_parallax_vista();
    } else {
        gen_clear_plane(BG_B);
    }
    VDP_setAutoInc(2);
    for (s16 dy = 0; dy < 18; dy++) {
        s16 my = (s16)(my0 + dy);
        u16 ty0 = (u16)((my * 2) & 63);
        u16 ty1 = (u16)((ty0 + 1u) & 63);
        for (s16 dx = 0; dx < 24; dx++) {
            s16 mx = (s16)(mx0 + dx);
            u16 tx0 = (u16)((mx * 2) & 63);
            u16 tx1 = (u16)((tx0 + 1u) & 63);
            u16 a_tl, a_tr, a_bl, a_br;
            u16 b_tl = emp, b_tr = emp, b_bl = emp, b_br = emp;
            resolve_metatile_attrs(mx, my, use_parallax,
                &a_tl, &a_tr, &a_bl, &a_br,
                &b_tl, &b_tr, &b_bl, &b_br);
            VDP_setTileMapXY(BG_A, a_tl, tx0, ty0);
            VDP_setTileMapXY(BG_A, a_tr, tx1, ty0);
            VDP_setTileMapXY(BG_A, a_bl, tx0, ty1);
            VDP_setTileMapXY(BG_A, a_br, tx1, ty1);
            if (!use_parallax && (b_tl != emp || b_tr != emp || b_bl != emp || b_br != emp)) {
                u8 bx = (u8)((tx0 >> 1) & 31u);
                u8 by = (u8)((ty0 >> 1) & 31u);
                VDP_setTileMapXY(BG_B, b_tl, tx0, ty0);
                VDP_setTileMapXY(BG_B, b_tr, tx1, ty0);
                VDP_setTileMapXY(BG_B, b_bl, tx0, ty1);
                VDP_setTileMapXY(BG_B, b_br, tx1, ty1);
                g_bgb_meta_used[by][bx >> 3] |= (u8)(1u << (bx & 7u));
            }
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
        gen_vdp_begin_bulk();
        draw_full_world_viewport(cam_x, cam_y);
        gen_vdp_end_bulk();
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
    gen_clear_plane(BG_B);
    gen_clear_plane(BG_A);
    for (u16 my = 0u; my < 14u; my++) {
        u8 m = (my < 5u) ? (u8)(dark_arena ? CM_BATTLE_SKY_SHRINE : CM_BATTLE_SKY_MEADOW)
                         : (u8)(dark_arena ? CM_BATTLE_GND_SHRINE : CM_BATTLE_GND_MEADOW);
        u16 tl = gen_meta_attr(GEN_META_TL[m]);
        u16 tr = gen_meta_attr(GEN_META_TR[m]);
        u16 bl = gen_meta_attr(GEN_META_BL[m]);
        u16 br = gen_meta_attr(GEN_META_BR[m]);
        u16 ty0 = (u16)(my * 2u);
        u16 ty1 = (u16)(ty0 + 1u);
        for (u16 mx = 0u; mx < 20u; mx++) {
            u16 tx0 = (u16)(mx * 2u);
            u16 tx1 = (u16)(tx0 + 1u);
            VDP_setTileMapXY(BG_A, tl, tx0, ty0);
            VDP_setTileMapXY(BG_A, tr, tx1, ty0);
            VDP_setTileMapXY(BG_A, bl, tx0, ty1);
            VDP_setTileMapXY(BG_A, br, tx1, ty1);
        }
    }
    VDP_setHorizontalScrollVSync(BG_A, 0);
    VDP_setVerticalScrollVSync(BG_A, 0);
    VDP_setHorizontalScrollVSync(BG_B, 0);
    VDP_setVerticalScrollVSync(BG_B, 0);
}

static void draw_intro_backdrop(u8 scene) {
    /* Must ONLY be called outside bulk lock (g_vdp_bulk_depth == 0) so DMA_QUEUE flushes cleanly! */
    u8 p = (scene < CINEMA_ROM_PLATE_COUNT) ? scene : 0u;
    const uint16_t *mp = CINEMA_ROM_MAP_PTRS[p];

    gen_vdp_begin_bulk();
    gen_clear_plane(BG_B);
    gen_clear_plane(BG_A);
    gen_vdp_end_bulk();

    /* 128KB-boundary-safe VBlank DMA_QUEUE upload of Cinema CG tiles (<= 480 tiles) */
    g_vista_vram_dirty = 1u; /* Cinema CG shares 480-tile block -> re-bake vista/atlas on demand */
    g_atlas_baked = 0u;
    gen_upload_rom_tiles_dma(CINEMA_GEN_TILE_PTRS[p], VRAM_CG_BASE, CINEMA_ROM_TILE_COUNTS[p]);

    gen_vdp_begin_bulk();
    /* Pier Solar-style 45-color multi-palette Cinema CG (PAL0 + PAL1 + PAL3; PAL2 stays WINDOW UI!) */
    PAL_setColors(0,  CINEMA_ROM_PALS[p][0], 16, CPU);
    PAL_setColors(16, CINEMA_ROM_PALS[p][1], 16, CPU);
    PAL_setColors(48, CINEMA_ROM_PALS[p][2], 16, CPU);
    g_cinema_pals_active = 1u;

    /* Center 32x15 multi-palette 16-bit pixel-art CG plate on 40x28 Genesis screen (x=4..35, y=2..16) */
    for (u16 ty = 0; ty < CINEMA_ROM_TH; ty++) {
        for (u16 tx = 0; tx < CINEMA_ROM_TW; tx++) {
            u16 w = mp[(u16)(ty * CINEMA_ROM_TW + tx)];
            u16 pal_slot = (u16)((w >> 13) & 3u);
            u16 vf = (u16)((w >> 12) & 1u);
            u16 hf = (u16)((w >> 11) & 1u);
            u16 tid = (u16)(w & 0x07FFu);
            VDP_setTileMapXY(BG_A,
                             TILE_ATTR_FULL(pal_slot, 0, vf, hf, (u16)(VRAM_CG_BASE + tid)),
                             (u16)(4u + tx), (u16)(2u + ty));
        }
    }

    VDP_setHorizontalScrollVSync(BG_A, 0);
    VDP_setVerticalScrollVSync(BG_A, 0);
    VDP_setHorizontalScrollVSync(BG_B, 0);
    VDP_setVerticalScrollVSync(BG_B, 0);
    gen_vdp_end_bulk();
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
static u16 g_last_atlas_sig = 0xFFFFu;
static char g_last_hud_line[40];

static void draw_genesis_title_cursor(void) {
#if HEARTH_DESIGNER_MODE
    static const char *opts[3] = {
        "NEW STORY             ",
        "EXPLORE EVERTALE      ",
        "REPLAY OPENING CINEMA "
    };
    u8 cnt = 3u;
#else
    static const char *opts[2] = {
        "NEW STORY             ",
        "REPLAY OPENING CINEMA "
    };
    u8 cnt = 2u;
#endif
    for (u8 i = 0; i < cnt; i++) {
        u16 pal = (i == g_hearth.title_cursor) ? PAL3 : PAL2;
        u16 y = (u16)(16u + i * 2u);
        win_text(7, y, pal, (i == g_hearth.title_cursor) ? ">" : " ");
        win_text(9, y, pal, opts[i]);
    }
    g_last_title_cursor = g_hearth.title_cursor;
}

static void draw_genesis_atlas_footer(void) {
    static const char *VEH_LBL[6] = { "FOOT", "RAFT", "SHIP", "BALLOON", "AIRSHIP", "STARSHIP" };
    char line[40];
    u16 pos;

    VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 2, 21, 36, 5);
#if HEARTH_DESIGNER_MODE
    if (g_hearth.designer_sandbox) {
        pos = 0;
        append_str(line, &pos, sizeof(line), "< ");
        append_str(line, &pos, sizeof(line), HEARTH_REGION_NAMES[g_hearth.atlas_cursor % REGION_COUNT]);
        append_str(line, &pos, sizeof(line), " >  ");
        append_str(line, &pos, sizeof(line), VEH_LBL[g_hearth.active_vehicle < 6u ? g_hearth.active_vehicle : 0u]);
        win_text_n(3, 21, PAL3, line, 34u);
        win_text(3, 23, PAL2, "L/R:REGION  A:WARP  U/D:VESSEL");
        win_text(3, 25, PAL3, "START / B : CLOSE ATLAS");
    } else
#endif
    {
        pos = 0;
        append_str(line, &pos, sizeof(line), HEARTH_REGION_NAMES[g_hearth.world_region_id % REGION_COUNT]);
        append_str(line, &pos, sizeof(line), " - ");
        append_str(line, &pos, sizeof(line), VEH_LBL[g_hearth.active_vehicle < 6u ? g_hearth.active_vehicle : 0u]);
        win_text_n(3, 21, PAL3, line, 34u);
        win_text(3, 23, PAL2, "GOLD:PORT/TOWN  CYAN:LANDMARK");
        win_text(3, 25, PAL3, "START / B : CLOSE ATLAS");
    }
    g_last_atlas_sig = (u16)(((u16)g_hearth.atlas_cursor << 8) | g_hearth.active_vehicle);
}

static void upload_atlas_pal0_to_cram(TransferMethod tm) {
    static u16 cram[16];
    for (u16 c = 0; c < 16u; c++) {
        cram[c] = hearth_rgb_to_genesis_bgr333(ATLAS_PAL_RGB[c]);
    }
    PAL_setColors(0, cram, 16, tm);
    g_last_pal_id = 255u; /* Force zone terrain PAL0 restore immediately when Atlas closes */
}

static void draw_genesis_atlas_window(void) {
    u16 tile_idx = 0u;
    /* Callers ensure bake_genesis_atlas_bitmap() has already completed outside bulk lock! */

    upload_atlas_pal0_to_cram(DMA_QUEUE);
    gen_clear_plane(WINDOW);
    VDP_setWindowHPos(FALSE, 0);
    VDP_setWindowFullScreen();
    win_fill_box(1, 0, 38, 27);
    win_text(10, 1, PAL3, "EVERTALE WORLD ATLAS");

    /* Stamp pre-baked 24x16 (192x128 px) 4bpp Pixel-Art World Map centered at cols 8..31, rows 3..18 */
    for (u16 ry = 0u; ry < GEN_ATLAS_TH; ry++) {
        for (u16 rx = 0u; rx < GEN_ATLAS_TW; rx++) {
            VDP_setTileMapXY(WINDOW,
                TILE_ATTR_FULL(PAL0, 1, 0, 0, (u16)(VRAM_ATLAS_BASE + tile_idx)),
                (u16)(8u + rx), (u16)(3u + ry));
            tile_idx++;
        }
    }

    draw_genesis_atlas_footer();
}

static void update_window_ui(const HearthPpuPacket *ppu) {
    u8 want_layout = 0;
    if (g_hearth.atlas_open) want_layout = 7;
    else if (g_hearth.mode == MODE_INTRO) want_layout = 4;
    else if (g_hearth.mode == MODE_TITLE) want_layout = 1;
    else if (g_hearth.mode == MODE_SHOP) want_layout = 5;
    else if (g_hearth.mode == MODE_FISHING) want_layout = 6;
    else if (g_hearth.mode == MODE_BATTLE || g_hearth.mode == MODE_BATTLE_VICTORY || g_hearth.mode == MODE_BATTLE_DEFEAT) want_layout = 3;
    else if (g_hearth.dialogue.active) want_layout = 2;
    else want_layout = 0;

    if (want_layout != g_win_layout) {
        u8 masked = (g_vdp_bulk_depth == 0u) ? 1u : 0u;
        g_win_layout = want_layout;
        g_last_title_cursor = 255;
        g_last_atlas_sig = 0xFFFFu;
        g_last_hud_line[0] = '\0';
        if (want_layout == 7) {
            if (masked) { VDP_waitDMACompletion(); SYS_disableInts(); }
            draw_genesis_atlas_window();
            VDP_setAutoInc(2);
            if (masked) { SYS_enableInts(); }
            return;
        }
        if (masked) { VDP_waitDMACompletion(); SYS_disableInts(); }
        gen_clear_plane(WINDOW);
        VDP_setWindowHPos(FALSE, 0);
        if (want_layout == 1) {
            VDP_setWindowFullScreen();
            win_fill_box(2, 2, 36, 24);
            win_text(13, 4,  PAL3, "PROJECT HEARTH");
            win_text(8,  6,  PAL2, "16-BIT JRPG CORE (GENESIS)");
            win_text(5,  9,  PAL2, "DPAD  : MOVE    B : DASH");
            win_text(5,  11, PAL2, "A / C : ACT / TIMED HIT / PARRY");
            win_text(5,  13, PAL2, "START : WORLD ATLAS  X/Z: SWAP");
            draw_genesis_title_cursor();
            win_text(7,  23, PAL3, "DPAD:PICK  START OR A:BEGIN");
            win_text(30, 24, PAL2, HEARTH_VERSION_STR);
        } else if (want_layout == 0) {
            VDP_setWindowOnTop(2);
            VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 0, 0, 40, 2);
            VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_BOT),  0, 1, 40, 1);
        } else {
            VDP_setWindowOnBottom(8);
            win_fill_box(0, 20, 40, 8);
        }
        VDP_setAutoInc(2);
        if (masked) { SYS_enableInts(); }
        g_hud_tick = 0;
    }

    if (want_layout == 7) {
        u16 asig = (u16)(((u16)g_hearth.atlas_cursor << 8) | g_hearth.active_vehicle);
        if (asig != g_last_atlas_sig) {
            draw_genesis_atlas_footer();
        }
        return;
    }
    if (want_layout == 1) {
        if (g_hearth.title_cursor != g_last_title_cursor) {
            draw_genesis_title_cursor();
        }
        return;
    }

    /* Throttle text redraws to ~15Hz (30Hz during active fishing reel for smooth emerald catch zone) */
    {
        u8 tmask = (want_layout == 6 && g_hearth.fishing.phase == FISH_PHASE_REELING) ? 1u : 3u;
        if ((g_hud_tick & tmask) != 0u) {
            g_hud_tick++;
            return;
        }
        g_hud_tick++;
    }

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
        /* Keep Cinema narrative strictly on PAL2 so PAL0/PAL1/PAL3 dedicate 45 colors to the CG plate */
        win_text_n(2, 21, PAL2, hdr, 36u);
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
        win_text(2, 26, PAL2,
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
        {
            u8 diff = 0u;
            for (u16 k = 0u; k <= 38u; k++) {
                if (line[k] != g_last_hud_line[k]) {
                    diff = 1u;
                    break;
                }
            }
            if (diff) {
                for (u16 k = 0u; k <= 38u; k++) g_last_hud_line[k] = line[k];
                win_text(1, 0, PAL2, line);
            }
        }
    } else if (want_layout == 2) {
        const HearthDialogue *d = &g_hearth.dialogue;
        bool show_choices = (d->finished && d->next_text == NULL && d->choice_count > 0);
        VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 1, 21, 38, 6);
        win_text(2, 21, PAL3, d->speaker ? d->speaker : "...");
        if (d->full_text) {
            u16 vis = d->visible_chars;
            u16 start = 0u;
            u16 max_row = show_choices ? 23u : 25u;
            for (u16 row = 22u; row <= max_row && start < vis; row++) {
                while (start < vis && d->full_text[start] == ' ') start++;
                if (start >= vis) break;
                u16 take = 0u;
                u16 last_sp = 0u;
                while ((u16)(start + take) < vis && take < 36u && d->full_text[start + take] != '\0') {
                    if (d->full_text[start + take] == ' ') last_sp = take;
                    take++;
                }
                if (take == 36u && (u16)(start + take) < vis &&
                    d->full_text[start + take] != ' ' && d->full_text[start + take] != '\0' && last_sp > 0u) {
                    take = last_sp;
                }
                win_text_n(2, row, PAL2, d->full_text + start, take);
                start = (u16)(start + take);
            }
        }
        if (show_choices) {
            for (u8 c = 0; c < d->choice_count && c < 3u; c++) {
                win_text(2, (u16)(24 + c), (c == d->selected_choice) ? PAL3 : PAL2,
                    (c == d->selected_choice) ? ">" : " ");
                win_text(4, (u16)(24 + c), (c == d->selected_choice) ? PAL3 : PAL2, d->choices[c].label);
            }
        } else if (d->finished) {
            win_text(28, 26, PAL3, (d->next_text != NULL) ? "[A] MORE" : "[A] NEXT");
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

        /* Row 22: Selected Enemy HP & True 4bpp Pixel-Art Spell Lock Badges */
        if (b->selected_enemy < b->enemy_count) {
            const HearthBattleEnemy *e = &b->enemies[b->selected_enemy];
            pos = 0;
            append_str(line, &pos, sizeof(line), "TGT:");
            append_str(line, &pos, sizeof(line), e->name);
            append_str(line, &pos, sizeof(line), " HP:");
            append_int(line, &pos, sizeof(line), e->hp);
            win_text_n(2, 22, PAL3, line, 28u);
            if (e->lock_count > 0) {
                u16 lx = (u16)(3u + pos);
                if (lx > 33u) lx = 33u;
                for (u8 l = 0; l < e->lock_count && (u16)(lx + l) < 38u; l++) {
                    u16 ltile, lpal;
                    if (e->broken_locks[l]) {
                        ltile = GUI_LOCK_BROKEN; lpal = PAL2;
                    } else if (e->locks[l] == LOCK_SWORD) {
                        ltile = GUI_LOCK_SWORD;  lpal = PAL2;
                    } else if (e->locks[l] == LOCK_SUN) {
                        ltile = GUI_LOCK_SUN;    lpal = PAL3;
                    } else {
                        ltile = GUI_LOCK_STAR;   lpal = PAL3;
                    }
                    VDP_setTileMapXY(WINDOW,
                        TILE_ATTR_FULL(lpal, 1, 0, 0, (u16)(VRAM_UI_BASE + ltile)),
                        (u16)(lx + l), 22u);
                }
            }
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
        u8 count = (sh->tab == 0u) ? (u8)HEARTH_SHOP_ITEM_COUNT : (u8)HEARTH_FISH_SPECIES;
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
            u8 bi = (u8)((fs->bar_pos * 24u) / 100u);
            u8 bw = (u8)((fs->bar_half_w * 24u) / 100u);
            u8 lo, hi, pfill, tfill;
            if (bw < 2u) bw = 2u;
            lo = (bi > bw) ? (u8)(bi - bw) : 0u;
            hi = (u8)(bi + bw);
            if (hi > 23u) hi = 23u;

            win_text(2, 21, PAL3, "REELING! HOLD A TO RAISE ZONE");

            /* True 4bpp Pixel-Art Water Trough & Glowing Emerald Catch Zone (Row 23, cols 7..32) */
            VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FISH_CAP_L), 7, 23);
            for (u8 k = 0; k < 24u; k++) {
                u16 tile, pal;
                if (k >= lo && k <= hi) {
                    tile = (k == lo) ? GUI_FISH_ZONE_L : ((k == hi) ? GUI_FISH_ZONE_R : GUI_FISH_ZONE_M);
                    pal = PAL3;
                } else {
                    tile = GUI_FISH_WATER;
                    pal = PAL2;
                }
                VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(pal, 1, 0, 0, (u16)(VRAM_UI_BASE + tile)), (u16)(8u + k), 23);
            }
            VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FISH_CAP_R), 32, 23);

            /* True 4bpp Pixel-Art Catch Progress & Line Tension Bars (Row 25) */
            pfill = (u8)((fs->progress * 10u) / 100u);
            tfill = (u8)((fs->tension  * 10u) / 100u);
            if (fs->progress > 0u && pfill == 0u) pfill = 1u;
            if (fs->tension  > 0u && tfill == 0u) tfill = 1u;
            win_text(2, 25, PAL2, "CATCH");
            for (u8 k = 0; k < 10u; k++) {
                u8 filled = (k < pfill) ? 1u : 0u;
                VDP_setTileMapXY(WINDOW,
                    TILE_ATTR_FULL(filled ? PAL3 : PAL2, 1, 0, 0, VRAM_UI_BASE + (filled ? GUI_BAR_GRN : GUI_BAR_BG)),
                    (u16)(8u + k), 25);
            }
            win_text(20, 25, PAL2, "TENS");
            for (u8 k = 0; k < 10u; k++) {
                u8 filled = (k < tfill) ? 1u : 0u;
                VDP_setTileMapXY(WINDOW,
                    TILE_ATTR_FULL(filled ? PAL3 : PAL2, 1, 0, 0, VRAM_UI_BASE + (filled ? GUI_BAR_RED : GUI_BAR_BG)),
                    (u16)(25u + k), 25);
            }
            win_text(2, 26, PAL2, "KEEP FISH IN GREEN ZONE   B:RELEASE");
        } else if (fs->phase == FISH_PHASE_CAUGHT) {
            append_str(line, &pos, sizeof(line), "CAUGHT: ");
            append_str(line, &pos, sizeof(line), HEARTH_FISH_TABLE[fs->species % HEARTH_FISH_SPECIES].name);
            append_str(line, &pos, sizeof(line), " (");
            append_int(line, &pos, sizeof(line), (s16)fs->weight_oz);
            append_str(line, &pos, sizeof(line), " OZ)");
            win_text_n(6, 22, PAL3, line, 32u);
            win_text(6, 24, PAL2, "SELL IT AT WHISKERBURGH MARKET!");
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
    if (spr_id <= SPR_FAMILIAR_LUMINA || spr_id == SPR_PET_MILO ||
        spr_id == SPR_BOBBER || spr_id == SPR_RAFT) return PAL1;
    if (spr_id <= SPR_NPC_GUARD || spr_id == SPR_WINDMILL_BLADES || spr_id == SPR_WAYSTONE ||
        spr_id == SPR_CHEST || spr_id == SPR_CHEST_OPEN || spr_id == SPR_NPC_MOUSE ||
        spr_id == SPR_NPC_PIG || spr_id == SPR_HOLLOW_OAK || spr_id == SPR_NPC_TOWNSFOLK ||
        (spr_id >= SPR_NPC_MARA && spr_id <= SPR_NPC_SENTINEL) ||
        (spr_id >= SPR_FAUNA_CAMEL && spr_id <= SPR_BABANA_STALL)) return PAL2;
    return PAL3;
}

static void render_sprites(const HearthPpuPacket *ppu) {
    u16 count = 0;

    if (g_hearth.mode == MODE_TITLE) {
        VDP_setSpriteFull(0, -32, -32, SPRITE_SIZE(2, 2), 0, 0);
        VDP_updateSprites(1, DMA_QUEUE);
        return;
    }

    /* 60fps Hardware Sprites floating over the 192x128 Pixel-Art World Atlas (cols 8..31, rows 3..18) */
    if (g_hearth.atlas_open) {
        u16 htx = (u16)((u16)(FP_PX(g_hearth.hero_fx) >> 4) & 255u);
        s16 hty_s = (s16)(FP_PX(g_hearth.hero_fy) >> 4);
        while (hty_s < 0) hty_s = (s16)(hty_s + HEARTH_GLOBE_H);
        while (hty_s >= HEARTH_GLOBE_H) hty_s = (s16)(hty_s - HEARTH_GLOBE_H);
        s16 hx = (s16)(56 + (s16)(((htx * 3u) >> 2)));
        s16 hy = (s16)(16 + (s16)(((u16)hty_s * 171u) >> 8));
        u16 hpal = ((g_hearth.frame_count & 8u) != 0u) ? PAL3 : PAL1;
        VDP_setSpriteFull(count, hx, hy, SPRITE_SIZE(2, 2),
            TILE_ATTR_FULL(hpal, 1, 0, 0, VRAM_SPR_BASE + SPR_ATLAS_HERO_SLOT * 4u),
            (u8)(count + 1u));
        count++;

#if HEARTH_DESIGNER_MODE
        if (g_hearth.designer_sandbox) {
            static const u16 WARP_RX[REGION_COUNT] = { 104u, 132u,  52u, 128u, 212u,  32u, 220u };
            static const u16 WARP_RY[REGION_COUNT] = {  84u, 155u, 147u,  22u, 126u,  62u,  62u };
            u8 cid = (g_hearth.atlas_cursor < REGION_COUNT) ? g_hearth.atlas_cursor : 0u;
            s16 wx = (s16)(56 + (s16)((WARP_RX[cid] * 3u) >> 2));
            s16 wy = (s16)(16 + (s16)((WARP_RY[cid] * 171u) >> 8));
            u16 wpal = ((g_hearth.frame_count & 8u) != 0u) ? PAL1 : PAL3;
            VDP_setSpriteFull(count, wx, wy, SPRITE_SIZE(2, 2),
                TILE_ATTR_FULL(wpal, 1, 0, 0, VRAM_SPR_BASE + SPR_ATLAS_RETICLE_SLOT * 4u),
                (u8)(count + 1u));
            count++;
        }
#endif
        VDP_setSpriteFull(count, -32, -32, SPRITE_SIZE(1, 1), 0, 0);
        VDP_updateSprites((u16)(count + 1u), DMA_QUEUE);
        return;
    }

    for (u8 i = 0; i < ppu->oam_count && count < 58u; i++) {
        const HearthOamSprite *o = &ppu->oam[i];
        u8 tslot = genesis_tall_slot(o->sprite_id);
        s16 sx = (s16)(o->x - 8 + ppu->shake_x);
        s16 sy;
        u16 pal, tile;
        u8 flip_x;

        pal = sprite_palette(o->sprite_id);
        flip_x = (o->flags & OAM_FLAG_FLIP_X) ? 1u : 0u;

        if (tslot != 0xFFu) {
            /* Tall 16x24 JRPG character sprite (SPRITE_SIZE(2, 3), feet anchored at o->y) */
            sy = (s16)(o->y - 24 + ppu->shake_y);
            if (sx < -16 || sx >= 320 || sy < -24 || sy >= 224) continue;
            if (g_win_layout >= 2u && sy >= 140) continue;
            if (g_win_layout == 0u && sy < 0) continue;

            if (o->sprite_id == SPR_HERO) {
                u8 pose = (o->frame <= HERO_POSE_BTL_VICTORY) ? o->frame : hearth_compute_hero_pose(&g_hearth);
                u8 rkey = (u8)(((o->dir & 3u) << 4) | (pose & 0x0Fu));
                if (rkey != g_last_rowan_key) {
                    u8 pidx = gen_rowan_pose_idx(o->dir, pose);
                    g_last_rowan_key = rkey;
                    gen_queue_rom_tiles_dma(ROWAN_GEN_TILES[pidx], VRAM_TALL_SPR_BASE, 6u);
                }
                flip_x = 0u;
            }
            tile = (u16)(VRAM_TALL_SPR_BASE + (u16)tslot * 6u);
            VDP_setSpriteFull(count, sx, sy, SPRITE_SIZE(2, 3),
                TILE_ATTR_FULL(pal, 0, 0, flip_x, tile), (u8)(count + 1u));
            count++;
        } else {
            sy = (s16)(o->y - ((o->layer == 2) ? 8 : 16) + ppu->shake_y);
            if (sx < -16 || sx >= 320 || sy < -16 || sy >= 224) continue;
            /* Keep sprites behind bottom dialogue/battle/intro/shop/fishing window */
            if (g_win_layout >= 2u && sy >= 148) continue;
            if (g_win_layout == 0u && sy < 8) continue;

            if (o->sprite_id == SPR_PIP) {
                u8 pslot = (o->frame == 1u) ? SPR_PIP_HOP_SLOT
                         : ((o->frame == 2u) ? SPR_PIP_SQUASH_SLOT : SPR_PIP);
                tile = (u16)(VRAM_SPR_BASE + (u16)pslot * 4u);
            } else {
                tile = (u16)(VRAM_SPR_BASE + (u16)o->sprite_id * 4u);
            }
            VDP_setSpriteFull(count, sx, sy, SPRITE_SIZE(2, 2),
                TILE_ATTR_FULL(pal, 0, 0, flip_x, tile), (u8)(count + 1u));
            count++;
        }
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

    /* 60fps Pixel-Art Swimming / Trophy Fish Hardware Sprite over WINDOW Fishing Gauge */
    if (g_hearth.mode == MODE_FISHING && count < 63u) {
        if (g_hearth.fishing.phase == FISH_PHASE_REELING) {
            s16 fx = (s16)(56 + (s16)(((u16)g_hearth.fishing.fish_pos * 192u) / 100u));
            u8 fflip = (g_hearth.fishing.fish_vel < 0) ? 1u : 0u;
            VDP_setSpriteFull(count, fx, 180, SPRITE_SIZE(2, 2),
                TILE_ATTR_FULL(PAL1, 1, 0, fflip, VRAM_SPR_BASE + SPR_FISH_ICON_SLOT * 4u),
                (u8)(count + 1u));
            count++;
        } else if (g_hearth.fishing.phase == FISH_PHASE_CAUGHT) {
            s16 fy = (s16)(174 + (((g_hearth.frame_count & 16u) != 0u) ? 1 : 0));
            VDP_setSpriteFull(count, 20, fy, SPRITE_SIZE(2, 2),
                TILE_ATTR_FULL(PAL1, 1, 0, 0, VRAM_SPR_BASE + SPR_FISH_ICON_SLOT * 4u),
                (u8)(count + 1u));
            count++;
        }
    }

    /* 60fps Living Biome Atmosphere Hardware Motes (Petals, Autumn Maple Leaves, Fireflies, Snow) */
    if (g_hearth.mode == MODE_EXPLORE && !g_hearth.atlas_open && g_win_layout == 0u &&
        ppu->terrain_pal_id != ZPAL_COTTAGE_INTERIOR &&
        ppu->terrain_pal_id != ZPAL_SEWERS &&
        ppu->terrain_pal_id != ZPAL_BOARHOLD) {
        static const u16 MOTE_SEED_X[6] = { 28u, 86u, 148u, 206u, 264u, 304u };
        static const u16 MOTE_SEED_Y[6] = { 22u, 74u, 118u, 46u, 152u, 186u };
        u16 fc = (u16)g_hearth.frame_count;
        u8 is_night = (ppu->terrain_pal_id == ZPAL_AUTUMN_NIGHT || ppu->terrain_pal_id == ZPAL_SHADOW) ? 1u : 0u;
        u16 mpal = (ppu->terrain_pal_id == ZPAL_ALDERBROOK || ppu->terrain_pal_id == ZPAL_AUTUMN_DUSK ||
                    ppu->terrain_pal_id == ZPAL_VOLCANO) ? PAL0 : PAL3;
        for (u8 k = 0u; k < 6u && count < 68u; k++) {
            u16 drift_x = (u16)(MOTE_SEED_X[k] + (fc >> 1) + ((u16)k * 17u));
            u16 drift_y = is_night
                ? (u16)(MOTE_SEED_Y[k] + (((fc >> 2) + k * 5u) & 15u))
                : (u16)(MOTE_SEED_Y[k] + (fc >> 2) + ((u16)k * 13u));
            s16 px = (s16)((drift_x - (u16)(ppu->cam_x >> 1)) & 0x1FFu);
            s16 py = (s16)(12 + (s16)((drift_y - (u16)(ppu->cam_y >> 1)) & 255u));
            if (px >= 320) px = (s16)(px - 320);
            if (py >= 216) py = (s16)(py - 204);
            if (px >= 0 && px < 312 && py >= 12 && py < 216) {
                u8 hf = (u8)(((fc >> 4) + k) & 1u);
                VDP_setSpriteFull(count, px, py, SPRITE_SIZE(1, 1),
                    TILE_ATTR_FULL(mpal, 1, 0, hf, VRAM_BG_MOTE_TILE), (u8)(count + 1u));
                count++;
            }
        }
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
/* Milestone 4 Regional Continent PSG Themes */
static const u16 PSG_DESERT_LEAD[16] = {
    587, 622, 740, 784, 740, 622, 587, 440,
    466, 587, 622, 740, 622, 587, 466, 440
};
static const u16 PSG_DESERT_BASS[16] = {
    147, 220, 147, 220, 156, 220, 147, 220,
    147, 220, 156, 220, 147, 220, 147, 147
};
static const u16 PSG_PARADISE_LEAD[16] = {
    523, 659, 784, 659, 880, 784, 659, 587,
    659, 784, 880, 1047, 880, 784, 659, 523
};
static const u16 PSG_PARADISE_BASS[16] = {
    262, 196, 262, 196, 175, 220, 196, 247,
    262, 196, 220, 165, 175, 196, 262, 196
};
static const u16 PSG_FROST_LEAD[16] = {
    659, 659, 659, 0,   659, 659, 659, 0,
    659, 784, 523, 587, 659, 0,   784, 587
};
static const u16 PSG_FROST_BASS[16] = {
    262, 196, 262, 196, 262, 196, 262, 196,
    262, 196, 175, 196, 262, 196, 196, 147
};
static const u16 PSG_JUNGLE_LEAD[16] = {
    440, 523, 587, 659, 587, 523, 440, 392,
    440, 587, 659, 784, 659, 587, 523, 440
};
static const u16 PSG_JUNGLE_BASS[16] = {
    147, 147, 220, 147, 175, 175, 220, 175,
    147, 147, 220, 147, 196, 196, 220, 147
};
static const u16 PSG_VOLCANO_LEAD[16] = {
    294, 311, 294, 0,   349, 330, 311, 294,
    440, 466, 440, 392, 349, 330, 294, 0
};
static const u16 PSG_VOLCANO_BASS[16] = {
    147, 147, 156, 147, 147, 147, 175, 147,
    147, 156, 147, 131, 147, 147, 147, 131
};
static const u16 PSG_TECH_LEAD[16] = {
    523, 784, 659, 988, 523, 784, 659, 1047,
    587, 880, 740, 1175, 659, 988, 784, 1047
};
static const u16 PSG_TECH_BASS[16] = {
    131, 262, 131, 262, 147, 294, 147, 294,
    165, 330, 165, 330, 196, 392, 196, 262
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
    } else if (bgm_id == BGM_DESERT) {
        u8 idx = (u8)(g_bgm_step & 15u);
        psg_trigger_tone(0, PSG_DESERT_LEAD[idx], 8, 4);
        psg_trigger_tone(1, PSG_DESERT_BASS[idx], 10, 4);
        g_bgm_timer = 13;
    } else if (bgm_id == BGM_PARADISE) {
        u8 idx = (u8)(g_bgm_step & 15u);
        psg_trigger_tone(0, PSG_PARADISE_LEAD[idx], 8, 4);
        psg_trigger_tone(1, PSG_PARADISE_BASS[idx], 10, 4);
        g_bgm_timer = 12;
    } else if (bgm_id == BGM_FROST) {
        u8 idx = (u8)(g_bgm_step & 15u);
        if (PSG_FROST_LEAD[idx] > 0) psg_trigger_tone(0, PSG_FROST_LEAD[idx], 8, 5);
        psg_trigger_tone(1, PSG_FROST_BASS[idx], 11, 5);
        g_bgm_timer = 14;
    } else if (bgm_id == BGM_JUNGLE) {
        u8 idx = (u8)(g_bgm_step & 15u);
        psg_trigger_tone(0, PSG_JUNGLE_LEAD[idx], 8, 3);
        psg_trigger_tone(1, PSG_JUNGLE_BASS[idx], 10, 3);
        g_bgm_timer = 11;
    } else if (bgm_id == BGM_VOLCANO) {
        u8 idx = (u8)(g_bgm_step & 15u);
        if (PSG_VOLCANO_LEAD[idx] > 0) psg_trigger_tone(0, PSG_VOLCANO_LEAD[idx], 8, 4);
        psg_trigger_tone(1, PSG_VOLCANO_BASS[idx], 10, 4);
        g_bgm_timer = 12;
    } else if (bgm_id == BGM_TECH) {
        u8 idx = (u8)(g_bgm_step & 15u);
        psg_trigger_tone(0, PSG_TECH_LEAD[idx], 8, 3);
        psg_trigger_tone(1, PSG_TECH_BASS[idx], 10, 3);
        g_bgm_timer = 10;
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

static void upload_zone_pal0_to_cram(u8 pal_id, u8 terrain_pal3, TransferMethod tm) {
    static u16 cram0[16];
    static u16 cram3[16];
    u8 pid = (pal_id < ZPAL_COUNT) ? pal_id : 0u;
    for (u16 c = 0u; c < 16u; c++) {
        cram0[c] = GEN_CHRONO_PALS[pid][0][c];
        cram3[c] = terrain_pal3
            ? GEN_CHRONO_PALS[pid][1][c]
            : hearth_rgb_to_genesis_bgr333(HEARTH_PALETTES_RGB[3][c]);
    }
    PAL_setColors(0,  cram0, 16, tm);
    PAL_setColors(48, cram3, 16, tm);
    g_last_pal_id = pid;
    g_last_pal3_mode = terrain_pal3;
}

static void upload_palettes_to_cram(void) {
    u16 cram[16];
    for (u16 p = 0u; p < 4u; p++) {
        for (u16 c = 0u; c < 16u; c++) {
            cram[c] = hearth_rgb_to_genesis_bgr333(HEARTH_PALETTES_RGB[p][c]);
        }
        PAL_setColors((u16)(p * 16u), cram, 16, CPU);
    }
    g_last_pal_id = 255u;
    g_last_pal3_mode = 255u;
}

/* ============================================================================
 * 7.5 RETRO SEGA GENESIS BOOT SPLASH SCREENS & STAGED COLD BOOT
 *     - Reuses temporary tiles at VRAM_ATLAS_BASE (overwritten on first Atlas open)
 *     - First visible splash appears in < 25ms after power-on!
 *     - Stages heavy terrain & sprite VRAM baking in the dark beats between splashes
 * ============================================================================ */
#define VRAM_SPLASH_STAR0   ((u16)(VRAM_ATLAS_BASE + 0u))
#define VRAM_SPLASH_STAR1   ((u16)(VRAM_ATLAS_BASE + 1u))
#define VRAM_SPLASH_RULE    ((u16)(VRAM_ATLAS_BASE + 2u))
#define VRAM_SPLASH_CORNER  ((u16)(VRAM_ATLAS_BASE + 3u))
#define VRAM_SPLASH_ART0    ((u16)(VRAM_ATLAS_BASE + 4u))
#define VRAM_SPLASH_ART1    ((u16)(VRAM_ATLAS_BASE + 24u))

static u8 g_boot_stage = 0u;
static u8 g_splash_buf[48u * 32u];

static void gen_run_boot_stage(u8 target_stage) {
    if (target_stage >= 1u && g_boot_stage < 1u) {
        bake_bg_terrain_tiles();
        g_boot_stage = 1u;
    }
    if (target_stage >= 2u && g_boot_stage < 2u) {
        bake_all_sprites();
        bake_env_anim_frames();
        g_boot_stage = 2u;
    }
}

static void gsp_clear(u8 w, u8 h) {
    hearth_memset(g_splash_buf, 0, (size_t)w * (size_t)h);
}

static void gsp_px(u8 bw, u8 bh, s16 x, s16 y, u8 col) {
    if (x >= 0 && y >= 0 && x < (s16)bw && y < (s16)bh) {
        g_splash_buf[(u16)y * (u16)bw + (u16)x] = col;
    }
}

static void gsp_rect(u8 bw, u8 bh, s16 x, s16 y, s16 w, s16 h, u8 col) {
    for (s16 py = y; py < y + h; py++) {
        if (py < 0 || py >= (s16)bh) continue;
        for (s16 px = x; px < x + w; px++) {
            if (px < 0 || px >= (s16)bw) continue;
            g_splash_buf[(u16)py * (u16)bw + (u16)px] = col;
        }
    }
}

static void gen_upload_splash_rect(u8 bw, u8 tw, u8 th, u16 dst_tile) {
    u16 tid = dst_tile;
    for (u8 ty = 0u; ty < th; ty++) {
        for (u8 tx = 0u; tx < tw; tx++) {
            u32 tile[8];
            for (u8 ly = 0u; ly < 8u; ly++) {
                u32 row = 0u;
                u16 base = (u16)((ty * 8u + ly) * bw + tx * 8u);
                for (u8 lx = 0u; lx < 8u; lx++) {
                    row = (row << 4) | (u32)(g_splash_buf[base + lx] & 0x0Fu);
                }
                tile[ly] = row;
            }
            gen_dma_stage_push(tid, tile, 1u);
            tid++;
        }
    }
    gen_dma_stage_flush();
}

static void gen_stamp_splash_rect(u16 x, u16 y, u8 tw, u8 th, u16 src_tile) {
    u16 tid = src_tile;
    for (u8 ty = 0u; ty < th; ty++) {
        for (u8 tx = 0u; tx < tw; tx++) {
            VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, tid),
                             (u16)(x + tx), (u16)(y + ty));
            tid++;
        }
    }
}

static void bake_gen_splash_decor_tiles(void) {
    /* Soft glint star */
    p8_fill(0);
    g_pix8[3*8+3] = 14; g_pix8[3*8+4] = 14; g_pix8[4*8+3] = 14; g_pix8[4*8+4] = 14;
    g_pix8[2*8+3] = 10; g_pix8[5*8+4] = 10; g_pix8[3*8+2] = 10; g_pix8[4*8+5] = 10;
    upload_pix8_to_vram(VRAM_SPLASH_STAR0);

    /* Bright 4-point cross star */
    p8_fill(0);
    p8_rect(3, 1, 2, 6, 13); p8_rect(1, 3, 6, 2, 13);
    p8_rect(3, 3, 2, 2, 15);
    g_pix8[0*8+3] = 10; g_pix8[7*8+4] = 10; g_pix8[3*8+0] = 10; g_pix8[4*8+7] = 10;
    upload_pix8_to_vram(VRAM_SPLASH_STAR1);

    /* Decorative gilded hairline divider */
    p8_fill(0);
    p8_rect(0, 3, 8, 1, 10); p8_rect(0, 4, 8, 1, 13);
    upload_pix8_to_vram(VRAM_SPLASH_RULE);

    /* Storybook filigree corner bracket (top-left; H/V flipped for TR/BL/BR) */
    p8_fill(0);
    p8_rect(1, 1, 7, 1, 13); p8_rect(1, 1, 1, 7, 13);
    p8_rect(2, 2, 6, 1, 10); p8_rect(2, 2, 1, 6, 10);
    g_pix8[1*8+1] = 15; g_pix8[3*8+3] = 14; g_pix8[3*8+4] = 10; g_pix8[4*8+3] = 10;
    upload_pix8_to_vram(VRAM_SPLASH_CORNER);
    gen_dma_stage_flush();
}

/* Splash 1 Emblem: 48x24 (6x3 tile) Metallic Cobalt & Chrome "16-BIT" Hardware Badge */
static void bake_gen_splash1_badge(void) {
    /* Bold 5x7 pixel glyph rows for '1','6','-','B','I','T' inside the badge */
    static const u8 B16_GLYPH[6][7] = {
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
        {0x0E,0x10,0x1E,0x11,0x11,0x11,0x0E}, /* 6 */
        {0x00,0x00,0x00,0x1E,0x00,0x00,0x00}, /* - */
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}  /* T */
    };
    static const u8 B16_X[6] = { 6u, 13u, 19u, 25u, 32u, 37u };

    gsp_clear(48u, 24u);
    /* Chamfered outer chrome housing */
    gsp_rect(48u, 24u, 3, 1, 42, 22, 2);
    gsp_rect(48u, 24u, 2, 2, 44, 20, 2);
    gsp_rect(48u, 24u, 1, 4, 46, 16, 2);
    /* Chrome bevel rim (top bright, bottom steel) */
    gsp_rect(48u, 24u, 3, 2, 42, 1, 13);
    gsp_rect(48u, 24u, 2, 3, 44, 1, 11);
    gsp_rect(48u, 24u, 3, 21, 42, 1, 4);
    gsp_rect(48u, 24u, 2, 20, 44, 1, 5);
    gsp_rect(48u, 24u, 2, 4, 1, 16, 11);
    gsp_rect(48u, 24u, 45, 4, 1, 16, 5);
    /* Deep cobalt-sapphire metallic face */
    gsp_rect(48u, 24u, 3, 4, 42, 16, 3);
    gsp_rect(48u, 24u, 4, 5, 40, 6, 4);
    gsp_rect(48u, 24u, 4, 11, 40, 4, 6);
    gsp_rect(48u, 24u, 4, 15, 40, 4, 3);
    /* Electric cyan racing stripes */
    gsp_rect(48u, 24u, 4, 5, 40, 1, 8);
    gsp_rect(48u, 24u, 4, 18, 40, 1, 7);
    /* Diagonal chrome gloss highlight bands */
    for (s16 y = 5; y < 18; y++) {
        s16 sx = (s16)(11 + (18 - y));
        gsp_px(48u, 24u, sx,     y, 7);
        gsp_px(48u, 24u, sx + 1, y, 8);
        gsp_px(48u, 24u, sx + 18, y, 7);
    }

    /* Sculpted "16-BIT" text with dark drop shadow + brushed metallic gradient */
    for (u8 g = 0u; g < 6u; g++) {
        for (u8 gy = 0u; gy < 7u; gy++) {
            u8 bits = B16_GLYPH[g][gy];
            for (u8 gx = 0u; gx < 5u; gx++) {
                if (bits & (1u << (4u - gx))) {
                    s16 px = (s16)(B16_X[g] + gx);
                    s16 py = (s16)(8u + gy);
                    u8 col = (gy < 2u) ? 15u : ((gy < 4u) ? 12u : ((gy < 6u) ? 11u : 9u));
                    gsp_px(48u, 24u, px + 1, py + 1, 2u);
                    gsp_px(48u, 24u, px,     py,     col);
                }
            }
        }
    }

    gen_upload_splash_rect(48u, 6u, 3u, VRAM_SPLASH_ART0);
}

/* Splash 2 Emblem: 32x32 (4x4 tile) Wrought-Iron Hearth Lantern (2 flickering flame frames) */
static void bake_gen_splash2_lantern(u8 frame, u16 dst_tile) {
    gsp_clear(32u, 32u);
    /* Soft warm amber lantern halo */
    gsp_rect(32u, 32u, 9,  8,  14, 16, 2);
    gsp_rect(32u, 32u, 7,  10, 18, 12, 2);
    gsp_rect(32u, 32u, 11, 10, 10, 12, 3);
    /* Top ring & wrought-iron hanging loop */
    gsp_rect(32u, 32u, 14, 1, 4, 1, 11);
    gsp_rect(32u, 32u, 13, 2, 1, 2, 9);
    gsp_rect(32u, 32u, 18, 2, 1, 2, 9);
    gsp_rect(32u, 32u, 14, 3, 4, 1, 8);
    /* Stepped iron hood & brass finial */
    gsp_rect(32u, 32u, 12, 4, 8, 2, 8);
    gsp_rect(32u, 32u, 13, 4, 4, 1, 11);
    gsp_rect(32u, 32u, 9,  6, 14, 2, 7);
    gsp_rect(32u, 32u, 10, 6, 8, 1, 10);
    gsp_rect(32u, 32u, 7,  8, 18, 2, 8);
    gsp_rect(32u, 32u, 8,  8, 12, 1, 11);
    /* Glass lantern chamber */
    gsp_rect(32u, 32u, 10, 10, 12, 14, 4);
    gsp_rect(32u, 32u, 11, 11, 10, 12, 5);
    /* Animated golden hearth flame */
    if (frame == 0u) {
        gsp_rect(32u, 32u, 15, 11, 2, 2, 12);
        gsp_rect(32u, 32u, 14, 13, 4, 3, 13);
        gsp_rect(32u, 32u, 13, 16, 6, 4, 14);
        gsp_rect(32u, 32u, 14, 17, 4, 3, 15);
        gsp_rect(32u, 32u, 14, 20, 4, 2, 13);
    } else {
        gsp_rect(32u, 32u, 14, 10, 2, 2, 12);
        gsp_rect(32u, 32u, 14, 12, 4, 3, 13);
        gsp_rect(32u, 32u, 13, 15, 6, 5, 14);
        gsp_rect(32u, 32u, 15, 16, 3, 4, 15);
        gsp_rect(32u, 32u, 14, 20, 4, 2, 12);
    }
    /* Wrought-iron side pillars & glass specular shine */
    gsp_rect(32u, 32u, 9,  10, 2, 14, 8);
    gsp_rect(32u, 32u, 21, 10, 2, 14, 7);
    gsp_px(32u, 32u, 10, 11, 11); gsp_px(32u, 32u, 10, 12, 11);
    gsp_px(32u, 32u, 12, 12, 15); gsp_px(32u, 32u, 12, 13, 14);
    /* Stepped iron pedestal base */
    gsp_rect(32u, 32u, 8,  24, 16, 2, 8);
    gsp_rect(32u, 32u, 9,  24, 10, 1, 11);
    gsp_rect(32u, 32u, 7,  26, 18, 2, 7);
    gsp_rect(32u, 32u, 9,  28, 14, 2, 6);
    gsp_rect(32u, 32u, 11, 28, 8,  1, 9);

    gen_upload_splash_rect(32u, 4u, 4u, dst_tile);
}

/* Splash 3 Emblem: 32x32 (4x4 tile) Gilded Ruby Heart-Ember & Twin Oak-Leaf Laurel Crest */
static void bake_gen_splash3_crest(void) {
    gsp_clear(32u, 32u);
    /* Twin emerald oak-leaf laurel arcs */
    for (s16 a = 0; a < 9; a++) {
        s16 lx = (s16)(4 + ((a < 4) ? (3 - a) : (a - 4)));
        s16 ly = (s16)(8 + a * 2);
        s16 rx = (s16)(27 - lx);
        gsp_rect(32u, 32u, lx,     ly, 3, 2, 3);
        gsp_px(32u, 32u,   lx + 1, ly, 4);
        gsp_rect(32u, 32u, rx - 2, ly, 3, 2, 3);
        gsp_px(32u, 32u,   rx - 1, ly, 4);
    }
    /* Golden ribbon tie at bottom */
    gsp_rect(32u, 32u, 11, 26, 10, 2, 10);
    gsp_rect(32u, 32u, 13, 25, 6,  2, 13);
    gsp_rect(32u, 32u, 15, 25, 2,  1, 15);
    /* Radiant golden top crown spark */
    gsp_rect(32u, 32u, 15, 2, 2, 5, 13);
    gsp_rect(32u, 32u, 13, 4, 6, 1, 13);
    gsp_rect(32u, 32u, 15, 3, 2, 2, 15);
    /* Gilded bezel around central Heart-Ember */
    gsp_rect(32u, 32u, 10, 8,  5,  3, 10);
    gsp_rect(32u, 32u, 17, 8,  5,  3, 10);
    gsp_rect(32u, 32u, 9,  10, 14, 6, 10);
    gsp_rect(32u, 32u, 10, 16, 12, 3, 10);
    gsp_rect(32u, 32u, 12, 19, 8,  2, 10);
    gsp_rect(32u, 32u, 14, 21, 4,  2, 10);
    gsp_rect(32u, 32u, 15, 23, 2,  1, 13);
    /* Faceted ruby crimson Heart-Ember core */
    gsp_rect(32u, 32u, 11, 9,  4,  3, 6);
    gsp_rect(32u, 32u, 17, 9,  4,  3, 6);
    gsp_rect(32u, 32u, 10, 11, 12, 5, 7);
    gsp_rect(32u, 32u, 11, 16, 10, 2, 6);
    gsp_rect(32u, 32u, 13, 18, 6,  2, 6);
    gsp_rect(32u, 32u, 15, 20, 2,  2, 5);
    /* Glowing inner ember heart + white gem glint */
    gsp_rect(32u, 32u, 12, 11, 3, 3, 8);
    gsp_rect(32u, 32u, 14, 13, 4, 3, 13);
    gsp_rect(32u, 32u, 15, 13, 2, 2, 15);
    gsp_px(32u, 32u, 12, 10, 15);
    gsp_px(32u, 32u, 13, 11, 14);

    gen_upload_splash_rect(32u, 4u, 4u, VRAM_SPLASH_ART0);
}

static void gen_splash_upload_pals_scaled(const u32 pal0[16], const u32 pal2[16], const u32 pal3[16], u8 level) {
    /* Persistent WRAM buffers required by SGDK DMA_QUEUE (stack buffers corrupt during VBlank!) */
    static u16 c0[16];
    static u16 c2[16];
    static u16 c3[16];
    for (u16 i = 0u; i < 16u; i++) {
        u32 rgb0 = pal0[i];
        u32 rgb2 = pal2[i];
        u32 rgb3 = pal3[i];
        if (level < 8u) {
            u32 r0 = (((rgb0 >> 16) & 0xFFu) * level) >> 3;
            u32 g0 = (((rgb0 >> 8)  & 0xFFu) * level) >> 3;
            u32 b0 = (( rgb0        & 0xFFu) * level) >> 3;
            u32 r2 = (((rgb2 >> 16) & 0xFFu) * level) >> 3;
            u32 g2 = (((rgb2 >> 8)  & 0xFFu) * level) >> 3;
            u32 b2 = (( rgb2        & 0xFFu) * level) >> 3;
            u32 r3 = (((rgb3 >> 16) & 0xFFu) * level) >> 3;
            u32 g3 = (((rgb3 >> 8)  & 0xFFu) * level) >> 3;
            u32 b3 = (( rgb3        & 0xFFu) * level) >> 3;
            rgb0 = (r0 << 16) | (g0 << 8) | b0;
            rgb2 = (r2 << 16) | (g2 << 8) | b2;
            rgb3 = (r3 << 16) | (g3 << 8) | b3;
        }
        c0[i] = hearth_rgb_to_genesis_bgr333(rgb0);
        c2[i] = hearth_rgb_to_genesis_bgr333(rgb2);
        c3[i] = hearth_rgb_to_genesis_bgr333(rgb3);
    }
    PAL_setColors(0,  c0, 16, DMA_QUEUE);
    PAL_setColors(32, c2, 16, DMA_QUEUE);
    PAL_setColors(48, c3, 16, DMA_QUEUE);
}

static void gen_splash_psg_decay_tick(void) {
    for (u8 ch = 0u; ch < 4u; ch++) {
        if (g_psg_env[ch] < PSG_ENVELOPE_MIN) {
            g_psg_cnt[ch]++;
            if (g_psg_cnt[ch] >= g_psg_decay[ch]) {
                g_psg_cnt[ch] = 0u;
                g_psg_env[ch]++;
                SYS_disableInts();
                PSG_setEnvelope(ch, g_psg_env[ch]);
                SYS_enableInts();
            }
        }
    }
}

static void gen_splash_stamp_rule(u16 x, u16 y, u16 len) {
    for (u16 i = 0u; i < len; i++) {
        VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, VRAM_SPLASH_RULE),
                         (u16)(x + i), y);
    }
}

static void gen_splash_stamp_corners(u16 x0, u16 y0, u16 x1, u16 y1) {
    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, VRAM_SPLASH_CORNER), x0, y0);
    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 1, VRAM_SPLASH_CORNER), x1, y0);
    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 1, 0, VRAM_SPLASH_CORNER), x0, y1);
    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 1, 1, VRAM_SPLASH_CORNER), x1, y1);
}

static void run_genesis_boot_splashes(void) {
    static const u32 PAL_S1_ART[16] = {
        0x040814u, 0x040814u, 0x0E162Cu, 0x162C66u,
        0x224AA0u, 0x4C5C78u, 0x2862C8u, 0x3C9CE8u,
        0x7CE4FFu, 0x7A8CA8u, 0x5278B0u, 0xB8C8E0u,
        0xDCE8F8u, 0x8CC8FFu, 0xA8E0FFu, 0xFFFFFFu
    };
    static const u32 PAL_S1_TXT2[16] = {
        0x040814u, 0x040814u, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0xD8E8FCu
    };
    static const u32 PAL_S1_TXT3[16] = {
        0x040814u, 0x040814u, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0x7EE6FFu
    };

    static const u32 PAL_S2_ART[16] = {
        0x0A0810u, 0x0A0810u, 0x24160Eu, 0x422410u,
        0x582E12u, 0x783C14u, 0x262432u, 0x3A3848u,
        0x525064u, 0x706C82u, 0x9C7A46u, 0xC4B49Au,
        0xE86820u, 0xF8AE34u, 0xFFE27Au, 0xFFFBE0u
    };
    static const u32 PAL_S2_TXT2[16] = {
        0x0A0810u, 0x0A0810u, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0xF2E6D0u
    };
    static const u32 PAL_S2_TXT3[16] = {
        0x0A0810u, 0x0A0810u, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0xFFD060u
    };

    static const u32 PAL_S3_ART[16] = {
        0x0C0914u, 0x0C0914u, 0x1A3C26u, 0x2C7440u,
        0x58B464u, 0x6C1420u, 0xB42030u, 0xE23844u,
        0xFF7C64u, 0x8A6428u, 0xC8943Cu, 0xE0B858u,
        0xF2CE6Eu, 0xFFE488u, 0xFFF2B8u, 0xFFFFFFu
    };
    static const u32 PAL_S3_TXT2[16] = {
        0x0C0914u, 0x0C0914u, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0xF8EEDCu
    };
    static const u32 PAL_S3_TXT3[16] = {
        0x0C0914u, 0x0C0914u, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0xFFE27Au
    };

    u16 prev_pad = read_genesis_pad();
    u8 skipped = 0u;

    gen_vdp_begin_bulk();
    {
        u16 black[16];
        for (u16 i = 0u; i < 16u; i++) black[i] = 0u;
        PAL_setColors(0,  black, 16, CPU);
        PAL_setColors(32, black, 16, CPU);
        PAL_setColors(48, black, 16, CPU);
    }
    VDP_setBackgroundColor(0); /* PAL0[0] = dynamic splash backdrop */
    gen_clear_plane(BG_A);
    gen_clear_plane(BG_B);
    gen_clear_plane(WINDOW);
    VDP_setWindowHPos(FALSE, 0);
    VDP_setWindowFullScreen();
    gen_vdp_end_bulk();

    /* Upload splash decor tiles via VBlank DMA_QUEUE while CRAM is black */
    bake_gen_splash_decor_tiles();

    for (u8 sp = 0u; sp < 3u && !skipped; sp++) {
        const u32 *p0 = (sp == 0u) ? PAL_S1_ART  : ((sp == 1u) ? PAL_S2_ART  : PAL_S3_ART);
        const u32 *p2 = (sp == 0u) ? PAL_S1_TXT2 : ((sp == 1u) ? PAL_S2_TXT2 : PAL_S3_TXT2);
        const u32 *p3 = (sp == 0u) ? PAL_S1_TXT3 : ((sp == 1u) ? PAL_S2_TXT3 : PAL_S3_TXT3);
        u16 total_frames = (sp == 0u) ? 112u : ((sp == 1u) ? 116u : 122u);

        /* Upload next splash emblem tiles via VBlank DMA_QUEUE while CRAM is black */
        if (sp == 0u) {
            bake_gen_splash1_badge();
        } else if (sp == 1u) {
            bake_gen_splash2_lantern(0u, VRAM_SPLASH_ART0);
            bake_gen_splash2_lantern(1u, VRAM_SPLASH_ART1);
        } else {
            bake_gen_splash3_crest();
        }

        /* Stamp next splash tilemap while CRAM is black & interrupts masked (zero flicker) */
        gen_vdp_begin_bulk();
        gen_clear_plane(WINDOW);
        if (sp == 0u) {
            gen_stamp_splash_rect(17u, 6u, 6u, 3u, VRAM_SPLASH_ART0);
            gen_splash_stamp_rule(9u, 11u, 22u);
            win_text(10u, 13u, PAL3, "MEGA 16-BIT EDITION");
            win_text(7u,  16u, PAL2, "HIGH-DEFINITION 320X224 VDP");
            win_text(6u,  18u, PAL2, "68000 CPU * SN76489 PSG AUDIO");
            win_text(10u, 24u, PAL2, "PRESS START TO SKIP");
        } else if (sp == 1u) {
            gen_stamp_splash_rect(18u, 5u, 4u, 4u, VRAM_SPLASH_ART0);
            gen_splash_stamp_rule(8u, 11u, 24u);
            win_text(8u,  13u, PAL3, "HEARTHWORKS INTERACTIVE");
            win_text(7u,  16u, PAL2, "16-BIT STORYBOOK RPG ENGINE");
            win_text(14u, 18u, PAL2, "BUILD " HEARTH_VERSION_STR);
            win_text(10u, 24u, PAL2, "PRESS START TO SKIP");
        } else {
            gen_splash_stamp_corners(4u, 3u, 35u, 22u);
            gen_stamp_splash_rect(18u, 5u, 4u, 4u, VRAM_SPLASH_ART0);
            gen_splash_stamp_rule(9u, 11u, 22u);
            win_text(13u, 13u, PAL3, "PROJECT HEARTH");
            win_text(5u,  16u, PAL2, "TALES OF ALDERBROOK & EVERTALE");
            win_text(6u,  18u, PAL2, "A HANDCRAFTED 16-BIT JOURNEY");
            win_text(10u, 24u, PAL2, "PRESS START TO SKIP");
        }
        gen_vdp_end_bulk();

        for (u16 f = 0u; f < total_frames; f++) {
            u16 pad = read_genesis_pad();
            u16 edge = (u16)(pad & (u16)(~prev_pad));
            prev_pad = pad;
            if (edge & (PAD_START | PAD_A | PAD_B)) {
                skipped = 1u;
                break;
            }

            /* Smooth 8-step hardware CRAM fade-in & fade-out */
            u8 lvl = 8u;
            if (f < 16u) {
                lvl = (u8)((f >> 1) + 1u);
                if (lvl > 8u) lvl = 8u;
            } else if (f >= (u16)(total_frames - 16u)) {
                u16 rem = (u16)(total_frames - 1u - f);
                lvl = (u8)(rem >> 1);
            }

            if (sp == 0u && lvl == 8u) {
                /* Classic Mega Drive chrome metallic sheen sweep across badge highlight indices */
                if ((f & 3u) == 0u) {
                    u32 p0_anim[16];
                    u8 phase = (u8)((f >> 2) & 7u);
                    for (u16 i = 0u; i < 16u; i++) p0_anim[i] = p0[i];
                    if (phase == 1u)      { p0_anim[11] = 0xFFFFFFu; p0_anim[8] = 0xB8F8FFu; }
                    else if (phase == 2u) { p0_anim[12] = 0xFFFFFFu; p0_anim[13] = 0xE8F8FFu; }
                    else if (phase == 3u) { p0_anim[7]  = 0x7CE4FFu; }
                    gen_splash_upload_pals_scaled(p0_anim, p2, p3, 8u);
                }
            } else if (f < 18u || f >= (u16)(total_frames - 17u)) {
                gen_splash_upload_pals_scaled(p0, p2, p3, lvl);
            }

            /* Twinkling stars / flickering lantern animation */
            if (sp == 0u) {
                if ((f & 15u) == 0u) {
                    u16 stA = ((f & 16u) != 0u) ? VRAM_SPLASH_STAR1 : VRAM_SPLASH_STAR0;
                    u16 stB = ((f & 16u) == 0u) ? VRAM_SPLASH_STAR1 : VRAM_SPLASH_STAR0;
                    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, stA), 14u, 6u);
                    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, stB), 25u, 8u);
                    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, stB), 13u, 9u);
                    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, stA), 26u, 5u);
                }
                /* Iconic bright 16-bit 4-note major arpeggio chime */
                if (f == 12u) psg_trigger_tone(0, 523u, 6, 4);
                if (f == 18u) psg_trigger_tone(1, 659u, 6, 4);
                if (f == 24u) psg_trigger_tone(2, 784u, 5, 5);
                if (f == 30u) psg_trigger_tone(0, 1047u, 5, 7);
            } else if (sp == 1u) {
                if ((f & 7u) == 0u) {
                    u16 ltile = ((f & 8u) != 0u) ? VRAM_SPLASH_ART1 : VRAM_SPLASH_ART0;
                    gen_stamp_splash_rect(18u, 5u, 4u, 4u, ltile);
                }
                /* Warm Hearth Lantern acoustic bell triad */
                if (f == 14u) psg_trigger_tone(0, 440u, 7, 5);
                if (f == 22u) psg_trigger_tone(1, 554u, 7, 5);
                if (f == 30u) psg_trigger_tone(2, 659u, 6, 6);
            } else {
                if ((f & 15u) == 0u) {
                    u16 stA = ((f & 16u) != 0u) ? VRAM_SPLASH_STAR1 : VRAM_SPLASH_STAR0;
                    u16 stB = ((f & 16u) == 0u) ? VRAM_SPLASH_STAR1 : VRAM_SPLASH_STAR0;
                    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, stA), 15u, 5u);
                    VDP_setTileMapXY(WINDOW, TILE_ATTR_FULL(PAL0, 1, 0, 0, stB), 24u, 8u);
                }
                /* Storybook harp ascending sparkle */
                if (f == 12u) psg_trigger_tone(0, 392u, 7, 5);
                if (f == 19u) psg_trigger_tone(1, 494u, 7, 5);
                if (f == 26u) psg_trigger_tone(2, 587u, 6, 6);
                if (f == 33u) psg_trigger_tone(0, 784u, 6, 7);
            }

            gen_splash_psg_decay_tick();
            SYS_doVBlankProcess();
        }

        /* Ensure screen is completely dark before staging the next VRAM DMA_QUEUE upload */
        gen_splash_upload_pals_scaled(p0, p2, p3, 0u);
        SYS_doVBlankProcess();

        /* Hide terrain & sprite DMA_QUEUE uploads cleanly inside the dark beats between splashes!
         * Interrupts & display remain ENABLED so SYS_doVBlankProcess() flushes every DMA chunk! */
        if (!skipped) {
            if (sp == 0u) {
                gen_run_boot_stage(1u); /* 399 Chrono terrain tiles + mote (7 VBlank frames) */
            } else if (sp == 1u) {
                gen_run_boot_stage(2u); /* Hardware sprites + env animations (11 VBlank frames) */
            }
        }
    }

    /* Silence splash PSG chimes & finish any remaining boot DMA stages if user skipped early */
    PSG_reset();
    for (u8 ch = 0u; ch < 4u; ch++) {
        g_psg_env[ch] = PSG_ENVELOPE_MIN;
        g_psg_cnt[ch] = 0u;
    }
    g_last_bgm_id = 255u;

    gen_run_boot_stage(2u);

    gen_vdp_begin_bulk();
    gen_clear_plane(WINDOW);
    g_win_layout = 255u;
    g_vista_vram_dirty = 1u; /* Splash artwork shared VRAM_VISTA_BASE -> always re-bake vista! */
    gen_vdp_end_bulk();
}

int main(bool hard_reset) {
    (void)hard_reset;

    /* Halt Z80 PSG bus contention before initializing SN76489 hardware! */
    Z80_loadDriver(Z80_DRIVER_NULL, TRUE);
    PSG_reset();
    VDP_setScreenWidth320();
    VDP_setScreenHeight224();
    /* Explicitly lock 64x64 VDP table map (0xA800..0xFFFF) disjoint from tiles 16..1311 (0x0200..0xA3FF) */
    VDP_setPlaneSize(64, 64, TRUE);
    VDP_setHScrollTableAddress(0xA800);
    VDP_setSpriteListAddress(0xAC00);
    VDP_setWindowAddress(0xB000);
    VDP_setBGBAddress(0xC000);
    VDP_setBGAAddress(0xE000);
    VDP_setScrollingMode(HSCROLL_PLANE, VSCROLL_PLANE);
    VDP_resetSprites();

    /* Black CRAM cleanly before uploading UI + Font via VBlank DMA_QUEUE (~50ms) */
    gen_vdp_begin_bulk();
    {
        u16 black[16];
        for (u16 i = 0u; i < 16u; i++) black[i] = 0u;
        PAL_setColors(0,  black, 16, CPU);
        PAL_setColors(16, black, 16, CPU);
        PAL_setColors(32, black, 16, CPU);
        PAL_setColors(48, black, 16, CPU);
    }
    gen_clear_plane(BG_A);
    gen_clear_plane(BG_B);
    gen_clear_plane(WINDOW);
    gen_vdp_end_bulk();

    bake_ui_and_font_tiles();

    run_genesis_boot_splashes();

    hearth_init(&g_hearth);
    gen_invalidate_globe_cache();
    g_hearth.tuning.ppu_mode = PPU_GENESIS_320x224;
    {
        /* Seed prev_pad so skipping boot splashes never leaks a button edge into Opening Cinema */
        uint16_t init_pad = read_genesis_pad();
        g_hearth.prev_pad = init_pad;
        const HearthPpuPacket *boot_ppu = hearth_step_60hz(&g_hearth, init_pad);
        u8 boot_tpal3 = (g_hearth.mode == MODE_TITLE || g_hearth.mode == MODE_EXPLORE ||
                         g_hearth.mode == MODE_CUTSCENE || g_hearth.mode == MODE_SHOP) ? 1u : 0u;
        g_last_map_rev = g_hearth.map_revision;
        bake_genesis_zone_metas();

        /* Pre-bake vista outside bulk lock if starting on an alpine exterior */
        if (g_hearth.mode != MODE_INTRO && !g_hearth.is_world_map &&
            boot_ppu->has_mountain_vista && g_vista_vram_dirty) {
            bake_genesis_parallax_vista();
        }

        if (g_hearth.mode == MODE_INTRO) {
            g_last_scene_mode = 2u;
            g_last_intro_scene = g_hearth.intro.scene;
            gen_vdp_begin_bulk();
            upload_palettes_to_cram();
            VDP_setBackgroundColor(33); /* PAL2[1] warm slate backdrop */
            gen_vdp_end_bulk();
            draw_intro_backdrop(g_hearth.intro.scene);
            gen_vdp_begin_bulk();
            update_window_ui(boot_ppu);
            gen_vdp_end_bulk();
        } else {
            /* Atomic boot-to-title VDP setup: CRAM, vista, viewport, scroll & WINDOW UI */
            gen_vdp_begin_bulk();
            upload_palettes_to_cram();
            VDP_setBackgroundColor(33); /* PAL2[1] warm slate backdrop */
            upload_zone_pal0_to_cram(boot_ppu->terrain_pal_id, boot_tpal3, CPU);
            g_last_scene_mode = 0u;
            gen_clear_plane(BG_A);
            draw_full_world_viewport(boot_ppu->cam_x, boot_ppu->cam_y);
            VDP_setHorizontalScrollVSync(BG_A, (s16)(-boot_ppu->cam_x));
            VDP_setVerticalScrollVSync(BG_A,   (s16)( boot_ppu->cam_y));
            if (!g_hearth.is_world_map && boot_ppu->has_mountain_vista) {
                VDP_setHorizontalScrollVSync(BG_B, (s16)(-(boot_ppu->cam_x >> 2)));
                VDP_setVerticalScrollVSync(BG_B,   (s16)( (boot_ppu->cam_y >> 3)));
            } else {
                VDP_setHorizontalScrollVSync(BG_B, (s16)(-boot_ppu->cam_x));
                VDP_setVerticalScrollVSync(BG_B,   (s16)( boot_ppu->cam_y));
            }
            update_window_ui(boot_ppu);
            gen_vdp_end_bulk();
        }

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
        u8 want_tpal3 = ((g_hearth.mode == MODE_TITLE || g_hearth.mode == MODE_EXPLORE ||
                          g_hearth.mode == MODE_CUTSCENE || g_hearth.mode == MODE_SHOP) &&
                         !g_hearth.atlas_open) ? 1u : 0u;
        u8 did_scene_transition = 0u;

        /* 1. Handle full scene/map transitions FIRST (tile DMAs outside bulk, plane/CRAM inside bulk!) */
        if (scene_mode != g_last_scene_mode || map_changed ||
            (scene_mode == 2u && g_hearth.intro.scene != g_last_intro_scene)) {
            u8 prev_scene = g_last_scene_mode;
            g_last_scene_mode = scene_mode;
            g_last_intro_scene = g_hearth.intro.scene;
            g_last_map_rev = g_hearth.map_revision;
            did_scene_transition = 1u;
            if (map_changed) {
                gen_invalidate_globe_cache();
                bake_genesis_zone_metas();
            }
            if (scene_mode == 2u) {
                g_last_pal_id = 255u; /* Force zone PAL0+PAL3 restore upon leaving cinema */
                g_last_pal3_mode = 255u;
                draw_intro_backdrop(g_hearth.intro.scene);
                gen_vdp_begin_bulk();
                update_window_ui(ppu);
                gen_vdp_end_bulk();
            } else if (scene_mode == 1u) {
                gen_vdp_begin_bulk();
                if (g_cinema_pals_active) {
                    upload_palettes_to_cram();
                    g_cinema_pals_active = 0u;
                }
                upload_zone_pal0_to_cram(ppu->terrain_pal_id, 0u, CPU);
                draw_battle_backdrop(g_hearth.battle.biome);
                update_window_ui(ppu);
                gen_vdp_end_bulk();
            } else {
                if (!g_hearth.is_world_map && ppu->has_mountain_vista && g_vista_vram_dirty) {
                    bake_genesis_parallax_vista();
                }
                gen_vdp_begin_bulk();
                if (g_cinema_pals_active) {
                    upload_palettes_to_cram();
                    g_cinema_pals_active = 0u;
                }
                upload_zone_pal0_to_cram(ppu->terrain_pal_id, want_tpal3, CPU);
                if (prev_scene != 0u || map_changed) {
                    gen_clear_plane(BG_A);
                }
                draw_full_world_viewport(ppu->cam_x, ppu->cam_y);
                g_last_env_frame = 255u;
                update_window_ui(ppu);
                gen_vdp_end_bulk();
            }
        }

        /* 2. Lazy-bake World Atlas bitmap outside bulk lock before opening Atlas window */
        if (g_hearth.atlas_open && !g_atlas_baked) {
            gen_vdp_begin_bulk();
            VDP_setWindowHPos(FALSE, 0);
            VDP_setWindowFullScreen();
            win_fill_box(1, 0, 38, 27);
            win_text(10, 12, PAL3, "CHARTING EVERTALE...");
            gen_vdp_end_bulk();
            bake_genesis_atlas_bitmap();
        }

        /* 2b. Restore Alpine Parallax Vista tiles outside bulk lock if World Atlas just closed */
        if (!did_scene_transition && scene_mode == 0u && !g_hearth.atlas_open &&
            g_vista_vram_dirty && !g_hearth.is_world_map && ppu->has_mountain_vista) {
            if (g_win_layout == 7u) {
                gen_vdp_begin_bulk();
                VDP_fillTileMapRect(WINDOW, TILE_ATTR_FULL(PAL2, 1, 0, 0, VRAM_UI_BASE + GUI_FILL), 8, 3, 24, 16);
                gen_vdp_end_bulk();
            }
            bake_genesis_parallax_vista();
            gen_vdp_begin_bulk();
            draw_genesis_parallax_vista();
            gen_vdp_end_bulk();
        }

        /* 3. Incremental VBlank DMA_QUEUE palette updates (time-of-day / atlas close / battle flash) */
        if (scene_mode != 2u && !g_hearth.atlas_open &&
            (ppu->terrain_pal_id != g_last_pal_id || want_tpal3 != g_last_pal3_mode)) {
            upload_zone_pal0_to_cram(ppu->terrain_pal_id, want_tpal3, DMA_QUEUE);
        }

        /* 4. Per-frame incremental scrolling, environmental animations & VSync scroll latch */
        if (scene_mode == 0u) {
            stream_world_viewport(ppu->cam_x, ppu->cam_y);
            if (g_hearth.mode != MODE_TITLE && !g_hearth.atlas_open) {
                tick_env_animations(g_hearth.frame_count);
            }
            /* VSync scroll updates latch atomically in VBlank -> 0% screen tearing */
            VDP_setHorizontalScrollVSync(BG_A, (s16)(-(ppu->cam_x - ppu->shake_x)));
            VDP_setVerticalScrollVSync(BG_A,   (s16)( (ppu->cam_y - ppu->shake_y)));
            if (!g_hearth.is_world_map && ppu->has_mountain_vista) {
                /* True 60fps Hardware Parallax Alpine Mountain Vista on BG_B */
                VDP_setHorizontalScrollVSync(BG_B, (s16)(-((ppu->cam_x >> 2) - ppu->shake_x)));
                VDP_setVerticalScrollVSync(BG_B,   (s16)( ((ppu->cam_y >> 3) - ppu->shake_y)));
            } else {
                VDP_setHorizontalScrollVSync(BG_B, (s16)(-(ppu->cam_x - ppu->shake_x)));
                VDP_setVerticalScrollVSync(BG_B,   (s16)( (ppu->cam_y - ppu->shake_y)));
            }
        } else if (scene_mode == 2u) {
            VDP_setHorizontalScrollVSync(BG_A, (s16)ppu->shake_x);
            VDP_setVerticalScrollVSync(BG_A,   (s16)ppu->shake_y);
        } else {
            VDP_setHorizontalScrollVSync(BG_A, (s16)ppu->shake_x);
            VDP_setVerticalScrollVSync(BG_A,   (s16)ppu->shake_y);
        }

        if (!did_scene_transition) {
            update_window_ui(ppu);
        }
        render_sprites(ppu);

        for (uint8_t i = 0; i < ppu->sfx_count; i++) {
            play_genesis_sfx(ppu->sfx_queue[i]);
        }
        psg_tick_60hz(ppu->bgm_id);

        SYS_doVBlankProcess();
    }

    return 0;
}
