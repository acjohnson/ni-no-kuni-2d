/* ============================================================================
 * hearth_hero24.h — Shared 16x24 Tall JRPG Hero, Petite Pip & Key NPC Baker
 *
 * Pure strict ANSI C89/C90 — zero OS headers, zero malloc, tiny stack frame.
 * Used identically across Native PC (SDL2), Sega Genesis (SGDK SPRITE_SIZE(2,3)),
 * and Super Nintendo (PVSnesLib OBJ_SIZE8_L16 + VBlank DMA).
 *
 * Output pixel buffers are 4bpp indexed (0 = transparent, 1..15 = palette index):
 *   - Rowan (16x24 = 384 bytes, Palette 1)
 *   - Petite Pip (16x16 = 256 bytes, Palette 1)
 *   - 13 Tall Adult NPCs (16x24 = 384 bytes, Palette 2; Shadow Grandma Palette 3)
 * ============================================================================ */

#ifndef HEARTH_HERO24_H
#define HEARTH_HERO24_H

#include "hearth_types.h"

/* Compact hex-string pattern painter into an arbitrary w x h indexed buffer */
static void h24_blit_pat(uint8_t *dst, uint8_t bw, uint8_t bh,
                         const char *const *rows, uint8_t nrows,
                         int8_t ox, int8_t oy, uint8_t flip_x) {
    uint8_t r;
    uint8_t c;
    int16_t dx;
    int16_t dy;
    char ch;
    uint8_t col;
    uint8_t rlen;

    for (r = 0u; r < nrows; r++) {
        const char *line = rows[r];
        dy = (int16_t)oy + (int16_t)r;
        if (dy < 0 || dy >= (int16_t)bh) continue;
        rlen = 0u;
        while (line[rlen] != '\0') rlen++;
        for (c = 0u; c < rlen; c++) {
            ch = line[c];
            if (ch == '.' || ch == ' ') continue;
            if (ch >= '1' && ch <= '9') col = (uint8_t)(ch - '0');
            else if (ch >= 'a' && ch <= 'f') col = (uint8_t)(10 + (ch - 'a'));
            else if (ch >= 'A' && ch <= 'F') col = (uint8_t)(10 + (ch - 'A'));
            else continue;
            dx = flip_x ? (int16_t)((int16_t)bw - 1 - ((int16_t)ox + (int16_t)c))
                        : (int16_t)((int16_t)ox + (int16_t)c);
            if (dx >= 0 && dx < (int16_t)bw) {
                dst[(uint16_t)((uint16_t)dy * (uint16_t)bw + (uint16_t)dx)] = col;
            }
        }
    }
}

static void h24_px(uint8_t *dst, uint8_t bw, uint8_t bh,
                   int16_t x, int16_t y, uint8_t col, uint8_t flip_x) {
    int16_t dx = flip_x ? (int16_t)((int16_t)bw - 1 - x) : x;
    if (dx >= 0 && dx < (int16_t)bw && y >= 0 && y < (int16_t)bh) {
        dst[(uint16_t)((uint16_t)y * (uint16_t)bw + (uint16_t)dx)] = col;
    }
}

static void h24_rect(uint8_t *dst, uint8_t bw, uint8_t bh,
                     int16_t x, int16_t y, int16_t w, int16_t h,
                     uint8_t col, uint8_t flip_x) {
    int16_t py;
    int16_t px;
    for (py = y; py < (int16_t)(y + h); py++) {
        for (px = x; px < (int16_t)(x + w); px++) {
            h24_px(dst, bw, bh, px, py, col, flip_x);
        }
    }
}

/* ============================================================================
 * 1. ROWAN — 16x24 EXPRESSIVE JRPG HERO WITH FLOWING TERRACOTTA SCARF (PAL 1)
 *    dir:  DIR_DOWN(0), DIR_UP(1), DIR_LEFT(2), DIR_RIGHT(3)
 *    pose: HERO_POSE_IDLE(0)..HERO_POSE_BTL_VICTORY(11)
 * ============================================================================ */
static void hearth_bake_rowan_16x24(uint8_t *dst384, uint8_t dir, uint8_t pose) {
    /* Sculpted tousled chestnut hair + expressive face heads (16x10) */
    static const char *const HEAD_DOWN[10] = {
        ".....111111.....",
        "...1145554411...",
        "..145544445441..",
        ".14544444444441.",
        ".14414422441441.",
        ".14222222222241.",
        "..12e122221e21..",
        "..121122221121..",
        "...1232222321...",
        "....11222211...."
    };
    static const char *const HEAD_DOWN_BLINK[10] = {
        ".....111111.....",
        "...1145554411...",
        "..145544445441..",
        ".14544444444441.",
        ".14414422441441.",
        ".14222222222241.",
        "..122222222221..",
        "..121122221121..",
        "...1232222321...",
        "....11222211...."
    };
    static const char *const HEAD_UP[10] = {
        ".....111111.....",
        "...1145554411...",
        "..145544445541..",
        ".14544444445441.",
        ".14444555444441.",
        ".14444444444441.",
        "..144444444441..",
        "..144444444441..",
        "...1144444411...",
        "....11111111...."
    };
    /* Right-facing 3/4 JRPG profile head (H-flipped cleanly for DIR_LEFT) */
    static const char *const HEAD_SIDE[10] = {
        "....1111111.....",
        "..11455544411...",
        ".1455444454441..",
        ".14444444445441.",
        "..1444444422441.",
        "..144444222221..",
        "...14442221e21..",
        "...14432221121..",
        "....114222231...",
        "......112211...."
    };
    static const char *const HEAD_SIDE_BLINK[10] = {
        "....1111111.....",
        "..11455544411...",
        ".1455444454441..",
        ".14444444445441.",
        "..1444444422441.",
        "..144444222221..",
        "...14442222221..",
        "...14432221121..",
        "....114222231...",
        "......112211...."
    };
    /* Layered forest-green tunic + wrapped terracotta scarf collar + brass heart locket (16x9) */
    static const char *const TORSO_FRONT[9] = {
        "...118899998811.",
        "..1899888888981.",
        "..168889aa98861.",
        "..167666bb66761.",
        ".1676667aa766761",
        ".126666666666621",
        "..114544aa44541.",
        "...16666666661..",
        "...1cccccccc1..."
    };
    static const char *const TORSO_UP[9] = {
        "...118899998811.",
        "..1899888888981.",
        "..1688888888861.",
        "..1676666666761.",
        ".167666666666761",
        ".126666666666621",
        "..1144444444441.",
        "...16666666661..",
        "...1cccccccc1..."
    };
    static const char *const TORSO_SIDE[9] = {
        "....1188999881..",
        "...18998888981..",
        "...16688998861..",
        "...1676666aa61..",
        "...16766676761..",
        "...16662266661..",
        "....145444aa1...",
        "....166666661...",
        "....1cccccc1...."
    };

    uint8_t flip_x = 0u;
    uint8_t is_battle = (pose >= HERO_POSE_BTL_READY) ? 1u : 0u;
    uint8_t is_dash = (pose == HERO_POSE_DASH_A || pose == HERO_POSE_DASH_B) ? 1u : 0u;
    uint8_t eff_dir = is_battle ? (uint8_t)DIR_RIGHT : dir;
    int8_t bob = 0;
    int8_t lean_x = 0;
    int8_t l_leg_x = 0;
    int8_t r_leg_x = 0;
    int8_t l_leg_y = 0;
    int8_t r_leg_y = 0;
    int8_t scarf_wave = 0;

    hearth_memset(dst384, 0u, 384u);

    if (eff_dir == DIR_LEFT) {
        flip_x = 1u;
    }

    /* Compute per-pose bob, forward dash lean, stride & scarf flutter */
    if (pose == HERO_POSE_WALK_A) {
        bob = -1;
        l_leg_x = -1; r_leg_x = 1;
        l_leg_y = -1; r_leg_y = 0;
        scarf_wave = -1;
    } else if (pose == HERO_POSE_WALK_PASS) {
        bob = 0;
        scarf_wave = 0;
    } else if (pose == HERO_POSE_WALK_B) {
        bob = -1;
        l_leg_x = 1;  r_leg_x = -1;
        l_leg_y = 0;  r_leg_y = -1;
        scarf_wave = 1;
    } else if (pose == HERO_POSE_DASH_A) {
        bob = -1;
        lean_x = 1;
        l_leg_x = -2; r_leg_x = 2;
        l_leg_y = -1; r_leg_y = 0;
        scarf_wave = -2;
    } else if (pose == HERO_POSE_DASH_B) {
        bob = 0;
        lean_x = 1;
        l_leg_x = 2;  r_leg_x = -2;
        l_leg_y = 0;  r_leg_y = -1;
        scarf_wave = -1;
    } else if (pose == HERO_POSE_BTL_ATTACK) {
        bob = 0;
        lean_x = 2;
        l_leg_x = -2; r_leg_x = 2;
        scarf_wave = -2;
    } else if (pose == HERO_POSE_BTL_CAST) {
        bob = -1;
        scarf_wave = -2;
    } else if (pose == HERO_POSE_BTL_HURT) {
        bob = 1;
        lean_x = -2;
        scarf_wave = 1;
    } else if (pose == HERO_POSE_BTL_VICTORY) {
        bob = -1;
        scarf_wave = -1;
    }

    /* 1. Legs (indigo trousers) & Striding Dark Leather Boots (rows 18..23) */
    if (eff_dir == DIR_DOWN || eff_dir == DIR_UP) {
        /* Left leg & boot */
        h24_rect(dst384, 16u, 24u, 4, (int16_t)(18 + l_leg_y), 3, 2, 12u, flip_x);
        h24_rect(dst384, 16u, 24u, 3, (int16_t)(20 + l_leg_y), 4, 4, 1u,  flip_x);
        h24_rect(dst384, 16u, 24u, 4, (int16_t)(20 + l_leg_y), 2, 3, 15u, flip_x);
        h24_px(dst384, 16u, 24u, 4, (int16_t)(20 + l_leg_y), 5u, flip_x);
        /* Right leg & boot */
        h24_rect(dst384, 16u, 24u, 9, (int16_t)(18 + r_leg_y), 3, 2, 12u, flip_x);
        h24_rect(dst384, 16u, 24u, 9, (int16_t)(20 + r_leg_y), 4, 4, 1u,  flip_x);
        h24_rect(dst384, 16u, 24u, 10, (int16_t)(20 + r_leg_y), 2, 3, 15u, flip_x);
        h24_px(dst384, 16u, 24u, 10, (int16_t)(20 + r_leg_y), 5u, flip_x);
    } else {
        /* Side profile / battle dynamic split-stride legs & boots */
        int16_t bx = (int16_t)(5 + l_leg_x);
        int16_t fx = (int16_t)(8 + r_leg_x);
        /* Back leg */
        h24_rect(dst384, 16u, 24u, bx, (int16_t)(18 + l_leg_y), 3, 2, 12u, flip_x);
        h24_rect(dst384, 16u, 24u, bx, (int16_t)(20 + l_leg_y), 4, 4, 1u,  flip_x);
        h24_rect(dst384, 16u, 24u, (int16_t)(bx + 1), (int16_t)(20 + l_leg_y), 2, 3, 15u, flip_x);
        /* Front leg */
        h24_rect(dst384, 16u, 24u, fx, (int16_t)(18 + r_leg_y), 3, 2, 12u, flip_x);
        h24_rect(dst384, 16u, 24u, fx, (int16_t)(20 + r_leg_y), 4, 4, 1u,  flip_x);
        h24_rect(dst384, 16u, 24u, (int16_t)(fx + 1), (int16_t)(20 + r_leg_y), 2, 3, 15u, flip_x);
        h24_px(dst384, 16u, 24u, (int16_t)(fx + 2), (int16_t)(21 + r_leg_y), 5u, flip_x);
    }

    /* 2. Flowing Terracotta Scarf Tail (behind shoulders, animated by stride/dash/battle!) */
    if (eff_dir == DIR_DOWN) {
        /* Scarf tail peeks gracefully on Rowan's right shoulder & flutters */
        int16_t sy = (int16_t)(11 + bob);
        h24_rect(dst384, 16u, 24u, 11, sy, 3, 4, 1u, flip_x);
        h24_rect(dst384, 16u, 24u, 11, (int16_t)(sy + 1), 2, 2, 8u, flip_x);
        h24_px(dst384, 16u, 24u, 12, (int16_t)(sy + 1), 9u, flip_x);
        h24_rect(dst384, 16u, 24u, (int16_t)(12 + (scarf_wave < 0 ? 1 : 0)), (int16_t)(sy + 3), 2, 3, 8u, flip_x);
        h24_px(dst384, 16u, 24u, (int16_t)(13 + (scarf_wave < 0 ? 1 : 0)), (int16_t)(sy + 4), 9u, flip_x);
    } else if (eff_dir == DIR_UP) {
        /* Full flowing terracotta scarf ribbons streaming down Rowan's back! */
        int16_t sy = (int16_t)(11 + bob);
        int16_t sw = (int16_t)(scarf_wave);
        h24_rect(dst384, 16u, 24u, (int16_t)(5 + (sw >> 1)), sy, 6, 5, 1u, flip_x);
        h24_rect(dst384, 16u, 24u, (int16_t)(6 + (sw >> 1)), sy, 4, 4, 8u, flip_x);
        h24_rect(dst384, 16u, 24u, (int16_t)(7 + (sw >> 1)), sy, 2, 3, 9u, flip_x);
        /* Split flutter tips */
        h24_rect(dst384, 16u, 24u, (int16_t)(5 + sw), (int16_t)(sy + 4), 2, 3, 8u, flip_x);
        h24_rect(dst384, 16u, 24u, (int16_t)(9 + sw), (int16_t)(sy + 4), 2, 2, 9u, flip_x);
    } else {
        /* Side / Dash / Battle — Iconic trailing JRPG terracotta scarf fluttering behind! */
        int16_t sy = (int16_t)(10 + bob);
        int16_t tail_y = (int16_t)(sy + 1 + scarf_wave);
        if (is_dash || pose == HERO_POSE_BTL_ATTACK || pose == HERO_POSE_BTL_CAST) {
            /* Long dramatic horizontal billowing scarf streaming behind Rowan! */
            h24_rect(dst384, 16u, 24u, 0, (int16_t)(tail_y + 1), 5, 4, 1u, flip_x);
            h24_rect(dst384, 16u, 24u, 0, (int16_t)(tail_y + 2), 4, 2, 8u, flip_x);
            h24_rect(dst384, 16u, 24u, 1, (int16_t)(tail_y + 2), 3, 1, 9u, flip_x);
            h24_px(dst384, 16u, 24u, 0, (int16_t)(tail_y + 1), 9u, flip_x);
        } else {
            /* Classic gentle trailing scarf curve behind shoulder */
            h24_rect(dst384, 16u, 24u, 1, (int16_t)(sy + 1), 4, 4, 1u, flip_x);
            h24_rect(dst384, 16u, 24u, 1, (int16_t)(sy + 2), 3, 2, 8u, flip_x);
            h24_px(dst384, 16u, 24u, 2, (int16_t)(sy + 2), 9u, flip_x);
            h24_rect(dst384, 16u, 24u, (int16_t)(1 + (scarf_wave > 0 ? 1 : 0)), (int16_t)(sy + 4), 2, 3, 8u, flip_x);
            h24_px(dst384, 16u, 24u, (int16_t)(1 + (scarf_wave > 0 ? 1 : 0)), (int16_t)(sy + 5), 9u, flip_x);
        }
    }

    /* 3. Torso (layered tunic + collar scarf + belt + locket) */
    if (eff_dir == DIR_DOWN) {
        h24_blit_pat(dst384, 16u, 24u, TORSO_FRONT, 9u, 0, (int8_t)(10 + bob), flip_x);
        /* Expressive arm swing on walk */
        if (pose == HERO_POSE_WALK_A || pose == HERO_POSE_DASH_A) {
            h24_px(dst384, 16u, 24u, 1, (int16_t)(14 + bob), 2u, flip_x);
            h24_px(dst384, 16u, 24u, 14, (int16_t)(16 + bob), 2u, flip_x);
        } else if (pose == HERO_POSE_WALK_B || pose == HERO_POSE_DASH_B) {
            h24_px(dst384, 16u, 24u, 1, (int16_t)(16 + bob), 2u, flip_x);
            h24_px(dst384, 16u, 24u, 14, (int16_t)(14 + bob), 2u, flip_x);
        }
    } else if (eff_dir == DIR_UP) {
        h24_blit_pat(dst384, 16u, 24u, TORSO_UP, 9u, 0, (int8_t)(10 + bob), flip_x);
    } else {
        h24_blit_pat(dst384, 16u, 24u, TORSO_SIDE, 9u, lean_x, (int8_t)(10 + bob), flip_x);
    }

    /* 4. Head (sculpted hair + face + idle blink) */
    if (eff_dir == DIR_DOWN) {
        h24_blit_pat(dst384, 16u, 24u,
            (pose == HERO_POSE_BLINK) ? HEAD_DOWN_BLINK : HEAD_DOWN,
            10u, 0, (int8_t)(1 + bob), flip_x);
    } else if (eff_dir == DIR_UP) {
        h24_blit_pat(dst384, 16u, 24u, HEAD_UP, 10u, 0, (int8_t)(1 + bob), flip_x);
    } else {
        h24_blit_pat(dst384, 16u, 24u,
            (pose == HERO_POSE_BLINK || pose == HERO_POSE_BTL_HURT) ? HEAD_SIDE_BLINK : HEAD_SIDE,
            10u, lean_x, (int8_t)(1 + bob), flip_x);
    }

    /* 5. Battle Hearthwood Wand & Heroic Action Arms (poses 7..11) */
    if (pose == HERO_POSE_BTL_READY) {
        /* Ready stance: hand holds Hearthwood Wand angled forward with warm star tip */
        h24_rect(dst384, 16u, 24u, 11, (int16_t)(13 + bob), 2, 2, 2u, flip_x);
        h24_px(dst384, 16u, 24u, 12, (int16_t)(12 + bob), 4u, flip_x);
        h24_px(dst384, 16u, 24u, 13, (int16_t)(11 + bob), 5u, flip_x);
        h24_px(dst384, 16u, 24u, 14, (int16_t)(10 + bob), 10u, flip_x);
        h24_px(dst384, 16u, 24u, 14, (int16_t)(9 + bob),  11u, flip_x);
    } else if (pose == HERO_POSE_BTL_ATTACK) {
        /* Forward wand slash lunge + gleaming star arc! */
        h24_rect(dst384, 16u, 24u, 11, (int16_t)(12 + bob), 3, 2, 2u, flip_x);
        h24_rect(dst384, 16u, 24u, 13, (int16_t)(11 + bob), 2, 2, 5u, flip_x);
        h24_px(dst384, 16u, 24u, 15, (int16_t)(10 + bob), 11u, flip_x);
        h24_px(dst384, 16u, 24u, 15, (int16_t)(11 + bob), 14u, flip_x);
        h24_px(dst384, 16u, 24u, 14, (int16_t)(9 + bob),  10u, flip_x);
    } else if (pose == HERO_POSE_BTL_CAST) {
        /* Spell cast: arm & wand raised skyward with radiant golden-cyan spark! */
        h24_rect(dst384, 16u, 24u, 11, (int16_t)(9 + bob), 2, 4, 6u, flip_x);
        h24_rect(dst384, 16u, 24u, 11, (int16_t)(7 + bob), 2, 2, 2u, flip_x);
        h24_rect(dst384, 16u, 24u, 12, (int16_t)(3 + bob), 1, 5, 5u, flip_x);
        h24_rect(dst384, 16u, 24u, 11, (int16_t)(1 + bob), 3, 3, 10u, flip_x);
        h24_px(dst384, 16u, 24u, 12, (int16_t)(2 + bob), 11u, flip_x);
        h24_px(dst384, 16u, 24u, 14, (int16_t)(2 + bob), 14u, flip_x);
    } else if (pose == HERO_POSE_BTL_VICTORY) {
        /* Joyful victory fist & wand raised high! */
        h24_rect(dst384, 16u, 24u, 11, (int16_t)(8 + bob), 2, 5, 7u, flip_x);
        h24_rect(dst384, 16u, 24u, 11, (int16_t)(6 + bob), 2, 2, 2u, flip_x);
        h24_rect(dst384, 16u, 24u, 12, (int16_t)(2 + bob), 1, 4, 5u, flip_x);
        h24_px(dst384, 16u, 24u, 12, (int16_t)(1 + bob), 11u, flip_x);
        h24_px(dst384, 16u, 24u, 13, (int16_t)(2 + bob), 10u, flip_x);
    }
}

/* ============================================================================
 * 2. PETITE BOUNCY GARDEN-GNOME PIP — ~11x13 INSIDE 16x16 (PAL 1, ZERO WINGS!)
 *    Proportioned ~half of 16x24 Rowan's height (Oliver & Mr. Drippy scale!)
 *    pose: 0=Grounded Idle, 1=Mid-Air Stretch Hop, 2=Landing/Takeoff Squash
 * ============================================================================ */
static void hearth_bake_pip_16x16(uint8_t *dst256, uint8_t pose) {
    static const char *const PIP_P0[16] = {
        "................",
        "................",
        "................",
        ".......11.......",
        "......1671.11...",
        ".....167761771..",
        "....166776661...",
        "....14555541....",
        "...1212222121...",
        "....12922921....",
        "...1898888981...",
        "...18a1bb1a81...",
        "....1a1bb1a1....",
        "....1aaaaaa1....",
        ".....141141.....",
        "....1ff11ff1...."
    };
    static const char *const PIP_P1[16] = {
        "................",
        ".......11..11...",
        "......16711771..",
        ".....16776661...",
        ".....1667761....",
        "....14555541....",
        "....12122121....",
        "....12922921....",
        "...1898888981...",
        "....18a1b1a81...",
        "....1a1bb1a1....",
        ".....1aaaa1.....",
        ".....141141.....",
        "....1f1..1f1....",
        "................",
        "................"
    };
    static const char *const PIP_P2[16] = {
        "................",
        "................",
        "................",
        "................",
        "................",
        "......1111......",
        ".....16776111...",
        "....1667766771..",
        "...11455555411..",
        "..121222222121..",
        "...1292222921...",
        "..1898a11a8981..",
        "..18aa1bb1aa81..",
        "...1aaaaaaaa1...",
        "...1441111441...",
        "..1fff1..1fff1.."
    };
    const char *const *pat = (pose == 1u) ? PIP_P1 : ((pose == 2u) ? PIP_P2 : PIP_P0);
    hearth_memset(dst256, 0u, 256u);
    h24_blit_pat(dst256, 16u, 16u, pat, 16u, 0, 0, 0u);
}

/* ============================================================================
 * 3. TALL 16x24 ADULT NPCS — GRANDMA MARA, ELDER WREN, CLARA, GUARD & CAST
 *    Returns 1 if sprite_id is a tall 16x24 character, 0 otherwise.
 * ============================================================================ */
static uint8_t hearth_is_tall_sprite(uint8_t sprite_id) {
    if (sprite_id == SPR_HERO) return 1u;
    if (sprite_id == SPR_NPC_MARA || sprite_id == SPR_NPC_ELDER ||
        sprite_id == SPR_NPC_CLARA || sprite_id == SPR_NPC_GUARD ||
        sprite_id == SPR_NPC_BARNABY || sprite_id == SPR_NPC_MAPLE ||
        sprite_id == SPR_NPC_JASPER || sprite_id == SPR_NPC_TESSA ||
        sprite_id == SPR_NPC_CORIN || sprite_id == SPR_NPC_SENTINEL ||
        sprite_id == SPR_NPC_TOWNSFOLK || sprite_id == SPR_NPC_SHADOW_GRAN ||
        sprite_id == SPR_NPC_SHIPWRIGHT) {
        return 1u;
    }
    return 0u;
}

/* Bake any of the 13 tall 16x24 adult NPCs into dst384 (PAL 2, Shadow Grandma PAL 3) */
static void hearth_bake_npc_16x24(uint8_t *dst384, uint8_t sprite_id, uint8_t frame) {
    /* Shared graceful 16x24 adult templates customized per character */
    static const char *const MARA_ART[24] = {
        ".....111111.....",
        "....144ff441....",
        "...1444444441...",
        "..145444444541..",
        "..144222222441..",
        "..142222222241..",
        "..142122221241..",
        "...1232222321...",
        "...1122332211...",
        "..1aaaaffaaaa1..",
        ".1aaaddffddaaa1.",
        ".1aaaffffffaaa1.",
        ".1a2affffffa2a1.",
        ".1aaaffffffaaa1.",
        "..1ddffffffdd1..",
        "..1ddffffffdd1..",
        "..1d1ffffff1d1..",
        "..1d1ffffff1d1..",
        "..1dddddddddd1..",
        "..1dddddddddd1..",
        "...1111111111...",
        "....1b1..1b1....",
        "...1bb1..1bb1...",
        "...1111..1111..."
    };
    static const char *const ELDER_ART[24] = {
        "....11111111....",
        "...1444ff4441...",
        "..145444444541..",
        "..144222222441..",
        "..142122221241..",
        "..142222222241..",
        "..14444ff44441..",
        "...1444444441...",
        "...1144444411...",
        "..166744447661..",
        ".16677444477661.",
        ".16667777776661.",
        ".16266777766261.",
        ".16666677666661.",
        "..166667766661..",
        "..166667766661..",
        "..166667766661..",
        "..166666666661..",
        "..166666666661..",
        "..177777777771..",
        "...1111111111...",
        "....1b1..1b1....",
        "...1bb1..1bb1...",
        "...1111..1111..."
    };
    static const char *const CLARA_ART[24] = {
        "....11111111....",
        "...1cc7777cc1...",
        "..1ccc7777ccc1..",
        "..1cc222222cc1..",
        "..1c22222222c1..",
        "..1c21222212c1..",
        "..1c23222232c1..",
        "...1c222222c1...",
        "...11ffffff11...",
        "..1777ffff7771..",
        ".1777ffffff7771.",
        ".1727ffffff7271.",
        ".1777ffffff7771.",
        "..177ffffff771..",
        "..1677ffff7761..",
        "..1677ffff7761..",
        "..166777777661..",
        "..166777777661..",
        "..166666666661..",
        "..1ffffffffff1..",
        "...1111111111...",
        "....1b1..1b1....",
        "...1bb1..1bb1...",
        "...1111..1111..."
    };
    static const char *const GUARD_ART[24] = {
        ".....11aa11.....",
        "....1aaaaaa1....",
        "...1554444551...",
        "..154444444451..",
        "..154111111451..",
        "..154212212451..",
        "..154222222451..",
        "...1552222551...",
        "...1155555511...",
        "..158887788851..",
        ".15588877888551.",
        ".15288877888251.",
        ".15588888888551.",
        "..11b777777b11..",
        "..188887788881..",
        "..188887788881..",
        "..188888888881..",
        "...1555115551...",
        "...1555115551...",
        "...1551..1551...",
        "...1111..1111...",
        "...1bb1..1bb1...",
        "..1bbb1..1bbb1..",
        "..11111..11111.."
    };
    static const char *const BARNABY_ART[24] = {
        "...1111111111...",
        "..188897798881..",
        ".11111111111111.",
        "..188222222881..",
        "..122222222221..",
        "..122122221221..",
        "..124444444421..",
        "...1444224441...",
        "...11ffffff11...",
        "..188897798881..",
        ".18888877888881.",
        ".18888877888881.",
        ".18288877888281.",
        ".18888877888881.",
        "..111777777111..",
        "..188888888881..",
        "..188888888881..",
        "...1888118881...",
        "...1888118881...",
        "...1881..1881...",
        "...1111..1111...",
        "...1bb1..1bb1...",
        "..1bbb1..1bbb1..",
        "..11111..11111.."
    };
    static const char *const MAPLE_ART[24] = {
        "....11111111....",
        "...1ffffffff1...",
        "..1ff444444ff1..",
        "..1cc222222cc1..",
        "..1c22222222c1..",
        "..1c21222212c1..",
        "..1c23222232c1..",
        "...1c222222c1...",
        "...11ffffff11...",
        "..1aaaffffaaa1..",
        ".1aaaaffffaaaa1.",
        ".1a2aaffffaa2a1.",
        ".1aaaaffffaaaa1.",
        "..1aaaffffaaa1..",
        "..1aaaffffaaa1..",
        "..1aaaffffaaa1..",
        "..1aaaffffaaa1..",
        "..1aaaaaaaaaa1..",
        "..1aaaaaaaaaa1..",
        "..1dddddddddd1..",
        "...1111111111...",
        "....1b1..1b1....",
        "...1bb1..1bb1...",
        "...1111..1111..."
    };
    static const char *const JASPER_ART[24] = {
        "....11111111....",
        "...1bbccccbb1...",
        "..1bbccccccbb1..",
        "..1bb222222bb1..",
        "..1b21222212b1..",
        "..1b44444444b1..",
        "..1b444ff444b1..",
        "...1444444441...",
        "...11aaaaaaa1...",
        "..1bbbaaaabbb1..",
        ".1bbbccccccbbb1.",
        ".1bbcbbbbbbcbb1.",
        ".1b2bbbbbbbb2b1.",
        ".1bbbbbbbbbbbb1.",
        "..11b777777b11..",
        "..1bbbbbbbbbb1..",
        "..1bbbbbbbbbb1..",
        "...1888118881...",
        "...1888118881...",
        "...1881..1881...",
        "...1111..1111...",
        "...1bb1..1bb1...",
        "..1bbb1..1bbb1..",
        "..11111..11111.."
    };
    static const char *const TESSA_ART[24] = {
        ".....111111.....",
        "....1bbccbb1....",
        "...1ffffffff1...",
        "..1bb222222bb1..",
        "..1b22222222b1..",
        "..1b21222212b1..",
        "..1b23222232b1..",
        "...1222222221...",
        "...11ffffff11...",
        "..1777ffff7771..",
        ".17777ffff77771.",
        ".17277ffff77271.",
        ".17777ffff77771.",
        "..1666ffff6661..",
        "..1666ffff6661..",
        "..1666ffff6661..",
        "..1666ffff6661..",
        "..166666666661..",
        "..166666666661..",
        "..1aaaaaaaaaa1..",
        "...1111111111...",
        "....1b1..1b1....",
        "...1bb1..1bb1...",
        "...1111..1111..."
    };
    static const char *const CORIN_ART[24] = {
        "....11111111....",
        "...1445555441...",
        "..145444444541..",
        "..144222222441..",
        "..142f1221f241..",
        "..142222222241..",
        "...1222332221...",
        "...1122222211...",
        "..188877778881..",
        ".18899777799881.",
        ".18888777788881.",
        ".18288877888281.",
        ".18888877888881.",
        "..188887788881..",
        "..188887788881..",
        "..188887788881..",
        "..188887788881..",
        "..188887788881..",
        "..188888888881..",
        "..177777777771..",
        "...1111111111...",
        "....1b1..1b1....",
        "...1bb1..1bb1...",
        "...1111..1111..."
    };
    static const char *const SENTINEL_ART[24] = {
        ".....117711.....",
        "....17777771....",
        "...1444ff4441...",
        "..144555555441..",
        "..144111111441..",
        "..144212212441..",
        "..144555555441..",
        "...1444444441...",
        "..148887788841..",
        ".14488877888441.",
        ".14488877888441.",
        ".14498877889441.",
        ".14488877888441.",
        "..114777777411..",
        "..148887788841..",
        "..148887788841..",
        "..144888888441..",
        "...1444114441...",
        "...1444114441...",
        "...1551..1551...",
        "...1111..1111...",
        "...1441..1441...",
        "..14441..14441..",
        "..11111..11111.."
    };
    static const char *const SHIPWRIGHT_ART[24] = {
        "....11111111....",
        "...1888998881...",
        "..188888888881..",
        "..1bb222222bb1..",
        "..1b21222212b1..",
        "..1b22bbbb22b1..",
        "...1bbbbbbbb1...",
        "...11cccccc11...",
        "..1ccbbbbbbcc1..",
        ".1cccbbbbbbccc1.",
        ".1cc2bbbbbb2cc1.",
        ".1cccbbbbbbccc1.",
        ".1cc2bbbbbb2cc1.",
        "..111777777111..",
        "..188888888881..",
        "..188888888881..",
        "..188888888881..",
        "...1888118881...",
        "...1888118881...",
        "...1881..1881...",
        "...1111..1111...",
        "...1bb1..1bb1...",
        "..1bbb1..1bbb1..",
        "..11111..11111.."
    };
    static const char *const TOWNSFOLK_ART[24] = {
        "....11111111....",
        "...1bbccccbb1...",
        "..1bbccccccbb1..",
        "..1b22222222b1..",
        "..1b22222222b1..",
        "..1b21222212b1..",
        "...1232222321...",
        "...11ffffff11...",
        "..18889ff98881..",
        ".188999ff999881.",
        ".18899999999881.",
        ".18288888888281.",
        ".18888888888881.",
        "..11bbbbbbbb11..",
        "..188888888881..",
        "..188888888881..",
        "...1bb1111bb1...",
        "...1bb1..1bb1...",
        "...1bb1..1bb1...",
        "...1bb1..1bb1...",
        "...1111..1111...",
        "...1bb1..1bb1...",
        "..1bbb1..1bbb1..",
        "..11111..11111.."
    };
    /* Shadow Grandma (Palette 3 — dark hooded cloak, silver bun, piercing lavender eyes!) */
    static const char *const SHADOW_GRAN_ART[24] = {
        "....11111111....",
        "...1225555221...",
        "..122333333221..",
        ".12233222233221.",
        ".12322111122321.",
        ".12321441441231.",
        ".12321111111231.",
        "..123221122321..",
        "..122333333221..",
        ".12223344332221.",
        ".12223344332221.",
        ".12222333322221.",
        ".12222333322221.",
        ".12222233222221.",
        ".12222233222221.",
        ".12222233222221.",
        ".12222222222221.",
        ".12222222222221.",
        ".12222222222221.",
        ".12221222212221.",
        "..122122221221..",
        "..121.1221.121..",
        "...1...11...1...",
        "................"
    };

    const char *const *art = TOWNSFOLK_ART;
    int8_t bob = (frame & 1u) ? 1 : 0;

    hearth_memset(dst384, 0u, 384u);

    if (sprite_id == SPR_NPC_MARA)             art = MARA_ART;
    else if (sprite_id == SPR_NPC_ELDER)       art = ELDER_ART;
    else if (sprite_id == SPR_NPC_CLARA)       art = CLARA_ART;
    else if (sprite_id == SPR_NPC_GUARD)       art = GUARD_ART;
    else if (sprite_id == SPR_NPC_BARNABY)     art = BARNABY_ART;
    else if (sprite_id == SPR_NPC_MAPLE)       art = MAPLE_ART;
    else if (sprite_id == SPR_NPC_JASPER)      art = JASPER_ART;
    else if (sprite_id == SPR_NPC_TESSA)       art = TESSA_ART;
    else if (sprite_id == SPR_NPC_CORIN)       art = CORIN_ART;
    else if (sprite_id == SPR_NPC_SENTINEL)    art = SENTINEL_ART;
    else if (sprite_id == SPR_NPC_SHIPWRIGHT)  art = SHIPWRIGHT_ART;
    else if (sprite_id == SPR_NPC_SHADOW_GRAN) art = SHADOW_GRAN_ART;

    /* Draw base 16x24 sprite (with gentle 1px idle breath on upper torso/head!) */
    h24_blit_pat(dst384, 16u, 24u, art, 24u, 0, 0, 0u);
    if (bob != 0 && sprite_id != SPR_NPC_SHADOW_GRAN) {
        /* Subtle 1px breathing bob on upper 16 rows while keeping boots planted! */
        uint8_t ry;
        uint8_t rx;
        for (ry = 15u; ry > 0u; ry--) {
            for (rx = 0u; rx < 16u; rx++) {
                dst384[(uint16_t)((uint16_t)ry * 16u + rx)] =
                    dst384[(uint16_t)((uint16_t)(ry - 1u) * 16u + rx)];
            }
        }
        for (rx = 0u; rx < 16u; rx++) dst384[rx] = 0u;
    }

    /* Signature hand-held props for key NPCs (fit cleanly inside 16x24!) */
    if (sprite_id == SPR_NPC_GUARD || sprite_id == SPR_NPC_SENTINEL) {
        /* Gleaming vertical halberd / spear along right edge */
        h24_rect(dst384, 16u, 24u, 14, 1, 2, 22, 1u, 0u);
        h24_rect(dst384, 16u, 24u, 14, 2, 2, 4,  4u, 0u);
        h24_rect(dst384, 16u, 24u, 14, 6, 1, 16, 7u, 0u);
    } else if (sprite_id == SPR_NPC_ELDER || sprite_id == SPR_NPC_JASPER) {
        /* Carved wooden walking staff */
        h24_rect(dst384, 16u, 24u, 14, 5, 2, 18, 1u, 0u);
        h24_rect(dst384, 16u, 24u, 14, 6, 1, 16, 12u, 0u);
        h24_px(dst384, 16u, 24u, 14, 5, 7u, 0u);
    } else if (sprite_id == SPR_NPC_CORIN) {
        /* Rolled parchment scroll in hand */
        h24_rect(dst384, 16u, 24u, 13, 11, 3, 6, 1u, 0u);
        h24_rect(dst384, 16u, 24u, 14, 12, 1, 4, 15u, 0u);
    } else if (sprite_id == SPR_NPC_SHIPWRIGHT) {
        /* Carpenter's mallet */
        h24_rect(dst384, 16u, 24u, 13, 9, 3, 9, 1u, 0u);
        h24_rect(dst384, 16u, 24u, 13, 8, 3, 3, 7u, 0u);
        h24_rect(dst384, 16u, 24u, 14, 11, 1, 6, 12u, 0u);
    }
}

#endif /* HEARTH_HERO24_H */
