/* ============================================================================
 * headless_test.c — Zero-Dependency Pure C99 Core Verification Suite
 *
 * Compiles with strict `-std=c99 -Wall -Wextra -Werror -pedantic` using only
 * standard C (no SDL2 required). Verifies RAM footprint for Genesis/SNES,
 * deterministic 16.8 fixed-point physics, Pip trail, cutscene VM, instant
 * Familiar hot-swap, Timed Hit crit/follow-up, Spell Lock break, and Parry.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include "../core/hearth_core.h"

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d)\n", (msg), __LINE__); \
        return 1; \
    } \
} while (0)

static HearthState g_st;

int main(void) {
    printf("=== Project Hearth — Pure C99 Core Verification ===\n");
    printf("sizeof(HearthState)     = %lu bytes (Genesis 64KB RAM / SNES 128KB WRAM budget)\n",
           (unsigned long)sizeof(HearthState));
    printf("sizeof(HearthPpuPacket) = %lu bytes\n",
           (unsigned long)sizeof(HearthPpuPacket));

    /* 1. Verify Retro Console RAM Budget (< 32 KB static state!) */
    CHECK(sizeof(HearthState) < 32768u, "HearthState must fit comfortably inside 64KB Genesis RAM");

    /* 2. Hardware Palette Packer Sanity Checks */
    CHECK(hearth_rgb_to_genesis_bgr333(0xFFFFFFu) == 0x0EEEu, "Genesis BGR333 white pack");
    CHECK(hearth_rgb_to_snes_bgr555(0xFFFFFFu) == 0x7FFFu, "SNES BGR555 white pack");

    /* 3. Init -> Retro Opening Intro -> Title -> Sandbox Transition */
    hearth_init(&g_st);
    CHECK(g_st.ppu.width == 320 && g_st.ppu.height == 224, "hearth_init must populate valid PPU dimensions before frame 1");
    CHECK(g_st.mode == MODE_INTRO, "Initial mode should be MODE_INTRO");
    hearth_step_60hz(&g_st, PAD_START);
    CHECK(g_st.mode == MODE_TITLE, "Pressing PAD_START during intro skips cleanly to MODE_TITLE");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_A);
    CHECK(g_st.mode == MODE_EXPLORE, "Pressing PAD_A on option 0 starts sandbox");

    /* 4. 8-Way Fixed-Point Movement + Pip Breadcrumb Trail */
    fp16_t start_x = g_st.hero_fx;
    fp16_t start_y = g_st.hero_fy;
    fp16_t pip_start_x = g_st.pip_fx;
    for (int f = 0; f < 45; f++) {
        hearth_step_60hz(&g_st, PAD_RIGHT | PAD_B); /* Dash east */
    }
    CHECK(g_st.hero_fx > start_x + TO_FP(40), "Hero should dash smoothly east");
    CHECK(g_st.hero_fy == start_y, "Pure east input keeps Y stable on open plaza");
    CHECK(g_st.pip_fx > pip_start_x + TO_FP(16), "Pip breadcrumb trail should follow Rowan");
    printf("[PASS] 8-way fixed-point movement, dash, and Pip breadcrumb trail\n");

    /* 5. Bytecode Cutscene VM + Heart-Mending Flag Check */
    hearth_start_cutscene(&g_st, CUTSCENE_TAKE_COURAGE);
    CHECK(g_st.mode == MODE_CUTSCENE, "Cutscene VM enters MODE_CUTSCENE");
    for (int f = 0; f < 400 && g_st.mode == MODE_CUTSCENE; f++) {
        uint16_t pad = ((f & 3) == 0) ? PAD_A : 0;
        hearth_step_60hz(&g_st, pad);
    }
    CHECK(g_st.mode == MODE_EXPLORE, "Cutscene VM completes cleanly back to MODE_EXPLORE");
    CHECK(g_st.flag_has_courage, "CUTSCENE_TAKE_COURAGE sets Courage Locket flag");
    printf("[PASS] Bytecode Cutscene VM & Heart-Orb transfer\n");

    /* 6. Shrine Boss Battle: Instant Familiar Hot-Swap (L/R) + Timed Hit + Lock Break */
    hearth_start_battle(&g_st, ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, -1);
    CHECK(g_st.mode == MODE_BATTLE, "Boss battle started");
    CHECK(g_st.battle.is_boss_battle, "Shrine boss flag active");

    /* Wait for BPHASE_INTRO to enter BPHASE_COMMAND */
    for (int f = 0; f < 60 && g_st.battle.phase != BPHASE_COMMAND; f++) {
        hearth_step_60hz(&g_st, 0);
    }
    CHECK(g_st.battle.phase == BPHASE_COMMAND, "Battle reaches BPHASE_COMMAND");

    /* Hot-Swap from Bramblekit (1) to Lumina (2) using PAD_R */
    CHECK(g_st.battle.active_slot == 1, "Default active combatant starts on Bramblekit (slot 1)");
    hearth_step_60hz(&g_st, PAD_R);
    CHECK(g_st.battle.phase == BPHASE_SWAP_ARC, "PAD_R triggers Soul-Arc swap");
    CHECK(g_st.battle.active_slot == 2, "Active combatant switched to Lumina (slot 2)");
    for (int f = 0; f < 30 && g_st.battle.phase != BPHASE_COMMAND; f++) {
        hearth_step_60hz(&g_st, 0);
    }
    CHECK(g_st.battle.phase == BPHASE_COMMAND, "Swap arc returns cleanly to BPHASE_COMMAND (free action)");

    /* Launch Lumina Starlight (LOCK_STAR) and press PAD_A at exact sweet-spot frame! */
    int16_t boss_hp_before = g_st.battle.enemies[0].hp;
    hearth_step_60hz(&g_st, PAD_A); /* Select STRIKE */
    bool pressed_timed = false;
    for (int f = 0; f < 220; f++) {
        uint16_t pad = 0;
        if (!pressed_timed && g_st.battle.ring.active &&
            g_st.battle.ring.progress_fp >= g_st.battle.ring.sweet_start_fp) {
            pad = PAD_A;
            pressed_timed = true;
        }
        hearth_step_60hz(&g_st, pad);
        if (g_st.battle.phase == BPHASE_ENEMY_SELECT) break;
    }
    CHECK(pressed_timed, "Timed-hit ring activated during player lunge");
    CHECK(g_st.battle.combos >= 1, "Perfect timed hit recorded combo");
    CHECK(g_st.battle.locks_broken >= 1, "Lumina Starlight shattered Briar Warden Star Spell Lock");
    CHECK(g_st.battle.enemies[0].hp < boss_hp_before, "Boss took damage from timed strike + follow-up");
    printf("[PASS] Instant Familiar Hot-Swap, Timed-Hit Critical + Follow-up, & Spell Lock Break\n");

    /* 7. Boss Turn 1: Briar Warden charges Bramble Tempest (charge_turns 2 -> 1) */
    for (int f = 0; f < 60 && g_st.battle.phase != BPHASE_COMMAND; f++) {
        hearth_step_60hz(&g_st, 0);
    }
    CHECK(g_st.battle.phase == BPHASE_COMMAND, "Turn 1 charge loops cleanly back to BPHASE_COMMAND");
    CHECK(g_st.battle.enemies[0].charge_turns == 1, "Boss charge_turns decremented from 2 to 1");

    /* Turn 2: Select GUARD (RIGHT, RIGHT, A) then land Perfect Timed Parry on Briar Warden's attack! */
    hearth_step_60hz(&g_st, PAD_RIGHT);
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_RIGHT);
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_A); /* Execute GUARD -> triggers enemy attack */

    bool pressed_parry = false;
    for (int f = 0; f < 240; f++) {
        uint16_t pad = 0;
        if (!pressed_parry && g_st.battle.ring.active && g_st.battle.ring.is_parry &&
            g_st.battle.ring.progress_fp >= g_st.battle.ring.sweet_start_fp) {
            pad = PAD_A;
            pressed_parry = true;
        }
        hearth_step_60hz(&g_st, pad);
        if (g_st.battle.phase == BPHASE_COMMAND) break;
    }
    CHECK(pressed_parry, "Parry timing ring activated during enemy attack");
    CHECK(g_st.battle.parries >= 1, "Perfect parry registered");
    CHECK(g_st.battle.phase == BPHASE_COMMAND, "Turn looped cleanly back to player command");
    printf("[PASS] Boss Spell Charge, Guard, & Perfect Timed Parry\n");

    /* 8. PPU Mode Toggle (320x224 Genesis <-> 256x224 SNES) */
    hearth_toggle_ppu_mode(&g_st);
    const HearthPpuPacket *pkt = hearth_step_60hz(&g_st, 0);
    CHECK(pkt->width == 256 && pkt->height == 224, "PPU toggled to 256x224 SNES mode");
    CHECK(pkt->oam_count > 0 && pkt->oam_count <= pkt->oam_limit, "OAM packet count valid");
    printf("[PASS] Dual-Resolution PPU Packet (Genesis 320x224 / SNES 256x224)\n");

    printf("ALL PURE C99 CORE TESTS PASSED!\n");
    return 0;
}
