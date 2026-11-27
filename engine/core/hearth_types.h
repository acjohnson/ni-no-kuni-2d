/* ============================================================================
 * Project Hearth — Platform-Agnostic 16-Bit JRPG Engine Core (Pure C99/C89)
 *
 * STRICT PORTABILITY RULES:
 *  - Only <stdint.h>, <stdbool.h>, <stddef.h>
 *  - Zero dynamic allocation (no malloc/free) — fixed static ROM/RAM budgets
 *  - Zero floating-point math in simulation — 16.8 fixed-point integer math
 *  - Zero OS, SDL, browser, or console-specific headers inside engine/core/
 *
 * Compiles natively for Linux/Windows/macOS (SDL2), Sega Genesis (SGDK),
 * Super Nintendo (PVSnesLib / WDC 65816), and WebAssembly.
 * ============================================================================ */

#ifndef HEARTH_TYPES_H
#define HEARTH_TYPES_H

/* Compile-time Designer / Explore Evertale mode (override via -DHEARTH_DESIGNER_MODE=0 for release builds) */
#ifndef HEARTH_DESIGNER_MODE
    #define HEARTH_DESIGNER_MODE 1
#endif

/* Unified Build Version String (injected by Makefile from VERSION file via HEARTH_VERSION_RAW) */
#define HEARTH_STR_IMPL(x) #x
#define HEARTH_STR(x)      HEARTH_STR_IMPL(x)
#ifndef HEARTH_VERSION_STR
    #ifdef HEARTH_VERSION_RAW
        #define HEARTH_VERSION_STR HEARTH_STR(HEARTH_VERSION_RAW)
    #else
        #define HEARTH_VERSION_STR "v0.6.0"
    #endif
#endif

#if defined(HEARTH_TARGET_SNES)
    #include <snes.h>
    typedef s8  int8_t;
    typedef u8  uint8_t;
    typedef s16 int16_t;
    typedef u16 uint16_t;
    typedef s32 int32_t;
    typedef u32 uint32_t;
    #define HEARTH_INLINE static
#elif defined(SGDK_GCC)
    #include <types.h>
    #ifndef NULL
        #define NULL ((void *)0)
    #endif
    #define HEARTH_INLINE static inline
#else
    #include <stdbool.h>
    #include <stddef.h>
    #include <stdint.h>
    #if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
        #define HEARTH_INLINE static inline
    #else
        #define HEARTH_INLINE static
    #endif
#endif

/* Portable zero-dependency byte fill (avoids libc <string.h> / SGDK <memory.h> clashes) */
void hearth_memset(void *dst, uint8_t val, uint16_t len);

/* 16.8 Fixed-Point Arithmetic (1 pixel = 256 subpixels) */
typedef int32_t fp16_t;
#define FP_SHIFT        8
#define FP_ONE          256
#define TO_FP(px)       ((fp16_t)((int32_t)(px) << FP_SHIFT))
#define FROM_FP(fp)     ((int16_t)((fp) >> FP_SHIFT))
#define FP_MUL(a, b)    ((fp16_t)(((int32_t)(a) * (int32_t)(b)) >> FP_SHIFT))
#if defined(HEARTH_TARGET_SNES)
    /* On little-endian 65816, bytes +1..+2 of an fp16_t lvalue equal (int16_t)(fp >> 8) */
    #define FP_PX(fp)   (*((const int16_t *)((const uint8_t *)&(fp) + 1)))
#else
    #define FP_PX(fp)   FROM_FP(fp)
#endif

/* 12-Button Retro Gamepad Bitmask (uint16_t) */
#define PAD_UP          (1u << 0)
#define PAD_DOWN        (1u << 1)
#define PAD_LEFT        (1u << 2)
#define PAD_RIGHT       (1u << 3)
#define PAD_A           (1u << 4)  /* Confirm / Interact / Timed Hit / Parry / Reel */
#define PAD_B           (1u << 5)  /* Dash / Cancel */
#define PAD_X           (1u << 6)  /* Info / Companion */
#define PAD_Y           (1u << 7)  /* Quick Action */
#define PAD_L           (1u << 8)  /* Prev Familiar (Free Hot-Swap) */
#define PAD_R           (1u << 9)  /* Next Familiar (Free Hot-Swap) */
#define PAD_START       (1u << 10)
#define PAD_SELECT      (1u << 11)

/* Virtual Hardware Budgets
 * Local zones (towns, cottages, dungeons) use compact 44x28 RAM arrays (1,232 cells).
 * The 256x192 Evertale Toroidal Globe streams on-the-fly from ROM with ZERO RAM map overhead! */
#define HEARTH_TILE_SIZE            16
#define HEARTH_MAP_W                44
#define HEARTH_MAP_H                28
#define HEARTH_MAP_CELLS            (HEARTH_MAP_W * HEARTH_MAP_H)
#define HEARTH_MAX_MAP_W            HEARTH_MAP_W
#define HEARTH_MAX_MAP_H            HEARTH_MAP_H
#define HEARTH_MAX_MAP_CELLS        HEARTH_MAP_CELLS
#define HEARTH_GLOBE_W              256
#define HEARTH_GLOBE_H              192
#define HEARTH_ATLAS_W              64
#define HEARTH_ATLAS_H              48
#define HEARTH_GLOBE_LANDMARK_COUNT 14
#define HEARTH_GLOBE_FAUNA_COUNT    24
#define HEARTH_MAX_OAM              64
#define HEARTH_MAX_PARTICLES        64
#define HEARTH_MAX_FTEXT            16
#define HEARTH_TRAIL_LEN            48
#define HEARTH_MAX_NPCS             8
#define HEARTH_MAX_FIELD_ENEMIES    4
#define HEARTH_MAX_BATTLE_ENEMIES   2
#define HEARTH_MAX_LOCKS            3
#define HEARTH_MAX_PORTALS          8
#define HEARTH_FISH_SPECIES         6

/* Directions */
typedef enum {
    DIR_DOWN  = 0,
    DIR_UP    = 1,
    DIR_LEFT  = 2,
    DIR_RIGHT = 3
} HearthDir;

/* Virtual PPU Resolution Modes */
typedef enum {
    PPU_GENESIS_320x224 = 0,
    PPU_SNES_256x224    = 1
} HearthPpuMode;

/* Top-Level Engine Game Modes */
typedef enum {
    MODE_TITLE          = 0,
    MODE_EXPLORE        = 1,
    MODE_CUTSCENE       = 2,
    MODE_BATTLE         = 3,
    MODE_BATTLE_VICTORY = 4,
    MODE_BATTLE_DEFEAT  = 5,
    MODE_INTRO          = 6,
    MODE_SHOP           = 7,
    MODE_FISHING        = 8
} HearthMode;

/* 16 Connected World Regions (4 Real-World Mountain Maps + Evertale Globe & Landmarks) */
typedef enum {
    ZONE_ALDERBROOK       = 0,  /* Autumn Mountain Rail Town */
    ZONE_HAMLET           = 1,  /* Evertale: Lakeside Windmill Hamlet */
    ZONE_WHISKERBURGH     = 2,  /* Evertale: Anthropomorphic Mouse Market Town */
    ZONE_SEWERS           = 3,  /* Evertale: Dark Lantern-Lit Sunken Sewers */
    ZONE_BOARHOLD         = 4,  /* Evertale: Anthropomorphic Pig Citadel & Twilight Shrine */
    ZONE_ROWAN_HOME       = 5,  /* Chapter 1: Rowan & Grandma Mara's Cedar Cottage Interior + Attic */
    ZONE_WHISPERING_WOODS = 6,  /* Chapter 1: Rocky Autumn Mountain Forest, Rapids & Lantern Trail */
    ZONE_HOLLOW_GLADE     = 7,  /* Chapter 1: High-Ridge Mountain Sanctuary & Hollow Oak Gate */
    ZONE_EVERTALE_WORLD   = 8,  /* Chapter 2: Seamless 256x192 Full-Toroidal Evertale Globe */
    ZONE_SUNHILL_MARKET   = 9,  /* Chapter 2: Sunlit Hilltop Market Town beneath Castle Aurelia */
    ZONE_CASTLE_GATE      = 10, /* Chapter 2: Enterable Castle Aurelia Drawbridge & Gatehouse */
    ZONE_BRAMBLE_EDGE     = 11, /* Chapter 2: Enterable Bramblewood Crossing Forest Border */
    ZONE_SUNKEN_RUINS     = 12, /* Chapter 2: Enterable Sunken Watchtower Coastal Ruins */
    ZONE_TIMBER_PIER      = 13, /* Chapter 2: Heartland Timber Pier — Shipwright Orin's Raft Yard */
    ZONE_CORAL_COVE       = 14, /* Chapter 2: Suncrest Paradise — Tropical Coral Cove & Pirate Dock */
    ZONE_BABANA_OASIS     = 15, /* Chapter 2: Sands of Babana — Golden Babana Oasis & Desert Outpost */
    /* Milestone 5: Cozy Enterable Town & Village Interiors (8 Hand-Crafted Rooms) */
    ZONE_ALDER_DEPOT      = 16, /* Alderbrook: Barnaby's Steam & Brass Depot */
    ZONE_ALDER_BAKERY     = 17, /* Alderbrook: Maple's Cinnamon & Cider Bakery */
    ZONE_HAMLET_MILL      = 18, /* Windmill Hamlet: The Old Stone Windmill Interior */
    ZONE_ELDER_STUDY      = 19, /* Windmill Hamlet: Elder Wren's Library & Study */
    ZONE_SUNHILL_BAKERY   = 20, /* Sunhill Market: Tessa's Golden Crust Tea House */
    ZONE_SUNHILL_ARCHIVE  = 21, /* Sunhill Market: Corin's Cartography & Globe Hall */
    ZONE_WHISKER_GUILD    = 22, /* Whiskerburgh: Cheddar & Whiskers Mouse Guildhall */
    ZONE_BABANA_TENT      = 23, /* Oasis of Babana: Golden Babana Silk Caravanserai */
    ZONE_ROWAN_UPSTAIRS   = 24, /* Chapter 1: Grandma Mara's Upstairs Bedroom & Keepsake Loft */
    ZONE_COUNT            = 25
} HearthZoneId;

/* 7 Seamless Continents on the 256x192 Evertale Toroidal Globe */
typedef enum {
    REGION_HEARTLAND = 0, /* Emerald Heartland (Starting Realm) */
    REGION_DESERT    = 1, /* Sands of Babana (Hot Dunes, Camels, Scorpions, Sandworm) */
    REGION_PARADISE  = 2, /* Suncrest Paradise (Beaches, Coral Reef, Pirates) */
    REGION_FROSTMERE = 3, /* Frostmere North (Perpetual Festive Winter Wonderland, Penguins) */
    REGION_CANOPY    = 4, /* Canopy Wilds (Amazon Rainforest, Treetop Platforms, Monkeys) */
    REGION_VOLCANO   = 5, /* Cinderpeak Caldera (Death-Mountain Fiery Volcano, Lava Rivers) */
    REGION_AETHERIS  = 6, /* Aetheris Citadel (High-Tech Magitek Endgame Civilization, Robots) */
    REGION_COUNT     = 7
} HearthGlobeRegion;

/* Multi-Tier World Travel Ladder (Strictly Story-Unlocked in New Story) */
typedef enum {
    VEH_FOOT      = 0,
    VEH_RAFT      = 1, /* Chapter 2: Cedar Raft across turquoise shallows & straits */
    VEH_SHIP      = 2, /* Future: Open-ocean Sailing Ship */
    VEH_BALLOON   = 3, /* Future: Hot Air Balloon over cliffs & canyons */
    VEH_AIRSHIP   = 4, /* Future: Sky Airship across all 7 continents */
    VEH_SPACESHIP = 5  /* Endgame: Starbound Vessel */
} HearthVehicle;

#define VEH_MASK_RAFT      (1u << 1)
#define VEH_MASK_SHIP      (1u << 2)
#define VEH_MASK_BALLOON   (1u << 3)
#define VEH_MASK_AIRSHIP   (1u << 4)
#define VEH_MASK_SPACESHIP (1u << 5)

/* Story-Driven Time of Day (Chapter 1: Morning -> Afternoon -> Dusk -> Moonlit Night) */
typedef enum {
    TOD_MORNING   = 0,
    TOD_AFTERNOON = 1,
    TOD_DUSK      = 2,
    TOD_NIGHT     = 3
} HearthTimeOfDay;

/* Shared 1024-step outdoor Time-of-Day color-grade transform (Q8 mul + Q8 8-bit add) */
typedef struct {
    uint16_t mr, mg, mb;       /* 0..256 fixed-point Q8 channel multipliers */
    uint16_t ar8, ag8, ab8;    /* 0..16384 fixed-point Q8 8-bit channel offsets */
} HearthTodGrade;

/* Per-Zone, Continent & Time-of-Day Terrain Sub-Palette IDs (Swapped into PAL0 on all 4 hosts) */
typedef enum {
    ZPAL_HAMLET           = 0,
    ZPAL_ALDERBROOK       = 1,  /* Autumn Mountain Afternoon */
    ZPAL_WHISKERBURGH     = 2,
    ZPAL_SEWERS           = 3,
    ZPAL_BOARHOLD         = 4,
    ZPAL_SHADOW           = 5,
    ZPAL_AUTUMN_MORNING   = 6,  /* Autumn Mountain Dawn Rose-Gold */
    ZPAL_AUTUMN_DUSK      = 7,  /* Autumn Mountain Copper-Amber Sunset */
    ZPAL_AUTUMN_NIGHT     = 8,  /* Autumn Mountain Moonlit Indigo & Silver */
    ZPAL_COTTAGE_INTERIOR = 9,  /* Cozy Honey Cedar & Hearthstone Interior */
    ZPAL_EVERTALE_WORLD   = 10, /* Emerald Heartland Storybook Meadow & Sea */
    ZPAL_SUNHILL_MARKET   = 11, /* Warm Hilltop Limestone, Terracotta Roofs & Gold Sun */
    ZPAL_DESERT           = 12, /* Sands of Babana: Warm Amber Sand, Sandstone & Oasis Jade */
    ZPAL_PARADISE         = 13, /* Suncrest Paradise: Turquoise Shallows, Coral Pink & Palm Green */
    ZPAL_FROST            = 14, /* Frostmere North: Crisp Snow White, Ice Cyan & Festive Evergreen */
    ZPAL_JUNGLE           = 15, /* Canopy Wilds: Deep Emerald Rainforest, Jade River & Cedar Bridges */
    ZPAL_VOLCANO          = 16, /* Cinderpeak Caldera: Basalt Charcoal, Crimson Magma & Ash */
    ZPAL_TECH             = 17, /* Aetheris Citadel: Ivory Alloy, Cobalt Steel & Electric Cyan */
    ZPAL_COUNT            = 18
} HearthZonePalId;

/* Music Track IDs (Consumed by Web Audio, SDL2 Synth, Genesis PSG, SNES SPC700) */
typedef enum {
    BGM_HAMLET         = 0,
    BGM_ALDERBROOK     = 1,
    BGM_WHISKERBURGH   = 2,
    BGM_SEWERS         = 3,
    BGM_BOARHOLD       = 4,
    BGM_SHADOW         = 5,
    BGM_BATTLE         = 6,
    BGM_BOSS           = 7,
    BGM_VICTORY        = 8,
    BGM_CINEMA_WONDER  = 9,
    BGM_CINEMA_LULLABY = 10,
    BGM_DESERT         = 11,
    BGM_PARADISE       = 12,
    BGM_FROST          = 13,
    BGM_JUNGLE         = 14,
    BGM_VOLCANO        = 15,
    BGM_TECH           = 16,
    BGM_COUNT          = 17
} HearthBgmId;

/* Opening Cinema & Mid-Chapter Story CG Plates, Camera Motions & Atmospheric Moods */
typedef enum {
    CG_ALDERBROOK_DUSK      = 0,
    CG_HOLLOW_OAK_CHASE     = 1,
    CG_EVERTALE_VISTA       = 2,
    CG_SHADOW_GRANDMA       = 3,
    CG_BEDROOM_COMFORT      = 4,
    CG_PIP_WINDOW           = 5,
    CG_ATTIC_KEEPSAKE       = 6,
    CG_PIP_DREAM_RETURN     = 7,
    CG_HOLLOW_OAK_AWAKENS   = 8,
    CG_EVERTALE_WORLD_VISTA = 9,
    CG_PLATE_COUNT          = 10
} HearthCinemaPlate;

/* Expressive Character Dialogue Portraits (1:1 Painted Busts on Web/PC) */
typedef enum {
    PORTRAIT_NONE     = 0,
    PORTRAIT_ROWAN    = 1,
    PORTRAIT_MARA     = 2,
    PORTRAIT_PIP      = 3,
    PORTRAIT_BARNABY  = 4,
    PORTRAIT_MAPLE    = 5,
    PORTRAIT_JASPER   = 6,
    PORTRAIT_MIDO     = 7,
    PORTRAIT_TESSA    = 8,
    PORTRAIT_CORIN    = 9,
    PORTRAIT_SENTINEL = 10,
    PORTRAIT_ELDER    = 11,
    PORTRAIT_COUNT    = 12
} HearthPortraitId;

typedef enum {
    CIN_CAM_PAN_RIGHT   = 0,
    CIN_CAM_PAN_LEFT    = 1,
    CIN_CAM_ZOOM_IN     = 2,
    CIN_CAM_SHAKE_FLASH = 3,
    CIN_CAM_STILL_DAWN  = 4
} HearthCinemaCam;

typedef enum {
    CIN_FX_STEAM_DUSK        = 0,
    CIN_FX_FOREST_POLLEN     = 1,
    CIN_FX_EVERTALE_LANTERNS = 2,
    CIN_FX_VIOLET_STORM      = 3,
    CIN_FX_MOON_FIREFLIES    = 4
} HearthCinemaFx;

/* Story Progression Stages */
typedef enum {
    STORY_AGE5_FLASHBACK    = 0, /* Brief playable Age-5 Evertale wonder -> Shadow Grandma trauma */
    STORY_AGE10_ALDERBROOK  = 1, /* Four years later in Alderbrook, no wand yet, find Milo's bell */
    STORY_EVERTALE_AWAKENED = 2, /* Back in Windmill Hamlet, Pip is real & grants Hearthwood Wand */
    STORY_SEWER_KEY         = 3, /* Mayor Brie gave Sewer Key & Lantern Charm in Whiskerburgh */
    STORY_MUCKMAW_DEFEATED  = 4, /* Sewer Mini-Boss beaten -> North Sluice to Boarhold open */
    STORY_IRONFANG_DEFEATED = 5  /* Boarhold Arc Boss beaten -> Chapter 1 Finale */
} HearthStoryStage;

/* Battle Biomes (12 Context-Matched 16-Bit 3/4 Isometric Arenas) & Lock Elements */
typedef enum {
    BIOME_MEADOW   = 0,
    BIOME_SHRINE   = 1,
    BIOME_SEWER    = 2,
    BIOME_BOARHOLD = 3,
    BIOME_DESERT   = 4,
    BIOME_PARADISE = 5,
    BIOME_FOREST   = 6,
    BIOME_TOWN     = 7,
    BIOME_INTERIOR = 8,
    BIOME_SEA      = 9,
    BIOME_FROST    = 10,
    BIOME_VOLCANO  = 11,
    BIOME_COUNT    = 12,
    BIOME_AUTO     = 255
} HearthBiome;

typedef enum {
    LOCK_NONE  = 0,
    LOCK_SWORD = 1,
    LOCK_SUN   = 2,
    LOCK_STAR  = 3
} HearthLockKind;

typedef enum {
    ENCOUNTER_MEADOW_PACK     = 0,
    ENCOUNTER_GLADE_DUO       = 1,
    ENCOUNTER_SHRINE_BOSS     = 2,
    ENCOUNTER_SEWER_PACK      = 3,
    ENCOUNTER_SEWER_BOSS      = 4,
    ENCOUNTER_BOAR_GUARD      = 5,
    ENCOUNTER_IRONFANG_BOSS   = 6,
    ENCOUNTER_DESERT_SCORPION = 7,
    ENCOUNTER_SANDWORM_BOSS   = 8
} HearthEncounterPreset;

/* Heart-Mending Aura States */
typedef enum {
    AURA_NONE            = 0,
    AURA_RADIANT_COURAGE = 1,
    AURA_HOLLOW_FEAR     = 2,
    AURA_MENDED          = 3
} HearthAura;

/* Sprite IDs (Mapped to ROM Tiles / Atlas Entries in Platform Host — 32 total!) */
typedef enum {
    SPR_HERO             = 0,
    SPR_PIP              = 1,
    SPR_FAMILIAR_BRAMBLE = 2,
    SPR_FAMILIAR_LUMINA  = 3,
    SPR_NPC_ELDER        = 4,
    SPR_NPC_CLARA        = 5,
    SPR_NPC_GUARD        = 6,
    SPR_ENEMY_GLOOMCAP   = 7,
    SPR_ENEMY_THORNLING  = 8,
    SPR_ENEMY_DUSK_MOTH  = 9,
    SPR_BOSS_BRIAR       = 10,
    SPR_WINDMILL_BLADES  = 11,
    SPR_WAYSTONE         = 12,
    SPR_CHEST            = 13,
    SPR_EMOTE_BANG       = 14,
    SPR_EMOTE_HEART      = 15,
    SPR_EMOTE_QUESTION   = 16,
    SPR_EMOTE_SPARK      = 17,
    SPR_HEART_ORB        = 18,
    SPR_CHEST_OPEN       = 19,
    SPR_NPC_MOUSE        = 20,
    SPR_NPC_PIG          = 21,
    SPR_NPC_SHADOW_GRAN  = 22,
    SPR_PET_MILO         = 23,
    SPR_ENEMY_RAT        = 24,
    SPR_ENEMY_SLIME      = 25,
    SPR_ENEMY_BOAR       = 26,
    SPR_BOSS_MUCKMAW     = 27,
    SPR_BOSS_IRONFANG    = 28,
    SPR_HOLLOW_OAK       = 29,
    SPR_BOBBER           = 30,
    SPR_NPC_TOWNSFOLK    = 31,
    SPR_NPC_MARA         = 32,
    SPR_NPC_BARNABY      = 33,
    SPR_NPC_MAPLE        = 34,
    SPR_NPC_JASPER       = 35,
    SPR_LOCOMOTIVE       = 36,
    SPR_NPC_TESSA        = 37,
    SPR_NPC_CORIN        = 38,
    SPR_NPC_SENTINEL     = 39,
    SPR_RAFT             = 40,
    SPR_ENEMY_SCORPION   = 41,
    SPR_ENEMY_SANDWORM   = 42,
    SPR_FAUNA_CAMEL      = 43,
    SPR_FAUNA_PENGUIN    = 44,
    SPR_FAUNA_MONKEY     = 45,
    SPR_FAUNA_ROBOT      = 46,
    SPR_NPC_SHIPWRIGHT   = 47,
    SPR_BABANA_STALL     = 48,
    SPR_COUNT            = 49
} HearthSpriteId;

/* Abstract Sound Driver Command IDs (SDL2 Synth / Genesis PSG / SNES SPC700 / Web Audio) */
typedef enum {
    SFX_NONE            = 0,
    SFX_STEP            = 1,
    SFX_GRASS_RUSTLE    = 2,
    SFX_WATER_SPLASH    = 3,
    SFX_DASH            = 4,
    SFX_CURSOR          = 5,
    SFX_CONFIRM         = 6,
    SFX_CANCEL          = 7,
    SFX_TEXT_HERO       = 8,
    SFX_TEXT_PIP        = 9,
    SFX_TEXT_ELDER      = 10,
    SFX_EMOTE           = 11,
    SFX_ENEMY_ALERT     = 12,
    SFX_BATTLE_SWOOP    = 13,
    SFX_SUMMON_ARC      = 14,
    SFX_HIT_NORMAL      = 15,
    SFX_HIT_CRITICAL    = 16,
    SFX_PARRY_CLANG     = 17,
    SFX_LOCK_BREAK      = 18,
    SFX_LOCK_ALL_BREAK  = 19,
    SFX_SPELL_CAST      = 20,
    SFX_HEAL            = 21,
    SFX_HEART_TAKE      = 22,
    SFX_HEART_GIVE      = 23,
    SFX_CHEST_OPEN      = 24,
    SFX_VICTORY_FANFARE = 25,
    SFX_HURT            = 26,
    SFX_COIN            = 27,
    SFX_FISH_BITE       = 28,
    SFX_FISH_REEL       = 29,
    SFX_FISH_CATCH      = 30,
    SFX_PORTAL          = 31
} HearthSfxId;

#define HEARTH_MAX_SFX_QUEUE 16

/* Rowan 16x24 Expressive Hero Animation Frame / Pose IDs (shared across Web, SDL2, Genesis, SNES) */
typedef enum {
    HERO_POSE_IDLE        = 0,  /* Standing neutral */
    HERO_POSE_WALK_A      = 1,  /* Left stride + scarf lift */
    HERO_POSE_WALK_PASS   = 2,  /* Passing stride + gentle bob */
    HERO_POSE_WALK_B      = 3,  /* Right stride + scarf flutter */
    HERO_POSE_BLINK       = 4,  /* Standing idle gentle eye blink */
    HERO_POSE_DASH_A      = 5,  /* Forward-leaning dash stride A + streaming scarf */
    HERO_POSE_DASH_B      = 6,  /* Forward-leaning dash stride B + streaming scarf */
    HERO_POSE_BTL_READY   = 7,  /* Battle ready stance with wand drawn */
    HERO_POSE_BTL_ATTACK  = 8,  /* Battle wand swing lunge + scarf whip */
    HERO_POSE_BTL_CAST    = 9,  /* Battle spell cast wand raised + billowing scarf */
    HERO_POSE_BTL_HURT    = 10, /* Battle hurt recoil */
    HERO_POSE_BTL_VICTORY = 11  /* Battle joyful victory arm raise */
} HearthHeroPose;

/* OAM Flags (bits 3..6 carry compact dir & walk-stride bits for 8-byte SNES OAM) */
#define OAM_FLAG_FLIP_X      (1u << 0)
#define OAM_FLAG_FLASH_WHITE (1u << 1)
#define OAM_FLAG_SHADOW      (1u << 2)
#define OAM_FLAG_DIR_SHIFT   3u
#define OAM_FLAG_DIR_MASK    (3u << 3)
#define OAM_FLAG_STEP_BIT    (1u << 5)
#define OAM_FLAG_WALK_BIT    (1u << 6)
#define OAM_FLAG_LAYER2      (1u << 7)

/* Flat Hardware-Friendly OAM Sprite Entry */
#if defined(HEARTH_TARGET_SNES)
typedef struct {
    int16_t  x;
    int16_t  y;
    int16_t  sort_y;
    uint8_t  sprite_id;
    uint8_t  flags;
} HearthOamSprite;
#else
typedef struct {
    int16_t  x;
    int16_t  y;
    uint8_t  sprite_id;
    uint8_t  frame;
    uint8_t  dir;
    uint8_t  flags;
    int16_t  scale_x_fp;
    int16_t  scale_y_fp;
    uint8_t  aura;
    uint8_t  layer;
    int16_t  sort_y;
    uint16_t rot_deg;
} HearthOamSprite;
#endif

typedef struct {
    bool     active;
    uint8_t  kind;
    fp16_t   fx, fy;
    fp16_t   vx, vy;
    uint8_t  life;
    uint8_t  max_life;
    uint8_t  size;
    uint8_t  pal_idx;
    uint8_t  col_idx;
    bool     screen_space;
} HearthParticle;

typedef struct {
    bool     active;
    int16_t  x, y;
    int16_t  vy_fp;
    char     text[24];
    uint8_t  pal_idx;
    uint8_t  col_idx;
    uint8_t  life;
    bool     world_space;
} HearthFloatText;

/* Live-Tunable Feel & Hardware Registers */
typedef struct {
    uint8_t  ppu_mode;
    int16_t  walk_speed_fp;
    int16_t  dash_speed_fp;
    int16_t  accel_fp;
    uint8_t  follow_dist_px;
    uint8_t  hitstop_normal_f;
    uint8_t  hitstop_crit_f;
    uint8_t  hitstop_parry_f;
    uint8_t  timed_hit_window_f;
    uint8_t  parry_window_f;
    bool     show_hitboxes;
} HearthTuning;

/* Party & Familiar Combatant */
typedef struct {
    uint8_t  slot;
    const char *name;
    const char *basic_name;
    const char *skill_name;
    uint8_t  sprite_id;
    int16_t  hp, max_hp;
    int16_t  mp, max_mp;
    int16_t  atk, def;
    uint8_t  basic_kind;
    uint8_t  skill_kind;
    uint8_t  skill_cost;
    bool     skill_is_heal;
    bool     skill_is_aoe;
} HearthPartyMember;

/* Battle Enemy with Spell Locks */
typedef struct {
    const char *name;
    const char *special_name;
    uint8_t  sprite_id;
    int8_t   field_idx;
    bool     is_boss;
    bool     alive;
    bool     staggered;
    int16_t  hp, max_hp;
    int16_t  atk, def;
    int16_t  home_x, home_y;
    int16_t  x, y;
    int16_t  scale_x_fp, scale_y_fp;
    uint8_t  flash_frames;
    uint8_t  hurt_frames;
    uint8_t  charge_turns;
    uint8_t  max_charge_turns;
    uint8_t  lock_count;
    uint8_t  locks[HEARTH_MAX_LOCKS];
    bool     broken_locks[HEARTH_MAX_LOCKS];
    uint16_t anim_frame;
} HearthBattleEnemy;

typedef enum {
    BPHASE_INTRO           = 0,
    BPHASE_COMMAND         = 1,
    BPHASE_SWAP_ARC        = 2,
    BPHASE_PLAYER_WINDUP   = 3,
    BPHASE_PLAYER_LUNGE    = 4,
    BPHASE_PLAYER_IMPACT   = 5,
    BPHASE_PLAYER_FOLLOWUP = 6,
    BPHASE_PLAYER_RETURN   = 7,
    BPHASE_ENEMY_SELECT    = 8,
    BPHASE_ENEMY_TELEGRAPH = 9,
    BPHASE_ENEMY_LUNGE     = 10,
    BPHASE_ENEMY_IMPACT    = 11,
    BPHASE_ENEMY_RETURN    = 12,
    BPHASE_VICTORY_POSE    = 13,
    BPHASE_DEFEAT_POSE     = 14
} HearthBattlePhase;

typedef struct {
    bool     active;
    bool     is_parry;
    int16_t  x, y;
    uint16_t progress_fp;
    uint16_t sweet_start_fp;
    uint8_t  result;
} HearthTimedRing;

typedef enum {
    BVFX_NONE         = 0,
    BVFX_STAFF_ARC    = 1,
    BVFX_ACORN_SPIN   = 2,
    BVFX_SOLAR_METEOR = 3,
    BVFX_STAR_BOLT    = 4,
    BVFX_PRISM_NOVA   = 5,
    BVFX_GUARD_SHIELD = 6,
    BVFX_ENEMY_CLAW   = 7,
    BVFX_HEAL_BLOOM   = 8
} HearthBattleVfxKind;

typedef struct {
    bool     active;
    uint8_t  biome;
    uint8_t  phase;
    uint16_t phase_timer;
    uint8_t  active_slot;
    uint8_t  prev_slot;
    uint16_t swap_arc_fp;
    int16_t  hero_home_x, hero_home_y;
    int16_t  hero_x, hero_y;
    int16_t  hero_scale_x_fp, hero_scale_y_fp;
    uint8_t  hero_flash_frames;
    bool     hero_guarding;
    int16_t  wizard_back_x, wizard_back_y;
    uint8_t  selected_cmd;
    uint8_t  selected_enemy;
    uint8_t  active_action;
    uint8_t  timed_buffer;
    uint8_t  timed_result;
    uint8_t  enemy_turn_idx;
    uint8_t  enemy_count;
    bool     is_boss_battle;
    uint8_t  encounter_preset;
    int8_t   field_enemy_idx;
    HearthBattleEnemy enemies[HEARTH_MAX_BATTLE_ENEMIES];
    HearthTimedRing   ring;
    uint8_t  vfx_kind;
    uint16_t vfx_timer;
    uint16_t vfx_dur;
    int16_t  vfx_x, vfx_y;
    int16_t  vfx_tx, vfx_ty;
    char     banner[40];
    uint8_t  banner_frames;
    uint16_t combos;
    uint16_t parries;
    uint16_t locks_broken;
    uint16_t exp_earned;
    uint16_t guilders_earned;
} HearthBattleState;

/* 7-Act Illustrated Opening Cinema State (Emerald Dragon / Ni no Kuni Style) */
typedef struct {
    uint8_t     act_num;
    uint8_t     plate_id;
    uint8_t     bgm_id;
    uint8_t     cam_mode;
    uint8_t     fx_mode;
    uint8_t     voice_sfx;
    const char *act_title;
    const char *speaker;
    const char *line1;
    const char *line2;
} HearthCinemaBeat;

typedef struct {
    uint8_t     script_id;     /* 0=Opening Cinema, 1=Attic Keepsake, 2=Pip Dream, 3=Hollow Oak */
    uint8_t     beat;
    uint8_t     beat_count;
    uint8_t     scene;         /* active CG plate_id (0..8) */
    uint8_t     act_num;       /* 1..7 */
    uint8_t     bgm_id;
    uint8_t     cam_mode;
    uint8_t     fx_mode;
    uint8_t     fade;          /* 0..15 hardware crossfade */
    uint8_t     fade_dir;      /* 0=steady, 1=fading_in, 2=out_to_next, 3=out_to_end */
    bool        auto_play;     /* false = player-paced reading; true = gentle auto-advance */
    bool        launch_story;  /* true = New Story -> Rowan Cottage Dawn; false = Replay -> Title */
    uint16_t    timer;
    uint16_t    plate_timer;   /* continuous frames on current CG plate for smooth pan */
    uint16_t    auto_hold;
    uint16_t    chars;
    const char *title;
    const char *speaker;
    const char *line1;
    const char *line2;
} HearthIntroState;

/* Interactive Merchant Shop State (Mouse Market & Boarhold Provisions) */
typedef struct {
    bool        active;
    uint8_t     shop_id;    /* 0=Barnaby's Mouse Market, 1=Porkins' Boarhold Provisions */
    uint8_t     tab;        /* 0=BUY, 1=SELL FISH */
    uint8_t     cursor;     /* 0..5 */
    uint8_t     msg_timer;
    const char *title;
    char        msg[40];
} HearthShopState;

/* Zelda / Sea-of-Stars Timed-Reel Fishing Minigame State */
typedef enum {
    FISH_PHASE_CAST       = 0,
    FISH_PHASE_WAIT_BITE  = 1,
    FISH_PHASE_BITE_ALERT = 2,
    FISH_PHASE_REELING    = 3,
    FISH_PHASE_CAUGHT     = 4,
    FISH_PHASE_ESCAPED    = 5
} HearthFishPhase;

typedef struct {
    bool     active;
    uint8_t  phase;
    uint16_t timer;
    uint16_t bite_delay;
    int16_t  bobber_x;
    int16_t  bobber_y;
    int16_t  fish_pos;    /* 0..100 */
    int8_t   fish_vel;
    int16_t  bar_pos;     /* 0..100 */
    uint8_t  bar_half_w;  /* 14..22 */
    uint8_t  progress;    /* 0..100 */
    uint8_t  tension;     /* 0..100 */
    uint8_t  species;     /* 0..5 */
    uint8_t  weight_oz;
} HearthFishingState;

/* Dialogue & Choices */
#define HEARTH_MAX_CHOICES 3

typedef struct {
    const char *label;
    uint8_t     action_id;
} HearthChoice;

typedef struct {
    bool         active;
    const char  *speaker;
    const char  *full_text;
    const char  *next_text;
    uint16_t     full_len;
    uint16_t     visible_chars;
    bool         finished;
    uint8_t      pause_frames;
    uint8_t      voice_sfx;
    uint8_t      portrait_id;
    uint8_t      choice_count;
    uint8_t      selected_choice;
    HearthChoice choices[HEARTH_MAX_CHOICES];
} HearthDialogue;

/* Cutscene Bytecode Instruction */
typedef enum {
    OP_WAIT          = 0,
    OP_PAN_CAM       = 1,
    OP_CAM_FOLLOW    = 2,
    OP_FACE_ACTOR    = 3,
    OP_EMOTE         = 4,
    OP_SAY           = 5,
    OP_SFX           = 6,
    OP_SHAKE         = 7,
    OP_HEART_ORB     = 8,
    OP_SET_FLAG      = 9,
    OP_END           = 10,
    OP_SET_PAL       = 11, /* a0 = HearthZonePalId, a1 = HearthBgmId */
    OP_WARP_ZONE     = 12, /* a0 = zone_id, a1 = tile_x, a2 = tile_y */
    OP_SHOW_ACTOR    = 13  /* a0 = npc slot, a1 = tile_x, a2 = tile_y (a1<0 hides) */
} HearthCutsceneOpcode;

typedef struct {
    uint8_t     op;
    int16_t     a0;
    int16_t     a1;
    uint8_t     a2;
    const char *speaker;
    const char *text;
} HearthCutsceneOp;

/* NPC & Roaming Field Enemy */
typedef struct {
    uint8_t  id;
    bool     active;
    const char *name;
    uint8_t  sprite_id;
    fp16_t   fx, fy;
    uint8_t  dir;
    uint8_t  aura;
    uint8_t  emote;
    uint8_t  emote_frames;
} HearthNpc;

typedef struct {
    const char *name;
    uint8_t  sprite_id;
    uint8_t  preset;
    uint8_t  biome;
    bool     is_boss;
    bool     alive;
    fp16_t   home_fx, home_fy;
    fp16_t   fx, fy;
    fp16_t   vx, vy;
    uint8_t  dir;
    bool     alerted;
    uint8_t  alert_frames;
    uint8_t  patrol_timer;
    uint16_t anim_frame;
} HearthFieldEnemy;

typedef struct {
    bool     active;
    int16_t  start_x, start_y;
    int16_t  end_x, end_y;
    uint8_t  frame;
    uint8_t  duration;
} HearthHeartOrb;

/* Zone Interactive Props & Portals (Populated Dynamically per Active Zone) */
typedef struct {
    bool    active;
    uint8_t tx, ty;
    uint8_t dst_zone;
    uint8_t dst_tx, dst_ty;
    uint8_t req_stage;
    const char *label;
} HearthPortal;

typedef struct {
    bool    active;
    uint8_t tx, ty;
    int8_t  cast_dx, cast_dy;
} HearthFishSpot;

typedef struct {
    bool    active;
    uint8_t tx, ty;
    uint8_t chest_bit;
    uint8_t reward_kind; /* 0=Tonics+G, 1=StarDrop+G, 2=Snack+G, 3=BigGuilders */
} HearthChestProp;

/* ROM-Resident Globe Landmark & Biome Fauna Descriptors (Zero RAM overhead) */
typedef struct {
    uint16_t    tx;
    uint16_t    ty;
    uint8_t     region_id;
    uint8_t     dst_zone;    /* 255 = preview landmark (dialog reaction on globe) */
    uint8_t     dst_tx;
    uint8_t     dst_ty;
    uint8_t     kind;        /* 0=Oak, 1=Town, 2=Castle, 3=Forest, 4=Ruins, 5=Pier, 6=Cove, 7=Oasis, 8=Noel, 9=Canopy, 10=Volcano, 11=Citadel */
    uint8_t     enterable;
    const char *name;
} HearthGlobeLandmark;

typedef struct {
    uint16_t tx;
    uint16_t ty;
    uint8_t  sprite_id;
    uint8_t  region_id;
    uint8_t  phase;
} HearthGlobeFauna;

/* Complete PPU Output Packet Consumed by Any Platform Host (SDL2 / Genesis / SNES / Web) */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t frame_count;
    uint8_t  mode;
    uint8_t  zone_id;
    uint8_t  map_w;
    uint8_t  map_h;
    uint16_t globe_w;
    uint16_t globe_h;
    uint8_t  world_region_id;
    uint8_t  active_vehicle;
    uint8_t  vehicle_unlocked_mask;
    bool     atlas_open;
    bool     designer_mode_enabled;
    uint8_t  terrain_pal_id;
    uint8_t  bgm_id;
    uint8_t  time_of_day;
    uint16_t tod_phase;       /* 0..1023 ultra-smooth real-time day/night phase (256 steps/quarter!) */
    bool     zone_is_dark;
    bool     has_mountain_vista;
    bool     has_outdoor_tod;
    bool     is_world_map;
    uint8_t  zone_banner_frames;
    const char *zone_name;
    const char *objective_text;
    int16_t  cam_x;
    int16_t  cam_y;
    int8_t   shake_x;
    int8_t   shake_y;
    uint8_t  fade_level;
    uint8_t  flash_level;
    bool     hitstop_active;
    uint8_t  oam_count;
    uint8_t  oam_limit;
    HearthOamSprite oam[HEARTH_MAX_OAM];
    bool     interact_active;
    int16_t  interact_x;
    int16_t  interact_y;
    const char *interact_label;
    uint8_t  sfx_count;
    uint8_t  sfx_queue[HEARTH_MAX_SFX_QUEUE];
} HearthPpuPacket;

#endif /* HEARTH_TYPES_H */
