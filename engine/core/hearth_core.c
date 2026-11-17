/* ============================================================================
 * hearth_core.c — Project Hearth 60Hz Fixed-Point 16-Bit Simulation Core
 *
 * Strictly portable C99 (-std=c99 -Wall -Wextra -pedantic).
 * Zero floating-point math, zero malloc, zero OS/graphics headers.
 * ============================================================================ */

#include "hearth_core.h"
#include <string.h>

/* Choice Action IDs */
enum {
    ACT_CLOSE        = 0,
    ACT_TAKE_COURAGE = 1,
    ACT_GIVE_COURAGE = 2,
    ACT_REPLAY_INTRO = 3,
    ACT_JUMP_BOSS    = 4
};

/* ============================================================================
 * INTERNAL INTEGER HELPERS
 * ============================================================================ */
static int16_t iabs16(int16_t v) { return (v < 0) ? (int16_t)(-v) : v; }
static int32_t iabs32(int32_t v) { return (v < 0) ? -v : v; }
static int16_t iclamp16(int16_t v, int16_t lo, int16_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static fp16_t approach_fp(fp16_t cur, fp16_t target, fp16_t step) {
    if (cur < target) return (cur + step > target) ? target : (cur + step);
    if (cur > target) return (cur - step < target) ? target : (cur - step);
    return cur;
}

static int16_t sin_arc(uint16_t num, uint16_t den, int16_t peak) {
    if (den == 0) return 0;
    uint32_t idx = ((uint32_t)num * 32u) / (uint32_t)den;
    if (idx > 32u) idx = 32u;
    return (int16_t)(((int32_t)HEARTH_SIN64[idx & 63u] * (int32_t)peak) / 127);
}

static void push_sfx(HearthState *st, uint8_t sfx) {
    if (sfx == SFX_NONE) return;
    if (st->ppu.sfx_count < HEARTH_MAX_SFX_QUEUE) {
        st->ppu.sfx_queue[st->ppu.sfx_count++] = sfx;
    }
}

static void add_shake(HearthState *st, uint8_t intensity, uint8_t frames) {
    if (intensity > st->shake_intensity) st->shake_intensity = intensity;
    if (frames > st->shake_frames) st->shake_frames = frames;
}

static void set_hitstop(HearthState *st, uint8_t frames) {
    if (frames > st->hitstop_frames) st->hitstop_frames = frames;
}

static void get_ppu_dims(const HearthState *st, uint16_t *w, uint16_t *h, uint8_t *oam_lim) {
    if (st->tuning.ppu_mode == PPU_SNES_256x224) {
        *w = 256; *h = 224; *oam_lim = 64;
    } else {
        *w = 320; *h = 224; *oam_lim = 64;
    }
}

static void copy_str_safe(char *dst, size_t dst_cap, const char *src) {
    if (dst_cap == 0) return;
    size_t i = 0;
    if (src) {
        while (i + 1 < dst_cap && src[i] != '\0') {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

/* Format signed integer into tiny buffer without <stdio.h> */
static void fmt_int_text(char *buf, size_t cap, const char *prefix, int16_t val, const char *suffix) {
    char digits[12];
    int dlen = 0;
    int32_t v = val;
    bool neg = false;
    if (v < 0) { neg = true; v = -v; }
    if (v == 0) {
        digits[dlen++] = '0';
    } else {
        while (v > 0 && dlen < 10) {
            digits[dlen++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    size_t pos = 0;
    if (prefix) {
        while (*prefix && pos + 1 < cap) buf[pos++] = *prefix++;
    }
    if (neg && pos + 1 < cap) buf[pos++] = '-';
    while (dlen > 0 && pos + 1 < cap) buf[pos++] = digits[--dlen];
    if (suffix) {
        while (*suffix && pos + 1 < cap) buf[pos++] = *suffix++;
    }
    buf[pos] = '\0';
}

/* ============================================================================
 * PARTICLE & FLOATING TEXT POOLS
 * ============================================================================ */
static void spawn_particle(
    HearthState *st, uint8_t kind, int16_t x, int16_t y,
    fp16_t vx, fp16_t vy, uint8_t life, uint8_t size,
    uint8_t pal_idx, uint8_t col_idx, bool screen_space
) {
    for (int i = 0; i < HEARTH_MAX_PARTICLES; i++) {
        HearthParticle *p = &st->particles[i];
        if (!p->active) {
            p->active = true;
            p->kind = kind;
            p->fx = TO_FP(x);
            p->fy = TO_FP(y);
            p->vx = vx;
            p->vy = vy;
            p->life = life;
            p->max_life = life;
            p->size = size;
            p->pal_idx = pal_idx;
            p->col_idx = col_idx;
            p->screen_space = screen_space;
            return;
        }
    }
}

static void spawn_burst(
    HearthState *st, uint8_t kind, int16_t x, int16_t y,
    int count, uint8_t pal_idx, uint8_t col_idx, bool screen_space
) {
    for (int i = 0; i < count; i++) {
        fp16_t vx = hearth_prng_range(&st->prng, -340, 340);
        fp16_t vy = (kind == 5)
            ? hearth_prng_range(&st->prng, -240, -60)
            : hearth_prng_range(&st->prng, -360, 140);
        uint8_t life = (uint8_t)hearth_prng_range(&st->prng, 16, 30);
        uint8_t size = (kind == 3 || kind == 4) ? 3 : 2;
        spawn_particle(st, kind,
            (int16_t)(x + hearth_prng_range(&st->prng, -4, 4)),
            (int16_t)(y + hearth_prng_range(&st->prng, -4, 4)),
            vx, vy, life, size, pal_idx, col_idx, screen_space);
    }
}

static void spawn_ftext(
    HearthState *st, int16_t x, int16_t y, const char *text,
    uint8_t pal_idx, uint8_t col_idx, bool world_space
) {
    for (int i = 0; i < HEARTH_MAX_FTEXT; i++) {
        HearthFloatText *ft = &st->ftexts[i];
        if (!ft->active) {
            ft->active = true;
            ft->x = x;
            ft->y = y;
            ft->vy_fp = -260;
            copy_str_safe(ft->text, sizeof(ft->text), text);
            ft->pal_idx = pal_idx;
            ft->col_idx = col_idx;
            ft->life = 44;
            ft->world_space = world_space;
            return;
        }
    }
}

static void step_fx_pools(HearthState *st) {
    for (int i = 0; i < HEARTH_MAX_PARTICLES; i++) {
        HearthParticle *p = &st->particles[i];
        if (!p->active) continue;
        if (p->life <= 1) { p->active = false; continue; }
        p->life--;
        p->fx += p->vx;
        p->fy += p->vy;
        if (p->kind == 1 || p->kind == 4) {
            p->vy += 22;
        } else if (p->kind == 3) {
            p->vx = (p->vx * 235) >> 8;
            p->vy = (p->vy * 235) >> 8;
        }
    }
    for (int i = 0; i < HEARTH_MAX_FTEXT; i++) {
        HearthFloatText *ft = &st->ftexts[i];
        if (!ft->active) continue;
        if (ft->life <= 1) { ft->active = false; continue; }
        ft->life--;
        ft->y = (int16_t)(ft->y + FROM_FP(ft->vy_fp));
        ft->vy_fp = (int16_t)((ft->vy_fp * 232) >> 8);
    }
}

/* ============================================================================
 * INITIALIZATION & PARTY SETUP
 * ============================================================================ */
void hearth_default_tuning(HearthTuning *t) {
    t->ppu_mode           = PPU_GENESIS_320x224;
    t->walk_speed_fp      = 384; /* 1.50 px/frame */
    t->dash_speed_fp      = 614; /* 2.40 px/frame */
    t->accel_fp           = 128;
    t->follow_dist_px     = 22;
    t->hitstop_normal_f   = 3;
    t->hitstop_crit_f     = 7;
    t->hitstop_parry_f    = 5;
    t->timed_hit_window_f = 8;
    t->parry_window_f     = 9;
    t->show_hitboxes      = false;
}

static void init_party(HearthState *st) {
    st->party[0] = (HearthPartyMember){
        0, "Rowan", "Locket Staff", "Mending Dew",
        SPR_HERO, 84, 84, 32, 32, 15, 8,
        LOCK_SWORD, LOCK_NONE, 7, true, false
    };
    st->party[1] = (HearthPartyMember){
        1, "Bramblekit", "Acorn Rush", "Solar Slam",
        SPR_FAMILIAR_BRAMBLE, 96, 96, 24, 24, 19, 11,
        LOCK_SWORD, LOCK_SUN, 6, false, false
    };
    st->party[2] = (HearthPartyMember){
        2, "Lumina", "Starlight", "Prism Nova",
        SPR_FAMILIAR_LUMINA, 72, 72, 40, 40, 17, 7,
        LOCK_STAR, LOCK_STAR, 9, false, true
    };
    st->active_slot = 1;
    st->potions = 3;
}

static void snap_cam_to_hero(HearthState *st) {
    uint16_t w, h; uint8_t lim;
    get_ppu_dims(st, &w, &h, &lim);
    int16_t max_x = (int16_t)(HEARTH_MAP_W * HEARTH_TILE_SIZE - (int)w);
    int16_t max_y = (int16_t)(HEARTH_MAP_H * HEARTH_TILE_SIZE - (int)h);
    int16_t tx = iclamp16((int16_t)(FROM_FP(st->hero_fx) - (int16_t)(w >> 1)), 0, max_x);
    int16_t ty = iclamp16((int16_t)(FROM_FP(st->hero_fy) - (int16_t)(h >> 1)), 0, max_y);
    st->cam_fx = TO_FP(tx);
    st->cam_fy = TO_FP(ty);
}

void hearth_respawn_enemies(HearthState *st) {
    st->field_enemies[0] = (HearthFieldEnemy){
        "Gloomcap", SPR_ENEMY_GLOOMCAP, ENCOUNTER_MEADOW_PACK, BIOME_MEADOW, false, true,
        TO_FP(26 * 16 + 8), TO_FP(15 * 16 + 8), TO_FP(26 * 16 + 8), TO_FP(15 * 16 + 8),
        0, 0, DIR_LEFT, false, 0, 30, 0
    };
    st->field_enemies[1] = (HearthFieldEnemy){
        "Thornling", SPR_ENEMY_THORNLING, ENCOUNTER_GLADE_DUO, BIOME_MEADOW, false, true,
        TO_FP(30 * 16 + 8), TO_FP(10 * 16 + 8), TO_FP(30 * 16 + 8), TO_FP(10 * 16 + 8),
        0, 0, DIR_DOWN, false, 0, 55, 0
    };
    st->field_enemies[2] = (HearthFieldEnemy){
        "Dusk Moth", SPR_ENEMY_DUSK_MOTH, ENCOUNTER_GLADE_DUO, BIOME_MEADOW, false, true,
        TO_FP(31 * 16 + 8), TO_FP(20 * 16 + 8), TO_FP(31 * 16 + 8), TO_FP(20 * 16 + 8),
        0, 0, DIR_LEFT, false, 0, 80, 0
    };
    st->field_enemies[3] = (HearthFieldEnemy){
        "Briar Warden", SPR_BOSS_BRIAR, ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, true, true,
        TO_FP(38 * 16 + 8), TO_FP(13 * 16 + 8), TO_FP(38 * 16 + 8), TO_FP(13 * 16 + 8),
        0, 0, DIR_LEFT, false, 0, 0, 0
    };
}

void hearth_init(HearthState *st) {
    memset(st, 0, sizeof(*st));
    hearth_default_tuning(&st->tuning);
    hearth_prng_seed(&st->prng, 0x5EEDu);
    hearth_build_sandbox_map(st->map_bg1, st->map_bg2, st->map_col);
    init_party(st);

    st->mode = MODE_TITLE;
    st->hero_fx = TO_FP(10 * HEARTH_TILE_SIZE + 8);
    st->hero_fy = TO_FP(14 * HEARTH_TILE_SIZE + 8);
    st->hero_dir = DIR_DOWN;

    for (int i = 0; i < HEARTH_TRAIL_LEN; i++) {
        st->trail_x[i] = st->hero_fx - TO_FP(18);
        st->trail_y[i] = st->hero_fy - TO_FP(4);
    }
    st->pip_fx = st->hero_fx - TO_FP(18);
    st->pip_fy = st->hero_fy - TO_FP(4);
    st->pip_dir = DIR_RIGHT;

    st->npcs[0] = (HearthNpc){ 0, "Elder Wren", SPR_NPC_ELDER, TO_FP(8*16+8),  TO_FP(11*16+8), DIR_DOWN, AURA_NONE, 0, 0 };
    st->npcs[1] = (HearthNpc){ 1, "Clara",      SPR_NPC_CLARA, TO_FP(13*16+8), TO_FP(16*16+8), DIR_DOWN, AURA_RADIANT_COURAGE, 0, 0 };
    st->npcs[2] = (HearthNpc){ 2, "Guard Bram", SPR_NPC_GUARD, TO_FP(22*16+8), TO_FP(13*16+8), DIR_LEFT, AURA_HOLLOW_FEAR, 0, 0 };

    hearth_respawn_enemies(st);
    snap_cam_to_hero(st);
}

void hearth_heal_party(HearthState *st) {
    for (int i = 0; i < 3; i++) {
        st->party[i].hp = st->party[i].max_hp;
        st->party[i].mp = st->party[i].max_mp;
    }
    push_sfx(st, SFX_HEAL);
    spawn_burst(st, 5, FROM_FP(st->hero_fx), (int16_t)(FROM_FP(st->hero_fy) - 10), 12, 0, 4, false);
    spawn_ftext(st, FROM_FP(st->hero_fx), (int16_t)(FROM_FP(st->hero_fy) - 24), "FULL RESTORE!", 0, 4, true);
}

void hearth_toggle_ppu_mode(HearthState *st) {
    st->tuning.ppu_mode = (st->tuning.ppu_mode == PPU_GENESIS_320x224)
        ? PPU_SNES_256x224 : PPU_GENESIS_320x224;
    snap_cam_to_hero(st);
}

void hearth_start_sandbox(HearthState *st) {
    st->mode = MODE_EXPLORE;
    st->cam_override = false;
    st->dialogue.active = false;
    st->battle.active = false;
    snap_cam_to_hero(st);
    push_sfx(st, SFX_CONFIRM);
}

void hearth_start_cutscene(HearthState *st, const HearthCutsceneOp *script) {
    st->mode = MODE_CUTSCENE;
    st->battle.active = false;
    st->dialogue.active = false;
    st->cs_script = script;
    st->cs_ip = 0;
    st->cs_wait_frames = 0;
}

/* ============================================================================
 * COLLISION & FLUID 8-WAY CORNER-NUDGING PHYSICS
 * ============================================================================ */
static uint8_t get_col_at_px(const HearthState *st, int16_t px, int16_t py) {
    int16_t tx = (int16_t)(px / HEARTH_TILE_SIZE);
    int16_t ty = (int16_t)(py / HEARTH_TILE_SIZE);
    if (tx < 0 || ty < 0 || tx >= HEARTH_MAP_W || ty >= HEARTH_MAP_H) return COL_SOLID;
    return st->map_col[ty * HEARTH_MAP_W + tx];
}

static bool collides_world(const HearthState *st, fp16_t fx, fp16_t fy) {
    int16_t cx = FROM_FP(fx);
    int16_t cy = FROM_FP(fy);
    static const int8_t off[4][2] = { {-5,-3}, {5,-3}, {-5,3}, {5,3} };
    for (int i = 0; i < 4; i++) {
        uint8_t c = get_col_at_px(st, (int16_t)(cx + off[i][0]), (int16_t)(cy + off[i][1]));
        if (c == COL_SOLID || c == COL_WATER) return true;
    }
    for (int i = 0; i < HEARTH_MAX_NPCS; i++) {
        if (iabs16((int16_t)(cx - FROM_FP(st->npcs[i].fx))) < 11 &&
            iabs16((int16_t)(cy - FROM_FP(st->npcs[i].fy))) < 9) {
            return true;
        }
    }
    /* Waystone (8,17) & Chest (26,6) */
    if (iabs16((int16_t)(cx - (8 * 16 + 8))) < 11 && iabs16((int16_t)(cy - (17 * 16 + 8))) < 9) return true;
    if (iabs16((int16_t)(cx - (26 * 16 + 8))) < 10 && iabs16((int16_t)(cy - (6 * 16 + 8))) < 8) return true;
    return false;
}

static void step_hero_movement(HearthState *st, uint16_t pad) {
    int16_t ix = 0, iy = 0;
    if (pad & PAD_LEFT)  ix -= 1;
    if (pad & PAD_RIGHT) ix += 1;
    if (pad & PAD_UP)    iy -= 1;
    if (pad & PAD_DOWN)  iy += 1;

    st->hero_dashing = ((pad & PAD_B) != 0) && (ix != 0 || iy != 0);
    fp16_t max_spd = st->hero_dashing ? st->tuning.dash_speed_fp : st->tuning.walk_speed_fp;

    fp16_t tvx = (fp16_t)ix * max_spd;
    fp16_t tvy = (fp16_t)iy * max_spd;
    if (ix != 0 && iy != 0) {
        /* 181/256 ≈ 0.7071 integer diagonal normalization */
        tvx = (tvx * 181) >> 8;
        tvy = (tvy * 181) >> 8;
    }

    st->hero_vx = approach_fp(st->hero_vx, tvx, st->tuning.accel_fp);
    st->hero_vy = approach_fp(st->hero_vy, tvy, st->tuning.accel_fp);

    if (ix != 0 || iy != 0) {
        if (iabs16(ix) >= iabs16(iy) && ix != 0) st->hero_dir = (ix < 0) ? DIR_LEFT : DIR_RIGHT;
        else if (iy != 0) st->hero_dir = (iy < 0) ? DIR_UP : DIR_DOWN;
    }

    /* Axis-separated collision with 5px automatic corner-nudging! */
    if (st->hero_vx != 0) {
        fp16_t nfx = st->hero_fx + st->hero_vx;
        if (!collides_world(st, nfx, st->hero_fy)) {
            st->hero_fx = nfx;
        } else if (!collides_world(st, nfx, st->hero_fy - TO_FP(5))) {
            st->hero_fy -= 192;
        } else if (!collides_world(st, nfx, st->hero_fy + TO_FP(5))) {
            st->hero_fy += 192;
        } else {
            st->hero_vx = 0;
        }
    }
    if (st->hero_vy != 0) {
        fp16_t nfy = st->hero_fy + st->hero_vy;
        if (!collides_world(st, st->hero_fx, nfy)) {
            st->hero_fy = nfy;
        } else if (!collides_world(st, st->hero_fx - TO_FP(5), nfy)) {
            st->hero_fx -= 192;
        } else if (!collides_world(st, st->hero_fx + TO_FP(5), nfy)) {
            st->hero_fx += 192;
        } else {
            st->hero_vy = 0;
        }
    }

    bool moving = (iabs32(st->hero_vx) > 24 || iabs32(st->hero_vy) > 24);
    if (moving) {
        st->hero_anim_tick = (uint16_t)(st->hero_anim_tick + (st->hero_dashing ? 2 : 1));
        st->hero_step_timer++;

        int16_t dx = FROM_FP(st->hero_fx - st->trail_x[st->trail_head]);
        int16_t dy = FROM_FP(st->hero_fy - st->trail_y[st->trail_head]);
        if (dx * dx + dy * dy >= 4) {
            st->trail_head = (uint8_t)((st->trail_head + 1) % HEARTH_TRAIL_LEN);
            st->trail_x[st->trail_head] = st->hero_fx;
            st->trail_y[st->trail_head] = st->hero_fy;
        }

        uint8_t cadence = st->hero_dashing ? 10 : 16;
        if (st->hero_step_timer >= cadence) {
            st->hero_step_timer = 0;
            int16_t hx = FROM_FP(st->hero_fx);
            int16_t hy = FROM_FP(st->hero_fy);
            uint8_t foot = get_col_at_px(st, hx, hy);
            if (foot == COL_TALL_GRASS) {
                push_sfx(st, SFX_GRASS_RUSTLE);
                spawn_burst(st, 1, hx, hy, 4, 0, 4, false);
            } else if (foot == COL_SHALLOW) {
                push_sfx(st, SFX_WATER_SPLASH);
                spawn_burst(st, 2, hx, hy, 4, 0, 14, false);
            } else {
                push_sfx(st, st->hero_dashing ? SFX_DASH : SFX_STEP);
                if (st->hero_dashing) spawn_burst(st, 0, hx, hy, 2, 0, 8, false);
            }
        }
    } else {
        st->hero_anim_tick = 0;
        st->hero_step_timer = 10;
    }
}

static void step_pip_follower(HearthState *st) {
    int back = (int)(st->tuning.follow_dist_px / 2);
    if (back < 4) back = 4;
    if (back > HEARTH_TRAIL_LEN - 2) back = HEARTH_TRAIL_LEN - 2;
    int idx = (st->trail_head - back + HEARTH_TRAIL_LEN) % HEARTH_TRAIL_LEN;

    fp16_t dx = st->trail_x[idx] - st->pip_fx;
    fp16_t dy = (st->trail_y[idx] - TO_FP(4)) - st->pip_fy;
    st->pip_fx += dx / 7;
    st->pip_fy += dy / 7;
    if (iabs32(dx) > 40) st->pip_dir = (dx < 0) ? DIR_LEFT : DIR_RIGHT;
}

/* ============================================================================
 * TACTILE FAMILIAR COMBAT ENGINE (TIMED HITS, PARRIES, LOCK BREAKS, SWAP ARC)
 * ============================================================================ */
static uint8_t first_alive_enemy(const HearthBattleState *b) {
    for (uint8_t i = 0; i < b->enemy_count; i++) {
        if (b->enemies[i].alive) return i;
    }
    return 0;
}

void hearth_start_battle(HearthState *st, uint8_t preset, uint8_t biome, int8_t field_idx) {
    uint16_t w, h; uint8_t lim;
    get_ppu_dims(st, &w, &h, &lim);

    int16_t right_base = (w == 256) ? 188 : 236;
    int16_t left_base  = (w == 256) ? 68  : 84;

    HearthBattleState *b = &st->battle;
    memset(b, 0, sizeof(*b));
    b->active = true;
    b->biome = biome;
    b->phase = BPHASE_INTRO;
    b->active_slot = st->active_slot;
    b->prev_slot = st->active_slot;
    b->swap_arc_fp = FP_ONE;
    b->hero_home_x = left_base;
    b->hero_home_y = 126;
    b->hero_x = (int16_t)(left_base - 54);
    b->hero_y = 126;
    b->hero_scale_x_fp = FP_ONE;
    b->hero_scale_y_fp = FP_ONE;
    b->wizard_back_x = (int16_t)((left_base > 46) ? (left_base - 42) : 24);
    b->wizard_back_y = 130;
    b->field_enemy_idx = field_idx;
    b->is_boss_battle = (preset == ENCOUNTER_SHRINE_BOSS);

    if (field_idx < 0) {
        /* Quick Battle Arena launch restores party cleanly */
        for (int i = 0; i < 3; i++) {
            st->party[i].hp = st->party[i].max_hp;
            st->party[i].mp = st->party[i].max_mp;
        }
        if (st->potions < 2) st->potions = 2;
    }

    if (preset == ENCOUNTER_SHRINE_BOSS) {
        b->enemy_count = 1;
        b->enemies[0] = (HearthBattleEnemy){
            "Briar Warden", "Bramble Tempest", SPR_BOSS_BRIAR, field_idx, true, true, false,
            165, 165, 21, 5, right_base, 126, (int16_t)(right_base + 56), 126,
            FP_ONE, FP_ONE, 0, 0, 2, 2, 3, { LOCK_SWORD, LOCK_SUN, LOCK_STAR }, { false, false, false }, 0
        };
        copy_str_safe(b->banner, sizeof(b->banner), "SHRINE GUARDIAN: BRIAR WARDEN!");
    } else if (preset == ENCOUNTER_GLADE_DUO) {
        b->enemy_count = 2;
        b->enemies[0] = (HearthBattleEnemy){
            "Thornling", "Briar Flurry", SPR_ENEMY_THORNLING, field_idx, false, true, false,
            58, 58, 14, 3, (int16_t)(right_base - 14), 108, (int16_t)(right_base + 56), 108,
            FP_ONE, FP_ONE, 0, 0, 2, 2, 2, { LOCK_SWORD, LOCK_SUN, LOCK_NONE }, { false, false, false }, 0
        };
        b->enemies[1] = (HearthBattleEnemy){
            "Dusk Moth", "Hollow Dust", SPR_ENEMY_DUSK_MOTH, field_idx, false, true, false,
            48, 48, 13, 2, (int16_t)(right_base + 16), 142, (int16_t)(right_base + 76), 142,
            FP_ONE, FP_ONE, 0, 0, 1, 2, 1, { LOCK_STAR, LOCK_NONE, LOCK_NONE }, { false, false, false }, 0
        };
        copy_str_safe(b->banner, sizeof(b->banner), "GLADE ENCOUNTER!");
    } else {
        b->enemy_count = 2;
        b->enemies[0] = (HearthBattleEnemy){
            "Gloomcap", "Spore Burst", SPR_ENEMY_GLOOMCAP, field_idx, false, true, false,
            52, 52, 13, 2, (int16_t)(right_base - 12), 112, (int16_t)(right_base + 52), 112,
            FP_ONE, FP_ONE, 0, 0, 2, 2, 2, { LOCK_SUN, LOCK_STAR, LOCK_NONE }, { false, false, false }, 0
        };
        b->enemies[1] = (HearthBattleEnemy){
            "Thornling", "Briar Flurry", SPR_ENEMY_THORNLING, field_idx, false, true, false,
            54, 54, 14, 3, (int16_t)(right_base + 14), 142, (int16_t)(right_base + 74), 142,
            FP_ONE, FP_ONE, 0, 0, 0, 2, 0, { LOCK_NONE, LOCK_NONE, LOCK_NONE }, { false, false, false }, 0
        };
        copy_str_safe(b->banner, sizeof(b->banner), "MEADOW ENCOUNTER!");
    }

    b->banner_frames = 60;
    st->dialogue.active = false;
    st->mode = MODE_BATTLE;
    push_sfx(st, SFX_BATTLE_SWOOP);
}

static void begin_enemy_turn(HearthBattleState *b) {
    b->enemy_turn_idx = 0;
    b->phase = BPHASE_ENEMY_SELECT;
    b->phase_timer = 0;
    b->ring.active = false;
}

static void resolve_player_strike(HearthState *st, bool is_followup) {
    HearthBattleState *b = &st->battle;
    HearthPartyMember *actor = &st->party[b->active_slot];
    bool is_aoe = (b->active_action == 1 && actor->skill_is_aoe);
    uint8_t strike_kind = (b->active_action == 1 && actor->skill_kind != LOCK_NONE)
        ? actor->skill_kind : actor->basic_kind;

    bool is_perfect = (b->timed_result == 1);
    bool is_good    = (b->timed_result == 2);

    if (is_followup) {
        b->combos++;
        actor->mp = (int16_t)((actor->mp + 4 > actor->max_mp) ? actor->max_mp : actor->mp + 4);
        push_sfx(st, SFX_HIT_CRITICAL);
        set_hitstop(st, st->tuning.hitstop_crit_f);
        add_shake(st, 4, 10);
        st->flash_level = 6;
        spawn_ftext(st, b->hero_home_x, (int16_t)(b->hero_home_y - 34), "+4 MP", 3, 15, false);
    } else if (is_perfect) {
        push_sfx(st, SFX_HIT_CRITICAL);
        set_hitstop(st, st->tuning.hitstop_crit_f);
        add_shake(st, 3, 8);
        st->flash_level = 4;
        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 30), "PERFECT!", 3, 11, false);
    } else {
        push_sfx(st, SFX_HIT_NORMAL);
        set_hitstop(st, st->tuning.hitstop_normal_f);
        add_shake(st, 2, 5);
    }

    for (uint8_t i = 0; i < b->enemy_count; i++) {
        if (!is_aoe && i != b->selected_enemy) continue;
        HearthBattleEnemy *e = &b->enemies[i];
        if (!e->alive) continue;

        /* Pure integer damage formula (scaled by /100) */
        int32_t mult = (b->active_action == 1) ? 145 : 100;
        if (is_followup) mult = (mult * 55) / 100;
        else if (is_good) mult = (mult * 115) / 100;
        if (e->staggered) mult = (mult * 125) / 100;

        int16_t raw = (int16_t)((actor->atk * mult) / 100 - e->def + hearth_prng_range(&st->prng, -1, 2));
        int16_t dmg = (raw < 4) ? 4 : raw;
        e->hp = (e->hp > dmg) ? (int16_t)(e->hp - dmg) : 0;
        e->flash_frames = 8;
        e->hurt_frames = 14;
        e->scale_x_fp = 325;
        e->scale_y_fp = 195;
        e->x = (int16_t)(e->home_x + 6);

        spawn_burst(st, 3, e->home_x, (int16_t)(e->home_y - 12), (is_perfect || is_followup) ? 12 : 7,
                    3, (is_perfect || is_followup) ? 11 : 12, true);

        char dmg_txt[16];
        fmt_int_text(dmg_txt, sizeof(dmg_txt), "", dmg, is_followup ? "!" : "");
        spawn_ftext(st, (int16_t)(e->home_x + hearth_prng_range(&st->prng, -5, 5)),
                    (int16_t)(e->home_y - (is_followup ? 34 : 24)),
                    dmg_txt, 3, (is_followup || is_perfect) ? 11 : 12, false);

        /* Spell Lock Shattering! */
        if (e->charge_turns > 0 && e->lock_count > 0 && e->hp > 0) {
            int match = -1;
            for (uint8_t li = 0; li < e->lock_count; li++) {
                if (!e->broken_locks[li] && e->locks[li] == strike_kind) {
                    match = (int)li;
                    break;
                }
            }
            if (match >= 0) {
                e->broken_locks[match] = true;
                b->locks_broken++;
                spawn_burst(st, 4, e->home_x, (int16_t)(e->home_y - 34), 10, 0, 4, true);

                bool all_broken = true;
                for (uint8_t li = 0; li < e->lock_count; li++) {
                    if (!e->broken_locks[li]) { all_broken = false; break; }
                }
                if (all_broken) {
                    e->charge_turns = 0;
                    e->lock_count = 0;
                    e->staggered = true;
                    push_sfx(st, SFX_LOCK_ALL_BREAK);
                    set_hitstop(st, (uint8_t)(st->tuning.hitstop_crit_f + 2));
                    add_shake(st, 5, 12);
                    spawn_ftext(st, e->home_x, (int16_t)(e->home_y - 44), "SHATTERED!", 0, 4, false);
                } else {
                    push_sfx(st, SFX_LOCK_BREAK);
                    spawn_ftext(st, e->home_x, (int16_t)(e->home_y - 40), "LOCK BREAK!", 3, 15, false);
                }
            }
        }

        if (e->hp <= 0) {
            e->alive = false;
            spawn_burst(st, 4, e->home_x, (int16_t)(e->home_y - 12), 14, 3, 4, true);
        }
    }
}

static void resolve_enemy_hit(HearthState *st, HearthBattleEnemy *e) {
    HearthBattleState *b = &st->battle;
    HearthPartyMember *actor = &st->party[b->active_slot];
    bool is_special = (e->charge_turns == 1);
    bool is_parried = (b->timed_result == 1);

    int32_t base = (e->atk * (is_special ? 155 : 100)) / 100 - (actor->def * 6) / 10;
    int16_t dmg = (int16_t)(base + hearth_prng_range(&st->prng, -1, 2));
    if (dmg < 3) dmg = 3;
    if (b->hero_guarding) dmg = (int16_t)((dmg * 55) / 100);
    if (is_parried) {
        dmg = (int16_t)((dmg * 45) / 100);
        if (dmg < 1) dmg = 1;
        b->parries++;
        actor->mp = (int16_t)((actor->mp + 3 > actor->max_mp) ? actor->max_mp : actor->mp + 3);
        push_sfx(st, SFX_PARRY_CLANG);
        set_hitstop(st, st->tuning.hitstop_parry_f);
        add_shake(st, 2, 6);
        spawn_burst(st, 3, b->hero_home_x, (int16_t)(b->hero_home_y - 12), 12, 3, 15, true);
        spawn_ftext(st, b->hero_home_x, (int16_t)(b->hero_home_y - 34), "GUARD!", 3, 15, false);
    } else {
        push_sfx(st, SFX_HURT);
        set_hitstop(st, st->tuning.hitstop_normal_f);
        add_shake(st, is_special ? 5 : 3, 9);
        spawn_burst(st, 3, b->hero_home_x, (int16_t)(b->hero_home_y - 12), 10, 3, 10, true);
    }

    char txt[16];
    fmt_int_text(txt, sizeof(txt), "-", dmg, "");
    spawn_ftext(st, (int16_t)(b->hero_home_x + 10), (int16_t)(b->hero_home_y - 22),
                txt, 3, is_parried ? 15 : 10, false);

    actor->hp = (actor->hp > dmg) ? (int16_t)(actor->hp - dmg) : 0;
    b->hero_flash_frames = 8;
    b->hero_scale_x_fp = 310;
    b->hero_scale_y_fp = 205;
}

static void step_battle(HearthState *st, uint16_t just_pressed) {
    HearthBattleState *b = &st->battle;
    if (!b->active) return;

    if (b->banner_frames > 0) b->banner_frames--;
    if (b->hero_flash_frames > 0) b->hero_flash_frames--;
    b->hero_scale_x_fp = (int16_t)(b->hero_scale_x_fp + ((FP_ONE - b->hero_scale_x_fp) >> 2));
    b->hero_scale_y_fp = (int16_t)(b->hero_scale_y_fp + ((FP_ONE - b->hero_scale_y_fp) >> 2));

    for (uint8_t i = 0; i < b->enemy_count; i++) {
        HearthBattleEnemy *e = &b->enemies[i];
        if (e->flash_frames > 0) e->flash_frames--;
        if (e->hurt_frames > 0) e->hurt_frames--;
        e->scale_x_fp = (int16_t)(e->scale_x_fp + ((FP_ONE - e->scale_x_fp) >> 2));
        e->scale_y_fp = (int16_t)(e->scale_y_fp + ((FP_ONE - e->scale_y_fp) >> 2));
        if (e->alive) e->anim_frame++;
    }

    if (just_pressed & PAD_A) b->timed_buffer = 6;
    else if (b->timed_buffer > 0) b->timed_buffer--;

    HearthPartyMember *actor = &st->party[b->active_slot];
    b->phase_timer++;

    switch (b->phase) {
        case BPHASE_INTRO: {
            const uint16_t dur = 24;
            uint16_t t = (b->phase_timer > dur) ? dur : b->phase_timer;
            int16_t rem = (int16_t)(dur - t);
            b->hero_x = (int16_t)(b->hero_home_x - (54 * rem) / (int16_t)dur);
            b->hero_y = (int16_t)(b->hero_home_y - sin_arc(t, dur, 16));
            for (uint8_t i = 0; i < b->enemy_count; i++) {
                b->enemies[i].x = (int16_t)(b->enemies[i].home_x + (56 * rem) / (int16_t)dur);
                b->enemies[i].y = (int16_t)(b->enemies[i].home_y - sin_arc(t, dur, 12));
            }
            if (b->phase_timer >= dur) {
                b->hero_x = b->hero_home_x;
                b->hero_y = b->hero_home_y;
                b->hero_scale_x_fp = 320;
                b->hero_scale_y_fp = 200;
                for (uint8_t i = 0; i < b->enemy_count; i++) {
                    b->enemies[i].x = b->enemies[i].home_x;
                    b->enemies[i].y = b->enemies[i].home_y;
                }
                b->phase = BPHASE_COMMAND;
                b->phase_timer = 0;
                b->selected_enemy = first_alive_enemy(b);
            }
            break;
        }

        case BPHASE_COMMAND: {
            if (!b->enemies[b->selected_enemy].alive) b->selected_enemy = first_alive_enemy(b);

            /* Free Ni No Kuni Familiar Hot-Swap with L / R! */
            if (just_pressed & (PAD_L | PAD_R)) {
                uint8_t next = (just_pressed & PAD_L)
                    ? (uint8_t)((b->active_slot + 2) % 3)
                    : (uint8_t)((b->active_slot + 1) % 3);
                if (st->party[next].hp > 0 && next != b->active_slot) {
                    b->prev_slot = b->active_slot;
                    b->active_slot = next;
                    st->active_slot = next;
                    b->phase = BPHASE_SWAP_ARC;
                    b->phase_timer = 0;
                    b->swap_arc_fp = 0;
                    b->hero_guarding = false;
                    push_sfx(st, SFX_SUMMON_ARC);
                    spawn_burst(st, 3, (int16_t)(b->wizard_back_x + 6), (int16_t)(b->wizard_back_y - 12), 10, 3, 11, true);
                    break;
                }
            }

            if (just_pressed & PAD_LEFT)  { b->selected_cmd = (uint8_t)((b->selected_cmd + 3) % 4); push_sfx(st, SFX_CURSOR); }
            if (just_pressed & PAD_RIGHT) { b->selected_cmd = (uint8_t)((b->selected_cmd + 1) % 4); push_sfx(st, SFX_CURSOR); }
            if ((just_pressed & (PAD_UP | PAD_DOWN)) && b->enemy_count > 1) {
                uint8_t other = (uint8_t)(1 - b->selected_enemy);
                if (b->enemies[other].alive) { b->selected_enemy = other; push_sfx(st, SFX_CURSOR); }
            }

            if (just_pressed & PAD_A) {
                if (b->selected_cmd == 0) {
                    b->active_action = 0;
                    b->hero_guarding = false;
                    b->phase = BPHASE_PLAYER_WINDUP;
                    b->phase_timer = 0;
                    b->timed_result = 0;
                    b->timed_buffer = 0;
                    push_sfx(st, SFX_CONFIRM);
                } else if (b->selected_cmd == 1) {
                    if (actor->mp < actor->skill_cost) {
                        push_sfx(st, SFX_CANCEL);
                        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 26), "LOW MP!", 3, 10, false);
                    } else {
                        actor->mp = (int16_t)(actor->mp - actor->skill_cost);
                        b->active_action = 1;
                        b->hero_guarding = false;
                        b->phase = BPHASE_PLAYER_WINDUP;
                        b->phase_timer = 0;
                        b->timed_result = 0;
                        b->timed_buffer = 0;
                        push_sfx(st, SFX_SPELL_CAST);
                        copy_str_safe(b->banner, sizeof(b->banner), actor->skill_name);
                        b->banner_frames = 44;
                    }
                } else if (b->selected_cmd == 2) {
                    b->hero_guarding = true;
                    actor->mp = (int16_t)((actor->mp + 6 > actor->max_mp) ? actor->max_mp : actor->mp + 6);
                    push_sfx(st, SFX_PARRY_CLANG);
                    spawn_burst(st, 3, b->hero_x, (int16_t)(b->hero_y - 12), 8, 3, 15, true);
                    spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 28), "GUARD +6 MP", 3, 15, false);
                    begin_enemy_turn(b);
                } else {
                    if (st->potions == 0) {
                        push_sfx(st, SFX_CANCEL);
                        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 26), "NO TONICS!", 3, 10, false);
                    } else {
                        st->potions--;
                        for (int i = 0; i < 3; i++) {
                            st->party[i].hp = (int16_t)((st->party[i].hp + 45 > st->party[i].max_hp) ? st->party[i].max_hp : st->party[i].hp + 45);
                            st->party[i].mp = (int16_t)((st->party[i].mp + 10 > st->party[i].max_mp) ? st->party[i].max_mp : st->party[i].mp + 10);
                        }
                        push_sfx(st, SFX_HEAL);
                        spawn_burst(st, 5, b->hero_x, (int16_t)(b->hero_y - 10), 12, 0, 4, true);
                        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 28), "+45 HP / +10 MP", 0, 4, false);
                        begin_enemy_turn(b);
                    }
                }
            }
            break;
        }

        case BPHASE_SWAP_ARC: {
            const uint16_t dur = 16;
            b->swap_arc_fp = (uint16_t)((b->phase_timer * FP_ONE) / dur);
            if ((b->phase_timer & 1u) == 0u) {
                int16_t ax = (int16_t)(b->wizard_back_x + ((b->hero_home_x - b->wizard_back_x) * (int16_t)b->phase_timer) / (int16_t)dur);
                int16_t ay = (int16_t)(b->hero_home_y - sin_arc(b->phase_timer, dur, 32));
                spawn_particle(st, 3, ax, (int16_t)(ay - 10), 0, -40, 14, 2, 3, 15, true);
            }
            if (b->phase_timer >= dur) {
                b->hero_x = b->hero_home_x;
                b->hero_y = b->hero_home_y;
                b->hero_scale_x_fp = 345;
                b->hero_scale_y_fp = 185;
                b->hero_flash_frames = 4;
                spawn_burst(st, 3, b->hero_x, (int16_t)(b->hero_y - 4), 8, 3, 12, true);
                spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 28), actor->name, 3, 11, false);
                b->phase = BPHASE_COMMAND;
                b->phase_timer = 0;
            }
            break;
        }

        case BPHASE_PLAYER_WINDUP: {
            const uint16_t dur = 12;
            b->hero_x = (int16_t)(b->hero_home_x - sin_arc(b->phase_timer, dur, 8));
            b->hero_scale_x_fp = 290;
            b->hero_scale_y_fp = 215;
            if (b->phase_timer >= dur) {
                if (b->active_action == 1 && actor->skill_is_heal) {
                    for (int i = 0; i < 3; i++) {
                        st->party[i].hp = (int16_t)((st->party[i].hp + 38 > st->party[i].max_hp) ? st->party[i].max_hp : st->party[i].hp + 38);
                    }
                    push_sfx(st, SFX_HEAL);
                    spawn_burst(st, 5, b->hero_home_x, (int16_t)(b->hero_home_y - 10), 14, 0, 4, true);
                    spawn_ftext(st, b->hero_home_x, (int16_t)(b->hero_home_y - 30), "+38 HP!", 0, 4, false);
                    b->hero_x = b->hero_home_x;
                    begin_enemy_turn(b);
                } else {
                    b->phase = BPHASE_PLAYER_LUNGE;
                    b->phase_timer = 0;
                    b->timed_result = 0;
                    b->timed_buffer = 0;
                }
            }
            break;
        }

        case BPHASE_PLAYER_LUNGE: {
            HearthBattleEnemy *e = &b->enemies[b->selected_enemy];
            const uint16_t dur = 18;
            int16_t dest_x = (actor->slot == 2)
                ? (int16_t)((b->hero_home_x + e->home_x) / 2 - 12)
                : (int16_t)(e->home_x - 24);
            int16_t dest_y = (actor->slot == 2) ? (int16_t)(b->hero_home_y - 6) : e->home_y;

            b->hero_x = (int16_t)(b->hero_home_x + ((dest_x - b->hero_home_x) * (int16_t)b->phase_timer) / (int16_t)dur);
            b->hero_y = (int16_t)(b->hero_home_y + ((dest_y - b->hero_home_y) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, 14));
            b->hero_scale_x_fp = 230;
            b->hero_scale_y_fp = 282;

            uint16_t win = (st->tuning.timed_hit_window_f < 4) ? 4 : st->tuning.timed_hit_window_f;
            uint16_t sweet_start = (dur > win) ? (uint16_t)(dur - win) : 1;

            b->ring.active = true;
            b->ring.is_parry = false;
            b->ring.x = e->home_x;
            b->ring.y = (int16_t)(e->home_y - 14);
            b->ring.progress_fp = (uint16_t)((b->phase_timer * FP_ONE) / dur);
            b->ring.sweet_start_fp = (uint16_t)((sweet_start * FP_ONE) / dur);
            b->ring.result = b->timed_result;

            if (b->timed_result == 0 && b->timed_buffer > 0) {
                if (b->phase_timer >= sweet_start) {
                    b->timed_result = 1;
                    b->ring.result = 1;
                    b->timed_buffer = 0;
                } else if (b->phase_timer + 4 >= sweet_start) {
                    b->timed_result = 2;
                    b->ring.result = 2;
                    b->timed_buffer = 0;
                }
            }

            if (b->phase_timer >= dur) {
                b->phase = BPHASE_PLAYER_IMPACT;
                b->phase_timer = 0;
                resolve_player_strike(st, false);
            }
            break;
        }

        case BPHASE_PLAYER_IMPACT: {
            uint16_t late = (uint16_t)((st->tuning.timed_hit_window_f >> 1) + 2);
            if (b->phase_timer <= late && b->timed_result == 0 && b->timed_buffer > 0) {
                b->timed_result = 1;
                b->timed_buffer = 0;
                b->phase = BPHASE_PLAYER_FOLLOWUP;
                b->phase_timer = 0;
                push_sfx(st, SFX_HIT_CRITICAL);
                spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 32), "PERFECT!", 3, 11, false);
                break;
            }
            if (b->phase_timer >= 10) {
                b->ring.active = false;
                bool any_alive = false;
                for (uint8_t i = 0; i < b->enemy_count; i++) if (b->enemies[i].alive) any_alive = true;
                if (b->timed_result == 1 && any_alive) {
                    b->phase = BPHASE_PLAYER_FOLLOWUP;
                    b->phase_timer = 0;
                } else {
                    b->phase = BPHASE_PLAYER_RETURN;
                    b->phase_timer = 0;
                }
            }
            break;
        }

        case BPHASE_PLAYER_FOLLOWUP: {
            uint8_t ti = b->enemies[b->selected_enemy].alive ? b->selected_enemy : first_alive_enemy(b);
            HearthBattleEnemy *e = &b->enemies[ti];
            const uint16_t dur = 16;
            int16_t base_x = (actor->slot == 2) ? (int16_t)(b->hero_home_x + 36) : (int16_t)(e->home_x - 24);
            b->hero_x = (int16_t)(base_x - sin_arc(b->phase_timer, dur, 14));
            b->hero_y = (int16_t)(e->home_y - sin_arc(b->phase_timer, dur, 26));
            b->hero_scale_x_fp = 310;
            b->hero_scale_y_fp = 210;

            if (b->phase_timer == 10) {
                b->selected_enemy = ti;
                resolve_player_strike(st, true);
            }
            if (b->phase_timer >= dur) {
                b->phase = BPHASE_PLAYER_RETURN;
                b->phase_timer = 0;
            }
            break;
        }

        case BPHASE_PLAYER_RETURN: {
            const uint16_t dur = 12;
            b->hero_x = (int16_t)(b->hero_x + ((b->hero_home_x - b->hero_x) * (int16_t)b->phase_timer) / (int16_t)dur);
            b->hero_y = (int16_t)(b->hero_y + ((b->hero_home_y - b->hero_y) * (int16_t)b->phase_timer) / (int16_t)dur);
            if (b->phase_timer >= dur) {
                b->hero_x = b->hero_home_x;
                b->hero_y = b->hero_home_y;
                b->ring.active = false;

                bool all_dead = true;
                for (uint8_t i = 0; i < b->enemy_count; i++) if (b->enemies[i].alive) all_dead = false;
                if (all_dead) {
                    b->exp_earned = b->is_boss_battle ? 120 : 48;
                    b->phase = BPHASE_VICTORY_POSE;
                    b->phase_timer = 0;
                    push_sfx(st, SFX_VICTORY_FANFARE);
                    copy_str_safe(b->banner, sizeof(b->banner), "VICTORY!");
                    b->banner_frames = 90;
                } else {
                    begin_enemy_turn(b);
                }
            }
            break;
        }

        case BPHASE_ENEMY_SELECT: {
            while (b->enemy_turn_idx < b->enemy_count && !b->enemies[b->enemy_turn_idx].alive) {
                b->enemy_turn_idx++;
            }
            if (b->enemy_turn_idx >= b->enemy_count) {
                b->phase = BPHASE_COMMAND;
                b->phase_timer = 0;
                b->hero_guarding = false;
                for (uint8_t i = 0; i < b->enemy_count; i++) {
                    HearthBattleEnemy *e = &b->enemies[i];
                    if (!e->alive) continue;
                    if (e->staggered) {
                        e->staggered = false;
                    } else if (e->charge_turns == 0 && hearth_prng_range(&st->prng, 0, 2) == 0) {
                        e->charge_turns = e->max_charge_turns;
                        if (e->is_boss) {
                            e->lock_count = 3;
                            e->locks[0] = LOCK_SWORD; e->locks[1] = LOCK_SUN; e->locks[2] = LOCK_STAR;
                        } else {
                            e->lock_count = 2;
                            e->locks[0] = LOCK_SWORD; e->locks[1] = LOCK_SUN;
                        }
                        e->broken_locks[0] = e->broken_locks[1] = e->broken_locks[2] = false;
                    }
                }
                break;
            }
            HearthBattleEnemy *e = &b->enemies[b->enemy_turn_idx];
            if (e->staggered) {
                spawn_ftext(st, e->home_x, (int16_t)(e->home_y - 26), "STAGGERED!", 3, 11, false);
                b->enemy_turn_idx++;
                b->phase_timer = 0;
                break;
            }
            if (e->charge_turns > 1) {
                e->charge_turns--;
                spawn_ftext(st, e->home_x, (int16_t)(e->home_y - 28), "CHARGING...", 3, 4, false);
                spawn_burst(st, 3, e->home_x, (int16_t)(e->home_y - 12), 5, 3, 4, true);
                b->enemy_turn_idx++;
                b->phase_timer = 0;
                break;
            }
            b->phase = BPHASE_ENEMY_TELEGRAPH;
            b->phase_timer = 0;
            b->timed_result = 0;
            b->timed_buffer = 0;
            if (e->charge_turns == 1) {
                copy_str_safe(b->banner, sizeof(b->banner), e->special_name);
                b->banner_frames = 44;
            }
            break;
        }

        case BPHASE_ENEMY_TELEGRAPH: {
            HearthBattleEnemy *e = &b->enemies[b->enemy_turn_idx];
            const uint16_t dur = 20;
            e->x = (int16_t)(e->home_x + sin_arc(b->phase_timer, dur, 9));
            e->scale_x_fp = 290;
            e->scale_y_fp = 215;
            if (b->phase_timer == 8) {
                spawn_burst(st, 3, (int16_t)(e->x - 6), (int16_t)(e->y - 14), 5, 3, 10, true);
            }
            if (b->phase_timer >= dur) {
                b->phase = BPHASE_ENEMY_LUNGE;
                b->phase_timer = 0;
                b->timed_result = 0;
                b->timed_buffer = 0;
            }
            break;
        }

        case BPHASE_ENEMY_LUNGE: {
            HearthBattleEnemy *e = &b->enemies[b->enemy_turn_idx];
            const uint16_t dur = 16;
            int16_t dest_x = (int16_t)(b->hero_home_x + 24);
            e->x = (int16_t)(e->home_x + ((dest_x - e->home_x) * (int16_t)b->phase_timer) / (int16_t)dur);
            e->y = (int16_t)(e->home_y + ((b->hero_home_y - e->home_y) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, 10));

            uint16_t win = (st->tuning.parry_window_f < 4) ? 4 : st->tuning.parry_window_f;
            uint16_t sweet_start = (dur > win) ? (uint16_t)(dur - win) : 1;

            b->ring.active = true;
            b->ring.is_parry = true;
            b->ring.x = b->hero_home_x;
            b->ring.y = (int16_t)(b->hero_home_y - 14);
            b->ring.progress_fp = (uint16_t)((b->phase_timer * FP_ONE) / dur);
            b->ring.sweet_start_fp = (uint16_t)((sweet_start * FP_ONE) / dur);
            b->ring.result = b->timed_result;

            if (b->timed_result == 0 && b->timed_buffer > 0 && b->phase_timer >= sweet_start) {
                b->timed_result = 1;
                b->ring.result = 1;
                b->timed_buffer = 0;
            }

            if (b->phase_timer >= dur) {
                b->phase = BPHASE_ENEMY_IMPACT;
                b->phase_timer = 0;
                resolve_enemy_hit(st, e);
            }
            break;
        }

        case BPHASE_ENEMY_IMPACT: {
            if (b->phase_timer >= 12) {
                b->ring.active = false;
                b->phase = BPHASE_ENEMY_RETURN;
                b->phase_timer = 0;
            }
            break;
        }

        case BPHASE_ENEMY_RETURN: {
            HearthBattleEnemy *e = &b->enemies[b->enemy_turn_idx];
            const uint16_t dur = 12;
            e->x = (int16_t)(e->x + ((e->home_x - e->x) * (int16_t)b->phase_timer) / (int16_t)dur);
            e->y = (int16_t)(e->y + ((e->home_y - e->y) * (int16_t)b->phase_timer) / (int16_t)dur);
            if (b->phase_timer >= dur) {
                e->x = e->home_x;
                e->y = e->home_y;
                if (e->charge_turns == 1) { e->charge_turns = 0; e->lock_count = 0; }

                if (actor->hp <= 0) {
                    int healthy = -1;
                    for (int i = 0; i < 3; i++) if (st->party[i].hp > 0) { healthy = i; break; }
                    if (healthy >= 0) {
                        b->active_slot = (uint8_t)healthy;
                        st->active_slot = (uint8_t)healthy;
                        push_sfx(st, SFX_SUMMON_ARC);
                    } else {
                        b->phase = BPHASE_DEFEAT_POSE;
                        b->phase_timer = 0;
                        break;
                    }
                }
                b->enemy_turn_idx++;
                b->phase = BPHASE_ENEMY_SELECT;
                b->phase_timer = 0;
            }
            break;
        }

        case BPHASE_VICTORY_POSE: {
            if ((b->phase_timer % 18u) == 0u) {
                b->hero_scale_x_fp = 225;
                b->hero_scale_y_fp = 295;
            }
            if (b->phase_timer >= 48) {
                if (b->field_enemy_idx >= 0 && b->field_enemy_idx < HEARTH_MAX_FIELD_ENEMIES) {
                    st->field_enemies[b->field_enemy_idx].alive = false;
                }
                if (b->is_boss_battle) st->flag_boss_defeated = true;
                st->mode = MODE_BATTLE_VICTORY;
            }
            break;
        }

        case BPHASE_DEFEAT_POSE: {
            if (b->phase_timer >= 44) {
                for (int i = 0; i < 3; i++) {
                    st->party[i].hp = st->party[i].max_hp;
                    st->party[i].mp = st->party[i].max_mp;
                }
                st->hero_fx = TO_FP(8 * 16 + 8);
                st->hero_fy = TO_FP(18 * 16 + 8);
                snap_cam_to_hero(st);
                st->mode = MODE_BATTLE_DEFEAT;
            }
            break;
        }
    }
}

/* ============================================================================
 * DIALOGUE, INTERACTION & BYTECODE CUTSCENE VM
 * ============================================================================ */
static void open_dialogue(
    HearthState *st, const char *speaker, uint8_t voice, const char *text,
    uint8_t choice_count, const HearthChoice *choices
) {
    HearthDialogue *d = &st->dialogue;
    d->active = true;
    d->speaker = speaker;
    d->full_text = text;
    d->full_len = (uint16_t)strlen(text);
    d->visible_chars = 0;
    d->finished = false;
    d->pause_frames = 0;
    d->voice_sfx = voice;
    d->choice_count = choice_count;
    d->selected_choice = 0;
    for (uint8_t i = 0; i < choice_count && i < HEARTH_MAX_CHOICES; i++) {
        d->choices[i] = choices[i];
    }
}

static void confirm_dialogue_choice(HearthState *st) {
    uint8_t act = st->dialogue.choices[st->dialogue.selected_choice].action_id;
    st->dialogue.active = false;
    push_sfx(st, SFX_CONFIRM);
    if (act == ACT_TAKE_COURAGE) hearth_start_cutscene(st, CUTSCENE_TAKE_COURAGE);
    else if (act == ACT_GIVE_COURAGE) hearth_start_cutscene(st, CUTSCENE_GIVE_COURAGE);
    else if (act == ACT_REPLAY_INTRO) hearth_start_cutscene(st, CUTSCENE_INTRO_DEMO);
    else if (act == ACT_JUMP_BOSS) hearth_start_battle(st, ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, -1);
}

static void step_dialogue(HearthState *st, uint16_t just_pressed) {
    HearthDialogue *d = &st->dialogue;
    if (!d->active) return;
    if (!d->finished) {
        if (just_pressed & (PAD_A | PAD_B)) {
            d->visible_chars = d->full_len;
            d->finished = true;
            return;
        }
        if (d->pause_frames > 0) { d->pause_frames--; return; }
        if (d->visible_chars < d->full_len) {
            d->visible_chars++;
            char ch = d->full_text[d->visible_chars - 1];
            if (ch == ',') d->pause_frames = 4;
            else if (ch == '.' || ch == '!' || ch == '?') d->pause_frames = 8;
            else if ((d->visible_chars % 3u) == 0u && ch != ' ') push_sfx(st, d->voice_sfx);
        }
        if (d->visible_chars >= d->full_len) d->finished = true;
    } else {
        if (d->choice_count > 0) {
            if (just_pressed & (PAD_UP | PAD_LEFT)) {
                d->selected_choice = (uint8_t)((d->selected_choice + d->choice_count - 1) % d->choice_count);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & (PAD_DOWN | PAD_RIGHT)) {
                d->selected_choice = (uint8_t)((d->selected_choice + 1) % d->choice_count);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & PAD_A) {
                confirm_dialogue_choice(st);
            } else if (just_pressed & PAD_B) {
                d->active = false;
                push_sfx(st, SFX_CANCEL);
            }
        } else if (just_pressed & (PAD_A | PAD_B)) {
            d->active = false;
            push_sfx(st, SFX_CONFIRM);
        }
    }
}

static void get_actor_px(const HearthState *st, int16_t actor_id, int16_t *ox, int16_t *oy) {
    if (actor_id == ACTOR_HERO) { *ox = FROM_FP(st->hero_fx); *oy = FROM_FP(st->hero_fy); return; }
    if (actor_id == ACTOR_PIP)  { *ox = FROM_FP(st->pip_fx);  *oy = FROM_FP(st->pip_fy);  return; }
    int idx = actor_id - ACTOR_ELDER;
    if (idx >= 0 && idx < HEARTH_MAX_NPCS) {
        *ox = FROM_FP(st->npcs[idx].fx);
        *oy = FROM_FP(st->npcs[idx].fy);
        return;
    }
    *ox = FROM_FP(st->hero_fx); *oy = FROM_FP(st->hero_fy);
}

static void step_cutscene(HearthState *st, uint16_t just_pressed) {
    if (st->heart_orb.active) {
        st->heart_orb.frame++;
        if (st->heart_orb.frame >= st->heart_orb.duration) {
            st->heart_orb.active = false;
            spawn_burst(st, 3, st->heart_orb.end_x, (int16_t)(st->heart_orb.end_y - 8), 14, 3, 11, false);
            st->flash_level = 6;
        }
    }
    if (st->dialogue.active) { step_dialogue(st, just_pressed); return; }
    if (st->cs_wait_frames > 0) { st->cs_wait_frames--; return; }
    if (!st->cs_script) { st->mode = MODE_EXPLORE; return; }

    for (;;) {
        const HearthCutsceneOp *op = &st->cs_script[st->cs_ip++];
        switch (op->op) {
            case OP_WAIT:
                st->cs_wait_frames = (uint16_t)op->a0;
                return;
            case OP_PAN_CAM: {
                uint16_t w, h; uint8_t lim;
                get_ppu_dims(st, &w, &h, &lim);
                int16_t mx = (int16_t)(HEARTH_MAP_W * 16 - (int)w);
                int16_t my = (int16_t)(HEARTH_MAP_H * 16 - (int)h);
                st->cam_override = true;
                st->cam_start_fx = st->cam_fx;
                st->cam_start_fy = st->cam_fy;
                st->cam_target_fx = TO_FP(iclamp16((int16_t)(op->a0 * 16 - (int16_t)(w >> 1)), 0, mx));
                st->cam_target_fy = TO_FP(iclamp16((int16_t)(op->a1 * 16 - (int16_t)(h >> 1)), 0, my));
                st->cam_pan_frame = 0;
                st->cam_pan_dur = op->a2 ? op->a2 : 24;
                st->cs_wait_frames = st->cam_pan_dur;
                return;
            }
            case OP_CAM_FOLLOW: {
                uint16_t w, h; uint8_t lim;
                get_ppu_dims(st, &w, &h, &lim);
                int16_t mx = (int16_t)(HEARTH_MAP_W * 16 - (int)w);
                int16_t my = (int16_t)(HEARTH_MAP_H * 16 - (int)h);
                st->cam_override = true;
                st->cam_start_fx = st->cam_fx;
                st->cam_start_fy = st->cam_fy;
                st->cam_target_fx = TO_FP(iclamp16((int16_t)(FROM_FP(st->hero_fx) - (int16_t)(w >> 1)), 0, mx));
                st->cam_target_fy = TO_FP(iclamp16((int16_t)(FROM_FP(st->hero_fy) - (int16_t)(h >> 1)), 0, my));
                st->cam_pan_frame = 0;
                st->cam_pan_dur = (op->a0 > 0) ? (uint8_t)op->a0 : 24;
                st->cs_wait_frames = st->cam_pan_dur;
                return;
            }
            case OP_FACE_ACTOR:
                if (op->a0 == ACTOR_HERO) st->hero_dir = (uint8_t)op->a1;
                else if (op->a0 == ACTOR_PIP) st->pip_dir = (uint8_t)op->a1;
                else if (op->a0 >= ACTOR_ELDER && op->a0 < ACTOR_ELDER + HEARTH_MAX_NPCS) {
                    st->npcs[op->a0 - ACTOR_ELDER].dir = (uint8_t)op->a1;
                }
                break;
            case OP_EMOTE:
                if (op->a0 == ACTOR_HERO) { st->hero_emote = (uint8_t)op->a1; st->hero_emote_frames = op->a2; }
                else if (op->a0 == ACTOR_PIP) { st->pip_emote = (uint8_t)op->a1; st->pip_emote_frames = op->a2; }
                else if (op->a0 >= ACTOR_ELDER && op->a0 < ACTOR_ELDER + HEARTH_MAX_NPCS) {
                    st->npcs[op->a0 - ACTOR_ELDER].emote = (uint8_t)op->a1;
                    st->npcs[op->a0 - ACTOR_ELDER].emote_frames = op->a2;
                }
                st->cs_wait_frames = 16;
                return;
            case OP_SFX:
                push_sfx(st, (uint8_t)op->a0);
                break;
            case OP_SHAKE:
                add_shake(st, (uint8_t)op->a0, (uint8_t)op->a1);
                break;
            case OP_HEART_ORB: {
                int16_t sx, sy, ex, ey;
                get_actor_px(st, op->a0, &sx, &sy);
                get_actor_px(st, op->a1, &ex, &ey);
                st->heart_orb.active = true;
                st->heart_orb.start_x = sx;
                st->heart_orb.start_y = (int16_t)(sy - 12);
                st->heart_orb.end_x = ex;
                st->heart_orb.end_y = (int16_t)(ey - 12);
                st->heart_orb.frame = 0;
                st->heart_orb.duration = op->a2 ? op->a2 : 40;
                st->cs_wait_frames = st->heart_orb.duration;
                return;
            }
            case OP_SET_FLAG:
                if (op->a0 == FLAG_HAS_COURAGE) {
                    st->flag_has_courage = true;
                    st->npcs[1].aura = AURA_MENDED;
                } else if (op->a0 == FLAG_GUARD_MENDED) {
                    st->flag_guard_mended = true;
                    st->flag_has_courage = false;
                    st->potions++;
                    st->npcs[2].aura = AURA_MENDED;
                    spawn_ftext(st, FROM_FP(st->npcs[2].fx), (int16_t)(FROM_FP(st->npcs[2].fy) - 24), "HEART MENDED!", 3, 11, true);
                } else if (op->a0 == FLAG_ELDER_SEEN) {
                    st->flag_elder_seen = true;
                }
                break;
            case OP_SAY:
                open_dialogue(st, op->speaker ? op->speaker : "Pip", (uint8_t)op->a0, op->text ? op->text : "", 0, NULL);
                return;
            case OP_END:
            default:
                st->cam_override = false;
                st->mode = MODE_EXPLORE;
                return;
        }
    }
}

/* ============================================================================
 * ROAMING ENEMIES & WORLD INTERACTABLES
 * ============================================================================ */
static void step_field_enemies(HearthState *st, bool can_engage) {
    int16_t hx = FROM_FP(st->hero_fx);
    int16_t hy = FROM_FP(st->hero_fy);
    for (int i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++) {
        HearthFieldEnemy *e = &st->field_enemies[i];
        if (!e->alive) continue;
        e->anim_frame++;
        if (e->alert_frames > 0) e->alert_frames--;

        int16_t ex = FROM_FP(e->fx);
        int16_t ey = FROM_FP(e->fy);
        int32_t dx = hx - ex;
        int32_t dy = hy - ey;
        int32_t dist2 = dx * dx + dy * dy;
        int32_t sight = e->is_boss ? 52 : 66;

        if (can_engage && st->hero_safe_frames == 0 && dist2 < sight * sight) {
            if (!e->alerted) {
                e->alerted = true;
                e->alert_frames = 28;
                push_sfx(st, SFX_ENEMY_ALERT);
            }
            int16_t spd = e->is_boss ? 140 : 200;
            fp16_t nvx = (dx > 2) ? spd : ((dx < -2) ? -spd : 0);
            fp16_t nvy = (dy > 2) ? spd : ((dy < -2) ? -spd : 0);
            if (nvx != 0 && nvy != 0) { nvx = (nvx * 181) >> 8; nvy = (nvy * 181) >> 8; }
            uint8_t cx = get_col_at_px(st, FROM_FP(e->fx + nvx), ey);
            uint8_t cy = get_col_at_px(st, ex, FROM_FP(e->fy + nvy));
            if (cx != COL_SOLID && cx != COL_WATER) e->fx += nvx;
            if (cy != COL_SOLID && cy != COL_WATER) e->fy += nvy;
            e->dir = (iabs32(dx) > iabs32(dy)) ? ((dx < 0) ? DIR_LEFT : DIR_RIGHT) : ((dy < 0) ? DIR_UP : DIR_DOWN);

            if (dist2 < 15 * 15) {
                hearth_start_battle(st, e->preset, e->biome, (int8_t)i);
                return;
            }
        } else {
            e->alerted = false;
            if (!e->is_boss) {
                if (e->patrol_timer > 0) e->patrol_timer--;
                else {
                    e->patrol_timer = (uint8_t)hearth_prng_range(&st->prng, 50, 105);
                    int16_t hdx = FROM_FP(e->home_fx - e->fx);
                    int16_t hdy = FROM_FP(e->home_fy - e->fy);
                    if (hdx * hdx + hdy * hdy > 32 * 32) {
                        e->vx = (hdx > 0) ? 85 : -85;
                        e->vy = (hdy > 0) ? 85 : -85;
                    } else {
                        int16_t pick = hearth_prng_range(&st->prng, 0, 4);
                        e->vx = (pick == 1) ? 80 : ((pick == 2) ? -80 : 0);
                        e->vy = (pick == 3) ? 80 : ((pick == 4) ? -80 : 0);
                    }
                }
                uint8_t c = get_col_at_px(st, FROM_FP(e->fx + e->vx), FROM_FP(e->fy + e->vy));
                if (c != COL_SOLID && c != COL_WATER) { e->fx += e->vx; e->fy += e->vy; }
                else { e->vx = 0; e->vy = 0; }
            }
        }
    }
}

static int find_interactable(const HearthState *st, int16_t *ox, int16_t *oy, const char **olabel) {
    int16_t hx = FROM_FP(st->hero_fx);
    int16_t hy = FROM_FP(st->hero_fy);
    int16_t px = hx, py = hy;
    if (st->hero_dir == DIR_UP) py -= 15;
    else if (st->hero_dir == DIR_DOWN) py += 15;
    else if (st->hero_dir == DIR_LEFT) px -= 15;
    else px += 15;

    for (int i = 0; i < HEARTH_MAX_NPCS; i++) {
        int16_t nx = FROM_FP(st->npcs[i].fx);
        int16_t ny = FROM_FP(st->npcs[i].fy);
        int32_t d1 = (int32_t)(px - nx) * (px - nx) + (int32_t)(py - ny) * (py - ny);
        int32_t d2 = (int32_t)(hx - nx) * (hx - nx) + (int32_t)(hy - ny) * (hy - ny);
        if (d1 < 19 * 19 || d2 < 22 * 22) {
            *ox = nx; *oy = (int16_t)(ny - 24); *olabel = st->npcs[i].name;
            return i; /* 0=elder, 1=clara, 2=guard */
        }
    }
    int16_t wx = 8 * 16 + 8, wy = 17 * 16 + 8;
    if ((int32_t)(hx - wx) * (hx - wx) + (int32_t)(hy - wy) * (hy - wy) < 22 * 22) {
        *ox = wx; *oy = (int16_t)(wy - 22); *olabel = "Waystone";
        return 10;
    }
    int16_t chx = 26 * 16 + 8, chy = 6 * 16 + 8;
    if (!st->flag_chest_opened && (int32_t)(hx - chx) * (hx - chx) + (int32_t)(hy - chy) * (hy - chy) < 22 * 22) {
        *ox = chx; *oy = (int16_t)(chy - 18); *olabel = "Chest";
        return 11;
    }
    int16_t sx = 16 * 16 + 8, sy = 15 * 16 + 8;
    if ((int32_t)(hx - sx) * (hx - sx) + (int32_t)(hy - sy) * (hy - sy) < 21 * 21) {
        *ox = sx; *oy = (int16_t)(sy - 18); *olabel = "Read Sign";
        return 12;
    }
    return -1;
}

static void try_interact(HearthState *st) {
    int16_t ox = 0, oy = 0; const char *lbl = NULL;
    int hit = find_interactable(st, &ox, &oy, &lbl);
    if (hit < 0) return;

    if (hit == 10) {
        hearth_heal_party(st);
        open_dialogue(st, "Waystone", SFX_TEXT_PIP,
            "Warm spring light fills Rowan, Bramblekit, and Lumina! Full HP & MP restored.", 0, NULL);
    } else if (hit == 11) {
        st->flag_chest_opened = true;
        st->potions += 2;
        push_sfx(st, SFX_CHEST_OPEN);
        spawn_burst(st, 3, ox, (int16_t)(oy + 10), 12, 3, 11, false);
        spawn_ftext(st, ox, oy, "+2 STAR TONICS!", 3, 11, true);
        open_dialogue(st, "Treasure Chest", SFX_TEXT_HERO,
            "Found 2 Star Tonics! Use them in battle to restore party HP and MP.", 0, NULL);
    } else if (hit == 12) {
        push_sfx(st, SFX_CONFIRM);
        open_dialogue(st, "Hamlet Signpost", SFX_TEXT_ELDER,
            "WEST: Windmill Hamlet | EAST: Timber Bridge, Whispering Meadow & Briar Shrine | SOUTH: Pebble Ford.", 0, NULL);
    } else if (hit == 0) {
        if (!st->flag_elder_seen) {
            hearth_start_cutscene(st, CUTSCENE_ELDER_TALK);
        } else {
            static const HearthChoice opts[3] = {
                { "Replay Cutscene Demo", ACT_REPLAY_INTRO },
                { "Fight Shrine Boss",    ACT_JUMP_BOSS },
                { "Keep Exploring",       ACT_CLOSE }
            };
            open_dialogue(st, "Elder Wren", SFX_TEXT_ELDER,
                "Press Q/E (L/R) in combat to hot-swap Familiars for free, and tap Space/Z on impact for a double follow-up!",
                3, opts);
        }
    } else if (hit == 1) {
        if (!st->flag_has_courage && !st->flag_guard_mended) {
            static const HearthChoice opts[2] = {
                { "Cast Take Heart (Courage)", ACT_TAKE_COURAGE },
                { "Maybe Later",               ACT_CLOSE }
            };
            open_dialogue(st, "Clara", SFX_TEXT_PIP,
                "My heart is brimming with Courage! Want a spark in your locket to help Guard Bram by the bridge?",
                2, opts);
        } else {
            open_dialogue(st, "Clara", SFX_TEXT_PIP,
                st->flag_guard_mended
                    ? "Guard Bram looks sturdy as oak again! Sharing courage lifts two hearts at once."
                    : "You already carry Courage in your locket—bring it east to Guard Bram!", 0, NULL);
        }
    } else if (hit == 2) {
        if (st->flag_guard_mended) {
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "My knees stopped shaking! Head east into the Stone Shrine and show the Briar Warden what you can do!", 0, NULL);
        } else if (st->flag_has_courage) {
            static const HearthChoice opts[2] = {
                { "Cast Give Heart (Courage)", ACT_GIVE_COURAGE },
                { "Hold On",                   ACT_CLOSE }
            };
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "B-brr... the mist from the Shrine chills my bones. Wait—is that golden Courage glowing in your locket?",
                2, opts);
        } else {
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "My heart feels hollow with dread. You can still walk past me into the Meadow, or ask Clara for a spark of Courage!", 0, NULL);
        }
    }
}

/* ============================================================================
 * CAMERA & PPU FRAME PACKET BUILDER
 * ============================================================================ */
static void step_camera(HearthState *st) {
    uint16_t w, h; uint8_t lim;
    get_ppu_dims(st, &w, &h, &lim);
    if (st->cam_override) {
        if (st->cam_pan_frame < st->cam_pan_dur) {
            st->cam_pan_frame++;
            st->cam_fx = st->cam_start_fx + ((st->cam_target_fx - st->cam_start_fx) * st->cam_pan_frame) / st->cam_pan_dur;
            st->cam_fy = st->cam_start_fy + ((st->cam_target_fy - st->cam_start_fy) * st->cam_pan_frame) / st->cam_pan_dur;
        }
        return;
    }
    int16_t mx = (int16_t)(HEARTH_MAP_W * 16 - (int)w);
    int16_t my = (int16_t)(HEARTH_MAP_H * 16 - (int)h);
    fp16_t gx = TO_FP(iclamp16((int16_t)(FROM_FP(st->hero_fx) - (int16_t)(w >> 1)), 0, mx));
    fp16_t gy = TO_FP(iclamp16((int16_t)(FROM_FP(st->hero_fy) - (int16_t)(h >> 1)), 0, my));
    st->cam_fx += (gx - st->cam_fx) >> 3;
    st->cam_fy += (gy - st->cam_fy) >> 3;
}

static void push_oam(HearthPpuPacket *ppu, HearthOamSprite spr) {
    if (ppu->oam_count < HEARTH_MAX_OAM) {
        ppu->oam[ppu->oam_count++] = spr;
    }
}

static void build_ppu_packet(HearthState *st) {
    HearthPpuPacket *p = &st->ppu;
    get_ppu_dims(st, &p->width, &p->height, &p->oam_limit);
    p->frame_count = st->frame_count;
    p->mode = st->mode;
    p->cam_x = FROM_FP(st->cam_fx);
    p->cam_y = FROM_FP(st->cam_fy);
    p->shake_x = (st->shake_frames > 0) ? (int8_t)hearth_prng_range(&st->prng, -(int16_t)st->shake_intensity, (int16_t)st->shake_intensity) : 0;
    p->shake_y = (st->shake_frames > 0) ? (int8_t)hearth_prng_range(&st->prng, -(int16_t)st->shake_intensity, (int16_t)st->shake_intensity) : 0;
    p->flash_level = st->flash_level;
    p->hitstop_active = (st->hitstop_frames > 0);
    p->oam_count = 0;
    p->interact_active = false;

    if (st->mode != MODE_BATTLE && st->mode != MODE_BATTLE_VICTORY && st->mode != MODE_BATTLE_DEFEAT) {
        int16_t cx = p->cam_x, cy = p->cam_y;

        /* Windmill Blades (Overhead Layer 2) */
        push_oam(p, (HearthOamSprite){
            (int16_t)(6 * 16 + 8 - cx), (int16_t)(5 * 16 + 4 - cy),
            SPR_WINDMILL_BLADES, 0, DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 2000,
            (uint16_t)((st->frame_count * 7u / 10u) % 360u)
        });

        /* Waystone & Chest */
        push_oam(p, (HearthOamSprite){
            (int16_t)(8 * 16 + 8 - cx), (int16_t)(17 * 16 + 12 - cy),
            SPR_WAYSTONE, (uint8_t)((st->frame_count >> 4) & 3u), DIR_DOWN, OAM_FLAG_SHADOW,
            FP_ONE, FP_ONE, AURA_NONE, 1, 17 * 16 + 12, 0
        });
        push_oam(p, (HearthOamSprite){
            (int16_t)(26 * 16 + 8 - cx), (int16_t)(6 * 16 + 12 - cy),
            SPR_CHEST, st->flag_chest_opened ? 1 : 0, DIR_DOWN, OAM_FLAG_SHADOW,
            FP_ONE, FP_ONE, AURA_NONE, 1, 6 * 16 + 12, 0
        });

        /* NPCs */
        for (int i = 0; i < HEARTH_MAX_NPCS; i++) {
            const HearthNpc *n = &st->npcs[i];
            int16_t nx = FROM_FP(n->fx), ny = FROM_FP(n->fy);
            push_oam(p, (HearthOamSprite){
                (int16_t)(nx - cx), (int16_t)(ny - cy), n->sprite_id,
                (uint8_t)((st->frame_count >> 5) & 1u), n->dir, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, n->aura, 1, ny, 0
            });
            if (n->emote_frames > 0 && n->emote != 0) {
                push_oam(p, (HearthOamSprite){
                    (int16_t)(nx - cx), (int16_t)(ny - 25 - cy), n->emote,
                    0, DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 2500, 0
                });
            }
        }

        /* Roaming Field Enemies */
        for (int i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++) {
            const HearthFieldEnemy *e = &st->field_enemies[i];
            if (!e->alive) continue;
            int16_t ex = FROM_FP(e->fx), ey = FROM_FP(e->fy);
            uint8_t fl = OAM_FLAG_SHADOW | ((e->dir == DIR_RIGHT) ? OAM_FLAG_FLIP_X : 0);
            push_oam(p, (HearthOamSprite){
                (int16_t)(ex - cx), (int16_t)(ey - cy), e->sprite_id,
                (uint8_t)((e->anim_frame >> 4) & 1u), e->dir, fl,
                FP_ONE, FP_ONE, AURA_NONE, 1, ey, 0
            });
            if (e->alert_frames > 0) {
                push_oam(p, (HearthOamSprite){
                    (int16_t)(ex - cx), (int16_t)(ey - 26 - cy), SPR_EMOTE_BANG,
                    0, DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 2500, 0
                });
            }
        }

        /* Hero (Rowan) */
        int16_t hx = FROM_FP(st->hero_fx), hy = FROM_FP(st->hero_fy);
        uint8_t wf = (st->hero_anim_tick == 0) ? 0 : (uint8_t)((st->hero_anim_tick >> 3) & 3u);
        if (st->hero_safe_frames == 0 || (st->hero_safe_frames & 4u) == 0u) {
            push_oam(p, (HearthOamSprite){
                (int16_t)(hx - cx), (int16_t)(hy - cy), SPR_HERO, wf, st->hero_dir, OAM_FLAG_SHADOW,
                st->hero_dashing ? 270 : FP_ONE, st->hero_dashing ? 244 : FP_ONE,
                AURA_NONE, 1, hy, 0
            });
        }
        if (st->hero_emote_frames > 0 && st->hero_emote != 0) {
            push_oam(p, (HearthOamSprite){
                (int16_t)(hx - cx), (int16_t)(hy - 26 - cy), st->hero_emote,
                0, DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 2500, 0
            });
        }

        /* Pip Hovering Companion */
        int16_t px = FROM_FP(st->pip_fx), py = FROM_FP(st->pip_fy);
        int16_t bob = (int16_t)((HEARTH_SIN64[(st->frame_count >> 1) & 63u] * 3) / 127);
        uint8_t pfl = OAM_FLAG_SHADOW | ((st->pip_dir == DIR_LEFT) ? OAM_FLAG_FLIP_X : 0);
        push_oam(p, (HearthOamSprite){
            (int16_t)(px - cx), (int16_t)(py - 4 + bob - cy), SPR_PIP,
            (uint8_t)((st->frame_count >> 3) & 1u), st->pip_dir, pfl,
            FP_ONE, FP_ONE, AURA_NONE, 1, py, 0
        });
        if (st->pip_emote_frames > 0 && st->pip_emote != 0) {
            push_oam(p, (HearthOamSprite){
                (int16_t)(px - cx), (int16_t)(py - 24 - cy), st->pip_emote,
                0, DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 2500, 0
            });
        }

        /* Heart-Orb Transfer Sprite */
        if (st->heart_orb.active) {
            int16_t ox = (int16_t)(st->heart_orb.start_x + ((st->heart_orb.end_x - st->heart_orb.start_x) * st->heart_orb.frame) / st->heart_orb.duration);
            int16_t oy = (int16_t)(st->heart_orb.start_y + ((st->heart_orb.end_y - st->heart_orb.start_y) * st->heart_orb.frame) / st->heart_orb.duration - sin_arc(st->heart_orb.frame, st->heart_orb.duration, 32));
            push_oam(p, (HearthOamSprite){
                (int16_t)(ox - cx), (int16_t)(oy - cy), SPR_HEART_ORB,
                (uint8_t)((st->frame_count >> 2) & 1u), DIR_DOWN, 0,
                FP_ONE, FP_ONE, AURA_NONE, 2, 3000, 0
            });
        }

        /* Insertion sort OAM by layer then sort_y (fast & zero stack overhead on 68k/65816) */
        for (int i = 1; i < p->oam_count; i++) {
            HearthOamSprite key = p->oam[i];
            int j = i - 1;
            while (j >= 0 && (p->oam[j].layer > key.layer || (p->oam[j].layer == key.layer && p->oam[j].sort_y > key.sort_y))) {
                p->oam[j + 1] = p->oam[j];
                j--;
            }
            p->oam[j + 1] = key;
        }

        /* Interact Prompt */
        if (st->mode == MODE_EXPLORE && !st->dialogue.active) {
            int16_t ix = 0, iy = 0; const char *ilbl = NULL;
            if (find_interactable(st, &ix, &iy, &ilbl) >= 0) {
                p->interact_active = true;
                p->interact_x = (int16_t)(ix - cx);
                p->interact_y = (int16_t)(iy - cy);
                p->interact_label = ilbl;
            }
        }
    } else {
        /* Battle Arena OAM Packet (Screen-Space Coordinates) */
        const HearthBattleState *b = &st->battle;

        /* Rear Wizard Casting Stance when a Familiar is active */
        if (b->active_slot != 0) {
            push_oam(p, (HearthOamSprite){
                b->wizard_back_x, b->wizard_back_y, SPR_HERO,
                (uint8_t)((st->frame_count >> 5) & 1u), DIR_RIGHT, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, AURA_NONE, 1, b->wizard_back_y, 0
            });
        }

        /* Active Frontline Combatant (Hidden briefly mid-Soul-Arc swap) */
        if (b->phase != BPHASE_SWAP_ARC) {
            const HearthPartyMember *actor = &st->party[b->active_slot];
            uint8_t hflags = OAM_FLAG_SHADOW | ((b->hero_flash_frames > 0) ? OAM_FLAG_FLASH_WHITE : 0);
            push_oam(p, (HearthOamSprite){
                b->hero_x, b->hero_y, actor->sprite_id,
                (uint8_t)((st->frame_count >> 4) & 1u), DIR_RIGHT, hflags,
                b->hero_scale_x_fp, b->hero_scale_y_fp,
                b->hero_guarding ? AURA_RADIANT_COURAGE : AURA_NONE, 1, b->hero_y, 0
            });
        }

        /* Battle Enemies */
        for (uint8_t i = 0; i < b->enemy_count; i++) {
            const HearthBattleEnemy *e = &b->enemies[i];
            if (!e->alive) continue;
            uint8_t eflags = OAM_FLAG_SHADOW | ((e->flash_frames > 0) ? OAM_FLAG_FLASH_WHITE : 0);
            push_oam(p, (HearthOamSprite){
                e->x, e->y, e->sprite_id,
                (uint8_t)((e->anim_frame >> 4) & 1u), DIR_LEFT, eflags,
                e->scale_x_fp, e->scale_y_fp,
                e->staggered ? AURA_HOLLOW_FEAR : AURA_NONE, 1, e->y, 0
            });
        }

        /* Sort Battle OAM by Y so foreground overlaps cleanly */
        for (int i = 1; i < p->oam_count; i++) {
            HearthOamSprite key = p->oam[i];
            int j = i - 1;
            while (j >= 0 && p->oam[j].sort_y > key.sort_y) {
                p->oam[j + 1] = p->oam[j];
                j--;
            }
            p->oam[j + 1] = key;
        }
    }
}

/* ============================================================================
 * MASTER 60Hz ENTRY POINT
 * ============================================================================ */
const HearthPpuPacket *hearth_step_60hz(HearthState *st, uint16_t pad_bits) {
    st->ppu.sfx_count = 0;
    uint16_t just_pressed = (uint16_t)(pad_bits & (uint16_t)(~st->prev_pad));
    st->prev_pad = pad_bits;
    st->frame_count++;

    if (st->shake_frames > 0) st->shake_frames--;
    if (st->flash_level > 0) st->flash_level--;

    if (st->hitstop_frames > 0) {
        st->hitstop_frames--;
        build_ppu_packet(st);
        return &st->ppu;
    }

    step_fx_pools(st);

    if (st->hero_emote_frames > 0) st->hero_emote_frames--; else st->hero_emote = 0;
    if (st->pip_emote_frames > 0) st->pip_emote_frames--; else st->pip_emote = 0;
    for (int i = 0; i < HEARTH_MAX_NPCS; i++) {
        if (st->npcs[i].emote_frames > 0) st->npcs[i].emote_frames--;
        else st->npcs[i].emote = 0;
    }

    switch (st->mode) {
        case MODE_TITLE: {
            uint16_t w, h; uint8_t lim;
            get_ppu_dims(st, &w, &h, &lim);
            int16_t sx = (int16_t)(14 * 16 + (HEARTH_SIN64[(st->frame_count >> 3) & 63u] / 2));
            int16_t sy = (int16_t)(12 * 16 + (HEARTH_SIN64[((st->frame_count >> 3) + 16u) & 63u] / 5));
            st->cam_fx = TO_FP(iclamp16((int16_t)(sx - (int16_t)(w >> 1)), 0, (int16_t)(HEARTH_MAP_W * 16 - (int)w)));
            st->cam_fy = TO_FP(iclamp16((int16_t)(sy - (int16_t)(h >> 1)), 0, (int16_t)(HEARTH_MAP_H * 16 - (int)h)));
            step_field_enemies(st, false);

            if (just_pressed & (PAD_UP | PAD_LEFT)) {
                st->title_cursor = (uint8_t)((st->title_cursor + 2) % 3);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & (PAD_DOWN | PAD_RIGHT)) {
                st->title_cursor = (uint8_t)((st->title_cursor + 1) % 3);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & (PAD_A | PAD_START)) {
                if (st->title_cursor == 0) hearth_start_sandbox(st);
                else if (st->title_cursor == 1) hearth_start_battle(st, ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, -1);
                else hearth_start_cutscene(st, CUTSCENE_INTRO_DEMO);
            }
            break;
        }

        case MODE_EXPLORE: {
            if (st->hero_safe_frames > 0) st->hero_safe_frames--;
            if (st->dialogue.active) {
                step_dialogue(st, just_pressed);
            } else {
                step_hero_movement(st, pad_bits);
                step_pip_follower(st);
                step_field_enemies(st, true);
                if (just_pressed & PAD_A) try_interact(st);
                if (just_pressed & PAD_L) { st->active_slot = (uint8_t)((st->active_slot + 2) % 3); push_sfx(st, SFX_CURSOR); }
                if (just_pressed & PAD_R) { st->active_slot = (uint8_t)((st->active_slot + 1) % 3); push_sfx(st, SFX_CURSOR); }
            }
            step_camera(st);
            break;
        }

        case MODE_CUTSCENE:
            step_pip_follower(st);
            step_cutscene(st, just_pressed);
            step_camera(st);
            break;

        case MODE_BATTLE:
            step_battle(st, just_pressed);
            break;

        case MODE_BATTLE_VICTORY:
        case MODE_BATTLE_DEFEAT:
            if (just_pressed & (PAD_A | PAD_START)) {
                st->battle.active = false;
                st->mode = MODE_EXPLORE;
                st->hero_safe_frames = 90;
                push_sfx(st, SFX_CONFIRM);
            } else if (just_pressed & PAD_B) {
                hearth_start_battle(st, st->battle.is_boss_battle ? ENCOUNTER_SHRINE_BOSS : ENCOUNTER_MEADOW_PACK,
                                    st->battle.biome, -1);
            }
            break;
    }

    build_ppu_packet(st);
    return &st->ppu;
}
