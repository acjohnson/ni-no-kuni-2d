/* ============================================================================
 * hearth_data.h — Static ROM Palettes, 5-Zone Map Builders, Shop/Fish Tables & Cutscenes
 * ============================================================================ */

#ifndef HEARTH_DATA_H
#define HEARTH_DATA_H

#include "hearth_types.h"

/* BG1 Ground & Architecture Tile IDs */
enum {
    T_GRASS         = 0,
    T_GRASS_ALT     = 1,
    T_FLOWERS       = 2,
    T_TALL_GRASS    = 3,
    T_DIRT          = 4,
    T_COBBLE        = 5,
    T_WATER         = 6,
    T_FORD          = 7,
    T_BRIDGE        = 8,
    T_BRIDGE_RAIL_N = 9,
    T_BRIDGE_RAIL_S = 10,
    T_FENCE         = 11,
    T_TREE_TRUNK    = 12,
    T_BUSH          = 13,
    T_WALL_WOOD     = 14,
    T_WALL_WINDOW   = 15,
    T_WALL_DOOR     = 16,
    T_ROOF_BODY     = 17,
    T_ROOF_TOP      = 18,
    T_WINDMILL_WALL = 19,
    T_WINDMILL_DOOR = 20,
    T_WINDMILL_ROOF = 21,
    T_SHRINE_FLOOR  = 22,
    T_SHRINE_RUNE   = 23,
    T_SHRINE_PILLAR = 24,
    T_SIGNPOST      = 25,
    T_LANTERN_POST  = 26,
    T_FLOWER_BED    = 27
};

/* BG2 Overhead Canopy / Roof Eave / Archway Tile IDs */
enum {
    O_NONE        = 0,
    O_CANOPY_TL   = 1,
    O_CANOPY_TR   = 2,
    O_CANOPY_BL   = 3,
    O_CANOPY_BR   = 4,
    O_ROOF_EAVE   = 5,
    O_SHRINE_ARCH = 6
};

/* Collision Codes */
enum {
    COL_WALKABLE   = 0,
    COL_SOLID      = 1,
    COL_TALL_GRASS = 2,
    COL_SHALLOW    = 3,
    COL_WATER      = 4
};

/* Actor Target IDs for Cutscene Bytecode */
enum {
    ACTOR_HERO  = 0,
    ACTOR_PIP   = 1,
    ACTOR_NPC0  = 2,
    ACTOR_NPC1  = 3,
    ACTOR_NPC2  = 4,
    ACTOR_NPC3  = 5,
    ACTOR_ELDER = 2,
    ACTOR_CLARA = 3,
    ACTOR_GUARD = 4
};

/* Flag IDs for Cutscene OP_SET_FLAG */
enum {
    FLAG_HAS_COURAGE        = 1,
    FLAG_GUARD_MENDED       = 2,
    FLAG_ELDER_SEEN         = 3,
    FLAG_FLASHBACK_DONE     = 4,
    FLAG_WAND_RECEIVED      = 5,
    FLAG_SEWER_KEY          = 6,
    FLAG_CAPTAIN_MENDED     = 7,
    FLAG_MUCKMAW_DEFEATED   = 8,
    FLAG_IRONFANG_DEFEATED  = 9
};

/* Fish Species Metadata */
typedef struct {
    const char *name;
    uint16_t    sell_price;
    uint8_t     heal_hp;
    uint8_t     min_lure;
    uint8_t     zone_pref; /* 255 = any, else HearthZoneId */
    uint8_t     agility;   /* 1..4 */
} HearthFishInfo;

/* Shop Buy Catalog Entry */
typedef struct {
    const char *name;
    const char *desc;
    uint16_t    price;
    uint8_t     item_kind; /* 0=Tonic, 1=StarDrop, 2=Snack, 3=RiverLure, 4=GlowLure */
} HearthShopItem;

/* 4 x 16-Color Master Hardware Sub-Palettes (Default PAL0..PAL3, 0xRRGGBB) */
extern const uint32_t HEARTH_PALETTES_RGB[4][16];

/* 6 x 16-Color Per-Zone Terrain Sub-Palettes (Loaded into PAL0 per zone/mood) */
extern const uint32_t HEARTH_ZONE_PALETTES_RGB[ZPAL_COUNT][16];

/* Zone Display Names */
extern const char * const HEARTH_ZONE_NAMES[ZONE_COUNT];

/* Fish & Shop Static Tables */
extern const HearthFishInfo HEARTH_FISH_TABLE[HEARTH_FISH_SPECIES];
extern const HearthShopItem HEARTH_SHOP_ITEMS[5];

/* Integer Sine Lookup Table (64 steps, range -127..+127) — zero <math.h> needed! */
extern const int8_t HEARTH_SIN64[64];

/* Procedural 44x28 Zone Map Builders (write in-place into RAM buffer — zero extra RAM!) */
void hearth_build_zone_map(uint8_t zone_id, uint8_t *bg1, uint8_t *bg2, uint8_t *col);
void hearth_build_sandbox_map(uint8_t *bg1, uint8_t *bg2, uint8_t *col);

/* 7-Act Illustrated Opening Cinema Script (23 player-paced beats) */
#define HEARTH_CINEMA_BEAT_COUNT 23u
extern const HearthCinemaBeat HEARTH_CINEMA_SCRIPT[HEARTH_CINEMA_BEAT_COUNT];

/* Cutscene ROM Bytecode Scripts */
extern const HearthCutsceneOp CUTSCENE_INTRO_DEMO[];
extern const HearthCutsceneOp CUTSCENE_ELDER_TALK[];
extern const HearthCutsceneOp CUTSCENE_TAKE_COURAGE[];
extern const HearthCutsceneOp CUTSCENE_GIVE_COURAGE[];
extern const HearthCutsceneOp CUTSCENE_SHADOW_GRANDMA[];
extern const HearthCutsceneOp CUTSCENE_PIP_REUNION[];
extern const HearthCutsceneOp CUTSCENE_MAYOR_SEWER_KEY[];
extern const HearthCutsceneOp CUTSCENE_MEND_CAPTAIN[];
extern const HearthCutsceneOp CUTSCENE_IRONFANG_VICTORY[];

/* Hardware Palette Packers for Sega Genesis (BGR333) & SNES (BGR555) */
HEARTH_INLINE uint16_t hearth_rgb_to_genesis_bgr333(uint32_t rgb) {
    uint16_t r3 = (uint16_t)(((rgb >> 16) & 0xFFu) >> 5);
    uint16_t g3 = (uint16_t)(((rgb >> 8)  & 0xFFu) >> 5);
    uint16_t b3 = (uint16_t)((rgb         & 0xFFu) >> 5);
    return (uint16_t)((b3 << 9) | (g3 << 5) | (r3 << 1));
}

HEARTH_INLINE uint16_t hearth_rgb_to_snes_bgr555(uint32_t rgb) {
    uint16_t r5 = (uint16_t)(((rgb >> 16) & 0xFFu) >> 3);
    uint16_t g5 = (uint16_t)(((rgb >> 8)  & 0xFFu) >> 3);
    uint16_t b5 = (uint16_t)((rgb         & 0xFFu) >> 3);
    return (uint16_t)((b5 << 10) | (g5 << 5) | r5);
}

#endif /* HEARTH_DATA_H */
