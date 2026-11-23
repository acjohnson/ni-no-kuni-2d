/* ============================================================================
 * sdl2_main.c — Project Hearth Native Desktop Host (Linux / Windows / macOS)
 *
 * Pure C99 + SDL2 ONLY (zero SDL_ttf, SDL_image, or SDL_mixer dependencies!).
 * Features:
 *   - Integer-scaled 16-bit virtual PPU (320x224 Genesis / 256x224 SNES)
 *   - Built-in 16x16 pixel-art tile & sprite software renderer using the
 *     exact 4x16 master palettes from engine/core/hearth_data.c
 *   - Crisp embedded 5x7 bitmap font (no external font files required)
 *   - Built-in 8-channel 16-bit retro PCM synthesizer (Square / Tri / Noise)
 *   - Keyboard + automatic SDL_GameController hot-plug support
 *   - Exact 60Hz deterministic simulation accumulator
 *
 * Build on Ubuntu Noble amd64:
 *   sudo apt install build-essential libsdl2-dev
 *   make run
 * ============================================================================ */

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/hearth_core.h"
#include "pixel_art_data.h"
#include "cinema_cg_sdl2.h"
#include "hearth_hero24.h"

/* ============================================================================
 * 1. VIRTUAL PPU FRAMEBUFFER & PIXEL HELPERS
 * ============================================================================ */
#define FB_MAX_W 320
#define FB_MAX_H 224

static uint32_t g_fb[FB_MAX_W * FB_MAX_H];
static uint32_t g_pal_argb[4][16];
static HearthState g_state;

static bool g_scanlines = false;
static bool g_muted     = false;
static bool g_fullscreen = false;
static uint16_t g_ppu_w = 320;
static uint16_t g_ppu_h = 224;

static inline uint32_t rgb_to_argb(uint32_t rgb) {
    return 0xFF000000u | (rgb & 0x00FFFFFFu);
}

static inline uint32_t pal_col(uint8_t pal, uint8_t idx) {
    return g_pal_argb[pal & 3u][idx & 15u];
}

static uint8_t g_last_terrain_pal = 255u;

static void apply_zone_palette(uint8_t pal_id) {
    if (pal_id >= ZPAL_COUNT) pal_id = 0;
    if (g_last_terrain_pal == pal_id) return;
    g_last_terrain_pal = pal_id;
    for (int i = 0; i < 16; i++) {
        g_pal_argb[0][i] = (i == 0) ? 0u : rgb_to_argb(HEARTH_ZONE_PALETTES_RGB[pal_id][i]);
    }
}

static void init_palettes(void) {
    for (int p = 0; p < 4; p++) {
        for (int i = 0; i < 16; i++) {
            g_pal_argb[p][i] = (i == 0) ? 0u : rgb_to_argb(HEARTH_PALETTES_RGB[p][i]);
        }
    }
    g_last_terrain_pal = 0;
}

static inline void put_px(int x, int y, uint32_t col) {
    if ((unsigned)x < (unsigned)g_ppu_w && (unsigned)y < (unsigned)g_ppu_h) {
        g_fb[y * FB_MAX_W + x] = col;
    }
}

static inline void put_px_blend(int x, int y, uint32_t col, uint8_t alpha) {
    if ((unsigned)x >= (unsigned)g_ppu_w || (unsigned)y >= (unsigned)g_ppu_h) return;
    uint32_t dst = g_fb[y * FB_MAX_W + x];
    uint32_t inv = (uint32_t)(255u - alpha);
    uint32_t sr = (col >> 16) & 0xFFu, sg = (col >> 8) & 0xFFu, sb = col & 0xFFu;
    uint32_t dr = (dst >> 16) & 0xFFu, dg = (dst >> 8) & 0xFFu, db = dst & 0xFFu;
    uint32_t r = (sr * alpha + dr * inv) >> 8;
    uint32_t g = (sg * alpha + dg * inv) >> 8;
    uint32_t b = (sb * alpha + db * inv) >> 8;
    g_fb[y * FB_MAX_W + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void fill_rect_px(int x, int y, int w, int h, uint32_t col) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)g_ppu_w ? (int)g_ppu_w : x + w;
    int y1 = y + h > (int)g_ppu_h ? (int)g_ppu_h : y + h;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = &g_fb[py * FB_MAX_W];
        for (int px = x0; px < x1; px++) {
            row[px] = col;
        }
    }
}

static void fill_rect_alpha(int x, int y, int w, int h, uint32_t col, uint8_t alpha) {
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > (int)g_ppu_w ? (int)g_ppu_w : x + w;
    int y1 = y + h > (int)g_ppu_h ? (int)g_ppu_h : y + h;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            put_px_blend(px, py, col, alpha);
        }
    }
}

static void draw_rect_outline(int x, int y, int w, int h, uint32_t col) {
    fill_rect_px(x, y, w, 1, col);
    fill_rect_px(x, y + h - 1, w, 1, col);
    fill_rect_px(x, y, 1, h, col);
    fill_rect_px(x + w - 1, y, 1, h, col);
}

static void draw_circle_outline(int cx, int cy, int r, uint32_t col) {
    if (r <= 0) return;
    int x = r, y = 0, err = 1 - x;
    while (x >= y) {
        put_px(cx + x, cy + y, col); put_px(cx + y, cy + x, col);
        put_px(cx - y, cy + x, col); put_px(cx - x, cy + y, col);
        put_px(cx - x, cy - y, col); put_px(cx - y, cy - x, col);
        put_px(cx + y, cy - x, col); put_px(cx + x, cy - y, col);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x + 1);
        }
    }
}

static void fill_ellipse_alpha(int cx, int cy, int rx, int ry, uint32_t col, uint8_t alpha) {
    if (rx <= 0 || ry <= 0) return;
    int rx2 = rx * rx, ry2 = ry * ry;
    for (int dy = -ry; dy <= ry; dy++) {
        for (int dx = -rx; dx <= rx; dx++) {
            if (dx * dx * ry2 + dy * dy * rx2 <= rx2 * ry2) {
                put_px_blend(cx + dx, cy + dy, col, alpha);
            }
        }
    }
}

static void draw_line_px(int x0, int y0, int x1, int y1, uint32_t col) {
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        put_px(x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* ============================================================================
 * 2. EMBEDDED 5x7 CRISP 16-BIT BITMAP FONT (ASCII 32..95, lowercase auto-upper)
 * ============================================================================ */
static const uint8_t FONT_5X7[64][7] = {
    /* 32 ' ' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
    /* 33 '!' */ {0x04,0x04,0x04,0x04,0x04,0x00,0x04},
    /* 34 '"' */ {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00},
    /* 35 '#' */ {0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0x00},
    /* 36 '$' */ {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04},
    /* 37 '%' */ {0x18,0x19,0x02,0x04,0x08,0x13,0x03},
    /* 38 '&' */ {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D},
    /* 39 ''' */ {0x04,0x04,0x00,0x00,0x00,0x00,0x00},
    /* 40 '(' */ {0x02,0x04,0x08,0x08,0x08,0x04,0x02},
    /* 41 ')' */ {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    /* 42 '*' */ {0x00,0x0A,0x04,0x1F,0x04,0x0A,0x00},
    /* 43 '+' */ {0x00,0x04,0x04,0x1F,0x04,0x04,0x00},
    /* 44 ',' */ {0x00,0x00,0x00,0x00,0x04,0x04,0x08},
    /* 45 '-' */ {0x00,0x00,0x00,0x1F,0x00,0x00,0x00},
    /* 46 '.' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x04},
    /* 47 '/' */ {0x01,0x01,0x02,0x04,0x08,0x10,0x10},
    /* 48 '0' */ {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},
    /* 49 '1' */ {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    /* 50 '2' */ {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},
    /* 51 '3' */ {0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},
    /* 52 '4' */ {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},
    /* 53 '5' */ {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    /* 54 '6' */ {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},
    /* 55 '7' */ {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    /* 56 '8' */ {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
    /* 57 '9' */ {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    /* 58 ':' */ {0x00,0x04,0x00,0x00,0x00,0x04,0x00},
    /* 59 ';' */ {0x00,0x04,0x00,0x00,0x04,0x04,0x08},
    /* 60 '<' */ {0x02,0x04,0x08,0x10,0x08,0x04,0x02},
    /* 61 '=' */ {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00},
    /* 62 '>' */ {0x08,0x04,0x02,0x01,0x02,0x04,0x08},
    /* 63 '?' */ {0x0E,0x11,0x01,0x02,0x04,0x00,0x04},
    /* 64 '@' */ {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E},
    /* 65 'A' */ {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 66 'B' */ {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    /* 67 'C' */ {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
    /* 68 'D' */ {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    /* 69 'E' */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
    /* 70 'F' */ {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    /* 71 'G' */ {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},
    /* 72 'H' */ {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    /* 73 'I' */ {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
    /* 74 'J' */ {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    /* 75 'K' */ {0x11,0x12,0x14,0x18,0x14,0x12,0x11},
    /* 76 'L' */ {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    /* 77 'M' */ {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},
    /* 78 'N' */ {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    /* 79 'O' */ {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 80 'P' */ {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    /* 81 'Q' */ {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},
    /* 82 'R' */ {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    /* 83 'S' */ {0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},
    /* 84 'T' */ {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    /* 85 'U' */ {0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
    /* 86 'V' */ {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    /* 87 'W' */ {0x11,0x11,0x11,0x15,0x15,0x1B,0x11},
    /* 88 'X' */ {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    /* 89 'Y' */ {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},
    /* 90 'Z' */ {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    /* 91 '[' */ {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E},
    /* 92 '\' */ {0x10,0x10,0x08,0x04,0x02,0x01,0x01},
    /* 93 ']' */ {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E},
    /* 94 '^' */ {0x04,0x0A,0x11,0x00,0x00,0x00,0x00},
    /* 95 '_' */ {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}
};

static void draw_char(int x, int y, char c, uint32_t col, int scale) {
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    if (c < 32 || c > 95) c = '?';
    const uint8_t *rows = FONT_5X7[c - 32];
    for (int ry = 0; ry < 7; ry++) {
        uint8_t bits = rows[ry];
        for (int rx = 0; rx < 5; rx++) {
            if (bits & (1u << (4 - rx))) {
                if (scale <= 1) {
                    put_px(x + rx, y + ry, col);
                } else {
                    fill_rect_px(x + rx * scale, y + ry * scale, scale, scale, col);
                }
            }
        }
    }
}

static void draw_text_n(int x, int y, const char *s, size_t max_chars, uint32_t col, bool shadow) {
    if (!s) return;
    int cx = x;
    for (size_t i = 0; i < max_chars && s[i] != '\0'; i++) {
        if (shadow) draw_char(cx + 1, y + 1, s[i], 0xFF14101Au, 1);
        draw_char(cx, y, s[i], col, 1);
        cx += 6;
    }
}

static void draw_text(int x, int y, const char *s, uint32_t col, bool shadow) {
    if (!s) return;
    draw_text_n(x, y, s, strlen(s), col, shadow);
}

static void draw_text_centered(int cx, int y, const char *s, uint32_t col, bool shadow) {
    if (!s) return;
    int w = (int)strlen(s) * 6 - 1;
    draw_text(cx - w / 2, y, s, col, shadow);
}

static void draw_text_scaled_centered(int cx, int y, const char *s, uint32_t col, int scale) {
    if (!s) return;
    size_t len = strlen(s);
    int w = (int)len * 6 * scale - scale;
    int x = cx - w / 2;
    for (size_t i = 0; i < len; i++) {
        draw_char(x + 1, y + 1, s[i], 0xFF120F18u, scale);
        draw_char(x, y, s[i], col, scale);
        x += 6 * scale;
    }
}

static int count_wrapped_lines(int max_w, const char *s, size_t max_chars) {
    if (!s || max_chars == 0) return 1;
    int max_cols = max_w / 6;
    if (max_cols < 8) max_cols = 8;
    size_t pos = 0;
    int lines = 0;
    while (pos < max_chars && s[pos] != '\0' && lines < 6) {
        while (pos < max_chars && s[pos] == ' ') pos++;
        if (pos >= max_chars || s[pos] == '\0') break;
        size_t take = 0;
        size_t last_space = 0;
        while (pos + take < max_chars && s[pos + take] != '\0' && (int)take < max_cols) {
            if (s[pos + take] == ' ') last_space = take;
            take++;
        }
        if ((int)take == max_cols && pos + take < max_chars && s[pos + take] != ' ' && s[pos + take] != '\0' && last_space > 0) {
            take = last_space;
        }
        pos += take;
        lines++;
    }
    return (lines < 1) ? 1 : lines;
}

static void draw_text_wrapped(int x, int y, int max_w, const char *s, size_t visible_chars, uint32_t col) {
    if (!s) return;
    int max_cols = max_w / 6;
    if (max_cols < 8) max_cols = 8;
    size_t pos = 0;
    int cy = y;
    int line_num = 0;
    while (pos < visible_chars && s[pos] != '\0' && line_num < 6) {
        while (pos < visible_chars && s[pos] == ' ') pos++;
        if (pos >= visible_chars || s[pos] == '\0') break;
        size_t take = 0;
        size_t last_space = 0;
        while (pos + take < visible_chars && s[pos + take] != '\0' && (int)take < max_cols) {
            if (s[pos + take] == ' ') last_space = take;
            take++;
        }
        if ((int)take == max_cols && pos + take < visible_chars && s[pos + take] != ' ' && s[pos + take] != '\0' && last_space > 0) {
            take = last_space;
        }
        draw_text_n(x, cy, s + pos, take, col, true);
        pos += take;
        cy += 10;
        line_num++;
    }
}

/* ============================================================================
 * 3. RETRO 8-VOICE 16-BIT PCM AUDIO SYNTHESIZER (SDL2 Audio Callback)
 * ============================================================================ */
#define SYNTH_SAMPLE_RATE 44100
#define SYNTH_MAX_VOICES  8

typedef enum {
    WAVE_SQUARE50 = 0,
    WAVE_SQUARE25 = 1,
    WAVE_TRIANGLE = 2,
    WAVE_NOISE    = 3
} SynthWave;

typedef struct {
    bool      active;
    uint8_t   wave;
    uint32_t  phase_acc;
    uint32_t  phase_inc;
    int32_t   phase_slide;
    int32_t   samples_left;
    int32_t   total_samples;
    int32_t   volume;      /* 0..8192 */
    uint16_t  lfsr;
} SynthVoice;

static SynthVoice g_voices[SYNTH_MAX_VOICES];
static SDL_AudioDeviceID g_audio_dev = 0;

/* ----------------------------------------------------------------------------
 * 9-Track Procedural 16-Bit JRPG BGM Sequencer (Matches HearthBgmId 0..8)
 * ---------------------------------------------------------------------------- */

#define BGM_REST 0u
#define BGM_HOLD 255u

/* Integer Hz frequencies for MIDI notes 36 (C2) .. 95 (B6) */
static const uint16_t BGM_NOTE_HZ[60] = {
    /* C2..B2 */  65,  69,  73,  78,  82,  87,  93,  98, 104, 110, 117, 123,
    /* C3..B3 */ 131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
    /* C4..B4 */ 262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
    /* C5..B5 */ 523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988,
    /* C6..B6 */1047,1109,1175,1245,1319,1397,1480,1568,1661,1760,1865,1976
};

/* MIDI note shorthand constants */
enum {
    N_C2=36, N_Cs2=37, N_D2=38, N_Eb2=39, N_E2=40, N_F2=41, N_Fs2=42, N_G2=43, N_Gs2=44, N_A2=45, N_Bb2=46, N_B2=47,
    N_C3=48, N_Cs3=49, N_D3=50, N_Eb3=51, N_E3=52, N_F3=53, N_Fs3=54, N_G3=55, N_Gs3=56, N_A3=57, N_Bb3=58, N_B3=59,
    N_C4=60, N_Cs4=61, N_D4=62, N_Eb4=63, N_E4=64, N_F4=65, N_Fs4=66, N_G4=67, N_Gs4=68, N_A4=69, N_Bb4=70, N_B4=71,
    N_C5=72, N_Cs5=73, N_D5=74, N_Eb5=75, N_E5=76, N_F5=77, N_Fs5=78, N_G5=79, N_Gs5=80, N_A5=81, N_Bb5=82, N_B5=83,
    N_C6=84, N_D6=86
};

typedef struct {
    uint8_t  length;
    uint16_t step_ms;
    const uint8_t *lead;
    const uint8_t *arp;
    const uint8_t *bass;
    const uint8_t *drum; /* 0=none, 1=soft brush/hat, 2=snare, 3=kick */
} BgmTrack;

/* Track 0: Windmill Hamlet — Warm 3/4 Pastoral Waltz (48 eighth-note steps = 8 measures) */
static const uint8_t HAMLET_LEAD[48] = {
    N_G4, N_B4, N_D5, BGM_HOLD, N_E5, N_D5,
    N_B4, BGM_HOLD, N_G4, BGM_HOLD, N_A4, N_B4,
    N_C5, BGM_HOLD, N_E5, N_D5, N_C5, N_B4,
    N_A4, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_B4, N_C5,
    N_D5, BGM_HOLD, N_G5, N_Fs5, N_E5, N_D5,
    N_B4, BGM_HOLD, N_E5, N_D5, N_C5, N_B4,
    N_A4, N_B4, N_C5, N_D5, N_B4, N_A4,
    N_G4, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t HAMLET_ARP[48] = {
    N_G3, N_B3, N_D4, N_G4, N_D4, N_B3,
    N_E3, N_G3, N_B3, N_E4, N_B3, N_G3,
    N_C4, N_E4, N_G4, N_E4, N_C4, N_G3,
    N_D3, N_Fs3, N_A3, N_D4, N_C4, N_A3,
    N_B3, N_D4, N_Fs4, N_D4, N_B3, N_Fs3,
    N_E3, N_G3, N_B3, N_E4, N_B3, N_G3,
    N_A3, N_C4, N_E4, N_D4, N_Fs4, N_C4,
    N_G3, N_B3, N_D4, N_G4, N_D4, N_B3
};
static const uint8_t HAMLET_BASS[48] = {
    N_G2, BGM_HOLD, N_D3, BGM_REST, N_D3, BGM_REST,
    N_E2, BGM_HOLD, N_B2, BGM_REST, N_B2, BGM_REST,
    N_C3, BGM_HOLD, N_G3, BGM_REST, N_G3, BGM_REST,
    N_D2, BGM_HOLD, N_A2, BGM_REST, N_A2, BGM_REST,
    N_B2, BGM_HOLD, N_Fs3, BGM_REST, N_Fs3, BGM_REST,
    N_E2, BGM_HOLD, N_B2, BGM_REST, N_B2, BGM_REST,
    N_A2, BGM_HOLD, N_E3, N_D2, BGM_HOLD, N_A2,
    N_G2, BGM_HOLD, N_D3, N_G2, BGM_HOLD, BGM_REST
};
static const uint8_t HAMLET_DRUM[48] = {
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
    3, 0, 1, 3, 0, 1,  3, 0, 1, 0, 0, 0
};

/* Track 1: Whispering Meadow Battle — Heroic 4/4 JRPG Battle (64 16th-note/8th steps) */
static const uint8_t BATTLE_LEAD[64] = {
    N_A4, BGM_HOLD, N_C5, N_E5, N_A5, BGM_HOLD, N_G5, N_E5,
    N_D5, N_E5, N_F5, N_E5, N_D5, N_C5, N_B4, BGM_HOLD,
    N_C5, BGM_HOLD, N_E5, N_G5, N_C6, BGM_HOLD, N_B5, N_G5,
    N_A5, N_G5, N_E5, N_D5, N_E5, BGM_HOLD, BGM_REST, N_E5,
    N_F5, BGM_HOLD, N_E5, N_D5, N_C5, BGM_HOLD, N_D5, N_E5,
    N_G5, BGM_HOLD, N_F5, N_E5, N_D5, BGM_HOLD, N_C5, N_B4,
    N_A4, N_C5, N_E5, N_A5, N_Gs5, N_E5, N_B4, N_Gs5,
    N_A5, BGM_HOLD, N_E5, N_C5, N_A4, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t BATTLE_ARP[64] = {
    N_A3, N_E4, N_C4, N_E4, N_A3, N_E4, N_C4, N_E4,
    N_G3, N_D4, N_B3, N_D4, N_G3, N_D4, N_B3, N_D4,
    N_C4, N_G4, N_E4, N_G4, N_C4, N_G4, N_E4, N_G4,
    N_E3, N_B3, N_Gs3, N_B3, N_E3, N_B3, N_E4, N_B3,
    N_F3, N_C4, N_A3, N_C4, N_C4, N_G4, N_E4, N_G4,
    N_G3, N_D4, N_B3, N_D4, N_D3, N_A3, N_F3, N_A3,
    N_A3, N_E4, N_C4, N_E4, N_E3, N_B3, N_Gs3, N_B3,
    N_A3, N_E4, N_C4, N_E4, N_A3, N_C4, N_E4, N_G4
};
static const uint8_t BATTLE_BASS[64] = {
    N_A2, N_A2, N_A3, N_A2, N_A2, N_A2, N_G2, N_A2,
    N_G2, N_G2, N_G3, N_G2, N_G2, N_G2, N_F2, N_G2,
    N_C3, N_C3, N_C4, N_C3, N_C3, N_C3, N_B2, N_C3,
    N_E2, N_E2, N_E3, N_E2, N_E2, N_Fs2, N_G2, N_Gs2,
    N_F2, N_F2, N_F3, N_F2, N_C3, N_C3, N_C4, N_C3,
    N_G2, N_G2, N_G3, N_G2, N_D2, N_D2, N_D3, N_D2,
    N_A2, N_A2, N_A3, N_A2, N_E2, N_E2, N_E3, N_E2,
    N_A2, N_A2, N_E2, N_G2, N_A2, BGM_HOLD, BGM_REST, N_E2
};
static const uint8_t BATTLE_DRUM[64] = {
    3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 1, 2, 2,
    3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 2, 2, 1,
    3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 1, 2, 1,
    3, 1, 2, 1, 3, 3, 2, 1,  3, 2, 3, 2, 3, 0, 2, 2
};

/* Track 2: Briar Warden Boss — Dramatic 4/4 D-Minor Boss Theme */
static const uint8_t BOSS_LEAD[64] = {
    N_D5, BGM_HOLD, N_Eb5, N_D5, N_C5, N_Bb4, N_A4, BGM_HOLD,
    N_D5, N_F5, N_A5, BGM_HOLD, N_G5, N_F5, N_E5, BGM_HOLD,
    N_F5, BGM_HOLD, N_G5, N_F5, N_E5, N_D5, N_Cs5, BGM_HOLD,
    N_A4, N_Cs5, N_E5, N_G5, N_F5, N_E5, N_D5, BGM_HOLD,
    N_Bb5, BGM_HOLD, N_A5, N_G5, N_F5, BGM_HOLD, N_G5, N_A5,
    N_G5, BGM_HOLD, N_F5, N_E5, N_D5, BGM_HOLD, N_E5, N_F5,
    N_E5, N_F5, N_G5, N_A5, N_Bb5, N_A5, N_G5, N_E5,
    N_A5, BGM_HOLD, BGM_HOLD, N_G5, N_F5, N_E5, N_D5, BGM_HOLD
};
static const uint8_t BOSS_ARP[64] = {
    N_D4, N_A3, N_F4, N_A3, N_D4, N_A3, N_Eb4, N_A3,
    N_D4, N_A3, N_F4, N_A3, N_C4, N_G3, N_E4, N_G3,
    N_Bb3, N_F3, N_D4, N_F3, N_A3, N_E3, N_Cs4, N_E3,
    N_A3, N_E3, N_Cs4, N_E3, N_D4, N_A3, N_F4, N_A3,
    N_Bb3, N_F4, N_D4, N_F4, N_F3, N_C4, N_A3, N_C4,
    N_G3, N_D4, N_Bb3, N_D4, N_D4, N_A3, N_F4, N_A3,
    N_C4, N_G4, N_E4, N_G4, N_A3, N_E4, N_Cs4, N_E4,
    N_A3, N_Cs4, N_E4, N_G4, N_F4, N_E4, N_D4, N_A3
};
static const uint8_t BOSS_BASS[64] = {
    N_D2, N_D2, N_D3, N_D2, N_Eb2, N_D2, N_C2, N_D2,
    N_D2, N_D2, N_D3, N_D2, N_C2, N_C2, N_C3, N_C2,
    N_Bb2, N_Bb2, N_Bb2, N_A2, N_A2, N_A2, N_A3, N_A2,
    N_A2, N_G2, N_F2, N_E2, N_D2, N_D2, N_A2, N_D2,
    N_Bb2, N_Bb2, N_Bb2, N_Bb2, N_F2, N_F2, N_F3, N_F2,
    N_G2, N_G2, N_G3, N_G2, N_D2, N_D2, N_D3, N_D2,
    N_C3, N_C3, N_C3, N_C3, N_A2, N_A2, N_A3, N_A2,
    N_A2, N_A2, N_A2, N_G2, N_F2, N_E2, N_D2, N_A2
};
static const uint8_t BOSS_DRUM[64] = {
    3, 3, 2, 1, 3, 1, 2, 3,  3, 3, 2, 1, 3, 2, 2, 2,
    3, 3, 2, 1, 3, 1, 2, 3,  3, 1, 2, 1, 3, 2, 3, 2,
    3, 3, 2, 1, 3, 1, 2, 3,  3, 3, 2, 1, 3, 2, 2, 1,
    3, 1, 2, 1, 3, 3, 2, 2,  3, 2, 3, 2, 2, 2, 3, 2
};

/* Track 3: Victory Fanfare — Bright C Major Celebration */
static const uint8_t VICTORY_LEAD[32] = {
    N_C5, N_C5, N_C5, BGM_HOLD, N_C5, BGM_HOLD, N_G4, N_A4,
    N_B4, BGM_HOLD, N_C5, BGM_HOLD, N_D5, N_E5, N_G5, BGM_HOLD,
    N_E5, N_C5, N_D5, N_E5, N_F5, BGM_HOLD, N_E5, N_D5,
    N_C5, BGM_HOLD, N_E5, N_G5, N_C6, BGM_HOLD, BGM_HOLD, BGM_REST
};
static const uint8_t VICTORY_ARP[32] = {
    N_C4, N_E4, N_G4, N_E4, N_C4, N_E4, N_G4, N_E4,
    N_G3, N_B3, N_D4, N_B3, N_G3, N_B3, N_D4, N_B3,
    N_A3, N_C4, N_E4, N_C4, N_F3, N_A3, N_C4, N_A3,
    N_G3, N_C4, N_E4, N_G4, N_C5, N_G4, N_E4, N_C4
};
static const uint8_t VICTORY_BASS[32] = {
    N_C3, BGM_REST, N_G2, BGM_REST, N_C3, BGM_REST, N_G2, BGM_REST,
    N_G2, BGM_REST, N_D3, BGM_REST, N_G2, BGM_REST, N_B2, BGM_REST,
    N_A2, BGM_REST, N_E3, BGM_REST, N_F2, BGM_REST, N_C3, BGM_REST,
    N_G2, BGM_REST, N_G2, BGM_REST, N_C3, BGM_HOLD, N_G2, N_C3
};
static const uint8_t VICTORY_DRUM[32] = {
    3, 2, 2, 1, 3, 1, 2, 1,  3, 1, 2, 1, 3, 2, 2, 1,
    3, 1, 2, 1, 3, 1, 2, 1,  3, 1, 2, 2, 3, 1, 2, 1
};

/* Track 1: Alderbrook Overture — "Steam & Pine" (96 steps, 6/8 Waltz, C Major -> Am -> F -> G) */
static const uint8_t ALDER_LEAD[96] = {
    /* Section A: Mountain Dusk Waltz */
    N_E4, BGM_HOLD, N_G4, N_C5, BGM_HOLD, N_B4,
    N_A4, BGM_HOLD, N_G4, N_E4, BGM_HOLD, BGM_REST,
    N_F4, BGM_HOLD, N_A4, N_G4, BGM_HOLD, N_F4,
    N_E4, BGM_HOLD, N_D4, N_C4, BGM_HOLD, BGM_REST,
    N_E4, BGM_HOLD, N_G4, N_C5, BGM_HOLD, N_D5,
    N_E5, BGM_HOLD, N_D5, N_C5, BGM_HOLD, N_A4,
    N_G4, BGM_HOLD, N_A4, N_B4, BGM_HOLD, N_D5,
    N_C5, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST,
    /* Section B: Distant Steam Whistle & Warm Hearth Swell */
    N_A4, BGM_HOLD, N_C5, N_E5, BGM_HOLD, N_D5,
    N_C5, BGM_HOLD, N_B4, N_A4, BGM_HOLD, N_G4,
    N_F4, BGM_HOLD, N_A4, N_C5, BGM_HOLD, N_B4,
    N_A4, BGM_HOLD, N_G4, N_E4, BGM_HOLD, BGM_REST,
    N_D5, BGM_HOLD, N_E5, N_F5, BGM_HOLD, N_E5,
    N_D5, BGM_HOLD, N_C5, N_B4, BGM_HOLD, N_A4,
    N_G4, BGM_HOLD, N_B4, N_D5, BGM_HOLD, N_B4,
    N_C5, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t ALDER_ARP[96] = {
    N_C4, N_E4, N_G4, N_E4, N_G4, N_C5,  N_A3, N_C4, N_E4, N_C4, N_E4, N_A4,
    N_F3, N_A3, N_C4, N_A3, N_C4, N_F4,  N_G3, N_B3, N_D4, N_G3, N_C4, N_E4,
    N_C4, N_E4, N_G4, N_E4, N_G4, N_C5,  N_A3, N_C4, N_E4, N_F3, N_A3, N_C4,
    N_G3, N_B3, N_D4, N_G3, N_B3, N_F4,  N_C4, N_E4, N_G4, N_E4, N_C4, N_G3,
    N_A3, N_C4, N_E4, N_A4, N_E4, N_C4,  N_E3, N_G3, N_B3, N_E4, N_B3, N_G3,
    N_F3, N_A3, N_C4, N_F4, N_C4, N_A3,  N_C4, N_E4, N_G4, N_E4, N_C4, N_G3,
    N_D3, N_F3, N_A3, N_D4, N_A3, N_F3,  N_F3, N_A3, N_C4, N_A3, N_F3, N_C4,
    N_G3, N_B3, N_D4, N_G4, N_D4, N_B3,  N_C4, N_E4, N_G4, N_C5, N_G4, N_E4
};
static const uint8_t ALDER_BASS[96] = {
    N_C3, BGM_HOLD, N_G2, BGM_REST, N_G2, BGM_REST,  N_A2, BGM_HOLD, N_E3, BGM_REST, N_E3, BGM_REST,
    N_F2, BGM_HOLD, N_C3, BGM_REST, N_C3, BGM_REST,  N_G2, BGM_HOLD, N_D3, N_C3, BGM_HOLD, BGM_REST,
    N_C3, BGM_HOLD, N_G2, BGM_REST, N_G2, BGM_REST,  N_A2, BGM_HOLD, N_E3, N_F2, BGM_HOLD, N_C3,
    N_G2, BGM_HOLD, N_D3, BGM_REST, N_G2, BGM_REST,  N_C3, BGM_HOLD, N_G2, N_C3, BGM_HOLD, BGM_REST,
    N_A2, BGM_HOLD, N_E3, BGM_REST, N_E3, BGM_REST,  N_E2, BGM_HOLD, N_B2, BGM_REST, N_B2, BGM_REST,
    N_F2, BGM_HOLD, N_C3, BGM_REST, N_C3, BGM_REST,  N_C3, BGM_HOLD, N_G2, BGM_REST, N_G2, BGM_REST,
    N_D2, BGM_HOLD, N_A2, BGM_REST, N_A2, BGM_REST,  N_F2, BGM_HOLD, N_C3, BGM_REST, N_C3, BGM_REST,
    N_G2, BGM_HOLD, N_D3, BGM_REST, N_G2, BGM_REST,  N_C3, BGM_HOLD, N_G2, N_C3, BGM_HOLD, BGM_REST
};
static const uint8_t ALDER_DRUM[96] = {
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 0, 0,
    3, 0, 1, 0, 1, 0,  3, 0, 1, 3, 0, 1,  3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 0, 0,
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,  3, 0, 0, 0, 0, 0
};

/* Track 2: Whiskerburgh — Jaunty Mouse Market Jig (32 steps, F Major) */
static const uint8_t WHISK_LEAD[32] = {
    N_F4, N_A4, N_C5, N_A4, N_D5, N_C5, N_Bb4, N_A4,
    N_G4, N_Bb4, N_D5, N_C5, N_A4, BGM_HOLD, N_F4, BGM_REST,
    N_C5, N_D5, N_F5, N_E5, N_D5, N_C5, N_Bb4, N_G4,
    N_A4, N_C5, N_Bb4, N_G4, N_F4, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t WHISK_ARP[32] = {
    N_F3, N_A3, N_C4, N_A3, N_Bb3, N_D4, N_F4, N_D4,
    N_C4, N_E4, N_G4, N_E4, N_F3, N_A3, N_C4, N_A3,
    N_A3, N_C4, N_F4, N_C4, N_Bb3, N_D4, N_G4, N_D4,
    N_C4, N_E4, N_G4, N_E4, N_F3, N_A3, N_C4, N_F4
};
static const uint8_t WHISK_BASS[32] = {
    N_F2, N_C3, N_F2, N_C3, N_Bb2, N_F3, N_Bb2, N_F2,
    N_C3, N_G2, N_C3, N_G2, N_F2, N_C3, N_F2, BGM_REST,
    N_F2, N_C3, N_A2, N_C3, N_Bb2, N_F3, N_G2, N_D3,
    N_C3, N_G2, N_C3, N_G2, N_F2, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t WHISK_DRUM[32] = {
    3, 1, 2, 1, 3, 1, 2, 1,  3, 1, 2, 1, 3, 1, 2, 0,
    3, 1, 2, 1, 3, 1, 2, 1,  3, 1, 2, 1, 3, 2, 1, 0
};

/* Track 3: Sunken Sewers — Mysterious Eerie Minor Pluck (32 steps, E Minor) */
static const uint8_t SEWER_LEAD[32] = {
    N_E4, BGM_REST, N_G4, BGM_REST, N_Fs4, N_E4, N_B3, BGM_HOLD,
    N_C4, BGM_REST, N_B3, BGM_REST, N_A3, BGM_HOLD, BGM_REST, BGM_REST,
    N_E4, N_Fs4, N_G4, BGM_REST, N_A4, N_G4, N_Fs4, BGM_HOLD,
    N_D4, BGM_REST, N_Fs4, N_E4, N_E4, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t SEWER_ARP[32] = {
    N_E3, N_B3, N_G3, N_B3, N_E3, N_B3, N_Fs3, N_B3,
    N_C3, N_G3, N_E3, N_G3, N_B2, N_Fs3, N_D3, N_Fs3,
    N_E3, N_B3, N_G3, N_B3, N_A2, N_E3, N_C3, N_E3,
    N_B2, N_Fs3, N_Eb3, N_Fs3, N_E3, N_B3, N_G3, BGM_REST
};
static const uint8_t SEWER_BASS[32] = {
    N_E2, BGM_REST, BGM_REST, N_E2, N_G2, BGM_REST, N_Fs2, BGM_REST,
    N_C2, BGM_REST, BGM_REST, N_C2, N_B2, BGM_REST, N_B2, BGM_REST,
    N_E2, BGM_REST, N_G2, BGM_REST, N_A2, BGM_REST, N_Fs2, BGM_REST,
    N_B2, BGM_REST, N_B2, BGM_REST, N_E2, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t SEWER_DRUM[32] = {
    3, 0, 0, 1, 0, 0, 2, 0,  3, 0, 0, 1, 0, 0, 1, 0,
    3, 0, 1, 0, 0, 0, 2, 0,  3, 0, 1, 0, 3, 0, 0, 0
};

/* Track 4: Boarhold Citadel — Brassy Iron March (32 steps, G Minor) */
static const uint8_t BOAR_LEAD[32] = {
    N_G4, BGM_HOLD, N_G4, N_A4, N_Bb4, BGM_HOLD, N_A4, N_G4,
    N_F4, BGM_HOLD, N_G4, N_A4, N_D4, BGM_HOLD, BGM_REST, N_D4,
    N_Bb4, BGM_HOLD, N_C5, N_D5, N_C5, N_Bb4, N_A4, N_G4,
    N_A4, BGM_HOLD, N_Fs4, N_A4, N_G4, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t BOAR_ARP[32] = {
    N_G3, N_Bb3, N_D4, N_Bb3, N_G3, N_Bb3, N_D4, N_Bb3,
    N_F3, N_A3, N_C4, N_A3, N_D3, N_Fs3, N_A3, N_Fs3,
    N_Eb3, N_G3, N_Bb3, N_G3, N_C3, N_Eb3, N_G3, N_Eb3,
    N_D3, N_Fs3, N_A3, N_Fs3, N_G3, N_Bb3, N_D4, N_G3
};
static const uint8_t BOAR_BASS[32] = {
    N_G2, N_G2, N_D2, N_G2, N_G2, N_G2, N_F2, N_G2,
    N_F2, N_F2, N_C2, N_F2, N_D2, N_D2, N_A2, N_D2,
    N_Eb2, N_Eb2, N_Bb2, N_Eb2, N_C2, N_C2, N_G2, N_C2,
    N_D2, N_D2, N_D2, N_D2, N_G2, BGM_HOLD, N_D2, N_G2
};
static const uint8_t BOAR_DRUM[32] = {
    3, 1, 2, 3, 3, 1, 2, 1,  3, 1, 2, 3, 3, 2, 2, 1,
    3, 1, 2, 3, 3, 1, 2, 1,  3, 2, 2, 2, 3, 1, 2, 2
};

/* Track 5: "The Hollow Mirror" — Shadow Grandmother Theme (64 steps, C Minor) */
static const uint8_t SHADOW_LEAD[64] = {
    N_C5, BGM_HOLD, N_Eb5, BGM_HOLD, N_D5, BGM_HOLD, N_B4, BGM_HOLD,
    N_C5, BGM_HOLD, N_Gs4, BGM_HOLD, N_G4, BGM_HOLD, BGM_REST, BGM_REST,
    N_Eb5, BGM_HOLD, N_Fs5, BGM_HOLD, N_F5, BGM_HOLD, N_D5, BGM_HOLD,
    N_Eb5, BGM_HOLD, N_D5, N_B4, N_C5, BGM_HOLD, BGM_REST, BGM_REST,
    N_G5, BGM_HOLD, N_Fs5, BGM_HOLD, N_F5, BGM_HOLD, N_Eb5, BGM_HOLD,
    N_D5, BGM_HOLD, N_C5, BGM_HOLD, N_B4, BGM_HOLD, N_G4, BGM_HOLD,
    N_Gs4, BGM_HOLD, N_C5, BGM_HOLD, N_B4, BGM_HOLD, N_D5, BGM_HOLD,
    N_C5, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST, BGM_REST, BGM_REST
};
static const uint8_t SHADOW_ARP[64] = {
    N_C4, N_Eb4, N_G4, BGM_REST, N_B3, N_D4, N_G4, BGM_REST,
    N_Gs3, N_C4, N_Eb4, BGM_REST, N_G3, N_B3, N_D4, BGM_REST,
    N_C4, N_Eb4, N_Fs4, BGM_REST, N_B3, N_D4, N_F4, BGM_REST,
    N_Gs3, N_C4, N_G3, N_B3, N_C4, BGM_HOLD, BGM_REST, BGM_REST,
    N_C4, N_G4, N_Eb4, N_G4, N_B3, N_G4, N_D4, N_G4,
    N_Gs3, N_Eb4, N_C4, N_Eb4, N_G3, N_D4, N_B3, N_D4,
    N_F3, N_C4, N_Gs3, N_C4, N_G3, N_D4, N_B3, N_D4,
    N_C4, N_Eb4, N_G4, BGM_HOLD, BGM_REST, BGM_REST, BGM_REST, BGM_REST
};
static const uint8_t SHADOW_BASS[64] = {
    N_C2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_B2, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_Gs2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_G2, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_C2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_F2, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_Gs2, BGM_HOLD, N_G2, BGM_HOLD, N_C2, BGM_HOLD, BGM_REST, BGM_REST,
    N_C2, BGM_HOLD, BGM_HOLD, N_C2, N_B2, BGM_HOLD, BGM_HOLD, N_B2,
    N_Gs2, BGM_HOLD, BGM_HOLD, N_Gs2, N_G2, BGM_HOLD, BGM_HOLD, N_G2,
    N_F2, BGM_HOLD, BGM_HOLD, N_F2, N_G2, BGM_HOLD, BGM_HOLD, N_G2,
    N_C2, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST, BGM_REST, BGM_REST
};
static const uint8_t SHADOW_DRUM[64] = {
    3, 0, 0, 0, 0, 0, 1, 0,  3, 0, 0, 0, 0, 0, 1, 0,
    3, 0, 0, 0, 0, 0, 1, 0,  3, 0, 0, 0, 3, 0, 0, 0,
    3, 0, 1, 0, 3, 0, 1, 0,  3, 0, 1, 0, 3, 0, 2, 0,
    3, 0, 1, 0, 3, 0, 2, 0,  3, 0, 0, 0, 0, 0, 0, 0
};

/* Track 9: "Beyond the Hollow Oak" — Evertale Wonder Suite (64 steps, D Major) */
static const uint8_t WONDER_LEAD[64] = {
    N_D5, BGM_HOLD, N_Fs5, N_A5, N_G5, BGM_HOLD, N_Fs5, N_E5,
    N_D5, BGM_HOLD, N_B4, N_D5, N_A4, BGM_HOLD, BGM_REST, N_A4,
    N_B4, BGM_HOLD, N_D5, N_Fs5, N_E5, BGM_HOLD, N_D5, N_B4,
    N_A4, BGM_HOLD, N_B4, N_Cs5, N_D5, BGM_HOLD, BGM_REST, BGM_REST,
    N_Fs5, BGM_HOLD, N_G5, N_A5, N_B5, BGM_HOLD, N_A5, N_Fs5,
    N_E5, BGM_HOLD, N_Fs5, N_G5, N_Fs5, BGM_HOLD, N_D5, N_B4,
    N_A4, BGM_HOLD, N_D5, N_Fs5, N_E5, BGM_HOLD, N_Cs5, N_E5,
    N_D5, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST, BGM_REST, BGM_REST
};
static const uint8_t WONDER_ARP[64] = {
    N_D4, N_Fs4, N_A4, N_D5, N_G3, N_B3, N_D4, N_G4,
    N_B3, N_D4, N_Fs4, N_B4, N_A3, N_Cs4, N_E4, N_A4,
    N_G3, N_B3, N_D4, N_G4, N_D4, N_Fs4, N_A4, N_D5,
    N_A3, N_Cs4, N_E4, N_G4, N_D4, N_Fs4, N_A4, N_D5,
    N_D4, N_Fs4, N_A4, N_D5, N_G4, N_B4, N_D5, N_B4,
    N_A3, N_Cs4, N_E4, N_A4, N_B3, N_D4, N_Fs4, N_B4,
    N_G3, N_B3, N_D4, N_G4, N_A3, N_Cs4, N_E4, N_A4,
    N_D4, N_Fs4, N_A4, N_D5, N_A4, N_Fs4, N_D4, BGM_REST
};
static const uint8_t WONDER_BASS[64] = {
    N_D3, BGM_HOLD, N_A2, BGM_HOLD, N_G2, BGM_HOLD, N_D3, BGM_HOLD,
    N_B2, BGM_HOLD, N_Fs2, BGM_HOLD, N_A2, BGM_HOLD, N_E2, BGM_HOLD,
    N_G2, BGM_HOLD, N_D3, BGM_HOLD, N_D3, BGM_HOLD, N_A2, BGM_HOLD,
    N_A2, BGM_HOLD, N_E3, BGM_HOLD, N_D3, BGM_HOLD, N_A2, BGM_REST,
    N_D3, BGM_HOLD, N_A2, BGM_HOLD, N_G2, BGM_HOLD, N_D3, BGM_HOLD,
    N_A2, BGM_HOLD, N_E3, BGM_HOLD, N_B2, BGM_HOLD, N_Fs2, BGM_HOLD,
    N_G2, BGM_HOLD, N_D3, BGM_HOLD, N_A2, BGM_HOLD, N_E3, BGM_HOLD,
    N_D3, BGM_HOLD, N_A2, BGM_HOLD, N_D3, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t WONDER_DRUM[64] = {
    3, 0, 1, 0, 0, 0, 1, 0,  3, 0, 1, 0, 0, 0, 1, 0,
    3, 0, 1, 0, 3, 0, 1, 0,  3, 0, 1, 0, 3, 0, 0, 0,
    3, 0, 1, 0, 3, 0, 1, 0,  3, 0, 1, 0, 3, 0, 1, 0,
    3, 0, 1, 0, 3, 0, 1, 0,  3, 0, 0, 0, 0, 0, 0, 0
};

/* Track 10: "Lantern Lullaby" — Grandma's Comfort & Pip's Window (64 steps, F Major) */
static const uint8_t LULLABY_LEAD[64] = {
    N_A4, BGM_HOLD, N_C5, BGM_HOLD, N_D5, BGM_HOLD, N_C5, N_A4,
    N_G4, BGM_HOLD, N_A4, BGM_HOLD, N_F4, BGM_HOLD, BGM_REST, BGM_REST,
    N_Bb4, BGM_HOLD, N_D5, BGM_HOLD, N_C5, BGM_HOLD, N_A4, N_G4,
    N_A4, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST, N_G4, N_A4,
    N_C5, BGM_HOLD, N_F5, BGM_HOLD, N_E5, BGM_HOLD, N_D5, N_C5,
    N_D5, BGM_HOLD, N_C5, N_A4, N_G4, BGM_HOLD, BGM_REST, N_G4,
    N_A4, BGM_HOLD, N_Bb4, N_A4, N_G4, BGM_HOLD, N_E4, N_G4,
    N_F4, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST, BGM_REST, BGM_REST
};
static const uint8_t LULLABY_ARP[64] = {
    N_F3, N_A3, N_C4, N_F4, N_Bb3, N_D4, N_F4, N_D4,
    N_C4, N_E4, N_G4, N_E4, N_F3, N_A3, N_C4, N_A3,
    N_Bb3, N_D4, N_F4, N_D4, N_A3, N_C4, N_F4, N_C4,
    N_D3, N_F3, N_A3, N_D4, N_C4, N_E4, N_G4, N_C4,
    N_F3, N_A3, N_C4, N_F4, N_C4, N_E4, N_G4, N_E4,
    N_Bb3, N_D4, N_F4, N_D4, N_C4, N_E4, N_G4, N_E4,
    N_D3, N_F3, N_A3, N_D4, N_C4, N_E4, N_G4, N_Bb4,
    N_F3, N_A3, N_C4, N_F4, N_C4, N_A3, N_F3, BGM_REST
};
static const uint8_t LULLABY_BASS[64] = {
    N_F2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_Bb2, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_C3, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_F2, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_Bb2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_A2, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_D2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_C3, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_F2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_C3, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_Bb2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_C3, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_D2, BGM_HOLD, BGM_HOLD, BGM_HOLD, N_C3, BGM_HOLD, BGM_HOLD, BGM_HOLD,
    N_F2, BGM_HOLD, BGM_HOLD, BGM_HOLD, BGM_REST, BGM_REST, BGM_REST, BGM_REST
};
static const uint8_t LULLABY_DRUM[64] = {
    0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 0, 0,  0, 0, 1, 0, 0, 0, 0, 0,
    3, 0, 1, 0, 0, 0, 1, 0,  3, 0, 1, 0, 0, 0, 1, 0,
    3, 0, 1, 0, 0, 0, 1, 0,  0, 0, 0, 0, 0, 0, 0, 0
};

/* 11: BGM_DESERT — Sands of Babana (D Phrygian caravan sway, 32 steps) */
static const uint8_t DESERT_LEAD[32] = {
    N_D4, N_E4, N_F4, BGM_HOLD, N_A4, N_G4, N_F4, N_E4,
    N_D4, BGM_HOLD, N_C4, N_D4, N_E4, BGM_HOLD, BGM_REST, N_E4,
    N_F4, N_G4, N_A4, BGM_HOLD, N_Bb4, N_A4, N_G4, N_F4,
    N_E4, N_F4, N_E4, N_D4, N_D4, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t DESERT_ARP[32] = {
    N_D3, N_A3, N_F3, N_A3, N_D3, N_A3, N_F3, N_A3,
    N_D3, N_A3, N_E3, N_A3, N_C4, N_A3, N_E3, N_A3,
    N_F3, N_C4, N_A3, N_C4, N_Bb3, N_F4, N_D4, N_F4,
    N_A3, N_E4, N_C4, N_E4, N_D3, N_A3, N_F3, N_A3
};
static const uint8_t DESERT_BASS[32] = {
    N_D2, BGM_HOLD, N_A2, BGM_REST, N_D2, BGM_HOLD, N_A2, BGM_REST,
    N_D2, BGM_HOLD, N_A2, BGM_REST, N_C3, BGM_HOLD, N_A2, BGM_REST,
    N_F2, BGM_HOLD, N_C3, BGM_REST, N_Bb2, BGM_HOLD, N_F2, BGM_REST,
    N_A2, BGM_HOLD, N_E2, BGM_REST, N_D2, BGM_HOLD, N_A2, BGM_REST
};
static const uint8_t DESERT_DRUM[32] = {
    3, 0, 1, 2, 3, 0, 1, 0,  3, 0, 1, 2, 3, 1, 2, 1,
    3, 0, 1, 2, 3, 0, 1, 0,  3, 1, 2, 1, 3, 0, 1, 0
};

/* 12: BGM_PARADISE — Suncrest Coral Reefs (breezy C major island calypso, 32 steps) */
static const uint8_t PARADISE_LEAD[32] = {
    N_E4, N_G4, N_C5, BGM_HOLD, N_B4, N_A4, N_G4, BGM_REST,
    N_F4, N_A4, N_D5, BGM_HOLD, N_C5, N_B4, N_A4, N_G4,
    N_E4, N_G4, N_C5, N_D5, N_E5, BGM_HOLD, N_D5, N_C5,
    N_A4, N_B4, N_C5, BGM_HOLD, N_G4, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t PARADISE_ARP[32] = {
    N_C4, N_E4, N_G4, N_E4, N_C4, N_E4, N_G4, N_E4,
    N_F3, N_A3, N_C4, N_A3, N_G3, N_B3, N_D4, N_B3,
    N_C4, N_E4, N_G4, N_E4, N_A3, N_C4, N_E4, N_C4,
    N_F3, N_A3, N_G3, N_B3, N_C4, N_E4, N_G4, N_E4
};
static const uint8_t PARADISE_BASS[32] = {
    N_C3, BGM_REST, N_G2, BGM_HOLD, N_C3, BGM_REST, N_G2, BGM_HOLD,
    N_F2, BGM_REST, N_C3, BGM_HOLD, N_G2, BGM_REST, N_D3, BGM_HOLD,
    N_C3, BGM_REST, N_G2, BGM_HOLD, N_A2, BGM_REST, N_E3, BGM_HOLD,
    N_F2, BGM_REST, N_G2, BGM_HOLD, N_C3, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t PARADISE_DRUM[32] = {
    3, 0, 1, 0, 2, 0, 1, 1,  3, 0, 1, 0, 2, 0, 1, 0,
    3, 0, 1, 0, 2, 0, 1, 1,  3, 0, 2, 1, 3, 0, 1, 0
};

/* 13: BGM_FROST — Frostmere North (festive sparkling winter chimes, 32 steps) */
static const uint8_t FROST_LEAD[32] = {
    N_G4, BGM_HOLD, N_C5, N_B4, N_A4, BGM_HOLD, N_G4, BGM_REST,
    N_F4, N_G4, N_A4, N_C5, N_B4, BGM_HOLD, N_G4, BGM_REST,
    N_E5, BGM_HOLD, N_D5, N_C5, N_A4, BGM_HOLD, N_F4, N_A4,
    N_G4, N_C5, N_B4, N_D5, N_C5, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t FROST_ARP[32] = {
    N_C4, N_G4, N_E4, N_G4, N_A3, N_E4, N_C4, N_E4,
    N_F3, N_C4, N_A3, N_C4, N_G3, N_D4, N_B3, N_D4,
    N_C4, N_G4, N_E4, N_G4, N_F3, N_C4, N_A3, N_C4,
    N_G3, N_D4, N_G3, N_B3, N_C4, N_E4, N_G4, N_C5
};
static const uint8_t FROST_BASS[32] = {
    N_C3, BGM_HOLD, BGM_HOLD, BGM_REST, N_A2, BGM_HOLD, BGM_HOLD, BGM_REST,
    N_F2, BGM_HOLD, BGM_HOLD, BGM_REST, N_G2, BGM_HOLD, BGM_HOLD, BGM_REST,
    N_C3, BGM_HOLD, BGM_HOLD, BGM_REST, N_F2, BGM_HOLD, BGM_HOLD, BGM_REST,
    N_G2, BGM_HOLD, N_G2, BGM_HOLD, N_C3, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t FROST_DRUM[32] = {
    0, 0, 1, 0, 0, 0, 1, 0,  0, 0, 1, 0, 0, 0, 1, 0,
    3, 0, 1, 0, 0, 0, 1, 0,  3, 0, 1, 0, 1, 0, 0, 0
};

/* 14: BGM_JUNGLE — Canopy Wilds (bouncy pentatonic rainforest groove, 32 steps) */
static const uint8_t JUNGLE_LEAD[32] = {
    N_A4, N_C5, N_D5, BGM_REST, N_E5, N_D5, N_C5, N_A4,
    N_G4, N_A4, N_C5, BGM_HOLD, N_A4, BGM_REST, N_G4, N_E4,
    N_A4, N_C5, N_D5, N_E5, N_G5, BGM_HOLD, N_E5, N_D5,
    N_C5, N_D5, N_C5, N_A4, N_A4, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t JUNGLE_ARP[32] = {
    N_A3, N_E4, N_C4, N_E4, N_D4, N_A3, N_C4, N_E4,
    N_G3, N_D4, N_B3, N_D4, N_A3, N_E4, N_C4, N_A3,
    N_A3, N_E4, N_C4, N_E4, N_C4, N_G4, N_E4, N_G4,
    N_G3, N_D4, N_E3, N_B3, N_A3, N_C4, N_E4, N_A3
};
static const uint8_t JUNGLE_BASS[32] = {
    N_A2, BGM_REST, N_E2, N_A2, N_D2, BGM_REST, N_A2, N_D2,
    N_G2, BGM_REST, N_D2, N_G2, N_A2, BGM_REST, N_E2, N_G2,
    N_A2, BGM_REST, N_E2, N_A2, N_C3, BGM_REST, N_G2, N_C3,
    N_G2, BGM_REST, N_E2, N_G2, N_A2, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t JUNGLE_DRUM[32] = {
    3, 1, 2, 1, 3, 2, 1, 1,  3, 1, 2, 3, 3, 1, 2, 1,
    3, 1, 2, 1, 3, 2, 1, 1,  3, 2, 2, 1, 3, 1, 2, 0
};

/* 15: BGM_VOLCANO — Cinderpeak Caldera (heavy volcanic march, 32 steps) */
static const uint8_t VOLCANO_LEAD[32] = {
    N_D4, BGM_HOLD, N_F4, N_E4, N_D4, BGM_HOLD, N_A3, BGM_REST,
    N_Bb3, BGM_HOLD, N_C4, N_D4, N_E4, BGM_HOLD, BGM_REST, BGM_REST,
    N_F4, BGM_HOLD, N_G4, N_F4, N_E4, BGM_HOLD, N_D4, N_C4,
    N_D4, N_E4, N_F4, N_E4, N_D4, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t VOLCANO_ARP[32] = {
    N_D3, N_A3, N_D3, N_A3, N_D3, N_F3, N_A3, N_F3,
    N_Bb2, N_F3, N_Bb2, N_F3, N_A2, N_E3, N_A2, N_E3,
    N_D3, N_A3, N_F3, N_A3, N_C3, N_G3, N_E3, N_G3,
    N_Bb2, N_F3, N_A2, N_E3, N_D3, N_A3, N_D3, BGM_REST
};
static const uint8_t VOLCANO_BASS[32] = {
    N_D2, N_D2, N_A2, N_D2, N_D2, N_D2, N_F2, N_D2,
    N_Bb2, N_Bb2, N_F2, N_Bb2, N_A2, N_A2, N_E2, N_A2,
    N_D2, N_D2, N_F2, N_D2, N_C3, N_C3, N_G2, N_C3,
    N_Bb2, N_Bb2, N_A2, N_A2, N_D2, BGM_HOLD, N_A2, N_D2
};
static const uint8_t VOLCANO_DRUM[32] = {
    3, 0, 2, 3, 3, 0, 2, 1,  3, 0, 2, 3, 3, 2, 2, 1,
    3, 0, 2, 3, 3, 0, 2, 1,  3, 2, 3, 2, 3, 0, 2, 2
};

/* 16: BGM_TECH — Aetheris Citadel (crystalline sci-fi synth pulse, 32 steps) */
static const uint8_t TECH_LEAD[32] = {
    N_E5, BGM_REST, N_B4, N_E5, N_G5, BGM_HOLD, N_F5, N_E5,
    N_D5, BGM_REST, N_A4, N_D5, N_E5, BGM_HOLD, BGM_REST, BGM_REST,
    N_C5, N_E5, N_G5, N_A5, N_G5, BGM_HOLD, N_E5, N_D5,
    N_E5, N_G5, N_F5, N_D5, N_E5, BGM_HOLD, BGM_REST, BGM_REST
};
static const uint8_t TECH_ARP[32] = {
    N_E4, N_B4, N_E4, N_G4, N_E4, N_B4, N_G4, N_B4,
    N_D4, N_A4, N_D4, N_F4, N_E4, N_B4, N_G4, N_B4,
    N_C4, N_G4, N_E4, N_G4, N_D4, N_A4, N_F4, N_A4,
    N_E4, N_B4, N_D4, N_A4, N_E4, N_G4, N_B4, N_E5
};
static const uint8_t TECH_BASS[32] = {
    N_E2, N_E3, N_E2, N_E3, N_E2, N_E3, N_G2, N_E2,
    N_D2, N_D3, N_D2, N_D3, N_E2, N_E3, N_B2, N_E2,
    N_C3, N_C3, N_G2, N_C3, N_D2, N_D3, N_A2, N_D2,
    N_E2, N_E3, N_D2, N_D3, N_E2, BGM_HOLD, N_B2, N_E2
};
static const uint8_t TECH_DRUM[32] = {
    3, 1, 1, 2, 3, 1, 1, 2,  3, 1, 1, 2, 3, 1, 2, 1,
    3, 1, 1, 2, 3, 1, 1, 2,  3, 1, 2, 1, 3, 1, 2, 1
};

static const BgmTrack BGM_TRACKS[BGM_COUNT] = {
    /* 0: BGM_HAMLET         */ { 48, 240, HAMLET_LEAD,   HAMLET_ARP,   HAMLET_BASS,   HAMLET_DRUM   },
    /* 1: BGM_ALDERBROOK     */ { 96, 245, ALDER_LEAD,    ALDER_ARP,    ALDER_BASS,    ALDER_DRUM    },
    /* 2: BGM_WHISKERBURGH   */ { 32, 185, WHISK_LEAD,    WHISK_ARP,    WHISK_BASS,    WHISK_DRUM    },
    /* 3: BGM_SEWERS         */ { 32, 235, SEWER_LEAD,    SEWER_ARP,    SEWER_BASS,    SEWER_DRUM    },
    /* 4: BGM_BOARHOLD       */ { 32, 175, BOAR_LEAD,     BOAR_ARP,     BOAR_BASS,     BOAR_DRUM     },
    /* 5: BGM_SHADOW         */ { 64, 265, SHADOW_LEAD,   SHADOW_ARP,   SHADOW_BASS,   SHADOW_DRUM   },
    /* 6: BGM_BATTLE         */ { 64, 155, BATTLE_LEAD,   BATTLE_ARP,   BATTLE_BASS,   BATTLE_DRUM   },
    /* 7: BGM_BOSS           */ { 64, 140, BOSS_LEAD,     BOSS_ARP,     BOSS_BASS,     BOSS_DRUM     },
    /* 8: BGM_VICTORY        */ { 32, 165, VICTORY_LEAD,  VICTORY_ARP,  VICTORY_BASS,  VICTORY_DRUM  },
    /* 9: BGM_CINEMA_WONDER  */ { 64, 215, WONDER_LEAD,   WONDER_ARP,   WONDER_BASS,   WONDER_DRUM   },
    /* 10: BGM_CINEMA_LULLABY*/ { 64, 270, LULLABY_LEAD,  LULLABY_ARP,  LULLABY_BASS,  LULLABY_DRUM  },
    /* 11: BGM_DESERT        */ { 32, 195, DESERT_LEAD,   DESERT_ARP,   DESERT_BASS,   DESERT_DRUM   },
    /* 12: BGM_PARADISE      */ { 32, 185, PARADISE_LEAD, PARADISE_ARP, PARADISE_BASS, PARADISE_DRUM },
    /* 13: BGM_FROST         */ { 32, 225, FROST_LEAD,    FROST_ARP,    FROST_BASS,    FROST_DRUM    },
    /* 14: BGM_JUNGLE        */ { 32, 170, JUNGLE_LEAD,   JUNGLE_ARP,   JUNGLE_BASS,   JUNGLE_DRUM   },
    /* 15: BGM_VOLCANO       */ { 32, 180, VOLCANO_LEAD,  VOLCANO_ARP,  VOLCANO_BASS,  VOLCANO_DRUM  },
    /* 16: BGM_TECH          */ { 32, 160, TECH_LEAD,     TECH_ARP,     TECH_BASS,     TECH_DRUM     }
};

typedef struct {
    uint8_t  wave;
    uint32_t phase_acc;
    uint32_t phase_inc;
    int32_t  base_vol;
    int32_t  env_fp;      /* 0..65536 */
    int32_t  decay_fp;    /* per-sample linear/exp-like decay */
    bool     gate;
    uint16_t lfsr;
} BgmChannel;

typedef struct {
    uint8_t    track_id;
    uint8_t    step_idx;
    int32_t    samples_to_next;
    uint32_t   vib_phase;
    BgmChannel ch[4]; /* 0=lead, 1=arp, 2=bass, 3=drum */
} BgmSequencer;

static BgmSequencer g_bgm;

static uint32_t bgm_midi_to_inc(uint8_t midi) {
    if (midi < 36u || midi >= 96u) return 0u;
    uint32_t hz = BGM_NOTE_HZ[midi - 36u];
    return (uint32_t)(((uint64_t)hz << 32) / SYNTH_SAMPLE_RATE);
}

static void bgm_init(void) {
    memset(&g_bgm, 0, sizeof(g_bgm));
    g_bgm.track_id = BGM_ALDERBROOK;
    g_bgm.step_idx = 0;
    g_bgm.samples_to_next = 0;
    g_bgm.ch[0].wave = WAVE_TRIANGLE;
    g_bgm.ch[1].wave = WAVE_SQUARE25;
    g_bgm.ch[2].wave = WAVE_TRIANGLE;
    g_bgm.ch[3].wave = WAVE_NOISE;
    g_bgm.ch[3].lfsr = 0xBEEFu;
}

static void bgm_set_track(uint8_t track_id) {
    if (track_id >= BGM_COUNT || g_bgm.track_id == track_id) return;
    if (g_audio_dev != 0) SDL_LockAudioDevice(g_audio_dev);
    g_bgm.track_id = track_id;
    g_bgm.step_idx = 0;
    g_bgm.samples_to_next = 0;
    for (int i = 0; i < 4; i++) {
        g_bgm.ch[i].gate = false;
        g_bgm.ch[i].env_fp = 0;
    }
    /* Tailor timbres to track mood */
    if (track_id == BGM_HAMLET || track_id == BGM_ALDERBROOK ||
        track_id == BGM_CINEMA_WONDER || track_id == BGM_CINEMA_LULLABY ||
        track_id == BGM_FROST || track_id == BGM_PARADISE) {
        g_bgm.ch[0].wave = WAVE_TRIANGLE;  /* Warm pastoral flute / winter chime */
        g_bgm.ch[1].wave = WAVE_SQUARE25;  /* Plucked harp / celesta arpeggio */
    } else if (track_id == BGM_SEWERS || track_id == BGM_SHADOW) {
        g_bgm.ch[0].wave = WAVE_TRIANGLE;  /* Haunting hollow chime */
        g_bgm.ch[1].wave = WAVE_SQUARE25;
    } else if (track_id == BGM_BOSS || track_id == BGM_BOARHOLD || track_id == BGM_VOLCANO) {
        g_bgm.ch[0].wave = WAVE_SQUARE50;  /* Bold dramatic brass/lead */
        g_bgm.ch[1].wave = WAVE_SQUARE25;
    } else {
        g_bgm.ch[0].wave = WAVE_SQUARE25;  /* Bright heroic/caravan/tech lead */
        g_bgm.ch[1].wave = WAVE_SQUARE50;
    }
    g_bgm.ch[2].wave = WAVE_TRIANGLE;
    g_bgm.ch[3].wave = WAVE_NOISE;
    if (g_audio_dev != 0) SDL_UnlockAudioDevice(g_audio_dev);
}

static void bgm_trigger_step(void) {
    const BgmTrack *tr = &BGM_TRACKS[g_bgm.track_id];
    uint8_t s = g_bgm.step_idx;
    bool pastoral = (g_bgm.track_id == BGM_HAMLET || g_bgm.track_id == BGM_ALDERBROOK ||
                     g_bgm.track_id == BGM_SHADOW || g_bgm.track_id == BGM_CINEMA_WONDER ||
                     g_bgm.track_id == BGM_CINEMA_LULLABY || g_bgm.track_id == BGM_FROST ||
                     g_bgm.track_id == BGM_PARADISE);

    /* Lead */
    uint8_t n0 = tr->lead[s];
    if (n0 == BGM_REST) {
        g_bgm.ch[0].gate = false;
    } else if (n0 != BGM_HOLD) {
        g_bgm.ch[0].phase_inc = bgm_midi_to_inc(n0);
        g_bgm.ch[0].base_vol  = pastoral ? 1850 : 1450;
        g_bgm.ch[0].env_fp    = 65536;
        g_bgm.ch[0].decay_fp  = pastoral ? 3 : 5;
        g_bgm.ch[0].gate      = true;
    }

    /* Harp / Harmony Arpeggio */
    uint8_t n1 = tr->arp[s];
    if (n1 == BGM_REST) {
        g_bgm.ch[1].gate = false;
    } else if (n1 != BGM_HOLD) {
        g_bgm.ch[1].phase_inc = bgm_midi_to_inc(n1);
        g_bgm.ch[1].base_vol  = pastoral ? 760 : 720;
        g_bgm.ch[1].env_fp    = 65536;
        g_bgm.ch[1].decay_fp  = pastoral ? 8 : 10;
        g_bgm.ch[1].gate      = true;
    }

    /* Bass */
    uint8_t n2 = tr->bass[s];
    if (n2 == BGM_REST) {
        g_bgm.ch[2].gate = false;
    } else if (n2 != BGM_HOLD) {
        g_bgm.ch[2].phase_inc = bgm_midi_to_inc(n2);
        g_bgm.ch[2].base_vol  = pastoral ? 1750 : 1850;
        g_bgm.ch[2].env_fp    = 65536;
        g_bgm.ch[2].decay_fp  = pastoral ? 5 : 9;
        g_bgm.ch[2].gate      = true;
    }

    /* Percussion */
    uint8_t d = tr->drum[s];
    if (d != 0u) {
        if (d == 3u) { /* Soft kick thump */
            g_bgm.ch[3].wave      = WAVE_TRIANGLE;
            g_bgm.ch[3].phase_inc = (uint32_t)(((uint64_t)95u << 32) / SYNTH_SAMPLE_RATE);
            g_bgm.ch[3].base_vol  = pastoral ? 900 : 1650;
            g_bgm.ch[3].decay_fp  = 24;
        } else if (d == 2u) { /* Snare */
            g_bgm.ch[3].wave      = WAVE_NOISE;
            g_bgm.ch[3].phase_inc = (uint32_t)(((uint64_t)1800u << 32) / SYNTH_SAMPLE_RATE);
            g_bgm.ch[3].base_vol  = 980;
            g_bgm.ch[3].decay_fp  = 26;
        } else { /* Soft brush / hi-hat */
            g_bgm.ch[3].wave      = WAVE_NOISE;
            g_bgm.ch[3].phase_inc = (uint32_t)(((uint64_t)4200u << 32) / SYNTH_SAMPLE_RATE);
            g_bgm.ch[3].base_vol  = pastoral ? 340 : 520;
            g_bgm.ch[3].decay_fp  = 42;
        }
        g_bgm.ch[3].env_fp = 65536;
        g_bgm.ch[3].gate   = true;
    }

    g_bgm.step_idx = (uint8_t)((s + 1u) % tr->length);
    g_bgm.samples_to_next = (int32_t)(((uint32_t)tr->step_ms * SYNTH_SAMPLE_RATE) / 1000u);
}

static void synth_push_note(uint8_t wave, uint32_t freq_hz, int32_t slide_hz_per_sec, uint32_t ms, int32_t vol) {
    if (g_audio_dev == 0 || g_muted) return;
    int slot = -1;
    for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
        if (!g_voices[i].active) { slot = i; break; }
    }
    if (slot < 0) slot = 0;

    SynthVoice *v = &g_voices[slot];
    v->active        = true;
    v->wave          = wave;
    v->phase_acc     = 0;
    v->phase_inc     = (uint32_t)(((uint64_t)freq_hz << 32) / SYNTH_SAMPLE_RATE);
    v->phase_slide   = (int32_t)(((int64_t)slide_hz_per_sec << 32) / ((int64_t)SYNTH_SAMPLE_RATE * SYNTH_SAMPLE_RATE));
    v->total_samples = (int32_t)((ms * SYNTH_SAMPLE_RATE) / 1000u);
    v->samples_left  = v->total_samples;
    v->volume        = vol;
    v->lfsr          = 0xACE1u;
}

static void synth_play_sfx(uint8_t sfx) {
    if (g_audio_dev == 0 || g_muted || sfx == SFX_NONE) return;
    SDL_LockAudioDevice(g_audio_dev);
    switch (sfx) {
        case SFX_STEP:
            synth_push_note(WAVE_NOISE, 220, -400, 22, 1200);
            break;
        case SFX_GRASS_RUSTLE:
            synth_push_note(WAVE_NOISE, 1400, -1800, 48, 1600);
            break;
        case SFX_WATER_SPLASH:
            synth_push_note(WAVE_NOISE, 900, 1200, 75, 1900);
            synth_push_note(WAVE_TRIANGLE, 520, -600, 60, 1400);
            break;
        case SFX_DASH:
            synth_push_note(WAVE_NOISE, 700, 1800, 90, 2200);
            synth_push_note(WAVE_SQUARE25, 260, 1200, 70, 1400);
            break;
        case SFX_CURSOR:
            synth_push_note(WAVE_SQUARE25, 660, 0, 28, 2200);
            break;
        case SFX_CONFIRM:
            synth_push_note(WAVE_SQUARE25, 587, 600, 55, 2400);
            synth_push_note(WAVE_TRIANGLE, 880, 200, 85, 2200);
            break;
        case SFX_CANCEL:
            synth_push_note(WAVE_SQUARE50, 390, -1200, 80, 2200);
            break;
        case SFX_TEXT_HERO:
            synth_push_note(WAVE_TRIANGLE, 420, 40, 24, 1800);
            break;
        case SFX_TEXT_PIP:
            synth_push_note(WAVE_SQUARE25, 760, 280, 26, 1700);
            break;
        case SFX_TEXT_ELDER:
            synth_push_note(WAVE_TRIANGLE, 260, -40, 28, 2000);
            break;
        case SFX_EMOTE:
            synth_push_note(WAVE_SQUARE25, 740, 2200, 75, 2200);
            break;
        case SFX_ENEMY_ALERT:
            synth_push_note(WAVE_SQUARE50, 310, 1600, 120, 2800);
            break;
        case SFX_BATTLE_SWOOP:
            synth_push_note(WAVE_SQUARE50, 220, 900, 220, 2600);
            synth_push_note(WAVE_NOISE, 600, 1400, 180, 1800);
            break;
        case SFX_SUMMON_ARC:
            synth_push_note(WAVE_TRIANGLE, 440, 1800, 160, 3000);
            synth_push_note(WAVE_SQUARE25, 660, 1400, 140, 2000);
            break;
        case SFX_HIT_NORMAL:
            synth_push_note(WAVE_NOISE, 420, -1400, 70, 3200);
            synth_push_note(WAVE_SQUARE50, 210, -900, 65, 2800);
            break;
        case SFX_HIT_CRITICAL:
            synth_push_note(WAVE_NOISE, 580, -1800, 110, 3800);
            synth_push_note(WAVE_SQUARE25, 523, 1800, 130, 3200);
            synth_push_note(WAVE_TRIANGLE, 784, 1200, 150, 2800);
            break;
        case SFX_PARRY_CLANG:
            synth_push_note(WAVE_SQUARE25, 1175, -400, 110, 3200);
            synth_push_note(WAVE_SQUARE50, 1760, -600, 90, 2600);
            break;
        case SFX_LOCK_BREAK:
            synth_push_note(WAVE_SQUARE25, 988, 1200, 110, 3000);
            synth_push_note(WAVE_NOISE, 1800, -2400, 80, 2400);
            break;
        case SFX_LOCK_ALL_BREAK:
            synth_push_note(WAVE_SQUARE25, 659, 1600, 180, 3200);
            synth_push_note(WAVE_TRIANGLE, 988, 1200, 220, 3000);
            synth_push_note(WAVE_NOISE, 2200, -3000, 140, 2600);
            break;
        case SFX_SPELL_CAST:
            synth_push_note(WAVE_TRIANGLE, 520, 1400, 180, 2800);
            synth_push_note(WAVE_SQUARE25, 780, 900, 140, 2000);
            break;
        case SFX_HEAL:
            synth_push_note(WAVE_TRIANGLE, 523, 700, 200, 2800);
            synth_push_note(WAVE_SQUARE25, 659, 800, 180, 1800);
            break;
        case SFX_HEART_TAKE:
            synth_push_note(WAVE_TRIANGLE, 494, 900, 240, 2800);
            synth_push_note(WAVE_SQUARE25, 740, 600, 200, 1800);
            break;
        case SFX_HEART_GIVE:
            synth_push_note(WAVE_TRIANGLE, 587, 1100, 260, 3000);
            synth_push_note(WAVE_SQUARE25, 880, 800, 220, 2000);
            break;
        case SFX_CHEST_OPEN:
            synth_push_note(WAVE_SQUARE25, 523, 1200, 220, 2800);
            synth_push_note(WAVE_TRIANGLE, 784, 900, 240, 2600);
            break;
        case SFX_VICTORY_FANFARE:
            synth_push_note(WAVE_SQUARE25, 523, 350, 260, 2800);
            synth_push_note(WAVE_TRIANGLE, 659, 450, 320, 2800);
            synth_push_note(WAVE_SQUARE50, 784, 520, 380, 2200);
            break;
        case SFX_HURT:
            synth_push_note(WAVE_SQUARE50, 190, -700, 110, 3200);
            synth_push_note(WAVE_NOISE, 320, -900, 95, 2800);
            break;
        case SFX_COIN:
            synth_push_note(WAVE_SQUARE25, 988, 1200, 65, 2400);
            synth_push_note(WAVE_TRIANGLE, 1319, 200, 95, 2200);
            break;
        case SFX_FISH_BITE:
            synth_push_note(WAVE_SQUARE25, 880, 1600, 85, 2800);
            synth_push_note(WAVE_NOISE, 1100, -1400, 70, 2100);
            break;
        case SFX_FISH_REEL:
            synth_push_note(WAVE_SQUARE25, 520, 400, 30, 1400);
            break;
        case SFX_FISH_CATCH:
            synth_push_note(WAVE_SQUARE25, 587, 900, 160, 2600);
            synth_push_note(WAVE_TRIANGLE, 880, 800, 220, 2600);
            break;
        case SFX_PORTAL:
            synth_push_note(WAVE_TRIANGLE, 392, 1100, 220, 2500);
            synth_push_note(WAVE_SQUARE25, 587, 1400, 180, 1900);
            break;
        default:
            break;
    }
    SDL_UnlockAudioDevice(g_audio_dev);
}

static void SDLCALL synth_audio_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    int16_t *out = (int16_t *)stream;
    int samples = len / (int)sizeof(int16_t);

    for (int s = 0; s < samples; s++) {
        int32_t mix = 0;

        /* 1. Background Music Sequencer (4 Musical Voices) */
        if (!g_muted) {
            if (g_bgm.samples_to_next <= 0) {
                bgm_trigger_step();
            }
            g_bgm.samples_to_next--;
            g_bgm.vib_phase += 520u; /* Gentle ~5.3Hz vibrato for lead flute */

            for (int c = 0; c < 4; c++) {
                BgmChannel *ch = &g_bgm.ch[c];
                if (ch->env_fp <= 0) continue;
                uint32_t prev = ch->phase_acc;
                uint32_t inc  = ch->phase_inc;
                if (c == 0 && g_bgm.track_id == BGM_HAMLET) {
                    int32_t vib = HEARTH_SIN64[(g_bgm.vib_phase >> 26) & 63u];
                    inc = (uint32_t)((int64_t)inc + ((int64_t)inc * vib) / 26000);
                }
                ch->phase_acc += inc;
                if (c == 3 && ch->wave == WAVE_TRIANGLE && ch->phase_inc > 400000u) {
                    ch->phase_inc -= 1800u; /* Pitch drop for punchy kick drum */
                }

                int32_t env = (int32_t)(((int64_t)ch->base_vol * ch->env_fp) >> 16);
                uint32_t top = ch->phase_acc >> 24;
                int32_t sample = 0;
                if (ch->wave == WAVE_SQUARE50) {
                    sample = (top < 128u) ? env : -env;
                } else if (ch->wave == WAVE_SQUARE25) {
                    sample = (top < 64u) ? env : -env;
                } else if (ch->wave == WAVE_TRIANGLE) {
                    int32_t tri = (top < 128u) ? (int32_t)top * 2 - 128 : (int32_t)(255u - top) * 2 - 128;
                    sample = (tri * env) >> 7;
                } else {
                    if (ch->phase_acc < prev) {
                        uint16_t bit = (uint16_t)(((ch->lfsr >> 0) ^ (ch->lfsr >> 2) ^ (ch->lfsr >> 3) ^ (ch->lfsr >> 5)) & 1u);
                        ch->lfsr = (uint16_t)((ch->lfsr >> 1) | (bit << 15));
                    }
                    sample = (ch->lfsr & 1u) ? env : -env;
                }
                mix += sample;
                ch->env_fp -= ch->gate ? ch->decay_fp : (ch->decay_fp * 4 + 24);
                if (ch->env_fp < 0) ch->env_fp = 0;
            }
        }

        /* 2. Foreground Sound Effects (8 Voices) */
        for (int i = 0; i < SYNTH_MAX_VOICES; i++) {
            SynthVoice *v = &g_voices[i];
            if (!v->active) continue;
            if (v->samples_left <= 0) {
                v->active = false;
                continue;
            }
            uint32_t prev = v->phase_acc;
            v->phase_acc += v->phase_inc;
            int64_t next_inc = (int64_t)v->phase_inc + (int64_t)v->phase_slide;
            if (next_inc < 10000) next_inc = 10000;
            v->phase_inc = (uint32_t)next_inc;

            int32_t env = (v->volume * v->samples_left) / (v->total_samples > 0 ? v->total_samples : 1);
            int32_t sample = 0;
            uint32_t top = v->phase_acc >> 24;

            if (v->wave == WAVE_SQUARE50) {
                sample = (top < 128u) ? env : -env;
            } else if (v->wave == WAVE_SQUARE25) {
                sample = (top < 64u) ? env : -env;
            } else if (v->wave == WAVE_TRIANGLE) {
                int32_t tri = (top < 128u) ? (int32_t)top * 2 - 128 : (int32_t)(255u - top) * 2 - 128;
                sample = (tri * env) >> 7;
            } else {
                if (v->phase_acc < prev) {
                    uint16_t bit = (uint16_t)(((v->lfsr >> 0) ^ (v->lfsr >> 2) ^ (v->lfsr >> 3) ^ (v->lfsr >> 5)) & 1u);
                    v->lfsr = (uint16_t)((v->lfsr >> 1) | (bit << 15));
                }
                sample = (v->lfsr & 1u) ? env : -env;
            }
            mix += sample;
            v->samples_left--;
        }
        if (mix > 32767) mix = 32767;
        if (mix < -32768) mix = -32768;
        out[s] = (int16_t)mix;
    }
}

/* ============================================================================
 * 4. 16x16 SOFTWARE PPU TILE RENDERER (BG1 Terrain + BG2 Canopies)
 * ============================================================================ */
static void draw_bg1_tile(int sx, int sy, uint8_t tile, uint32_t frame) {
    uint32_t c_dark  = pal_col(0, 1);
    uint32_t c_g0    = pal_col(0, 2);
    uint32_t c_g1    = pal_col(0, 3);
    uint32_t c_g2    = pal_col(0, 4);
    uint32_t c_g3    = pal_col(0, 5);
    uint32_t c_w0    = pal_col(0, 6);
    uint32_t c_w1    = pal_col(0, 7);
    uint32_t c_w2    = pal_col(0, 8);
    uint32_t c_s0    = pal_col(0, 9);
    uint32_t c_s1    = pal_col(0, 10);
    uint32_t c_s2    = pal_col(0, 11);
    uint32_t c_b0    = pal_col(0, 12);
    uint32_t c_b1    = pal_col(0, 13);
    uint32_t c_b2    = pal_col(0, 14);
    uint32_t c_red   = pal_col(0, 15);
    uint32_t c_gold  = pal_col(1, 10);
    uint32_t c_cream = pal_col(1, 11);

    switch (tile) {
        case T_GRASS:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            put_px(sx + 3, sy + 4, c_g3); put_px(sx + 4, sy + 5, c_g1);
            put_px(sx + 11, sy + 10, c_g3); put_px(sx + 12, sy + 11, c_g1);
            put_px(sx + 7, sy + 13, c_g1);
            break;
        case T_GRASS_ALT:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            fill_rect_px(sx + 2, sy + 3, 2, 1, c_g3);
            fill_rect_px(sx + 9, sy + 6, 3, 2, c_g1);
            put_px(sx + 10, sy + 5, c_g3);
            put_px(sx + 4, sy + 12, c_g1);
            put_px(sx + 5, sy + 11, c_g3);
            break;
        case T_FLOWERS:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            put_px(sx + 4, sy + 5, c_g0); put_px(sx + 3, sy + 4, c_cream); put_px(sx + 5, sy + 4, c_cream);
            put_px(sx + 4, sy + 3, c_cream); put_px(sx + 4, sy + 4, c_gold);
            put_px(sx + 11, sy + 11, c_g0); put_px(sx + 10, sy + 10, c_red); put_px(sx + 12, sy + 10, c_red);
            put_px(sx + 11, sy + 9, c_red); put_px(sx + 11, sy + 10, c_cream);
            break;
        case T_TALL_GRASS: {
            int sway = ((frame >> 4) & 1u) ? 1 : 0;
            fill_rect_px(sx, sy, 16, 16, c_g1);
            for (int i = 0; i < 4; i++) {
                int bx = sx + 2 + i * 4;
                fill_rect_px(bx, sy + 9, 2, 6, c_g0);
                fill_rect_px(bx + sway, sy + 4, 2, 6, c_g2);
                put_px(bx + sway, sy + 2, c_g3);
                put_px(bx + 1, sy + 12, c_g2);
            }
            break;
        }
        case T_DIRT:
            fill_rect_px(sx, sy, 16, 16, c_w1);
            fill_rect_px(sx + 2, sy + 3, 3, 2, c_w2);
            fill_rect_px(sx + 10, sy + 9, 3, 2, c_w0);
            put_px(sx + 6, sy + 13, c_w2);
            put_px(sx + 13, sy + 4, c_w0);
            break;
        case T_COBBLE:
            fill_rect_px(sx, sy, 16, 16, c_s0);
            fill_rect_px(sx + 1, sy + 1, 6, 6, c_s1); fill_rect_px(sx + 1, sy + 1, 5, 2, c_s2);
            fill_rect_px(sx + 8, sy + 1, 7, 6, c_s1); fill_rect_px(sx + 8, sy + 1, 6, 2, c_s2);
            fill_rect_px(sx + 1, sy + 8, 9, 7, c_s1); fill_rect_px(sx + 1, sy + 8, 8, 2, c_s2);
            fill_rect_px(sx + 11, sy + 8, 4, 7, c_s1); fill_rect_px(sx + 11, sy + 8, 3, 2, c_s2);
            break;
        case T_WATER: {
            int wave = (int)((frame >> 4) & 3u);
            fill_rect_px(sx, sy, 16, 16, c_b0);
            fill_rect_px(sx + ((wave * 2) & 7), sy + 3, 5, 2, c_b1);
            fill_rect_px(sx + ((wave * 2 + 2) & 7), sy + 3, 3, 1, c_b2);
            fill_rect_px(sx + ((10 - wave * 2) & 7) + 2, sy + 10, 5, 2, c_b1);
            fill_rect_px(sx + ((10 - wave * 2) & 7) + 3, sy + 10, 2, 1, c_b2);
            break;
        }
        case T_FORD: {
            int wave = (int)((frame >> 4) & 1u);
            fill_rect_px(sx, sy, 16, 16, c_b1);
            fill_rect_px(sx + 2, sy + 4, 4, 3, c_w2);
            fill_rect_px(sx + 9, sy + 10, 5, 3, c_w1);
            fill_rect_px(sx + 4 + wave, sy + 8, 5, 1, c_b2);
            fill_rect_px(sx + 8 - wave, sy + 2, 4, 1, c_b2);
            break;
        }
        case T_BRIDGE:
            fill_rect_px(sx, sy, 16, 16, c_w0);
            for (int bx = 0; bx < 16; bx += 4) {
                fill_rect_px(sx + bx, sy + 1, 3, 14, c_w1);
                fill_rect_px(sx + bx, sy + 1, 3, 2, c_w2);
            }
            break;
        case T_BRIDGE_RAIL_N:
        case T_BRIDGE_RAIL_S:
            fill_rect_px(sx, sy, 16, 16, c_b0);
            fill_rect_px(sx, sy + 4, 16, 8, c_w0);
            fill_rect_px(sx, sy + 5, 16, 3, c_w2);
            fill_rect_px(sx + 2, sy + 3, 3, 10, c_w1);
            fill_rect_px(sx + 11, sy + 3, 3, 10, c_w1);
            break;
        case T_FENCE:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            fill_rect_px(sx + 2, sy + 4, 3, 10, c_w0);
            fill_rect_px(sx + 11, sy + 4, 3, 10, c_w0);
            fill_rect_px(sx, sy + 6, 16, 2, c_w2);
            fill_rect_px(sx, sy + 10, 16, 2, c_w1);
            break;
        case T_TREE_TRUNK:
            fill_rect_px(sx, sy, 16, 16, c_g1);
            fill_ellipse_alpha(sx + 8, sy + 12, 7, 3, c_dark, 110);
            fill_rect_px(sx + 4, sy + 1, 8, 12, c_w0);
            fill_rect_px(sx + 5, sy + 1, 3, 11, c_w1);
            fill_rect_px(sx + 3, sy + 11, 10, 3, c_w0);
            break;
        case T_BUSH:
            fill_rect_px(sx, sy, 16, 16, c_g0);
            fill_rect_px(sx + 1, sy + 2, 14, 12, c_g1);
            fill_rect_px(sx + 2, sy + 1, 12, 8, c_g2);
            fill_rect_px(sx + 4, sy + 2, 8, 4, c_g3);
            put_px(sx + 4, sy + 8, c_red);
            put_px(sx + 11, sy + 6, c_gold);
            break;
        case T_WALL_WOOD:
            fill_rect_px(sx, sy, 16, 16, c_w2);
            fill_rect_px(sx, sy, 2, 16, c_w0);
            fill_rect_px(sx + 14, sy, 2, 16, c_w0);
            fill_rect_px(sx, sy + 13, 16, 3, c_s0);
            break;
        case T_WALL_WINDOW:
            fill_rect_px(sx, sy, 16, 16, c_w2);
            fill_rect_px(sx + 3, sy + 3, 10, 8, c_w0);
            fill_rect_px(sx + 4, sy + 4, 8, 6, c_gold);
            fill_rect_px(sx + 7, sy + 4, 2, 6, c_w0);
            fill_rect_px(sx + 4, sy + 6, 8, 2, c_w0);
            fill_rect_px(sx, sy + 13, 16, 3, c_s0);
            break;
        case T_WALL_DOOR:
            fill_rect_px(sx, sy, 16, 16, c_w2);
            fill_rect_px(sx + 3, sy + 2, 10, 14, c_w0);
            fill_rect_px(sx + 4, sy + 3, 8, 12, c_w1);
            put_px(sx + 10, sy + 9, c_gold);
            break;
        case T_ROOF_BODY:
            fill_rect_px(sx, sy, 16, 16, c_red);
            for (int ry = 0; ry < 16; ry += 4) {
                fill_rect_px(sx, sy + ry + 3, 16, 1, c_w0);
                for (int rx = (ry & 4) ? 2 : 0; rx < 16; rx += 4) {
                    put_px(sx + rx, sy + ry + 1, c_w2);
                }
            }
            break;
        case T_ROOF_TOP:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            fill_rect_px(sx, sy + 5, 16, 11, c_red);
            fill_rect_px(sx, sy + 4, 16, 2, c_w2);
            fill_rect_px(sx, sy + 11, 16, 1, c_w0);
            break;
        case T_WINDMILL_WALL:
            fill_rect_px(sx, sy, 16, 16, c_s1);
            fill_rect_px(sx + 1, sy + 2, 6, 5, c_s2);
            fill_rect_px(sx + 9, sy + 9, 6, 5, c_s2);
            fill_rect_px(sx, sy + 7, 16, 1, c_s0);
            fill_rect_px(sx, sy + 15, 16, 1, c_s0);
            break;
        case T_WINDMILL_DOOR:
            fill_rect_px(sx, sy, 16, 16, c_s1);
            fill_rect_px(sx + 3, sy + 3, 10, 13, c_w0);
            fill_rect_px(sx + 4, sy + 4, 8, 12, c_w1);
            fill_rect_px(sx + 5, sy + 2, 6, 2, c_s0);
            put_px(sx + 10, sy + 10, c_gold);
            break;
        case T_WINDMILL_ROOF:
            fill_rect_px(sx, sy, 16, 16, c_b0);
            fill_rect_px(sx + 1, sy + 2, 14, 12, c_s0);
            fill_rect_px(sx + 2, sy + 3, 12, 4, c_b1);
            break;
        case T_SHRINE_FLOOR:
            fill_rect_px(sx, sy, 16, 16, c_s0);
            fill_rect_px(sx + 1, sy + 1, 14, 14, c_s1);
            fill_rect_px(sx + 2, sy + 2, 12, 1, c_s2);
            put_px(sx + 3, sy + 12, c_g1);
            break;
        case T_SHRINE_RUNE: {
            uint32_t glow = ((frame >> 4) & 1u) ? pal_col(3, 15) : pal_col(0, 13);
            fill_rect_px(sx, sy, 16, 16, c_s0);
            fill_rect_px(sx + 1, sy + 1, 14, 14, c_s1);
            draw_rect_outline(sx + 4, sy + 4, 8, 8, glow);
            fill_rect_px(sx + 7, sy + 7, 2, 2, c_cream);
            break;
        }
        case T_SHRINE_PILLAR:
            fill_rect_px(sx, sy, 16, 16, c_s0);
            fill_rect_px(sx + 3, sy + 1, 10, 14, c_s2);
            fill_rect_px(sx + 4, sy + 2, 3, 12, c_s1);
            fill_rect_px(sx + 2, sy + 1, 12, 2, c_cream);
            fill_rect_px(sx + 2, sy + 13, 12, 2, c_dark);
            break;
        case T_SIGNPOST:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            fill_rect_px(sx + 7, sy + 8, 2, 7, c_w0);
            fill_rect_px(sx + 2, sy + 3, 12, 6, c_w1);
            draw_rect_outline(sx + 2, sy + 3, 12, 6, c_w0);
            fill_rect_px(sx + 4, sy + 5, 8, 1, c_dark);
            fill_rect_px(sx + 4, sy + 7, 6, 1, c_dark);
            break;
        case T_LANTERN_POST:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            fill_rect_px(sx + 7, sy + 7, 2, 8, c_dark);
            fill_rect_px(sx + 5, sy + 2, 6, 6, c_dark);
            fill_rect_px(sx + 6, sy + 3, 4, 4, ((frame >> 3) & 1u) ? c_cream : c_gold);
            break;
        case T_FLOWER_BED:
            fill_rect_px(sx, sy, 16, 16, c_w0);
            fill_rect_px(sx + 1, sy + 2, 14, 12, c_g1);
            fill_rect_px(sx + 3, sy + 4, 3, 3, c_red);
            fill_rect_px(sx + 9, sy + 5, 3, 3, c_gold);
            fill_rect_px(sx + 5, sy + 9, 3, 3, c_cream);
            fill_rect_px(sx + 11, sy + 9, 2, 2, pal_col(2, 14));
            break;
        /* === Ground-Up Autumn Mountain & Cottage Interior Tileset (Milestone 2) === */
        case T_MTN_SKY:
            /* Transparent skyline window — parallax snow-dusted peaks rendered underneath */
            break;
        case T_CLIFF_WALL:
            fill_rect_px(sx, sy, 16, 16, c_s0);
            fill_rect_px(sx + 1, sy + 1, 6, 5, c_s1); fill_rect_px(sx + 1, sy + 1, 5, 1, c_s2);
            fill_rect_px(sx + 8, sy + 2, 7, 5, c_s1); fill_rect_px(sx + 8, sy + 2, 6, 1, c_s2);
            fill_rect_px(sx, sy + 7, 16, 2, c_dark);
            fill_rect_px(sx + 2, sy + 9, 8, 6, c_s1); fill_rect_px(sx + 2, sy + 9, 7, 1, c_s2);
            fill_rect_px(sx + 11, sy + 9, 5, 6, c_s0);
            fill_rect_px(sx + 4, sy + 14, 3, 2, c_g0);
            break;
        case T_CLIFF_LEDGE:
            fill_rect_px(sx, sy, 16, 16, c_s0);
            fill_rect_px(sx, sy, 16, 5, c_g1);
            fill_rect_px(sx + 1, sy + 1, 14, 2, c_g2);
            fill_rect_px(sx + 3, sy + 2, 4, 1, c_g3);
            fill_rect_px(sx, sy + 5, 16, 3, c_s2);
            fill_rect_px(sx, sy + 8, 16, 2, c_dark);
            fill_rect_px(sx + 2, sy + 10, 6, 5, c_s1);
            fill_rect_px(sx + 9, sy + 10, 6, 5, c_s1);
            break;
        case T_BOULDER:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            fill_ellipse_alpha(sx + 8, sy + 13, 7, 3, c_dark, 110);
            fill_rect_px(sx + 2, sy + 4, 12, 9, c_s0);
            fill_rect_px(sx + 3, sy + 3, 10, 8, c_s1);
            fill_rect_px(sx + 4, sy + 3, 6, 3, c_s2);
            fill_rect_px(sx + 4, sy + 4, 3, 2, c_g1);
            put_px(sx + 11, sy + 9, c_dark);
            break;
        case T_AUTUMN_GRASS:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            put_px(sx + 3, sy + 3, c_g3); put_px(sx + 4, sy + 4, c_g1);
            put_px(sx + 11, sy + 11, c_g3); put_px(sx + 12, sy + 12, c_g1);
            /* Fallen crimson & amber maple leaves */
            fill_rect_px(sx + 5, sy + 9, 2, 2, c_red);
            put_px(sx + 11, sy + 4, c_gold); put_px(sx + 12, sy + 5, c_red);
            put_px(sx + 2, sy + 13, c_gold);
            break;
        case T_ROCKY_PATH:
            fill_rect_px(sx, sy, 16, 16, c_w1);
            fill_rect_px(sx + 1, sy + 2, 5, 4, c_s1); fill_rect_px(sx + 2, sy + 2, 3, 1, c_s2);
            fill_rect_px(sx + 9, sy + 4, 5, 4, c_s1); fill_rect_px(sx + 10, sy + 4, 3, 1, c_s2);
            fill_rect_px(sx + 4, sy + 10, 6, 4, c_s1); fill_rect_px(sx + 5, sy + 10, 4, 1, c_s2);
            put_px(sx + 12, sy + 12, c_w0); put_px(sx + 2, sy + 8, c_w2);
            break;
        case T_RAPIDS: {
            int flow = (int)((frame >> 3) & 7u);
            fill_rect_px(sx, sy, 16, 16, c_b0);
            fill_rect_px(sx + 1, sy + ((flow * 2) & 15), 4, 2, c_b1);
            fill_rect_px(sx + 2, sy + ((flow * 2) & 15), 2, 1, c_b2);
            fill_rect_px(sx + 8, sy + ((flow * 2 + 6) & 15), 5, 2, c_b1);
            fill_rect_px(sx + 9, sy + ((flow * 2 + 6) & 15), 3, 1, 0xFFF2F8FCu);
            fill_rect_px(sx + 4, sy + ((flow * 2 + 12) & 15), 6, 1, 0xFFE6F4FAu);
            break;
        }
        case T_RAIL_TRACK:
            fill_rect_px(sx, sy, 16, 16, c_s0);
            put_px(sx + 2, sy + 2, c_s1); put_px(sx + 11, sy + 13, c_s1);
            put_px(sx + 7, sy + 8, c_s2); put_px(sx + 13, sy + 3, c_w0);
            /* Heavy creosote timber sleepers */
            for (int rx = 1; rx < 16; rx += 5) {
                fill_rect_px(sx + rx, sy + 1, 3, 14, c_w0);
                fill_rect_px(sx + rx + 1, sy + 2, 1, 12, c_w1);
            }
            /* Twin brushed-iron rails */
            fill_rect_px(sx, sy + 3, 16, 2, c_dark);
            fill_rect_px(sx, sy + 3, 16, 1, c_s2);
            fill_rect_px(sx, sy + 11, 16, 2, c_dark);
            fill_rect_px(sx, sy + 11, 16, 1, c_s2);
            break;
        case T_MAPLE_TRUNK:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            fill_ellipse_alpha(sx + 8, sy + 13, 7, 3, c_dark, 115);
            fill_rect_px(sx + 5, sy + 1, 6, 12, c_w0);
            fill_rect_px(sx + 6, sy + 1, 2, 11, c_w1);
            fill_rect_px(sx + 3, sy + 11, 10, 3, c_w0);
            put_px(sx + 2, sy + 13, c_red); put_px(sx + 12, sy + 12, c_gold);
            break;
        case T_PINE_TRUNK:
            fill_rect_px(sx, sy, 16, 16, c_g1);
            fill_ellipse_alpha(sx + 8, sy + 13, 7, 3, c_dark, 115);
            fill_rect_px(sx + 6, sy + 4, 4, 10, c_w0);
            fill_rect_px(sx + 1, sy + 1, 14, 7, c_g0);
            fill_rect_px(sx + 3, sy + 2, 10, 4, c_g1);
            fill_rect_px(sx + 5, sy + 1, 6, 2, c_g2);
            break;
        case T_WOOD_FLOOR:
            fill_rect_px(sx, sy, 16, 16, c_w1);
            fill_rect_px(sx, sy + 4, 16, 1, c_w0);
            fill_rect_px(sx, sy + 9, 16, 1, c_w0);
            fill_rect_px(sx, sy + 14, 16, 1, c_w0);
            fill_rect_px(sx + 2, sy + 1, 6, 1, c_w2);
            fill_rect_px(sx + 8, sy + 6, 5, 1, c_w2);
            fill_rect_px(sx + 1, sy + 11, 7, 1, c_w2);
            put_px(sx + 7, sy + 2, c_w0); put_px(sx + 13, sy + 7, c_w0);
            break;
        case T_HEARTH_RUG:
            fill_rect_px(sx, sy, 16, 16, c_red);
            draw_rect_outline(sx + 1, sy + 1, 14, 14, c_gold);
            fill_rect_px(sx + 4, sy + 4, 8, 8, c_w0);
            fill_rect_px(sx + 6, sy + 6, 4, 4, c_cream);
            break;
        case T_FIREPLACE: {
            int flk = (int)((frame >> 2) & 1u);
            fill_rect_px(sx, sy, 16, 16, c_s0);
            fill_rect_px(sx + 1, sy + 1, 14, 3, c_s2);
            fill_rect_px(sx + 1, sy + 4, 3, 12, c_s1);
            fill_rect_px(sx + 12, sy + 4, 3, 12, c_s1);
            fill_rect_px(sx + 4, sy + 5, 8, 11, c_dark);
            fill_rect_px(sx + 5, sy + 12, 6, 3, c_w0);
            fill_rect_px(sx + 5, sy + 8 + flk, 6, 5 - flk, c_red);
            fill_rect_px(sx + 6, sy + 9 - flk, 4, 4, c_gold);
            fill_rect_px(sx + 7, sy + 10, 2, 2, c_cream);
            break;
        }
        case T_BED_QUILT:
            fill_rect_px(sx, sy, 16, 16, c_w0);
            fill_rect_px(sx + 1, sy + 1, 14, 14, c_w1);
            fill_rect_px(sx + 2, sy + 2, 12, 4, c_cream);
            fill_rect_px(sx + 2, sy + 6, 6, 4, c_red);
            fill_rect_px(sx + 8, sy + 6, 6, 4, c_b1);
            fill_rect_px(sx + 2, sy + 10, 6, 4, c_gold);
            fill_rect_px(sx + 8, sy + 10, 6, 4, c_red);
            break;
        case T_ATTIC_CHEST: {
            int shim = (int)((frame >> 4) & 1u);
            fill_rect_px(sx, sy, 16, 16, c_w1);
            fill_rect_px(sx + 2, sy + 3, 12, 11, c_w0);
            fill_rect_px(sx + 3, sy + 4, 10, 4, c_red);
            fill_rect_px(sx + 3, sy + 9, 10, 4, c_w2);
            fill_rect_px(sx + 2, sy + 8, 12, 1, c_gold);
            fill_rect_px(sx + 7, sy + 7, 2, 3, shim ? c_cream : c_gold);
            break;
        }
        case T_HOLLOW_OAK: {
            int pulse = (int)((frame >> 4) & 1u);
            fill_rect_px(sx, sy, 16, 16, c_w0);
            fill_rect_px(sx + 1, sy + 1, 14, 14, c_w1);
            fill_rect_px(sx + 4, sy + 3, 8, 13, c_dark);
            fill_rect_px(sx + 5, sy + 4, 6, 11, pulse ? c_b2 : c_b1);
            fill_rect_px(sx + 6, sy + 6, 4, 7, pulse ? c_cream : c_gold);
            break;
        }
        case T_STONE_STAIRS:
            fill_rect_px(sx, sy, 16, 16, c_s0);
            for (int ry = 0; ry < 16; ry += 4) {
                fill_rect_px(sx + 1, sy + ry, 14, 3, c_s1);
                fill_rect_px(sx + 1, sy + ry, 14, 1, c_s2);
                fill_rect_px(sx + 1, sy + ry + 3, 14, 1, c_dark);
            }
            break;
        case T_INTERIOR_WALL:
            fill_rect_px(sx, sy, 16, 16, c_w2);
            fill_rect_px(sx, sy, 16, 2, c_w0);
            fill_rect_px(sx, sy + 12, 16, 4, c_w0);
            fill_rect_px(sx + 7, sy + 2, 2, 10, c_w1);
            break;
        case T_WORLD_OCEAN: {
            int wave = (int)((frame >> 4) & 3u);
            fill_rect_px(sx, sy, 16, 16, 0xFF28588Au);
            fill_rect_px(sx + ((wave * 3) & 11), sy + 3, 5, 1, 0xFF4284B8u);
            fill_rect_px(sx + ((wave * 3 + 6) & 11), sy + 10, 6, 1, 0xFF6AB2DCu);
            fill_rect_px(sx + ((wave * 2 + 2) & 11), sy + 14, 3, 1, 0xFF9CD4F0u);
            break;
        }
        case T_WORLD_BEACH: {
            int wave = (int)((frame >> 4) & 3u);
            fill_rect_px(sx, sy, 16, 16, 0xFFE6CE94u);
            fill_rect_px(sx + 2, sy + 2, 12, 10, 0xFFF2DEAAu);
            fill_rect_px(sx + ((wave * 2) & 7), sy + 13, 8, 2, 0xFF6AB8E0u);
            put_px(sx + 4, sy + 5, 0xFFC8A86Eu);
            put_px(sx + 11, sy + 9, 0xFFC8A86Eu);
            break;
        }
        case T_WORLD_PEAK:
            fill_rect_px(sx, sy, 16, 16, 0xFF548840u);
            fill_rect_px(sx + 1, sy + 8, 14, 8, 0xFF5E5A68u);
            fill_rect_px(sx + 3, sy + 4, 10, 9, 0xFF767080u);
            fill_rect_px(sx + 5, sy + 1, 6, 5, 0xFF948EA0u);
            fill_rect_px(sx + 6, sy + 1, 4, 3, 0xFFF4F6FAu);
            break;
        case T_WORLD_FOREST:
            fill_rect_px(sx, sy, 16, 16, 0xFF2E5C32u);
            fill_rect_px(sx + 1, sy + 2, 7, 7, 0xFF3E7840u);
            fill_rect_px(sx + 2, sy + 3, 4, 3, 0xFF5A9852u);
            fill_rect_px(sx + 8, sy + 6, 7, 7, 0xFF3A6E3Cu);
            fill_rect_px(sx + 10, sy + 7, 4, 3, 0xFF5A9852u);
            fill_rect_px(sx + 4, sy + 10, 5, 5, 0xFF28502Cu);
            break;
        case T_WORLD_HILL:
            fill_rect_px(sx, sy, 16, 16, 0xFF68A64Eu);
            fill_rect_px(sx + 2, sy + 6, 12, 9, 0xFF548840u);
            fill_rect_px(sx + 4, sy + 4, 8, 6, 0xFF7BB256u);
            fill_rect_px(sx + 5, sy + 4, 5, 2, 0xFF98CA6Cu);
            break;
        case T_WORLD_CASTLE:
            fill_rect_px(sx, sy, 16, 16, 0xFFD8D0C0u);
            fill_rect_px(sx + 1, sy + 2, 14, 13, 0xFFEEE8D8u);
            fill_rect_px(sx + 2, sy + 0, 4, 4, 0xFF3462A4u);
            fill_rect_px(sx + 10, sy + 0, 4, 4, 0xFF3462A4u);
            fill_rect_px(sx + 6, sy + 8, 4, 7, 0xFF423026u);
            break;
        case T_WORLD_TOWN:
            fill_rect_px(sx, sy, 16, 16, 0xFF68A64Eu);
            fill_rect_px(sx + 2, sy + 6, 12, 9, 0xFFF4EAD2u);
            fill_rect_px(sx + 1, sy + 2, 14, 5, 0xFFCC523Cu);
            fill_rect_px(sx + 3, sy + 3, 10, 2, 0xFFE47254u);
            fill_rect_px(sx + 6, sy + 10, 4, 5, 0xFF5C3A24u);
            break;
        case T_WORLD_RUINS:
            fill_rect_px(sx, sy, 16, 16, 0xFF5A8248u);
            fill_rect_px(sx + 2, sy + 3, 4, 11, 0xFF888C9Cu);
            fill_rect_px(sx + 10, sy + 5, 4, 9, 0xFF767A8Au);
            fill_rect_px(sx + 2, sy + 3, 12, 2, 0xFFB0B4C2u);
            fill_rect_px(sx + 6, sy + 9, 3, 3, 0xFF426A3Eu);
            break;
        case T_WORLD_SHALLOW: {
            int wave = (int)((frame >> 4) & 3u);
            fill_rect_px(sx, sy, 16, 16, 0xFF3EAEC4u);
            fill_rect_px(sx + ((wave * 3) & 11), sy + 4, 5, 1, 0xFF7ADCF0u);
            fill_rect_px(sx + ((wave * 2 + 5) & 11), sy + 11, 6, 1, 0xFFB8F4FCu);
            put_px(sx + ((wave * 3 + 2) & 15), sy + 8, 0xFFE8FCFFu);
            break;
        }
        case T_WORLD_SAND:
            fill_rect_px(sx, sy, 16, 16, 0xFFE8C26Eu);
            fill_rect_px(sx + 1, sy + 3, 12, 2, 0xFFF4D688u);
            fill_rect_px(sx + 3, sy + 10, 11, 2, 0xFFD4A654u);
            put_px(sx + 4, sy + 7, 0xFFC29242u);
            put_px(sx + 12, sy + 13, 0xFFC29242u);
            break;
        case T_WORLD_SNOW:
            fill_rect_px(sx, sy, 16, 16, 0xFFE6F0FAu);
            fill_rect_px(sx + 2, sy + 4, 9, 2, 0xFFFFFFFFu);
            fill_rect_px(sx + 5, sy + 11, 8, 2, 0xFFC8DCF0u);
            put_px(sx + 13, sy + 3, 0xFF9AC4E8u);
            put_px(sx + 3, sy + 13, 0xFF9AC4E8u);
            break;
        case T_WORLD_LAVA: {
            int glow = (int)((frame >> 3) & 3u);
            fill_rect_px(sx, sy, 16, 16, 0xFF9E2418u);
            fill_rect_px(sx + 1, sy + ((glow * 3) & 11), 13, 3, 0xFFE85224u);
            fill_rect_px(sx + 3, sy + ((glow * 3 + 7) & 11), 9, 2, 0xFFFFB838u);
            put_px(sx + 6 + glow, sy + 5, 0xFFFFF4A8u);
            break;
        }
        case T_WORLD_JUNGLE:
            fill_rect_px(sx, sy, 16, 16, 0xFF1F542Cu);
            fill_rect_px(sx + 1, sy + 2, 8, 7, 0xFF2E783Cu);
            fill_rect_px(sx + 7, sy + 7, 8, 7, 0xFF3E9448u);
            fill_rect_px(sx + 2, sy + 3, 4, 2, 0xFF70BA5Au);
            put_px(sx + 11, sy + 9, 0xFFE86878u);
            break;
        case T_WORLD_CORAL: {
            int wave = (int)((frame >> 4) & 3u);
            fill_rect_px(sx, sy, 16, 16, 0xFF3CAEC6u);
            fill_rect_px(sx + ((wave * 2) & 9), sy + 2, 5, 1, 0xFF9CECF8u);
            fill_rect_px(sx + 3, sy + 5, 4, 4, 0xFFF27084u);
            fill_rect_px(sx + 9, sy + 8, 5, 4, 0xFFF4A45Au);
            fill_rect_px(sx + 5, sy + 11, 3, 3, 0xFF7AD6A8u);
            break;
        }
        case T_WORLD_TECH:
            fill_rect_px(sx, sy, 16, 16, 0xFF283448u);
            fill_rect_px(sx + 1, sy + 1, 14, 14, 0xFF364862u);
            fill_rect_px(sx + 0, sy + 7, 16, 2, 0xFF42D6F4u);
            fill_rect_px(sx + 7, sy + 0, 2, 16, 0xFF42D6F4u);
            fill_rect_px(sx + 6, sy + 6, 4, 4, 0xFFB8F6FFu);
            break;
        case T_PALM_TREE:
            fill_rect_px(sx, sy, 16, 16, 0xFFE8C26Eu);
            fill_rect_px(sx + 6, sy + 7, 4, 8, 0xFF8C5832u);
            fill_rect_px(sx + 7, sy + 7, 2, 8, 0xFFB07446u);
            fill_rect_px(sx + 1, sy + 2, 14, 5, 0xFF2E7E42u);
            fill_rect_px(sx + 3, sy + 1, 10, 4, 0xFF52A858u);
            fill_rect_px(sx + 6, sy + 5, 4, 2, 0xFFE8C84Au);
            break;
        case T_DOCK_PIER:
            fill_rect_px(sx, sy, 16, 16, 0xFF3EAEC4u);
            fill_rect_px(sx + 1, sy + 1, 14, 14, 0xFF7C4E2Cu);
            fill_rect_px(sx + 2, sy + 2, 12, 3, 0xFFA66E42u);
            fill_rect_px(sx + 2, sy + 6, 12, 3, 0xFFA66E42u);
            fill_rect_px(sx + 2, sy + 10, 12, 3, 0xFFA66E42u);
            fill_rect_px(sx + 2, sy + 2, 2, 2, 0xFF52321Au);
            fill_rect_px(sx + 12, sy + 11, 2, 2, 0xFF52321Au);
            break;
        default:
            fill_rect_px(sx, sy, 16, 16, c_g2);
            break;
    }
}

static void draw_bg2_tile(int sx, int sy, uint8_t tile) {
    uint32_t c_g0 = pal_col(0, 2);
    uint32_t c_g1 = pal_col(0, 3);
    uint32_t c_g2 = pal_col(0, 4);
    uint32_t c_g3 = pal_col(0, 5);
    uint32_t c_red = pal_col(0, 15);
    uint32_t c_gold = pal_col(1, 10);
    uint32_t c_cream = pal_col(1, 11);
    switch (tile) {
        case O_CANOPY_TL:
            fill_rect_px(sx + 4, sy + 4, 12, 12, c_g1);
            fill_rect_px(sx + 6, sy + 2, 10, 10, c_g2);
            fill_rect_px(sx + 8, sy + 4, 6, 5, c_g3);
            break;
        case O_CANOPY_TR:
            fill_rect_px(sx, sy + 4, 12, 12, c_g1);
            fill_rect_px(sx, sy + 2, 10, 10, c_g2);
            fill_rect_px(sx + 2, sy + 4, 6, 5, c_g3);
            break;
        case O_CANOPY_BL:
            fill_rect_px(sx + 2, sy, 14, 12, c_g0);
            fill_rect_px(sx + 4, sy, 12, 9, c_g1);
            fill_rect_px(sx + 6, sy + 1, 10, 5, c_g2);
            break;
        case O_CANOPY_BR:
            fill_rect_px(sx, sy, 14, 12, c_g0);
            fill_rect_px(sx, sy, 12, 9, c_g1);
            fill_rect_px(sx, sy + 1, 10, 5, c_g2);
            break;
        case O_ROOF_EAVE:
            fill_rect_px(sx, sy + 12, 16, 4, pal_col(0, 15));
            fill_rect_px(sx, sy + 15, 16, 1, pal_col(0, 1));
            break;
        case O_SHRINE_ARCH:
            fill_rect_px(sx + 2, sy, 12, 6, pal_col(0, 10));
            fill_rect_px(sx + 3, sy + 1, 10, 2, pal_col(0, 11));
            fill_rect_px(sx + 2, sy + 6, 12, 1, pal_col(0, 1));
            break;
        case O_MAPLE_TL:
            fill_rect_px(sx + 3, sy + 3, 13, 13, pal_col(0, 6));
            fill_rect_px(sx + 4, sy + 2, 12, 11, c_red);
            fill_rect_px(sx + 6, sy + 3, 8, 6, c_gold);
            put_px(sx + 8, sy + 4, c_cream);
            break;
        case O_MAPLE_TR:
            fill_rect_px(sx, sy + 3, 13, 13, pal_col(0, 6));
            fill_rect_px(sx, sy + 2, 12, 11, c_red);
            fill_rect_px(sx + 2, sy + 3, 8, 6, c_gold);
            put_px(sx + 5, sy + 5, c_cream);
            break;
        case O_PINE_TOP:
            fill_rect_px(sx + 6, sy + 1, 4, 4, c_g1);
            fill_rect_px(sx + 4, sy + 5, 8, 5, c_g0);
            fill_rect_px(sx + 2, sy + 10, 12, 6, c_g0);
            fill_rect_px(sx + 5, sy + 4, 4, 3, c_g2);
            fill_rect_px(sx + 3, sy + 10, 6, 3, c_g1);
            break;
        case O_OAK_CROWN:
            fill_rect_px(sx + 1, sy + 2, 14, 14, pal_col(0, 6));
            fill_rect_px(sx + 2, sy + 1, 12, 11, c_red);
            fill_rect_px(sx + 4, sy + 2, 8, 7, c_gold);
            fill_rect_px(sx + 6, sy + 4, 4, 3, c_cream);
            break;
        default:
            break;
    }
}

/* ============================================================================
 * 5. SOFTWARE OAM SPRITE RENDERER (16-Bit Characters, Familiars, Boss & Props)
 * ============================================================================ */
static void spr_box(int ox, int oy, int rx, int ry, int rw, int rh,
                    uint32_t col, bool flip_x, bool flash_white,
                    int16_t sx_fp, int16_t sy_fp) {
    uint32_t c = flash_white ? 0xFFFFFFFFu : col;
    int px0 = flip_x ? -(rx + rw) : rx;
    int px1 = px0 + rw;
    int x0 = ox + ((px0 * sx_fp) >> 8);
    int x1 = ox + ((px1 * sx_fp) >> 8);
    int y0 = oy + ((ry * sy_fp) >> 8);
    int y1 = oy + (((ry + rh) * sy_fp) >> 8);
    if (x1 <= x0) x1 = x0 + 1;
    if (y1 <= y0) y1 = y0 + 1;
    fill_rect_px(x0, y0, x1 - x0, y1 - y0, c);
}

static inline uint8_t hex_char_to_idx(char ch) {
    if (ch >= '1' && ch <= '9') return (uint8_t)(ch - '0');
    if (ch >= 'a' && ch <= 'f') return (uint8_t)(10 + (ch - 'a'));
    if (ch >= 'A' && ch <= 'F') return (uint8_t)(10 + (ch - 'A'));
    return 0u;
}

static void draw_pixel_art(const char *const *rows, int nrows, int ox, int oy,
                           int anchor_x, int anchor_y, uint8_t pal_idx,
                           bool flip_x, bool flash_white,
                           int16_t sx_fp, int16_t sy_fp) {
    if (nrows <= 0 || !rows[0]) return;
    int ncols = (int)strlen(rows[0]);
    for (int r = 0; r < nrows; r++) {
        const char *line = rows[r];
        for (int c = 0; c < ncols; c++) {
            uint8_t idx = hex_char_to_idx(line[c]);
            if (idx == 0u) continue;
            uint32_t col = flash_white ? 0xFFFFFFFFu : pal_col(pal_idx, idx);
            int rx = flip_x ? (anchor_x - 1 - c) : (c - anchor_x);
            int ry = r - anchor_y;
            if (sx_fp == FP_ONE && sy_fp == FP_ONE) {
                put_px(ox + rx, oy + ry, col);
            } else {
                int x0 = ox + ((rx * sx_fp) >> 8);
                int x1 = ox + (((rx + 1) * sx_fp) >> 8);
                int y0 = oy + ((ry * sy_fp) >> 8);
                int y1 = oy + (((ry + 1) * sy_fp) >> 8);
                if (x1 <= x0) x1 = x0 + 1;
                if (y1 <= y0) y1 = y0 + 1;
                fill_rect_px(x0, y0, x1 - x0, y1 - y0, col);
            }
        }
    }
}

static void draw_indexed_buf(const uint8_t *buf, int bw, int bh,
                             int ox, int oy, int anchor_x, int anchor_y,
                             uint8_t pal_idx, bool flip_x, bool flash_white,
                             int16_t sx_fp, int16_t sy_fp) {
    for (int y = 0; y < bh; y++) {
        int ry = y - anchor_y;
        int y0 = oy + ((ry * sy_fp) >> 8);
        int y1 = oy + (((ry + 1) * sy_fp) >> 8);
        if (y1 <= y0) y1 = y0 + 1;
        for (int x = 0; x < bw; x++) {
            uint8_t idx = buf[y * bw + x];
            if (idx == 0u) continue;
            uint32_t col = flash_white ? 0xFFFFFFFFu : pal_col(pal_idx, idx);
            int rx = flip_x ? (anchor_x - 1 - x) : (x - anchor_x);
            int x0 = ox + ((rx * sx_fp) >> 8);
            int x1 = ox + (((rx + 1) * sx_fp) >> 8);
            if (x1 <= x0) x1 = x0 + 1;
            fill_rect_px(x0, y0, x1 - x0, y1 - y0, col);
        }
    }
}

static void draw_oam_sprite(const HearthOamSprite *o, int ox, int oy, uint32_t frame) {
    bool flip  = (o->flags & OAM_FLAG_FLIP_X) != 0;
    bool flash = (o->flags & OAM_FLAG_FLASH_WHITE) != 0;
    int16_t sx = o->scale_x_fp ? o->scale_x_fp : FP_ONE;
    int16_t sy = o->scale_y_fp ? o->scale_y_fp : FP_ONE;

    if ((o->flags & OAM_FLAG_SHADOW) && o->sprite_id != SPR_RAFT) {
        int r = (o->sprite_id == SPR_LOCOMOTIVE) ? 22
              : (o->sprite_id == SPR_BOSS_BRIAR || o->sprite_id == SPR_BOSS_MUCKMAW || o->sprite_id == SPR_BOSS_IRONFANG || o->sprite_id == SPR_ENEMY_SANDWORM) ? 15
              : (o->sprite_id == SPR_HOLLOW_OAK) ? 12
              : (o->sprite_id == SPR_PIP || o->sprite_id == SPR_PET_MILO || o->sprite_id == SPR_BOBBER) ? 4 : 6;
        int sh_y = (o->sprite_id == SPR_PIP) ? ((int)o->sort_y + 1) : (oy + 1);
        fill_ellipse_alpha(ox, sh_y, r, 3, 0xFF10141Au, 115);
    }

    /* Heart-Mending Aura Rings */
    if (o->aura == AURA_RADIANT_COURAGE) {
        int pulse = 10 + (((int)frame >> 3) & 3);
        draw_circle_outline(ox, oy - 7, pulse, pal_col(1, 10));
        draw_circle_outline(ox, oy - 7, pulse - 2, pal_col(1, 11));
    } else if (o->aura == AURA_HOLLOW_FEAR) {
        int pulse = 9 + (((int)frame >> 4) & 2);
        draw_circle_outline(ox, oy - 7, pulse, pal_col(3, 3));
    }

    int step = (int)(o->frame & 3u);
    int bob = (step == 1 || step == 3) ? -1 : 0;

    switch (o->sprite_id) {
        case SPR_HERO: {
            /* Expressive 16x24 JRPG Hero Rowan (4 dirs x 12 poses: walk, blink, dash, battle!) */
            static uint8_t s_hero_buf[384];
            hearth_bake_rowan_16x24(s_hero_buf, o->dir, o->frame);
            draw_indexed_buf(s_hero_buf, 16, 24, ox, oy, 8, 24, 1, false, flash, sx, sy);
            break;
        }
        case SPR_PIP: {
            /* Petite ~11x13 Bouncy Garden-Gnome Pip (zero wings, Oliver & Mr. Drippy scale!) */
            static uint8_t s_pip_buf[256];
            hearth_bake_pip_16x16(s_pip_buf, o->frame);
            fill_ellipse_alpha(ox, oy - 4, 7, 6, pal_col(1, 10), 42);
            draw_indexed_buf(s_pip_buf, 16, 16, ox, oy, 8, 16, 1, flip, flash, sx, sy);
            break;
        }
        case SPR_FAMILIAR_BRAMBLE: {
            draw_pixel_art(BRAMBLE_ART, 13, ox, oy + bob, 10, 13, 1, flip, flash, sx, sy);
            break;
        }
        case SPR_FAMILIAR_LUMINA: {
            int fy = -3 + (HEARTH_SIN64[(frame >> 1) & 63u] >> 6);
            fill_ellipse_alpha(ox, oy + fy - 6, 9, 6, pal_col(1, 13), 48);
            draw_pixel_art(LUMINA_ART, 12, ox, oy + fy, 10, 12, 1, flip, flash, sx, sy);
            break;
        }
        case SPR_NPC_ELDER:
        case SPR_NPC_CLARA:
        case SPR_NPC_GUARD:
        case SPR_NPC_TOWNSFOLK:
        case SPR_NPC_MARA:
        case SPR_NPC_BARNABY:
        case SPR_NPC_MAPLE:
        case SPR_NPC_JASPER:
        case SPR_NPC_TESSA:
        case SPR_NPC_CORIN:
        case SPR_NPC_SENTINEL:
        case SPR_NPC_SHIPWRIGHT: {
            /* Tall 16x24 Adult Cast (Grandma Mara, Elder Wren, Clara, Guard Bram & Townsfolk) */
            static uint8_t s_npc_buf[384];
            hearth_bake_npc_16x24(s_npc_buf, o->sprite_id, (uint8_t)((frame >> 4) & 1u));
            draw_indexed_buf(s_npc_buf, 16, 24, ox, oy, 8, 24, 2, flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_GLOOMCAP: {
            draw_pixel_art(GLOOMCAP_ART, 11, ox, oy + bob, 10, 11, 3, flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_THORNLING: {
            draw_pixel_art(THORNLING_ART, 11, ox, oy + bob, 10, 11, 3, flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_DUSK_MOTH: {
            int fy = -4 + (HEARTH_SIN64[(frame >> 1) & 63u] >> 5);
            const char *const *mf = ((frame >> 2) & 1u) ? DUSK_MOTH_F1 : DUSK_MOTH_F0;
            draw_pixel_art(mf, 7, ox, oy + fy, 11, 7, 3, flip, flash, sx, sy);
            break;
        }
        case SPR_BOSS_BRIAR: {
            int b = ((frame >> 4) & 1u) ? -1 : 0;
            draw_pixel_art(BOSS_BRIAR_ART, 21, ox, oy + b, 16, 21, 3, flip, flash, sx, sy);
            /* Pulsing heart rune core */
            if ((frame >> 3) & 1u) {
                fill_ellipse_alpha(ox, oy - 9 + b, 5, 4, pal_col(3, 15), 110);
            }
            break;
        }
        case SPR_WINDMILL_BLADES: {
            int ang = (int)((o->rot_deg * 64u) / 360u) & 63;
            for (int arm = 0; arm < 4; arm++) {
                int a = (ang + arm * 16) & 63;
                int ca = HEARTH_SIN64[(a + 16) & 63];
                int sa = HEARTH_SIN64[a];
                int ex = ox + (ca * 22) / 127;
                int ey = oy + (sa * 22) / 127;
                draw_line_px(ox, oy, ex, ey, pal_col(0, 6));
                draw_line_px(ox + 1, oy, ex + 1, ey, pal_col(0, 8));
                int px = ox + (ca * 14) / 127 - (sa * 5) / 127;
                int py = oy + (sa * 14) / 127 + (ca * 5) / 127;
                int mx = ox + (ca * 8) / 127 - (sa * 4) / 127;
                int my = oy + (sa * 8) / 127 + (ca * 4) / 127;
                draw_line_px(px, py, ex, ey, pal_col(2, 15));
                draw_line_px(mx, my, px, py, pal_col(2, 14));
            }
            fill_rect_px(ox - 2, oy - 2, 5, 5, pal_col(0, 6));
            fill_rect_px(ox - 1, oy - 1, 3, 3, pal_col(1, 10));
            break;
        }
        case SPR_WAYSTONE: {
            int fy = (HEARTH_SIN64[(frame >> 2) & 63u] >> 6);
            spr_box(ox, oy, -5, -7, 10, 7, pal_col(0, 10), flip, flash, sx, sy);
            spr_box(ox, oy, -4, -6,  8, 5, pal_col(0, 11), flip, flash, sx, sy);
            fill_ellipse_alpha(ox, oy - 13 + fy, 7, 7, pal_col(3, 15), 70);
            draw_pixel_art(WAYSTONE_CRYSTAL, 9, ox, oy - 8 + fy, 8, 9, 3, flip, flash, sx, sy);
            break;
        }
        case SPR_CHEST: {
            const char *const *ch = (o->frame > 0) ? CHEST_OPEN : CHEST_CLOSED;
            draw_pixel_art(ch, 10, ox, oy, 8, 10, 2, flip, flash, sx, sy);
            break;
        }
        case SPR_EMOTE_BANG:
        case SPR_EMOTE_HEART:
        case SPR_EMOTE_QUESTION:
        case SPR_EMOTE_SPARK: {
            fill_rect_px(ox - 6, oy - 12, 12, 11, 0xFFFFF8E8u);
            draw_rect_outline(ox - 6, oy - 12, 12, 11, 0xFF1F171Au);
            const char *sym = (o->sprite_id == SPR_EMOTE_BANG) ? "!"
                            : (o->sprite_id == SPR_EMOTE_HEART) ? "+"
                            : (o->sprite_id == SPR_EMOTE_QUESTION) ? "?" : "*";
            uint32_t col = (o->sprite_id == SPR_EMOTE_HEART) ? 0xFFE04E4Eu : 0xFFE8A020u;
            draw_text(ox - 2, oy - 10, sym, col, false);
            break;
        }
        case SPR_HEART_ORB: {
            int pulse = 6 + (((int)frame >> 3) & 2);
            fill_ellipse_alpha(ox, oy, pulse + 2, pulse + 2, pal_col(1, 10), 100);
            fill_ellipse_alpha(ox, oy, pulse, pulse, pal_col(1, 11), 165);
            fill_rect_px(ox - 2, oy - 2, 4, 4, 0xFFFFFFFFu);
            break;
        }
        case SPR_CHEST_OPEN: {
            draw_pixel_art(CHEST_OPEN, 10, ox, oy, 8, 10, 2, flip, flash, sx, sy);
            break;
        }
        case SPR_NPC_MOUSE: {
            /* Anthropomorphic Mouse Folk (round pink-lined ears, whiskered snout, tailored vest) */
            spr_box(ox, oy, -7, -15, 5, 5, 0xFF9EA2B0u, flip, flash, sx, sy);
            spr_box(ox, oy, -6, -14, 3, 3, 0xFFE8A4B4u, flip, flash, sx, sy);
            spr_box(ox, oy,  2, -15, 5, 5, 0xFF9EA2B0u, flip, flash, sx, sy);
            spr_box(ox, oy,  3, -14, 3, 3, 0xFFE8A4B4u, flip, flash, sx, sy);
            spr_box(ox, oy, -4, -12, 8, 6, 0xFFC2C6D2u, flip, flash, sx, sy);
            spr_box(ox, oy, -2, -10, 1, 2, 0xFF18141Cu, flip, flash, sx, sy);
            spr_box(ox, oy,  1, -10, 1, 2, 0xFF18141Cu, flip, flash, sx, sy);
            spr_box(ox, oy, -1,  -8, 2, 1, 0xFFE87E94u, flip, flash, sx, sy);
            spr_box(ox, oy, -5,  -6, 10, 5, 0xFFB84C3Eu, flip, flash, sx, sy);
            spr_box(ox, oy, -2,  -6,  4, 4, 0xFFF5D88Cu, flip, flash, sx, sy);
            spr_box(ox, oy, -4,  -1,  3, 2, 0xFF523426u, flip, flash, sx, sy);
            spr_box(ox, oy,  1,  -1,  3, 2, 0xFF523426u, flip, flash, sx, sy);
            break;
        }
        case SPR_NPC_PIG: {
            /* Anthropomorphic Pig Knight / Merchant (pink snout, sturdy iron breastplate & tunic) */
            spr_box(ox, oy, -6, -15, 3, 3, 0xFFE698A2u, flip, flash, sx, sy);
            spr_box(ox, oy,  3, -15, 3, 3, 0xFFE698A2u, flip, flash, sx, sy);
            spr_box(ox, oy, -5, -13, 10, 7, 0xFFF4B6B8u, flip, flash, sx, sy);
            spr_box(ox, oy, -3, -11, 1, 2, 0xFF18141Cu, flip, flash, sx, sy);
            spr_box(ox, oy,  2, -11, 1, 2, 0xFF18141Cu, flip, flash, sx, sy);
            spr_box(ox, oy, -2,  -9, 4, 3, 0xFFE07C8Cu, flip, flash, sx, sy);
            spr_box(ox, oy, -1,  -8, 1, 1, 0xFF58222Cu, flip, flash, sx, sy);
            spr_box(ox, oy,  1,  -8, 1, 1, 0xFF58222Cu, flip, flash, sx, sy);
            spr_box(ox, oy, -6,  -6, 12, 6, 0xFF6A7084u, flip, flash, sx, sy);
            spr_box(ox, oy, -4,  -5,  8, 2, 0xFFE8B84Cu, flip, flash, sx, sy);
            spr_box(ox, oy, -5,   0,  4, 2, 0xFF3E2620u, flip, flash, sx, sy);
            spr_box(ox, oy,  1,   0,  4, 2, 0xFF3E2620u, flip, flash, sx, sy);
            break;
        }
        case SPR_NPC_SHADOW_GRAN: {
            /* Tall 16x24 Shadow Grandma — Dark Hooded Cloak, Silver Bun & Glowing Lavender Eyes */
            static uint8_t s_sg_buf[384];
            int sw = ((frame >> 4) & 1u) ? -1 : 0;
            fill_ellipse_alpha(ox, oy - 11 + sw, 11, 12, 0xFF4C2278u, 70);
            hearth_bake_npc_16x24(s_sg_buf, SPR_NPC_SHADOW_GRAN, 0u);
            draw_indexed_buf(s_sg_buf, 16, 24, ox, oy + sw, 8, 24, 3, flip, flash, sx, sy);
            break;
        }
        case SPR_PET_MILO: {
            /* Mido — Amber Mountain Fox-Kit with Pointed Dark-Tipped Ears, Crimson Scarf & Brass Bell */
            int hb = ((frame >> 3) & 1u) ? -1 : 0;
            /* Bushy cream-tipped fox tail */
            spr_box(ox, oy + hb, -8, -8, 4, 3, 0xFFD86C28u, flip, flash, sx, sy);
            spr_box(ox, oy + hb, -9, -9, 3, 2, 0xFFFBF3E4u, flip, flash, sx, sy);
            /* Body & cream chest tuft */
            spr_box(ox, oy + hb, -4, -7, 8, 5, 0xFFE47B32u, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  1, -6, 3, 3, 0xFFFBF3E4u, flip, flash, sx, sy);
            /* Fox head + pointed dark-tipped ears */
            spr_box(ox, oy + hb,  1, -11, 6, 5, 0xFFE88238u, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  1, -14, 2, 3, 0xFFC2521Cu, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  5, -14, 2, 3, 0xFFC2521Cu, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  1, -14, 2, 1, 0xFF3A2018u, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  5, -14, 2, 1, 0xFF3A2018u, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  3,  -9, 3, 2, 0xFFFBF3E4u, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  4, -10, 1, 1, 0xFF18141Cu, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  6,  -9, 1, 1, 0xFF18141Cu, flip, flash, sx, sy);
            /* Crimson scarf & bright brass collar bell */
            spr_box(ox, oy + hb,  0,  -7, 5, 2, 0xFFC8343Cu, flip, flash, sx, sy);
            spr_box(ox, oy + hb,  2,  -6, 2, 2, 0xFFFFE066u, flip, flash, sx, sy);
            /* Dark fox paws */
            spr_box(ox, oy,      -3,  -2, 2, 2, 0xFF4A2416u, flip, flash, sx, sy);
            spr_box(ox, oy,       2,  -2, 2, 2, 0xFF4A2416u, flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_RAT: {
            /* Gutter Rat — Slate Fur, Red Eyes, Pink Tail */
            spr_box(ox, oy + bob, -6, -8, 11, 6, 0xFF5A5D6Eu, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -7, -10, 3, 3, 0xFF8C6874u, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -4,  -7, 2, 2, 0xFFFF4A5Au, flip, flash, sx, sy);
            spr_box(ox, oy + bob,  5,  -6, 4, 2, 0xFFD88496u, flip, flash, sx, sy);
            spr_box(ox, oy,       -5,  -2, 2, 2, 0xFF34323Eu, flip, flash, sx, sy);
            spr_box(ox, oy,        2,  -2, 2, 2, 0xFF34323Eu, flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_SLIME: {
            /* Mire Slime — Translucent Emerald-Teal Blob with Gleaming Eyes */
            int sq = ((frame >> 3) & 1u) ? 1 : 0;
            fill_ellipse_alpha(ox, oy - 5 + sq, 7 + sq, 6 - sq, 0xFF44C888u, 225);
            fill_ellipse_alpha(ox - 2, oy - 7 + sq, 3, 2, 0xFFB8F8D8u, 190);
            spr_box(ox, oy + sq, -3, -6, 2, 2, 0xFF141820u, flip, flash, sx, sy);
            spr_box(ox, oy + sq,  1, -6, 2, 2, 0xFF141820u, flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_BOAR: {
            /* Boar Halberdier — Tusks, Iron Helm, Crimson Plume & Halberd */
            spr_box(ox, oy + bob, -6, -15, 12, 8, 0xFF586074u, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -2, -18,  4, 3, 0xFFD83E4Au, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -4, -10,  8, 4, 0xFFE0929Cu, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -6,  -8,  2, 2, 0xFFFFF6E4u, flip, flash, sx, sy);
            spr_box(ox, oy + bob,  4,  -8,  2, 2, 0xFFFFF6E4u, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -6,  -6, 12, 6, 0xFF424858u, flip, flash, sx, sy);
            spr_box(ox, oy + bob,  6, -17,  2, 17, 0xFF7A5638u, flip, flash, sx, sy);
            spr_box(ox, oy + bob,  7, -17,  4,  5, 0xFFC8D4E4u, flip, flash, sx, sy);
            break;
        }
        case SPR_BOSS_MUCKMAW: {
            /* King Muckmaw — Huge Crowned Sewer Ooze Boss */
            int b = ((frame >> 3) & 1u) ? -1 : 0;
            fill_ellipse_alpha(ox, oy - 13 + b, 18, 13, 0xFF2E8C6Au, 240);
            fill_ellipse_alpha(ox, oy - 15 + b, 14, 10, 0xFF52C890u, 220);
            /* Crooked Gold Crown */
            spr_box(ox, oy + b, -8, -28, 16, 4, 0xFFF5B841u, flip, flash, sx, sy);
            spr_box(ox, oy + b, -8, -31,  3, 3, 0xFFFFF1A8u, flip, flash, sx, sy);
            spr_box(ox, oy + b, -1, -32,  3, 4, 0xFFFFF1A8u, flip, flash, sx, sy);
            spr_box(ox, oy + b,  5, -31,  3, 3, 0xFFFFF1A8u, flip, flash, sx, sy);
            /* Glowing Amber Eyes & Wide Maw */
            spr_box(ox, oy + b, -8, -19,  4, 4, 0xFFFFE066u, flip, flash, sx, sy);
            spr_box(ox, oy + b,  4, -19,  4, 4, 0xFFFFE066u, flip, flash, sx, sy);
            spr_box(ox, oy + b, -9, -11, 18, 5, 0xFF182420u, flip, flash, sx, sy);
            spr_box(ox, oy + b, -7, -11,  3, 2, 0xFFFFF8E8u, flip, flash, sx, sy);
            spr_box(ox, oy + b,  4, -11,  3, 2, 0xFFFFF8E8u, flip, flash, sx, sy);
            break;
        }
        case SPR_BOSS_IRONFANG: {
            /* Lord Ironfang — Towering Armored Boar Warlord Boss */
            int b = ((frame >> 4) & 1u) ? -1 : 0;
            spr_box(ox, oy + b, -13, -24, 26, 18, 0xFF3A3E4Eu, flip, flash, sx, sy);
            spr_box(ox, oy + b, -15, -22, 30,  6, 0xFF687088u, flip, flash, sx, sy);
            spr_box(ox, oy + b, -11, -18, 22,  3, 0xFFE8B84Cu, flip, flash, sx, sy);
            /* Crimson Cape */
            spr_box(ox, oy + b, -15, -16,  4, 15, 0xFFB82C3Cu, flip, flash, sx, sy);
            spr_box(ox, oy + b,  11, -16,  4, 15, 0xFFB82C3Cu, flip, flash, sx, sy);
            /* Horned Boar Head + Tusks */
            spr_box(ox, oy + b,  -8, -32, 16, 10, 0xFFE296A0u, flip, flash, sx, sy);
            spr_box(ox, oy + b,  -9, -35, 18,  4, 0xFF52586Cu, flip, flash, sx, sy);
            spr_box(ox, oy + b,  -5, -29,  3,  2, 0xFFFF4A4Au, flip, flash, sx, sy);
            spr_box(ox, oy + b,   2, -29,  3,  2, 0xFFFF4A4Au, flip, flash, sx, sy);
            spr_box(ox, oy + b, -10, -25,  3,  4, 0xFFFFF8E8u, flip, flash, sx, sy);
            spr_box(ox, oy + b,   7, -25,  3,  4, 0xFFFFF8E8u, flip, flash, sx, sy);
            /* Boots */
            spr_box(ox, oy,      -9,  -6,  7,  6, 0xFF262834u, flip, flash, sx, sy);
            spr_box(ox, oy,       2,  -6,  7,  6, 0xFF262834u, flip, flash, sx, sy);
            break;
        }
        case SPR_HOLLOW_OAK: {
            /* The Hollow Oak Portal — Ancient Gnarled Trunk with Shimmering Starlight Arch */
            int pulse = 5 + (((int)frame >> 3) & 2);
            spr_box(ox, oy, -11, -24, 22, 24, 0xFF583824u, flip, flash, sx, sy);
            spr_box(ox, oy,  -9, -22, 18, 20, 0xFF724B30u, flip, flash, sx, sy);
            fill_ellipse_alpha(ox, oy - 9, pulse + 2, 8, 0xFF5AD2F4u, 110);
            fill_ellipse_alpha(ox, oy - 9, 5, 7, 0xFFFFF1A8u, 220);
            fill_ellipse_alpha(ox, oy - 24, 15, 8, 0xFF3E7C44u, 255);
            fill_ellipse_alpha(ox, oy - 27, 11, 6, 0xFF68B058u, 255);
            break;
        }
        case SPR_BOBBER: {
            /* Fishing Bobber (red/white float + water ripples) */
            int boby = ((frame >> 3) & 1u) ? 1 : 0;
            draw_circle_outline(ox, oy + boby, 6 + (((int)frame >> 3) & 3), 0xFF9AD8F4u);
            fill_rect_px(ox - 2, oy - 4 + boby, 4, 3, 0xFFE84848u);
            fill_rect_px(ox - 2, oy - 1 + boby, 4, 2, 0xFFFFF8E8u);
            break;
        }
        /* Tall 16x24 Alderbrook NPCs (Townsfolk, Mara, Barnaby, Maple, Jasper) handled above */
        case SPR_LOCOMOTIVE: {
            /* Alderbrook Mountain Steam Locomotive (48x28 idling brass & iron engine) */
            int chug = ((frame >> 4) & 1u) ? -1 : 0;
            /* Animated chimney steam plume */
            int puff = (int)((frame >> 2) & 7u);
            fill_ellipse_alpha(ox - 16 - (puff >> 1), oy - 26 - puff, 4 + (puff >> 2), 3, 0xFFEAF0F6u, (uint8_t)(175 - puff * 16));
            fill_ellipse_alpha(ox - 14, oy - 23 - (puff >> 1), 3, 2, 0xFFFFFFFFu, 190);
            /* Iron cowcatcher & smokestack */
            spr_box(ox, oy + chug, -24,  -7,  6,  6, 0xFF3A3E4Au, flip, flash, sx, sy);
            spr_box(ox, oy + chug, -19, -22,  5,  8, 0xFF2C303Cu, flip, flash, sx, sy);
            spr_box(ox, oy + chug, -20, -23,  7,  2, 0xFFE8B84Cu, flip, flash, sx, sy);
            /* Main dark-emerald & iron boiler with brass bands */
            spr_box(ox, oy + chug, -20, -15, 26, 10, 0xFF283E3Au, flip, flash, sx, sy);
            spr_box(ox, oy + chug, -18, -14, 22,  3, 0xFF3E5E54u, flip, flash, sx, sy);
            spr_box(ox, oy + chug, -11, -18,  6,  4, 0xFFE8B84Cu, flip, flash, sx, sy); /* Brass dome */
            spr_box(ox, oy + chug,  -2, -15,  2, 10, 0xFFE8B84Cu, flip, flash, sx, sy);
            /* Warm lit engineer's cab */
            spr_box(ox, oy + chug,   6, -21, 16, 16, 0xFF6A2E34u, flip, flash, sx, sy);
            spr_box(ox, oy + chug,   4, -23, 20,  3, 0xFF262A34u, flip, flash, sx, sy);
            spr_box(ox, oy + chug,  10, -18,  8,  6, 0xFFFFE498u, flip, flash, sx, sy);
            /* Warm headlamp glow */
            fill_ellipse_alpha(ox - 22, oy - 12 + chug, 5, 4, 0xFFFFE898u, 110);
            spr_box(ox, oy + chug, -22, -13,  2,  3, 0xFFFFF8D0u, flip, flash, sx, sy);
            /* Crimson iron spoked wheels & brass rod */
            for (int wx = -16; wx <= 16; wx += 10) {
                spr_box(ox, oy, wx - 3, -6, 6, 6, 0xFF9E3438u, flip, flash, sx, sy);
                spr_box(ox, oy, wx - 1, -4, 2, 2, 0xFFE8B84Cu, flip, flash, sx, sy);
            }
            spr_box(ox, oy, -16, -4 + (chug ? 1 : 0), 32, 2, 0xFFD4DCE6u, flip, flash, sx, sy);
            break;
        }
        /* Tall 16x24 Sunhill NPCs (Tessa, Corin, Sentinel) handled above */
        case SPR_RAFT: {
            /* Sun-Cedar Raft + Animated Turquoise Water Wake */
            int wake = (int)((frame >> 3) & 3u);
            draw_circle_outline(ox, oy - 1, 9 + wake, 0xFFA8F2FCu);
            spr_box(ox, oy, -9, -6, 18, 9, 0xFF5C361Cu, flip, flash, sx, sy);
            spr_box(ox, oy, -8, -5, 16, 2, 0xFFB07242u, flip, flash, sx, sy);
            spr_box(ox, oy, -8, -2, 16, 2, 0xFFA06436u, flip, flash, sx, sy);
            spr_box(ox, oy, -8,  1, 16, 2, 0xFFB07242u, flip, flash, sx, sy);
            spr_box(ox, oy, -5, -6,  2, 9, 0xFFE6C88Au, flip, flash, sx, sy);
            spr_box(ox, oy,  3, -6,  2, 9, 0xFFE6C88Au, flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_SCORPION: {
            /* Dune Scorpion — Crimson Carapace, Pincers & Raised Stinger */
            spr_box(ox, oy + bob, -6,  -7, 12, 6, 0xFF9E2C28u, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -4,  -8,  8, 3, 0xFFD44E38u, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -10, -9,  4, 4, 0xFFC43A30u, flip, flash, sx, sy);
            spr_box(ox, oy + bob,   6, -9,  4, 4, 0xFFC43A30u, flip, flash, sx, sy);
            spr_box(ox, oy + bob,   3,-14,  3, 7, 0xFF9E2C28u, flip, flash, sx, sy);
            spr_box(ox, oy + bob,   1,-16,  4, 3, 0xFFFFE066u, flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_SANDWORM: {
            /* Giant Desert Sandworm — Segmented Coiling Ochre Titan */
            int sw = ((frame >> 3) & 1u) ? -1 : 0;
            spr_box(ox, oy + sw, -12, -24, 24, 22, 0xFFB8783Cu, flip, flash, sx, sy);
            spr_box(ox, oy + sw, -10, -22, 20,  5, 0xFFD89A54u, flip, flash, sx, sy);
            spr_box(ox, oy + sw, -10, -14, 20,  5, 0xFFD89A54u, flip, flash, sx, sy);
            spr_box(ox, oy + sw,  -8, -28, 16,  8, 0xFF42181Cu, flip, flash, sx, sy);
            spr_box(ox, oy + sw,  -6, -27,  2,  3, 0xFFFFF4DCu, flip, flash, sx, sy);
            spr_box(ox, oy + sw,   4, -27,  2,  3, 0xFFFFF4DCu, flip, flash, sx, sy);
            spr_box(ox, oy + sw,  -1, -28,  2,  3, 0xFFFFF4DCu, flip, flash, sx, sy);
            break;
        }
        case SPR_FAUNA_CAMEL: {
            /* Sands of Babana Caravan Camel */
            spr_box(ox, oy + bob, -6, -10, 12, 6, 0xFFD8A662u, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -3, -14,  6, 4, 0xFFC48E4Cu, flip, flash, sx, sy);
            spr_box(ox, oy + bob,  5, -15,  4, 7, 0xFFD8A662u, flip, flash, sx, sy);
            spr_box(ox, oy + bob,  6, -15,  4, 3, 0xFFE6BC7Au, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -4, -10,  7, 3, 0xFFC43A3Cu, flip, flash, sx, sy);
            spr_box(ox, oy,       -5,  -4,  2, 4, 0xFFA8783Cu, flip, flash, sx, sy);
            spr_box(ox, oy,        3,  -4,  2, 4, 0xFFA8783Cu, flip, flash, sx, sy);
            break;
        }
        case SPR_FAUNA_PENGUIN: {
            /* Frostmere North Festive Penguin with Red Scarf */
            spr_box(ox, oy + bob, -4, -12,  8, 10, 0xFF202A3Au, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -3,  -9,  6,  7, 0xFFF4F8FCu, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -4, -10,  8,  2, 0xFFD83844u, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -1, -11,  3,  2, 0xFFF5A838u, flip, flash, sx, sy);
            spr_box(ox, oy,       -3,  -2,  2,  2, 0xFFF5A838u, flip, flash, sx, sy);
            spr_box(ox, oy,        1,  -2,  2,  2, 0xFFF5A838u, flip, flash, sx, sy);
            break;
        }
        case SPR_FAUNA_MONKEY: {
            /* Canopy Wilds Golden-Tailed Monkey */
            spr_box(ox, oy + bob, -4,  -9,  8, 6, 0xFF8C5430u, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -3, -14,  6, 5, 0xFFA6683Cu, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -2, -12,  4, 3, 0xFFEEC69Cu, flip, flash, sx, sy);
            spr_box(ox, oy + bob, -7, -11,  3, 4, 0xFF8C5430u, flip, flash, sx, sy);
            spr_box(ox, oy,       -3,  -3,  2, 3, 0xFF6E4024u, flip, flash, sx, sy);
            spr_box(ox, oy,        1,  -3,  2, 3, 0xFF6E4024u, flip, flash, sx, sy);
            break;
        }
        case SPR_FAUNA_ROBOT: {
            /* Aetheris Citadel Hover-Automaton with Glowing Cyan Visor */
            int hv = ((frame >> 3) & 1u) ? -2 : -1;
            spr_box(ox, oy + hv, -5, -14, 10, 10, 0xFFAEC0D8u, flip, flash, sx, sy);
            spr_box(ox, oy + hv, -4, -13,  8,  4, 0xFF202C3Cu, flip, flash, sx, sy);
            spr_box(ox, oy + hv, -3, -12,  6,  2, 0xFF4AF2FFu, flip, flash, sx, sy);
            spr_box(ox, oy + hv, -1, -17,  2,  3, 0xFF4AF2FFu, flip, flash, sx, sy);
            fill_ellipse_alpha(ox, oy - 2, 5, 2, 0xFF4AF2FFu, 150);
            break;
        }
        /* Tall 16x24 Shipwright Orin handled above */
        case SPR_BABANA_STALL: {
            /* Golden Babana Desert Market Stall with Striped Canopy */
            spr_box(ox, oy, -11, -16, 22,  5, 0xFFE4503Cu, flip, flash, sx, sy);
            spr_box(ox, oy,  -7, -16,  4,  5, 0xFFFFF0C4u, flip, flash, sx, sy);
            spr_box(ox, oy,   3, -16,  4,  5, 0xFFFFF0C4u, flip, flash, sx, sy);
            spr_box(ox, oy, -10, -11,  2, 11, 0xFF7C4E2Cu, flip, flash, sx, sy);
            spr_box(ox, oy,   8, -11,  2, 11, 0xFF7C4E2Cu, flip, flash, sx, sy);
            spr_box(ox, oy, -10,  -6, 20,  6, 0xFF9C663Au, flip, flash, sx, sy);
            /* Pyramid of bright golden Babanas! */
            spr_box(ox, oy,  -6,  -9, 12,  3, 0xFFFFD838u, flip, flash, sx, sy);
            spr_box(ox, oy,  -3, -11,  6,  2, 0xFFFFF07Cu, flip, flash, sx, sy);
            break;
        }
        default:
            break;
    }
}

/* ============================================================================
 * 6. EMERALD DRAGON RETRO INTRO, BATTLE ARENA, DIALOGUE, TITLE & HUD OVERLAYS
 * ============================================================================ */
static inline uint32_t rgb565_to_argb(uint16_t px) {
    uint32_t r = ((uint32_t)(px >> 11) & 31u);
    uint32_t g = ((uint32_t)(px >> 5)  & 63u);
    uint32_t b = ((uint32_t)px         & 31u);
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

static void render_intro_scene(const HearthState *st, const HearthPpuPacket *ppu) {
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    uint8_t plate = (st->intro.scene < CINEMA_SDL_PLATE_COUNT) ? st->intro.scene : 0u;
    uint32_t f = ppu->frame_count;
    int pt = (int)st->intro.plate_timer;
    if (pt > 720) pt = 720;

    /* 1. Full-Color Painted Anime CG Plate with smooth Ken Burns camera pan */
    int view_y0 = 18;
    int view_h  = 134;
    int max_pan_x = CINEMA_SDL_W - w;
    if (max_pan_x < 0) max_pan_x = 0;
    int max_pan_y = CINEMA_SDL_H - view_h;
    if (max_pan_y < 0) max_pan_y = 0;

    int pan_x = max_pan_x / 2;
    int pan_y = max_pan_y / 2;
    int prog = (pt * 256) / 720; /* 0..256 */

    switch (st->intro.cam_mode) {
        case CIN_CAM_PAN_RIGHT:
            pan_x = (max_pan_x * prog) >> 8;
            pan_y = max_pan_y / 2;
            break;
        case CIN_CAM_PAN_LEFT:
            pan_x = max_pan_x - ((max_pan_x * prog) >> 8);
            pan_y = max_pan_y / 2;
            break;
        case CIN_CAM_ZOOM_IN:
            pan_x = max_pan_x / 2;
            pan_y = (max_pan_y * (256 - prog)) >> 8;
            break;
        case CIN_CAM_SHAKE_FLASH:
            pan_x = max_pan_x / 2 + ppu->shake_x;
            pan_y = max_pan_y / 2 + ppu->shake_y;
            break;
        case CIN_CAM_STILL_DAWN:
        default:
            pan_x = max_pan_x / 2;
            pan_y = max_pan_y / 2;
            break;
    }
    if (pan_x < 0) pan_x = 0;
    if (pan_x > max_pan_x) pan_x = max_pan_x;
    if (pan_y < 0) pan_y = 0;
    if (pan_y > max_pan_y) pan_y = max_pan_y;

    const uint16_t *src = CINEMA_SDL_PLATES[plate];
    bool dawn_wash = (st->intro.cam_mode == CIN_CAM_STILL_DAWN);
    for (int y = 0; y < view_h; y++) {
        int sy = pan_y + y;
        if (sy >= CINEMA_SDL_H) sy = CINEMA_SDL_H - 1;
        const uint16_t *srow = &src[sy * CINEMA_SDL_W + pan_x];
        uint32_t *drow = &g_fb[(view_y0 + y) * FB_MAX_W];
        for (int x = 0; x < w; x++) {
            uint32_t col = rgb565_to_argb(srow[x]);
            if (dawn_wash) {
                uint32_t r = ((col >> 16) & 0xFFu) + 26u;
                uint32_t g = ((col >> 8)  & 0xFFu) + 14u;
                uint32_t b = (col         & 0xFFu);
                if (r > 255u) r = 255u;
                if (g > 255u) g = 255u;
                b = (b * 7u) >> 3;
                col = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
            drow[x] = col;
        }
    }

    /* 2. Atmospheric Cinema Particles (steam dusk, forest pollen, lanterns, violet storm, fireflies) */
    uint8_t fx = st->intro.fx_mode;
    {
        uint32_t pcol = (fx == CIN_FX_VIOLET_STORM)      ? 0xFFB888E8u
                      : (fx == CIN_FX_MOON_FIREFLIES)    ? 0xFFAEEBFFu
                      : (fx == CIN_FX_FOREST_POLLEN)     ? 0xFFFFE59Au
                      : (fx == CIN_FX_EVERTALE_LANTERNS) ? 0xFFFFD878u
                      : 0xFFE8DCC8u;
        int pcount = (fx == CIN_FX_VIOLET_STORM) ? 26 : 18;
        for (int i = 0; i < pcount; i++) {
            int px, py;
            if (fx == CIN_FX_VIOLET_STORM) {
                px = ((i * 37 + (int)(f >> 1)) % w);
                py = view_y0 + ((view_h - (((i * 23 + (int)f) % view_h))) % view_h);
            } else if (fx == CIN_FX_STEAM_DUSK || fx == CIN_FX_EVERTALE_LANTERNS) {
                px = ((i * 43 + (int)(f >> 2)) % w);
                py = view_y0 + ((view_h - (((i * 19 + (int)(f >> 1)) % view_h))) % view_h);
            } else {
                px = ((i * 53 + (int)(f >> 2)) % w);
                py = view_y0 + ((i * 29 + (HEARTH_SIN64[((int)(f >> 2) + i * 5) & 63] >> 4) + view_h) % view_h);
            }
            put_px_blend(px, py, pcol, 190);
            if ((i & 3) == 0) {
                put_px_blend(px + 1, py, pcol, 110);
                put_px_blend(px, py + 1, pcol, 110);
            }
        }
    }

    /* Subtle top & bottom vignette seam into letterbox bars */
    fill_rect_alpha(0, view_y0, w, 6, 0xFF08070Fu, 130);
    fill_rect_alpha(0, view_y0 + view_h - 8, w, 8, 0xFF08070Fu, 150);

    /* 3. Top Cinema Letterbox Bar: Act Title + 7 Act Notches + AUTO Indicator */
    fill_rect_px(0, 0, w, view_y0, 0xFF090810u);
    fill_rect_px(0, view_y0 - 1, w, 1, 0xFF9E7B42u);
    if (st->intro.title && st->intro.title[0] != '\0') {
        draw_text(8, 5, st->intro.title, 0xFFFFF1A8u, true);
    }
    int total_pips = (st->intro.script_id > 0 && st->intro.beat_count > 0) ? (int)st->intro.beat_count : 7;
    if (total_pips > 8) total_pips = 8;
    int cur_pip = (st->intro.script_id > 0) ? ((int)st->intro.beat + 1) : (int)st->intro.act_num;
    int pip_x0 = w - (total_pips * 8 + 6);
    for (int i = 0; i < total_pips; i++) {
        int act_idx = i + 1;
        uint32_t col = (act_idx == cur_pip) ? 0xFFFFE59Au
                     : (act_idx < cur_pip)  ? 0xFF8C7244u : 0xFF332D42u;
        fill_rect_px(pip_x0 + i * 8, 6, (act_idx == cur_pip) ? 6 : 4, 4, col);
    }

    /* 4. Bottom Illustrated Narrative Bar: Speaker Nameplate + 2-line Typewriter + Controls */
    int ny = view_y0 + view_h;
    fill_rect_px(0, ny, w, h - ny, 0xFF0A0912u);
    fill_rect_px(0, ny, w, 1, 0xFF9E7B42u);

    if (st->intro.speaker && st->intro.speaker[0] != '\0') {
        draw_text(10, ny + 6, st->intro.speaker, 0xFFFFD878u, true);
    }

    int total_vis = (int)st->intro.chars;
    const char *l1 = st->intro.line1 ? st->intro.line1 : "";
    const char *l2 = st->intro.line2 ? st->intro.line2 : "";
    int len1 = (int)strlen(l1);
    int v1 = (total_vis < len1) ? total_vis : len1;
    int v2 = (total_vis > len1) ? (total_vis - len1) : 0;

    char b1[96], b2[96];
    if (v1 > 90) v1 = 90;
    if (v2 > 90) v2 = 90;
    memcpy(b1, l1, (size_t)v1); b1[v1] = '\0';
    memcpy(b2, l2, (size_t)v2); b2[v2] = '\0';

    int text_y = (st->intro.speaker && st->intro.speaker[0] != '\0') ? (ny + 18) : (ny + 12);
    draw_text_centered(w / 2, text_y,      b1, 0xFFF7F0DCu, true);
    draw_text_centered(w / 2, text_y + 13, b2, 0xFFEADAB8u, true);

    draw_text(8, h - 12,
        st->intro.auto_play ? "[SPACE/A] NEXT  [Y] AUTO:ON" : "[SPACE/A] NEXT  [Y] AUTO:OFF",
        0xFF9AD8F4u, false);
    draw_text(w - 74, h - 12, "[ESC] SKIP", 0xFFC8BA9Au, false);
}

static void render_battle_vfx(const HearthState *st, const HearthPpuPacket *ppu) {
    if (st->battle.vfx_kind == BVFX_NONE || st->battle.vfx_dur == 0) return;
    int sx = st->battle.vfx_x + ppu->shake_x;
    int sy = st->battle.vfx_y + ppu->shake_y;
    int tx = st->battle.vfx_tx + ppu->shake_x;
    int ty = st->battle.vfx_ty + ppu->shake_y;
    int dur = (int)st->battle.vfx_dur;
    int elap = dur - (int)st->battle.vfx_timer;
    if (elap < 0) elap = 0;
    int p = (dur > 0) ? (elap * 256) / dur : 256;
    if (p > 256) p = 256;

    switch (st->battle.vfx_kind) {
        case BVFX_STAFF_ARC: {
            int cx = sx + (((tx - sx) * p) >> 8);
            int cy = sy + (((ty - sy) * p) >> 8) - ((HEARTH_SIN64[(p >> 3) & 31] * 14) >> 7);
            draw_line_px(cx - 10, cy - 8, cx + 8, cy + 8, 0xFFFFF6CCu);
            draw_line_px(cx - 8, cy - 10, cx + 10, cy + 6, 0xFFF5B841u);
            draw_circle_outline(cx, cy, 8 + (p >> 5), 0xFFFFE082u);
            break;
        }
        case BVFX_ACORN_SPIN: {
            int cx = sx + (((tx - sx) * p) >> 8);
            int cy = sy + (((ty - sy) * p) >> 8);
            draw_circle_outline(cx, cy, 12, 0xFF8CD674u);
            draw_circle_outline(cx, cy, 8, 0xFFFFF1A8u);
            draw_line_px(cx - 11, cy, cx + 11, cy, 0xFFF5B841u);
            draw_line_px(cx, cy - 11, cx, cy + 11, 0xFFF5B841u);
            break;
        }
        case BVFX_SOLAR_METEOR: {
            int cx = sx + (((tx - sx) * p) >> 8);
            int cy = sy + (((ty - sy) * p) >> 8) - ((HEARTH_SIN64[(p >> 3) & 31] * 48) >> 7);
            fill_ellipse_alpha(cx, cy, 14, 14, 0xFFE86A4Au, 115);
            fill_ellipse_alpha(cx, cy, 8, 8, 0xFFFFE066u, 190);
            draw_circle_outline(cx, cy, 16 + ((elap >> 1) & 3), 0xFFFFF6CCu);
            if (p > 210) {
                draw_circle_outline(tx, ty, (p - 210) * 1, 0xFFFFE066u);
                draw_circle_outline(tx, ty, (p - 210) / 2 + 4, 0xFFE86A4Au);
            }
            break;
        }
        case BVFX_STAR_BOLT: {
            int cx = sx + (((tx - sx) * p) >> 8);
            int cy = sy + (((ty - sy) * p) >> 8) - ((HEARTH_SIN64[(p >> 3) & 31] * 26) >> 7);
            draw_line_px(sx, sy, cx, cy, 0xFF5AD2F4u);
            fill_ellipse_alpha(cx, cy, 9, 9, 0xFF5AD2F4u, 140);
            fill_rect_px(cx - 3, cy - 3, 6, 6, 0xFFFFFFFFu);
            draw_line_px(cx - 8, cy, cx + 8, cy, 0xFFFFF6CCu);
            draw_line_px(cx, cy - 8, cx, cy + 8, 0xFFFFF6CCu);
            break;
        }
        case BVFX_PRISM_NOVA: {
            int r1 = 8 + ((p * 32) >> 8);
            int r2 = 4 + ((p * 20) >> 8);
            fill_ellipse_alpha(tx, ty, r2, r2, 0xFF5AD2F4u, 85);
            draw_circle_outline(tx, ty, r1, 0xFFFFF1A8u);
            draw_circle_outline(tx, ty, r2, 0xFF5AD2F4u);
            draw_line_px(tx - r1, ty, tx + r1, ty, 0xFFFFF6CCu);
            draw_line_px(tx, ty - r1, tx, ty + r1, 0xFF9AD8F4u);
            break;
        }
        case BVFX_ENEMY_CLAW: {
            int cx = sx + (((tx - sx) * p) >> 8);
            int cy = sy + (((ty - sy) * p) >> 8);
            for (int k = -1; k <= 1; k++) {
                draw_line_px(cx - 10, cy - 8 + k * 4, cx + 10, cy + 8 + k * 4, 0xFFE85272u);
            }
            break;
        }
        case BVFX_GUARD_SHIELD: {
            int r = 14 + ((p * 10) >> 8);
            fill_ellipse_alpha(sx, sy, r - 2, r, 0xFF5AD2F4u, 80);
            draw_circle_outline(sx, sy, r, 0xFFFFFFFFu);
            draw_circle_outline(sx, sy, r - 3, 0xFF5AD2F4u);
            break;
        }
        case BVFX_HEAL_BLOOM: {
            int r = 10 + ((p * 18) >> 8);
            fill_ellipse_alpha(sx, sy, r, r / 2 + 4, 0xFF74D680u, 75);
            draw_circle_outline(sx, sy - (elap >> 1), r / 2 + 4, 0xFFFFF1A8u);
            break;
        }
        default:
            break;
    }
}

static void render_battle_scene(const HearthState *st, const HearthPpuPacket *ppu) {
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    uint8_t biome = st->battle.biome;
    bool shrine   = (biome == BIOME_SHRINE);
    bool sewer    = (biome == BIOME_SEWER);
    bool boar     = (biome == BIOME_BOARHOLD);
    bool desert   = (biome == BIOME_DESERT);
    bool paradise = (biome == BIOME_PARADISE);

    /* Sky / Cavern / Citadel / Desert / Paradise gradient */
    for (int y = 0; y < 96; y++) {
        uint32_t col;
        if (sewer) {
            col = (y < 32) ? 0xFF0A1418u : (y < 64) ? 0xFF12262Cu : 0xFF1B3A3Cu;
        } else if (boar) {
            col = (y < 32) ? 0xFF28121Cu : (y < 64) ? 0xFF48202Cu : 0xFF723438u;
        } else if (shrine) {
            col = (y < 32) ? 0xFF1B1932u : (y < 64) ? 0xFF28264Bu : 0xFF3A3562u;
        } else if (desert) {
            col = (y < 32) ? 0xFFE08C3Au : (y < 64) ? 0xFFF2B85Cu : 0xFFFFE29Cu;
        } else if (paradise) {
            col = (y < 32) ? 0xFF2896D8u : (y < 64) ? 0xFF56C6E8u : 0xFFA8F0F8u;
        } else {
            col = (y < 32) ? 0xFF4D9CD0u : (y < 64) ? 0xFF77BBE6u : 0xFFBEE7F8u;
        }
        fill_rect_px(0, y, w, 1, col);
    }

    /* Distant rolling hills / dunes / coral horizon */
    for (int x = 0; x < w; x++) {
        int hill = 82 + (HEARTH_SIN64[(x >> 2) & 63] >> 4);
        uint32_t hcol = sewer ? 0xFF102022u : boar ? 0xFF321820u : shrine ? 0xFF232538u
                      : desert ? 0xFFD29A48u : paradise ? 0xFF3AAEC4u : 0xFF4A884Eu;
        fill_rect_px(x, hill, 1, 96 - hill, hcol);
    }

    /* Battleground stage floor */
    uint32_t floor_col = sewer ? 0xFF2A3E42u : boar ? 0xFF5A4C52u : shrine ? 0xFF4C5868u
                       : desert ? 0xFFE8C26Eu : paradise ? 0xFFF2DDA6u : 0xFF74B461u;
    uint32_t trim_col  = sewer ? 0xFF44B888u : boar ? 0xFFE07A48u : shrine ? 0xFF8591A0u
                       : desert ? 0xFFF8DF98u : paradise ? 0xFF7ADCF0u : 0xFFA8D678u;
    fill_rect_px(0, 96, w, h - 96, floor_col);
    fill_rect_px(0, 96, w, 3, trim_col);
    if (sewer) {
        fill_ellipse_alpha(w / 2, 136, 95, 18, 0xFF38A878u, 70);
    } else if (boar) {
        draw_circle_outline(w / 2, 136, 46, 0xFFE8A048u);
    } else if (shrine) {
        draw_circle_outline(w / 2, 134, 44, pal_col(3, 15));
        draw_circle_outline(w / 2, 134, 30, pal_col(0, 11));
    } else if (desert) {
        fill_ellipse_alpha(w / 2, 136, 95, 20, 0xFFD4A04Cu, 110);
    } else if (paradise) {
        fill_ellipse_alpha(w / 2, 136, 95, 20, 0xFF52C4D8u, 85);
    } else {
        fill_ellipse_alpha(w / 2, 136, 95, 22, pal_col(0, 3), 90);
    }

    /* Ni No Kuni Soul-Arc Swap Beam */
    if (st->battle.phase == BPHASE_SWAP_ARC) {
        int t = (int)st->battle.swap_arc_fp;
        int sx = st->battle.wizard_back_x + ppu->shake_x;
        int sy = st->battle.wizard_back_y - 10 + ppu->shake_y;
        int ex = st->battle.hero_home_x + ppu->shake_x;
        int ey = st->battle.hero_home_y - 10 + ppu->shake_y;
        int orb_x = sx + (((ex - sx) * t) >> 8);
        int orb_y = sy + (((ey - sy) * t) >> 8) - ((HEARTH_SIN64[(t >> 3) & 31] * 26) >> 7);
        draw_line_px(sx, sy, orb_x, orb_y, pal_col(1, 11));
        fill_ellipse_alpha(orb_x, orb_y, 7, 7, pal_col(1, 10), 180);
        fill_rect_px(orb_x - 3, orb_y - 3, 6, 6, 0xFFFFFFFFu);
    }

    /* Draw Y-sorted OAM battle sprites */
    for (uint8_t i = 0; i < ppu->oam_count; i++) {
        const HearthOamSprite *o = &ppu->oam[i];
        draw_oam_sprite(o, o->x + ppu->shake_x, o->y + ppu->shake_y, ppu->frame_count);
    }

    /* Sea-of-Stars Choreographed Battle VFX */
    render_battle_vfx(st, ppu);

    /* Enemy HP bars, Target Arrow, and Spell Lock Badges */
    for (uint8_t i = 0; i < st->battle.enemy_count; i++) {
        const HearthBattleEnemy *e = &st->battle.enemies[i];
        if (!e->alive) continue;
        int ex = e->x + ppu->shake_x;
        int ey = e->y + ppu->shake_y - (e->is_boss ? 36 : 22);

        /* Target selector chevron */
        if (st->battle.phase == BPHASE_COMMAND && st->battle.selected_enemy == i) {
            int bounce = ((ppu->frame_count >> 3) & 1u) ? -2 : 0;
            draw_text_centered(ex, ey - 20 + bounce, "V", 0xFFFFF1A8u, true);
        }

        /* HP Bar */
        int bw = e->is_boss ? 44 : 28;
        int bx = ex - bw / 2;
        fill_rect_px(bx - 1, ey - 1, bw + 2, 5, 0xFF14101Au);
        int fill = (e->max_hp > 0) ? (bw * e->hp) / e->max_hp : 0;
        if (fill < 0) fill = 0;
        fill_rect_px(bx, ey, fill, 3, e->staggered ? 0xFFF5B841u : 0xFFE04E4Eu);

        /* Spell Locks Row */
        if (e->lock_count > 0) {
            int row_w = e->lock_count * 10 + 10;
            int lx = ex - row_w / 2;
            int ly = ey - 11;
            char tbuf[4];
            tbuf[0] = (char)('0' + (e->charge_turns % 10));
            tbuf[1] = '\0';
            fill_rect_px(lx - 1, ly - 1, 9, 9, 0xFF191421u);
            draw_text(lx + 1, ly, tbuf, (e->charge_turns <= 1) ? 0xFFFF5555u : 0xFFFFF1A8u, false);
            lx += 11;
            for (uint8_t k = 0; k < e->lock_count; k++) {
                uint32_t lcol = (e->locks[k] == LOCK_SWORD) ? 0xFFF5B841u
                              : (e->locks[k] == LOCK_SUN)   ? 0xFFE86A4Au : 0xFF5AD2F4u;
                const char *ch = (e->locks[k] == LOCK_SWORD) ? "S"
                               : (e->locks[k] == LOCK_SUN)   ? "O" : "*";
                if (e->broken_locks[k]) {
                    fill_rect_px(lx, ly, 8, 8, 0xFF282430u);
                    draw_rect_outline(lx, ly, 8, 8, 0xFF555060u);
                } else {
                    fill_rect_px(lx, ly, 8, 8, 0xFF1B1726u);
                    draw_rect_outline(lx, ly, 8, 8, lcol);
                    draw_text(lx + 2, ly + 1, ch, lcol, false);
                }
                lx += 10;
            }
        }
    }

    /* Timed Hit / Parry Converging Timing Ring (Clearer & Wider Sweet Spot) */
    if (st->battle.ring.active) {
        int rx = st->battle.ring.x + ppu->shake_x;
        int ry = st->battle.ring.y + ppu->shake_y;
        int rem = 256 - (int)st->battle.ring.progress_fp;
        if (rem < 0) rem = 0;
        int outer_r = 10 + (rem * 28) / 256;
        bool in_sweet = st->battle.ring.progress_fp >= st->battle.ring.sweet_start_fp;
        uint32_t ring_col = in_sweet ? 0xFFFFF1A8u
                          : (st->battle.ring.is_parry ? 0xFF5AD2F4u : 0xFFF5B841u);
        if (in_sweet) {
            fill_ellipse_alpha(rx, ry, 12, 12, st->battle.ring.is_parry ? 0xFF5AD2F4u : 0xFFF5B841u, 75);
        }
        draw_circle_outline(rx, ry, 10, in_sweet ? 0xFFFFFFFFu : 0xFFE8B43Cu);
        draw_circle_outline(rx, ry, outer_r, ring_col);
        draw_circle_outline(rx, ry, outer_r - 1, ring_col);
        if (in_sweet) {
            draw_text_centered(rx, ry - 22, st->battle.ring.is_parry ? ">> [SPACE] PARRY! <<" : ">> [SPACE] STRIKE! <<", 0xFFFFF1A8u, true);
        } else {
            draw_text_centered(rx, ry - 22, st->battle.ring.is_parry ? "WAIT TO PARRY..." : "TIME [SPACE]...", 0xFF9AD8F4u, true);
        }
    }

    /* Top Action Banner */
    if (st->battle.banner_frames > 0 && st->battle.banner[0] != '\0') {
        fill_rect_alpha(w / 2 - 104, 6, 208, 14, 0xFF14111Du, 215);
        draw_rect_outline(w / 2 - 104, 6, 208, 14, 0xFFE8B43Cu);
        draw_text_centered(w / 2, 10, st->battle.banner, 0xFFFFF6CCu, true);
    }

    /* Bottom Classic 16-Bit JRPG Battle HUD Bar */
    int hud_y = h - 64;
    fill_rect_alpha(4, hud_y, w - 8, 60, 0xFF161322u, 238);
    draw_rect_outline(4, hud_y, w - 8, 60, 0xFFE8B852u);
    draw_rect_outline(5, hud_y + 1, w - 10, 58, 0xFF5C4A28u);

    /* Explicit Controls Hint Bar so players always know how to switch familiars! */
    const HearthPartyMember *act_pm = &st->party[st->battle.active_slot];
    draw_text(8, hud_y + 3, "[Q/E] SWAP FAMILIAR", 0xFF9AD8F4u, false);
    draw_text(w - 118, hud_y + 3, "[W/S] TARGET [SPACE] ACT", 0xFFD0C4A8u, false);

    /* Top Row inside HUD: Horizontal Battle Commands with Real Move Names */
    int tab_w = (w - 16) / 4;
    for (int i = 0; i < 4; i++) {
        int tx = 8 + i * tab_w;
        int ty = hud_y + 13;
        bool sel = (st->battle.selected_cmd == (uint8_t)i);
        fill_rect_px(tx, ty, tab_w - 3, 12, (sel && st->battle.phase == BPHASE_COMMAND) ? 0xFF3A2E58u : 0xFF211C31u);
        draw_rect_outline(tx, ty, tab_w - 3, 12, (sel && st->battle.phase == BPHASE_COMMAND) ? 0xFFFFF1A8u : 0xFF4A4262u);

        char label[24];
        if (i == 0) {
            snprintf(label, sizeof(label), "%s%.9s", sel ? ">" : "", act_pm->basic_name);
        } else if (i == 1) {
            snprintf(label, sizeof(label), "%s%.7s %u", sel ? ">" : "", act_pm->skill_name, (unsigned)act_pm->skill_cost);
        } else if (i == 2) {
            snprintf(label, sizeof(label), "%sGUARD", sel ? ">" : "");
        } else {
            snprintf(label, sizeof(label), "%sTONIC:%u", sel ? ">" : "", (unsigned)st->potions);
        }
        draw_text_centered(tx + (tab_w - 3) / 2, ty + 3, label, sel ? 0xFFFFF1A8u : 0xFFD0CCC4u, false);
    }

    /* Bottom Row inside HUD: 3 Party / Familiar Cards (Q/E or L/R instant hot-swap!) */
    int card_w = (w - 16) / 3;
    for (int i = 0; i < 3; i++) {
        const HearthPartyMember *pm = &st->party[i];
        int cx = 8 + i * card_w;
        int cy = hud_y + 28;
        bool active = (st->battle.active_slot == (uint8_t)i);
        fill_rect_px(cx, cy, card_w - 3, 29, active ? 0xFF2B2446u : 0xFF1D192Bu);
        draw_rect_outline(cx, cy, card_w - 3, 29, active ? 0xFFF5B841u : 0xFF4C4562u);

        char hdr[18];
        snprintf(hdr, sizeof(hdr), "%s%.8s", active ? "*" : "", pm->name);
        draw_text(cx + 3, cy + 3, hdr, active ? 0xFFFFF1A8u : 0xFFD0CCC4u, false);

        char hpbuf[32];
        snprintf(hpbuf, sizeof(hpbuf), "H%d/%d M%d", pm->hp, pm->max_hp, pm->mp);
        draw_text(cx + 3, cy + 12, hpbuf, 0xFFA8D678u, false);

        int bar_w = card_w - 9;
        int hfill = (pm->max_hp > 0) ? (bar_w * pm->hp) / pm->max_hp : 0;
        int mfill = (pm->max_mp > 0) ? (bar_w * pm->mp) / pm->max_mp : 0;
        fill_rect_px(cx + 3, cy + 21, bar_w, 2, 0xFF2E1A22u);
        fill_rect_px(cx + 3, cy + 21, hfill, 2, 0xFF74B461u);
        fill_rect_px(cx + 3, cy + 25, bar_w, 2, 0xFF1A2438u);
        fill_rect_px(cx + 3, cy + 25, mfill, 2, 0xFF5AD2F4u);
    }
}

static void render_particles_and_text(const HearthState *st, const HearthPpuPacket *ppu) {
    /* Particles */
    for (int i = 0; i < HEARTH_MAX_PARTICLES; i++) {
        const HearthParticle *p = &st->particles[i];
        if (!p->active) continue;
        int x = FROM_FP(p->fx) + (p->screen_space ? ppu->shake_x : -ppu->cam_x + ppu->shake_x);
        int y = FROM_FP(p->fy) + (p->screen_space ? ppu->shake_y : -ppu->cam_y + ppu->shake_y);
        uint32_t col = pal_col(p->pal_idx, p->col_idx);
        fill_rect_px(x, y, p->size, p->size, col);
    }

    /* Floating Popups */
    for (int i = 0; i < HEARTH_MAX_FTEXT; i++) {
        const HearthFloatText *ft = &st->ftexts[i];
        if (!ft->active) continue;
        int x = ft->x + (ft->world_space ? -ppu->cam_x : 0) + ppu->shake_x;
        int y = ft->y + (ft->world_space ? -ppu->cam_y : 0) + ppu->shake_y;
        draw_text_centered(x, y, ft->text, pal_col(ft->pal_idx, ft->col_idx), true);
    }
}

static void render_dialogue_overlay(const HearthState *st) {
    if (!st->dialogue.active) return;
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    bool show_choices = (st->dialogue.finished && st->dialogue.next_text == NULL && st->dialogue.choice_count > 0);
    int choice_rows = show_choices ? (int)st->dialogue.choice_count : 0;
    int bx = 8, bw = w - 16;
    int est_text_w = (st->dialogue.portrait_id >= 1 && st->dialogue.portrait_id <= CINEMA_SDL_PORTRAIT_COUNT)
                   ? (bw - 54) : (bw - 43);
    int text_lines = count_wrapped_lines(est_text_w, st->dialogue.full_text, st->dialogue.full_len);
    int text_h = 18 + text_lines * 10;
    if (text_h < 48) text_h = 48;
    int bh = text_h + choice_rows * 13;
    if (bh > h - 26) bh = h - 26;
    int by = h - bh - 6;

    fill_rect_alpha(bx, by, bw, bh, 0xFF171423u, 240);
    draw_rect_outline(bx, by, bw, bh, 0xFFE8B852u);
    draw_rect_outline(bx + 1, by + 1, bw - 2, bh - 2, 0xFF544324u);

    /* Speaker Nameplate */
    if (st->dialogue.speaker && st->dialogue.speaker[0] != '\0') {
        int nw = (int)strlen(st->dialogue.speaker) * 6 + 10;
        fill_rect_px(bx + 6, by - 11, nw, 12, 0xFF28203Cu);
        draw_rect_outline(bx + 6, by - 11, nw, 12, 0xFFE8B852u);
        draw_text(bx + 11, by - 8, st->dialogue.speaker, 0xFFFFF1A8u, true);
    }

    /* Painted 40x40 Character Bust Portrait (Chapter 1) or Evertale Pixel Portrait Fallback */
    int text_x = bx + 8;
    int text_w = bw - 16;
    if (st->dialogue.portrait_id >= 1 && st->dialogue.portrait_id <= CINEMA_SDL_PORTRAIT_COUNT) {
        const uint16_t *psrc = CINEMA_SDL_PORTRAITS[st->dialogue.portrait_id - 1];
        int px = bx + 6;
        int py = by + 6;
        int psize = 34;
        fill_rect_px(px - 1, py - 1, psize + 2, psize + 2, 0xFF1F192Eu);
        draw_rect_outline(px - 1, py - 1, psize + 2, psize + 2, 0xFFE8B852u);
        for (int dy = 0; dy < psize; dy++) {
            int sy = (dy * CINEMA_SDL_PORT_H) / psize;
            const uint16_t *srow = &psrc[sy * CINEMA_SDL_PORT_W];
            for (int dx = 0; dx < psize; dx++) {
                int sx = (dx * CINEMA_SDL_PORT_W) / psize;
                put_px(px + dx, py + dy, rgb565_to_argb(srow[sx]));
            }
        }
        text_x = bx + 46;
        text_w = bw - 54;
    } else {
        const char *spk = st->dialogue.speaker;
        int portrait_id = 0;
        if (spk && spk[0] != '\0') {
            if (strstr(spk, "Rowan")) portrait_id = 1;
            else if (strstr(spk, "Pip")) portrait_id = 2;
            else if (strstr(spk, "Elder") || strstr(spk, "Wren")) portrait_id = 3;
            else if (strstr(spk, "Clara")) portrait_id = 4;
            else if (strstr(spk, "Guard") || strstr(spk, "Bram")) portrait_id = 5;
        }
        if (portrait_id != 0) {
            int px = bx + 6;
            int py = by + 7;
            fill_rect_px(px, py, 24, 24, 0xFF231D36u);
            draw_rect_outline(px, py, 24, 24, 0xFF8B6A36u);
            int cx = px + 12;
            int cy = py + 21;
            switch (portrait_id) {
                case 1:
                    draw_pixel_art(HERO_BODY, 7, cx, cy, 8, 9, 1, false, false, FP_ONE, FP_ONE);
                    draw_pixel_art(HERO_HEAD_DOWN, 7, cx, cy, 8, 16, 1, false, false, FP_ONE, FP_ONE);
                    break;
                case 2:
                    draw_pixel_art(PIP_FRAME0, 16, cx, cy - 1, 8, 16, 1, false, false, FP_ONE, FP_ONE);
                    break;
                case 3:
                    draw_pixel_art(NPC_ELDER_ART, 12, cx, cy - 2, 8, 12, 2, false, false, FP_ONE, FP_ONE);
                    break;
                case 4:
                    draw_pixel_art(NPC_CLARA_ART, 12, cx, cy - 2, 8, 12, 2, false, false, FP_ONE, FP_ONE);
                    break;
                case 5:
                    draw_pixel_art(NPC_GUARD_ART, 12, cx, cy - 2, 8, 12, 2, false, false, FP_ONE, FP_ONE);
                    break;
                default:
                    break;
            }
            text_x = bx + 35;
            text_w = bw - 43;
        }
    }

    draw_text_wrapped(text_x, by + 7, text_w, st->dialogue.full_text, st->dialogue.visible_chars, 0xFFF4EEDCu);

    if (st->dialogue.finished) {
        if (show_choices) {
            /* Stack choices vertically (full-width rows) so long labels NEVER overlap! */
            int cy = by + bh - choice_rows * 13 - 4;
            for (uint8_t i = 0; i < st->dialogue.choice_count; i++) {
                bool sel = (st->dialogue.selected_choice == i);
                int row_y = cy + (int)i * 13;
                if (sel) {
                    fill_rect_px(bx + 6, row_y - 2, bw - 12, 11, 0xFF33294Eu);
                    draw_rect_outline(bx + 6, row_y - 2, bw - 12, 11, 0xFFF5B841u);
                    draw_text(bx + 10, row_y, ">", 0xFFFFF1A8u, true);
                }
                draw_text(bx + 18, row_y, st->dialogue.choices[i].label, sel ? 0xFFFFF1A8u : 0xFFB8B2C4u, true);
            }
        } else if ((st->frame_count >> 4) & 1u) {
            const char *cue = (st->dialogue.next_text != NULL) ? "[A] MORE" : "[A]";
            int cw = (int)strlen(cue) * 6 + 6;
            draw_text(bx + bw - cw, by + bh - 11, cue, 0xFFF5B841u, true);
        }
    }
}

static void render_shop_overlay(const HearthState *st) {
    if (st->mode != MODE_SHOP || !st->shop.active) return;
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    int bx = 14, by = 16, bw = w - 28, bh = h - 32;
    char buf[64];

    fill_rect_alpha(bx, by, bw, bh, 0xFF161322u, 242);
    draw_rect_outline(bx, by, bw, bh, 0xFFE8B852u);
    draw_rect_outline(bx + 1, by + 1, bw - 2, bh - 2, 0xFF5C4A28u);

    draw_text(bx + 8, by + 7, st->shop.title ? st->shop.title : "EVERTALE SHOP", 0xFFFFF1A8u, true);
    snprintf(buf, sizeof(buf), "%uG", (unsigned)st->guilders);
    draw_text(bx + bw - (int)strlen(buf) * 6 - 8, by + 7, buf, 0xFFF5B841u, true);

    /* Tabs: BUY / SELL FISH */
    int tw = (bw - 20) / 2;
    fill_rect_px(bx + 8, by + 19, tw, 12, st->shop.tab == 0 ? 0xFF3A2E58u : 0xFF211C31u);
    draw_rect_outline(bx + 8, by + 19, tw, 12, st->shop.tab == 0 ? 0xFFFFF1A8u : 0xFF4A4262u);
    draw_text_centered(bx + 8 + tw / 2, by + 22, "BUY SUPPLIES", st->shop.tab == 0 ? 0xFFFFF1A8u : 0xFFB0AAB8u, false);

    fill_rect_px(bx + 12 + tw, by + 19, tw, 12, st->shop.tab == 1 ? 0xFF3A2E58u : 0xFF211C31u);
    draw_rect_outline(bx + 12 + tw, by + 19, tw, 12, st->shop.tab == 1 ? 0xFFFFF1A8u : 0xFF4A4262u);
    draw_text_centered(bx + 12 + tw + tw / 2, by + 22, "SELL FISH", st->shop.tab == 1 ? 0xFFFFF1A8u : 0xFFB0AAB8u, false);

    int rows = (st->shop.tab == 0) ? HEARTH_SHOP_ITEM_COUNT : HEARTH_FISH_SPECIES;
    for (int i = 0; i < rows; i++) {
        int ry = by + 36 + i * 16;
        bool sel = (st->shop.cursor == (uint8_t)i);
        if (sel) {
            fill_rect_px(bx + 8, ry - 2, bw - 16, 14, 0xFF2D2548u);
            draw_rect_outline(bx + 8, ry - 2, bw - 16, 14, 0xFFF5B841u);
            draw_text(bx + 11, ry + 1, ">", 0xFFFFF1A8u, false);
        }
        if (st->shop.tab == 0) {
            const HearthShopItem *it = &HEARTH_SHOP_ITEMS[i];
            draw_text(bx + 20, ry + 1, it->name, sel ? 0xFFFFF1A8u : 0xFFE6E0D4u, false);
            snprintf(buf, sizeof(buf), "%uG", (unsigned)it->price);
            draw_text(bx + bw - (int)strlen(buf) * 6 - 12, ry + 1, buf, 0xFFF5B841u, false);
        } else {
            const HearthFishInfo *fi = &HEARTH_FISH_TABLE[i];
            uint8_t qty = st->fish_bag[i];
            snprintf(buf, sizeof(buf), "%.14s X%u", fi->name, (unsigned)qty);
            draw_text(bx + 20, ry + 1, buf, qty > 0 ? (sel ? 0xFFFFF1A8u : 0xFFA8D678u) : 0xFF706B7Cu, false);
            snprintf(buf, sizeof(buf), "+%uG", (unsigned)fi->sell_price);
            draw_text(bx + bw - (int)strlen(buf) * 6 - 12, ry + 1, buf, qty > 0 ? 0xFFF5B841u : 0xFF605B6Cu, false);
        }
    }

    /* Footer feedback / item description */
    int fy = by + bh - 32;
    fill_rect_px(bx + 8, fy, bw - 16, 14, 0xFF221D34u);
    if (st->shop.msg_timer > 0 && st->shop.msg[0] != '\0') {
        draw_text_centered(w / 2, fy + 4, st->shop.msg, 0xFFA8D678u, false);
    } else if (st->shop.tab == 0 && st->shop.cursor < HEARTH_SHOP_ITEM_COUNT) {
        draw_text_centered(w / 2, fy + 4, HEARTH_SHOP_ITEMS[st->shop.cursor].desc, 0xFF9AD8F4u, false);
    } else {
        draw_text_centered(w / 2, fy + 4, "SELL CAUGHT FISH FOR GUILDERS", 0xFF9AD8F4u, false);
    }
    draw_text_centered(w / 2, by + bh - 14, "[A/D] TAB  [W/S] PICK  [SPACE] DEAL  [X] EXIT", 0xFFD0C4A8u, false);
}

static void render_fishing_overlay(const HearthState *st, const HearthPpuPacket *ppu) {
    if (st->mode != MODE_FISHING || !st->fishing.active) return;
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    const HearthFishingState *f = &st->fishing;
    char buf[64];

    /* Fishing line from Rowan to bobber */
    int hx = FROM_FP(st->hero_fx) - ppu->cam_x + ppu->shake_x;
    int hy = FROM_FP(st->hero_fy) - ppu->cam_y - 10 + ppu->shake_y;
    int bx = f->bobber_x - ppu->cam_x + ppu->shake_x;
    int by = f->bobber_y - ppu->cam_y + ppu->shake_y;
    draw_line_px(hx, hy, bx, by, 0xFFE6F0F8u);

    int gy = h - 54;
    int gw = w - 28;
    int gx = 14;
    fill_rect_alpha(gx, gy, gw, 46, 0xFF141826u, 232);
    draw_rect_outline(gx, gy, gw, 46, 0xFF5AD2F4u);

    if (f->phase == FISH_PHASE_CAST || f->phase == FISH_PHASE_WAIT_BITE) {
        draw_text_centered(w / 2, gy + 10, "WATCH THE BOBBER...", 0xFF9AD8F4u, true);
        draw_text_centered(w / 2, gy + 26, "WAIT FOR THE SPLASH  [X] CANCEL", 0xFFD0C4A8u, false);
    } else if (f->phase == FISH_PHASE_BITE_ALERT) {
        draw_text_centered(w / 2, gy + 10, "!! BITE! PRESS [SPACE/A] NOW !!", 0xFFFFF1A8u, true);
        draw_text_centered(w / 2, gy + 26, "HOOK IT BEFORE IT SLIPS!", 0xFFF5B841u, false);
    } else if (f->phase == FISH_PHASE_REELING) {
        /* Sea of Stars / Zelda Timed-Reel Sweet-Spot Gauge */
        int track_x = gx + 10;
        int track_y = gy + 8;
        int track_w = gw - 20;
        fill_rect_px(track_x, track_y, track_w, 10, 0xFF1F2B3Eu);
        draw_rect_outline(track_x, track_y, track_w, 10, 0xFF5A7898u);

        int b0 = f->bar_pos - f->bar_half_w;
        int b1 = f->bar_pos + f->bar_half_w;
        if (b0 < 0) b0 = 0;
        if (b1 > 100) b1 = 100;
        int sx0 = track_x + (b0 * track_w) / 100;
        int sw  = ((b1 - b0) * track_w) / 100;
        if (sw < 4) sw = 4;
        fill_rect_px(sx0, track_y + 1, sw, 8, 0xFF48B878u);

        int fx = track_x + (f->fish_pos * (track_w - 4)) / 100;
        fill_rect_px(fx, track_y - 1, 5, 12, 0xFFFFF1A8u);
        draw_rect_outline(fx, track_y - 1, 5, 12, 0xFF18141Cu);

        /* Progress & Tension bars */
        int half_w = (track_w - 10) / 2;
        draw_text(track_x, gy + 22, "REEL", 0xFFA8D678u, false);
        fill_rect_px(track_x + 28, gy + 23, half_w - 28, 5, 0xFF202C24u);
        fill_rect_px(track_x + 28, gy + 23, ((half_w - 28) * f->progress) / 100, 5, 0xFF74D680u);

        int tx = track_x + half_w + 10;
        draw_text(tx, gy + 22, "LINE", f->tension > 70 ? 0xFFFF6464u : 0xFFF5B841u, false);
        fill_rect_px(tx + 28, gy + 23, half_w - 28, 5, 0xFF341C24u);
        fill_rect_px(tx + 28, gy + 23, ((half_w - 28) * f->tension) / 100, 5, f->tension > 70 ? 0xFFFF5555u : 0xFFF5B841u);

        draw_text_centered(w / 2, gy + 34, "HOLD [SPACE] TO PULL RIGHT  RELEASE TO DRIFT LEFT", 0xFF9AD8F4u, false);
    } else if (f->phase == FISH_PHASE_CAUGHT) {
        const HearthFishInfo *fi = &HEARTH_FISH_TABLE[f->species % HEARTH_FISH_SPECIES];
        snprintf(buf, sizeof(buf), "CAUGHT %s! (%u OZ)", fi->name, (unsigned)f->weight_oz);
        draw_text_centered(w / 2, gy + 10, buf, 0xFFFFF1A8u, true);
        snprintf(buf, sizeof(buf), "SELLS FOR %uG IN TOWN  [SPACE] OK", (unsigned)fi->sell_price);
        draw_text_centered(w / 2, gy + 26, buf, 0xFFA8D678u, false);
    } else {
        draw_text_centered(w / 2, gy + 12, "THE LINE SNAPPED! IT GOT AWAY...", 0xFFFF8888u, true);
        draw_text_centered(w / 2, gy + 28, "[SPACE] CONTINUE", 0xFFD0C4A8u, false);
    }
}

static void render_mountain_vista(const HearthState *st, const HearthPpuPacket *ppu, int cam_x) {
    int w = (int)g_ppu_w;
    int vista_h = 96;
    uint8_t tod = ppu->time_of_day;
    uint32_t sky_top, sky_mid, sky_hor, far_mtn, snow_cap, near_ridge;

    if (tod == TOD_MORNING) {
        sky_top = 0xFF5A7CA8u; sky_mid = 0xFF9EB4CEu; sky_hor = 0xFFF2C8A0u;
        far_mtn = 0xFF586884u; snow_cap = 0xFFF6EDE6u; near_ridge = 0xFF6A4842u;
    } else if (tod == TOD_DUSK) {
        sky_top = 0xFF2E2A52u; sky_mid = 0xFF8E4A5Cu; sky_hor = 0xFFF29C58u;
        far_mtn = 0xFF423254u; snow_cap = 0xFFF8D0B4u; near_ridge = 0xFF4C2A32u;
    } else if (tod == TOD_NIGHT) {
        sky_top = 0xFF0E1228u; sky_mid = 0xFF1A2246u; sky_hor = 0xFF2C3A64u;
        far_mtn = 0xFF202A48u; snow_cap = 0xFFC4D4EEu; near_ridge = 0xFF161E34u;
    } else {
        sky_top = 0xFF4882B8u; sky_mid = 0xFF82B2D8u; sky_hor = 0xFFDCE6ECu;
        far_mtn = 0xFF566E8Cu; snow_cap = 0xFFF4F6FAu; near_ridge = 0xFF584C3Cu;
    }

    for (int y = 0; y < vista_h; y++) {
        uint32_t c = (y < 28) ? sky_top : (y < 58) ? sky_mid : sky_hor;
        fill_rect_px(0, y, w, 1, c);
    }

    /* Stars & Crescent Moon at Night, or Drifting High Clouds by Day */
    if (tod == TOD_NIGHT) {
        for (int i = 0; i < 24; i++) {
            int sx = (i * 41 + 13) % w;
            int sy = 4 + ((i * 19) % 44);
            uint32_t sc = (((ppu->frame_count >> 4) + (uint32_t)i) & 3u) == 0u ? 0xFFFFFFFFu : 0xFFAEC4EEu;
            put_px(sx, sy, sc);
        }
        fill_ellipse_alpha(w - 48, 22, 11, 11, 0xFF9AC0F8u, 50);
        fill_ellipse_alpha(w - 48, 22, 7, 7, 0xFFF6F2DCu, 255);
        fill_ellipse_alpha(w - 45, 20, 6, 6, sky_top, 255);
    } else {
        int drift = (int)((ppu->frame_count >> 3) & 511u);
        for (int i = 0; i < 4; i++) {
            int cx = ((i * 96 + drift - (cam_x >> 3)) % (w + 64)) - 32;
            int cy = 12 + (i & 1) * 14;
            fill_ellipse_alpha(cx, cy, 20, 5, (tod == TOD_DUSK) ? 0xFFF4B484u : 0xFFF8F4ECu, 150);
            fill_ellipse_alpha(cx + 12, cy - 2, 12, 4, (tod == TOD_DUSK) ? 0xFFF8CCA0u : 0xFFFFFFFFu, 175);
        }
    }

    /* Far Parallax Snow-Dusted Alpine Peaks (0.25x scroll) */
    int p1 = cam_x >> 2;
    for (int x = 0; x < w; x++) {
        int wx = x + p1;
        int peak = 46 - abs(((wx % 88) + 88) % 88 - 44) * 3 / 5
                      - abs((((wx + 37) % 120) + 120) % 120 - 60) / 3;
        if (peak < 14) peak = 14;
        fill_rect_px(x, peak, 1, vista_h - peak, far_mtn);
        if (peak < 32) {
            int snow_h = (34 - peak) / 2;
            if (snow_h > 10) snow_h = 10;
            fill_rect_px(x, peak, 1, snow_h, snow_cap);
        }
    }

    /* Castle Aurelia Distant Ivory Spires above Sunhill Market & Castle Gate */
    if (st->zone_id == ZONE_SUNHILL_MARKET || st->zone_id == ZONE_CASTLE_GATE) {
        int cx = w / 2 - (cam_x >> 3) + 18;
        int cy = 44;
        fill_rect_px(cx - 26, cy, 52, 28, 0xFFD8D0C2u);
        fill_rect_px(cx - 24, cy + 2, 48, 26, 0xFFEDE6D8u);
        fill_rect_px(cx - 28, cy - 12, 10, 40, 0xFFE2DACCu);
        fill_rect_px(cx + 18, cy - 12, 10, 40, 0xFFE2DACCu);
        fill_rect_px(cx - 9, cy - 22, 18, 50, 0xFFF4EFE2u);
        fill_rect_px(cx - 29, cy - 18, 12, 6, 0xFF3E68A6u);
        fill_rect_px(cx + 17, cy - 18, 12, 6, 0xFF3E68A6u);
        fill_rect_px(cx - 10, cy - 30, 20, 8, 0xFF2D5594u);
        fill_rect_px(cx - 1, cy - 36, 2, 6, 0xFFF5B841u);
        fill_rect_px(cx + 1, cy - 35, 5, 3, 0xFFE04C4Cu);
    }

    /* Mid Parallax Autumn Maple & Pine Foothill Ridge (0.45x scroll) */
    int p2 = (cam_x * 9) >> 4;
    for (int x = 0; x < w; x++) {
        int wx = x + p2;
        int ridge = 66 + (HEARTH_SIN64[(wx >> 2) & 63] >> 4) + (HEARTH_SIN64[((wx >> 3) + 17) & 63] >> 5);
        if (ridge < 46) ridge = 46;
        if (ridge < vista_h) {
            fill_rect_px(x, ridge, 1, vista_h - ridge, near_ridge);
            if (((wx >> 2) & 3) == 0 && ridge + 2 < vista_h) {
                put_px(x, ridge + 1, (tod == TOD_NIGHT) ? 0xFF283A52u : 0xFFC85838u);
            }
        }
    }
}

/* Signed shortest toroidal delta across a wrapped world axis */
static int wrap_delta(int a, int b, int period) {
    int d = a - b;
    int half = period / 2;
    while (d >  half) d -= period;
    while (d < -half) d += period;
    return d;
}

/* Grand Multi-Tile Storybook Landmarks across all 7 Continents of the 256x192 Evertale Globe */
static void render_sdl_world_landmarks(int cam_x, int cam_y, uint32_t frame) {
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    int gw_px = HEARTH_GLOBE_W * HEARTH_TILE_SIZE;
    int gh_px = HEARTH_GLOBE_H * HEARTH_TILE_SIZE;
    int flutter = ((frame >> 4) & 1u) ? 1 : 0;

    for (uint8_t idx = 0; idx < HEARTH_GLOBE_LANDMARK_COUNT; idx++) {
        const HearthGlobeLandmark *lm = &HEARTH_GLOBE_LANDMARKS[idx];
        int sx = w / 2 + wrap_delta((int)lm->tx * 16 + 8, cam_x + w / 2, gw_px);
        int sy = h / 2 + wrap_delta((int)lm->ty * 16 + 8, cam_y + h / 2, gh_px);
        if (sx < -56 || sx > w + 56 || sy < -56 || sy > h + 56) continue;

        switch (idx) {
            case 0: /* Hollow Oak Gate */
                fill_ellipse_alpha(sx, sy + 6, 22, 7, 0xFF142214u, 90);
                fill_rect_px(sx - 9, sy - 14, 18, 22, 0xFF6B4228u);
                fill_rect_px(sx - 6, sy - 12, 12, 18, 0xFF825434u);
                fill_ellipse_alpha(sx, sy - 2, 6, 8, 0xFF5AD2F4u, 150);
                fill_ellipse_alpha(sx, sy - 2, 4, 6, 0xFFFFF1A8u, 235);
                fill_ellipse_alpha(sx, sy - 20, 22, 11, 0xFF2E6B38u, 255);
                fill_ellipse_alpha(sx - 8, sy - 24, 14, 8, 0xFF4E924Au, 255);
                fill_ellipse_alpha(sx + 8, sy - 23, 13, 8, 0xFF6AB258u, 255);
                break;
            case 1: { /* Windmill Hamlet */
                fill_ellipse_alpha(sx, sy + 8, 26, 8, 0xFF142214u, 85);
                fill_rect_px(sx - 20, sy - 4, 14, 12, 0xFFF2E6CCu);
                fill_rect_px(sx - 22, sy - 10, 18, 6, 0xFFC24C3Cu);
                fill_rect_px(sx + 6, sy - 2, 14, 10, 0xFFF2E6CCu);
                fill_rect_px(sx + 4, sy - 8, 18, 6, 0xFFC24C3Cu);
                fill_rect_px(sx - 5, sy - 20, 12, 26, 0xFFECE2CEu);
                fill_rect_px(sx - 7, sy - 26, 16, 7, 0xFF9E3A32u);
                fill_rect_px(sx - 1, sy - 3, 4, 9, 0xFF5A3824u);
                int ang = (int)((frame >> 1) & 63u);
                for (int arm = 0; arm < 4; arm++) {
                    int a = (ang + arm * 16) & 63;
                    int ex = sx + 1 + (HEARTH_SIN64[(a + 16) & 63] * 14) / 127;
                    int ey = sy - 18 + (HEARTH_SIN64[a] * 14) / 127;
                    draw_line_px(sx + 1, sy - 18, ex, ey, 0xFFFFF6E0u);
                }
                break;
            }
            case 2: /* Sunhill Market */
                fill_ellipse_alpha(sx, sy + 10, 34, 10, 0xFF182218u, 95);
                fill_rect_px(sx - 30, sy - 2, 60, 12, 0xFFC8BCA4u);
                fill_rect_px(sx - 26, sy - 14, 14, 14, 0xFFF6ECD8u);
                fill_rect_px(sx - 28, sy - 20, 18, 6, 0xFFD25A3Cu);
                fill_rect_px(sx + 12, sy - 14, 14, 14, 0xFFF6ECD8u);
                fill_rect_px(sx + 10, sy - 20, 18, 6, 0xFFD25A3Cu);
                fill_rect_px(sx - 10, sy - 20, 20, 20, 0xFFFAF2E0u);
                fill_rect_px(sx - 12, sy - 28, 24, 8, 0xFFE29C38u);
                fill_rect_px(sx - 3, sy - 8, 6, 8, 0xFF5A3824u);
                fill_rect_px(sx - 20, sy + 1, 10, 5, 0xFFE05248u);
                fill_rect_px(sx + 10, sy + 1, 10, 5, 0xFF3E84B8u);
                break;
            case 3: /* Castle Aurelia */
                fill_ellipse_alpha(sx, sy + 12, 42, 11, 0xFF141824u, 105);
                fill_rect_px(sx - 36, sy - 10, 72, 22, 0xFFE2DAC8u);
                fill_rect_px(sx - 38, sy - 28, 14, 40, 0xFFEEE8D8u);
                fill_rect_px(sx + 24, sy - 28, 14, 40, 0xFFEEE8D8u);
                fill_rect_px(sx - 14, sy - 38, 28, 50, 0xFFF8F4E8u);
                fill_rect_px(sx - 40, sy - 36, 18, 8, 0xFF3462A4u);
                fill_rect_px(sx + 22, sy - 36, 18, 8, 0xFF3462A4u);
                fill_rect_px(sx - 16, sy - 48, 32, 10, 0xFF285292u);
                fill_rect_px(sx - 6, sy - 2, 12, 14, 0xFF3A2820u);
                fill_rect_px(sx - 1, sy - 56, 2, 8, 0xFFF5B841u);
                fill_rect_px(sx + 1, sy - 55 + flutter, 8, 4, 0xFFE04448u);
                break;
            case 4: /* Bramblewood Crossing */
                fill_ellipse_alpha(sx, sy + 8, 28, 8, 0xFF102014u, 95);
                for (int k = -1; k <= 1; k++) {
                    int btx = sx + k * 14;
                    int bty = sy + ((k == 0) ? -4 : 2);
                    fill_rect_px(btx - 3, bty - 4, 6, 12, 0xFF5C3A24u);
                    fill_ellipse_alpha(btx, bty - 14, 13, 11, 0xFF245830u, 255);
                    fill_ellipse_alpha(btx - 2, bty - 17, 9, 7, 0xFF428444u, 255);
                }
                break;
            case 5: /* Sunken Watchtower */
                fill_ellipse_alpha(sx, sy + 8, 24, 7, 0xFF141820u, 90);
                fill_rect_px(sx - 18, sy - 10, 8, 18, 0xFF8C90A0u);
                fill_rect_px(sx + 10, sy - 14, 8, 22, 0xFF7C8090u);
                fill_rect_px(sx - 8, sy - 26, 16, 34, 0xFF9EA2B2u);
                fill_rect_px(sx - 6, sy - 24, 12, 4, 0xFF4C7C46u);
                fill_rect_px(sx - 3, sy - 8, 6, 10, 0xFF282438u);
                fill_ellipse_alpha(sx, sy - 16, 4, 4, 0xFF5AD2F4u, 150);
                break;
            case 6: /* Whiskerburgh */
                fill_ellipse_alpha(sx, sy + 8, 28, 8, 0xFF14222Au, 95);
                fill_rect_px(sx - 24, sy - 6, 16, 14, 0xFFF2E6D0u);
                fill_rect_px(sx - 26, sy - 12, 20, 6, 0xFF3E689Au);
                fill_rect_px(sx - 6, sy - 14, 18, 22, 0xFFF8EED8u);
                fill_rect_px(sx - 8, sy - 20, 22, 6, 0xFFB8443Eu);
                fill_rect_px(sx + 14, sy - 18, 8, 26, 0xFFEAE4D8u);
                fill_rect_px(sx + 15, sy - 22, 6, 4, 0xFFFFE478u);
                break;
            case 7: /* Heartland Timber Pier */
                fill_rect_px(sx - 22, sy - 2, 44, 12, 0xFF7C4E2Cu);
                fill_rect_px(sx - 20, sy, 40, 8, 0xFFA66E42u);
                fill_rect_px(sx - 16, sy - 18, 16, 16, 0xFFE8D8B8u);
                fill_rect_px(sx - 18, sy - 23, 20, 6, 0xFF3E6E9Cu);
                fill_rect_px(sx + 8, sy - 12, 12, 10, 0xFFB07242u);
                fill_rect_px(sx + 13, sy - 24, 2, 14, 0xFF6E4424u);
                fill_rect_px(sx + 15, sy - 23 + flutter, 7, 4, 0xFFF5C45Au);
                break;
            case 8: /* Suncrest Coral Cove */
                fill_ellipse_alpha(sx, sy + 6, 26, 8, 0xFF287C98u, 110);
                fill_rect_px(sx - 20, sy - 8, 16, 12, 0xFFF6E6C4u);
                fill_rect_px(sx - 22, sy - 15, 20, 7, 0xFFE89C44u);
                fill_rect_px(sx + 4, sy - 10, 16, 14, 0xFFF6E6C4u);
                fill_rect_px(sx + 2, sy - 17, 20, 7, 0xFF42B8A8u);
                fill_rect_px(sx - 3, sy - 26, 4, 18, 0xFF8C5832u);
                fill_ellipse_alpha(sx - 1, sy - 28, 12, 5, 0xFF3E9C52u, 255);
                fill_rect_px(sx + 10, sy - 24 + flutter, 7, 4, 0xFF20222Cu);
                break;
            case 9: /* Oasis of Babana */
                fill_ellipse_alpha(sx, sy + 6, 30, 9, 0xFF9C7238u, 90);
                fill_ellipse_alpha(sx, sy + 2, 18, 7, 0xFF44BCD4u, 235);
                fill_rect_px(sx - 22, sy - 14, 16, 16, 0xFFF2DFB4u);
                fill_rect_px(sx - 20, sy - 22, 12, 8, 0xFFE8B43Cu);
                fill_rect_px(sx + 6, sy - 12, 16, 14, 0xFFEED4A2u);
                fill_rect_px(sx + 4, sy - 17, 20, 5, 0xFFE4503Cu);
                fill_rect_px(sx + 10, sy - 10, 8, 3, 0xFFFFD838u); /* Golden Babana pyramid! */
                break;
            case 10: /* Frostmere Noel Village */
                fill_ellipse_alpha(sx, sy + 8, 28, 8, 0xFF80A4C8u, 95);
                fill_rect_px(sx - 22, sy - 8, 16, 14, 0xFF7C4E34u);
                fill_rect_px(sx - 24, sy - 15, 20, 7, 0xFFF4F8FFu);
                fill_rect_px(sx + 6, sy - 8, 16, 14, 0xFF7C4E34u);
                fill_rect_px(sx + 4, sy - 15, 20, 7, 0xFFF4F8FFu);
                /* Festive Evergreen Tree with Star */
                fill_rect_px(sx - 2, sy - 4, 4, 10, 0xFF5C3A24u);
                fill_rect_px(sx - 9, sy - 12, 18, 8, 0xFF266E44u);
                fill_rect_px(sx - 6, sy - 20, 12, 8, 0xFF348854u);
                fill_rect_px(sx - 3, sy - 26, 6, 6, 0xFF48A464u);
                fill_rect_px(sx - 2, sy - 30, 4, 4, 0xFFFFE460u);
                put_px(sx - 4, sy - 10, 0xFFE84048u);
                put_px(sx + 4, sy - 16, 0xFF5AD2F4u);
                break;
            case 11: /* Canopy Treetop Village */
                fill_ellipse_alpha(sx, sy + 8, 30, 8, 0xFF102818u, 110);
                fill_rect_px(sx - 18, sy - 18, 8, 24, 0xFF6E4424u);
                fill_rect_px(sx + 10, sy - 18, 8, 24, 0xFF6E4424u);
                fill_rect_px(sx - 22, sy - 18, 44, 6, 0xFFA67242u); /* Elevated wooden platform */
                fill_rect_px(sx - 16, sy - 28, 14, 10, 0xFFE6D4A8u);
                fill_rect_px(sx + 2, sy - 28, 14, 10, 0xFFE6D4A8u);
                fill_ellipse_alpha(sx, sy - 32, 26, 8, 0xFF2E8442u, 255);
                break;
            case 12: /* Cinderpeak Caldera Gate */
                fill_ellipse_alpha(sx, sy + 8, 32, 9, 0xFF281010u, 115);
                fill_rect_px(sx - 26, sy - 10, 52, 18, 0xFF44363Au);
                fill_rect_px(sx - 18, sy - 24, 36, 16, 0xFF5A4448u);
                fill_rect_px(sx - 10, sy - 34, 20, 12, 0xFF6E4E52u);
                fill_rect_px(sx - 6, sy - 34, 12, 5, 0xFFFF7828u);
                fill_rect_px(sx - 3, sy - 32 + flutter, 6, 4, 0xFFFFE058u);
                fill_rect_px(sx - 6, sy - 4, 12, 12, 0xFF201014u);
                break;
            case 13: /* Aetheris Citadel Spire */
                fill_ellipse_alpha(sx, sy + 10, 32, 9, 0xFF101C2Cu, 120);
                fill_rect_px(sx - 24, sy - 12, 48, 22, 0xFF344864u);
                fill_rect_px(sx - 14, sy - 32, 28, 42, 0xFF6482A8u);
                fill_rect_px(sx - 6, sy - 48, 12, 56, 0xFF9CB8D8u);
                fill_rect_px(sx - 2, sy - 46, 4, 44, 0xFF4AF2FFu);
                fill_ellipse_alpha(sx, sy - 50, 7 + flutter, 7 + flutter, 0xFF4AF2FFu, 150);
                fill_rect_px(sx - 2, sy - 52, 4, 4, 0xFFFFFFFFu);
                break;
            default:
                break;
        }
    }
}

static void render_sdl_world_banners(const HearthState *st, int cam_x, int cam_y) {
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    int gw_px = HEARTH_GLOBE_W * HEARTH_TILE_SIZE;
    int gh_px = HEARTH_GLOBE_H * HEARTH_TILE_SIZE;
    int hx = FROM_FP(st->hero_fx);
    int hy = FROM_FP(st->hero_fy);
    for (uint8_t i = 0; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
        const HearthGlobeLandmark *lm = &HEARTH_GLOBE_LANDMARKS[i];
        int lx = (int)lm->tx * 16 + 8;
        int ly = (int)lm->ty * 16 + 8;
        int dx = wrap_delta(hx, lx, gw_px);
        int dy = wrap_delta(hy, ly, gh_px);
        if (dx * dx + dy * dy < 92 * 92) {
            int sx = w / 2 + wrap_delta(lx, cam_x + w / 2, gw_px);
            int sy = h / 2 + wrap_delta(ly, cam_y + h / 2, gh_px) - 34;
            int bw = (int)strlen(lm->name) * 6 + 10;
            fill_rect_alpha(sx - bw / 2, sy - 5, bw, 11, 0xFF141120u, 215);
            draw_rect_outline(sx - bw / 2, sy - 5, bw, 11, 0xFFE8B852u);
            draw_text_centered(sx, sy - 3, lm->name, 0xFFFFF1A8u, false);
        }
    }
}

/* World Atlas Minimap Overlay (64x48 Toroidal Macro-Biome Map + 14 Landmark Pins) */
static void render_world_atlas_overlay(const HearthState *st) {
    static const uint32_t ATLAS_COLORS[10] = {
        0xFF1F4C82u, /* 0: Deep Ocean */
        0xFF3EB4CCu, /* 1: Turquoise Shallows (Raftable) */
        0xFFE87EA8u, /* 2: Coral Reef Shoals (Raftable) */
        0xFF5EA84Eu, /* 3: Emerald Heartland */
        0xFFE4BC62u, /* 4: Sands of Babana */
        0xFF56C49Au, /* 5: Suncrest Paradise */
        0xFFE6F0FCu, /* 6: Frostmere North */
        0xFF226E3Au, /* 7: Canopy Wilds */
        0xFFD44824u, /* 8: Cinderpeak Caldera */
        0xFF4898C8u  /* 9: Aetheris Citadel */
    };
    static const char *VEH_LABELS[6] = {
        "ON FOOT", "CEDAR RAFT", "SHIP", "BALLOON", "AIRSHIP", "SPACESHIP"
    };
    static const uint16_t WARP_RX[REGION_COUNT] = { 104u, 132u,  52u, 128u, 212u,  32u, 220u };
    static const uint16_t WARP_RY[REGION_COUNT] = {  84u, 155u, 147u,  22u, 126u,  62u,  62u };
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    int scale = (w >= 300) ? 3 : 2;
    int map_w = HEARTH_ATLAS_W * scale;
    int map_h = HEARTH_ATLAS_H * scale;
#if HEARTH_DESIGNER_MODE
    bool show_designer = st->designer_sandbox;
#else
    bool show_designer = false;
#endif
    int extra_h = show_designer ? 54 : 44;
    int bx = (w - (map_w + 24)) / 2;
    int by = (h - (map_h + extra_h)) / 2;
    int bw = map_w + 24;
    int bh = map_h + extra_h;
    int mx0 = bx + 12;
    int my0 = by + 18;

    fill_rect_alpha(0, 0, w, h, 0xFF0A0C16u, 185);
    fill_rect_alpha(bx, by, bw, bh, 0xFF161322u, 245);
    draw_rect_outline(bx, by, bw, bh, 0xFFE8B852u);
    draw_rect_outline(bx + 1, by + 1, bw - 2, bh - 2, 0xFF5C4A28u);

    draw_text(bx + 8, by + 5, "EVERTALE WORLD ATLAS", 0xFFFFF1A8u, true);
    const char *reg = (st->world_region_id < REGION_COUNT) ? HEARTH_REGION_NAMES[st->world_region_id] : "EVERTALE";
    draw_text(bx + bw - (int)strlen(reg) * 6 - 8, by + 5, reg, 0xFF9AD8F4u, false);

    /* 64x48 ROM Biome Grid */
    for (uint8_t ay = 0; ay < HEARTH_ATLAS_H; ay++) {
        for (uint8_t ax = 0; ax < HEARTH_ATLAS_W; ax++) {
            uint8_t cell = hearth_globe_atlas_cell(ax, ay);
            uint32_t c = ATLAS_COLORS[(cell < 10u) ? cell : 0u];
            fill_rect_px(mx0 + (int)ax * scale, my0 + (int)ay * scale, scale, scale, c);
        }
    }
    draw_rect_outline(mx0 - 1, my0 - 1, map_w + 2, map_h + 2, 0xFF9E8248u);

    /* 14 Landmark Pins */
    for (uint8_t i = 0; i < HEARTH_GLOBE_LANDMARK_COUNT; i++) {
        const HearthGlobeLandmark *lm = &HEARTH_GLOBE_LANDMARKS[i];
        int lx = mx0 + ((int)(lm->tx / 4u)) * scale;
        int ly = my0 + ((int)(lm->ty / 4u)) * scale;
        fill_rect_px(lx, ly, scale, scale, lm->enterable ? 0xFFFFF1A8u : 0xFFE8A048u);
    }

    /* Designer Mode Selected Continent Warp Reticle */
    if (show_designer) {
        uint8_t cid = (st->atlas_cursor < REGION_COUNT) ? st->atlas_cursor : 0u;
        int wx = mx0 + ((int)(WARP_RX[cid] / 4u)) * scale;
        int wy = my0 + ((int)(WARP_RY[cid] / 4u)) * scale;
        uint32_t rc = ((st->frame_count >> 3) & 1u) ? 0xFFFFF1A8u : 0xFF66E2FFu;
        draw_rect_outline(wx - 3, wy - 3, scale + 6, scale + 6, rc);
    }

    /* Blinking Live Hero Marker */
    int htx = ((FROM_FP(st->hero_fx) / HEARTH_TILE_SIZE) % HEARTH_GLOBE_W + HEARTH_GLOBE_W) % HEARTH_GLOBE_W;
    int hty = ((FROM_FP(st->hero_fy) / HEARTH_TILE_SIZE) % HEARTH_GLOBE_H + HEARTH_GLOBE_H) % HEARTH_GLOBE_H;
    int px = mx0 + (htx / 4) * scale;
    int py = my0 + (hty / 4) * scale;
    uint32_t hc = ((st->frame_count >> 3) & 1u) ? 0xFFFFFFFFu : 0xFFFF4A4Au;
    fill_rect_px(px - 1, py - 1, scale + 2, scale + 2, 0xFF14101Cu);
    fill_rect_px(px, py, scale, scale, hc);

    char foot[72];
    const char *vname = (st->active_vehicle < 6u) ? VEH_LABELS[st->active_vehicle] : "ON FOOT";
    if (show_designer) {
        uint8_t cid = (st->atlas_cursor < REGION_COUNT) ? st->atlas_cursor : 0u;
        snprintf(foot, sizeof(foot), "WARP: < %s >  [SPACE] GO", HEARTH_REGION_NAMES[cid]);
        draw_text_centered(w / 2, by + bh - 23, foot, 0xFFFFF1A8u, false);
        snprintf(foot, sizeof(foot), "VESSEL: %s [UP/DN]  [M/ESC] CLOSE", vname);
        draw_text_centered(w / 2, by + bh - 12, foot, 0xFF9AD8F4u, false);
    } else {
        snprintf(foot, sizeof(foot), "TRAVEL: %s   [M / ESC / B] CLOSE", vname);
        draw_text_centered(w / 2, by + bh - 14, foot, 0xFFD8D0C0u, false);
    }
}

static void render_frame(const HearthState *st, const HearthPpuPacket *ppu) {
    g_ppu_w = ppu->width;
    g_ppu_h = ppu->height;
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;

    apply_zone_palette(ppu->terrain_pal_id);

    if (st->mode == MODE_INTRO) {
        render_intro_scene(st, ppu);
        render_particles_and_text(st, ppu);
    } else if (st->mode == MODE_BATTLE || st->mode == MODE_BATTLE_VICTORY || st->mode == MODE_BATTLE_DEFEAT) {
        render_battle_scene(st, ppu);
        render_particles_and_text(st, ppu);
    } else {
        /* Overworld / Title / Cutscene Tilemap Renderer */
        int cam_x = ppu->cam_x - ppu->shake_x;
        int cam_y = ppu->cam_y - ppu->shake_y;
        int mw = st->map_w ? (int)st->map_w : HEARTH_MAP_W;
        int mh = st->map_h ? (int)st->map_h : HEARTH_MAP_H;

        if (ppu->has_mountain_vista) {
            render_mountain_vista(st, ppu, cam_x);
        }

        int tx0 = ((cam_x >= 0) ? (cam_x / HEARTH_TILE_SIZE) : ((cam_x - 15) / HEARTH_TILE_SIZE)) - 1;
        int ty0 = ((cam_y >= 0) ? (cam_y / HEARTH_TILE_SIZE) : ((cam_y - 15) / HEARTH_TILE_SIZE)) - 1;
        int tx1 = ((cam_x + w) >= 0 ? ((cam_x + w) / HEARTH_TILE_SIZE) : (((cam_x + w) - 15) / HEARTH_TILE_SIZE)) + 1;
        int ty1 = ((cam_y + h) >= 0 ? ((cam_y + h) / HEARTH_TILE_SIZE) : (((cam_y + h) - 15) / HEARTH_TILE_SIZE)) + 1;
        if (!st->is_world_map) {
            if (tx0 < 0) tx0 = 0;
            if (ty0 < 0) ty0 = 0;
            if (tx1 >= mw) tx1 = mw - 1;
            if (ty1 >= mh) ty1 = mh - 1;
        }

        /* Layer 1: BG1 Ground & Architecture (Real-time Toroidal Globe Streaming vs Zone Buffer) */
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++) {
                uint8_t tile;
                if (st->is_world_map) {
                    uint16_t wx = (uint16_t)(tx & (HEARTH_GLOBE_W - 1));
                    uint16_t wy = (uint16_t)(((ty % HEARTH_GLOBE_H) + HEARTH_GLOBE_H) % HEARTH_GLOBE_H);
                    hearth_globe_cell(wx, wy, &tile, NULL, NULL, NULL);
                } else {
                    tile = st->map_bg1[ty * mw + tx];
                    if (tile == T_MTN_SKY && ppu->has_mountain_vista) continue;
                }
                draw_bg1_tile(tx * HEARTH_TILE_SIZE - cam_x, ty * HEARTH_TILE_SIZE - cam_y, tile, ppu->frame_count);
            }
        }

        /* World Map Large Multi-Tile Storybook Landmarks across all 7 Continents */
        if (st->is_world_map) {
            render_sdl_world_landmarks(cam_x, cam_y, ppu->frame_count);
        }

        /* Layer 2: Y-Sorted OAM Sprites (o->x, o->y are already screen-relative from hearth_core.c) */
        for (uint8_t i = 0; i < ppu->oam_count; i++) {
            const HearthOamSprite *o = &ppu->oam[i];
            if (o->layer <= 1) {
                draw_oam_sprite(o, o->x + ppu->shake_x, o->y + ppu->shake_y, ppu->frame_count);
            }
        }

        /* Layer 3: BG2 Overhead Tree Canopies & Roof Eaves */
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++) {
                uint8_t ov;
                if (st->is_world_map) {
                    uint16_t wx = (uint16_t)(tx & (HEARTH_GLOBE_W - 1));
                    uint16_t wy = (uint16_t)(((ty % HEARTH_GLOBE_H) + HEARTH_GLOBE_H) % HEARTH_GLOBE_H);
                    hearth_globe_cell(wx, wy, NULL, &ov, NULL, NULL);
                } else {
                    ov = st->map_bg2[ty * mw + tx];
                }
                if (ov != O_NONE) {
                    draw_bg2_tile(tx * HEARTH_TILE_SIZE - cam_x, ty * HEARTH_TILE_SIZE - cam_y, ov);
                }
            }
        }

        /* Layer 4: Overhead FX OAM (Windmill sails, emotes, Heart Orb) */
        for (uint8_t i = 0; i < ppu->oam_count; i++) {
            const HearthOamSprite *o = &ppu->oam[i];
            if (o->layer >= 2) {
                draw_oam_sprite(o, o->x + ppu->shake_x, o->y + ppu->shake_y, ppu->frame_count);
            }
        }

        if (st->is_world_map && st->mode == MODE_EXPLORE) {
            render_sdl_world_banners(st, cam_x, cam_y);
        }

        /* Optional Debug Hitbox Overlay (F6) */
        if (st->tuning.show_hitboxes) {
            int hx = FROM_FP(st->hero_fx) - cam_x;
            int hy = FROM_FP(st->hero_fy) - cam_y;
            draw_rect_outline(hx - 5, hy - 7, 10, 8, 0xFF5AD2F4u);
        }

        /* Lantern Lighting Mask (Deep Sewer Darkness vs Soft Moonlit Mountain Night) */
        if (ppu->zone_is_dark && !st->is_world_map && (st->mode == MODE_EXPLORE || st->mode == MODE_CUTSCENE || st->mode == MODE_FISHING)) {
            int lhx = FROM_FP(st->hero_fx) - cam_x;
            int lhy = FROM_FP(st->hero_fy) - cam_y - 6;
            int min_scale = ppu->has_mountain_vista ? 116 : 56;
            int blue_boost = ppu->has_mountain_vista ? 34 : 18;
            int span = 255 - min_scale;
            /* Collect visible lantern posts for warm light pools */
            int lx[8], ly[8], lcnt = 0;
            for (int ty = ty0; ty <= ty1 && lcnt < 8; ty++) {
                for (int tx = tx0; tx <= tx1 && lcnt < 8; tx++) {
                    if (st->map_bg1[ty * mw + tx] == T_LANTERN_POST) {
                        lx[lcnt] = tx * HEARTH_TILE_SIZE + 8 - cam_x;
                        ly[lcnt] = ty * HEARTH_TILE_SIZE + 4 - cam_y;
                        lcnt++;
                    }
                }
            }
            for (int py = 0; py < h; py++) {
                uint32_t *row = &g_fb[py * FB_MAX_W];
                for (int px = 0; px < w; px++) {
                    int dx = px - lhx, dy = py - lhy;
                    int d2 = dx * dx + dy * dy;
                    for (int k = 0; k < lcnt; k++) {
                        int kdx = px - lx[k], kdy = py - ly[k];
                        int kd2 = kdx * kdx + kdy * kdy;
                        if (kd2 < d2) d2 = kd2;
                    }
                    if (d2 > 2200) {
                        int scale = (d2 > 7600) ? min_scale : (255 - ((d2 - 2200) * span) / 5400);
                        uint32_t c = row[px];
                        uint32_t r = (((c >> 16) & 0xFFu) * (uint32_t)scale) >> 8;
                        uint32_t g = (((c >> 8)  & 0xFFu) * (uint32_t)scale) >> 8;
                        uint32_t b = ((c         & 0xFFu) * (uint32_t)(scale + blue_boost > 255 ? 255 : scale + blue_boost)) >> 8;
                        row[px] = 0xFF000000u | (r << 16) | (g << 8) | b;
                    }
                }
            }
        }

        render_particles_and_text(st, ppu);

        /* Contextual [A] Interact Prompt (already screen-relative) */
        if (ppu->interact_active && ppu->interact_label && st->mode == MODE_EXPLORE) {
            int ix = ppu->interact_x + ppu->shake_x;
            int iy = ppu->interact_y + ppu->shake_y;
            int lw = (int)strlen(ppu->interact_label) * 6 + 10;
            fill_rect_alpha(ix - lw / 2, iy - 5, lw, 11, 0xFF161220u, 220);
            draw_rect_outline(ix - lw / 2, iy - 5, lw, 11, 0xFFF5B841u);
            draw_text_centered(ix, iy - 3, ppu->interact_label, 0xFFFFF6CCu, false);
        }

        /* Minimal Overworld Top Status Ribbon */
        if (st->mode == MODE_EXPLORE) {
            fill_rect_alpha(4, 4, w - 8, 12, 0xFF14111Du, 195);
            char hud[64];
            if (st->is_world_map) {
                const char *vtag = (st->active_vehicle == VEH_RAFT) ? "RAFT"
                                 : (st->active_vehicle == VEH_SHIP) ? "SHIP"
                                 : (st->active_vehicle == VEH_BALLOON) ? "BALLOON"
                                 : (st->active_vehicle == VEH_AIRSHIP) ? "AIRSHIP"
                                 : (st->active_vehicle == VEH_SPACESHIP) ? "STARSHIP" : "FOOT";
                snprintf(hud, sizeof(hud), "%s  %uG  [M] ATLAS", vtag, (unsigned)st->guilders);
            } else if (ppu->objective_text && ppu->objective_text[0] != '\0' &&
                (st->zone_id == ZONE_ROWAN_HOME || st->zone_id == ZONE_ALDERBROOK ||
                 st->zone_id == ZONE_WHISPERING_WOODS || st->zone_id == ZONE_HOLLOW_GLADE)) {
                const char *tod_lbl = HEARTH_TOD_NAMES[ppu->time_of_day & 3u];
                snprintf(hud, sizeof(hud), "%s - %.26s", tod_lbl, ppu->objective_text);
            } else {
                snprintf(hud, sizeof(hud), "HP %d/%d MP %d %uG",
                         st->party[0].hp, st->party[0].max_hp,
                         st->party[0].mp, (unsigned)st->guilders);
            }
            draw_text(8, 7, hud, 0xFFF4EEDCu, false);
            const char *zn = (st->is_world_map && st->world_region_id < REGION_COUNT)
                           ? HEARTH_REGION_NAMES[st->world_region_id]
                           : (ppu->zone_name ? ppu->zone_name : "EVERTALE");
            int zw = (int)strlen(zn) * 6;
            draw_text(w - zw - 8, 7, zn, 0xFFFFF1A8u, false);
        }

        /* Zone / Continent Arrival Title Ribbon */
        if (ppu->zone_banner_frames > 0 && ppu->zone_name && st->mode == MODE_EXPLORE) {
            int bw2 = (int)strlen(ppu->zone_name) * 6 + 24;
            fill_rect_alpha(w / 2 - bw2 / 2, 22, bw2, 16, 0xFF14111Du, 224);
            draw_rect_outline(w / 2 - bw2 / 2, 22, bw2, 16, 0xFFE8B852u);
            draw_text_centered(w / 2, 27, ppu->zone_name, 0xFFFFF1A8u, true);
        }
    }

    /* Shop & Fishing Minigame Overlays */
    render_shop_overlay(st);
    render_fishing_overlay(st, ppu);

    /* World Atlas Minimap Overlay */
    if (st->atlas_open && st->mode == MODE_EXPLORE) {
        render_world_atlas_overlay(st);
    }

    /* Dialogue Box */
    render_dialogue_overlay(st);

    /* Title Overlay */
    if (st->mode == MODE_TITLE) {
        fill_rect_alpha(0, 0, w, h, 0xFF100E1Bu, 145);
        draw_text_scaled_centered(w / 2, 32, "PROJECT HEARTH", 0xFFFFF1A8u, 2);
        draw_text_centered(w / 2, 54, "ALDERBROOK & EVERTALE", 0xFF9AD8F4u, true);

#if HEARTH_DESIGNER_MODE
        static const char *OPTS[3] = {
            "NEW STORY",
            "EXPLORE EVERTALE",
            "REPLAY OPENING INTRO"
        };
        const int opt_cnt = 3;
#else
        static const char *OPTS[2] = {
            "NEW STORY",
            "REPLAY OPENING INTRO"
        };
        const int opt_cnt = 2;
#endif
        int my = 92;
        int box_h = opt_cnt * 16 + 12;
        fill_rect_alpha(w / 2 - 84, my - 8, 168, box_h, 0xFF161322u, 228);
        draw_rect_outline(w / 2 - 84, my - 8, 168, box_h, 0xFFE8B852u);
        for (int i = 0; i < opt_cnt; i++) {
            bool sel = (st->title_cursor == (uint8_t)i);
            if (sel) {
                fill_rect_px(w / 2 - 78, my + i * 16 - 2, 156, 12, 0xFF342A4Eu);
                draw_text(w / 2 - 74, my + i * 16, ">", 0xFFFFF1A8u, false);
            }
            draw_text_centered(w / 2 + 4, my + i * 16, OPTS[i], sel ? 0xFFFFF1A8u : 0xFFD4CFC4u, true);
        }
        draw_text_centered(w / 2, h - 34, "WASD/ARROWS MOVE  SPACE/Z ACT  M ATLAS  Q/E SWAP", 0xFFC8D2DCu, true);
        draw_text_centered(w / 2, h - 20, "F1 PPU  F2 CRT  F3 BOSS  F4 INTRO  F7 MUTE", 0xFF9AD8F4u, true);
        draw_text(w - 40, h - 10, HEARTH_VERSION_STR, 0xFF8A92A4u, false);
    }

    /* Battle Victory / Defeat Banners */
    if (st->mode == MODE_BATTLE_VICTORY || st->mode == MODE_BATTLE_DEFEAT) {
        bool won = (st->mode == MODE_BATTLE_VICTORY);
        fill_rect_alpha(w / 2 - 96, 46, 192, 68, 0xFF161322u, 236);
        draw_rect_outline(w / 2 - 96, 46, 192, 68, won ? 0xFFF5B841u : 0xFFE04E4Eu);
        draw_text_centered(w / 2, 54, won ? "VICTORY!" : "DEFEATED...", won ? 0xFFFFF1A8u : 0xFFFF8888u, true);
        char stats[48];
        snprintf(stats, sizeof(stats), "COMBOS:%u  PARRIES:%u  LOCKS:%u",
                 (unsigned)st->battle.combos, (unsigned)st->battle.parries, (unsigned)st->battle.locks_broken);
        draw_text_centered(w / 2, 72, stats, 0xFF9AD8F4u, true);
        snprintf(stats, sizeof(stats), "EXP +%u   GUILDERS +%uG",
                 (unsigned)st->battle.exp_earned, (unsigned)st->battle.guilders_earned);
        draw_text_centered(w / 2, 84, stats, 0xFFA8D678u, true);
        draw_text_centered(w / 2, 100, "PRESS [SPACE / A] TO CONTINUE", 0xFFFFF6CCu, true);
    }

    /* Hitstop / Critical White Screen Flash */
    if (ppu->flash_level > 0) {
        uint8_t a = (uint8_t)(ppu->flash_level * 12u);
        fill_rect_alpha(0, 0, w, h, 0xFFFFFFFFu, a);
    }

    /* Intro / Scene Crossfade Black Veil */
    if (ppu->fade_level > 0) {
        uint8_t a = (uint8_t)((ppu->fade_level >= 15u) ? 255u : (ppu->fade_level * 17u));
        fill_rect_alpha(0, 0, w, h, 0xFF06050Cu, a);
    }

    /* Optional Toggleable CRT Scanlines (F2) */
    if (g_scanlines) {
        for (int y = 1; y < h; y += 2) {
            uint32_t *row = &g_fb[y * FB_MAX_W];
            for (int x = 0; x < w; x++) {
                uint32_t c = row[x];
                uint32_t r = ((c >> 16) & 0xFFu) * 3u >> 2;
                uint32_t g = ((c >> 8)  & 0xFFu) * 3u >> 2;
                uint32_t b = (c         & 0xFFu) * 3u >> 2;
                row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }
}

/* ============================================================================
 * 7. KEYBOARD + GAMEPAD INPUT ADAPTER -> 12-BIT RETRO PAD BITMASK
 * ============================================================================ */
static uint16_t poll_pad_bits(SDL_GameController *pad) {
    const Uint8 *k = SDL_GetKeyboardState(NULL);
    uint16_t b = 0;

    if (k[SDL_SCANCODE_W] || k[SDL_SCANCODE_UP])    b |= PAD_UP;
    if (k[SDL_SCANCODE_S] || k[SDL_SCANCODE_DOWN])  b |= PAD_DOWN;
    if (k[SDL_SCANCODE_A] || k[SDL_SCANCODE_LEFT])  b |= PAD_LEFT;
    if (k[SDL_SCANCODE_D] || k[SDL_SCANCODE_RIGHT]) b |= PAD_RIGHT;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_SPACE] || k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_J]) b |= PAD_A;
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_LSHIFT] || k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_K]) b |= PAD_B;
    if (k[SDL_SCANCODE_C]) b |= PAD_X;
    if (k[SDL_SCANCODE_V] || k[SDL_SCANCODE_Y]) b |= PAD_Y;
    if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_LEFTBRACKET])  b |= PAD_L;
    if (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_RIGHTBRACKET]) b |= PAD_R;
    if (k[SDL_SCANCODE_ESCAPE]) b |= PAD_START;
    if (k[SDL_SCANCODE_TAB] || k[SDL_SCANCODE_M]) b |= PAD_SELECT;

    if (pad && SDL_GameControllerGetAttached(pad)) {
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP))    b |= PAD_UP;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  b |= PAD_DOWN;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  b |= PAD_LEFT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) b |= PAD_RIGHT;
        int16_t lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int16_t ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ly < -12000) b |= PAD_UP;
        if (ly >  12000) b |= PAD_DOWN;
        if (lx < -12000) b |= PAD_LEFT;
        if (lx >  12000) b |= PAD_RIGHT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) b |= PAD_A;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) b |= PAD_B;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_X)) b |= PAD_X;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_Y)) b |= PAD_Y;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  b |= PAD_L;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) b |= PAD_R;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) b |= PAD_START;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK))  b |= PAD_SELECT;
    }

    return b;
}

/* ============================================================================
 * 8. MAIN ENTRY POINT (60Hz FIXED TIMESTEP LOOP)
 * ============================================================================ */
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* Crisp nearest-neighbor pixels */

    SDL_Window *win = SDL_CreateWindow(
        "Project Hearth — Native C99 16-Bit JRPG Engine",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        960, 672,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) {
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!ren) {
        fprintf(stderr, "SDL_CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 1;
    }

    SDL_Texture *tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, FB_MAX_W, FB_MAX_H
    );

    /* Open Retro 16-Bit PCM Audio Device */
    SDL_AudioSpec want;
    memset(&want, 0, sizeof(want));
    want.freq     = SYNTH_SAMPLE_RATE;
    want.format   = AUDIO_S16SYS;
    want.channels = 1;
    want.samples  = 512;
    want.callback = synth_audio_callback;
    g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (g_audio_dev != 0) {
        SDL_PauseAudioDevice(g_audio_dev, 0);
    }

    /* Detect first connected gamepad */
    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (SDL_IsGameController(i)) {
            pad = SDL_GameControllerOpen(i);
            if (pad) break;
        }
    }

    init_palettes();
    bgm_init();
    hearth_init(&g_state);

    printf("Project Hearth (Native C99/SDL2) — Core RAM Footprint: %lu bytes\n",
           (unsigned long)sizeof(HearthState));
    printf("Controls: WASD/Arrows Move | Space/Z Act/Timed Hit | Shift Dash | M World Atlas | Q/E Hot-Swap Familiar\n");
    printf("Hotkeys:  F1 PPU (320x224/256x224) | F2 CRT Scanlines | F3 Shrine Boss | F4 Intro | F5 Heal | F7 Mute | F11 Fullscreen\n");

    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t step_ticks = (freq >= 60u) ? (freq / 60u) : 1u;
    uint64_t prev_ticks = SDL_GetPerformanceCounter();
    uint64_t acc_ticks  = 0;

    bool running = true;
    const HearthPpuPacket *ppu = hearth_step_60hz(&g_state, 0);

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) {
                running = false;
            } else if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad) {
                if (SDL_IsGameController(ev.cdevice.which)) {
                    pad = SDL_GameControllerOpen(ev.cdevice.which);
                }
            } else if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad) {
                if (ev.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))) {
                    SDL_GameControllerClose(pad);
                    pad = NULL;
                }
            } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
                switch (ev.key.keysym.sym) {
                    case SDLK_F1:
                        hearth_toggle_ppu_mode(&g_state);
                        break;
                    case SDLK_F2:
                        g_scanlines = !g_scanlines;
                        break;
                    case SDLK_F3:
                        hearth_start_battle(&g_state, ENCOUNTER_SHRINE_BOSS, BIOME_SHRINE, -1);
                        break;
                    case SDLK_F4:
                        hearth_start_intro(&g_state);
                        break;
                    case SDLK_F5:
                        hearth_heal_party(&g_state);
                        hearth_respawn_enemies(&g_state);
                        break;
                    case SDLK_F6:
                        g_state.tuning.show_hitboxes = !g_state.tuning.show_hitboxes;
                        break;
                    case SDLK_F7:
                        g_muted = !g_muted;
                        break;
                    case SDLK_F11:
                        g_fullscreen = !g_fullscreen;
                        SDL_SetWindowFullscreen(win, g_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        break;
                    default:
                        break;
                }
            }
        }

        uint64_t now = SDL_GetPerformanceCounter();
        uint64_t delta = now - prev_ticks;
        prev_ticks = now;
        if (delta > freq / 10u) delta = freq / 10u; /* Clamp spiral of death */
        acc_ticks += delta;

        int steps = 0;
        while (acc_ticks >= step_ticks && steps < 4) {
            uint16_t bits = poll_pad_bits(pad);
            ppu = hearth_step_60hz(&g_state, bits);
            for (uint8_t i = 0; i < ppu->sfx_count; i++) {
                synth_play_sfx(ppu->sfx_queue[i]);
            }
            bgm_set_track(ppu->bgm_id);
            acc_ticks -= step_ticks;
            steps++;
        }

        render_frame(&g_state, ppu);

        SDL_UpdateTexture(tex, NULL, g_fb, FB_MAX_W * (int)sizeof(uint32_t));
        SDL_SetRenderDrawColor(ren, 12, 10, 18, 255);
        SDL_RenderClear(ren);

        /* Preserve exact aspect ratio inside window */
        int win_w = 0, win_h = 0;
        SDL_GetRendererOutputSize(ren, &win_w, &win_h);
        int pw = (ppu->width  > 0) ? (int)ppu->width  : 320;
        int ph = (ppu->height > 0) ? (int)ppu->height : 224;
        SDL_Rect src = { 0, 0, pw, ph };

        int scale_x = win_w / pw;
        int scale_y = win_h / ph;
        int int_scale = (scale_x < scale_y) ? scale_x : scale_y;
        SDL_Rect dst;
        if (int_scale >= 1) {
            dst.w = pw * int_scale;
            dst.h = ph * int_scale;
        } else {
            dst.w = win_w;
            dst.h = win_h;
        }
        dst.x = (win_w - dst.w) / 2;
        dst.y = (win_h - dst.h) / 2;

        SDL_RenderCopy(ren, tex, &src, &dst);
        SDL_RenderPresent(ren);
        SDL_Delay(1);
    }

    if (pad) SDL_GameControllerClose(pad);
    if (g_audio_dev != 0) SDL_CloseAudioDevice(g_audio_dev);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
