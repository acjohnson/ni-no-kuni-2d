/* ============================================================================
 * wasm_bridge.c — Freestanding WebAssembly Host Bridge for engine/core/
 *
 * Compiles hearth_data.c + hearth_core.c + wasm_bridge.c directly to
 * public/hearth_core.wasm via Zig Clang (wasm32-freestanding) with ZERO libc,
 * ZERO dynamic allocation, and ZERO duplicated simulation logic.
 * ============================================================================ */

#include "hearth_core.h"

#define WASM_EXPORT(name) __attribute__((export_name(#name)))

#define SCALAR_LEN       144
#define OAM_STRIDE       12
#define PARTY_STRIDE     16
#define ENEMY_STRIDE     28
#define PARTICLE_STRIDE  10
#define FTEXT_STRIDE     6
#define FIELD_BOX_STRIDE 3
#define MAX_SFX_OUT      32

static HearthState g_state;
static int32_t     g_scalars[SCALAR_LEN];
static int32_t     g_oam_buf[HEARTH_MAX_OAM * OAM_STRIDE];
static int32_t     g_party_buf[3 * PARTY_STRIDE];
static int32_t     g_enemies_buf[HEARTH_MAX_BATTLE_ENEMIES * ENEMY_STRIDE];
static int32_t     g_particles_buf[HEARTH_MAX_PARTICLES * PARTICLE_STRIDE];
static int32_t     g_ftexts_buf[HEARTH_MAX_FTEXT * FTEXT_STRIDE];
static int32_t     g_field_box_buf[HEARTH_MAX_FIELD_ENEMIES * FIELD_BOX_STRIDE];
static int32_t     g_cs_op_buf[6];
static uint8_t     g_sfx_out[MAX_SFX_OUT];
static uint8_t     g_sfx_out_count = 0;

static void append_ppu_sfx(void) {
    uint8_t i;
    for (i = 0; i < g_state.ppu.sfx_count; i++) {
        if (g_sfx_out_count < MAX_SFX_OUT) {
            g_sfx_out[g_sfx_out_count++] = g_state.ppu.sfx_queue[i];
        }
    }
    g_state.ppu.sfx_count = 0;
}

static void push_bridge_sfx(uint8_t sfx) {
    if (sfx != SFX_NONE && g_sfx_out_count < MAX_SFX_OUT) {
        g_sfx_out[g_sfx_out_count++] = sfx;
    }
}

static void sync_snapshot(void) {
    const HearthPpuPacket *p = &g_state.ppu;
    const HearthDialogue *d = &g_state.dialogue;
    const HearthBattleState *b = &g_state.battle;
    uint8_t active_fe = 0;
    uint32_t locket_mask = 0;
    uint16_t i;
    uint16_t p_cnt = 0;
    uint16_t f_cnt = 0;

    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++) {
        const HearthFieldEnemy *fe = &g_state.field_enemies[i];
        int32_t *fb = &g_field_box_buf[i * FIELD_BOX_STRIDE];
        fb[0] = fe->alive ? 1 : 0;
        fb[1] = FROM_FP(fe->fx);
        fb[2] = FROM_FP(fe->fy);
        if (fe->alive) active_fe++;
    }

    if (g_state.flag_has_courage)   locket_mask |= 1u << 0;
    if (g_state.flag_guard_mended)  locket_mask |= 1u << 1;
    if (g_state.flag_chest_opened)  locket_mask |= 1u << 2;
    if (g_state.flag_elder_seen)    locket_mask |= 1u << 3;
    if (g_state.flag_boss_defeated) locket_mask |= 1u << 4;

    /* Pack OAM Sprites */
    for (i = 0; i < p->oam_count && i < HEARTH_MAX_OAM; i++) {
        const HearthOamSprite *o = &p->oam[i];
        int32_t *out = &g_oam_buf[i * OAM_STRIDE];
        out[0]  = o->x;
        out[1]  = o->y;
        out[2]  = o->sprite_id;
        out[3]  = o->frame;
        out[4]  = o->dir;
        out[5]  = o->flags;
        out[6]  = o->scale_x_fp;
        out[7]  = o->scale_y_fp;
        out[8]  = o->aura;
        out[9]  = o->layer;
        out[10] = o->sort_y;
        out[11] = o->rot_deg;
    }

    /* Pack Party Members */
    for (i = 0; i < 3; i++) {
        const HearthPartyMember *m = &g_state.party[i];
        int32_t *out = &g_party_buf[i * PARTY_STRIDE];
        out[0]  = m->slot;
        out[1]  = (int32_t)(uintptr_t)m->name;
        out[2]  = (int32_t)(uintptr_t)m->basic_name;
        out[3]  = (int32_t)(uintptr_t)m->skill_name;
        out[4]  = m->sprite_id;
        out[5]  = m->hp;
        out[6]  = m->max_hp;
        out[7]  = m->mp;
        out[8]  = m->max_mp;
        out[9]  = m->atk;
        out[10] = m->def;
        out[11] = m->basic_kind;
        out[12] = m->skill_kind;
        out[13] = m->skill_cost;
        out[14] = m->skill_is_heal ? 1 : 0;
        out[15] = m->skill_is_aoe ? 1 : 0;
    }

    /* Pack Battle Enemies */
    for (i = 0; i < b->enemy_count && i < HEARTH_MAX_BATTLE_ENEMIES; i++) {
        const HearthBattleEnemy *e = &b->enemies[i];
        int32_t *out = &g_enemies_buf[i * ENEMY_STRIDE];
        out[0]  = (int32_t)(uintptr_t)e->name;
        out[1]  = (int32_t)(uintptr_t)e->special_name;
        out[2]  = e->sprite_id;
        out[3]  = e->is_boss ? 1 : 0;
        out[4]  = e->alive ? 1 : 0;
        out[5]  = e->staggered ? 1 : 0;
        out[6]  = e->hp;
        out[7]  = e->max_hp;
        out[8]  = e->atk;
        out[9]  = e->def;
        out[10] = e->home_x;
        out[11] = e->home_y;
        out[12] = e->x;
        out[13] = e->y;
        out[14] = e->scale_x_fp;
        out[15] = e->scale_y_fp;
        out[16] = e->flash_frames;
        out[17] = e->hurt_frames;
        out[18] = e->charge_turns;
        out[19] = e->max_charge_turns;
        out[20] = e->lock_count;
        out[21] = e->locks[0];
        out[22] = e->locks[1];
        out[23] = e->locks[2];
        out[24] = e->broken_locks[0] ? 1 : 0;
        out[25] = e->broken_locks[1] ? 1 : 0;
        out[26] = e->broken_locks[2] ? 1 : 0;
        out[27] = e->anim_frame;
    }

    /* Pack Active Particles */
    for (i = 0; i < HEARTH_MAX_PARTICLES; i++) {
        const HearthParticle *pt = &g_state.particles[i];
        int32_t *out;
        if (!pt->active) continue;
        out = &g_particles_buf[p_cnt * PARTICLE_STRIDE];
        out[0] = pt->kind;
        out[1] = pt->fx;
        out[2] = pt->fy;
        out[3] = pt->vx;
        out[4] = pt->vy;
        out[5] = pt->life;
        out[6] = pt->max_life;
        out[7] = pt->size;
        out[8] = ((int32_t)pt->pal_idx << 8) | (int32_t)pt->col_idx;
        out[9] = pt->screen_space ? 1 : 0;
        p_cnt++;
    }

    /* Pack Active Floating Texts */
    for (i = 0; i < HEARTH_MAX_FTEXT; i++) {
        const HearthFloatText *ft = &g_state.ftexts[i];
        int32_t *out;
        if (!ft->active) continue;
        out = &g_ftexts_buf[f_cnt * FTEXT_STRIDE];
        out[0] = ft->x;
        out[1] = ft->y;
        out[2] = (int32_t)(uintptr_t)ft->text;
        out[3] = ((int32_t)ft->pal_idx << 8) | (int32_t)ft->col_idx;
        out[4] = ft->life;
        out[5] = ft->world_space ? 1 : 0;
        f_cnt++;
    }

    /* Pack Scalar Registers */
    g_scalars[0]  = p->width;
    g_scalars[1]  = p->height;
    g_scalars[2]  = (int32_t)p->frame_count;
    g_scalars[3]  = p->mode;
    g_scalars[4]  = p->cam_x;
    g_scalars[5]  = p->cam_y;
    g_scalars[6]  = p->shake_x;
    g_scalars[7]  = p->shake_y;
    g_scalars[8]  = p->flash_level;
    g_scalars[9]  = p->hitstop_active ? 1 : 0;
    g_scalars[10] = p->oam_count;
    g_scalars[11] = p->oam_limit;
    g_scalars[12] = p->interact_active ? 1 : 0;
    g_scalars[13] = p->interact_x;
    g_scalars[14] = p->interact_y;
    g_scalars[15] = (int32_t)(uintptr_t)p->interact_label;

    g_scalars[16] = g_state.active_slot;
    g_scalars[17] = g_state.potions;
    g_scalars[18] = g_state.hero_fx;
    g_scalars[19] = g_state.hero_fy;
    g_scalars[20] = (int32_t)locket_mask;
    g_scalars[21] = active_fe;

    g_scalars[22] = g_state.tuning.ppu_mode;
    g_scalars[23] = g_state.tuning.walk_speed_fp;
    g_scalars[24] = g_state.tuning.dash_speed_fp;
    g_scalars[25] = g_state.tuning.accel_fp;
    g_scalars[26] = g_state.tuning.follow_dist_px;
    g_scalars[27] = g_state.tuning.hitstop_normal_f;
    g_scalars[28] = g_state.tuning.hitstop_crit_f;
    g_scalars[29] = g_state.tuning.hitstop_parry_f;
    g_scalars[30] = g_state.tuning.timed_hit_window_f;
    g_scalars[31] = g_state.tuning.parry_window_f;
    g_scalars[32] = g_state.tuning.show_hitboxes ? 1 : 0;

    g_scalars[33] = d->active ? 1 : 0;
    g_scalars[34] = (int32_t)(uintptr_t)d->speaker;
    g_scalars[35] = (int32_t)(uintptr_t)d->full_text;
    g_scalars[36] = d->visible_chars;
    g_scalars[37] = d->finished ? 1 : 0;
    g_scalars[38] = d->choice_count;
    g_scalars[39] = d->selected_choice;
    g_scalars[40] = d->voice_sfx;
    g_scalars[41] = (int32_t)(uintptr_t)(d->choice_count > 0 ? d->choices[0].label : NULL);
    g_scalars[42] = (int32_t)(uintptr_t)(d->choice_count > 1 ? d->choices[1].label : NULL);
    g_scalars[43] = (int32_t)(uintptr_t)(d->choice_count > 2 ? d->choices[2].label : NULL);
    g_scalars[44] = d->choice_count > 0 ? d->choices[0].action_id : 0;
    g_scalars[45] = d->choice_count > 1 ? d->choices[1].action_id : 0;
    g_scalars[46] = d->choice_count > 2 ? d->choices[2].action_id : 0;

    g_scalars[47] = b->active ? 1 : 0;
    g_scalars[48] = b->biome;
    g_scalars[49] = b->phase;
    g_scalars[50] = b->phase_timer;
    g_scalars[51] = b->active_slot;
    g_scalars[52] = b->prev_slot;
    g_scalars[53] = b->swap_arc_fp;
    g_scalars[54] = b->hero_x;
    g_scalars[55] = b->hero_y;
    g_scalars[56] = b->hero_scale_x_fp;
    g_scalars[57] = b->hero_scale_y_fp;
    g_scalars[58] = b->hero_flash_frames;
    g_scalars[59] = b->hero_guarding ? 1 : 0;
    g_scalars[60] = b->wizard_back_x;
    g_scalars[61] = b->wizard_back_y;
    g_scalars[62] = b->selected_cmd;
    g_scalars[63] = b->selected_enemy;
    g_scalars[64] = b->enemy_count;
    g_scalars[65] = b->is_boss_battle ? 1 : 0;
    g_scalars[66] = b->ring.active ? 1 : 0;
    g_scalars[67] = b->ring.is_parry ? 1 : 0;
    g_scalars[68] = b->ring.x;
    g_scalars[69] = b->ring.y;
    g_scalars[70] = b->ring.progress_fp;
    g_scalars[71] = b->ring.sweet_start_fp;
    g_scalars[72] = b->ring.result;
    g_scalars[73] = (int32_t)(uintptr_t)b->banner;
    g_scalars[74] = b->banner_frames;
    g_scalars[75] = b->combos;
    g_scalars[76] = b->parries;
    g_scalars[77] = b->locks_broken;
    g_scalars[78] = b->exp_earned;
    g_scalars[79] = p_cnt;
    g_scalars[80] = f_cnt;
    g_scalars[81] = p->fade_level;
    g_scalars[82] = g_state.intro.scene;
    g_scalars[83] = g_state.intro.timer;
    g_scalars[84] = g_state.intro.chars;
    g_scalars[85] = (int32_t)(uintptr_t)g_state.intro.title;
    g_scalars[86] = (int32_t)(uintptr_t)g_state.intro.line1;
    g_scalars[87] = (int32_t)(uintptr_t)g_state.intro.line2;
    g_scalars[88] = b->vfx_kind;
    g_scalars[89] = b->vfx_timer;
    g_scalars[90] = b->vfx_dur;
    g_scalars[91] = b->vfx_x;
    g_scalars[92] = b->vfx_y;
    g_scalars[93]  = b->vfx_tx;
    g_scalars[94]  = b->vfx_ty;

    /* Multi-Zone, Story, Economy, Shop & Fishing Registers */
    g_scalars[95]  = p->zone_id;
    g_scalars[96]  = p->terrain_pal_id;
    g_scalars[97]  = p->bgm_id;
    g_scalars[98]  = p->zone_is_dark ? 1 : 0;
    g_scalars[99]  = p->zone_banner_frames;
    g_scalars[100] = (int32_t)(uintptr_t)p->zone_name;
    g_scalars[101] = g_state.story_stage;
    g_scalars[102] = g_state.map_revision;
    g_scalars[103] = g_state.guilders;
    g_scalars[104] = g_state.star_drops;
    g_scalars[105] = g_state.snacks;
    g_scalars[106] = g_state.lure_tier;
    g_scalars[107] = g_state.fish_total_caught;
    g_scalars[108] = g_state.fish_bag[0];
    g_scalars[109] = g_state.fish_bag[1];
    g_scalars[110] = g_state.fish_bag[2];
    g_scalars[111] = g_state.fish_bag[3];
    g_scalars[112] = g_state.fish_bag[4];
    g_scalars[113] = g_state.fish_bag[5];
    g_scalars[114] = g_state.shop.active ? 1 : 0;
    g_scalars[115] = g_state.shop.shop_id;
    g_scalars[116] = g_state.shop.tab;
    g_scalars[117] = g_state.shop.cursor;
    g_scalars[118] = g_state.shop.msg_timer;
    g_scalars[119] = (int32_t)(uintptr_t)g_state.shop.title;
    g_scalars[120] = (int32_t)(uintptr_t)g_state.shop.msg;
    g_scalars[121] = g_state.fishing.active ? 1 : 0;
    g_scalars[122] = g_state.fishing.phase;
    g_scalars[123] = g_state.fishing.bobber_x;
    g_scalars[124] = g_state.fishing.bobber_y;
    g_scalars[125] = g_state.fishing.fish_pos;
    g_scalars[126] = g_state.fishing.bar_pos;
    g_scalars[127] = g_state.fishing.bar_half_w;
    g_scalars[128] = g_state.fishing.progress;
    g_scalars[129] = g_state.fishing.tension;
    g_scalars[130] = g_state.fishing.species;
    g_scalars[131] = g_state.fishing.weight_oz;
    g_scalars[132] = b->guilders_earned;
    g_scalars[133] = b->encounter_preset;
    g_scalars[134] = g_state.flag_has_wand ? 1 : 0;
    g_scalars[135] = g_state.flag_sewer_key ? 1 : 0;
}

static void inject_ui_pad(uint16_t mask) {
    uint16_t saved_prev = g_state.prev_pad;
    g_state.prev_pad = (uint16_t)(saved_prev & (uint16_t)(~mask));
    hearth_step_60hz(&g_state, mask);
    g_state.prev_pad = (uint16_t)(saved_prev & (uint16_t)(~mask));
    append_ppu_sfx();
}

/* ============================================================================
 * EXPORTED WASM LIFECYCLE & DATA ACCESSORS
 * ============================================================================ */
WASM_EXPORT(wasm_init)
void wasm_init(void) {
    hearth_init(&g_state);
    g_sfx_out_count = 0;
    sync_snapshot();
}

WASM_EXPORT(wasm_step)
void wasm_step(uint32_t pad_bits) {
    hearth_step_60hz(&g_state, (uint16_t)pad_bits);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_start_story)
void wasm_start_story(void) {
    g_state.ppu.sfx_count = 0;
    hearth_start_story(&g_state);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_start_sandbox)
void wasm_start_sandbox(void) {
    g_state.ppu.sfx_count = 0;
    hearth_start_sandbox(&g_state);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_enter_zone)
void wasm_enter_zone(uint32_t zone_id, uint32_t tx, uint32_t ty) {
    g_state.ppu.sfx_count = 0;
    hearth_enter_zone(&g_state, (uint8_t)zone_id, (uint8_t)tx, (uint8_t)ty);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_start_cutscene_demo)
void wasm_start_cutscene_demo(void) {
    int i;
    g_state.ppu.sfx_count = 0;
    g_state.hero_fx = TO_FP(10 * HEARTH_TILE_SIZE + 8);
    g_state.hero_fy = TO_FP(14 * HEARTH_TILE_SIZE + 8);
    g_state.hero_vx = 0;
    g_state.hero_vy = 0;
    g_state.hero_dir = DIR_DOWN;
    for (i = 0; i < HEARTH_TRAIL_LEN; i++) {
        g_state.trail_x[i] = g_state.hero_fx - TO_FP(18);
        g_state.trail_y[i] = g_state.hero_fy - TO_FP(4);
    }
    g_state.pip_fx = g_state.hero_fx - TO_FP(18);
    g_state.pip_fy = g_state.hero_fy - TO_FP(4);
    g_state.cam_override = false;
    hearth_start_cutscene(&g_state, CUTSCENE_INTRO_DEMO);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_start_quick_battle)
void wasm_start_quick_battle(uint32_t preset) {
    uint8_t biome = (uint8_t)BIOME_MEADOW;
    if (preset == ENCOUNTER_SHRINE_BOSS) biome = (uint8_t)BIOME_SHRINE;
    else if (preset == ENCOUNTER_SEWER_PACK || preset == ENCOUNTER_SEWER_BOSS) biome = (uint8_t)BIOME_SEWER;
    else if (preset == ENCOUNTER_BOAR_GUARD || preset == ENCOUNTER_IRONFANG_BOSS) biome = (uint8_t)BIOME_BOARHOLD;
    g_state.ppu.sfx_count = 0;
    hearth_start_battle(&g_state, (uint8_t)preset, biome, -1);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_start_intro)
void wasm_start_intro(void) {
    g_state.ppu.sfx_count = 0;
    hearth_start_intro(&g_state);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_skip_intro)
void wasm_skip_intro(void) {
    g_state.ppu.sfx_count = 0;
    hearth_skip_intro(&g_state);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_return_to_title)
void wasm_return_to_title(void) {
    g_state.ppu.sfx_count = 0;
    hearth_skip_intro(&g_state);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_heal_party)
void wasm_heal_party(void) {
    g_state.ppu.sfx_count = 0;
    hearth_heal_party(&g_state);
    append_ppu_sfx();
    sync_snapshot();
}

WASM_EXPORT(wasm_respawn_enemies)
void wasm_respawn_enemies(void) {
    hearth_respawn_enemies(&g_state);
    sync_snapshot();
}

WASM_EXPORT(wasm_set_tuning)
void wasm_set_tuning(
    uint32_t ppu_mode,
    int32_t walk_speed_fp,
    int32_t dash_speed_fp,
    int32_t accel_fp,
    uint32_t follow_dist_px,
    uint32_t hitstop_normal_f,
    uint32_t hitstop_crit_f,
    uint32_t hitstop_parry_f,
    uint32_t timed_hit_window_f,
    uint32_t parry_window_f,
    uint32_t show_hitboxes
) {
    uint8_t old_mode = g_state.tuning.ppu_mode;
    g_state.tuning.ppu_mode = (ppu_mode == PPU_SNES_256x224) ? (uint8_t)PPU_SNES_256x224 : (uint8_t)PPU_GENESIS_320x224;
    g_state.tuning.walk_speed_fp = (int16_t)walk_speed_fp;
    g_state.tuning.dash_speed_fp = (int16_t)dash_speed_fp;
    g_state.tuning.accel_fp = (int16_t)accel_fp;
    g_state.tuning.follow_dist_px = (uint8_t)follow_dist_px;
    g_state.tuning.hitstop_normal_f = (uint8_t)hitstop_normal_f;
    g_state.tuning.hitstop_crit_f = (uint8_t)hitstop_crit_f;
    g_state.tuning.hitstop_parry_f = (uint8_t)hitstop_parry_f;
    g_state.tuning.timed_hit_window_f = (uint8_t)timed_hit_window_f;
    g_state.tuning.parry_window_f = (uint8_t)parry_window_f;
    g_state.tuning.show_hitboxes = (show_hitboxes != 0);

    if (old_mode != g_state.tuning.ppu_mode) {
        uint16_t new_w = (g_state.tuning.ppu_mode == PPU_SNES_256x224) ? 256u : 320u;
        g_state.ppu.width = new_w;
        g_state.ppu.height = 224u;
        if (g_state.battle.active) {
            int16_t left_base = (new_w == 256u) ? 68 : 84;
            int16_t right_base = (new_w == 256u) ? 188 : 236;
            int16_t dx_hero = (int16_t)(left_base - g_state.battle.hero_home_x);
            int16_t old_right = (new_w == 256u) ? 236 : 188;
            int16_t dx_enemy = (int16_t)(right_base - old_right);
            uint8_t ei;
            g_state.battle.hero_home_x = left_base;
            g_state.battle.hero_x = (int16_t)(g_state.battle.hero_x + dx_hero);
            g_state.battle.wizard_back_x = (int16_t)((left_base > 46) ? (left_base - 42) : 24);
            for (ei = 0; ei < g_state.battle.enemy_count; ei++) {
                g_state.battle.enemies[ei].home_x = (int16_t)(g_state.battle.enemies[ei].home_x + dx_enemy);
                g_state.battle.enemies[ei].x = (int16_t)(g_state.battle.enemies[ei].x + dx_enemy);
            }
        }
    }
    sync_snapshot();
}

/* ============================================================================
 * UI MOUSE/TOUCH BRIDGE HOOKS (Delegate Directly to C Core!)
 * ============================================================================ */
WASM_EXPORT(wasm_ui_battle_command)
void wasm_ui_battle_command(uint32_t cmd_idx) {
    if (g_state.mode != MODE_BATTLE || g_state.battle.phase != BPHASE_COMMAND) return;
    if (cmd_idx >= 4u) return;
    g_state.battle.selected_cmd = (uint8_t)cmd_idx;
    inject_ui_pad(PAD_A);
    sync_snapshot();
}

WASM_EXPORT(wasm_ui_battle_swap)
void wasm_ui_battle_swap(uint32_t slot) {
    if (g_state.mode != MODE_BATTLE || g_state.battle.phase != BPHASE_COMMAND) return;
    if (slot >= 3u || slot == g_state.battle.active_slot || g_state.party[slot].hp <= 0) return;
    if (slot == (uint32_t)((g_state.battle.active_slot + 1u) % 3u)) {
        inject_ui_pad(PAD_R);
    } else {
        inject_ui_pad(PAD_L);
    }
    sync_snapshot();
}

WASM_EXPORT(wasm_ui_battle_select_enemy)
void wasm_ui_battle_select_enemy(uint32_t idx) {
    if (g_state.mode != MODE_BATTLE || g_state.battle.phase != BPHASE_COMMAND) return;
    if (idx >= g_state.battle.enemy_count || !g_state.battle.enemies[idx].alive) return;
    g_state.battle.selected_enemy = (uint8_t)idx;
    push_bridge_sfx(SFX_CURSOR);
    sync_snapshot();
}

WASM_EXPORT(wasm_ui_timed_tap)
void wasm_ui_timed_tap(void) {
    if (g_state.mode == MODE_BATTLE || g_state.mode == MODE_INTRO || g_state.dialogue.active) {
        inject_ui_pad(PAD_A);
        sync_snapshot();
    }
}

WASM_EXPORT(wasm_ui_select_choice)
void wasm_ui_select_choice(uint32_t idx) {
    if (!g_state.dialogue.active || !g_state.dialogue.finished) return;
    if (idx >= g_state.dialogue.choice_count) return;
    g_state.dialogue.selected_choice = (uint8_t)idx;
    inject_ui_pad(PAD_A);
    sync_snapshot();
}

WASM_EXPORT(wasm_ui_finish_battle_summary)
void wasm_ui_finish_battle_summary(uint32_t rematch) {
    if (g_state.mode != MODE_BATTLE_VICTORY && g_state.mode != MODE_BATTLE_DEFEAT) return;
    inject_ui_pad(rematch ? PAD_B : PAD_A);
    sync_snapshot();
}

WASM_EXPORT(wasm_ui_shop_tab)
void wasm_ui_shop_tab(uint32_t tab) {
    if (g_state.mode != MODE_SHOP || !g_state.shop.active) return;
    g_state.shop.tab = (tab != 0u) ? 1u : 0u;
    g_state.shop.cursor = 0;
    push_bridge_sfx(SFX_CURSOR);
    sync_snapshot();
}

WASM_EXPORT(wasm_ui_shop_select)
void wasm_ui_shop_select(uint32_t idx) {
    uint32_t max_rows;
    if (g_state.mode != MODE_SHOP || !g_state.shop.active) return;
    max_rows = (g_state.shop.tab == 0u) ? 5u : (uint32_t)HEARTH_FISH_SPECIES;
    if (idx >= max_rows) return;
    g_state.shop.cursor = (uint8_t)idx;
    inject_ui_pad(PAD_A);
    sync_snapshot();
}

WASM_EXPORT(wasm_ui_shop_close)
void wasm_ui_shop_close(void) {
    if (g_state.mode != MODE_SHOP || !g_state.shop.active) return;
    inject_ui_pad(PAD_B);
    sync_snapshot();
}

/* ============================================================================
 * MEMORY BUFFER POINTERS & CUTSCENE ROM TABLE READERS
 * ============================================================================ */
WASM_EXPORT(wasm_get_scalars_ptr)
const int32_t *wasm_get_scalars_ptr(void) { return g_scalars; }

WASM_EXPORT(wasm_get_oam_ptr)
const int32_t *wasm_get_oam_ptr(void) { return g_oam_buf; }

WASM_EXPORT(wasm_get_party_ptr)
const int32_t *wasm_get_party_ptr(void) { return g_party_buf; }

WASM_EXPORT(wasm_get_enemies_ptr)
const int32_t *wasm_get_enemies_ptr(void) { return g_enemies_buf; }

WASM_EXPORT(wasm_get_particles_ptr)
const int32_t *wasm_get_particles_ptr(void) { return g_particles_buf; }

WASM_EXPORT(wasm_get_ftexts_ptr)
const int32_t *wasm_get_ftexts_ptr(void) { return g_ftexts_buf; }

WASM_EXPORT(wasm_get_field_boxes_ptr)
const int32_t *wasm_get_field_boxes_ptr(void) { return g_field_box_buf; }

WASM_EXPORT(wasm_get_sfx_count)
uint32_t wasm_get_sfx_count(void) { return g_sfx_out_count; }

WASM_EXPORT(wasm_get_sfx_ptr)
const uint8_t *wasm_get_sfx_ptr(void) { return g_sfx_out; }

WASM_EXPORT(wasm_clear_sfx)
void wasm_clear_sfx(void) { g_sfx_out_count = 0; }

WASM_EXPORT(wasm_get_bg1_ptr)
const uint8_t *wasm_get_bg1_ptr(void) { return g_state.map_bg1; }

WASM_EXPORT(wasm_get_bg2_ptr)
const uint8_t *wasm_get_bg2_ptr(void) { return g_state.map_bg2; }

WASM_EXPORT(wasm_get_col_ptr)
const uint8_t *wasm_get_col_ptr(void) { return g_state.map_col; }

WASM_EXPORT(wasm_get_palettes_ptr)
const uint32_t *wasm_get_palettes_ptr(void) { return &HEARTH_PALETTES_RGB[0][0]; }

WASM_EXPORT(wasm_get_zone_palettes_ptr)
const uint32_t *wasm_get_zone_palettes_ptr(void) { return &HEARTH_ZONE_PALETTES_RGB[0][0]; }

WASM_EXPORT(wasm_get_shop_item_name)
const char *wasm_get_shop_item_name(uint32_t i) {
    return (i < 5u) ? HEARTH_SHOP_ITEMS[i].name : "";
}

WASM_EXPORT(wasm_get_shop_item_desc)
const char *wasm_get_shop_item_desc(uint32_t i) {
    return (i < 5u) ? HEARTH_SHOP_ITEMS[i].desc : "";
}

WASM_EXPORT(wasm_get_shop_item_price)
uint32_t wasm_get_shop_item_price(uint32_t i) {
    return (i < 5u) ? HEARTH_SHOP_ITEMS[i].price : 0u;
}

WASM_EXPORT(wasm_get_fish_name)
const char *wasm_get_fish_name(uint32_t i) {
    return (i < (uint32_t)HEARTH_FISH_SPECIES) ? HEARTH_FISH_TABLE[i].name : "";
}

WASM_EXPORT(wasm_get_fish_price)
uint32_t wasm_get_fish_price(uint32_t i) {
    return (i < (uint32_t)HEARTH_FISH_SPECIES) ? HEARTH_FISH_TABLE[i].sell_price : 0u;
}

WASM_EXPORT(wasm_get_cutscene_intro_count)
uint32_t wasm_get_cutscene_intro_count(void) {
    uint32_t n = 0;
    while (CUTSCENE_INTRO_DEMO[n].op != OP_END && n < 128u) {
        n++;
    }
    return n + 1u; /* Include OP_END */
}

WASM_EXPORT(wasm_read_cutscene_intro_op)
const int32_t *wasm_read_cutscene_intro_op(uint32_t idx) {
    const HearthCutsceneOp *op = &CUTSCENE_INTRO_DEMO[idx];
    g_cs_op_buf[0] = op->op;
    g_cs_op_buf[1] = op->a0;
    g_cs_op_buf[2] = op->a1;
    g_cs_op_buf[3] = op->a2;
    g_cs_op_buf[4] = (int32_t)(uintptr_t)op->speaker;
    g_cs_op_buf[5] = (int32_t)(uintptr_t)op->text;
    return g_cs_op_buf;
}
