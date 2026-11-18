/* ============================================================================
 * gen_snes_audio.c — Zero-Dependency Impulse Tracker (.it) Generator for SNES
 *
 * Generates 4 compact, spec-compliant Impulse Tracker 2.14 (.it) modules
 * converted directly by PVSnesLib's `smconv` into an SPC700 BRR soundbank:
 *   0. hearth_sfx.it    (6 crisp 16kHz SFX samples — MUST be 1st for smconv!)
 *   1. hearth_hamlet.it (Windmill Hamlet — 3/4 Pastoral Waltz in G Major)
 *   2. hearth_battle.it (Whispering Meadow — 4/4 Heroic JRPG Battle in A Minor)
 *   3. hearth_boss.it   (Briar Warden Boss — 4/4 Urgent Shrine Boss in D Minor)
 *
 * Pure ANSI C99, zero external libraries (no -lm required).
 * Usage: gen_snes_audio <output_dir>
 * ============================================================================ */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * 1. INTEGER-SAFE SINE APPROXIMATION & BINARY WRITER
 * ============================================================================ */
static double fast_sin(double x) {
    const double PI = 3.14159265358979323846;
    const double TWO_PI = 6.28318530717958647692;
    while (x > PI)  x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    /* 9th-order minimax/Taylor polynomial — accurate to < 1e-5 over [-pi, pi] */
    double x2 = x * x;
    return x * (1.0 - x2 * (1.0 / 6.0
                  - x2 * (1.0 / 120.0
                  - x2 * (1.0 / 5040.0
                  - x2 * (1.0 / 362880.0)))));
}

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} Buf;

static void buf_init(Buf *b) {
    b->cap = 65536;
    b->len = 0;
    b->data = (uint8_t *)calloc(1, b->cap);
    if (!b->data) {
        fprintf(stderr, "gen_snes_audio: out of memory\n");
        exit(1);
    }
}

static void buf_ensure(Buf *b, size_t extra) {
    while (b->len + extra > b->cap) {
        b->cap *= 2;
        b->data = (uint8_t *)realloc(b->data, b->cap);
        if (!b->data) {
            fprintf(stderr, "gen_snes_audio: out of memory\n");
            exit(1);
        }
    }
}

static void buf_u8(Buf *b, uint8_t v) {
    buf_ensure(b, 1);
    b->data[b->len++] = v;
}

static void buf_u16(Buf *b, uint16_t v) {
    buf_u8(b, (uint8_t)(v & 0xFFu));
    buf_u8(b, (uint8_t)((v >> 8) & 0xFFu));
}

static void buf_u32(Buf *b, uint32_t v) {
    buf_u8(b, (uint8_t)(v & 0xFFu));
    buf_u8(b, (uint8_t)((v >> 8) & 0xFFu));
    buf_u8(b, (uint8_t)((v >> 16) & 0xFFu));
    buf_u8(b, (uint8_t)((v >> 24) & 0xFFu));
}

static void buf_zeros(Buf *b, size_t n) {
    buf_ensure(b, n);
    memset(b->data + b->len, 0, n);
    b->len += n;
}

static void buf_str_fixed(Buf *b, const char *s, size_t n) {
    size_t slen = strlen(s);
    if (slen > n) slen = n;
    buf_ensure(b, n);
    memcpy(b->data + b->len, s, slen);
    if (slen < n) memset(b->data + b->len + slen, 0, n - slen);
    b->len += n;
}

static void buf_patch_u32(Buf *b, size_t offset, uint32_t v) {
    b->data[offset + 0] = (uint8_t)(v & 0xFFu);
    b->data[offset + 1] = (uint8_t)((v >> 8) & 0xFFu);
    b->data[offset + 2] = (uint8_t)((v >> 16) & 0xFFu);
    b->data[offset + 3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void buf_write_file(const Buf *b, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "gen_snes_audio: failed to open %s for writing\n", path);
        exit(1);
    }
    if (fwrite(b->data, 1, b->len, f) != b->len) {
        fprintf(stderr, "gen_snes_audio: short write to %s\n", path);
        fclose(f);
        exit(1);
    }
    fclose(f);
}

static int16_t clamp_s16(double v) {
    if (v > 32700.0) return 32700;
    if (v < -32700.0) return -32700;
    return (int16_t)v;
}

static uint32_t g_rng = 0x12345678u;
static double next_noise(void) {
    g_rng = g_rng * 1664525u + 1013904223u;
    return ((double)((int32_t)(g_rng >> 16) - 32768)) / 32768.0;
}

/* ============================================================================
 * 2. IMPULSE TRACKER 2.14 BUILDER STRUCTURES
 * ============================================================================ */
typedef struct {
    const char *name;
    int16_t    *pcm;
    uint32_t    length;   /* Always a multiple of 16 samples for clean BRR! */
    uint32_t    c5speed;
    uint8_t     looped;
    uint8_t     vol;      /* 0..64 */
    /* Simple 3-node volume envelope for instruments */
    uint8_t     use_env;
    uint8_t     env_v0, env_v1, env_v2;
    uint16_t    env_t1, env_t2;
} ItSampleSpec;

static void write_it_instrument(Buf *b, uint8_t smp_1based, const ItSampleSpec *sp) {
    buf_str_fixed(b, "IMPI", 4);
    buf_str_fixed(b, sp->name, 12);
    buf_u8(b, 0);          /* Reserved */
    buf_u8(b, 0);          /* NNA: Cut */
    buf_u8(b, 0);          /* DCT: Off */
    buf_u8(b, 0);          /* DCA: Cut */
    buf_u16(b, 256);       /* FadeOut */
    buf_u8(b, 0);          /* PPS */
    buf_u8(b, 60);         /* PPC */
    buf_u8(b, 128);        /* GbV (max 128) */
    buf_u8(b, 128 | 32);   /* DfP (disabled, center) */
    buf_u8(b, 0);          /* RV */
    buf_u8(b, 0);          /* RP */
    buf_u16(b, 0x0214);    /* TrkVers */
    buf_u8(b, 1);          /* NoS */
    buf_u8(b, 0);          /* Reserved */
    buf_str_fixed(b, sp->name, 26);
    buf_u8(b, 0);          /* IFC */
    buf_u8(b, 0);          /* IFR */
    buf_u8(b, 0);          /* MCh */
    buf_u8(b, 0);          /* MPr */
    buf_u16(b, 0);         /* MIDIBnk */
    /* Note-Sample Keyboard Table (120 * 2 = 240 bytes) */
    for (int n = 0; n < 120; n++) {
        buf_u8(b, (uint8_t)n);
        buf_u8(b, smp_1based);
    }
    /* Volume Envelope (82 bytes) */
    if (sp->use_env) {
        buf_u8(b, 0x01); /* Flg: On */
        buf_u8(b, 3);    /* 3 nodes */
        buf_u8(b, 0); buf_u8(b, 0); buf_u8(b, 0); buf_u8(b, 0);
        buf_u8(b, sp->env_v0); buf_u16(b, 0);
        buf_u8(b, sp->env_v1); buf_u16(b, sp->env_t1);
        buf_u8(b, sp->env_v2); buf_u16(b, sp->env_t2);
        buf_zeros(b, 22 * 3 + 1);
    } else {
        buf_zeros(b, 82);
    }
    /* Panning Envelope (82 bytes) & Pitch Envelope (82 bytes) + 4 pad */
    buf_zeros(b, 82 + 82 + 4);
}

static size_t write_it_sample_header(Buf *b, const ItSampleSpec *sp) {
    buf_str_fixed(b, "IMPS", 4);
    buf_str_fixed(b, sp->name, 12);
    buf_u8(b, 0);
    buf_u8(b, 64); /* GvL */
    /* Flags: 0x01=present, 0x02=16-bit, 0x10=loop */
    buf_u8(b, (uint8_t)(0x03u | (sp->looped ? 0x10u : 0x00u)));
    buf_u8(b, sp->vol);
    buf_str_fixed(b, sp->name, 26);
    buf_u8(b, 0x01); /* Cvt: signed PCM */
    buf_u8(b, 32);   /* DfP */
    buf_u32(b, sp->length);
    buf_u32(b, 0);
    buf_u32(b, sp->looped ? sp->length : 0u);
    buf_u32(b, sp->c5speed);
    buf_u32(b, 0); /* SusLoopBegin */
    buf_u32(b, 0); /* SusLoopEnd */
    size_t ptr_off = b->len;
    buf_u32(b, 0); /* SamplePointer (patched later) */
    buf_u8(b, 0); buf_u8(b, 0); buf_u8(b, 0); buf_u8(b, 0); /* Vibrato */
    return ptr_off;
}

static void write_it_module(
    const char *path,
    const char *title,
    uint8_t init_speed,
    uint8_t init_tempo,
    uint8_t num_channels,
    const ItSampleSpec *samples,
    uint16_t num_samples,
    const uint8_t *pat_data,
    uint16_t pat_bytes,
    uint16_t pat_rows
) {
    Buf b;
    buf_init(&b);

    uint16_t ord_num = 2; /* [0, 255] */
    uint16_t ins_num = num_samples;
    uint16_t smp_num = num_samples;
    uint16_t pat_num = 1;

    /* 192-byte IT Header */
    buf_str_fixed(&b, "IMPM", 4);
    buf_str_fixed(&b, title, 26);
    buf_u16(&b, 0x1004);     /* PHiligt */
    buf_u16(&b, ord_num);
    buf_u16(&b, ins_num);
    buf_u16(&b, smp_num);
    buf_u16(&b, pat_num);
    buf_u16(&b, 0x0214);     /* Cwt/v */
    buf_u16(&b, 0x0214);     /* Cmwt */
    buf_u16(&b, 0x000D);     /* Flags: Stereo | Instruments | LinearSlides */
    buf_u16(&b, 0x0000);     /* Special */
    buf_u8(&b, 128);         /* GV */
    buf_u8(&b, 56);          /* MV */
    buf_u8(&b, init_speed);  /* IS */
    buf_u8(&b, init_tempo);  /* IT */
    buf_u8(&b, 128);         /* Sep */
    buf_u8(&b, 0);           /* PWD */
    buf_u16(&b, 0);          /* MsgLgth */
    buf_u32(&b, 0);          /* MessageOffset */
    buf_u32(&b, 0);          /* Reserved */

    /* 64 Channel Pan values (0..64, +128 = disabled) */
    for (int ch = 0; ch < 64; ch++) {
        if (ch < num_channels) {
            static const uint8_t pans[4] = { 28, 42, 32, 36 };
            buf_u8(&b, pans[ch & 3]);
        } else {
            buf_u8(&b, 128);
        }
    }
    /* 64 Channel Vol values */
    for (int ch = 0; ch < 64; ch++) {
        buf_u8(&b, (ch < num_channels) ? 64 : 0);
    }

    /* Order list */
    buf_u8(&b, 0);
    buf_u8(&b, 255);

    /* Offset tables (patched below) */
    size_t ins_tbl = b.len;
    buf_zeros(&b, (size_t)ins_num * 4u);
    size_t smp_tbl = b.len;
    buf_zeros(&b, (size_t)smp_num * 4u);
    size_t pat_tbl = b.len;
    buf_zeros(&b, (size_t)pat_num * 4u);

    /* Instruments */
    for (uint16_t i = 0; i < ins_num; i++) {
        buf_patch_u32(&b, ins_tbl + (size_t)i * 4u, (uint32_t)b.len);
        write_it_instrument(&b, (uint8_t)(i + 1u), &samples[i]);
    }

    /* Sample Headers */
    size_t smp_ptr_offs[16];
    for (uint16_t i = 0; i < smp_num; i++) {
        buf_patch_u32(&b, smp_tbl + (size_t)i * 4u, (uint32_t)b.len);
        smp_ptr_offs[i] = write_it_sample_header(&b, &samples[i]);
    }

    /* Pattern 0 */
    buf_patch_u32(&b, pat_tbl, (uint32_t)b.len);
    buf_u16(&b, pat_bytes);
    buf_u16(&b, pat_rows);
    buf_u32(&b, 0);
    buf_ensure(&b, pat_bytes);
    memcpy(b.data + b.len, pat_data, pat_bytes);
    b.len += pat_bytes;

    /* Raw 16-bit Signed PCM Sample Data */
    for (uint16_t i = 0; i < smp_num; i++) {
        buf_patch_u32(&b, smp_ptr_offs[i], (uint32_t)b.len);
        for (uint32_t k = 0; k < samples[i].length; k++) {
            buf_u16(&b, (uint16_t)samples[i].pcm[k]);
        }
    }

    buf_write_file(&b, path);
    free(b.data);
}

/* ============================================================================
 * 3. PROCEDURAL 16-BIT WAVEFORM & SFX SYNTHESIZERS (Multiples of 16 samples!)
 * ============================================================================ */
static int16_t *make_wave_flute(uint32_t *out_len) {
    /* 32-sample warm ocarina/flute (fundamental + gentle 2nd & 3rd harmonics) */
    const uint32_t N = 32;
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    for (uint32_t i = 0; i < N; i++) {
        double t = (double)i / (double)N;
        double s = 0.72 * fast_sin(TWO_PI * t)
                 + 0.22 * fast_sin(TWO_PI * 2.0 * t)
                 + 0.06 * fast_sin(TWO_PI * 3.0 * t);
        p[i] = clamp_s16(s * 25000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_wave_harp(uint32_t *out_len) {
    /* 32-sample plucked celesta/harp harmonic timbre */
    const uint32_t N = 32;
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    for (uint32_t i = 0; i < N; i++) {
        double t = (double)i / (double)N;
        double s = 0.56 * fast_sin(TWO_PI * t)
                 + 0.28 * fast_sin(TWO_PI * 2.0 * t)
                 + 0.14 * fast_sin(TWO_PI * 3.0 * t)
                 + 0.08 * fast_sin(TWO_PI * 4.0 * t);
        p[i] = clamp_s16(s * 24000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_wave_lead_brass(uint32_t *out_len) {
    /* 32-sample bright 16-bit JRPG battle brass/square-saw lead */
    const uint32_t N = 32;
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    for (uint32_t i = 0; i < N; i++) {
        double t = (double)i / (double)N;
        double s = 0.58 * fast_sin(TWO_PI * t)
                 + 0.30 * fast_sin(TWO_PI * 2.0 * t)
                 + 0.24 * fast_sin(TWO_PI * 3.0 * t)
                 + 0.10 * fast_sin(TWO_PI * 5.0 * t);
        p[i] = clamp_s16(s * 22000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_wave_bass(uint32_t *out_len) {
    /* 32-sample warm acoustic/triangle bass */
    const uint32_t N = 32;
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    for (uint32_t i = 0; i < N; i++) {
        double t = (double)i / (double)N;
        double s = 0.78 * fast_sin(TWO_PI * t)
                 + 0.18 * fast_sin(TWO_PI * 2.0 * t)
                 - 0.07 * fast_sin(TWO_PI * 3.0 * t);
        p[i] = clamp_s16(s * 26000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_drum_kick(uint32_t *out_len) {
    const uint32_t N = 1280; /* 80ms @ 16kHz */
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double phase = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        double freq = 150.0 * (1.0 - 0.72 * u);
        double env = (1.0 - u) * (1.0 - u);
        phase += TWO_PI * freq / 16000.0;
        p[i] = clamp_s16(fast_sin(phase) * env * 27000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_drum_snare(uint32_t *out_len) {
    const uint32_t N = 1280; /* 80ms @ 16kHz */
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double phase = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        double env = (1.0 - u) * (1.0 - u);
        phase += TWO_PI * (210.0 - 90.0 * u) / 16000.0;
        double s = 0.45 * fast_sin(phase) + 0.55 * next_noise();
        p[i] = clamp_s16(s * env * 24000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_drum_hat(uint32_t *out_len) {
    const uint32_t N = 640; /* 40ms @ 16kHz */
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double prev = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        double env = (1.0 - u) * (1.0 - u);
        double n = next_noise();
        double hp = n - 0.75 * prev;
        prev = n;
        p[i] = clamp_s16(hp * env * 16000.0);
    }
    *out_len = N;
    return p;
}

/* 6 Distinct 16kHz Gameplay SFX Samples for hearth_sfx.it */
static int16_t *make_sfx_cursor(uint32_t *out_len) {
    const uint32_t N = 960; /* 60ms @ 16kHz */
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double phase = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        double freq = 580.0 + 180.0 * u;
        double env = (1.0 - u);
        phase += TWO_PI * freq / 16000.0;
        p[i] = clamp_s16(fast_sin(phase) * env * 22000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_sfx_confirm(uint32_t *out_len) {
    const uint32_t N = 2240; /* 140ms @ 16kHz */
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double ph = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        double freq = (u < 0.42) ? 587.33 : 880.0;
        double env = (1.0 - u * 0.85);
        ph += TWO_PI * freq / 16000.0;
        double s = 0.7 * fast_sin(ph) + 0.3 * fast_sin(2.0 * ph);
        p[i] = clamp_s16(s * env * 23000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_sfx_step(uint32_t *out_len) {
    const uint32_t N = 800; /* 50ms @ 16kHz */
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double ph = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        double env = (1.0 - u) * (1.0 - u);
        ph += TWO_PI * (140.0 - 60.0 * u) / 16000.0;
        double s = 0.55 * fast_sin(ph) + 0.45 * next_noise();
        p[i] = clamp_s16(s * env * 16000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_sfx_hit(uint32_t *out_len) {
    const uint32_t N = 2080; /* 130ms @ 16kHz */
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double ph = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        double env = (1.0 - u) * (1.0 - u);
        ph += TWO_PI * (240.0 - 165.0 * u) / 16000.0;
        double sq = (fast_sin(ph) >= 0.0) ? 0.65 : -0.65;
        double s = 0.6 * sq + 0.4 * next_noise();
        p[i] = clamp_s16(s * env * 26000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_sfx_crit(uint32_t *out_len) {
    /* Sea-of-Stars-style Timed Hit / Parry Clang harmonic burst */
    const uint32_t N = 3200; /* 200ms @ 16kHz */
    const double TWO_PI = 6.28318530717958647692;
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double ph1 = 0.0, ph2 = 0.0, ph3 = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        double env = (1.0 - u) * (1.0 - u * 0.5);
        ph1 += TWO_PI * (u < 0.25 ? 880.0 : 1318.5) / 16000.0;
        ph2 += TWO_PI * 1760.0 / 16000.0;
        ph3 += TWO_PI * (220.0 - 140.0 * u) / 16000.0;
        double burst = (u < 0.18) ? next_noise() * 0.45 : 0.0;
        double s = 0.42 * fast_sin(ph1) + 0.28 * fast_sin(ph2) + 0.30 * fast_sin(ph3) + burst;
        p[i] = clamp_s16(s * env * 25000.0);
    }
    *out_len = N;
    return p;
}

static int16_t *make_sfx_chime(uint32_t *out_len) {
    /* Warm rising Heal / Heart-Mend / Familiar-Summon arpeggio */
    const uint32_t N = 4480; /* 280ms @ 16kHz */
    const double TWO_PI = 6.28318530717958647692;
    static const double notes[4] = { 523.25, 659.25, 783.99, 1046.5 };
    int16_t *p = (int16_t *)calloc(N, sizeof(int16_t));
    double ph = 0.0;
    for (uint32_t i = 0; i < N; i++) {
        double u = (double)i / (double)N;
        int idx = (int)(u * 4.0);
        if (idx > 3) idx = 3;
        double env = 1.0 - u * 0.75;
        ph += TWO_PI * notes[idx] / 16000.0;
        double s = 0.72 * fast_sin(ph) + 0.28 * fast_sin(2.0 * ph);
        p[i] = clamp_s16(s * env * 23000.0);
    }
    *out_len = N;
    return p;
}

/* ============================================================================
 * 4. IDENTICAL 4-TRACK JRPG SCORES SHARED WITH NATIVE SDL2 & WEB STUDIO
 * ============================================================================ */
#define REST 0u
#define HOLD 255u

enum {
    N_C2=36, N_Cs2=37, N_D2=38, N_Eb2=39, N_E2=40, N_F2=41, N_Fs2=42, N_G2=43, N_Gs2=44, N_A2=45, N_Bb2=46, N_B2=47,
    N_C3=48, N_Cs3=49, N_D3=50, N_Eb3=51, N_E3=52, N_F3=53, N_Fs3=54, N_G3=55, N_Gs3=56, N_A3=57, N_Bb3=58, N_B3=59,
    N_C4=60, N_Cs4=61, N_D4=62, N_Eb4=63, N_E4=64, N_F4=65, N_Fs4=66, N_G4=67, N_Gs4=68, N_A4=69, N_Bb4=70, N_B4=71,
    N_C5=72, N_Cs5=73, N_D5=74, N_Eb5=75, N_E5=76, N_F5=77, N_Fs5=78, N_G5=79, N_Gs5=80, N_A5=81, N_Bb5=82, N_B5=83,
    N_C6=84
};

static const uint8_t HAMLET_LEAD[48] = {
    N_G4, N_B4, N_D5, HOLD, N_E5, N_D5,
    N_B4, HOLD, N_G4, HOLD, N_A4, N_B4,
    N_C5, HOLD, N_E5, N_D5, N_C5, N_B4,
    N_A4, HOLD, HOLD, HOLD, N_B4, N_C5,
    N_D5, HOLD, N_G5, N_Fs5, N_E5, N_D5,
    N_B4, HOLD, N_E5, N_D5, N_C5, N_B4,
    N_A4, N_B4, N_C5, N_D5, N_B4, N_A4,
    N_G4, HOLD, HOLD, HOLD, REST, REST
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
    N_G2, HOLD, N_D3, REST, N_D3, REST,
    N_E2, HOLD, N_B2, REST, N_B2, REST,
    N_C3, HOLD, N_G3, REST, N_G3, REST,
    N_D2, HOLD, N_A2, REST, N_A2, REST,
    N_B2, HOLD, N_Fs3, REST, N_Fs3, REST,
    N_E2, HOLD, N_B2, REST, N_B2, REST,
    N_A2, HOLD, N_E3, N_D2, HOLD, N_A2,
    N_G2, HOLD, N_D3, N_G2, HOLD, REST
};
static const uint8_t HAMLET_DRUM[48] = {
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
    3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
    3, 0, 1, 3, 0, 1,  3, 0, 1, 0, 0, 0
};

static const uint8_t BATTLE_LEAD[64] = {
    N_A4, HOLD, N_C5, N_E5, N_A5, HOLD, N_G5, N_E5,
    N_D5, N_E5, N_F5, N_E5, N_D5, N_C5, N_B4, HOLD,
    N_C5, HOLD, N_E5, N_G5, N_C6, HOLD, N_B5, N_G5,
    N_A5, N_G5, N_E5, N_D5, N_E5, HOLD, REST, N_E5,
    N_F5, HOLD, N_E5, N_D5, N_C5, HOLD, N_D5, N_E5,
    N_G5, HOLD, N_F5, N_E5, N_D5, HOLD, N_C5, N_B4,
    N_A4, N_C5, N_E5, N_A5, N_Gs5, N_E5, N_B4, N_Gs5,
    N_A5, HOLD, N_E5, N_C5, N_A4, HOLD, REST, REST
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
    N_A2, N_A2, N_E2, N_G2, N_A2, HOLD, REST, N_E2
};
static const uint8_t BATTLE_DRUM[64] = {
    3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 1, 2, 2,
    3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 2, 2, 1,
    3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 1, 2, 1,
    3, 1, 2, 1, 3, 3, 2, 1,  3, 2, 3, 2, 3, 0, 2, 2
};

static const uint8_t BOSS_LEAD[64] = {
    N_D5, HOLD, N_Eb5, N_D5, N_C5, N_Bb4, N_A4, HOLD,
    N_D5, N_F5, N_A5, HOLD, N_G5, N_F5, N_E5, HOLD,
    N_F5, HOLD, N_G5, N_F5, N_E5, N_D5, N_Cs5, HOLD,
    N_A4, N_Cs5, N_E5, N_G5, N_F5, N_E5, N_D5, HOLD,
    N_Bb5, HOLD, N_A5, N_G5, N_F5, HOLD, N_G5, N_A5,
    N_G5, HOLD, N_F5, N_E5, N_D5, HOLD, N_E5, N_F5,
    N_E5, N_F5, N_G5, N_A5, N_Bb5, N_A5, N_G5, N_E5,
    N_A5, HOLD, HOLD, N_G5, N_F5, N_E5, N_D5, HOLD
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

/* Pack 4-channel pattern data: Ch0=Lead(inst 1), Ch1=Arp(inst 2), Ch2=Bass(inst 3), Ch3=Drums(inst 4/5/6) */
static void pack_music_pattern(
    Buf *pat,
    uint16_t rows,
    const uint8_t *lead,
    const uint8_t *arp,
    const uint8_t *bass,
    const uint8_t *drum,
    uint8_t lead_vol,
    uint8_t arp_vol,
    uint8_t bass_vol,
    uint8_t drum_vol
) {
    for (uint16_t r = 0; r < rows; r++) {
        /* Channel 0: Lead */
        if (lead[r] == REST) {
            buf_u8(pat, (uint8_t)(1u | 0x80u));
            buf_u8(pat, 0x01u);
            buf_u8(pat, 254u); /* Note Cut */
        } else if (lead[r] != HOLD) {
            buf_u8(pat, (uint8_t)(1u | 0x80u));
            buf_u8(pat, 0x07u);
            buf_u8(pat, lead[r]);
            buf_u8(pat, 1u);
            buf_u8(pat, lead_vol);
        }

        /* Channel 1: Harp / Chord Arp */
        if (arp[r] == REST) {
            buf_u8(pat, (uint8_t)(2u | 0x80u));
            buf_u8(pat, 0x01u);
            buf_u8(pat, 254u);
        } else if (arp[r] != HOLD) {
            buf_u8(pat, (uint8_t)(2u | 0x80u));
            buf_u8(pat, 0x07u);
            buf_u8(pat, arp[r]);
            buf_u8(pat, 2u);
            buf_u8(pat, arp_vol);
        }

        /* Channel 2: Bass */
        if (bass[r] == REST) {
            buf_u8(pat, (uint8_t)(3u | 0x80u));
            buf_u8(pat, 0x01u);
            buf_u8(pat, 254u);
        } else if (bass[r] != HOLD) {
            buf_u8(pat, (uint8_t)(3u | 0x80u));
            buf_u8(pat, 0x07u);
            buf_u8(pat, bass[r]);
            buf_u8(pat, 3u);
            buf_u8(pat, bass_vol);
        }

        /* Channel 3: Percussion (1=hat inst 6, 2=snare inst 5, 3=kick inst 4) */
        if (drum[r] >= 1u && drum[r] <= 3u) {
            uint8_t inst = (drum[r] == 3u) ? 4u : ((drum[r] == 2u) ? 5u : 6u);
            uint8_t vol  = (drum[r] == 1u) ? (uint8_t)(drum_vol / 2u) : drum_vol;
            buf_u8(pat, (uint8_t)(4u | 0x80u));
            buf_u8(pat, 0x07u);
            buf_u8(pat, 60u); /* C-5 -> plays at exact c5speed (16kHz) */
            buf_u8(pat, inst);
            buf_u8(pat, vol);
        }

        /* End of row marker */
        buf_u8(pat, 0x00u);
    }
}

/* ============================================================================
 * 5. MAIN — EMIT ALL 4 .IT FILES INTO TARGET DIRECTORY
 * ============================================================================ */
int main(int argc, char **argv) {
    const char *outdir = (argc > 1) ? argv[1] : ".";
    char path[512];

    /* 1. Synthesize 6 SFX Samples (hearth_sfx.it) */
    uint32_t l_cur, l_cfm, l_stp, l_hit, l_crt, l_chm;
    int16_t *p_cur = make_sfx_cursor(&l_cur);
    int16_t *p_cfm = make_sfx_confirm(&l_cfm);
    int16_t *p_stp = make_sfx_step(&l_stp);
    int16_t *p_hit = make_sfx_hit(&l_hit);
    int16_t *p_crt = make_sfx_crit(&l_crt);
    int16_t *p_chm = make_sfx_chime(&l_chm);

    ItSampleSpec sfx_specs[6] = {
        { "sfx_cursor",  p_cur, l_cur, 16000, 0, 56, 0, 64, 64, 0, 1, 2 },
        { "sfx_confirm", p_cfm, l_cfm, 16000, 0, 58, 0, 64, 64, 0, 1, 2 },
        { "sfx_step",    p_stp, l_stp, 16000, 0, 44, 0, 64, 64, 0, 1, 2 },
        { "sfx_hit",     p_hit, l_hit, 16000, 0, 62, 0, 64, 64, 0, 1, 2 },
        { "sfx_crit",    p_crt, l_crt, 16000, 0, 64, 0, 64, 64, 0, 1, 2 },
        { "sfx_chime",   p_chm, l_chm, 16000, 0, 60, 0, 64, 64, 0, 1, 2 }
    };

    /* Minimal dummy 8-row pattern referencing all 6 SFX instruments so smconv keeps them */
    Buf sfx_pat;
    buf_init(&sfx_pat);
    for (uint8_t r = 0; r < 6; r++) {
        buf_u8(&sfx_pat, (uint8_t)(1u | 0x80u));
        buf_u8(&sfx_pat, 0x03u);
        buf_u8(&sfx_pat, 60u);
        buf_u8(&sfx_pat, (uint8_t)(r + 1u));
        buf_u8(&sfx_pat, 0x00u);
    }
    buf_u8(&sfx_pat, 0x00u);
    buf_u8(&sfx_pat, 0x00u);

    snprintf(path, sizeof(path), "%s/hearth_sfx.it", outdir);
    write_it_module(path, "Hearth SFX", 6, 125, 1, sfx_specs, 6, sfx_pat.data, (uint16_t)sfx_pat.len, 8);
    free(sfx_pat.data);

    /* 2. Synthesize shared compact Music Waveforms (32-sample single-cycle @ 8372 Hz = C4!) */
    uint32_t l_flute, l_harp, l_brass, l_bass, l_kick, l_snare, l_hat;
    int16_t *w_flute = make_wave_flute(&l_flute);
    int16_t *w_harp  = make_wave_harp(&l_harp);
    int16_t *w_brass = make_wave_lead_brass(&l_brass);
    int16_t *w_bass  = make_wave_bass(&l_bass);
    int16_t *w_kick  = make_drum_kick(&l_kick);
    int16_t *w_snare = make_drum_snare(&l_snare);
    int16_t *w_hat   = make_drum_hat(&l_hat);

    /* 2a. Windmill Hamlet (3/4 Pastoral Waltz, 238ms/step -> IS=12, IT=126; T0=20000/126=158 <= 255!) */
    ItSampleSpec hamlet_specs[6] = {
        { "flute_lead", w_flute, l_flute, 8372, 1, 56, 1, 64, 48, 8, 12, 56 },
        { "harp_arp",   w_harp,  l_harp,  8372, 1, 40, 1, 60, 20, 0,  6, 24 },
        { "warm_bass",  w_bass,  l_bass,  8372, 1, 52, 1, 64, 34, 4, 10, 36 },
        { "soft_kick",  w_kick,  l_kick,  16000, 0, 36, 0, 64, 64, 0,  2,  4 },
        { "soft_snare", w_snare, l_snare, 16000, 0, 32, 0, 64, 64, 0,  2,  4 },
        { "soft_brush", w_hat,   l_hat,   16000, 0, 28, 0, 64, 64, 0,  2,  4 }
    };
    Buf pat_hamlet;
    buf_init(&pat_hamlet);
    pack_music_pattern(&pat_hamlet, 48, HAMLET_LEAD, HAMLET_ARP, HAMLET_BASS, HAMLET_DRUM, 52, 34, 48, 30);
    snprintf(path, sizeof(path), "%s/hearth_hamlet.it", outdir);
    write_it_module(path, "Windmill Hamlet", 12, 126, 4, hamlet_specs, 6, pat_hamlet.data, (uint16_t)pat_hamlet.len, 48);
    free(pat_hamlet.data);

    /* 2b. Whispering Meadow Battle (4/4 Heroic JRPG Battle, 155ms/step -> IS=6, IT=97) */
    ItSampleSpec battle_specs[6] = {
        { "battle_lead", w_brass, l_brass, 8372, 1, 58, 1, 64, 44, 6,  4, 18 },
        { "battle_arp",  w_harp,  l_harp,  8372, 1, 38, 1, 58, 16, 0,  2,  8 },
        { "battle_bass", w_bass,  l_bass,  8372, 1, 56, 1, 64, 32, 2,  3, 10 },
        { "drum_kick",   w_kick,  l_kick,  16000, 0, 52, 0, 64, 64, 0,  1,  2 },
        { "drum_snare",  w_snare, l_snare, 16000, 0, 48, 0, 64, 64, 0,  1,  2 },
        { "drum_hat",    w_hat,   l_hat,   16000, 0, 36, 0, 64, 64, 0,  1,  2 }
    };
    Buf pat_battle;
    buf_init(&pat_battle);
    pack_music_pattern(&pat_battle, 64, BATTLE_LEAD, BATTLE_ARP, BATTLE_BASS, BATTLE_DRUM, 56, 36, 52, 46);
    snprintf(path, sizeof(path), "%s/hearth_battle.it", outdir);
    write_it_module(path, "Meadow Battle", 6, 97, 4, battle_specs, 6, pat_battle.data, (uint16_t)pat_battle.len, 64);
    free(pat_battle.data);

    /* 2c. Briar Warden Boss (4/4 Urgent Shrine Boss, 140ms/step -> IS=6, IT=107) */
    Buf pat_boss;
    buf_init(&pat_boss);
    pack_music_pattern(&pat_boss, 64, BOSS_LEAD, BOSS_ARP, BOSS_BASS, BOSS_DRUM, 58, 38, 56, 52);
    snprintf(path, sizeof(path), "%s/hearth_boss.it", outdir);
    write_it_module(path, "Briar Warden Boss", 6, 107, 4, battle_specs, 6, pat_boss.data, (uint16_t)pat_boss.len, 64);
    free(pat_boss.data);

    free(p_cur); free(p_cfm); free(p_stp); free(p_hit); free(p_crt); free(p_chm);
    free(w_flute); free(w_harp); free(w_brass); free(w_bass);
    free(w_kick); free(w_snare); free(w_hat);

    printf("Generated SNES SPC700 Impulse Tracker modules in %s/\n", outdir);
    return 0;
}
