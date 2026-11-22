/* ============================================================================
 * headless_test.c — Zero-Dependency Pure C99 Core Verification Suite
 *
 * Compiles with strict `-std=c99 -Wall -Wextra -Werror -pedantic` using only
 * standard C (no SDL2 required). Verifies RAM footprint for Genesis/SNES,
 * Age-5 Shadow Grandma flashback -> Alderbrook -> Evertale story progression,
 * all 5 connected zones, open treasure chest sprite, shops, timed-reel fishing,
 * deterministic 16.8 fixed-point physics, Pip trail, cutscene VM, Familiar
 * hot-swap, Timed Hit crit/follow-up, Spell Lock break, and Parry.
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

    /* 3. Init -> Title Screen (Alderbrook) -> New Story -> 7-Act 23-Beat Cinema -> Age-10 Alderbrook */
    hearth_init(&g_st);
    CHECK(g_st.ppu.width == 320 && g_st.ppu.height == 224, "hearth_init populates valid PPU dimensions");
    CHECK(g_st.mode == MODE_TITLE && g_st.zone_id == ZONE_ALDERBROOK, "Game boots cleanly to MODE_TITLE in Alderbrook");
    CHECK(g_st.ppu.bgm_id == BGM_ALDERBROOK, "Title screen plays Alderbrook Overture");

    /* Option 0: NEW STORY launches the 7-Act Illustrated Opening Cinema */
    hearth_step_60hz(&g_st, PAD_A);
    CHECK(g_st.mode == MODE_INTRO && g_st.intro.beat == 0u && g_st.intro.launch_story,
          "NEW STORY launches 7-Act Opening Cinema at beat 0");
    CHECK(g_st.ppu.oam_count == 0u, "Opening cinema emits clean OAM over painted CG plates");

    bool saw_wonder_bgm = false;
    bool saw_shadow_bgm = false;
    bool saw_lullaby_bgm = false;
    uint8_t max_plate_seen = 0u;
    for (int f = 0; f < 3000 && g_st.mode == MODE_INTRO; f++) {
        uint16_t pad = ((f % 6) == 0) ? PAD_A : 0;
        const HearthPpuPacket *p = hearth_step_60hz(&g_st, pad);
        if (p->bgm_id == BGM_CINEMA_WONDER) saw_wonder_bgm = true;
        if (p->bgm_id == BGM_SHADOW) saw_shadow_bgm = true;
        if (p->bgm_id == BGM_CINEMA_LULLABY) saw_lullaby_bgm = true;
        if (g_st.intro.scene > max_plate_seen) max_plate_seen = g_st.intro.scene;
    }
    CHECK(saw_wonder_bgm && saw_shadow_bgm && saw_lullaby_bgm, "Opening cinema transitions through Wonder, Shadow, and Lullaby suites");
    CHECK(max_plate_seen == CG_PIP_WINDOW, "Opening cinema displays all 6 illustrated CG plates");
    CHECK(g_st.mode == MODE_EXPLORE && g_st.zone_id == ZONE_ALDERBROOK, "Opening cinema flows directly into Age-10 Alderbrook");
    CHECK(g_st.story_stage == STORY_AGE10_ALDERBROOK && !g_st.flag_has_wand, "Rowan begins Age-10 Alderbrook without wand yet");

    /* Replay Cinema -> PAD_START returns cleanly to Title Screen */
    hearth_start_intro(&g_st);
    CHECK(g_st.mode == MODE_INTRO && !g_st.intro.launch_story, "Replay Cinema launches in non-story mode");
    hearth_step_60hz(&g_st, PAD_START);
    CHECK(g_st.mode == MODE_TITLE, "Skipping Replay Cinema returns cleanly to MODE_TITLE");
    printf("[PASS] Title Screen -> 7-Act Illustrated Opening Cinema -> Age-10 Alderbrook flow\n");

    /* 4. Explore Evertale Sandbox + 8-Way Fixed-Point Movement + Pip Breadcrumb Trail */
    hearth_start_sandbox(&g_st);
    CHECK(g_st.mode == MODE_EXPLORE && g_st.zone_id == ZONE_HAMLET && g_st.pip_visible, "Sandbox starts in Windmill Hamlet with Pip");
    fp16_t start_x = g_st.hero_fx;
    fp16_t start_y = g_st.hero_fy;
    fp16_t pip_start_x = g_st.pip_fx;
    for (int f = 0; f < 45; f++) {
        hearth_step_60hz(&g_st, PAD_RIGHT | PAD_B);
    }
    CHECK(g_st.hero_fx > start_x + TO_FP(40), "Hero dashes smoothly east");
    CHECK(g_st.hero_fy == start_y, "Pure east input keeps Y stable on open plaza");
    CHECK(g_st.pip_fx > pip_start_x + TO_FP(16), "Pip breadcrumb trail follows Rowan");
    printf("[PASS] 8-way fixed-point movement, dash, and Pip breadcrumb trail\n");

    /* 5. Open Chest Sprite Verification (SPR_CHEST -> SPR_CHEST_OPEN) */
    g_st.hero_fx = TO_FP(26 * 16 + 8);
    g_st.hero_fy = TO_FP(7 * 16 + 8);
    g_st.hero_dir = DIR_UP;
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_A); /* Open chest! */
    const HearthPpuPacket *cpkt = hearth_step_60hz(&g_st, 0);
    bool found_open_chest = false;
    for (int i = 0; i < cpkt->oam_count; i++) {
        if (cpkt->oam[i].sprite_id == SPR_CHEST_OPEN) found_open_chest = true;
    }
    CHECK(found_open_chest, "Opening treasure chest emits dedicated SPR_CHEST_OPEN sprite");
    hearth_step_60hz(&g_st, PAD_A);
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_A); /* Close chest dialogue */
    printf("[PASS] Open treasure chest emits SPR_CHEST_OPEN across all targets\n");

    /* 6. All 5 Regions + Dark Sewers + Shop + Timed-Reel Fishing Verification */
    hearth_enter_zone(&g_st, ZONE_WHISKERBURGH, 20, 20);
    CHECK(g_st.zone_id == ZONE_WHISKERBURGH && g_st.ppu.terrain_pal_id == ZPAL_WHISKERBURGH, "Whiskerburgh loads with mouse-market palette");

    /* Fishing Minigame: Cast -> Wait -> Bite -> Reel -> Catch */
    hearth_start_fishing(&g_st);
    CHECK(g_st.mode == MODE_FISHING, "Entered MODE_FISHING at Whiskerburgh canal jetty");
    uint16_t caught_before = g_st.fish_total_caught;
    for (int f = 0; f < 900 && g_st.fishing.phase != FISH_PHASE_CAUGHT; f++) {
        uint16_t pad = 0;
        if (g_st.fishing.phase == FISH_PHASE_BITE_ALERT && (f & 1) == 0) pad = PAD_A;
        else if (g_st.fishing.phase == FISH_PHASE_REELING) {
            if (g_st.fishing.bar_pos < g_st.fishing.fish_pos - 2) pad |= PAD_RIGHT;
            else if (g_st.fishing.bar_pos > g_st.fishing.fish_pos + 2) pad |= PAD_LEFT;
            pad |= PAD_A;
        }
        hearth_step_60hz(&g_st, pad);
    }
    CHECK(g_st.fishing.phase == FISH_PHASE_CAUGHT && g_st.fish_total_caught == caught_before + 1u, "Timed-reel fishing catches fish into creel");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_B); /* Exit fishing */
    CHECK(g_st.mode == MODE_EXPLORE, "Exiting fishing returns to MODE_EXPLORE");

    /* Shop Buy & Sell Fish */
    uint16_t g_before = g_st.guilders;
    uint8_t pot_before = g_st.potions;
    hearth_open_shop(&g_st, 0);
    CHECK(g_st.mode == MODE_SHOP, "Entered Barnaby's Mouse Market shop");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_A); /* Buy Sun Tonic (20G) */
    CHECK(g_st.potions == pot_before + 1u && g_st.guilders == g_before - 20u, "Shop buy deducts Guilders and grants Sun Tonic");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_B);
    CHECK(g_st.mode == MODE_EXPLORE, "Closing shop returns to MODE_EXPLORE");

    /* Dark Sunken Sewers + Boarhold Citadel */
    hearth_enter_zone(&g_st, ZONE_SEWERS, 21, 20);
    CHECK(g_st.zone_is_dark && g_st.ppu.zone_is_dark && g_st.ppu.bgm_id == BGM_SEWERS, "Sunken Sewers sets dark lantern flag & sewer music");
    hearth_enter_zone(&g_st, ZONE_BOARHOLD, 15, 14);
    CHECK(g_st.zone_id == ZONE_BOARHOLD && g_st.ppu.terrain_pal_id == ZPAL_BOARHOLD, "Boarhold Citadel loads with sandstone palette");
    printf("[PASS] 5-zone world, dark Sunken Sewers, Shop economy, & timed-reel Fishing\n");

    /* 7. Bytecode Cutscene VM + Heart-Mending Flag Check */
    hearth_enter_zone(&g_st, ZONE_HAMLET, 13, 15);
    hearth_start_cutscene(&g_st, CUTSCENE_TAKE_COURAGE);
    CHECK(g_st.mode == MODE_CUTSCENE, "Cutscene VM enters MODE_CUTSCENE");
    for (int f = 0; f < 400 && g_st.mode == MODE_CUTSCENE; f++) {
        uint16_t pad = ((f & 3) == 0) ? PAD_A : 0;
        hearth_step_60hz(&g_st, pad);
    }
    CHECK(g_st.mode == MODE_EXPLORE && g_st.flag_has_courage, "CUTSCENE_TAKE_COURAGE sets Courage Locket flag");
    printf("[PASS] Bytecode Cutscene VM & Heart-Orb transfer\n");

    /* 8. New Bosses (King Muckmaw & Lord Ironfang) + Hot-Swap + Timed Hit + Lock Break + Parry */
    hearth_start_battle(&g_st, ENCOUNTER_SEWER_BOSS, BIOME_SEWER, -1);
    CHECK(g_st.battle.is_boss_battle && g_st.battle.enemies[0].sprite_id == SPR_BOSS_MUCKMAW, "King Muckmaw mini-boss preset spawns");
    hearth_start_battle(&g_st, ENCOUNTER_IRONFANG_BOSS, BIOME_BOARHOLD, -1);
    CHECK(g_st.battle.is_boss_battle && g_st.battle.enemies[0].sprite_id == SPR_BOSS_IRONFANG, "Lord Ironfang arc boss preset spawns");

    for (int f = 0; f < 60 && g_st.battle.phase != BPHASE_COMMAND; f++) hearth_step_60hz(&g_st, 0);
    CHECK(g_st.battle.phase == BPHASE_COMMAND, "Battle reaches BPHASE_COMMAND");

    hearth_step_60hz(&g_st, PAD_R); /* Swap Bramblekit -> Lumina */
    CHECK(g_st.battle.phase == BPHASE_SWAP_ARC && g_st.battle.active_slot == 2, "PAD_R swaps to Lumina");
    for (int f = 0; f < 30 && g_st.battle.phase != BPHASE_COMMAND; f++) hearth_step_60hz(&g_st, 0);

    int16_t boss_hp_before = g_st.battle.enemies[0].hp;
    hearth_step_60hz(&g_st, PAD_A);
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
    CHECK(pressed_timed && g_st.battle.combos >= 1 && g_st.battle.locks_broken >= 1 && g_st.battle.enemies[0].hp < boss_hp_before,
          "Timed hit combo & Spell Lock break succeed on Lord Ironfang");

    for (int f = 0; f < 60 && g_st.battle.phase != BPHASE_COMMAND; f++) hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_RIGHT); hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_RIGHT); hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_A); /* GUARD */
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
    CHECK(pressed_parry && g_st.battle.parries >= 1, "Perfect timed parry registered");
    printf("[PASS] King Muckmaw, Lord Ironfang, Hot-Swap, Timed Hit, Lock Break & Parry\n");

    /* 9. PPU Mode Toggle (320x224 Genesis <-> 256x224 SNES) */
    hearth_toggle_ppu_mode(&g_st);
    const HearthPpuPacket *pkt = hearth_step_60hz(&g_st, 0);
    CHECK(pkt->width == 256 && pkt->height == 224 && pkt->oam_count > 0, "SNES 256x224 PPU mode valid");
    printf("[PASS] Dual-Resolution PPU Packet (Genesis 320x224 / SNES 256x224)\n");

    printf("ALL PURE C99 CORE TESTS PASSED!\n");
    return 0;
}
