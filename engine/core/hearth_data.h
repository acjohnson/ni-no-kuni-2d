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
    T_FLOWER_BED    = 27,
    /* Ground-Up Mountain Autumn & Cottage Interior Tileset (Milestone 2) */
    T_MTN_SKY       = 28, /* Distant Snow-Dusted Alpine Peaks & Viaduct Skyline */
    T_CLIFF_WALL    = 29, /* Layered Granite Cliff Face */
    T_CLIFF_LEDGE   = 30, /* Mossy Granite Cliff Rim */
    T_BOULDER       = 31, /* Mossy Mountain Boulder on Autumn Meadow */
    T_AUTUMN_GRASS  = 32, /* Ochre/Sage Mountain Meadow with Fallen Autumn Leaves */
    T_ROCKY_PATH    = 33, /* Crushed Granite Gravel & Flagstone Mountain Trail */
    T_RAPIDS        = 34, /* Winding Mountain Stream with Whitewater Riffles */
    T_RAIL_TRACK    = 35, /* Mountain Railway Iron Ties & Gravel Ballast */
    T_MAPLE_TRUNK   = 36, /* Autumn Maple Tree Trunk */
    T_PINE_TRUNK    = 37, /* Alpine Spruce Tree Base */
    T_WOOD_FLOOR    = 38, /* Warm Honey Cedar Interior Plank Floor */
    T_HEARTH_RUG    = 39, /* Woven Burgundy & Gold Hearthside Rug */
    T_FIREPLACE     = 40, /* Crackling Stone Cottage Fireplace */
    T_BED_QUILT     = 41, /* Rowan's Patchwork Quilt Bed */
    T_ATTIC_CHEST   = 42, /* Grandma's Heirloom Cedar Chest */
    T_HOLLOW_OAK    = 43, /* Monumental Gnarled Hollow Oak Archway */
    T_STONE_STAIRS  = 44, /* Carved Granite Mountain Steps */
    T_INTERIOR_WALL = 45, /* Cedar-Beam Cottage Interior Wall */
    /* Evertale Continent Overworld Tileset (Milestone 3 — fits <= 63 BG1 tiles on SNES!) */
    T_WORLD_OCEAN   = 46, /* Sparkling Cerulean Storybook Ocean */
    T_WORLD_BEACH   = 47, /* Golden Coastal Sand & Cove Rim */
    T_WORLD_PEAK    = 48, /* Snow-Capped Storybook Mountain Range */
    T_WORLD_FOREST  = 49, /* Dense Emerald Overworld Woodland Canopy */
    T_WORLD_HILL    = 50, /* Rolling Sunlit Highland Clover Hill */
    T_WORLD_CASTLE  = 51, /* Gleaming White-Stone Castle Keep & Towers */
    T_WORLD_TOWN    = 52, /* Terracotta-Roofed Village / Market Cluster */
    T_WORLD_RUINS   = 53, /* Moss-Draped Ancient Stone Watchtower Ruins */
    /* Toroidal Globe 7-Continent Biome Tiles (Milestone 4 — fits <= 63 BG1 tiles!) */
    T_WORLD_SHALLOW = 54, /* Turquoise Raftable Coastal Shallows & Straits */
    T_WORLD_SAND    = 55, /* Sun-Baked Golden Desert Dunes */
    T_WORLD_SNOW    = 56, /* Crisp Frostmere Winter Snowfield & Ice */
    T_WORLD_LAVA    = 57, /* Glowing Cinderpeak Caldera Molten Magma */
    T_WORLD_JUNGLE  = 58, /* Lush Canopy Wilds Amazon Floor & Vines */
    T_WORLD_CORAL   = 59, /* Shimmering Suncrest Coral Reef Shoal */
    T_WORLD_TECH    = 60, /* Aetheris Citadel Chrome Plating & Cyan Conduit */
    T_PALM_TREE     = 61, /* Desert Oasis & Tropical Suncrest Palm Tree */
    T_DOCK_PIER     = 62, /* Weathered Sun-Cedar Timber Pier & Dock Planks */
    T_TILE_COUNT    = 63
};

/* BG2 Overhead Canopy / Roof Eave / Archway Tile IDs */
enum {
    O_NONE        = 0,
    O_CANOPY_TL   = 1,
    O_CANOPY_TR   = 2,
    O_CANOPY_BL   = 3,
    O_CANOPY_BR   = 4,
    O_ROOF_EAVE   = 5,
    O_SHRINE_ARCH = 6,
    O_MAPLE_TL    = 7,
    O_MAPLE_TR    = 8,
    O_PINE_TOP    = 9,
    O_OAK_CROWN   = 10
};

/* Collision Codes */
enum {
    COL_WALKABLE   = 0,
    COL_SOLID      = 1,
    COL_TALL_GRASS = 2,
    COL_SHALLOW    = 3, /* Raftable turquoise shallows / ford */
    COL_WATER      = 4, /* Deep cobalt ocean (requires Ship / Airship) */
    COL_LAVA       = 5  /* Molten magma (requires Balloon / Airship) */
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
    FLAG_IRONFANG_DEFEATED  = 9,
    FLAG_CH1_MARA_GREETED   = 10,
    FLAG_CH1_MAPLE_LOAF     = 11,
    FLAG_CH1_BARNABY_THERMOS= 12,
    FLAG_CH1_JASPER_MET     = 13,
    FLAG_CH1_DUSK_TEA       = 14,
    FLAG_CH2_TESSA_CLUE     = 15,
    FLAG_CH2_CORIN_HINTS    = 16,
    FLAG_CH2_MIDO_REUNION   = 17,
    FLAG_CH2_FIRST_FAMILIAR = 18,
    FLAG_CH2_ORIN_QUEST     = 19,
    FLAG_CH2_RAFT_BUILT     = 20,
    FLAG_CH2_CORAL_MET      = 21,
    FLAG_CH2_BABANA_CLUE    = 22
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

#define HEARTH_SHOP_ITEM_COUNT 6

/* 4 x 16-Color Master Hardware Sub-Palettes (Default PAL0..PAL3, 0xRRGGBB) */
extern const uint32_t HEARTH_PALETTES_RGB[4][16];

/* 18 x 16-Color Per-Zone, Continent & Time-of-Day Terrain Sub-Palettes (Loaded into PAL0) */
extern const uint32_t HEARTH_ZONE_PALETTES_RGB[ZPAL_COUNT][16];

/* Zone Display Names, Globe Continent Names & Time-of-Day Labels */
extern const char * const HEARTH_ZONE_NAMES[ZONE_COUNT];
extern const char * const HEARTH_REGION_NAMES[REGION_COUNT];
extern const char * const HEARTH_TOD_NAMES[4];

/* Fish & Shop Static Tables */
extern const HearthFishInfo HEARTH_FISH_TABLE[HEARTH_FISH_SPECIES];
extern const HearthShopItem HEARTH_SHOP_ITEMS[HEARTH_SHOP_ITEM_COUNT];

/* Integer Sine Lookup Table (64 steps, range -127..+127) — zero <math.h> needed! */
extern const int8_t HEARTH_SIN64[64];

/* Toroidal 256x192 Evertale Globe ROM Streaming API (Zero RAM map allocation!) */
extern const HearthGlobeLandmark HEARTH_GLOBE_LANDMARKS[HEARTH_GLOBE_LANDMARK_COUNT];
extern const HearthGlobeFauna    HEARTH_GLOBE_FAUNA[HEARTH_GLOBE_FAUNA_COUNT];
void    hearth_globe_cell(uint16_t tx, uint16_t ty, uint8_t *out_bg1, uint8_t *out_bg2, uint8_t *out_col, uint8_t *out_region);
uint8_t hearth_globe_region_at(uint16_t tx, uint16_t ty);
uint8_t hearth_globe_atlas_cell(uint8_t mx, uint8_t my);

/* Procedural Zone Map Builders (write in-place into 44x28 RAM buffer; returns map_w/map_h) */
void hearth_build_zone_map(uint8_t zone_id, uint8_t *bg1, uint8_t *bg2, uint8_t *col, uint8_t *out_w, uint8_t *out_h);
void hearth_build_sandbox_map(uint8_t *bg1, uint8_t *bg2, uint8_t *col);

/* 6-Act Illustrated Opening Cinema & Mid-Chapter Story CG Scripts */
#define HEARTH_CINEMA_BEAT_COUNT   21u
#define HEARTH_CG_ATTIC_COUNT       4u
#define HEARTH_CG_DREAM_COUNT       4u
#define HEARTH_CG_OAK_COUNT         5u
#define HEARTH_CG_WORLD_ARRIVAL_CNT 4u
extern const HearthCinemaBeat HEARTH_CINEMA_SCRIPT[HEARTH_CINEMA_BEAT_COUNT];
extern const HearthCinemaBeat HEARTH_CG_ATTIC_SCRIPT[HEARTH_CG_ATTIC_COUNT];
extern const HearthCinemaBeat HEARTH_CG_DREAM_SCRIPT[HEARTH_CG_DREAM_COUNT];
extern const HearthCinemaBeat HEARTH_CG_OAK_SCRIPT[HEARTH_CG_OAK_COUNT];
extern const HearthCinemaBeat HEARTH_CG_WORLD_ARRIVAL_SCRIPT[HEARTH_CG_WORLD_ARRIVAL_CNT];

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
extern const HearthCutsceneOp CUTSCENE_CH1_MORNING_MARA[];
extern const HearthCutsceneOp CUTSCENE_CH1_MAPLE_LOAF[];
extern const HearthCutsceneOp CUTSCENE_CH1_BARNABY_THERMOS[];
extern const HearthCutsceneOp CUTSCENE_CH1_DUSK_TEA[];
extern const HearthCutsceneOp CUTSCENE_CH2_SUNHILL_TESSA[];
extern const HearthCutsceneOp CUTSCENE_CH2_SUNHILL_CORIN[];
extern const HearthCutsceneOp CUTSCENE_CH2_SUNHILL_REUNION[];
extern const HearthCutsceneOp CUTSCENE_CH2_BOND_FAMILIAR[];
extern const HearthCutsceneOp CUTSCENE_CH2_ORIN_START[];
extern const HearthCutsceneOp CUTSCENE_CH2_ORIN_RAFT_BUILT[];
extern const HearthCutsceneOp CUTSCENE_CH2_CORAL_BARNACLE[];
extern const HearthCutsceneOp CUTSCENE_CH2_BABANA_CLUE[];

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
