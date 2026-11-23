/* ============================================================================
 * hearth_core.h — Project Hearth Platform-Agnostic 16-Bit Simulation Core
 *
 * Pure C99/C89. Zero dynamic allocation. Zero floating-point simulation math.
 * Pass a pointer to a static/stack HearthState (fits comfortably inside
 * Sega Genesis 64KB RAM and SNES 128KB WRAM!).
 * ============================================================================ */

#ifndef HEARTH_CORE_H
#define HEARTH_CORE_H

#include "hearth_data.h"
#include "hearth_prng.h"
#include "hearth_types.h"

typedef struct {
    /* World ROM/RAM Map Buffers (rebuilt in-place per active zone; up to 88x56!) */
    uint8_t map_bg1[HEARTH_MAX_MAP_CELLS];
    uint8_t map_bg2[HEARTH_MAX_MAP_CELLS];
    uint8_t map_col[HEARTH_MAX_MAP_CELLS];

    /* Tuning & Deterministic State */
    HearthTuning tuning;
    HearthPrng   prng;
    uint8_t      mode;
    uint32_t     frame_count;
    uint16_t     prev_pad;

    /* Multi-Zone World & Story Progression */
    uint8_t      zone_id;
    uint8_t      map_w;
    uint8_t      map_h;
    uint8_t      world_region_id; /* Active continent on 256x192 globe (0..6) */
    uint8_t      globe_enemy_region; /* Region currently owning active field_enemies[] */
    uint8_t      active_vehicle;  /* VEH_FOOT..VEH_SPACESHIP */
    uint8_t      vehicle_unlocked_mask; /* Bitmask of story-unlocked travel tiers */
    bool         atlas_open;      /* Toggleable World Atlas minimap overlay */
    uint8_t      atlas_cursor;    /* Selected continent index (0..6) for Designer Atlas warp */
    bool         deep_sea_hint_shown;
    bool         designer_sandbox;/* True when launched via Explore Evertale (Designer) */
    uint8_t      planks_found_mask; /* bits 0..2: 3 Sun-Cedar Driftwood Planks at Timber Pier */
    uint8_t      terrain_pal_id;
    uint8_t      bgm_id;
    uint8_t      time_of_day;     /* TOD_MORNING..TOD_NIGHT */
    bool         zone_is_dark;
    bool         has_mountain_vista;
    bool         is_world_map;
    uint8_t      zone_banner_frames;
    uint8_t      story_stage;
    uint8_t      ch1_step;        /* 0..8 Chapter 1 Alderbrook & Woods progression */
    uint8_t      ch2_step;        /* 0..7 Chapter 2 Evertale Globe, Sunhill & Raft Expedition */
    uint8_t      ch1_errands_mask;/* bit0=Maple Loaf, bit1=Barnaby Thermos, bit2=Jasper Met */
    uint8_t      woods_shrines_mask; /* bits 0..2: 3 lantern markers in Whispering Woods */
    bool         has_milo_bell;   /* Mido's heirloom brass bell & pressed oak leaf */
    uint8_t      map_revision;
    uint8_t      warp_fade;       /* 0..15 fade out/in on zone warp */
    bool         warp_pending;
    uint8_t      warp_dst_zone;
    uint16_t     warp_dst_tx;
    uint16_t     warp_dst_ty;
    bool         flashback_triggered;

    /* Active Zone Interactive Props & Portals */
    HearthPortal    portals[HEARTH_MAX_PORTALS];
    HearthFishSpot  fish_spot;
    HearthChestProp chest;
    uint8_t         waystone_tx, waystone_ty;
    bool            has_waystone;
    uint8_t         sign_tx, sign_ty;
    bool            has_sign;
    bool            has_windmill;
    bool            has_locomotive;
    uint8_t         loco_tx, loco_ty;

    /* Hero Fixed-Point 16.8 State */
    fp16_t   hero_fx, hero_fy;
    fp16_t   hero_vx, hero_vy;
    uint8_t  hero_dir;
    uint16_t hero_anim_tick;
    uint8_t  hero_step_timer;
    bool     hero_dashing;
    uint8_t  hero_emote;
    uint8_t  hero_emote_frames;
    uint8_t  hero_safe_frames;

    /* Pip Breadcrumb Circular Trail */
    fp16_t   trail_x[HEARTH_TRAIL_LEN];
    fp16_t   trail_y[HEARTH_TRAIL_LEN];
    uint8_t  trail_head;
    fp16_t   pip_fx, pip_fy;
    uint8_t  pip_dir;
    uint8_t  pip_emote;
    uint8_t  pip_emote_frames;
    bool     pip_visible;
    int16_t  pip_hop_z_fp;       /* 16.8 vertical bounce height above floor (0 = grounded) */
    int16_t  pip_hop_vz_fp;      /* 16.8 vertical bounce velocity */
    uint8_t  pip_squash_frames;  /* crouched squash frames on takeoff/landing */
    uint8_t  pip_idle_hop_cd;    /* cheerful Mr. Drippy idle hop countdown */

    /* Camera & Screen Juice Registers */
    fp16_t   cam_fx, cam_fy;
    bool     cam_override;
    fp16_t   cam_start_fx, cam_start_fy;
    fp16_t   cam_target_fx, cam_target_fy;
    uint8_t  cam_pan_frame, cam_pan_dur;
    uint8_t  shake_intensity, shake_frames;
    uint8_t  hitstop_frames;
    uint8_t  flash_level;

    /* Party, Economy, Inventory & Heart Locket Flags */
    HearthPartyMember party[3];
    uint8_t  active_slot;
    uint16_t guilders;
    uint8_t  potions;
    uint8_t  star_drops;
    uint8_t  snacks;
    uint8_t  babanas;
    uint8_t  lure_tier;
    uint8_t  fish_bag[HEARTH_FISH_SPECIES];
    uint16_t fish_total_caught;
    uint16_t chests_opened_mask;
    bool     flag_has_courage;
    bool     flag_guard_mended;
    bool     flag_chest_opened;
    bool     flag_elder_seen;
    bool     flag_boss_defeated;
    bool     flag_has_wand;
    bool     flag_has_familiar;      /* False until Rowan bonds first Familiar -> awakens overworld enemies */
    bool     flag_has_heart_vessel;  /* False until future Hearthglass Pendant arc -> hides Give/Take Heart */
    bool     flag_sewer_key;
    bool     flag_captain_mended;
    bool     flag_muckmaw_defeated;
    bool     flag_ironfang_defeated;
    /* Milestone 5: 5 Side Quests (0=Unstarted, 1=Active, 2=Item Ready, 3=Complete) */
    uint8_t  side_quests[5];     /* 0=Whistle, 1=Flour, 2=Tart, 3=MoonBrie, 4=Babanas */
    bool     has_master_lure;    /* Awarded by Whiskerburgh Moon-Brie quest (+7 fishing bar width!) */

    /* World Actors */
    HearthNpc        npcs[HEARTH_MAX_NPCS];
    HearthFieldEnemy field_enemies[HEARTH_MAX_FIELD_ENEMIES];
    HearthHeartOrb   heart_orb;

    /* Dialogue, Intro, Cutscene VM, Shop & Fishing */
    HearthDialogue          dialogue;
    HearthIntroState        intro;
    HearthShopState         shop;
    HearthFishingState      fishing;
    const HearthCutsceneOp *cs_script;
    uint16_t                cs_ip;
    uint16_t                cs_wait_frames;

    /* Combat State */
    HearthBattleState battle;

    /* Title Menu Cursor: 0=New Story, 1=Explore Evertale, 2=Replay Intro */
    uint8_t title_cursor;

    /* Fixed-Capacity FX Pools */
    HearthParticle  particles[HEARTH_MAX_PARTICLES];
    HearthFloatText ftexts[HEARTH_MAX_FTEXT];
    uint8_t         active_particles;
    uint8_t         active_ftexts;
    uint16_t        windmill_rot;

    /* Latest PPU Frame Output Packet */
    HearthPpuPacket ppu;
} HearthState;

#if defined(HEARTH_TARGET_SNES)
/* Single static Bank $7E WRAM instance for zero-overhead direct 65816 addressing */
extern HearthState g_hearth;
#endif

/* Initialize engine state & world (zero malloc) */
void hearth_init(HearthState *st);

/* Reset tuning registers to default feel */
void hearth_default_tuning(HearthTuning *t);

/* Step deterministic 60Hz simulation by 1 frame using 12-bit pad bitmask */
const HearthPpuPacket *hearth_step_60hz(HearthState *st, uint16_t pad_bits);

/* Direct Mode Launchers & Utility Hooks */
void hearth_start_intro(HearthState *st);
void hearth_start_story_cg(HearthState *st, uint8_t script_id);
void hearth_skip_intro(HearthState *st);
void hearth_toggle_cinema_auto(HearthState *st);
void hearth_start_story(HearthState *st);
void hearth_start_sandbox(HearthState *st);
const char *hearth_get_objective_text(const HearthState *st);
void hearth_enter_zone(HearthState *st, uint8_t zone_id, uint16_t tx, uint16_t ty);
void hearth_toggle_atlas(HearthState *st);
void hearth_designer_cycle_vehicle(HearthState *st);
void hearth_designer_warp_region(HearthState *st, uint8_t region_id);
void hearth_start_cutscene(HearthState *st, const HearthCutsceneOp *script);
void hearth_start_battle(HearthState *st, uint8_t preset, uint8_t biome, int8_t field_idx);
void hearth_open_shop(HearthState *st, uint8_t shop_id);
void hearth_start_fishing(HearthState *st);
void hearth_respawn_enemies(HearthState *st);
void hearth_heal_party(HearthState *st);
void hearth_toggle_ppu_mode(HearthState *st);
uint8_t hearth_compute_hero_pose(const HearthState *st);

#endif /* HEARTH_CORE_H */
