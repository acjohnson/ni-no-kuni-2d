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

static void init_palettes(void) {
    for (int p = 0; p < 4; p++) {
        for (int i = 0; i < 16; i++) {
            g_pal_argb[p][i] = (i == 0) ? 0u : rgb_to_argb(HEARTH_PALETTES_RGB[p][i]);
        }
    }
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

static void draw_text_wrapped(int x, int y, int max_w, const char *s, size_t visible_chars, uint32_t col) {
    if (!s) return;
    int max_cols = max_w / 6;
    if (max_cols < 8) max_cols = 8;
    size_t pos = 0;
    int cy = y;
    int line_num = 0;
    while (pos < visible_chars && s[pos] != '\0' && line_num < 3) {
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

static void draw_oam_sprite(const HearthOamSprite *o, int ox, int oy, uint32_t frame) {
    bool flip  = (o->flags & OAM_FLAG_FLIP_X) != 0;
    bool flash = (o->flags & OAM_FLAG_FLASH_WHITE) != 0;
    int16_t sx = o->scale_x_fp ? o->scale_x_fp : FP_ONE;
    int16_t sy = o->scale_y_fp ? o->scale_y_fp : FP_ONE;

    if (o->flags & OAM_FLAG_SHADOW) {
        int r = (o->sprite_id == SPR_BOSS_BRIAR) ? 14 : 6;
        fill_ellipse_alpha(ox, oy + 1, r, 3, 0xFF10141Au, 110);
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

    int leg = (o->frame & 1u) ? 1 : -1;
    int bob = (o->frame & 1u) ? -1 : 0;

    switch (o->sprite_id) {
        case SPR_HERO: {
            /* Rowan: auburn hair, golden scarf, emerald adventurer tunic */
            uint32_t out  = pal_col(1, 1);
            uint32_t skin = pal_col(1, 2);
            uint32_t hair = pal_col(1, 4);
            uint32_t tunic= pal_col(1, 6);
            uint32_t tun2 = pal_col(1, 7);
            uint32_t cape = pal_col(1, 8);
            uint32_t gold = pal_col(1, 10);
            uint32_t boot = pal_col(1, 15);
            /* Boots */
            spr_box(ox, oy, -4, -3 + (leg > 0 ? -1 : 0), 3, 3, boot, flip, flash, sx, sy);
            spr_box(ox, oy,  1, -3 + (leg < 0 ? -1 : 0), 3, 3, boot, flip, flash, sx, sy);
            /* Tunic + cloak */
            spr_box(ox, oy, -5, -10 + bob, 10, 7, out, flip, flash, sx, sy);
            spr_box(ox, oy, -4, -9 + bob,  8, 6, tunic, flip, flash, sx, sy);
            spr_box(ox, oy, -3, -9 + bob,  5, 4, tun2, flip, flash, sx, sy);
            /* Scarf */
            spr_box(ox, oy, -4, -10 + bob, 8, 2, cape, flip, flash, sx, sy);
            spr_box(ox, oy, -2, -10 + bob, 4, 2, gold, flip, flash, sx, sy);
            /* Head & hair */
            spr_box(ox, oy, -5, -18 + bob, 10, 8, out, flip, flash, sx, sy);
            spr_box(ox, oy, -4, -17 + bob,  8, 7, skin, flip, flash, sx, sy);
            spr_box(ox, oy, -5, -19 + bob, 10, 4, hair, flip, flash, sx, sy);
            if (o->dir == DIR_UP) {
                spr_box(ox, oy, -4, -16 + bob, 8, 5, hair, flip, flash, sx, sy);
            } else if (o->dir == DIR_LEFT || o->dir == DIR_RIGHT) {
                spr_box(ox, oy,  1, -14 + bob, 2, 2, out, flip, flash, sx, sy);
            } else {
                spr_box(ox, oy, -3, -14 + bob, 2, 2, out, flip, flash, sx, sy);
                spr_box(ox, oy,  1, -14 + bob, 2, 2, out, flip, flash, sx, sy);
            }
            break;
        }
        case SPR_PIP: {
            /* Pip: floating brass lantern fairy */
            int fy = -10 + (HEARTH_SIN64[(frame >> 1) & 63u] >> 5);
            spr_box(ox, oy, -5, fy - 4, 10, 9, pal_col(1, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -4, fy - 3,  8, 7, pal_col(1, 10), flip, flash, sx, sy);
            spr_box(ox, oy, -3, fy - 2,  6, 5, pal_col(1, 11), flip, flash, sx, sy);
            /* Wings */
            int wf = ((frame >> 2) & 1u) ? -1 : 1;
            spr_box(ox, oy, -8, fy - 2 + wf, 3, 4, pal_col(1, 14), flip, flash, sx, sy);
            spr_box(ox, oy,  5, fy - 2 - wf, 3, 4, pal_col(1, 14), flip, flash, sx, sy);
            /* Eyes */
            spr_box(ox, oy, -2, fy - 1, 1, 2, pal_col(1, 1), flip, flash, sx, sy);
            spr_box(ox, oy,  1, fy - 1, 1, 2, pal_col(1, 1), flip, flash, sx, sy);
            break;
        }
        case SPR_FAMILIAR_BRAMBLE: {
            /* Bramblekit: Leafy acorn knight with oak shield & sword */
            spr_box(ox, oy, -6, -13 + bob, 12, 11, pal_col(1, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -5, -12 + bob, 10,  9, pal_col(1, 6), flip, flash, sx, sy);
            spr_box(ox, oy, -5, -16 + bob, 10,  5, pal_col(1, 4), flip, flash, sx, sy); /* Acorn cap */
            spr_box(ox, oy, -4, -15 + bob,  8,  3, pal_col(1, 5), flip, flash, sx, sy);
            spr_box(ox, oy, -1, -18 + bob,  2,  3, pal_col(1, 7), flip, flash, sx, sy); /* Sprout */
            /* Face */
            spr_box(ox, oy, -3, -10 + bob,  6,  4, pal_col(1, 2), flip, flash, sx, sy);
            spr_box(ox, oy, -2, -9 + bob,   1,  2, pal_col(1, 1), flip, flash, sx, sy);
            spr_box(ox, oy,  1, -9 + bob,   1,  2, pal_col(1, 1), flip, flash, sx, sy);
            /* Sword + Shield */
            spr_box(ox, oy,  6, -14 + bob,  2, 10, pal_col(1, 14), flip, flash, sx, sy);
            spr_box(ox, oy,  5,  -7 + bob,  4,  2, pal_col(1, 10), flip, flash, sx, sy);
            spr_box(ox, oy, -9,  -9 + bob,  4,  7, pal_col(1, 10), flip, flash, sx, sy);
            break;
        }
        case SPR_FAMILIAR_LUMINA: {
            /* Lumina: Floating radiant star-fox */
            int fy = -4 + (HEARTH_SIN64[(frame >> 1) & 63u] >> 6);
            spr_box(ox, oy, -6, -13 + fy, 12, 10, pal_col(1, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -5, -12 + fy, 10,  8, pal_col(1, 10), flip, flash, sx, sy);
            spr_box(ox, oy, -4, -10 + fy,  8,  5, pal_col(1, 11), flip, flash, sx, sy);
            /* Fox ears */
            spr_box(ox, oy, -6, -17 + fy,  3,  5, pal_col(1, 9), flip, flash, sx, sy);
            spr_box(ox, oy,  3, -17 + fy,  3,  5, pal_col(1, 9), flip, flash, sx, sy);
            /* Star gem & tail */
            spr_box(ox, oy, -1, -13 + fy,  2,  2, pal_col(1, 13), flip, flash, sx, sy);
            spr_box(ox, oy, -10, -10 + fy, 5,  5, pal_col(1, 14), flip, flash, sx, sy);
            spr_box(ox, oy, -2, -9 + fy,   1,  2, pal_col(1, 1), flip, flash, sx, sy);
            spr_box(ox, oy,  1, -9 + fy,   1,  2, pal_col(1, 1), flip, flash, sx, sy);
            break;
        }
        case SPR_NPC_ELDER:
        case SPR_NPC_CLARA:
        case SPR_NPC_GUARD: {
            uint32_t robe = (o->sprite_id == SPR_NPC_ELDER) ? pal_col(2, 13)
                          : (o->sprite_id == SPR_NPC_CLARA) ? pal_col(2, 10)
                          : pal_col(2, 8);
            uint32_t trim = (o->sprite_id == SPR_NPC_ELDER) ? pal_col(2, 7)
                          : (o->sprite_id == SPR_NPC_CLARA) ? pal_col(2, 15)
                          : pal_col(2, 5);
            uint32_t hair = (o->sprite_id == SPR_NPC_ELDER) ? pal_col(2, 4)
                          : (o->sprite_id == SPR_NPC_CLARA) ? pal_col(2, 7)
                          : pal_col(2, 11);
            spr_box(ox, oy, -5, -10, 10, 10, pal_col(2, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -4, -9,   8,  8, robe, flip, flash, sx, sy);
            spr_box(ox, oy, -4, -9,   8,  2, trim, flip, flash, sx, sy);
            spr_box(ox, oy, -5, -18, 10,  8, pal_col(2, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -4, -17,  8,  7, pal_col(2, 2), flip, flash, sx, sy);
            spr_box(ox, oy, -5, -19, 10,  4, hair, flip, flash, sx, sy);
            spr_box(ox, oy, -3, -14,  2,  2, pal_col(2, 1), flip, flash, sx, sy);
            spr_box(ox, oy,  1, -14,  2,  2, pal_col(2, 1), flip, flash, sx, sy);
            if (o->sprite_id == SPR_NPC_GUARD) {
                spr_box(ox, oy, 5, -17, 2, 16, pal_col(2, 5), flip, flash, sx, sy); /* Spear */
            }
            break;
        }
        case SPR_ENEMY_GLOOMCAP: {
            /* Purple mushroom imp */
            spr_box(ox, oy, -5, -8 + bob,  10, 7, pal_col(3, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -4, -7 + bob,   8, 5, pal_col(3, 5), flip, flash, sx, sy);
            spr_box(ox, oy, -8, -16 + bob, 16, 9, pal_col(3, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -7, -15 + bob, 14, 7, pal_col(3, 3), flip, flash, sx, sy);
            spr_box(ox, oy, -4, -13 + bob,  3, 3, pal_col(3, 4), flip, flash, sx, sy);
            spr_box(ox, oy,  2, -14 + bob,  3, 2, pal_col(3, 4), flip, flash, sx, sy);
            spr_box(ox, oy, -3,  -6 + bob,  2, 2, pal_col(3, 10), flip, flash, sx, sy);
            spr_box(ox, oy,  1,  -6 + bob,  2, 2, pal_col(3, 10), flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_THORNLING: {
            /* Spiky bramble golem */
            spr_box(ox, oy, -7, -14 + bob, 14, 13, pal_col(3, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -6, -13 + bob, 12, 11, pal_col(3, 6), flip, flash, sx, sy);
            spr_box(ox, oy, -5, -12 + bob, 10,  7, pal_col(3, 7), flip, flash, sx, sy);
            spr_box(ox, oy, -7, -17 + bob,  3,  4, pal_col(3, 8), flip, flash, sx, sy);
            spr_box(ox, oy,  4, -17 + bob,  3,  4, pal_col(3, 8), flip, flash, sx, sy);
            spr_box(ox, oy, -3, -9 + bob,   2,  2, pal_col(3, 11), flip, flash, sx, sy);
            spr_box(ox, oy,  1, -9 + bob,   2,  2, pal_col(3, 11), flip, flash, sx, sy);
            break;
        }
        case SPR_ENEMY_DUSK_MOTH: {
            int fy = -4 + (HEARTH_SIN64[(frame >> 1) & 63u] >> 5);
            int flap = ((frame >> 2) & 1u) ? -2 : 2;
            spr_box(ox, oy, -11, -13 + fy + flap, 7, 8, pal_col(3, 3), flip, flash, sx, sy);
            spr_box(ox, oy,   4, -13 + fy + flap, 7, 8, pal_col(3, 3), flip, flash, sx, sy);
            spr_box(ox, oy,  -9, -11 + fy + flap, 4, 4, pal_col(3, 15), flip, flash, sx, sy);
            spr_box(ox, oy,   5, -11 + fy + flap, 4, 4, pal_col(3, 15), flip, flash, sx, sy);
            spr_box(ox, oy,  -3, -14 + fy,        6, 10, pal_col(3, 1), flip, flash, sx, sy);
            spr_box(ox, oy,  -2, -13 + fy,        4,  8, pal_col(3, 2), flip, flash, sx, sy);
            spr_box(ox, oy,  -2, -12 + fy,        1,  2, pal_col(3, 11), flip, flash, sx, sy);
            spr_box(ox, oy,   1, -12 + fy,        1,  2, pal_col(3, 11), flip, flash, sx, sy);
            break;
        }
        case SPR_BOSS_BRIAR: {
            /* 32x32 Ancient Briar Warden Boss */
            int b = ((frame >> 4) & 1u) ? -1 : 0;
            /* Roots & armored body */
            spr_box(ox, oy, -14, -26 + b, 28, 25, pal_col(3, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -12, -24 + b, 24, 21, pal_col(3, 2), flip, flash, sx, sy);
            spr_box(ox, oy, -10, -22 + b, 20, 16, pal_col(3, 6), flip, flash, sx, sy);
            /* Antler crown */
            spr_box(ox, oy, -14, -33 + b,  5,  9, pal_col(3, 7), flip, flash, sx, sy);
            spr_box(ox, oy,   9, -33 + b,  5,  9, pal_col(3, 7), flip, flash, sx, sy);
            spr_box(ox, oy,  -4, -31 + b,  8,  6, pal_col(3, 8), flip, flash, sx, sy);
            /* Glowing core & eyes */
            spr_box(ox, oy,  -6, -18 + b,  4,  3, pal_col(3, 11), flip, flash, sx, sy);
            spr_box(ox, oy,   2, -18 + b,  4,  3, pal_col(3, 11), flip, flash, sx, sy);
            spr_box(ox, oy,  -4, -11 + b,  8,  6, pal_col(3, 4), flip, flash, sx, sy);
            spr_box(ox, oy,  -2, -10 + b,  4,  4, pal_col(3, 15), flip, flash, sx, sy);
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
                int px = ox + (ca * 14) / 127 - (sa * 4) / 127;
                int py = oy + (sa * 14) / 127 + (ca * 4) / 127;
                draw_line_px(px, py, ex, ey, pal_col(2, 15));
            }
            fill_rect_px(ox - 2, oy - 2, 5, 5, pal_col(0, 6));
            fill_rect_px(ox - 1, oy - 1, 3, 3, pal_col(1, 10));
            break;
        }
        case SPR_WAYSTONE: {
            uint32_t rune = ((frame >> 4) & 1u) ? pal_col(3, 15) : pal_col(0, 14);
            spr_box(ox, oy, -6, -18, 12, 18, pal_col(0, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -5, -17, 10, 16, pal_col(0, 10), flip, flash, sx, sy);
            spr_box(ox, oy, -4, -16,  8, 14, pal_col(0, 11), flip, flash, sx, sy);
            spr_box(ox, oy, -2, -13,  4,  7, rune, flip, flash, sx, sy);
            break;
        }
        case SPR_CHEST: {
            bool opened = o->frame > 0;
            spr_box(ox, oy, -7, -11, 14, 11, pal_col(2, 1), flip, flash, sx, sy);
            spr_box(ox, oy, -6, -10, 12,  9, opened ? pal_col(2, 11) : pal_col(2, 12), flip, flash, sx, sy);
            spr_box(ox, oy, -6,  -6, 12,  2, pal_col(2, 7), flip, flash, sx, sy);
            spr_box(ox, oy, -2,  -7,  4,  3, opened ? pal_col(2, 1) : pal_col(2, 15), flip, flash, sx, sy);
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
            fill_ellipse_alpha(ox, oy, 7, 7, pal_col(1, 10), 120);
            fill_rect_px(ox - 3, oy - 3, 6, 6, pal_col(1, 10));
            fill_rect_px(ox - 2, oy - 2, 4, 4, pal_col(1, 11));
            break;
        }
        default:
            break;
    }
}

/* ============================================================================
 * 6. BATTLE ARENA, DIALOGUE, TITLE & HUD OVERLAYS (Rendered inside Virtual PPU)
 * ============================================================================ */
static void render_battle_scene(const HearthState *st, const HearthPpuPacket *ppu) {
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;
    bool shrine = (st->battle.biome == BIOME_SHRINE);

    /* Sky gradient */
    for (int y = 0; y < 96; y++) {
        uint32_t col;
        if (shrine) {
            col = (y < 32) ? 0xFF1B1932u : (y < 64) ? 0xFF28264Bu : 0xFF3A3562u;
        } else {
            col = (y < 32) ? 0xFF4D9CD0u : (y < 64) ? 0xFF77BBE6u : 0xFFBEE7F8u;
        }
        fill_rect_px(0, y, w, 1, col);
    }

    /* Distant rolling hills / ruins silhouette */
    for (int x = 0; x < w; x++) {
        int hill = 82 + (HEARTH_SIN64[(x >> 2) & 63] >> 4);
        fill_rect_px(x, hill, 1, 96 - hill, shrine ? 0xFF232538u : 0xFF4A884Eu);
    }

    /* Battleground stage floor */
    fill_rect_px(0, 96, w, h - 96, shrine ? 0xFF4C5868u : 0xFF74B461u);
    fill_rect_px(0, 96, w, 3, shrine ? 0xFF8591A0u : 0xFFA8D678u);
    if (shrine) {
        draw_circle_outline(w / 2, 134, 44, pal_col(3, 15));
        draw_circle_outline(w / 2, 134, 30, pal_col(0, 11));
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

    /* Timed Hit / Parry Converging Timing Ring */
    if (st->battle.ring.active) {
        int rx = st->battle.ring.x + ppu->shake_x;
        int ry = st->battle.ring.y + ppu->shake_y;
        int rem = 256 - (int)st->battle.ring.progress_fp;
        if (rem < 0) rem = 0;
        int outer_r = 9 + (rem * 24) / 256;
        bool in_sweet = st->battle.ring.progress_fp >= st->battle.ring.sweet_start_fp;
        uint32_t ring_col = in_sweet ? 0xFFFFF1A8u
                          : (st->battle.ring.is_parry ? 0xFF5AD2F4u : 0xFFF5B841u);
        draw_circle_outline(rx, ry, 9, in_sweet ? 0xFFFFFFFFu : 0xFFE8B43Cu);
        draw_circle_outline(rx, ry, outer_r, ring_col);
        if (in_sweet) {
            draw_circle_outline(rx, ry, outer_r - 1, 0xFFFFFFFFu);
            draw_text_centered(rx, ry - 20, st->battle.ring.is_parry ? "[A] PARRY!" : "[A] STRIKE!", 0xFFFFF1A8u, true);
        }
    }

    /* Top Action Banner */
    if (st->battle.banner_frames > 0 && st->battle.banner[0] != '\0') {
        fill_rect_alpha(w / 2 - 96, 6, 192, 14, 0xFF14111Du, 215);
        draw_rect_outline(w / 2 - 96, 6, 192, 14, 0xFFE8B43Cu);
        draw_text_centered(w / 2, 10, st->battle.banner, 0xFFFFF6CCu, true);
    }

    /* Bottom Classic 16-Bit JRPG Battle HUD Bar */
    int hud_y = h - 56;
    fill_rect_alpha(4, hud_y, w - 8, 52, 0xFF161322u, 236);
    draw_rect_outline(4, hud_y, w - 8, 52, 0xFFE8B852u);
    draw_rect_outline(5, hud_y + 1, w - 10, 50, 0xFF5C4A28u);

    /* Top Row inside HUD: Horizontal Battle Commands (A/D or Left/Right selects) */
    int tab_w = (w - 16) / 4;
    for (int i = 0; i < 4; i++) {
        int tx = 8 + i * tab_w;
        int ty = hud_y + 4;
        bool sel = (st->battle.selected_cmd == (uint8_t)i);
        fill_rect_px(tx, ty, tab_w - 3, 12, (sel && st->battle.phase == BPHASE_COMMAND) ? 0xFF3A2E58u : 0xFF211C31u);
        draw_rect_outline(tx, ty, tab_w - 3, 12, (sel && st->battle.phase == BPHASE_COMMAND) ? 0xFFFFF1A8u : 0xFF4A4262u);

        char label[20];
        if (i == 0) {
            snprintf(label, sizeof(label), "%sSTRIKE", sel ? ">" : "");
        } else if (i == 1) {
            const HearthPartyMember *pm = &st->party[st->battle.active_slot];
            snprintf(label, sizeof(label), "%s%s %u", sel ? ">" : "", pm->skill_is_heal ? "HEAL" : "SKILL", (unsigned)pm->skill_cost);
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
        int cy = hud_y + 19;
        bool active = (st->battle.active_slot == (uint8_t)i);
        fill_rect_px(cx, cy, card_w - 3, 30, active ? 0xFF2B2446u : 0xFF1D192Bu);
        draw_rect_outline(cx, cy, card_w - 3, 30, active ? 0xFFF5B841u : 0xFF4C4562u);

        char hdr[16];
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
    int bx = 8, by = h - 62, bw = w - 16, bh = 54;

    fill_rect_alpha(bx, by, bw, bh, 0xFF171423u, 236);
    draw_rect_outline(bx, by, bw, bh, 0xFFE8B852u);
    draw_rect_outline(bx + 1, by + 1, bw - 2, bh - 2, 0xFF544324u);

    /* Speaker Nameplate */
    if (st->dialogue.speaker && st->dialogue.speaker[0] != '\0') {
        int nw = (int)strlen(st->dialogue.speaker) * 6 + 10;
        fill_rect_px(bx + 6, by - 11, nw, 12, 0xFF28203Cu);
        draw_rect_outline(bx + 6, by - 11, nw, 12, 0xFFE8B852u);
        draw_text(bx + 11, by - 8, st->dialogue.speaker, 0xFFFFF1A8u, true);
    }

    draw_text_wrapped(bx + 8, by + 8, bw - 16, st->dialogue.full_text, st->dialogue.visible_chars, 0xFFF4EEDCu);

    if (st->dialogue.finished) {
        if (st->dialogue.choice_count > 0) {
            int cy = by + 30;
            for (uint8_t i = 0; i < st->dialogue.choice_count; i++) {
                bool sel = (st->dialogue.selected_choice == i);
                int cx = bx + 12 + (int)i * ((bw - 24) / st->dialogue.choice_count);
                if (sel) draw_text(cx - 7, cy, ">", 0xFFFFF1A8u, true);
                draw_text(cx, cy, st->dialogue.choices[i].label, sel ? 0xFFFFF1A8u : 0xFFA8A4B4u, true);
            }
        } else if ((st->frame_count >> 4) & 1u) {
            draw_text(bx + bw - 26, by + bh - 11, "[A]", 0xFFF5B841u, true);
        }
    }
}

static void render_frame(const HearthState *st, const HearthPpuPacket *ppu) {
    g_ppu_w = ppu->width;
    g_ppu_h = ppu->height;
    int w = (int)g_ppu_w;
    int h = (int)g_ppu_h;

    if (st->mode == MODE_BATTLE || st->mode == MODE_BATTLE_VICTORY || st->mode == MODE_BATTLE_DEFEAT) {
        render_battle_scene(st, ppu);
        render_particles_and_text(st, ppu);
    } else {
        /* Overworld / Title / Cutscene Tilemap Renderer */
        int cam_x = ppu->cam_x - ppu->shake_x;
        int cam_y = ppu->cam_y - ppu->shake_y;

        int tx0 = cam_x / HEARTH_TILE_SIZE - 1;
        int ty0 = cam_y / HEARTH_TILE_SIZE - 1;
        int tx1 = (cam_x + w) / HEARTH_TILE_SIZE + 1;
        int ty1 = (cam_y + h) / HEARTH_TILE_SIZE + 1;
        if (tx0 < 0) tx0 = 0;
        if (ty0 < 0) ty0 = 0;
        if (tx1 >= HEARTH_MAP_W) tx1 = HEARTH_MAP_W - 1;
        if (ty1 >= HEARTH_MAP_H) ty1 = HEARTH_MAP_H - 1;

        /* Layer 1: BG1 Ground & Architecture */
        for (int ty = ty0; ty <= ty1; ty++) {
            for (int tx = tx0; tx <= tx1; tx++) {
                uint8_t tile = st->map_bg1[ty * HEARTH_MAP_W + tx];
                draw_bg1_tile(tx * HEARTH_TILE_SIZE - cam_x, ty * HEARTH_TILE_SIZE - cam_y, tile, ppu->frame_count);
            }
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
                uint8_t ov = st->map_bg2[ty * HEARTH_MAP_W + tx];
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

        /* Optional Debug Hitbox Overlay (F6) */
        if (st->tuning.show_hitboxes) {
            int hx = FROM_FP(st->hero_fx) - cam_x;
            int hy = FROM_FP(st->hero_fy) - cam_y;
            draw_rect_outline(hx - 5, hy - 7, 10, 8, 0xFF5AD2F4u);
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
            fill_rect_alpha(4, 4, w - 8, 12, 0xFF14111Du, 185);
            char hud[64];
            snprintf(hud, sizeof(hud), "HP %d/%d  MP %d/%d  %s",
                     st->party[0].hp, st->party[0].max_hp,
                     st->party[0].mp, st->party[0].max_mp,
                     st->flag_guard_mended ? "HEART:MENDED"
                     : (st->flag_has_courage ? "LOCKET:COURAGE" : "LOCKET:EMPTY"));
            draw_text(8, 7, hud, 0xFFF4EEDCu, false);
            draw_text(w - 76, 7, st->tuning.ppu_mode == PPU_GENESIS_320x224 ? "GEN 320X224" : "SNS 256X224", 0xFF9AD8F4u, false);
        }
    }

    /* Dialogue Box */
    render_dialogue_overlay(st);

    /* Title Overlay */
    if (st->mode == MODE_TITLE) {
        fill_rect_alpha(0, 0, w, h, 0xFF100E1Bu, 145);
        draw_text_scaled_centered(w / 2, 34, "PROJECT HEARTH", 0xFFFFF1A8u, 2);
        draw_text_centered(w / 2, 56, "PURE C99 16-BIT JRPG ENGINE", 0xFF9AD8F4u, true);

        static const char *OPTS[3] = {
            "PLAY WINDMILL HAMLET",
            "SHRINE BOSS BATTLE",
            "CUTSCENE VM DEMO"
        };
        int my = 96;
        fill_rect_alpha(w / 2 - 84, my - 8, 168, 60, 0xFF161322u, 225);
        draw_rect_outline(w / 2 - 84, my - 8, 168, 60, 0xFFE8B852u);
        for (int i = 0; i < 3; i++) {
            bool sel = (st->title_cursor == (uint8_t)i);
            if (sel) {
                fill_rect_px(w / 2 - 78, my + i * 16 - 2, 156, 12, 0xFF342A4Eu);
                draw_text(w / 2 - 74, my + i * 16, ">", 0xFFFFF1A8u, false);
            }
            draw_text_centered(w / 2 + 4, my + i * 16, OPTS[i], sel ? 0xFFFFF1A8u : 0xFFD4CFC4u, true);
        }
        draw_text_centered(w / 2, h - 32, "WASD/ARROWS MOVE  SPACE/Z ACT  SHIFT DASH", 0xFFC8D2DCu, true);
        draw_text_centered(w / 2, h - 20, "F1 PPU  F2 CRT  F3 BOSS  F4 CUTSCENE  M MUTE", 0xFF9AD8F4u, true);
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
        snprintf(stats, sizeof(stats), "EXP EARNED: +%u", (unsigned)st->battle.exp_earned);
        draw_text_centered(w / 2, 84, stats, 0xFFA8D678u, true);
        draw_text_centered(w / 2, 100, "PRESS [SPACE / A] TO CONTINUE", 0xFFFFF6CCu, true);
    }

    /* Hitstop / Critical White Screen Flash */
    if (ppu->flash_level > 0) {
        uint8_t a = (uint8_t)(ppu->flash_level * 12u);
        fill_rect_alpha(0, 0, w, h, 0xFFFFFFFFu, a);
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
    if (k[SDL_SCANCODE_V]) b |= PAD_Y;
    if (k[SDL_SCANCODE_Q] || k[SDL_SCANCODE_LEFTBRACKET])  b |= PAD_L;
    if (k[SDL_SCANCODE_E] || k[SDL_SCANCODE_RIGHTBRACKET]) b |= PAD_R;
    if (k[SDL_SCANCODE_ESCAPE]) b |= PAD_START;
    if (k[SDL_SCANCODE_TAB])    b |= PAD_SELECT;

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
    hearth_init(&g_state);

    printf("Project Hearth (Native C99/SDL2) — Core RAM Footprint: %lu bytes\n",
           (unsigned long)sizeof(HearthState));
    printf("Controls: WASD/Arrows Move | Space/Z Act/Timed Hit | Shift Dash | Q/E Hot-Swap Familiar\n");
    printf("Hotkeys:  F1 PPU (320x224/256x224) | F2 CRT Scanlines | F3 Shrine Boss | F4 Cutscene | F5 Heal | M Mute | F11 Fullscreen\n");

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
                        hearth_start_cutscene(&g_state, CUTSCENE_INTRO_DEMO);
                        break;
                    case SDLK_F5:
                        hearth_heal_party(&g_state);
                        hearth_respawn_enemies(&g_state);
                        break;
                    case SDLK_F6:
                        g_state.tuning.show_hitboxes = !g_state.tuning.show_hitboxes;
                        break;
                    case SDLK_m:
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
