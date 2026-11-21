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

/* Virtual Hardware Budgets */
#define HEARTH_TILE_SIZE          16
#define HEARTH_MAP_W              44
#define HEARTH_MAP_H              28
#define HEARTH_MAP_CELLS          (HEARTH_MAP_W * HEARTH_MAP_H)
#define HEARTH_MAX_OAM            64
#define HEARTH_MAX_PARTICLES      64
#define HEARTH_MAX_FTEXT          16
#define HEARTH_TRAIL_LEN          48
#define HEARTH_MAX_NPCS           4
#define HEARTH_MAX_FIELD_ENEMIES  4
#define HEARTH_MAX_BATTLE_ENEMIES 2
#define HEARTH_MAX_LOCKS          3
#define HEARTH_MAX_PORTALS        3
#define HEARTH_FISH_SPECIES       6

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

/* 5 Connected World Regions (Alderbrook + 4 Regions of Evertale) */
typedef enum {
    ZONE_ALDERBROOK   = 0,  /* Rowan's ordinary hometown (Age 10 real world) */
    ZONE_HAMLET       = 1,  /* Evertale: Windmill Hamlet, Creek Pier & Age-5 Glade */
    ZONE_WHISKERBURGH = 2,  /* Evertale: Anthropomorphic Mouse Market Town */
    ZONE_SEWERS       = 3,  /* Evertale: Dark Lantern-Lit Sunken Sewers */
    ZONE_BOARHOLD     = 4,  /* Evertale: Anthropomorphic Pig Citadel & Twilight Shrine */
    ZONE_COUNT        = 5
} HearthZoneId;

/* Per-Zone Terrain Sub-Palette IDs (Swapped Dynamically into PAL0 on all 4 hosts) */
typedef enum {
    ZPAL_HAMLET       = 0,
    ZPAL_ALDERBROOK   = 1,
    ZPAL_WHISKERBURGH = 2,
    ZPAL_SEWERS       = 3,
    ZPAL_BOARHOLD     = 4,
    ZPAL_SHADOW       = 5,
    ZPAL_COUNT        = 6
} HearthZonePalId;

/* Music Track IDs (Consumed by Web Audio, SDL2 Synth, Genesis PSG, SNES SPC700) */
typedef enum {
    BGM_HAMLET       = 0,
    BGM_ALDERBROOK   = 1,
    BGM_WHISKERBURGH = 2,
    BGM_SEWERS       = 3,
    BGM_BOARHOLD     = 4,
    BGM_SHADOW       = 5,
    BGM_BATTLE       = 6,
    BGM_BOSS         = 7,
    BGM_VICTORY      = 8
} HearthBgmId;

/* Story Progression Stages */
typedef enum {
    STORY_AGE5_FLASHBACK    = 0, /* Brief playable Age-5 Evertale wonder -> Shadow Grandma trauma */
    STORY_AGE10_ALDERBROOK  = 1, /* Four years later in Alderbrook, no wand yet, find Milo's bell */
    STORY_EVERTALE_AWAKENED = 2, /* Back in Windmill Hamlet, Pip is real & grants Hearthwood Wand */
    STORY_SEWER_KEY         = 3, /* Mayor Brie gave Sewer Key & Lantern Charm in Whiskerburgh */
    STORY_MUCKMAW_DEFEATED  = 4, /* Sewer Mini-Boss beaten -> North Sluice to Boarhold open */
    STORY_IRONFANG_DEFEATED = 5  /* Boarhold Arc Boss beaten -> Chapter 1 Finale */
} HearthStoryStage;

/* Battle Biomes & Lock Elements */
typedef enum {
    BIOME_MEADOW   = 0,
    BIOME_SHRINE   = 1,
    BIOME_SEWER    = 2,
    BIOME_BOARHOLD = 3
} HearthBiome;

typedef enum {
    LOCK_NONE  = 0,
    LOCK_SWORD = 1,
    LOCK_SUN   = 2,
    LOCK_STAR  = 3
} HearthLockKind;

typedef enum {
    ENCOUNTER_MEADOW_PACK   = 0,
    ENCOUNTER_GLADE_DUO     = 1,
    ENCOUNTER_SHRINE_BOSS   = 2,
    ENCOUNTER_SEWER_PACK    = 3,
    ENCOUNTER_SEWER_BOSS    = 4,
    ENCOUNTER_BOAR_GUARD    = 5,
    ENCOUNTER_IRONFANG_BOSS = 6
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
    SPR_COUNT            = 32
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

/* Retro Opening Cutscene State (Emerald Dragon / PC Engine CD Storybook Intro) */
typedef struct {
    uint8_t     scene;
    uint16_t    timer;
    uint8_t     fade;
    uint16_t    chars;
    const char *title;
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
    uint16_t     full_len;
    uint16_t     visible_chars;
    bool         finished;
    uint8_t      pause_frames;
    uint8_t      voice_sfx;
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

/* Complete PPU Output Packet Consumed by Any Platform Host (SDL2 / Genesis / SNES / Web) */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t frame_count;
    uint8_t  mode;
    uint8_t  zone_id;
    uint8_t  terrain_pal_id;
    uint8_t  bgm_id;
    bool     zone_is_dark;
    uint8_t  zone_banner_frames;
    const char *zone_name;
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
