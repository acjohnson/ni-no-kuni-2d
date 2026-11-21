/* ============================================================================
 * hearth_core.c — Project Hearth 60Hz Fixed-Point 16-Bit Simulation Core
 *
 * Strictly portable ANSI C89 / C99 (-std=c89 -pedantic AND -std=c99 -pedantic).
 * Zero floating-point math, zero malloc, zero OS/graphics headers.
 * Compiles cleanly for Linux/Windows/macOS (GCC/MinGW/Clang),
 * Sega Genesis (SGDK / m68k-gcc), and Super Nintendo (PVSnesLib / 816-tcc).
 * ============================================================================ */

#include "hearth_core.h"

/* Choice Action IDs */
enum {
    ACT_CLOSE        = 0,
    ACT_TAKE_COURAGE = 1,
    ACT_GIVE_COURAGE = 2,
    ACT_REPLAY_INTRO = 3,
    ACT_JUMP_BOSS    = 4
};

/* Forward declaration so hearth_init & hearth_toggle_ppu_mode always produce valid PPU state */
static void build_ppu_packet(HearthState *st);

/* On SNES (65816), direct static WRAM access avoids 24-bit far-pointer overhead (~6x faster).
 * All other targets dereference the passed HearthState* parameter identically. */
#if defined(HEARTH_TARGET_SNES)
#define HS           (g_hearth)
#define HP           (g_hearth.ppu)
#define OAM_AT(idx)  (g_hearth.ppu.oam[(idx)])
#define ST_UNUSED(s) ((void)(s))
#else
#define HS           (*st)
#define HP           (st->ppu)
#define OAM_AT(idx)  (p->oam[(idx)])
#define ST_UNUSED(s) ((void)0)
#endif

/* ============================================================================
 * INTERNAL INTEGER HELPERS
 * ============================================================================ */
static int16_t iclamp16(int16_t v, int16_t lo, int16_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static uint16_t str_len16(const char *s) {
    uint16_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0') n++;
    return n;
}

static int16_t sin_arc(uint16_t num, uint16_t den, int16_t peak) {
    uint16_t idx;
    if (den == 0u || num == 0u || num >= den) return 0;
    idx = (num < 2048u)
        ? (uint16_t)((uint16_t)(num << 5) / den)
        : (uint16_t)(((uint32_t)num * 32u) / (uint32_t)den);
    if (idx >= 32u) return 0;
    if (peak >= -255 && peak <= 255) {
        return (int16_t)(((int16_t)HEARTH_SIN64[idx] * peak) / 127);
    }
    return (int16_t)(((int32_t)HEARTH_SIN64[idx] * (int32_t)peak) / 127);
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

static void copy_str_safe(char *dst, uint16_t dst_cap, const char *src) {
    uint16_t i = 0;
    if (dst_cap == 0) return;
    if (src) {
        while ((uint16_t)(i + 1) < dst_cap && src[i] != '\0') {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

/* Format signed integer into tiny buffer without <stdio.h> */
static void fmt_int_text(char *buf, uint16_t cap, const char *prefix, int16_t val, const char *suffix) {
    char digits[12];
    int dlen = 0;
    int32_t v = val;
    bool neg = false;
    uint16_t pos = 0;

    if (v < 0) { neg = true; v = -v; }
    if (v == 0) {
        digits[dlen++] = '0';
    } else {
        while (v > 0 && dlen < 10) {
            digits[dlen++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
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
#if defined(HEARTH_TARGET_SNES)
    (void)st; (void)kind; (void)x; (void)y; (void)vx; (void)vy;
    (void)life; (void)size; (void)pal_idx; (void)col_idx; (void)screen_space;
#else
    int i;
    for (i = 0; i < HEARTH_MAX_PARTICLES; i++) {
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
            st->active_particles++;
            return;
        }
    }
#endif
}

static void spawn_burst(
    HearthState *st, uint8_t kind, int16_t x, int16_t y,
    int count, uint8_t pal_idx, uint8_t col_idx, bool screen_space
) {
#if defined(HEARTH_TARGET_SNES)
    (void)st; (void)kind; (void)x; (void)y; (void)count;
    (void)pal_idx; (void)col_idx; (void)screen_space;
#else
    int i;
    for (i = 0; i < count; i++) {
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
#endif
}

static void spawn_ftext(
    HearthState *st, int16_t x, int16_t y, const char *text,
    uint8_t pal_idx, uint8_t col_idx, bool world_space
) {
#if defined(HEARTH_TARGET_SNES)
    (void)st; (void)x; (void)y; (void)text;
    (void)pal_idx; (void)col_idx; (void)world_space;
#else
    int i;
    for (i = 0; i < HEARTH_MAX_FTEXT; i++) {
        HearthFloatText *ft = &st->ftexts[i];
        if (!ft->active) {
            ft->active = true;
            ft->x = x;
            ft->y = y;
            ft->vy_fp = -260;
            copy_str_safe(ft->text, (uint16_t)sizeof(ft->text), text);
            ft->pal_idx = pal_idx;
            ft->col_idx = col_idx;
            ft->life = 44;
            ft->world_space = world_space;
            st->active_ftexts++;
            return;
        }
    }
#endif
}

static void step_fx_pools(HearthState *st) {
#if defined(HEARTH_TARGET_SNES)
    (void)st;
#else
    int i;
    uint8_t rem;
    if (st->active_particles > 0) {
        HearthParticle *p = st->particles;
        rem = st->active_particles;
        for (i = 0; i < HEARTH_MAX_PARTICLES && rem > 0; i++, p++) {
            if (!p->active) continue;
            rem--;
            if (p->life <= 1) {
                p->active = false;
                if (st->active_particles > 0) st->active_particles--;
                continue;
            }
            p->life--;
            p->fx += p->vx;
            p->fy += p->vy;
            if (p->kind == 1 || p->kind == 4) {
                p->vy += 22;
            } else if (p->kind == 3) {
                p->vx -= (p->vx >> 4) + (p->vx >> 6);
                p->vy -= (p->vy >> 4) + (p->vy >> 6);
            }
        }
    }
    if (st->active_ftexts > 0) {
        HearthFloatText *ft = st->ftexts;
        rem = st->active_ftexts;
        for (i = 0; i < HEARTH_MAX_FTEXT && rem > 0; i++, ft++) {
            if (!ft->active) continue;
            rem--;
            if (ft->life <= 1) {
                ft->active = false;
                if (st->active_ftexts > 0) st->active_ftexts--;
                continue;
            }
            ft->life--;
            ft->y = (int16_t)(ft->y + FROM_FP(ft->vy_fp));
            ft->vy_fp = (int16_t)(ft->vy_fp - (ft->vy_fp >> 4) - (ft->vy_fp >> 5));
        }
    }
#endif
}

/* ============================================================================
 * INITIALIZATION & PARTY SETUP (C89-Clean Static Struct Templates)
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
    static const HearthPartyMember default_party[3] = {
        { 0, "Rowan",      "Locket Staff", "Mending Dew", SPR_HERO,             84, 84, 32, 32, 15,  8, LOCK_SWORD, LOCK_NONE, 7, true,  false },
        { 1, "Bramblekit", "Acorn Rush",   "Solar Slam",  SPR_FAMILIAR_BRAMBLE, 96, 96, 24, 24, 19, 11, LOCK_SWORD, LOCK_SUN,  6, false, false },
        { 2, "Lumina",     "Starlight",    "Prism Nova",  SPR_FAMILIAR_LUMINA,  72, 72, 40, 40, 17,  7, LOCK_STAR,  LOCK_STAR, 9, false, true  }
    };
    st->party[0] = default_party[0];
    st->party[1] = default_party[1];
    st->party[2] = default_party[2];
    st->active_slot = 1;
    st->potions = 3;
}

static void snap_cam_to_hero(HearthState *st) {
    uint16_t w, h;
    uint8_t lim;
    int16_t max_x, max_y, tx, ty;
    get_ppu_dims(st, &w, &h, &lim);
    max_x = (int16_t)(HEARTH_MAP_W * HEARTH_TILE_SIZE - (int)w);
    max_y = (int16_t)(HEARTH_MAP_H * HEARTH_TILE_SIZE - (int)h);
    tx = iclamp16((int16_t)(FP_PX(st->hero_fx) - (int16_t)(w >> 1)), 0, max_x);
    ty = iclamp16((int16_t)(FP_PX(st->hero_fy) - (int16_t)(h >> 1)), 0, max_y);
    st->cam_fx = TO_FP(tx);
    st->cam_fy = TO_FP(ty);
}

static void set_field_enemy(
    HearthFieldEnemy *e, const char *name, uint8_t sprite_id, uint8_t preset,
    uint8_t biome, bool is_boss, int16_t tx, int16_t ty, uint8_t dir, uint8_t timer
) {
    fp16_t fx = TO_FP(tx * 16 + 8);
    fp16_t fy = TO_FP(ty * 16 + 8);
    e->name = name;
    e->sprite_id = sprite_id;
    e->preset = preset;
    e->biome = biome;
    e->is_boss = is_boss;
    e->alive = true;
    e->home_fx = fx;
    e->home_fy = fy;
    e->fx = fx;
    e->fy = fy;
    e->vx = 0;
    e->vy = 0;
    e->dir = dir;
    e->alerted = false;
    e->alert_frames = 0;
    e->patrol_timer = timer;
    e->anim_frame = 0;
}

void hearth_respawn_enemies(HearthState *st) {
    set_field_enemy(&st->field_enemies[0], "Gloomcap",     SPR_ENEMY_GLOOMCAP,  ENCOUNTER_MEADOW_PACK, BIOME_MEADOW, false, 26, 15, DIR_LEFT, 30);
    set_field_enemy(&st->field_enemies[1], "Thornling",    SPR_ENEMY_THORNLING, ENCOUNTER_GLADE_DUO,   BIOME_MEADOW, false, 30, 10, DIR_DOWN, 55);
    set_field_enemy(&st->field_enemies[2], "Dusk Moth",    SPR_ENEMY_DUSK_MOTH, ENCOUNTER_GLADE_DUO,   BIOME_MEADOW, false, 31, 20, DIR_LEFT, 80);
    set_field_enemy(&st->field_enemies[3], "Briar Warden", SPR_BOSS_BRIAR,      ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, true,  38, 13, DIR_LEFT,  0);
}

static void set_npc(
    HearthNpc *n, uint8_t id, const char *name, uint8_t sprite_id,
    int16_t tx, int16_t ty, uint8_t dir, uint8_t aura
) {
    n->id = id;
    n->name = name;
    n->sprite_id = sprite_id;
    n->fx = TO_FP(tx * 16 + 8);
    n->fy = TO_FP(ty * 16 + 8);
    n->dir = dir;
    n->aura = aura;
    n->emote = 0;
    n->emote_frames = 0;
}

void hearth_init(HearthState *st) {
    int i;
    hearth_memset(st, 0, (uint16_t)sizeof(*st));
    hearth_default_tuning(&st->tuning);
    hearth_prng_seed(&st->prng, (uint16_t)0x5EEDu);
    hearth_build_sandbox_map(st->map_bg1, st->map_bg2, st->map_col);
    init_party(st);

    st->mode = MODE_TITLE;
    st->hero_fx = TO_FP(10 * HEARTH_TILE_SIZE + 8);
    st->hero_fy = TO_FP(14 * HEARTH_TILE_SIZE + 8);
    st->hero_dir = DIR_DOWN;

    for (i = 0; i < HEARTH_TRAIL_LEN; i++) {
        st->trail_x[i] = st->hero_fx - TO_FP(18);
        st->trail_y[i] = st->hero_fy - TO_FP(4);
    }
    st->pip_fx = st->hero_fx - TO_FP(18);
    st->pip_fy = st->hero_fy - TO_FP(4);
    st->pip_dir = DIR_RIGHT;

    set_npc(&st->npcs[0], 0, "Elder Wren", SPR_NPC_ELDER,  8, 11, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Clara",      SPR_NPC_CLARA, 13, 16, DIR_DOWN, AURA_RADIANT_COURAGE);
    set_npc(&st->npcs[2], 2, "Guard Bram", SPR_NPC_GUARD, 22, 13, DIR_LEFT, AURA_HOLLOW_FEAR);

    hearth_respawn_enemies(st);
    snap_cam_to_hero(st);
    build_ppu_packet(st);
}

void hearth_heal_party(HearthState *st) {
    int i;
    for (i = 0; i < 3; i++) {
        st->party[i].hp = st->party[i].max_hp;
        st->party[i].mp = st->party[i].max_mp;
    }
    push_sfx(st, SFX_HEAL);
    spawn_burst(st, 5, FROM_FP(st->hero_fx), (int16_t)(FROM_FP(st->hero_fy) - 10), 12, 0, 4, false);
    spawn_ftext(st, FROM_FP(st->hero_fx), (int16_t)(FROM_FP(st->hero_fy) - 24), "FULL RESTORE!", 0, 4, true);
}

void hearth_toggle_ppu_mode(HearthState *st) {
    st->tuning.ppu_mode = (st->tuning.ppu_mode == PPU_GENESIS_320x224)
        ? (uint8_t)PPU_SNES_256x224 : (uint8_t)PPU_GENESIS_320x224;
    snap_cam_to_hero(st);
    build_ppu_packet(st);
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
    uint16_t tx, ty;
    ST_UNUSED(st);
    if (px < 0 || py < 0) return COL_SOLID;
    tx = (uint16_t)px >> 4;
    ty = (uint16_t)py >> 4;
    if (tx >= HEARTH_MAP_W || ty >= HEARTH_MAP_H) return COL_SOLID;
    return HS.map_col[(ty << 5) + (ty << 3) + (ty << 2) + tx];
}

static bool collides_world_px(const HearthState *st, int16_t cx, int16_t cy) {
    uint16_t tx0, tx1, ty0, ty1, row0, row1, c;
    int16_t dx, dy;
    ST_UNUSED(st);

    if (cx < 5 || cy < 3) return true;
    tx0 = (uint16_t)(cx - 5) >> 4;
    tx1 = (uint16_t)(cx + 5) >> 4;
    ty0 = (uint16_t)(cy - 3) >> 4;
    ty1 = (uint16_t)(cy + 3) >> 4;
    if (tx1 >= HEARTH_MAP_W || ty1 >= HEARTH_MAP_H) return true;

    row0 = (uint16_t)((ty0 << 5) + (ty0 << 3) + (ty0 << 2));
    c = (uint16_t)HS.map_col[row0 + tx0];
    if (c == COL_SOLID || c == COL_WATER) return true;
    if (tx1 != tx0) {
        c = (uint16_t)HS.map_col[row0 + tx1];
        if (c == COL_SOLID || c == COL_WATER) return true;
    }
    if (ty1 != ty0) {
        row1 = (uint16_t)((ty1 << 5) + (ty1 << 3) + (ty1 << 2));
        c = (uint16_t)HS.map_col[row1 + tx0];
        if (c == COL_SOLID || c == COL_WATER) return true;
        if (tx1 != tx0) {
            c = (uint16_t)HS.map_col[row1 + tx1];
            if (c == COL_SOLID || c == COL_WATER) return true;
        }
    }

    /* 3 static NPCs (unrolled for zero far-pointer indexing on 65816) */
    dx = (int16_t)(cx - FP_PX(HS.npcs[0].fx));
    if (dx > -11 && dx < 11) {
        dy = (int16_t)(cy - FP_PX(HS.npcs[0].fy));
        if (dy > -9 && dy < 9) return true;
    }
    dx = (int16_t)(cx - FP_PX(HS.npcs[1].fx));
    if (dx > -11 && dx < 11) {
        dy = (int16_t)(cy - FP_PX(HS.npcs[1].fy));
        if (dy > -9 && dy < 9) return true;
    }
    dx = (int16_t)(cx - FP_PX(HS.npcs[2].fx));
    if (dx > -11 && dx < 11) {
        dy = (int16_t)(cy - FP_PX(HS.npcs[2].fy));
        if (dy > -9 && dy < 9) return true;
    }
    /* Waystone (8,17) & Chest (26,6) */
    dx = (int16_t)(cx - (8 * 16 + 8));
    if (dx > -11 && dx < 11) {
        dy = (int16_t)(cy - (17 * 16 + 8));
        if (dy > -9 && dy < 9) return true;
    }
    dx = (int16_t)(cx - (26 * 16 + 8));
    if (dx > -10 && dx < 10) {
        dy = (int16_t)(cy - (6 * 16 + 8));
        if (dy > -8 && dy < 8) return true;
    }
    return false;
}

static void step_hero_collide_xy(HearthState *st) {
    fp16_t nfx, nfy;
    int16_t hx, hy, nx, ny;

    hx = FP_PX(HS.hero_fx);
    hy = FP_PX(HS.hero_fy);

    /* Axis-separated collision with 5px automatic corner-nudging! */
    if (HS.hero_vx != 0) {
        nfx = HS.hero_fx + HS.hero_vx;
        nx = FP_PX(nfx);
        if (!collides_world_px(st, nx, hy)) {
            HS.hero_fx = nfx;
            hx = nx;
        } else if (!collides_world_px(st, nx, (int16_t)(hy - 5))) {
            HS.hero_fy -= 192;
            hy = FP_PX(HS.hero_fy);
        } else if (!collides_world_px(st, nx, (int16_t)(hy + 5))) {
            HS.hero_fy += 192;
            hy = FP_PX(HS.hero_fy);
        } else {
            HS.hero_vx = 0;
        }
    }
    if (HS.hero_vy != 0) {
        nfy = HS.hero_fy + HS.hero_vy;
        ny = FP_PX(nfy);
        if (!collides_world_px(st, hx, ny)) {
            HS.hero_fy = nfy;
        } else if (!collides_world_px(st, (int16_t)(hx - 5), ny)) {
            HS.hero_fx -= 192;
        } else if (!collides_world_px(st, (int16_t)(hx + 5), ny)) {
            HS.hero_fx += 192;
        } else {
            HS.hero_vy = 0;
        }
    }
}

static void step_hero_trail_and_fx(HearthState *st) {
    int16_t dx, dy, hx, hy;
    uint16_t th, cadence, foot, next_head;

    HS.hero_anim_tick = (uint16_t)(HS.hero_anim_tick + (HS.hero_dashing ? 2 : 1));
    HS.hero_step_timer++;

    hx = FP_PX(HS.hero_fx);
    hy = FP_PX(HS.hero_fy);
    th = (uint16_t)HS.trail_head;
    dx = (int16_t)(hx - FP_PX(HS.trail_x[th]));
    dy = (int16_t)(hy - FP_PX(HS.trail_y[th]));
    if (dx <= -2 || dx >= 2 || dy <= -2 || dy >= 2) {
        next_head = (uint16_t)(th + 1u);
        if (next_head >= (uint16_t)HEARTH_TRAIL_LEN) next_head = 0u;
        HS.trail_head = (uint8_t)next_head;
        HS.trail_x[next_head] = HS.hero_fx;
        HS.trail_y[next_head] = HS.hero_fy;
    }

    cadence = HS.hero_dashing ? 10u : 16u;
    if ((uint16_t)HS.hero_step_timer >= cadence) {
        HS.hero_step_timer = 0;
        foot = (uint16_t)get_col_at_px(st, hx, hy);
        if (foot == (uint16_t)COL_TALL_GRASS) {
            push_sfx(st, SFX_GRASS_RUSTLE);
            spawn_burst(st, 1, hx, hy, 4, 0, 4, false);
        } else if (foot == (uint16_t)COL_SHALLOW) {
            push_sfx(st, SFX_WATER_SPLASH);
            spawn_burst(st, 2, hx, hy, 4, 0, 14, false);
        } else {
            push_sfx(st, HS.hero_dashing ? (uint8_t)SFX_DASH : (uint8_t)SFX_STEP);
            if (HS.hero_dashing) spawn_burst(st, 0, hx, hy, 2, 0, 8, false);
        }
    }
}

static void step_hero_movement(HearthState *st, uint16_t pad) {
    int16_t ix = 0, iy = 0;
    int16_t max_spd, dspd, tvx, tvy, vx, vy, accel;

    if (pad & PAD_LEFT)  ix -= 1;
    if (pad & PAD_RIGHT) ix += 1;
    if (pad & PAD_UP)    iy -= 1;
    if (pad & PAD_DOWN)  iy += 1;

    HS.hero_dashing = ((pad & PAD_B) != 0) && (ix != 0 || iy != 0);
    max_spd = HS.hero_dashing ? HS.tuning.dash_speed_fp : HS.tuning.walk_speed_fp;

    if (ix != 0 && iy != 0) {
        /* 181/256 ~= 0.7071 integer diagonal normalization (128+32+16+4+1, zero multiply) */
        dspd = (int16_t)((max_spd >> 1) + (max_spd >> 3) + (max_spd >> 4) + (max_spd >> 6) + (max_spd >> 8));
        tvx = (ix > 0) ? dspd : (int16_t)(-dspd);
        tvy = (iy > 0) ? dspd : (int16_t)(-dspd);
    } else {
        tvx = (ix == 0) ? (int16_t)0 : ((ix > 0) ? max_spd : (int16_t)(-max_spd));
        tvy = (iy == 0) ? (int16_t)0 : ((iy > 0) ? max_spd : (int16_t)(-max_spd));
    }

    accel = HS.tuning.accel_fp;
    vx = (int16_t)HS.hero_vx;
    if (vx != tvx) {
        if (vx < tvx) { vx = (int16_t)(vx + accel); if (vx > tvx) vx = tvx; }
        else          { vx = (int16_t)(vx - accel); if (vx < tvx) vx = tvx; }
        HS.hero_vx = vx;
    }
    vy = (int16_t)HS.hero_vy;
    if (vy != tvy) {
        if (vy < tvy) { vy = (int16_t)(vy + accel); if (vy > tvy) vy = tvy; }
        else          { vy = (int16_t)(vy - accel); if (vy < tvy) vy = tvy; }
        HS.hero_vy = vy;
    }

    if (ix != 0) {
        HS.hero_dir = (uint8_t)((ix < 0) ? DIR_LEFT : DIR_RIGHT);
    } else if (iy != 0) {
        HS.hero_dir = (uint8_t)((iy < 0) ? DIR_UP : DIR_DOWN);
    }

    if (vx != 0 || vy != 0) {
        step_hero_collide_xy(st);
        vx = (int16_t)HS.hero_vx;
        vy = (int16_t)HS.hero_vy;
    }

    if (vx < -24 || vx > 24 || vy < -24 || vy > 24) {
        step_hero_trail_and_fx(st);
    } else {
        HS.hero_anim_tick = 0;
        HS.hero_step_timer = 10;
    }
}

static void step_pip_follower(HearthState *st) {
    int16_t back = (int16_t)(HS.tuning.follow_dist_px >> 1);
    int16_t idx;
    fp16_t dx, dy;
    ST_UNUSED(st);

    if (back < 4) back = 4;
    if (back > HEARTH_TRAIL_LEN - 2) back = HEARTH_TRAIL_LEN - 2;
    idx = (int16_t)((int16_t)HS.trail_head - back);
    if (idx < 0) idx = (int16_t)(idx + HEARTH_TRAIL_LEN);

    dx = HS.trail_x[idx] - HS.pip_fx;
    dy = (HS.trail_y[idx] - TO_FP(4)) - HS.pip_fy;
    if (dx != 0) HS.pip_fx += dx >> 3;
    if (dy != 0) HS.pip_fy += dy >> 3;
    if (dx < -40 || dx > 40) HS.pip_dir = (uint8_t)((dx < 0) ? DIR_LEFT : DIR_RIGHT);
}

/* ============================================================================
 * TACTILE FAMILIAR COMBAT ENGINE (TIMED HITS, PARRIES, LOCK BREAKS, SWAP ARC)
 * ============================================================================ */
static uint8_t first_alive_enemy(const HearthBattleState *b) {
    uint8_t i;
    for (i = 0; i < b->enemy_count; i++) {
        if (b->enemies[i].alive) return i;
    }
    return 0;
}

static void set_battle_enemy(
    HearthBattleEnemy *e, const char *name, const char *special, uint8_t sprite_id,
    int8_t field_idx, bool is_boss, int16_t hp, int16_t atk, int16_t def,
    int16_t home_x, int16_t home_y, int16_t start_x,
    uint8_t charge_turns, uint8_t max_charge, uint8_t lock_count,
    uint8_t l0, uint8_t l1, uint8_t l2
) {
    e->name = name;
    e->special_name = special;
    e->sprite_id = sprite_id;
    e->field_idx = field_idx;
    e->is_boss = is_boss;
    e->alive = true;
    e->staggered = false;
    e->hp = hp;
    e->max_hp = hp;
    e->atk = atk;
    e->def = def;
    e->home_x = home_x;
    e->home_y = home_y;
    e->x = start_x;
    e->y = home_y;
    e->scale_x_fp = FP_ONE;
    e->scale_y_fp = FP_ONE;
    e->flash_frames = 0;
    e->hurt_frames = 0;
    e->charge_turns = charge_turns;
    e->max_charge_turns = max_charge;
    e->lock_count = lock_count;
    e->locks[0] = l0; e->locks[1] = l1; e->locks[2] = l2;
    e->broken_locks[0] = false;
    e->broken_locks[1] = false;
    e->broken_locks[2] = false;
    e->anim_frame = 0;
}

void hearth_start_battle(HearthState *st, uint8_t preset, uint8_t biome, int8_t field_idx) {
    uint16_t w, h;
    uint8_t lim;
    int16_t right_base, left_base;
    HearthBattleState *b;
    int i;

    get_ppu_dims(st, &w, &h, &lim);
    right_base = (w == 256) ? 188 : 236;
    left_base  = (w == 256) ? 68  : 84;

    b = &st->battle;
    hearth_memset(b, 0, (uint16_t)sizeof(*b));
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
        for (i = 0; i < 3; i++) {
            st->party[i].hp = st->party[i].max_hp;
            st->party[i].mp = st->party[i].max_mp;
        }
        if (st->potions < 2) st->potions = 2;
    }

    if (preset == ENCOUNTER_SHRINE_BOSS) {
        b->enemy_count = 1;
        set_battle_enemy(&b->enemies[0], "Briar Warden", "Bramble Tempest", SPR_BOSS_BRIAR,
            field_idx, true, 165, 21, 5, right_base, 126, (int16_t)(right_base + 56),
            2, 2, 3, LOCK_SWORD, LOCK_SUN, LOCK_STAR);
        copy_str_safe(b->banner, sizeof(b->banner), "SHRINE GUARDIAN: BRIAR WARDEN!");
    } else if (preset == ENCOUNTER_GLADE_DUO) {
        b->enemy_count = 2;
        set_battle_enemy(&b->enemies[0], "Thornling", "Briar Flurry", SPR_ENEMY_THORNLING,
            field_idx, false, 58, 14, 3, (int16_t)(right_base - 14), 108, (int16_t)(right_base + 56),
            2, 2, 2, LOCK_SWORD, LOCK_SUN, LOCK_NONE);
        set_battle_enemy(&b->enemies[1], "Dusk Moth", "Hollow Dust", SPR_ENEMY_DUSK_MOTH,
            field_idx, false, 48, 13, 2, (int16_t)(right_base + 16), 142, (int16_t)(right_base + 76),
            1, 2, 1, LOCK_STAR, LOCK_NONE, LOCK_NONE);
        copy_str_safe(b->banner, sizeof(b->banner), "GLADE ENCOUNTER!");
    } else {
        b->enemy_count = 2;
        set_battle_enemy(&b->enemies[0], "Gloomcap", "Spore Burst", SPR_ENEMY_GLOOMCAP,
            field_idx, false, 52, 13, 2, (int16_t)(right_base - 12), 112, (int16_t)(right_base + 52),
            2, 2, 2, LOCK_SUN, LOCK_STAR, LOCK_NONE);
        set_battle_enemy(&b->enemies[1], "Thornling", "Briar Flurry", SPR_ENEMY_THORNLING,
            field_idx, false, 54, 14, 3, (int16_t)(right_base + 14), 142, (int16_t)(right_base + 74),
            0, 2, 0, LOCK_NONE, LOCK_NONE, LOCK_NONE);
        copy_str_safe(b->banner, sizeof(b->banner), "MEADOW ENCOUNTER!");
    }

    b->banner_frames = 60;
    st->dialogue.active = false;
    st->mode = MODE_BATTLE;
    push_sfx(st, SFX_BATTLE_SWOOP);
    build_ppu_packet(st);
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
    uint8_t i, li;

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

    for (i = 0; i < b->enemy_count; i++) {
        HearthBattleEnemy *e;
        int32_t mult;
        int16_t raw, dmg;
        char dmg_txt[16];
        int match;
        bool all_broken;

        if (!is_aoe && i != b->selected_enemy) continue;
        e = &b->enemies[i];
        if (!e->alive) continue;

        mult = (b->active_action == 1) ? 145 : 100;
        if (is_followup) mult = (mult * 55) / 100;
        else if (is_good) mult = (mult * 115) / 100;
        if (e->staggered) mult = (mult * 125) / 100;

        raw = (int16_t)((actor->atk * mult) / 100 - e->def + hearth_prng_range(&st->prng, -1, 2));
        dmg = (raw < 4) ? 4 : raw;
        e->hp = (e->hp > dmg) ? (int16_t)(e->hp - dmg) : 0;
        e->flash_frames = 8;
        e->hurt_frames = 14;
        e->scale_x_fp = 325;
        e->scale_y_fp = 195;
        e->x = (int16_t)(e->home_x + 6);

        spawn_burst(st, 3, e->home_x, (int16_t)(e->home_y - 12), (is_perfect || is_followup) ? 12 : 7,
                    3, (is_perfect || is_followup) ? 11 : 12, true);

        fmt_int_text(dmg_txt, sizeof(dmg_txt), "", dmg, is_followup ? "!" : "");
        spawn_ftext(st, (int16_t)(e->home_x + hearth_prng_range(&st->prng, -5, 5)),
                    (int16_t)(e->home_y - (is_followup ? 34 : 24)),
                    dmg_txt, 3, (is_followup || is_perfect) ? 11 : 12, false);

        /* Spell Lock Shattering! */
        if (e->charge_turns > 0 && e->lock_count > 0 && e->hp > 0) {
            match = -1;
            for (li = 0; li < e->lock_count; li++) {
                if (!e->broken_locks[li] && e->locks[li] == strike_kind) {
                    match = (int)li;
                    break;
                }
            }
            if (match >= 0) {
                e->broken_locks[match] = true;
                b->locks_broken++;
                spawn_burst(st, 4, e->home_x, (int16_t)(e->home_y - 34), 10, 0, 4, true);

                all_broken = true;
                for (li = 0; li < e->lock_count; li++) {
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
    char txt[16];

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
    HearthPartyMember *actor;
    uint8_t i;

    if (!b->active) return;

    if (b->banner_frames > 0) b->banner_frames--;
    if (b->hero_flash_frames > 0) b->hero_flash_frames--;
    b->hero_scale_x_fp = (int16_t)(b->hero_scale_x_fp + ((FP_ONE - b->hero_scale_x_fp) >> 2));
    b->hero_scale_y_fp = (int16_t)(b->hero_scale_y_fp + ((FP_ONE - b->hero_scale_y_fp) >> 2));

    for (i = 0; i < b->enemy_count; i++) {
        HearthBattleEnemy *e = &b->enemies[i];
        if (e->flash_frames > 0) e->flash_frames--;
        if (e->hurt_frames > 0) e->hurt_frames--;
        e->scale_x_fp = (int16_t)(e->scale_x_fp + ((FP_ONE - e->scale_x_fp) >> 2));
        e->scale_y_fp = (int16_t)(e->scale_y_fp + ((FP_ONE - e->scale_y_fp) >> 2));
        if (e->alive) e->anim_frame++;
    }

    if (just_pressed & PAD_A) b->timed_buffer = 6;
    else if (b->timed_buffer > 0) b->timed_buffer--;

    actor = &st->party[b->active_slot];
    b->phase_timer++;

    switch (b->phase) {
        case BPHASE_INTRO: {
            const uint16_t dur = 24;
            uint16_t t = (b->phase_timer > dur) ? dur : b->phase_timer;
            int16_t rem = (int16_t)(dur - t);
            b->hero_x = (int16_t)(b->hero_home_x - (54 * rem) / (int16_t)dur);
            b->hero_y = (int16_t)(b->hero_home_y - sin_arc(t, dur, 16));
            for (i = 0; i < b->enemy_count; i++) {
                b->enemies[i].x = (int16_t)(b->enemies[i].home_x + (56 * rem) / (int16_t)dur);
                b->enemies[i].y = (int16_t)(b->enemies[i].home_y - sin_arc(t, dur, 12));
            }
            if (b->phase_timer >= dur) {
                b->hero_x = b->hero_home_x;
                b->hero_y = b->hero_home_y;
                b->hero_scale_x_fp = 320;
                b->hero_scale_y_fp = 200;
                for (i = 0; i < b->enemy_count; i++) {
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
            int p_idx;
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
                        for (p_idx = 0; p_idx < 3; p_idx++) {
                            st->party[p_idx].hp = (int16_t)((st->party[p_idx].hp + 45 > st->party[p_idx].max_hp) ? st->party[p_idx].max_hp : st->party[p_idx].hp + 45);
                            st->party[p_idx].mp = (int16_t)((st->party[p_idx].mp + 10 > st->party[p_idx].max_mp) ? st->party[p_idx].max_mp : st->party[p_idx].mp + 10);
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
            int p_idx;
            b->hero_x = (int16_t)(b->hero_home_x - sin_arc(b->phase_timer, dur, 8));
            b->hero_scale_x_fp = 290;
            b->hero_scale_y_fp = 215;
            if (b->phase_timer >= dur) {
                if (b->active_action == 1 && actor->skill_is_heal) {
                    for (p_idx = 0; p_idx < 3; p_idx++) {
                        st->party[p_idx].hp = (int16_t)((st->party[p_idx].hp + 38 > st->party[p_idx].max_hp) ? st->party[p_idx].max_hp : st->party[p_idx].hp + 38);
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
            uint16_t win = (st->tuning.timed_hit_window_f < 4) ? 4 : st->tuning.timed_hit_window_f;
            uint16_t sweet_start = (dur > win) ? (uint16_t)(dur - win) : 1;

            b->hero_x = (int16_t)(b->hero_home_x + ((dest_x - b->hero_home_x) * (int16_t)b->phase_timer) / (int16_t)dur);
            b->hero_y = (int16_t)(b->hero_home_y + ((dest_y - b->hero_home_y) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, 14));
            b->hero_scale_x_fp = 230;
            b->hero_scale_y_fp = 282;

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
            bool any_alive;
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
                any_alive = false;
                for (i = 0; i < b->enemy_count; i++) if (b->enemies[i].alive) any_alive = true;
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
            bool all_dead;
            b->hero_x = (int16_t)(b->hero_x + ((b->hero_home_x - b->hero_x) * (int16_t)b->phase_timer) / (int16_t)dur);
            b->hero_y = (int16_t)(b->hero_y + ((b->hero_home_y - b->hero_y) * (int16_t)b->phase_timer) / (int16_t)dur);
            if (b->phase_timer >= dur) {
                b->hero_x = b->hero_home_x;
                b->hero_y = b->hero_home_y;
                b->ring.active = false;

                all_dead = true;
                for (i = 0; i < b->enemy_count; i++) if (b->enemies[i].alive) all_dead = false;
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
            HearthBattleEnemy *e;
            while (b->enemy_turn_idx < b->enemy_count && !b->enemies[b->enemy_turn_idx].alive) {
                b->enemy_turn_idx++;
            }
            if (b->enemy_turn_idx >= b->enemy_count) {
                b->phase = BPHASE_COMMAND;
                b->phase_timer = 0;
                b->hero_guarding = false;
                for (i = 0; i < b->enemy_count; i++) {
                    e = &b->enemies[i];
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
                        e->broken_locks[0] = false;
                        e->broken_locks[1] = false;
                        e->broken_locks[2] = false;
                    }
                }
                break;
            }
            e = &b->enemies[b->enemy_turn_idx];
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
            uint16_t win = (st->tuning.parry_window_f < 4) ? 4 : st->tuning.parry_window_f;
            uint16_t sweet_start = (dur > win) ? (uint16_t)(dur - win) : 1;

            e->x = (int16_t)(e->home_x + ((dest_x - e->home_x) * (int16_t)b->phase_timer) / (int16_t)dur);
            e->y = (int16_t)(e->home_y + ((b->hero_home_y - e->home_y) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, 10));

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
            int healthy, p_idx;
            e->x = (int16_t)(e->x + ((e->home_x - e->x) * (int16_t)b->phase_timer) / (int16_t)dur);
            e->y = (int16_t)(e->y + ((e->home_y - e->y) * (int16_t)b->phase_timer) / (int16_t)dur);
            if (b->phase_timer >= dur) {
                e->x = e->home_x;
                e->y = e->home_y;
                if (e->charge_turns == 1) { e->charge_turns = 0; e->lock_count = 0; }

                if (actor->hp <= 0) {
                    healthy = -1;
                    for (p_idx = 0; p_idx < 3; p_idx++) {
                        if (st->party[p_idx].hp > 0) { healthy = p_idx; break; }
                    }
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
                    st->field_enemies[(int)b->field_enemy_idx].alive = false;
                }
                if (b->is_boss_battle) st->flag_boss_defeated = true;
                st->mode = MODE_BATTLE_VICTORY;
            }
            break;
        }

        case BPHASE_DEFEAT_POSE: {
            int p_idx;
            if (b->phase_timer >= 44) {
                for (p_idx = 0; p_idx < 3; p_idx++) {
                    st->party[p_idx].hp = st->party[p_idx].max_hp;
                    st->party[p_idx].mp = st->party[p_idx].max_mp;
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
    uint8_t i;
    d->active = true;
    d->speaker = speaker;
    d->full_text = text;
    d->full_len = str_len16(text);
    d->visible_chars = 0;
    d->finished = false;
    d->pause_frames = 0;
    d->voice_sfx = voice;
    d->choice_count = choice_count;
    d->selected_choice = 0;
    for (i = 0; i < choice_count && i < HEARTH_MAX_CHOICES; i++) {
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
    char ch;
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
            ch = d->full_text[d->visible_chars - 1];
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
    int idx;
    if (actor_id == ACTOR_HERO) { *ox = FROM_FP(st->hero_fx); *oy = FROM_FP(st->hero_fy); return; }
    if (actor_id == ACTOR_PIP)  { *ox = FROM_FP(st->pip_fx);  *oy = FROM_FP(st->pip_fy);  return; }
    idx = actor_id - ACTOR_ELDER;
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
                uint16_t w, h;
                uint8_t lim;
                int16_t mx, my;
                get_ppu_dims(st, &w, &h, &lim);
                mx = (int16_t)(HEARTH_MAP_W * 16 - (int)w);
                my = (int16_t)(HEARTH_MAP_H * 16 - (int)h);
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
                uint16_t w, h;
                uint8_t lim;
                int16_t mx, my;
                get_ppu_dims(st, &w, &h, &lim);
                mx = (int16_t)(HEARTH_MAP_W * 16 - (int)w);
                my = (int16_t)(HEARTH_MAP_H * 16 - (int)h);
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
static void retarget_enemy_patrol(HearthState *st, HearthFieldEnemy *e) {
    int16_t hdx, hdy, pick;
    e->patrol_timer = (uint8_t)hearth_prng_range(&st->prng, 50, 105);
    hdx = (int16_t)(FP_PX(e->home_fx) - FP_PX(e->fx));
    hdy = (int16_t)(FP_PX(e->home_fy) - FP_PX(e->fy));
    if (hdx < -32 || hdx > 32 || hdy < -32 || hdy > 32) {
        e->vx = (hdx > 0) ? 85 : -85;
        e->vy = (hdy > 0) ? 85 : -85;
    } else {
        pick = hearth_prng_range(&st->prng, 0, 4);
        e->vx = (pick == 1) ? 80 : ((pick == 2) ? -80 : 0);
        e->vy = (pick == 3) ? 80 : ((pick == 4) ? -80 : 0);
    }
}

static void step_enemy_patrol(HearthState *st, HearthFieldEnemy *e) {
    fp16_t nfx, nfy;
    int16_t px, py;
    uint16_t tx, ty, c;

    e->alerted = false;
    if (e->is_boss) return;

    if (e->patrol_timer > 0) {
        e->patrol_timer--;
    } else {
        retarget_enemy_patrol(st, e);
    }
    if (e->vx == 0 && e->vy == 0) return;

    nfx = e->fx + e->vx;
    nfy = e->fy + e->vy;
    px = FP_PX(nfx);
    py = FP_PX(nfy);
    if (px < 0 || py < 0) { e->vx = 0; e->vy = 0; return; }
    tx = (uint16_t)px >> 4;
    ty = (uint16_t)py >> 4;
    if (tx >= HEARTH_MAP_W || ty >= HEARTH_MAP_H) { e->vx = 0; e->vy = 0; return; }
    c = (uint16_t)HS.map_col[(ty << 5) + (ty << 3) + (ty << 2) + tx];
    if (c != COL_SOLID && c != COL_WATER) {
        e->fx = nfx;
        e->fy = nfy;
    } else {
        e->vx = 0;
        e->vy = 0;
    }
}

static bool step_enemy_chase(HearthState *st, HearthFieldEnemy *e, int idx, int16_t dx, int16_t dy, int16_t ex, int16_t ey) {
    int16_t spd, adx, ady, doct;
    fp16_t nvx, nvy, nfx, nfy;
    uint16_t cx, cy;

    if (!e->alerted) {
        e->alerted = true;
        e->alert_frames = 28;
        push_sfx(st, SFX_ENEMY_ALERT);
    }
    spd = e->is_boss ? 140 : 200;
    if (dx > 2 || dx < -2) {
        if (dy > 2 || dy < -2) {
            int16_t dspd = (int16_t)((spd >> 1) + (spd >> 3) + (spd >> 4) + (spd >> 6) + (spd >> 8));
            nvx = (dx > 0) ? dspd : -dspd;
            nvy = (dy > 0) ? dspd : -dspd;
        } else {
            nvx = (dx > 0) ? spd : -spd;
            nvy = 0;
        }
    } else {
        nvx = 0;
        nvy = (dy > 2) ? spd : ((dy < -2) ? -spd : 0);
    }
    nfx = e->fx + nvx;
    nfy = e->fy + nvy;
    cx = (uint16_t)get_col_at_px(st, FP_PX(nfx), ey);
    cy = (uint16_t)get_col_at_px(st, ex, FP_PX(nfy));
    if (cx != COL_SOLID && cx != COL_WATER) e->fx = nfx;
    if (cy != COL_SOLID && cy != COL_WATER) e->fy = nfy;
    adx = (dx < 0) ? (int16_t)(-dx) : dx;
    ady = (dy < 0) ? (int16_t)(-dy) : dy;
    e->dir = (uint8_t)((adx > ady)
        ? ((dx < 0) ? DIR_LEFT : DIR_RIGHT)
        : ((dy < 0) ? DIR_UP : DIR_DOWN));

    doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
    if (doct < 16) {
        hearth_start_battle(st, e->preset, e->biome, (int8_t)idx);
        return true;
    }
    return false;
}

static void step_field_enemies(HearthState *st, bool can_engage) {
    int16_t hx = FP_PX(HS.hero_fx);
    int16_t hy = FP_PX(HS.hero_fy);
    int16_t i;
    HearthFieldEnemy *e = HS.field_enemies;
    uint16_t check_aggro = (can_engage && HS.hero_safe_frames == 0) ? 1u : 0u;

    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++, e++) {
        int16_t ex, ey, dx, dy, adx, ady, sight, doct;

        if (!e->alive) continue;
        e->anim_frame++;
        if (e->alert_frames > 0) e->alert_frames--;

        if (check_aggro) {
            ex = FP_PX(e->fx);
            ey = FP_PX(e->fy);
            dx = (int16_t)(hx - ex);
            dy = (int16_t)(hy - ey);
            sight = e->is_boss ? 52 : 66;
            if (dx > -sight && dx < sight && dy > -sight && dy < sight) {
                adx = (dx < 0) ? (int16_t)(-dx) : dx;
                ady = (dy < 0) ? (int16_t)(-dy) : dy;
                doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
                if (doct < (e->is_boss ? 56 : 71)) {
                    if (step_enemy_chase(st, e, (int)i, dx, dy, ex, ey)) {
                        return;
                    }
                    continue;
                }
            }
        }
        if (!e->is_boss) {
            step_enemy_patrol(st, e);
        } else {
            e->alerted = false;
        }
    }
}

static int find_interactable(const HearthState *st, int16_t *ox, int16_t *oy, const char **olabel) {
    int16_t hx = FP_PX(HS.hero_fx);
    int16_t hy = FP_PX(HS.hero_fy);
    int16_t px, py, dx, dy, adx, ady, doct, nx, ny;
    int16_t i;
    const HearthNpc *n = HS.npcs;
    ST_UNUSED(st);

    px = hx; py = hy;
    if (HS.hero_dir == DIR_UP) py -= 15;
    else if (HS.hero_dir == DIR_DOWN) py += 15;
    else if (HS.hero_dir == DIR_LEFT) px -= 15;
    else px += 15;

    for (i = 0; i < HEARTH_MAX_NPCS; i++, n++) {
        nx = FP_PX(n->fx);
        dx = (int16_t)(hx - nx);
        if (dx <= -37 || dx >= 37) continue;
        ny = FP_PX(n->fy);
        dy = (int16_t)(hy - ny);
        if (dy <= -37 || dy >= 37) continue;

        if (dx > -22 && dx < 22 && dy > -22 && dy < 22) {
            adx = (dx < 0) ? (int16_t)(-dx) : dx;
            ady = (dy < 0) ? (int16_t)(-dy) : dy;
            doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
            if (doct < 24) {
                *ox = nx; *oy = (int16_t)(ny - 24); *olabel = n->name;
                return (int)i;
            }
        }
        dx = (int16_t)(px - nx);
        dy = (int16_t)(py - ny);
        if (dx > -19 && dx < 19 && dy > -19 && dy < 19) {
            adx = (dx < 0) ? (int16_t)(-dx) : dx;
            ady = (dy < 0) ? (int16_t)(-dy) : dy;
            doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
            if (doct < 21) {
                *ox = nx; *oy = (int16_t)(ny - 24); *olabel = n->name;
                return (int)i;
            }
        }
    }
    dx = (int16_t)(hx - (8 * 16 + 8));
    if (dx > -22 && dx < 22) {
        dy = (int16_t)(hy - (17 * 16 + 8));
        if (dy > -22 && dy < 22) {
            adx = (dx < 0) ? (int16_t)(-dx) : dx;
            ady = (dy < 0) ? (int16_t)(-dy) : dy;
            doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
            if (doct < 24) {
                *ox = 8 * 16 + 8; *oy = (int16_t)(17 * 16 + 8 - 22); *olabel = "Waystone";
                return 10;
            }
        }
    }
    if (!HS.flag_chest_opened) {
        dx = (int16_t)(hx - (26 * 16 + 8));
        if (dx > -22 && dx < 22) {
            dy = (int16_t)(hy - (6 * 16 + 8));
            if (dy > -22 && dy < 22) {
                adx = (dx < 0) ? (int16_t)(-dx) : dx;
                ady = (dy < 0) ? (int16_t)(-dy) : dy;
                doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
                if (doct < 24) {
                    *ox = 26 * 16 + 8; *oy = (int16_t)(6 * 16 + 8 - 18); *olabel = "Chest";
                    return 11;
                }
            }
        }
    }
    dx = (int16_t)(hx - (16 * 16 + 8));
    if (dx > -21 && dx < 21) {
        dy = (int16_t)(hy - (15 * 16 + 8));
        if (dy > -21 && dy < 21) {
            adx = (dx < 0) ? (int16_t)(-dx) : dx;
            ady = (dy < 0) ? (int16_t)(-dy) : dy;
            doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
            if (doct < 23) {
                *ox = 16 * 16 + 8; *oy = (int16_t)(15 * 16 + 8 - 18); *olabel = "Read Sign";
                return 12;
            }
        }
    }
    return -1;
}

static void try_interact(HearthState *st) {
    int16_t ox = 0, oy = 0;
    const char *lbl = NULL;
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
            static const HearthChoice elder_opts[3] = {
                { "Replay Cutscene Demo", ACT_REPLAY_INTRO },
                { "Fight Shrine Boss",    ACT_JUMP_BOSS },
                { "Keep Exploring",       ACT_CLOSE }
            };
            open_dialogue(st, "Elder Wren", SFX_TEXT_ELDER,
                "Press Q/E (L/R) in combat to hot-swap Familiars for free, and tap Space/Z on impact for a double follow-up!",
                3, elder_opts);
        }
    } else if (hit == 1) {
        if (!st->flag_has_courage && !st->flag_guard_mended) {
            static const HearthChoice clara_opts[2] = {
                { "Cast Take Heart (Courage)", ACT_TAKE_COURAGE },
                { "Maybe Later",               ACT_CLOSE }
            };
            open_dialogue(st, "Clara", SFX_TEXT_PIP,
                "My heart is brimming with Courage! Want a spark in your locket to help Guard Bram by the bridge?",
                2, clara_opts);
        } else {
            open_dialogue(st, "Clara", SFX_TEXT_PIP,
                st->flag_guard_mended
                    ? "Guard Bram looks sturdy as oak again! Sharing courage lifts two hearts at once."
                    : "You already carry Courage in your locket - bring it east to Guard Bram!", 0, NULL);
        }
    } else if (hit == 2) {
        if (st->flag_guard_mended) {
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "My knees stopped shaking! Head east into the Stone Shrine and show the Briar Warden what you can do!", 0, NULL);
        } else if (st->flag_has_courage) {
            static const HearthChoice guard_opts[2] = {
                { "Cast Give Heart (Courage)", ACT_GIVE_COURAGE },
                { "Hold On",                   ACT_CLOSE }
            };
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "B-brr... the mist from the Shrine chills my bones. Wait - is that golden Courage glowing in your locket?",
                2, guard_opts);
        } else {
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "My heart feels hollow with dread. You can still walk past me into the Meadow, or ask Clara for a spark of Courage!", 0, NULL);
        }
    }
}

/* ============================================================================
 * CAMERA & PPU FRAME PACKET BUILDER
 * ============================================================================ */
static void step_camera_override(HearthState *st) {
    if (st->cam_pan_frame < st->cam_pan_dur) {
        st->cam_pan_frame++;
        st->cam_fx = st->cam_start_fx + ((st->cam_target_fx - st->cam_start_fx) * st->cam_pan_frame) / st->cam_pan_dur;
        st->cam_fy = st->cam_start_fy + ((st->cam_target_fy - st->cam_start_fy) * st->cam_pan_frame) / st->cam_pan_dur;
    }
}

static void step_camera(HearthState *st) {
    int16_t w, mx, tx, ty;
    fp16_t gx, gy, dx, dy;

    if (HS.cam_override) {
        step_camera_override(st);
        return;
    }
    w = (HS.tuning.ppu_mode == PPU_SNES_256x224) ? (int16_t)256 : (int16_t)320;
    mx = (int16_t)(HEARTH_MAP_W * 16 - w);
    tx = (int16_t)(FP_PX(HS.hero_fx) - (w >> 1));
    if (tx < 0) tx = 0; else if (tx > mx) tx = mx;
    ty = (int16_t)(FP_PX(HS.hero_fy) - 112);
    if (ty < 0) ty = 0; else if (ty > (HEARTH_MAP_H * 16 - 224)) ty = (HEARTH_MAP_H * 16 - 224);

    if (tx != FP_PX(HS.cam_fx) || ((uint8_t)HS.cam_fx) != 0u) {
        gx = TO_FP(tx);
        dx = gx - HS.cam_fx;
        if (dx != 0) HS.cam_fx += dx >> 3;
    }
    if (ty != FP_PX(HS.cam_fy) || ((uint8_t)HS.cam_fy) != 0u) {
        gy = TO_FP(ty);
        dy = gy - HS.cam_fy;
        if (dy != 0) HS.cam_fy += dy >> 3;
    }
}

static void emit_oam(
    HearthPpuPacket *p, int16_t x, int16_t y, uint8_t sprite_id,
    uint8_t frame, uint8_t dir, uint8_t flags,
    int16_t sx_fp, int16_t sy_fp, uint8_t aura, uint8_t layer,
    int16_t sort_y, uint16_t rot_deg
) {
    uint16_t idx;
#if defined(HEARTH_TARGET_SNES)
    (void)p; (void)frame; (void)dir; (void)sx_fp; (void)sy_fp; (void)aura; (void)rot_deg;
    idx = (uint16_t)g_hearth.ppu.oam_count;
    if (idx >= HEARTH_MAX_OAM) return;
    g_hearth.ppu.oam_count = (uint8_t)(idx + 1u);
    g_hearth.ppu.oam[idx].x = x;
    g_hearth.ppu.oam[idx].y = y;
    g_hearth.ppu.oam[idx].sort_y = sort_y;
    g_hearth.ppu.oam[idx].sprite_id = sprite_id;
    g_hearth.ppu.oam[idx].flags = (uint8_t)(flags | ((layer >= 2u) ? OAM_FLAG_LAYER2 : 0u));
#else
    HearthOamSprite *o;
    idx = (uint16_t)p->oam_count;
    if (idx >= HEARTH_MAX_OAM) return;
    o = &p->oam[p->oam_count++];
    o->x = x;
    o->y = y;
    o->sprite_id = sprite_id;
    o->frame = frame;
    o->dir = dir;
    o->flags = flags;
    o->scale_x_fp = sx_fp;
    o->scale_y_fp = sy_fp;
    o->aura = aura;
    o->layer = layer;
    o->sort_y = sort_y;
    o->rot_deg = rot_deg;
#endif
}

static void emit_oam_std(
    HearthPpuPacket *p, int16_t x, int16_t y, uint8_t sprite_id,
    uint8_t frame, uint8_t dir, uint8_t flags, uint8_t layer, int16_t sort_y
) {
    uint16_t idx;
#if defined(HEARTH_TARGET_SNES)
    (void)p; (void)frame; (void)dir;
    idx = (uint16_t)g_hearth.ppu.oam_count;
    if (idx >= HEARTH_MAX_OAM) return;
    g_hearth.ppu.oam_count = (uint8_t)(idx + 1u);
    g_hearth.ppu.oam[idx].x = x;
    g_hearth.ppu.oam[idx].y = y;
    g_hearth.ppu.oam[idx].sort_y = sort_y;
    g_hearth.ppu.oam[idx].sprite_id = sprite_id;
    g_hearth.ppu.oam[idx].flags = (uint8_t)(flags | ((layer >= 2u) ? OAM_FLAG_LAYER2 : 0u));
#else
    HearthOamSprite *o;
    idx = (uint16_t)p->oam_count;
    if (idx >= HEARTH_MAX_OAM) return;
    o = &p->oam[p->oam_count++];
    o->x = x;
    o->y = y;
    o->sprite_id = sprite_id;
    o->frame = frame;
    o->dir = dir;
    o->flags = flags;
    o->scale_x_fp = FP_ONE;
    o->scale_y_fp = FP_ONE;
    o->aura = AURA_NONE;
    o->layer = layer;
    o->sort_y = sort_y;
    o->rot_deg = 0;
#endif
}

#if defined(HEARTH_TARGET_SNES)
#define OAM_SET_IDX(p, idx, vx, vy, vsid, vfr, vdir, vfl, vsx, vsy, vaura, vlay, vsort, vrot) \
    do { \
        g_hearth.ppu.oam[(idx)].x = (vx); \
        g_hearth.ppu.oam[(idx)].y = (vy); \
        g_hearth.ppu.oam[(idx)].sort_y = (vsort); \
        g_hearth.ppu.oam[(idx)].sprite_id = (vsid); \
        g_hearth.ppu.oam[(idx)].flags = (uint8_t)((vfl) | (((vlay) >= 2u) ? OAM_FLAG_LAYER2 : 0u)); \
    } while (0)
#else
#define OAM_SET_IDX(p, idx, vx, vy, vsid, vfr, vdir, vfl, vsx, vsy, vaura, vlay, vsort, vrot) \
    do { \
        HearthOamSprite *_o = &(p)->oam[(idx)]; \
        _o->x = (vx); _o->y = (vy); _o->sprite_id = (vsid); \
        _o->frame = (vfr); _o->dir = (vdir); _o->flags = (vfl); \
        _o->scale_x_fp = (vsx); _o->scale_y_fp = (vsy); \
        _o->aura = (vaura); _o->layer = (vlay); \
        _o->sort_y = (vsort); _o->rot_deg = (vrot); \
    } while (0)
#endif

static void sort_ppu_oam(HearthPpuPacket *p, int16_t sort_cnt) {
    int16_t i, j, kv;
    HearthOamSprite tmp;
#if defined(HEARTH_TARGET_SNES)
    (void)p;
#endif

    if (sort_cnt < 2) return;
    for (i = 1; i < sort_cnt; i++) {
        kv = OAM_AT(i).sort_y;
        if (OAM_AT(i - 1).sort_y <= kv) continue;
        tmp = OAM_AT(i);
        j = i;
        while (j > 0 && OAM_AT(j - 1).sort_y > kv) {
            OAM_AT(j) = OAM_AT(j - 1);
            j--;
        }
        OAM_AT(j) = tmp;
    }
}

static uint16_t build_explore_ppu_props_and_actors(
    HearthState *st, HearthPpuPacket *p, int16_t cx, int16_t cy, int16_t max_sx
) {
    int16_t i, sx, sy, nx, ny, ex, ey;
    uint16_t fc16 = (uint16_t)HS.frame_count;
    uint16_t cnt = 0u;
    const HearthFieldEnemy *e = HS.field_enemies;
    uint8_t npc_f = (uint8_t)((fc16 >> 5) & 1u);
    uint8_t fl;
    ST_UNUSED(st);
#if defined(HEARTH_TARGET_SNES)
    (void)p;
#endif

    /* Emit static props/NPCs in ascending sort_y order & cull offscreen actors */
    /* 1. Chest (y=108) */
    sx = (int16_t)(26 * 16 + 8 - cx);
    sy = (int16_t)(6 * 16 + 12 - cy);
    if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248 && cnt < HEARTH_MAX_OAM) {
        OAM_SET_IDX(p, cnt, sx, sy, SPR_CHEST, HS.flag_chest_opened ? 1 : 0, DIR_DOWN, OAM_FLAG_SHADOW,
            FP_ONE, FP_ONE, AURA_NONE, 1, 6 * 16 + 12, 0);
        cnt++;
    }

    /* 2. NPCs (ordered Elder 184, Guard 216, Clara 264 -> ascending Y, zero far-pointer indexing!) */
    nx = FP_PX(HS.npcs[0].fx);
    sx = (int16_t)(nx - cx);
    if (sx >= -24 && sx <= max_sx && cnt < HEARTH_MAX_OAM) {
        ny = FP_PX(HS.npcs[0].fy);
        sy = (int16_t)(ny - cy);
        if (sy >= -24 && sy <= 248) {
            OAM_SET_IDX(p, cnt, sx, sy, HS.npcs[0].sprite_id, npc_f, HS.npcs[0].dir, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, HS.npcs[0].aura, 1, ny, 0);
            cnt++;
        }
    }
    nx = FP_PX(HS.npcs[2].fx);
    sx = (int16_t)(nx - cx);
    if (sx >= -24 && sx <= max_sx && cnt < HEARTH_MAX_OAM) {
        ny = FP_PX(HS.npcs[2].fy);
        sy = (int16_t)(ny - cy);
        if (sy >= -24 && sy <= 248) {
            OAM_SET_IDX(p, cnt, sx, sy, HS.npcs[2].sprite_id, npc_f, HS.npcs[2].dir, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, HS.npcs[2].aura, 1, ny, 0);
            cnt++;
        }
    }
    nx = FP_PX(HS.npcs[1].fx);
    sx = (int16_t)(nx - cx);
    if (sx >= -24 && sx <= max_sx && cnt < HEARTH_MAX_OAM) {
        ny = FP_PX(HS.npcs[1].fy);
        sy = (int16_t)(ny - cy);
        if (sy >= -24 && sy <= 248) {
            OAM_SET_IDX(p, cnt, sx, sy, HS.npcs[1].sprite_id, npc_f, HS.npcs[1].dir, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, HS.npcs[1].aura, 1, ny, 0);
            cnt++;
        }
    }

    /* 3. Waystone (y=284) */
    sx = (int16_t)(8 * 16 + 8 - cx);
    sy = (int16_t)(17 * 16 + 12 - cy);
    if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248 && cnt < HEARTH_MAX_OAM) {
        OAM_SET_IDX(p, cnt, sx, sy, SPR_WAYSTONE, (uint8_t)((fc16 >> 4) & 3u), DIR_DOWN, OAM_FLAG_SHADOW,
            FP_ONE, FP_ONE, AURA_NONE, 1, 17 * 16 + 12, 0);
        cnt++;
    }

    /* 4. Roaming Field Enemies */
    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES && cnt < HEARTH_MAX_OAM; i++, e++) {
        if (!e->alive) continue;
        ex = FP_PX(e->fx);
        sx = (int16_t)(ex - cx);
        if (sx < -24 || sx > max_sx) continue;
        ey = FP_PX(e->fy);
        sy = (int16_t)(ey - cy);
        if (sy < -24 || sy > 248) continue;
        fl = (uint8_t)(OAM_FLAG_SHADOW | ((e->dir == DIR_RIGHT) ? OAM_FLAG_FLIP_X : 0));
        OAM_SET_IDX(p, cnt, sx, sy, e->sprite_id,
            (uint8_t)((e->anim_frame >> 4) & 1u), e->dir, fl,
            FP_ONE, FP_ONE, AURA_NONE, 1, ey, 0);
        cnt++;
    }

    return cnt;
}

static void emit_heart_orb_oam(const HearthState *st, HearthPpuPacket *p, uint16_t idx, int16_t cx, int16_t cy) {
    int16_t ox = (int16_t)(HS.heart_orb.start_x + ((HS.heart_orb.end_x - HS.heart_orb.start_x) * HS.heart_orb.frame) / HS.heart_orb.duration);
    int16_t oy = (int16_t)(HS.heart_orb.start_y + ((HS.heart_orb.end_y - HS.heart_orb.start_y) * HS.heart_orb.frame) / HS.heart_orb.duration - sin_arc(HS.heart_orb.frame, HS.heart_orb.duration, 32));
    ST_UNUSED(st);
#if defined(HEARTH_TARGET_SNES)
    (void)p;
#endif
    OAM_SET_IDX(p, idx, (int16_t)(ox - cx), (int16_t)(oy - cy), SPR_HEART_ORB,
        (uint8_t)((((uint16_t)HS.frame_count) >> 2) & 1u), DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 3000, 0);
}

static void emit_emote_oam(HearthPpuPacket *p, uint16_t idx, int16_t x, int16_t y, uint8_t emote) {
#if defined(HEARTH_TARGET_SNES)
    (void)p;
#endif
    OAM_SET_IDX(p, idx, x, y, emote, 0, DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 2500, 0);
}

static uint16_t build_explore_ppu_layer2(
    HearthState *st, HearthPpuPacket *p, uint16_t cnt, int16_t cx, int16_t cy,
    int16_t hx, int16_t hy, int16_t px, int16_t py, int16_t max_sx
) {
    int16_t i, sx, sy;
    const HearthFieldEnemy *e = HS.field_enemies;
    ST_UNUSED(st);
#if defined(HEARTH_TARGET_SNES)
    (void)p;
#endif

    /* Layer 2 Overhead Sprites emitted LAST so sort_ppu_oam never touches them */
    sx = (int16_t)(6 * 16 + 8 - cx);
    sy = (int16_t)(5 * 16 + 4 - cy);
    if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248 && cnt < HEARTH_MAX_OAM) {
        OAM_SET_IDX(p, cnt, sx, sy, SPR_WINDMILL_BLADES, 0, DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 2000,
            HS.windmill_rot);
        cnt++;
    }

    if (HS.npcs[0].emote_frames > 0 && HS.npcs[0].emote != 0 && cnt < HEARTH_MAX_OAM) {
        emit_emote_oam(p, cnt++, (int16_t)(FP_PX(HS.npcs[0].fx) - cx), (int16_t)(FP_PX(HS.npcs[0].fy) - 25 - cy), HS.npcs[0].emote);
    }
    if (HS.npcs[1].emote_frames > 0 && HS.npcs[1].emote != 0 && cnt < HEARTH_MAX_OAM) {
        emit_emote_oam(p, cnt++, (int16_t)(FP_PX(HS.npcs[1].fx) - cx), (int16_t)(FP_PX(HS.npcs[1].fy) - 25 - cy), HS.npcs[1].emote);
    }
    if (HS.npcs[2].emote_frames > 0 && HS.npcs[2].emote != 0 && cnt < HEARTH_MAX_OAM) {
        emit_emote_oam(p, cnt++, (int16_t)(FP_PX(HS.npcs[2].fx) - cx), (int16_t)(FP_PX(HS.npcs[2].fy) - 25 - cy), HS.npcs[2].emote);
    }
    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES && cnt < HEARTH_MAX_OAM; i++, e++) {
        if (e->alive && e->alert_frames > 0) {
            emit_emote_oam(p, cnt++, (int16_t)(FP_PX(e->fx) - cx), (int16_t)(FP_PX(e->fy) - 26 - cy), SPR_EMOTE_BANG);
        }
    }
    if (HS.hero_emote_frames > 0 && HS.hero_emote != 0 && cnt < HEARTH_MAX_OAM) {
        emit_emote_oam(p, cnt++, (int16_t)(hx - cx), (int16_t)(hy - 26 - cy), HS.hero_emote);
    }
    if (HS.pip_emote_frames > 0 && HS.pip_emote != 0 && cnt < HEARTH_MAX_OAM) {
        emit_emote_oam(p, cnt++, (int16_t)(px - cx), (int16_t)(py - 24 - cy), HS.pip_emote);
    }
    if (HS.heart_orb.active && cnt < HEARTH_MAX_OAM) {
        emit_heart_orb_oam(st, p, cnt++, cx, cy);
    }

    return cnt;
}

static void build_explore_ppu_oam(HearthState *st, HearthPpuPacket *p) {
    int16_t cx = HP.cam_x, cy = HP.cam_y;
    int16_t max_sx = (int16_t)(HP.width + 24);
    int16_t hx, hy, px, py, bob;
    uint16_t fc16 = (uint16_t)HS.frame_count;
    uint16_t cnt, layer1_cnt;
    uint8_t wf, pfl;

    cnt = build_explore_ppu_props_and_actors(st, p, cx, cy, max_sx);

    /* Hero (Rowan, Layer 1) */
    hx = FP_PX(HS.hero_fx);
    hy = FP_PX(HS.hero_fy);
    wf = (HS.hero_anim_tick == 0) ? 0 : (uint8_t)((HS.hero_anim_tick >> 3) & 3u);
    if ((HS.hero_safe_frames == 0 || (HS.hero_safe_frames & 4u) == 0u) && cnt < HEARTH_MAX_OAM) {
        OAM_SET_IDX(p, cnt, (int16_t)(hx - cx), (int16_t)(hy - cy), SPR_HERO, wf, HS.hero_dir, OAM_FLAG_SHADOW,
            HS.hero_dashing ? 270 : FP_ONE, HS.hero_dashing ? 244 : FP_ONE,
            AURA_NONE, 1, hy, 0);
        cnt++;
    }

    /* Pip Hovering Companion (Layer 1) */
    px = FP_PX(HS.pip_fx);
    py = FP_PX(HS.pip_fy);
    bob = (int16_t)((HEARTH_SIN64[(fc16 >> 1) & 63u] * 3) >> 7);
    pfl = (uint8_t)(OAM_FLAG_SHADOW | ((HS.pip_dir == DIR_LEFT) ? OAM_FLAG_FLIP_X : 0));
    if (cnt < HEARTH_MAX_OAM) {
        OAM_SET_IDX(p, cnt, (int16_t)(px - cx), (int16_t)(py - 4 + bob - cy), SPR_PIP,
            (uint8_t)((fc16 >> 3) & 1u), HS.pip_dir, pfl,
            FP_ONE, FP_ONE, AURA_NONE, 1, py, 0);
        cnt++;
    }

    layer1_cnt = cnt;
    cnt = build_explore_ppu_layer2(st, p, cnt, cx, cy, hx, hy, px, py, max_sx);
    HP.oam_count = (uint8_t)cnt;
    sort_ppu_oam(p, (int16_t)layer1_cnt);

    /* Interact Prompt */
    if (HS.mode == MODE_EXPLORE && !HS.dialogue.active) {
        int16_t ix = 0, iy = 0;
        const char *ilbl = NULL;
        if (find_interactable(st, &ix, &iy, &ilbl) >= 0) {
            HP.interact_active = true;
            HP.interact_x = (int16_t)(ix - cx);
            HP.interact_y = (int16_t)(iy - cy);
            HP.interact_label = ilbl;
        }
    }
}

static void build_battle_ppu_oam(HearthState *st, HearthPpuPacket *p) {
    const HearthBattleState *b = &HS.battle;
    uint16_t fc16 = (uint16_t)HS.frame_count;
    uint8_t ei;

    if (b->active_slot != 0) {
        emit_oam_std(p, b->wizard_back_x, b->wizard_back_y, SPR_HERO,
            (uint8_t)((fc16 >> 5) & 1u), DIR_RIGHT, OAM_FLAG_SHADOW,
            1, b->wizard_back_y);
    }

    if (b->phase != BPHASE_SWAP_ARC) {
        const HearthPartyMember *actor = &HS.party[b->active_slot];
        uint8_t hflags = (uint8_t)(OAM_FLAG_SHADOW | ((b->hero_flash_frames > 0) ? OAM_FLAG_FLASH_WHITE : 0));
        emit_oam(p, b->hero_x, b->hero_y, actor->sprite_id,
            (uint8_t)((fc16 >> 4) & 1u), DIR_RIGHT, hflags,
            b->hero_scale_x_fp, b->hero_scale_y_fp,
            b->hero_guarding ? AURA_RADIANT_COURAGE : AURA_NONE, 1, b->hero_y, 0);
    }

    for (ei = 0; ei < b->enemy_count; ei++) {
        const HearthBattleEnemy *e = &b->enemies[ei];
        uint8_t eflags;
        if (!e->alive) continue;
        eflags = (uint8_t)(OAM_FLAG_SHADOW | ((e->flash_frames > 0) ? OAM_FLAG_FLASH_WHITE : 0));
        emit_oam(p, e->x, e->y, e->sprite_id,
            (uint8_t)((e->anim_frame >> 4) & 1u), DIR_LEFT, eflags,
            e->scale_x_fp, e->scale_y_fp,
            e->staggered ? AURA_HOLLOW_FEAR : AURA_NONE, 1, e->y, 0);
    }

    sort_ppu_oam(p, (int16_t)HP.oam_count);
}

static void build_ppu_packet(HearthState *st) {
    HearthPpuPacket *p = &HS.ppu;

    HP.width = (HS.tuning.ppu_mode == PPU_SNES_256x224) ? (uint16_t)256 : (uint16_t)320;
    HP.height = 224;
    HP.oam_limit = 64;
    HP.frame_count = HS.frame_count;
    HP.mode = HS.mode;
    HP.cam_x = FP_PX(HS.cam_fx);
    HP.cam_y = FP_PX(HS.cam_fy);
    HP.shake_x = (HS.shake_frames > 0) ? (int8_t)hearth_prng_range(&HS.prng, -(int16_t)HS.shake_intensity, (int16_t)HS.shake_intensity) : 0;
    HP.shake_y = (HS.shake_frames > 0) ? (int8_t)hearth_prng_range(&HS.prng, -(int16_t)HS.shake_intensity, (int16_t)HS.shake_intensity) : 0;
    HP.flash_level = HS.flash_level;
    HP.hitstop_active = (HS.hitstop_frames > 0);
    HP.oam_count = 0;
    HP.interact_active = false;

    if (HS.mode != MODE_BATTLE && HS.mode != MODE_BATTLE_VICTORY && HS.mode != MODE_BATTLE_DEFEAT) {
        build_explore_ppu_oam(st, p);
    } else {
        build_battle_ppu_oam(st, p);
    }
}

/* ============================================================================
 * MASTER 60Hz ENTRY POINT
 * ============================================================================ */
const HearthPpuPacket *hearth_step_60hz(HearthState *st, uint16_t pad_bits) {
    uint16_t just_pressed;

    HP.sfx_count = 0;
    just_pressed = (uint16_t)(pad_bits & (uint16_t)(~HS.prev_pad));
    HS.prev_pad = pad_bits;
    HS.frame_count++;
    if ((((uint16_t)HS.frame_count) & 1u) != 0u) {
        HS.windmill_rot++;
        if (HS.windmill_rot >= 360u) HS.windmill_rot = 0u;
    }

    if (HS.shake_frames > 0) HS.shake_frames--;
    if (HS.flash_level > 0) HS.flash_level--;

    if (HS.hitstop_frames > 0) {
        HS.hitstop_frames--;
        build_ppu_packet(st);
        return &HP;
    }

    step_fx_pools(st);

    if (HS.hero_emote_frames > 0) HS.hero_emote_frames--; else HS.hero_emote = 0;
    if (HS.pip_emote_frames > 0) HS.pip_emote_frames--; else HS.pip_emote = 0;
    if (HS.npcs[0].emote_frames > 0) HS.npcs[0].emote_frames--; else HS.npcs[0].emote = 0;
    if (HS.npcs[1].emote_frames > 0) HS.npcs[1].emote_frames--; else HS.npcs[1].emote = 0;
    if (HS.npcs[2].emote_frames > 0) HS.npcs[2].emote_frames--; else HS.npcs[2].emote = 0;

    switch (HS.mode) {
        case MODE_TITLE: {
            uint16_t w, h;
            uint8_t lim;
            int16_t sx, sy;
            get_ppu_dims(st, &w, &h, &lim);
            sx = (int16_t)(14 * 16 + (HEARTH_SIN64[(HS.frame_count >> 3) & 63u] >> 1));
            sy = (int16_t)(12 * 16 + (HEARTH_SIN64[((HS.frame_count >> 3) + 16u) & 63u] >> 2));
            HS.cam_fx = TO_FP(iclamp16((int16_t)(sx - (int16_t)(w >> 1)), 0, (int16_t)(HEARTH_MAP_W * 16 - (int)w)));
            HS.cam_fy = TO_FP(iclamp16((int16_t)(sy - (int16_t)(h >> 1)), 0, (int16_t)(HEARTH_MAP_H * 16 - (int)h)));
            step_field_enemies(st, false);

            if (just_pressed & (PAD_UP | PAD_LEFT)) {
                HS.title_cursor = (uint8_t)((HS.title_cursor + 2) % 3);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & (PAD_DOWN | PAD_RIGHT)) {
                HS.title_cursor = (uint8_t)((HS.title_cursor + 1) % 3);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & (PAD_A | PAD_START)) {
                if (HS.title_cursor == 0) hearth_start_sandbox(st);
                else if (HS.title_cursor == 1) hearth_start_battle(st, ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, -1);
                else hearth_start_cutscene(st, CUTSCENE_INTRO_DEMO);
            }
            break;
        }

        case MODE_EXPLORE: {
            if (HS.hero_safe_frames > 0) HS.hero_safe_frames--;
            if (HS.dialogue.active) {
                step_dialogue(st, just_pressed);
            } else {
                step_hero_movement(st, pad_bits);
                step_pip_follower(st);
                step_field_enemies(st, true);
                if (just_pressed & PAD_A) try_interact(st);
                if (just_pressed & PAD_L) { HS.active_slot = (uint8_t)((HS.active_slot + 2) % 3); push_sfx(st, SFX_CURSOR); }
                if (just_pressed & PAD_R) { HS.active_slot = (uint8_t)((HS.active_slot + 1) % 3); push_sfx(st, SFX_CURSOR); }
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
                HS.battle.active = false;
                HS.mode = MODE_EXPLORE;
                HS.hero_safe_frames = 90;
                push_sfx(st, SFX_CONFIRM);
            } else if (just_pressed & PAD_B) {
                hearth_start_battle(st, HS.battle.is_boss_battle ? ENCOUNTER_SHRINE_BOSS : ENCOUNTER_MEADOW_PACK,
                                    HS.battle.biome, -1);
            }
            break;
    }

    build_ppu_packet(st);
    return &HP;
}
