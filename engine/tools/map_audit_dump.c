/* ============================================================================
 * map_audit_dump.c — Production C99 World & Autotile Dumper for make map-audit
 *
 * Compiles directly against engine/core/hearth_data.c, hearth_core.c, and the
 * shared hearth_terrain_rom.h autotiler so the Python 6-pass auditor inspects
 * the EXACT maps, collisions, portals, NPCs, landmarks, and resolved metatiles
 * rendered on Web, PC SDL2, SNES, and Sega Genesis.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/hearth_core.h"
#include "../platform/hearth_terrain_rom.h"

static HearthState g_st;

static void write_json_str(FILE *fp, const char *s) {
    fputc('"', fp);
    if (s) {
        for (; *s; s++) {
            if (*s == '"' || *s == '\\') fputc('\\', fp);
            if ((unsigned char)*s >= 32u) fputc(*s, fp);
        }
    }
    fputc('"', fp);
}

static uint8_t sample_zone_bg1(const HearthState *st, int x, int y) {
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= (int)st->map_w) x = (int)st->map_w - 1;
    if (y >= (int)st->map_h) y = (int)st->map_h - 1;
    return st->map_bg1[y * (int)st->map_w + x];
}

int main(int argc, char **argv) {
    const char *out_dir = (argc > 1) ? argv[1] : "build/map_audit/raw";
    char path[512];
    FILE *jfp;

    snprintf(path, sizeof(path), "%s/world_dump.json", out_dir);
    jfp = fopen(path, "w");
    if (!jfp) {
        fprintf(stderr, "ERROR: Cannot open %s for writing\n", path);
        return 1;
    }

    hearth_init(&g_st);
    /* Unlock progression flags so all NPCs/portals/chests are inspectable */
    g_st.mode = MODE_EXPLORE;
    g_st.pip_visible = true;
    g_st.flag_has_wand = true;
    g_st.flag_has_familiar = true;

    fprintf(jfp, "{\n");
    fprintf(jfp, "  \"chrono_meta_count\": %u,\n", (unsigned)CHRONO_META_COUNT);
    fprintf(jfp, "  \"snes_tile_count\": %u,\n", (unsigned)SNES_CHRONO_TILE_COUNT);
    fprintf(jfp, "  \"gen_tile_count\": %u,\n", (unsigned)GEN_CHRONO_TILE_COUNT);
    fprintf(jfp, "  \"zones\": [\n");

    int first_zone = 1;
    for (uint8_t z = 0u; z < (uint8_t)ZONE_COUNT; z++) {
        if (z == (uint8_t)ZONE_EVERTALE_WORLD) continue;

        /* Enter zone at a neutral tile; hearth_enter_zone builds map, portals, NPCs, chests */
        hearth_enter_zone(&g_st, z, 10u, 10u);

        int w = (int)g_st.map_w;
        int h = (int)g_st.map_h;
        snprintf(path, sizeof(path), "%s/zone_%02u.bin", out_dir, (unsigned)z);
        FILE *bfp = fopen(path, "wb");
        if (!bfp) {
            fprintf(stderr, "ERROR: Cannot open %s\n", path);
            fclose(jfp);
            return 1;
        }

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                int idx = y * w + x;
                uint8_t bg1 = g_st.map_bg1[idx];
                uint8_t bg2 = g_st.map_bg2[idx];
                uint8_t col = g_st.map_col[idx];
                uint8_t tn  = sample_zone_bg1(&g_st, x, y - 1);
                uint8_t ts  = sample_zone_bg1(&g_st, x, y + 1);
                uint8_t tw  = sample_zone_bg1(&g_st, x - 1, y);
                uint8_t te  = sample_zone_bg1(&g_st, x + 1, y);
                uint8_t tnw = sample_zone_bg1(&g_st, x - 1, y - 1);
                uint8_t tne = sample_zone_bg1(&g_st, x + 1, y - 1);
                uint8_t tsw = sample_zone_bg1(&g_st, x - 1, y + 1);
                uint8_t tse = sample_zone_bg1(&g_st, x + 1, y + 1);
                uint8_t flags = hearth_local_tile_flags(g_st.map_bg1, (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h);
                uint8_t meta = hearth_resolve_chrono_meta(
                    bg1, bg2, tn, ts, tw, te, tnw, tne, tsw, tse,
                    (uint16_t)x, (uint16_t)y, flags);
                uint8_t rec[4] = { bg1, bg2, col, meta };
                fwrite(rec, 1, 4, bfp);
            }
        }
        fclose(bfp);

        if (!first_zone) fprintf(jfp, ",\n");
        first_zone = 0;
        fprintf(jfp, "    {\n");
        fprintf(jfp, "      \"id\": %u,\n", (unsigned)z);
        fprintf(jfp, "      \"name\": "); write_json_str(jfp, HEARTH_ZONE_NAMES[z]); fprintf(jfp, ",\n");
        fprintf(jfp, "      \"w\": %d, \"h\": %d,\n", w, h);
        fprintf(jfp, "      \"pal_id\": %u,\n", (unsigned)g_st.terrain_pal_id);
        fprintf(jfp, "      \"has_mountain_vista\": %s,\n", g_st.has_mountain_vista ? "true" : "false");
        fprintf(jfp, "      \"is_dark\": %s,\n", g_st.zone_is_dark ? "true" : "false");
        fprintf(jfp, "      \"bin\": \"zone_%02u.bin\",\n", (unsigned)z);

        /* Portals */
        fprintf(jfp, "      \"portals\": [");
        int first_p = 1;
        for (int i = 0; i < HEARTH_MAX_PORTALS; i++) {
            if (!g_st.portals[i].active) continue;
            if (!first_p) fprintf(jfp, ", ");
            first_p = 0;
            fprintf(jfp, "{\"tx\":%u,\"ty\":%u,\"dst_zone\":%u,\"dst_tx\":%u,\"dst_ty\":%u,\"label\":",
                    (unsigned)g_st.portals[i].tx, (unsigned)g_st.portals[i].ty,
                    (unsigned)g_st.portals[i].dst_zone,
                    (unsigned)g_st.portals[i].dst_tx, (unsigned)g_st.portals[i].dst_ty);
            write_json_str(jfp, g_st.portals[i].label);
            fprintf(jfp, "}");
        }
        fprintf(jfp, "],\n");

        /* NPCs */
        fprintf(jfp, "      \"npcs\": [");
        int first_n = 1;
        for (int i = 0; i < HEARTH_MAX_NPCS; i++) {
            if (!g_st.npcs[i].active) continue;
            if (!first_n) fprintf(jfp, ", ");
            first_n = 0;
            int ntx = FROM_FP(g_st.npcs[i].fx) >> 4;
            int nty = FROM_FP(g_st.npcs[i].fy) >> 4;
            fprintf(jfp, "{\"tx\":%d,\"ty\":%d,\"name\":", ntx, nty);
            write_json_str(jfp, g_st.npcs[i].name);
            fprintf(jfp, "}");
        }
        fprintf(jfp, "],\n");

        /* Interactive props */
        fprintf(jfp, "      \"chest\": %s", g_st.chest.active ? "" : "null");
        if (g_st.chest.active) {
            fprintf(jfp, "{\"tx\":%u,\"ty\":%u}", (unsigned)g_st.chest.tx, (unsigned)g_st.chest.ty);
        }
        fprintf(jfp, ",\n");
        fprintf(jfp, "      \"fish_spot\": %s", g_st.fish_spot.active ? "" : "null");
        if (g_st.fish_spot.active) {
            fprintf(jfp, "{\"tx\":%u,\"ty\":%u}", (unsigned)g_st.fish_spot.tx, (unsigned)g_st.fish_spot.ty);
        }
        fprintf(jfp, ",\n");
        fprintf(jfp, "      \"waystone\": %s", g_st.has_waystone ? "" : "null");
        if (g_st.has_waystone) {
            fprintf(jfp, "{\"tx\":%u,\"ty\":%u}", (unsigned)g_st.waystone_tx, (unsigned)g_st.waystone_ty);
        }
        fprintf(jfp, ",\n");
        fprintf(jfp, "      \"sign\": %s\n", g_st.has_sign ? "" : "null");
        if (g_st.has_sign) {
            fprintf(jfp, "{\"tx\":%u,\"ty\":%u}\n", (unsigned)g_st.sign_tx, (unsigned)g_st.sign_ty);
        }
        fprintf(jfp, "    }");
    }
    fprintf(jfp, "\n  ],\n");

    /* Dump full 256x192 Evertale Toroidal Globe */
    {
        static uint8_t g_bg1[HEARTH_GLOBE_W * HEARTH_GLOBE_H];
        static uint8_t g_bg2[HEARTH_GLOBE_W * HEARTH_GLOBE_H];
        static uint8_t g_col[HEARTH_GLOBE_W * HEARTH_GLOBE_H];
        static uint8_t g_reg[HEARTH_GLOBE_W * HEARTH_GLOBE_H];

        for (uint16_t y = 0u; y < (uint16_t)HEARTH_GLOBE_H; y++) {
            for (uint16_t x = 0u; x < (uint16_t)HEARTH_GLOBE_W; x++) {
                uint32_t idx = (uint32_t)y * HEARTH_GLOBE_W + x;
                hearth_globe_cell(x, y, &g_bg1[idx], &g_bg2[idx], &g_col[idx], &g_reg[idx]);
            }
        }

        snprintf(path, sizeof(path), "%s/globe.bin", out_dir);
        FILE *gfp = fopen(path, "wb");
        if (!gfp) {
            fprintf(stderr, "ERROR: Cannot open %s\n", path);
            fclose(jfp);
            return 1;
        }
        for (int y = 0; y < HEARTH_GLOBE_H; y++) {
            int yn = (y + HEARTH_GLOBE_H - 1) % HEARTH_GLOBE_H;
            int ys = (y + 1) % HEARTH_GLOBE_H;
            for (int x = 0; x < HEARTH_GLOBE_W; x++) {
                int xw = (x + HEARTH_GLOBE_W - 1) & 255;
                int xe = (x + 1) & 255;
                int idx = y * HEARTH_GLOBE_W + x;
                uint8_t bg1 = g_bg1[idx];
                uint8_t bg2 = g_bg2[idx];
                uint8_t col = g_col[idx];
                uint8_t reg = g_reg[idx];
                uint8_t meta = hearth_resolve_chrono_meta(
                    bg1, bg2,
                    g_bg1[yn * HEARTH_GLOBE_W + x],
                    g_bg1[ys * HEARTH_GLOBE_W + x],
                    g_bg1[y  * HEARTH_GLOBE_W + xw],
                    g_bg1[y  * HEARTH_GLOBE_W + xe],
                    g_bg1[yn * HEARTH_GLOBE_W + xw],
                    g_bg1[yn * HEARTH_GLOBE_W + xe],
                    g_bg1[ys * HEARTH_GLOBE_W + xw],
                    g_bg1[ys * HEARTH_GLOBE_W + xe],
                    (uint16_t)x, (uint16_t)y, 1u);
                uint8_t rec[5] = { bg1, bg2, col, meta, reg };
                fwrite(rec, 1, 5, gfp);
            }
        }
        fclose(gfp);

        fprintf(jfp, "  \"globe\": {\n");
        fprintf(jfp, "    \"w\": %u, \"h\": %u, \"bin\": \"globe.bin\",\n",
                (unsigned)HEARTH_GLOBE_W, (unsigned)HEARTH_GLOBE_H);
        fprintf(jfp, "    \"regions\": [");
        for (int r = 0; r < REGION_COUNT; r++) {
            if (r > 0) fprintf(jfp, ", ");
            write_json_str(jfp, HEARTH_REGION_NAMES[r]);
        }
        fprintf(jfp, "],\n");
        fprintf(jfp, "    \"landmarks\": [\n");
        for (int i = 0; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
            const HearthGlobeLandmark *lm = &HEARTH_GLOBE_LANDMARKS[i];
            fprintf(jfp, "      {\"tx\":%u,\"ty\":%u,\"region\":%u,\"dst_zone\":%u,\"dst_tx\":%u,\"dst_ty\":%u,\"enterable\":%u,\"name\":",
                    (unsigned)lm->tx, (unsigned)lm->ty, (unsigned)lm->region_id,
                    (unsigned)lm->dst_zone, (unsigned)lm->dst_tx, (unsigned)lm->dst_ty,
                    (unsigned)lm->enterable);
            write_json_str(jfp, lm->name);
            fprintf(jfp, "}%s\n", (i + 1 < HEARTH_GLOBE_LANDMARK_COUNT) ? "," : "");
        }
        fprintf(jfp, "    ]\n");
        fprintf(jfp, "  }\n");
    }

    fprintf(jfp, "}\n");
    fclose(jfp);
    return 0;
}
