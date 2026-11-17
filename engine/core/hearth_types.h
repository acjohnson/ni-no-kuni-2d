/* ============================================================================
 * Project Hearth — Platform-Agnostic 16-Bit JRPG Engine Core (Pure C99)
 *
 * STRICT PORTABILITY RULES:
 *  - Only <stdint.h>, <stdbool.h>, <stddef.h>
 *  - Zero dynamic allocation (no malloc/free) — fixed static ROM/RAM budgets
 *  - Zero floating-point math in simulation — 16.8 fixed-point integer math
 *  - Zero OS, SDL, browser, or console-specific headers inside engine/core/
 *
 * Compiles natively for Linux/Windows/macOS (SDL2), Sega Genesis (SGDK),
 * and Super Nintendo (PVSnesLib).
 * ============================================================================ */

#ifndef HEARTH_TYPES_H
#define HEARTH_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 16.8 Fixed-Point Arithmetic (1 pixel = 256 subpixels) */
typedef int32_t fp16_t;
#define FP_SHIFT        8
#define FP_ONE          256
#define TO_FP(px)       ((fp16_t)((int32_t)(px) << FP_SHIFT))
#define FROM_FP(fp)     ((int16_t)((fp) >> FP_SHIFT))
#define FP_MUL(a, b)    ((fp16_t)(((int32_t)(a) * (int32_t)(b)) >> FP_SHIFT))

/* 12-Button Retro Gamepad Bitmask (uint16_t) */
#define PAD_UP          (1u << 0)
#define PAD_DOWN        (1u << 1)
#define PAD_LEFT        (1u << 2)
#define PAD_RIGHT       (1u << 3)
#define PAD_A           (1u << 4)  /* Confirm / Interact / Timed Hit / Parry */
#define PAD_B           (1u << 5)  /* Dash / Cancel */
#define PAD_X           (1u << 6)  /* Info / Companion */
#define PAD_Y           (1u << 7)  /* Quick Action */
#define PAD_L           (1u << 8)  /* Prev Familiar (Free Hot-Swap) */
#define PAD_R           (1u << 9)  /* Next Familiar (Free Hot-Swap) */
#define PAD_START       (1u << 10)
#define PAD_SELECT      (1u << 11)

/* Virtual Hardware Budgets */
#define HEARTH_TILE_SIZE    16
#define HEARTH_MAP_W        44
#define HEARTH_MAP_H        28
#define HEARTH_MAP_CELLS    (HEARTH_MAP_W * HEARTH_MAP_H)
#define HEARTH_MAX_OAM      64
#define HEARTH_MAX_PARTICLES 64
#define HEARTH_MAX_FTEXT    16
#define HEARTH_TRAIL_LEN    48
#define HEARTH_MAX_NPCS     3
#define HEARTH_MAX_FIELD_ENEMIES 4
#define HEARTH_MAX_BATTLE_ENEMIES 2
#define HEARTH_MAX_LOCKS    3

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
    MODE_BATTLE_DEFEAT  = 5
} HearthMode;

/* Battle Biomes & Lock Elements */
typedef enum {
    BIOME_MEADOW = 0,
    BIOME_SHRINE = 1
} HearthBiome;

typedef enum {
    LOCK_NONE  = 0,
    LOCK_SWORD = 1,
    LOCK_SUN   = 2,
    LOCK_STAR  = 3
} HearthLockKind;

typedef enum {
    ENCOUNTER_MEADOW_PACK = 0,
    ENCOUNTER_GLADE_DUO   = 1,
    ENCOUNTER_SHRINE_BOSS = 2
} HearthEncounterPreset;

/* Heart-Mending Aura States */
typedef enum {
    AURA_NONE            = 0,
    AURA_RADIANT_COURAGE = 1,
    AURA_HOLLOW_FEAR     = 2,
    AURA_MENDED          = 3
} HearthAura;

/* Sprite IDs (Mapped to ROM Tiles / Atlas Entries in Platform Host) */
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
    SPR_COUNT            = 19
} HearthSpriteId;

/* Abstract Sound Driver Command IDs (SDL2 Synth / Genesis PSG-FM / SNES SPC700) */
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
    SFX_HURT            = 26
} HearthSfxId;

#define HEARTH_MAX_SFX_QUEUE 16

/* OAM Flags */
#define OAM_FLAG_FLIP_X      (1u << 0)
#define OAM_FLAG_FLASH_WHITE (1u << 1)
#define OAM_FLAG_SHADOW      (1u << 2)

/* Flat Hardware-Friendly OAM Sprite Entry */
typedef struct {
    int16_t  x;
    int16_t  y;
    uint8_t  sprite_id;
    uint8_t  frame;
    uint8_t  dir;
    uint8_t  flags;
    int16_t  scale_x_fp; /* 256 = 1.0x */
    int16_t  scale_y_fp;
    uint8_t  aura;
    uint8_t  layer;      /* 0=bg, 1=Y-sorted world, 2=overhead/FX */
    int16_t  sort_y;
    uint16_t rot_deg;
} HearthOamSprite;

typedef struct {
    bool     active;
    uint8_t  kind;       /* 0=dust, 1=grass, 2=ripple, 3=spark, 4=shatter, 5=mote */
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
    int16_t  walk_speed_fp;      /* 384 = 1.50 px/frame */
    int16_t  dash_speed_fp;      /* 614 = 2.40 px/frame */
    int16_t  accel_fp;           /* 128 */
    uint8_t  follow_dist_px;     /* 22 px */
    uint8_t  hitstop_normal_f;   /* 3 frames */
    uint8_t  hitstop_crit_f;     /* 7 frames */
    uint8_t  hitstop_parry_f;    /* 5 frames */
    uint8_t  timed_hit_window_f; /* 8 frames */
    uint8_t  parry_window_f;     /* 9 frames */
    bool     show_hitboxes;
} HearthTuning;

/* Party & Familiar Combatant */
typedef struct {
    uint8_t  slot;       /* 0=Rowan, 1=Bramblekit, 2=Lumina */
    const char *name;
    const char *basic_name;
    const char *skill_name;
    uint8_t  sprite_id;
    int16_t  hp, max_hp;
    int16_t  mp, max_mp;
    int16_t  atk, def;
    uint8_t  basic_kind; /* HearthLockKind */
    uint8_t  skill_kind; /* HearthLockKind or 0 for HEAL */
    uint8_t  skill_cost;
    bool     skill_is_heal;
    bool     skill_is_aoe;
} HearthPartyMember;

/* Battle Enemy with Spell Locks */
typedef struct {
    const char *name;
    const char *special_name;
    uint8_t  sprite_id;
    int8_t   field_idx;  /* -1 if quick battle */
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
    uint16_t progress_fp;   /* 0..256 (256 = impact peak) */
    uint16_t sweet_start_fp;
    uint8_t  result;        /* 0=NONE, 1=PERFECT, 2=GOOD */
} HearthTimedRing;

typedef struct {
    bool     active;
    uint8_t  biome;
    uint8_t  phase;
    uint16_t phase_timer;
    uint8_t  active_slot;
    uint8_t  prev_slot;
    uint16_t swap_arc_fp;   /* 0..256 */
    int16_t  hero_home_x, hero_home_y;
    int16_t  hero_x, hero_y;
    int16_t  hero_scale_x_fp, hero_scale_y_fp;
    uint8_t  hero_flash_frames;
    bool     hero_guarding;
    int16_t  wizard_back_x, wizard_back_y;
    uint8_t  selected_cmd;  /* 0=Strike, 1=Skill, 2=Guard, 3=Tonic */
    uint8_t  selected_enemy;
    uint8_t  active_action; /* 0=Strike, 1=Skill */
    uint8_t  timed_buffer;  /* 6-frame early input buffer */
    uint8_t  timed_result;  /* 0=NONE, 1=PERFECT, 2=GOOD */
    uint8_t  enemy_turn_idx;
    uint8_t  enemy_count;
    bool     is_boss_battle;
    int8_t   field_enemy_idx;
    HearthBattleEnemy enemies[HEARTH_MAX_BATTLE_ENEMIES];
    HearthTimedRing   ring;
    char     banner[40];
    uint8_t  banner_frames;
    uint16_t combos;
    uint16_t parries;
    uint16_t locks_broken;
    uint16_t exp_earned;
} HearthBattleState;

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
    OP_WAIT       = 0,
    OP_PAN_CAM    = 1,
    OP_CAM_FOLLOW = 2,
    OP_FACE_ACTOR = 3,
    OP_EMOTE      = 4,
    OP_SAY        = 5,
    OP_SFX        = 6,
    OP_SHAKE      = 7,
    OP_HEART_ORB  = 8,
    OP_SET_FLAG   = 9,
    OP_END        = 10
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
    uint8_t  id; /* 0=elder, 1=clara, 2=guard */
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

/* Complete PPU Output Packet Consumed by Any Platform Host (SDL2 / Genesis / SNES) */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint32_t frame_count;
    uint8_t  mode;
    int16_t  cam_x;
    int16_t  cam_y;
    int8_t   shake_x;
    int8_t   shake_y;
    uint8_t  flash_level; /* 0..16 */
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
