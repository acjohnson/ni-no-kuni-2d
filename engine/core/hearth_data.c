/* ============================================================================
 * hearth_data.c — Static 16-Color Zone Palettes, 5-Zone Maps & Story Cutscenes
 * Pure ANSI C89/C99 — compiles cleanly on Linux, Windows, macOS, Genesis & SNES.
 * ============================================================================ */

#include "hearth_data.h"

void hearth_memset(void *dst, uint8_t val, uint16_t len) {
    uint8_t *p = (uint8_t *)dst;
#if defined(HEARTH_TARGET_SNES)
    uint16_t wval;
    uint16_t *wp;
    if (len >= 2u) {
        wval = (uint16_t)(((uint16_t)val << 8) | (uint16_t)val);
        wp = (uint16_t *)dst;
        while (len >= 2u) {
            *wp++ = wval;
            len -= 2u;
        }
        p = (uint8_t *)wp;
    }
    if (len != 0u) {
        *p = val;
    }
#else
    uint16_t i;
    for (i = 0; i < len; i++) {
        p[i] = val;
    }
#endif
}

/* 4 x 16-Color Master Indexed Sub-Palettes (Index 0 = Transparent) */
const uint32_t HEARTH_PALETTES_RGB[4][16] = {
    /* Palette 0: Default Pastoral Terrain & Architecture (Windmill Hamlet) */
    {
        0x000000u, 0x1B241Eu, 0x2F5C39u, 0x4A884Eu,
        0x74B461u, 0xA8D678u, 0x7A5438u, 0xB88454u,
        0xDFBA84u, 0x4C5868u, 0x8591A0u, 0xC4CBD4u,
        0x2D699Eu, 0x4D9CD0u, 0x9AD8F4u, 0xB5483Au
    },
    /* Palette 1: Hero, Pip & Familiars */
    {
        0x000000u, 0x1F171Au, 0xF5D2B2u, 0xD49C76u,
        0x6B3E26u, 0x9C613Bu, 0x2F6E49u, 0x4FA167u,
        0xB84333u, 0xE86A4Au, 0xE8B43Cu, 0xFFF1A8u,
        0x3B5998u, 0x67B7E8u, 0xDDF5FFu, 0x4A362Cu
    },
    /* Palette 2: Villagers (Humans, Mouse Folk, Pig Folk) & Props */
    {
        0x000000u, 0x1D1B22u, 0xF3D1B0u, 0xCE9874u,
        0xE6E2D8u, 0xA5A29Eu, 0xC4852Du, 0xE8B852u,
        0x385888u, 0x5C84B8u, 0xC45A6Eu, 0x68432Eu,
        0xA87248u, 0x52386Eu, 0x9E76CCu, 0xF4E7C8u
    },
    /* Palette 3: Enemies, Bosses, Shadow Grandma & Combat FX */
    {
        0x000000u, 0x191421u, 0x462A5Eu, 0x7E4AA8u,
        0xC48AF0u, 0xEAE0C8u, 0x335A36u, 0x578E4Au,
        0x8BC45Eu, 0x8E2D36u, 0xE04E4Eu, 0xF5B841u,
        0xFFF6CCu, 0x3E4656u, 0x748094u, 0x5AD2F4u
    }
};

/* 6 x 16-Color Dynamic Zone Terrain Palettes (Swapped into PAL0 per zone!) */
const uint32_t HEARTH_ZONE_PALETTES_RGB[ZPAL_COUNT][16] = {
    /* ZPAL_HAMLET (0): Vivid Sunlit Meadow & Azure Creek */
    {
        0x000000u, 0x1B241Eu, 0x2F5C39u, 0x4A884Eu,
        0x74B461u, 0xA8D678u, 0x7A5438u, 0xB88454u,
        0xDFBA84u, 0x4C5868u, 0x8591A0u, 0xC4CBD4u,
        0x2D699Eu, 0x4D9CD0u, 0x9AD8F4u, 0xB5483Au
    },
    /* ZPAL_ALDERBROOK (1): Warm Golden Late-Afternoon Hometown */
    {
        0x000000u, 0x231E22u, 0x3E593Cu, 0x5C7D4Cu,
        0x84A262u, 0xB8C97Eu, 0x784834u, 0xB87A4Eu,
        0xE4BE88u, 0x485064u, 0x7E8698u, 0xD2C8BAu,
        0x36648Cu, 0x5A8FB8u, 0xA6D0E8u, 0xC2523Cu
    },
    /* ZPAL_WHISKERBURGH (2): Bright Ghibli Mouse Market (Cream, Terracotta & Cheese-Gold) */
    {
        0x000000u, 0x261E1Cu, 0x3B6842u, 0x5B9458u,
        0x8AC06Cu, 0xC2E088u, 0x8A5838u, 0xCCA068u,
        0xF2D492u, 0x6A5C56u, 0xB2A290u, 0xE6D8C0u,
        0x28769Cu, 0x4AAAD0u, 0xA6E4F8u, 0xD4583Cu
    },
    /* ZPAL_SEWERS (3): Dark Lantern-Lit Sunken Sewers (Deep Slate, Moss & Amber Glow) */
    {
        0x000000u, 0x0C1016u, 0x162228u, 0x203438u,
        0x2D4B46u, 0x447260u, 0x362C28u, 0x564A3Eu,
        0xF0B848u, 0x1E2634u, 0x384658u, 0x627488u,
        0x143E44u, 0x22686Au, 0x5AD8CCu, 0xE89038u
    },
    /* ZPAL_BOARHOLD (4): Sandstone Citadel, Forged Iron & Brass Braziers */
    {
        0x000000u, 0x1E1618u, 0x4A3832u, 0x6E5242u,
        0x9C7254u, 0xC89C70u, 0x583228u, 0x985A3Eu,
        0xE8B44Cu, 0x363240u, 0x645E70u, 0xA69EA8u,
        0x522638u, 0x8E3842u, 0xF09648u, 0xC83E38u
    },
    /* ZPAL_SHADOW (5): Eerie Violet Twilight (Age-5 Shadow Grandmother Vision!) */
    {
        0x000000u, 0x140E22u, 0x281B42u, 0x3E2A62u,
        0x583E80u, 0x8462AAu, 0x3E2640u, 0x644068u,
        0xA078A8u, 0x2A223Cu, 0x52466Au, 0x8A7EA4u,
        0x221A48u, 0x3C2E74u, 0x8A6ED0u, 0xC84272u
    }
};

const char * const HEARTH_ZONE_NAMES[ZONE_COUNT] = {
    "Alderbrook",
    "Windmill Hamlet",
    "Whiskerburgh",
    "Sunken Sewers",
    "Boarhold Citadel"
};

const HearthFishInfo HEARTH_FISH_TABLE[HEARTH_FISH_SPECIES] = {
    { "Sun Carp",       18u, 22u, 0u, ZONE_HAMLET,       1u },
    { "Meadow Trout",   26u, 32u, 0u, ZONE_HAMLET,       2u },
    { "Whisker Perch",  36u, 38u, 0u, ZONE_WHISKERBURGH, 2u },
    { "Sewer Glow-Eel", 55u, 48u, 0u, ZONE_SEWERS,       3u },
    { "Golden Koi",     90u, 65u, 1u, 255u,              4u },
    { "Sunken Boot",     6u,  0u, 0u, 255u,              1u }
};

const HearthShopItem HEARTH_SHOP_ITEMS[5] = {
    { "Sun Tonic",     "Heal +45 HP in battle",    20u, 0u },
    { "Star Drop",     "Restore +15 MP to party",  28u, 1u },
    { "Courage Snack", "Full HP & MP feast",       55u, 2u },
    { "River Lure",    "Wider reel bar + rare Koi",60u, 3u },
    { "Glow Lure",     "Master lure for all piers",120u,4u }
};

/* 64-Step Signed Integer Sine Lookup Table (-127..+127) */
const int8_t HEARTH_SIN64[64] = {
       0,   12,   25,   37,   49,   60,   71,   81,
      90,   98,  106,  112,  117,  122,  125,  126,
     127,  126,  125,  122,  117,  112,  106,   98,
      90,   81,   71,   60,   49,   37,   25,   12,
       0,  -12,  -25,  -37,  -49,  -60,  -71,  -81,
     -90,  -98, -106, -112, -117, -122, -125, -126,
    -127, -126, -125, -122, -117, -112, -106,  -98,
     -90,  -81,  -71,  -60,  -49,  -37,  -25,  -12
};

/* Internal map helpers */
static void set_bg1(uint8_t *bg1, uint8_t *col, int x, int y, uint8_t t, uint8_t c) {
    int idx;
    if (x < 0 || y < 0 || x >= HEARTH_MAP_W || y >= HEARTH_MAP_H) return;
    idx = y * HEARTH_MAP_W + x;
    bg1[idx] = t;
    col[idx] = c;
}

static void set_bg2(uint8_t *bg2, int x, int y, uint8_t t) {
    if (x < 0 || y < 0 || x >= HEARTH_MAP_W || y >= HEARTH_MAP_H) return;
    bg2[y * HEARTH_MAP_W + x] = t;
}

static void fill_rect(uint8_t *bg1, uint8_t *col, int x0, int y0, int w, int h, uint8_t t, uint8_t c) {
    int x, y;
    for (y = y0; y < y0 + h; y++) {
        for (x = x0; x < x0 + w; x++) {
            set_bg1(bg1, col, x, y, t, c);
        }
    }
}

static void place_tree(uint8_t *bg1, uint8_t *bg2, uint8_t *col, int tx, int ty) {
    set_bg1(bg1, col, tx, ty, T_TREE_TRUNK, COL_SOLID);
    set_bg2(bg2, tx - 1, ty - 2, O_CANOPY_TL);
    set_bg2(bg2, tx,     ty - 2, O_CANOPY_TR);
    set_bg2(bg2, tx + 1, ty - 2, O_CANOPY_TR);
    set_bg2(bg2, tx - 1, ty - 1, O_CANOPY_BL);
    set_bg2(bg2, tx,     ty - 1, O_CANOPY_BR);
    set_bg2(bg2, tx + 1, ty - 1, O_CANOPY_BR);
}

static void place_cottage(uint8_t *bg1, uint8_t *bg2, uint8_t *col, int x0, int y0) {
    int x;
    fill_rect(bg1, col, x0, y0, 5, 1, T_ROOF_TOP, COL_SOLID);
    fill_rect(bg1, col, x0, y0 + 1, 5, 1, T_ROOF_BODY, COL_SOLID);
    set_bg1(bg1, col, x0,     y0 + 2, T_WALL_WOOD, COL_SOLID);
    set_bg1(bg1, col, x0 + 1, y0 + 2, T_WALL_WINDOW, COL_SOLID);
    set_bg1(bg1, col, x0 + 2, y0 + 2, T_WALL_DOOR, COL_SOLID);
    set_bg1(bg1, col, x0 + 3, y0 + 2, T_WALL_WINDOW, COL_SOLID);
    set_bg1(bg1, col, x0 + 4, y0 + 2, T_WALL_WOOD, COL_SOLID);
    for (x = x0; x < x0 + 5; x++) set_bg2(bg2, x, y0 + 1, O_ROOF_EAVE);
}

static void fill_perimeter(uint8_t *bg1, uint8_t *col, uint8_t border_tile) {
    int x, y;
    for (x = 0; x < HEARTH_MAP_W; x++) {
        set_bg1(bg1, col, x, 0, border_tile, COL_SOLID);
        set_bg1(bg1, col, x, 1, border_tile, COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 1, border_tile, COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 2, border_tile, COL_SOLID);
    }
    for (y = 0; y < HEARTH_MAP_H; y++) {
        set_bg1(bg1, col, 0, y, border_tile, COL_SOLID);
        set_bg1(bg1, col, 1, y, border_tile, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, border_tile, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 2, y, border_tile, COL_SOLID);
    }
}

/* ============================================================================
 * ZONE 0: ALDERBROOK (Rowan's Quiet Real-World Hometown + Ancient Hollow Oak)
 * ============================================================================ */
static void build_zone_alderbrook(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash;
    uint16_t i;
    static const int8_t trees[][2] = {
        {4, 5}, {4, 21}, {13, 22}, {26, 6}, {32, 7}, {32, 20}, {38, 9}, {38, 18}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 29 + y * 53) & 15;
            if (hash == 2 || hash == 9) set_bg1(bg1, col, x, y, T_GRASS_ALT, COL_WALKABLE);
            else if (hash == 14 && x > 4 && x < 34 && y > 4 && y < 23) set_bg1(bg1, col, x, y, T_FLOWERS, COL_WALKABLE);
            else set_bg1(bg1, col, x, y, T_GRASS, COL_WALKABLE);
        }
    }
    fill_perimeter(bg1, col, T_BUSH);

    /* Cozy hometown lane & neighborhood cottages */
    fill_rect(bg1, col, 5, 13, 33, 3, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 7, 10, 6, 4, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 18, 10, 6, 4, T_COBBLE, COL_WALKABLE);

    place_cottage(bg1, bg2, col, 7, 6);  /* Rowan's Family Cottage */
    place_cottage(bg1, bg2, col, 18, 6); /* Neighbor Tom's House */
    place_cottage(bg1, bg2, col, 10, 17);/* Aunt Martha's Bakery Cottage */

    /* Picket fences, warm lamps, flowerbeds & signpost */
    for (x = 6; x <= 14; x++) set_bg1(bg1, col, x, 11, (x == 9 || x == 10) ? T_COBBLE : T_FENCE, (x == 9 || x == 10) ? COL_WALKABLE : COL_SOLID);
    set_bg1(bg1, col, 6, 12, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 17, 12, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 29, 12, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 15, 16, T_SIGNPOST, COL_SOLID);
    set_bg1(bg1, col, 7, 9, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 11, 9, T_FLOWER_BED, COL_SOLID);

    /* Gentle Brook & Wooden Footbridge separating town from the Hollow Oak Woods */
    for (y = 2; y < HEARTH_MAP_H - 2; y++) {
        set_bg1(bg1, col, 25, y, T_WATER, COL_WATER);
        set_bg1(bg1, col, 26, y, T_WATER, COL_WATER);
    }
    for (x = 25; x <= 26; x++) {
        set_bg1(bg1, col, x, 12, T_BRIDGE_RAIL_N, COL_SOLID);
        set_bg1(bg1, col, x, 13, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 14, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 15, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 16, T_BRIDGE_RAIL_S, COL_SOLID);
    }

    /* Ancient Hollow Oak Grove on East Edge (tile 38,14 is the Gateway!) */
    fill_rect(bg1, col, 30, 11, 10, 7, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 35, 12, 4, 5, T_SHRINE_FLOOR, COL_WALKABLE);
    set_bg1(bg1, col, 38, 14, T_SHRINE_RUNE, COL_WALKABLE);
    set_bg2(bg2, 37, 12, O_CANOPY_TL); set_bg2(bg2, 38, 12, O_CANOPY_TR); set_bg2(bg2, 39, 12, O_CANOPY_TR);
    set_bg2(bg2, 37, 13, O_CANOPY_BL); set_bg2(bg2, 38, 13, O_CANOPY_BR); set_bg2(bg2, 39, 13, O_CANOPY_BR);

    for (i = 0; i < sizeof(trees) / sizeof(trees[0]); i++) {
        place_tree(bg1, bg2, col, trees[i][0], trees[i][1]);
    }
}

/* ============================================================================
 * ZONE 1: WINDMILL HAMLET & WHISPERING MEADOW (Evertale Hub + Creek Pier)
 * ============================================================================ */
void hearth_build_sandbox_map(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash, curve, rx;
    uint16_t i;
    static const int8_t trees[][2] = {
        {3, 5}, {3, 10}, {3, 20}, {8, 24},
        {16, 5}, {22, 5}, {28, 4}, {34, 5},
        {39, 5}, {22, 18}, {35, 24}, {40, 23}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);

    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 37 + y * 73) & 15;
            if (hash == 0 || hash == 5) set_bg1(bg1, col, x, y, T_GRASS_ALT, COL_WALKABLE);
            else if (hash == 11 && x > 3 && x < HEARTH_MAP_W - 4 && y > 3 && y < HEARTH_MAP_H - 4)
                set_bg1(bg1, col, x, y, T_FLOWERS, COL_WALKABLE);
            else set_bg1(bg1, col, x, y, T_GRASS, COL_WALKABLE);
        }
    }

    fill_perimeter(bg1, col, T_BUSH);

    /* Winding Creek */
    for (y = 2; y < HEARTH_MAP_H - 2; y++) {
        curve = (y < 10) ? -1 : ((y > 19) ? 1 : 0);
        rx = 19 + curve;
        set_bg1(bg1, col, rx - 1, y, T_WATER, COL_WATER);
        set_bg1(bg1, col, rx,     y, T_WATER, COL_WATER);
        set_bg1(bg1, col, rx + 1, y, T_WATER, COL_WATER);
    }

    /* Windmill Hamlet Plaza, Roads & Hollow Oak West Clearing */
    fill_rect(bg1, col, 6, 10, 10, 8, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 7, 17, 5, 4, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 2, 12, 5, 3, T_DIRT, COL_WALKABLE); /* Hollow Oak return path */
    set_bg1(bg1, col, 2, 13, T_SHRINE_RUNE, COL_WALKABLE);
    fill_rect(bg1, col, 15, 12, 3, 3, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 21, 12, 14, 3, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 25, 8, 2, 5, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 13, 18, 3, 5, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 15, 21, 13, 2, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 27, 14, 2, 8, T_DIRT, COL_WALKABLE);

    /* Eastern Road Exit to Whiskerburgh Mouse Town! */
    fill_rect(bg1, col, 40, 12, 4, 3, T_DIRT, COL_WALKABLE);
    set_bg1(bg1, col, 41, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 41, 15, T_LANTERN_POST, COL_SOLID);

    /* Timber Bridge */
    for (x = 18; x <= 20; x++) {
        set_bg1(bg1, col, x, 11, T_BRIDGE_RAIL_N, COL_SOLID);
        set_bg1(bg1, col, x, 12, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 13, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 14, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 15, T_BRIDGE_RAIL_S, COL_SOLID);
    }

    /* Creekside Fishing Pier at (17, 18) jutting east to the creek! */
    set_bg1(bg1, col, 16, 18, T_BRIDGE, COL_WALKABLE);
    set_bg1(bg1, col, 17, 18, T_BRIDGE, COL_WALKABLE);
    set_bg1(bg1, col, 17, 17, T_LANTERN_POST, COL_SOLID);

    /* Shallow Pebble Ford */
    for (x = 19; x <= 21; x++) {
        set_bg1(bg1, col, x, 21, T_FORD, COL_SHALLOW);
        set_bg1(bg1, col, x, 22, T_FORD, COL_SHALLOW);
    }

    /* Stone Windmill */
    fill_rect(bg1, col, 5, 4, 3, 2, T_WINDMILL_ROOF, COL_SOLID);
    fill_rect(bg1, col, 5, 6, 3, 3, T_WINDMILL_WALL, COL_SOLID);
    set_bg1(bg1, col, 6, 8, T_WINDMILL_DOOR, COL_SOLID);

    /* Cozy Timber Cottage */
    place_cottage(bg1, bg2, col, 11, 4);

    /* Flower beds, lamps, signpost & fences */
    set_bg1(bg1, col, 11, 7, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 15, 7, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 10, 9, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 16, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 16, 15, T_SIGNPOST, COL_SOLID);
    for (x = 3; x <= 6; x++) set_bg1(bg1, col, x, 22, T_FENCE, COL_SOLID);
    for (x = 11; x <= 13; x++) set_bg1(bg1, col, x, 23, T_FENCE, COL_SOLID);

    /* Tall Rustling Wildgrass */
    fill_rect(bg1, col, 23, 6, 4, 3, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 29, 5, 5, 4, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 23, 16, 4, 4, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 29, 17, 5, 5, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 35, 20, 4, 3, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 3, 15, 3, 3, T_TALL_GRASS, COL_TALL_GRASS);

    /* Ancient Stone Shrine */
    fill_rect(bg1, col, 35, 9, 6, 9, T_SHRINE_FLOOR, COL_WALKABLE);
    fill_rect(bg1, col, 37, 12, 3, 3, T_SHRINE_RUNE, COL_WALKABLE);
    set_bg1(bg1, col, 35, 9, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 40, 9, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 35, 17, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 40, 17, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 35, 11, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 35, 15, T_SHRINE_PILLAR, COL_SOLID);
    set_bg2(bg2, 35, 12, O_SHRINE_ARCH);
    set_bg2(bg2, 35, 13, O_SHRINE_ARCH);
    set_bg2(bg2, 35, 14, O_SHRINE_ARCH);

    for (i = 0; i < sizeof(trees) / sizeof(trees[0]); i++) {
        place_tree(bg1, bg2, col, trees[i][0], trees[i][1]);
    }
}

/* ============================================================================
 * ZONE 2: WHISKERBURGH (Bright Anthropomorphic Mouse Market Town + Canal Pier)
 * ============================================================================ */
static void build_zone_whiskerburgh(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash;
    uint16_t i;
    static const int8_t trees[][2] = {
        {4, 5}, {4, 21}, {38, 6}, {39, 19}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 19 + y * 41) & 15;
            if (hash == 3) set_bg1(bg1, col, x, y, T_FLOWERS, COL_WALKABLE);
            else if (hash == 7) set_bg1(bg1, col, x, y, T_GRASS_ALT, COL_WALKABLE);
            else set_bg1(bg1, col, x, y, T_GRASS, COL_WALKABLE);
        }
    }
    fill_perimeter(bg1, col, T_BUSH);

    /* Grand Cobblestone Market Plaza & West Entry Road */
    fill_rect(bg1, col, 0, 12, 10, 3, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 8, 7, 28, 12, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 20, 2, 4, 6, T_COBBLE, COL_WALKABLE); /* Road to Northern Sewer Grate */

    /* Northern Sewer Grate Entrance (21..22, 2..3) */
    fill_rect(bg1, col, 20, 2, 4, 2, T_SHRINE_FLOOR, COL_WALKABLE);
    set_bg1(bg1, col, 21, 2, T_SHRINE_RUNE, COL_WALKABLE);
    set_bg1(bg1, col, 22, 2, T_SHRINE_RUNE, COL_WALKABLE);
    set_bg1(bg1, col, 19, 2, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 24, 2, T_SHRINE_PILLAR, COL_SOLID);
    set_bg2(bg2, 21, 2, O_SHRINE_ARCH);
    set_bg2(bg2, 22, 2, O_SHRINE_ARCH);

    /* Mouse Market Buildings: Mayor Brie's Hall, Barnaby's Shop & Cheese Bakery */
    place_cottage(bg1, bg2, col, 10, 5);
    place_cottage(bg1, bg2, col, 27, 5);
    place_cottage(bg1, bg2, col, 33, 9);

    /* Market Lanterns, Flower Beds & Signpost */
    set_bg1(bg1, col, 9, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 20, 9, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 24, 9, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 31, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 16, 13, T_SIGNPOST, COL_SOLID);
    set_bg1(bg1, col, 10, 8, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 14, 8, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 27, 8, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 31, 8, T_FLOWER_BED, COL_SOLID);

    /* Southern Sparkling Market Canal & Fishing Jetty (20, 20) */
    fill_rect(bg1, col, 6, 21, 32, 4, T_WATER, COL_WATER);
    for (x = 19; x <= 21; x++) {
        set_bg1(bg1, col, x, 19, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 20, T_BRIDGE, COL_WALKABLE);
    }
    set_bg1(bg1, col, 18, 20, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 22, 20, T_LANTERN_POST, COL_SOLID);

    for (i = 0; i < sizeof(trees) / sizeof(trees[0]); i++) {
        place_tree(bg1, bg2, col, trees[i][0], trees[i][1]);
    }
}

/* ============================================================================
 * ZONE 3: SUNKEN SEWERS (Dark Lantern-Lit Tunnels, Runoff Canals & King Muckmaw)
 * ============================================================================ */
static void build_zone_sewers(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y;

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    /* Fill solid damp stone walls first, then carve lantern-lit chambers */
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, HEARTH_MAP_H, T_SHRINE_PILLAR, COL_SOLID);

    /* Main North-South Vault & East-West Conduits */
    fill_rect(bg1, col, 18, 2, 8, 24, T_SHRINE_FLOOR, COL_WALKABLE);
    fill_rect(bg1, col, 6, 10, 32, 8, T_SHRINE_FLOOR, COL_WALKABLE);
    fill_rect(bg1, col, 6, 18, 10, 6, T_SHRINE_FLOOR, COL_WALKABLE);  /* West Cistern Fishing Chamber */
    fill_rect(bg1, col, 30, 18, 9, 6, T_SHRINE_FLOOR, COL_WALKABLE);  /* East Chest Vault */
    fill_rect(bg1, col, 15, 3, 14, 6, T_SHRINE_FLOOR, COL_WALKABLE);  /* King Muckmaw Boss Hall */

    /* Glowing Runoff Canals + Stone Bridges */
    fill_rect(bg1, col, 7, 13, 30, 2, T_WATER, COL_WATER);
    fill_rect(bg1, col, 7, 21, 7, 3, T_WATER, COL_WATER); /* Cistern Fishing Pool */
    for (x = 20; x <= 23; x++) {
        set_bg1(bg1, col, x, 13, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 14, T_BRIDGE, COL_WALKABLE);
    }
    for (x = 10; x <= 12; x++) {
        set_bg1(bg1, col, x, 13, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 14, T_BRIDGE, COL_WALKABLE);
    }
    for (x = 32; x <= 34; x++) {
        set_bg1(bg1, col, x, 13, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 14, T_BRIDGE, COL_WALKABLE);
    }
    /* Cistern Fishing Jetty at (10, 20) */
    set_bg1(bg1, col, 10, 20, T_BRIDGE, COL_WALKABLE);
    set_bg1(bg1, col, 11, 20, T_BRIDGE, COL_WALKABLE);

    /* Glowing Arcane Runes & Flickering Wall Sconces */
    set_bg1(bg1, col, 21, 25, T_SHRINE_RUNE, COL_WALKABLE); /* South Exit -> Whiskerburgh */
    set_bg1(bg1, col, 22, 25, T_SHRINE_RUNE, COL_WALKABLE);
    set_bg1(bg1, col, 21, 2,  T_SHRINE_RUNE, COL_WALKABLE); /* North Sluice -> Boarhold */
    set_bg1(bg1, col, 22, 2,  T_SHRINE_RUNE, COL_WALKABLE);
    set_bg2(bg2, 21, 2, O_SHRINE_ARCH);
    set_bg2(bg2, 22, 2, O_SHRINE_ARCH);

    /* Lantern Sconces lining corridors for atmospheric light pools */
    set_bg1(bg1, col, 19, 23, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 24, 23, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 19, 16, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 24, 16, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 9,  19, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 33, 19, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 17, 6,  T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 26, 6,  T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 20, 21, T_SIGNPOST, COL_SOLID);

    /* Decorative stone ribs overhead */
    for (y = 8; y <= 20; y += 6) {
        set_bg2(bg2, 20, y, O_SHRINE_ARCH);
        set_bg2(bg2, 23, y, O_SHRINE_ARCH);
    }
}

/* ============================================================================
 * ZONE 4: BOARHOLD CITADEL & TWILIGHT SHRINE (Pig Knights & Arc Boss Ironfang)
 * ============================================================================ */
static void build_zone_boarhold(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y;

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, HEARTH_MAP_H, T_DIRT, COL_WALKABLE);
    fill_perimeter(bg1, col, T_SHRINE_PILLAR);

    /* Sandstone Citadel Bailey & Forged Cobble Causeway */
    fill_rect(bg1, col, 5, 5, 22, 18, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 20, 22, 4, 4, T_COBBLE, COL_WALKABLE); /* South Sluice Entry */
    set_bg1(bg1, col, 21, 25, T_SHRINE_RUNE, COL_WALKABLE);
    set_bg1(bg1, col, 22, 25, T_SHRINE_RUNE, COL_WALKABLE);

    /* Pig Provision Hall & Armory Barracks */
    place_cottage(bg1, bg2, col, 7, 5);
    place_cottage(bg1, bg2, col, 16, 5);

    /* Brass Braziers & Royal Rampart Pillars */
    for (y = 9; y <= 21; y += 4) {
        set_bg1(bg1, col, 6, y, T_LANTERN_POST, COL_SOLID);
        set_bg1(bg1, col, 25, y, T_LANTERN_POST, COL_SOLID);
    }
    set_bg1(bg1, col, 14, 15, T_SIGNPOST, COL_SOLID);

    /* Iron Moat separating Outer Bailey from Lord Ironfang's Twilight Sanctum */
    for (y = 2; y < HEARTH_MAP_H - 2; y++) {
        set_bg1(bg1, col, 28, y, T_WATER, COL_WATER);
        set_bg1(bg1, col, 29, y, T_WATER, COL_WATER);
    }
    for (x = 28; x <= 29; x++) {
        set_bg1(bg1, col, x, 11, T_BRIDGE_RAIL_N, COL_SOLID);
        set_bg1(bg1, col, x, 12, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 13, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 14, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 15, T_BRIDGE_RAIL_S, COL_SOLID);
    }

    /* Eastern Twilight Shrine Sanctum (Lord Ironfang Boss Arena!) */
    fill_rect(bg1, col, 31, 6, 10, 15, T_SHRINE_FLOOR, COL_WALKABLE);
    fill_rect(bg1, col, 35, 11, 4, 5, T_SHRINE_RUNE, COL_WALKABLE);
    set_bg1(bg1, col, 31, 6,  T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 40, 6,  T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 31, 20, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 40, 20, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 33, 9,  T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 33, 17, T_LANTERN_POST, COL_SOLID);
    set_bg2(bg2, 31, 12, O_SHRINE_ARCH);
    set_bg2(bg2, 31, 13, O_SHRINE_ARCH);
    set_bg2(bg2, 31, 14, O_SHRINE_ARCH);
}

void hearth_build_zone_map(uint8_t zone_id, uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    if (zone_id == ZONE_ALDERBROOK) {
        build_zone_alderbrook(bg1, bg2, col);
    } else if (zone_id == ZONE_WHISKERBURGH) {
        build_zone_whiskerburgh(bg1, bg2, col);
    } else if (zone_id == ZONE_SEWERS) {
        build_zone_sewers(bg1, bg2, col);
    } else if (zone_id == ZONE_BOARHOLD) {
        build_zone_boarhold(bg1, bg2, col);
    } else {
        hearth_build_sandbox_map(bg1, bg2, col);
    }
}

/* ============================================================================
 * 7-ACT ILLUSTRATED OPENING CINEMA SCRIPT (23 Unhurried Story Beats)
 * ============================================================================ */
const HearthCinemaBeat HEARTH_CINEMA_SCRIPT[HEARTH_CINEMA_BEAT_COUNT] = {
    /* ACT I: Alderbrook Station at Dusk (Age 5) */
    { 1u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_PAN_RIGHT, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT I - ALDERBROOK STATION", "",
      "High in the timberline pines sits Alderbrook,",
      "a quiet mountain town lulled by the evening train." },
    { 1u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_PAN_RIGHT, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT I - ALDERBROOK STATION", "",
      "Ever since Rowan's parents vanished in the mist,",
      "Grandma Mara's hearth has been his whole world." },
    { 1u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_PAN_RIGHT, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT I - ALDERBROOK STATION", "GRANDMA MARA",
      "Hear that whistle, sparrow? The 6:15 express!",
      "Come inside before the ridge fog rolls in." },
    { 1u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_PAN_RIGHT, CIN_FX_STEAM_DUSK, SFX_TEXT_HERO,
      "ACT I - ALDERBROOK STATION", "ROWAN (AGE 5)",
      "One more minute, Grandma! Milo spotted a",
      "golden firefly by the cedar fence - wait, Milo!" },

    /* ACT II: Into Whispering Woods */
    { 2u, CG_HOLLOW_OAK_CHASE, BGM_ALDERBROOK, CIN_CAM_ZOOM_IN, CIN_FX_FOREST_POLLEN, SFX_TEXT_ELDER,
      "ACT II - WHISPERING WOODS", "",
      "With a joyful yip, the little fox-pup bolted",
      "past the rail trestle into Whispering Woods." },
    { 2u, CG_HOLLOW_OAK_CHASE, BGM_ALDERBROOK, CIN_CAM_ZOOM_IN, CIN_FX_FOREST_POLLEN, SFX_TEXT_HERO,
      "ACT II - WHISPERING WOODS", "ROWAN (AGE 5)",
      "Milo! Slow down, your red ribbon's slipping!",
      "Whoa... what is that light inside the big oak?" },
    { 2u, CG_HOLLOW_OAK_CHASE, BGM_ALDERBROOK, CIN_CAM_ZOOM_IN, CIN_FX_FOREST_POLLEN, SFX_TEXT_ELDER,
      "ACT II - WHISPERING WOODS", "",
      "Beneath the moss-draped roots opened an archway",
      "humming with warm sunlight and impossible song." },

    /* ACT III: A Glimpse of Evertale */
    { 3u, CG_EVERTALE_VISTA, BGM_CINEMA_WONDER, CIN_CAM_PAN_RIGHT, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_ELDER,
      "ACT III - EVERTALE", "",
      "One timid step through the bark, and the cold",
      "pines melted into a valley of golden clouds." },
    { 3u, CG_EVERTALE_VISTA, BGM_CINEMA_WONDER, CIN_CAM_PAN_RIGHT, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_ELDER,
      "ACT III - EVERTALE", "",
      "Towering windmills turned under cerulean skies",
      "while paper lanterns drifted over blue creeks." },
    { 3u, CG_EVERTALE_VISTA, BGM_CINEMA_WONDER, CIN_CAM_PAN_RIGHT, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_HERO,
      "ACT III - EVERTALE", "ROWAN (AGE 5)",
      "It's real... just like Grandma's storybooks!",
      "Look, Milo! Even the lanterns are floating!" },

    /* ACT IV: The Violet Eclipse & Shadow Grandma */
    { 4u, CG_SHADOW_GRANDMA, BGM_SHADOW, CIN_CAM_SHAKE_FLASH, CIN_FX_VIOLET_STORM, SFX_TEXT_ELDER,
      "ACT IV - THE VIOLET ECLIPSE", "",
      "Yet the sunlit breeze died in a heartbeat.",
      "The sky bruised black beneath a violet eclipse." },
    { 4u, CG_SHADOW_GRANDMA, BGM_SHADOW, CIN_CAM_SHAKE_FLASH, CIN_FX_VIOLET_STORM, SFX_TEXT_ELDER,
      "ACT IV - THE VIOLET ECLIPSE", "",
      "From the twisting briars stepped a dark echo",
      "wearing the face Rowan loved most in the world." },
    { 4u, CG_SHADOW_GRANDMA, BGM_SHADOW, CIN_CAM_SHAKE_FLASH, CIN_FX_VIOLET_STORM, SFX_TEXT_ELDER,
      "ACT IV - THE VIOLET ECLIPSE", "SHADOW GRANDMA",
      "So small... so fragile... Give me your spark,",
      "child. Hearts only break when left unguarded." },
    { 4u, CG_SHADOW_GRANDMA, BGM_SHADOW, CIN_CAM_SHAKE_FLASH, CIN_FX_VIOLET_STORM, SFX_TEXT_HERO,
      "ACT IV - THE VIOLET ECLIPSE", "ROWAN (AGE 5)",
      "G-Grandma?! Stop - you aren't her! Stay back!",
      "Grandma would NEVER look at me like that!" },
    { 4u, CG_SHADOW_GRANDMA, BGM_SHADOW, CIN_CAM_SHAKE_FLASH, CIN_FX_VIOLET_STORM, SFX_TEXT_ELDER,
      "ACT IV - THE VIOLET ECLIPSE", "",
      "Shaken to his core, Rowan scooped up Milo and",
      "scrambled blindly back through the Hollow Oak." },

    /* ACT V: Only a Bad Dream */
    { 5u, CG_BEDROOM_COMFORT, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT V - ONLY A BAD DREAM", "",
      "He ran until the pine woods gave way to timber",
      "walls, oil-lamp warmth, and a patchwork quilt." },
    { 5u, CG_BEDROOM_COMFORT, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT V - ONLY A BAD DREAM", "GRANDMA MARA",
      "Shh, my sweet boy... I am right here beside you.",
      "Look - Milo is safe, and my hands are warm." },
    { 5u, CG_BEDROOM_COMFORT, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT V - ONLY A BAD DREAM", "GRANDMA MARA",
      "There are no dark shadows in Alderbrook, Rowan.",
      "Close your eyes now. It was only a bad dream." },

    /* ACT VI: A Whisper at the Window */
    { 6u, CG_PIP_WINDOW, BGM_CINEMA_LULLABY, CIN_CAM_PAN_LEFT, CIN_FX_MOON_FIREFLIES, SFX_TEXT_ELDER,
      "ACT VI - A WHISPER AT THE WINDOW", "",
      "Long after midnight, frost crept on the glass,",
      "and a tiny golden glow tapped upon the sill." },
    { 6u, CG_PIP_WINDOW, BGM_CINEMA_LULLABY, CIN_CAM_PAN_LEFT, CIN_FX_MOON_FIREFLIES, SFX_TEXT_PIP,
      "ACT VI - A WHISPER AT THE WINDOW", "PIP",
      "Poor little dreamer... you saw the crooked",
      "mirror before you knew how bright your heart is." },
    { 6u, CG_PIP_WINDOW, BGM_CINEMA_LULLABY, CIN_CAM_PAN_LEFT, CIN_FX_MOON_FIREFLIES, SFX_TEXT_PIP,
      "ACT VI - A WHISPER AT THE WINDOW", "PIP",
      "Sleep safely now. When you're brave enough to",
      "remember, knock on the oak again. I'll wait." },

    /* ACT VII: Five Years Later - Dawn Over Alderbrook (Age 10) */
    { 7u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_STILL_DAWN, CIN_FX_FOREST_POLLEN, SFX_TEXT_ELDER,
      "ACT VII - FIVE YEARS LATER", "",
      "Five quiet winters passed beneath the mountain",
      "pines. Rowan turned ten, and forgot the oak." },
    { 7u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_STILL_DAWN, CIN_FX_FOREST_POLLEN, SFX_TEXT_ELDER,
      "ACT VII - FIVE YEARS LATER", "",
      "Until one crisp autumn dawn, when the 7:00",
      "train whistle woke a forgotten ember inside..." }
};

/* ============================================================================
 * BYTECODE CUTSCENE ROM TABLES (Alderbrook & Evertale Storyline!)
 * ============================================================================ */

/* Age-5 Evertale Flashback Climax: Sunlit Wonder -> Shadow Grandmother Trauma -> Pip's Dream -> Age 10 Alderbrook! */
const HearthCutsceneOp CUTSCENE_SHADOW_GRANDMA[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_SPARK, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Little Rowan",
      "Milo! Look at this sunlit meadow... wait, why is the sky turning purple?!" },
    { OP_SFX, SFX_BATTLE_SWOOP, 0, 0, NULL, NULL },
    { OP_SHAKE, 3, 18, 0, NULL, NULL },
    { OP_SET_PAL, ZPAL_SHADOW, BGM_SHADOW, 0, NULL, NULL },
    { OP_SHOW_ACTOR, 1, 22, 13, NULL, NULL }, /* Reveal Shadow Grandmother! */
    { OP_PAN_CAM, 22, 13, 30, NULL, NULL },
    { OP_WAIT, 18, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC1, SPR_EMOTE_BANG, 30, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Shadow Matron",
      "Come closer, little Rowan... why run from Grandmother's cold embrace?" },
    { OP_SHAKE, 4, 16, 0, NULL, NULL },
    { OP_CAM_FOLLOW, 20, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_BANG, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Little Rowan",
      "N-no!! Grandma was kind and warm! You aren't her! Stay away!!" },
    { OP_SFX, SFX_PORTAL, 0, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Memory",
      "Terrified, five-year-old Rowan scrambled back through the Hollow Oak into Alderbrook..." },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip (A Dream?)",
      "Psst, lad! At your window! That shadow wasn't your Grandma! Milo's safe—check the oak again!" },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Four Years Later",
      "It hurt too much to believe such a cruel reflection could be real. Rowan called it a nightmare and forgot... until today, age ten." },
    { OP_SET_FLAG, FLAG_FLASHBACK_DONE, 0, 0, NULL, NULL },
    { OP_WARP_ZONE, ZONE_ALDERBROOK, 9, 13, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* Age-10 Reunion in Windmill Hamlet: Pip is REAL & grants Rowan the Hearthwood Wand! */
const HearthCutsceneOp CUTSCENE_PIP_REUNION[] = {
    { OP_FACE_ACTOR, ACTOR_HERO, DIR_RIGHT, 0, NULL, NULL },
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_PIP, SPR_EMOTE_SPARK, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Well, butter my lantern! Four whole years, Rowan! You finally checked the Hollow Oak again!" },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_QUESTION, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Pip?! You... you weren't a dream at all! And Evertale is really here! Where's Milo?!" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Your ginger pup trotted east past Whiskerburgh! But gloom is hollowing out hearts across Evertale." },
    { OP_SFX, SFX_HEART_GIVE, 0, 0, NULL, NULL },
    { OP_SHAKE, 2, 12, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_HEART, 36, NULL, NULL },
    { OP_SET_FLAG, FLAG_WAND_RECEIVED, 0, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Take this Hearthwood Wand! Bramblekit and Lumina will fight beside you. Try fishing by the creek or follow the eastern road to Whiskerburgh!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* Whiskerburgh Mayor Brie grants the Lantern Charm & Sewer Grate Key */
const HearthCutsceneOp CUTSCENE_MAYOR_SEWER_KEY[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_SPARK, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Mayor Brie",
      "Squeak! A brave lad from beyond the Oak! Yes—a little ginger pup with a brass bell scampered north!" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Mayor Brie",
      "Darkness crept up from our Sunken Sewers right after! Take my Lantern Charm and Sewer Grate Key!" },
    { OP_SFX, SFX_CHEST_OPEN, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_SEWER_KEY, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_HEART, 32, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Mayor Brie",
      "Visit Barnaby's Market Stall for tonics or lures first, then head straight north into the Sunken Sewers!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* Boarhold Citadel: Mending Captain Tusk's Hollow Heart */
const HearthCutsceneOp CUTSCENE_MEND_CAPTAIN[] = {
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Captain Tusk, hold onto your courage! Let the Hearthwood Wand warm your spirit!" },
    { OP_SFX, SFX_HEART_GIVE, 0, 0, NULL, NULL },
    { OP_HEART_ORB, ACTOR_HERO, ACTOR_NPC1, 40, NULL, NULL },
    { OP_SHAKE, 3, 12, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CAPTAIN_MENDED, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC1, SPR_EMOTE_HEART, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Captain Tusk",
      "Hmph—snort! My chest feels warm again! Lord Ironfang fell under a pale witch's spell in the east sanctum. Break his Spell Locks!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* Chapter 1 Finale after defeating Arc Boss Lord Ironfang */
const HearthCutsceneOp CUTSCENE_IRONFANG_VICTORY[] = {
    { OP_SFX, SFX_VICTORY_FANFARE, 0, 0, NULL, NULL },
    { OP_SHAKE, 3, 14, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_PIP, SPR_EMOTE_HEART, 40, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Lord Ironfang",
      "Urgh... the pale fog lifts from my eyes! That cold witch wearing an old shawl—she twists memories to steal hearts!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "I knew it... that shadow in the glade was never really my grandmother! And listen—Milo's brass bell!" },
    { OP_SFX, SFX_COIN, 0, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Chapter 1 complete, Rowan! Boarhold's hearth shines again—and Milo's trail leads deeper into Evertale! Keep exploring, fishing, and mastering battles!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_INTRO_DEMO[] = {
    { OP_PAN_CAM,    7,  8, 42, NULL, NULL },
    { OP_WAIT,      16,  0,  0, NULL, NULL },
    { OP_SFX,  SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_PIP, SPR_EMOTE_SPARK, 32, NULL, NULL },
    { OP_CAM_FOLLOW, 28, 0,  0, NULL, NULL },
    { OP_FACE_ACTOR, ACTOR_HERO, DIR_LEFT, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Evertale is wide open! Fish at the creek pier, mend Guard Bram's heart, or take the east road to Whiskerburgh!" },
    { OP_CAM_FOLLOW, 26, 0,  0, NULL, NULL },
    { OP_FACE_ACTOR, ACTOR_HERO, DIR_DOWN, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_ELDER_SEEN, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_ELDER_TALK[] = {
    { OP_FACE_ACTOR, ACTOR_ELDER, DIR_DOWN, 0, NULL, NULL },
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_ELDER, SPR_EMOTE_SPARK, 24, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Elder Wren",
      "Welcome back to Windmill Hamlet, Rowan! Cast a line at our creek pier or travel east to Whiskerburgh Mouse Town." },
    { OP_PAN_CAM,   37, 13, 42, NULL, NULL },
    { OP_WAIT,      18,  0,  0, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Elder Wren",
      "Hot-swap Familiars with Q/E (L/R) in combat to shatter enemy Spell Locks, and time A on impact for follow-ups!" },
    { OP_CAM_FOLLOW, 32, 0,  0, NULL, NULL },
    { OP_SET_FLAG, FLAG_ELDER_SEEN, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_TAKE_COURAGE[] = {
    { OP_FACE_ACTOR, ACTOR_HERO, DIR_UP, 0, NULL, NULL },
    { OP_FACE_ACTOR, ACTOR_CLARA, DIR_DOWN, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Clara",
      "Plenty of Courage to spare! Take a bright spark into your brass locket for anyone with a hollow heart!" },
    { OP_SFX, SFX_HEART_TAKE, 0, 0, NULL, NULL },
    { OP_HEART_ORB, ACTOR_CLARA, ACTOR_HERO, 40, NULL, NULL },
    { OP_SHAKE, 2, 10, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_HEART, 30, NULL, NULL },
    { OP_SET_FLAG, FLAG_HAS_COURAGE, 0, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Spark of Courage bottled! Carry it to Guard Bram at the bridge—or Captain Tusk in Boarhold!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_GIVE_COURAGE[] = {
    { OP_FACE_ACTOR, ACTOR_GUARD, DIR_LEFT, 0, NULL, NULL },
    { OP_FACE_ACTOR, ACTOR_HERO, DIR_RIGHT, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Hold steady, Bram! Here is a warm spark of Courage from the hamlet!" },
    { OP_SFX, SFX_HEART_GIVE, 0, 0, NULL, NULL },
    { OP_HEART_ORB, ACTOR_HERO, ACTOR_GUARD, 44, NULL, NULL },
    { OP_SHAKE, 3, 14, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_GUARD_MENDED, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_GUARD, SPR_EMOTE_HEART, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Guard Bram",
      "Whoa... the chill vanished! My knees feel sturdy as oak again. Take 25 Guilders and a Sun Tonic for Whiskerburgh!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};
