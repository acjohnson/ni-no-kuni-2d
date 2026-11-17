/* ============================================================================
 * hearth_data.c — Static 16-Color Palettes, Map Data & Bytecode Cutscenes
 * Pure C99 — compiles cleanly on Linux, Windows, macOS, Genesis, and SNES.
 * ============================================================================ */

#include "hearth_data.h"

void hearth_memset(void *dst, uint8_t val, uint16_t len) {
    uint8_t *p = (uint8_t *)dst;
    uint16_t i;
    for (i = 0; i < len; i++) {
        p[i] = val;
    }
}

/* 4 x 16-Color Master Indexed Sub-Palettes (Index 0 = Transparent) */
const uint32_t HEARTH_PALETTES_RGB[4][16] = {
    /* Palette 0: Pastoral Terrain & Architecture */
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
    /* Palette 2: Villagers & Props */
    {
        0x000000u, 0x1D1B22u, 0xF3D1B0u, 0xCE9874u,
        0xE6E2D8u, 0xA5A29Eu, 0xC4852Du, 0xE8B852u,
        0x385888u, 0x5C84B8u, 0x8C3B58u, 0x68432Eu,
        0xA87248u, 0x52386Eu, 0x9E76CCu, 0xF4E7C8u
    },
    /* Palette 3: Enemies, Boss & Combat FX */
    {
        0x000000u, 0x191421u, 0x462A5Eu, 0x7E4AA8u,
        0xC48AF0u, 0xEAE0C8u, 0x335A36u, 0x578E4Au,
        0x8BC45Eu, 0x8E2D36u, 0xE04E4Eu, 0xF5B841u,
        0xFFF6CCu, 0x3E4656u, 0x748094u, 0x5AD2F4u
    }
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

void hearth_build_sandbox_map(uint8_t *bg1, uint8_t *bg2, uint8_t *col) {
    int x, y, hash, curve, rx;
    uint16_t i;
    static const int8_t trees[][2] = {
        {3, 5}, {3, 10}, {3, 20}, {8, 24},
        {16, 5}, {22, 5}, {28, 4}, {34, 5},
        {39, 5}, {22, 18}, {35, 24}, {40, 23}
    };

    hearth_memset(bg2, O_NONE, (uint16_t)HEARTH_MAP_CELLS);

    /* 1. Varied sunlit meadow grass */
    for (y = 0; y < HEARTH_MAP_H; y++) {
        for (x = 0; x < HEARTH_MAP_W; x++) {
            hash = (x * 37 + y * 73) & 15;
            if (hash == 0 || hash == 5) {
                set_bg1(bg1, col, x, y, T_GRASS_ALT, COL_WALKABLE);
            } else if (hash == 11 && x > 3 && x < HEARTH_MAP_W - 4 && y > 3 && y < HEARTH_MAP_H - 4) {
                set_bg1(bg1, col, x, y, T_FLOWERS, COL_WALKABLE);
            } else {
                set_bg1(bg1, col, x, y, T_GRASS, COL_WALKABLE);
            }
        }
    }

    /* 2. Perimeter forest border */
    for (x = 0; x < HEARTH_MAP_W; x++) {
        set_bg1(bg1, col, x, 0, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, x, 1, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 1, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, x, HEARTH_MAP_H - 2, T_BUSH, COL_SOLID);
    }
    for (y = 0; y < HEARTH_MAP_H; y++) {
        set_bg1(bg1, col, 0, y, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, 1, y, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 1, y, T_BUSH, COL_SOLID);
        set_bg1(bg1, col, HEARTH_MAP_W - 2, y, T_BUSH, COL_SOLID);
    }

    /* 3. Winding Creek */
    for (y = 2; y < HEARTH_MAP_H - 2; y++) {
        curve = (y < 10) ? -1 : ((y > 19) ? 1 : 0);
        rx = 19 + curve;
        set_bg1(bg1, col, rx - 1, y, T_WATER, COL_WATER);
        set_bg1(bg1, col, rx,     y, T_WATER, COL_WATER);
        set_bg1(bg1, col, rx + 1, y, T_WATER, COL_WATER);
    }

    /* 4. Windmill Hamlet Plaza & Roads */
    fill_rect(bg1, col, 6, 10, 10, 8, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 7, 17, 5, 4, T_COBBLE, COL_WALKABLE);
    fill_rect(bg1, col, 15, 12, 3, 3, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 21, 12, 14, 3, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 25, 8, 2, 5, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 13, 18, 3, 5, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 15, 21, 13, 2, T_DIRT, COL_WALKABLE);
    fill_rect(bg1, col, 27, 14, 2, 8, T_DIRT, COL_WALKABLE);

    /* 5. Timber Bridge */
    for (x = 18; x <= 20; x++) {
        set_bg1(bg1, col, x, 11, T_BRIDGE_RAIL_N, COL_SOLID);
        set_bg1(bg1, col, x, 12, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 13, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 14, T_BRIDGE, COL_WALKABLE);
        set_bg1(bg1, col, x, 15, T_BRIDGE_RAIL_S, COL_SOLID);
    }

    /* 6. Shallow Pebble Ford */
    for (x = 19; x <= 21; x++) {
        set_bg1(bg1, col, x, 21, T_FORD, COL_SHALLOW);
        set_bg1(bg1, col, x, 22, T_FORD, COL_SHALLOW);
    }

    /* 7. Stone Windmill */
    fill_rect(bg1, col, 5, 4, 3, 2, T_WINDMILL_ROOF, COL_SOLID);
    fill_rect(bg1, col, 5, 6, 3, 3, T_WINDMILL_WALL, COL_SOLID);
    set_bg1(bg1, col, 6, 8, T_WINDMILL_DOOR, COL_SOLID);

    /* 8. Cozy Timber Cottage */
    fill_rect(bg1, col, 11, 4, 5, 1, T_ROOF_TOP, COL_SOLID);
    fill_rect(bg1, col, 11, 5, 5, 1, T_ROOF_BODY, COL_SOLID);
    set_bg1(bg1, col, 11, 6, T_WALL_WOOD, COL_SOLID);
    set_bg1(bg1, col, 12, 6, T_WALL_WINDOW, COL_SOLID);
    set_bg1(bg1, col, 13, 6, T_WALL_DOOR, COL_SOLID);
    set_bg1(bg1, col, 14, 6, T_WALL_WINDOW, COL_SOLID);
    set_bg1(bg1, col, 15, 6, T_WALL_WOOD, COL_SOLID);
    for (x = 11; x <= 15; x++) {
        set_bg2(bg2, x, 5, O_ROOF_EAVE);
    }

    /* Flower beds, lamps, signpost & fences */
    set_bg1(bg1, col, 11, 7, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 15, 7, T_FLOWER_BED, COL_SOLID);
    set_bg1(bg1, col, 10, 9, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 16, 11, T_LANTERN_POST, COL_SOLID);
    set_bg1(bg1, col, 16, 15, T_SIGNPOST, COL_SOLID);
    for (x = 3; x <= 6; x++) set_bg1(bg1, col, x, 22, T_FENCE, COL_SOLID);
    for (x = 11; x <= 13; x++) set_bg1(bg1, col, x, 23, T_FENCE, COL_SOLID);

    /* 9. Tall Rustling Wildgrass */
    fill_rect(bg1, col, 23, 6, 4, 3, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 29, 5, 5, 4, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 23, 16, 4, 4, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 29, 17, 5, 5, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 35, 20, 4, 3, T_TALL_GRASS, COL_TALL_GRASS);
    fill_rect(bg1, col, 3, 14, 3, 4, T_TALL_GRASS, COL_TALL_GRASS);

    /* 10. Ancient Stone Shrine */
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

    /* 11. Scenic Trees with BG2 Overhead Canopies */
    for (i = 0; i < sizeof(trees) / sizeof(trees[0]); i++) {
        place_tree(bg1, bg2, col, trees[i][0], trees[i][1]);
    }
}

/* ============================================================================
 * BYTECODE CUTSCENE ROM TABLES
 * ============================================================================ */
const HearthCutsceneOp CUTSCENE_INTRO_DEMO[] = {
    { OP_PAN_CAM,    7,  8, 42, NULL, NULL },
    { OP_WAIT,      16,  0,  0, NULL, NULL },
    { OP_SFX,  SFX_EMOTE, 0, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_PIP, SPR_EMOTE_SPARK, 32, NULL, NULL },
    { OP_CAM_FOLLOW, 28, 0,  0, NULL, NULL },
    { OP_FACE_ACTOR, ACTOR_HERO, DIR_LEFT, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Oi! Native C99 core is humming at 60Hz! Smooth footsteps, rustling grass, and zero browser bloat." },
    { OP_PAN_CAM,   21, 13, 36, NULL, NULL },
    { OP_WAIT,      16,  0,  0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Borrow Courage from Clara to mend Guard Bram at the bridge, or dash east to test Familiar timed hits!" },
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
      "You move lightly today, Rowan! Try holding Dash (Shift/B) through the tall grass or southern ford." },
    { OP_PAN_CAM,   37, 13, 42, NULL, NULL },
    { OP_WAIT,      18,  0,  0, NULL, NULL },
    { OP_SAY, SFX_TEXT_ELDER, 0, 0, "Elder Wren",
      "East of the creek stands the Stone Shrine. Hot-swap Familiars with Q/E (L/R) to shatter the Briar Warden's Spell Locks!" },
    { OP_CAM_FOLLOW, 32, 0,  0, NULL, NULL },
    { OP_SET_FLAG, FLAG_ELDER_SEEN, 0, 0, NULL, NULL },
    { OP_END, 0, 0, 0, NULL, NULL }
};

const HearthCutsceneOp CUTSCENE_TAKE_COURAGE[] = {
    { OP_FACE_ACTOR, ACTOR_HERO, DIR_UP, 0, NULL, NULL },
    { OP_FACE_ACTOR, ACTOR_CLARA, DIR_DOWN, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Clara",
      "Plenty of Courage to spare! Take a bright spark into your brass locket for poor Guard Bram!" },
    { OP_SFX, SFX_HEART_TAKE, 0, 0, NULL, NULL },
    { OP_HEART_ORB, ACTOR_CLARA, ACTOR_HERO, 40, NULL, NULL },
    { OP_SHAKE, 2, 10, 0, NULL, NULL },
    { OP_EMOTE, ACTOR_HERO, SPR_EMOTE_HEART, 30, NULL, NULL },
    { OP_SET_FLAG, FLAG_HAS_COURAGE, 0, 0, NULL, NULL },
    { OP_SAY, SFX_TEXT_PIP, 0, 0, "Pip",
      "Spark of Courage bottled! Carry it east across the timber bridge to Guard Bram!" },
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
      "Whoa... the chill vanished! My knees feel sturdy as oak again. Take this Star Tonic for the Shrine ahead!" },
    { OP_END, 0, 0, 0, NULL, NULL }
};
