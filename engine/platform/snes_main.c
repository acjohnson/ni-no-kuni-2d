/* ============================================================================
 * snes_main.c — Project Hearth Super Nintendo Host (PVSnesLib / WDC 65816)
 *
 * Strictly ANSI C89/C90 compatible with PVSnesLib's 816-tcc compiler.
 * Bridges SNES PPU Mode 1 (BG1/BG2 + OAM), 12-button SNES joypad, and
 * CGRAM BGR555 palettes directly into the shared core engine.
 *
 * Build via top-level Makefile (auto-fetches PVSnesLib automatically!):
 *   make snes
 * ============================================================================ */

#ifndef HEARTH_TARGET_SNES
#define HEARTH_TARGET_SNES 1
#endif

#include <snes.h>
#include "hearth_core.h"

/* Placed statically in SNES 128KB Work RAM (zero malloc!) */
static HearthState g_hearth;

static uint16_t read_snes_pad(void) {
    u16 raw = padsCurrent(0);
    uint16_t bits = 0;

    if (raw & KEY_UP)     bits |= PAD_UP;
    if (raw & KEY_DOWN)   bits |= PAD_DOWN;
    if (raw & KEY_LEFT)   bits |= PAD_LEFT;
    if (raw & KEY_RIGHT)  bits |= PAD_RIGHT;
    if (raw & KEY_A)      bits |= PAD_A;
    if (raw & KEY_B)      bits |= PAD_B;
    if (raw & KEY_X)      bits |= PAD_X;
    if (raw & KEY_Y)      bits |= PAD_Y;
    if (raw & KEY_L)      bits |= PAD_L;
    if (raw & KEY_R)      bits |= PAD_R;
    if (raw & KEY_START)  bits |= PAD_START;
    if (raw & KEY_SELECT) bits |= PAD_SELECT;
    return bits;
}

static void upload_palettes_to_cgram(void) {
    u16 p, c, bgr555;
    for (p = 0; p < 4; p++) {
        for (c = 0; c < 16; c++) {
            bgr555 = hearth_rgb_to_snes_bgr555(HEARTH_PALETTES_RGB[p][c]);
            setPaletteColor((u8)(p * 16 + c), bgr555);
        }
    }
}

int main(void) {
    uint16_t pad;
    const HearthPpuPacket *ppu;
    u8 i;
    const HearthOamSprite *o;
    int16_t sx, sy;

    /* Initialize SNES PPU in Mode 1 (256x224, two 4bpp BG layers + OAM) */
    consoleInit();
    setMode(BG_MODE1, 0);
    bgSetDisable(2);

    upload_palettes_to_cgram();

    hearth_init(&g_hearth);
    g_hearth.tuning.ppu_mode = PPU_SNES_256x224;

    setScreenOn();

    while (1) {
        pad = read_snes_pad();
        ppu = hearth_step_60hz(&g_hearth, pad);

        /* Hardware PPU Scroll Registers */
        bgSetScroll(0, (u16)(ppu->cam_x - ppu->shake_x), (u16)(ppu->cam_y - ppu->shake_y));
        bgSetScroll(1, (u16)(ppu->cam_x - ppu->shake_x), (u16)(ppu->cam_y - ppu->shake_y));

        /* Stream Y-Sorted OAM Sprites to SNES Hardware OAM Buffer */
        for (i = 0; i < ppu->oam_count && i < 64; i++) {
            o = &ppu->oam[i];
            sx = (int16_t)(o->x + ppu->shake_x);
            sy = (int16_t)(o->y + ppu->shake_y);
            if (sx < -16 || sx > 256 || sy < -16 || sy > 224) {
                oamSetVisible((u16)(i * 4), OBJ_HIDE);
            } else {
                oamSet((u16)(i * 4), (u16)sx, (u16)sy, 3, (u8)((o->flags & OAM_FLAG_FLIP_X) ? 1 : 0), 0, (u16)(o->sprite_id * 4), 1);
                oamSetVisible((u16)(i * 4), OBJ_SHOW);
            }
        }
        for (i = ppu->oam_count; i < 64; i++) {
            oamSetVisible((u16)(i * 4), OBJ_HIDE);
        }

        WaitForVBlank();
    }

    return 0;
}
