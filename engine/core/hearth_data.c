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

/* 10 x 16-Color Dynamic Zone & Time-of-Day Terrain Palettes (Loaded into PAL0) */
const uint32_t HEARTH_ZONE_PALETTES_RGB[ZPAL_COUNT][16] = {
    /* ZPAL_HAMLET (0): Vivid Sunlit Meadow & Azure Creek */
    {
        0x000000u, 0x1B241Eu, 0x2F5C39u, 0x4A884Eu,
        0x74B461u, 0xA8D678u, 0x7A5438u, 0xB88454u,
        0xDFBA84u, 0x4C5868u, 0x8591A0u, 0xC4CBD4u,
        0x2D699Eu, 0x4D9CD0u, 0x9AD8F4u, 0xB5483Au
    },
    /* ZPAL_ALDERBROOK (1): Crisp Autumn Mountain Afternoon (Ochre, Slate, Spruce & Crimson Maple) */
    {
        0x000000u, 0x1D1920u, 0x284234u, 0x5E6B38u,
        0x9E843Cu, 0xDC9E3Au, 0x633B28u, 0x9C633Bu,
        0xE5C48Au, 0x3E4452u, 0x6E7584u, 0xC6CDD6u,
        0x275478u, 0x4A86B2u, 0xB8E0F2u, 0xC4402Eu
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
    },
    /* ZPAL_AUTUMN_MORNING (6): Rose-Gold Mountain Dawn Mist */
    {
        0x000000u, 0x221C2Au, 0x2C463Cu, 0x627248u,
        0xA89252u, 0xE6B05Cu, 0x6A4234u, 0xA8724Cu,
        0xF4D4A4u, 0x4A4860u, 0x828098u, 0xE2D8E4u,
        0x3C628Cu, 0x6A96C0u, 0xD8ECF8u, 0xD65842u
    },
    /* ZPAL_AUTUMN_DUSK (7): Copper-Amber Alpine Sunset & Glowing Lamps */
    {
        0x000000u, 0x1E1420u, 0x283630u, 0x5E4C32u,
        0x9C6834u, 0xE8963Cu, 0x582E24u, 0x944C30u,
        0xFFE088u, 0x3C3046u, 0x6C5A72u, 0xC89E9Au,
        0x343866u, 0x685E8Cu, 0xE8B49Cu, 0xD8442Eu
    },
    /* ZPAL_AUTUMN_NIGHT (8): Moonlit Indigo Ridge, Silver Rapids & Warm Lanterns */
    {
        0x000000u, 0x0E121Eu, 0x1B2E32u, 0x2E4048u,
        0x485862u, 0x7A8478u, 0x2C2434u, 0x4C3E4Eu,
        0xF4C458u, 0x222C44u, 0x465674u, 0xB0C2DCu,
        0x183258u, 0x325E92u, 0x9AC8ECu, 0xB85242u
    },
    /* ZPAL_COTTAGE_INTERIOR (9): Cozy Honey Cedar Planks, Hearthstone & Burgundy Rug */
    {
        0x000000u, 0x1E1412u, 0x2F4B38u, 0x52744Au,
        0x8CA25Eu, 0xDCAA4Eu, 0x683C22u, 0xA4683Cu,
        0xECD096u, 0x484246u, 0x7C7478u, 0xCCC4BAu,
        0x2A486Eu, 0x527EA8u, 0xB4D8EEu, 0xBA3C32u
    },
    /* ZPAL_EVERTALE_WORLD (10): Storybook Continent (Emerald Hills, Cerulean Sea, Limestone & Terracotta) */
    {
        0x000000u, 0x172420u, 0x295A3Au, 0x478A4Du,
        0x72BA5Eu, 0xB2DC7Au, 0x7A5638u, 0xC49458u,
        0xF2D488u, 0x4B5A6Eu, 0x90A0B2u, 0xEEF0F2u,
        0x1D5C94u, 0x3D92CCu, 0xA6E0F8u, 0xCC523Au
    },
    /* ZPAL_SUNHILL_MARKET (11): Sunlit Hilltop Limestone, Terracotta Awnings & Royal Gold */
    {
        0x000000u, 0x221C22u, 0x31623Eu, 0x529252u,
        0x82BE68u, 0xBEE088u, 0x825438u, 0xC48A54u,
        0xF4D68Eu, 0x626068u, 0xACA8A4u, 0xEFEAE0u,
        0x2B72A8u, 0x54A4DCu, 0xB8E6FAu, 0xCC503Au
    },
    /* ZPAL_DESERT (12): Sands of Babana — Sun-Baked Amber Dunes, Sandstone & Oasis Jade */
    {
        0x000000u, 0x2A1A14u, 0x3A6838u, 0x5C984Au,
        0x98C464u, 0xE6C668u, 0x9A582Au, 0xD89644u,
        0xFAD882u, 0x7A563Cu, 0xB88A5Cu, 0xF4E2B8u,
        0x1D6888u, 0x38A8B8u, 0xA8E8E8u, 0xD45232u
    },
    /* ZPAL_PARADISE (13): Suncrest Paradise — Turquoise Shallows, Coral Pink & Palm Green */
    {
        0x000000u, 0x16262Cu, 0x226846u, 0x3AA05Cu,
        0x6ED478u, 0xC2F092u, 0x8E603Au, 0xD8A868u,
        0xFCE8B2u, 0x4E6876u, 0x98B8C4u, 0xF4F6F0u,
        0x16789Eu, 0x2CC2D4u, 0x9AF4F6u, 0xF06A78u
    },
    /* ZPAL_FROST (14): Frostmere North — Crisp Snowfields, Ice Cyan & Festive Evergreen */
    {
        0x000000u, 0x182032u, 0x224E42u, 0x387860u,
        0x72A898u, 0xD8EEF8u, 0x6C4838u, 0xA47658u,
        0xF4DC9Au, 0x4A6284u, 0x94B8D8u, 0xF6FBFFu,
        0x225288u, 0x4C8EC8u, 0xB8E8FFu, 0xD43844u
    },
    /* ZPAL_JUNGLE (15): Canopy Wilds — Deep Rainforest Emerald, Jade River & Cedar Platforms */
    {
        0x000000u, 0x12221Cu, 0x1A4E32u, 0x2E7A44u,
        0x52AA58u, 0x92D66Cu, 0x684228u, 0xA86E3Eu,
        0xE4BA72u, 0x3C524Eu, 0x708C82u, 0xC8DCCAu,
        0x18666Au, 0x2EA096u, 0x8CE4CCu, 0xDC6238u
    },
    /* ZPAL_VOLCANO (16): Cinderpeak Caldera — Basalt Charcoal, Crimson Magma & Embers */
    {
        0x000000u, 0x181014u, 0x362426u, 0x563834u,
        0x825444u, 0xC67E4Au, 0x4A2622u, 0x883C2Au,
        0xF8B844u, 0x2E2A32u, 0x5C545Cu, 0xA89C9Cu,
        0x8A1E1Cu, 0xE04624u, 0xFFCC44u, 0xFF6828u
    },
    /* ZPAL_TECH (17): Aetheris Citadel — Ivory Alloy, Cobalt Steel & Electric Cyan */
    {
        0x000000u, 0x121828u, 0x223858u, 0x3A5A84u,
        0x5E88B8u, 0xA8D4F4u, 0x48526Cu, 0x7C8CA8u,
        0xDCE6F4u, 0x2A3248u, 0x627494u, 0xF2F7FFu,
        0x16568Au, 0x24B4DCu, 0x8EF6FFu, 0xD858E0u
    }
};

const char * const HEARTH_ZONE_NAMES[ZONE_COUNT] = {
    "Alderbrook",
    "Windmill Hamlet",
    "Whiskerburgh",
    "Sunken Sewers",
    "Boarhold Citadel",
    "Rowan's Cottage",
    "Whispering Woods",
    "Hollow Glade",
    "Evertale Globe",
    "Sunhill Market",
    "Castle Aurelia Gate",
    "Bramblewood Crossing",
    "Sunken Watchtower",
    "Heartland Timber Pier",
    "Suncrest Coral Cove",
    "Oasis of Babana"
};

const char * const HEARTH_REGION_NAMES[REGION_COUNT] = {
    "Emerald Heartland",
    "Sands of Babana",
    "Suncrest Paradise",
    "Frostmere North",
    "Canopy Wilds",
    "Cinderpeak Caldera",
    "Aetheris Citadel"
};

const char * const HEARTH_TOD_NAMES[4] = {
    "Morning",
    "Afternoon",
    "Dusk",
    "Moonlit Night"
};

const HearthFishInfo HEARTH_FISH_TABLE[HEARTH_FISH_SPECIES] = {
    { "Sun Carp",       18u, 22u, 0u, ZONE_HAMLET,       1u },
    { "Meadow Trout",   26u, 32u, 0u, ZONE_ALDERBROOK,   2u },
    { "Coral Snapper",  44u, 42u, 0u, ZONE_CORAL_COVE,   2u },
    { "Sewer Glow-Eel", 55u, 48u, 0u, ZONE_SEWERS,       3u },
    { "Golden Koi",     90u, 65u, 1u, 255u,              4u },
    { "Sunken Boot",     6u,  0u, 0u, 255u,              1u }
};

const HearthShopItem HEARTH_SHOP_ITEMS[HEARTH_SHOP_ITEM_COUNT] = {
    { "Sun Tonic",     "Heal +45 HP in battle",     20u, 0u },
    { "Star Drop",     "Restore +15 MP to party",   28u, 1u },
    { "Courage Snack", "Full HP & MP feast",        55u, 2u },
    { "River Lure",    "Wider reel bar + rare Koi", 60u, 3u },
    { "Glow Lure",     "Master lure for all piers", 120u,4u },
    { "Golden Babana", "Sweet desert heal +65 HP",  32u, 5u }
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

static void place_autumn_maple(uint8_t *bg1, uint8_t *bg2, uint8_t *col, int tx, int ty) {
    set_bg1(bg1, col, tx, ty, T_MAPLE_TRUNK, COL_SOLID);
    set_bg2(bg2, tx - 1, ty - 2, O_MAPLE_TL);
    set_bg2(bg2, tx,     ty - 2, O_MAPLE_TR);
    set_bg2(bg2, tx + 1, ty - 2, O_MAPLE_TR);
    set_bg2(bg2, tx - 1, ty - 1, O_MAPLE_TL);
    set_bg2(bg2, tx,     ty - 1, O_MAPLE_TR);
    set_bg2(bg2, tx + 1, ty - 1, O_MAPLE_TR);
}

static void place_alpine_pine(uint8_t *bg1, uint8_t *bg2, uint8_t *col, int tx, int ty) {
    set_bg1(bg1, col, tx, ty, T_PINE_TRUNK, COL_SOLID);
    set_bg2(bg2, tx, ty - 2, O_PINE_TOP);
    set_bg2(bg2, tx, ty - 1, O_PINE_TOP);
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
 * ZONE 5: ROWAN'S CEDAR COTTAGE INTERIOR (Hearth, Bedroom & Attic Loft)
 * ============================================================================ */
static void build_zone_rowan_home(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    /* Outer dark timber framing around snug centered interior */
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, HEARTH_MAP_H, T_INTERIOR_WALL, COL_SOLID);

    /* Main Cottage Floor: Honey Cedar Planks (x=13..30, y=8..19) */
    fill_rect(bg1, col, 13, 8, 18, 12, T_WOOD_FLOOR, COL_WALKABLE);

    /* North Interior Wall with Windows & Crackling Stone Fireplace */
    set_bg1(bg1, col, 16, 7, T_WALL_WINDOW, COL_SOLID);
    set_bg1(bg1, col, 23, 7, T_WALL_WINDOW, COL_SOLID);
    set_bg1(bg1, col, 19, 7, T_FIREPLACE,   COL_SOLID);
    set_bg1(bg1, col, 20, 7, T_FIREPLACE,   COL_SOLID);

    /* Woven Burgundy & Gold Hearthside Rug */
    fill_rect(bg1, col, 18, 9, 4, 3, T_HEARTH_RUG, COL_WALKABLE);

    /* Left Bedroom Partition & Rowan's Patchwork Quilt Bed */
    set_bg1(bg1, col, 17, 13, T_INTERIOR_WALL, COL_SOLID);
    set_bg1(bg1, col, 17, 14, T_INTERIOR_WALL, COL_SOLID);
    set_bg1(bg1, col, 17, 15, T_INTERIOR_WALL, COL_SOLID);
    set_bg1(bg1, col, 14, 15, T_BED_QUILT,     COL_SOLID);
    set_bg1(bg1, col, 14, 16, T_BED_QUILT,     COL_SOLID);
    set_bg1(bg1, col, 14, 14, T_LANTERN_POST,  COL_SOLID);

    /* Right Attic Loft Landing & Grandma's Heirloom Cedar Chest */
    set_bg1(bg1, col, 25, 8,  T_INTERIOR_WALL, COL_SOLID);
    set_bg1(bg1, col, 25, 9,  T_INTERIOR_WALL, COL_SOLID);
    set_bg1(bg1, col, 25, 10, T_INTERIOR_WALL, COL_SOLID);
    set_bg1(bg1, col, 25, 11, T_STONE_STAIRS,  COL_WALKABLE);
    set_bg1(bg1, col, 25, 12, T_STONE_STAIRS,  COL_WALKABLE);
    fill_rect(bg1, col, 26, 8, 5, 5, T_WOOD_FLOOR, COL_WALKABLE);
    set_bg1(bg1, col, 29, 9,  T_ATTIC_CHEST,   COL_SOLID);
    set_bg1(bg1, col, 27, 8,  T_LANTERN_POST,  COL_SOLID);

    /* Cozy Tea Table Corner & South Front Doorway Exit */
    set_bg1(bg1, col, 28, 17, T_FLOWER_BED,    COL_SOLID);
    set_bg1(bg1, col, 29, 17, T_FLOWER_BED,    COL_SOLID);
    set_bg1(bg1, col, 20, 20, T_WOOD_FLOOR,    COL_WALKABLE);
    set_bg1(bg1, col, 21, 20, T_WOOD_FLOOR,    COL_WALKABLE);
    set_bg1(bg1, col, 19, 19, T_LANTERN_POST,  COL_SOLID);
    set_bg1(bg1, col, 22, 19, T_LANTERN_POST,  COL_SOLID);
}

/* ============================================================================
 * ZONE 0: ALDERBROOK (Ground-Up Autumn Mountain Rail Town, Peaks & Brook)
 * ============================================================================ */
static void build_zone_alderbrook(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash, sy;
    uint16_t i;
    static const int8_t maples[][2] = {
        {4, 11}, {16, 11}, {31, 11}, {35, 14}, {38, 17},
        {6, 17}, {26, 18}, {37, 8}, {8, 24}, {34, 24}
    };
    static const int8_t pines[][2] = {
        {3, 7}, {3, 15}, {3, 23}, {15, 7}, {29, 7},
        {34, 7}, {40, 12}, {41, 16}, {18, 24}, {26, 25}, {40, 23}
    };
    static const int8_t boulders[][2] = {
        {6, 15}, {14, 17}, {20, 17}, {27, 17}, {33, 15},
        {5, 22}, {12, 22}, {25, 23}, {35, 20}, {38, 13}, {36, 10}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);

    /* Base Autumn Mountain Meadow */
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 31 + y * 57) & 15;
            if (hash == 13 && y > 8 && y < 24 && x > 3 && x < 40) {
                set_bg1(bg1, col, x, y, T_FLOWERS, COL_WALKABLE);
            } else {
                set_bg1(bg1, col, x, y, T_AUTUMN_GRASS, COL_WALKABLE);
            }
        }
    }

    /* Distant Alpine Mountain Horizon (Rows 0..2) & Granite Cliff Escarpment (Rows 3..4) */
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, 3, T_MTN_SKY, COL_SOLID);
    fill_rect(bg1, col, 0, 3, HEARTH_MAP_W, 1, T_CLIFF_LEDGE, COL_SOLID);
    fill_rect(bg1, col, 0, 4, HEARTH_MAP_W, 1, T_CLIFF_WALL,  COL_SOLID);

    /* Rocky West & East Mountain Flanks + Southern Pine Rim */
    for (y = 5; y < HEARTH_MAP_H; y++) {
        set_bg1(bg1, col, 0, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, 1, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 2, y, T_CLIFF_WALL, COL_SOLID);
    }
    for (x = 0; x < HEARTH_MAP_W; x++) {
        set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_CLIFF_WALL,  COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 2, T_CLIFF_LEDGE, COL_SOLID);
    }

    /* Upper Mountain Rail Shelf: Iron Railway Tracks (y=5) & Station Platform (y=6..7) */
    fill_rect(bg1, col, 2, 5, 33, 1, T_RAIL_TRACK, COL_WALKABLE);
    fill_rect(bg1, col, 4, 6, 30, 2, T_ROCKY_PATH, COL_WALKABLE);
    /* Granite Retain Wall separating Rail Shelf from Pine Square, with 2 Stone Stairways */
    fill_rect(bg1, col, 2, 8, 33, 1, T_CLIFF_LEDGE, COL_SOLID);
    set_bg1(bg1, col, 14, 8, T_STONE_STAIRS, COL_WALKABLE);
    set_bg1(bg1, col, 15, 8, T_STONE_STAIRS, COL_WALKABLE);
    set_bg1(bg1, col, 25, 8, T_STONE_STAIRS, COL_WALKABLE);
    set_bg1(bg1, col, 26, 8, T_STONE_STAIRS, COL_WALKABLE);

    /* Alderbrook Station Master Booth (west end of platform) */
    place_cottage(bg1, bg2, col, 4, 6);
    set_bg1(bg1, col, 10, 7, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 24, 7, T_LANTERN_POST, COL_SOLID);

    /* Central Pine Square & Rocky Mountain Lanes */
    fill_rect(bg1, col, 7, 13, 28, 3, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 14, 9, 2, 5, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 25, 9, 2, 5, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 15, 16, 2, 8, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 29, 16, 2, 8, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 11, 23, 21, 2, T_ROCKY_PATH, COL_WALKABLE);

    /* Rowan & Grandma Mara's Cedar Cottage (x=9..13, y=10..12; door at 11,12) */
    place_cottage(bg1, bg2, col, 9, 10);
    set_bg1(bg1, col, 9,  13, T_FLOWER_BED,   COL_SOLID);
    set_bg1(bg1, col, 13, 13, T_FLOWER_BED,   COL_SOLID);
    set_bg1(bg1, col, 8,  13, T_LANTERN_POST, COL_SOLID);

    /* Maple's Mountain Bakery & General Store (x=22..26, y=10..12) */
    place_cottage(bg1, bg2, col, 22, 10);
    set_bg1(bg1, col, 21, 13, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 27, 13, T_FLOWER_BED,   COL_SOLID);
    set_bg1(bg1, col, 18, 14, T_SIGNPOST,     COL_SOLID);

    /* Winding Mountain Stream with Whitewater Rapids curving across the lower valley! */
    for (x = 2; x < HEARTH_MAP_W - 2; x++) {
        if (x < 12)      sy = 19;
        else if (x < 18) sy = 20;
        else if (x < 26) sy = 21;
        else if (x < 33) sy = 20;
        else             sy = 21;
        set_bg1(bg1, col, x, sy,     ((x & 1) == 0) ? T_RAPIDS : T_WATER, COL_WATER);
        set_bg1(bg1, col, x, sy + 1, ((x & 1) != 0) ? T_RAPIDS : T_WATER, COL_WATER);
    }

    /* Two Sturdy Timber Footbridges spanning the Winding Brook */
    for (x = 15; x <= 16; x++) {
        set_bg1(bg1, col, x, 19, T_BRIDGE_RAIL_N, COL_SOLID);
        set_bg1(bg1, col, x, 20, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 21, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 22, T_BRIDGE_RAIL_S, COL_SOLID);
    }
    for (x = 29; x <= 30; x++) {
        set_bg1(bg1, col, x, 19, T_BRIDGE_RAIL_N, COL_SOLID);
        set_bg1(bg1, col, x, 20, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 21, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 22, T_BRIDGE_RAIL_S, COL_SOLID);
    }

    /* Alderbrook Creek Fishing Pier at (22, 20) jutting south into the mountain brook */
    set_bg1(bg1, col, 22, 19, T_ROCKY_PATH, COL_WALKABLE);
    set_bg1(bg1, col, 22, 20, T_BRIDGE,     COL_WALKABLE);
    set_bg1(bg1, col, 21, 19, T_LANTERN_POST, COL_SOLID);

    /* Northeast Rocky Switchback Trail climbing toward Whispering Woods (Portal at 41,9) */
    fill_rect(bg1, col, 34, 13, 5, 2, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 37, 9,  5, 5, T_ROCKY_PATH, COL_WALKABLE);
    set_bg1(bg1, col, 38, 11, T_STONE_STAIRS, COL_WALKABLE);
    set_bg1(bg1, col, 39, 11, T_STONE_STAIRS, COL_WALKABLE);
    set_bg1(bg1, col, 41, 9,  T_ROCKY_PATH,   COL_WALKABLE);
    set_bg1(bg1, col, 42, 9,  T_ROCKY_PATH,   COL_WALKABLE);
    set_bg1(bg1, col, 40, 8,  T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 40, 10, T_LANTERN_POST, COL_SOLID);

    /* Mossy Boulders, Crimson Autumn Maples & Dark Alpine Spruces */
    for (i = 0; i < sizeof(boulders) / sizeof(boulders[0]); i++) {
        set_bg1(bg1, col, boulders[i][0], boulders[i][1], T_BOULDER, COL_SOLID);
    }
    for (i = 0; i < sizeof(maples) / sizeof(maples[0]); i++) {
        place_autumn_maple(bg1, bg2, col, maples[i][0], maples[i][1]);
    }
    for (i = 0; i < sizeof(pines) / sizeof(pines[0]); i++) {
        place_alpine_pine(bg1, bg2, col, pines[i][0], pines[i][1]);
    }
}

/* ============================================================================
 * ZONE 6: WHISPERING WOODS (Rocky Autumn Forest, Rapids & 3 Lantern Shrines)
 * ============================================================================ */
static void build_zone_whispering_woods(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, rx, hash;
    uint16_t i;
    static const int8_t maples[][2] = {
        {6, 9}, {16, 8}, {28, 8}, {33, 13}, {8, 16},
        {14, 21}, {29, 21}, {38, 17}, {24, 23}, {6, 23}
    };
    static const int8_t pines[][2] = {
        {4, 7}, {11, 7}, {21, 7}, {31, 7}, {40, 8},
        {4, 14}, {17, 13}, {27, 12}, {39, 13}, {40, 23}, {10, 24}
    };
    static const int8_t boulders[][2] = {
        {8, 13}, {15, 16}, {18, 19}, {24, 12}, {28, 18},
        {32, 16}, {36, 12}, {34, 22}, {7, 19}, {13, 14}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 23 + y * 47) & 15;
            if (hash == 11 && y > 5 && y < 24 && x > 3 && x < 40) {
                set_bg1(bg1, col, x, y, T_FLOWERS, COL_WALKABLE);
            } else {
                set_bg1(bg1, col, x, y, T_AUTUMN_GRASS, COL_WALKABLE);
            }
        }
    }

    /* Northern Alpine Mountain Horizon & Granite Ridge */
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, 3, T_MTN_SKY,     COL_SOLID);
    fill_rect(bg1, col, 0, 3, HEARTH_MAP_W, 1, T_CLIFF_LEDGE, COL_SOLID);
    fill_rect(bg1, col, 0, 4, HEARTH_MAP_W, 1, T_CLIFF_WALL,  COL_SOLID);
    for (y = 5; y < HEARTH_MAP_H; y++) {
        set_bg1(bg1, col, 0, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, 1, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 2, y, T_CLIFF_WALL, COL_SOLID);
    }
    for (x = 0; x < HEARTH_MAP_W; x++) {
        set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_CLIFF_WALL,  COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 2, T_CLIFF_LEDGE, COL_SOLID);
    }

    /* Foaming Mountain Rapids cascading diagonally from north ridge to south valley */
    for (y = 5; y < HEARTH_MAP_H - 2; y++) {
        rx = 23 - ((y - 5) >> 1);
        set_bg1(bg1, col, rx,     y, ((y & 1) == 0) ? T_RAPIDS : T_WATER, COL_WATER);
        set_bg1(bg1, col, rx + 1, y, ((y & 1) != 0) ? T_RAPIDS : T_WATER, COL_WATER);
    }

    /* Winding Rocky Switchback Trail from SW Entry (2,21) -> Bridge -> 3 Shrines -> NE Pass (37,5) */
    fill_rect(bg1, col, 2,  20, 12, 3, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 11, 15,  9, 6, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 10, 10,  5, 6, T_ROCKY_PATH, COL_WALKABLE); /* Jasper's Camp Clearing */
    fill_rect(bg1, col, 17, 15, 17, 2, T_ROCKY_PATH, COL_WALKABLE); /* Rapids Log Bridge Crossing */
    fill_rect(bg1, col, 32,  9,  6, 8, T_ROCKY_PATH, COL_WALKABLE);
    fill_rect(bg1, col, 36,  4,  3, 6, T_ROCKY_PATH, COL_WALKABLE); /* High Pass to Hollow Glade */
    set_bg1(bg1, col, 37, 4, T_STONE_STAIRS, COL_WALKABLE);
    set_bg1(bg1, col, 37, 5, T_STONE_STAIRS, COL_WALKABLE);

    /* Rustic Timber Bridge over Forest Rapids (x=17..20, y=15..16) */
    for (x = 17; x <= 20; x++) {
        set_bg1(bg1, col, x, 14, T_BRIDGE_RAIL_N, COL_SOLID);
        set_bg1(bg1, col, x, 15, T_BRIDGE,        COL_WALKABLE);
        set_bg1(bg1, col, x, 16, T_BRIDGE,        COL_WALKABLE);
        set_bg1(bg1, col, x, 17, T_BRIDGE_RAIL_S, COL_SOLID);
    }

    /* 3 Stone Lantern Shrine Clearings along the Woods Trail */
    fill_rect(bg1, col, 11, 16, 3, 3, T_SHRINE_FLOOR, COL_WALKABLE);
    set_bg1(bg1, col, 12, 17, T_LANTERN_POST, COL_SOLID); /* Shrine 0 */

    fill_rect(bg1, col, 25, 14, 3, 3, T_SHRINE_FLOOR, COL_WALKABLE);
    set_bg1(bg1, col, 26, 15, T_LANTERN_POST, COL_SOLID); /* Shrine 1 */

    fill_rect(bg1, col, 32,  8, 3, 3, T_SHRINE_FLOOR, COL_WALKABLE);
    set_bg1(bg1, col, 33,  9, T_LANTERN_POST, COL_SOLID); /* Shrine 2 */

    /* Trail Signpost & Campfire Lantern */
    set_bg1(bg1, col, 7, 20, T_SIGNPOST, COL_SOLID);
    set_bg1(bg1, col, 11, 10, T_FIREPLACE, COL_SOLID);

    /* Mossy Boulders, Autumn Maples & Alpine Spruces */
    for (i = 0; i < sizeof(boulders) / sizeof(boulders[0]); i++) {
        set_bg1(bg1, col, boulders[i][0], boulders[i][1], T_BOULDER, COL_SOLID);
    }
    for (i = 0; i < sizeof(maples) / sizeof(maples[0]); i++) {
        place_autumn_maple(bg1, bg2, col, maples[i][0], maples[i][1]);
    }
    for (i = 0; i < sizeof(pines) / sizeof(pines[0]); i++) {
        place_alpine_pine(bg1, bg2, col, pines[i][0], pines[i][1]);
    }
}

/* ============================================================================
 * ZONE 7: HOLLOW GLADE (High-Ridge Sanctuary, Alpine Vista & Monumental Oak)
 * ============================================================================ */
static void build_zone_hollow_glade(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash;
    uint16_t i;
    static const int8_t maples[][2] = {
        {8, 11}, {13, 10}, {29, 10}, {34, 11},
        {9, 21}, {33, 21}, {15, 22}, {27, 22}
    };
    static const int8_t pines[][2] = {
        {5, 9}, {5, 16}, {5, 22}, {38, 9}, {38, 16}, {38, 22},
        {11, 8}, {31, 8}
    };
    static const int8_t boulders[][2] = {
        {14, 13}, {28, 13}, {15, 18}, {27, 18},
        {7, 15}, {35, 15}, {17, 21}, {25, 21}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 17 + y * 37) & 15;
            if (hash == 9 && y > 8 && y < 24 && x > 6 && x < 37) {
                set_bg1(bg1, col, x, y, T_FLOWERS, COL_WALKABLE);
            } else {
                set_bg1(bg1, col, x, y, T_AUTUMN_GRASS, COL_WALKABLE);
            }
        }
    }

    /* Panoramic 5-Row Alpine Peak Skyline & Horseshoe Cliff Escarpment */
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, 5, T_MTN_SKY,     COL_SOLID);
    fill_rect(bg1, col, 0, 5, HEARTH_MAP_W, 1, T_CLIFF_LEDGE, COL_SOLID);
    fill_rect(bg1, col, 0, 6, HEARTH_MAP_W, 1, T_CLIFF_WALL,  COL_SOLID);
    for (y = 7; y < HEARTH_MAP_H; y++) {
        fill_rect(bg1, col, 0, y, 4, 1, T_CLIFF_WALL, COL_SOLID);
        fill_rect(bg1, col, HEARTH_MAP_W - 4, y, 4, 1, T_CLIFF_WALL, COL_SOLID);
    }
    for (x = 0; x < HEARTH_MAP_W; x++) {
        set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_CLIFF_WALL,  COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 2, T_CLIFF_LEDGE, COL_SOLID);
    }

    /* South Entry Stairway from Whispering Woods (x=20..22, y=24..26) */
    fill_rect(bg1, col, 20, 18, 3, 9, T_ROCKY_PATH, COL_WALKABLE);
    set_bg1(bg1, col, 20, 24, T_STONE_STAIRS, COL_WALKABLE);
    set_bg1(bg1, col, 21, 24, T_STONE_STAIRS, COL_WALKABLE);
    set_bg1(bg1, col, 22, 24, T_STONE_STAIRS, COL_WALKABLE);

    /* Tranquil Highland Spring on the Western Ledge */
    fill_rect(bg1, col, 8, 14, 4, 4, T_WATER, COL_WATER);
    set_bg1(bg1, col, 9,  15, T_RAPIDS, COL_WATER);
    set_bg1(bg1, col, 10, 16, T_RAPIDS, COL_WATER);

    /* Ancient Moss-Ringed Glade Sanctuary & The Monumental Hollow Oak! */
    fill_rect(bg1, col, 16, 10, 11, 8, T_SHRINE_FLOOR, COL_WALKABLE);
    fill_rect(bg1, col, 19, 12, 5,  3, T_ROCKY_PATH,   COL_WALKABLE);
    set_bg1(bg1, col, 20, 11, T_HOLLOW_OAK, COL_SOLID);
    set_bg1(bg1, col, 21, 11, T_HOLLOW_OAK, COL_SOLID);
    set_bg1(bg1, col, 22, 11, T_HOLLOW_OAK, COL_SOLID);
    set_bg1(bg1, col, 21, 12, T_SHRINE_RUNE, COL_WALKABLE); /* Glowing Threshold */
    for (x = 19; x <= 23; x++) {
        set_bg2(bg2, x, 8,  O_OAK_CROWN);
        set_bg2(bg2, x, 9,  O_OAK_CROWN);
        set_bg2(bg2, x, 10, O_OAK_CROWN);
    }
    set_bg1(bg1, col, 17, 16, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 25, 16, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 19, 19, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 23, 19, T_LANTERN_POST, COL_SOLID);

    for (i = 0; i < sizeof(boulders) / sizeof(boulders[0]); i++) {
        set_bg1(bg1, col, boulders[i][0], boulders[i][1], T_BOULDER, COL_SOLID);
    }
    for (i = 0; i < sizeof(maples) / sizeof(maples[0]); i++) {
        place_autumn_maple(bg1, bg2, col, maples[i][0], maples[i][1]);
    }
    for (i = 0; i < sizeof(pines) / sizeof(pines[0]); i++) {
        place_alpine_pine(bg1, bg2, col, pines[i][0], pines[i][1]);
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

/* ============================================================================
 * 256x192 FULL-TOROIDAL EVERTALE GLOBE (Zero-RAM Pure-ROM Streaming!)
 *
 * Backed by a 64x48 ROM macro-biome grid (each cell = 4x4 tiles, 3,072 bytes in ROM,
 * 0 bytes in RAM!). Every platform streams cells on demand via hearth_globe_cell().
 *
 * 7 Seamless Continents across the Toroidal Globe (X mod 256, Y mod 192):
 *  0: Emerald Heartland (Center-West)  — 8 Landmarks (Hollow Oak .. Timber Pier)
 *  1: Sands of Babana   (South-East)   — Hot dunes, camels, scorpions, sandworm, Oasis
 *  2: Suncrest Paradise (South-West)   — Beaches, turquoise reefs, pirates, Coral Cove
 *  3: Frostmere North   (Far North)    — Perpetual festive winter wonderland, penguins
 *  4: Canopy Wilds      (East)         — Amazon rainforest, treetop platforms, monkeys
 *  5: Cinderpeak Caldera(North-West)   — Fiery Death-Mountain volcano, obsidian & lava
 *  6: Aetheris Citadel  (North-East)   — High-tech magitek civilization, chrome & robots
 * ============================================================================ */
static const char GLOBE_MACRO_ROWS[HEARTH_ATLAS_H][HEARTH_ATLAS_W + 1] = {
/* 00 */ "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
/* 01 */ "~~~~~~~~~~~~~~~~~~~~~iiiiiiIIiiiiiiiiIIiiiii~~~~~~~~~~~~~~~~~~~~",
/* 02 */ "~~~~~~~~~~~~~~~~~~iiiiiIIiiiiiiiiiiiiiiiiIIiiii~~~~~~~~~~~~~~~~~",
/* 03 */ "~~~~~~~~~~~~~~~~iiiiiiiiiIIiiiiiiiIIiiiiiiiiiiiii~~~~~~~~~~~~~~~",
/* 04 */ "~~~~~~~~~~~~~~~iiiiiIIiiiiiiiiiiiiiiiiiiiiIIiiiiii~~~~~~~~~~~~~~",
/* 05 */ "~~~~~~~~~~~~~~~iiiiiiiiiIiiiiiiiiiiiiiiiiiiiIiiiii~~~~~~~~~~~~~~",
/* 06 */ "~~~~~~~~~~~~~~~~iiiiiiiiiiiIIiiiiiiiiIIiiiiiiiiii~~~~~~~~~~~~~~~",
/* 07 */ "~~~~~~~~~~~~~~~~~~iiiiiiiiiiiiiiiiiiiiiiiiiiiii~~~~~~~~~~~~~~~~~",
/* 08 */ "~~~~~~~~~~~~~~~~~~~~~iiiiiiiiiiiiiiiiiiiiiii~~~~~~~~~~~~~~~~~~~~",
/* 09 */ "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
/* 10 */ "~~~~~~VVVVVV~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~TTTTTT~~~",
/* 11 */ "~~~~VVvvvvVVV~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~TTttttttTT~",
/* 12 */ "~~~VVvvlvvvvVV~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~TTttttttttT~",
/* 13 */ "~~VVvvvlvvVVvvV~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~TTttTTttttttT",
/* 14 */ "~~VvvvlllvvvvvV~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~TTttttttttttTT",
/* 15 */ "~~VvvvllvvvVVvV~~~~~~mmmmmmmmmmmmmmmmmm~~~~~~~~~~~TttttttttttttT",
/* 16 */ "~~VVvvllvvvvvvV~~~~mggggggggggggggggggggm~~~~~~~~~TttttTTttttttT",
/* 17 */ "~~~VvvllvvvvvVV~~~bggggggghhhhhhhggggggggb~~~~~~~~TTttttttttttTT",
/* 18 */ "~~~VVvvlvvvvVV~~~bggggggggghhhhhggggggggggb~~~~~~~~TTttttttttTT~",
/* 19 */ "~~~~VVvvvvVVV~~~bgggggfffggggggggggfgggggggb~~~~~~~~~TTttttTT~~~",
/* 20 */ "~~~~~VVVVVV~~~~~bggggggggghhhhhhhggggggggggb~~~~~~~~~~~TTTT~~~~~",
/* 21 */ "~~~~~~~~~~~~~~~~bggggggggghhhhhhhggggfffgggb~~~~~~~~~~~~~~~~~~~~",
/* 22 */ "~~~~~~~~~~~~~~~~bgggggggggggggggggggffffgggb~~~~~~~~~~~~~~~~~~~~",
/* 23 */ "~~~~~~~~~~~~~~~~bgggggggggggggggggggffffgggb~~~~~~~~~~~~~~~~~~~~",
/* 24 */ "~~~~~~~~~~~~~~~~bggggggggggggggggggggggggggb~~~~~~~~~~~~~~~~~~~~",
/* 25 */ "~~~~~~~~~~~~~~~~bggggggggggggfffgggggggggggb~~~~~~~~JJJJJJJJ~~~~",
/* 26 */ "~~~~~~~~~~~~~~~~bggggggggggggfffgggggggggggb~~~~~~JJjjjjjjjjJJ~~",
/* 27 */ "~~~~~~~~~~~~~~~~bfgggggggggggggggggggggggggb~~~~~JjjjjJJjjjjjjJ~",
/* 28 */ "~~~~~~~~~~~~~~~~~bbbbbbbbbbbbbbbbbbbbbbbbbb~~~~~JJjjjjjjjjJJjjJ~",
/* 29 */ "~~~~~~~~~~~~~~~~~~~~ssssssssssssssss~~~~~~~~~~~~JjjjJJjjjjjjjjJ~",
/* 30 */ "~~~~~~~~~~~~~~~~~ssssssssssssssssssss~~~~~~~~~~~JjjjjjjjjjjjjjJ~",
/* 31 */ "~~~~~~~~~~~~~~sssssssssssssssssssssssss~~~~~~~~~JjjjjjjJJjjjjjJ~",
/* 32 */ "~~~~~~~~~~~sssssspppppppsssssssbbbbbbsss~~~~~~~~JjjjjjjjjjjjjjJ~",
/* 33 */ "~~~~~~~~pppppprrrpppppppprssssbddddddbbb~~~~~~~~JjjjJJjjjjjjjjJ~",
/* 34 */ "~~~~~~ppppppppprpppppppppprssbddddddddddb~~~~~~~JJjjjjjjjjJJjjJ~",
/* 35 */ "~~~~~ppppppppppppppppppppprrbddddddddddddb~~~~~~~JjjjjjjjjjjjjJ~",
/* 36 */ "~~~~pppppppppppppppppppppprrdddddddddddddd~~~~~~~JJjjjJJjjjjjJJ~",
/* 37 */ "~~~~pppppppppppppppppppprrrrdddddddddddddd~~~~~~~~~JJjjjjjjjJJ~~",
/* 38 */ "~~~~ppppppppppppppppppprrrrbdddddooooddddd~~~~~~~~~~~JJJJJJJ~~~~",
/* 39 */ "~~~~~pppppppppppppppppprsssbdddddooooddddd~~~~~~~~~~~~~~~~~~~~~~",
/* 40 */ "~~~~~~pppppppppppppppppr~~~bdddddddddddddb~~~~~~~~~~~~~~~~~~~~~~",
/* 41 */ "~~~~~~~~pppppppppppppp~~~~~~bdddddddddddb~~~~~~~~~~~~~~~~~~~~~~~",
/* 42 */ "~~~~~~~~~~pppppppppp~~~~~~~~~bdddddddddb~~~~~~~~~~~~~~~~~~~~~~~~",
/* 43 */ "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~bbbbbbbbb~~~~~~~~~~~~~~~~~~~~~~~~~",
/* 44 */ "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
/* 45 */ "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
/* 46 */ "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~",
/* 47 */ "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
};

/* 14 Major Globe Landmarks Across All 7 Continents */
const HearthGlobeLandmark HEARTH_GLOBE_LANDMARKS[HEARTH_GLOBE_LANDMARK_COUNT] = {
    /* Emerald Heartland (0..7) */
    {  74u, 108u, REGION_HEARTLAND, ZONE_HOLLOW_GLADE,   22u, 14u,  0u, 1u, "Hollow Oak Gate" },
    {  78u,  86u, REGION_HEARTLAND, ZONE_HAMLET,         10u, 16u,  1u, 1u, "Windmill Hamlet" },
    { 104u,  82u, REGION_HEARTLAND, ZONE_SUNHILL_MARKET, 22u, 24u,  1u, 1u, "Sunhill Market" },
    { 104u,  68u, REGION_HEARTLAND, ZONE_CASTLE_GATE,    22u, 24u,  2u, 1u, "Castle Aurelia" },
    { 132u,  86u, REGION_HEARTLAND, ZONE_BRAMBLE_EDGE,    5u, 14u,  3u, 1u, "Bramblewood Crossing" },
    { 130u, 106u, REGION_HEARTLAND, ZONE_SUNKEN_RUINS,    6u, 13u,  4u, 1u, "Sunken Watchtower" },
    { 138u,  74u, REGION_HEARTLAND, ZONE_WHISKERBURGH,    4u, 14u,  1u, 1u, "Whiskerburgh" },
    { 108u, 113u, REGION_HEARTLAND, ZONE_TIMBER_PIER,    22u,  6u,  5u, 1u, "Heartland Timber Pier" },
    /* Suncrest Paradise (8) */
    {  52u, 146u, REGION_PARADISE,  ZONE_CORAL_COVE,     22u,  8u,  6u, 1u, "Suncrest Coral Cove" },
    /* Sands of Babana (9) */
    { 132u, 154u, REGION_DESERT,    ZONE_BABANA_OASIS,   22u, 24u,  7u, 1u, "Oasis of Babana" },
    /* Outer 4 Continents — Preview Landmarks (10..13) */
    { 128u,  22u, REGION_FROSTMERE, 255u,                 0u,  0u,  8u, 0u, "Frostmere Noel Village" },
    { 212u, 126u, REGION_CANOPY,    255u,                 0u,  0u,  9u, 0u, "Canopy Treetop Village" },
    {  32u,  62u, REGION_VOLCANO,   255u,                 0u,  0u, 10u, 0u, "Cinderpeak Caldera Gate" },
    { 220u,  62u, REGION_AETHERIS,  255u,                 0u,  0u, 11u, 0u, "Aetheris Citadel Spire" }
};

/* 24 Ambient Biome Fauna Sprites Streamed by Proximity on the Toroidal Globe */
const HearthGlobeFauna HEARTH_GLOBE_FAUNA[HEARTH_GLOBE_FAUNA_COUNT] = {
    /* Sands of Babana — Camels */
    { 120u, 144u, SPR_FAUNA_CAMEL,   REGION_DESERT,    0u },
    { 144u, 150u, SPR_FAUNA_CAMEL,   REGION_DESERT,   12u },
    { 118u, 160u, SPR_FAUNA_CAMEL,   REGION_DESERT,   24u },
    { 152u, 142u, SPR_FAUNA_CAMEL,   REGION_DESERT,   36u },
    /* Suncrest Paradise — Tropical Monkeys & Camels/Pirates */
    {  36u, 144u, SPR_FAUNA_MONKEY,  REGION_PARADISE,  4u },
    {  64u, 154u, SPR_FAUNA_MONKEY,  REGION_PARADISE, 18u },
    {  46u, 136u, SPR_FAUNA_MONKEY,  REGION_PARADISE, 30u },
    {  62u, 138u, SPR_FAUNA_MONKEY,  REGION_PARADISE, 42u },
    /* Frostmere North — Penguins */
    { 100u,  18u, SPR_FAUNA_PENGUIN, REGION_FROSTMERE, 2u },
    { 116u,  24u, SPR_FAUNA_PENGUIN, REGION_FROSTMERE,14u },
    { 140u,  20u, SPR_FAUNA_PENGUIN, REGION_FROSTMERE,26u },
    { 156u,  26u, SPR_FAUNA_PENGUIN, REGION_FROSTMERE,38u },
    {  84u,  22u, SPR_FAUNA_PENGUIN, REGION_FROSTMERE,50u },
    { 170u,  18u, SPR_FAUNA_PENGUIN, REGION_FROSTMERE, 8u },
    /* Canopy Wilds — Jungle Monkeys */
    { 200u, 118u, SPR_FAUNA_MONKEY,  REGION_CANOPY,    6u },
    { 224u, 122u, SPR_FAUNA_MONKEY,  REGION_CANOPY,   20u },
    { 204u, 136u, SPR_FAUNA_MONKEY,  REGION_CANOPY,   34u },
    { 228u, 138u, SPR_FAUNA_MONKEY,  REGION_CANOPY,   48u },
    /* Aetheris Citadel — Hovering Magitek Robots */
    { 208u,  54u, SPR_FAUNA_ROBOT,   REGION_AETHERIS,  0u },
    { 232u,  56u, SPR_FAUNA_ROBOT,   REGION_AETHERIS, 16u },
    { 212u,  70u, SPR_FAUNA_ROBOT,   REGION_AETHERIS, 32u },
    { 230u,  68u, SPR_FAUNA_ROBOT,   REGION_AETHERIS, 44u },
    /* Cinderpeak Caldera — Obsidian Scouts */
    {  24u,  54u, SPR_FAUNA_ROBOT,   REGION_VOLCANO,  10u },
    {  40u,  70u, SPR_FAUNA_ROBOT,   REGION_VOLCANO,  28u }
};

static uint8_t macro_char_to_region(char c, uint8_t mx, uint8_t my) {
    if (c == 'i' || c == 'I') return REGION_FROSTMERE;
    if (c == 'v' || c == 'V' || c == 'l') return REGION_VOLCANO;
    if (c == 't' || c == 'T') return REGION_AETHERIS;
    if (c == 'j' || c == 'J') return REGION_CANOPY;
    if (c == 'd' || c == 'o') return REGION_DESERT;
    if (c == 'p' || c == 'r') return REGION_PARADISE;
    if (c == 'g' || c == 'h' || c == 'f' || c == 'm') return REGION_HEARTLAND;
    if (c == 'b') {
        if (my >= 32u) return (mx < 22u) ? REGION_PARADISE : REGION_DESERT;
        return REGION_HEARTLAND;
    }
    /* Ocean / Shallows infer nearest continent band cleanly for seamless audio/palette transitions */
    if (my <= 9u) return REGION_FROSTMERE;
    if (my <= 22u) {
        if (mx < 15u) return REGION_VOLCANO;
        if (mx >= 46u) return REGION_AETHERIS;
        return REGION_HEARTLAND;
    }
    if (my <= 30u) {
        if (mx >= 44u) return REGION_CANOPY;
        return REGION_HEARTLAND;
    }
    if (mx >= 44u && my <= 40u) return REGION_CANOPY;
    if (mx < 22u) return REGION_PARADISE;
    if (mx < 44u) return REGION_DESERT;
    return REGION_HEARTLAND;
}

/* Multiply-free 65-byte-stride lookup: my * 65 + mx == (my << 6) + my + mx (zero __mulhi3 on SNES 65816!) */
#define GLOBE_MACRO_CHAR(mx, my) \
    ((&GLOBE_MACRO_ROWS[0][0])[((uint16_t)(my) << 6) + (uint16_t)(my) + (uint16_t)(mx)])

uint8_t hearth_globe_atlas_cell(uint8_t mx, uint8_t my) {
    char c;
    mx = (uint8_t)(mx & 63u);
    while (my >= (uint8_t)HEARTH_ATLAS_H) my = (uint8_t)(my - (uint8_t)HEARTH_ATLAS_H);
    c = GLOBE_MACRO_CHAR(mx, my);
    switch (c) {
        case '~': return 0u; /* Deep Cobalt Ocean */
        case 's': return 1u; /* Turquoise Shallows (Raftable) */
        case 'r': return 2u; /* Coral Reef Shoals (Raftable) */
        case 'g': case 'h': case 'f': case 'm': return 3u; /* Emerald Heartland */
        case 'b': return (my >= 32u) ? ((mx < 22u) ? 5u : 4u) : 3u;
        case 'd': case 'o': return 4u; /* Sands of Babana */
        case 'p': return 5u; /* Suncrest Paradise */
        case 'i': case 'I': return 6u; /* Frostmere North */
        case 'j': case 'J': return 7u; /* Canopy Wilds */
        case 'v': case 'V': case 'l': return 8u; /* Cinderpeak Caldera */
        case 't': case 'T': return 9u; /* Aetheris Citadel */
        default:  return 0u;
    }
}

uint8_t hearth_globe_region_at(uint16_t tx, uint16_t ty) {
    uint8_t mx = (uint8_t)((tx & 255u) >> 2);
    uint16_t y = ty;
    uint8_t my;
    while (y >= HEARTH_GLOBE_H) y = (uint16_t)(y - HEARTH_GLOBE_H);
    my = (uint8_t)(y >> 2);
    return macro_char_to_region(GLOBE_MACRO_CHAR(mx, my), mx, my);
}

void hearth_globe_cell(uint16_t tx, uint16_t ty, uint8_t *out_bg1, uint8_t *out_bg2, uint8_t *out_col, uint8_t *out_region) {
    uint16_t x = (uint16_t)(tx & 255u);
    uint16_t y = ty;
    uint8_t  mx, my, lx, ly, hash;
    char     c;
    uint8_t  bg1 = T_WORLD_OCEAN;
    uint8_t  bg2 = O_NONE;
    uint8_t  col = COL_WATER;

    while (y >= HEARTH_GLOBE_H) y = (uint16_t)(y - HEARTH_GLOBE_H);
    mx = (uint8_t)(x >> 2);
    my = (uint8_t)(y >> 2);
    c  = GLOBE_MACRO_CHAR(mx, my);

    /* Multiply-free 4-bit deterministic hash (pure shift/add/xor for 3.58MHz SNES 65816) */
    hash = (uint8_t)(((x + (x << 2)) ^ (y + (y << 2) + (y << 1)) ^ ((x >> 2) + (y >> 3))) & 15u);

    /* 1. Base macro-biome decode via O(1) switch (0 bytes RAM, 0 multiplies, 0 divides) */
    switch (c) {
        case '~':
            bg1 = T_WORLD_OCEAN;   col = COL_WATER;
            break;
        case 's':
            bg1 = T_WORLD_SHALLOW; col = COL_SHALLOW;
            break;
        case 'r':
            bg1 = ((hash & 3u) == 0u) ? T_WORLD_CORAL : T_WORLD_SHALLOW;
            col = COL_SHALLOW;
            break;
        case 'b':
            bg1 = T_WORLD_BEACH;   col = COL_WALKABLE;
            break;
        case 'g':
            if (hash == 2u || hash == 10u) bg1 = T_GRASS_ALT;
            else if (hash == 7u)           bg1 = T_FLOWERS;
            else                           bg1 = T_GRASS;
            col = COL_WALKABLE;
            break;
        case 'h':
            bg1 = ((hash & 3u) == 0u) ? T_GRASS : T_WORLD_HILL;
            col = COL_WALKABLE;
            break;
        case 'f':
            bg1 = T_WORLD_FOREST;  col = COL_SOLID;
            break;
        case 'm':
        case 'I':
        case 'V':
            bg1 = T_WORLD_PEAK;    col = COL_SOLID;
            break;
        case 'd':
            bg1 = (hash == 5u) ? T_WORLD_BEACH : T_WORLD_SAND;
            col = COL_WALKABLE;
            break;
        case 'o':
            lx = (uint8_t)(x & 3u);
            if (hash == 1u && (lx == 0u || lx == 3u)) {
                bg1 = T_PALM_TREE; col = COL_SOLID;
            } else {
                bg1 = (hash == 4u) ? T_FLOWERS : T_GRASS;
                col = COL_WALKABLE;
            }
            break;
        case 'p':
            lx = (uint8_t)(x & 3u);
            ly = (uint8_t)(y & 3u);
            if (hash == 3u && ly == 0u && lx == 2u) {
                bg1 = T_PALM_TREE; col = COL_SOLID;
            } else if (hash == 9u) {
                bg1 = T_GRASS;     col = COL_WALKABLE;
            } else {
                bg1 = T_WORLD_BEACH; col = COL_WALKABLE;
            }
            break;
        case 'i':
            bg1 = (hash == 6u) ? T_WORLD_HILL : T_WORLD_SNOW;
            col = COL_WALKABLE;
            break;
        case 'j':
            bg1 = (hash == 4u) ? T_FLOWERS : T_WORLD_JUNGLE;
            col = COL_WALKABLE;
            break;
        case 'J':
            bg1 = T_WORLD_FOREST;  col = COL_SOLID;
            break;
        case 'v':
            bg1 = (hash == 2u) ? T_BOULDER : T_ROCKY_PATH;
            col = (hash == 2u) ? COL_SOLID : COL_WALKABLE;
            break;
        case 'l':
            bg1 = T_WORLD_LAVA;    col = COL_LAVA;
            break;
        case 't':
            bg1 = (hash == 5u) ? T_COBBLE : T_WORLD_TECH;
            col = COL_WALKABLE;
            break;
        case 'T':
            bg1 = T_WORLD_CASTLE;  col = COL_SOLID;
            break;
        default:
            break;
    }

    /* 2. Y-Band Culled Landmark & Road Clearings (skips 90%+ of globe tiles in 1 branch!) */
    if (y >= 64u && y <= 118u) {
        if (x >= 68u && x <= 148u) {
            /* Hollow Oak Clearing (72..82, 106..113) + North Road to Windmill Hamlet (77..79, 86..111) */
            if (y >= 106u && y <= 113u && x >= 72u && x <= 82u) {
                bg1 = (y >= 110u && y <= 111u) ? T_DIRT : T_GRASS; col = COL_WALKABLE;
            }
            if (x >= 77u && x <= 79u && y >= 86u && y <= 111u) {
                bg1 = T_DIRT; col = COL_WALKABLE;
            }
            /* Main West-East Highway: Windmill Hamlet (78,87) -> Sunhill (104,87) -> Bramblewood (132,87) */
            if (y >= 86u && y <= 88u && x >= 77u && x <= 135u) {
                bg1 = T_DIRT; col = COL_WALKABLE;
            }
            /* Central Royal Cobble Avenue: Castle Aurelia (104,69) -> Sunhill (104,82) -> Timber Pier (108,114) */
            if (x >= 103u && x <= 105u && y >= 69u && y <= 104u) {
                bg1 = T_COBBLE; col = COL_WALKABLE;
            }
            if (y >= 102u && y <= 104u && x >= 104u && x <= 110u) {
                bg1 = T_DIRT; col = COL_WALKABLE;
            }
            if (x >= 107u && x <= 109u && y >= 102u && y <= 114u) {
                bg1 = T_DIRT; col = COL_WALKABLE;
            }
            /* Southern Meadow Connector: Hollow Oak (78,110) -> Timber Pier (108,110) -> Watchtower (130,108) */
            if (y >= 109u && y <= 111u && x >= 74u && x <= 132u) {
                bg1 = T_DIRT; col = COL_WALKABLE;
            }
            /* East Bank Highway: Watchtower (130,108) -> Bramblewood (132,86) -> Whiskerburgh (138,75) */
            if (x >= 130u && x <= 132u && y >= 75u && y <= 109u) {
                bg1 = T_DIRT; col = COL_WALKABLE;
            }
            if (y >= 74u && y <= 76u && x >= 131u && x <= 140u) {
                bg1 = T_DIRT; col = COL_WALKABLE;
            }
            /* Heartland Timber Pier Wooden Jetty into Turquoise Bay (107..109, 114..116) */
            if (y >= 114u && y <= 116u && x >= 107u && x <= 109u) {
                bg1 = T_DOCK_PIER; col = COL_WALKABLE;
            }
            /* Hollow Oak Gate Landmark (74, 108) */
            if (x == 74u && y == 108u) { bg1 = T_HOLLOW_OAK; col = COL_SOLID; }
            if (y == 107u && x >= 73u && x <= 75u) { bg2 = O_OAK_CROWN; }
            if (x == 74u && y == 109u) { bg1 = T_SHRINE_RUNE; col = COL_WALKABLE; }
            /* Windmill Hamlet (77..79, 84..85) */
            if (y >= 84u && y <= 85u && x >= 77u && x <= 79u) { bg1 = T_WORLD_TOWN; col = COL_SOLID; }
            /* Sunhill Market (102..106, 80..81) with walkable bypasses */
            if (y >= 80u && y <= 81u && x >= 102u && x <= 106u) { bg1 = T_WORLD_TOWN; col = COL_SOLID; }
            if ((x == 101u || x == 107u) && y >= 79u && y <= 83u) { bg1 = T_COBBLE; col = COL_WALKABLE; }
            /* Castle Aurelia (101..107, 65..67) */
            if (y >= 65u && y <= 67u && x >= 101u && x <= 107u) { bg1 = T_WORLD_CASTLE; col = COL_SOLID; }
            /* Bramblewood Crossing (132, 85) */
            if (x == 132u && y == 85u) { bg1 = T_SHRINE_PILLAR; col = COL_SOLID; }
            if (x == 132u && y == 86u) { bg1 = T_SHRINE_RUNE;   col = COL_WALKABLE; }
            /* Sunken Watchtower (129..131, 104..105) */
            if (y >= 104u && y <= 105u && x >= 129u && x <= 131u) { bg1 = T_WORLD_RUINS; col = COL_SOLID; }
            /* Whiskerburgh (136..140, 72..73) */
            if (y >= 72u && y <= 73u && x >= 136u && x <= 140u) { bg1 = T_WORLD_TOWN; col = COL_SOLID; }
        } else if (y == 64u) {
            if (x >= 30u && x <= 34u) { bg1 = T_ROCKY_PATH; col = COL_WALKABLE; }
            else if (x >= 218u && x <= 222u) { bg1 = T_WORLD_TECH; col = COL_WALKABLE; }
        }
    } else if (y >= 143u && y <= 157u) {
        /* 3. Suncrest Coral Cove (52, 146) & Oasis of Babana (132, 154) Clearings & Docks */
        if (x >= 49u && x <= 55u && y <= 149u) {
            bg1 = T_WORLD_BEACH; col = COL_WALKABLE;
            if (x >= 51u && x <= 53u && y >= 144u && y <= 145u) { bg1 = T_WORLD_TOWN; col = COL_SOLID; }
            if (x == 52u && y == 147u) { bg1 = T_DOCK_PIER; col = COL_WALKABLE; }
        } else if (x >= 129u && x <= 135u && y >= 151u) {
            bg1 = T_GRASS; col = COL_WALKABLE;
            if (x >= 131u && x <= 133u && y >= 152u && y <= 153u) { bg1 = T_WORLD_TOWN; col = COL_SOLID; }
            if ((x == 129u || x == 135u) && y == 153u) { bg1 = T_PALM_TREE; col = COL_SOLID; }
        }
    } else if (y <= 24u) {
        /* 4a. Frostmere Noel Village Plaza */
        if (y >= 20u && x >= 126u && x <= 130u) {
            bg1 = T_WORLD_SNOW; col = COL_WALKABLE;
            if (x >= 127u && x <= 129u && y == 21u) { bg1 = T_WORLD_TOWN; col = COL_SOLID; }
        }
    } else if (y >= 60u && y <= 63u) {
        /* 4b. Cinderpeak Caldera Gate & Aetheris Citadel Spire */
        if (x >= 30u && x <= 34u) {
            bg1 = T_ROCKY_PATH; col = COL_WALKABLE;
            if (x >= 31u && x <= 33u && y == 61u) { bg1 = T_WORLD_RUINS; col = COL_SOLID; }
        } else if (x >= 218u && x <= 222u) {
            bg1 = T_WORLD_TECH; col = COL_WALKABLE;
            if (x >= 219u && x <= 221u && y == 61u) { bg1 = T_WORLD_CASTLE; col = COL_SOLID; }
        }
    } else if (y >= 124u && y <= 128u) {
        /* 4c. Canopy Treetop Village */
        if (x >= 210u && x <= 214u) {
            bg1 = T_WORLD_JUNGLE; col = COL_WALKABLE;
            if (x >= 211u && x <= 213u && y == 125u) { bg1 = T_DOCK_PIER; col = COL_SOLID; }
        }
    }

    if (out_bg1)    *out_bg1 = bg1;
    if (out_bg2)    *out_bg2 = bg2;
    if (out_col)    *out_col = col;
    if (out_region) *out_region = macro_char_to_region(c, mx, my);
}

/* ============================================================================
 * ZONE 13: HEARTLAND TIMBER PIER (Shipwright Orin's Coastal Raft Yard)
 * ============================================================================ */
static void build_zone_timber_pier(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash;
    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 29 + y * 43) & 15;
            if (y < 10) {
                set_bg1(bg1, col, x, y, (hash == 4) ? T_FLOWERS : T_GRASS, COL_WALKABLE);
            } else if (y < 17) {
                set_bg1(bg1, col, x, y, T_WORLD_BEACH, COL_WALKABLE);
            } else {
                set_bg1(bg1, col, x, y, T_WORLD_SHALLOW, COL_SHALLOW);
            }
        }
    }
    /* North & Side Timber Borders with North Highway Exit to Evertale Globe */
    for (x = 0; x < HEARTH_MAP_W; x++) {
        if (x < 19 || x > 24) {
            set_bg1(bg1, col, x, 0, T_WORLD_FOREST, COL_SOLID);
            set_bg1(bg1, col, x, 1, T_WORLD_FOREST, COL_SOLID);
        }
    }
    for (y = 0; y < 17; y++) {
        set_bg1(bg1, col, 0, y, T_BOULDER, COL_SOLID);
        set_bg1(bg1, col, 1, y, T_BOULDER, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, T_BOULDER, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 2, y, T_BOULDER, COL_SOLID);
    }
    /* North Road -> Shipwright Orin's Cottage -> Sun-Cedar Timber Pier! */
    fill_rect(bg1, col, 20, 0, 4, 14, T_DIRT, COL_WALKABLE);
    place_cottage(bg1, bg2, col, 10, 4);
    fill_rect(bg1, col, 19, 14, 6, 10, T_DOCK_PIER, COL_WALKABLE);
    fill_rect(bg1, col, 16, 21, 12, 3, T_DOCK_PIER, COL_WALKABLE);
    set_bg1(bg1, col, 18, 13, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 25, 13, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 18, 8,  T_SIGNPOST,     COL_SOLID);
    /* Decorative Coastal Boulders & Palms */
    set_bg1(bg1, col, 6,  14, T_BOULDER,   COL_SOLID);
    set_bg1(bg1, col, 36, 14, T_BOULDER,   COL_SOLID);
    set_bg1(bg1, col, 33, 7,  T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 37, 9,  T_PALM_TREE, COL_SOLID);
}

/* ============================================================================
 * ZONE 14: SUNCREST CORAL COVE (Tropical Paradise Beach, Coral Reef & Pirate Pier)
 * ============================================================================ */
static void build_zone_coral_cove(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash;
    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 19 + y * 53) & 15;
            if (y < 7) {
                set_bg1(bg1, col, x, y, (hash == 3) ? T_WORLD_CORAL : T_WORLD_SHALLOW, COL_SHALLOW);
            } else if (y < 19) {
                set_bg1(bg1, col, x, y, T_WORLD_BEACH, COL_WALKABLE);
            } else {
                set_bg1(bg1, col, x, y, (hash == 7) ? T_FLOWERS : T_GRASS, COL_WALKABLE);
            }
        }
    }
    /* North Entry Pier from Turquoise Lagoon */
    fill_rect(bg1, col, 20, 0, 4, 10, T_DOCK_PIER, COL_WALKABLE);
    /* Coral Lagoon Cove on East Side for Tropical Fishing! */
    fill_rect(bg1, col, 31, 10, 11, 8, T_WORLD_SHALLOW, COL_WATER);
    fill_rect(bg1, col, 34, 12, 5,  4, T_WORLD_CORAL,   COL_WATER);
    fill_rect(bg1, col, 27, 13, 5,  2, T_DOCK_PIER,     COL_WALKABLE);
    /* Beachside Cabanas & Palm Grove */
    place_cottage(bg1, bg2, col, 7,  11);
    place_cottage(bg1, bg2, col, 16, 19);
    set_bg1(bg1, col, 5,  18, T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 12, 22, T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 29, 21, T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 36, 22, T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 19, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 24, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 15, 16, T_SIGNPOST,     COL_SOLID);
    /* Perimeter edges */
    for (y = 7; y < HEARTH_MAP_H; y++) {
        set_bg1(bg1, col, 0, y, T_PALM_TREE, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, T_PALM_TREE, COL_SOLID);
    }
    for (x = 0; x < HEARTH_MAP_W; x++) {
        set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_WORLD_FOREST, COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 2, T_WORLD_FOREST, COL_SOLID);
    }
}

/* ============================================================================
 * ZONE 15: OASIS OF BABANA (Sands of Babana Desert Outpost & Golden Babana Stall)
 * ============================================================================ */
static void build_zone_babana_oasis(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash;
    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 31 + y * 47) & 15;
            set_bg1(bg1, col, x, y, (hash == 6) ? T_WORLD_BEACH : T_WORLD_SAND, COL_WALKABLE);
        }
    }
    /* Sandstone Canyon Rim */
    for (x = 0; x < HEARTH_MAP_W; x++) {
        set_bg1(bg1, col, x, 0, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, x, 1, T_CLIFF_LEDGE, COL_SOLID);
        if (x < 19 || x > 24) {
            set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_CLIFF_WALL, COL_SOLID);
            set_bg1(bg1, col, x, HEARTH_MAP_H - 2, T_CLIFF_WALL, COL_SOLID);
        }
    }
    for (y = 0; y < HEARTH_MAP_H; y++) {
        set_bg1(bg1, col, 0, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, 1, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 2, y, T_CLIFF_WALL, COL_SOLID);
    }
    /* Lush Emerald Oasis Ring & Shimmering Spring */
    fill_rect(bg1, col, 11, 6,  22, 13, T_GRASS, COL_WALKABLE);
    fill_rect(bg1, col, 17, 9,  10, 5,  T_WORLD_SHALLOW, COL_WATER);
    fill_rect(bg1, col, 20, 18, 4,  10, T_COBBLE, COL_WALKABLE);
    /* Desert Pavilion & Babana Market */
    place_cottage(bg1, bg2, col, 7,  6);
    place_cottage(bg1, bg2, col, 30, 6);
    set_bg1(bg1, col, 13, 8,  T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 29, 14, T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 12, 16, T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 32, 19, T_PALM_TREE, COL_SOLID);
    set_bg1(bg1, col, 18, 17, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 25, 17, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 18, 22, T_SIGNPOST,     COL_SOLID);
}

/* ============================================================================
 * ZONE 9: SUNHILL MARKET (Welcoming Hilltop Market Town beneath Castle Aurelia!)
 * ============================================================================ */
static void build_zone_sunhill_market(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash;
    uint16_t i;
    static const int8_t trees[][2] = {
        {4, 10}, {4, 21}, {39, 10}, {39, 21}, {10, 23}, {33, 23}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 23 + y * 47) & 15;
            if (hash == 3)      set_bg1(bg1, col, x, y, T_FLOWERS,   COL_WALKABLE);
            else if (hash == 9) set_bg1(bg1, col, x, y, T_GRASS_ALT, COL_WALKABLE);
            else                set_bg1(bg1, col, x, y, T_GRASS,     COL_WALKABLE);
        }
    }

    /* Northern Panoramic Skyline looking up toward Castle Aurelia's Spires! */
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, 4, T_MTN_SKY,    COL_SOLID);
    fill_rect(bg1, col, 0, 4, HEARTH_MAP_W, 1, T_CLIFF_LEDGE, COL_SOLID);
    /* Distant Castle Aurelia silhouette towers atop the northern sky ridge */
    fill_rect(bg1, col, 18, 1, 8, 3, T_WORLD_CASTLE, COL_SOLID);

    /* Perimeter stone parapet walls */
    for (y = 5; y < HEARTH_MAP_H; y++) {
        set_bg1(bg1, col, 0, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, 1, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 2, y, T_CLIFF_WALL, COL_SOLID);
    }
    for (x = 0; x < HEARTH_MAP_W; x++) {
        set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_CLIFF_WALL, COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 2, T_CLIFF_WALL, COL_SOLID);
    }

    /* Upper Fountain Terrace (y=6..12) + Carved Stone Stairs + Lower Market Plaza (y=14..23) */
    fill_rect(bg1, col, 8,  6,  28, 7,  T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 6,  14, 32, 10, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 6,  13, 32, 1,  T_CLIFF_LEDGE, COL_SOLID);
    fill_rect(bg1, col, 19, 12, 6,  3,  T_STONE_STAIRS, COL_WALKABLE);

    /* Central Sunlit Fountain on Upper Terrace (20..23, 8..9) */
    fill_rect(bg1, col, 20, 8, 4, 2, T_WATER, COL_WATER);
    set_bg1(bg1, col, 19, 8, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 24, 8, T_FLOWER_BED, COL_SOLID);

    /* Market Cottages & Stalls: Tessa's Bakery (West), Corin's Archive (North-East), Market Hall (East) */
    place_cottage(bg1, bg2, col, 9,  15);
    place_cottage(bg1, bg2, col, 29,  6);
    place_cottage(bg1, bg2, col, 30, 15);
    place_cottage(bg1, bg2, col, 9,   6);

    /* Market Lanterns, Flower Beds & Signpost */
    set_bg1(bg1, col, 17, 13, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 26, 13, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 15, 20, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 28, 20, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 18, 21, T_SIGNPOST,     COL_SOLID);
    set_bg1(bg1, col, 14, 18, T_FLOWER_BED,   COL_SOLID);
    set_bg1(bg1, col, 29, 18, T_FLOWER_BED,   COL_SOLID);

    /* Southern Hilltop Gate Road back to Evertale Continent World Map */
    fill_rect(bg1, col, 19, 23, 6, 5, T_COBBLE, COL_WALKABLE);
    set_bg1(bg1, col, 18, 25, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 25, 25, T_LANTERN_POST, COL_SOLID);

    for (i = 0; i < sizeof(trees) / sizeof(trees[0]); i++) {
        place_tree(bg1, bg2, col, trees[i][0], trees[i][1]);
    }
}

/* ============================================================================
 * ZONE 10: CASTLE AURELIA GATEHOUSE (Enterable Grand Drawbridge & Courtyard)
 * ============================================================================ */
static void build_zone_castle_gate(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y;
    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, HEARTH_MAP_H, T_GRASS, COL_WALKABLE);
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, 4, T_MTN_SKY, COL_SOLID);

    /* Grand White-Stone Castle Walls & Twin Towers */
    fill_rect(bg1, col, 4, 4, 36, 6, T_WORLD_CASTLE, COL_SOLID);
    fill_rect(bg1, col, 18, 8, 8, 2, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 21, 9, T_WALL_DOOR, COL_SOLID);
    set_bg1(bg1, col, 22, 9, T_WALL_DOOR, COL_SOLID);
    set_bg2(bg2, 21, 9, O_SHRINE_ARCH);
    set_bg2(bg2, 22, 9, O_SHRINE_ARCH);

    /* Sparkling Royal Moat & Wide Limestone Drawbridge */
    fill_rect(bg1, col, 2, 16, 40, 4, T_WATER, COL_WATER);
    fill_rect(bg1, col, 18, 10, 8, 18, T_COBBLE, COL_WALKABLE);
    for (y = 16; y <= 19; y++) {
        for (x = 19; x <= 24; x++) {
            set_bg1(bg1, col, x, y, T_BRIDGE, COL_WALKABLE);
        }
    }
    fill_rect(bg1, col, 11, 10, 22, 6, T_COBBLE, COL_WALKABLE); /* Courtyard Terrace */

    /* Royal Lanterns & Flower Beds */
    set_bg1(bg1, col, 16, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 27, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 18, 22, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 25, 22, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 14, 13, T_FLOWER_BED,   COL_SOLID);
    set_bg1(bg1, col, 29, 13, T_FLOWER_BED,   COL_SOLID);
    set_bg1(bg1, col, 17, 23, T_SIGNPOST,     COL_SOLID);

    /* Side border hedges */
    for (y = 4; y < HEARTH_MAP_H; y++) {
        set_bg1(bg1, col, 0, y, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, 1, y, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 2, y, T_BUSH, COL_SOLID);
    }
    for (x = 0; x < 18; x++) set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_BUSH, COL_SOLID);
    for (x = 26; x < HEARTH_MAP_W; x++) set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_BUSH, COL_SOLID);
}

/* ============================================================================
 * ZONE 11: BRAMBLEWOOD CROSSING (Enterable Deep Forest Edge & Familiar Shrine)
 * ============================================================================ */
static void build_zone_bramble_edge(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash;
    uint16_t i;
    static const int8_t trees[][2] = {
        {6, 7}, {10, 6}, {16, 6}, {28, 6}, {34, 7}, {38, 9},
        {8, 19}, {14, 22}, {30, 21}, {36, 18}, {22, 7}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 31 + y * 59) & 15;
            if (hash == 2)      set_bg1(bg1, col, x, y, T_FLOWERS,   COL_WALKABLE);
            else if (hash == 7) set_bg1(bg1, col, x, y, T_GRASS_ALT, COL_WALKABLE);
            else                set_bg1(bg1, col, x, y, T_GRASS,     COL_WALKABLE);
        }
    }
    fill_perimeter(bg1, col, T_WORLD_FOREST);

    /* West Entry Trail -> Mossy Campfire Clearing -> Ancient Boundary Stone Circle */
    fill_rect(bg1, col, 0,  12, 16, 4, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 14, 10, 14, 9, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 28, 11, 8,  7, T_SHRINE_FLOOR, COL_WALKABLE);
    fill_rect(bg1, col, 30, 13, 3,  3, T_SHRINE_RUNE,  COL_WALKABLE);
    set_bg1(bg1, col, 28, 11, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 35, 11, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 28, 17, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 35, 17, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 18, 11, T_LANTERN_POST,  COL_SOLID);
    set_bg1(bg1, col, 12, 16, T_SIGNPOST,      COL_SOLID);

    /* Wildgrass patches */
    fill_rect(bg1, col, 16, 19, 8, 3, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 24, 7,  6, 3, T_TALL_GRASS, COL_TALL_GRASS);

    for (i = 0; i < sizeof(trees) / sizeof(trees[0]); i++) {
        place_tree(bg1, bg2, col, trees[i][0], trees[i][1]);
    }
}

/* ============================================================================
 * ZONE 12: SUNKEN WATCHTOWER (Enterable Coastal Cliffside Ruins)
 * ============================================================================ */
static void build_zone_sunken_ruins(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y;
    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);
    fill_rect(bg1, col, 0, 0, HEARTH_MAP_W, HEARTH_MAP_H, T_GRASS, COL_WALKABLE);

    /* Sparkling Southern & Eastern Ocean Cliffs */
    fill_rect(bg1, col, 0, 21, HEARTH_MAP_W, 7, T_WORLD_OCEAN, COL_WATER);
    fill_rect(bg1, col, 35, 0, 9, HEARTH_MAP_H, T_WORLD_OCEAN, COL_WATER);
    fill_rect(bg1, col, 0, 20, 35, 1, T_CLIFF_LEDGE, COL_SOLID);
    fill_rect(bg1, col, 34, 0, 1, 21, T_CLIFF_WALL,  COL_SOLID);

    /* North & West Borders with West Path Entry */
    for (x = 0; x < 35; x++) {
        set_bg1(bg1, col, x, 0, T_BOULDER, COL_SOLID);
        set_bg1(bg1, col, x, 1, T_BOULDER, COL_SOLID);
    }
    for (y = 0; y < 20; y++) {
        set_bg1(bg1, col, 0, y, T_BOULDER, COL_SOLID);
        set_bg1(bg1, col, 1, y, T_BOULDER, COL_SOLID);
    }
    fill_rect(bg1, col, 0, 11, 12, 4, T_ROCKY_PATH, COL_WALKABLE);

    /* Weathered Stone Watchtower Courtyard & Ancient Pillars */
    fill_rect(bg1, col, 12, 6, 18, 12, T_SHRINE_FLOOR, COL_WALKABLE);
    fill_rect(bg1, col, 19, 9, 4,  4,  T_SHRINE_RUNE,  COL_WALKABLE);
    fill_rect(bg1, col, 18, 4, 6,  3,  T_WORLD_RUINS,  COL_SOLID);
    set_bg1(bg1, col, 12, 6,  T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 29, 6,  T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 12, 17, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 29, 17, T_SHRINE_PILLAR, COL_SOLID);
    set_bg1(bg1, col, 15, 14, T_LANTERN_POST,  COL_SOLID);
    set_bg1(bg1, col, 26, 14, T_LANTERN_POST,  COL_SOLID);
    set_bg1(bg1, col, 11, 15, T_SIGNPOST,      COL_SOLID);
    set_bg2(bg2, 12, 11, O_SHRINE_ARCH);
    set_bg2(bg2, 12, 12, O_SHRINE_ARCH);
}

void hearth_build_zone_map(uint8_t zone_id, uint8_t *bg1, uint8_t *bg2, uint8_t *col, uint8_t *out_w, uint8_t *out_h) {
    if (zone_id == ZONE_EVERTALE_WORLD) {
        /* 256x192 Globe streams directly from ROM via hearth_globe_cell() — zero RAM fill needed! */
        if (out_w) *out_w = (uint8_t)HEARTH_MAP_W;
        if (out_h) *out_h = (uint8_t)HEARTH_MAP_H;
        return;
    }
    if (out_w) *out_w = (uint8_t)HEARTH_MAP_W;
    if (out_h) *out_h = (uint8_t)HEARTH_MAP_H;
    if (zone_id == ZONE_ALDERBROOK) {
        build_zone_alderbrook(bg1, bg2, col);
    } else if (zone_id == ZONE_ROWAN_HOME) {
        build_zone_rowan_home(bg1, bg2, col);
    } else if (zone_id == ZONE_WHISPERING_WOODS) {
        build_zone_whispering_woods(bg1, bg2, col);
    } else if (zone_id == ZONE_HOLLOW_GLADE) {
        build_zone_hollow_glade(bg1, bg2, col);
    } else if (zone_id == ZONE_WHISKERBURGH) {
        build_zone_whiskerburgh(bg1, bg2, col);
    } else if (zone_id == ZONE_SEWERS) {
        build_zone_sewers(bg1, bg2, col);
    } else if (zone_id == ZONE_BOARHOLD) {
        build_zone_boarhold(bg1, bg2, col);
    } else if (zone_id == ZONE_SUNHILL_MARKET) {
        build_zone_sunhill_market(bg1, bg2, col);
    } else if (zone_id == ZONE_CASTLE_GATE) {
        build_zone_castle_gate(bg1, bg2, col);
    } else if (zone_id == ZONE_BRAMBLE_EDGE) {
        build_zone_bramble_edge(bg1, bg2, col);
    } else if (zone_id == ZONE_SUNKEN_RUINS) {
        build_zone_sunken_ruins(bg1, bg2, col);
    } else if (zone_id == ZONE_TIMBER_PIER) {
        build_zone_timber_pier(bg1, bg2, col);
    } else if (zone_id == ZONE_CORAL_COVE) {
        build_zone_coral_cove(bg1, bg2, col);
    } else if (zone_id == ZONE_BABANA_OASIS) {
        build_zone_babana_oasis(bg1, bg2, col);
    } else {
        hearth_build_sandbox_map(bg1, bg2, col);
    }
}

/* ============================================================================
 * 6-ACT ILLUSTRATED OPENING CINEMA SCRIPT (21 Story Beats — Ends on Pip's Promise)
 * ============================================================================ */
const HearthCinemaBeat HEARTH_CINEMA_SCRIPT[HEARTH_CINEMA_BEAT_COUNT] = {
    /* ACT I: Alderbrook Station at Dusk */
    { 1u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_PAN_RIGHT, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT I - ALDERBROOK STATION", "",
      "High in the timberline pines sits Alderbrook,",
      "a quiet mountain town lulled by the evening train." },
    { 1u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_PAN_RIGHT, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT I - ALDERBROOK STATION", "",
      "Ever since Rowan's parents vanished in the mist,",
      "Grandma Mara's hillside cottage has been his home." },
    { 1u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_PAN_RIGHT, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT I - ALDERBROOK STATION", "GRANDMA MARA",
      "Hear that whistle, Rowan? The 6:15 express!",
      "Come inside before the ridge fog rolls in." },
    { 1u, CG_ALDERBROOK_DUSK, BGM_ALDERBROOK, CIN_CAM_PAN_RIGHT, CIN_FX_STEAM_DUSK, SFX_TEXT_HERO,
      "ACT I - ALDERBROOK STATION", "YOUNG ROWAN",
      "One more minute, Grandma! Mido spotted a",
      "golden firefly by the cedar fence - wait, Mido!" },

    /* ACT II: Into Whispering Woods */
    { 2u, CG_HOLLOW_OAK_CHASE, BGM_ALDERBROOK, CIN_CAM_ZOOM_IN, CIN_FX_FOREST_POLLEN, SFX_TEXT_ELDER,
      "ACT II - WHISPERING WOODS", "",
      "With a joyful yip, the amber fox-kit bolted",
      "past the rail trestle into Whispering Woods." },
    { 2u, CG_HOLLOW_OAK_CHASE, BGM_ALDERBROOK, CIN_CAM_ZOOM_IN, CIN_FX_FOREST_POLLEN, SFX_TEXT_HERO,
      "ACT II - WHISPERING WOODS", "YOUNG ROWAN",
      "Mido! Slow down, your red scarf is slipping!",
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
      "Towering windmills turned under bright blue skies",
      "while paper lanterns drifted over clear creeks." },
    { 3u, CG_EVERTALE_VISTA, BGM_CINEMA_WONDER, CIN_CAM_PAN_RIGHT, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_HERO,
      "ACT III - EVERTALE", "YOUNG ROWAN",
      "It's real... just like Grandma's storybooks!",
      "Look, Mido! Even the lanterns are floating!" },

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
      "ACT IV - THE VIOLET ECLIPSE", "YOUNG ROWAN",
      "G-Grandma?! Stop - you aren't her! Stay back!",
      "Grandma would NEVER look at me like that!" },
    { 4u, CG_SHADOW_GRANDMA, BGM_SHADOW, CIN_CAM_SHAKE_FLASH, CIN_FX_VIOLET_STORM, SFX_TEXT_ELDER,
      "ACT IV - THE VIOLET ECLIPSE", "",
      "Shaken to his core, Rowan scooped up Mido and",
      "scrambled blindly back through the Hollow Oak." },

    /* ACT V: Only a Bad Dream */
    { 5u, CG_BEDROOM_COMFORT, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT V - ONLY A BAD DREAM", "",
      "He ran until the pine woods gave way to timber",
      "walls, oil-lamp warmth, and a patchwork quilt." },
    { 5u, CG_BEDROOM_COMFORT, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "ACT V - ONLY A BAD DREAM", "GRANDMA MARA",
      "Shh, my sweet boy... I am right here beside you.",
      "Look - Mido is safe, and my hands are warm." },
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
      "remember, knock on the oak again. I'll wait." }
};

/* ============================================================================
 * CHAPTER 1 & 2 PAINTED CG SCRIPTS (Attic, Pip Dream, Hollow Oak, World Vista)
 * ============================================================================ */
const HearthCinemaBeat HEARTH_CG_ATTIC_SCRIPT[HEARTH_CG_ATTIC_COUNT] = {
    { 1u, CG_ATTIC_KEEPSAKE, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "CHAPTER I - THE ATTIC KEEPSAKE", "",
      "Beneath Grandma's folded autumn shawl, Rowan's",
      "fingers brushed a small tarnished brass bell." },
    { 1u, CG_ATTIC_KEEPSAKE, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "CHAPTER I - THE ATTIC KEEPSAKE", "",
      "Tied to its red ribbon was a pressed golden",
      "oak leaf that still hummed with faint warmth." },
    { 1u, CG_ATTIC_KEEPSAKE, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_HERO,
      "CHAPTER I - THE ATTIC KEEPSAKE", "ROWAN",
      "Wait... that's Mido's old bell! Why does this",
      "leaf make my chest ache like a forgotten song?" },
    { 1u, CG_ATTIC_KEEPSAKE, BGM_CINEMA_LULLABY, CIN_CAM_ZOOM_IN, CIN_FX_STEAM_DUSK, SFX_TEXT_ELDER,
      "CHAPTER I - THE ATTIC KEEPSAKE", "",
      "Outside the dormer glass, the mountain peaks",
      "burned copper-gold as dusk settled on Alderbrook." }
};

const HearthCinemaBeat HEARTH_CG_DREAM_SCRIPT[HEARTH_CG_DREAM_COUNT] = {
    { 1u, CG_PIP_DREAM_RETURN, BGM_CINEMA_LULLABY, CIN_CAM_PAN_LEFT, CIN_FX_MOON_FIREFLIES, SFX_TEXT_ELDER,
      "CHAPTER I - THE DREAM RETURNS", "",
      "With Mido's bell tucked beside his pillow, Rowan",
      "drifted beneath a silver autumn moon..." },
    { 1u, CG_PIP_DREAM_RETURN, BGM_CINEMA_LULLABY, CIN_CAM_PAN_LEFT, CIN_FX_MOON_FIREFLIES, SFX_TEXT_PIP,
      "CHAPTER I - THE DREAM RETURNS", "PIP",
      "Look how much you've grown, Rowan! You finally",
      "found the oak leaf I slipped into your coat!" },
    { 1u, CG_PIP_DREAM_RETURN, BGM_CINEMA_LULLABY, CIN_CAM_PAN_LEFT, CIN_FX_MOON_FIREFLIES, SFX_TEXT_PIP,
      "CHAPTER I - THE DREAM RETURNS", "PIP",
      "Evertale wasn't a nightmare, lad - a shadow",
      "wore a cruel mask to scare you from the gate." },
    { 1u, CG_PIP_DREAM_RETURN, BGM_CINEMA_LULLABY, CIN_CAM_PAN_LEFT, CIN_FX_MOON_FIREFLIES, SFX_TEXT_HERO,
      "CHAPTER I - THE DREAM RETURNS", "ROWAN",
      "Pip... I remember now! The open skies, the blue",
      "rivers - and the Hollow Oak up in Whispering Woods!" }
};

const HearthCinemaBeat HEARTH_CG_OAK_SCRIPT[HEARTH_CG_OAK_COUNT] = {
    { 1u, CG_HOLLOW_OAK_AWAKENS, BGM_CINEMA_WONDER, CIN_CAM_ZOOM_IN, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_ELDER,
      "CHAPTER I FINALE - THE HOLLOW OAK", "",
      "Mido's brass bell rang a single crystal note,",
      "and the moss-clad roots shivered with amber fire." },
    { 1u, CG_HOLLOW_OAK_AWAKENS, BGM_CINEMA_WONDER, CIN_CAM_ZOOM_IN, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_ELDER,
      "CHAPTER I FINALE - THE HOLLOW OAK", "",
      "Golden runes unfurled along the ancient bark,",
      "parting the night into sunlit clouds beyond." },
    { 1u, CG_HOLLOW_OAK_AWAKENS, BGM_CINEMA_WONDER, CIN_CAM_ZOOM_IN, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_HERO,
      "CHAPTER I FINALE - THE HOLLOW OAK", "ROWAN",
      "It's opening, Mido! Just like when I was little -",
      "only this time, I'm not running away." },
    { 1u, CG_HOLLOW_OAK_AWAKENS, BGM_CINEMA_WONDER, CIN_CAM_ZOOM_IN, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_ELDER,
      "CHAPTER I FINALE - THE HOLLOW OAK", "",
      "With an eager yip, the amber fox-kit darted",
      "straight through the shimmering oak archway!" },
    { 1u, CG_HOLLOW_OAK_AWAKENS, BGM_CINEMA_WONDER, CIN_CAM_ZOOM_IN, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_HERO,
      "CHAPTER I FINALE - THE HOLLOW OAK", "ROWAN",
      "Mido, wait up! Don't run off ahead of me!",
      "Rowan drew a breath and stepped into the light..." }
};

const HearthCinemaBeat HEARTH_CG_WORLD_ARRIVAL_SCRIPT[HEARTH_CG_WORLD_ARRIVAL_CNT] = {
    { 2u, CG_EVERTALE_WORLD_VISTA, BGM_CINEMA_WONDER, CIN_CAM_PAN_RIGHT, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_ELDER,
      "CHAPTER II - THE REALM OF EVERTALE", "",
      "Warm clover breeze washed over Rowan's face as",
      "an entire sunlit continent unfurled below him." },
    { 2u, CG_EVERTALE_WORLD_VISTA, BGM_CINEMA_WONDER, CIN_CAM_PAN_RIGHT, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_ELDER,
      "CHAPTER II - THE REALM OF EVERTALE", "",
      "Stone bridges spanned sapphire rivers, windmills",
      "turned by a lake, and white spires pierced the clouds." },
    { 2u, CG_EVERTALE_WORLD_VISTA, BGM_CINEMA_WONDER, CIN_CAM_PAN_RIGHT, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_HERO,
      "CHAPTER II - THE REALM OF EVERTALE", "ROWAN",
      "Whoa... it's huge! Wait - Mido! Where did you go?!",
      "I just saw a flash of his red scarf over the hill!" },
    { 2u, CG_EVERTALE_WORLD_VISTA, BGM_CINEMA_WONDER, CIN_CAM_PAN_RIGHT, CIN_FX_EVERTALE_LANTERNS, SFX_TEXT_ELDER,
      "CHAPTER II - THE REALM OF EVERTALE", "",
      "Roads wound toward a lakeside mill, a hilltop",
      "market town, deep woods, and the distant castle..." }
};

/* ============================================================================
 * BYTECODE CUTSCENE ROM TABLES (Alderbrook & Evertale Storyline!)
 * ============================================================================ */

/* Childhood Evertale Flashback Climax */
const HearthCutsceneOp CUTSCENE_SHADOW_GRANDMA[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_SPARK, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Young Rowan",
      "Mido! Look at this sunlit meadow... wait, why is the sky turning purple?!" },
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
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Young Rowan",
      "N-no!! Grandma was kind and warm! You aren't her! Stay away!!" },
    { OP_SFX, SFX_PORTAL, 0, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Memory",
      "Terrified, young Rowan scrambled back through the Hollow Oak into Alderbrook..." },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip (A Dream?)",
      "Psst, lad! At your window! That shadow wasn't your Grandma! Mido's safe - check the oak again!" },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Seasons Later",
      "It hurt too much to believe such a cruel reflection could be real. Rowan called it a nightmare and tucked it away as he grew up." },
    { OP_SET_FLAG, FLAG_FLASHBACK_DONE, 0, 0, NULL, NULL },
    { OP_WARP_ZONE, ZONE_ALDERBROOK, 9, 13, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* Reunion in Windmill Hamlet: Pip is REAL & grants Rowan the Hearthwood Wand! */
const HearthCutsceneOp CUTSCENE_PIP_REUNION[] = {
    { OP_FACE_ACTOR, ACTOR_HERO, DIR_RIGHT, 0, NULL, NULL },
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_PIP, SPR_EMOTE_SPARK, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Well, butter my lantern! Look how tall you've gotten, Rowan! You finally checked the Hollow Oak again!" },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_QUESTION, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Pip?! You... you weren't a dream at all! And Evertale is really here! Where's Mido?!" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Your amber fox-kit trotted across the hills! Follow the warm lanterns and ask friendly townsfolk along the way." },
    { OP_SFX, SFX_HEART_GIVE, 0, 0, NULL, NULL },
    { OP_SHAKE, 2, 12, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_HEART, 36, NULL, NULL },
    { OP_SET_FLAG, FLAG_WAND_RECEIVED, 0, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Take this Hearthwood Wand! Bramblekit and Lumina will fight beside you across Evertale!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* Whiskerburgh Mayor Brie grants the Lantern Charm & Sewer Grate Key */
const HearthCutsceneOp CUTSCENE_MAYOR_SEWER_KEY[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_SPARK, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Mayor Brie",
      "Squeak! A brave lad from beyond the Oak! We heard tales of your amber fox-kit Mido over in Sunhill Market!" },
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
      "Captain Tusk, hold onto your courage! Let the Hearthglass Pendant warm your spirit!" },
    { OP_SFX, SFX_HEART_GIVE, 0, 0, NULL, NULL },
    { OP_HEART_ORB, ACTOR_HERO, ACTOR_NPC1, 40, NULL, NULL },
    { OP_SHAKE, 3, 12, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CAPTAIN_MENDED, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC1, SPR_EMOTE_HEART, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Captain Tusk",
      "Hmph—snort! My chest feels warm again! Lord Ironfang fell under a pale witch's spell in the east sanctum. Break his Spell Locks!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* Finale after defeating Arc Boss Lord Ironfang */
const HearthCutsceneOp CUTSCENE_IRONFANG_VICTORY[] = {
    { OP_SFX, SFX_VICTORY_FANFARE, 0, 0, NULL, NULL },
    { OP_SHAKE, 3, 14, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_PIP, SPR_EMOTE_HEART, 40, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Lord Ironfang",
      "Urgh... the pale fog lifts from my eyes! That cold witch wearing an old shawl—she twists memories to steal hearts!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "I knew it... that shadow in the glade was never really my grandmother! And listen—Mido's brass bell!" },
    { OP_SFX, SFX_COIN, 0, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Boarhold's hearth shines again, Rowan—and the trail toward your parents and the Hearthglass Pendant leads deeper into Evertale!" },
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

/* ============================================================================
 * CHAPTER 1 ALDERBROOK BYTECODE CUTSCENES (Morning -> Afternoon -> Dusk)
 * ============================================================================ */
const HearthCutsceneOp CUTSCENE_CH1_MORNING_MARA[] = {
    { OP_FACE_ACTOR, ACTOR_NPC0, DIR_DOWN, 0, NULL, NULL },
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_HEART, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Grandma Mara",
      "Good morning, Rowan! Goodness, your jacket sleeves are already getting short on you. The 7:00 steam express just rolled in, and the maples along the ridge are bright as copper." },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Morning, Grandma! I had the strangest feeling when I heard the train whistle... like I almost remembered an old dream." },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Grandma Mara",
      "Fresh mountain air will clear the cobwebs, dear. Would you run two quick errands in Alderbrook for me?" },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Grandma Mara",
      "Pick up a honey-rye loaf from Baker Maple in Pine Square, and bring this hot ginger thermos up the steps to Stationmaster Barnaby." },
    { OP_SFX, SFX_CONFIRM, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH1_MARA_GREETED, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH1_MAPLE_LOAF[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC1, SPR_EMOTE_SPARK, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Baker Maple",
      "Well hello, Rowan! You've shot up like a mountain pine since last harvest - here's Grandma Mara's warm honey-rye loaf, plus a pocket biscuit for Mido!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Thanks, Maple! It smells amazing. Have you noticed the stream running extra bright down by the boulders today?" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Baker Maple",
      "Autumn snowmelt off the high peaks! Stop by anytime if you want to buy supplies or sell brook trout you catch at the creek pier." },
    { OP_SFX, SFX_COIN, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH1_MAPLE_LOAF, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH1_BARNABY_THERMOS[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_HEART, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Barnaby",
      "Bless Mara's heart - hot ginger tea right as the morning frost lifts off the iron rails! Look how tall you've gotten, Rowan - almost eye-level with Old No. 7's brass wheels!" },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Barnaby",
      "Funny thing: Old Woodsman Jasper swore he saw golden fireflies drifting up in Whispering Woods last night - way too late in autumn for bugs." },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Golden fireflies...? Wait, why did that just give me goosebumps?" },
    { OP_SFX, SFX_CONFIRM, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH1_BARNABY_THERMOS, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH1_DUSK_TEA[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_QUESTION, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Grandma Mara",
      "Oh, Rowan... you found Mido's little brass bell and that pressed oak leaf in the cedar chest. I couldn't bear to throw them away." },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Grandma, where did I get this golden leaf? The moment I touched it, I heard windmills turning in the wind." },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Grandma Mara",
      "You wandered into Whispering Woods when you were still knee-high and came home terribly frightened. Seasons have come and gone since then, and look how tall and steady you've grown." },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Grandma Mara",
      "The evening chill is settling over the valley, Rowan. Climb into your patchwork bed and rest - keep Mido's bell close if it comforts you." },
    { OP_SFX, SFX_HEAL, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH1_DUSK_TEA, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* ============================================================================
 * CHAPTER 2: SUNHILL MARKET & BRAMBLEWOOD STORY CUTSCENES
 * ============================================================================ */
const HearthCutsceneOp CUTSCENE_CH2_SUNHILL_TESSA[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_HEART, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Tessa the Baker",
      "Oh! Welcome to Sunhill Market, young traveler! Looking for a bright-eyed amber fox-kit wearing a red scarf?" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Yes! His name is Mido - he bolted right out of the trees after we arrived! Did you really see him?!" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Tessa the Baker",
      "Ha, couldn't miss him! He practically floated straight to my oven for a warm honey scone, then trotted up the stone steps toward Archivist Corin's bookstall!" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Tessa the Baker",
      "Climb the stairs to the upper fountain terrace - Corin knows every traveler who passes beneath Castle Aurelia's shadow." },
    { OP_SFX, SFX_CONFIRM, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH2_TESSA_CLUE, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH2_SUNHILL_CORIN[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC1, SPR_EMOTE_QUESTION, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Archivist Corin",
      "Ah - you must be the boy that nimble little fox-kit was waiting for! He's curled right beside our sunlit fountain just west of here." },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Archivist Corin",
      "Wait a moment, lad... may I glance at the brass bell in your coat? Hmm... mountain-oak engraving... and those steady sea-green eyes..." },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Is something wrong, sir? It's just Mido's old bell from Alderbrook..." },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Archivist Corin",
      "Nothing wrong at all, child. Only... many seasons ago, back when you would have been just a baby, a kind mapmaker and his wife arrived in Sunhill carrying a charm wrought from that very same mountain brass." },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Archivist Corin",
      "They spoke softly of a home beyond the mist, and left north toward the high passes before anyone knew where their road ended. Forgive an old man's ramblings - go greet your fox!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "(Back when I was just a baby...? Everyone back home says Mom and Dad are gone forever... Still... could they really have walked these same cobblestones?)" },
    { OP_SFX, SFX_CONFIRM, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH2_CORIN_HINTS, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH2_SUNHILL_REUNION[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC2, SPR_EMOTE_HEART, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Mido",
      "Yip! Yip! Mido leaps straight into Rowan's arms, tail wagging furiously as his brass bell chimes in the sunlit breeze!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Mido! Don't ever sprint off like that again - you nearly gave me a heart attack! Wait... look up by the fountain!" },
    { OP_SFX, SFX_HEART_GIVE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_SPARK, 32, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Ha! Found you two at last! Look how brave and tall you've grown, Rowan - you made it to Sunhill Market all on your own without a scratch!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Pip! You really ARE real! Listen - Grandma Mara's heart has been heavy ever since Mom and Dad vanished. And Corin just said two travelers from my world passed through here long ago..." },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Easy there, Rowan. Your heart is huge - and tales tell of an ancient Hearthglass Pendant hidden deep in Evertale that can heal even the deepest sorrow." },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "We'll search for answers together! First, though, wild creatures lurk beyond the safe roads. Visit Bramblewood Crossing east of Sunhill when you're ready to bond your first Familiar!" },
    { OP_SET_FLAG, FLAG_CH2_MIDO_REUNION, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH2_BOND_FAMILIAR[] = {
    { OP_SFX, SFX_HEART_GIVE, 0, 0, NULL, NULL },
    { OP_SHAKE, 2, 12, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_SPARK, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Look at the ancient boundary stones glowing! Your steady courage just called forth your very first Familiars - Bramblekit and Lumina!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Whoa... they feel warm, like standing beside Grandma's fireplace! I won't let anyone down." },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Now you can hold your own in the wilds! Next, head south to Heartland Timber Pier - Shipwright Orin builds Cedar Rafts for crossing the turquoise straits to Suncrest Paradise and the Sands of Babana!" },
    { OP_SET_FLAG, FLAG_CH2_FIRST_FAMILIAR, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

/* ============================================================================
 * CHAPTER 2 EXPANSION: CEDAR RAFT, SUNCREST PARADISE & SANDS OF BABANA
 * ============================================================================ */
const HearthCutsceneOp CUTSCENE_CH2_ORIN_START[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_SPARK, 28, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Shipwright Orin",
      "Ahoy there, lad! Look at those bright Familiars trailing behind you. Aiming to cross the turquoise straits to Suncrest Paradise or the Sands of Babana?" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Yes, sir! Archivist Corin said travelers from my world journeyed far across Evertale, and we need a way across the bay." },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Shipwright Orin",
      "A sturdy Sun-Cedar Raft will glide right over those turquoise shallows! Tell you what - last night's tide washed 3 Sun-Cedar Driftwood Planks onto this beach." },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Shipwright Orin",
      "Gather all 3 planks along the sandy shore here at Timber Pier and bring them back to me. I'll lash you a fine raft in no time!" },
    { OP_SFX, SFX_CONFIRM, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH2_ORIN_QUEST, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH2_ORIN_RAFT_BUILT[] = {
    { OP_SFX, SFX_CHEST_OPEN, 0, 0, NULL, NULL },
    { OP_SHAKE, 2, 12, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_HEART, 36, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Shipwright Orin",
      "Fine timber! Lash the crossbeams... cinch the hemp rope... and there she floats! One genuine Sun-Cedar Raft, all yours!" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Woohoo! Out on the Evertale Globe, just walk straight into the turquoise shallows to launch the Cedar Raft! Press M (or SELECT) anytime to open your World Atlas!" },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Shipwright Orin",
      "Mind the deep cobalt ocean swells, though - a raft hugs the turquoise straits. Sail southwest to Suncrest Coral Cove or southeast to the Sands of Babana!" },
    { OP_SET_FLAG, FLAG_CH2_RAFT_BUILT, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH2_CORAL_BARNACLE[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_SPARK, 30, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Capt. Barnacle",
      "Har-har! A young navigator arriving by Cedar Raft! Welcome to Suncrest Coral Cove - warmest sand, brightest coral reef, and best fishing pier in Evertale!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Whoa, this place is amazing! Have you seen any ships that can sail across the deep cobalt ocean to the northern snows or the eastern jungle?" },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Capt. Barnacle",
      "Aye, my old galleon could brave the deep swells once we mend her sun-sail! Follow the turquoise reef strait east to the Oasis of Babana - the desert merchants hold rumors of ancient star-charts and a giant Sandworm!" },
    { OP_SFX, SFX_CONFIRM, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH2_CORAL_MET, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_CH2_BABANA_CLUE[] = {
    { OP_SFX, SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_NPC0, SPR_EMOTE_HEART, 30, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Merchant Zari",
      "Welcome to the Oasis of Babana, traveler! Care for a sweet Golden Babana? Grown right beside our spring - nothing restores a weary adventurer faster!" },
    { OP_SAY, SFX_TEXT_HERO, 0, 0, "Rowan",
      "Golden Babanas! And we saw camels and huge scorpions out in the dunes! Did two travelers from beyond the mist ever trade here?" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Merchant Zari",
      "Indeed they did! Years ago, a mapmaker traded a brass compass for desert provisions - sketching seven great lands across the globe, from Frostmere North all the way to the shining Aetheris Citadel!" },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Seven whole lands wrapped round the globe, Rowan! Once we earn an ocean-going ship - and maybe someday a balloon or airship - we'll chart every single one of them!" },
    { OP_SFX, SFX_CONFIRM, 0, 0, NULL, NULL },
    { OP_SET_FLAG, FLAG_CH2_BABANA_CLUE, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};
