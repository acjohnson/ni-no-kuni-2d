/* ============================================================================
 * hearth_core.h — Project Hearth Platform-Agnostic 16-Bit Simulation Core
 *
 * Pure C99. Zero dynamic allocation. Zero floating-point simulation math.
 * Pass a pointer to a static/stack HearthState (fits comfortably inside
 * Sega Genesis 64KB RAM and SNES 128KB WRAM!).
 * ============================================================================ */

#ifndef HEARTH_CORE_H
#define HEARTH_CORE_H

#include "hearth_data.h"
#include "hearth_prng.h"
#include "hearth_types.h"

typedef struct {
    /* World ROM/RAM Map Buffers */
    uint8_t map_bg1[HEARTH_MAP_CELLS];
    uint8_t map_bg2[HEARTH_MAP_CELLS];
    uint8_t map_col[HEARTH_MAP_CELLS];

    /* Tuning & Deterministic State */
    HearthTuning tuning;
    HearthPrng   prng;
    uint8_t      mode;
    uint32_t     frame_count;
    uint16_t     prev_pad;

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

    /* Camera & Screen Juice Registers */
    fp16_t   cam_fx, cam_fy;
    bool     cam_override;
    fp16_t   cam_start_fx, cam_start_fy;
    fp16_t   cam_target_fx, cam_target_fy;
    uint8_t  cam_pan_frame, cam_pan_dur;
    uint8_t  shake_intensity, shake_frames;
    uint8_t  hitstop_frames;
    uint8_t  flash_level;

    /* Party, Inventory & Heart Locket Flags */
    HearthPartyMember party[3];
    uint8_t  active_slot;
    uint8_t  potions;
    bool     flag_has_courage;
    bool     flag_guard_mended;
    bool     flag_chest_opened;
    bool     flag_elder_seen;
    bool     flag_boss_defeated;

    /* World Actors */
    HearthNpc        npcs[HEARTH_MAX_NPCS];
    HearthFieldEnemy field_enemies[HEARTH_MAX_FIELD_ENEMIES];
    HearthHeartOrb   heart_orb;

    /* Dialogue & Cutscene VM */
    HearthDialogue          dialogue;
    HearthIntroState        intro;
    const HearthCutsceneOp *cs_script;
    uint16_t                cs_ip;
    uint16_t                cs_wait_frames;

    /* Combat State */
    HearthBattleState battle;

    /* Title Menu Cursor */
    uint8_t title_cursor; /* 0=Start Sandbox, 1=Shrine Boss, 2=Replay Opening Intro */

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

/* Initialize engine state & sandbox map (zero malloc) */
void hearth_init(HearthState *st);

/* Reset tuning registers to default feel */
void hearth_default_tuning(HearthTuning *t);

/* Step deterministic 60Hz simulation by 1 frame using 12-bit pad bitmask */
const HearthPpuPacket *hearth_step_60hz(HearthState *st, uint16_t pad_bits);

/* Direct Mode Launchers & Utility Hooks */
void hearth_start_intro(HearthState *st);
void hearth_skip_intro(HearthState *st);
void hearth_start_sandbox(HearthState *st);
void hearth_start_cutscene(HearthState *st, const HearthCutsceneOp *script);
void hearth_start_battle(HearthState *st, uint8_t preset, uint8_t biome, int8_t field_idx);
void hearth_respawn_enemies(HearthState *st);
void hearth_heal_party(HearthState *st);
void hearth_toggle_ppu_mode(HearthState *st);

#endif /* HEARTH_CORE_H */
