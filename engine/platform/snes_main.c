/* ============================================================================
 * snes_main.c — Project Hearth Super Nintendo Host (PVSnesLib / 65816)
 *
 * Bridges SNES hardware PPU Mode 1 (BG1/BG2 + OAM), 12-button SNES joypad,
 * and CGRAM BGR555 palettes directly into the pure C99 engine core.
 *
 * Build via top-level Makefile:
 *   PVSNESLIB_HOME=/path/to/pvsneslib make snes
 * ============================================================================ */

#include <snes.h>
#include "../core/hearth_core.h"

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
    for (u16 p = 0; p < 4; p++) {
        for (u16 c = 0; c < 16; c++) {
            u16 bgr555 = hearth_rgb_to_snes_bgr555(HEARTH_PALETTES_RGB[p][c]);
            setPaletteColor((u8)(p * 16 + c), bgr555);
        }
    }
}

int main(void) {
    /* Initialize SNES PPU in Mode 1 (256x224, two 4bpp BG layers + 128 OAM) */
    consoleInit();
    setMode(BG_MODE1, 0);
    bgSetDisable(2);

    upload_palettes_to_cgram();

    hearth_init(&g_hearth);
    g_hearth.tuning.ppu_mode = PPU_SNES_256x224;

    setScreenOn();

    while (1) {
        uint16_t pad = read_snes_pad();
        const HearthPpuPacket *ppu = hearth_step_60hz(&g_hearth, pad);

        /* Hardware PPU Scroll Registers */
        bgSetScroll(0, (u16)(ppu->cam_x - ppu->shake_x), (u16)(ppu->cam_y - ppu->shake_y));
        bgSetScroll(1, (u16)(ppu->cam_x - ppu->shake_x), (u16)(ppu->cam_y - ppu->shake_y));

        /* Stream Y-Sorted OAM Sprites to SNES Hardware OAM Buffer (already screen-relative) */
        for (u8 i = 0; i < ppu->oam_count && i < 64; i++) {
            const HearthOamSprite *o = &ppu->oam[i];
            int16_t sx = (int16_t)(o->x + ppu->shake_x);
            int16_t sy = (int16_t)(o->y + ppu->shake_y);
            if (sx < -16 || sx > 256 || sy < -16 || sy > 224) {
                oamSetVisible(i * 4, OBJ_HIDE);
            } else {
                oamSet(i * 4, (u16)sx, (u16)sy, 3, (o->flags & OAM_FLAG_FLIP_X) ? 1 : 0, 0, o->sprite_id * 4, 1);
                oamSetVisible(i * 4, OBJ_SHOW);
            }
        }
        for (u8 i = ppu->oam_count; i < 64; i++) {
            oamSetVisible(i * 4, OBJ_HIDE);
        }

        WaitForVBlank();
    }

    return 0;
}
