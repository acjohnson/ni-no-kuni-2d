#!/usr/bin/env python3
"""
Project Hearth — 12-Scene Pure Hand-Pixelated 16-Bit Battle Backdrop Baker
(FF4 / FF5 / Dual Orb II 3/4 Isometric Perspective Style)

Generates unified, deterministic 16-bit pixel-art battle arenas across ALL 4 targets:
  1. engine/platform/battle_bg_rom.h     : Genesis 40x20 4bpp ROM tiles/maps + 4-ToD PAL0 RGB333,
                                           SDL2 40x20 60-color 4bpp tiles/maps + 4-ToD ARGB8888,
                                           and SNES extern declarations
  2. engine/platform/battle_bg_snes.asm  : SNES 32x20 60-color (4x15) 4bpp planar CHR + maps +
                                           4-ToD RGB555 CGRAM palettes in SUPERFREE LoROM banks
  3. src/host/battleBgData.ts            : Web Canvas 40x20 60-color 4bpp tiles/maps + 4-ToD palettes

Zero external runtime dependencies. Pure stdlib Python (fast, deterministic, reproducible).
"""

import base64
import math
import os
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_ROM_H = os.path.join(ROOT, "engine", "platform", "battle_bg_rom.h")
OUT_SNES_ASM = os.path.join(ROOT, "engine", "platform", "battle_bg_snes.asm")
OUT_WEB_TS = os.path.join(ROOT, "src", "host", "battleBgData.ts")

W, H = 320, 160          # 40 x 20 tiles (Web, SDL2, Sega Genesis)
TW, TH = W // 8, H // 8  # 40 x 20
SNES_X0 = 32             # Centered 256px crop (x=32..287) for SNES 32x20
SNES_W, SNES_H = 256, 160
SNES_TW, SNES_TH = 32, 20

MAX_TILES_HI = 448       # Web / SDL2 60-color 40x20 budget
MAX_TILES_GEN = 360      # Genesis 40x20 budget (fits easily inside 480-tile VRAM_CG_BASE; <=6 VBlanks!)
MAX_TILES_SNES = 304     # SNES 32x20 budget (<=9.7KB CHR + 1.3KB map + 512B pal = ~11.5KB/scene)

# Ordered 2x2 Bayer matrix (0..3) for classic crisp 16-bit FF4/FF5 pixel dithering
BAYER2 = ((0, 2), (3, 1))
# Ordered 4x4 Bayer matrix (0..15) for smooth wide gradients
BAYER4 = (
    ( 0,  8,  2, 10),
    (12,  4, 14,  6),
    ( 3, 11,  1,  9),
    (15,  7, 13,  5),
)

BIOME_NAMES = [
    "MEADOW",    # 0
    "SHRINE",    # 1
    "SEWER",     # 2
    "BOARHOLD",  # 3
    "DESERT",    # 4
    "PARADISE",  # 5
    "FOREST",    # 6
    "TOWN",      # 7
    "INTERIOR",  # 8
    "SEA",       # 9
    "FROST",     # 10
    "VOLCANO",   # 11
]

# Time-of-day modes: 0=MORNING, 1=AFTERNOON, 2=DUSK, 3=NIGHT
# Mode per scene: 'outdoor' (full 4-phase shift), 'window' (interior window + warm bounce),
#                 'dungeon' (preserves dramatic torch/magma lighting with subtle ambient shift)
TOD_MODE = [
    "outdoor",  # 0 MEADOW
    "outdoor",  # 1 SHRINE
    "dungeon",  # 2 SEWER
    "dungeon",  # 3 BOARHOLD
    "outdoor",  # 4 DESERT
    "outdoor",  # 5 PARADISE
    "outdoor",  # 6 FOREST
    "outdoor",  # 7 TOWN
    "window",   # 8 INTERIOR
    "outdoor",  # 9 SEA
    "outdoor",  # 10 FROST
    "dungeon",  # 11 VOLCANO
]


def clamp(v, lo=0, hi=255):
    return lo if v < lo else (hi if v > hi else int(v))


def h_rgb(hex_s):
    s = hex_s.lstrip("#")
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def rgb_to_bgr555(c):
    r, g, b = c
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def rgb_to_bgr333(c):
    r, g, b = c
    return (((b >> 5) & 7) << 9) | (((g >> 5) & 7) << 5) | (((r >> 5) & 7) << 1)


def rgb_to_argb8888(c):
    r, g, b = c
    return (0xFF << 24) | (r << 16) | (g << 8) | b


def snap_gen_rgb(c):
    r3 = (c[0] >> 5) & 7
    g3 = (c[1] >> 5) & 7
    b3 = (c[2] >> 5) & 7
    return (r3 * 36, g3 * 36, b3 * 36)


def color_dist2(a, b):
    dr = a[0] - b[0]
    dg = a[1] - b[1]
    db = a[2] - b[2]
    return dr * dr * 3 + dg * dg * 6 + db * db * 2


def mix_rgb(a, b, t):
    return (
        clamp(a[0] + (b[0] - a[0]) * t),
        clamp(a[1] + (b[1] - a[1]) * t),
        clamp(a[2] + (b[2] - a[2]) * t),
    )


def ihash(x, y, seed=0):
    n = (x * 374761393 + y * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return (n ^ (n >> 16)) & 0xFF


def dither_ramp(t, ramp_len, x, y, use4=False):
    """Map t in [0, 1] smoothly onto indices [0..ramp_len-1] using crisp Bayer dithering."""
    if t <= 0.0:
        return 0
    if t >= 1.0:
        return ramp_len - 1
    scaled = t * (ramp_len - 1)
    idx = int(scaled)
    frac = scaled - idx
    if use4:
        thr = (BAYER4[y & 3][x & 3] + 0.5) / 16.0
    else:
        thr = (BAYER2[y & 1][x & 1] + 0.5) / 4.0
    if frac > thr and idx < ramp_len - 1:
        idx += 1
    return idx


# =============================================================================
# 1. CURATED 4-PALETTE (60-COLOR) DEFINITIONS FOR ALL 12 SCENES
#    Each scene defines 4 sub-palettes (16 entries, index 0 = transparent/bg):
#      PAL0: Sky gradient, clouds/stars/aurora, distant haze & far horizon
#      PAL1: Mid-ground landmarks (hills, trees, buildings, arches, peaks, cliffs)
#      PAL2: 3/4 Isometric combat floor (ground, flagstones, planks, path, shadows)
#      PAL3: Foreground accents (water/canal/lagoon, fire/magma, runes, flowers, trim)
# =============================================================================
def make_pal(hex_list):
    assert len(hex_list) == 15, f"Need 15 colors, got {len(hex_list)}"
    return [(10, 12, 20)] + [h_rgb(x) for x in hex_list]


SCENE_BASE_PALS = {
    # 0: EMERALD MEADOW (Heartland Plains & Windmill Hamlet)
    "MEADOW": [
        # PAL0: Sky (1..5), Clouds (6..8), Distant Misty Mountains (9..11), Sun/Haze (12..15)
        make_pal([
            "#3B7BC2", "#5596D8", "#76B6E8", "#A2D4F2", "#D6ECFA",
            "#B8D4E6", "#E2F0F8", "#F8FCFF",
            "#547A96", "#6B92A6", "#88B0BC",
            "#F8E4A8", "#FFF6D2", "#447852", "#5A9260",
        ]),
        # PAL1: Rolling Hills & Trees (1..6), Windmill Stone & Roof (7..12), Sails/Wood (13..15)
        make_pal([
            "#1E482E", "#2C623A", "#3E7E46", "#549A52", "#72B660", "#96CE72",
            "#3C4250", "#586070", "#7A8496", "#A4AEBC", "#28406E", "#3E5E96",
            "#6A462C", "#9E6E44", "#E6D2A2",
        ]),
        # PAL2: 3/4 Iso Grass Arena (1..5), Angled Cobble/Dirt Road (6..11), Cast Shadows & Curb (12..15)
        make_pal([
            "#224A2C", "#32683A", "#468446", "#60A254", "#82BC64",
            "#563E2A", "#78583C", "#9C7652", "#BC946A", "#D6B284", "#EAD0A4",
            "#183622", "#6A7278", "#949CA0", "#B8C0C0",
        ]),
        # PAL3: Wildflowers, Fence, Warm Lanterns, Brook & Foreground Trim
        make_pal([
            "#183824", "#285834", "#407842", "#629C52",
            "#D84E58", "#F48A8A", "#F2C044", "#FFF098",
            "#5C9CE0", "#8CC8F4", "#EEF6FC",
            "#4A301E", "#7C5234", "#B07E52", "#E2BA82",
        ]),
    ],

    # 1: ANCIENT SHRINE (Hollow Glade & Waystone Sanctuaries)
    "SHRINE": [
        make_pal([
            "#1A223C", "#263456", "#384C76", "#526C96", "#7894BA",
            "#485A7C", "#6C82A6", "#9CB4D2",
            "#24324A", "#344864", "#4A627E",
            "#5CE2F8", "#A6F6FF", "#284846", "#3E6A60",
        ]),
        make_pal([
            "#18242C", "#243A3A", "#36564C", "#4E765E", "#6E9872", "#94BA88",
            "#2A303E", "#40485A", "#5A667A", "#7A889E", "#A0B0C4", "#C8D6E6",
            "#2A6A82", "#46A8C4", "#88E8FF",
        ]),
        make_pal([
            "#222836", "#323A4C", "#465066", "#5E6B84", "#7C8AA4", "#A0AEC4",
            "#1A202C", "#283834", "#3B564A", "#52745E",
            "#1E5872", "#3494B6", "#64D4F2", "#B8F6FF", "#DCE6F2",
        ]),
        make_pal([
            "#161C28", "#263240", "#3C4C5C", "#586A7C",
            "#247C9A", "#46B8D8", "#84ECFF", "#DCFCFF",
            "#2E5640", "#488058", "#74AA74",
            "#B89242", "#E8C668", "#FFF0A8", "#9CA8B8",
        ]),
    ],

    # 2: SUNKEN SEWERS (Lantern-Lit Vaulted Underground)
    "SEWER": [
        make_pal([
            "#081016", "#0E1A22", "#162630", "#203440", "#2C4654",
            "#142428", "#1E383C", "#2A4E52",
            "#3A4E58", "#506872", "#6A848E",
            "#8C5420", "#D48C34", "#F6C85A", "#FFF2B2",
        ]),
        make_pal([
            "#0C161C", "#16242C", "#223640", "#324A56", "#466270", "#607E8C",
            "#162E28", "#224A3E", "#346A56", "#4E8E72",
            "#342C24", "#5C4428", "#A86E2E", "#E6A842", "#FFE68C",
        ]),
        make_pal([
            "#101A20", "#1A2A32", "#283C46", "#38505C", "#4C6876", "#668492",
            "#0E2C2A", "#184642", "#266860", "#3E9284", "#64C2AC", "#9AE8D0",
            "#1E3C2C", "#305C40", "#4A8058",
        ]),
        make_pal([
            "#0A1418", "#14242A", "#223A40", "#36565C",
            "#145850", "#248476", "#42B69E", "#7CE4C8",
            "#784018", "#B86A24", "#EEAA3C", "#FFE480",
            "#284A38", "#407052", "#629872",
        ]),
    ],

    # 3: BOARHOLD FORTRESS (Obsidian Battlements & War Braziers)
    "BOARHOLD": [
        make_pal([
            "#180C12", "#2A141C", "#441E26", "#642C30", "#8A4038",
            "#381A22", "#562830", "#7A3C3C",
            "#241A22", "#3A2C34", "#524048",
            "#B84422", "#E4722C", "#F8B244", "#FFEE98",
        ]),
        make_pal([
            "#181218", "#282028", "#3C323C", "#544854", "#70626E", "#90808C",
            "#54141C", "#82202A", "#B2323A", "#DC524E",
            "#684624", "#9E7034", "#D8A44C", "#F8D87C", "#2A1820",
        ]),
        make_pal([
            "#1E181E", "#302830", "#443A42", "#5C5058", "#786A72", "#96868E",
            "#4A1A20", "#762A30", "#A63E40", "#CC5A52",
            "#6E4A28", "#A6763C", "#DDA854", "#F4CE7C", "#120E14",
        ]),
        make_pal([
            "#161014", "#281E24", "#403238", "#5E4C52",
            "#9A2818", "#D44E20", "#F68A2E", "#FFD058", "#FFF4B4",
            "#661A24", "#982A36", "#C8424A",
            "#886230", "#C49448", "#F0CA74",
        ]),
    ],

    # 4: SANDS OF BABANA (Golden Dunes & Ancient Sandstone Arch)
    "DESERT": [
        make_pal([
            "#2A64A6", "#4888C6", "#74B0DC", "#B6D4E0", "#F0D8A6",
            "#E6C288", "#F8E0AA", "#FFF4CC",
            "#C88E4A", "#DDA45C", "#EEBC72",
            "#FFE498", "#FFF8D4", "#8A6238", "#AE7E48",
        ]),
        make_pal([
            "#5E3A1E", "#84542C", "#AA723E", "#CE9252", "#E6B26C", "#F6CE88",
            "#22502E", "#34723C", "#4E964A", "#74BA5C",
            "#7A583A", "#A47C56", "#C8A074", "#E6C296", "#3E2614",
        ]),
        make_pal([
            "#784A24", "#9C6634", "#BE8244", "#DA9E58", "#EEBA70", "#FAD48C",
            "#FFF0B8", "#5A381C", "#8C6848", "#B48E68", "#D4B088", "#ECD0AA",
            "#482C16", "#2E6A7A", "#52A4B4",
        ]),
        make_pal([
            "#623C1E", "#8A582C", "#B67C40", "#DDA058",
            "#F2C476", "#FCE09C", "#FFF6CC",
            "#245E34", "#3C8846", "#64B45C",
            "#1E6E8A", "#3CA4C2", "#7CE0F4",
            "#A83434", "#E46A46",
        ]),
    ],

    # 5: CORAL PARADISE (Suncrest Coral Cove & Tropical Pier)
    "PARADISE": [
        make_pal([
            "#1E74C2", "#3498DC", "#5ABCEC", "#8EDCF6", "#CAF4FC",
            "#C4E8F4", "#E6F8FC", "#FFFFFF",
            "#1E80A8", "#2EA2C6", "#4CC6DC",
            "#7CE4F0", "#B8F6FA", "#2E7A56", "#4AA068",
        ]),
        make_pal([
            "#1A482A", "#286C38", "#3E9448", "#5EBC58", "#88DC6E", "#B2F088",
            "#583A24", "#7E5636", "#A4744A", "#C89866",
            "#1C6E9C", "#2C96C0", "#4CC2DC", "#84E6F4", "#D8FCFF",
        ]),
        make_pal([
            "#8E7048", "#B49262", "#D4B47E", "#EACE9C", "#F6E2BA", "#FFF2D6",
            "#66462C", "#8C623E", "#B08054", "#CE9E6E",
            "#2082AC", "#38B0D2", "#68DCF0", "#B8F6FC", "#EAFEFF",
        ]),
        make_pal([
            "#7A5E3C", "#A48254", "#CEAC78", "#EED0A0",
            "#1E7EA8", "#3AB2D4", "#72E2F2", "#D2FAFF",
            "#E0546E", "#F68696", "#F6BE42", "#FFF08C",
            "#2E7A44", "#50AA58", "#82D26E",
        ]),
    ],

    # 6: WHISPERING FOREST (Dense Layered Canopy, Moss & Autumn/Emerald Woods)
    "FOREST": [
        make_pal([
            "#2E628C", "#4882AC", "#6CA4C4", "#98C6D6", "#CBE2D8",
            "#7EA68C", "#A8CCB0", "#DAEED6",
            "#1E4234", "#2C5A44", "#3E7654",
            "#DCE8A4", "#F6F8C8", "#264A32", "#386842",
        ]),
        make_pal([
            "#142C1E", "#1E442A", "#2C6036", "#408044", "#5CA052", "#82C064",
            "#342218", "#503422", "#6E4A30", "#906442", "#B28258",
            "#9A4424", "#C8662E", "#E6943E", "#F6C25A",
        ]),
        make_pal([
            "#1A3824", "#265232", "#387040", "#50904E", "#70AE5E",
            "#3C281A", "#5A3C26", "#7A5436", "#9C704A", "#BE9062", "#D8B080",
            "#12281A", "#4A6242", "#74925E", "#A8C078",
        ]),
        make_pal([
            "#142C1C", "#22462C", "#34643A", "#4E8648",
            "#B84826", "#E07432", "#F4AE48", "#FFE082",
            "#6892B0", "#9CC8DC", "#E4F6FC",
            "#4A301E", "#764E32", "#A4744A", "#D6A672",
        ]),
    ],

    # 7: MARKET TOWN (Alderbrook, Sunhill Market, Whiskerburgh & Castle Gate)
    "TOWN": [
        make_pal([
            "#3A72B8", "#5690D2", "#7CB2E4", "#ACD4F2", "#DCF0FC",
            "#C0D6E8", "#E4F0FA", "#FAFDFE",
            "#546880", "#70849C", "#92A4B8",
            "#F6DC98", "#FFF6CC", "#486858", "#62866E",
        ]),
        make_pal([
            "#3A2218", "#5C3826", "#825238", "#AA704C",
            "#88342A", "#B24838", "#D2644C", "#EA8668",
            "#B8A890", "#D8C8AE", "#F0E4CC",
            "#3A4454", "#586478", "#7E8CA0", "#F8C854",
        ]),
        make_pal([
            "#363C48", "#4A5262", "#626C7E", "#7E889C", "#9EA8BA", "#C0C8D6",
            "#242832", "#4E3A2A", "#74563E", "#9C7858", "#C29C76",
            "#2C5634", "#467A48", "#E6B44A", "#FFF09E",
        ]),
        make_pal([
            "#262A34", "#3E4452", "#5A6274", "#7C8698",
            "#B62C36", "#E24E54", "#2E64A8", "#5492D6",
            "#B87624", "#E6AA3C", "#FFE074", "#FFF8C8",
            "#3C281C", "#6A4832", "#9A6E4E",
        ]),
    ],

    # 8: TIMBER INTERIOR (Cottages, Guildhalls, Windmill Mill, Bakeries & Archives)
    "INTERIOR": [
        # PAL0: Window glass (1..5 dynamically tinted by ToD!), Wall Plaster (6..10), Ceiling Beams (11..15)
        make_pal([
            "#4484C8", "#68AAE2", "#9CD0F4", "#D2ECFC", "#F6FCFF",
            "#786452", "#9A826A", "#BCA284", "#D8C2A2", "#EEDCC0",
            "#281810", "#3E2618", "#583824", "#764C32", "#966644",
        ]),
        # PAL1: Cedar Walls, Timber Trusses, Bookshelves & Stone Hearth
        make_pal([
            "#24140C", "#3C2214", "#58341E", "#76482A", "#98603A", "#BA7E50",
            "#3E3C42", "#58565E", "#76747E", "#9896A0",
            "#B83A22", "#E66E2C", "#F8B244", "#FFEE98", "#466E88",
        ]),
        # PAL2: 3/4 Isometric Cedar Floorboards (1..6) & Woven Crimson-Gold Parlor Rug (7..15)
        make_pal([
            "#2A180E", "#442818", "#623C24", "#825232", "#A46C44", "#C68C5E",
            "#4A141C", "#74202C", "#9E303E", "#C44650",
            "#8C6226", "#C4923A", "#EAC258", "#FCE492", "#1A100A",
        ]),
        # PAL3: Hearth Firelight, Brass Lamps, Books, Cushion Trim & Window Glow
        make_pal([
            "#22140C", "#3A2416", "#583822", "#784E32",
            "#A8321C", "#DF6424", "#FA9E34", "#FFD662", "#FFF6B8",
            "#2A5C44", "#448660", "#325892", "#5886C4",
            "#C8A464", "#ECE0C0",
        ]),
    ],

    # 9: OPEN SEA & CEDAR RAFT (Ocean Waters & Sunken Watchtower Coast)
    "SEA": [
        make_pal([
            "#225EA8", "#3A80C8", "#5EA6E2", "#92CCF2", "#CCEAF8",
            "#B6D6EA", "#DCF0FA", "#F8FCFF",
            "#264A6A", "#3A6484", "#52809C",
            "#FCE4A4", "#FFF8D4", "#18467A", "#26629E",
        ]),
        make_pal([
            "#103260", "#184A82", "#2468A4", "#368AC2", "#56B0DC", "#8ED6F0",
            "#CCF2FC", "#FFFFFF",
            "#364454", "#506072", "#708294",
            "#4A301C", "#724C2E", "#9C6E44", "#C89866",
        ]),
        # PAL2: 3/4 Isometric Cedar Raft Planks (1..7), Hemp Rope Lashings (8..11), Surrounding Sea (12..15)
        make_pal([
            "#321E12", "#4E301C", "#6E462A", "#8E5E3A", "#B07A4E", "#CE9868", "#E6B888",
            "#685434", "#947A4E", "#BE9E68", "#E2C890",
            "#123E72", "#1E5C98", "#3484BE", "#74C4E6",
        ]),
        make_pal([
            "#103666", "#1C548E", "#2E78B4", "#4CA4D6", "#82D0EE", "#C8F2FC", "#FFFFFF",
            "#462A18", "#74482A", "#A46C42", "#D29A68",
            "#A82C34", "#DC5254", "#E6B646", "#FFF09A",
        ]),
    ],

    # 10: FROSTMERE GLACIER (Northern Tundra, Ice Spires & Aurora)
    "FROST": [
        make_pal([
            "#162848", "#243E68", "#385A8A", "#567EA8", "#84AAC8",
            "#267872", "#42B69A", "#7CEAC2", "#C4FCE4",
            "#526E90", "#7694B4", "#A4C0DA",
            "#D8ECFA", "#F4FAFF", "#344C72",
        ]),
        make_pal([
            "#1C2C46", "#2C4464", "#426286", "#6086AC", "#88B0D2", "#B8D8EE", "#E6F4FC",
            "#183638", "#265050", "#3A6E68", "#588E84",
            "#547EA2", "#7EB0D4", "#B6E0F8", "#F0FAFF",
        ]),
        make_pal([
            "#466284", "#6484A6", "#88A8C8", "#B2CCE2", "#D6E8F6", "#F0F8FF",
            "#264A6C", "#3A6E94", "#589AC2", "#84C8E8", "#BEEAFA",
            "#2A3E5A", "#1E3048", "#6C98B8", "#9EC8E4",
        ]),
        make_pal([
            "#284260", "#426688", "#6692B6", "#96C2DE", "#CCE8F8", "#F4FCFF",
            "#247C78", "#44C0A6", "#84F2D0", "#D0FFEC",
            "#3C2A22", "#6A4C3A", "#D8A646", "#FFE484", "#1A2C44",
        ]),
    ],

    # 11: CINDERPEAK CALDERA (Volcanic Peaks, Magma Rivers & Obsidian Ridge)
    "VOLCANO": [
        make_pal([
            "#180A0E", "#2C1014", "#48181A", "#6C2420", "#963626",
            "#381E20", "#542C2A", "#783E34",
            "#26161A", "#3C2426", "#583434",
            "#C8461E", "#EE7624", "#FCB83E", "#FFF090",
        ]),
        make_pal([
            "#160E12", "#26181E", "#3A262C", "#52383E", "#6E4E54", "#8C686C",
            "#8A2012", "#C43C18", "#EE6A20", "#FCA832", "#FFE26C", "#FFF8B8",
            "#4A2824", "#703A2E", "#9A523C",
        ]),
        make_pal([
            "#181216", "#282026", "#3C3238", "#54484E", "#6E6066", "#8C7C82",
            "#6C1C14", "#A43018", "#DC581E", "#F8942E", "#FFD458", "#FFF4A8",
            "#100C10", "#48302E", "#66423C",
        ]),
        make_pal([
            "#161014", "#281E22", "#403036", "#5E4A50",
            "#962414", "#D0441A", "#F67C24", "#FFC448", "#FFF4A4",
            "#682C26", "#944236", "#C25E48",
            "#382A30", "#54424A", "#78626C",
        ]),
    ],
}


# =============================================================================
# 2. TIME-OF-DAY COLOR GRADING (Morning=0, Afternoon=1, Dusk=2, Night=3)
# =============================================================================
def grade_tod_color(rgb, mode, pal_idx, col_idx, tod):
    """Apply authentic 16-bit FF4/FF5 Time-of-Day color grading per palette role."""
    if tod == 1:
        # Afternoon = baseline signature daylight
        return rgb
    r, g, b = rgb

    if mode == "dungeon":
        # Dungeons preserve torch/magma/lantern drama with subtle ambient mood shift
        if tod == 0:  # Morning: slightly crisper cool stone bounce
            return (clamp(r * 0.98 + 4), clamp(g * 1.00 + 4), clamp(b * 1.05 + 6))
        if tod == 2:  # Dusk: warmer torch bounce
            return (clamp(r * 1.08 + 8), clamp(g * 0.96), clamp(b * 0.90))
        # Night: deeper shadows, brighter luminous embers/lanterns
        lum = (r * 3 + g * 6 + b) // 10
        if lum > 165:
            return (clamp(r * 1.06 + 6), clamp(g * 1.02 + 4), clamp(b * 0.98))
        return (clamp(r * 0.84), clamp(g * 0.86), clamp(b * 0.94 + 4))

    if mode == "window":
        # Interior: PAL0 indices 1..5 are the leaded-glass window sky; rest is cozy interior!
        if pal_idx == 0 and 1 <= col_idx <= 5:
            if tod == 0:  # Golden Dawn window
                dawn = [(164, 114, 146), (218, 148, 126), (244, 190, 134), (252, 224, 172), (255, 246, 214)]
                return dawn[col_idx - 1]
            if tod == 2:  # Crimson Sunset window
                dusk = [(86, 48, 96), (154, 68, 86), (218, 108, 74), (246, 162, 92), (254, 214, 136)]
                return dusk[col_idx - 1]
            if tod == 3:  # Moonlit Indigo window
                night = [(14, 22, 52), (22, 36, 78), (36, 56, 108), (64, 92, 148), (152, 184, 224)]
                return night[col_idx - 1]
        # Subtle warm hearth glow shift inside the room
        if tod == 0:
            return (clamp(r * 1.02 + 4), clamp(g * 1.00 + 2), clamp(b * 0.96))
        if tod == 2:
            return (clamp(r * 1.06 + 6), clamp(g * 0.94), clamp(b * 0.84))
        if tod == 3:
            lum = (r * 3 + g * 6 + b) // 10
            if lum > 175:  # Keep fireplace/lamp flames bright!
                return (clamp(r * 1.06 + 6), clamp(g * 1.02), clamp(b * 0.90))
            return (clamp(r * 0.78), clamp(g * 0.76), clamp(b * 0.86 + 8))
        return rgb

    # Outdoor scenes (full 4-phase atmospheric grading!)
    lum = (r * 3 + g * 6 + b) // 10
    is_luminous = (pal_idx == 3 and col_idx >= 6 and lum > 170) or (pal_idx == 0 and col_idx in (12, 13))

    if tod == 0:  # MORNING: Crisp golden-peach dawn horizon, cool dew shadows
        if pal_idx == 0 and col_idx <= 8:
            # Sky & clouds get soft rose-gold sunrise tint
            return (clamp(r * 1.10 + 18), clamp(g * 0.98 + 8), clamp(b * 0.90))
        return (clamp(r * 1.06 + 8), clamp(g * 1.01 + 4), clamp(b * 0.94))

    if tod == 2:  # DUSK: Dramatic amber-crimson sunset sky & warm violet shadows
        if pal_idx == 0 and col_idx <= 5:
            # Sky gradient -> deep plum zenith to blazing coral-gold horizon
            t = (col_idx - 1) / 4.0
            dusk_sky = [
                (44, 34, 82),
                (98, 48, 92),
                (172, 74, 86),
                (228, 124, 78),
                (250, 186, 112),
            ]
            return mix_rgb(rgb, dusk_sky[col_idx - 1], 0.78 + 0.12 * t)
        if pal_idx == 0 and 6 <= col_idx <= 8:
            # Clouds illuminated in peach-gold
            return (clamp(r * 1.06 + 18), clamp(g * 0.82 + 6), clamp(b * 0.68))
        if is_luminous:
            return (clamp(r * 1.08 + 10), clamp(g * 1.02 + 4), clamp(b * 0.92))
        return (clamp(r * 1.08 + 10), clamp(g * 0.84), clamp(b * 0.78 + 8))

    if tod == 3:  # NIGHT: Deep starlit indigo sky, cool silver-blue moonlight, glowing accents!
        if pal_idx == 0 and col_idx <= 5:
            night_sky = [
                (10, 16, 40),
                (16, 26, 62),
                (24, 40, 86),
                (36, 58, 112),
                (54, 82, 138),
            ]
            return mix_rgb(rgb, night_sky[col_idx - 1], 0.86)
        if pal_idx == 0 and 6 <= col_idx <= 8:
            # Moonlit silver-indigo clouds
            return (clamp(r * 0.42 + 12), clamp(g * 0.50 + 18), clamp(b * 0.74 + 28))
        if is_luminous:
            # Glowing runes, lantern sconces, stars, moon stay vibrant at night!
            return (clamp(r * 1.06 + 8), clamp(g * 1.06 + 8), clamp(b * 1.08 + 10))
        return (clamp(r * 0.46 + 6), clamp(g * 0.54 + 10), clamp(b * 0.78 + 20))

    return rgb


def build_tod_palettes(name):
    """Returns [4 ToD][4 sub-pals][16 RGB tuples]."""
    base = SCENE_BASE_PALS[name]
    mode = TOD_MODE[BIOME_NAMES.index(name)]
    out = []
    for tod in range(4):
        tp = []
        for pi in range(4):
            pal = [base[pi][0]]
            for ci in range(1, 16):
                pal.append(grade_tod_color(base[pi][ci], mode, pi, ci, tod))
            tp.append(pal)
        out.append(tp)
    return out


# =============================================================================
# 3. HAND-PIXELATED 16-BIT SCENE CANVAS & PRIMITIVES
# =============================================================================
class PixelStage:
    """320x160 indexed pixel buffer storing (pal_idx 0..3, col_idx 1..15)."""
    def __init__(self):
        self.pal = bytearray(W * H)
        self.col = bytearray(W * H)

    def put(self, x, y, p, c):
        if 0 <= x < W and 0 <= y < H:
            i = y * W + x
            self.pal[i] = p
            self.col[i] = c

    def get(self, x, y):
        if 0 <= x < W and 0 <= y < H:
            i = y * W + x
            return (self.pal[i], self.col[i])
        return (0, 1)

    def fill_rect(self, x0, y0, rw, rh, p, c):
        x1 = min(W, x0 + rw)
        y1 = min(H, y0 + rh)
        x0 = max(0, x0)
        y0 = max(0, y0)
        for y in range(y0, y1):
            row = y * W
            for x in range(x0, x1):
                self.pal[row + x] = p
                self.col[row + x] = c

    def draw_sky_gradient(self, y0, y1, p=0, cols=(1, 2, 3, 4, 5)):
        span = max(1, y1 - y0 - 1)
        for y in range(max(0, y0), min(H, y1)):
            t = (y - y0) / span
            row = y * W
            for x in range(W):
                ci = cols[dither_ramp(t, len(cols), x, y, use4=True)]
                self.pal[row + x] = p
                self.col[row + x] = ci

    def draw_cloud(self, cx, cy, rx, ry, p=0, c_sh=6, c_mid=7, c_hi=8):
        for y in range(max(0, cy - ry - 4), min(86, cy + ry + 3)):
            for x in range(max(0, cx - rx - 8), min(W, cx + rx + 8)):
                # Multi-puff classic 16-bit pixel cloud silhouette
                d1 = ((x - cx) / max(1, rx)) ** 2 + ((y - cy) / max(1, ry)) ** 2
                d2 = ((x - (cx - rx * 0.55)) / max(1, rx * 0.58)) ** 2 + ((y - (cy + 1)) / max(1, ry * 0.72)) ** 2
                d3 = ((x - (cx + rx * 0.52)) / max(1, rx * 0.62)) ** 2 + ((y - (cy + 1)) / max(1, ry * 0.75)) ** 2
                d4 = ((x - (cx + rx * 0.12)) / max(1, rx * 0.55)) ** 2 + ((y - (cy - ry * 0.42)) / max(1, ry * 0.68)) ** 2
                if min(d1, d2, d3, d4) <= 1.0 and y <= cy + ry - 1:
                    rel_y = (y - (cy - ry)) / max(1.0, ry * 2.0)
                    if rel_y < 0.38:
                        c = c_hi
                    elif rel_y < 0.72:
                        c = c_mid if ((x + y) & 1 or rel_y > 0.48) else c_hi
                    else:
                        c = c_sh
                    self.put(x, y, p, c)

    def draw_starfield(self, ymax=56, seed=11, p=0, c_dim=12, c_hi=13):
        for i in range(28):
            sx = ihash(i, 1, seed) * W // 256
            sy = 3 + (ihash(i, 2, seed) % max(1, ymax - 5))
            self.put(sx, sy, p, c_hi if (i % 3 == 0) else c_dim)
            if i % 7 == 0:
                self.put(sx - 1, sy, p, c_dim)
                self.put(sx + 1, sy, p, c_dim)
                self.put(sx, sy - 1, p, c_dim)
                self.put(sx, sy + 1, p, c_dim)

    def draw_ridge(self, y_base, amp, freq1, freq2, phase, p, c_top, c_mid, c_bot, y_floor=88):
        for x in range(W):
            h = int(amp * (0.55 + 0.45 * math.sin(x * freq1 + phase) * 0.7 + 0.3 * math.cos(x * freq2 + phase * 1.7)))
            top_y = y_base - h
            for y in range(max(0, top_y), min(H, y_floor)):
                dy = y - top_y
                if dy <= 2:
                    c = c_top
                elif dy <= 8:
                    c = c_top if (dy <= 4 and ((x + y) & 1) == 0) else c_mid
                else:
                    c = c_mid if (dy <= 14 and ((x + y) & 1) == 0) else c_bot
                self.put(x, y, p, c)

    def draw_pine_tree(self, cx, base_y, height, p=1, c_dk=1, c_md=2, c_lt=4, c_hi=5, trunk_c=None):
        tiers = max(3, height // 8)
        if trunk_c is not None:
            self.fill_rect(cx - 2, base_y - 6, 4, 7, p, trunk_c)
        for t in range(tiers):
            ty = base_y - 4 - int((t + 1) * (height - 4) / tiers)
            tw = int((tiers - t) * (height * 0.14) + 3)
            th = max(5, height // tiers + 3)
            for dy in range(th):
                span = max(1, int(tw * (dy + 1) / th))
                yy = ty + dy
                for dx in range(-span, span + 1):
                    xx = cx + dx
                    if dx < -span // 3:
                        c = c_hi if dy < th // 2 else c_lt
                    elif dx < span // 3:
                        c = c_lt if ((xx + yy) & 1) == 0 else c_md
                    else:
                        c = c_dk
                    self.put(xx, yy, p, c)

    def draw_oak_tree(self, cx, base_y, crown_r, trunk_h, p=1, c_dk=1, c_md=2, c_lt=4, c_hi=5, c_tr_d=7, c_tr_l=9):
        # Trunk with roots & bark shading
        for dy in range(trunk_h):
            yy = base_y - dy
            tw = 3 + (2 if dy < 4 else 0)
            for dx in range(-tw, tw + 1):
                self.put(cx + dx, yy, p, c_tr_l if dx <= -1 else c_tr_d)
        # Lush multi-lobe 16-bit Mana/FF5 foliage crown
        cy = base_y - trunk_h - crown_r // 2 + 2
        lobes = [
            (0, -crown_r // 3, crown_r * 0.85),
            (-crown_r * 0.65, 1, crown_r * 0.72),
            (crown_r * 0.65, 2, crown_r * 0.70),
            (0, crown_r * 0.2, crown_r * 0.78),
        ]
        for y in range(max(0, cy - crown_r - 6), min(H, cy + crown_r + 4)):
            for x in range(max(0, cx - crown_r - 8), min(W, cx + crown_r + 8)):
                inside = False
                for lx, ly, lr in lobes:
                    if (x - (cx + lx)) ** 2 + (y - (cy + ly)) ** 2 <= lr * lr:
                        inside = True
                        break
                if inside:
                    # Top-left sunlight + crisp pixel leaf cluster dither
                    lit = (-(x - cx) * 0.55 - (y - cy) * 0.85) / max(1.0, crown_r)
                    n = (ihash(x >> 1, y >> 1, cx) / 255.0 - 0.5) * 0.35
                    v = lit + n
                    if v > 0.38:
                        c = c_hi
                    elif v > 0.05:
                        c = c_lt
                    elif v > -0.35:
                        c = c_md
                    else:
                        c = c_dk
                    self.put(x, y, p, c)

    def draw_iso_floor_base(self, y_top, y_bot, fn):
        """Evaluate fn(x, y, iso_u, iso_v, z) -> (pal, col) for a 3/4 isometric perspective stage."""
        for y in range(y_top, y_bot):
            z = (y - y_top + 6.0) / max(1.0, (y_bot - y_top + 6.0))
            # True perspective depth + angled 3/4 isometric shear (angled toward upper-right!)
            persp = 0.52 + 0.88 * z
            iso_v = 118.0 / (y - (y_top - 32.0))
            row = y * W
            for x in range(W):
                iso_u = (x - 160.0) / persp + (y - y_top) * 0.44
                p, c = fn(x, y, iso_u, iso_v, z)
                self.pal[row + x] = p
                self.col[row + x] = c


# =============================================================================
# 4. ALL 12 HAND-PIXELATED 3/4 ISOMETRIC BATTLE SCENES
# =============================================================================
def draw_scene_meadow():
    st = PixelStage()
    # Sky & puffy sunlit clouds
    st.draw_sky_gradient(0, 86, 0, (1, 2, 3, 4, 5))
    st.draw_cloud(54, 18, 28, 8)
    st.draw_cloud(162, 13, 34, 9)
    st.draw_cloud(266, 22, 26, 7)
    # Distant misty blue mountain range
    st.draw_ridge(68, 22, 0.022, 0.053, 0.4, 0, 11, 10, 9, 86)
    # Mid-ground lush emerald hills
    st.draw_ridge(78, 16, 0.028, 0.061, 1.9, 1, 5, 3, 2, 88)
    st.draw_ridge(84, 10, 0.036, 0.075, 3.2, 1, 4, 2, 1, 90)
    # Iconic Windmill Hamlet tower on right-center hill (visible in both 320px & 256px SNES crop!)
    wx, wy = 224, 72
    for dy in range(28):
        hw = 5 + dy // 5
        for dx in range(-hw, hw + 1):
            c = 10 if dx < -hw // 3 else (9 if dx < hw // 3 else 7)
            if (dy % 5 == 0) and ((dx + dy) & 1):
                c = 8
            st.put(wx + dx, wy - 26 + dy, 1, c)
    # Conical slate-blue windmill cap
    for dy in range(8):
        hw = 1 + dy
        for dx in range(-hw, hw + 1):
            st.put(wx + dx, wy - 33 + dy, 1, 12 if dx <= 0 else 11)
    # Windmill timber sails (crisp diagonal pixel arms)
    for d in range(-14, 15):
        st.put(wx + d, wy - 22 + d, 1, 14)
        st.put(wx + d, wy - 22 - d, 1, 14)
        if abs(d) > 3:
            st.put(wx + d + 1, wy - 22 + d, 1, 15)
            st.put(wx + d, wy - 21 + d, 1, 15)
            st.put(wx + d + 1, wy - 22 - d, 1, 15)
            st.put(wx + d, wy - 21 - d, 1, 15)
    # Cozy distant oak trees framing left & far right
    st.draw_oak_tree(62, 82, 14, 10, 1, 1, 2, 4, 5, 13, 14)
    st.draw_oak_tree(96, 84, 11, 8, 1, 1, 2, 3, 5, 13, 14)
    st.draw_oak_tree(276, 82, 13, 9, 1, 1, 2, 4, 5, 13, 14)
    # Wooden pasture fence along horizon ledge (y=80..87)
    for x in range(0, W):
        if x % 20 in (0, 1, 2):
            for yy in range(79, 88):
                st.put(x, yy, 3, 14 if x % 20 == 0 else 12)
        else:
            st.put(x, 81, 3, 14)
            st.put(x, 82, 3, 13)
            st.put(x, 85, 3, 13)
    # 3/4 Isometric Meadow & Diagonal Cobble/Dirt Country Path
    def floor_fn(x, y, u, v, z):
        if y >= 153:
            # Darkened grass foreground anchor
            return (2, 12 if (y >= 156 or ((x + y) & 1)) else 1)
        # Angled 3/4 country lane running diagonally across the combat arena
        road_center = -12.0 + (v - 2.0) * 14.0
        dist_road = abs(u - road_center)
        wobble = math.sin(v * 2.2) * 5.0
        if dist_road < 56.0 + wobble:
            # Tactile dirt road with recessed pebble stones & sunlit ruts
            tu = int(math.floor(u * 0.35))
            tv = int(math.floor(v * 5.5))
            h = ihash(tu, tv, 7)
            edge = dist_road / (56.0 + wobble)
            if edge > 0.86:
                # Stone curb & grass-dirt transition
                return (2, 14 if (h & 3) == 0 else (7 if ((x + y) & 1) else 2))
            if (h & 15) == 0:
                return (2, 13)  # Small embedded pebble
            band = dither_ramp(0.25 + 0.55 * (1.0 - edge) + ((h & 7) - 3.5) * 0.04, 4, x, y)
            return (2, (7, 8, 9, 10)[band])
        # Lush 3/4 meadow grass with angled blades & scattered wildflowers
        tu = int(math.floor(u * 0.45))
        tv = int(math.floor(v * 6.5))
        h = ihash(tu, tv, 19)
        if y < 92:
            return (2, 2 if ((x + y) & 1) else 3)
        if (h == 17 or h == 91) and 98 <= y <= 146:
            return (3, 7 if (h == 17) else 5)  # Tiny wildflower dot
        shade = dither_ramp(0.18 + 0.62 * z + ((h & 7) - 3.5) * 0.05, 4, x, y)
        return (2, (2, 3, 4, 5)[shade])

    st.draw_iso_floor_base(87, H, floor_fn)
    return st


def draw_scene_shrine():
    st = PixelStage()
    st.draw_sky_gradient(0, 86, 0, (1, 2, 3, 4, 5))
    st.draw_starfield(44, seed=31, p=0, c_dim=7, c_hi=13)
    st.draw_cloud(76, 24, 30, 7, 0, 6, 7, 8)
    st.draw_cloud(236, 18, 34, 8, 0, 6, 7, 8)
    # Distant misty alpine peaks & sacred pine forest
    st.draw_ridge(68, 26, 0.025, 0.068, 1.1, 0, 11, 10, 9, 86)
    for tx in range(10, W, 22):
        st.draw_pine_tree(tx, 84, 20 + (ihash(tx, 3, 5) % 12), 1, 1, 2, 3, 4)
    # Towering ancient mossy rune monoliths framing the shrine sanctuary
    monoliths = [(48, 86, 16, 54), (104, 84, 14, 42), (214, 84, 14, 46), (268, 86, 18, 58)]
    for mx, my, mw, mh in monoliths:
        for dy in range(mh):
            yy = my - mh + dy
            taper = 2 if dy < 5 else 0
            for dx in range(-mw // 2 + taper, mw // 2 - taper):
                c = 11 if dx < -mw // 4 else (9 if dx < mw // 5 else 7)
                if (dy % 7 == 0) or (dx == 0 and dy % 4 == 0):
                    c = 8
                # Moss cap on top of monoliths
                if dy < 6 and ((dx + dy) & 1) == 0:
                    c = 4
                st.put(mx + dx, yy, 1, c)
        # Glowing cyan vertical rune carvings down monolith face
        for ry in range(my - mh + 12, my - 10, 6):
            st.put(mx - 1, ry, 1, 14)
            st.put(mx, ry, 1, 15)
            st.put(mx, ry + 1, 1, 14)
            st.put(mx + 1, ry + 2, 1, 13)
    # Recessed sanctuary parapet wall (y=82..89)
    for y in range(82, 90):
        for x in range(W):
            is_seam = (y in (82, 89)) or ((x + (y // 4) * 8) % 16 == 0)
            st.put(x, y, 2, 7 if is_seam else (5 if y < 85 else 3))

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 7 if y >= 156 else 1)
        # Centered 3/4 isometric glowing arcane waystone rune circle!
        ex = (x - 160.0) / 78.0
        ey = (y - 122.0) / 21.0
        r = math.hypot(ex, ey)
        ang = math.atan2(ey, ex)
        if 0.88 <= r <= 1.00 or 0.58 <= r <= 0.65:
            glow = 7 if (int((ang + 3.14) * 6.0) & 1) else 6
            return (3, glow)
        if 0.65 < r < 0.88 and abs(math.sin(ang * 4.0)) < 0.08:
            return (3, 6)
        # Angled 3/4 carved slate flagstones with mossy mortar joints
        fu = u * 0.085
        fv = v * 2.2
        iu = int(math.floor(fu))
        iv = int(math.floor(fv))
        frac_u = fu - iu
        frac_v = fv - iv
        if frac_u < 0.08 or frac_v < 0.11:
            moss = (ihash(iu, iv, 41) & 3) == 0
            return (2, 9 if moss else 7)
        if frac_u < 0.16 or frac_v < 0.19:
            return (2, 5)  # Beveled stone highlight
        h = ihash(iu, iv, 43)
        if (h & 7) == 0 and frac_u > 0.72:
            return (2, 9)  # Creeping shrine moss
        shade = 2 + ((h + (1 if z > 0.55 else 0)) % 3)
        return (2, shade)

    st.draw_iso_floor_base(89, H, floor_fn)
    return st


def draw_scene_sewer():
    st = PixelStage()
    # Deep vaulted ceiling shadow gradient
    st.draw_sky_gradient(0, 86, 0, (1, 1, 2, 3, 4))
    # Perspective receding brick wall masonry (y=0..86)
    for y in range(0, 86):
        row_b = y // 6
        for x in range(W):
            bx = (x + (row_b & 1) * 7) % 14
            by = y % 6
            if bx == 0 or by == 0:
                st.put(x, y, 1, 1)
            else:
                h = ihash(x // 14, row_b, 9)
                st.put(x, y, 1, 2 + (h % 3))
    # 5 Grand Arched Vault Alcoves with iron drainage grates & glowing wall lanterns
    for ax in (28, 94, 160, 226, 292):
        # Dark arched recess
        for y in range(22, 86):
            for x in range(ax - 22, ax + 23):
                dx = x - ax
                in_arch = (y >= 42) or ((dx * dx) / (22.0 * 22.0) + ((y - 42) * (y - 42)) / (20.0 * 20.0) <= 1.0)
                if in_arch:
                    # Stone arch ring border
                    dist_ring = math.hypot(dx / 22.0, min(0, y - 42) / 20.0) if y < 42 else abs(dx) / 22.0
                    if dist_ring > 0.84:
                        st.put(x, y, 1, 5 if dx < 0 else 4)
                    else:
                        # Iron sluice bars inside arch
                        is_bar = (abs(dx) % 6 == 0) or (y in (54, 72))
                        st.put(x, y, 1, 3 if is_bar else 1)
        # Hanging lantern sconce between arches
        lx = ax + 33
        if lx < W - 4:
            st.fill_rect(lx - 1, 34, 3, 10, 1, 11)
            st.fill_rect(lx - 2, 42, 5, 7, 3, 10)
            st.fill_rect(lx - 1, 43, 3, 5, 3, 11)
            st.put(lx, 45, 3, 12)
            # Warm lantern halo on wet bricks
            for gy in range(36, 56):
                for gx in range(lx - 10, lx + 11):
                    d = math.hypot(gx - lx, gy - 45)
                    if 4 < d < 10 and ((gx + gy) & 1) == 0:
                        st.put(gx, gy, 1, 13 if d < 7 else 12)

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 1)
        # Glowing emerald-teal subterranean canal along back edge (y=86..101)
        if y < 101:
            if y in (86, 87, 99, 100):
                return (2, 5 if ((x + y) & 3) else 4)  # Wet stone canal curb
            wave = math.sin(x * 0.14 + y * 0.6)
            sh = dither_ramp(0.25 + 0.55 * ((y - 88) / 11.0) + wave * 0.18, 5, x, y)
            return (2, (7, 8, 9, 10, 11)[sh])
        # Angled 3/4 wet stone catwalk slabs with mossy joints & puddle sheen
        fu = u * 0.08
        fv = v * 2.4
        iu, iv = int(math.floor(fu)), int(math.floor(fv))
        frac_u, frac_v = fu - iu, fv - iv
        if frac_u < 0.08 or frac_v < 0.11:
            return (2, 14 if (ihash(iu, iv, 51) & 3) == 0 else 1)
        if frac_u < 0.15 or frac_v < 0.18:
            return (2, 5)
        h = ihash(iu, iv, 53)
        # Subtle glowing emerald water reflections in floor puddles
        if (h & 7) == 1 and 0.25 < frac_u < 0.75 and 0.3 < frac_v < 0.7:
            return (2, 10 if ((x + y) & 1) else 9)
        return (2, 2 + (h % 3))

    st.draw_iso_floor_base(86, H, floor_fn)
    return st


def draw_scene_boarhold():
    st = PixelStage()
    # Stormy crimson-obsidian twilight sky
    st.draw_sky_gradient(0, 86, 0, (1, 2, 3, 4, 5))
    st.draw_cloud(84, 18, 34, 8, 0, 6, 7, 8)
    st.draw_cloud(230, 14, 38, 9, 0, 6, 7, 8)
    # Massive fortress battlements & spiked towers across the horizon
    for x in range(W):
        cren = 10 if ((x // 12) & 1) == 0 else 0
        top_y = 38 - cren
        for y in range(top_y, 88):
            bx = (x + (y // 8) * 6) % 16
            by = y % 8
            c = 1 if (bx == 0 or by == 0) else (4 if y < 52 else 3)
            st.put(x, y, 1, c)
    # Central iron portcullis archway (x=134..186)
    for y in range(44, 88):
        for x in range(134, 187):
            dx = x - 160
            in_gate = (y >= 60) or ((dx * dx) / (26.0 * 26.0) + ((y - 60) * (y - 60)) / (16.0 * 16.0) <= 1.0)
            if in_gate:
                bar = (abs(dx) % 7 == 0) or (y in (58, 70, 82))
                st.put(x, y, 1, 5 if bar else 1)
    # Tattered crimson-and-gold war banners hanging on fortress wall
    for bx in (86, 234):
        for dy in range(34):
            w = 9 - (dy // 14)
            for dx in range(-w, w + 1):
                if dy > 26 and abs(dx) < (dy - 26):
                    continue  # Swallowtail banner cut
                c = 13 if abs(dx) >= w - 1 else (9 if dx < 0 else 8)
                if dy in (12, 13, 14) and abs(dx) <= 3:
                    c = 14  # Gold boar crest emblem
                st.put(bx + dx, 46 + dy, 1, c)
    # Roaring iron fire braziers flanking the arena
    for fx in (58, 122, 198, 262):
        st.fill_rect(fx - 5, 76, 11, 10, 1, 2)
        st.fill_rect(fx - 6, 74, 13, 3, 1, 5)
        for dy in range(14):
            hw = max(1, 5 - dy // 3)
            for dx in range(-hw, hw + 1):
                yy = 73 - dy
                c = 9 if (dy < 4 and abs(dx) <= 2) else (8 if dy < 8 else (7 if dy < 11 else 6))
                if ((dx + dy) & 1) == 0 or dy < 6:
                    st.put(fx + dx, yy, 3, c)

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 15 if y >= 156 else 1)
        if y < 92:
            return (2, 5 if (x % 8 != 0) else 1)  # Fortress stone rampart curb
        # Crimson-gold royal carpet runner angled along 3/4 arena center
        if abs(u) < 42.0:
            edge = abs(u) / 42.0
            if edge > 0.88:
                return (2, 13 if ((x + y) & 1) else 12)  # Gilded trim
            pat = 9 if ((int(u * 0.4) + int(v * 5.0)) & 3) == 0 else 8
            return (2, pat)
        # Angled 3/4 obsidian basalt flagstones
        fu, fv = u * 0.08, v * 2.2
        iu, iv = int(math.floor(fu)), int(math.floor(fv))
        frac_u, frac_v = fu - iu, fv - iv
        if frac_u < 0.08 or frac_v < 0.11:
            return (2, 15)
        if frac_u < 0.15 or frac_v < 0.18:
            return (2, 5)
        h = ihash(iu, iv, 61)
        return (2, 2 + (h % 3))

    st.draw_iso_floor_base(88, H, floor_fn)
    return st


def draw_scene_desert():
    st = PixelStage()
    st.draw_sky_gradient(0, 86, 0, (1, 2, 3, 4, 5))
    # Blazing golden Babana sun with pixel corona
    sx, sy = 232, 22
    for dy in range(-14, 15):
        for dx in range(-14, 15):
            d = math.hypot(dx, dy)
            if d <= 8.5:
                st.put(sx + dx, sy + dy, 0, 13)
            elif d <= 11.5:
                st.put(sx + dx, sy + dy, 0, 12)
            elif d <= 14.0 and ((dx + dy) & 1) == 0:
                st.put(sx + dx, sy + dy, 0, 7)
    # Layered sweeping desert sand dunes (distant amber -> warm gold)
    st.draw_ridge(70, 20, 0.020, 0.041, 0.8, 0, 11, 10, 9, 86)
    st.draw_ridge(80, 14, 0.026, 0.055, 2.7, 1, 6, 4, 3, 88)
    # Weathered Babana sandstone ruin archway on mid-left ridge
    ax, ay = 88, 82
    st.fill_rect(ax - 18, ay - 30, 8, 30, 1, 13)
    st.fill_rect(ax - 18, ay - 30, 3, 30, 1, 14)
    st.fill_rect(ax + 10, ay - 28, 8, 28, 1, 12)
    st.fill_rect(ax + 10, ay - 28, 3, 28, 1, 13)
    st.fill_rect(ax - 21, ay - 34, 42, 6, 1, 13)
    st.fill_rect(ax - 21, ay - 34, 42, 2, 1, 14)
    # Oasis date palms flanking the right dune
    for px, py, ph in ((244, 84, 30), (264, 86, 24)):
        for dy in range(ph):
            bend = int(math.sin(dy / ph * 1.4) * 5)
            st.fill_rect(px + bend - 1, py - dy, 3, 1, 1, 2 if (dy & 2) else 3)
        top_x, top_y = px + 5, py - ph
        for ang_deg in (-155, -125, -80, -35, 10, 35):
            rad = math.radians(ang_deg)
            for r in range(1, 16):
                fx = int(top_x + math.cos(rad) * r)
                fy = int(top_y + math.sin(rad) * r * 0.6 + (r * r) * 0.03)
                st.put(fx, fy, 1, 10 if r < 9 else 8)
                st.put(fx, fy + 1, 1, 7)

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 13 if y >= 156 else 1)
        # Half-buried sandstone temple slabs along the upper-left diagonal
        if -88 < u < -22 and 2.0 < v < 3.4:
            su = (u + 88) * 0.12
            sv = (v - 2.0) * 3.0
            if (su - int(su)) < 0.1 or (sv - int(sv)) < 0.12:
                return (2, 8)
            return (2, 12 if (su - int(su)) < 0.2 else 11)
        # Diagonal wind-rippled golden Babana sand waves
        ripple = math.sin(v * 7.5 + math.sin(u * 0.05) * 1.6)
        t = 0.22 + 0.56 * z + ripple * 0.22
        idx = dither_ramp(t, 6, x, y)
        return (2, (2, 3, 4, 5, 6, 7)[idx])

    st.draw_iso_floor_base(86, H, floor_fn)
    return st


def draw_scene_paradise():
    st = PixelStage()
    st.draw_sky_gradient(0, 62, 0, (1, 2, 3, 4, 5))
    st.draw_cloud(62, 15, 30, 8)
    st.draw_cloud(188, 12, 36, 9)
    st.draw_cloud(280, 20, 24, 7)
    # Distant sunlit turquoise lagoon & emerald coral islands
    for y in range(56, 92):
        t = (y - 56) / 36.0
        for x in range(W):
            shimmer = 1 if ((x + y * 5) % 19 < 2 and y > 62) else 0
            c = 13 if shimmer else (9, 10, 11, 12)[dither_ramp(t, 4, x, y)]
            st.put(x, y, 0, c)
    # Lush tropical islands on the horizon
    for ix, irx, iry in ((74, 34, 10), (242, 42, 12)):
        for dy in range(iry):
            hw = int(irx * math.sqrt(max(0.0, 1.0 - ((iry - dy) / iry) ** 2)))
            for dx in range(-hw, hw + 1):
                st.put(ix + dx, 57 - iry + dy, 1, 5 if dy < iry // 2 else 3)
    # Swaying beachfront coconut palms on left and right edges
    for px, py, ph, lean in ((46, 92, 42, 8), (274, 92, 38, -7)):
        for dy in range(ph):
            bx = px + int(math.sin(dy / ph * 1.4) * lean)
            st.fill_rect(bx - 2, py - dy, 4, 1, 1, 8 if (dy % 4 == 0) else 9)
        tx, ty = px + lean, py - ph
        for ang_deg in (-165, -130, -85, -40, 5, 35):
            rad = math.radians(ang_deg)
            for r in range(1, 20):
                fx = int(tx + math.cos(rad) * r)
                fy = int(ty + math.sin(rad) * r * 0.55 + (r * r) * 0.025)
                st.put(fx, fy, 1, 5 if r < 11 else 4)
                st.put(fx, fy + 1, 1, 2)

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 1 if y >= 156 else 2)
        # Diagonal sunlit turquoise surf lapping onto white-sand cove!
        shore_line = 2.65 + math.sin(u * 0.045) * 0.25 + u * 0.004
        if v > shore_line:
            # Shallow crystalline lagoon surf & white sea-foam
            if v < shore_line + 0.24:
                return (2, 15 if ((x + y) & 1) else 14)  # Crisp foam lace
            wt = min(1.0, (v - shore_line) * 0.8)
            return (2, (13, 12, 11)[dither_ramp(wt, 3, x, y)])
        # Weathered wooden pier deck jutting in from far-left 3/4 angle
        if u < -72.0 and v < 2.2:
            plank = int(math.floor(v * 6.5))
            if (v * 6.5 - plank) < 0.14:
                return (2, 7)
            return (2, 9 if (plank & 1) else 10)
        # Warm 3/4 sunlit white coral sand with tiny pink/gold shells
        h = ihash(int(u * 0.4), int(v * 6.0), 77)
        if h == 11 and y > 106:
            return (3, 10)  # Coral shell fleck
        ripple = math.sin(v * 6.0 + u * 0.03) * 0.14
        idx = dither_ramp(0.28 + 0.52 * z + ripple, 5, x, y)
        return (2, (2, 3, 4, 5, 6)[idx])

    st.draw_iso_floor_base(84, H, floor_fn)
    return st


def draw_scene_forest():
    st = PixelStage()
    # Dappled canopy sky peeks
    st.draw_sky_gradient(0, 86, 0, (1, 2, 3, 4, 5))
    # Deep far forest silhouette wall
    st.draw_ridge(72, 18, 0.042, 0.09, 0.5, 0, 11, 10, 9, 86)
    # Layered 3D-depth tree trunks & canopies (far -> mid -> near framing)
    for tx in (26, 78, 134, 192, 246, 294):
        st.draw_oak_tree(tx, 84, 18, 22, 1, 1, 2, 4, 5, 7, 9)
    # Vibrant autumn-amber Whispering Woods accents in mid canopy
    for tx, ty in ((56, 48), (160, 44), (264, 50)):
        st.draw_oak_tree(tx, ty + 34, 14, 14, 1, 12, 13, 14, 15, 7, 9)
    # Foreground massive framing tree trunks on far edges
    for fx in (14, 306):
        for y in range(0, 94):
            for dx in range(-12, 13):
                c = 10 if dx < -4 else (9 if dx < 2 else 7)
                if (dx % 4 == 0) and ((y + dx) & 3) == 0:
                    c = 8
                st.put(fx + dx, y, 1, c)
    # Diagonal shafts of golden forest sunlight piercing the canopy
    for beam_x in (92, 178, 248):
        for y in range(0, 88):
            bx = beam_x + (y >> 1)
            for dx in range(-6, 7):
                if ((bx + dx + y) & 3) == 0:
                    st.put(bx + dx, y, 0, 12)

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 12 if y >= 156 else 1)
        # Angled 3/4 mossy timber-plank forest footpath across the clearing
        path_u = u + 8.0
        if abs(path_u) < 46.0:
            edge = abs(path_u) / 46.0
            if edge > 0.88:
                return (2, 7 if ((x + y) & 1) else 2)  # Log border
            plank = v * 5.2
            ip = int(math.floor(plank))
            if (plank - ip) < 0.13:
                return (2, 6)  # Gap between timber planks
            grain = ihash(int(u * 0.5), ip, 83)
            return (2, (8, 9, 10, 11)[dither_ramp(0.2 + 0.55 * (1.0 - edge) + (grain & 3) * 0.08, 4, x, y)])
        # Deep mossy fern forest floor + fallen autumn leaves
        h = ihash(int(u * 0.45), int(v * 6.5), 89)
        if h in (7, 41, 109) and 94 <= y <= 148:
            return (3, 6 if h == 7 else 7)  # Fallen golden-amber leaf
        shade = dither_ramp(0.15 + 0.65 * z + ((h & 7) - 3.5) * 0.05, 4, x, y)
        return (2, (1, 2, 3, 4)[shade])

    st.draw_iso_floor_base(86, H, floor_fn)
    return st


def draw_scene_town():
    st = PixelStage()
    st.draw_sky_gradient(0, 86, 0, (1, 2, 3, 4, 5))
    st.draw_cloud(68, 16, 28, 7)
    st.draw_cloud(238, 14, 32, 8)
    # Distant castle spire & alpine town ridge
    st.draw_ridge(70, 16, 0.028, 0.06, 2.2, 0, 11, 10, 9, 86)
    # Row of charming 16-bit Tudor cottages & market awnings across the horizon (y=28..88)
    houses = [
        (8, 42, 64, 46, True),
        (78, 34, 68, 54, False),
        (154, 38, 66, 50, True),
        (228, 32, 76, 56, False),
    ]
    for hx, hy, hw, hh, red_roof in houses:
        # Pitched shingle roof
        roof_h = 18
        for dy in range(roof_h):
            inset = (roof_h - 1 - dy)
            for dx in range(inset, hw - inset):
                shingle = ((dx + (dy // 3) * 3) % 6 == 0) or (dy % 3 == 0)
                if red_roof:
                    c = 5 if shingle else (7 if dy < 9 else 6)
                else:
                    c = 12 if shingle else (14 if dy < 9 else 13)
                st.put(hx + dx, hy + dy, 1, c)
        # Half-timbered plaster facade
        wall_y = hy + roof_h
        for y in range(wall_y, min(88, hy + hh)):
            for x in range(hx + 2, hx + hw - 2):
                lx = x - (hx + 2)
                ly = y - wall_y
                is_beam = (lx < 3 or lx >= hw - 7 or ly < 2 or ly == 14 or lx == (hw - 4) // 2)
                st.put(x, y, 1, 2 if is_beam else (11 if lx < hw // 2 else 10))
        # Warm glowing mullioned windows (bright gold at Dusk/Night!)
        for wx in (hx + 12, hx + hw - 24):
            wy = wall_y + 5
            st.fill_rect(wx, wy, 10, 9, 1, 1)
            st.fill_rect(wx + 1, wy + 1, 8, 7, 1, 15)
            st.fill_rect(wx + 4, wy + 1, 2, 7, 1, 2)
            st.fill_rect(wx + 1, wy + 4, 8, 1, 1, 2)
    # Festive string of market pennants spanning between cottages
    for x in range(36, 286):
        sag = int(math.sin((x - 36) / 250.0 * math.pi) * 10)
        ry = 48 + sag
        st.put(x, ry, 3, 13)
        if (x % 14) < 8:
            tri_h = 5 - abs((x % 14) - 4)
            col = 6 if ((x // 14) & 1) == 0 else 8
            for dy in range(1, tri_h + 1):
                st.put(x, ry + dy, 3, col)
    # Warm brass street lamps flanking the town square
    for lx in (64, 158, 254):
        st.fill_rect(lx - 1, 64, 3, 24, 3, 2)
        st.fill_rect(lx - 3, 56, 7, 8, 3, 10)
        st.fill_rect(lx - 2, 57, 5, 6, 3, 11)
        st.put(lx, 59, 3, 12)

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 7 if y >= 156 else 1)
        if y < 91:
            return (2, 6 if ((x >> 2) & 1) else 5)  # Town plaza curbstone
        # Angled 3/4 European cobblestone square with warm lantern light pools
        fv = v * 4.2
        iv = int(math.floor(fv))
        fu = u * 0.16 + (iv & 1) * 0.5
        iu = int(math.floor(fu))
        frac_u, frac_v = fu - iu, fv - iv
        if frac_u < 0.10 or frac_v < 0.14:
            return (2, 7)  # Cobble mortar seam
        # Subtle warm golden lantern pool near town lamps
        near_lamp = min(abs(x - 64), abs(x - 158), abs(x - 254))
        if near_lamp < 22 and y < 112 and ((x + y) & 1) == 0:
            return (2, 14 if near_lamp < 10 else 5)
        if frac_u < 0.22 or frac_v < 0.24:
            return (2, 5)
        h = ihash(iu, iv, 101)
        return (2, 2 + (h % 3))

    st.draw_iso_floor_base(86, H, floor_fn)
    return st


def draw_scene_interior():
    st = PixelStage()
    # Warm plastered cottage/guildhall upper wall & timber rafters
    st.draw_sky_gradient(0, 88, 0, (6, 7, 8, 9, 8))
    # Horizontal ceiling beam & vertical cedar wainscoting studs
    st.fill_rect(0, 0, W, 10, 1, 2)
    st.fill_rect(0, 10, W, 3, 1, 4)
    for tx in range(12, W, 44):
        st.fill_rect(tx, 10, 8, 78, 1, 3)
        st.fill_rect(tx + 1, 10, 2, 78, 1, 5)
    # Lower cedar wood wainscoting paneling (y=58..88)
    for y in range(58, 88):
        for x in range(W):
            if y in (58, 59, 86, 87):
                st.put(x, y, 1, 5 if y == 58 else 2)
            elif x % 12 == 0:
                st.put(x, y, 1, 1)
            else:
                st.put(x, y, 1, 4 if (x % 12) < 4 else 3)
    # Two leaded-glass arched windows (uses PAL0 1..5 -> dynamically shifts Morning/Afternoon/Dusk/Night!)
    for wx in (78, 242):
        for y in range(20, 56):
            for x in range(wx - 16, wx + 17):
                dx = x - wx
                in_win = (y >= 34) or ((dx * dx) / (16.0 * 16.0) + ((y - 34) * (y - 34)) / (14.0 * 14.0) <= 1.0)
                if in_win:
                    edge = (abs(dx) >= 14 or y >= 54 or (y < 34 and math.hypot(dx / 16.0, (y - 34) / 14.0) > 0.86))
                    mullion = (abs(dx) <= 1 or y in (36, 46))
                    if edge or mullion:
                        st.put(x, y, 1, 2)
                    else:
                        t = (y - 22) / 32.0
                        st.put(x, y, 0, (1, 2, 3, 4, 5)[dither_ramp(t, 5, x, y)])
    # Cozy stone hearth fireplace centered at back wall (x=136..184)
    st.fill_rect(134, 34, 52, 54, 1, 8)
    for y in range(34, 88):
        for x in range(134, 186):
            if ((x + (y // 6) * 5) % 10 == 0) or (y % 6 == 0):
                st.put(x, y, 1, 7)
            else:
                st.put(x, y, 1, 9 if x < 160 else 8)
    st.fill_rect(130, 52, 60, 4, 1, 5)  # Cedar mantelpiece
    st.fill_rect(144, 62, 32, 26, 1, 1)  # Firebox
    # Glowing hearth flames
    for dy in range(16):
        hw = max(2, 11 - dy // 2)
        for dx in range(-hw, hw + 1):
            c = 8 if dy < 5 else (7 if dy < 10 else 6)
            if ((dx + dy) & 1) == 0 or dy < 8:
                st.put(160 + dx, 86 - dy, 3, c)

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 15 if y >= 156 else 1)
        # Angled 3/4 woven crimson-and-gold parlor rug right under the battle stage!
        if abs(u) < 74.0 and 1.85 < v < 3.55:
            ru = abs(u) / 74.0
            rv = abs(v - 2.70) / 0.85
            m = max(ru, rv)
            if m > 0.88:
                return (2, 13 if ((x + y) & 1) else 12)  # Gold fringe border
            if 0.72 < m <= 0.80:
                return (2, 12)  # Inner geometric gold frame
            diamond = ((int(u * 0.25) + int(v * 4.0)) & 3) == 0
            return (2, 10 if diamond else (9 if ((x + y) & 1) else 8))
        # Angled 3/4 warm cedar floorboards with wood-grain & plank seams
        plank = v * 4.8
        ip = int(math.floor(plank))
        frac_v = plank - ip
        if frac_v < 0.11:
            return (2, 1)
        if frac_v < 0.19:
            return (2, 5)
        seam_u = (u * 0.05 + (ip & 1) * 0.5)
        if (seam_u - math.floor(seam_u)) < 0.03:
            return (2, 1)
        h = ihash(int(math.floor(seam_u)), ip, 113)
        return (2, 3 + (h % 3))

    st.draw_iso_floor_base(88, H, floor_fn)
    return st


def draw_scene_sea():
    st = PixelStage()
    st.draw_sky_gradient(0, 66, 0, (1, 2, 3, 4, 5))
    st.draw_cloud(58, 16, 32, 8)
    st.draw_cloud(172, 12, 38, 9)
    st.draw_cloud(274, 22, 26, 7)
    # Distant dramatic rocky sea stacks on the ocean horizon
    st.draw_ridge(68, 14, 0.032, 0.08, 0.9, 0, 11, 10, 9, 68)
    # Rolling deep cerulean ocean swells (y=62..100)
    for y in range(62, 100):
        t = (y - 62) / 38.0
        for x in range(W):
            crest = math.sin(x * 0.09 + y * 0.55) + math.cos(x * 0.04 - y * 0.2) * 0.5
            if crest > 1.18 and y > 68:
                c = 7 if crest > 1.32 else 6
            else:
                c = (1, 2, 3, 4, 5)[dither_ramp(t + crest * 0.12, 5, x, y)]
            st.put(x, y, 1, c)
    # Cedar Raft mast & billowing cream sail rising on far-left edge (x=36..62)
    st.fill_rect(44, 18, 4, 78, 1, 13)
    st.fill_rect(44, 18, 2, 78, 1, 14)
    for y in range(24, 72):
        bulge = int(math.sin((y - 24) / 48.0 * math.pi) * 14)
        for x in range(48, 56 + bulge):
            st.put(x, y, 1, 8 if x < 54 else 7)

    def floor_fn(x, y, u, v, z):
        # Foreground 3/4 Isometric Cedar Raft Timber Deck surrounded by foaming sea!
        if y >= 153:
            return (2, 12 if ((x + y) & 1) else 13)  # Fore sea swell below raft edge
        in_raft = (-122.0 < u < 126.0) and (1.42 < v < 3.65)
        if not in_raft:
            # Foaming ocean wave edge right beside the cedar raft logs
            near = (-132.0 < u < 136.0) and (1.28 < v < 3.85)
            if near and ((x + y) & 1) == 0:
                return (3, 6 if (x & 2) else 5)
            wave = math.sin(u * 0.08 + v * 4.5)
            return (2, (12, 13, 14, 15)[dither_ramp(0.25 + 0.55 * z + wave * 0.2, 4, x, y)])
        # Thick rope lashings binding the cedar logs along 3/4 seams
        if abs(u + 68.0) < 5.0 or abs(u - 68.0) < 5.0:
            rope = int(math.floor(v * 9.0))
            return (2, 10 if (rope & 1) else 9)
        # Beveled cedar logs running diagonally along 3/4 raft deck
        log_v = v * 4.6
        il = int(math.floor(log_v))
        frac_v = log_v - il
        if frac_v < 0.13:
            return (2, 1)  # Deep shadow groove between cedar logs
        if frac_v < 0.28:
            return (2, 6)  # Rounded sunlit top edge of cedar log
        h = ihash(int(u * 0.15), il, 127)
        return (2, 3 + (h % 3))

    st.draw_iso_floor_base(92, H, floor_fn)
    return st


def draw_scene_frost():
    st = PixelStage()
    st.draw_sky_gradient(0, 86, 0, (1, 2, 3, 4, 5))
    st.draw_starfield(42, seed=57, p=0, c_dim=13, c_hi=14)
    # Shimmering emerald-teal Northern Aurora curtains across the Frostmere sky!
    for x in range(W):
        ay0 = int(14 + math.sin(x * 0.032) * 8 + math.cos(x * 0.017) * 5)
        for dy in range(18):
            yy = ay0 + dy
            if 0 <= yy < 54:
                t = dy / 17.0
                if ((x + dy) & 1) == 0 or (0.2 < t < 0.7):
                    c = (6, 7, 8, 9, 7, 6)[dither_ramp(t, 6, x, yy)]
                    st.put(x, yy, 0, c)
    # Jagged glacial peaks & crystalline ice spires
    st.draw_ridge(68, 28, 0.035, 0.082, 0.6, 1, 7, 5, 3, 86)
    st.draw_ridge(78, 16, 0.048, 0.11, 2.3, 1, 6, 4, 2, 88)
    # Translucent crystalline ice spires rising left & right
    for ix, ih in ((52, 42), (264, 46)):
        for dy in range(ih):
            hw = max(1, int((dy / ih) * 11))
            for dx in range(-hw, hw + 1):
                c = 15 if dx < -hw // 3 else (14 if dx < hw // 4 else 12)
                st.put(ix + dx, 86 - ih + dy, 1, c)
    # Snow-laden alpine pines along mid ridge
    for tx in (24, 92, 124, 206, 232, 296):
        st.draw_pine_tree(tx, 86, 18 + (tx % 9), 1, 8, 9, 10, 7)

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 13 if y >= 156 else 12)
        if y < 91:
            return (2, 10 if ((x + y) & 1) else 9)  # Ice shelf ridge edge
        # Angled 3/4 frozen glacier ice sheet in center + packed snow perimeter
        if abs(u) < 76.0 and 1.65 < v < 3.45:
            # Cracked translucent blue glacier ice with crystalline specular streaks
            crack = abs(math.sin(u * 0.06 + v * 2.4))
            if crack < 0.055:
                return (2, 11)  # Crisp bright fracture line
            if crack < 0.11:
                return (2, 7)
            glint = math.sin(u * 0.04 - v * 1.8) * 0.25
            return (2, (8, 9, 10, 11)[dither_ramp(0.3 + 0.45 * z + glint, 4, x, y)])
        # Crisp wind-sculpted packed snowdrifts
        drift = math.sin(v * 5.0 + u * 0.04) * 0.18
        idx = dither_ramp(0.28 + 0.54 * z + drift, 5, x, y)
        return (2, (2, 3, 4, 5, 6)[idx])

    st.draw_iso_floor_base(86, H, floor_fn)
    return st


def draw_scene_volcano():
    st = PixelStage()
    st.draw_sky_gradient(0, 86, 0, (1, 2, 3, 4, 5))
    # Ash clouds lit from below in fiery ember-amber
    st.draw_cloud(76, 16, 36, 9, 0, 6, 7, 8)
    st.draw_cloud(236, 20, 40, 10, 0, 6, 7, 8)
    # Jagged volcanic caldera ridges & central Cinderpeak cone
    st.draw_ridge(72, 24, 0.029, 0.072, 1.4, 1, 5, 3, 2, 88)
    # Towering smoking Cinderpeak volcano cone centered at x=160
    for y in range(20, 88):
        hw = 14 + int((y - 20) * 0.95)
        for dx in range(-hw, hw + 1):
            x = 160 + dx
            # Glowing caldera crater rim at top
            if y < 25 and abs(dx) < 12:
                st.put(x, y, 1, 11 if y > 21 else 10)
            else:
                # Winding glowing magma river carving down the volcano face
                lava_x = int(math.sin(y * 0.16) * 8)
                dist_lava = abs(dx - lava_x)
                if dist_lava <= 2:
                    st.put(x, y, 1, 11 if dist_lava == 0 else 9)
                elif dist_lava <= 4:
                    st.put(x, y, 1, 8)
                else:
                    c = 4 if dx < -hw // 3 else (3 if dx < hw // 4 else 2)
                    st.put(x, y, 1, c)
    # Jagged basalt rock fangs left & right
    for rx, rh in ((44, 34), (274, 38)):
        for dy in range(rh):
            hw = max(2, int((dy / rh) * 14))
            for dx in range(-hw, hw + 1):
                st.put(rx + dx, 88 - rh + dy, 1, 5 if dx < -2 else (3 if dx < 4 else 1))

    def floor_fn(x, y, u, v, z):
        if y >= 153:
            return (2, 13 if y >= 156 else 1)
        # Glowing magma chasm running behind the obsidian ridge (y=86..96)
        if y < 96:
            glow = math.sin(x * 0.12 + y * 0.7) * 0.25
            return (2, (8, 9, 10, 11, 12)[dither_ramp(0.35 + 0.45 * ((y - 86) / 10.0) + glow, 5, x, y)])
        # Angled 3/4 cracked obsidian basalt arena with glowing magma fissures near edges!
        fu, fv = u * 0.085, v * 2.3
        iu, iv = int(math.floor(fu)), int(math.floor(fv))
        frac_u, frac_v = fu - iu, fv - iv
        is_crack = (frac_u < 0.08 or frac_v < 0.10)
        if is_crack:
            # Outer cracks glow molten orange; inner combat center stays dark charcoal for readability
            if abs(u) > 68.0 or y < 104:
                return (2, 10 if ((x + y) & 1) else 9)
            return (2, 13)
        if frac_u < 0.15 or frac_v < 0.18:
            return (2, 5)
        h = ihash(iu, iv, 149)
        return (2, 2 + (h % 3))

    st.draw_iso_floor_base(86, H, floor_fn)
    return st


SCENE_DRAWERS = {
    "MEADOW":   draw_scene_meadow,
    "SHRINE":   draw_scene_shrine,
    "SEWER":    draw_scene_sewer,
    "BOARHOLD": draw_scene_boarhold,
    "DESERT":   draw_scene_desert,
    "PARADISE": draw_scene_paradise,
    "FOREST":   draw_scene_forest,
    "TOWN":     draw_scene_town,
    "INTERIOR": draw_scene_interior,
    "SEA":      draw_scene_sea,
    "FROST":    draw_scene_frost,
    "VOLCANO":  draw_scene_volcano,
}


# =============================================================================
# 5. TILE QUANTIZATION, H/V-FLIP DEDUPLICATION & SMART BUDGET MERGING
# =============================================================================
def nearest_in_pal(rgb, pal):
    best_i = 1
    best_d = 1 << 30
    for i in range(1, 16):
        d = color_dist2(rgb, pal[i])
        if d < best_d:
            best_d = d
            best_i = i
    return best_i


def extract_60color_tiles(stage, tod_pals, x0, tw, th, max_tiles):
    """
    Convert PixelStage sub-rect (x0..x0+tw*8, 0..th*8) into strictly 1-palette-per-tile
    4bpp tiles (using the Afternoon base palette for perceptual distance at boundaries)
    with exact H/V-flip deduplication and perceptual merging if > max_tiles.
    Returns: (tiles_list[N x 64 bytes], map_entries[tw*th of (tid, pal, hf, vf)])
    """
    base_pals = tod_pals[1]  # Afternoon baseline for clean cross-palette matching
    tile_pixels = []         # list of tuples(64 ints)
    tile_pals = []           # list of pal_idx (0..3)
    raw_map = []             # list of (unique_idx, pal_idx, hf, vf)
    lookup = {}              # (pal_idx, pixels_tuple) -> (uid, hf, vf)

    for ty in range(th):
        for tx in range(tw):
            # Choose majority sub-palette inside this 8x8 tile
            counts = [0, 0, 0, 0]
            px_x0 = x0 + tx * 8
            py_y0 = ty * 8
            for ly in range(8):
                row = (py_y0 + ly) * W + px_x0
                for lx in range(8):
                    counts[stage.pal[row + lx]] += 1
            best_p = max(range(4), key=lambda k: counts[k])
            pal = base_pals[best_p]
            pix = []
            for ly in range(8):
                row = (py_y0 + ly) * W + px_x0
                for lx in range(8):
                    p0 = stage.pal[row + lx]
                    c0 = stage.col[row + lx]
                    if p0 == best_p:
                        pix.append(c0)
                    else:
                        pix.append(nearest_in_pal(base_pals[p0][c0], pal))
            pt = tuple(pix)
            key = (best_p, pt)
            if key in lookup:
                uid, hf, vf = lookup[key]
                raw_map.append([uid, best_p, hf, vf])
            else:
                uid = len(tile_pixels)
                tile_pixels.append(pt)
                tile_pals.append(best_p)
                # Register all 4 flip orientations
                h_pt = tuple(pt[r * 8 + (7 - c)] for r in range(8) for c in range(8))
                v_pt = tuple(pt[(7 - r) * 8 + c] for r in range(8) for c in range(8))
                hv_pt = tuple(pt[(7 - r) * 8 + (7 - c)] for r in range(8) for c in range(8))
                lookup[(best_p, hv_pt)] = (uid, 1, 1)
                lookup[(best_p, v_pt)] = (uid, 0, 1)
                lookup[(best_p, h_pt)] = (uid, 1, 0)
                lookup[(best_p, pt)] = (uid, 0, 0)
                raw_map.append([uid, best_p, 0, 0])

    # Merge closest same-palette tile pairs if unique count exceeds hardware budget
    if len(tile_pixels) > max_tiles:
        tile_pixels, raw_map = merge_excess_tiles(tile_pixels, tile_pals, raw_map, base_pals, max_tiles)

    return tile_pixels, raw_map


def merge_excess_tiles(tile_pixels, tile_pals, raw_map, base_pals, max_tiles):
    """Fast perceptual tile reduction strictly preserving palette slot & crisp contrast."""
    n = len(tile_pixels)
    # Precompute 64 RGB tuples & mean RGB per tile for fast nearest-neighbor pruning
    rgbs = []
    means = []
    for i in range(n):
        pal = base_pals[tile_pals[i]]
        t_rgb = [pal[c] for c in tile_pixels[i]]
        rgbs.append(t_rgb)
        mr = sum(c[0] for c in t_rgb) >> 6
        mg = sum(c[1] for c in t_rgb) >> 6
        mb = sum(c[2] for c in t_rgb) >> 6
        means.append((mr, mg, mb))

    # Count map usage so rare tiles merge into more frequent ones
    uses = [0] * n
    for e in raw_map:
        uses[e[0]] += 1

    active = [True] * n
    remap = list(range(n))
    cur_count = n

    # Group active indices by palette
    by_pal = [[i for i in range(n) if tile_pals[i] == p] for p in range(4)]

    # Build candidate nearest pair per tile (exact MSE within same palette)
    def tile_mse(a, b):
        ra, rb = rgbs[a], rgbs[b]
        s = 0
        for k in range(0, 64, 2):  # 32-pixel checkerboard sample + fast exact check
            s += color_dist2(ra[k], rb[k])
        return s

    best_partner = [-1] * n
    best_err = [1 << 60] * n

    for p in range(4):
        grp = by_pal[p]
        for idx_a, a in enumerate(grp):
            ma = means[a]
            be = 1 << 60
            bp = -1
            for b in grp:
                if a == b:
                    continue
                mb = means[b]
                # Mean color lower bound pruning
                md = color_dist2(ma, mb) * 28
                if md >= be:
                    continue
                err = tile_mse(a, b)
                if err < be:
                    be = err
                    bp = b
            best_partner[a] = bp
            best_err[a] = be

    while cur_count > max_tiles:
        # Pick active tile with lowest merge error
        best_a = -1
        be = 1 << 60
        for i in range(n):
            if active[i] and best_partner[i] >= 0 and active[best_partner[i]]:
                if best_err[i] < be:
                    be = best_err[i]
                    best_a = i
        if best_a < 0:
            # Refresh partners for any stale entries
            for p in range(4):
                grp = [i for i in by_pal[p] if active[i]]
                for a in grp:
                    if best_partner[a] < 0 or not active[best_partner[a]]:
                        ma = means[a]
                        be2 = 1 << 60
                        bp2 = -1
                        for b in grp:
                            if a == b:
                                continue
                            if color_dist2(ma, means[b]) * 28 >= be2:
                                continue
                            err = tile_mse(a, b)
                            if err < be2:
                                be2 = err
                                bp2 = b
                        best_partner[a] = bp2
                        best_err[a] = be2
            continue

        b = best_partner[best_a]
        # Keep the more frequently used tile
        if uses[best_a] > uses[b]:
            keep, drop = best_a, b
        else:
            keep, drop = b, best_a
        active[drop] = False
        remap[drop] = keep
        uses[keep] += uses[drop]
        cur_count -= 1

        # Recompute partner only for active tiles that pointed to `drop`
        p_grp = [i for i in by_pal[tile_pals[drop]] if active[i]]
        for a in p_grp:
            if best_partner[a] == drop:
                ma = means[a]
                be2 = 1 << 60
                bp2 = -1
                for cand in p_grp:
                    if a == cand:
                        continue
                    if color_dist2(ma, means[cand]) * 28 >= be2:
                        continue
                    err = tile_mse(a, cand)
                    if err < be2:
                        be2 = err
                        bp2 = cand
                best_partner[a] = bp2
                best_err[a] = be2

    # Flatten remap chain and compact tile IDs
    def resolve(i):
        while remap[i] != i:
            i = remap[i]
        return i

    new_tiles = []
    old_to_new = {}
    for i in range(n):
        if active[i]:
            old_to_new[i] = len(new_tiles)
            new_tiles.append(tile_pixels[i])

    new_map = []
    for uid, p, hf, vf in raw_map:
        nid = old_to_new[resolve(uid)]
        new_map.append([nid, p, hf, vf])

    return new_tiles, new_map


def build_genesis_15color_scene(stage, tod_pals, max_tiles):
    """
    Build a sprite-safe 15-color Genesis PAL0 scene (+ 4 Time-of-Day RGB333 palettes!)
    so PAL1 (Party), PAL2 (Window UI), and PAL3 (Enemies/VFX) remain 100% untouched.
    Uses weighted frequency + role-balanced 15-color selection directly from the scene's
    authored 60-color palette, preserving sky, landmarks, 3/4 floor, and luminous accents!
    """
    base_pals = tod_pals[1]
    # Count exact frequency of every (pal_idx, col_idx) actually used on screen
    freq = {}
    for i in range(W * H):
        key = (stage.pal[i], stage.col[i])
        freq[key] = freq.get(key, 0) + 1

    # Select 15 representative (pal_idx, col_idx) anchors maximizing visual coverage
    # Guarantee key roles: top colors from PAL0 (sky), PAL1 (landmarks), PAL2 (floor), PAL3 (accents)
    ranked = sorted(freq.items(), key=lambda kv: -kv[1])
    chosen_keys = []
    # Pick seed color with highest frequency first
    if ranked:
        chosen_keys.append(ranked[0][0])

    # Farthest-point weighted selection (favors distinct hues + high pixel count)
    while len(chosen_keys) < 15 and len(chosen_keys) < len(ranked):
        best_k = None
        best_score = -1.0
        for k, cnt in ranked:
            if k in chosen_keys:
                continue
            rgb_k = snap_gen_rgb(base_pals[k[0]][k[1]])
            min_d = min(color_dist2(rgb_k, snap_gen_rgb(base_pals[ck[0]][ck[1]])) for ck in chosen_keys)
            # Mild boost to ensure floor & sky & luminous details all get crisp representation
            role_w = 1.15 if k[0] in (0, 2) else (1.05 if k[0] == 1 else 1.20)
            score = (min_d + 40) * (math.log(cnt + 2.0) ** 1.35) * role_w
            if score > best_score:
                best_score = score
                best_k = k
        if best_k is None:
            break
        chosen_keys.append(best_k)

    while len(chosen_keys) < 15:
        chosen_keys.append(chosen_keys[-1] if chosen_keys else (0, 1))

    # Build 4 Time-of-Day 16-entry Genesis RGB333 PAL0 tables from the exact 15 chosen anchors!
    gen_tod_pals_rgb = []
    gen_tod_pals_bgr333 = []
    for tod in range(4):
        pal_rgb = [(8, 8, 16)]
        pal_333 = [0x0000]
        for (pk, ck) in chosen_keys:
            c = tod_pals[tod][pk][ck]
            pal_rgb.append(snap_gen_rgb(c))
            pal_333.append(rgb_to_bgr333(c))
        gen_tod_pals_rgb.append(pal_rgb)
        gen_tod_pals_bgr333.append(pal_333)

    # Map every pixel of stage cleanly to PAL0 index 1..15 with mild 2x2 ordered Bayer smoothing
    ref_pal = gen_tod_pals_rgb[1]
    key_to_idx = {}
    for pk in range(4):
        for ck in range(1, 16):
            key_to_idx[(pk, ck)] = nearest_in_pal(base_pals[pk][ck], ref_pal)

    gen_stage = PixelStage()
    for y in range(H):
        row = y * W
        for x in range(W):
            pk = stage.pal[row + x]
            ck = stage.col[row + x]
            gen_stage.pal[row + x] = 0
            gen_stage.col[row + x] = key_to_idx[(pk, ck)]

    # Reuse exact 60-color extractor (all tiles are PAL0!) with Genesis tile budget
    dummy_pals = [[ref_pal, ref_pal, ref_pal, ref_pal]] * 4
    tiles, map_entries = extract_60color_tiles(gen_stage, dummy_pals, 0, TW, TH, max_tiles)
    return tiles, map_entries, gen_tod_pals_bgr333


# =============================================================================
# 6. HARDWARE BINARY PACKERS (SNES 4bpp Planar / Genesis 4bpp Packed)
# =============================================================================
def pack_snes_4bpp(tile64):
    """Pack 8x8 tile into 32-byte SNES 4bpp planar format (bp0/1 interleaved, then bp2/3)."""
    out = bytearray(32)
    for y in range(8):
        b0 = b1 = b2 = b3 = 0
        for x in range(8):
            c = tile64[y * 8 + x] & 0x0F
            bit = 7 - x
            b0 |= ((c >> 0) & 1) << bit
            b1 |= ((c >> 1) & 1) << bit
            b2 |= ((c >> 2) & 1) << bit
            b3 |= ((c >> 3) & 1) << bit
        out[y * 2] = b0
        out[y * 2 + 1] = b1
        out[16 + y * 2] = b2
        out[16 + y * 2 + 1] = b3
    return bytes(out)


def pack_gen_4bpp_u32(tile64):
    """Pack 8x8 tile into 8 big-endian u32 words (SGDK/VDP 4bpp chunky rows)."""
    words = []
    for y in range(8):
        w = 0
        for x in range(8):
            w = (w << 4) | (tile64[y * 8 + x] & 0x0F)
        words.append(w)
    return words


def pack_hi_4bpp_bytes(tile64):
    """Pack 8x8 tile into 32 bytes (2 pixels per byte: high nibble=left, low nibble=right)."""
    out = bytearray(32)
    for i in range(32):
        out[i] = ((tile64[i * 2] & 0x0F) << 4) | (tile64[i * 2 + 1] & 0x0F)
    return bytes(out)


def fmt_asm_bytes(data, per_line=16):
    lines = []
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        lines.append("    .db " + ",".join(f"${b:02X}" for b in chunk))
    return "\n".join(lines)


def fmt_asm_words(words, per_line=12):
    lines = []
    for i in range(0, len(words), per_line):
        chunk = words[i:i + per_line]
        lines.append("    .dw " + ",".join(f"${w:04X}" for w in chunk))
    return "\n".join(lines)


# =============================================================================
# 7. MAIN BAKER PIPELINE
# =============================================================================
def main():
    print("Baking 12 hand-pixelated 16-bit 3/4 isometric battle backdrops...")
    results = []

    for idx, name in enumerate(BIOME_NAMES):
        stage = SCENE_DRAWERS[name]()
        tod_pals = build_tod_palettes(name)

        # 1) High-craft 60-color 40x20 (Web Canvas + SDL2 Native PC)
        hi_tiles, hi_map = extract_60color_tiles(stage, tod_pals, 0, TW, TH, MAX_TILES_HI)

        # 2) SNES 60-color 32x20 centered crop (x=32..287)
        snes_tiles, snes_map = extract_60color_tiles(stage, tod_pals, SNES_X0, SNES_TW, SNES_TH, MAX_TILES_SNES)

        # 3) Sega Genesis sprite-safe 15-color PAL0 40x20 (+ 4-ToD RGB333)
        gen_tiles, gen_map, gen_tod_333 = build_genesis_15color_scene(stage, tod_pals, MAX_TILES_GEN)

        assert len(snes_tiles) <= MAX_TILES_SNES, f"SNES tile budget exceeded for {name}: {len(snes_tiles)}"
        assert len(gen_tiles) <= MAX_TILES_GEN, f"Genesis tile budget exceeded for {name}: {len(gen_tiles)}"
        assert len(hi_tiles) <= MAX_TILES_HI, f"Hi-color tile budget exceeded for {name}: {len(hi_tiles)}"

        print(f"  [{idx:2d}] {name:9s}  |  Web/SDL2: {len(hi_tiles):3d}t  |  SNES: {len(snes_tiles):3d}t ({len(snes_tiles)*32/1024:.1f}KB)  |  Genesis: {len(gen_tiles):3d}t ({len(gen_tiles)*32/1024:.1f}KB)")
        results.append({
            "idx": idx,
            "name": name,
            "tod_pals": tod_pals,
            "hi_tiles": hi_tiles,
            "hi_map": hi_map,
            "snes_tiles": snes_tiles,
            "snes_map": snes_map,
            "gen_tiles": gen_tiles,
            "gen_map": gen_map,
            "gen_tod_333": gen_tod_333,
        })

    # -------------------------------------------------------------------------
    # WRITE 1: engine/platform/battle_bg_snes.asm (12 SUPERFREE LoROM banks!)
    # -------------------------------------------------------------------------
    snes_lines = [
        "; ============================================================================",
        "; battle_bg_snes.asm — AUTO-GENERATED by engine/tools/bake_battle_bgs.py",
        "; 12 Hand-Pixelated 16-Bit 3/4 Isometric Battle Stages (60-color, 4-ToD)",
        "; ============================================================================",
        "; Appended after cinema_cg_snes.asm (which includes hdr.asm) into data.asm",
        "",
    ]
    total_snes_bytes = 0
    for r in results:
        idx = r["idx"]
        name = r["name"]
        chr_bytes = b"".join(pack_snes_4bpp(t) for t in r["snes_tiles"])
        # SNES tilemap word: vhopppcc cccccccc (priority=0 so BG0 UI & OBJ sprites sit cleanly on top)
        map_words = [
            ((vf & 1) << 15) | ((hf & 1) << 14) | ((pal & 3) << 10) | (tid & 0x03FF)
            for (tid, pal, hf, vf) in r["snes_map"]
        ]
        # 4 ToD x 4 palettes x 16 BGR555 words = 256 words (512 bytes)
        pal_words = []
        for tod in range(4):
            for pi in range(4):
                for ci in range(16):
                    pal_words.append(rgb_to_bgr555(r["tod_pals"][tod][pi][ci]))

        sec_bytes = len(chr_bytes) + len(map_words) * 2 + len(pal_words) * 2
        total_snes_bytes += sec_bytes
        assert sec_bytes < 30000, f"SNES LoROM bank overflow in {name}: {sec_bytes} B"

        snes_lines.append(f'.section ".rodata_bbg{idx}" superfree')
        snes_lines.append(f"; Scene {idx}: {name} ({len(r['snes_tiles'])} tiles, {sec_bytes} bytes)")
        snes_lines.append(f"bbg_{idx}_tiles:")
        snes_lines.append(fmt_asm_bytes(chr_bytes, 16))
        snes_lines.append(f"bbg_{idx}_map:")
        snes_lines.append(fmt_asm_words(map_words, 16))
        snes_lines.append(f"bbg_{idx}_pal:")
        snes_lines.append(fmt_asm_words(pal_words, 16))
        snes_lines.append(".ends\n")

    with open(OUT_SNES_ASM, "w") as f:
        f.write("\n".join(snes_lines))

    # -------------------------------------------------------------------------
    # WRITE 2: engine/platform/battle_bg_rom.h (Genesis + SNES externs + SDL2)
    # -------------------------------------------------------------------------
    h_lines = [
        "/* ============================================================================",
        " * battle_bg_rom.h — AUTO-GENERATED by engine/tools/bake_battle_bgs.py",
        " * 12 Hand-Pixelated 16-Bit 3/4 Isometric Battle Backdrops + 4-ToD Palettes",
        " * ============================================================================ */",
        "#ifndef HEARTH_BATTLE_BG_ROM_H",
        "#define HEARTH_BATTLE_BG_ROM_H",
        "",
        "#define BATTLE_BG_COUNT    12u",
        "#define BATTLE_BG_GEN_TW   40u",
        "#define BATTLE_BG_GEN_TH   20u",
        "#define BATTLE_BG_SNES_TW  32u",
        "#define BATTLE_BG_SNES_TH  20u",
        "#define BATTLE_BG_SNES_LEN 640u",
        "",
    ]

    # --- SNES externs & tile counts ---
    h_lines.append("#ifdef HEARTH_INCLUDE_SNES_BATTLE_BG")
    for r in results:
        i = r["idx"]
        h_lines.append(f"extern char bbg_{i}_tiles, bbg_{i}_map, bbg_{i}_pal;")
    counts_s = ", ".join(f"{len(r['snes_tiles'])}u" for r in results)
    h_lines.append(f"static const unsigned short BATTLE_SNES_TILE_COUNTS[BATTLE_BG_COUNT] = {{ {counts_s} }};")
    h_lines.append("#endif /* HEARTH_INCLUDE_SNES_BATTLE_BG */")
    h_lines.append("")

    # --- Sega Genesis ROM tiles (4bpp u32), 40x20 maps, 4-ToD PAL0 RGB333 ---
    h_lines.append("#ifdef HEARTH_INCLUDE_GENESIS_BATTLE_BG")
    total_gen_bytes = 0
    for r in results:
        i = r["idx"]
        words = []
        for t in r["gen_tiles"]:
            words.extend(pack_gen_4bpp_u32(t))
        total_gen_bytes += len(words) * 4
        h_lines.append(f"static const u32 BATTLE_GEN_TILES_{i}[{len(words)}] = {{")
        for k in range(0, len(words), 8):
            chunk = words[k:k + 8]
            h_lines.append("    " + ",".join(f"0x{w:08X}u" for w in chunk) + ",")
        h_lines.append("};")
        # Genesis map entry word: vf(12) | hf(11) | tid(0..10) (always PAL0!)
        mwords = [
            ((vf & 1) << 12) | ((hf & 1) << 11) | (tid & 0x07FF)
            for (tid, _p, hf, vf) in r["gen_map"]
        ]
        total_gen_bytes += len(mwords) * 2
        h_lines.append(f"static const u16 BATTLE_GEN_MAP_{i}[800] = {{")
        for k in range(0, len(mwords), 16):
            chunk = mwords[k:k + 16]
            h_lines.append("    " + ",".join(f"0x{w:04X}u" for w in chunk) + ",")
        h_lines.append("};")

    tptrs = ", ".join(f"BATTLE_GEN_TILES_{i}" for i in range(12))
    mptrs = ", ".join(f"BATTLE_GEN_MAP_{i}" for i in range(12))
    gcnts = ", ".join(f"{len(r['gen_tiles'])}u" for r in results)
    h_lines.append(f"static const u32 * const BATTLE_GEN_TILE_PTRS[BATTLE_BG_COUNT] = {{ {tptrs} }};")
    h_lines.append(f"static const u16 * const BATTLE_GEN_MAP_PTRS[BATTLE_BG_COUNT]  = {{ {mptrs} }};")
    h_lines.append(f"static const u16 BATTLE_GEN_TILE_COUNTS[BATTLE_BG_COUNT]       = {{ {gcnts} }};")

    h_lines.append("static const u16 BATTLE_GEN_PAL0[BATTLE_BG_COUNT][4][16] = {")
    for r in results:
        h_lines.append(f"  {{ /* {r['name']} */")
        for tod in range(4):
            row = ",".join(f"0x{w:04X}u" for w in r["gen_tod_333"][tod])
            h_lines.append(f"    {{ {row} }},")
        h_lines.append("  },")
    h_lines.append("};")
    h_lines.append("#endif /* HEARTH_INCLUDE_GENESIS_BATTLE_BG */")
    h_lines.append("")

    # --- Native PC (SDL2) 60-color 40x20 4bpp tiles, maps & 4-ToD ARGB8888 palettes ---
    h_lines.append("#ifdef HEARTH_INCLUDE_SDL2_BATTLE_BG")
    for r in results:
        i = r["idx"]
        tbytes = b"".join(pack_hi_4bpp_bytes(t) for t in r["hi_tiles"])
        h_lines.append(f"static const uint8_t BATTLE_HI_TILES_{i}[{len(tbytes)}] = {{")
        for k in range(0, len(tbytes), 24):
            chunk = tbytes[k:k + 24]
            h_lines.append("    " + ",".join(f"0x{b:02X}u" for b in chunk) + ",")
        h_lines.append("};")
        # Map entry: vf(15) | hf(14) | pal(12..13) | tid(0..11)
        mwords = [
            ((vf & 1) << 15) | ((hf & 1) << 14) | ((pal & 3) << 12) | (tid & 0x0FFF)
            for (tid, pal, hf, vf) in r["hi_map"]
        ]
        h_lines.append(f"static const uint16_t BATTLE_HI_MAP_{i}[800] = {{")
        for k in range(0, len(mwords), 16):
            chunk = mwords[k:k + 16]
            h_lines.append("    " + ",".join(f"0x{w:04X}u" for w in chunk) + ",")
        h_lines.append("};")

    htptrs = ", ".join(f"BATTLE_HI_TILES_{i}" for i in range(12))
    hmptrs = ", ".join(f"BATTLE_HI_MAP_{i}" for i in range(12))
    h_lines.append(f"static const uint8_t  * const BATTLE_HI_TILE_PTRS[BATTLE_BG_COUNT] = {{ {htptrs} }};")
    h_lines.append(f"static const uint16_t * const BATTLE_HI_MAP_PTRS[BATTLE_BG_COUNT]  = {{ {hmptrs} }};")

    h_lines.append("static const uint32_t BATTLE_HI_PALS[BATTLE_BG_COUNT][4][4][16] = {")
    for r in results:
        h_lines.append(f"  {{ /* {r['name']} */")
        for tod in range(4):
            h_lines.append("    {")
            for pi in range(4):
                row = ",".join(f"0x{rgb_to_argb8888(r['tod_pals'][tod][pi][ci]):08X}u" for ci in range(16))
                h_lines.append(f"      {{ {row} }},")
            h_lines.append("    },")
        h_lines.append("  },")
    h_lines.append("};")
    h_lines.append("#endif /* HEARTH_INCLUDE_SDL2_BATTLE_BG */")
    h_lines.append("")
    h_lines.append("#endif /* HEARTH_BATTLE_BG_ROM_H */")
    h_lines.append("")

    with open(OUT_ROM_H, "w") as f:
        f.write("\n".join(h_lines))

    # -------------------------------------------------------------------------
    # WRITE 3: src/host/battleBgData.ts (Web Canvas 60-color 40x20 4bpp)
    # -------------------------------------------------------------------------
    ts_lines = [
        "// AUTO-GENERATED by engine/tools/bake_battle_bgs.py",
        "// 12 Pure Hand-Pixelated 16-Bit 3/4 Isometric Battle Stages (60-color + 4-ToD)",
        "",
        "import type { BattleBiome } from '../core/types';",
        "",
        "export interface BattleBgAsset {",
        "  readonly tilesB64: string;",
        "  readonly mapB64: string;",
        "  readonly palettes: readonly (readonly (readonly string[])[])[]; // [4 ToD][4 pals][16 hex]",
        "}",
        "",
        "export const BATTLE_BG_W = 320;",
        "export const BATTLE_BG_H = 160;",
        "export const BATTLE_BG_TW = 40;",
        "export const BATTLE_BG_TH = 20;",
        "",
        "export const BATTLE_BG_ASSETS: Record<BattleBiome, BattleBgAsset> = {",
    ]
    for r in results:
        tbytes = b"".join(pack_hi_4bpp_bytes(t) for t in r["hi_tiles"])
        mwords = [
            ((vf & 1) << 15) | ((hf & 1) << 14) | ((pal & 3) << 12) | (tid & 0x0FFF)
            for (tid, pal, hf, vf) in r["hi_map"]
        ]
        mbytes = struct.pack(f"<{len(mwords)}H", *mwords)
        tb64 = base64.b64encode(tbytes).decode("ascii")
        mb64 = base64.b64encode(mbytes).decode("ascii")
        ts_lines.append(f"  {r['name']}: {{")
        ts_lines.append(f"    tilesB64: '{tb64}',")
        ts_lines.append(f"    mapB64: '{mb64}',")
        ts_lines.append("    palettes: [")
        for tod in range(4):
            pstr = []
            for pi in range(4):
                hexs = ",".join(
                    f"'#{c[0]:02X}{c[1]:02X}{c[2]:02X}'" for c in r["tod_pals"][tod][pi]
                )
                pstr.append(f"[{hexs}]")
            ts_lines.append("      [" + ",".join(pstr) + "],")
        ts_lines.append("    ],")
        ts_lines.append("  },")
    ts_lines.append("};")
    ts_lines.append("")

    with open(OUT_WEB_TS, "w") as f:
        f.write("\n".join(ts_lines))

    print(f"Done! SNES total: {total_snes_bytes/1024:.1f} KB (12 SUPERFREE banks) | Genesis total: {total_gen_bytes/1024:.1f} KB")
    print(f"  Wrote: {OUT_ROM_H}")
    print(f"  Wrote: {OUT_SNES_ASM}")
    print(f"  Wrote: {OUT_WEB_TS}")


if __name__ == "__main__":
    main()
