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
    ACT_CLOSE         = 0,
    ACT_TAKE_COURAGE  = 1,
    ACT_GIVE_COURAGE  = 2,
    ACT_REPLAY_INTRO  = 3,
    ACT_JUMP_BOSS     = 4,
    ACT_OPEN_SHOP_0   = 5,
    ACT_OPEN_SHOP_1   = 6,
    ACT_MEND_CAPTAIN  = 7,
    ACT_START_FISHING = 8,
    ACT_OPEN_SHOP_2   = 9,
    ACT_REST_BED      = 10,
    ACT_OPEN_SHOP_3   = 11,
    ACT_OPEN_SHOP_4   = 12
};

/* Forward declarations */
static void build_ppu_packet(HearthState *st);
static void request_zone_warp(HearthState *st, uint8_t dst_zone, uint16_t tx, uint16_t ty);

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
 * INTERNAL INTEGER & TOROIDAL GLOBE MATH HELPERS
 * ============================================================================ */
#define GLOBE_PX_W  4096
#define GLOBE_PX_H  3072
#define GLOBE_FP_W  ((fp16_t)1048576L)
#define GLOBE_FP_H  ((fp16_t)786432L)

static int16_t iclamp16(int16_t v, int16_t lo, int16_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static uint16_t map_row_offset(uint16_t ty, uint8_t mw) {
    (void)mw;
    return (uint16_t)((ty << 5) + (ty << 3) + (ty << 2));
}

static int16_t wrap_globe_px_x(int16_t x) {
    while (x < 0) x = (int16_t)(x + GLOBE_PX_W);
    while (x >= GLOBE_PX_W) x = (int16_t)(x - GLOBE_PX_W);
    return x;
}

static int16_t wrap_globe_px_y(int16_t y) {
    while (y < 0) y = (int16_t)(y + GLOBE_PX_H);
    while (y >= GLOBE_PX_H) y = (int16_t)(y - GLOBE_PX_H);
    return y;
}

static fp16_t wrap_globe_fp_x(fp16_t fx) {
    return (fp16_t)((uint32_t)fx & (uint32_t)(GLOBE_FP_W - 1));
}

static fp16_t wrap_globe_fp_y(fp16_t fy) {
    if (fy < 0) fy += GLOBE_FP_H;
    else if (fy >= GLOBE_FP_H) fy -= GLOBE_FP_H;
    return fy;
}

static int16_t wrap_delta_x(int16_t dx) {
    if (dx > (GLOBE_PX_W >> 1)) dx = (int16_t)(dx - GLOBE_PX_W);
    else if (dx < -(GLOBE_PX_W >> 1)) dx = (int16_t)(dx + GLOBE_PX_W);
    return dx;
}

static int16_t wrap_delta_y(int16_t dy) {
    if (dy > (GLOBE_PX_H >> 1)) dy = (int16_t)(dy - GLOBE_PX_H);
    else if (dy < -(GLOBE_PX_H >> 1)) dy = (int16_t)(dy + GLOBE_PX_H);
    return dy;
}

static int16_t world_dx(const HearthState *st, int16_t a, int16_t b) {
    int16_t d = (int16_t)(a - b);
    ST_UNUSED(st);
    return HS.is_world_map ? wrap_delta_x(d) : d;
}

static int16_t world_dy(const HearthState *st, int16_t a, int16_t b) {
    int16_t d = (int16_t)(a - b);
    ST_UNUSED(st);
    return HS.is_world_map ? wrap_delta_y(d) : d;
}

static uint8_t region_to_pal(uint8_t reg) {
    if (reg == REGION_DESERT)    return (uint8_t)ZPAL_DESERT;
    if (reg == REGION_PARADISE)  return (uint8_t)ZPAL_PARADISE;
    if (reg == REGION_FROSTMERE) return (uint8_t)ZPAL_FROST;
    if (reg == REGION_CANOPY)    return (uint8_t)ZPAL_JUNGLE;
    if (reg == REGION_VOLCANO)   return (uint8_t)ZPAL_VOLCANO;
    if (reg == REGION_AETHERIS)  return (uint8_t)ZPAL_TECH;
    return (uint8_t)ZPAL_EVERTALE_WORLD;
}

static uint8_t region_to_bgm(uint8_t reg) {
    if (reg == REGION_DESERT)    return (uint8_t)BGM_DESERT;
    if (reg == REGION_PARADISE)  return (uint8_t)BGM_PARADISE;
    if (reg == REGION_FROSTMERE) return (uint8_t)BGM_FROST;
    if (reg == REGION_CANOPY)    return (uint8_t)BGM_JUNGLE;
    if (reg == REGION_VOLCANO)   return (uint8_t)BGM_VOLCANO;
    if (reg == REGION_AETHERIS)  return (uint8_t)BGM_TECH;
    return (uint8_t)BGM_CINEMA_WONDER;
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
 * INITIALIZATION, PARTY & 5-ZONE WORLD BUILDERS
 * ============================================================================ */
void hearth_default_tuning(HearthTuning *t) {
    t->ppu_mode           = PPU_GENESIS_320x224;
    t->walk_speed_fp      = 384; /* 1.50 px/frame */
    t->dash_speed_fp      = 614; /* 2.40 px/frame */
    t->accel_fp           = 128;
    t->follow_dist_px     = 22;
    t->hitstop_normal_f   = 4;
    t->hitstop_crit_f     = 8;
    t->hitstop_parry_f    = 6;
    t->timed_hit_window_f = 15;
    t->parry_window_f     = 16;
    t->show_hitboxes      = false;
}

static void init_party(HearthState *st) {
    static const HearthPartyMember default_party[3] = {
        { 0, "Rowan",      "Hearth Wand",  "Mending Dew", SPR_HERO,             88, 88, 34, 34, 16,  8, LOCK_SWORD, LOCK_NONE, 7, true,  false },
        { 1, "Bramblekit", "Acorn Rush",   "Solar Slam",  SPR_FAMILIAR_BRAMBLE, 98, 98, 26, 26, 20, 11, LOCK_SWORD, LOCK_SUN,  6, false, false },
        { 2, "Lumina",     "Starlight",    "Prism Nova",  SPR_FAMILIAR_LUMINA,  74, 74, 42, 42, 18,  7, LOCK_STAR,  LOCK_STAR, 9, false, true  }
    };
    st->party[0] = default_party[0];
    st->party[1] = default_party[1];
    st->party[2] = default_party[2];
    st->active_slot = 1;
    st->guilders = 45;
    st->potions = 3;
    st->star_drops = 1;
    st->snacks = 0;
    st->babanas = 0;
    st->lure_tier = 0;
}

static void snap_cam_to_hero(HearthState *st) {
    uint16_t w, h;
    uint8_t lim;
    int16_t max_x, max_y, tx, ty;
    int16_t mw, mh;
    get_ppu_dims(st, &w, &h, &lim);
    if (st->is_world_map) {
        tx = wrap_globe_px_x((int16_t)(FP_PX(st->hero_fx) - (int16_t)(w >> 1)));
        ty = wrap_globe_px_y((int16_t)(FP_PX(st->hero_fy) - (int16_t)(h >> 1)));
        st->cam_fx = TO_FP(tx);
        st->cam_fy = TO_FP(ty);
        return;
    }
    mw = (int16_t)(st->map_w ? st->map_w : (uint8_t)HEARTH_MAP_W);
    mh = (int16_t)(st->map_h ? st->map_h : (uint8_t)HEARTH_MAP_H);
    max_x = (int16_t)(mw * HEARTH_TILE_SIZE - (int)w);
    max_y = (int16_t)(mh * HEARTH_TILE_SIZE - (int)h);
    tx = iclamp16((int16_t)(FP_PX(st->hero_fx) - (int16_t)(w >> 1)), 0, max_x);
    ty = iclamp16((int16_t)(FP_PX(st->hero_fy) - (int16_t)(h >> 1)), 0, max_y);
    st->cam_fx = TO_FP(tx);
    st->cam_fy = TO_FP(ty);
}

static void reset_trail_to_hero(HearthState *st) {
    int i;
    fp16_t px = st->hero_fx - TO_FP(16);
    fp16_t py = st->hero_fy - TO_FP(2);
    if (st->is_world_map) {
        px = wrap_globe_fp_x(px);
        py = wrap_globe_fp_y(py);
    }
    for (i = 0; i < HEARTH_TRAIL_LEN; i++) {
        st->trail_x[i] = px;
        st->trail_y[i] = py;
    }
    st->trail_head = 0;
    st->pip_fx = px;
    st->pip_fy = py;
    st->pip_dir = DIR_RIGHT;
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

static void set_npc(
    HearthNpc *n, uint8_t id, const char *name, uint8_t sprite_id,
    int16_t tx, int16_t ty, uint8_t dir, uint8_t aura
) {
    n->id = id;
    n->active = true;
    n->name = name;
    n->sprite_id = sprite_id;
    n->fx = TO_FP(tx * 16 + 8);
    n->fy = TO_FP(ty * 16 + 8);
    n->dir = dir;
    n->aura = aura;
    n->emote = 0;
    n->emote_frames = 0;
}

static void set_portal(
    HearthPortal *pt, uint8_t tx, uint8_t ty, uint8_t dst_zone,
    uint8_t dst_tx, uint8_t dst_ty, uint8_t req_stage, const char *label
) {
    pt->active = true;
    pt->tx = tx;
    pt->ty = ty;
    pt->dst_zone = dst_zone;
    pt->dst_tx = dst_tx;
    pt->dst_ty = dst_ty;
    pt->req_stage = req_stage;
    pt->label = label;
}

static uint8_t tod_to_mountain_pal(uint8_t tod) {
    if (tod == TOD_MORNING) return (uint8_t)ZPAL_AUTUMN_MORNING;
    if (tod == TOD_DUSK)    return (uint8_t)ZPAL_AUTUMN_DUSK;
    if (tod == TOD_NIGHT)   return (uint8_t)ZPAL_AUTUMN_NIGHT;
    return (uint8_t)ZPAL_ALDERBROOK;
}

static void clear_zone_entities(HearthState *st) {
    int i;
    for (i = 0; i < HEARTH_MAX_NPCS; i++) st->npcs[i].active = false;
    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++) st->field_enemies[i].alive = false;
    for (i = 0; i < HEARTH_MAX_PORTALS; i++) st->portals[i].active = false;
    st->fish_spot.active = false;
    st->chest.active = false;
    st->has_waystone = false;
    st->has_sign = false;
    st->has_windmill = false;
    st->has_locomotive = false;
    st->has_mountain_vista = false;
    st->zone_is_dark = false;
    st->is_world_map = false;
}

static void populate_zone_rowan_home(HearthState *st) {
    st->terrain_pal_id = ZPAL_COTTAGE_INTERIOR;
    st->bgm_id = (st->time_of_day >= TOD_DUSK) ? (uint8_t)BGM_CINEMA_LULLABY : (uint8_t)BGM_ALDERBROOK;
    st->zone_is_dark = (st->time_of_day == TOD_NIGHT);
    st->pip_visible = false;

    set_npc(&st->npcs[0], 0, "Grandma Mara", SPR_NPC_MARA, 19, 12, DIR_DOWN, AURA_NONE);
    if (st->time_of_day != TOD_NIGHT && st->ch1_step < 6u) {
        set_npc(&st->npcs[1], 1, "Mido", SPR_PET_MILO, 22, 11, DIR_LEFT, AURA_NONE);
    }
    set_portal(&st->portals[0], 20, 20, ZONE_ALDERBROOK, 11, 14, 0, "Step Outside");
    set_portal(&st->portals[1], 21, 20, ZONE_ALDERBROOK, 11, 14, 0, "Step Outside");
}

static void populate_zone_alderbrook(HearthState *st) {
    st->terrain_pal_id = tod_to_mountain_pal(st->time_of_day);
    st->bgm_id = (st->time_of_day == TOD_NIGHT) ? (uint8_t)BGM_CINEMA_LULLABY : (uint8_t)BGM_ALDERBROOK;
    st->zone_is_dark = (st->time_of_day == TOD_NIGHT);
    st->has_mountain_vista = true;
    st->has_locomotive = true;
    st->loco_tx = 20;
    st->loco_ty = 5;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_sign = true;
    st->sign_tx = 18;
    st->sign_ty = 14;

    st->fish_spot.active = true;
    st->fish_spot.tx = 22;
    st->fish_spot.ty = 20;
    st->fish_spot.cast_dx = 0;
    st->fish_spot.cast_dy = 2;

    st->chest.active = true;
    st->chest.tx = 5;
    st->chest.ty = 24;
    st->chest.chest_bit = 4;
    st->chest.reward_kind = 0;

    if (st->time_of_day == TOD_NIGHT) {
        set_npc(&st->npcs[0], 0, "Mido", SPR_PET_MILO, 36, 13, DIR_RIGHT, AURA_NONE);
    } else {
        set_npc(&st->npcs[0], 0, "Barnaby",     SPR_NPC_BARNABY, 13,  7, DIR_DOWN,  AURA_NONE);
        set_npc(&st->npcs[1], 1, "Baker Maple", SPR_NPC_MAPLE,   24, 13, DIR_DOWN,  AURA_NONE);
        set_npc(&st->npcs[2], 2, "Jasper",      SPR_NPC_JASPER,  13, 23, DIR_RIGHT, AURA_NONE);
        set_npc(&st->npcs[3], 3, "Mido",        SPR_PET_MILO,    12, 14, DIR_RIGHT, AURA_NONE);
    }

    set_portal(&st->portals[0], 11, 13, ZONE_ROWAN_HOME,       20, 19, 0, "Rowan's Cottage");
    set_portal(&st->portals[1], 41,  9, ZONE_WHISPERING_WOODS,  3, 21, 0, "Whispering Woods");
}

static void populate_zone_whispering_woods(HearthState *st) {
    st->terrain_pal_id = tod_to_mountain_pal(st->time_of_day);
    st->bgm_id = (st->time_of_day == TOD_NIGHT) ? (uint8_t)BGM_CINEMA_LULLABY : (uint8_t)BGM_ALDERBROOK;
    st->zone_is_dark = (st->time_of_day == TOD_NIGHT);
    st->has_mountain_vista = true;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_sign = true;
    st->sign_tx = 7;
    st->sign_ty = 20;

    st->chest.active = true;
    st->chest.tx = 38;
    st->chest.ty = 21;
    st->chest.chest_bit = 5;
    st->chest.reward_kind = 1;

    set_npc(&st->npcs[0], 0, "Jasper", SPR_NPC_JASPER, 12, 11, DIR_DOWN, AURA_NONE);
    if (st->ch1_step >= 6u) {
        set_npc(&st->npcs[1], 1, "Mido", SPR_PET_MILO, 16, 15, DIR_RIGHT, AURA_NONE);
    }

    set_portal(&st->portals[0],  2, 21, ZONE_ALDERBROOK,   39,  9, 0, "To Alderbrook");
    set_portal(&st->portals[1], 37,  4, ZONE_HOLLOW_GLADE, 21, 23, 0, "Hollow Glade");
}

static void populate_zone_hollow_glade(HearthState *st) {
    st->terrain_pal_id = tod_to_mountain_pal(st->time_of_day);
    st->bgm_id = (st->time_of_day == TOD_NIGHT) ? (uint8_t)BGM_CINEMA_LULLABY : (uint8_t)BGM_ALDERBROOK;
    st->zone_is_dark = (st->time_of_day == TOD_NIGHT);
    st->has_mountain_vista = true;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);

    if (st->ch1_step >= 6u && st->story_stage < STORY_EVERTALE_AWAKENED) {
        set_npc(&st->npcs[0], 0, "Mido", SPR_PET_MILO, 19, 13, DIR_RIGHT, AURA_NONE);
    }
    set_portal(&st->portals[0], 21, 25, ZONE_WHISPERING_WOODS, 37, 6, 0, "Whispering Woods");
}

static void populate_globe_enemies_for_region(HearthState *st, uint8_t reg) {
    int i;
    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++) st->field_enemies[i].alive = false;
    st->globe_enemy_region = reg;
    /* World Map stays 100% peaceful until Rowan bonds his first Familiar! */
    if (!st->flag_has_familiar) return;

    if (reg == REGION_DESERT) {
        set_field_enemy(&st->field_enemies[0], "Dune Scorpion",  SPR_ENEMY_SCORPION, ENCOUNTER_DESERT_SCORPION, BIOME_DESERT, false, 120, 148, DIR_LEFT,  30);
        set_field_enemy(&st->field_enemies[1], "Dune Scorpion",  SPR_ENEMY_SCORPION, ENCOUNTER_DESERT_SCORPION, BIOME_DESERT, false, 142, 142, DIR_DOWN,  55);
        set_field_enemy(&st->field_enemies[2], "Dune Scorpion",  SPR_ENEMY_SCORPION, ENCOUNTER_DESERT_SCORPION, BIOME_DESERT, false, 146, 158, DIR_LEFT,  80);
        set_field_enemy(&st->field_enemies[3], "Giant Sandworm", SPR_ENEMY_SANDWORM, ENCOUNTER_SANDWORM_BOSS,   BIOME_DESERT, true,  124, 160, DIR_RIGHT,  0);
    } else if (reg == REGION_HEARTLAND) {
        set_field_enemy(&st->field_enemies[0], "Gloomcap",   SPR_ENEMY_GLOOMCAP,  ENCOUNTER_MEADOW_PACK, BIOME_MEADOW, false,  88,  92, DIR_LEFT,  30);
        set_field_enemy(&st->field_enemies[1], "Thornling",  SPR_ENEMY_THORNLING, ENCOUNTER_GLADE_DUO,   BIOME_MEADOW, false, 118,  86, DIR_DOWN,  55);
        set_field_enemy(&st->field_enemies[2], "Dusk Moth",  SPR_ENEMY_DUSK_MOTH, ENCOUNTER_GLADE_DUO,   BIOME_MEADOW, false, 110,  98, DIR_LEFT,  80);
        set_field_enemy(&st->field_enemies[3], "Boar Scout", SPR_ENEMY_BOAR,      ENCOUNTER_BOAR_GUARD,  BIOME_MEADOW, false, 124,  76, DIR_RIGHT, 45);
    }
}

static void update_globe_region_streaming(HearthState *st, bool show_banner) {
    uint16_t tx, ty;
    uint8_t reg;
    if (!st->is_world_map) return;
    tx = (uint16_t)((uint16_t)wrap_globe_px_x(FP_PX(st->hero_fx)) >> 4);
    ty = (uint16_t)((uint16_t)wrap_globe_px_y(FP_PX(st->hero_fy)) >> 4);
    reg = hearth_globe_region_at(tx, ty);
    if (reg != st->world_region_id) {
        st->world_region_id = reg;
        st->terrain_pal_id = region_to_pal(reg);
        st->bgm_id = region_to_bgm(reg);
        if (show_banner) st->zone_banner_frames = 100;
        if (reg != st->globe_enemy_region && (reg == REGION_HEARTLAND || reg == REGION_DESERT)) {
            populate_globe_enemies_for_region(st, reg);
        }
    }
}

static void populate_zone_evertale_world(HearthState *st) {
    uint16_t tx = (uint16_t)((uint16_t)wrap_globe_px_x(FP_PX(st->hero_fx)) >> 4);
    uint16_t ty = (uint16_t)((uint16_t)wrap_globe_px_y(FP_PX(st->hero_fy)) >> 4);
    uint8_t reg = hearth_globe_region_at(tx, ty);
    st->is_world_map = true;
    st->has_windmill = true;
    st->world_region_id = reg;
    st->terrain_pal_id = region_to_pal(reg);
    st->bgm_id = region_to_bgm(reg);
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);

    /* All 14 landmarks across 7 continents live in ROM table HEARTH_GLOBE_LANDMARKS[]! */
    populate_globe_enemies_for_region(st, (reg == REGION_DESERT) ? (uint8_t)REGION_DESERT : (uint8_t)REGION_HEARTLAND);
}

static void populate_zone_timber_pier(HearthState *st) {
    st->terrain_pal_id = ZPAL_EVERTALE_WORLD;
    st->bgm_id = BGM_HAMLET;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_sign = true;
    st->sign_tx = 18;
    st->sign_ty = 8;

    st->chest.active = true;
    st->chest.tx = 35;
    st->chest.ty = 11;
    st->chest.chest_bit = 8;
    st->chest.reward_kind = 0;

    st->fish_spot.active = true;
    st->fish_spot.tx = 26;
    st->fish_spot.ty = 22;
    st->fish_spot.cast_dx = 2;
    st->fish_spot.cast_dy = 0;

    set_npc(&st->npcs[0], 0, "Shipwright Orin", SPR_NPC_SHIPWRIGHT, 21, 16, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Deckhand Kip",    SPR_NPC_TOWNSFOLK,  15, 11, DIR_RIGHT, AURA_NONE);

    set_portal(&st->portals[0], 21, 1, ZONE_EVERTALE_WORLD, 108, 115, 0, "Evertale Globe");
    set_portal(&st->portals[1], 22, 1, ZONE_EVERTALE_WORLD, 108, 115, 0, "Evertale Globe");
}

static void populate_zone_coral_cove(HearthState *st) {
    st->terrain_pal_id = ZPAL_PARADISE;
    st->bgm_id = BGM_PARADISE;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_sign = true;
    st->sign_tx = 15;
    st->sign_ty = 16;

    st->chest.active = true;
    st->chest.tx = 8;
    st->chest.ty = 22;
    st->chest.chest_bit = 9;
    st->chest.reward_kind = 1;

    st->fish_spot.active = true;
    st->fish_spot.tx = 30;
    st->fish_spot.ty = 13;
    st->fish_spot.cast_dx = 2;
    st->fish_spot.cast_dy = 0;

    set_npc(&st->npcs[0], 0, "Capt. Barnacle", SPR_NPC_SHIPWRIGHT, 21, 13, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Diver Maya",     SPR_NPC_CLARA,      26, 15, DIR_LEFT, AURA_NONE);
    set_npc(&st->npcs[2], 2, "Reef Monkey",    SPR_FAUNA_MONKEY,   11, 18, DIR_RIGHT, AURA_NONE);

    set_portal(&st->portals[0], 21, 1, ZONE_EVERTALE_WORLD, 52, 148, 0, "Evertale Globe");
    set_portal(&st->portals[1], 22, 1, ZONE_EVERTALE_WORLD, 52, 148, 0, "Evertale Globe");
}

static void populate_zone_babana_oasis(HearthState *st) {
    st->terrain_pal_id = ZPAL_DESERT;
    st->bgm_id = BGM_DESERT;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_waystone = true;
    st->waystone_tx = 21;
    st->waystone_ty = 15;
    st->has_sign = true;
    st->sign_tx = 18;
    st->sign_ty = 22;

    st->chest.active = true;
    st->chest.tx = 36;
    st->chest.ty = 12;
    st->chest.chest_bit = 10;
    st->chest.reward_kind = 2;

    set_npc(&st->npcs[0], 0, "Merchant Zari", SPR_NPC_TESSA,    12, 13, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Guide Farid",   SPR_NPC_CORIN,    27, 15, DIR_LEFT, AURA_NONE);
    set_npc(&st->npcs[2], 2, "Oasis Camel",   SPR_FAUNA_CAMEL,  15, 18, DIR_RIGHT, AURA_NONE);

    set_portal(&st->portals[0], 21, 26, ZONE_EVERTALE_WORLD, 132, 156, 0, "Evertale Globe");
    set_portal(&st->portals[1], 22, 26, ZONE_EVERTALE_WORLD, 132, 156, 0, "Evertale Globe");
}

static void populate_zone_sunhill_market(HearthState *st) {
    st->terrain_pal_id = ZPAL_SUNHILL_MARKET;
    st->bgm_id = BGM_HAMLET;
    st->has_mountain_vista = true;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_sign = true;
    st->sign_tx = 18;
    st->sign_ty = 21;

    st->chest.active = true;
    st->chest.tx = 36;
    st->chest.ty = 10;
    st->chest.chest_bit = 6;
    st->chest.reward_kind = 0;

    set_npc(&st->npcs[0], 0, "Tessa the Baker", SPR_NPC_TESSA,     12, 18, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Archivist Corin", SPR_NPC_CORIN,     31,  9, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[2], 2, "Mido",            SPR_PET_MILO,      22, 11, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[3], 3, "Market Florist",  SPR_NPC_TOWNSFOLK, 26, 18, DIR_LEFT, AURA_NONE);

    set_portal(&st->portals[0], 21, 26, ZONE_EVERTALE_WORLD, 104, 84, 0, "Evertale Globe");
    set_portal(&st->portals[1], 22, 26, ZONE_EVERTALE_WORLD, 104, 84, 0, "Evertale Globe");
}

static void populate_zone_castle_gate(HearthState *st) {
    st->terrain_pal_id = ZPAL_SUNHILL_MARKET;
    st->bgm_id = BGM_HAMLET;
    st->has_mountain_vista = true;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_sign = true;
    st->sign_tx = 17;
    st->sign_ty = 23;

    set_npc(&st->npcs[0], 0, "Royal Sentinel", SPR_NPC_SENTINEL, 19, 11, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Royal Sentinel", SPR_NPC_SENTINEL, 24, 11, DIR_DOWN, AURA_NONE);

    set_portal(&st->portals[0], 21, 26, ZONE_EVERTALE_WORLD, 104, 70, 0, "Evertale Globe");
    set_portal(&st->portals[1], 22, 26, ZONE_EVERTALE_WORLD, 104, 70, 0, "Evertale Globe");
}

static void populate_zone_bramble_edge(HearthState *st) {
    st->terrain_pal_id = ZPAL_HAMLET;
    st->bgm_id = BGM_HAMLET;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_sign = true;
    st->sign_tx = 12;
    st->sign_ty = 16;

    st->chest.active = true;
    st->chest.tx = 22;
    st->chest.ty = 18;
    st->chest.chest_bit = 7;
    st->chest.reward_kind = 1;

    set_npc(&st->npcs[0], 0, "Lantern Keeper", SPR_NPC_JASPER, 16, 13, DIR_DOWN, AURA_NONE);

    set_portal(&st->portals[0], 1, 13, ZONE_EVERTALE_WORLD, 132, 88, 0, "Evertale Globe");
    set_portal(&st->portals[1], 1, 14, ZONE_EVERTALE_WORLD, 132, 88, 0, "Evertale Globe");
}

static void populate_zone_sunken_ruins(HearthState *st) {
    st->terrain_pal_id = ZPAL_HAMLET;
    st->bgm_id = BGM_HAMLET;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_sign = true;
    st->sign_tx = 11;
    st->sign_ty = 15;

    set_npc(&st->npcs[0], 0, "Antiquarian", SPR_NPC_ELDER, 21, 11, DIR_DOWN, AURA_NONE);

    set_portal(&st->portals[0], 1, 12, ZONE_EVERTALE_WORLD, 130, 108, 0, "Evertale Globe");
    set_portal(&st->portals[1], 1, 13, ZONE_EVERTALE_WORLD, 130, 108, 0, "Evertale Globe");
}

static void populate_zone_hamlet(HearthState *st) {
    st->terrain_pal_id = ZPAL_HAMLET;
    st->bgm_id = BGM_HAMLET;
    st->has_windmill = true;

    if (st->story_stage == STORY_AGE5_FLASHBACK) {
        st->pip_visible = false;
        set_npc(&st->npcs[0], 0, "Mido", SPR_PET_MILO, 17, 13, DIR_RIGHT, AURA_NONE);
        set_npc(&st->npcs[1], 1, "Shadow Matron", SPR_NPC_SHADOW_GRAN, 22, 13, DIR_LEFT, AURA_NONE);
        st->npcs[1].active = false; /* Revealed by OP_SHOW_ACTOR during flashback */
        return;
    }

    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_waystone = true;
    st->waystone_tx = 8;
    st->waystone_ty = 17;
    st->has_sign = true;
    st->sign_tx = 16;
    st->sign_ty = 15;

    st->chest.active = true;
    st->chest.tx = 26;
    st->chest.ty = 6;
    st->chest.chest_bit = 0;
    st->chest.reward_kind = 0;

    st->fish_spot.active = true;
    st->fish_spot.tx = 17;
    st->fish_spot.ty = 18;
    st->fish_spot.cast_dx = 2;
    st->fish_spot.cast_dy = 0;

    set_npc(&st->npcs[0], 0, "Elder Wren", SPR_NPC_ELDER,  8, 11, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Clara",      SPR_NPC_CLARA, 13, 16, DIR_DOWN,
        st->flag_has_heart_vessel
            ? ((st->flag_has_courage || st->flag_guard_mended) ? AURA_MENDED : AURA_RADIANT_COURAGE)
            : AURA_NONE);
    set_npc(&st->npcs[2], 2, "Guard Bram", SPR_NPC_GUARD, 22, 13, DIR_LEFT,
        st->flag_has_heart_vessel
            ? (st->flag_guard_mended ? AURA_MENDED : AURA_HOLLOW_FEAR)
            : AURA_NONE);

    set_portal(&st->portals[0], 2,  13, ZONE_EVERTALE_WORLD, 78, 88, 0, "Evertale Globe");
    set_portal(&st->portals[1], 42, 13, ZONE_WHISKERBURGH,    2, 13, 0, "To Whiskerburgh");

    if (st->flag_has_familiar) {
        set_field_enemy(&st->field_enemies[0], "Gloomcap",     SPR_ENEMY_GLOOMCAP,  ENCOUNTER_MEADOW_PACK, BIOME_MEADOW, false, 26, 15, DIR_LEFT, 30);
        set_field_enemy(&st->field_enemies[1], "Thornling",    SPR_ENEMY_THORNLING, ENCOUNTER_GLADE_DUO,   BIOME_MEADOW, false, 30, 10, DIR_DOWN, 55);
        set_field_enemy(&st->field_enemies[2], "Dusk Moth",    SPR_ENEMY_DUSK_MOTH, ENCOUNTER_GLADE_DUO,   BIOME_MEADOW, false, 31, 20, DIR_LEFT, 80);
        set_field_enemy(&st->field_enemies[3], "Briar Warden", SPR_BOSS_BRIAR,      ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, true,  38, 13, DIR_LEFT,  0);
        if (st->flag_boss_defeated) st->field_enemies[3].alive = false;
    }
}

static void populate_zone_whiskerburgh(HearthState *st) {
    st->terrain_pal_id = ZPAL_WHISKERBURGH;
    st->bgm_id = BGM_WHISKERBURGH;
    st->pip_visible = (st->ch2_step >= 3u || st->flag_has_familiar);
    st->has_waystone = true;
    st->waystone_tx = 11;
    st->waystone_ty = 15;
    st->has_sign = true;
    st->sign_tx = 16;
    st->sign_ty = 13;

    st->chest.active = true;
    st->chest.tx = 35;
    st->chest.ty = 14;
    st->chest.chest_bit = 1;
    st->chest.reward_kind = 1;

    st->fish_spot.active = true;
    st->fish_spot.tx = 20;
    st->fish_spot.ty = 20;
    st->fish_spot.cast_dx = 0;
    st->fish_spot.cast_dy = 2;

    set_npc(&st->npcs[0], 0, "Mayor Brie",   SPR_NPC_MOUSE, 12,  9, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Barnaby Shop", SPR_NPC_MOUSE, 29,  9, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[2], 2, "Angler Pippo", SPR_NPC_MOUSE, 22, 16, DIR_LEFT, AURA_NONE);

    set_portal(&st->portals[0],  1, 13, ZONE_EVERTALE_WORLD, 138, 76, 0, "Evertale Globe");
    set_portal(&st->portals[1], 21,  2, ZONE_SEWERS,          21, 24, STORY_SEWER_KEY, "Sunken Sewers");
}

static void populate_zone_sewers(HearthState *st) {
    st->terrain_pal_id = ZPAL_SEWERS;
    st->bgm_id = BGM_SEWERS;
    st->zone_is_dark = true;
    st->pip_visible = true;
    st->has_waystone = true;
    st->waystone_tx = 23;
    st->waystone_ty = 21;
    st->has_sign = true;
    st->sign_tx = 20;
    st->sign_ty = 21;

    st->chest.active = true;
    st->chest.tx = 35;
    st->chest.ty = 20;
    st->chest.chest_bit = 2;
    st->chest.reward_kind = 2;

    st->fish_spot.active = true;
    st->fish_spot.tx = 10;
    st->fish_spot.ty = 20;
    st->fish_spot.cast_dx = 0;
    st->fish_spot.cast_dy = 2;

    set_portal(&st->portals[0], 21, 25, ZONE_WHISKERBURGH, 21,  4, 0, "To Whiskerburgh");
    set_portal(&st->portals[1], 21,  2, ZONE_BOARHOLD,     21, 24, STORY_MUCKMAW_DEFEATED, "Boarhold Sluice");

    set_field_enemy(&st->field_enemies[0], "Gutter Rat",   SPR_ENEMY_RAT,    ENCOUNTER_SEWER_PACK, BIOME_SEWER, false, 14, 11, DIR_RIGHT, 25);
    set_field_enemy(&st->field_enemies[1], "Mire Slime",   SPR_ENEMY_SLIME,  ENCOUNTER_SEWER_PACK, BIOME_SEWER, false, 29, 11, DIR_LEFT,  45);
    set_field_enemy(&st->field_enemies[2], "Sewer Rat",    SPR_ENEMY_RAT,    ENCOUNTER_SEWER_PACK, BIOME_SEWER, false, 15, 16, DIR_DOWN,  70);
    set_field_enemy(&st->field_enemies[3], "King Muckmaw", SPR_BOSS_MUCKMAW, ENCOUNTER_SEWER_BOSS, BIOME_SEWER, true,  21,  5, DIR_DOWN,   0);
    if (st->flag_muckmaw_defeated) st->field_enemies[3].alive = false;
}

static void populate_zone_boarhold(HearthState *st) {
    st->terrain_pal_id = ZPAL_BOARHOLD;
    st->bgm_id = BGM_BOARHOLD;
    st->pip_visible = true;
    st->has_waystone = true;
    st->waystone_tx = 12;
    st->waystone_ty = 19;
    st->has_sign = true;
    st->sign_tx = 14;
    st->sign_ty = 15;

    st->chest.active = true;
    st->chest.tx = 7;
    st->chest.ty = 20;
    st->chest.chest_bit = 3;
    st->chest.reward_kind = 3;

    set_npc(&st->npcs[0], 0, "Merchant Porkins", SPR_NPC_PIG,  9,  9, DIR_DOWN, AURA_NONE);
    set_npc(&st->npcs[1], 1, "Captain Tusk",     SPR_NPC_PIG, 18,  9, DIR_DOWN,
        st->flag_has_heart_vessel
            ? (st->flag_captain_mended ? AURA_MENDED : AURA_HOLLOW_FEAR)
            : AURA_NONE);
    set_npc(&st->npcs[2], 2, "Squire Snout",     SPR_NPC_PIG, 24, 13, DIR_LEFT, AURA_NONE);

    set_portal(&st->portals[0], 21, 25, ZONE_SEWERS, 21, 4, 0, "To Sewers");

    set_field_enemy(&st->field_enemies[0], "Boar Guard",    SPR_ENEMY_BOAR,    ENCOUNTER_BOAR_GUARD,    BIOME_BOARHOLD, false, 22, 17, DIR_LEFT, 35);
    set_field_enemy(&st->field_enemies[1], "Iron Halberd",  SPR_ENEMY_BOAR,    ENCOUNTER_BOAR_GUARD,    BIOME_BOARHOLD, false, 33, 10, DIR_DOWN, 60);
    set_field_enemy(&st->field_enemies[3], "Lord Ironfang", SPR_BOSS_IRONFANG, ENCOUNTER_IRONFANG_BOSS, BIOME_BOARHOLD, true,  37, 13, DIR_LEFT,  0);
    if (st->flag_ironfang_defeated) st->field_enemies[3].alive = false;
}

void hearth_enter_zone(HearthState *st, uint8_t zone_id, uint16_t tx, uint16_t ty) {
    if (zone_id >= ZONE_COUNT) zone_id = ZONE_EVERTALE_WORLD;
    st->zone_id = zone_id;
    st->map_revision++;
    st->zone_banner_frames = 120;
    st->cam_override = false;
    st->atlas_open = false;
    st->hero_vx = 0;
    st->hero_vy = 0;
    if (zone_id == ZONE_EVERTALE_WORLD) {
        st->hero_fx = TO_FP((int16_t)((tx & 255u) * 16u + 8u));
        st->hero_fy = TO_FP((int16_t)((ty % HEARTH_GLOBE_H) * 16u + 8u));
    } else {
        st->hero_fx = TO_FP((int16_t)tx * 16 + 8);
        st->hero_fy = TO_FP((int16_t)ty * 16 + 8);
        st->active_vehicle = VEH_FOOT;
    }
    st->hero_safe_frames = 45;

    clear_zone_entities(st);
    hearth_build_zone_map(zone_id, st->map_bg1, st->map_bg2, st->map_col, &st->map_w, &st->map_h);

    if (zone_id == ZONE_ALDERBROOK)            populate_zone_alderbrook(st);
    else if (zone_id == ZONE_ROWAN_HOME)       populate_zone_rowan_home(st);
    else if (zone_id == ZONE_WHISPERING_WOODS) populate_zone_whispering_woods(st);
    else if (zone_id == ZONE_HOLLOW_GLADE)     populate_zone_hollow_glade(st);
    else if (zone_id == ZONE_EVERTALE_WORLD)   populate_zone_evertale_world(st);
    else if (zone_id == ZONE_SUNHILL_MARKET)   populate_zone_sunhill_market(st);
    else if (zone_id == ZONE_CASTLE_GATE)      populate_zone_castle_gate(st);
    else if (zone_id == ZONE_BRAMBLE_EDGE)     populate_zone_bramble_edge(st);
    else if (zone_id == ZONE_SUNKEN_RUINS)     populate_zone_sunken_ruins(st);
    else if (zone_id == ZONE_TIMBER_PIER)      populate_zone_timber_pier(st);
    else if (zone_id == ZONE_CORAL_COVE)       populate_zone_coral_cove(st);
    else if (zone_id == ZONE_BABANA_OASIS)     populate_zone_babana_oasis(st);
    else if (zone_id == ZONE_WHISKERBURGH)     populate_zone_whiskerburgh(st);
    else if (zone_id == ZONE_SEWERS)           populate_zone_sewers(st);
    else if (zone_id == ZONE_BOARHOLD)         populate_zone_boarhold(st);
    else                                       populate_zone_hamlet(st);

    reset_trail_to_hero(st);

    if (zone_id == ZONE_WHISPERING_WOODS && st->ch1_step == 6u) {
        st->ch1_step = 7u;
    }

    snap_cam_to_hero(st);
    build_ppu_packet(st);
}

void hearth_respawn_enemies(HearthState *st) {
    if (st->zone_id == ZONE_EVERTALE_WORLD && st->flag_has_familiar) {
        populate_globe_enemies_for_region(st, (st->world_region_id == REGION_DESERT) ? (uint8_t)REGION_DESERT : (uint8_t)REGION_HEARTLAND);
    } else if (st->zone_id == ZONE_HAMLET && st->story_stage != STORY_AGE5_FLASHBACK && st->flag_has_familiar) {
        set_field_enemy(&st->field_enemies[0], "Gloomcap",     SPR_ENEMY_GLOOMCAP,  ENCOUNTER_MEADOW_PACK, BIOME_MEADOW, false, 26, 15, DIR_LEFT, 30);
        set_field_enemy(&st->field_enemies[1], "Thornling",    SPR_ENEMY_THORNLING, ENCOUNTER_GLADE_DUO,   BIOME_MEADOW, false, 30, 10, DIR_DOWN, 55);
        set_field_enemy(&st->field_enemies[2], "Dusk Moth",    SPR_ENEMY_DUSK_MOTH, ENCOUNTER_GLADE_DUO,   BIOME_MEADOW, false, 31, 20, DIR_LEFT, 80);
        set_field_enemy(&st->field_enemies[3], "Briar Warden", SPR_BOSS_BRIAR,      ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, true,  38, 13, DIR_LEFT,  0);
    } else if (st->zone_id == ZONE_SEWERS) {
        set_field_enemy(&st->field_enemies[0], "Gutter Rat",   SPR_ENEMY_RAT,    ENCOUNTER_SEWER_PACK, BIOME_SEWER, false, 14, 11, DIR_RIGHT, 25);
        set_field_enemy(&st->field_enemies[1], "Mire Slime",   SPR_ENEMY_SLIME,  ENCOUNTER_SEWER_PACK, BIOME_SEWER, false, 29, 11, DIR_LEFT,  45);
        set_field_enemy(&st->field_enemies[2], "Sewer Rat",    SPR_ENEMY_RAT,    ENCOUNTER_SEWER_PACK, BIOME_SEWER, false, 15, 16, DIR_DOWN,  70);
        set_field_enemy(&st->field_enemies[3], "King Muckmaw", SPR_BOSS_MUCKMAW, ENCOUNTER_SEWER_BOSS, BIOME_SEWER, true,  21,  5, DIR_DOWN,   0);
    } else if (st->zone_id == ZONE_BOARHOLD) {
        set_field_enemy(&st->field_enemies[0], "Boar Guard",    SPR_ENEMY_BOAR,    ENCOUNTER_BOAR_GUARD,    BIOME_BOARHOLD, false, 22, 17, DIR_LEFT, 35);
        set_field_enemy(&st->field_enemies[1], "Iron Halberd",  SPR_ENEMY_BOAR,    ENCOUNTER_BOAR_GUARD,    BIOME_BOARHOLD, false, 33, 10, DIR_DOWN, 60);
        set_field_enemy(&st->field_enemies[3], "Lord Ironfang", SPR_BOSS_IRONFANG, ENCOUNTER_IRONFANG_BOSS, BIOME_BOARHOLD, true,  37, 13, DIR_LEFT,  0);
    }
}

/* ============================================================================
 * 7-ACT OPENING CINEMA & MID-CHAPTER STORY CG DIRECTOR
 * ============================================================================ */
static const HearthCinemaBeat *get_cinema_script(uint8_t script_id, uint8_t *out_count) {
    if (script_id == 1u) { *out_count = (uint8_t)HEARTH_CG_ATTIC_COUNT;         return HEARTH_CG_ATTIC_SCRIPT; }
    if (script_id == 2u) { *out_count = (uint8_t)HEARTH_CG_DREAM_COUNT;         return HEARTH_CG_DREAM_SCRIPT; }
    if (script_id == 3u) { *out_count = (uint8_t)HEARTH_CG_OAK_COUNT;           return HEARTH_CG_OAK_SCRIPT; }
    if (script_id == 4u) { *out_count = (uint8_t)HEARTH_CG_WORLD_ARRIVAL_CNT;   return HEARTH_CG_WORLD_ARRIVAL_SCRIPT; }
    *out_count = (uint8_t)HEARTH_CINEMA_BEAT_COUNT;
    return HEARTH_CINEMA_SCRIPT;
}

static void apply_cinema_beat(HearthState *st, uint8_t beat_idx, bool plate_changed) {
    HearthIntroState *in = &st->intro;
    const HearthCinemaBeat *scr;
    const HearthCinemaBeat *b;
    uint8_t bcnt = 0u;
    scr = get_cinema_script(in->script_id, &bcnt);
    in->beat_count = bcnt;
    if (beat_idx >= bcnt) beat_idx = (uint8_t)(bcnt - 1u);
    b = &scr[beat_idx];
    in->beat = beat_idx;
    in->scene = b->plate_id;
    in->act_num = b->act_num;
    in->bgm_id = b->bgm_id;
    in->cam_mode = b->cam_mode;
    in->fx_mode = b->fx_mode;
    in->title = b->act_title;
    in->speaker = b->speaker;
    in->line1 = b->line1;
    in->line2 = b->line2;
    in->timer = 0;
    in->auto_hold = 0;
    in->chars = 0;
    if (plate_changed) {
        in->plate_timer = 0;
        in->fade = 15;
        in->fade_dir = 1u; /* Fade in smoothly from matte black */
        if (b->plate_id == CG_SHADOW_GRANDMA) {
            st->shake_frames = 22;
            st->shake_intensity = 3;
            st->flash_level = 10;
            push_sfx(st, SFX_BATTLE_SWOOP);
        } else if (b->plate_id == CG_HOLLOW_OAK_AWAKENS) {
            st->flash_level = 8;
            push_sfx(st, SFX_HEART_GIVE);
        }
    }
}

static void frame_title_camera(HearthState *st) {
    uint16_t w, h;
    uint8_t lim;
    int16_t tx, ty;
    int16_t mw = (int16_t)(st->map_w ? st->map_w : (uint8_t)HEARTH_MAP_W);
    int16_t mh = (int16_t)(st->map_h ? st->map_h : (uint8_t)HEARTH_MAP_H);
    get_ppu_dims(st, &w, &h, &lim);
    tx = iclamp16((int16_t)(18 * 16 - (int16_t)(w >> 1)), 0, (int16_t)(mw * 16 - (int)w));
    ty = iclamp16((int16_t)(9 * 16 - (int16_t)(h >> 1)), 0, (int16_t)(mh * 16 - (int)h));
    st->cam_fx = TO_FP(tx);
    st->cam_fy = TO_FP(ty);
}

static void launch_cinema_mode(HearthState *st, uint8_t script_id, bool launch_story) {
    st->mode = MODE_INTRO;
    st->battle.active = false;
    st->dialogue.active = false;
    st->shop.active = false;
    st->fishing.active = false;
    st->cam_override = false;
    st->intro.script_id = script_id;
    st->intro.auto_play = false;
    st->intro.launch_story = launch_story;
    st->intro.fade = 15;
    st->intro.fade_dir = 1u;
    apply_cinema_beat(st, 0u, true);
    if (script_id == 0u) frame_title_camera(st);
    build_ppu_packet(st);
}

static void finish_cinema_mode(HearthState *st, bool play_sfx) {
    uint8_t sid = st->intro.script_id;
    st->intro.fade = 0;
    st->intro.fade_dir = 0;
    if (sid == 1u) {
        /* Attic Keepsake CG finished -> Dusk settles, Milo's Bell found! */
        st->has_milo_bell = true;
        st->time_of_day = TOD_DUSK;
        if (st->ch1_step < 4u) st->ch1_step = 4u;
        st->mode = MODE_EXPLORE;
        st->warp_fade = 12;
        populate_zone_rowan_home(st);
        if (play_sfx) push_sfx(st, SFX_CONFIRM);
        build_ppu_packet(st);
        return;
    }
    if (sid == 2u) {
        /* Pip Dream Return CG finished -> Moonlit Night awakens Rowan beside his bed! */
        st->time_of_day = TOD_NIGHT;
        if (st->ch1_step < 6u) st->ch1_step = 6u;
        st->mode = MODE_EXPLORE;
        hearth_enter_zone(st, ZONE_ROWAN_HOME, 16, 15);
        st->hero_dir = DIR_RIGHT;
        st->warp_fade = 14;
        if (play_sfx) push_sfx(st, SFX_CONFIRM);
        build_ppu_packet(st);
        return;
    }
    if (sid == 3u) {
        /* Hollow Oak Awakens CG -> Chain into the sweeping Evertale Continent Vista CG! */
        st->story_stage = STORY_EVERTALE_AWAKENED;
        st->ch1_step = 8u;
        hearth_start_story_cg(st, 4u);
        return;
    }
    if (sid == 4u) {
        /* Continent of Evertale Vista CG -> Step onto the peaceful Evertale Open World Map! */
        st->story_stage = STORY_EVERTALE_AWAKENED;
        st->ch1_step = 8u;
        st->ch2_step = 0u;
        st->flag_has_wand = false;
        st->flag_has_familiar = false;
        st->flag_has_heart_vessel = false;
        st->pip_visible = false;
        st->mode = MODE_EXPLORE;
        hearth_enter_zone(st, ZONE_EVERTALE_WORLD, 74, 110);
        st->hero_dir = DIR_UP;
        st->warp_fade = 14;
        if (play_sfx) push_sfx(st, SFX_CONFIRM);
        build_ppu_packet(st);
        return;
    }

    if (st->intro.launch_story) {
        /* Opening Cinema -> Chapter 1 Dawn inside Rowan & Grandma Mara's Cottage! */
        st->story_stage = STORY_AGE10_ALDERBROOK;
        st->time_of_day = TOD_MORNING;
        st->ch1_step = 0u;
        st->ch2_step = 0u;
        st->ch1_errands_mask = 0u;
        st->woods_shrines_mask = 0u;
        st->planks_found_mask = 0u;
        st->active_vehicle = VEH_FOOT;
        st->vehicle_unlocked_mask = 0u;
        st->atlas_open = false;
        st->deep_sea_hint_shown = false;
        st->designer_sandbox = false;
        st->has_milo_bell = false;
        st->flag_has_wand = false;
        st->flag_has_familiar = false;
        st->flag_has_heart_vessel = false;
        st->flag_has_courage = false;
        st->flag_guard_mended = false;
        st->flag_sewer_key = false;
        st->flag_captain_mended = false;
        st->flag_muckmaw_defeated = false;
        st->flag_ironfang_defeated = false;
        st->flag_boss_defeated = false;
        st->flashback_triggered = true;
        st->mode = MODE_EXPLORE;
        st->intro.launch_story = false;
        hearth_enter_zone(st, ZONE_ROWAN_HOME, 16, 15);
        st->hero_dir = DIR_RIGHT;
        st->warp_fade = 14;
        if (play_sfx) push_sfx(st, SFX_CONFIRM);
        build_ppu_packet(st);
    } else {
        st->mode = MODE_TITLE;
        st->title_cursor = 0;
        frame_title_camera(st);
        if (play_sfx) push_sfx(st, SFX_CONFIRM);
        build_ppu_packet(st);
    }
}

void hearth_start_intro(HearthState *st) {
    launch_cinema_mode(st, 0u, false);
}

void hearth_start_story_cg(HearthState *st, uint8_t script_id) {
    launch_cinema_mode(st, script_id, false);
}

void hearth_skip_intro(HearthState *st) {
    finish_cinema_mode(st, true);
}

void hearth_toggle_cinema_auto(HearthState *st) {
    st->intro.auto_play = !st->intro.auto_play;
    st->intro.auto_hold = 0;
    push_sfx(st, SFX_CURSOR);
}

static void request_cinema_advance(HearthState *st) {
    HearthIntroState *in = &HS.intro;
    uint8_t bcnt = 0u;
    const HearthCinemaBeat *scr = get_cinema_script(in->script_id, &bcnt);
    if ((uint8_t)(in->beat + 1u) < bcnt) {
        const HearthCinemaBeat *cur = &scr[in->beat];
        const HearthCinemaBeat *nxt = &scr[in->beat + 1u];
        if (cur->plate_id != nxt->plate_id || cur->act_num != nxt->act_num) {
            in->fade_dir = 2u;
        } else {
            apply_cinema_beat(st, (uint8_t)(in->beat + 1u), false);
        }
    } else {
        in->fade_dir = 2u;
    }
}

static void step_intro_cutscene(HearthState *st, uint16_t just_pressed) {
    HearthIntroState *in = &HS.intro;
    uint16_t total_chars;
    uint8_t bcnt = 0u;
    const HearthCinemaBeat *scr;
    const HearthCinemaBeat *cur_beat;

    if (just_pressed & PAD_START) {
        hearth_skip_intro(st);
        return;
    }
    if (just_pressed & PAD_Y) {
        hearth_toggle_cinema_auto(st);
    }

    in->timer++;
    in->plate_timer++;
    scr = get_cinema_script(in->script_id, &bcnt);

    /* Smooth fade-out transition between plates or at cinema end */
    if (in->fade_dir == 2u) {
        if ((in->timer & 1u) == 0u || (just_pressed & (PAD_A | PAD_B))) {
            if (in->fade < 15u) {
                in->fade++;
            } else {
                if ((uint8_t)(in->beat + 1u) < bcnt) {
                    apply_cinema_beat(st, (uint8_t)(in->beat + 1u), true);
                } else {
                    finish_cinema_mode(st, false);
                }
            }
        }
        return;
    }

    /* Smooth fade-in from matte black on new plate */
    if (in->fade_dir == 1u) {
        if ((in->timer & 1u) == 0u) {
            if (in->fade > 0u) in->fade--;
            else in->fade_dir = 0u;
        }
    } else {
        in->fade = 0u;
    }

    total_chars = (uint16_t)(str_len16(in->line1) + str_len16(in->line2));
    cur_beat = &scr[(in->beat < bcnt) ? in->beat : 0u];

    if (just_pressed & (PAD_A | PAD_B)) {
        if (in->fade > 3u) {
            in->fade = 0u;
            in->fade_dir = 0u;
        } else if (in->chars < total_chars) {
            in->chars = total_chars;
            in->auto_hold = 0;
            push_sfx(st, SFX_CURSOR);
        } else {
            push_sfx(st, SFX_CONFIRM);
            request_cinema_advance(st);
        }
        return;
    }

    /* Unhurried storybook typewriter once plate has mostly faded in */
    if (in->fade <= 4u && in->chars < total_chars && ((in->timer & 1u) == 0u)) {
        in->chars++;
        if ((in->chars % 5u) == 0u) {
            push_sfx(st, cur_beat->voice_sfx);
        }
    }

    /* Optional leisurely auto-advance mode (~4.0s reading hold after text completes) */
    if (in->auto_play && in->fade == 0u && in->chars >= total_chars) {
        in->auto_hold++;
        if (in->auto_hold >= 240u) {
            request_cinema_advance(st);
        }
    }
}

const char *hearth_get_objective_text(const HearthState *st) {
    if (st->story_stage < STORY_EVERTALE_AWAKENED) {
        if (st->ch1_step == 0u) return "Talk to Grandma Mara downstairs";
        if (st->ch1_step == 1u) return "Visit Baker Maple & Stationmaster Barnaby";
        if (st->ch1_step == 2u) return "Return home to Grandma Mara";
        if (st->ch1_step == 3u) return "Inspect the cedar chest in the attic loft";
        if (st->ch1_step == 4u) return "Show Mido's brass bell to Grandma Mara";
        if (st->ch1_step == 5u) return "Rest in Rowan's patchwork bed";
        if (st->ch1_step == 6u) return "Follow Mido northeast to Whispering Woods";
        if ((st->woods_shrines_mask & 7u) != 7u) return "Light the 3 Lantern Shrines in the woods";
        return "Climb north to Hollow Glade & awaken the Oak";
    }
    if (st->story_stage == STORY_EVERTALE_AWAKENED) {
        if (st->ch2_step == 0u) return "Explore Evertale & ask townsfolk about Mido";
        if (st->ch2_step == 1u) return "Visit Archivist Corin on Sunhill's upper terrace";
        if (st->ch2_step == 2u) return "Meet Mido by Sunhill Market's sunlit fountain";
        if (!st->flag_has_familiar) return "Visit Bramblewood Crossing east to bond a Familiar";
        if ((st->vehicle_unlocked_mask & VEH_MASK_RAFT) == 0u) {
            if (st->ch2_step < 5u) return "Visit Shipwright Orin south at Heartland Timber Pier";
            return "Gather 3 Sun-Cedar Planks along Timber Pier beach";
        }
        if (st->ch2_step < 7u) return "Sail the Cedar Raft south to Suncrest & Sands of Babana";
        return "Explore the Evertale Globe & uncover clues to the Sea Chart";
    }
    if (st->story_stage == STORY_SEWER_KEY) return "Descend into the Sunken Sewers north of Whiskerburgh";
    if (st->story_stage == STORY_MUCKMAW_DEFEATED) return "Cross into Boarhold Citadel & face Lord Ironfang";
    return "Evertale shines bright! Fish, explore & master combat";
}

void hearth_init(HearthState *st) {
    hearth_memset(st, 0, (uint16_t)sizeof(*st));
    hearth_default_tuning(&st->tuning);
    hearth_prng_seed(&st->prng, (uint16_t)0x5EEDu);
    init_party(st);

    /* Boot cleanly onto the Title Screen framed over redesigned Autumn Alderbrook */
    st->story_stage = STORY_AGE10_ALDERBROOK;
    st->time_of_day = TOD_AFTERNOON;
    st->flag_has_wand = false;
    hearth_enter_zone(st, ZONE_ALDERBROOK, 18, 14);
    st->zone_banner_frames = 0;
    st->mode = MODE_TITLE;
    st->title_cursor = 0;
    st->intro.fade = 0;
    st->intro.fade_dir = 0;
    frame_title_camera(st);
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
    if (st->mode == MODE_TITLE || st->mode == MODE_INTRO) frame_title_camera(st);
    else snap_cam_to_hero(st);
    build_ppu_packet(st);
}

void hearth_start_story(HearthState *st) {
    /* New Story launches the Illustrated Opening Cinema -> Rowan's Cottage Dawn! */
    st->story_stage = STORY_AGE10_ALDERBROOK;
    st->time_of_day = TOD_MORNING;
    st->ch1_step = 0u;
    st->ch2_step = 0u;
    st->ch1_errands_mask = 0u;
    st->woods_shrines_mask = 0u;
    st->planks_found_mask = 0u;
    st->active_vehicle = VEH_FOOT;
    st->vehicle_unlocked_mask = 0u;
    st->atlas_open = false;
    st->deep_sea_hint_shown = false;
    st->designer_sandbox = false;
    st->has_milo_bell = false;
    st->flag_has_wand = false;
    st->flag_has_familiar = false;
    st->flag_has_heart_vessel = false;
    st->flag_has_courage = false;
    st->flag_guard_mended = false;
    st->flag_sewer_key = false;
    st->flag_captain_mended = false;
    st->flag_muckmaw_defeated = false;
    st->flag_ironfang_defeated = false;
    st->flag_boss_defeated = false;
    st->flashback_triggered = false;
    push_sfx(st, SFX_CONFIRM);
    launch_cinema_mode(st, 0u, true);
}

void hearth_start_sandbox(HearthState *st) {
#if HEARTH_DESIGNER_MODE
    /* Designer Mode: Jump straight onto the 256x192 Evertale Globe with travel tiers ready! */
    if (st->story_stage < STORY_EVERTALE_AWAKENED) {
        st->story_stage = STORY_EVERTALE_AWAKENED;
    }
    st->ch1_step = 8u;
    st->ch2_step = 6u;
    st->flag_has_wand = true;
    st->flag_has_familiar = true;
    st->flag_has_heart_vessel = false;
    st->pip_visible = true;
    st->designer_sandbox = true;
    st->active_vehicle = VEH_FOOT;
    st->vehicle_unlocked_mask = (uint8_t)(VEH_MASK_RAFT | VEH_MASK_SHIP | VEH_MASK_BALLOON | VEH_MASK_AIRSHIP);
    st->atlas_open = false;
    st->mode = MODE_EXPLORE;
    st->intro.fade = 0;
    st->cam_override = false;
    st->dialogue.active = false;
    st->battle.active = false;
    st->shop.active = false;
    st->fishing.active = false;
    hearth_enter_zone(st, ZONE_EVERTALE_WORLD, 104, 84);
    st->hero_dir = DIR_DOWN;
    push_sfx(st, SFX_CONFIRM);
#else
    hearth_start_story(st);
#endif
}

void hearth_toggle_atlas(HearthState *st) {
    if (st->mode != MODE_EXPLORE || st->dialogue.active || st->warp_pending) return;
    st->atlas_open = !st->atlas_open;
    if (st->atlas_open) st->atlas_cursor = st->world_region_id;
    push_sfx(st, SFX_CURSOR);
    build_ppu_packet(st);
}

void hearth_designer_cycle_vehicle(HearthState *st) {
#if HEARTH_DESIGNER_MODE
    static const char *const veh_labels[5] = {
        "ON FOOT", "CEDAR RAFT", "SAILING SHIP", "HOT AIR BALLOON", "SKY AIRSHIP"
    };
    if (!st->designer_sandbox || !st->is_world_map || st->mode != MODE_EXPLORE) return;
    st->active_vehicle = (uint8_t)((st->active_vehicle + 1u) % 5u);
    if (st->active_vehicle == VEH_RAFT)    st->vehicle_unlocked_mask |= (uint8_t)VEH_MASK_RAFT;
    if (st->active_vehicle == VEH_SHIP)    st->vehicle_unlocked_mask |= (uint8_t)(VEH_MASK_RAFT | VEH_MASK_SHIP);
    if (st->active_vehicle == VEH_BALLOON) st->vehicle_unlocked_mask |= (uint8_t)VEH_MASK_BALLOON;
    if (st->active_vehicle == VEH_AIRSHIP) st->vehicle_unlocked_mask |= (uint8_t)VEH_MASK_AIRSHIP;
    push_sfx(st, SFX_CONFIRM);
    spawn_ftext(st, FP_PX(st->hero_fx), (int16_t)(FP_PX(st->hero_fy) - 22),
        veh_labels[st->active_vehicle], 3, 11, true);
    build_ppu_packet(st);
#else
    (void)st;
#endif
}

void hearth_designer_warp_region(HearthState *st, uint8_t region_id) {
#if HEARTH_DESIGNER_MODE
    static const uint16_t rx[REGION_COUNT] = { 104u, 132u,  52u, 128u, 212u,  32u, 220u };
    static const uint16_t ry[REGION_COUNT] = {  84u, 155u, 147u,  22u, 126u,  62u,  62u };
    if (!st->designer_sandbox) return;
    if (region_id >= REGION_COUNT) region_id = REGION_HEARTLAND;
    st->atlas_open = false;
    st->mode = MODE_EXPLORE;
    st->dialogue.active = false;
    hearth_enter_zone(st, ZONE_EVERTALE_WORLD, rx[region_id], ry[region_id]);
    push_sfx(st, SFX_PORTAL);
#else
    (void)st; (void)region_id;
#endif
}

void hearth_start_cutscene(HearthState *st, const HearthCutsceneOp *script) {
    st->mode = MODE_CUTSCENE;
    st->intro.fade = 0;
    st->atlas_open = false;
    st->battle.active = false;
    st->dialogue.active = false;
    st->shop.active = false;
    st->fishing.active = false;
    st->cs_script = script;
    st->cs_ip = 0;
    st->cs_wait_frames = 0;
}

static void request_zone_warp(HearthState *st, uint8_t dst_zone, uint16_t tx, uint16_t ty) {
    st->warp_pending = true;
    st->warp_fade = 1;
    st->warp_dst_zone = dst_zone;
    st->warp_dst_tx = tx;
    st->warp_dst_ty = ty;
    push_sfx(st, SFX_PORTAL);
}

/* ============================================================================
 * COLLISION & FLUID 8-WAY CORNER-NUDGING PHYSICS (WITH TOROIDAL GLOBE & VEHICLES)
 * ============================================================================ */
static void open_dialogue(
    HearthState *st, const char *speaker, uint8_t voice, const char *text,
    uint8_t choice_count, const HearthChoice *choices
);

static uint8_t get_col_at_px(const HearthState *st, int16_t px, int16_t py) {
    uint16_t tx, ty;
    uint8_t mw, mh, col = COL_SOLID;
    ST_UNUSED(st);
    if (HS.is_world_map) {
        tx = (uint16_t)((uint16_t)wrap_globe_px_x(px) >> 4);
        ty = (uint16_t)((uint16_t)wrap_globe_px_y(py) >> 4);
        hearth_globe_cell(tx, ty, NULL, NULL, &col, NULL);
        return col;
    }
    if (px < 0 || py < 0) return COL_SOLID;
    tx = (uint16_t)px >> 4;
    ty = (uint16_t)py >> 4;
    mw = HS.map_w ? HS.map_w : (uint8_t)HEARTH_MAP_W;
    mh = HS.map_h ? HS.map_h : (uint8_t)HEARTH_MAP_H;
    if (tx >= mw || ty >= mh) return COL_SOLID;
    return HS.map_col[map_row_offset(ty, mw) + tx];
}

static bool globe_col_blocked(const HearthState *st, uint8_t col) {
    ST_UNUSED(st);
    if (HS.active_vehicle >= VEH_BALLOON) return false;
    if (col == COL_WALKABLE || col == COL_TALL_GRASS) return false;
    if (col == COL_SHALLOW) {
        return ((HS.vehicle_unlocked_mask & VEH_MASK_RAFT) == 0u && HS.active_vehicle < VEH_RAFT);
    }
    if (col == COL_WATER) {
        return ((HS.vehicle_unlocked_mask & VEH_MASK_SHIP) == 0u && HS.active_vehicle < VEH_SHIP);
    }
    return true; /* COL_SOLID or COL_LAVA */
}

static bool collides_world_px(const HearthState *st, int16_t cx, int16_t cy) {
    uint16_t tx0, tx1, ty0, ty1, row0, row1, c;
    int16_t dx, dy, i;
    uint8_t mw, mh;
    bool block_shallow;
    ST_UNUSED(st);

    if (HS.is_world_map) {
        uint8_t col_t = COL_SOLID;
        if (HS.active_vehicle >= VEH_BALLOON) return false;
        tx0 = (uint16_t)((uint16_t)wrap_globe_px_x((int16_t)(cx - 5)) >> 4);
        tx1 = (uint16_t)((uint16_t)wrap_globe_px_x((int16_t)(cx + 5)) >> 4);
        ty0 = (uint16_t)((uint16_t)wrap_globe_px_y((int16_t)(cy - 3)) >> 4);
        ty1 = (uint16_t)((uint16_t)wrap_globe_px_y((int16_t)(cy + 3)) >> 4);
        hearth_globe_cell(tx0, ty0, NULL, NULL, &col_t, NULL);
        if (globe_col_blocked(st, col_t)) return true;
        if (tx1 != tx0) {
            hearth_globe_cell(tx1, ty0, NULL, NULL, &col_t, NULL);
            if (globe_col_blocked(st, col_t)) return true;
        }
        if (ty1 != ty0) {
            hearth_globe_cell(tx0, ty1, NULL, NULL, &col_t, NULL);
            if (globe_col_blocked(st, col_t)) return true;
            if (tx1 != tx0) {
                hearth_globe_cell(tx1, ty1, NULL, NULL, &col_t, NULL);
                if (globe_col_blocked(st, col_t)) return true;
            }
        }
        return false;
    }

    if (cx < 5 || cy < 3) return true;
    tx0 = (uint16_t)(cx - 5) >> 4;
    tx1 = (uint16_t)(cx + 5) >> 4;
    ty0 = (uint16_t)(cy - 3) >> 4;
    ty1 = (uint16_t)(cy + 3) >> 4;
    mw = HS.map_w ? HS.map_w : (uint8_t)HEARTH_MAP_W;
    mh = HS.map_h ? HS.map_h : (uint8_t)HEARTH_MAP_H;
    if (tx1 >= mw || ty1 >= mh) return true;
    block_shallow = (HS.zone_id == ZONE_TIMBER_PIER || HS.zone_id == ZONE_CORAL_COVE);

    row0 = map_row_offset(ty0, mw);
    c = (uint16_t)HS.map_col[row0 + tx0];
    if (c == COL_SOLID || c == COL_WATER || c == COL_LAVA || (block_shallow && c == COL_SHALLOW)) return true;
    if (tx1 != tx0) {
        c = (uint16_t)HS.map_col[row0 + tx1];
        if (c == COL_SOLID || c == COL_WATER || c == COL_LAVA || (block_shallow && c == COL_SHALLOW)) return true;
    }
    if (ty1 != ty0) {
        row1 = map_row_offset(ty1, mw);
        c = (uint16_t)HS.map_col[row1 + tx0];
        if (c == COL_SOLID || c == COL_WATER || c == COL_LAVA || (block_shallow && c == COL_SHALLOW)) return true;
        if (tx1 != tx0) {
            c = (uint16_t)HS.map_col[row1 + tx1];
            if (c == COL_SOLID || c == COL_WATER || c == COL_LAVA || (block_shallow && c == COL_SHALLOW)) return true;
        }
    }

    for (i = 0; i < HEARTH_MAX_NPCS; i++) {
        if (!HS.npcs[i].active) continue;
        dx = (int16_t)(cx - FP_PX(HS.npcs[i].fx));
        if (dx > -11 && dx < 11) {
            dy = (int16_t)(cy - FP_PX(HS.npcs[i].fy));
            if (dy > -9 && dy < 9) return true;
        }
    }
    if (HS.has_waystone) {
        dx = (int16_t)(cx - ((int16_t)HS.waystone_tx * 16 + 8));
        if (dx > -11 && dx < 11) {
            dy = (int16_t)(cy - ((int16_t)HS.waystone_ty * 16 + 8));
            if (dy > -9 && dy < 9) return true;
        }
    }
    if (HS.chest.active) {
        dx = (int16_t)(cx - ((int16_t)HS.chest.tx * 16 + 8));
        if (dx > -10 && dx < 10) {
            dy = (int16_t)(cy - ((int16_t)HS.chest.ty * 16 + 8));
            if (dy > -8 && dy < 8) return true;
        }
    }
    return false;
}

static void step_hero_collide_xy(HearthState *st) {
    fp16_t nfx, nfy;
    int16_t hx, hy, nx, ny;
    bool hit_block = false;
    int16_t probe_x = 0, probe_y = 0;

    hx = FP_PX(HS.hero_fx);
    hy = FP_PX(HS.hero_fy);

    if (HS.hero_vx != 0) {
        nfx = HS.hero_fx + HS.hero_vx;
        if (HS.is_world_map) nfx = wrap_globe_fp_x(nfx);
        nx = FP_PX(nfx);
        if (!collides_world_px(st, nx, hy)) {
            HS.hero_fx = nfx;
            hx = nx;
        } else if (!collides_world_px(st, nx, (int16_t)(hy - 5))) {
            HS.hero_fy -= 192;
            if (HS.is_world_map) HS.hero_fy = wrap_globe_fp_y(HS.hero_fy);
            hy = FP_PX(HS.hero_fy);
        } else if (!collides_world_px(st, nx, (int16_t)(hy + 5))) {
            HS.hero_fy += 192;
            if (HS.is_world_map) HS.hero_fy = wrap_globe_fp_y(HS.hero_fy);
            hy = FP_PX(HS.hero_fy);
        } else {
            hit_block = true;
            probe_x = (int16_t)(nx + ((HS.hero_vx > 0) ? 6 : -6));
            probe_y = hy;
            HS.hero_vx = 0;
        }
    }
    if (HS.hero_vy != 0) {
        nfy = HS.hero_fy + HS.hero_vy;
        if (HS.is_world_map) nfy = wrap_globe_fp_y(nfy);
        ny = FP_PX(nfy);
        if (!collides_world_px(st, hx, ny)) {
            HS.hero_fy = nfy;
            hy = ny;
        } else if (!collides_world_px(st, (int16_t)(hx - 5), ny)) {
            HS.hero_fx -= 192;
            if (HS.is_world_map) HS.hero_fx = wrap_globe_fp_x(HS.hero_fx);
            hx = FP_PX(HS.hero_fx);
        } else if (!collides_world_px(st, (int16_t)(hx + 5), ny)) {
            HS.hero_fx += 192;
            if (HS.is_world_map) HS.hero_fx = wrap_globe_fp_x(HS.hero_fx);
            hx = FP_PX(HS.hero_fx);
        } else {
            hit_block = true;
            probe_x = hx;
            probe_y = (int16_t)(ny + ((HS.hero_vy > 0) ? 5 : -5));
            HS.hero_vy = 0;
        }
    }

    if (HS.is_world_map) {
        uint8_t cur_col = get_col_at_px(st, hx, hy);
        if (HS.active_vehicle < VEH_BALLOON) {
            if (cur_col == COL_WATER && (HS.vehicle_unlocked_mask & VEH_MASK_SHIP) != 0u) {
                HS.active_vehicle = VEH_SHIP;
            } else if (cur_col == COL_SHALLOW && (HS.vehicle_unlocked_mask & VEH_MASK_RAFT) != 0u) {
                if (HS.active_vehicle == VEH_FOOT) {
                    HS.active_vehicle = VEH_RAFT;
                    push_sfx(st, SFX_WATER_SPLASH);
                }
            } else if (cur_col == COL_WALKABLE || cur_col == COL_TALL_GRASS) {
                if (HS.active_vehicle == VEH_RAFT || HS.active_vehicle == VEH_SHIP) {
                    HS.active_vehicle = VEH_FOOT;
                }
            }
        }
        /* One-time Pip hint when nudging deep cobalt ocean on the Cedar Raft */
        if (hit_block && HS.active_vehicle == VEH_RAFT &&
            (HS.vehicle_unlocked_mask & VEH_MASK_SHIP) == 0u &&
            !HS.deep_sea_hint_shown &&
            get_col_at_px(st, probe_x, probe_y) == COL_WATER) {
            HS.deep_sea_hint_shown = true;
            open_dialogue(st, "Pip", SFX_TEXT_PIP,
                "Whoa, steady there Rowan! Our Cedar Raft can glide across turquoise coastal shallows and coral channels, but we'll need a sturdy Sailing Ship to cross the deep cobalt sea!",
                0, NULL);
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
    dx = world_dx(st, hx, FP_PX(HS.trail_x[th]));
    dy = world_dy(st, hy, FP_PX(HS.trail_y[th]));
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
        if (HS.is_world_map && HS.active_vehicle >= VEH_BALLOON) {
            spawn_burst(st, 0, hx, (int16_t)(hy + 6), 2, 0, 14, false);
        } else if (foot == (uint16_t)COL_TALL_GRASS) {
            push_sfx(st, SFX_GRASS_RUSTLE);
            spawn_burst(st, 1, hx, hy, 4, 0, 4, false);
        } else if (foot == (uint16_t)COL_SHALLOW || foot == (uint16_t)COL_WATER) {
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
    if (HS.is_world_map && HS.active_vehicle == VEH_RAFT) {
        max_spd = (int16_t)(max_spd + (max_spd >> 3));
    } else if (HS.is_world_map && HS.active_vehicle >= VEH_SHIP) {
        max_spd = (int16_t)(max_spd + (max_spd >> 1));
    }

    if (ix != 0 && iy != 0) {
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

    if (!HS.pip_visible) return;
    if (back < 4) back = 4;
    if (back > HEARTH_TRAIL_LEN - 2) back = HEARTH_TRAIL_LEN - 2;
    idx = (int16_t)((int16_t)HS.trail_head - back);
    if (idx < 0) idx = (int16_t)(idx + HEARTH_TRAIL_LEN);

    dx = HS.trail_x[idx] - HS.pip_fx;
    dy = (HS.trail_y[idx] - TO_FP(4)) - HS.pip_fy;
    if (HS.is_world_map) {
        if (dx > (GLOBE_FP_W >> 1)) dx -= GLOBE_FP_W;
        else if (dx < -(GLOBE_FP_W >> 1)) dx += GLOBE_FP_W;
        if (dy > (GLOBE_FP_H >> 1)) dy -= GLOBE_FP_H;
        else if (dy < -(GLOBE_FP_H >> 1)) dy += GLOBE_FP_H;
    }
    if (dx != 0) {
        if (dx >= -16384 && dx <= 16383) HS.pip_fx += (fp16_t)((int16_t)dx >> 3);
        else HS.pip_fx += dx >> 3;
    }
    if (dy != 0) {
        if (dy >= -16384 && dy <= 16383) HS.pip_fy += (fp16_t)((int16_t)dy >> 3);
        else HS.pip_fy += dy >> 3;
    }
    if (HS.is_world_map) {
        HS.pip_fx = wrap_globe_fp_x(HS.pip_fx);
        HS.pip_fy = wrap_globe_fp_y(HS.pip_fy);
    }
    if (dx < -40 || dx > 40) HS.pip_dir = (uint8_t)((dx < 0) ? DIR_LEFT : DIR_RIGHT);
}

/* ============================================================================
 * TACTILE FAMILIAR COMBAT ENGINE (7 ENCOUNTERS + TIMED HITS + LOCK BREAKS)
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

static void populate_battle_preset(HearthBattleState *b, uint8_t preset, int8_t field_idx, int16_t right_base) {
    if (preset == ENCOUNTER_SHRINE_BOSS) {
        b->enemy_count = 1;
        set_battle_enemy(&b->enemies[0], "Briar Warden", "Bramble Tempest", SPR_BOSS_BRIAR,
            field_idx, true, 165, 21, 5, right_base, 126, (int16_t)(right_base + 56),
            2, 2, 3, LOCK_SWORD, LOCK_SUN, LOCK_STAR);
        copy_str_safe(b->banner, sizeof(b->banner), "SHRINE GUARDIAN: BRIAR WARDEN!");
    } else if (preset == ENCOUNTER_SEWER_BOSS) {
        b->enemy_count = 1;
        set_battle_enemy(&b->enemies[0], "King Muckmaw", "Sludge Deluge", SPR_BOSS_MUCKMAW,
            field_idx, true, 185, 22, 5, right_base, 126, (int16_t)(right_base + 56),
            2, 2, 3, LOCK_SUN, LOCK_STAR, LOCK_SWORD);
        copy_str_safe(b->banner, sizeof(b->banner), "SEWER MINI-BOSS: KING MUCKMAW!");
    } else if (preset == ENCOUNTER_IRONFANG_BOSS) {
        b->enemy_count = 1;
        set_battle_enemy(&b->enemies[0], "Lord Ironfang", "Tusk Cataclysm", SPR_BOSS_IRONFANG,
            field_idx, true, 220, 24, 6, right_base, 126, (int16_t)(right_base + 56),
            2, 2, 3, LOCK_SWORD, LOCK_SUN, LOCK_STAR);
        copy_str_safe(b->banner, sizeof(b->banner), "CITADEL BOSS: LORD IRONFANG!");
    } else if (preset == ENCOUNTER_SEWER_PACK) {
        b->enemy_count = 2;
        set_battle_enemy(&b->enemies[0], "Gutter Rat", "Fang Flurry", SPR_ENEMY_RAT,
            field_idx, false, 60, 15, 3, (int16_t)(right_base - 14), 108, (int16_t)(right_base + 54),
            2, 2, 2, LOCK_SWORD, LOCK_SUN, LOCK_NONE);
        set_battle_enemy(&b->enemies[1], "Mire Slime", "Ooze Splash", SPR_ENEMY_SLIME,
            field_idx, false, 56, 14, 3, (int16_t)(right_base + 14), 142, (int16_t)(right_base + 74),
            1, 2, 1, LOCK_STAR, LOCK_NONE, LOCK_NONE);
        copy_str_safe(b->banner, sizeof(b->banner), "SEWER AMBUSH!");
    } else if (preset == ENCOUNTER_BOAR_GUARD) {
        b->enemy_count = 2;
        set_battle_enemy(&b->enemies[0], "Boar Halberd", "Iron Charge", SPR_ENEMY_BOAR,
            field_idx, false, 72, 17, 4, (int16_t)(right_base - 12), 110, (int16_t)(right_base + 54),
            2, 2, 2, LOCK_SWORD, LOCK_STAR, LOCK_NONE);
        set_battle_enemy(&b->enemies[1], "Mire Slime", "Ooze Splash", SPR_ENEMY_SLIME,
            field_idx, false, 58, 15, 3, (int16_t)(right_base + 16), 142, (int16_t)(right_base + 74),
            0, 2, 0, LOCK_NONE, LOCK_NONE, LOCK_NONE);
        copy_str_safe(b->banner, sizeof(b->banner), "BOARHOLD SENTINELS!");
    } else if (preset == ENCOUNTER_GLADE_DUO) {
        b->enemy_count = 2;
        set_battle_enemy(&b->enemies[0], "Thornling", "Briar Flurry", SPR_ENEMY_THORNLING,
            field_idx, false, 58, 14, 3, (int16_t)(right_base - 14), 108, (int16_t)(right_base + 56),
            2, 2, 2, LOCK_SWORD, LOCK_SUN, LOCK_NONE);
        set_battle_enemy(&b->enemies[1], "Dusk Moth", "Hollow Dust", SPR_ENEMY_DUSK_MOTH,
            field_idx, false, 48, 13, 2, (int16_t)(right_base + 16), 142, (int16_t)(right_base + 76),
            1, 2, 1, LOCK_STAR, LOCK_NONE, LOCK_NONE);
        copy_str_safe(b->banner, sizeof(b->banner), "GLADE ENCOUNTER!");
    } else if (preset == ENCOUNTER_SANDWORM_BOSS) {
        b->enemy_count = 1;
        set_battle_enemy(&b->enemies[0], "Giant Sandworm", "Dune Tremor", SPR_ENEMY_SANDWORM,
            field_idx, true, 195, 22, 5, right_base, 126, (int16_t)(right_base + 56),
            2, 2, 3, LOCK_SWORD, LOCK_SUN, LOCK_STAR);
        copy_str_safe(b->banner, sizeof(b->banner), "DESERT TITAN: GIANT SANDWORM!");
    } else if (preset == ENCOUNTER_DESERT_SCORPION) {
        b->enemy_count = 2;
        set_battle_enemy(&b->enemies[0], "Dune Scorpion", "Amber Sting", SPR_ENEMY_SCORPION,
            field_idx, false, 64, 16, 4, (int16_t)(right_base - 14), 108, (int16_t)(right_base + 54),
            2, 2, 2, LOCK_SWORD, LOCK_SUN, LOCK_NONE);
        set_battle_enemy(&b->enemies[1], "Sand Stinger", "Tail Sweep", SPR_ENEMY_SCORPION,
            field_idx, false, 56, 15, 3, (int16_t)(right_base + 16), 142, (int16_t)(right_base + 74),
            1, 2, 1, LOCK_STAR, LOCK_NONE, LOCK_NONE);
        copy_str_safe(b->banner, sizeof(b->banner), "SANDS OF BABANA AMBUSH!");
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
    b->encounter_preset = preset;
    b->is_boss_battle = (preset == ENCOUNTER_SHRINE_BOSS || preset == ENCOUNTER_SEWER_BOSS ||
                         preset == ENCOUNTER_IRONFANG_BOSS || preset == ENCOUNTER_SANDWORM_BOSS);

    if (field_idx < 0) {
        for (i = 0; i < 3; i++) {
            st->party[i].hp = st->party[i].max_hp;
            st->party[i].mp = st->party[i].max_mp;
        }
        if (st->potions < 2) st->potions = 2;
    }

    populate_battle_preset(b, preset, field_idx, right_base);

    b->banner_frames = 60;
    st->dialogue.active = false;
    st->shop.active = false;
    st->fishing.active = false;
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

static void set_battle_vfx(
    HearthBattleState *b, uint8_t kind, uint16_t dur,
    int16_t x, int16_t y, int16_t tx, int16_t ty
) {
    b->vfx_kind = kind;
    b->vfx_timer = 0;
    b->vfx_dur = dur;
    b->vfx_x = x;
    b->vfx_y = y;
    b->vfx_tx = tx;
    b->vfx_ty = ty;
}

static void use_battle_item(HearthState *st) {
    HearthBattleState *b = &st->battle;
    int p_idx;
    if (st->potions > 0) {
        st->potions--;
        for (p_idx = 0; p_idx < 3; p_idx++) {
            st->party[p_idx].hp = (int16_t)((st->party[p_idx].hp + 45 > st->party[p_idx].max_hp) ? st->party[p_idx].max_hp : st->party[p_idx].hp + 45);
            st->party[p_idx].mp = (int16_t)((st->party[p_idx].mp + 10 > st->party[p_idx].max_mp) ? st->party[p_idx].max_mp : st->party[p_idx].mp + 10);
        }
        push_sfx(st, SFX_HEAL);
        spawn_burst(st, 5, b->hero_x, (int16_t)(b->hero_y - 10), 12, 0, 4, true);
        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 28), "TONIC +45HP", 0, 4, false);
        begin_enemy_turn(b);
    } else if (st->snacks > 0) {
        st->snacks--;
        for (p_idx = 0; p_idx < 3; p_idx++) {
            st->party[p_idx].hp = st->party[p_idx].max_hp;
            st->party[p_idx].mp = st->party[p_idx].max_mp;
        }
        push_sfx(st, SFX_HEAL);
        spawn_burst(st, 5, b->hero_x, (int16_t)(b->hero_y - 10), 14, 3, 11, true);
        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 28), "SNACK FULL!", 3, 11, false);
        begin_enemy_turn(b);
    } else if (st->babanas > 0) {
        st->babanas--;
        for (p_idx = 0; p_idx < 3; p_idx++) {
            st->party[p_idx].hp = (int16_t)((st->party[p_idx].hp + 70 > st->party[p_idx].max_hp) ? st->party[p_idx].max_hp : st->party[p_idx].hp + 70);
            st->party[p_idx].mp = (int16_t)((st->party[p_idx].mp + 15 > st->party[p_idx].max_mp) ? st->party[p_idx].max_mp : st->party[p_idx].mp + 15);
        }
        push_sfx(st, SFX_HEAL);
        spawn_burst(st, 5, b->hero_x, (int16_t)(b->hero_y - 10), 14, 3, 11, true);
        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 28), "BABANA +70!", 3, 11, false);
        begin_enemy_turn(b);
    } else if (st->star_drops > 0) {
        st->star_drops--;
        for (p_idx = 0; p_idx < 3; p_idx++) {
            st->party[p_idx].mp = (int16_t)((st->party[p_idx].mp + 15 > st->party[p_idx].max_mp) ? st->party[p_idx].max_mp : st->party[p_idx].mp + 15);
        }
        push_sfx(st, SFX_HEAL);
        spawn_burst(st, 5, b->hero_x, (int16_t)(b->hero_y - 10), 10, 3, 15, true);
        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 28), "STAR +15MP", 3, 15, false);
        begin_enemy_turn(b);
    } else {
        push_sfx(st, SFX_CANCEL);
        spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 26), "NO ITEMS!", 3, 10, false);
    }
}

static void step_battle_command(HearthState *st, uint16_t just_pressed) {
    HearthBattleState *b = &st->battle;
    HearthPartyMember *actor = &st->party[b->active_slot];

    if (b->phase == BPHASE_SWAP_ARC) {
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
        return;
    }

    if (!b->enemies[b->selected_enemy].alive) b->selected_enemy = first_alive_enemy(b);

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
            return;
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
            use_battle_item(st);
        }
    }
}

static void step_battle_player_windup(HearthState *st) {
    HearthBattleState *b = &st->battle;
    HearthPartyMember *actor = &st->party[b->active_slot];
    HearthBattleEnemy *e = &b->enemies[b->selected_enemy];
    const uint16_t dur = 28;
    int p_idx;

    if (actor->slot == 2) {
        int16_t ox = (int16_t)((HEARTH_SIN64[(b->phase_timer * 2u) & 63u] * 8) >> 7);
        b->hero_x = (int16_t)(b->hero_home_x - 4 + ox);
        b->hero_y = (int16_t)(b->hero_home_y - sin_arc(b->phase_timer, dur, 18));
        b->hero_scale_x_fp = 240;
        b->hero_scale_y_fp = 276;
        if ((b->phase_timer % 6u) == 0u) {
            spawn_particle(st, 3, (int16_t)(b->hero_x + hearth_prng_range(&st->prng, -8, 8)),
                (int16_t)(b->hero_y - 12), 0, -60, 14, 2, 3, 15, true);
        }
    } else if (actor->slot == 1) {
        b->hero_x = (int16_t)(b->hero_home_x - sin_arc(b->phase_timer, dur, 14));
        b->hero_y = (int16_t)(b->hero_home_y - ((b->active_action == 1) ? sin_arc(b->phase_timer, dur, 10) : 0));
        b->hero_scale_x_fp = 315;
        b->hero_scale_y_fp = 195;
        if ((b->phase_timer % 5u) == 0u) {
            spawn_particle(st, 3, b->hero_x, (int16_t)(b->hero_y - 6),
                hearth_prng_range(&st->prng, -90, 60), -110, 14, 2, 3, 11, true);
        }
    } else {
        b->hero_x = (int16_t)(b->hero_home_x - sin_arc(b->phase_timer, dur, 10));
        b->hero_y = (int16_t)(b->hero_home_y - sin_arc(b->phase_timer, dur, 6));
        b->hero_scale_x_fp = 285;
        b->hero_scale_y_fp = 225;
        if ((b->phase_timer % 7u) == 0u) {
            spawn_particle(st, 5, b->hero_x, (int16_t)(b->hero_y - 14), 0, -90, 16, 2, 3, 11, true);
        }
    }
    if (b->phase_timer == 14) {
        b->hero_flash_frames = 3;
        if (b->active_action == 1 && !actor->skill_is_heal) {
            spawn_burst(st, 3, b->hero_x, (int16_t)(b->hero_y - 10), 7, 3, (actor->slot == 2) ? 15 : 11, true);
        }
    }
    if (b->phase_timer >= dur) {
        if (b->active_action == 1 && actor->skill_is_heal) {
            for (p_idx = 0; p_idx < 3; p_idx++) {
                st->party[p_idx].hp = (int16_t)((st->party[p_idx].hp + 38 > st->party[p_idx].max_hp) ? st->party[p_idx].max_hp : st->party[p_idx].hp + 38);
            }
            push_sfx(st, SFX_HEAL);
            set_battle_vfx(b, BVFX_HEAL_BLOOM, 28, b->hero_home_x, (int16_t)(b->hero_home_y - 10), b->hero_home_x, b->hero_home_y);
            spawn_burst(st, 5, b->hero_home_x, (int16_t)(b->hero_home_y - 10), 16, 0, 4, true);
            spawn_ftext(st, b->hero_home_x, (int16_t)(b->hero_home_y - 30), "+38 HP!", 0, 4, false);
            b->hero_x = b->hero_home_x;
            b->hero_y = b->hero_home_y;
            begin_enemy_turn(b);
        } else {
            uint16_t ldur = (b->active_action == 1) ? 40u : 36u;
            b->phase = BPHASE_PLAYER_LUNGE;
            b->phase_timer = 0;
            b->timed_result = 0;
            b->timed_buffer = 0;
            if (actor->slot == 0) {
                set_battle_vfx(b, BVFX_STAFF_ARC, ldur, b->hero_home_x, (int16_t)(b->hero_home_y - 10), e->home_x, (int16_t)(e->home_y - 12));
            } else if (actor->slot == 1) {
                set_battle_vfx(b, (b->active_action == 1) ? BVFX_SOLAR_METEOR : BVFX_ACORN_SPIN,
                    ldur, b->hero_home_x, (int16_t)(b->hero_home_y - 10), e->home_x, (int16_t)(e->home_y - 12));
            } else {
                set_battle_vfx(b, (b->active_action == 1) ? BVFX_PRISM_NOVA : BVFX_STAR_BOLT,
                    ldur, (int16_t)(b->hero_home_x + 12), (int16_t)(b->hero_home_y - 16), e->home_x, (int16_t)(e->home_y - 14));
            }
        }
    }
}

static void step_battle_player_lunge(HearthState *st) {
    HearthBattleState *b = &st->battle;
    HearthPartyMember *actor = &st->party[b->active_slot];
    HearthBattleEnemy *e = &b->enemies[b->selected_enemy];
    uint16_t dur = (b->active_action == 1) ? 40u : 36u;
    int16_t dest_x = (actor->slot == 2)
        ? (int16_t)((b->hero_home_x + e->home_x) / 2 - 20)
        : (int16_t)(e->home_x - 24);
    int16_t dest_y = (actor->slot == 2) ? (int16_t)(b->hero_home_y - 14) : e->home_y;
    int16_t arc_h;
    uint16_t win = (st->tuning.timed_hit_window_f < 6) ? 6 : st->tuning.timed_hit_window_f;
    uint16_t sweet_start = (dur > win) ? (uint16_t)(dur - win) : 1;

    if (actor->slot == 1 && b->active_action == 1) arc_h = 56;
    else if (actor->slot == 1) arc_h = 26;
    else if (actor->slot == 2) arc_h = (b->active_action == 1) ? 28 : 16;
    else arc_h = 22;

    b->hero_x = (int16_t)(b->hero_home_x + ((dest_x - b->hero_home_x) * (int16_t)b->phase_timer) / (int16_t)dur);
    b->hero_y = (int16_t)(b->hero_home_y + ((dest_y - b->hero_home_y) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, arc_h));

    if (actor->slot == 1) {
        b->hero_scale_x_fp = ((b->phase_timer & 4u) != 0u) ? 290 : 220;
        b->hero_scale_y_fp = ((b->phase_timer & 4u) != 0u) ? 225 : 295;
        if ((b->phase_timer & 3u) == 0u) {
            spawn_particle(st, 3, b->hero_x, (int16_t)(b->hero_y - 8), -50, 20, 12,
                (b->active_action == 1) ? 3 : 2, 3, 11, true);
        }
        b->vfx_x = b->hero_x;
        b->vfx_y = (int16_t)(b->hero_y - 8);
    } else if (actor->slot == 2) {
        b->hero_scale_x_fp = 235;
        b->hero_scale_y_fp = 285;
        b->vfx_x = (int16_t)(b->hero_x + ((e->home_x - b->hero_x) * (int16_t)b->phase_timer) / (int16_t)dur);
        b->vfx_y = (int16_t)((b->hero_y - 12) + (((e->home_y - 12) - (b->hero_y - 12)) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, (b->active_action == 1) ? 34 : 22));
        if ((b->phase_timer & 3u) == 0u) {
            spawn_particle(st, 3, b->vfx_x, b->vfx_y, -30, -20, 12, 2, 3, 15, true);
        }
    } else {
        b->hero_scale_x_fp = 230;
        b->hero_scale_y_fp = 282;
        b->vfx_x = (int16_t)(b->hero_x + 8);
        b->vfx_y = (int16_t)(b->hero_y - 10);
    }

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
        } else if (b->phase_timer + 6 >= sweet_start) {
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
}

static void step_battle_player_aftermath(HearthState *st) {
    HearthBattleState *b = &st->battle;
    HearthPartyMember *actor = &st->party[b->active_slot];
    uint8_t i;

    if (b->phase == BPHASE_PLAYER_IMPACT) {
        uint16_t late = (uint16_t)((st->tuning.timed_hit_window_f >> 1) + 3);
        bool any_alive;
        if (b->phase_timer <= late && b->timed_result == 0 && b->timed_buffer > 0) {
            b->timed_result = 1;
            b->ring.result = 1;
            b->timed_buffer = 0;
            b->phase = BPHASE_PLAYER_FOLLOWUP;
            b->phase_timer = 0;
            push_sfx(st, SFX_HIT_CRITICAL);
            spawn_ftext(st, b->hero_x, (int16_t)(b->hero_y - 32), "PERFECT!", 3, 11, false);
            return;
        }
        if (b->phase_timer >= 20) {
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
    } else if (b->phase == BPHASE_PLAYER_FOLLOWUP) {
        uint8_t ti = b->enemies[b->selected_enemy].alive ? b->selected_enemy : first_alive_enemy(b);
        HearthBattleEnemy *e = &b->enemies[ti];
        const uint16_t dur = 30;
        int16_t base_x = (actor->slot == 2) ? (int16_t)(b->hero_home_x + 36) : (int16_t)(e->home_x - 24);
        b->hero_x = (int16_t)(base_x - sin_arc(b->phase_timer, dur, 22));
        b->hero_y = (int16_t)(e->home_y - sin_arc(b->phase_timer, dur, 40));
        b->hero_scale_x_fp = (b->phase_timer < 15) ? 235 : 325;
        b->hero_scale_y_fp = (b->phase_timer < 15) ? 295 : 200;

        if (b->phase_timer == 14) {
            b->hero_flash_frames = 4;
            spawn_burst(st, 3, b->hero_x, (int16_t)(b->hero_y - 10), 8, 3, 11, true);
            set_battle_vfx(b, (actor->slot == 2) ? BVFX_PRISM_NOVA : BVFX_SOLAR_METEOR,
                16, b->hero_x, (int16_t)(b->hero_y - 8), e->home_x, (int16_t)(e->home_y - 12));
        }
        if (b->phase_timer == 21) {
            b->selected_enemy = ti;
            resolve_player_strike(st, true);
        }
        if (b->phase_timer >= dur) {
            b->phase = BPHASE_PLAYER_RETURN;
            b->phase_timer = 0;
        }
    } else {
        const uint16_t dur = 18;
        bool all_dead;
        b->hero_x = (int16_t)(b->hero_x + ((b->hero_home_x - b->hero_x) * (int16_t)b->phase_timer) / (int16_t)dur);
        b->hero_y = (int16_t)(b->hero_y + ((b->hero_home_y - b->hero_y) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, 12));
        if (b->phase_timer >= dur) {
            b->hero_x = b->hero_home_x;
            b->hero_y = b->hero_home_y;
            b->ring.active = false;

            all_dead = true;
            for (i = 0; i < b->enemy_count; i++) if (b->enemies[i].alive) all_dead = false;
            if (all_dead) {
                b->exp_earned = b->is_boss_battle ? 120u : 48u;
                b->guilders_earned = b->is_boss_battle ? 95u : 28u;
                st->guilders = (uint16_t)(st->guilders + b->guilders_earned);
                b->phase = BPHASE_VICTORY_POSE;
                b->phase_timer = 0;
                push_sfx(st, SFX_VICTORY_FANFARE);
                copy_str_safe(b->banner, sizeof(b->banner), "VICTORY!");
                b->banner_frames = 90;
            } else {
                begin_enemy_turn(b);
            }
        }
    }
}

static void step_battle_enemy(HearthState *st) {
    HearthBattleState *b = &st->battle;
    HearthPartyMember *actor = &st->party[b->active_slot];
    HearthBattleEnemy *e;
    uint8_t i;

    if (b->phase == BPHASE_ENEMY_SELECT) {
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
            return;
        }
        e = &b->enemies[b->enemy_turn_idx];
        if (e->staggered) {
            spawn_ftext(st, e->home_x, (int16_t)(e->home_y - 26), "STAGGERED!", 3, 11, false);
            b->enemy_turn_idx++;
            b->phase_timer = 0;
            return;
        }
        if (e->charge_turns > 1) {
            e->charge_turns--;
            spawn_ftext(st, e->home_x, (int16_t)(e->home_y - 28), "CHARGING...", 3, 4, false);
            spawn_burst(st, 3, e->home_x, (int16_t)(e->home_y - 12), 5, 3, 4, true);
            b->enemy_turn_idx++;
            b->phase_timer = 0;
            return;
        }
        b->phase = BPHASE_ENEMY_TELEGRAPH;
        b->phase_timer = 0;
        b->timed_result = 0;
        b->timed_buffer = 0;
        if (e->charge_turns == 1) {
            copy_str_safe(b->banner, sizeof(b->banner), e->special_name);
            b->banner_frames = 60;
        } else {
            copy_str_safe(b->banner, sizeof(b->banner), "INCOMING! READY PARRY!");
            b->banner_frames = 36;
        }
    } else if (b->phase == BPHASE_ENEMY_TELEGRAPH) {
        const uint16_t dur = 38;
        int16_t jitter = ((b->phase_timer > 18) && ((b->phase_timer & 2u) != 0u)) ? 2 : 0;
        e = &b->enemies[b->enemy_turn_idx];
        e->x = (int16_t)(e->home_x + sin_arc(b->phase_timer, dur, 14) + jitter);
        e->y = (int16_t)(e->home_y - ((e->charge_turns == 1) ? sin_arc(b->phase_timer, dur, 10) : 0));
        e->scale_x_fp = 310;
        e->scale_y_fp = 200;
        if (b->phase_timer == 10 || b->phase_timer == 24) {
            e->flash_frames = 3;
            spawn_burst(st, 3, (int16_t)(e->x - 6), (int16_t)(e->y - 14), 6, 3, (e->charge_turns == 1) ? 4 : 10, true);
            if (b->phase_timer == 10) push_sfx(st, SFX_ENEMY_ALERT);
        }
        if (b->phase_timer >= dur) {
            b->phase = BPHASE_ENEMY_LUNGE;
            b->phase_timer = 0;
            b->timed_result = 0;
            b->timed_buffer = 0;
            set_battle_vfx(b, BVFX_ENEMY_CLAW, 34, e->home_x, (int16_t)(e->home_y - 12), b->hero_home_x, (int16_t)(b->hero_home_y - 12));
        }
    } else if (b->phase == BPHASE_ENEMY_LUNGE) {
        const uint16_t dur = 32;
        int16_t dest_x = (int16_t)(b->hero_home_x + 24);
        int16_t arc_h;
        uint16_t win = (st->tuning.parry_window_f < 6) ? 6 : st->tuning.parry_window_f;
        uint16_t sweet_start = (dur > win) ? (uint16_t)(dur - win) : 1;
        e = &b->enemies[b->enemy_turn_idx];
        arc_h = (e->charge_turns == 1 || e->is_boss) ? 30 : 18;

        e->x = (int16_t)(e->home_x + ((dest_x - e->home_x) * (int16_t)b->phase_timer) / (int16_t)dur);
        e->y = (int16_t)(e->home_y + ((b->hero_home_y - e->home_y) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, arc_h));
        b->vfx_x = (int16_t)(e->x - 6);
        b->vfx_y = (int16_t)(e->y - 10);

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
            if (b->timed_result == 1) {
                set_battle_vfx(b, BVFX_GUARD_SHIELD, 20, b->hero_home_x, (int16_t)(b->hero_home_y - 12), e->x, e->y);
                e->x = (int16_t)(e->x + 14);
            }
        }
    } else if (b->phase == BPHASE_ENEMY_IMPACT) {
        if (b->phase_timer >= 20) {
            b->ring.active = false;
            b->phase = BPHASE_ENEMY_RETURN;
            b->phase_timer = 0;
        }
    } else {
        const uint16_t dur = 16;
        int healthy, p_idx;
        e = &b->enemies[b->enemy_turn_idx];
        e->x = (int16_t)(e->x + ((e->home_x - e->x) * (int16_t)b->phase_timer) / (int16_t)dur);
        e->y = (int16_t)(e->y + ((e->home_y - e->y) * (int16_t)b->phase_timer) / (int16_t)dur - sin_arc(b->phase_timer, dur, 8));
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
                    return;
                }
            }
            b->enemy_turn_idx++;
            b->phase = BPHASE_ENEMY_SELECT;
            b->phase_timer = 0;
        }
    }
}

static void step_battle(HearthState *st, uint16_t just_pressed) {
    HearthBattleState *b = &st->battle;
    uint8_t i;

    if (!b->active) return;

    if (b->banner_frames > 0) b->banner_frames--;
    if (b->hero_flash_frames > 0) b->hero_flash_frames--;
    if (b->vfx_kind != BVFX_NONE) {
        b->vfx_timer++;
        if (b->vfx_timer >= b->vfx_dur) {
            b->vfx_kind = BVFX_NONE;
            b->vfx_timer = 0;
        }
    }
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

    if (just_pressed & PAD_A) b->timed_buffer = 9;
    else if (b->timed_buffer > 0) b->timed_buffer--;

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

        case BPHASE_COMMAND:
        case BPHASE_SWAP_ARC:
            step_battle_command(st, just_pressed);
            break;

        case BPHASE_PLAYER_WINDUP:
            step_battle_player_windup(st);
            break;

        case BPHASE_PLAYER_LUNGE:
            step_battle_player_lunge(st);
            break;

        case BPHASE_PLAYER_IMPACT:
        case BPHASE_PLAYER_FOLLOWUP:
        case BPHASE_PLAYER_RETURN:
            step_battle_player_aftermath(st);
            break;

        case BPHASE_ENEMY_SELECT:
        case BPHASE_ENEMY_TELEGRAPH:
        case BPHASE_ENEMY_LUNGE:
        case BPHASE_ENEMY_IMPACT:
        case BPHASE_ENEMY_RETURN:
            step_battle_enemy(st);
            break;

        case BPHASE_VICTORY_POSE: {
            if ((b->phase_timer % 18u) == 0u) {
                b->hero_scale_x_fp = 225;
                b->hero_scale_y_fp = 295;
            }
            if (b->phase_timer >= 48) {
                if (b->field_enemy_idx >= 0 && b->field_enemy_idx < HEARTH_MAX_FIELD_ENEMIES) {
                    st->field_enemies[(int)b->field_enemy_idx].alive = false;
                }
                if (b->encounter_preset == ENCOUNTER_SHRINE_BOSS) {
                    st->flag_boss_defeated = true;
                } else if (b->encounter_preset == ENCOUNTER_SEWER_BOSS) {
                    st->flag_muckmaw_defeated = true;
                    if (st->story_stage < STORY_MUCKMAW_DEFEATED) st->story_stage = STORY_MUCKMAW_DEFEATED;
                } else if (b->encounter_preset == ENCOUNTER_IRONFANG_BOSS) {
                    st->flag_ironfang_defeated = true;
                    st->story_stage = STORY_IRONFANG_DEFEATED;
                }
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
                if (st->has_waystone) {
                    st->hero_fx = TO_FP((int16_t)st->waystone_tx * 16 + 8);
                    st->hero_fy = TO_FP((int16_t)(st->waystone_ty + 1) * 16 + 8);
                }
                snap_cam_to_hero(st);
                st->mode = MODE_BATTLE_DEFEAT;
            }
            break;
        }
    }
}

/* ============================================================================
 * MERCHANT SHOPS (MODE_SHOP: BUY ITEMS OR SELL CAUGHT FISH FOR GUILDERS)
 * ============================================================================ */
void hearth_open_shop(HearthState *st, uint8_t shop_id) {
    HearthShopState *sh = &st->shop;
    sh->active = true;
    sh->shop_id = shop_id;
    sh->tab = 0;
    sh->cursor = 0;
    sh->msg_timer = 90;
    if (shop_id == 4u) sh->title = "ZARI'S GOLDEN BABANA STALL";
    else if (shop_id == 3u) sh->title = "TESSA'S SUNHILL BAKERY";
    else if (shop_id == 2u) sh->title = "MAPLE'S MOUNTAIN BAKERY";
    else if (shop_id == 1u) sh->title = "PORKINS' CITADEL PROVISIONS";
    else sh->title = "BARNABY'S MOUSE MARKET";
    copy_str_safe(sh->msg, sizeof(sh->msg), "L/R: Switch BUY / SELL FISH  B: Exit");
    st->dialogue.active = false;
    st->mode = MODE_SHOP;
    push_sfx(st, SFX_CONFIRM);
}

static void shop_buy_item(HearthState *st, uint8_t idx) {
    const HearthShopItem *it;
    HearthShopState *sh = &st->shop;
    if (idx >= HEARTH_SHOP_ITEM_COUNT) return;
    it = &HEARTH_SHOP_ITEMS[idx];
    if (it->item_kind == 3u && st->lure_tier >= 1u) {
        copy_str_safe(sh->msg, sizeof(sh->msg), "You already own River Lure!");
        push_sfx(st, SFX_CANCEL);
        return;
    }
    if (it->item_kind == 4u && st->lure_tier >= 2u) {
        copy_str_safe(sh->msg, sizeof(sh->msg), "You already own Glow Lure!");
        push_sfx(st, SFX_CANCEL);
        return;
    }
    if (st->guilders < it->price) {
        copy_str_safe(sh->msg, sizeof(sh->msg), "Not enough Guilders!");
        push_sfx(st, SFX_CANCEL);
        return;
    }
    st->guilders = (uint16_t)(st->guilders - it->price);
    if (it->item_kind == 0u) st->potions++;
    else if (it->item_kind == 1u) st->star_drops++;
    else if (it->item_kind == 2u) st->snacks++;
    else if (it->item_kind == 3u) st->lure_tier = 1;
    else if (it->item_kind == 4u) st->lure_tier = 2;
    else if (it->item_kind == 5u) st->babanas++;

    copy_str_safe(sh->msg, sizeof(sh->msg), "Purchased! Thank you!");
    sh->msg_timer = 75;
    push_sfx(st, SFX_COIN);
}

static void shop_sell_fish(HearthState *st, uint8_t idx) {
    HearthShopState *sh = &st->shop;
    if (idx >= HEARTH_FISH_SPECIES) return;
    if (st->fish_bag[idx] == 0u) {
        copy_str_safe(sh->msg, sizeof(sh->msg), "None in your creel to sell!");
        push_sfx(st, SFX_CANCEL);
        return;
    }
    st->fish_bag[idx]--;
    st->guilders = (uint16_t)(st->guilders + HEARTH_FISH_TABLE[idx].sell_price);
    fmt_int_text(sh->msg, sizeof(sh->msg), "Sold for +", (int16_t)HEARTH_FISH_TABLE[idx].sell_price, "G!");
    sh->msg_timer = 75;
    push_sfx(st, SFX_COIN);
}

static void step_shop(HearthState *st, uint16_t just_pressed) {
    HearthShopState *sh = &st->shop;
    uint8_t max_rows = (sh->tab == 0u) ? (uint8_t)HEARTH_SHOP_ITEM_COUNT : (uint8_t)HEARTH_FISH_SPECIES;

    if (sh->msg_timer > 0) sh->msg_timer--;

    if (just_pressed & (PAD_B | PAD_START)) {
        sh->active = false;
        st->mode = MODE_EXPLORE;
        push_sfx(st, SFX_CANCEL);
        return;
    }
    if (just_pressed & (PAD_LEFT | PAD_RIGHT | PAD_L | PAD_R)) {
        sh->tab = (uint8_t)(sh->tab ^ 1u);
        sh->cursor = 0;
        push_sfx(st, SFX_CURSOR);
        return;
    }
    if (just_pressed & PAD_UP) {
        sh->cursor = (uint8_t)((sh->cursor + max_rows - 1u) % max_rows);
        push_sfx(st, SFX_CURSOR);
    } else if (just_pressed & PAD_DOWN) {
        sh->cursor = (uint8_t)((sh->cursor + 1u) % max_rows);
        push_sfx(st, SFX_CURSOR);
    } else if (just_pressed & PAD_A) {
        if (sh->tab == 0u) shop_buy_item(st, sh->cursor);
        else shop_sell_fish(st, sh->cursor);
    }
}

/* ============================================================================
 * ZELDA / SEA-OF-STARS TIMED-REEL FISHING MINIGAME (MODE_FISHING)
 * ============================================================================ */
static uint8_t roll_fish_species(HearthState *st) {
    int16_t r = hearth_prng_range(&st->prng, 0, 99);
    if (r < 7) return 5u; /* Sunken Boot */
    if (st->zone_id == ZONE_CORAL_COVE) return (uint8_t)((r < 72) ? 4u : 0u); /* Tropical Coral Snapper */
    if (st->lure_tier >= 1u && r >= 84) return 4u; /* Rare Coral Snapper */
    if (st->zone_id == ZONE_SEWERS) return (uint8_t)((r < 68) ? 3u : 4u); /* Sewer Glow-Eel */
    if (st->zone_id == ZONE_WHISKERBURGH) return (uint8_t)((r < 65) ? 2u : 1u); /* Whisker Perch */
    if (st->zone_id == ZONE_ALDERBROOK) return (uint8_t)((r < 72) ? 1u : 0u); /* Mountain Meadow Trout */
    return (uint8_t)((r < 55) ? 0u : 1u); /* Sun Carp or Meadow Trout */
}

void hearth_start_fishing(HearthState *st) {
    HearthFishingState *f = &st->fishing;
    if (!st->fish_spot.active) return;

    hearth_memset(f, 0, (uint16_t)sizeof(*f));
    f->active = true;
    f->phase = FISH_PHASE_CAST;
    f->timer = 0;
    f->bite_delay = (uint16_t)hearth_prng_range(&st->prng, 65, 145);
    f->bobber_x = (int16_t)(((int16_t)st->fish_spot.tx + (int16_t)st->fish_spot.cast_dx) * 16 + 8);
    f->bobber_y = (int16_t)(((int16_t)st->fish_spot.ty + (int16_t)st->fish_spot.cast_dy) * 16 + 8);
    f->fish_pos = 50;
    f->fish_vel = 1;
    f->bar_pos = 50;
    f->bar_half_w = (uint8_t)(15u + st->lure_tier * 4u);
    f->progress = 26;
    f->tension = 18;
    f->species = roll_fish_species(st);
    f->weight_oz = (uint8_t)hearth_prng_range(&st->prng, 10, 48);

    st->hero_dir = (st->fish_spot.cast_dx > 0) ? (uint8_t)DIR_RIGHT : (uint8_t)DIR_DOWN;
    st->dialogue.active = false;
    st->mode = MODE_FISHING;
    push_sfx(st, SFX_WATER_SPLASH);
}

static void step_fishing_reeling(HearthState *st, uint16_t pad_bits) {
    HearthFishingState *f = &st->fishing;
    uint8_t agil = HEARTH_FISH_TABLE[f->species].agility;
    int16_t diff;

    /* Move player's sweet-spot bar with Left/Right or hold A to pull right */
    if (pad_bits & PAD_LEFT) f->bar_pos -= 3;
    if (pad_bits & PAD_RIGHT) f->bar_pos += 3;
    if (pad_bits & PAD_A) {
        if ((pad_bits & (PAD_LEFT | PAD_RIGHT)) == 0u) f->bar_pos += 2;
        if ((f->timer & 7u) == 0u) push_sfx(st, SFX_FISH_REEL);
    } else if ((pad_bits & (PAD_LEFT | PAD_RIGHT)) == 0u) {
        f->bar_pos -= 1;
    }
    f->bar_pos = iclamp16(f->bar_pos, 12, 88);

    /* Fish darts playfully inside 10..90 */
    if ((f->timer % (uint16_t)(28u - agil * 4u)) == 0u) {
        int16_t spd = (int16_t)agil;
        f->fish_vel = (int8_t)hearth_prng_range(&st->prng, (int16_t)(-spd), spd);
        if (f->fish_vel == 0) f->fish_vel = (int8_t)((f->fish_pos > 50) ? -1 : 1);
    }
    f->fish_pos = (int16_t)(f->fish_pos + f->fish_vel);
    if (f->fish_pos < 10) { f->fish_pos = 10; f->fish_vel = (int8_t)(-f->fish_vel); }
    if (f->fish_pos > 90) { f->fish_pos = 90; f->fish_vel = (int8_t)(-f->fish_vel); }

    diff = (int16_t)(f->fish_pos - f->bar_pos);
    if (diff < 0) diff = (int16_t)(-diff);

    if (diff <= (int16_t)f->bar_half_w) {
        /* Inside sweet spot -> reel progress climbs & tension eases! */
        if ((f->timer & 1u) == 0u && f->progress < 100u) f->progress++;
        if ((pad_bits & PAD_A) && (f->timer % 3u) == 0u && f->progress < 100u) f->progress++;
        if (f->tension > 0u && (f->timer % 3u) == 0u) f->tension--;
    } else {
        /* Outside sweet spot -> tension rises */
        if ((f->timer & 1u) == 0u && f->tension < 100u) f->tension++;
        if (f->progress > 4u && (f->timer % 5u) == 0u) f->progress--;
    }

    if (f->progress >= 100u) {
        f->phase = FISH_PHASE_CAUGHT;
        f->timer = 0;
        if (st->fish_bag[f->species] < 99u) st->fish_bag[f->species]++;
        st->fish_total_caught++;
        push_sfx(st, SFX_FISH_CATCH);
        spawn_burst(st, 2, f->bobber_x, f->bobber_y, 12, 0, 14, false);
        spawn_ftext(st, FP_PX(st->hero_fx), (int16_t)(FP_PX(st->hero_fy) - 24), HEARTH_FISH_TABLE[f->species].name, 3, 11, true);
    } else if (f->tension >= 100u) {
        f->phase = FISH_PHASE_ESCAPED;
        f->timer = 0;
        push_sfx(st, SFX_CANCEL);
    }
}

static void step_fishing(HearthState *st, uint16_t pad_bits, uint16_t just_pressed) {
    HearthFishingState *f = &st->fishing;
    f->timer++;

    if (just_pressed & PAD_B) {
        f->active = false;
        st->mode = MODE_EXPLORE;
        push_sfx(st, SFX_CANCEL);
        return;
    }

    if (f->phase == FISH_PHASE_CAST) {
        if (f->timer >= 20u) {
            f->phase = FISH_PHASE_WAIT_BITE;
            f->timer = 0;
            spawn_burst(st, 2, f->bobber_x, f->bobber_y, 6, 0, 14, false);
        }
    } else if (f->phase == FISH_PHASE_WAIT_BITE) {
        if (f->timer >= f->bite_delay) {
            f->phase = FISH_PHASE_BITE_ALERT;
            f->timer = 0;
            push_sfx(st, SFX_FISH_BITE);
            add_shake(st, 2, 6);
        }
    } else if (f->phase == FISH_PHASE_BITE_ALERT) {
        if (just_pressed & PAD_A) {
            f->phase = FISH_PHASE_REELING;
            f->timer = 0;
            push_sfx(st, SFX_FISH_REEL);
        } else if (f->timer >= 54u) {
            f->phase = FISH_PHASE_ESCAPED;
            f->timer = 0;
            push_sfx(st, SFX_CANCEL);
        }
    } else if (f->phase == FISH_PHASE_REELING) {
        step_fishing_reeling(st, pad_bits);
    } else {
        /* CAUGHT or ESCAPED summary: A casts again, B exits */
        if (f->timer > 18u && (just_pressed & PAD_A)) {
            hearth_start_fishing(st);
        }
    }
}

/* ============================================================================
 * DIALOGUE, INTERACTION & BYTECODE CUTSCENE VM
 * ============================================================================ */
static uint8_t infer_portrait_id(const HearthState *st, const char *speaker) {
    if (!speaker || speaker[0] == '\0') return (uint8_t)PORTRAIT_NONE;
    if ((speaker[0] == 'R' && speaker[1] == 'o' && speaker[2] == 'w') ||
        (speaker[0] == 'L' && speaker[1] == 'i' && speaker[2] == 't')) {
        return (uint8_t)PORTRAIT_ROWAN;
    }
    if (speaker[0] == 'G' && speaker[1] == 'r' && speaker[2] == 'a') {
        return (uint8_t)PORTRAIT_MARA;
    }
    if (speaker[0] == 'P' && speaker[1] == 'i' && speaker[2] == 'p') {
        return (uint8_t)PORTRAIT_PIP;
    }
    if ((speaker[0] == 'B' && speaker[1] == 'a' && speaker[2] == 'k') ||
        (speaker[0] == 'M' && speaker[1] == 'a' && speaker[2] == 'p')) {
        return (uint8_t)PORTRAIT_MAPLE;
    }
    if (speaker[0] == 'J' && speaker[1] == 'a' && speaker[2] == 's') {
        return (uint8_t)PORTRAIT_JASPER;
    }
    if (speaker[0] == 'M' && speaker[1] == 'i' && speaker[2] == 'd') {
        return (uint8_t)PORTRAIT_MIDO;
    }
    if (speaker[0] == 'T' && speaker[1] == 'e' && speaker[2] == 's') {
        return (uint8_t)PORTRAIT_TESSA;
    }
    if ((speaker[0] == 'A' && speaker[1] == 'r' && speaker[2] == 'c') ||
        (speaker[0] == 'C' && speaker[1] == 'o' && speaker[2] == 'r')) {
        return (uint8_t)PORTRAIT_CORIN;
    }
    if ((speaker[0] == 'R' && speaker[1] == 'o' && speaker[2] == 'y') ||
        (speaker[0] == 'S' && speaker[1] == 'e' && speaker[2] == 'n')) {
        return (uint8_t)PORTRAIT_SENTINEL;
    }
    if (speaker[0] == 'B' && speaker[1] == 'a' && speaker[2] == 'r' &&
        (st->zone_id == ZONE_ALDERBROOK || st->zone_id == ZONE_ROWAN_HOME ||
         st->zone_id == ZONE_WHISPERING_WOODS || st->zone_id == ZONE_HOLLOW_GLADE)) {
        return (uint8_t)PORTRAIT_BARNABY;
    }
    return (uint8_t)PORTRAIT_NONE;
}

static void load_dialogue_page(HearthState *st, const char *text) {
    HearthDialogue *d = &st->dialogue;
    uint16_t len, limit, cut, i, sent_cut, space_cut;
    const char *nxt;

    if (!text) {
        d->full_text = "";
        d->next_text = NULL;
        d->full_len = 0;
        d->visible_chars = 0;
        d->finished = true;
        return;
    }
    while (*text == ' ') text++;
    len = str_len16(text);
    if (st->tuning.ppu_mode == PPU_SNES_256x224) {
        limit = (d->choice_count > 0u) ? 68u : 74u;
    } else {
        limit = (d->choice_count > 0u) ? 88u : 96u;
    }

    if (len <= limit) {
        d->full_text = text;
        d->full_len = len;
        d->next_text = NULL;
    } else {
        sent_cut = 0u;
        space_cut = 0u;
        for (i = 0u; i < limit; i++) {
            char c = text[i];
            if (c == ' ') space_cut = i;
            if ((c == '.' || c == '!' || c == '?') &&
                (text[i + 1u] == ' ' || text[i + 1u] == '\0') &&
                i >= (uint16_t)(limit >> 1)) {
                sent_cut = (uint16_t)(i + 1u);
            }
        }
        if (sent_cut > 0u)       cut = sent_cut;
        else if (space_cut > 0u) cut = space_cut;
        else                     cut = limit;

        nxt = text + cut;
        while (*nxt == ' ') nxt++;
        d->full_text = text;
        d->full_len = cut;
        d->next_text = (*nxt != '\0') ? nxt : NULL;
    }
    d->visible_chars = 0;
    d->finished = (d->full_len == 0u);
    d->pause_frames = 0;
}

static void open_dialogue(
    HearthState *st, const char *speaker, uint8_t voice, const char *text,
    uint8_t choice_count, const HearthChoice *choices
) {
    HearthDialogue *d = &st->dialogue;
    uint8_t i;
    d->active = true;
    d->speaker = speaker;
    d->voice_sfx = voice;
    d->portrait_id = infer_portrait_id(st, speaker);
    d->choice_count = choice_count;
    d->selected_choice = 0;
    for (i = 0; i < choice_count && i < HEARTH_MAX_CHOICES; i++) {
        d->choices[i] = choices[i];
    }
    load_dialogue_page(st, text);
}

static void confirm_dialogue_choice(HearthState *st) {
    uint8_t act = st->dialogue.choices[st->dialogue.selected_choice].action_id;
    st->dialogue.active = false;
    push_sfx(st, SFX_CONFIRM);
    if (act == ACT_TAKE_COURAGE && st->flag_has_heart_vessel)       hearth_start_cutscene(st, CUTSCENE_TAKE_COURAGE);
    else if (act == ACT_GIVE_COURAGE && st->flag_has_heart_vessel)  hearth_start_cutscene(st, CUTSCENE_GIVE_COURAGE);
    else if (act == ACT_REPLAY_INTRO)  hearth_start_cutscene(st, CUTSCENE_INTRO_DEMO);
    else if (act == ACT_JUMP_BOSS)     hearth_start_battle(st, ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, -1);
    else if (act == ACT_OPEN_SHOP_0)   hearth_open_shop(st, 0);
    else if (act == ACT_OPEN_SHOP_1)   hearth_open_shop(st, 1);
    else if (act == ACT_OPEN_SHOP_2)   hearth_open_shop(st, 2);
    else if (act == ACT_OPEN_SHOP_3)   hearth_open_shop(st, 3);
    else if (act == ACT_OPEN_SHOP_4)   hearth_open_shop(st, 4);
    else if (act == ACT_MEND_CAPTAIN && st->flag_has_heart_vessel)  hearth_start_cutscene(st, CUTSCENE_MEND_CAPTAIN);
    else if (act == ACT_START_FISHING) hearth_start_fishing(st);
    else if (act == ACT_REST_BED) {
        if (st->ch1_step == 5u) {
            hearth_start_story_cg(st, 2u);
        } else {
            hearth_heal_party(st);
        }
    }
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
    } else if (d->next_text != NULL) {
        if (just_pressed & (PAD_A | PAD_B)) {
            push_sfx(st, SFX_CONFIRM);
            load_dialogue_page(st, d->next_text);
        }
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
    idx = actor_id - ACTOR_NPC0;
    if (idx >= 0 && idx < HEARTH_MAX_NPCS) {
        *ox = FROM_FP(st->npcs[idx].fx);
        *oy = FROM_FP(st->npcs[idx].fy);
        return;
    }
    *ox = FROM_FP(st->hero_fx); *oy = FROM_FP(st->hero_fy);
}

static void apply_cutscene_flag(HearthState *st, int16_t flag_id) {
    if (flag_id == FLAG_HAS_COURAGE) {
        st->flag_has_courage = true;
        st->npcs[1].aura = AURA_MENDED;
    } else if (flag_id == FLAG_GUARD_MENDED) {
        st->flag_guard_mended = true;
        st->flag_has_courage = false;
        st->potions++;
        st->guilders = (uint16_t)(st->guilders + 25u);
        st->npcs[2].aura = AURA_MENDED;
        spawn_ftext(st, FROM_FP(st->npcs[2].fx), (int16_t)(FROM_FP(st->npcs[2].fy) - 24), "HEART MENDED!", 3, 11, true);
    } else if (flag_id == FLAG_ELDER_SEEN) {
        st->flag_elder_seen = true;
    } else if (flag_id == FLAG_FLASHBACK_DONE) {
        st->story_stage = STORY_AGE10_ALDERBROOK;
        st->flashback_triggered = true;
    } else if (flag_id == FLAG_WAND_RECEIVED) {
        st->flag_has_wand = true;
        st->pip_visible = true;
        if (st->story_stage < STORY_EVERTALE_AWAKENED) st->story_stage = STORY_EVERTALE_AWAKENED;
    } else if (flag_id == FLAG_SEWER_KEY) {
        st->flag_sewer_key = true;
        if (st->story_stage < STORY_SEWER_KEY) st->story_stage = STORY_SEWER_KEY;
    } else if (flag_id == FLAG_CAPTAIN_MENDED) {
        st->flag_captain_mended = true;
        st->flag_has_courage = false;
        st->npcs[1].aura = AURA_MENDED;
        st->guilders = (uint16_t)(st->guilders + 40u);
    } else if (flag_id == FLAG_MUCKMAW_DEFEATED) {
        st->flag_muckmaw_defeated = true;
        if (st->story_stage < STORY_MUCKMAW_DEFEATED) st->story_stage = STORY_MUCKMAW_DEFEATED;
    } else if (flag_id == FLAG_IRONFANG_DEFEATED) {
        st->flag_ironfang_defeated = true;
        st->story_stage = STORY_IRONFANG_DEFEATED;
    } else if (flag_id == FLAG_CH1_MARA_GREETED) {
        if (st->ch1_step < 1u) st->ch1_step = 1u;
    } else if (flag_id == FLAG_CH1_MAPLE_LOAF) {
        st->ch1_errands_mask |= 1u;
        st->guilders = (uint16_t)(st->guilders + 15u);
        if ((st->ch1_errands_mask & 3u) == 3u) {
            if (st->ch1_step < 2u) st->ch1_step = 2u;
            if (st->time_of_day < TOD_AFTERNOON) {
                st->time_of_day = TOD_AFTERNOON;
                if (st->zone_id == ZONE_ALDERBROOK) st->terrain_pal_id = ZPAL_ALDERBROOK;
            }
        }
    } else if (flag_id == FLAG_CH1_BARNABY_THERMOS) {
        st->ch1_errands_mask |= 2u;
        if ((st->ch1_errands_mask & 3u) == 3u) {
            if (st->ch1_step < 2u) st->ch1_step = 2u;
            if (st->time_of_day < TOD_AFTERNOON) {
                st->time_of_day = TOD_AFTERNOON;
                if (st->zone_id == ZONE_ALDERBROOK) st->terrain_pal_id = ZPAL_ALDERBROOK;
            }
        }
    } else if (flag_id == FLAG_CH1_JASPER_MET) {
        st->ch1_errands_mask |= 4u;
    } else if (flag_id == FLAG_CH1_DUSK_TEA) {
        if (st->ch1_step < 5u) st->ch1_step = 5u;
    } else if (flag_id == FLAG_CH2_TESSA_CLUE) {
        if (st->ch2_step < 1u) st->ch2_step = 1u;
    } else if (flag_id == FLAG_CH2_CORIN_HINTS) {
        if (st->ch2_step < 2u) st->ch2_step = 2u;
    } else if (flag_id == FLAG_CH2_MIDO_REUNION) {
        if (st->ch2_step < 3u) st->ch2_step = 3u;
        st->pip_visible = true;
    } else if (flag_id == FLAG_CH2_FIRST_FAMILIAR) {
        if (st->ch2_step < 4u) st->ch2_step = 4u;
        st->flag_has_familiar = true;
        st->flag_has_wand = true;
        st->pip_visible = true;
    } else if (flag_id == FLAG_CH2_ORIN_QUEST) {
        if (st->ch2_step < 5u) st->ch2_step = 5u;
    } else if (flag_id == FLAG_CH2_RAFT_BUILT) {
        if (st->ch2_step < 6u) st->ch2_step = 6u;
        st->vehicle_unlocked_mask |= (uint8_t)VEH_MASK_RAFT;
        spawn_ftext(st, FP_PX(st->hero_fx), (int16_t)(FP_PX(st->hero_fy) - 24), "CEDAR RAFT!", 3, 11, true);
    } else if (flag_id == FLAG_CH2_CORAL_MET) {
        if (st->ch2_step < 7u) st->ch2_step = 7u;
        st->guilders = (uint16_t)(st->guilders + 25u);
    } else if (flag_id == FLAG_CH2_BABANA_CLUE) {
        if (st->ch2_step < 7u) st->ch2_step = 7u;
        st->babanas++;
    }
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
                int16_t mw = (int16_t)(st->map_w ? st->map_w : (uint8_t)HEARTH_MAP_W);
                int16_t mh = (int16_t)(st->map_h ? st->map_h : (uint8_t)HEARTH_MAP_H);
                get_ppu_dims(st, &w, &h, &lim);
                mx = (int16_t)(mw * 16 - (int)w);
                my = (int16_t)(mh * 16 - (int)h);
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
                int16_t mw = (int16_t)(st->map_w ? st->map_w : (uint8_t)HEARTH_MAP_W);
                int16_t mh = (int16_t)(st->map_h ? st->map_h : (uint8_t)HEARTH_MAP_H);
                get_ppu_dims(st, &w, &h, &lim);
                mx = (int16_t)(mw * 16 - (int)w);
                my = (int16_t)(mh * 16 - (int)h);
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
                else if (op->a0 >= ACTOR_NPC0 && op->a0 < ACTOR_NPC0 + HEARTH_MAX_NPCS) {
                    st->npcs[op->a0 - ACTOR_NPC0].dir = (uint8_t)op->a1;
                }
                break;
            case OP_EMOTE:
                if (op->a0 == ACTOR_HERO) { st->hero_emote = (uint8_t)op->a1; st->hero_emote_frames = op->a2; }
                else if (op->a0 == ACTOR_PIP) { st->pip_emote = (uint8_t)op->a1; st->pip_emote_frames = op->a2; }
                else if (op->a0 >= ACTOR_NPC0 && op->a0 < ACTOR_NPC0 + HEARTH_MAX_NPCS) {
                    st->npcs[op->a0 - ACTOR_NPC0].emote = (uint8_t)op->a1;
                    st->npcs[op->a0 - ACTOR_NPC0].emote_frames = op->a2;
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
                apply_cutscene_flag(st, op->a0);
                break;
            case OP_SET_PAL:
                st->terrain_pal_id = (uint8_t)op->a0;
                st->bgm_id = (uint8_t)op->a1;
                break;
            case OP_WARP_ZONE:
                hearth_enter_zone(st, (uint8_t)op->a0, (uint8_t)op->a1, op->a2);
                break;
            case OP_SHOW_ACTOR:
                if (op->a0 >= 0 && op->a0 < HEARTH_MAX_NPCS) {
                    if (op->a1 < 0) {
                        st->npcs[op->a0].active = false;
                    } else {
                        st->npcs[op->a0].active = true;
                        st->npcs[op->a0].fx = TO_FP(op->a1 * 16 + 8);
                        st->npcs[op->a0].fy = TO_FP((int16_t)op->a2 * 16 + 8);
                    }
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
 * ROAMING ENEMIES, PORTAL WARPS & MULTI-ZONE INTERACTABLES
 * ============================================================================ */
static void retarget_enemy_patrol(HearthState *st, HearthFieldEnemy *e) {
    int16_t hdx, hdy, pick;
    e->patrol_timer = (uint8_t)hearth_prng_range(&st->prng, 50, 105);
    hdx = world_dx(st, FP_PX(e->home_fx), FP_PX(e->fx));
    hdy = world_dy(st, FP_PX(e->home_fy), FP_PX(e->fy));
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
    uint16_t c;

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
    if (HS.is_world_map) {
        nfx = wrap_globe_fp_x(nfx);
        nfy = wrap_globe_fp_y(nfy);
    }
    px = FP_PX(nfx);
    py = FP_PX(nfy);
    c = (uint16_t)get_col_at_px(st, px, py);
    if (c == COL_WALKABLE || c == COL_TALL_GRASS) {
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
    if (HS.is_world_map) {
        nfx = wrap_globe_fp_x(nfx);
        nfy = wrap_globe_fp_y(nfy);
    }
    cx = (uint16_t)get_col_at_px(st, FP_PX(nfx), ey);
    cy = (uint16_t)get_col_at_px(st, ex, FP_PX(nfy));
    if (cx == COL_WALKABLE || cx == COL_TALL_GRASS) e->fx = nfx;
    if (cy == COL_WALKABLE || cy == COL_TALL_GRASS) e->fy = nfy;
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
    uint16_t check_aggro = (can_engage && HS.hero_safe_frames == 0 && HS.active_vehicle == VEH_FOOT) ? 1u : 0u;
    bool globe = HS.is_world_map;

    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++, e++) {
        int16_t ex, ey, dx, dy, adx, ady, sight, doct;

        if (!e->alive) continue;
        e->anim_frame++;
        if (e->alert_frames > 0) e->alert_frames--;

        if (globe || check_aggro) {
            ex = FP_PX(e->fx);
            ey = FP_PX(e->fy);
            dx = world_dx(st, hx, ex);
            dy = world_dy(st, hy, ey);
            /* On the giant 4096x3072 globe, skip patrol/collision AI for far off-screen enemies */
            if (globe && (dx < -208 || dx > 208 || dy < -184 || dy > 184)) {
                e->alerted = false;
                continue;
            }
            if (check_aggro) {
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
        }
        if (!e->is_boss) {
            step_enemy_patrol(st, e);
        } else {
            e->alerted = false;
        }
    }
}

static bool near_point_px(int16_t hx, int16_t hy, int16_t tx, int16_t ty, int16_t rad) {
    int16_t dx = (int16_t)(hx - tx);
    int16_t dy = (int16_t)(hy - ty);
    int16_t adx, ady, doct;
    if (dx <= -rad || dx >= rad || dy <= -rad || dy >= rad) return false;
    adx = (dx < 0) ? (int16_t)(-dx) : dx;
    ady = (dy < 0) ? (int16_t)(-dy) : dy;
    doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
    return (doct < (int16_t)(rad + 2));
}

static bool near_world_point_px(const HearthState *st, int16_t hx, int16_t hy, int16_t tx, int16_t ty, int16_t rad) {
    int16_t dx = world_dx(st, hx, tx);
    int16_t dy = world_dy(st, hy, ty);
    int16_t adx, ady, doct;
    if (dx <= -rad || dx >= rad || dy <= -rad || dy >= rad) return false;
    adx = (dx < 0) ? (int16_t)(-dx) : dx;
    ady = (dy < 0) ? (int16_t)(-dy) : dy;
    doct = (adx > ady) ? (int16_t)(adx + (ady >> 1)) : (int16_t)(ady + (adx >> 1));
    return (doct < (int16_t)(rad + 2));
}

static int find_ch1_prop_interactable(const HearthState *st, int16_t hx, int16_t hy, int16_t *ox, int16_t *oy, const char **olabel) {
    static const uint8_t s_tx[3] = { 12u, 26u, 33u };
    static const uint8_t s_ty[3] = { 17u, 15u,  9u };
    static const uint8_t p_tx[3] = {  8u, 15u, 34u };
    static const uint8_t p_ty[3] = { 15u, 16u, 15u };
    int16_t nx, ny, i;
    ST_UNUSED(st);

    if (HS.zone_id == ZONE_ROWAN_HOME) {
        nx = (int16_t)(29 * 16 + 8);
        ny = (int16_t)(9 * 16 + 8);
        if (near_point_px(hx, hy, nx, ny, 24)) {
            *ox = nx; *oy = (int16_t)(ny - 18); *olabel = "Cedar Chest";
            return 14;
        }
        nx = (int16_t)(14 * 16 + 8);
        ny = (int16_t)(15 * 16 + 12);
        if (near_point_px(hx, hy, nx, ny, 24)) {
            *ox = nx; *oy = (int16_t)(ny - 18); *olabel = (HS.ch1_step == 5u) ? "Sleep" : "Rowan's Bed";
            return 15;
        }
    } else if (HS.zone_id == ZONE_WHISPERING_WOODS) {
        for (i = 0; i < 3; i++) {
            nx = (int16_t)((int16_t)s_tx[i] * 16 + 8);
            ny = (int16_t)((int16_t)s_ty[i] * 16 + 8);
            if (near_point_px(hx, hy, nx, ny, 24)) {
                *ox = nx; *oy = (int16_t)(ny - 20);
                *olabel = ((HS.woods_shrines_mask & (uint8_t)(1u << i)) != 0u) ? "Lit Lantern" : "Light Lantern";
                return 16 + (int)i;
            }
        }
    } else if (HS.zone_id == ZONE_HOLLOW_GLADE) {
        nx = (int16_t)(21 * 16 + 8);
        ny = (int16_t)(12 * 16 + 8);
        if (near_point_px(hx, hy, nx, ny, 26)) {
            *ox = nx; *oy = (int16_t)(ny - 22);
            *olabel = (HS.story_stage >= STORY_EVERTALE_AWAKENED) ? "Enter Evertale" : "Hollow Oak";
            return 19;
        }
    } else if (HS.zone_id == ZONE_BRAMBLE_EDGE) {
        nx = (int16_t)(31 * 16 + 8);
        ny = (int16_t)(14 * 16 + 8);
        if (near_point_px(hx, hy, nx, ny, 26)) {
            *ox = nx; *oy = (int16_t)(ny - 22);
            *olabel = HS.flag_has_familiar ? "Familiar Shrine" : "Touch Shrine";
            return 19;
        }
    } else if (HS.zone_id == ZONE_TIMBER_PIER && (HS.vehicle_unlocked_mask & VEH_MASK_RAFT) == 0u) {
        for (i = 0; i < 3; i++) {
            if ((HS.planks_found_mask & (uint8_t)(1u << i)) != 0u) continue;
            nx = (int16_t)((int16_t)p_tx[i] * 16 + 8);
            ny = (int16_t)((int16_t)p_ty[i] * 16 + 8);
            if (near_point_px(hx, hy, nx, ny, 24)) {
                *ox = nx; *oy = (int16_t)(ny - 18); *olabel = "Cedar Plank";
                return 30 + (int)i;
            }
        }
    }
    return -1;
}

static int find_interactable(const HearthState *st, int16_t *ox, int16_t *oy, const char **olabel) {
    int16_t hx = FP_PX(HS.hero_fx);
    int16_t hy = FP_PX(HS.hero_fy);
    int16_t px = hx, py = hy, nx, ny;
    int16_t i;
    int ch1_hit;
    ST_UNUSED(st);

    if (HS.is_world_map) {
        for (i = 0; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
            const HearthGlobeLandmark *lm = &HEARTH_GLOBE_LANDMARKS[i];
            int16_t ddx;
            nx = (int16_t)(((int16_t)lm->tx << 4) + 8);
            ddx = wrap_delta_x((int16_t)(hx - nx));
            if (ddx <= -24 || ddx >= 24) continue;
            ny = (int16_t)(((int16_t)lm->ty << 4) + 8);
            if (near_world_point_px(st, hx, hy, nx, ny, 24)) {
                *ox = nx; *oy = (int16_t)(ny - 20); *olabel = lm->name;
                return 40 + (int)i;
            }
        }
        return -1;
    }

    if (HS.hero_dir == DIR_UP) py -= 15;
    else if (HS.hero_dir == DIR_DOWN) py += 15;
    else if (HS.hero_dir == DIR_LEFT) px -= 15;
    else px += 15;

    for (i = 0; i < HEARTH_MAX_NPCS; i++) {
        if (!HS.npcs[i].active) continue;
        nx = FP_PX(HS.npcs[i].fx);
        ny = FP_PX(HS.npcs[i].fy);
        if (near_point_px(hx, hy, nx, ny, 22) || near_point_px(px, py, nx, ny, 19)) {
            *ox = nx; *oy = (int16_t)(ny - 24); *olabel = HS.npcs[i].name;
            return (int)i;
        }
    }
    ch1_hit = find_ch1_prop_interactable(st, hx, hy, ox, oy, olabel);
    if (ch1_hit >= 0) return ch1_hit;

    if (HS.has_waystone) {
        nx = (int16_t)((int16_t)HS.waystone_tx * 16 + 8);
        ny = (int16_t)((int16_t)HS.waystone_ty * 16 + 8);
        if (near_point_px(hx, hy, nx, ny, 22)) {
            *ox = nx; *oy = (int16_t)(ny - 22); *olabel = "Waystone";
            return 10;
        }
    }
    if (HS.chest.active && ((HS.chests_opened_mask & (uint16_t)(1u << HS.chest.chest_bit)) == 0u)) {
        nx = (int16_t)((int16_t)HS.chest.tx * 16 + 8);
        ny = (int16_t)((int16_t)HS.chest.ty * 16 + 8);
        if (near_point_px(hx, hy, nx, ny, 22)) {
            *ox = nx; *oy = (int16_t)(ny - 18); *olabel = "Chest";
            return 11;
        }
    }
    if (HS.has_sign) {
        nx = (int16_t)((int16_t)HS.sign_tx * 16 + 8);
        ny = (int16_t)((int16_t)HS.sign_ty * 16 + 8);
        if (near_point_px(hx, hy, nx, ny, 21)) {
            *ox = nx; *oy = (int16_t)(ny - 18); *olabel = "Read Sign";
            return 12;
        }
    }
    if (HS.fish_spot.active) {
        nx = (int16_t)((int16_t)HS.fish_spot.tx * 16 + 8);
        ny = (int16_t)((int16_t)HS.fish_spot.ty * 16 + 8);
        if (near_point_px(hx, hy, nx, ny, 22)) {
            *ox = nx; *oy = (int16_t)(ny - 18); *olabel = "Fish Here";
            return 13;
        }
    }
    for (i = 0; i < HEARTH_MAX_PORTALS; i++) {
        if (!HS.portals[i].active) continue;
        nx = (int16_t)((int16_t)HS.portals[i].tx * 16 + 8);
        ny = (int16_t)((int16_t)HS.portals[i].ty * 16 + 8);
        if (near_point_px(hx, hy, nx, ny, 22)) {
            *ox = nx; *oy = (int16_t)(ny - 20); *olabel = HS.portals[i].label;
            return 20 + (int)i;
        }
    }
    return -1;
}

static void interact_chest(HearthState *st, int16_t ox, int16_t oy) {
    uint8_t rk = st->chest.reward_kind;
    st->chests_opened_mask |= (uint16_t)(1u << st->chest.chest_bit);
    if (st->chest.chest_bit == 0u) st->flag_chest_opened = true;
    push_sfx(st, SFX_CHEST_OPEN);
    spawn_burst(st, 3, ox, (int16_t)(oy + 10), 12, 3, 11, false);
    if (rk == 0u) {
        st->potions += 2;
        st->guilders = (uint16_t)(st->guilders + 25u);
        spawn_ftext(st, ox, oy, "+2 TONICS +25G", 3, 11, true);
        open_dialogue(st, "Treasure Chest", SFX_TEXT_HERO,
            "Found 2 Sun Tonics and 25 Guilders!", 0, NULL);
    } else if (rk == 1u) {
        st->star_drops += 2;
        st->guilders = (uint16_t)(st->guilders + 35u);
        spawn_ftext(st, ox, oy, "+2 STARS +35G", 3, 15, true);
        open_dialogue(st, "Treasure Chest", SFX_TEXT_HERO,
            "Found 2 Star Drops and 35 Guilders tucked among the rocks!", 0, NULL);
    } else if (rk == 2u) {
        st->snacks += 1;
        st->guilders = (uint16_t)(st->guilders + 50u);
        spawn_ftext(st, ox, oy, "+1 SNACK +50G", 3, 11, true);
        open_dialogue(st, "Treasure Chest", SFX_TEXT_HERO,
            "Found a Courage Snack (full party restore) and 50 Guilders!", 0, NULL);
    } else {
        st->potions += 1;
        st->guilders = (uint16_t)(st->guilders + 90u);
        spawn_ftext(st, ox, oy, "+1 TONIC +90G", 3, 11, true);
        open_dialogue(st, "Treasure Chest", SFX_TEXT_HERO,
            "Boarhold royal coffer! Found 90 Guilders and a Sun Tonic!", 0, NULL);
    }
}

static void interact_signpost(HearthState *st) {
    push_sfx(st, SFX_CONFIRM);
    if (st->zone_id == ZONE_ALDERBROOK) {
        open_dialogue(st, "Alderbrook Sign", SFX_TEXT_ELDER,
            "NORTH: Steam Rail Station | WEST: Rowan's Cottage | EAST: Maple's Bakery & Whispering Woods | SOUTH: Mountain Brook Pier.", 0, NULL);
    } else if (st->zone_id == ZONE_WHISPERING_WOODS) {
        open_dialogue(st, "Trail Marker", SFX_TEXT_ELDER,
            "WHISPERING WOODS | Follow the 3 ancient stone lanterns across the rapids bridge to reach Hollow Glade atop the ridge.", 0, NULL);
    } else if (st->zone_id == ZONE_SUNHILL_MARKET) {
        open_dialogue(st, "Sunhill Sign", SFX_TEXT_ELDER,
            "SUNHILL MARKET | WEST: Tessa's Bakery | UP STAIRS: Sunlit Fountain & Archivist Corin | NORTH RIDGE: Castle Aurelia.", 0, NULL);
    } else if (st->zone_id == ZONE_CASTLE_GATE) {
        open_dialogue(st, "Royal Notice", SFX_TEXT_ELDER,
            "CASTLE AURELIA | Inner keep reserved for Guild Pass holders. Travelers and scholars welcome in Sunhill Market directly south.", 0, NULL);
    } else if (st->zone_id == ZONE_BRAMBLE_EDGE) {
        open_dialogue(st, "Forest Post", SFX_TEXT_ELDER,
            "BRAMBLEWOOD CROSSING | Ancient Familiar Boundary Stone lies east. Do not enter the deep wilds without a bonded Familiar.", 0, NULL);
    } else if (st->zone_id == ZONE_SUNKEN_RUINS) {
        open_dialogue(st, "Coastal Stone", SFX_TEXT_ELDER,
            "SUNKEN WATCHTOWER | Old sea-cliffs overlooking the southern bay. Scholars from Sunhill Market study its weathered runes.", 0, NULL);
    } else if (st->zone_id == ZONE_TIMBER_PIER) {
        open_dialogue(st, "Harbor Board", SFX_TEXT_ELDER,
            "HEARTLAND TIMBER PIER | Shipwright Orin's Raft Yard. Turquoise coastal channels reach Suncrest Paradise (SW) and Sands of Babana (SE).", 0, NULL);
    } else if (st->zone_id == ZONE_CORAL_COVE) {
        open_dialogue(st, "Lagoon Post", SFX_TEXT_ELDER,
            "SUNCREST CORAL COVE | EAST PIER: Tropical Coral Snapper Fishing | NORTH CHANNEL: Sail back to Heartland or east to Sands of Babana.", 0, NULL);
    } else if (st->zone_id == ZONE_BABANA_OASIS) {
        open_dialogue(st, "Oasis Tablet", SFX_TEXT_ELDER,
            "OASIS OF BABANA | WEST: Merchant Zari's Golden Babana Stall | CENTER: Healing Waystone | WATCH OUT: Dune Scorpions & Giant Sandworm roam the dunes!", 0, NULL);
    } else if (st->zone_id == ZONE_WHISKERBURGH) {
        open_dialogue(st, "Market Sign", SFX_TEXT_ELDER,
            "WEST: Evertale World Map | NORTH: Sunken Sewers | SOUTH: Canal Fishing Jetty | EAST: Barnaby's Shop.", 0, NULL);
    } else if (st->zone_id == ZONE_SEWERS) {
        open_dialogue(st, "Sewer Marker", SFX_TEXT_ELDER,
            "WEST: Cistern Pool (Glow-Eels!) | NORTH: King Muckmaw & Boarhold Sluice | SOUTH: Whiskerburgh.", 0, NULL);
    } else if (st->zone_id == ZONE_BOARHOLD) {
        open_dialogue(st, "Citadel Banner", SFX_TEXT_ELDER,
            "BOARHOLD CITADEL | WEST: Porkins' Shop & Captain Tusk | EAST across Moat: Lord Ironfang's Sanctum.", 0, NULL);
    } else {
        open_dialogue(st, "Hamlet Sign", SFX_TEXT_ELDER,
            "WEST: Evertale World Map | SOUTH: Creek Fishing Pier | EAST: Briar Shrine & Whiskerburgh Mouse Town.", 0, NULL);
    }
}

static void interact_portal(HearthState *st, int pidx) {
    const HearthPortal *pt = &st->portals[pidx];
    if (st->zone_id == ZONE_WHISPERING_WOODS && pt->dst_zone == ZONE_HOLLOW_GLADE &&
        st->time_of_day == TOD_NIGHT && (st->woods_shrines_mask & 7u) != 7u) {
        push_sfx(st, SFX_CANCEL);
        open_dialogue(st, "Misty Ridge Pass", SFX_TEXT_HERO,
            "Thick silver mountain fog hides the upper switchback! Light all 3 ancient stone lanterns along the Whispering Woods trail first.", 0, NULL);
        return;
    }
    if (pt->req_stage > 0u && st->story_stage < pt->req_stage) {
        push_sfx(st, SFX_CANCEL);
        if (pt->req_stage == STORY_SEWER_KEY) {
            open_dialogue(st, "Locked Grate", SFX_TEXT_PIP,
                "The iron sewer grate is locked! Speak with Mayor Brie in Whiskerburgh for the key and Lantern Charm.", 0, NULL);
        } else {
            open_dialogue(st, "Sealed Sluice", SFX_TEXT_PIP,
                "Heavy sludge seals the northern sluice! Defeat King Muckmaw in the sewer hall to clear the way to Boarhold.", 0, NULL);
        }
        return;
    }
    request_zone_warp(st, pt->dst_zone, pt->dst_tx, pt->dst_ty);
}

static void interact_npc_hamlet(HearthState *st, int hit) {
    if (st->story_stage == STORY_AGE5_FLASHBACK && hit == 0) {
        st->flashback_triggered = true;
        hearth_start_cutscene(st, CUTSCENE_SHADOW_GRANDMA);
        return;
    }
    if (!st->flag_has_heart_vessel) {
        /* Early-game peaceful hamlet: zero Give/Take Heart prompts until Hearthglass Pendant! */
        if (hit == 0) {
            open_dialogue(st, "Elder Wren", SFX_TEXT_ELDER,
                "Welcome to Windmill Hamlet, young traveler! Searching for a bright amber fox-kit? Try crossing the river east to Sunhill Market - every road meets beneath Castle Aurelia.", 0, NULL);
        } else if (hit == 1) {
            open_dialogue(st, "Clara", SFX_TEXT_PIP,
                "What a lovely brass bell you're carrying! Folks over in Sunhill Market love trading stories - Baker Tessa spots every stray creature that wanders into town.", 0, NULL);
        } else {
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "Quiet afternoon by the mills, lad! If you're new to Evertale, stay on the sunlit roads toward Sunhill Market before venturing deep into the eastern wilds.", 0, NULL);
        }
        return;
    }
    if (hit == 0) {
        if (!st->flag_elder_seen) {
            hearth_start_cutscene(st, CUTSCENE_ELDER_TALK);
        } else {
            static const HearthChoice elder_opts[3] = {
                { "Replay Tour",        ACT_REPLAY_INTRO },
                { "Fight Shrine Boss",  ACT_JUMP_BOSS },
                { "Keep Exploring",     ACT_CLOSE }
            };
            open_dialogue(st, "Elder Wren", SFX_TEXT_ELDER,
                "Fish at the south creek pier, or take the east road past the Shrine to Whiskerburgh Mouse Town!",
                3, elder_opts);
        }
    } else if (hit == 1) {
        if (!st->flag_has_courage) {
            static const HearthChoice clara_opts[2] = {
                { "Take Heart (Courage)", ACT_TAKE_COURAGE },
                { "Maybe Later",          ACT_CLOSE }
            };
            open_dialogue(st, "Clara", SFX_TEXT_PIP,
                "My heart brims with Courage! Want a spark in your Hearthglass Pendant for anyone who feels hollow?",
                2, clara_opts);
        } else {
            open_dialogue(st, "Clara", SFX_TEXT_PIP,
                "You already hold a bright spark of Courage—share it with Guard Bram or Captain Tusk!", 0, NULL);
        }
    } else if (hit == 2) {
        if (st->flag_guard_mended) {
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "Sturdy as oak! Follow the eastern road past the Stone Shrine to reach Whiskerburgh!", 0, NULL);
        } else if (st->flag_has_courage) {
            static const HearthChoice guard_opts[2] = {
                { "Give Heart (Courage)", ACT_GIVE_COURAGE },
                { "Hold On",              ACT_CLOSE }
            };
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "B-brr... the mist chills my bones. Wait—is that golden Courage glowing in your pendant?",
                2, guard_opts);
        } else {
            open_dialogue(st, "Guard Bram", SFX_TEXT_ELDER,
                "My heart feels hollow with dread. Ask Clara by the square for a spark of Courage!", 0, NULL);
        }
    }
}

static void interact_npc_chapter1(HearthState *st, int hit) {
    if (st->zone_id == ZONE_ROWAN_HOME) {
        if (hit == 0) {
            if (st->ch1_step == 0u) {
                hearth_start_cutscene(st, CUTSCENE_CH1_MORNING_MARA);
            } else if (st->ch1_step == 1u) {
                open_dialogue(st, "Grandma Mara", SFX_TEXT_ELDER,
                    "Step outside into Pine Square, dear! Pick up the honey-rye loaf from Baker Maple and bring Barnaby his hot ginger thermos up by the train.", 0, NULL);
            } else if (st->ch1_step == 2u) {
                st->ch1_step = 3u;
                open_dialogue(st, "Grandma Mara", SFX_TEXT_ELDER,
                    "Oh, thank you, Rowan! The afternoon breeze is turning crisp off the peaks. Could you fetch my red wool shawl from the cedar chest up in the attic loft?", 0, NULL);
            } else if (st->ch1_step == 3u) {
                open_dialogue(st, "Grandma Mara", SFX_TEXT_ELDER,
                    "Climb the little stairs on the right side of the room to check my heirloom cedar chest, Rowan.", 0, NULL);
            } else if (st->ch1_step == 4u) {
                hearth_start_cutscene(st, CUTSCENE_CH1_DUSK_TEA);
            } else if (st->ch1_step == 5u) {
                open_dialogue(st, "Grandma Mara", SFX_TEXT_ELDER,
                    "Go crawl under your patchwork quilt, Rowan. A good night's sleep will settle your heart.", 0, NULL);
            } else {
                open_dialogue(st, "Grandma Mara", SFX_TEXT_ELDER,
                    "Grandma is fast asleep by the embers... Rowan gently pulls on his autumn scarf and slips quietly toward the door.", 0, NULL);
            }
        } else {
            open_dialogue(st, "Mido", SFX_TEXT_PIP,
                "Yip! Mido curls his bushy amber tail beside the warm stone hearth, ears twitching in the cedar air.", 0, NULL);
        }
    } else if (st->zone_id == ZONE_ALDERBROOK) {
        if (st->time_of_day == TOD_NIGHT) {
            open_dialogue(st, "Mido", SFX_TEXT_PIP,
                "Yip! Yip! Mido paws excitedly toward the northeast rocky switchback leading into Whispering Woods!", 0, NULL);
        } else if (hit == 0) {
            if (st->ch1_step == 1u && (st->ch1_errands_mask & 2u) == 0u) {
                hearth_start_cutscene(st, CUTSCENE_CH1_BARNABY_THERMOS);
            } else {
                open_dialogue(st, "Barnaby", SFX_TEXT_ELDER,
                    "Old No. 7's brass boiler is purring sweet today! Nothing beats watching the autumn fog drift past those snow peaks.", 0, NULL);
            }
        } else if (hit == 1) {
            if (st->ch1_step == 1u && (st->ch1_errands_mask & 1u) == 0u) {
                hearth_start_cutscene(st, CUTSCENE_CH1_MAPLE_LOAF);
            } else {
                static const HearthChoice shop2_opts[2] = {
                    { "Open Maple's Bakery", ACT_OPEN_SHOP_2 },
                    { "Just Visiting",       ACT_CLOSE }
                };
                open_dialogue(st, "Baker Maple", SFX_TEXT_PIP,
                    "Fresh ginger loaves, fishing lures, and top Guilders for mountain brook trout! Want to browse my counter, Rowan?",
                    2, shop2_opts);
            }
        } else if (hit == 2) {
            st->ch1_errands_mask |= 4u;
            open_dialogue(st, "Jasper", SFX_TEXT_ELDER,
                "Hear that whitewater rushing over the granite, boy? Follow the northeast steps into Whispering Woods sometime - the old Hollow Oak still hums when the wind turns silver.", 0, NULL);
        } else {
            open_dialogue(st, "Mido", SFX_TEXT_PIP,
                "Yip! Mido pounces on a falling crimson maple leaf and glances eagerly toward the mountain brook.", 0, NULL);
        }
    } else if (st->zone_id == ZONE_WHISPERING_WOODS) {
        if (hit == 0) {
            open_dialogue(st, "Jasper", SFX_TEXT_ELDER,
                (st->time_of_day == TOD_NIGHT)
                    ? "Out under the harvest moon, Rowan? Light all three ancient stone lanterns along this trail and the mist over Hollow Glade will part for you."
                    : "Careful on the wet granite by the rapids, lad. Three stone lanterns mark the old lumber trail up to Hollow Glade.", 0, NULL);
        } else {
            open_dialogue(st, "Mido", SFX_TEXT_PIP,
                "Yip! Mido bounds across the timber bridge, glancing back to make sure Rowan is following!", 0, NULL);
        }
    } else if (st->zone_id == ZONE_HOLLOW_GLADE) {
        open_dialogue(st, "Mido", SFX_TEXT_PIP,
            "Mido sits beneath the monumental roots of the Hollow Oak, his brass bell chiming softly toward the glowing archway!", 0, NULL);
    }
}

static void interact_npc_chapter2(HearthState *st, int hit) {
    if (st->zone_id == ZONE_SUNHILL_MARKET) {
        if (hit == 0) {
            if (st->ch2_step == 0u) {
                hearth_start_cutscene(st, CUTSCENE_CH2_SUNHILL_TESSA);
            } else {
                static const HearthChoice shop3_opts[2] = {
                    { "Open Sunhill Bakery", ACT_OPEN_SHOP_3 },
                    { "Just Visiting",       ACT_CLOSE }
                };
                open_dialogue(st, "Tessa the Baker", SFX_TEXT_PIP,
                    "Warm honey scones and travel tonics! Tell Archivist Corin up by the fountain I sent you - and keep an eye on that clever fox-kit!",
                    2, shop3_opts);
            }
        } else if (hit == 1) {
            if (st->ch2_step < 2u) {
                hearth_start_cutscene(st, CUTSCENE_CH2_SUNHILL_CORIN);
            } else {
                open_dialogue(st, "Archivist Corin", SFX_TEXT_ELDER,
                    "Two travelers from beyond the mist passed through Sunhill long ago... If anyone can uncover their trail - and the legend of the Hearthglass Pendant - it will be a steady heart like yours.", 0, NULL);
            }
        } else if (hit == 2) {
            if (st->ch2_step < 3u) {
                hearth_start_cutscene(st, CUTSCENE_CH2_SUNHILL_REUNION);
            } else {
                open_dialogue(st, "Mido", SFX_TEXT_PIP,
                    "Yip! Mido happily trots in circles around the sunlit fountain, his little brass bell chiming bright as morning!", 0, NULL);
            }
        } else {
            open_dialogue(st, "Market Florist", SFX_TEXT_PIP,
                "Look up at Castle Aurelia gleaming above the terraces! Folks from every corner of Evertale gather here in Sunhill Market.", 0, NULL);
        }
    } else if (st->zone_id == ZONE_CASTLE_GATE) {
        if (hit == 0) {
            open_dialogue(st, "Royal Sentinel", SFX_TEXT_ELDER,
                "Halt - er, welcome, little lad! Castle Aurelia's inner court requires a Royal Summons, so we can't lower the portcullis today.", 0, NULL);
        } else {
            open_dialogue(st, "Royal Sentinel", SFX_TEXT_ELDER,
                "Looking for a lost fox-kit? Try Sunhill Market just down the southern steps! Archivist Corin keeps records of every traveler who walks Evertale - even folks from beyond the mist.", 0, NULL);
        }
    } else if (st->zone_id == ZONE_BRAMBLE_EDGE) {
        if (!st->flag_has_familiar) {
            open_dialogue(st, "Lantern Keeper", SFX_TEXT_ELDER,
                (st->ch2_step >= 3u)
                    ? "Pip sent you to the Boundary Stone? Step east into the mossy rune circle and reach out with a steady heart - your first Familiar will answer!"
                    : "Whoa there, youngster! Wild sprites prowl past these pines - don't wander deeper without a Familiar. I did see an amber fox-kit dart west toward Sunhill Market, though!", 0, NULL);
        } else {
            open_dialogue(st, "Lantern Keeper", SFX_TEXT_ELDER,
                "Bramblekit and Lumina took right to you! Stay sharp out on the Evertale World Map - roaming sprites are stirring across the roads now.", 0, NULL);
        }
    } else if (st->zone_id == ZONE_SUNKEN_RUINS) {
        open_dialogue(st, "Antiquarian", SFX_TEXT_ELDER,
            "Hmm... curious mountain-oak brass you've got on your coat, boy! I've only seen metalwork like that in sketches held by Archivist Corin up in Sunhill Market. Ask him!", 0, NULL);
    } else if (st->zone_id == ZONE_TIMBER_PIER) {
        if (hit == 0) {
            if ((st->vehicle_unlocked_mask & VEH_MASK_RAFT) != 0u) {
                open_dialogue(st, "Shipwright Orin", SFX_TEXT_ELDER,
                    "That Cedar Raft rides like a dream, Rowan! Step right off the southern beaches into the turquoise shallows on the Globe to reach Suncrest Paradise or Sands of Babana!", 0, NULL);
            } else if ((st->planks_found_mask & 7u) == 7u) {
                hearth_start_cutscene(st, CUTSCENE_CH2_ORIN_RAFT_BUILT);
            } else if (st->ch2_step < 5u) {
                hearth_start_cutscene(st, CUTSCENE_CH2_ORIN_START);
            } else {
                open_dialogue(st, "Shipwright Orin", SFX_TEXT_ELDER,
                    "Search the sandy beaches west and east of my pier for all 3 Sun-Cedar Driftwood Planks, Rowan! Bring them back and I'll lash your Cedar Raft together!", 0, NULL);
            }
        } else {
            open_dialogue(st, "Deckhand Kip", SFX_TEXT_PIP,
                "Press M (or SELECT) anytime on the Evertale Globe to open your World Atlas! You can spot the bright turquoise raft channels right from the map!", 0, NULL);
        }
    } else if (st->zone_id == ZONE_CORAL_COVE) {
        if (hit == 0) {
            if (st->ch2_step < 7u) {
                hearth_start_cutscene(st, CUTSCENE_CH2_CORAL_BARNACLE);
            } else {
                open_dialogue(st, "Capt. Barnacle", SFX_TEXT_ELDER,
                    "Har! Try casting a line off the east coral pier for tropical Coral Snappers, or ride the reef channel east straight to the Sands of Babana!", 0, NULL);
            }
        } else if (hit == 1) {
            open_dialogue(st, "Diver Maya", SFX_TEXT_PIP,
                "The coral reef glows emerald and pink at midday! Beyond the deep cobalt sea lie Frostmere North, Canopy Wilds, Cinderpeak Volcano, and even Aetheris Citadel!", 0, NULL);
        } else {
            open_dialogue(st, "Reef Monkey", SFX_TEXT_PIP,
                "Ooh-ooh! The cheeky island monkey juggles two shiny seashells and points eagerly toward the golden desert across the eastern reef!", 0, NULL);
        }
    } else if (st->zone_id == ZONE_BABANA_OASIS) {
        if (hit == 0) {
            static const HearthChoice shop4_opts[2] = {
                { "Browse Golden Babanas", ACT_OPEN_SHOP_4 },
                { "Just Visiting",         ACT_CLOSE }
            };
            open_dialogue(st, "Merchant Zari", SFX_TEXT_PIP,
                "Sweetest Golden Babanas in all of Evertale! Restores 70 HP and 15 MP in battle - and I pay top Guilders for tropical Coral Snappers!",
                2, shop4_opts);
        } else if (hit == 1) {
            hearth_start_cutscene(st, CUTSCENE_CH2_BABANA_CLUE);
        } else {
            open_dialogue(st, "Oasis Camel", SFX_TEXT_ELDER,
                "Mrrph! The desert camel chews contentedly on a Golden Babana leaf, completely unfazed by the scorpions out in the dunes.", 0, NULL);
        }
    }
}

static void interact_npc_other_zones(HearthState *st, int hit) {
    if (st->zone_id == ZONE_ROWAN_HOME || st->zone_id == ZONE_ALDERBROOK ||
        st->zone_id == ZONE_WHISPERING_WOODS || st->zone_id == ZONE_HOLLOW_GLADE) {
        interact_npc_chapter1(st, hit);
    } else if (st->zone_id == ZONE_SUNHILL_MARKET || st->zone_id == ZONE_CASTLE_GATE ||
               st->zone_id == ZONE_BRAMBLE_EDGE || st->zone_id == ZONE_SUNKEN_RUINS ||
               st->zone_id == ZONE_TIMBER_PIER || st->zone_id == ZONE_CORAL_COVE ||
               st->zone_id == ZONE_BABANA_OASIS) {
        interact_npc_chapter2(st, hit);
    } else if (st->zone_id == ZONE_WHISKERBURGH) {
        if (hit == 0) {
            if (!st->flag_sewer_key) {
                hearth_start_cutscene(st, CUTSCENE_MAYOR_SEWER_KEY);
            } else {
                open_dialogue(st, "Mayor Brie", SFX_TEXT_PIP,
                    "Take my Lantern Charm north into the Sunken Sewers! Beyond King Muckmaw lies Boarhold Citadel.", 0, NULL);
            }
        } else if (hit == 1) {
            static const HearthChoice shop0_opts[2] = {
                { "Open Market Shop", ACT_OPEN_SHOP_0 },
                { "Just Browsing",    ACT_CLOSE }
            };
            open_dialogue(st, "Barnaby", SFX_TEXT_PIP,
                "Squeak! Finest tonics, fishing lures, and top Guilders paid for fresh fish! Care to trade?",
                2, shop0_opts);
        } else {
            open_dialogue(st, "Angler Pippo", SFX_TEXT_PIP,
                "Cast from the south canal jetty! Keep the fish inside your reel bar and tap A to reel in Coral Snappers!", 0, NULL);
        }
    } else if (st->zone_id == ZONE_BOARHOLD) {
        if (hit == 0) {
            static const HearthChoice shop1_opts[2] = {
                { "Open Citadel Shop", ACT_OPEN_SHOP_1 },
                { "Not Right Now",     ACT_CLOSE }
            };
            open_dialogue(st, "Merchant Porkins", SFX_TEXT_ELDER,
                "Snort! Stock up on Sun Tonics and Courage Snacks before crossing the moat to face Lord Ironfang!",
                2, shop1_opts);
        } else if (hit == 1) {
            if (!st->flag_has_heart_vessel) {
                open_dialogue(st, "Captain Tusk", SFX_TEXT_ELDER,
                    "Snort... my spirit feels heavy since Lord Ironfang turned cruel. Only an ancient heart-mending pendant could lift a gloom this deep.", 0, NULL);
            } else if (st->flag_captain_mended) {
                open_dialogue(st, "Captain Tusk", SFX_TEXT_ELDER,
                    "Cross the eastern bridge into the Twilight Sanctum and shatter Lord Ironfang's Spell Locks!", 0, NULL);
            } else if (st->flag_has_courage) {
                static const HearthChoice cap_opts[2] = {
                    { "Mend Captain Tusk", ACT_MEND_CAPTAIN },
                    { "Not Yet",           ACT_CLOSE }
                };
                open_dialogue(st, "Captain Tusk", SFX_TEXT_ELDER,
                    "Snort... my courage drained away when Lord Ironfang turned cruel. Wait - that pendant glows!",
                    2, cap_opts);
            } else {
                open_dialogue(st, "Captain Tusk", SFX_TEXT_ELDER,
                    "My heart feels hollow... Clara back in Windmill Hamlet carries spare Courage, or you can brave Lord Ironfang directly!", 0, NULL);
            }
        } else {
            open_dialogue(st, "Squire Snout", SFX_TEXT_ELDER,
                "Lord Ironfang locked the eastern sanctum! Stay sharp across the ramparts, traveler.", 0, NULL);
        }
    }
}

static void interact_ch1_prop(HearthState *st, int hit, int16_t ox, int16_t oy) {
    if (hit == 14) {
        if (st->ch1_step == 3u) {
            hearth_start_story_cg(st, 1u);
        } else if (st->has_milo_bell) {
            open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                "Grandma's heirloom cedar chest... I already tucked Mido's brass bell and the golden oak leaf safely in my pocket.", 0, NULL);
        } else {
            open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                "Grandma's carved cedar chest smells like mountain pine and lavender. I should ask her before opening it.", 0, NULL);
        }
    } else if (hit == 15) {
        if (st->ch1_step == 5u) {
            static const HearthChoice sleep_opts[2] = {
                { "Sleep Until Night", ACT_REST_BED },
                { "Stay Awake",        ACT_CLOSE }
            };
            open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                "Mido's brass bell feels warm in my hand. Crawl beneath the patchwork quilt and close my eyes?",
                2, sleep_opts);
        } else {
            static const HearthChoice rest_opts[2] = {
                { "Rest (Restore HP/MP)", ACT_REST_BED },
                { "Not Tired Yet",        ACT_CLOSE }
            };
            open_dialogue(st, "Rowan's Bed", SFX_TEXT_HERO,
                "A cozy patchwork quilt stitched by Grandma Mara. Take a quick breather?",
                2, rest_opts);
        }
    } else if (hit >= 16 && hit <= 18) {
        uint8_t bit = (uint8_t)(1u << (hit - 16));
        if ((st->woods_shrines_mask & bit) == 0u) {
            st->woods_shrines_mask |= bit;
            push_sfx(st, SFX_HEART_GIVE);
            spawn_burst(st, 3, ox, (int16_t)(oy + 10), 12, 3, 11, false);
            spawn_ftext(st, ox, oy, "LANTERN LIT!", 3, 11, true);
            if ((st->woods_shrines_mask & 7u) == 7u) {
                open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                    "All three woodland lanterns are glowing golden! Look - the mountain mist over the northern pass to Hollow Glade just lifted!", 0, NULL);
            } else {
                open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                    "The mossy stone lantern sparks to life with amber warmth, lighting the granite trail deeper into Whispering Woods!", 0, NULL);
            }
        } else {
            open_dialogue(st, "Stone Lantern", SFX_TEXT_ELDER,
                "A steady golden flame flickers inside the carved granite lantern, pushing back the autumn chill.", 0, NULL);
        }
    } else if (hit == 19) {
        if (st->zone_id == ZONE_BRAMBLE_EDGE) {
            if (st->flag_has_familiar) {
                hearth_heal_party(st);
                open_dialogue(st, "Familiar Shrine", SFX_TEXT_PIP,
                    "The mossy boundary stones hum softly, restoring Rowan, Bramblekit, and Lumina to full strength!", 0, NULL);
            } else if (st->ch2_step >= 3u) {
                hearth_start_cutscene(st, CUTSCENE_CH2_BOND_FAMILIAR);
            } else {
                open_dialogue(st, "Boundary Stone", SFX_TEXT_HERO,
                    "Ancient runes glow faintly across the mossy circle. Maybe someone in Sunhill Market knows how to awaken a Familiar here.", 0, NULL);
            }
            return;
        }
        if (st->story_stage >= STORY_EVERTALE_AWAKENED) {
            request_zone_warp(st, ZONE_EVERTALE_WORLD, 74, 110);
        } else if (st->ch1_step >= 6u) {
            hearth_start_story_cg(st, 3u);
        } else {
            open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                "An enormous, ancient oak standing alone beneath the alpine peaks... Why does staring into its hollow trunk make my heart race?", 0, NULL);
        }
    } else if (hit >= 30 && hit <= 32) {
        uint8_t bit = (uint8_t)(1u << (hit - 30));
        uint8_t cnt = 0u;
        st->planks_found_mask |= bit;
        if (st->ch2_step < 5u) st->ch2_step = 5u;
        if (st->planks_found_mask & 1u) cnt++;
        if (st->planks_found_mask & 2u) cnt++;
        if (st->planks_found_mask & 4u) cnt++;
        push_sfx(st, SFX_CHEST_OPEN);
        spawn_burst(st, 3, ox, (int16_t)(oy + 8), 10, 3, 11, false);
        if (cnt == 3u) {
            spawn_ftext(st, ox, oy, "3/3 PLANKS!", 3, 11, true);
            open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                "That's all 3 Sun-Cedar Driftwood Planks! Let's hurry back to Shipwright Orin on the pier so he can build our Cedar Raft!", 0, NULL);
        } else if (cnt == 2u) {
            spawn_ftext(st, ox, oy, "2/3 PLANKS", 3, 11, true);
            open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                "Picked up a sturdy Sun-Cedar Plank! Just one more plank left along the sandy shore.", 0, NULL);
        } else {
            spawn_ftext(st, ox, oy, "1/3 PLANKS", 3, 11, true);
            open_dialogue(st, "Rowan", SFX_TEXT_HERO,
                "Found a dry Sun-Cedar Driftwood Plank! Two more along the beach and Orin can lash our Cedar Raft together.", 0, NULL);
        }
    }
}

static void interact_globe_landmark(HearthState *st, int lidx) {
    const HearthGlobeLandmark *lm;
    if (lidx < 0 || lidx >= HEARTH_GLOBE_LANDMARK_COUNT) return;
    lm = &HEARTH_GLOBE_LANDMARKS[lidx];
    if (lm->enterable && lm->dst_zone < ZONE_COUNT) {
        request_zone_warp(st, lm->dst_zone, lm->dst_tx, lm->dst_ty);
        return;
    }
    push_sfx(st, SFX_CONFIRM);
    if (lm->region_id == REGION_FROSTMERE) {
        open_dialogue(st, "Frostmere Noel Village", SFX_TEXT_PIP,
            "Sleigh bells echo beneath glowing evergreens while cheerful penguins waddle across the ice! Frostmere's festive gates will open in a future chapter.", 0, NULL);
    } else if (lm->region_id == REGION_CANOPY) {
        open_dialogue(st, "Canopy Treetop Village", SFX_TEXT_PIP,
            "Timber walkways and rope bridges weave high through giant rainforest boughs while monkeys chatter above! The treetop lift opens in a future chapter.", 0, NULL);
    } else if (lm->region_id == REGION_VOLCANO) {
        open_dialogue(st, "Cinderpeak Caldera Gate", SFX_TEXT_ELDER,
            "Molten lava rivers glow beside jagged basalt cliffs! You'll need volcanic wards from a future chapter to venture inside Cinderpeak Caldera.", 0, NULL);
    } else {
        open_dialogue(st, "Aetheris Citadel Spire", SFX_TEXT_ELDER,
            "Gleaming chrome arches and hovering light-drones hum with star-magic! Aetheris Citadel's high-tech spires await the endgame chapter.", 0, NULL);
    }
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
        interact_chest(st, ox, oy);
    } else if (hit == 12) {
        interact_signpost(st);
    } else if (hit == 13) {
        static const HearthChoice fish_opts[2] = {
            { "Cast Fishing Line", ACT_START_FISHING },
            { "Step Away",         ACT_CLOSE }
        };
        open_dialogue(st, "Fishing Pier", SFX_TEXT_PIP,
            "Ripples dance on the water! Cast your line and keep the fish inside the reel bar?",
            2, fish_opts);
    } else if ((hit >= 14 && hit <= 19) || (hit >= 30 && hit <= 32)) {
        interact_ch1_prop(st, hit, ox, oy);
    } else if (hit >= 20 && hit < 20 + HEARTH_MAX_PORTALS) {
        interact_portal(st, hit - 20);
    } else if (hit >= 40 && hit < 40 + HEARTH_GLOBE_LANDMARK_COUNT) {
        interact_globe_landmark(st, hit - 40);
    } else if (st->zone_id == ZONE_HAMLET) {
        interact_npc_hamlet(st, hit);
    } else {
        interact_npc_other_zones(st, hit);
    }
}

static void check_explore_triggers(HearthState *st) {
    int16_t hx = FP_PX(HS.hero_fx);
    int16_t hy = FP_PX(HS.hero_fy);
    int i;

    /* Age-5 Flashback trigger: walking east toward Mido sparks Shadow Grandmother cutscene! */
    if (st->zone_id == ZONE_HAMLET && st->story_stage == STORY_AGE5_FLASHBACK && !st->flashback_triggered) {
        if (hx >= 14 * 16) {
            st->flashback_triggered = true;
            hearth_start_cutscene(st, CUTSCENE_SHADOW_GRANDMA);
            return;
        }
    }

    /* Globe Landmark walk-in warps (only when on foot/raft/ship and not in spawn grace frames!) */
    if (HS.is_world_map) {
        if (HS.active_vehicle < VEH_BALLOON && HS.hero_safe_frames == 0) {
            for (i = 0; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
                const HearthGlobeLandmark *lm = &HEARTH_GLOBE_LANDMARKS[i];
                int16_t px, py, ddx;
                if (!lm->enterable || lm->dst_zone >= ZONE_COUNT) continue;
                px = (int16_t)(((int16_t)lm->tx << 4) + 8);
                ddx = wrap_delta_x((int16_t)(hx - px));
                if (ddx <= -12 || ddx >= 12) continue;
                py = (int16_t)(((int16_t)lm->ty << 4) + 8);
                if (near_world_point_px(st, hx, hy, px, py, 12)) {
                    request_zone_warp(st, lm->dst_zone, lm->dst_tx, lm->dst_ty);
                    return;
                }
            }
        }
        return;
    }

    /* Automatic walk-in portal transitions (edges & open archways) */
    for (i = 0; i < HEARTH_MAX_PORTALS; i++) {
        const HearthPortal *pt = &st->portals[i];
        int16_t px, py;
        if (!pt->active) continue;
        px = (int16_t)((int16_t)pt->tx * 16 + 8);
        py = (int16_t)((int16_t)pt->ty * 16 + 8);
        if (near_point_px(hx, hy, px, py, 13)) {
            if ((st->zone_id == ZONE_WHISPERING_WOODS && pt->dst_zone == ZONE_HOLLOW_GLADE &&
                 st->time_of_day == TOD_NIGHT && (st->woods_shrines_mask & 7u) != 7u) ||
                (pt->req_stage > 0u && st->story_stage < pt->req_stage)) {
                st->hero_fy += TO_FP(10);
                interact_portal(st, i);
                return;
            }
            request_zone_warp(st, pt->dst_zone, pt->dst_tx, pt->dst_ty);
            return;
        }
    }
}

static bool step_zone_warp(HearthState *st) {
    if (!st->warp_pending) return false;
    if (st->warp_fade < 15u) {
        st->warp_fade += 2u;
        if (st->warp_fade >= 15u) {
            uint8_t prev_zone = st->zone_id;
            st->warp_fade = 15u;
            hearth_enter_zone(st, st->warp_dst_zone, st->warp_dst_tx, st->warp_dst_ty);
            if ((prev_zone == ZONE_ALDERBROOK || prev_zone == ZONE_HOLLOW_GLADE) &&
                st->zone_id == ZONE_HAMLET && !st->flag_has_wand) {
                st->warp_pending = false;
                st->warp_fade = 0;
                hearth_start_cutscene(st, CUTSCENE_PIP_REUNION);
                return true;
            }
            st->warp_pending = false;
        }
    }
    return true;
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
    int16_t w, mx, my, tx, ty;
    int16_t mw, mh;
    fp16_t gx, gy, dx, dy;

    if (HS.cam_override) {
        step_camera_override(st);
        return;
    }
    w = (HS.tuning.ppu_mode == PPU_SNES_256x224) ? (int16_t)256 : (int16_t)320;
    if (HS.is_world_map) {
        gx = wrap_globe_fp_x(HS.hero_fx - TO_FP(w >> 1));
        gy = wrap_globe_fp_y(HS.hero_fy - TO_FP(112));
        dx = gx - HS.cam_fx;
        if (dx > (GLOBE_FP_W >> 1)) dx -= GLOBE_FP_W;
        else if (dx < -(GLOBE_FP_W >> 1)) dx += GLOBE_FP_W;
        dy = gy - HS.cam_fy;
        if (dy > (GLOBE_FP_H >> 1)) dy -= GLOBE_FP_H;
        else if (dy < -(GLOBE_FP_H >> 1)) dy += GLOBE_FP_H;
        if (dx != 0) {
            if (dx >= -16384 && dx <= 16383) HS.cam_fx = wrap_globe_fp_x(HS.cam_fx + (fp16_t)((int16_t)dx >> 3));
            else HS.cam_fx = wrap_globe_fp_x(HS.cam_fx + (dx >> 3));
        }
        if (dy != 0) {
            if (dy >= -16384 && dy <= 16383) HS.cam_fy = wrap_globe_fp_y(HS.cam_fy + (fp16_t)((int16_t)dy >> 3));
            else HS.cam_fy = wrap_globe_fp_y(HS.cam_fy + (dy >> 3));
        }
        return;
    }
    mw = (int16_t)(HS.map_w ? HS.map_w : (uint8_t)HEARTH_MAP_W);
    mh = (int16_t)(HS.map_h ? HS.map_h : (uint8_t)HEARTH_MAP_H);
    mx = (int16_t)(mw * 16 - w);
    my = (int16_t)(mh * 16 - 224);
    tx = (int16_t)(FP_PX(HS.hero_fx) - (w >> 1));
    if (tx < 0) tx = 0; else if (tx > mx) tx = mx;
    ty = (int16_t)(FP_PX(HS.hero_fy) - 112);
    if (ty < 0) ty = 0; else if (ty > my) ty = my;

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
    (void)p; (void)sx_fp; (void)sy_fp; (void)aura; (void)rot_deg;
    idx = (uint16_t)g_hearth.ppu.oam_count;
    if (idx >= HEARTH_MAX_OAM) return;
    g_hearth.ppu.oam_count = (uint8_t)(idx + 1u);
    g_hearth.ppu.oam[idx].x = x;
    g_hearth.ppu.oam[idx].y = y;
    g_hearth.ppu.oam[idx].sort_y = sort_y;
    g_hearth.ppu.oam[idx].sprite_id = sprite_id;
    g_hearth.ppu.oam[idx].flags = (uint8_t)(
        flags |
        (((uint8_t)(dir & 3u)) << OAM_FLAG_DIR_SHIFT) |
        ((frame & 1u) ? OAM_FLAG_STEP_BIT : 0u) |
        ((frame != 0u) ? OAM_FLAG_WALK_BIT : 0u) |
        ((layer >= 2u) ? OAM_FLAG_LAYER2 : 0u)
    );
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
    (void)p;
    idx = (uint16_t)g_hearth.ppu.oam_count;
    if (idx >= HEARTH_MAX_OAM) return;
    g_hearth.ppu.oam_count = (uint8_t)(idx + 1u);
    g_hearth.ppu.oam[idx].x = x;
    g_hearth.ppu.oam[idx].y = y;
    g_hearth.ppu.oam[idx].sort_y = sort_y;
    g_hearth.ppu.oam[idx].sprite_id = sprite_id;
    g_hearth.ppu.oam[idx].flags = (uint8_t)(
        flags |
        (((uint8_t)(dir & 3u)) << OAM_FLAG_DIR_SHIFT) |
        ((frame & 1u) ? OAM_FLAG_STEP_BIT : 0u) |
        ((frame != 0u) ? OAM_FLAG_WALK_BIT : 0u) |
        ((layer >= 2u) ? OAM_FLAG_LAYER2 : 0u)
    );
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
        g_hearth.ppu.oam[(idx)].flags = (uint8_t)((vfl) | \
            (((uint8_t)((vdir) & 3u)) << OAM_FLAG_DIR_SHIFT) | \
            (((vfr) & 1u) ? OAM_FLAG_STEP_BIT : 0u) | \
            (((vfr) != 0u) ? OAM_FLAG_WALK_BIT : 0u) | \
            (((vlay) >= 2u) ? OAM_FLAG_LAYER2 : 0u)); \
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

static uint16_t build_explore_ppu_enemies(
    const HearthState *st, HearthPpuPacket *p, uint16_t cnt, int16_t cx, int16_t cy, int16_t max_sx
) {
    int16_t i, sx, sy, ey;
    const HearthFieldEnemy *e = HS.field_enemies;
    uint8_t fl;
    ST_UNUSED(st);
#if defined(HEARTH_TARGET_SNES)
    (void)p;
#endif
    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES && cnt < HEARTH_MAX_OAM; i++, e++) {
        if (!e->alive) continue;
        sx = world_dx(st, FP_PX(e->fx), cx);
        if (sx < -24 || sx > max_sx) continue;
        ey = FP_PX(e->fy);
        sy = world_dy(st, ey, cy);
        if (sy < -24 || sy > 248) continue;
        fl = (uint8_t)(OAM_FLAG_SHADOW | ((e->dir == DIR_RIGHT) ? OAM_FLAG_FLIP_X : 0));
        OAM_SET_IDX(p, cnt, sx, sy, e->sprite_id,
            (uint8_t)((e->anim_frame >> 4) & 1u), e->dir, fl,
            FP_ONE, FP_ONE, AURA_NONE, 1, sy, 0);
        cnt++;
    }
    return cnt;
}

static uint16_t build_explore_ppu_props_and_actors(
    HearthState *st, HearthPpuPacket *p, int16_t cx, int16_t cy, int16_t max_sx
) {
    static const uint8_t p_tx[3] = {  8u, 15u, 34u };
    static const uint8_t p_ty[3] = { 15u, 16u, 15u };
    int16_t i, sx, sy, ny;
    uint16_t fc16 = (uint16_t)HS.frame_count;
    uint16_t cnt = 0u;
    uint8_t npc_f = (uint8_t)((fc16 >> 5) & 1u);
    uint8_t c_spr;
    ST_UNUSED(st);
#if defined(HEARTH_TARGET_SNES)
    (void)p;
#endif

    /* 1. Treasure Chest (emits dedicated SPR_CHEST_OPEN when opened!) */
    if (HS.chest.active) {
        ny = (int16_t)((int16_t)HS.chest.ty * 16 + 12);
        sx = (int16_t)((int16_t)HS.chest.tx * 16 + 8 - cx);
        sy = (int16_t)(ny - cy);
        if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248 && cnt < HEARTH_MAX_OAM) {
            c_spr = ((HS.chests_opened_mask & (uint16_t)(1u << HS.chest.chest_bit)) != 0u)
                ? (uint8_t)SPR_CHEST_OPEN : (uint8_t)SPR_CHEST;
            OAM_SET_IDX(p, cnt, sx, sy, c_spr, (c_spr == (uint8_t)SPR_CHEST_OPEN) ? 1u : 0u,
                DIR_DOWN, OAM_FLAG_SHADOW, FP_ONE, FP_ONE, AURA_NONE, 1, sy, 0);
            cnt++;
        }
    }

    /* 1b. Driftwood Cedar Planks along Timber Pier beach (uncollected) */
    if (HS.zone_id == ZONE_TIMBER_PIER && (HS.vehicle_unlocked_mask & VEH_MASK_RAFT) == 0u) {
        for (i = 0; i < 3 && cnt < HEARTH_MAX_OAM; i++) {
            if ((HS.planks_found_mask & (uint8_t)(1u << i)) != 0u) continue;
            ny = (int16_t)((int16_t)p_ty[i] * 16 + 10);
            sx = (int16_t)((int16_t)p_tx[i] * 16 + 8 - cx);
            sy = (int16_t)(ny - cy);
            if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248) {
                OAM_SET_IDX(p, cnt, sx, sy, SPR_RAFT, npc_f, DIR_DOWN, OAM_FLAG_SHADOW,
                    FP_ONE, FP_ONE, AURA_NONE, 1, sy, 0);
                cnt++;
            }
        }
    }

    /* 1c. Golden Babana Stall prop in Oasis of Babana */
    if (HS.zone_id == ZONE_BABANA_OASIS && cnt < HEARTH_MAX_OAM) {
        ny = (int16_t)(11 * 16 + 12);
        sx = (int16_t)(11 * 16 + 8 - cx);
        sy = (int16_t)(ny - cy);
        if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248) {
            OAM_SET_IDX(p, cnt, sx, sy, SPR_BABANA_STALL, npc_f, DIR_DOWN, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, AURA_NONE, 1, sy, 0);
            cnt++;
        }
    }

    /* 2. Hollow Oak Gateway Arch Sprite in Hollow Glade, Windmill Hamlet, & Evertale Globe */
    if (HS.zone_id == ZONE_HOLLOW_GLADE || HS.zone_id == ZONE_HAMLET || HS.zone_id == ZONE_EVERTALE_WORLD) {
        if (HS.zone_id == ZONE_HOLLOW_GLADE) {
            ny = (int16_t)(12 * 16 + 4);
            sx = (int16_t)(21 * 16 + 8 - cx);
            sy = (int16_t)(ny - cy);
        } else if (HS.zone_id == ZONE_EVERTALE_WORLD) {
            ny = (int16_t)(108 * 16 + 10);
            sx = world_dx(st, (int16_t)(74 * 16 + 8), cx);
            sy = world_dy(st, ny, cy);
        } else {
            ny = (int16_t)(13 * 16 + 8);
            sx = (int16_t)(2 * 16 + 8 - cx);
            sy = (int16_t)(ny - cy);
        }
        if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248 && cnt < HEARTH_MAX_OAM) {
            OAM_SET_IDX(p, cnt, sx, sy, SPR_HOLLOW_OAK, npc_f, DIR_DOWN, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, AURA_NONE, 1, (int16_t)(sy - 4), 0);
            cnt++;
        }
    }

    /* 2b. Idling Steam Locomotive on Alderbrook Mountain Rail Shelf */
    if (HS.has_locomotive) {
        ny = (int16_t)((int16_t)HS.loco_ty * 16 + 10);
        sx = (int16_t)((int16_t)HS.loco_tx * 16 + 8 - cx);
        sy = (int16_t)(ny - cy);
        if (sx >= -32 && sx <= (int16_t)(max_sx + 16) && sy >= -24 && sy <= 248 && cnt < HEARTH_MAX_OAM) {
            OAM_SET_IDX(p, cnt, sx, sy, SPR_LOCOMOTIVE, npc_f, DIR_RIGHT, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, AURA_NONE, 1, sy, 0);
            cnt++;
        }
    }

    /* 2c. Proximity-Streamed Biome Fauna across all 7 Continents on the Toroidal Globe */
    if (HS.is_world_map) {
        for (i = 0; i < HEARTH_GLOBE_FAUNA_COUNT && cnt < (uint16_t)(HEARTH_MAX_OAM - 12); i++) {
            const HearthGlobeFauna *fa = &HEARTH_GLOBE_FAUNA[i];
            int16_t sv, bob;
            sx = wrap_delta_x((int16_t)((((int16_t)fa->tx << 4) + 8) - cx));
            if (sx < -24 || sx > (int16_t)(max_sx + 4)) continue;
            sy = wrap_delta_y((int16_t)((((int16_t)fa->ty << 4) + 8) - cy));
            if (sy < -20 || sy > 244) continue;
            sv = (int16_t)HEARTH_SIN64[((fc16 >> 2) + fa->phase) & 63u];
            bob = (int16_t)((sv + (sv << 1)) >> 7);
            sx = (int16_t)(sx + bob);
            OAM_SET_IDX(p, cnt, sx, sy, fa->sprite_id,
                (uint8_t)(((fc16 + fa->phase) >> 4) & 1u),
                (bob < 0) ? DIR_LEFT : DIR_RIGHT,
                (uint8_t)(OAM_FLAG_SHADOW | ((bob < 0) ? OAM_FLAG_FLIP_X : 0u)),
                FP_ONE, FP_ONE, AURA_NONE, 1, sy, 0);
            cnt++;
        }
    }

    /* 3. Active Zone NPCs (suppress heart-aura sparkles until Hearthglass Pendant!) */
    for (i = 0; i < HEARTH_MAX_NPCS && cnt < HEARTH_MAX_OAM; i++) {
        if (!HS.npcs[i].active) continue;
        sx = (int16_t)(FP_PX(HS.npcs[i].fx) - cx);
        if (sx < -24 || sx > max_sx) continue;
        ny = FP_PX(HS.npcs[i].fy);
        sy = (int16_t)(ny - cy);
        if (sy < -24 || sy > 248) continue;
        OAM_SET_IDX(p, cnt, sx, sy, HS.npcs[i].sprite_id, npc_f, HS.npcs[i].dir, OAM_FLAG_SHADOW,
            FP_ONE, FP_ONE, HS.flag_has_heart_vessel ? HS.npcs[i].aura : AURA_NONE, 1, sy, 0);
        cnt++;
    }

    /* 4. Waystone */
    if (HS.has_waystone) {
        ny = (int16_t)((int16_t)HS.waystone_ty * 16 + 12);
        sx = (int16_t)((int16_t)HS.waystone_tx * 16 + 8 - cx);
        sy = (int16_t)(ny - cy);
        if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248 && cnt < HEARTH_MAX_OAM) {
            OAM_SET_IDX(p, cnt, sx, sy, SPR_WAYSTONE, (uint8_t)((fc16 >> 4) & 3u), DIR_DOWN, OAM_FLAG_SHADOW,
                FP_ONE, FP_ONE, AURA_NONE, 1, sy, 0);
            cnt++;
        }
    }

    /* 5. Fishing Bobber when active */
    if (HS.mode == MODE_FISHING && HS.fishing.active && cnt < HEARTH_MAX_OAM) {
        ny = (HS.fishing.phase == FISH_PHASE_BITE_ALERT) ? 3
            : (int16_t)((HEARTH_SIN64[(fc16 >> 2) & 63u] * 2) >> 7);
        sx = (int16_t)(HS.fishing.bobber_x - cx);
        sy = (int16_t)(HS.fishing.bobber_y + ny - cy);
        OAM_SET_IDX(p, cnt, sx, sy, SPR_BOBBER, (uint8_t)((fc16 >> 3) & 1u), DIR_DOWN, 0,
            FP_ONE, FP_ONE, AURA_NONE, 1, sy, 0);
        cnt++;
    }

    /* 6. Roaming Field Enemies */
    return build_explore_ppu_enemies(st, p, cnt, cx, cy, max_sx);
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
    int16_t hsx, int16_t hsy, int16_t psx, int16_t psy, int16_t max_sx
) {
    int16_t i, sx, sy;
    const HearthFieldEnemy *e = HS.field_enemies;
    ST_UNUSED(st);
#if defined(HEARTH_TARGET_SNES)
    (void)p;
#endif

    if (HS.has_windmill) {
        if (HS.zone_id == ZONE_EVERTALE_WORLD) {
            sx = world_dx(st, (int16_t)(78 * 16 + 8), cx);
            sy = world_dy(st, (int16_t)(84 * 16 + 4), cy);
        } else {
            sx = (int16_t)(6 * 16 + 8 - cx);
            sy = (int16_t)(5 * 16 + 4 - cy);
        }
        if (sx >= -24 && sx <= max_sx && sy >= -24 && sy <= 248 && cnt < HEARTH_MAX_OAM) {
            OAM_SET_IDX(p, cnt, sx, sy, SPR_WINDMILL_BLADES, 0, DIR_DOWN, 0, FP_ONE, FP_ONE, AURA_NONE, 2, 2000,
                HS.windmill_rot);
            cnt++;
        }
    }

    for (i = 0; i < HEARTH_MAX_NPCS && cnt < HEARTH_MAX_OAM; i++) {
        if (HS.npcs[i].active && HS.npcs[i].emote_frames > 0 && HS.npcs[i].emote != 0) {
            emit_emote_oam(p, cnt++, (int16_t)(FP_PX(HS.npcs[i].fx) - cx),
                (int16_t)(FP_PX(HS.npcs[i].fy) - 25 - cy), HS.npcs[i].emote);
        }
    }
    for (i = 0; i < HEARTH_MAX_FIELD_ENEMIES && cnt < HEARTH_MAX_OAM; i++, e++) {
        if (e->alive && e->alert_frames > 0) {
            emit_emote_oam(p, cnt++, world_dx(st, FP_PX(e->fx), cx),
                (int16_t)(world_dy(st, FP_PX(e->fy), cy) - 26), SPR_EMOTE_BANG);
        }
    }
    if (HS.mode == MODE_FISHING && HS.fishing.phase == FISH_PHASE_BITE_ALERT && cnt < HEARTH_MAX_OAM) {
        emit_emote_oam(p, cnt++, (int16_t)(HS.fishing.bobber_x - cx), (int16_t)(HS.fishing.bobber_y - 18 - cy), SPR_EMOTE_BANG);
    }
    if (HS.hero_emote_frames > 0 && HS.hero_emote != 0 && cnt < HEARTH_MAX_OAM) {
        emit_emote_oam(p, cnt++, hsx, (int16_t)(hsy - 26), HS.hero_emote);
    }
    if (HS.pip_visible && HS.pip_emote_frames > 0 && HS.pip_emote != 0 && cnt < HEARTH_MAX_OAM) {
        emit_emote_oam(p, cnt++, psx, (int16_t)(psy - 24), HS.pip_emote);
    }
    if (HS.heart_orb.active && cnt < HEARTH_MAX_OAM) {
        emit_heart_orb_oam(st, p, cnt++, cx, cy);
    }

    return cnt;
}

static void build_explore_ppu_oam(HearthState *st, HearthPpuPacket *p) {
    int16_t cx = HP.cam_x, cy = HP.cam_y;
    int16_t max_sx = (int16_t)(HP.width + 24);
    int16_t hsx, hsy, psx, psy, bob, raft_bob;
    uint16_t fc16 = (uint16_t)HS.frame_count;
    uint16_t cnt, layer1_cnt;
    uint8_t wf, pfl;

    cnt = build_explore_ppu_props_and_actors(st, p, cx, cy, max_sx);

    /* Hero (Rowan, Layer 1) + Active Vehicle Vessel on Globe */
    hsx = world_dx(st, FP_PX(HS.hero_fx), cx);
    hsy = world_dy(st, FP_PX(HS.hero_fy), cy);
    raft_bob = 0;
    if (HS.is_world_map && HS.active_vehicle >= VEH_RAFT && cnt < HEARTH_MAX_OAM) {
        raft_bob = (int16_t)((HEARTH_SIN64[(fc16 >> 2) & 63u] * 2) >> 7);
        if (HS.active_vehicle >= VEH_BALLOON) raft_bob = (int16_t)(raft_bob - 8);
        OAM_SET_IDX(p, cnt, hsx, (int16_t)(hsy + 4 + raft_bob), SPR_RAFT,
            (uint8_t)((fc16 >> 4) & 1u), HS.hero_dir, OAM_FLAG_SHADOW,
            FP_ONE, FP_ONE, AURA_NONE, 1, (int16_t)(hsy - 1), 0);
        cnt++;
    }
    wf = (HS.hero_anim_tick == 0) ? 0 : (uint8_t)((HS.hero_anim_tick >> 3) & 3u);
    if ((HS.hero_safe_frames == 0 || (HS.hero_safe_frames & 4u) == 0u) && cnt < HEARTH_MAX_OAM) {
        OAM_SET_IDX(p, cnt, hsx, (int16_t)(hsy + raft_bob), SPR_HERO, wf, HS.hero_dir, OAM_FLAG_SHADOW,
            HS.hero_dashing ? 270 : FP_ONE, HS.hero_dashing ? 244 : FP_ONE,
            AURA_NONE, 1, hsy, 0);
        cnt++;
    }

    /* Pip Hovering Companion (visible once reunited in Evertale) */
    psx = world_dx(st, FP_PX(HS.pip_fx), cx);
    psy = world_dy(st, FP_PX(HS.pip_fy), cy);
    if (HS.pip_visible && cnt < HEARTH_MAX_OAM) {
        bob = (int16_t)((HEARTH_SIN64[(fc16 >> 1) & 63u] * 3) >> 7);
        pfl = (uint8_t)(OAM_FLAG_SHADOW | ((HS.pip_dir == DIR_LEFT) ? OAM_FLAG_FLIP_X : 0));
        OAM_SET_IDX(p, cnt, psx, (int16_t)(psy - 4 + bob), SPR_PIP,
            (uint8_t)((fc16 >> 3) & 1u), HS.pip_dir, pfl,
            FP_ONE, FP_ONE, AURA_NONE, 1, psy, 0);
        cnt++;
    }

    layer1_cnt = cnt;
    cnt = build_explore_ppu_layer2(st, p, cnt, cx, cy, hsx, hsy, psx, psy, max_sx);
    HP.oam_count = (uint8_t)cnt;
    sort_ppu_oam(p, (int16_t)layer1_cnt);

    if (HS.mode == MODE_EXPLORE && !HS.dialogue.active && !HS.atlas_open) {
        int16_t ix = 0, iy = 0;
        const char *ilbl = NULL;
        if (find_interactable(st, &ix, &iy, &ilbl) >= 0) {
            HP.interact_active = true;
            HP.interact_x = world_dx(st, ix, cx);
            HP.interact_y = world_dy(st, iy, cy);
            HP.interact_label = ilbl;
        }
    }
}

static void build_battle_ppu_oam(HearthState *st, HearthPpuPacket *p) {
    const HearthBattleState *b = &HS.battle;
    uint16_t fc16 = (uint16_t)HS.frame_count;
    uint8_t ei;
    (void)st;

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

static void build_intro_ppu_oam(HearthState *st, HearthPpuPacket *p) {
    /* Painted anime CG plates fill the viewport across Web, SDL2, Genesis, and SNES;
       keep OAM clear during MODE_INTRO so sprites never obscure the artwork. */
    (void)st;
    (void)p;
}

static uint8_t resolve_active_bgm(const HearthState *st) {
    (void)st;
    if (HS.mode == MODE_INTRO) return HS.intro.bgm_id;
    if (HS.mode == MODE_TITLE) return (uint8_t)BGM_ALDERBROOK;
    if (HS.mode == MODE_BATTLE) return HS.battle.is_boss_battle ? (uint8_t)BGM_BOSS : (uint8_t)BGM_BATTLE;
    if (HS.mode == MODE_BATTLE_VICTORY) return (uint8_t)BGM_VICTORY;
    return HS.bgm_id;
}

static void build_ppu_packet(HearthState *st) {
    HearthPpuPacket *p = &HS.ppu;

    HP.width = (HS.tuning.ppu_mode == PPU_SNES_256x224) ? (uint16_t)256 : (uint16_t)320;
    HP.height = 224;
    HP.oam_limit = 64;
    HP.frame_count = HS.frame_count;
    HP.mode = HS.mode;
    HP.zone_id = HS.zone_id;
    HP.map_w = HS.map_w ? HS.map_w : (uint8_t)HEARTH_MAP_W;
    HP.map_h = HS.map_h ? HS.map_h : (uint8_t)HEARTH_MAP_H;
    HP.globe_w = (uint16_t)HEARTH_GLOBE_W;
    HP.globe_h = (uint16_t)HEARTH_GLOBE_H;
    HP.world_region_id = HS.world_region_id;
    HP.active_vehicle = HS.active_vehicle;
    HP.vehicle_unlocked_mask = HS.vehicle_unlocked_mask;
    HP.atlas_open = HS.atlas_open;
    HP.designer_mode_enabled = (HEARTH_DESIGNER_MODE != 0);
    HP.is_world_map = HS.is_world_map;
    HP.terrain_pal_id = HS.terrain_pal_id;
    HP.bgm_id = resolve_active_bgm(st);
    HP.time_of_day = HS.time_of_day;
    HP.zone_is_dark = HS.zone_is_dark;
    HP.has_mountain_vista = HS.has_mountain_vista;
    HP.zone_banner_frames = HS.zone_banner_frames;
    HP.zone_name = HS.is_world_map
        ? HEARTH_REGION_NAMES[(HS.world_region_id < REGION_COUNT) ? HS.world_region_id : 0u]
        : HEARTH_ZONE_NAMES[(HS.zone_id < ZONE_COUNT) ? HS.zone_id : 1u];
    HP.objective_text = hearth_get_objective_text(st);
    HP.cam_x = FP_PX(HS.cam_fx);
    HP.cam_y = FP_PX(HS.cam_fy);
    HP.shake_x = (HS.shake_frames > 0) ? (int8_t)hearth_prng_range(&HS.prng, -(int16_t)HS.shake_intensity, (int16_t)HS.shake_intensity) : 0;
    HP.shake_y = (HS.shake_frames > 0) ? (int8_t)hearth_prng_range(&HS.prng, -(int16_t)HS.shake_intensity, (int16_t)HS.shake_intensity) : 0;
    if (HS.mode == MODE_INTRO) HP.fade_level = HS.intro.fade;
    else HP.fade_level = HS.warp_fade;
    HP.flash_level = HS.flash_level;
    HP.hitstop_active = (HS.hitstop_frames > 0);
    HP.oam_count = 0;
    HP.interact_active = false;

    if (HS.mode == MODE_INTRO) {
        build_intro_ppu_oam(st, p);
    } else if (HS.mode != MODE_BATTLE && HS.mode != MODE_BATTLE_VICTORY && HS.mode != MODE_BATTLE_DEFEAT) {
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
    int i;

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
    if (HS.zone_banner_frames > 0) HS.zone_banner_frames--;

    if (HS.hitstop_frames > 0) {
        HS.hitstop_frames--;
        build_ppu_packet(st);
        return &HP;
    }

    step_fx_pools(st);

    if (HS.hero_emote_frames > 0) HS.hero_emote_frames--; else HS.hero_emote = 0;
    if (HS.pip_emote_frames > 0) HS.pip_emote_frames--; else HS.pip_emote = 0;
    for (i = 0; i < HEARTH_MAX_NPCS; i++) {
        if (HS.npcs[i].emote_frames > 0) HS.npcs[i].emote_frames--; else HS.npcs[i].emote = 0;
    }

    /* Smooth zone transition fade-out / fade-in */
    if (HS.warp_pending) {
        step_zone_warp(st);
        build_ppu_packet(st);
        return &HP;
    } else if (HS.warp_fade > 0) {
        HS.warp_fade = (HS.warp_fade >= 2u) ? (uint8_t)(HS.warp_fade - 2u) : 0u;
    }

    switch (HS.mode) {
        case MODE_INTRO:
            step_intro_cutscene(st, just_pressed);
            break;

        case MODE_TITLE: {
            frame_title_camera(st);
            step_field_enemies(st, false);

#if HEARTH_DESIGNER_MODE
            if (just_pressed & (PAD_UP | PAD_LEFT)) {
                HS.title_cursor = (uint8_t)((HS.title_cursor + 2) % 3);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & (PAD_DOWN | PAD_RIGHT)) {
                HS.title_cursor = (uint8_t)((HS.title_cursor + 1) % 3);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & (PAD_A | PAD_START)) {
                if (HS.title_cursor == 0) hearth_start_story(st);
                else if (HS.title_cursor == 1) hearth_start_sandbox(st);
                else hearth_start_intro(st);
            }
#else
            if (HS.title_cursor > 1u) HS.title_cursor = 0u;
            if (just_pressed & (PAD_UP | PAD_LEFT | PAD_DOWN | PAD_RIGHT)) {
                HS.title_cursor = (uint8_t)(HS.title_cursor ^ 1u);
                push_sfx(st, SFX_CURSOR);
            } else if (just_pressed & (PAD_A | PAD_START)) {
                if (HS.title_cursor == 0) hearth_start_story(st);
                else hearth_start_intro(st);
            }
#endif
            break;
        }

        case MODE_EXPLORE: {
            if (HS.hero_safe_frames > 0) HS.hero_safe_frames--;
            if (HS.dialogue.active) {
                step_dialogue(st, just_pressed);
            } else if (HS.atlas_open) {
                if (just_pressed & (PAD_SELECT | PAD_B | PAD_START | PAD_X)) {
                    HS.atlas_open = false;
                    push_sfx(st, SFX_CANCEL);
                }
#if HEARTH_DESIGNER_MODE
                else if (HS.designer_sandbox) {
                    if (just_pressed & PAD_LEFT) {
                        HS.atlas_cursor = (uint8_t)((HS.atlas_cursor + REGION_COUNT - 1u) % REGION_COUNT);
                        push_sfx(st, SFX_CURSOR);
                    } else if (just_pressed & PAD_RIGHT) {
                        HS.atlas_cursor = (uint8_t)((HS.atlas_cursor + 1u) % REGION_COUNT);
                        push_sfx(st, SFX_CURSOR);
                    } else if (just_pressed & PAD_A) {
                        hearth_designer_warp_region(st, HS.atlas_cursor);
                    } else if ((just_pressed & (PAD_UP | PAD_DOWN | PAD_Y | PAD_L | PAD_R)) && HS.is_world_map) {
                        hearth_designer_cycle_vehicle(st);
                    }
                }
#endif
            } else {
                if (just_pressed & (PAD_SELECT | PAD_START | PAD_X)) {
                    HS.atlas_open = true;
                    HS.atlas_cursor = HS.world_region_id;
                    push_sfx(st, SFX_CURSOR);
                    break;
                }
#if HEARTH_DESIGNER_MODE
                if ((just_pressed & PAD_Y) && HS.designer_sandbox && HS.is_world_map) {
                    hearth_designer_cycle_vehicle(st);
                }
#endif
                step_hero_movement(st, pad_bits);
                if (HS.is_world_map) update_globe_region_streaming(st, true);
                step_pip_follower(st);
                step_field_enemies(st, true);
                if (HS.mode == MODE_EXPLORE) check_explore_triggers(st);
                if (HS.mode == MODE_EXPLORE && !HS.warp_pending && !HS.dialogue.active) {
                    if (just_pressed & PAD_A) try_interact(st);
                    if (just_pressed & PAD_L) { HS.active_slot = (uint8_t)((HS.active_slot + 2) % 3); push_sfx(st, SFX_CURSOR); }
                    if (just_pressed & PAD_R) { HS.active_slot = (uint8_t)((HS.active_slot + 1) % 3); push_sfx(st, SFX_CURSOR); }
                }
            }
#if !defined(HEARTH_TARGET_SNES)
            if (HS.has_mountain_vista && ((HS.frame_count & 31u) == 0u)) {
                int16_t px = (int16_t)(FP_PX(HS.cam_fx) + hearth_prng_range(&HS.prng, 16, 304));
                int16_t py = (int16_t)(FP_PX(HS.cam_fy) + hearth_prng_range(&HS.prng, 40, 200));
                if (HS.time_of_day == TOD_NIGHT) {
                    spawn_particle(st, 3, px, py, hearth_prng_range(&HS.prng, -24, 24),
                        hearth_prng_range(&HS.prng, -32, -8), 54, 2, 0, 8, false);
                } else {
                    spawn_particle(st, 0, px, py, hearth_prng_range(&HS.prng, -42, -14),
                        hearth_prng_range(&HS.prng, 28, 62), 60, 2, 0,
                        ((HS.frame_count & 32u) != 0u) ? 15 : 5, false);
                }
            } else if (HS.is_world_map && ((HS.frame_count & 15u) == 0u)) {
                int16_t sx_p = hearth_prng_range(&HS.prng, 16, 304);
                int16_t sy_p = hearth_prng_range(&HS.prng, 16, 204);
                if (HS.world_region_id == REGION_FROSTMERE) {
                    spawn_particle(st, 0, sx_p, sy_p, hearth_prng_range(&HS.prng, -28, 12),
                        hearth_prng_range(&HS.prng, 34, 64), 48, 2, 3, 12, true);
                } else if (HS.world_region_id == REGION_VOLCANO) {
                    spawn_particle(st, 3, sx_p, sy_p, hearth_prng_range(&HS.prng, -20, 20),
                        hearth_prng_range(&HS.prng, -56, -24), 40, 2, 3, 10, true);
                } else if (HS.world_region_id == REGION_AETHERIS) {
                    spawn_particle(st, 3, sx_p, sy_p, hearth_prng_range(&HS.prng, -16, 16),
                        hearth_prng_range(&HS.prng, -32, 16), 36, 2, 3, 15, true);
                } else if (HS.world_region_id == REGION_DESERT) {
                    spawn_particle(st, 0, sx_p, sy_p, hearth_prng_range(&HS.prng, 38, 72),
                        hearth_prng_range(&HS.prng, -10, 14), 34, 2, 3, 11, true);
                }
            }
#endif
            step_camera(st);
            break;
        }

        case MODE_CUTSCENE:
            step_pip_follower(st);
            step_cutscene(st, just_pressed);
            step_camera(st);
            break;

        case MODE_SHOP:
            step_shop(st, just_pressed);
            break;

        case MODE_FISHING:
            step_fishing(st, pad_bits, just_pressed);
            step_camera(st);
            break;

        case MODE_BATTLE:
            step_battle(st, just_pressed);
            break;

        case MODE_BATTLE_VICTORY:
        case MODE_BATTLE_DEFEAT:
            if (just_pressed & (PAD_A | PAD_START)) {
                bool won_ironfang = (HS.mode == MODE_BATTLE_VICTORY && HS.battle.encounter_preset == ENCOUNTER_IRONFANG_BOSS);
                HS.battle.active = false;
                HS.hero_safe_frames = 90;
                push_sfx(st, SFX_CONFIRM);
                if (won_ironfang) {
                    hearth_start_cutscene(st, CUTSCENE_IRONFANG_VICTORY);
                } else {
                    HS.mode = MODE_EXPLORE;
                }
            } else if (just_pressed & PAD_B) {
                hearth_start_battle(st, HS.battle.encounter_preset, HS.battle.biome, -1);
            }
            break;
    }

    build_ppu_packet(st);
    return &HP;
}
