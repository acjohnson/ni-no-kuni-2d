/* ============================================================================
 * genesis_main.c — Project Hearth Sega Genesis / Mega Drive Host (SGDK)
 *
 * Bridges SGDK hardware VDP, 68000 joypad I/O, and PSG/FM sound directly
 * into the platform-agnostic pure C99 core (engine/core/hearth_core.c).
 *
 * Build via top-level Makefile (uses official SGDK Docker image automatically):
 *   make genesis
 * ============================================================================ */

#include <genesis.h>
#include "hearth_core.h"

/* Placed statically in Sega Genesis 64KB 68000 Work RAM (zero malloc!) */
static HearthState g_hearth;

static uint16_t read_genesis_pad(void) {
    u16 raw = JOY_readJoypad(JOY_1);
    uint16_t bits = 0;

    if (raw & BUTTON_UP)    bits |= PAD_UP;
    if (raw & BUTTON_DOWN)  bits |= PAD_DOWN;
    if (raw & BUTTON_LEFT)  bits |= PAD_LEFT;
    if (raw & BUTTON_RIGHT) bits |= PAD_RIGHT;
    if (raw & BUTTON_A)     bits |= PAD_A;
    if (raw & BUTTON_B)     bits |= PAD_B;
    if (raw & BUTTON_C)     bits |= PAD_X;
    if (raw & BUTTON_X)     bits |= PAD_L;      /* 6-Button Pad L Hot-Swap */
    if (raw & BUTTON_Z)     bits |= PAD_R;      /* 6-Button Pad R Hot-Swap */
    if (raw & BUTTON_Y)     bits |= PAD_Y;
    if (raw & BUTTON_START) bits |= PAD_START;
    if (raw & BUTTON_MODE)  bits |= PAD_SELECT;
    return bits;
}

static void upload_palettes_to_cram(void) {
    u16 cram[16];
    for (u16 p = 0; p < 4; p++) {
        for (u16 c = 0; c < 16; c++) {
            cram[c] = hearth_rgb_to_genesis_bgr333(HEARTH_PALETTES_RGB[p][c]);
        }
        PAL_setColors(p * 16, cram, 16, CPU);
    }
}

static void play_genesis_sfx(uint8_t sfx) {
    /* Minimal built-in PSG tone cues; replace with XGM PCM/VGM SFX banks */
    switch (sfx) {
        case SFX_CURSOR:
            PSG_setFrequency(0, 660);
            PSG_setEnvelope(0, PSG_ENVELOPE_MIN + 4);
            break;
        case SFX_CONFIRM:
        case SFX_HIT_CRITICAL:
        case SFX_LOCK_BREAK:
            PSG_setFrequency(0, 880);
            PSG_setEnvelope(0, PSG_ENVELOPE_MIN + 2);
            break;
        case SFX_HIT_NORMAL:
        case SFX_HURT:
            PSG_setNoise(PSG_NOISE_TYPE_WHITE, PSG_NOISE_FREQ_TONE3);
            PSG_setEnvelope(3, PSG_ENVELOPE_MIN + 3);
            break;
        default:
            break;
    }
}

int main(bool hard_reset) {
    (void)hard_reset;

    VDP_setScreenWidth320();
    VDP_setScreenHeight224();
    VDP_setPlaneSize(64, 32, TRUE);
    SPR_init();

    upload_palettes_to_cram();

    hearth_init(&g_hearth);
    g_hearth.tuning.ppu_mode = PPU_GENESIS_320x224;

    /* Initial HUD status banner on Plane Window */
    VDP_drawText("PROJECT HEARTH - SEGA GENESIS (SGDK)", 2, 1);

    while (TRUE) {
        uint16_t pad = read_genesis_pad();
        const HearthPpuPacket *ppu = hearth_step_60hz(&g_hearth, pad);

        /* Hardware Scroll Registers (Plane A = BG1, Plane B = BG2) */
        VDP_setHorizontalScroll(BG_A, -(ppu->cam_x - ppu->shake_x));
        VDP_setVerticalScroll(BG_A,    (ppu->cam_y - ppu->shake_y));
        VDP_setHorizontalScroll(BG_B, -(ppu->cam_x - ppu->shake_x));
        VDP_setVerticalScroll(BG_B,    (ppu->cam_y - ppu->shake_y));

        /* Flush Sound Driver Queue */
        for (uint8_t i = 0; i < ppu->sfx_count; i++) {
            play_genesis_sfx(ppu->sfx_queue[i]);
        }

        /* NOTE: Bind compiled rescomp SpriteDefinition tables here to stream
         * ppu->oam[0..ppu->oam_count-1] into SPR_setPosition / SPR_setAnim. */
        SPR_update();
        SYS_doVBlankProcess();
    }

    return 0;
}
