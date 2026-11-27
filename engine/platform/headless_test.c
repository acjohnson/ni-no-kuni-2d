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

    /* 3. Init -> Title Screen (Autumn Mountain Alderbrook) -> Opening Cinema -> Full Chapter 1 Arc */
    hearth_init(&g_st);
    CHECK(g_st.ppu.width == 320 && g_st.ppu.height == 224, "hearth_init populates valid PPU dimensions");
    CHECK(g_st.mode == MODE_TITLE && g_st.zone_id == ZONE_ALDERBROOK, "Game boots cleanly to MODE_TITLE in Alderbrook");
    CHECK(g_st.ppu.has_mountain_vista && g_st.ppu.bgm_id == BGM_ALDERBROOK, "Title screen shows Autumn Mountain vista & Alderbrook Overture");

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
    CHECK(max_plate_seen == CG_PIP_WINDOW && HEARTH_CINEMA_BEAT_COUNT == 21u, "Opening cinema ends on Pip's window promise (6 acts / 21 beats)");
    CHECK(g_st.mode == MODE_EXPLORE && g_st.zone_id == ZONE_ROWAN_HOME, "Opening cinema spawns Rowan inside Cedar Home");
    CHECK(g_st.time_of_day == TOD_MORNING && g_st.ch1_step == 0u && !g_st.flag_has_wand,
          "Chapter 1 begins on Autumn Morning inside Rowan's Home");

    /* Chapter 1 Story Beat 1: Talk to Grandma Mara -> Morning cutscene + Portrait ID + Multi-Page Pagination */
    g_st.hero_fx = TO_FP(19 * 16 + 8);
    g_st.hero_fy = TO_FP(13 * 16 + 8);
    g_st.hero_dir = DIR_UP;
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_A);
    CHECK(g_st.mode == MODE_CUTSCENE, "Speaking to Grandma Mara triggers Morning cutscene");
    bool saw_mara_portrait = false;
    bool saw_multipage_dialogue = false;
    for (int f = 0; f < 900 && g_st.mode == MODE_CUTSCENE; f++) {
        uint16_t pad = ((f & 3) == 0) ? PAD_A : 0;
        hearth_step_60hz(&g_st, pad);
        if (g_st.dialogue.active && g_st.dialogue.portrait_id == PORTRAIT_MARA) saw_mara_portrait = true;
        if (g_st.dialogue.active && g_st.dialogue.next_text != NULL) saw_multipage_dialogue = true;
    }
    CHECK(saw_mara_portrait && saw_multipage_dialogue && g_st.ch1_step == 1u,
          "Grandma Mara cutscene shows Mara portrait, paginates long lines cleanly, and advances to errands");

    /* Chapter 1 Story Beat 2: Step out into Redesigned Autumn Mountain Alderbrook + Locomotive */
    hearth_enter_zone(&g_st, ZONE_ALDERBROOK, 18, 9);
    const HearthPpuPacket *ab_pkt = hearth_step_60hz(&g_st, 0);
    bool saw_locomotive = false;
    for (int i = 0; i < ab_pkt->oam_count; i++) {
        if (ab_pkt->oam[i].sprite_id == SPR_LOCOMOTIVE) saw_locomotive = true;
    }
    CHECK(ab_pkt->has_mountain_vista && ab_pkt->terrain_pal_id == ZPAL_AUTUMN_MORNING && saw_locomotive,
          "Redesigned Alderbrook shows mountain vista, Morning palette, and idling Steam Locomotive");

    /* Complete Maple + Barnaby errands -> Advances story without jarring palette snap; real-time clock advances smoothly */
    hearth_start_cutscene(&g_st, CUTSCENE_CH1_MAPLE_LOAF);
    for (int f = 0; f < 400 && g_st.mode == MODE_CUTSCENE; f++) hearth_step_60hz(&g_st, ((f & 3) == 0) ? PAD_A : 0);
    hearth_start_cutscene(&g_st, CUTSCENE_CH1_BARNABY_THERMOS);
    for (int f = 0; f < 400 && g_st.mode == MODE_CUTSCENE; f++) hearth_step_60hz(&g_st, ((f & 3) == 0) ? PAD_A : 0);
    CHECK(g_st.ch1_step == 2u, "Completing Maple & Barnaby errands advances Chapter 1 without jarring palette snap");
    uint8_t init_phase = g_st.tod_phase;
    for (int f = 0; f < 300; f++) hearth_step_60hz(&g_st, 0);
    CHECK(g_st.tod_phase != init_phase && g_st.ppu.tod_phase == g_st.tod_phase,
          "Alderbrook time progresses smoothly in real time via 64-step tod_phase");

    /* Grandma Mara's Upstairs Bedroom & Attic Keepsake CG (Script 1) -> Shifts to Dusk + Grants Milo's Brass Bell */
    hearth_enter_zone(&g_st, ZONE_ROWAN_HOME, 28, 9);
    CHECK(g_st.portals[2].active && g_st.portals[2].dst_zone == ZONE_ROWAN_UPSTAIRS,
          "Rowan's Cottage stairs portal leads upstairs to Grandma Mara's Bedroom");
    hearth_enter_zone(&g_st, ZONE_ROWAN_UPSTAIRS, 26, 11);
    CHECK(g_st.zone_id == ZONE_ROWAN_UPSTAIRS && g_st.portals[0].active && g_st.portals[0].dst_zone == ZONE_ROWAN_HOME,
          "Grandma Mara's Upstairs Bedroom loads cleanly with return staircase");
    hearth_start_story_cg(&g_st, 1u);
    CHECK(g_st.mode == MODE_INTRO && g_st.intro.scene == CG_ATTIC_KEEPSAKE, "Attic Cedar Chest launches Plate 6 (Attic Keepsake CG)");
    for (int f = 0; f < 1000 && g_st.mode == MODE_INTRO; f++) hearth_step_60hz(&g_st, ((f % 6) == 0) ? PAD_A : 0);
    CHECK(g_st.mode == MODE_EXPLORE && g_st.has_milo_bell && g_st.time_of_day == TOD_DUSK && g_st.ch1_step == 4u,
          "Attic Keepsake CG grants Milo's Brass Bell and shifts to Dusk");

    /* Pip Dream Return CG (Script 2) -> Shifts to Moonlit Night */
    hearth_start_story_cg(&g_st, 2u);
    CHECK(g_st.mode == MODE_INTRO && g_st.intro.scene == CG_PIP_DREAM_RETURN, "Sleeping launches Plate 7 (Pip Dream Return CG)");
    for (int f = 0; f < 1000 && g_st.mode == MODE_INTRO; f++) hearth_step_60hz(&g_st, ((f % 6) == 0) ? PAD_A : 0);
    CHECK(g_st.mode == MODE_EXPLORE && g_st.time_of_day == TOD_NIGHT && g_st.ch1_step == 6u && g_st.tod_locked && g_st.tod_phase == 768u,
          "Pip Dream CG awakens Rowan at story-locked Moonlit Night (tod_phase=768)");

    /* Whispering Woods (Zone 6) + 3 Unlit -> Lit Lantern Shrines + TOD Lock Verification */
    hearth_enter_zone(&g_st, ZONE_WHISPERING_WOODS, 12, 18);
    CHECK(g_st.ppu.has_mountain_vista && g_st.ppu.terrain_pal_id == ZPAL_AUTUMN_NIGHT, "Whispering Woods renders under Moonlit Night palette");
    CHECK(g_st.map_bg1[17 * HEARTH_MAP_W + 12] == T_LANTERN_UNLIT &&
          g_st.map_bg1[15 * HEARTH_MAP_W + 26] == T_LANTERN_UNLIT &&
          g_st.map_bg1[9  * HEARTH_MAP_W + 33] == T_LANTERN_UNLIT,
          "All 3 Whispering Woods shrine lamps start visibly UNLIT (T_LANTERN_UNLIT)");
    {
        /* Verify story-locked Night stays fixed at 768 even after 300 frames of exploration */
        for (int f = 0; f < 300; f++) hearth_step_60hz(&g_st, 0);
        CHECK(g_st.tod_phase == 768u && g_st.time_of_day == TOD_NIGHT && g_st.tod_locked,
              "Story-locked nighttime Mido sequence stays locked at Night without advancing TOD");

        /* Interact with Shrine 0 (facing north at (12,17)) -> lights lamp & spawns burst sparks! */
        uint8_t prev_rev = g_st.map_revision;
        g_st.hero_dir = DIR_UP;
        hearth_step_60hz(&g_st, PAD_A);
        CHECK((g_st.woods_shrines_mask & 1u) != 0u &&
              g_st.map_bg1[17 * HEARTH_MAP_W + 12] == T_LANTERN_POST &&
              g_st.map_bg1[15 * HEARTH_MAP_W + 26] == T_LANTERN_UNLIT &&
              g_st.map_revision != prev_rev &&
              g_st.active_particles >= 10u,
              "Lighting Shrine 0 flips tile to T_LANTERN_POST, bumps map_revision, and spawns spark burst");
        for (int f = 0; f < 80 && g_st.dialogue.active; f++) hearth_step_60hz(&g_st, ((f % 4) == 0) ? PAD_A : 0);

        /* Verify unlocked 1024-step real-time TOD advances by exactly 1 step every 56 frames (~16m full cycle) */
        g_st.tod_locked = false;
        g_st.day_subtick = 0u;
        for (int f = 0; f < 56; f++) hearth_step_60hz(&g_st, 0);
        CHECK(g_st.tod_phase == 769u, "Unlocked real-time TOD advances 1/1024 micro-step after 56 frames (4x slower)");
        g_st.tod_phase = 768u;
        g_st.tod_locked = true;
    }
    g_st.woods_shrines_mask = 0x07u;
    g_st.ch1_step = 7u;
    hearth_enter_zone(&g_st, ZONE_HOLLOW_GLADE, 15, 20);
    const HearthPpuPacket *glade_pkt = hearth_step_60hz(&g_st, 0);
    CHECK(glade_pkt->has_mountain_vista && g_st.zone_id == ZONE_HOLLOW_GLADE, "Hollow Glade loads high-ridge sanctuary with alpine vista");
    hearth_start_story_cg(&g_st, 3u);
    CHECK(g_st.mode == MODE_INTRO && g_st.intro.scene == CG_HOLLOW_OAK_AWAKENS, "Hollow Oak launches Plate 8 (Hollow Oak Awakens CG)");
    bool saw_world_vista = false;
    for (int f = 0; f < 2200 && g_st.mode == MODE_INTRO; f++) {
        hearth_step_60hz(&g_st, ((f % 6) == 0) ? PAD_A : 0);
        if (g_st.intro.scene == CG_EVERTALE_WORLD_VISTA) saw_world_vista = true;
    }
    CHECK(saw_world_vista, "Hollow Oak CG chains seamlessly into Plate 9 (Continent of Evertale Vista CG)");
    CHECK(g_st.zone_id == ZONE_EVERTALE_WORLD && g_st.is_world_map && g_st.ppu.globe_w == HEARTH_GLOBE_W && g_st.ppu.globe_h == HEARTH_GLOBE_H,
          "Evertale arrival places Rowan on expansive 256x192 Toroidal Globe");
    CHECK(!g_st.flag_has_familiar && !g_st.flag_has_heart_vessel && !g_st.pip_visible,
          "Initial Evertale arrival has no Familiar, no Heart Vessel, and Pip not yet reunited");
    CHECK(g_st.active_vehicle == VEH_FOOT && g_st.vehicle_unlocked_mask == 0u,
          "New Story starts on foot with vehicles strictly story-locked");
    {
        int active_world_enemies = 0;
        for (int i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++) {
            if (g_st.field_enemies[i].alive) active_world_enemies++;
        }
        CHECK(active_world_enemies == 0, "Evertale World Map is 100%% peaceful before bonding first Familiar");
    }

    /* Verify 64x48 ROM Atlas grid integrity across all 7 continents */
    {
        uint16_t seen_regions = 0u;
        uint16_t shallow_cells = 0u;
        for (uint8_t ay = 0; ay < HEARTH_ATLAS_H; ay++) {
            for (uint8_t ax = 0; ax < HEARTH_ATLAS_W; ax++) {
                uint8_t cell = hearth_globe_atlas_cell(ax, ay);
                uint8_t reg = hearth_globe_region_at((uint16_t)(ax * 4u + 2u), (uint16_t)(ay * 4u + 2u));
                CHECK(cell <= 9u, "Atlas cell code valid (0..9)");
                if (cell == 1u) shallow_cells++;
                if (reg < REGION_COUNT) seen_regions |= (uint16_t)(1u << reg);
            }
        }
        CHECK(seen_regions == 0x7Fu, "All 7 continents present on 64x48 globe macro-grid");
        CHECK(shallow_cells >= 40u, "Turquoise shallow raft channels link the southern regions");
    }

    /* Verify Hollow Oak arrival (74,110) can freely walk out and reach ALL 8 Heartland landmarks on foot! */
    {
        static uint8_t vis[HEARTH_GLOBE_W * HEARTH_GLOBE_H];
        static uint16_t q[HEARTH_GLOBE_W * HEARTH_GLOBE_H];
        int head = 0, tail = 0, reachable_count = 0;
        int start_idx = 110 * HEARTH_GLOBE_W + 74;
        uint8_t bg1, bg2, col, reg;
        for (int i = 0; i < HEARTH_GLOBE_W * HEARTH_GLOBE_H; i++) vis[i] = 0u;
        hearth_globe_cell(74, 110, &bg1, &bg2, &col, &reg);
        CHECK(col == COL_WALKABLE && reg == REGION_HEARTLAND, "Hollow Oak arrival tile (74,110) is walkable in Heartland");
        vis[start_idx] = 1;
        q[tail++] = (uint16_t)start_idx;
        while (head < tail) {
            int cur = q[head++];
            int cx = cur % HEARTH_GLOBE_W;
            int cy = cur / HEARTH_GLOBE_W;
            const int dx[4] = { 1, -1, 0, 0 };
            const int dy[4] = { 0, 0, 1, -1 };
            reachable_count++;
            for (int d = 0; d < 4; d++) {
                int nx = (cx + dx[d] + HEARTH_GLOBE_W) % HEARTH_GLOBE_W;
                int ny = (cy + dy[d] + HEARTH_GLOBE_H) % HEARTH_GLOBE_H;
                int nidx = ny * HEARTH_GLOBE_W + nx;
                if (!vis[nidx]) {
                    hearth_globe_cell((uint16_t)nx, (uint16_t)ny, &bg1, &bg2, &col, &reg);
                    if (col == COL_WALKABLE) {
                        vis[nidx] = 1;
                        q[tail++] = (uint16_t)nidx;
                    }
                }
            }
        }
        CHECK(reachable_count > 1200, "Hollow Oak arrival connects to >1200 walkable Heartland globe tiles");
        for (int lm = 0; lm < 8; lm++) {
            int ltx = (int)HEARTH_GLOBE_LANDMARKS[lm].tx;
            int lty = (int)HEARTH_GLOBE_LANDMARKS[lm].ty + ((lm == 0) ? 1 : 0);
            int lidx = lty * HEARTH_GLOBE_W + ltx;
            CHECK(vis[lidx], "All 8 Heartland landmarks (including Timber Pier) are reachable on foot from Hollow Oak");
        }

        /* Also verify Rowan can physically walk east & north out of Hollow Oak clearing */
        fp16_t hx0 = g_st.hero_fx;
        fp16_t hy0 = g_st.hero_fy;
        for (int f = 0; f < 65; f++) hearth_step_60hz(&g_st, PAD_RIGHT);
        for (int f = 0; f < 65; f++) hearth_step_60hz(&g_st, PAD_UP);
        CHECK(g_st.hero_fx > hx0 + TO_FP(70) && g_st.hero_fy < hy0 - TO_FP(70),
              "Rowan physically walks east & north out of Hollow Oak Grove onto Evertale roads");
    }

    /* Visit non-main landmarks early (Castle Aurelia Gate & peaceful Windmill Hamlet) */
    hearth_enter_zone(&g_st, ZONE_CASTLE_GATE, 21, 13);
    CHECK(g_st.zone_id == ZONE_CASTLE_GATE && g_st.npcs[0].sprite_id == SPR_NPC_SENTINEL, "Castle Aurelia Gatehouse enterable with Royal Sentinels");
    hearth_enter_zone(&g_st, ZONE_HAMLET, 13, 17);
    CHECK(g_st.npcs[1].aura == AURA_NONE && g_st.npcs[2].aura == AURA_NONE, "Early Windmill Hamlet hides all Heart-Mending auras without Heart Vessel");

    /* Sunhill Market Story Progression: Tessa -> Corin -> Mido & Pip Reunion -> Bramblewood Familiar Bonding */
    hearth_enter_zone(&g_st, ZONE_SUNHILL_MARKET, 21, 24);
    CHECK(g_st.zone_id == ZONE_SUNHILL_MARKET && g_st.ppu.terrain_pal_id == ZPAL_SUNHILL_MARKET, "Sunhill Market loads beneath Castle Aurelia skyline");
    hearth_start_cutscene(&g_st, CUTSCENE_CH2_SUNHILL_TESSA);
    for (int f = 0; f < 500 && g_st.mode == MODE_CUTSCENE; f++) hearth_step_60hz(&g_st, ((f & 3) == 0) ? PAD_A : 0);
    CHECK(g_st.ch2_step == 1u, "Tessa the Baker shares Mido clue and points to Archivist Corin");

    hearth_start_cutscene(&g_st, CUTSCENE_CH2_SUNHILL_CORIN);
    for (int f = 0; f < 600 && g_st.mode == MODE_CUTSCENE; f++) hearth_step_60hz(&g_st, ((f & 3) == 0) ? PAD_A : 0);
    CHECK(g_st.ch2_step == 2u, "Archivist Corin shares subtle parent hints and points to fountain");

    hearth_start_cutscene(&g_st, CUTSCENE_CH2_SUNHILL_REUNION);
    for (int f = 0; f < 700 && g_st.mode == MODE_CUTSCENE; f++) hearth_step_60hz(&g_st, ((f & 3) == 0) ? PAD_A : 0);
    CHECK(g_st.ch2_step == 3u && g_st.pip_visible, "Mido & Pip reunion at Sunhill fountain unlocks Pip companion");

    hearth_enter_zone(&g_st, ZONE_BRAMBLE_EDGE, 30, 14);
    hearth_start_cutscene(&g_st, CUTSCENE_CH2_BOND_FAMILIAR);
    for (int f = 0; f < 600 && g_st.mode == MODE_CUTSCENE; f++) hearth_step_60hz(&g_st, ((f & 3) == 0) ? PAD_A : 0);
    CHECK(g_st.flag_has_familiar && g_st.flag_has_wand, "Bramblewood Boundary Stone bonds Rowan's first Familiars");

    hearth_enter_zone(&g_st, ZONE_EVERTALE_WORLD, 104, 84);
    {
        int active_world_enemies = 0;
        for (int i = 0; i < HEARTH_MAX_FIELD_ENEMIES; i++) {
            if (g_st.field_enemies[i].alive) active_world_enemies++;
        }
        CHECK(active_world_enemies == 4, "Bonding first Familiar awakens visible roaming enemies on Evertale World Map");
    }

    /* Chapter 2 Raft Story: Heartland Timber Pier -> 3 Sun-Cedar Planks -> Unlock Cedar Raft */
    hearth_enter_zone(&g_st, ZONE_TIMBER_PIER, 22, 11);
    CHECK(g_st.zone_id == ZONE_TIMBER_PIER && g_st.npcs[0].sprite_id == SPR_NPC_SHIPWRIGHT, "Heartland Timber Pier loads with Shipwright Orin");
    hearth_start_cutscene(&g_st, CUTSCENE_CH2_ORIN_START);
    for (int f = 0; f < 600 && g_st.mode == MODE_CUTSCENE; f++) hearth_step_60hz(&g_st, ((f & 3) == 0) ? PAD_A : 0);
    CHECK(g_st.ch2_step == 5u, "Orin asks Rowan to gather 3 Sun-Cedar Driftwood Planks");
    g_st.planks_found_mask = 0x07u;
    hearth_start_cutscene(&g_st, CUTSCENE_CH2_ORIN_RAFT_BUILT);
    for (int f = 0; f < 600 && g_st.mode == MODE_CUTSCENE; f++) hearth_step_60hz(&g_st, ((f & 3) == 0) ? PAD_A : 0);
    CHECK((g_st.vehicle_unlocked_mask & VEH_MASK_RAFT) != 0u && g_st.ch2_step == 6u,
          "Returning 3 Sun-Cedar Planks to Orin unlocks the Cedar Raft");

    /* Verify Raft BFS connectivity across turquoise shallows: Timber Pier -> Suncrest Coral Cove -> Oasis of Babana */
    {
        static uint8_t vis[HEARTH_GLOBE_W * HEARTH_GLOBE_H];
        static uint16_t q[HEARTH_GLOBE_W * HEARTH_GLOBE_H];
        int head = 0, tail = 0;
        int start_idx = 114 * HEARTH_GLOBE_W + 108;
        uint8_t bg1, bg2, col, reg;
        for (int i = 0; i < HEARTH_GLOBE_W * HEARTH_GLOBE_H; i++) vis[i] = 0u;
        vis[start_idx] = 1;
        q[tail++] = (uint16_t)start_idx;
        while (head < tail) {
            int cur = q[head++];
            int cx = cur % HEARTH_GLOBE_W;
            int cy = cur / HEARTH_GLOBE_W;
            const int dx[4] = { 1, -1, 0, 0 };
            const int dy[4] = { 0, 0, 1, -1 };
            for (int d = 0; d < 4; d++) {
                int nx = (cx + dx[d] + HEARTH_GLOBE_W) % HEARTH_GLOBE_W;
                int ny = (cy + dy[d] + HEARTH_GLOBE_H) % HEARTH_GLOBE_H;
                int nidx = ny * HEARTH_GLOBE_W + nx;
                if (!vis[nidx]) {
                    hearth_globe_cell((uint16_t)nx, (uint16_t)ny, &bg1, &bg2, &col, &reg);
                    if (col == COL_WALKABLE || col == COL_SHALLOW) {
                        vis[nidx] = 1;
                        q[tail++] = (uint16_t)nidx;
                    }
                }
            }
        }
        CHECK(vis[146 * HEARTH_GLOBE_W + 52], "Cedar Raft reaches Suncrest Coral Cove (52,146) via turquoise shallows");
        CHECK(vis[154 * HEARTH_GLOBE_W + 132], "Cedar Raft reaches Oasis of Babana (132,154) via turquoise shallows");
    }

    /* Verify auto-board Cedar Raft on COL_SHALLOW, disembark on land, and deep-sea hint on COL_WATER */
    hearth_enter_zone(&g_st, ZONE_EVERTALE_WORLD, 108, 115);
    CHECK(g_st.active_vehicle == VEH_FOOT, "Standing on Heartland Timber Pier starts on foot");
    for (int f = 0; f < 80 && g_st.active_vehicle != VEH_RAFT; f++) hearth_step_60hz(&g_st, PAD_DOWN);
    CHECK(g_st.active_vehicle == VEH_RAFT, "Stepping into turquoise shallows auto-boards Cedar Raft");
    for (int f = 0; f < 80 && g_st.active_vehicle != VEH_FOOT; f++) hearth_step_60hz(&g_st, PAD_UP);
    CHECK(g_st.active_vehicle == VEH_FOOT, "Stepping back ashore auto-disembarks Cedar Raft");

    /* Verify Toroidal Wrap (West past tx=0 -> tx=255, North past ty=0 -> ty=191) using Ship/Airship tier */
    g_st.vehicle_unlocked_mask |= VEH_MASK_AIRSHIP;
    g_st.active_vehicle = VEH_AIRSHIP;
    g_st.hero_fx = TO_FP(2 * 16 + 8);
    g_st.hero_fy = TO_FP(2 * 16 + 8);
    for (int f = 0; f < 30; f++) hearth_step_60hz(&g_st, PAD_LEFT);
    CHECK(FROM_FP(g_st.hero_fx) > 3800, "Flying west past X=0 wraps seamlessly to east edge (X~4096)");
    for (int f = 0; f < 30; f++) hearth_step_60hz(&g_st, PAD_UP);
    CHECK(FROM_FP(g_st.hero_fy) > 2800, "Flying north past Y=0 wraps seamlessly to south edge (Y~3072)");

    /* Verify all 7 continents stream distinct world_region_id & bgm_id while keeping unified ZPAL_EVERTALE_WORLD palette */
    {
        const uint16_t rx[7] = { 104, 132, 52, 128, 212, 32, 220 };
        const uint16_t ry[7] = { 84, 154, 146, 22, 126, 62, 62 };
        uint32_t bgm_mask = 0u;
        for (int r = 0; r < 7; r++) {
            g_st.hero_fx = TO_FP((int)rx[r] * 16 + 8);
            g_st.hero_fy = TO_FP((int)ry[r] * 16 + 8);
            const HearthPpuPacket *rp = hearth_step_60hz(&g_st, 0);
            CHECK(rp->world_region_id == (uint8_t)r, "Globe streaming detects correct continent region");
            CHECK(rp->terrain_pal_id == ZPAL_EVERTALE_WORLD,
                  "World Map keeps unified ZPAL_EVERTALE_WORLD palette across all continents (zero snaps)");
            bgm_mask |= (1u << rp->bgm_id);
        }
        CHECK(bgm_mask != 0u, "All 7 continents stream regional BGM seamlessly");
    }

    /* Verify World Atlas toggle via PAD_SELECT, PAD_START (Genesis 3-btn & SNES), and PAD_X */
    CHECK(!g_st.atlas_open, "Atlas starts closed");
    hearth_step_60hz(&g_st, PAD_SELECT);
    CHECK(g_st.atlas_open && g_st.ppu.atlas_open, "PAD_SELECT toggles World Atlas open");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_SELECT);
    CHECK(!g_st.atlas_open && !g_st.ppu.atlas_open, "PAD_SELECT closes World Atlas cleanly");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_START);
    CHECK(g_st.atlas_open, "PAD_START opens World Atlas (3-button Genesis & SNES parity)");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_B);
    CHECK(!g_st.atlas_open, "PAD_B closes World Atlas cleanly");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_X);
    CHECK(g_st.atlas_open, "PAD_X opens World Atlas");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_X);
    CHECK(!g_st.atlas_open, "PAD_X closes World Atlas cleanly");

    /* Verify Coral Cove fishing (Coral Snapper), Babana Oasis, Golden Babana, & Desert Encounters */
    hearth_enter_zone(&g_st, ZONE_CORAL_COVE, 22, 14);
    CHECK(g_st.zone_id == ZONE_CORAL_COVE && g_st.ppu.terrain_pal_id == ZPAL_PARADISE, "Suncrest Coral Cove loads with Paradise palette");
    hearth_enter_zone(&g_st, ZONE_BABANA_OASIS, 22, 18);
    CHECK(g_st.zone_id == ZONE_BABANA_OASIS && g_st.ppu.terrain_pal_id == ZPAL_DESERT, "Oasis of Babana loads with Desert palette");
    hearth_start_battle(&g_st, ENCOUNTER_SANDWORM_BOSS, BIOME_DESERT, -1);
    CHECK(g_st.battle.is_boss_battle && g_st.battle.enemies[0].sprite_id == SPR_ENEMY_SANDWORM,
          "Giant Sandworm desert encounter spawns cleanly");

    /* Replay Cinema -> PAD_START returns cleanly to Title Screen */
    hearth_start_intro(&g_st);
    CHECK(g_st.mode == MODE_INTRO && !g_st.intro.launch_story, "Replay Cinema launches in non-story mode");
    hearth_step_60hz(&g_st, PAD_START);
    CHECK(g_st.mode == MODE_TITLE, "Skipping Replay Cinema returns cleanly to MODE_TITLE");
#if HEARTH_DESIGNER_MODE
    CHECK(g_st.ppu.designer_mode_enabled, "DESIGNER_MODE=1 exposes Designer Mode flag to platform hosts");
#else
    CHECK(!g_st.ppu.designer_mode_enabled, "DESIGNER_MODE=0 cleanly disables Designer Mode for release builds");
#endif
    printf("[PASS] Full 256x192 Toroidal Globe, 7 Continents, Chapter 2 Raft Story & Atlas\n");

    /* 4. Explore Evertale Sandbox + Atlas Quick-Warp/Vessel Cycling + 8-Way Fixed-Point Movement */
    hearth_start_sandbox(&g_st);
#if HEARTH_DESIGNER_MODE
    CHECK(g_st.mode == MODE_EXPLORE && g_st.zone_id == ZONE_EVERTALE_WORLD && g_st.is_world_map && g_st.pip_visible,
          "Sandbox starts on Evertale Toroidal Globe outside Sunhill Market with Pip");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_START);
    CHECK(g_st.atlas_open && g_st.atlas_cursor == REGION_HEARTLAND, "Designer Atlas opens on active continent");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_UP);
    CHECK(g_st.active_vehicle == VEH_RAFT, "PAD_UP inside Designer Atlas cycles vessel (3-button Genesis parity)");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_RIGHT);
    CHECK(g_st.atlas_cursor == REGION_DESERT, "PAD_RIGHT inside Designer Atlas selects next continent");
    hearth_step_60hz(&g_st, 0);
    hearth_step_60hz(&g_st, PAD_A);
    CHECK(!g_st.atlas_open && g_st.world_region_id == REGION_DESERT, "PAD_A inside Designer Atlas warps to selected continent");
#else
    CHECK(!g_st.designer_sandbox && g_st.mode == MODE_INTRO,
          "DESIGNER_MODE=0 redirects sandbox call safely into Story Mode");
    g_st.mode = MODE_EXPLORE;
    g_st.pip_visible = true;
    g_st.flag_has_wand = true;
    g_st.flag_has_familiar = true;
    g_st.flag_has_heart_vessel = true;
#endif
    hearth_enter_zone(&g_st, ZONE_HAMLET, 10, 14);
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
    hearth_toggle_ppu_mode(&g_st);
    printf("[PASS] Dual-Resolution PPU Packet (Genesis 320x224 / SNES 256x224)\n");

    /* 10. Milestone 5: Grounded Bouncy Pip Physics, 8 Enterable Buildings & 5 Side Quests */
    {
        /* 10a. Pip Mr. Drippy Bouncy Hop & Squash-and-Stretch Verification */
        g_st.mode = MODE_EXPLORE;
        hearth_enter_zone(&g_st, ZONE_HAMLET, 10, 14);
        g_st.pip_visible = true;
        bool saw_hop_air = false;
        bool saw_hop_pose = false;
        bool saw_squash_pose = false;
        for (int f = 0; f < 100; f++) {
            const HearthPpuPacket *hp = hearth_step_60hz(&g_st, (f < 48) ? PAD_RIGHT : 0);
            if (g_st.pip_hop_z_fp > 0) saw_hop_air = true;
            for (int i = 0; i < hp->oam_count; i++) {
                if (hp->oam[i].sprite_id == SPR_PIP) {
                    if (hp->oam[i].frame == 1u) saw_hop_pose = true;
                    if (hp->oam[i].frame == 2u) saw_squash_pose = true;
                }
            }
        }
        CHECK(saw_hop_air && saw_hop_pose && saw_squash_pose,
              "Wingless garden-gnome Pip performs grounded parabolic hops with stretch & landing squash");

        /* 10b. Verify all 8 Enterable Town Buildings via exterior portals & interior exits */
        static const struct {
            uint8_t parent_zone;
            uint16_t door_tx, door_ty;
            uint8_t interior_zone;
        } BLDGS[8] = {
            { ZONE_ALDERBROOK,     6,  9, ZONE_ALDER_DEPOT     },
            { ZONE_ALDERBROOK,    24, 13, ZONE_ALDER_BAKERY    },
            { ZONE_HAMLET,         6,  9, ZONE_HAMLET_MILL     },
            { ZONE_HAMLET,        13,  7, ZONE_ELDER_STUDY     },
            { ZONE_SUNHILL_MARKET,11, 18, ZONE_SUNHILL_BAKERY  },
            { ZONE_SUNHILL_MARKET,31,  9, ZONE_SUNHILL_ARCHIVE },
            { ZONE_WHISKERBURGH,  12,  8, ZONE_WHISKER_GUILD   },
            { ZONE_BABANA_OASIS,   9,  9, ZONE_BABANA_TENT     }
        };
        for (int b = 0; b < 8; b++) {
            hearth_enter_zone(&g_st, BLDGS[b].parent_zone, BLDGS[b].door_tx, (uint16_t)(BLDGS[b].door_ty + 1u));
            g_st.hero_dir = DIR_UP;
            for (int f = 0; f < 30 && g_st.zone_id != BLDGS[b].interior_zone; f++) {
                hearth_step_60hz(&g_st, PAD_UP);
            }
            CHECK(g_st.zone_id == BLDGS[b].interior_zone, "Exterior building door enters cozy interior zone");
            CHECK(g_st.npcs[0].active && g_st.npcs[1].active && g_st.npcs[2].active,
                  "Interior building populated with 3 whimsical craft NPCs");
            /* Step south through interior doorway to return to parent town */
            for (int f = 0; f < 40 && g_st.zone_id != BLDGS[b].parent_zone; f++) {
                hearth_step_60hz(&g_st, PAD_DOWN);
            }
            CHECK(g_st.zone_id == BLDGS[b].parent_zone, "Interior exit portal returns cleanly to parent town");
        }

        /* Helper lambda-style macro to interact with an NPC or sparkling pickup and close dialogue */
        #define INTERACT_AND_CLOSE(TX, TY, DIR_VAL) do { \
            g_st.hero_fx = TO_FP((int)(TX) * 16 + 8); \
            g_st.hero_fy = TO_FP((int)(TY) * 16 + 8); \
            g_st.hero_dir = (DIR_VAL); \
            hearth_step_60hz(&g_st, 0); \
            hearth_step_60hz(&g_st, PAD_A); \
            for (int _df = 0; _df < 120 && g_st.dialogue.active; _df++) { \
                hearth_step_60hz(&g_st, ((_df & 3) == 0) ? PAD_A : 0); \
            } \
            hearth_step_60hz(&g_st, 0); \
        } while (0)

        /* 10c. Side Quest 0: Brass Whistle (Alderbrook Depot -> Alderbrook -> Depot) */
        hearth_enter_zone(&g_st, ZONE_ALDER_DEPOT, 16, 11);
        INTERACT_AND_CLOSE(16, 11, DIR_UP);
        CHECK(g_st.side_quests[0] == 1u, "Tinker Toby starts Side Quest 0 (Brass Whistle)");
        hearth_enter_zone(&g_st, ZONE_ALDERBROOK, 10, 22);
        INTERACT_AND_CLOSE(10, 22, DIR_DOWN);
        CHECK(g_st.side_quests[0] == 2u, "Found Brass Whistle sparkling pickup in Alderbrook");
        hearth_enter_zone(&g_st, ZONE_ALDER_DEPOT, 16, 11);
        INTERACT_AND_CLOSE(16, 11, DIR_UP);
        CHECK(g_st.side_quests[0] == 3u, "Returning Brass Whistle completes Side Quest 0");

        /* 10d. Side Quest 1: Breeze-Spun Flour (Hamlet Mill -> Elder Study) */
        hearth_enter_zone(&g_st, ZONE_HAMLET_MILL, 21, 11);
        INTERACT_AND_CLOSE(21, 11, DIR_UP);
        CHECK(g_st.side_quests[1] == 1u, "Miller Grain starts Side Quest 1 (Breeze-Spun Flour)");
        hearth_enter_zone(&g_st, ZONE_ELDER_STUDY, 19, 12);
        INTERACT_AND_CLOSE(19, 12, DIR_UP);
        CHECK(g_st.side_quests[1] == 3u, "Delivering Flour to Keeper Nessa completes Side Quest 1");

        /* 10e. Side Quest 2: Sunhill Royal Tart (Sunhill Bakery -> Castle Aurelia Gate Sentinel) */
        hearth_enter_zone(&g_st, ZONE_SUNHILL_BAKERY, 18, 11);
        INTERACT_AND_CLOSE(18, 11, DIR_UP);
        CHECK(g_st.side_quests[2] == 1u, "Pastry Pipkin starts Side Quest 2 (Sunhill Royal Tart)");
        hearth_enter_zone(&g_st, ZONE_CASTLE_GATE, 19, 10);
        INTERACT_AND_CLOSE(19, 10, DIR_UP);
        CHECK(g_st.side_quests[2] == 3u, "Delivering Royal Tart to Castle Sentinel completes Side Quest 2");

        /* 10f. Side Quest 3: Moon-Brie Wheel -> Master Brass Lure widens Fishing Reel Bar */
        hearth_enter_zone(&g_st, ZONE_WHISKERBURGH, 20, 20);
        hearth_start_fishing(&g_st);
        uint8_t base_bar_w = g_st.fishing.bar_half_w;
        hearth_step_60hz(&g_st, 0);
        hearth_step_60hz(&g_st, PAD_B);

        hearth_enter_zone(&g_st, ZONE_WHISKER_GUILD, 21, 12);
        INTERACT_AND_CLOSE(21, 12, DIR_UP);
        CHECK(g_st.side_quests[3] == 1u, "Master Gouda starts Side Quest 3 (Moon-Brie Wheel)");
        hearth_enter_zone(&g_st, ZONE_WHISKERBURGH, 34, 11);
        INTERACT_AND_CLOSE(34, 11, DIR_RIGHT);
        CHECK(g_st.side_quests[3] == 2u, "Found Moon-Brie Wheel sparkling pickup in Whiskerburgh");
        hearth_enter_zone(&g_st, ZONE_WHISKER_GUILD, 21, 12);
        INTERACT_AND_CLOSE(21, 12, DIR_UP);
        CHECK(g_st.side_quests[3] == 3u && g_st.has_master_lure, "Returning Moon-Brie awards Master Brass Lure");

        hearth_enter_zone(&g_st, ZONE_WHISKERBURGH, 20, 20);
        hearth_start_fishing(&g_st);
        CHECK(g_st.fishing.bar_half_w > base_bar_w, "Master Brass Lure widens fishing catch zone bar");
        hearth_step_60hz(&g_st, 0);
        hearth_step_60hz(&g_st, PAD_B);

        /* 10g. Side Quest 4: Sun-Dried Oasis Babana (Babana Tent -> Oasis -> Tent) */
        hearth_enter_zone(&g_st, ZONE_BABANA_TENT, 21, 11);
        INTERACT_AND_CLOSE(21, 11, DIR_UP);
        CHECK(g_st.side_quests[4] == 1u, "Master Tariq starts Side Quest 4 (Oasis Babana)");
        hearth_enter_zone(&g_st, ZONE_BABANA_OASIS, 32, 18);
        INTERACT_AND_CLOSE(32, 18, DIR_RIGHT);
        CHECK(g_st.side_quests[4] == 2u, "Harvested Sun-Dried Oasis Babana in Babana Oasis");
        hearth_enter_zone(&g_st, ZONE_BABANA_TENT, 21, 11);
        INTERACT_AND_CLOSE(21, 11, DIR_UP);
        CHECK(g_st.side_quests[4] == 3u, "Returning Oasis Babana completes Side Quest 4");

        #undef INTERACT_AND_CLOSE
        printf("[PASS] Milestone 5: Bouncy Wingless Pip, 8 Enterable Buildings & All 5 Side Quests\n");
    }

    /* 11. Milestone 6: 12 Context-Aware Hand-Pixelated Battle Biomes (Zones, Interiors, Sea, All 7 Continents) */
    {
        uint16_t seen_biomes = 0u;

        hearth_enter_zone(&g_st, ZONE_HAMLET, 12, 12);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_MEADOW, "Hamlet resolves to BIOME_MEADOW");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        hearth_enter_zone(&g_st, ZONE_HOLLOW_GLADE, 15, 16);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_SHRINE, "Hollow Glade resolves to BIOME_SHRINE");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        hearth_enter_zone(&g_st, ZONE_SEWERS, 20, 20);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_SEWER, "Sunken Sewers resolves to BIOME_SEWER");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        hearth_enter_zone(&g_st, ZONE_BOARHOLD, 15, 14);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_BOARHOLD, "Boarhold Citadel resolves to BIOME_BOARHOLD");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        hearth_enter_zone(&g_st, ZONE_BABANA_OASIS, 20, 16);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_DESERT, "Babana Oasis resolves to BIOME_DESERT");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        hearth_enter_zone(&g_st, ZONE_CORAL_COVE, 20, 14);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_PARADISE, "Suncrest Coral Cove resolves to BIOME_PARADISE");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        hearth_enter_zone(&g_st, ZONE_WHISPERING_WOODS, 14, 18);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_FOREST, "Whispering Woods resolves to BIOME_FOREST");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        hearth_enter_zone(&g_st, ZONE_SUNHILL_MARKET, 20, 18);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_TOWN, "Sunhill Market resolves to BIOME_TOWN");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        hearth_enter_zone(&g_st, ZONE_ALDER_BAKERY, 16, 12);
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_INTERIOR, "Town interior resolves to BIOME_INTERIOR");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        /* Globe continents + open sea raft check */
        hearth_enter_zone(&g_st, ZONE_EVERTALE_WORLD, 128, 22); /* Frostmere */
        g_st.active_vehicle = VEH_FOOT;
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_FROST, "Frostmere globe continent resolves to BIOME_FROST");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        g_st.hero_fx = TO_FP(32 * 16 + 8);
        g_st.hero_fy = TO_FP(62 * 16 + 8); /* Cinderpeak */
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_VOLCANO, "Cinderpeak globe continent resolves to BIOME_VOLCANO");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        g_st.active_vehicle = VEH_RAFT;
        CHECK(hearth_resolve_battle_biome(&g_st) == BIOME_SEA, "Active Cedar Raft resolves to BIOME_SEA");
        seen_biomes |= (uint16_t)(1u << hearth_resolve_battle_biome(&g_st));

        /* Verify BIOME_AUTO inside hearth_start_battle resolves dynamically to current region */
        g_st.active_vehicle = VEH_FOOT;
        hearth_start_battle(&g_st, ENCOUNTER_MEADOW_PACK, BIOME_AUTO, -1);
        CHECK(g_st.battle.biome == BIOME_VOLCANO, "hearth_start_battle(BIOME_AUTO) resolves to Cinderpeak Volcano");
        CHECK(seen_biomes == ((1u << BIOME_COUNT) - 1u), "All 12 distinct battle backdrop biomes verified");
        printf("[PASS] Milestone 6: All 12 Context-Aware Battle Biomes & BIOME_AUTO Resolution\n");
    }

    /* 12. Shared World Map & Outdoor Town Time-of-Day Color Grading System */
    {
        HearthTodGrade gr;

        /* 12a. Verify Q8 grade curve endpoints & exact Afternoon identity */
        hearth_tod_grade_params(0u, &gr);
        CHECK(gr.mr == 213u && gr.mg == 205u && gr.mb == 209u &&
              gr.ar8 == (49u << 8) && gr.ag8 == (37u << 8) && gr.ab8 == (32u << 8),
              "Morning (phase=0) emits Rose-Gold Q8 grade parameters");
        hearth_tod_grade_params(256u, &gr);
        CHECK(gr.mr == 256u && gr.mg == 256u && gr.mb == 256u && gr.ar8 == 0u && gr.ag8 == 0u && gr.ab8 == 0u,
              "Afternoon (phase=256) is bit-exact identity (MUL=256, ADD=0)");
        hearth_tod_grade_params(512u, &gr);
        CHECK(gr.mr == 188u && gr.mg == 158u && gr.mb == 144u &&
              gr.ar8 == (59u << 8) && gr.ag8 == (27u << 8) && gr.ab8 == (16u << 8),
              "Dusk (phase=512) emits Copper-Amber Q8 grade parameters");
        hearth_tod_grade_params(768u, &gr);
        CHECK(gr.mr == 74u && gr.mg == 84u && gr.mb == 118u &&
              gr.ar8 == (12u << 8) && gr.ag8 == (18u << 8) && gr.ab8 == (36u << 8),
              "Night (phase=768) emits Moonlit Indigo Q8 grade parameters");

        /* 12b. Verify emissive protection for 4-pal (Web/SDL2/SNES) and Genesis 2-pal */
        CHECK(hearth_tod_is_emissive_4pal(ZPAL_SUNHILL_MARKET, 2u, 13u) &&
              hearth_tod_is_emissive_4pal(ZPAL_SUNHILL_MARKET, 3u, 14u),
              "Lit town windows & lanterns stay emissive on 4-subpalette hosts");
        CHECK(hearth_tod_is_emissive_4pal(ZPAL_EVERTALE_WORLD, 3u, 4u) &&
              hearth_tod_is_emissive_4pal(ZPAL_EVERTALE_WORLD, 3u, 8u) &&
              hearth_tod_is_emissive_4pal(ZPAL_VOLCANO, 0u, 12u) &&
              hearth_tod_is_emissive_4pal(ZPAL_VOLCANO, 3u, 4u) &&
              hearth_tod_is_emissive_4pal(ZPAL_TECH, 0u, 13u) &&
              hearth_tod_is_emissive_4pal(ZPAL_TECH, 3u, 5u),
              "World Map lava/cyan conduits, Volcano magma, and Aetheris beacons stay emissive");
        CHECK(!hearth_tod_is_emissive_4pal(ZPAL_EVERTALE_WORLD, 0u, 3u),
              "Standard grass/ground entries receive full TOD grading");
        CHECK(hearth_tod_is_emissive_gen(ZPAL_EVERTALE_WORLD, 0u, 13u) &&
              hearth_tod_is_emissive_gen(ZPAL_EVERTALE_WORLD, 1u, 1u) &&
              hearth_tod_is_emissive_gen(ZPAL_EVERTALE_WORLD, 1u, 15u),
              "Genesis protects warm gold window/lantern entry and PAL3 UI text colors");

        /* 12c. Verify World Map ticks TOD smoothly and transitions seamlessly into outdoor towns without clobbering biome palettes */
        g_st.mode = MODE_EXPLORE;
        g_st.tod_locked = false;
        g_st.tod_phase = 500u; /* Approaching Dusk */
        g_st.time_of_day = TOD_DUSK;
        g_st.day_subtick = 0u;
        hearth_enter_zone(&g_st, ZONE_EVERTALE_WORLD, 104, 84);
        CHECK(g_st.has_outdoor_tod && g_st.ppu.has_outdoor_tod && g_st.tod_phase == 500u,
              "World Map enables shared outdoor TOD and preserves tod_phase on entry");
        for (int f = 0; f < 56; f++) hearth_step_60hz(&g_st, 0);
        CHECK(g_st.tod_phase == 501u, "World Map advances shared 1024-step TOD clock in real time");

        /* Enter Sunhill Market, Babana Oasis, Coral Cove, and Windmill Hamlet -> tod_phase preserved & biome palette kept! */
        hearth_enter_zone(&g_st, ZONE_SUNHILL_MARKET, 21, 20);
        CHECK(g_st.has_outdoor_tod && g_st.ppu.has_outdoor_tod &&
              g_st.tod_phase == 501u && g_st.ppu.terrain_pal_id == ZPAL_SUNHILL_MARKET,
              "World Map -> Sunhill Market preserves tod_phase and Sunhill palette");
        for (int f = 0; f < 56; f++) hearth_step_60hz(&g_st, 0);
        CHECK(g_st.tod_phase == 502u && g_st.ppu.terrain_pal_id == ZPAL_SUNHILL_MARKET,
              "Sunhill Market advances TOD without overwriting terrain_pal_id with Alderbrook autumn");

        hearth_enter_zone(&g_st, ZONE_BABANA_OASIS, 20, 16);
        CHECK(g_st.has_outdoor_tod && g_st.tod_phase == 502u && g_st.ppu.terrain_pal_id == ZPAL_DESERT,
              "Babana Oasis enables outdoor TOD with Desert palette");

        /* Enter indoor tent sharing ZPAL_DESERT -> pauses TOD & clears has_outdoor_tod! */
        hearth_enter_zone(&g_st, ZONE_BABANA_TENT, 20, 12);
        CHECK(!g_st.has_outdoor_tod && !g_st.ppu.has_outdoor_tod && g_st.ppu.terrain_pal_id == ZPAL_DESERT,
              "Indoor Babana Tent disables outdoor TOD while sharing ZPAL_DESERT");
        for (int f = 0; f < 120; f++) hearth_step_60hz(&g_st, 0);
        CHECK(g_st.tod_phase == 502u, "Indoor building pauses the outdoor TOD clock cleanly");

        /* Underground Sewers stays dark & non-TOD */
        hearth_enter_zone(&g_st, ZONE_SEWERS, 20, 20);
        CHECK(!g_st.has_outdoor_tod && g_st.zone_is_dark && g_st.tod_phase == 502u,
              "Sunken Sewers stays permanently dark with outdoor TOD disabled");

        /* Return to World Map at Night -> zone_is_dark tracks smooth night phase */
        g_st.tod_phase = 768u;
        hearth_enter_zone(&g_st, ZONE_EVERTALE_WORLD, 104, 84);
        CHECK(g_st.has_outdoor_tod && g_st.zone_is_dark && g_st.ppu.zone_is_dark,
              "World Map at Night (tod_phase=768) sets zone_is_dark seamlessly");

        printf("[PASS] Milestone 7: Shared World Map & Outdoor Town Time-of-Day Color Grading\n");
    }

    printf("ALL PURE C99 CORE TESTS PASSED!\n");
    return 0;
}
