#!/usr/bin/env python3
"""
bake_chrono_tileset.py — Project Hearth Unified 16-Bit Chrono/Mana Art Compiler

Authors a cohesive, hue-shifted late-16-bit JRPG tileset (inspired by Chrono Trigger,
Secret of Mana, and Trials of Mana) from a single pure-Python source of truth and
emits ready-to-render assets for ALL four Project Hearth hosts:

  1. engine/platform/hearth_terrain_rom.h
       - 18 Biome/Time-of-Day x 4 Sub-Palettes (60 simultaneous terrain colors!)
       - 18 Biome/Time-of-Day x 2 Genesis CRAM Palettes (30 simultaneous colors!)
       - Unified 138-metatile ID table + portable C89/C99 autotile/shadow/prop resolver
       - Lossless 16x16 indexed tilebank for Native PC (SDL2)
       - Pre-packed 4bpp ROM tilebank (<= 392 unique tiles) + 4-frame animations for Genesis
       - Pre-baked 4-palette tilemap lookup tables for SNES

  2. engine/platform/hearth_terrain_snes.asm
       - Pre-packed 4bpp SNES LoROM SUPERFREE tilebank (<= 448 unique tiles)
       - 4-frame contiguous animated tile bank (32 tiles x 4 frames = 1024 B VBlank DMA!)

  3. src/host/chronoTilesetData.ts
       - Exact same 18 x 4 x 16 biome palettes + compact base64 lossless 16x16 tiles
         and 4-frame animations for the Web Virtual PPU Renderer.

Zero external Python dependencies required (Python 3.8+ standard library only).
"""

import base64
import math
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT_H    = os.path.join(ROOT, "engine", "platform", "hearth_terrain_rom.h")
OUT_ASM  = os.path.join(ROOT, "engine", "platform", "hearth_terrain_snes.asm")
OUT_TS   = os.path.join(ROOT, "src", "host", "chronoTilesetData.ts")

# ==============================================================================
# 1. 18 BIOME / TIME-OF-DAY 4-SUB-PALETTE COLOR ARCHITECTURE (60 COLORS EACH!)
# ==============================================================================
# Sub-Palette 0 (TPAL_FOLIAGE  / SNES PAL0):
#   0: transparent
#   1: deepest cool forest shadow   2: dark moss/canopy shadow  3: shaded turf mid
#   4: main sunlit meadow turf      5: bright warm grass tip    6: golden sunlit leaf tip
#   7: deep dirt/bark shadow        8: rich earth/bark mid      9: sunlit ochre dirt/bark
#  10: pale sandy highlight        11: cream petal/clover      12: goldenrod flower
#  13: coral-rose blossom          14: periwinkle flower       15: canopy dapple shadow
#
# Sub-Palette 1 (TPAL_WATER_SKY / SNES PAL1 — 100% HARMONIZED WITH BG2 2bpp VISTA!):
#   1..3  (2bpp PAL4 Sky):   1=deep water/zenith sky, 2=cerulean mid, 3=sky mist/light aqua
#   4     (4bpp only):       4=deep ocean abyss
#   5..7  (2bpp PAL5 Ridge): 5=granite shadow, 6=lit granite, 7=snowcap/bright stone
#   8     (4bpp only):       8=cool indigo cast shadow
#   9..11 (2bpp PAL6 Pine):  9=deep alpine spruce, 10=lit spruce/moss, 11=distant ridge mist
#  12     (4bpp only):      12=translucent turquoise shallow
#  13..15 (2bpp PAL7 Foam): 13=soft cloud/foam shadow, 14=sunlit cream foam, 15=pure glint white
#
# Sub-Palette 2 (TPAL_ARCH_STONE / SNES PAL2 — Cottages, Cobblestone, Timber & Roofs):
#   1: deep beam/mortar outline     2: weathered stone shadow   3: mid dressed cobble
#   4: warm sunlit limestone        5: pale flagstone gleam     6: deep cedar/eave shadow
#   7: honey-cedar timber           8: sunlit cedar/plaster     9: warm cream stucco
#  10: deep terracotta shadow      11: rich terracotta shingle 12: sunlit copper shingle
#  13: warm lantern/window amber   14: bright hearth cream     15: soft moss-in-mortar
#
# Sub-Palette 3 (TPAL_BIOME_ACCENT / SNES PAL3 — Biome Signature Ramp):
#   1: deep outline/shadow          2: dark accent base         3: rich accent mid
#   4: vivid accent bright          5: sunlit accent tip        6: secondary dark
#   7: secondary mid                8: secondary bright         9: secondary cream
#  10: dark trim/slate             11: mid trim/metal          12: bright trim/gold
#  13: glow base                   14: glow bright             15: pure spark white

TPAL_FOLIAGE = 0
TPAL_WATER   = 1
TPAL_ARCH    = 2
TPAL_ACCENT  = 3

ZPAL_NAMES = [
    "ZPAL_HAMLET", "ZPAL_ALDERBROOK", "ZPAL_WHISKERBURGH", "ZPAL_SEWERS",
    "ZPAL_BOARHOLD", "ZPAL_SHADOW", "ZPAL_AUTUMN_MORNING", "ZPAL_AUTUMN_DUSK",
    "ZPAL_AUTUMN_NIGHT", "ZPAL_COTTAGE_INTERIOR", "ZPAL_EVERTALE_WORLD",
    "ZPAL_SUNHILL_MARKET", "ZPAL_DESERT", "ZPAL_PARADISE", "ZPAL_FROST",
    "ZPAL_JUNGLE", "ZPAL_VOLCANO", "ZPAL_TECH",
]

# Base Pastoral Chrono/Mana 4-Palette Set (Windmill Hamlet)
BASE_FOLIAGE = [
    0x000000, 0x162928, 0x244B38, 0x3A7442, 0x5EA04E, 0x8FCC60, 0xC8EA80,
    0x4C2E20, 0x7E5236, 0xB68452, 0xE2BC84, 0xF5EFE0, 0xF4CA4C, 0xE26468,
    0x78C2EA, 0x2B563D,
]
BASE_WATER = [
    0x000000, 0x1E4B82, 0x337BB8, 0x6EC2E4, 0x12305A, 0x3D475C, 0x768298,
    0xDCE6EE, 0x232E44, 0x1E4438, 0x3F7252, 0x688296, 0x44ACBE, 0x9AC6DC,
    0xE4F2F8, 0xFAFEFF,
]
BASE_ARCH = [
    0x000000, 0x241B22, 0x4B4E5C, 0x7C808E, 0xB2AEA4, 0xE2DDD0, 0x4E2B1E,
    0x8C5634, 0xC89058, 0xEED8AA, 0x6E2424, 0xB04432, 0xDE7444, 0xF6CA62,
    0xFFF4BC, 0x56824A,
]
BASE_ACCENT = [
    0x000000, 0x241620, 0x6E2028, 0xB4362C, 0xE06834, 0xF6B44A, 0x24485E,
    0x3C7C98, 0x72C4DC, 0xDCF4FC, 0x443648, 0x8A768C, 0xE6C868, 0xF29438,
    0xFFE278, 0xFFF8DC,
]

def clamp(v, lo=0, hi=255):
    return lo if v < lo else (hi if v > hi else int(v))

def rgb_unpack(c):
    return (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF

def rgb_pack(r, g, b):
    return (clamp(r) << 16) | (clamp(g) << 8) | clamp(b)

def blend_rgb(c1, c2, t):
    r1, g1, b1 = rgb_unpack(c1)
    r2, g2, b2 = rgb_unpack(c2)
    return rgb_pack(r1 + (r2 - r1) * t, g1 + (g2 - g1) * t, b1 + (b2 - b1) * t)

def tint_pal(pal, tint_rgb, strength, scale_r=1.0, scale_g=1.0, scale_b=1.0, skip_glow=False):
    out = [0]
    for i in range(1, 16):
        if skip_glow and i in (13, 14):
            out.append(pal[i])
            continue
        r, g, b = rgb_unpack(pal[i])
        r = clamp(r * scale_r)
        g = clamp(g * scale_g)
        b = clamp(b * scale_b)
        c = rgb_pack(r, g, b)
        if strength > 0:
            c = blend_rgb(c, tint_rgb, strength)
        out.append(c)
    return out

def build_zone_palettes():
    """Returns 18 x 4 x 16 rich Chrono/Mana RGB palettes."""
    z = {}

    # 0: ZPAL_HAMLET — Sunlit pastoral green meadow, cerulean creek, terracotta roofs
    z[0] = [list(BASE_FOLIAGE), list(BASE_WATER), list(BASE_ARCH), list(BASE_ACCENT)]

    # 1: ZPAL_ALDERBROOK — Crisp Autumn Mountain Afternoon (Ochre/Sage, Granite, Crimson Maples)
    alder_fol = [
        0x000000, 0x1D1E22, 0x2D4432, 0x566E3A, 0x8E8B40, 0xC6A84C, 0xECD072,
        0x482B1E, 0x7A4B30, 0xB27A48, 0xDFB87E, 0xF2E8D6, 0xF4BC44, 0xD85038,
        0x7EB4DA, 0x3C5434,
    ]
    alder_wat = [
        0x000000, 0x204A76, 0x3A7CAE, 0x82C4E4, 0x143052, 0x3C4252, 0x70788A,
        0xDCE2EA, 0x242A3A, 0x1E3C32, 0x3A6248, 0x627488, 0x489CB4, 0x9EC2D8,
        0xE4EEF6, 0xF8FCFF,
    ]
    alder_arch = [
        0x000000, 0x241A1E, 0x484A58, 0x787C8A, 0xAEABA4, 0xDFDBD0, 0x4A291C,
        0x844E2E, 0xBE844E, 0xE8CCA0, 0x6C2220, 0xB03C2A, 0xDC6C3C, 0xF8C456,
        0xFFF0AE, 0x587242,
    ]
    alder_acc = [
        0x000000, 0x28141A, 0x6E1E22, 0xB83226, 0xE2622C, 0xF6B040, 0x244234,
        0x3E6A48, 0x78A85A, 0xD8E89A, 0x4A3A44, 0x887A86, 0xE8C460, 0xF48A30,
        0xFFD868, 0xFFF6D4,
    ]
    z[1] = [alder_fol, alder_wat, alder_arch, alder_acc]

    # 2: ZPAL_WHISKERBURGH — Bright Ghibli Mouse Market (Cream Limestone, Cheese-Gold & Terracotta)
    z[2] = [
        [0x000000, 0x22201E, 0x325E3C, 0x508C4E, 0x7EB860, 0xB4DA78, 0xDFF098,
         0x583422, 0x905E3A, 0xCC9860, 0xF2D494, 0xFAF2E2, 0xF6CE4C, 0xE46050,
         0x78C8E8, 0x3A6C44],
        list(BASE_WATER),
        [0x000000, 0x281E1C, 0x5E5450, 0x968A7E, 0xC8BCAC, 0xF0E6D4, 0x583222,
         0x98603A, 0xD29C62, 0xF6DFB0, 0x7E2822, 0xC84E36, 0xEC824C, 0xFAD266,
         0xFFF6C2, 0x628E50],
        list(BASE_ACCENT),
    ]

    # 3: ZPAL_SEWERS — Atmospheric Lantern-Lit Sunken Sewers (Deep Slate, Emerald Moss & Amber Glow)
    z[3] = [
        [0x000000, 0x0B1018, 0x152428, 0x223B38, 0x345848, 0x4E7C5C, 0x78A874,
         0x261E1E, 0x40342E, 0x625244, 0x88745E, 0xC8C0AE, 0xF0B848, 0xC85848,
         0x52B8B0, 0x182C2E],
        [0x000000, 0x0E2A36, 0x1A4B56, 0x347E82, 0x0A1C26, 0x1E2636, 0x38465C,
         0x6C7E96, 0x121824, 0x142A2A, 0x244842, 0x44586A, 0x28686C, 0x589A9C,
         0x96D6D0, 0xD2F4F0],
        [0x000000, 0x0E1018, 0x222A3A, 0x3B485C, 0x5C6C80, 0x8A98AA, 0x2A201E,
         0x4A3A30, 0x6E5846, 0x9E8466, 0x422428, 0x6C3836, 0x9A5442, 0xF2B244,
         0xFFE68C, 0x325246],
        [0x000000, 0x100E1A, 0x341E3C, 0x5C3468, 0x8C589C, 0xC290D0, 0x1A3E44,
         0x2E7474, 0x54B8B0, 0xAEF0E4, 0x343848, 0x666E82, 0xD8A248, 0xF49A34,
         0xFFE078, 0xFFF6C8],
    ]

    # 4: ZPAL_BOARHOLD — Sandstone Citadel, Forged Iron & Brass Braziers
    z[4] = [
        [0x000000, 0x1C1418, 0x3E2E2A, 0x62483A, 0x8E684E, 0xBA9068, 0xE2BC8A,
         0x40221C, 0x703E2E, 0xA66644, 0xD69A6A, 0xEED8B4, 0xF2B644, 0xD24438,
         0x8A7898, 0x4C342E],
        [0x000000, 0x2E1A2A, 0x542C3E, 0x884852, 0x1C101C, 0x342E3E, 0x625A6E,
         0xA69EA8, 0x201A28, 0x3A2A2E, 0x644844, 0x846A70, 0x7A3844, 0xB86A5C,
         0xEEAC7A, 0xFFF0C8],
        [0x000000, 0x1E1418, 0x423844, 0x726470, 0xA49294, 0xD4C4BA, 0x46241E,
         0x7E4632, 0xB6764E, 0xE4B27E, 0x621E24, 0xA23232, 0xD65C3E, 0xF6B846,
         0xFFEA96, 0x645440],
        [0x000000, 0x1E1216, 0x681A20, 0xB22C2A, 0xE85C34, 0xFCAE48, 0x3A2C40,
         0x6A5470, 0xA288A4, 0xD8C6D8, 0x44363A, 0x867268, 0xEAB850, 0xF68E32,
         0xFFE074, 0xFFF6CE],
    ]

    # 5: ZPAL_SHADOW — Eerie Violet Twilight (Age-5 Shadow Grandmother Vision)
    z[5] = [
        tint_pal(BASE_FOLIAGE, 0x3C2268, 0.52, 0.72, 0.56, 1.05),
        tint_pal(BASE_WATER,   0x2C1856, 0.50, 0.78, 0.58, 1.08),
        tint_pal(BASE_ARCH,    0x361E5E, 0.52, 0.76, 0.58, 1.04, skip_glow=True),
        [0x000000, 0x140C24, 0x381C58, 0x66368C, 0x9A60C2, 0xD4A2F0, 0x241A46,
         0x443278, 0x765EB4, 0xB8A6E4, 0x382448, 0x704E82, 0xC85284, 0xE472A8,
         0xFFB4D8, 0xFFF0FA],
    ]

    # 6: ZPAL_AUTUMN_MORNING — Rose-Gold Mountain Dawn Mist
    z[6] = [
        tint_pal(alder_fol,  0xF4B898, 0.22, 1.04, 1.00, 1.02),
        tint_pal(alder_wat,  0xE8B4C4, 0.20, 1.05, 0.98, 1.05),
        tint_pal(alder_arch, 0xF6C4A4, 0.20, 1.04, 1.00, 1.02, skip_glow=True),
        tint_pal(alder_acc,  0xF4B8A8, 0.18, 1.04, 1.00, 1.02, skip_glow=True),
    ]

    # 7: ZPAL_AUTUMN_DUSK — Copper-Amber Alpine Sunset & Warm Lanterns
    z[7] = [
        tint_pal(alder_fol,  0xD86434, 0.30, 1.02, 0.86, 0.76),
        tint_pal(alder_wat,  0x9C4E68, 0.36, 1.00, 0.78, 0.88),
        tint_pal(alder_arch, 0xC85C36, 0.28, 1.00, 0.86, 0.78, skip_glow=True),
        tint_pal(alder_acc,  0xE27030, 0.24, 1.04, 0.90, 0.76, skip_glow=True),
    ]

    # 8: ZPAL_AUTUMN_NIGHT — Moonlit Indigo Ridge, Silver Rapids & Glowing Gold Lanterns
    z[8] = [
        tint_pal(alder_fol,  0x18264A, 0.56, 0.56, 0.66, 0.92),
        tint_pal(alder_wat,  0x122248, 0.46, 0.58, 0.72, 1.02),
        tint_pal(alder_arch, 0x1A2444, 0.52, 0.58, 0.66, 0.90, skip_glow=True),
        tint_pal(alder_acc,  0x1C2042, 0.48, 0.68, 0.66, 0.88, skip_glow=True),
    ]

    # 9: ZPAL_COTTAGE_INTERIOR — Cozy Honey Cedar Planks, Hearthstone, Burgundy Rug & Firelight
    z[9] = [
        [0x000000, 0x201514, 0x304B36, 0x4E7246, 0x7A9C56, 0xB4C86C, 0xE2DC94,
         0x4A2918, 0x7E4A2A, 0xB87A46, 0xE4B87A, 0xF6ECD8, 0xF6C654, 0xD8503E,
         0x86BCE0, 0x382820],
        [0x000000, 0x2A4870, 0x4A7EA8, 0x94CAE8, 0x1A3050, 0x443E44, 0x7A7276,
         0xCAC2B8, 0x2C2224, 0x284234, 0x4A6E4E, 0x747882, 0x5898B8, 0xB2D0E2,
         0xE8F0F4, 0xFFFDF8],
        [0x000000, 0x221512, 0x4A4246, 0x7A7274, 0xB2AAA2, 0xE0D8CC, 0x4C2816,
         0x8A522C, 0xC6884E, 0xECC88E, 0x6A2220, 0xAE3C30, 0xDC6C3C, 0xF8C858,
         0xFFF2B6, 0x627848],
        [0x000000, 0x241214, 0x681A20, 0xA82C32, 0xD84C3C, 0xF6A246, 0x2C4C72,
         0x4E7EA8, 0x8CBEE0, 0xE4F2FC, 0x543A26, 0x9E723C, 0xECC458, 0xF89834,
         0xFFE274, 0xFFF8D8],
    ]

    # 10: ZPAL_EVERTALE_WORLD — Lush Chrono Storybook Continent (Emerald Hills, Cerulean Sea, Snow Peaks)
    z[10] = [
        [0x000000, 0x142622, 0x225238, 0x3A8248, 0x62B256, 0x9AD86C, 0xD2EE8C,
         0x523422, 0x8A5E3A, 0xC69658, 0xF0D286, 0xF8F2E4, 0xF6CE4C, 0xE26258,
         0x7AD0F2, 0x2A603E],
        [0x000000, 0x18508C, 0x2E84C4, 0x6EC8EC, 0x103464, 0x3C4A60, 0x7C8DA4,
         0xEBF2F8, 0x20304A, 0x1B4A38, 0x3A7A4E, 0x6A869E, 0x3CB6CE, 0x98D2EA,
         0xE2F4FC, 0xFCFFFF],
        [0x000000, 0x221A20, 0x4B505E, 0x828896, 0xB8B6AE, 0xEEEBE0, 0x522E20,
         0x925C38, 0xCE985E, 0xF4DCAC, 0x762624, 0xC04834, 0xEA7846, 0xFAD064,
         0xFFF5BC, 0x568A4C],
        [0x000000, 0x241620, 0x7A2434, 0xC6424A, 0xEC7C64, 0xF8C678, 0x1E6254,
         0x34A488, 0x74DEC0, 0xD8FAF0, 0x4A4458, 0x8E88A2, 0xF0CA60, 0xF69638,
         0xFFE478, 0xFFFAD8],
    ]

    # 11: ZPAL_SUNHILL_MARKET — Sunlit Hilltop Limestone, Terracotta Awnings & Royal Gold
    z[11] = [
        [0x000000, 0x1B2624, 0x2A583A, 0x46884C, 0x72B65C, 0xA8DA76, 0xD8F096,
         0x563424, 0x8C5E3C, 0xC8925A, 0xF0D28E, 0xFAF4E4, 0xF8D252, 0xE65E52,
         0x78C6EE, 0x326440],
        [0x000000, 0x205A94, 0x3E90CE, 0x88D0F4, 0x143A68, 0x4C4E5C, 0x8C8E9A,
         0xECEAE2, 0x2C3448, 0x26523C, 0x488254, 0x7C8C9E, 0x4CB4D0, 0xA8D4EC,
         0xECF6FC, 0xFFFFFF],
        [0x000000, 0x241C20, 0x54545E, 0x8E8C8A, 0xC4BEB2, 0xF0EBE0, 0x543022,
         0x965E3A, 0xD0985E, 0xF4DDB0, 0x782824, 0xC24834, 0xEC7C48, 0xFAD266,
         0xFFF6C2, 0x5E8C4E],
        [0x000000, 0x26181E, 0x7E2428, 0xC84238, 0xEE7848, 0xFAD062, 0x24587C,
         0x3E8EC0, 0x7CCAE8, 0xE0F6FC, 0x544854, 0x9C8E98, 0xF2CE62, 0xF8983C,
         0xFFE680, 0xFFFADC],
    ]

    # 12: ZPAL_DESERT — Sands of Babana (Warm Amber Dunes, Sandstone & Oasis Jade)
    z[12] = [
        [0x000000, 0x2A1A14, 0x366032, 0x568E44, 0x88BC5A, 0xC6D872, 0xEAE498,
         0x6E3C1E, 0xAE6E34, 0xDF9E4C, 0xF8D47C, 0xFCF0C8, 0xFCE05C, 0xDE5838,
         0x5CC8C4, 0x467238],
        [0x000000, 0x1A5878, 0x3090A8, 0x7AD4DC, 0x123C54, 0x6A4834, 0xAC7E54,
         0xF0D8AE, 0x3A2824, 0x285636, 0x4A884C, 0x94765A, 0x42B4BC, 0x9EDCE0,
         0xE6F8F6, 0xFCFFFF],
        [0x000000, 0x2A1A14, 0x6E4A34, 0xAA7E54, 0xDAB27C, 0xF6E0B4, 0x5A301A,
         0x9A5E30, 0xD29450, 0xF4CE8A, 0x7A2622, 0xC44832, 0xEC7C44, 0xFAD25E,
         0xFFF5B8, 0x568846],
        [0x000000, 0x2A1614, 0x822424, 0xCC4432, 0xF27E44, 0xFCD668, 0x1A6264,
         0x32A09A, 0x70DAD0, 0xD8FAF4, 0x6A4A36, 0xB88E64, 0xF6CE58, 0xFA9A38,
         0xFFE67C, 0xFFFAD8],
    ]

    # 13: ZPAL_PARADISE — Suncrest Paradise (Turquoise Shallows, Coral Pink & Palm Green)
    z[13] = [
        [0x000000, 0x14262A, 0x1E6040, 0x349654, 0x5CC666, 0x96E47E, 0xD2F6A2,
         0x5A3A24, 0x966840, 0xD6A668, 0xF6DC9E, 0xFCF6E6, 0xFCD654, 0xF0688C,
         0x62DCF0, 0x28744A],
        [0x000000, 0x126288, 0x24A0BC, 0x6EE0EC, 0x0C3E62, 0x445868, 0x88A0AC,
         0xF0F4F4, 0x1E384E, 0x1C5A44, 0x389462, 0x72A2AE, 0x3CCCD4, 0x98ECF2,
         0xE4FAF8, 0xFFFFFF],
        [0x000000, 0x241E22, 0x545A66, 0x8C949E, 0xC6C4BC, 0xF2EEE4, 0x563222,
         0x9A623C, 0xD69C64, 0xF6DFB0, 0x862A44, 0xD25270, 0xF688A0, 0xFAD466,
         0xFFF6C2, 0x46985C],
        [0x000000, 0x261624, 0x88244A, 0xD44874, 0xF67CA0, 0xFCD0DC, 0x147080,
         0x2EB8C4, 0x78EAE8, 0xDCFCFA, 0x56485A, 0x9E8EA2, 0xFAD45E, 0xFA9A46,
         0xFFE888, 0xFFFCE0],
    ]

    # 14: ZPAL_FROST — Frostmere North (Crisp Snow White, Ice Cyan & Festive Evergreen)
    z[14] = [
        [0x000000, 0x142234, 0x1E4A44, 0x327462, 0x66A492, 0xB8DCE6, 0xEFF8FE,
         0x3E3444, 0x6C5C6E, 0xA498AA, 0xDCE2EE, 0xFAFDFF, 0xF8D466, 0xD8444E,
         0x78D4FA, 0x26564E],
        [0x000000, 0x1C4C86, 0x3A82BC, 0x84CCF0, 0x12305A, 0x4A5E7C, 0x8CA2BE,
         0xEEF6FC, 0x263854, 0x1C4646, 0x367268, 0x7C9AB4, 0x5CBCE2, 0xB4E2F6,
         0xECF8FE, 0xFFFFFF],
        [0x000000, 0x1E1C2A, 0x4C5872, 0x8290AA, 0xBCC6D8, 0xEEF2F8, 0x482E28,
         0x825444, 0xBC8A72, 0xE6CCB8, 0x6E222E, 0xB83A44, 0xE26A6C, 0xFAD268,
         0xFFF6C4, 0x4A7C70],
        [0x000000, 0x18182C, 0x781E2E, 0xC43844, 0xEC6C6E, 0xFCD0BE, 0x1E5A88,
         0x4298CC, 0x8CD8F8, 0xE4F8FF, 0x4C5C78, 0x94A8C4, 0xFAD66C, 0xF8A64A,
         0xFFE88E, 0xFFFFFF],
    ]

    # 15: ZPAL_JUNGLE — Canopy Wilds (Deep Emerald Rainforest, Jade River & Cedar Bridges)
    z[15] = [
        [0x000000, 0x0E201C, 0x18442C, 0x28703A, 0x44A048, 0x74CA58, 0xB4E878,
         0x3E2418, 0x6C422A, 0xA26C44, 0xD4A26E, 0xF2ECD8, 0xF8CE48, 0xE85458,
         0x52D0BA, 0x1C5232],
        [0x000000, 0x124A5C, 0x208088, 0x52C2BC, 0x0A2E3C, 0x34444E, 0x64787E,
         0xB8CCC8, 0x182C32, 0x143E2C, 0x2A6E44, 0x548272, 0x34A89A, 0x8CD8CC,
         0xD4F4EC, 0xF4FFFC],
        [0x000000, 0x1C1818, 0x3E4646, 0x6C7872, 0x9EA89C, 0xD4DCD0, 0x422416,
         0x7A482A, 0xB47848, 0xE0B47E, 0x6A2622, 0xB04632, 0xDE7442, 0xF6C858,
         0xFFF2B6, 0x3E7C44],
        [0x000000, 0x1A141E, 0x7A1E3A, 0xC83C5C, 0xF27284, 0xFCC49A, 0x165E4C,
         0x2E9E7A, 0x6ADCA8, 0xD0FAE4, 0x42404E, 0x80828E, 0xF4CC52, 0xF89438,
         0xFFE278, 0xFFF8D8],
    ]

    # 16: ZPAL_VOLCANO — Cinderpeak Caldera (Basalt Charcoal, Crimson Magma & Ash)
    z[16] = [
        [0x000000, 0x181014, 0x302226, 0x4E383A, 0x745452, 0xA27A70, 0xD4AE9C,
         0x381E1C, 0x62342C, 0x985440, 0xCE8464, 0xEAD2BC, 0xFAAC3C, 0xE84428,
         0x9A7288, 0x3E282C],
        [0x000000, 0x58161A, 0xA8281E, 0xF06828, 0x300C12, 0x342A32, 0x5E505A,
         0x9A8C94, 0x20161E, 0x362228, 0x5E3E42, 0x7C626C, 0xDC4822, 0xF89834,
         0xFFE068, 0xFFF8C8],
        [0x000000, 0x1A1216, 0x362E36, 0x5E525C, 0x8E8088, 0xC2B4B8, 0x40201A,
         0x723A2A, 0xAA6244, 0xDC986E, 0x6A1A1A, 0xB82E22, 0xEC622C, 0xFAAC38,
         0xFFE878, 0x6E4E48],
        [0x000000, 0x1E0E14, 0x76161C, 0xC82C22, 0xF46C2C, 0xFCE060, 0x3A2634,
         0x6A4C5E, 0xA28092, 0xD8BEC6, 0x3E343C, 0x7A6C76, 0xF6B442, 0xFA842C,
         0xFFE26C, 0xFFF8D0],
    ]

    # 17: ZPAL_TECH — Aetheris Citadel (Ivory Alloy, Cobalt Steel & Electric Cyan)
    z[17] = [
        [0x000000, 0x141C2A, 0x244A58, 0x387886, 0x58B0B8, 0x92DCE0, 0xD6F6F8,
         0x323E54, 0x586A88, 0x8CA2C2, 0xCAD8EC, 0xF2F7FC, 0xF4D468, 0x56E4FF,
         0x9C8EF0, 0x2A5868],
        [0x000000, 0x163C74, 0x2A6CB4, 0x62C4F6, 0x0E2448, 0x384864, 0x7086A8,
         0xDCE6F4, 0x1E2A42, 0x24485E, 0x427A96, 0x7898B8, 0x3CE0F8, 0x9AEAFE,
         0xE2F8FF, 0xFFFFFF],
        [0x000000, 0x181E2E, 0x3E4C66, 0x7284A2, 0xACBCCE, 0xE6EEF6, 0x384258,
         0x667896, 0x9EB2CE, 0xDCE6F2, 0x2A4A7C, 0x4478B8, 0x74B2EA, 0x56E8FF,
         0xC8FAFF, 0x4A8C96],
        [0x000000, 0x141A2C, 0x204A88, 0x3686CC, 0x56D8FF, 0xC8FAFF, 0x3C2C6E,
         0x6C54B2, 0xA892EA, 0xE4DCFC, 0x4A5872, 0x8EA0BC, 0xF6D468, 0x58E8FF,
         0xB8F8FF, 0xFFFFFF],
    ]

    return [z[i] for i in range(18)]

def build_genesis_palettes(zone_pals):
    """
    Build harmonized 2-palette (30 simultaneous terrain colors!) sets for Sega Genesis:
      GEN_PAL0: Lush Foliage + Warm Earth/Timber/Terracotta (indices 1..15)
      GEN_PAL3: Water/Shallows/Foam + Weathered Stone/Cobble + Cast Shadow + Biome Accent
    """
    gen = []
    for pid in range(len(zone_pals)):
        fol, wat, arch, acc = zone_pals[pid]
        # GEN PAL0 (15 colors):
        #  1: deep shadow    2: dark green     3: mid green     4: lit green     5: bright grass
        #  6: deep dirt/wood 7: mid dirt/wood  8: lit wood/sand 9: cream stucco 10: dark roof
        # 11: mid terracotta 12: lit roof/acc 13: lantern gold 14: blossom/cream 15: bright gold/accent
        p0 = [
            0x000000,
            fol[1], fol[2], fol[3], fol[4], fol[5],
            arch[6], arch[7], arch[8], arch[9],
            arch[10], arch[11], arch[12],
            arch[13], fol[11], acc[4],
        ]
        # GEN PAL3 (15 colors):
        #  1: deep slate (also crisp PAL3 UI text bg!)  2: deep water   3: mid cerulean
        #  4: turquoise shallow  5: bright foam white   6: stone dark   7: cobble mid
        #  8: cobble lit         9: pale flagstone     10: cast shadow
        # 11: accent dark       12: accent mid         13: accent bright
        # 14: warm gold (glow/emote)  15: bright sunlit cream-cyan (also crisp PAL3 UI highlight text!)
        p3 = [
            0x000000,
            0x161826, wat[1], wat[2], wat[12], wat[15],
            arch[2], arch[3], arch[4], arch[5], wat[8],
            acc[2], acc[3], acc[4], 0xF8CC58, 0xE6F8FF,
        ]
        gen.append((p0, p3))
    return gen

def rgb_to_snes555(rgb):
    r, g, b = rgb_unpack(rgb)
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)

def rgb_to_gen333(rgb):
    r, g, b = rgb_unpack(rgb)
    return ((b >> 5) << 9) | ((g >> 5) << 5) | ((r >> 5) << 1)

# ==============================================================================
# 2. METATILE CATALOG (138 METATILES SHARED ACROSS ALL 4 PLATFORMS)
# ==============================================================================
META_NAMES = [
    # 0..62: Base BG1 tile IDs (exact 1:1 match with T_GRASS..T_DOCK_PIER!)
    "T_GRASS", "T_GRASS_ALT", "T_FLOWERS", "T_TALL_GRASS", "T_DIRT",
    "T_COBBLE", "T_WATER", "T_FORD", "T_BRIDGE", "T_BRIDGE_RAIL_N",
    "T_BRIDGE_RAIL_S", "T_FENCE", "T_TREE_TRUNK", "T_BUSH", "T_WALL_WOOD",
    "T_WALL_WINDOW", "T_WALL_DOOR", "T_ROOF_BODY", "T_ROOF_TOP", "T_WINDMILL_WALL",
    "T_WINDMILL_DOOR", "T_WINDMILL_ROOF", "T_SHRINE_FLOOR", "T_SHRINE_RUNE", "T_SHRINE_PILLAR",
    "T_SIGNPOST", "T_LANTERN_POST", "T_FLOWER_BED", "T_MTN_SKY", "T_CLIFF_WALL",
    "T_CLIFF_LEDGE", "T_BOULDER", "T_AUTUMN_GRASS", "T_ROCKY_PATH", "T_RAPIDS",
    "T_RAIL_TRACK", "T_MAPLE_TRUNK", "T_PINE_TRUNK", "T_WOOD_FLOOR", "T_HEARTH_RUG",
    "T_FIREPLACE", "T_BED_QUILT", "T_ATTIC_CHEST", "T_HOLLOW_OAK", "T_STONE_STAIRS",
    "T_INTERIOR_WALL", "T_WORLD_OCEAN", "T_WORLD_BEACH", "T_WORLD_PEAK", "T_WORLD_FOREST",
    "T_WORLD_HILL", "T_WORLD_CASTLE", "T_WORLD_TOWN", "T_WORLD_RUINS", "T_WORLD_SHALLOW",
    "T_WORLD_SAND", "T_WORLD_SNOW", "T_WORLD_LAVA", "T_WORLD_JUNGLE", "T_WORLD_CORAL",
    "T_WORLD_TECH", "T_PALM_TREE", "T_DOCK_PIER",
    # 63..72: Overhead BG2 canopies, eaves & shrine arch (exact match with O_CANOPY_TL..O_OAK_CROWN)
    "CM_OV_CANOPY_TL", "CM_OV_CANOPY_TR", "CM_OV_CANOPY_BL", "CM_OV_CANOPY_BR",
    "CM_OV_ROOF_EAVE", "CM_OV_SHRINE_ARCH", "CM_OV_MAPLE_TL", "CM_OV_MAPLE_TR",
    "CM_OV_PINE_TOP", "CM_OV_OAK_CROWN",
    # 73..88: Organic Spatial Ground Variations (eliminates wallpaper tiling!)
    "CM_GRASS_V1", "CM_GRASS_V2", "CM_GRASS_V3",
    "CM_AUTUMN_V1", "CM_AUTUMN_V2",
    "CM_COBBLE_V1", "CM_COBBLE_V2", "CM_COBBLE_V3",
    "CM_DIRT_V1", "CM_ROCKY_V1", "CM_WOOD_FLOOR_V1", "CM_SAND_V1",
    "CM_SNOW_V1", "CM_JUNGLE_V1", "CM_SHRINE_V1", "CM_OCEAN_V1",
    # 89..104: Rounded Shorelines & Feathered Organic Path/Plaza Transitions
    "CM_SHORE_N", "CM_SHORE_S", "CM_SHORE_W", "CM_SHORE_E",
    "CM_SHORE_NW", "CM_SHORE_NE", "CM_SHORE_SW", "CM_SHORE_SE",
    "CM_COBBLE_EDGE_N", "CM_COBBLE_EDGE_S", "CM_COBBLE_EDGE_W", "CM_COBBLE_EDGE_E",
    "CM_DIRT_EDGE_N", "CM_DIRT_EDGE_S", "CM_DIRT_EDGE_W", "CM_DIRT_EDGE_E",
    # 105..114: Cast Ground Shadows & Canopy Dapple
    "CM_GRASS_SHAD_N", "CM_GRASS_SHAD_W", "CM_COBBLE_SHAD_N", "CM_COBBLE_SHAD_W",
    "CM_DIRT_SHAD_N", "CM_AUTUMN_SHAD_N", "CM_ROCKY_SHAD_N", "CM_WOOD_SHAD_N",
    "CM_GRASS_DAPPLE", "CM_AUTUMN_DAPPLE",
    # 115..128: Seamless Ground-Composited Props (ZERO green boxes on stone/pier/dirt!)
    "CM_LAMP_COBBLE", "CM_SIGN_COBBLE", "CM_FLOWERBED_COBBLE", "CM_BUSH_COBBLE",
    "CM_FENCE_COBBLE", "CM_LAMP_PIER", "CM_SIGN_PIER", "CM_BOULDER_PIER",
    "CM_LAMP_AUTUMN", "CM_SIGN_AUTUMN", "CM_BOULDER_AUTUMN",
    "CM_LAMP_DIRT", "CM_SIGN_DIRT", "CM_PIER_WATER_EDGE",
    # 129..137: Battle Backdrops, Evertale Coastlines & Empty Vista Sky
    "CM_BATTLE_SKY_MEADOW", "CM_BATTLE_SKY_SHRINE", "CM_BATTLE_GND_MEADOW", "CM_BATTLE_GND_SHRINE",
    "CM_WORLD_COAST_N", "CM_WORLD_COAST_S", "CM_WORLD_COAST_W", "CM_WORLD_COAST_E",
    "CM_EMPTY",
    # 138..147: Opaque-Backed Overhead Canopies & Eaves (SNES BG1 & Genesis Parallax BG_A — zero sky bleed!)
    "CM_OPQ_CANOPY_TL", "CM_OPQ_CANOPY_TR", "CM_OPQ_CANOPY_BL", "CM_OPQ_CANOPY_BR",
    "CM_OPQ_ROOF_EAVE", "CM_OPQ_SHRINE_ARCH", "CM_OPQ_MAPLE_TL", "CM_OPQ_MAPLE_TR",
    "CM_OPQ_PINE_TOP", "CM_OPQ_OAK_CROWN",
]
META_IDX = {name: i for i, name in enumerate(META_NAMES)}

# ==============================================================================
# 3. 16x16 CHRONO / MANA PIXEL ART DRAWING HELPERS
# ==============================================================================
class Tile16:
    """16x16 tile with per-quadrant 4-palette assignment (SNES/Web/SDL2) and pixel matrix."""
    def __init__(self, pal=TPAL_FOLIAGE, fill=0, prio=0):
        self.px = [[fill] * 16 for _ in range(16)]
        self.qpal = [pal, pal, pal, pal]  # TL, TR, BL, BR
        self.prio = prio

    def clone(self):
        t = Tile16(self.qpal[0], 0, self.prio)
        t.px = [row[:] for row in self.px]
        t.qpal = self.qpal[:]
        return t

    def p(self, x, y, c):
        if 0 <= x < 16 and 0 <= y < 16:
            self.px[y][x] = c

    def rect(self, x, y, w, h, c):
        for yy in range(max(0, y), min(16, y + h)):
            row = self.px[yy]
            for xx in range(max(0, x), min(16, x + w)):
                row[xx] = c

    def hline(self, x, y, w, c):
        self.rect(x, y, w, 1, c)

    def vline(self, x, y, h, c):
        self.rect(x, y, 1, h, c)

    def disc(self, cx, cy, rx, ry, c):
        for y in range(16):
            for x in range(16):
                dx = (x - cx) / max(0.5, rx)
                dy = (y - cy) / max(0.5, ry)
                if dx * dx + dy * dy <= 1.0:
                    self.px[y][x] = c

    def stamp(self, art, ox=0, oy=0):
        """Stamp hex string grid ('.' = transparent, '1'..'f' = color index)."""
        for y, line in enumerate(art):
            yy = oy + y
            if not (0 <= yy < 16):
                continue
            for x, ch in enumerate(line):
                xx = ox + x
                if not (0 <= xx < 16) or ch in ('.', ' '):
                    continue
                self.px[yy][xx] = int(ch, 16)

    def flip_h(self):
        t = self.clone()
        t.px = [row[::-1] for row in self.px]
        t.qpal = [self.qpal[1], self.qpal[0], self.qpal[3], self.qpal[2]]
        return t

    def flip_v(self):
        t = self.clone()
        t.px = self.px[::-1]
        t.qpal = [self.qpal[2], self.qpal[3], self.qpal[0], self.qpal[1]]
        return t

    def overlay_from(self, src):
        for y in range(16):
            for x in range(16):
                c = src.px[y][x]
                if c != 0:
                    self.px[y][x] = c

    def darken_strip_n(self, depth=4, ramp=None):
        """Apply soft cool cast shadow along top rows (under cottage walls/cliffs)."""
        if ramp is None:
            ramp = {6: 5, 5: 4, 4: 3, 3: 2, 2: 1, 10: 9, 9: 8, 8: 7, 11: 4, 12: 8, 13: 8}
        for y in range(depth):
            for x in range(16):
                if y == depth - 1 and ((x + y) & 1):
                    continue
                c = self.px[y][x]
                if c in ramp:
                    self.px[y][x] = ramp[c]

    def darken_strip_w(self, depth=4, ramp=None):
        if ramp is None:
            ramp = {6: 5, 5: 4, 4: 3, 3: 2, 2: 1, 10: 9, 9: 8, 8: 7, 11: 4, 12: 8, 13: 8}
        for y in range(16):
            for x in range(depth):
                if x == depth - 1 and ((x + y) & 1):
                    continue
                c = self.px[y][x]
                if c in ramp:
                    self.px[y][x] = ramp[c]

    def dapple_canopy(self, ramp=None):
        """Apply painterly dappled canopy shadow beneath tree crowns."""
        if ramp is None:
            ramp = {6: 4, 5: 3, 4: 3, 3: 2, 2: 1, 10: 8, 9: 8, 8: 7}
        for y in range(16):
            for x in range(16):
                dx = (x - 7.5) / 7.2
                dy = (y - 7.5) / 6.2
                d = dx * dx + dy * dy
                if d < 0.78 or (d < 1.05 and ((x + y) & 1) == 0):
                    # Leave 2 small sunlit leaf gaps for Chrono/Mana dappled charm
                    if (x in (5, 6) and y in (5, 6)) or (x in (10, 11) and y in (9, 10)):
                        continue
                    c = self.px[y][x]
                    if c in ramp:
                        self.px[y][x] = ramp[c]

# ==============================================================================
# 4. HANDCRAFTED CHRONO TRIGGER / TRIALS OF MANA 16x16 PIXEL ART LIBRARY
# ==============================================================================
def make_grass(variant=0):
    """Lush, painterly Chrono/Mana sunlit meadow turf with clustered soft blades."""
    t = Tile16(TPAL_FOLIAGE, 4)
    # Soft rolling organic tonal patches (low-contrast, restful background!)
    patterns = [
        [
            "4444445554444444",
            "4444555555444344",
            "4434455544443344",
            "4333444444433444",
            "4434444444444444",
            "4444444455444444",
            "4444445555544444",
            "4444444554444434",
            "4443444444444334",
            "4433344444443344",
            "4443444444444444",
            "4554444444445544",
            "5555444444455544",
            "4554444344445444",
            "4444443334444444",
            "4444444344444444",
        ],
        [
            "4444444444554444",
            "4434444445555444",
            "4333444444554444",
            "4434444444444444",
            "4444554444444434",
            "4445555444444334",
            "4444554444443344",
            "4444444443444444",
            "4444444433344444",
            "4455444443444444",
            "4555544444444554",
            "4455444444445554",
            "4444443444444544",
            "4444433344444444",
            "4444443444444444",
            "4444444444444444",
        ],
        [
            "4444554444444444",
            "4445555444344444",
            "4444554443334444",
            "4444444444344444",
            "4344444444445544",
            "3334444444455554",
            "4344444554445544",
            "4444445555444444",
            "4444444554444344",
            "4444444444443334",
            "4434444444444344",
            "4333444444444444",
            "4434444455444444",
            "4444444555444444",
            "4444444454444444",
            "4444444444444444",
        ],
        [
            "4444444444444444",
            "4455444444434444",
            "4555544444333444",
            "4455444444434444",
            "4444444344444444",
            "4444443334445544",
            "4444444344455554",
            "4434444444445544",
            "4333444444444444",
            "4434445544444444",
            "4444455554434444",
            "4444445544333444",
            "4444444444434444",
            "4455444444444444",
            "4555444444444444",
            "4454444444444444",
        ],
    ]
    t.stamp(patterns[variant & 3])
    # Delicate hand-placed grass blade clusters + tiny sunny clover highlights
    if variant == 0:
        t.p(6, 1, 6); t.p(7, 2, 5); t.p(3, 3, 2); t.p(12, 8, 2); t.p(1, 12, 6)
    elif variant == 1:
        t.p(11, 1, 6); t.p(5, 5, 6); t.p(9, 8, 2); t.p(2, 10, 6); t.p(6, 13, 2)
    elif variant == 2:
        t.p(4, 1, 6); t.p(12, 5, 6); t.p(8, 7, 6); t.p(2, 11, 2); t.p(13, 9, 2)
        t.p(10, 3, 11)  # tiny white clover bud
    else:
        t.p(2, 2, 6); t.p(12, 6, 6); t.p(6, 10, 6); t.p(11, 11, 2); t.p(7, 5, 2)
        t.p(13, 13, 12)  # tiny golden clover fleck
    return t

def make_flowers(frame=0):
    t = make_grass(0)
    dx = 1 if (frame in (1, 2)) else 0
    dy = 1 if (frame == 2) else 0
    # 3 charming multi-pixel wildflower clusters (cream, goldenrod, coral-rose)
    clusters = [
        (3 + dx, 4, 11, 12),
        (11 - dx, 6 + dy, 13, 12),
        (6 + dx, 12, 12, 11),
        (12, 12 - dy, 14, 11),
    ]
    for fx, fy, pet, ctr in clusters:
        t.p(fx, fy + 2, 2); t.p(fx - 1, fy + 1, 3); t.p(fx + 1, fy + 1, 3)
        t.p(fx - 1, fy, pet); t.p(fx + 1, fy, pet); t.p(fx, fy - 1, pet); t.p(fx, fy + 1, pet)
        t.p(fx, fy, ctr)
    return t

def make_tall_grass(frame=0):
    t = make_grass(1)
    sway = 1 if frame in (1, 2) else (-1 if frame == 3 else 0)
    # Lush overlapping Chrono Trigger tall meadow fronds with deep base shadow
    tufts = [(2, 6), (7, 5), (12, 6), (4, 12), (10, 13), (14, 12)]
    for bx, by in tufts:
        t.hline(bx - 1, by + 1, 3, 2)
        t.p(bx - 1, by, 3); t.p(bx, by, 3); t.p(bx + 1, by, 2)
        t.p(bx - 1 + sway, by - 1, 4); t.p(bx + sway, by - 1, 5); t.p(bx + 1 + sway, by - 1, 3)
        t.p(bx - 1 + sway, by - 2, 5); t.p(bx + sway, by - 2, 6)
        t.p(bx + sway, by - 3, 6)
    return t

def make_autumn_grass(variant=0):
    t = make_grass(variant & 3)
    # Warm fallen crimson & golden maple leaves scattered organically on mountain turf
    leaves = [
        [(3, 3, 13, 12), (11, 5, 12, 10), (6, 11, 13, 8), (13, 13, 12, 13)],
        [(5, 2, 12, 10), (12, 8, 13, 12), (2, 12, 13, 8), (9, 14, 12, 10)],
        [(2, 5, 13, 12), (9, 3, 12, 10), (13, 11, 13, 8), (5, 13, 12, 13)],
    ][variant % 3]
    for lx, ly, c1, c2 in leaves:
        t.p(lx, ly + 1, 7)
        t.p(lx, ly, c1); t.p(lx + 1, ly, c2); t.p(lx, ly - 1, c2)
    return t

def make_dirt(variant=0):
    """Warm sunlit ochre packed-earth footpath with soft worn ruts & pebbles."""
    t = Tile16(TPAL_FOLIAGE, 9)
    base = [
        "999999aa99999999",
        "9999aaaa99989999",
        "99899aa999889999",
        "9888999999989999",
        "9989999999999999",
        "9999999aa9999999",
        "999999aaaa999989",
        "9999999aa9999889",
        "9998999999998899",
        "9988899999999999",
        "9998999999999999",
        "99aa99999999aa99",
        "9aaaa999999aaaa9",
        "99aa99989999aa99",
        "9999998889999999",
        "9999999899999999",
    ]
    t.stamp(base)
    if variant == 0:
        t.p(4, 3, 7); t.p(5, 3, 10); t.p(11, 9, 7); t.p(12, 8, 10)
    else:
        t.p(2, 8, 7); t.p(3, 7, 10); t.p(12, 3, 7); t.p(13, 3, 10); t.p(7, 13, 7)
    return t

def make_rocky_path(variant=0):
    """Mountain crushed-granite trail with organic embedded flagstones."""
    t = Tile16(TPAL_ARCH, 3)
    t.stamp([
        "3333334433333233",
        "3344444443322233",
        "3344544433233333",
        "3334443332334433",
        "3223333223344443",
        "2333322333345443",
        "3344323333334433",
        "3444433322333332",
        "3454433233322322",
        "3344332333443233",
        "3333223344444333",
        "3223333344544333",
        "2333443334443322",
        "3334444333333233",
        "3334544332233333",
        "3333443323333333",
    ])
    if variant == 1:
        t = t.flip_h()
        t.p(3, 5, 15); t.p(12, 11, 15)  # subtle alpine moss in crevice
    return t

def make_cobble(variant=0):
    """
    Chrono Trigger / Trials of Mana weathered Sunhill limestone cobblestone plaza!
    Rounded staggered flagstones with warm/cool variation, soft mortar & occasional moss.
    """
    t = Tile16(TPAL_ARCH, 2)
    # 4 varied hand-fitted rounded stone patterns (eliminates rigid brick wall look!)
    pats = [
        [
            "2344443223444322",
            "3445444334444432",
            "3444444334454432",
            "2344433223444322",
            "2222222222222222",
            "4432234444432234",
            "4443344544443344",
            "4443344444443344",
            "3332234444332234",
            "2222222222222222",
            "2344432234444432",
            "3445443344544443",
            "3444443344444443",
            "2334432233444332",
            "2222222222222222",
            "3443223444322344",
        ],
        [
            "2344444322344322",
            "3445444433444432",
            "3444444333444432",
            "2344433222344322",
            "2222222f22222222",
            "4443223444322344",
            "4544334454433444",
            "4444334444433444",
            "3443223344322334",
            "222222222222f222",
            "2344443223444432",
            "3444444334454443",
            "3445444334444443",
            "2344433223344332",
            "2222222222222222",
            "3443223444322344",
        ],
        [
            "2334443223444432",
            "3444444334454443",
            "3445444334444443",
            "2344433223444332",
            "2222222222222222",
            "3432234444432234",
            "4443344444443345",
            "4443344544443344",
            "3332234444332234",
            "2222f22222222222",
            "2344432234444322",
            "3445443344444432",
            "3444443344544432",
            "2344432234443322",
            "2222222222222222",
            "3443223444322344",
        ],
        [
            "2344443223444322",
            "3444444334454432",
            "3445444334444432",
            "2344433223444322",
            "2222222222222222",
            "4432234444322344",
            "4443344544433444",
            "4443344444433444",
            "3332234443322334",
            "2222222222222222",
            "2344443223444432",
            "3445444334444443",
            "3444444334454443",
            "2334433223444332",
            "222222222f222222",
            "3443223444322344",
        ],
    ]
    t.stamp(pats[variant & 3])
    return t

def make_water(frame=0, deep=False, shallow=False, rapids=False):
    """Multi-shade animated Chrono/Mana water with shimmering curved ripples."""
    t = Tile16(TPAL_WATER, 1 if deep else (12 if shallow else 2))
    shift = (frame * 4) & 15
    for y in range(16):
        for x in range(16):
            wx = (x + shift + (y // 4) * 5) & 15
            if rapids:
                ry = (y - frame * 4) & 15
                if (x in (2, 3, 10, 11) and ry in (1, 2, 3, 9, 10)) or (x in (6, 7) and ry in (5, 6, 7, 13, 14)):
                    t.p(x, y, 15 if (ry & 1) else 14)
                elif (x + ry) % 5 == 0:
                    t.p(x, y, 3)
                else:
                    t.p(x, y, 2 if ((x + y) & 3) else 1)
            elif deep:
                # Deep ocean swells
                if y in (3, 11) and wx in (2, 3, 4, 5):
                    t.p(x, y, 2)
                elif y in (4, 12) and wx in (3, 4):
                    t.p(x, y, 3)
                elif y in (2, 10) and wx in (9, 10, 11):
                    t.p(x, y, 4)
                elif y == (7 if (frame & 1) else 15) and wx == 12:
                    t.p(x, y, 14)  # gentle sun sparkle
            elif shallow:
                # Translucent turquoise raftable shallows
                if y in (2, 10) and wx in (1, 2, 3, 4):
                    t.p(x, y, 3)
                elif y in (3, 11) and wx in (2, 3):
                    t.p(x, y, 14 if ((frame + y) & 1) else 13)
                elif y in (6, 14) and wx in (8, 9, 10):
                    t.p(x, y, 2)
            else:
                # Vivid creek/lake water with gentle sunlit caustics
                if y in (2, 10) and wx in (2, 3, 4, 5):
                    t.p(x, y, 12)
                elif y in (3, 11) and wx in (3, 4):
                    t.p(x, y, 14 if (wx == 3 and (frame & 1) == 0) else 3)
                elif y in (6, 14) and wx in (9, 10, 11, 12):
                    t.p(x, y, 1)
                elif y == 7 and wx == (5 if (frame < 2) else 13):
                    t.p(x, y, 15)
    return t

def make_shore(edge, frame=0):
    """
    Rounded organic grassy shoreline curving into shimmering water with 2px whitewater foam!
    edge in ('N', 'S', 'W', 'E', 'NW', 'NE', 'SW', 'SE')
    """
    if edge == 'E':
        return make_shore('W', frame).flip_h()
    if edge == 'S':
        return make_shore('N', frame).flip_v()
    if edge == 'NE':
        return make_shore('NW', frame).flip_h()
    if edge == 'SW':
        return make_shore('NW', frame).flip_v()
    if edge == 'SE':
        return make_shore('NW', frame).flip_h().flip_v()

    t = make_water(frame, shallow=True)
    foam_y = 1 if (frame in (1, 2)) else 0
    # Use TPAL_WATER indices (9=deep spruce/bank, 10=lit moss/grass bank, 5=wet earth edge, 15/14=foam)
    if edge == 'N':
        # Organic curved bank across top of tile dipping smoothly into foaming water
        curve = [4, 5, 5, 6, 6, 5, 5, 4, 4, 5, 5, 6, 6, 5, 5, 4]
        for x in range(16):
            h = curve[x]
            for y in range(h - 2):
                t.p(x, y, 10 if ((x + y) & 3) else 9)
            t.p(x, h - 2, 9)
            t.p(x, h - 1, 5)
            # Animated curling whitewater foam!
            t.p(x, h + foam_y, 15 if ((x + frame) & 1) else 14)
            t.p(x, h + 1 + foam_y, 13)
    elif edge == 'W':
        curve = [4, 5, 5, 6, 6, 5, 5, 4, 4, 5, 5, 6, 6, 5, 5, 4]
        for y in range(16):
            w = curve[y]
            for x in range(w - 2):
                t.p(x, y, 10 if ((x + y) & 3) else 9)
            t.p(w - 2, y, 9)
            t.p(w - 1, y, 5)
            t.p(w + foam_y, y, 15 if ((y + frame) & 1) else 14)
            t.p(w + 1 + foam_y, y, 13)
    elif edge == 'NW':
        # Smooth rounded cove corner!
        for y in range(16):
            for x in range(16):
                # Distance from bottom-right water center (13.5, 13.5)
                dx = 13.5 - x
                dy = 13.5 - y
                r = math.hypot(dx, dy)
                if r > 12.2:
                    t.p(x, y, 10 if ((x + y) & 3) else 9)
                elif r > 11.1:
                    t.p(x, y, 9)
                elif r > 10.1:
                    t.p(x, y, 5)
                elif r > 8.9 - foam_y * 0.4:
                    t.p(x, y, 15 if ((x + y + frame) & 1) else 14)
                elif r > 7.8:
                    t.p(x, y, 13)
    return t

def make_world_coast(edge):
    """Organic sandy golden shoreline on the 256x192 Evertale Globe."""
    if edge == 'E': return make_world_coast('W').flip_h()
    if edge == 'S': return make_world_coast('N').flip_v()
    t = make_water(0, shallow=True)
    curve = [4, 5, 6, 6, 5, 5, 4, 4, 5, 6, 6, 5, 5, 4, 4, 5]
    if edge == 'N':
        for x in range(16):
            h = curve[x]
            for y in range(h - 1):
                t.p(x, y, 14 if ((x + y) & 3) else 7)  # golden/cream coastal sand
            t.p(x, h - 1, 13)
            t.p(x, h, 15 if (x & 1) else 14)  # surf foam
            t.p(x, h + 1, 3)
    else:
        for y in range(16):
            w = curve[y]
            for x in range(w - 1):
                t.p(x, y, 14 if ((x + y) & 3) else 7)
            t.p(w - 1, y, 13)
            t.p(w, y, 15 if (y & 1) else 14)
            t.p(w + 1, y, 3)
    return t

def make_bridge():
    """Weathered arched honey-cedar footbridge planks with beveled woodgrain & iron bolts."""
    t = Tile16(TPAL_ARCH, 7)
    for x in range(0, 16, 4):
        t.vline(x, 0, 16, 6)
        t.vline(x + 1, 0, 16, 8)
        t.vline(x + 2, 0, 16, 8 if (x & 4) else 7)
        t.vline(x + 3, 0, 16, 7)
        t.p(x + 1, 2, 9); t.p(x + 2, 13, 9)
        t.p(x + 2, 1, 2); t.p(x + 2, 14, 2)  # iron nail heads
    # Subtle horizontal wear band in center
    t.hline(0, 7, 16, 8); t.hline(0, 8, 16, 8)
    for x in range(0, 16, 4):
        t.p(x, 7, 6); t.p(x, 8, 6)
    return t

def make_bridge_rail(south=False):
    t = make_bridge()
    if not south:
        t.hline(0, 0, 16, 6)
        t.hline(0, 1, 16, 9)
        t.hline(0, 2, 16, 8)
        t.hline(0, 3, 16, 6)
        for px in (1, 8, 14):
            t.rect(px, 0, 2, 5, 6); t.vline(px, 0, 4, 9)
    else:
        t.hline(0, 12, 16, 6)
        t.hline(0, 13, 16, 9)
        t.hline(0, 14, 16, 8)
        t.hline(0, 15, 16, 1)
        for px in (1, 8, 14):
            t.rect(px, 11, 2, 5, 6); t.vline(px, 11, 4, 9)
    return t

def make_dock_pier():
    """
    Weathered Sun-Cedar Harbor Timber Pier!
    Replaces flat blue/tan stripes with rich plank grain, dark plank gaps, nail pegs & rope trim.
    """
    t = Tile16(TPAL_ARCH, 7)
    # 4 sturdy weathered boardwalk planks horizontally with beveled shadows & iron pegs
    for y0 in (0, 4, 8, 12):
        t.hline(0, y0, 16, 6)
        t.hline(0, y0 + 1, 16, 9 if (y0 & 4) == 0 else 8)
        t.hline(0, y0 + 2, 16, 8)
        t.hline(0, y0 + 3, 16, 7)
        # Staggered vertical plank seams & weathered woodgrain
        seam = (y0 * 3 + 5) & 15
        t.vline(seam, y0, 4, 6)
        t.p((seam + 1) & 15, y0 + 1, 9)
        t.p((seam - 2) & 15, y0 + 2, 2)  # peg nail
        t.p((seam + 2) & 15, y0 + 2, 2)
    # Vertical support stringer hint along edges
    t.vline(0, 0, 16, 6)
    t.vline(15, 0, 16, 6)
    return t

def make_pier_water_edge(frame=0):
    """South edge of timber pier showing rounded wooden pilings dipping into shimmering water!"""
    t = make_water(frame, shallow=True)
    # Top 6 rows: timber pier deck edge
    t.hline(0, 0, 16, 6); t.hline(0, 1, 16, 14); t.hline(0, 2, 16, 6); t.hline(0, 3, 16, 5)
    t.hline(0, 4, 16, 8)  # cast shadow under pier deck
    # Two rounded weathered pilings dipping into turquoise water + animated ripple rings!
    for px in (2, 11):
        t.rect(px, 3, 3, 8, 5)
        t.vline(px, 3, 8, 6)
        t.vline(px + 2, 3, 8, 8)
        # Water ripple ring around piling base
        r = 1 if (frame in (1, 2)) else 0
        t.hline(px - 1 - r, 11, 5 + r * 2, 15)
        t.hline(px, 12, 3, 14)
    return t

def make_wood_floor(variant=0):
    """Warm honey-cedar cottage floorboards with beveled seams & subtle sunlit grain."""
    t = Tile16(TPAL_ARCH, 8)
    for y0 in (0, 4, 8, 12):
        t.hline(0, y0, 16, 6)
        t.hline(0, y0 + 1, 16, 9 if ((y0 >> 2) + variant) & 1 else 8)
        t.hline(0, y0 + 2, 16, 8)
        t.hline(0, y0 + 3, 16, 7)
        seam = ((y0 * 5) + (variant * 7) + 3) & 15
        t.vline(seam, y0, 4, 6)
        t.p((seam + 1) & 15, y0 + 1, 9)
        t.p((seam + 6) & 15, y0 + 2, 7)
    return t

def make_hearth_rug():
    """Woven burgundy & antique-gold storybook cottage hearth rug with tasseled trim."""
    t = Tile16(TPAL_ACCENT, 3)
    t.rect(0, 0, 16, 16, 2)
    t.rect(1, 1, 14, 14, 12)  # antique gold inner border
    t.rect(2, 2, 12, 12, 3)   # rich burgundy pile
    t.rect(4, 4, 8, 8, 4)
    # Woven diamond medallion center
    t.stamp([
        "....c...",
        "...c5c..",
        "..c5f5c.",
        "...c5c..",
        "....c...",
    ], 4, 5)
    # Soft cream tassels along left & right edges
    for y in range(1, 15, 2):
        t.p(0, y, 15); t.p(15, y, 15)
    return t

def make_roof_top():
    """Scalloped Chrono/Mana terracotta roof ridge with sunlit cap tiles."""
    t = Tile16(TPAL_ARCH, 11)
    # Sunlit terracotta ridge beam along top
    t.hline(0, 0, 16, 10)
    t.hline(0, 1, 16, 12)
    t.hline(0, 2, 16, 13)
    t.hline(0, 3, 16, 12)
    t.hline(0, 4, 16, 10)
    for x in range(0, 16, 4):
        t.vline(x, 0, 5, 10)
        t.p(x + 1, 2, 14)
    # Scalloped overlapping terracotta shingles below ridge
    for row_i, y0 in enumerate((5, 9, 13)):
        off = 2 if (row_i & 1) else 0
        for x in range(16):
            sx = (x + off) & 3
            if y0 < 16: t.p(x, y0, 12 if sx in (1, 2) else 11)
            if y0 + 1 < 16: t.p(x, y0 + 1, 11 if sx != 0 else 10)
            if y0 + 2 < 16: t.p(x, y0 + 2, 10 if sx in (0, 3) else 11)
            if y0 + 3 < 16: t.p(x, y0 + 3, 10)
    return t

def make_roof_body():
    """Overlapping scalloped terracotta cottage roof shingles with moss flecks."""
    t = Tile16(TPAL_ARCH, 11)
    for row_i, y0 in enumerate((0, 4, 8, 12)):
        off = 2 if (row_i & 1) else 0
        for x in range(16):
            sx = (x + off) & 3
            t.p(x, y0, 12 if sx in (1, 2) else 11)
            t.p(x, y0 + 1, 11 if sx != 0 else 10)
            t.p(x, y0 + 2, 10 if sx in (0, 3) else 11)
            t.p(x, y0 + 3, 10 if sx != 2 else 6)
    # Tiny weathered sage moss detail on one shingle
    t.p(10, 5, 15); t.p(11, 5, 15); t.p(10, 6, 15)
    return t

def make_roof_eave():
    """Overhanging cottage roof eave with scalloped drip edge & deep shadow."""
    t = Tile16(TPAL_ARCH, 0, prio=1)
    for x in range(16):
        sx = x & 3
        t.p(x, 0, 12 if sx in (1, 2) else 11)
        t.p(x, 1, 11 if sx != 0 else 10)
        t.p(x, 2, 11 if sx in (1, 2) else 10)
        t.p(x, 3, 10)
        t.p(x, 4, 6 if sx in (1, 2) else 1)
        t.p(x, 5, 1 if sx in (1, 2) else 0)
    return t

def make_wall_wood():
    """Half-timbered storybook cottage wall: cream stucco, dark cedar beams & stone skirting!"""
    t = Tile16(TPAL_ARCH, 9)
    # Cast shadow from overhanging roof eave along top 2px
    t.hline(0, 0, 16, 6); t.hline(0, 1, 16, 8)
    # Warm textured cream plaster with subtle shading
    for y in range(2, 12):
        for x in range(16):
            t.p(x, y, 9 if ((x + y) & 3) else 8)
    # Dark cedar timber framing (left/right posts + cross-beam)
    t.rect(0, 0, 2, 12, 6); t.vline(1, 1, 10, 7)
    t.rect(14, 0, 2, 12, 6); t.vline(14, 1, 10, 7)
    t.hline(0, 6, 16, 6); t.hline(2, 7, 12, 8)
    # Dressed stone foundation skirting along bottom 4px
    t.hline(0, 12, 16, 2)
    t.rect(0, 13, 16, 3, 3)
    for sx in (0, 5, 10):
        t.rect(sx + 1, 13, 3, 2, 4)
        t.vline(sx, 12, 4, 2)
    t.hline(0, 15, 16, 1)
    return t

def make_wall_window():
    """Warm storybook mullioned cottage window with golden interior glow & flower box!"""
    t = make_wall_wood()
    # Recessed wooden shutter frame
    t.rect(3, 2, 10, 9, 6)
    t.rect(4, 3, 8, 7, 13)  # warm sunlit/amber glass panes
    # Diagonal glint & bright warm interior glow
    t.rect(5, 4, 2, 2, 14); t.rect(9, 4, 2, 2, 14)
    t.p(5, 7, 14); t.p(9, 7, 14)
    # Wooden mullions
    t.vline(7, 3, 7, 7); t.vline(8, 3, 7, 6)
    t.hline(4, 6, 8, 6)
    # Charming windowsill flower box with greenery & warm blooms!
    t.rect(2, 10, 12, 3, 6)
    t.hline(3, 11, 10, 7)
    for fx in (4, 6, 8, 10, 11):
        t.p(fx, 10, 15)
    t.p(5, 9, 12); t.p(9, 9, 14); t.p(7, 9, 15)
    return t

def make_wall_door():
    """Recessed arched oak cottage door with iron hinges, brass knob & stone threshold."""
    t = make_wall_wood()
    # Arched timber doorframe
    t.rect(3, 2, 10, 14, 6)
    t.p(3, 2, 9); t.p(12, 2, 9)  # rounded arch corners
    t.rect(4, 3, 8, 12, 7)
    # Vertical oak door planks
    t.vline(6, 3, 12, 6); t.vline(9, 3, 12, 6)
    t.vline(5, 4, 10, 8); t.vline(8, 4, 10, 8); t.vline(10, 4, 10, 8)
    # Iron strap hinges & warm brass doorknob
    t.hline(4, 5, 3, 2); t.hline(4, 11, 3, 2)
    t.p(10, 9, 13); t.p(10, 8, 14)
    # Dressed stone doorstep threshold
    t.rect(2, 14, 12, 2, 4); t.hline(2, 15, 12, 2)
    return t

def make_windmill_wall():
    t = make_cobble(0)
    t.hline(0, 0, 16, 2); t.hline(0, 15, 16, 1)
    # Warm arched timber bracing
    t.vline(0, 0, 16, 6); t.vline(15, 0, 16, 6)
    return t

def make_windmill_door():
    t = make_windmill_wall()
    t.rect(3, 3, 10, 13, 6)
    t.rect(4, 4, 8, 11, 7)
    t.vline(7, 4, 11, 6); t.vline(8, 4, 11, 8)
    t.p(10, 9, 13)
    t.rect(2, 14, 12, 2, 5)
    return t

def make_windmill_roof():
    t = make_roof_body()
    # Tapered dark slate/terracotta windmill cap trim
    t.hline(0, 0, 16, 12); t.hline(0, 1, 16, 13)
    return t

def make_interior_wall():
    """Cozy cedar-beamed cottage interior wall with wainscoted wainscoting."""
    t = Tile16(TPAL_ARCH, 9)
    t.hline(0, 0, 16, 6); t.hline(0, 1, 16, 7)
    for y in range(2, 9):
        for x in range(16):
            t.p(x, y, 9 if ((x + y) & 3) else 8)
    # Wainscoting wood rail & vertical cedar panels
    t.hline(0, 9, 16, 6); t.hline(0, 10, 16, 8)
    for x in range(0, 16, 4):
        t.vline(x, 10, 6, 6)
        t.vline(x + 1, 11, 4, 8)
        t.vline(x + 2, 11, 4, 7)
        t.vline(x + 3, 11, 4, 7)
    t.hline(0, 15, 16, 1)
    return t

def make_fireplace(frame=0):
    """Crackling dressed-stone cottage hearth with animated warm ember flames!"""
    t = Tile16(TPAL_ARCH, 3)
    # Stone chimney breast & wooden mantel shelf
    t.rect(0, 0, 16, 4, 6); t.hline(0, 1, 16, 8); t.hline(0, 2, 16, 7)
    for y in (4, 8, 12):
        t.hline(0, y, 16, 2)
    t.rect(1, 5, 3, 10, 4); t.rect(12, 5, 3, 10, 4)
    # Dark arched firebox
    t.rect(4, 6, 8, 9, 1)
    t.p(4, 6, 3); t.p(11, 6, 3)
    # Animated crackling fire!
    flick = 1 if (frame & 1) else 0
    t.rect(5, 12, 6, 3, 11)
    t.rect(6, 10 - flick, 4, 4 + flick, 12)
    t.rect(7, 9 + (1 - flick), 2, 4, 13)
    t.p(7 + flick, 11, 14); t.p(8, 12, 14)
    # Hearth stone lip
    t.hline(1, 15, 14, 5)
    return t

def make_bed_quilt():
    """Rowan's cozy cedar-framed bed with stitched crimson & golden patchwork quilt!"""
    t = Tile16(TPAL_ACCENT, 10)
    # Wooden headboard & posts
    t.rect(1, 0, 14, 4, 10); t.hline(2, 1, 12, 11); t.hline(2, 2, 12, 12)
    # Plump white pillow
    t.rect(3, 3, 10, 4, 9); t.rect(4, 4, 8, 2, 15)
    # Stitched patchwork quilt (burgundy, gold & sky blue squares!)
    t.rect(2, 7, 12, 7, 3)
    for qy in (7, 10):
        for qx in (2, 5, 8, 11):
            col = 4 if ((qx + qy) & 1) else 7
            t.rect(qx, qy, 3, 3, col)
            t.p(qx + 1, qy + 1, 12 if col == 4 else 8)
    # Footboard
    t.rect(1, 14, 14, 2, 10); t.hline(2, 14, 12, 11)
    return t

def make_attic_chest():
    """Grandma's heirloom brass-bound cedar treasure chest on floorboards."""
    t = make_wood_floor(0)
    t.rect(2, 4, 12, 10, 6)
    t.rect(3, 5, 10, 3, 8); t.hline(3, 5, 10, 9)
    t.rect(3, 9, 10, 4, 7)
    # Brass bands & central keyhole clasp
    t.vline(4, 4, 10, 13); t.vline(11, 4, 10, 13)
    t.hline(2, 8, 12, 1)
    t.rect(7, 7, 2, 3, 14); t.p(7, 8, 6)
    t.hline(2, 14, 12, 1)  # soft base shadow
    return t

def make_cliff_wall():
    """Layered Chrono Trigger granite cliff face with vertical fissures & warm sunlit facets."""
    t = Tile16(TPAL_WATER, 5)
    t.stamp([
        "5566665556665555",
        "5667666566766555",
        "5666665566665565",
        "8566655856655566",
        "8556558855558566",
        "5555588555588555",
        "5665555666555565",
        "6676556676655666",
        "6666556666555666",
        "5665585665585565",
        "5555885555885555",
        "8558855558855585",
        "5566556655566555",
        "5666566765666655",
        "5565556655566558",
        "8855885588855888",
    ])
    return t

def make_cliff_ledge():
    """Mossy organic granite cliff rim overhanging the rock wall below!"""
    t = make_cliff_wall()
    # Organic scalloped mossy grass overhang across top 7 pixels!
    rim = [6, 7, 7, 8, 7, 6, 6, 7, 8, 7, 6, 6, 7, 7, 6, 5]
    for x in range(16):
        h = rim[x]
        for y in range(h - 2):
            t.p(x, y, 10 if ((x + y) & 3) else 9)
        t.p(x, h - 2, 9)
        t.p(x, h - 1, 8)   # overhanging cast shadow onto cliff rock
        t.p(x, h, 8)
    return t

def make_stone_stairs():
    """Carved mountain granite steps with sunlit treads & cool risers."""
    t = Tile16(TPAL_ARCH, 3)
    for y0 in (0, 4, 8, 12):
        t.hline(1, y0, 14, 5)
        t.hline(1, y0 + 1, 14, 4)
        t.hline(1, y0 + 2, 14, 3)
        t.hline(1, y0 + 3, 14, 2)
        t.p(4 + (y0 & 4), y0 + 1, 3)
        t.p(11 - (y0 & 4), y0, 4)
    # Mossy granite curb stones on left & right
    t.vline(0, 0, 16, 2); t.vline(15, 0, 16, 2)
    t.p(0, 3, 15); t.p(15, 9, 15)
    return t

def make_rail_track():
    """Alderbrook mountain railway: iron rails, oak sleepers & crushed gravel ballast."""
    t = make_rocky_path(0)
    # Horizontal timber sleepers every 4px
    for y0 in (1, 5, 9, 13):
        t.rect(1, y0, 14, 2, 6)
        t.hline(2, y0, 12, 7)
    # Gleaming twin iron rails
    for rx in (3, 11):
        t.vline(rx, 0, 16, 2)
        t.vline(rx + 1, 0, 16, 5)
        for y in range(0, 16, 4):
            t.p(rx + 1, y, 4)
    return t

def make_shrine_floor(variant=0):
    """Ancient carved moss-lined temple flagstones."""
    t = Tile16(TPAL_WATER, 5)
    t.rect(0, 0, 16, 16, 8)
    t.rect(1, 1, 14, 14, 5)
    t.rect(2, 2, 12, 12, 6)
    t.hline(2, 2, 12, 7); t.vline(2, 2, 12, 7)
    t.hline(2, 13, 12, 5); t.vline(13, 2, 12, 5)
    if variant == 1:
        t.p(1, 1, 10); t.p(2, 1, 9); t.p(14, 13, 10)
    return t

def make_shrine_rune(frame=0):
    t = make_shrine_floor(0)
    glow = 15 if (frame in (1, 2)) else 13
    ring = 12 if (frame & 1) else 3
    t.stamp([
        "...rrrr...",
        "..r.gg.r..",
        ".r.g..g.r.",
        ".rg.gg.gr.",
        ".rg.gg.gr.",
        ".r.g..g.r.",
        "..r.gg.r..",
        "...rrrr...",
    ].copy(), 3, 4) if False else None
    # Draw crisp glowing arcane diamond sigil
    for d in range(-4, 5):
        ad = abs(d)
        t.p(7 + d, 7 - (4 - ad), ring)
        t.p(7 + d, 7 + (4 - ad), ring)
    t.rect(6, 6, 4, 4, ring)
    t.rect(7, 7, 2, 2, glow)
    return t

def make_shrine_arch():
    t = Tile16(TPAL_WATER, 0, prio=1)
    t.rect(0, 0, 16, 6, 5)
    t.hline(0, 0, 16, 7); t.hline(0, 1, 16, 6); t.hline(0, 5, 16, 8)
    t.rect(0, 6, 3, 4, 5); t.rect(13, 6, 3, 4, 5)
    t.p(2, 3, 12); t.p(7, 3, 15); t.p(8, 3, 15); t.p(13, 3, 12)
    return t

def make_hollow_oak():
    """Monumental gnarled ancient Hollow Oak portal archway with starlit moss."""
    t = Tile16(TPAL_FOLIAGE, 2)
    t.rect(0, 0, 16, 16, 7)
    t.rect(1, 0, 14, 16, 8)
    t.vline(2, 0, 16, 9); t.vline(13, 0, 16, 7)
    # Glowing misty hollow opening
    t.disc(7.5, 10, 4.5, 5.5, 1)
    t.disc(7.5, 10, 3.2, 4.2, 14)
    t.disc(7.5, 10, 1.8, 2.5, 11)
    # Overhanging verdant moss & root flares
    t.hline(0, 0, 16, 3); t.hline(1, 1, 14, 4); t.hline(3, 2, 10, 5)
    return t

# ------------------------------------------------------------------------------
# TRANSPARENT PROPS & COMPOSITED PROP-ON-GROUND BUILDERS (ZERO GREEN BOXES!)
# ------------------------------------------------------------------------------
def paint_tree_trunk(t, autumn=False, pine=False, palm=False):
    """Draw gnarled trunk + flared roots + soft cast ground shadow onto ground tile t."""
    # Oval cast shadow around trunk base
    for y in range(11, 16):
        for x in range(2, 14):
            if ((x - 7.5) / 5.5) ** 2 + ((y - 13.0) / 2.4) ** 2 <= 1.0:
                if t.px[y][x] != 0:
                    t.p(x, y, 2 if t.qpal[0] == TPAL_FOLIAGE else 1)
    if palm:
        # Curved tropical palm trunk
        for y in range(0, 14):
            cx = 7 + int(math.sin(y * 0.25) * 1.5)
            t.p(cx - 1, y, 7); t.p(cx, y, 9 if (y & 1) else 8); t.p(cx + 1, y, 8 if (y & 1) else 7)
            t.p(cx + 2, y, 7)
        # Crown fronds peeking at top
        t.hline(2, 0, 12, 4); t.hline(1, 1, 14, 3); t.hline(4, 2, 8, 5)
    elif pine:
        # Layered alpine spruce boughs sweeping down to mossy trunk
        t.rect(6, 10, 4, 4, 7); t.vline(7, 10, 4, 8)
        for row, w in [(0, 4), (1, 6), (2, 8), (3, 10), (4, 12), (5, 10), (6, 12), (7, 14), (8, 14), (9, 12), (10, 10)]:
            x0 = 8 - w // 2
            for x in range(x0, x0 + w):
                col = 5 if (x < x0 + w // 2 and row in (0, 2, 4, 6, 8)) else (3 if x < x0 + w - 2 else 2)
                if row in (3, 7, 10): col = 2
                t.p(x, row, col)
    else:
        # Gnarled Chrono/Mana oak or maple trunk with sunlit bark ridges & flared roots
        t.rect(5, 0, 6, 12, 7)
        t.vline(6, 0, 12, 8); t.vline(7, 0, 12, 9); t.vline(8, 0, 12, 8); t.vline(9, 0, 11, 7)
        # Flared roots
        t.rect(4, 11, 8, 3, 7); t.p(3, 13, 7); t.p(12, 13, 7)
        t.p(5, 11, 8); t.p(6, 12, 9); t.p(8, 12, 8); t.p(10, 12, 8)
        # Canopy shadow collar at top of trunk
        t.hline(5, 0, 6, 1); t.hline(5, 1, 6, 7)
        if autumn:
            t.p(4, 13, 13); t.p(11, 13, 12); t.p(12, 12, 13)  # fallen maple leaves at base

def paint_bush(t, pal_mode=TPAL_FOLIAGE):
    """Voluminous rounded Chrono/Mana shrub with sunlit berry leaves & oval shadow."""
    if pal_mode == TPAL_FOLIAGE:
        s0, c1, c2, c3, c4, c5, b1 = 1, 2, 3, 4, 5, 6, 13
    else:  # Cobblestone TPAL_ARCH mode (uses moss/warm tones!)
        s0, c1, c2, c3, c4, c5, b1 = 1, 2, 15, 15, 9, 14, 12
    # Oval base shadow
    for x in range(2, 14):
        t.p(x, 14, s0); t.p(x, 13, c1)
    # 3 overlapping painterly foliage puffs
    t.disc(7.5, 9.0, 6.2, 4.8, c1)
    t.disc(5.2, 8.0, 3.8, 3.6, c2)
    t.disc(10.2, 8.5, 3.6, 3.4, c2)
    t.disc(7.5, 6.2, 4.2, 3.6, c3)
    t.disc(6.2, 5.4, 2.8, 2.2, c4)
    t.disc(9.5, 6.5, 2.2, 1.8, c4)
    t.p(5, 4, c5); t.p(6, 4, c5); t.p(9, 5, c5)
    t.p(4, 9, b1); t.p(10, 8, b1); t.p(7, 10, b1)  # tiny warm berries/blossoms

def paint_lantern(t, pal_mode=TPAL_ARCH):
    """Wrought-iron storybook lamppost with warm glowing lantern glass & cast shadow."""
    if pal_mode == TPAL_ARCH:
        sh, iron_d, iron_l, glow_m, glow_b, cap = 1, 2, 4, 13, 14, 6
    else:  # TPAL_FOLIAGE
        sh, iron_d, iron_l, glow_m, glow_b, cap = 1, 7, 9, 12, 11, 7
    # Soft grounded shadow
    t.hline(5, 14, 6, sh); t.hline(6, 15, 4, sh)
    # Base pedestal & slender post
    t.rect(6, 12, 4, 3, iron_d); t.hline(6, 12, 3, iron_l)
    t.vline(7, 6, 7, iron_d); t.vline(8, 6, 7, iron_l)
    # Lantern housing + glowing amber glass
    t.rect(5, 2, 6, 5, iron_d)
    t.rect(6, 3, 4, 3, glow_m)
    t.rect(7, 3, 2, 2, glow_b)
    # Peaked iron cap
    t.hline(4, 2, 8, iron_d); t.hline(6, 1, 4, iron_l); t.p(7, 0, cap); t.p(8, 0, cap)

def paint_signpost(t, pal_mode=TPAL_ARCH):
    """Carved wooden storybook signpost with bevel grain & oval ground shadow."""
    if pal_mode == TPAL_ARCH:
        sh, wd, wm, wl, txt = 1, 6, 7, 9, 2
    else:
        sh, wd, wm, wl, txt = 1, 7, 8, 10, 7
    t.hline(5, 14, 6, sh)
    t.rect(7, 9, 2, 6, wd); t.vline(7, 9, 5, wm)
    # Beveled wooden sign board
    t.rect(2, 2, 12, 7, wd)
    t.rect(3, 3, 10, 5, wm)
    t.hline(3, 3, 10, wl)
    # Inscribed text lines
    t.hline(4, 5, 7, txt); t.hline(4, 7, 5, txt)

def paint_flower_bed(t, pal_mode=TPAL_ARCH):
    """Timber-framed storybook flower planter bursting with colorful blossoms."""
    if pal_mode == TPAL_ARCH:
        sh, wd, wm, lf, f1, f2, f3 = 1, 6, 8, 15, 12, 14, 13
    else:
        sh, wd, wm, lf, f1, f2, f3 = 1, 7, 9, 3, 13, 11, 12
    t.hline(1, 14, 14, sh)
    t.rect(1, 7, 14, 7, wd)
    t.rect(2, 8, 12, 5, wm)
    t.hline(2, 8, 12, wd)
    # Lush cascading greenery & bright blossoms
    t.rect(2, 3, 12, 5, lf)
    t.disc(5, 4, 3.0, 2.5, lf); t.disc(11, 4, 3.0, 2.5, lf)
    for fx, fy, fc in [(4, 3, f1), (7, 2, f2), (10, 3, f3), (12, 5, f1), (6, 5, f3), (9, 5, f2)]:
        t.p(fx, fy, fc); t.p(fx + 1, fy, fc); t.p(fx, fy + 1, f2 if fc != f2 else f3)

def paint_boulder(t, pal_mode=TPAL_WATER):
    """Moss-capped rounded granite boulder with cast shadow."""
    if pal_mode == TPAL_WATER:
        sh, s1, s2, s3, m1, m2 = 8, 5, 6, 7, 9, 10
    elif pal_mode == TPAL_ARCH:
        sh, s1, s2, s3, m1, m2 = 1, 2, 3, 5, 15, 15
    else:
        sh, s1, s2, s3, m1, m2 = 1, 7, 9, 11, 3, 5
    t.disc(8.0, 12.2, 6.2, 2.8, sh)
    t.disc(7.5, 9.0, 6.0, 5.2, s1)
    t.disc(6.8, 7.8, 4.8, 4.0, s2)
    t.disc(5.8, 6.2, 2.8, 2.0, s3)
    # Organic moss cap on top-right
    t.p(9, 4, m1); t.p(10, 4, m2); t.p(11, 5, m1); t.p(10, 5, m2)

def paint_fence(t, pal_mode=TPAL_ARCH):
    if pal_mode == TPAL_ARCH:
        sh, wd, wm, wl = 1, 6, 7, 9
    else:
        sh, wd, wm, wl = 1, 7, 8, 10
    t.hline(0, 13, 16, sh)
    # Horizontal cross rails
    t.hline(0, 6, 16, wd); t.hline(0, 7, 16, wl); t.hline(0, 8, 16, wm)
    t.hline(0, 10, 16, wd); t.hline(0, 11, 16, wl)
    # Rounded cedar pickets
    for px in (1, 6, 11):
        t.rect(px, 3, 4, 11, wd)
        t.rect(px + 1, 4, 2, 9, wm)
        t.vline(px + 1, 3, 9, wl)

def paint_shrine_pillar(t):
    t.hline(3, 15, 10, 8)
    t.rect(3, 1, 10, 14, 5)
    t.rect(4, 2, 8, 12, 6)
    t.vline(5, 2, 12, 7); t.vline(10, 2, 12, 5)
    t.rect(2, 0, 12, 3, 6); t.hline(2, 0, 12, 7); t.hline(2, 2, 12, 8)
    t.rect(2, 13, 12, 2, 6); t.hline(2, 14, 12, 8)
    t.p(7, 6, 12); t.p(8, 6, 15); t.p(7, 7, 15); t.p(8, 7, 12)

# ------------------------------------------------------------------------------
# LUSH PAINTERLY OVERHEAD TREE CANOPIES (BG2 — ORGANIC SCALLOPED SILHOUETTES!)
# ------------------------------------------------------------------------------
def make_canopy_quad(quad, maple=False):
    """Voluminous Chrono/Mana multi-tile tree crown quadrant (TL/TR/BL/BR) with organic leaf clusters."""
    pal = TPAL_ACCENT if maple else TPAL_FOLIAGE
    t = Tile16(pal, 0, prio=1)
    # Color indices: outline/deep, dark leaf, mid leaf, sunlit leaf, bright tip
    c0, c1, c2, c3, c4 = (1, 2, 3, 4, 5) if maple else (1, 2, 3, 4, 6)
    c3b = 4 if maple else 5

    if quad == 'TR':
        return make_canopy_quad('TL', maple).flip_h()
    if quad == 'BR':
        return make_canopy_quad('BL', maple).flip_h()

    if quad == 'TL':
        # Rounded top-left crown puff clusters (leaves open corner at top-left so trees aren't boxes!)
        puffs = [(12, 12, 7.5), (13, 6, 5.5), (6, 12, 5.2), (8, 7, 4.8)]
        for cx, cy, r in puffs:
            t.disc(cx, cy, r, r, c0)
        for cx, cy, r in puffs:
            t.disc(cx, cy, r - 1.0, r - 1.0, c1)
        for cx, cy, r in [(12, 11, 6.2), (12, 5.5, 4.4), (6.5, 11, 4.0), (8.2, 6.8, 3.8)]:
            t.disc(cx, cy, r, r, c2)
        for cx, cy, r in [(11.5, 9.5, 4.5), (12.0, 4.5, 3.2), (7.5, 6.0, 2.8), (6.2, 10.2, 2.4)]:
            t.disc(cx, cy, r, r, c3b)
        for x, y in [(11, 3), (12, 3), (7, 5), (8, 5), (11, 8), (12, 8), (6, 9), (14, 7)]:
            t.p(x, y, c4)
    else:  # 'BL' — lower canopy skirt with shaded under-leaves!
        puffs = [(12, 4, 7.5), (6, 5, 5.5), (11, 10, 5.2), (5, 9, 4.0)]
        for cx, cy, r in puffs:
            t.disc(cx, cy, r, r, c0)
        for cx, cy, r in puffs:
            t.disc(cx, cy, r - 1.0, r - 1.0, c1)
        for cx, cy, r in [(12, 3.5, 6.0), (6.5, 4.5, 4.2), (11, 8.2, 3.8)]:
            t.disc(cx, cy, r, r, c2)
        for cx, cy, r in [(11.5, 2.5, 4.0), (6.5, 3.8, 2.6)]:
            t.disc(cx, cy, r, r, c3b)
        for x, y in [(11, 1), (12, 2), (6, 3)]:
            t.p(x, y, c4)
    return t

def make_pine_top():
    t = Tile16(TPAL_FOLIAGE, 0, prio=1)
    for row, w in [(1, 2), (2, 4), (3, 4), (4, 6), (5, 8), (6, 6), (7, 8), (8, 10), (9, 12), (10, 10), (11, 12), (12, 14), (13, 14), (14, 12), (15, 10)]:
        x0 = 8 - w // 2
        for x in range(x0, x0 + w):
            c = 5 if (x < 8 and row in (1, 2, 4, 7, 11)) else (4 if x < 9 else (3 if x < x0 + w - 1 else 2))
            if row in (5, 9, 13, 15): c = 2
            t.p(x, row, c)
    t.p(7, 1, 6); t.p(7, 4, 6); t.p(6, 8, 6)
    return t

def make_oak_crown():
    t = make_canopy_quad('TL', False)
    t.overlay_from(make_canopy_quad('TR', False))
    return t

# ------------------------------------------------------------------------------
# OVERHAULED EVERTALE OPEN WORLD GLOBE TILES (CHRONO TRIGGER OVERWORLD FEEL!)
# ------------------------------------------------------------------------------
def make_world_beach():
    t = Tile16(TPAL_FOLIAGE, 10)
    for y in range(16):
        for x in range(16):
            v = (x * 3 + y * 5) & 7
            t.p(x, y, 11 if v == 0 else (9 if v == 7 else 10))
    t.p(4, 5, 11); t.p(5, 5, 11); t.p(11, 11, 9); t.p(12, 11, 9)
    return t

def make_world_peak():
    """Snow-capped storybook mountain range with warm sunlit western slope & cool eastern shadow."""
    t = Tile16(TPAL_WATER, 9)
    # Fill base with deep valley spruce
    for y in range(16):
        for x in range(16):
            t.p(x, y, 10 if ((x + y) & 3) else 9)
    # Twin layered alpine peaks
    peaks = [(5, 4, 7), (11, 2, 9)]
    for px, py, h in peaks:
        for dy in range(h + 4):
            y = py + dy
            if y >= 15: break
            hw = dy + 1
            for dx in range(-hw, hw + 1):
                x = px + dx
                if not (0 <= x < 16): continue
                if dy <= 2:
                    t.p(x, y, 15 if dx <= 0 else 13)  # snowcap
                elif dy == 3 and ((x + dy) & 1):
                    t.p(x, y, 7)
                elif dx < 0:
                    t.p(x, y, 6 if (dx > -hw) else 5)  # sunlit west granite
                elif dx == 0:
                    t.p(x, y, 7 if (dy < 5) else 6)
                else:
                    t.p(x, y, 5 if (dx < hw) else 8)   # cool shaded east slope
    return t

def make_world_forest():
    """Dense lush Chrono Trigger overworld woodland cluster (overlapping round crowns!)."""
    t = make_grass(0)
    crowns = [(4, 5, 4.2), (11, 5, 4.2), (8, 9, 4.5), (3, 12, 3.8), (12, 12, 3.8)]
    for cx, cy, r in crowns:
        t.disc(cx, cy + 1, r, r * 0.85, 1)
        t.disc(cx, cy, r - 0.5, r * 0.82, 2)
        t.disc(cx - 0.4, cy - 0.8, r - 1.4, r * 0.65, 3)
        t.disc(cx - 0.8, cy - 1.4, r - 2.3, r * 0.45, 5)
        t.p(int(cx - 1), int(cy - 2), 6)
    return t

def make_world_hill():
    """Rolling sunlit highland clover knoll with curved ridge highlight."""
    t = make_grass(0)
    t.disc(7.5, 11.0, 6.8, 4.2, 3)
    t.disc(7.5, 10.0, 6.0, 3.6, 4)
    t.disc(6.5, 8.8, 4.4, 2.4, 5)
    t.hline(4, 7, 5, 6)
    return t

def make_world_castle():
    """Gleaming white-stone Castle Aurelia miniature keep with twin spires & banners."""
    t = make_grass(0)
    t.qpal = [TPAL_ARCH, TPAL_ARCH, TPAL_ARCH, TPAL_ARCH]
    # Re-paint soft grass base in TPAL_ARCH moss tone
    t.rect(0, 0, 16, 16, 15)
    t.hline(1, 14, 14, 1)
    # Keep body & flanking towers
    t.rect(2, 6, 12, 8, 3); t.rect(3, 7, 10, 7, 4); t.vline(3, 7, 7, 5)
    t.rect(2, 4, 3, 10, 4); t.vline(2, 4, 10, 5); t.rect(11, 4, 3, 10, 4)
    # Crimson pointed spires + golden pennants
    t.rect(2, 2, 3, 2, 11); t.p(3, 1, 12); t.p(4, 1, 13)
    t.rect(11, 2, 3, 2, 11); t.p(12, 1, 12); t.p(13, 1, 13)
    t.rect(6, 3, 4, 4, 5); t.rect(6, 1, 4, 2, 11); t.p(7, 0, 13); t.p(8, 0, 13)
    # Arched castle gate
    t.rect(6, 10, 4, 4, 1); t.hline(6, 10, 4, 6)
    return t

def make_world_town():
    """Charming terracotta-roofed storybook village cluster on the Evertale Globe."""
    t = Tile16(TPAL_ARCH, 15)
    t.hline(1, 14, 14, 1)
    # Left tiny cottage
    t.rect(1, 7, 6, 7, 9); t.rect(1, 4, 6, 3, 11); t.hline(2, 4, 4, 12); t.rect(3, 10, 2, 4, 6)
    t.p(2, 8, 13); t.p(5, 8, 13)
    # Right taller town house
    t.rect(8, 5, 7, 9, 9); t.rect(8, 2, 7, 3, 11); t.hline(9, 2, 5, 12); t.rect(10, 10, 2, 4, 6)
    t.p(9, 7, 14); t.p(13, 7, 14)
    return t

def make_world_ruins():
    """Moss-draped broken stone watchtower & ancient columns."""
    t = Tile16(TPAL_WATER, 10)
    t.hline(2, 14, 12, 8)
    t.rect(3, 4, 4, 10, 6); t.vline(3, 4, 10, 7); t.p(4, 3, 5); t.p(6, 5, 9)
    t.rect(9, 6, 4, 8, 5); t.vline(9, 6, 8, 6); t.p(10, 5, 7); t.p(11, 7, 10)
    t.hline(5, 11, 6, 5); t.p(7, 10, 12)  # faint glowing relic
    return t

def make_world_sand(variant=0):
    t = Tile16(TPAL_FOLIAGE, 10)
    # Sweeping curved desert dune ripples
    for y in range(16):
        for x in range(16):
            wave = (y + int(math.sin((x + variant * 5) * 0.4) * 1.4)) & 7
            t.p(x, y, 11 if wave == 1 else (9 if wave == 6 else 10))
    return t

def make_world_snow(variant=0):
    t = Tile16(TPAL_WATER, 14)
    for y in range(16):
        for x in range(16):
            wave = (y + int(math.cos((x + variant * 4) * 0.4) * 1.2)) & 7
            t.p(x, y, 15 if wave in (0, 1) else (13 if wave == 6 else 14))
    return t

def make_world_lava(frame=0):
    t = Tile16(TPAL_ACCENT, 3)
    s = (frame * 3) & 15
    for y in range(16):
        for x in range(16):
            v = ((x + s) * 3 + y * 5 + int(math.sin((x + y) * 0.4) * 2)) & 15
            if v in (0, 1, 2): t.p(x, y, 15 if (v == 1) else 14)
            elif v in (3, 4, 13, 14): t.p(x, y, 5)
            elif v in (8, 9): t.p(x, y, 2)
            else: t.p(x, y, 4)
    return t

def make_world_jungle(variant=0):
    t = Tile16(TPAL_FOLIAGE, 2)
    # Dense rainforest canopy with bright emerald fern fronds
    for cx, cy in [(4, 4), (11, 3), (8, 9), (3, 13), (12, 12)]:
        t.disc(cx + variant, cy, 4.0, 3.2, 3)
        t.disc(cx + variant - 0.5, cy - 0.8, 2.5, 2.0, 4)
        t.p((cx + variant) & 15, max(0, cy - 2), 6)
    t.p(6, 6, 13); t.p(12, 9, 12)  # tropical orchids
    return t

def make_world_coral(frame=0):
    t = make_water(frame, shallow=True)
    t.qpal = [TPAL_ACCENT, TPAL_ACCENT, TPAL_ACCENT, TPAL_ACCENT]
    t.rect(0, 0, 16, 16, 7)
    # Turquoise reef water + vibrant coral pink branches
    for y in range(16):
        for x in range(16):
            if ((x + frame * 2 + y * 3) & 7) == 0: t.p(x, y, 8)
    for cx, cy in [(4, 6), (11, 10), (7, 12)]:
        t.disc(cx, cy, 2.6, 2.2, 3)
        t.disc(cx - 0.4, cy - 0.5, 1.5, 1.3, 4)
        t.p(cx - 1, cy - 1, 5)
    return t

def make_world_tech():
    t = Tile16(TPAL_ACCENT, 10)
    t.rect(1, 1, 14, 14, 11)
    t.hline(1, 1, 14, 12); t.vline(1, 1, 14, 12)
    t.hline(2, 7, 12, 3); t.hline(2, 8, 12, 4)
    t.vline(7, 2, 12, 3); t.vline(8, 2, 12, 4)
    t.rect(6, 6, 4, 4, 13); t.rect(7, 7, 2, 2, 15)
    return t

def make_mtn_sky():
    t = Tile16(TPAL_WATER, 2)
    for y in range(16):
        c = 1 if y < 5 else (2 if y < 11 else 3)
        t.hline(0, y, 16, c)
    return t

# ------------------------------------------------------------------------------
# FEATHERED ORGANIC GRASS-TO-COBBLE & GRASS-TO-DIRT TRANSITIONS
# ------------------------------------------------------------------------------
def make_feathered_edge(base_fn, edge, moss_on_arch=False):
    if edge == 'E': return make_feathered_edge(base_fn, 'W', moss_on_arch).flip_h()
    if edge == 'S': return make_feathered_edge(base_fn, 'N', moss_on_arch).flip_v()
    t = base_fn(0)
    tuft = [4, 3, 5, 4, 2, 3, 4, 5, 3, 2, 4, 5, 3, 4, 3, 2]
    if moss_on_arch:
        c_deep, c_mid, c_lit = 2, 15, 15
    else:
        c_deep, c_mid, c_lit = 2, 4, 5
    if edge == 'N':
        for x in range(16):
            h = tuft[x]
            for y in range(h - 1):
                t.p(x, y, c_lit if (y == 0 and (x & 1)) else c_mid)
            t.p(x, h - 1, c_deep)
    else:  # 'W'
        for y in range(16):
            w = tuft[y]
            for x in range(w - 1):
                t.p(x, y, c_lit if (x == 0 and (y & 1)) else c_mid)
            t.p(w - 1, y, c_deep)
    return t

# ------------------------------------------------------------------------------
# BATTLE BACKDROP TILES
# ------------------------------------------------------------------------------
def make_battle_sky(shrine=False):
    t = Tile16(TPAL_WATER, 1 if shrine else 2)
    for y in range(16):
        t.hline(0, y, 16, 4 if (shrine and y < 8) else (1 if y < 6 else (2 if y < 12 else 3)))
    return t

def make_battle_ground(shrine=False):
    return make_shrine_floor(0) if shrine else make_grass(0)

# ==============================================================================
# 5. BUILD ALL 138 METATILES + 4 ANIMATION FRAMES FOR ANIMATED METATILES
# ==============================================================================
ANIM_META_NAMES = [
    "T_WATER", "T_FORD", "T_RAPIDS", "T_WORLD_OCEAN",
    "T_WORLD_SHALLOW", "T_FLOWERS", "T_TALL_GRASS", "T_SHRINE_RUNE",
    "T_FIREPLACE", "T_WORLD_LAVA", "T_WORLD_CORAL", "CM_SHORE_N",
    "CM_SHORE_W", "CM_SHORE_NW", "CM_PIER_WATER_EDGE",
]

def build_all_metatiles(frame=0):
    M = {}
    M["T_GRASS"]         = make_grass(0)
    M["T_GRASS_ALT"]     = make_grass(2)
    M["T_FLOWERS"]       = make_flowers(frame)
    M["T_TALL_GRASS"]    = make_tall_grass(frame)
    M["T_DIRT"]          = make_dirt(0)
    M["T_COBBLE"]        = make_cobble(0)
    M["T_WATER"]         = make_water(frame)
    M["T_FORD"]          = make_water(frame, shallow=True)
    M["T_BRIDGE"]        = make_bridge()
    M["T_BRIDGE_RAIL_N"] = make_bridge_rail(False)
    M["T_BRIDGE_RAIL_S"] = make_bridge_rail(True)

    # Default meadow-backed props
    t = make_grass(0); paint_fence(t, TPAL_FOLIAGE); M["T_FENCE"] = t
    t = make_grass(0); paint_tree_trunk(t); M["T_TREE_TRUNK"] = t
    t = make_grass(0); paint_bush(t, TPAL_FOLIAGE); M["T_BUSH"] = t

    M["T_WALL_WOOD"]     = make_wall_wood()
    M["T_WALL_WINDOW"]   = make_wall_window()
    M["T_WALL_DOOR"]     = make_wall_door()
    M["T_ROOF_BODY"]     = make_roof_body()
    M["T_ROOF_TOP"]      = make_roof_top()
    M["T_WINDMILL_WALL"] = make_windmill_wall()
    M["T_WINDMILL_DOOR"] = make_windmill_door()
    M["T_WINDMILL_ROOF"] = make_windmill_roof()
    M["T_SHRINE_FLOOR"]  = make_shrine_floor(0)
    M["T_SHRINE_RUNE"]   = make_shrine_rune(frame)
    t = make_shrine_floor(0); paint_shrine_pillar(t); M["T_SHRINE_PILLAR"] = t

    t = make_grass(0); paint_signpost(t, TPAL_FOLIAGE); M["T_SIGNPOST"] = t
    t = make_grass(0); paint_lantern(t, TPAL_FOLIAGE); M["T_LANTERN_POST"] = t
    t = make_grass(0); paint_flower_bed(t, TPAL_FOLIAGE); M["T_FLOWER_BED"] = t

    M["T_MTN_SKY"]       = make_mtn_sky()
    M["T_CLIFF_WALL"]    = make_cliff_wall()
    M["T_CLIFF_LEDGE"]   = make_cliff_ledge()
    t = make_grass(0); paint_boulder(t, TPAL_FOLIAGE); M["T_BOULDER"] = t
    M["T_AUTUMN_GRASS"]  = make_autumn_grass(0)
    M["T_ROCKY_PATH"]    = make_rocky_path(0)
    M["T_RAPIDS"]        = make_water(frame, rapids=True)
    M["T_RAIL_TRACK"]    = make_rail_track()
    t = make_autumn_grass(0); paint_tree_trunk(t, autumn=True); M["T_MAPLE_TRUNK"] = t
    t = make_autumn_grass(0); paint_tree_trunk(t, pine=True); M["T_PINE_TRUNK"] = t

    M["T_WOOD_FLOOR"]    = make_wood_floor(0)
    M["T_HEARTH_RUG"]    = make_hearth_rug()
    M["T_FIREPLACE"]     = make_fireplace(frame)
    M["T_BED_QUILT"]     = make_bed_quilt()
    M["T_ATTIC_CHEST"]   = make_attic_chest()
    M["T_HOLLOW_OAK"]    = make_hollow_oak()
    M["T_STONE_STAIRS"]  = make_stone_stairs()
    M["T_INTERIOR_WALL"] = make_interior_wall()

    M["T_WORLD_OCEAN"]   = make_water(frame, deep=True)
    M["T_WORLD_BEACH"]   = make_world_beach()
    M["T_WORLD_PEAK"]    = make_world_peak()
    M["T_WORLD_FOREST"]  = make_world_forest()
    M["T_WORLD_HILL"]    = make_world_hill()
    M["T_WORLD_CASTLE"]  = make_world_castle()
    M["T_WORLD_TOWN"]    = make_world_town()
    M["T_WORLD_RUINS"]   = make_world_ruins()
    M["T_WORLD_SHALLOW"] = make_water(frame, shallow=True)
    M["T_WORLD_SAND"]    = make_world_sand(0)
    M["T_WORLD_SNOW"]    = make_world_snow(0)
    M["T_WORLD_LAVA"]    = make_world_lava(frame)
    M["T_WORLD_JUNGLE"]  = make_world_jungle(0)
    M["T_WORLD_CORAL"]   = make_world_coral(frame)
    M["T_WORLD_TECH"]    = make_world_tech()
    t = make_world_sand(0); paint_tree_trunk(t, palm=True); M["T_PALM_TREE"] = t
    M["T_DOCK_PIER"]     = make_dock_pier()

    # Overhead BG2 Canopies & Eaves
    M["CM_OV_CANOPY_TL"]   = make_canopy_quad('TL', False)
    M["CM_OV_CANOPY_TR"]   = make_canopy_quad('TR', False)
    M["CM_OV_CANOPY_BL"]   = make_canopy_quad('BL', False)
    M["CM_OV_CANOPY_BR"]   = make_canopy_quad('BR', False)
    M["CM_OV_ROOF_EAVE"]   = make_roof_eave()
    M["CM_OV_SHRINE_ARCH"] = make_shrine_arch()
    M["CM_OV_MAPLE_TL"]    = make_canopy_quad('TL', True)
    M["CM_OV_MAPLE_TR"]    = make_canopy_quad('TR', True)
    M["CM_OV_PINE_TOP"]    = make_pine_top()
    M["CM_OV_OAK_CROWN"]   = make_oak_crown()

    # Spatial Ground Variations
    M["CM_GRASS_V1"]      = make_grass(1)
    M["CM_GRASS_V2"]      = make_grass(2)
    M["CM_GRASS_V3"]      = make_grass(3)
    M["CM_AUTUMN_V1"]     = make_autumn_grass(1)
    M["CM_AUTUMN_V2"]     = make_autumn_grass(2)
    M["CM_COBBLE_V1"]     = make_cobble(1)
    M["CM_COBBLE_V2"]     = make_cobble(2)
    M["CM_COBBLE_V3"]     = make_cobble(3)
    M["CM_DIRT_V1"]       = make_dirt(1)
    M["CM_ROCKY_V1"]      = make_rocky_path(1)
    M["CM_WOOD_FLOOR_V1"] = make_wood_floor(1)
    M["CM_SAND_V1"]       = make_world_sand(1)
    M["CM_SNOW_V1"]       = make_world_snow(1)
    M["CM_JUNGLE_V1"]     = make_world_jungle(1)
    M["CM_SHRINE_V1"]     = make_shrine_floor(1)
    M["CM_OCEAN_V1"]      = make_water((frame + 2) & 3, deep=True)

    # Rounded Shorelines & Organic Feathered Edges
    for e in ('N', 'S', 'W', 'E', 'NW', 'NE', 'SW', 'SE'):
        M[f"CM_SHORE_{e}"] = make_shore(e, frame)
    for e in ('N', 'S', 'W', 'E'):
        M[f"CM_COBBLE_EDGE_{e}"] = make_feathered_edge(make_cobble, e, moss_on_arch=True)
        M[f"CM_DIRT_EDGE_{e}"]   = make_feathered_edge(make_dirt, e, moss_on_arch=False)

    # Cast Ground Shadows & Canopy Dapple
    arch_ramp = {5: 4, 4: 3, 3: 2, 2: 1, 9: 8, 8: 7, 7: 6, 15: 2}
    t = make_grass(0); t.darken_strip_n(4); M["CM_GRASS_SHAD_N"] = t
    t = make_grass(0); t.darken_strip_w(4); M["CM_GRASS_SHAD_W"] = t
    t = make_cobble(0); t.darken_strip_n(5, arch_ramp); M["CM_COBBLE_SHAD_N"] = t
    t = make_cobble(0); t.darken_strip_w(4, arch_ramp); M["CM_COBBLE_SHAD_W"] = t
    t = make_dirt(0); t.darken_strip_n(4); M["CM_DIRT_SHAD_N"] = t
    t = make_autumn_grass(0); t.darken_strip_n(4); M["CM_AUTUMN_SHAD_N"] = t
    t = make_rocky_path(0); t.darken_strip_n(5, arch_ramp); M["CM_ROCKY_SHAD_N"] = t
    t = make_wood_floor(0); t.darken_strip_n(4, arch_ramp); M["CM_WOOD_SHAD_N"] = t
    t = make_grass(0); t.dapple_canopy(); M["CM_GRASS_DAPPLE"] = t
    t = make_autumn_grass(0); t.dapple_canopy(); M["CM_AUTUMN_DAPPLE"] = t

    # Seamless Ground-Composited Props (ZERO green boxes on Cobblestone / Pier / Autumn / Dirt!)
    t = make_cobble(0); paint_lantern(t, TPAL_ARCH); M["CM_LAMP_COBBLE"] = t
    t = make_cobble(0); paint_signpost(t, TPAL_ARCH); M["CM_SIGN_COBBLE"] = t
    t = make_cobble(0); paint_flower_bed(t, TPAL_ARCH); M["CM_FLOWERBED_COBBLE"] = t
    t = make_cobble(0); paint_bush(t, TPAL_ARCH); M["CM_BUSH_COBBLE"] = t
    t = make_cobble(0); paint_fence(t, TPAL_ARCH); M["CM_FENCE_COBBLE"] = t

    t = make_dock_pier(); paint_lantern(t, TPAL_ARCH); M["CM_LAMP_PIER"] = t
    t = make_dock_pier(); paint_signpost(t, TPAL_ARCH); M["CM_SIGN_PIER"] = t
    t = make_dock_pier(); paint_boulder(t, TPAL_ARCH); M["CM_BOULDER_PIER"] = t

    t = make_autumn_grass(0); paint_lantern(t, TPAL_FOLIAGE); M["CM_LAMP_AUTUMN"] = t
    t = make_autumn_grass(0); paint_signpost(t, TPAL_FOLIAGE); M["CM_SIGN_AUTUMN"] = t
    t = make_autumn_grass(0); paint_boulder(t, TPAL_FOLIAGE); M["CM_BOULDER_AUTUMN"] = t

    t = make_dirt(0); paint_lantern(t, TPAL_FOLIAGE); M["CM_LAMP_DIRT"] = t
    t = make_dirt(0); paint_signpost(t, TPAL_FOLIAGE); M["CM_SIGN_DIRT"] = t
    M["CM_PIER_WATER_EDGE"] = make_pier_water_edge(frame)

    # Battle Backdrops, Evertale Coastlines & Empty
    M["CM_BATTLE_SKY_MEADOW"] = make_battle_sky(False)
    M["CM_BATTLE_SKY_SHRINE"] = make_battle_sky(True)
    M["CM_BATTLE_GND_MEADOW"] = make_battle_ground(False)
    M["CM_BATTLE_GND_SHRINE"] = make_battle_ground(True)
    for e in ('N', 'S', 'W', 'E'):
        M[f"CM_WORLD_COAST_{e}"] = make_world_coast(e)
    M["CM_EMPTY"] = Tile16(TPAL_FOLIAGE, 0)

    # 138..147: Opaque-backed overhead canopies & eaves (for SNES BG1 & Genesis Parallax mode!)
    def make_opq_canopy(ov_tile, bg_tile, prio=1):
        out_t = bg_tile.clone()
        out_t.prio = prio
        if ov_tile.qpal[0] == out_t.qpal[0]:
            out_t.overlay_from(ov_tile)
        else:
            # Autumn maple canopy (TPAL_ACCENT) over warm shaded earth/foliage accent base (indices 6,7)
            out_t = Tile16(ov_tile.qpal[0], 6, prio=prio)
            for y in range(16):
                for x in range(16):
                    out_t.p(x, y, 7 if ((x + y) & 3) else 6)
            out_t.overlay_from(ov_tile)
        return out_t

    dapple_g = make_grass(0); dapple_g.dapple_canopy()
    M["CM_OPQ_CANOPY_TL"]   = make_opq_canopy(M["CM_OV_CANOPY_TL"], dapple_g)
    M["CM_OPQ_CANOPY_TR"]   = M["CM_OPQ_CANOPY_TL"].flip_h()
    M["CM_OPQ_CANOPY_BL"]   = make_opq_canopy(M["CM_OV_CANOPY_BL"], dapple_g)
    M["CM_OPQ_CANOPY_BR"]   = M["CM_OPQ_CANOPY_BL"].flip_h()
    eave_bg = make_wall_wood(); eave_bg.overlay_from(M["CM_OV_ROOF_EAVE"]); eave_bg.prio = 1
    M["CM_OPQ_ROOF_EAVE"]   = eave_bg
    arch_bg = make_shrine_floor(0); arch_bg.overlay_from(M["CM_OV_SHRINE_ARCH"]); arch_bg.prio = 1
    M["CM_OPQ_SHRINE_ARCH"] = arch_bg
    M["CM_OPQ_MAPLE_TL"]    = make_opq_canopy(M["CM_OV_MAPLE_TL"], dapple_g)
    M["CM_OPQ_MAPLE_TR"]    = M["CM_OPQ_MAPLE_TL"].flip_h()
    M["CM_OPQ_PINE_TOP"]    = make_opq_canopy(M["CM_OV_PINE_TOP"], dapple_g)
    M["CM_OPQ_OAK_CROWN"]   = make_opq_canopy(M["CM_OV_OAK_CROWN"], dapple_g)

    return [M[name] for name in META_NAMES]

# ==============================================================================
# 6. 8x8 TILE COMPILER WITH H/V FLIP DEDUP + HARDWARE BUDGET FOLDING
# ==============================================================================
# Exact color remapping from 4-palette (TPAL_*) indices into Genesis 2-palette (PAL0 / PAL3):
#   TPAL_FOLIAGE -> GEN PAL0 (slot 0)
#   TPAL_ARCH    -> GEN PAL0 (timber/roofs) OR GEN PAL3 (stone/cobble) depending on pixel content!
#   TPAL_WATER   -> GEN PAL3 (slot 3)
#   TPAL_ACCENT  -> GEN PAL3 (slot 3)
GEN_MAP_FOLIAGE_TO_P0 = [0, 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 14, 13, 15, 14, 2]
GEN_MAP_ARCH_TO_P0    = [0, 1, 6, 7, 8, 9, 6, 7, 8, 9, 10, 11, 12, 13, 14, 3]
GEN_MAP_ARCH_TO_P3    = [0, 1, 6, 7, 8, 9, 1, 6, 8, 9, 11, 12, 13, 14, 15, 4]
GEN_MAP_WATER_TO_P3   = [0, 2, 3, 4, 1, 6, 7, 9, 10, 1, 6, 7, 4, 9, 15, 5]
GEN_MAP_ACCENT_TO_P3  = [0, 1, 11, 12, 13, 14, 2, 3, 4, 5, 6, 7, 14, 14, 15, 5]

def remap_quad_for_genesis(q8, qpal):
    """Map an 8x8 tile from 4-palette space onto Genesis PAL0 (0) or PAL3 (3)."""
    if qpal == TPAL_FOLIAGE:
        return tuple(GEN_MAP_FOLIAGE_TO_P0[c] for c in q8), 0
    if qpal == TPAL_WATER:
        return tuple(GEN_MAP_WATER_TO_P3[c] for c in q8), 3
    if qpal == TPAL_ACCENT:
        return tuple(GEN_MAP_ACCENT_TO_P3[c] for c in q8), 3
    # TPAL_ARCH: check if quadrant is predominantly stone/cobble (indices 2..5) vs wood/roof (6..12)
    stone_cnt = sum(1 for c in q8 if c in (2, 3, 4, 5))
    wood_cnt  = sum(1 for c in q8 if c in (6, 7, 8, 9, 10, 11, 12))
    if stone_cnt > wood_cnt:
        return tuple(GEN_MAP_ARCH_TO_P3[c] for c in q8), 3
    return tuple(GEN_MAP_ARCH_TO_P0[c] for c in q8), 0

def extract_quad8(tile16, qidx):
    ox = 8 if (qidx & 1) else 0
    oy = 8 if (qidx & 2) else 0
    out = []
    for y in range(8):
        row = tile16.px[oy + y]
        for x in range(8):
            out.append(row[ox + x])
    return tuple(out)

def flip_q8(q8, hf, vf):
    out = [0] * 64
    for y in range(8):
        sy = (7 - y) if vf else y
        for x in range(8):
            sx = (7 - x) if hf else x
            out[y * 8 + x] = q8[sy * 8 + sx]
    return tuple(out)

def pack_snes_4bpp(q8):
    """Pack 64 4-bit pixels into 32 bytes of SNES 4bpp planar VRAM format."""
    b = bytearray(32)
    for y in range(8):
        p0 = p1 = p2 = p3 = 0
        for x in range(8):
            c = q8[y * 8 + x] & 15
            bit = 7 - x
            if c & 1: p0 |= (1 << bit)
            if c & 2: p1 |= (1 << bit)
            if c & 4: p2 |= (1 << bit)
            if c & 8: p3 |= (1 << bit)
        b[y * 2]      = p0
        b[y * 2 + 1]  = p1
        b[16 + y * 2] = p2
        b[16 + y * 2 + 1] = p3
    return bytes(b)

def pack_gen_4bpp(q8):
    """Pack 64 4-bit pixels into 8 u32 big-endian words (Genesis 4bpp packed format)."""
    words = []
    for y in range(8):
        w = 0
        for x in range(8):
            w = (w << 4) | (q8[y * 8 + x] & 15)
        words.append(w)
    return words

def compile_console_bank(frames_meta, max_tiles, is_genesis=False):
    """
    Compile 138 metatiles + 4 animation frames into a deduplicated 8x8 tilebank
    strictly <= max_tiles.
      - Tile 0: always empty transparent
      - Tiles 1..ANIM_SLOTS: contiguous animated 8x8 tiles (4 frames each!)
      - Tiles ANIM_SLOTS+1..N: static deduplicated 8x8 tiles (with H/V/HV flip dedup!)
    """
    # Step 1: Collect animated 8x8 slots from primary animated tiles (deduped across 4-frame sequences!)
    anim_keys = {}   # (f0, f1, f2, f3) -> (tid, hf, vf)
    anim_seqs = []   # list of [q8_f0, q8_f1, q8_f2, q8_f3]

    # Always reserve tile 0 = empty
    static_tiles = [tuple([0] * 64)]
    static_map = {tuple([0] * 64): (0, 0, 0)}

    # Keep animated bank compact (<= 32 tiles -> <= 1024 B VBlank DMA on SNES & Genesis!)
    CORE_ANIM = [
        "T_WATER", "T_FORD", "T_RAPIDS", "T_WORLD_OCEAN",
        "T_FLOWERS", "T_TALL_GRASS", "T_SHRINE_RUNE", "CM_SHORE_N",
    ]
    CORE_ANIM_SET = set(CORE_ANIM)

    def get_quad(meta_id, qidx, frame_idx):
        t = frames_meta[frame_idx][meta_id]
        q8 = extract_quad8(t, qidx)
        pal = t.qpal[qidx]
        if is_genesis:
            q8, pal = remap_quad_for_genesis(q8, pal)
        return q8, pal, t.prio

    # Register animated sequences first (indices 1..K)
    for mname in CORE_ANIM:
        mid = META_IDX[mname]
        for qidx in range(4):
            seq = tuple(get_quad(mid, qidx, f)[0] for f in range(4))
            # Check if constant across frames -> treat as static
            if seq[0] == seq[1] == seq[2] == seq[3]:
                continue
            found = None
            for vf in (0, 1):
                for hf in (0, 1):
                    fseq = tuple(flip_q8(s, hf, vf) for s in seq)
                    if fseq in anim_keys:
                        tid, _, _ = anim_keys[fseq]
                        found = (tid, hf, vf)
                        break
                if found:
                    break
            if not found and len(anim_seqs) < 32:
                tid = 1 + len(anim_seqs)
                anim_seqs.append(seq)
                anim_keys[seq] = (tid, 0, 0)

    anim_count = len(anim_seqs)
    # Seed static_tiles with frame 0 of each animated tile at indices 1..anim_count
    for seq in anim_seqs:
        static_tiles.append(seq[0])

    # Step 2: Register all 138 metatiles' quadrants
    meta_quads = []  # [mid][qidx] = (tid, pal, hf, vf, prio)
    for mid, mname in enumerate(META_NAMES):
        qlist = []
        for qidx in range(4):
            q0, pal, prio = get_quad(mid, qidx, 0)
            seq = tuple(get_quad(mid, qidx, f)[0] for f in range(4))
            # First check if this quadrant matches an animated sequence (including mirrored shorelines!)
            matched = None
            if not (seq[0] == seq[1] == seq[2] == seq[3]):
                for vf in (0, 1):
                    for hf in (0, 1):
                        fseq = tuple(flip_q8(s, hf, vf) for s in seq)
                        if fseq in anim_keys:
                            tid, _, _ = anim_keys[fseq]
                            matched = (tid, pal, hf, vf, prio)
                            break
                    if matched:
                        break
            if matched:
                qlist.append(matched)
                continue

            # Check static dictionary (with exact + H/V/HV flip dedup!)
            found = None
            for vf in (0, 1):
                for hf in (0, 1):
                    fq = flip_q8(q0, hf, vf)
                    if fq in static_map:
                        tid, _, _ = static_map[fq]
                        found = (tid, pal, hf, vf, prio)
                        break
                if found:
                    break
            if found:
                qlist.append(found)
            else:
                tid = len(static_tiles)
                static_tiles.append(q0)
                static_map[q0] = (tid, 0, 0)
                qlist.append((tid, pal, 0, 0, prio))
        meta_quads.append(qlist)

    # Step 3: If over hardware VRAM budget, fold the closest non-animated tiles gracefully!
    if len(static_tiles) > max_tiles:
        # Compute Hamming distance between static tiles above anim_count and fold closest pairs
        remap = list(range(len(static_tiles)))
        active = list(range(1 + anim_count, len(static_tiles)))
        while len(active) + 1 + anim_count > max_tiles:
            best_d = 999
            best_pair = None
            # Compare last-added tile (least critical variant) against earlier tiles
            cand = active[-1]
            qc = static_tiles[cand]
            for other in active[:-1]:
                qo = static_tiles[other]
                d = sum(1 for a, b in zip(qc, qo) if a != b)
                if d < best_d:
                    best_d = d
                    best_pair = (cand, other)
                    if d <= 2:
                        break
            c_idx, o_idx = best_pair
            remap[c_idx] = o_idx
            active.pop()

        # Compact surviving tile indices
        keep = list(range(0, 1 + anim_count)) + active
        new_id = {}
        new_tiles = []
        for old in keep:
            new_id[old] = len(new_tiles)
            new_tiles.append(static_tiles[old])
        for i in range(len(remap)):
            r = i
            while remap[r] != r:
                r = remap[r]
            remap[i] = new_id[r]

        static_tiles = new_tiles
        for mid in range(len(meta_quads)):
            for qidx in range(4):
                tid, pal, hf, vf, prio = meta_quads[mid][qidx]
                meta_quads[mid][qidx] = (remap[tid], pal, hf, vf, prio)

    return static_tiles, anim_seqs, meta_quads

# ==============================================================================
# 7. EMITTERS FOR SNES ASM, SHARED C HEADER (GENESIS + SDL2 + SNES LUTS), AND WEB TS
# ==============================================================================
def emit_snes_asm(snes_tiles, snes_anim):
    lines = [
        "; Auto-generated by engine/tools/bake_chrono_tileset.py — DO NOT EDIT",
        f"; Unified Chrono/Mana 4bpp SNES Terrain Bank ({len(snes_tiles)} tiles + {len(snes_anim)} anim x 4 frames)",
        "",
        "; Appended after cinema_cg_snes.asm (which includes hdr.asm) into data.asm",
        "",
        '.section ".rodata_chrono_terrain" superfree',
        "",
        "snes_chrono_tiles:",
    ]
    for tid, q8 in enumerate(snes_tiles):
        b = pack_snes_4bpp(q8)
        for off in (0, 16):
            hexs = ",".join(f"${v:02X}" for v in b[off:off+16])
            lines.append(f".db {hexs}")
    lines.append("")
    lines.append("snes_chrono_anim_frames:")
    for f in range(4):
        for seq in snes_anim:
            b = pack_snes_4bpp(seq[f])
            for off in (0, 16):
                hexs = ",".join(f"${v:02X}" for v in b[off:off+16])
                lines.append(f".db {hexs}")
    lines.append("")
    lines.append(".ends")
    lines.append("")
    with open(OUT_ASM, "w") as fp:
        fp.write("\n".join(lines))

def emit_c_header(zone_pals, gen_pals, frames_meta, snes_tiles, snes_anim, snes_meta, gen_tiles, gen_anim, gen_meta):
    out = []
    out.append("/* ============================================================================")
    out.append(" * hearth_terrain_rom.h — Auto-generated by engine/tools/bake_chrono_tileset.py")
    out.append(" * Unified 16-Bit Chrono/Mana Master Tileset, 60-Color Biome Palettes & Autotiler")
    out.append(" * Pure ANSI C89/C99 compatible (SNES 816-tcc, Genesis SGDK, SDL2, WASM).")
    out.append(" * ============================================================================ */")
    out.append("#ifndef HEARTH_TERRAIN_ROM_H")
    out.append("#define HEARTH_TERRAIN_ROM_H")
    out.append("")
    out.append("#include \"hearth_data.h\"")
    out.append("")
    out.append(f"#define CHRONO_META_COUNT       {len(META_NAMES)}u")
    out.append(f"#define SNES_CHRONO_TILE_COUNT  {len(snes_tiles)}u")
    out.append(f"#define SNES_CHRONO_ANIM_COUNT  {len(snes_anim)}u")
    out.append(f"#define GEN_CHRONO_TILE_COUNT   {len(gen_tiles)}u")
    out.append(f"#define GEN_CHRONO_ANIM_COUNT   {len(gen_anim)}u")
    out.append("")

    # Enum of extended metatile IDs (63..137)
    out.append("enum {")
    for idx in range(63, len(META_NAMES)):
        comma = "," if idx + 1 < len(META_NAMES) else ""
        out.append(f"    {META_NAMES[idx]} = {idx}{comma}")
    out.append("};")
    out.append("")

    # ------------------------------------------------------------------
    # SHARED PORTABLE C89/C99 AUTOTILE, PROP-UNDERLAY & CAST-SHADOW RESOLVER
    # ------------------------------------------------------------------
    out.append("/* Portable C89/C99 spatial-variation, curved shoreline, prop-underlay & cast-shadow resolver */")
    out.append("static uint8_t hearth_is_solid_caster(uint8_t t) {")
    out.append("    return (t == T_WALL_WOOD || t == T_WALL_WINDOW || t == T_WALL_DOOR ||")
    out.append("            t == T_WINDMILL_WALL || t == T_WINDMILL_DOOR || t == T_CLIFF_WALL ||")
    out.append("            t == T_INTERIOR_WALL || t == T_HOLLOW_OAK) ? 1u : 0u;")
    out.append("}")
    out.append("")
    out.append("static uint8_t hearth_is_water_like(uint8_t t) {")
    out.append("    return (t == T_WATER || t == T_FORD || t == T_WORLD_OCEAN || t == T_WORLD_SHALLOW) ? 1u : 0u;")
    out.append("}")
    out.append("")
    out.append("static uint8_t hearth_is_grass_like(uint8_t t) {")
    out.append("    return (t == T_GRASS || t == T_GRASS_ALT || t == T_FLOWERS ||")
    out.append("            t == T_TALL_GRASS || t == T_AUTUMN_GRASS) ? 1u : 0u;")
    out.append("}")
    out.append("")
    out.append("/* Infer dominant walkable ground underneath a prop (eliminates green-box props on stone/pier!) */")
    out.append("static uint8_t hearth_infer_prop_ground(uint8_t tn, uint8_t ts, uint8_t tw, uint8_t te) {")
    out.append("    uint8_t cob = 0u, pier = 0u, aut = 0u, dirt = 0u;")
    out.append("    if (tn == T_COBBLE) cob++;")
    out.append("    if (ts == T_COBBLE) cob++;")
    out.append("    if (tw == T_COBBLE) cob++;")
    out.append("    if (te == T_COBBLE) cob++;")
    out.append("    if (tn == T_DOCK_PIER || tn == T_BRIDGE) pier++;")
    out.append("    if (ts == T_DOCK_PIER || ts == T_BRIDGE) pier++;")
    out.append("    if (tw == T_DOCK_PIER || tw == T_BRIDGE) pier++;")
    out.append("    if (te == T_DOCK_PIER || te == T_BRIDGE) pier++;")
    out.append("    if (tn == T_AUTUMN_GRASS || tn == T_ROCKY_PATH) aut++;")
    out.append("    if (ts == T_AUTUMN_GRASS || ts == T_ROCKY_PATH) aut++;")
    out.append("    if (tw == T_AUTUMN_GRASS || tw == T_ROCKY_PATH) aut++;")
    out.append("    if (te == T_AUTUMN_GRASS || te == T_ROCKY_PATH) aut++;")
    out.append("    if (tn == T_DIRT) dirt++;")
    out.append("    if (ts == T_DIRT) dirt++;")
    out.append("    if (tw == T_DIRT) dirt++;")
    out.append("    if (te == T_DIRT) dirt++;")
    out.append("    if (pier >= 1u) return 2u; /* Pier/Timber */")
    out.append("    if (cob >= 1u)  return 1u; /* Cobblestone */")
    out.append("    if (aut >= 1u)  return 3u; /* Autumn Meadow */")
    out.append("    if (dirt >= 2u) return 4u; /* Dirt Path */")
    out.append("    return 0u; /* Lush Meadow Grass */")
    out.append("}")
    out.append("")
    out.append("static uint8_t hearth_resolve_chrono_meta(uint8_t t, uint8_t ov,")
    out.append("                                          uint8_t tn, uint8_t ts, uint8_t tw, uint8_t te,")
    out.append("                                          uint16_t mx, uint16_t my, uint8_t is_world) {")
    out.append("    uint8_t v = (uint8_t)(((mx * 3u + my * 5u) ^ (mx >> 1)) & 3u);")
    out.append("    uint8_t gnd;")
    out.append("    /* 1. Seamless Ground-Composited Props (ZERO green boxes on Cobblestone/Pier/Autumn/Dirt!) */")
    out.append("    if (t == T_LANTERN_POST || t == T_SIGNPOST || t == T_FLOWER_BED || t == T_BUSH || t == T_FENCE || t == T_BOULDER) {")
    out.append("        gnd = hearth_infer_prop_ground(tn, ts, tw, te);")
    out.append("        if (gnd == 1u) { /* Cobblestone plaza */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_COBBLE;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_COBBLE;")
    out.append("            if (t == T_FLOWER_BED)   return (uint8_t)CM_FLOWERBED_COBBLE;")
    out.append("            if (t == T_BUSH)         return (uint8_t)CM_BUSH_COBBLE;")
    out.append("            if (t == T_FENCE)        return (uint8_t)CM_FENCE_COBBLE;")
    out.append("        } else if (gnd == 2u) { /* Timber Pier */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_PIER;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_PIER;")
    out.append("            if (t == T_BOULDER)      return (uint8_t)CM_BOULDER_PIER;")
    out.append("        } else if (gnd == 3u) { /* Autumn Mountain */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_AUTUMN;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_AUTUMN;")
    out.append("            if (t == T_BOULDER)      return (uint8_t)CM_BOULDER_AUTUMN;")
    out.append("        } else if (gnd == 4u) { /* Dirt Road */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_DIRT;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_DIRT;")
    out.append("        }")
    out.append("        return t;")
    out.append("    }")
    out.append("    /* 2. Timber Pier South Water-Piling Edge */")
    out.append("    if (t == T_DOCK_PIER && hearth_is_water_like(ts)) {")
    out.append("        return (uint8_t)CM_PIER_WATER_EDGE;")
    out.append("    }")
    out.append("    /* 3. Rounded Shorelines & Curved Coves on Water / Shallows */")
    out.append("    if (!is_world && (t == T_WATER || t == T_FORD)) {")
    out.append("        uint8_t ln = (!hearth_is_water_like(tn) && tn != T_BRIDGE && tn != T_BRIDGE_RAIL_N && tn != T_BRIDGE_RAIL_S && tn != T_DOCK_PIER) ? 1u : 0u;")
    out.append("        uint8_t ls = (!hearth_is_water_like(ts) && ts != T_BRIDGE && ts != T_BRIDGE_RAIL_N && ts != T_BRIDGE_RAIL_S && ts != T_DOCK_PIER) ? 1u : 0u;")
    out.append("        uint8_t lw = (!hearth_is_water_like(tw) && tw != T_BRIDGE && tw != T_BRIDGE_RAIL_N && tw != T_BRIDGE_RAIL_S && tw != T_DOCK_PIER) ? 1u : 0u;")
    out.append("        uint8_t le = (!hearth_is_water_like(te) && te != T_BRIDGE && te != T_BRIDGE_RAIL_N && te != T_BRIDGE_RAIL_S && te != T_DOCK_PIER) ? 1u : 0u;")
    out.append("        if (ln && lw) return (uint8_t)CM_SHORE_NW;")
    out.append("        if (ln && le) return (uint8_t)CM_SHORE_NE;")
    out.append("        if (ls && lw) return (uint8_t)CM_SHORE_SW;")
    out.append("        if (ls && le) return (uint8_t)CM_SHORE_SE;")
    out.append("        if (ln) return (uint8_t)CM_SHORE_N;")
    out.append("        if (ls) return (uint8_t)CM_SHORE_S;")
    out.append("        if (lw) return (uint8_t)CM_SHORE_W;")
    out.append("        if (le) return (uint8_t)CM_SHORE_E;")
    out.append("        return t;")
    out.append("    }")
    out.append("    /* 4. Evertale Open World Curved Sandy Coastlines */")
    out.append("    if (is_world && t == T_WORLD_SHALLOW) {")
    out.append("        uint8_t ln = (!hearth_is_water_like(tn) && tn != T_WORLD_CORAL) ? 1u : 0u;")
    out.append("        uint8_t ls = (!hearth_is_water_like(ts) && ts != T_WORLD_CORAL) ? 1u : 0u;")
    out.append("        uint8_t lw = (!hearth_is_water_like(tw) && tw != T_WORLD_CORAL) ? 1u : 0u;")
    out.append("        uint8_t le = (!hearth_is_water_like(te) && te != T_WORLD_CORAL) ? 1u : 0u;")
    out.append("        if (ln && !ls) return (uint8_t)CM_WORLD_COAST_N;")
    out.append("        if (ls && !ln) return (uint8_t)CM_WORLD_COAST_S;")
    out.append("        if (lw && !le) return (uint8_t)CM_WORLD_COAST_W;")
    out.append("        if (le && !lw) return (uint8_t)CM_WORLD_COAST_E;")
    out.append("        return t;")
    out.append("    }")
    out.append("    /* 5. Cast Ground Shadows & Canopy Dapple */")
    out.append("    if (!is_world) {")
    out.append("        if (t == T_COBBLE) {")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_COBBLE_SHAD_N;")
    out.append("            if (hearth_is_solid_caster(tw)) return (uint8_t)CM_COBBLE_SHAD_W;")
    out.append("            if (hearth_is_grass_like(tn)) return (uint8_t)CM_COBBLE_EDGE_N;")
    out.append("            if (hearth_is_grass_like(ts)) return (uint8_t)CM_COBBLE_EDGE_S;")
    out.append("            if (hearth_is_grass_like(tw)) return (uint8_t)CM_COBBLE_EDGE_W;")
    out.append("            if (hearth_is_grass_like(te)) return (uint8_t)CM_COBBLE_EDGE_E;")
    out.append("            if (v == 1u) return (uint8_t)CM_COBBLE_V1;")
    out.append("            if (v == 2u) return (uint8_t)CM_COBBLE_V2;")
    out.append("            if (v == 3u) return (uint8_t)CM_COBBLE_V3;")
    out.append("            return T_COBBLE;")
    out.append("        }")
    out.append("        if (t == T_GRASS || t == T_GRASS_ALT) {")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_GRASS_SHAD_N;")
    out.append("            if (hearth_is_solid_caster(tw)) return (uint8_t)CM_GRASS_SHAD_W;")
    out.append("            if (ov != 0u) return (uint8_t)CM_GRASS_DAPPLE;")
    out.append("            if (v == 1u) return (uint8_t)CM_GRASS_V1;")
    out.append("            if (v == 2u) return (uint8_t)CM_GRASS_V2;")
    out.append("            if (v == 3u) return (uint8_t)CM_GRASS_V3;")
    out.append("            return T_GRASS;")
    out.append("        }")
    out.append("        if (t == T_AUTUMN_GRASS) {")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_AUTUMN_SHAD_N;")
    out.append("            if (ov != 0u) return (uint8_t)CM_AUTUMN_DAPPLE;")
    out.append("            if (v == 1u) return (uint8_t)CM_AUTUMN_V1;")
    out.append("            if (v == 2u) return (uint8_t)CM_AUTUMN_V2;")
    out.append("            return T_AUTUMN_GRASS;")
    out.append("        }")
    out.append("        if (t == T_DIRT) {")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_DIRT_SHAD_N;")
    out.append("            if (hearth_is_grass_like(tn)) return (uint8_t)CM_DIRT_EDGE_N;")
    out.append("            if (hearth_is_grass_like(ts)) return (uint8_t)CM_DIRT_EDGE_S;")
    out.append("            if (hearth_is_grass_like(tw)) return (uint8_t)CM_DIRT_EDGE_W;")
    out.append("            if (hearth_is_grass_like(te)) return (uint8_t)CM_DIRT_EDGE_E;")
    out.append("            return (v & 1u) ? (uint8_t)CM_DIRT_V1 : T_DIRT;")
    out.append("        }")
    out.append("        if (t == T_ROCKY_PATH) {")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_ROCKY_SHAD_N;")
    out.append("            return (v & 1u) ? (uint8_t)CM_ROCKY_V1 : T_ROCKY_PATH;")
    out.append("        }")
    out.append("        if (t == T_WOOD_FLOOR) {")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_WOOD_SHAD_N;")
    out.append("            return (v & 1u) ? (uint8_t)CM_WOOD_FLOOR_V1 : T_WOOD_FLOOR;")
    out.append("        }")
    out.append("        if (t == T_SHRINE_FLOOR && (v & 1u)) return (uint8_t)CM_SHRINE_V1;")
    out.append("    } else {")
    out.append("        /* World Map spatial variation */")
    out.append("        if (t == T_GRASS && v > 0u) return (uint8_t)(CM_GRASS_V1 + (v - 1u));")
    out.append("        if (t == T_WORLD_SAND && (v & 1u)) return (uint8_t)CM_SAND_V1;")
    out.append("        if (t == T_WORLD_SNOW && (v & 1u)) return (uint8_t)CM_SNOW_V1;")
    out.append("        if (t == T_WORLD_JUNGLE && (v & 1u)) return (uint8_t)CM_JUNGLE_V1;")
    out.append("        if (t == T_WORLD_OCEAN && (v & 1u)) return (uint8_t)CM_OCEAN_V1;")
    out.append("    }")
    out.append("    return (t < 63u) ? t : (uint8_t)T_GRASS;")
    out.append("}")
    out.append("")

    # ------------------------------------------------------------------
    # SECTION A: SNES 4-PALETTE (60-COLOR) BGR555 TABLES & METATILE LUTS
    # ------------------------------------------------------------------
    out.append("#ifdef HEARTH_INCLUDE_SNES_TERRAIN")
    out.append("extern char snes_chrono_tiles;")
    out.append("extern char snes_chrono_anim_frames;")
    out.append("")
    out.append("/* 18 Biome Moods x 4 BG1 Sub-Palettes (60 simultaneous colors!) in SNES BGR555 */")
    out.append("static const uint16_t SNES_CHRONO_PALS[ZPAL_COUNT][4][16] = {")
    for pid in range(len(zone_pals)):
        out.append(f"    {{ /* {ZPAL_NAMES[pid]} */")
        for sp in range(4):
            vals = ",".join(f"0x{rgb_to_snes555(c):04X}u" for c in zone_pals[pid][sp])
            comma = "," if sp < 3 else ""
            out.append(f"        {{{vals}}}{comma}")
        comma_z = "," if pid + 1 < len(zone_pals) else ""
        out.append(f"    }}{comma_z}")
    out.append("};")
    out.append("")

    # Pre-baked SNES tilemap words: vhopppcc cccccccc
    for qname, qidx in [("TL", 0), ("TR", 1), ("BL", 2), ("BR", 3)]:
        out.append(f"static const uint16_t SNES_META_{qname}[CHRONO_META_COUNT] = {{")
        row_vals = []
        for mid in range(len(META_NAMES)):
            tid, pal, hf, vf, prio = snes_meta[mid][qidx]
            word = (tid & 0x3FF) | ((pal & 7) << 10) | ((prio & 1) << 13) | ((hf & 1) << 14) | ((vf & 1) << 15)
            row_vals.append(f"0x{word:04X}u")
        for i in range(0, len(row_vals), 8):
            chunk = ",".join(row_vals[i:i+8])
            comma = "," if i + 8 < len(row_vals) else ""
            out.append(f"    {chunk}{comma}")
        out.append("};")
    out.append("#endif /* HEARTH_INCLUDE_SNES_TERRAIN */")
    out.append("")

    # ------------------------------------------------------------------
    # SECTION B: SEGA GENESIS 2-PALETTE (30-COLOR) BGR333 + 4bpp ROM BANK
    # ------------------------------------------------------------------
    out.append("#ifdef HEARTH_INCLUDE_GENESIS_TERRAIN")
    out.append("/* 18 Biome Moods x 2 Terrain Palettes (PAL0 + PAL3 = 30 simultaneous colors!) in BGR333 */")
    out.append("static const uint16_t GEN_CHRONO_PALS[ZPAL_COUNT][2][16] = {")
    for pid in range(len(gen_pals)):
        p0, p3 = gen_pals[pid]
        v0 = ",".join(f"0x{rgb_to_gen333(c):04X}u" for c in p0)
        v3 = ",".join(f"0x{rgb_to_gen333(c):04X}u" for c in p3)
        comma = "," if pid + 1 < len(gen_pals) else ""
        out.append(f"    {{ /* {ZPAL_NAMES[pid]} */ {{{v0}}}, {{{v3}}} }}{comma}")
    out.append("};")
    out.append("")
    out.append(f"static const uint32_t GEN_CHRONO_TILES[{len(gen_tiles)}u * 8u] = {{")
    for tid, q8 in enumerate(gen_tiles):
        ws = pack_gen_4bpp(q8)
        comma = "," if tid + 1 < len(gen_tiles) else ""
        out.append("    " + ",".join(f"0x{w:08X}u" for w in ws) + comma)
    out.append("};")
    out.append("")
    out.append(f"static const uint32_t GEN_CHRONO_ANIM[4u][{len(gen_anim)}u * 8u] = {{")
    for f in range(4):
        out.append("    {")
        for ai, seq in enumerate(gen_anim):
            ws = pack_gen_4bpp(seq[f])
            comma = "," if ai + 1 < len(gen_anim) else ""
            out.append("        " + ",".join(f"0x{w:08X}u" for w in ws) + comma)
        comma_f = "," if f < 3 else ""
        out.append(f"    }}{comma_f}")
    out.append("};")
    out.append("")
    # Pre-baked Genesis tilemap attributes (relative to VRAM_BG_BASE; add VRAM_BG_BASE at write time!)
    # Genesis tilemap word: PCCV HNNN NNNN NNNN (P=bit15, CC=bits14:13, V=bit12, H=bit11, tile=bits10:0)
    for qname, qidx in [("TL", 0), ("TR", 1), ("BL", 2), ("BR", 3)]:
        out.append(f"static const uint16_t GEN_META_{qname}[CHRONO_META_COUNT] = {{")
        row_vals = []
        for mid in range(len(META_NAMES)):
            tid, pal, hf, vf, prio = gen_meta[mid][qidx]
            word = (tid & 0x7FF) | ((hf & 1) << 11) | ((vf & 1) << 12) | ((pal & 3) << 13) | ((prio & 1) << 15)
            row_vals.append(f"0x{word:04X}u")
        for i in range(0, len(row_vals), 8):
            chunk = ",".join(row_vals[i:i+8])
            comma = "," if i + 8 < len(row_vals) else ""
            out.append(f"    {chunk}{comma}")
        out.append("};")
    out.append("#endif /* HEARTH_INCLUDE_GENESIS_TERRAIN */")
    out.append("")

    # ------------------------------------------------------------------
    # SECTION C: NATIVE PC (SDL2) LOSSLESS 60-COLOR 16x16 TILEBANK
    # ------------------------------------------------------------------
    out.append("#ifdef HEARTH_INCLUDE_SDL2_TERRAIN")
    out.append("/* 18 Biome Moods x 4 Sub-Palettes (60 simultaneous colors!) in 0xRRGGBB */")
    out.append("static const uint32_t SDL_CHRONO_PALS[ZPAL_COUNT][4][16] = {")
    for pid in range(len(zone_pals)):
        out.append(f"    {{ /* {ZPAL_NAMES[pid]} */")
        for sp in range(4):
            vals = ",".join(f"0x{c:06X}u" for c in zone_pals[pid][sp])
            comma = "," if sp < 3 else ""
            out.append(f"        {{{vals}}}{comma}")
        comma_z = "," if pid + 1 < len(zone_pals) else ""
        out.append(f"    }}{comma_z}")
    out.append("};")
    out.append("")
    # Packed 4bpp 16x16 tiles (128 bytes per tile x 138 = 17.6 KB)
    out.append("static const uint8_t SDL_CHRONO_QPAL[CHRONO_META_COUNT][4] = {")
    for mid, t in enumerate(frames_meta[0]):
        comma = "," if mid + 1 < len(META_NAMES) else ""
        out.append(f"    {{{t.qpal[0]}u,{t.qpal[1]}u,{t.qpal[2]}u,{t.qpal[3]}u}}{comma}")
    out.append("};")
    out.append("")
    out.append("static const uint8_t SDL_CHRONO_PX[CHRONO_META_COUNT][128] = {")
    for mid, t in enumerate(frames_meta[0]):
        b = []
        for y in range(16):
            for x in range(0, 16, 2):
                b.append((t.px[y][x] << 4) | (t.px[y][x + 1] & 15))
        comma = "," if mid + 1 < len(META_NAMES) else ""
        out.append("    {" + ",".join(f"0x{v:02X}u" for v in b) + "}" + comma)
    out.append("};")
    out.append("")
    # Animated 4-frame overrides for the 15 animated metatiles
    out.append(f"#define SDL_CHRONO_ANIM_META_COUNT {len(ANIM_META_NAMES)}u")
    out.append("static const uint8_t SDL_CHRONO_ANIM_IDS[SDL_CHRONO_ANIM_META_COUNT] = {")
    out.append("    " + ",".join(f"{META_IDX[n]}u" for n in ANIM_META_NAMES))
    out.append("};")
    out.append("static const uint8_t SDL_CHRONO_ANIM_PX[SDL_CHRONO_ANIM_META_COUNT][4][128] = {")
    for ai, mname in enumerate(ANIM_META_NAMES):
        mid = META_IDX[mname]
        out.append("    {")
        for f in range(4):
            t = frames_meta[f][mid]
            b = []
            for y in range(16):
                for x in range(0, 16, 2):
                    b.append((t.px[y][x] << 4) | (t.px[y][x + 1] & 15))
            comma_f = "," if f < 3 else ""
            out.append("        {" + ",".join(f"0x{v:02X}u" for v in b) + "}" + comma_f)
        comma_a = "," if ai + 1 < len(ANIM_META_NAMES) else ""
        out.append("    }" + comma_a)
    out.append("};")
    out.append("#endif /* HEARTH_INCLUDE_SDL2_TERRAIN */")
    out.append("")
    out.append("#endif /* HEARTH_TERRAIN_ROM_H */")
    out.append("")

    with open(OUT_H, "w") as fp:
        fp.write("\n".join(out))

def emit_web_ts(zone_pals, frames_meta):
    """Emit compact TypeScript Chrono/Mana 60-color biome palettes + base64 16x16 lossless tiles."""
    # Pack all 138 base tiles into one contiguous byte buffer (138 * 128 = 17,664 bytes)
    base_bytes = bytearray()
    qpals = []
    for t in frames_meta[0]:
        qpals.append(t.qpal)
        for y in range(16):
            for x in range(0, 16, 2):
                base_bytes.append((t.px[y][x] << 4) | (t.px[y][x + 1] & 15))

    anim_bytes = bytearray()
    anim_ids = [META_IDX[n] for n in ANIM_META_NAMES]
    for mid in anim_ids:
        for f in range(4):
            t = frames_meta[f][mid]
            for y in range(16):
                for x in range(0, 16, 2):
                    anim_bytes.append((t.px[y][x] << 4) | (t.px[y][x + 1] & 15))

    b64_base = base64.b64encode(bytes(base_bytes)).decode("ascii")
    b64_anim = base64.b64encode(bytes(anim_bytes)).decode("ascii")

    lines = [
        "/**",
        " * Auto-generated by engine/tools/bake_chrono_tileset.py — DO NOT EDIT",
        " * Unified Chrono Trigger / Trials of Mana 16-Bit Master Tileset & 60-Color Biome Palettes",
        " */",
        "",
        f"export const CHRONO_META_COUNT = {len(META_NAMES)};",
        "",
    ]
    # Export extended metatile constants
    for idx in range(63, len(META_NAMES)):
        lines.append(f"export const {META_NAMES[idx]} = {idx};")
    lines.append("")

    # Export 18 x 4 x 16 CSS hex palettes
    lines.append("export const CHRONO_ZONE_PALS: readonly (readonly (readonly string[])[])[] = [")
    for pid, zp in enumerate(zone_pals):
        lines.append(f"  [ // {ZPAL_NAMES[pid]}")
        for sp in range(4):
            hexs = ", ".join(f"'#{c:06X}'" if i > 0 else "'rgba(0,0,0,0)'" for i, c in enumerate(zp[sp]))
            lines.append(f"    [{hexs}],")
        lines.append("  ],")
    lines.append("];")
    lines.append("")

    qpal_str = ",".join(f"[{q[0]},{q[1]},{q[2]},{q[3]}]" for q in qpals)
    lines.append(f"export const CHRONO_META_QPAL: readonly (readonly [number, number, number, number])[] = [{qpal_str}];")
    lines.append(f"export const CHRONO_ANIM_META_IDS: readonly number[] = [{', '.join(str(i) for i in anim_ids)}];")
    lines.append(f"export const CHRONO_BASE_PX_B64 = '{b64_base}';")
    lines.append(f"export const CHRONO_ANIM_PX_B64 = '{b64_anim}';")
    lines.append("")

    # Export exact same TypeScript autotile/shadow/prop resolver matching C core!
    lines.extend([
        "function isSolidCaster(t: number): boolean {",
        "  return t === 14 || t === 15 || t === 16 || t === 19 || t === 20 || t === 29 || t === 45 || t === 43;",
        "}",
        "function isWaterLike(t: number): boolean {",
        "  return t === 6 || t === 7 || t === 46 || t === 54;",
        "}",
        "function isGrassLike(t: number): boolean {",
        "  return t === 0 || t === 1 || t === 2 || t === 3 || t === 32;",
        "}",
        "function inferPropGround(tn: number, ts: number, tw: number, te: number): number {",
        "  let cob = 0, pier = 0, aut = 0, dirt = 0;",
        "  if (tn === 5) cob++; if (ts === 5) cob++; if (tw === 5) cob++; if (te === 5) cob++;",
        "  if (tn === 62 || tn === 8) pier++; if (ts === 62 || ts === 8) pier++;",
        "  if (tw === 62 || tw === 8) pier++; if (te === 62 || te === 8) pier++;",
        "  if (tn === 32 || tn === 33) aut++; if (ts === 32 || ts === 33) aut++;",
        "  if (tw === 32 || tw === 33) aut++; if (te === 32 || te === 33) aut++;",
        "  if (tn === 4) dirt++; if (ts === 4) dirt++; if (tw === 4) dirt++; if (te === 4) dirt++;",
        "  if (pier >= 1) return 2;",
        "  if (cob >= 1) return 1;",
        "  if (aut >= 1) return 3;",
        "  if (dirt >= 2) return 4;",
        "  return 0;",
        "}",
        "",
        "export function resolveChronoMeta(",
        "  t: number, ov: number,",
        "  tn: number, ts: number, tw: number, te: number,",
        "  mx: number, my: number, isWorld: boolean",
        "): number {",
        "  const v = (((mx * 3 + my * 5) ^ (mx >> 1)) & 3);",
        "  if (t === 26 || t === 25 || t === 27 || t === 13 || t === 11 || t === 31) {",
        "    const gnd = inferPropGround(tn, ts, tw, te);",
        "    if (gnd === 1) {",
        "      if (t === 26) return CM_LAMP_COBBLE;",
        "      if (t === 25) return CM_SIGN_COBBLE;",
        "      if (t === 27) return CM_FLOWERBED_COBBLE;",
        "      if (t === 13) return CM_BUSH_COBBLE;",
        "      if (t === 11) return CM_FENCE_COBBLE;",
        "    } else if (gnd === 2) {",
        "      if (t === 26) return CM_LAMP_PIER;",
        "      if (t === 25) return CM_SIGN_PIER;",
        "      if (t === 31) return CM_BOULDER_PIER;",
        "    } else if (gnd === 3) {",
        "      if (t === 26) return CM_LAMP_AUTUMN;",
        "      if (t === 25) return CM_SIGN_AUTUMN;",
        "      if (t === 31) return CM_BOULDER_AUTUMN;",
        "    } else if (gnd === 4) {",
        "      if (t === 26) return CM_LAMP_DIRT;",
        "      if (t === 25) return CM_SIGN_DIRT;",
        "    }",
        "    return t;",
        "  }",
        "  if (t === 62 && isWaterLike(ts)) return CM_PIER_WATER_EDGE;",
        "  if (!isWorld && (t === 6 || t === 7)) {",
        "    const ln = !isWaterLike(tn) && tn !== 8 && tn !== 9 && tn !== 10 && tn !== 62;",
        "    const ls = !isWaterLike(ts) && ts !== 8 && ts !== 9 && ts !== 10 && ts !== 62;",
        "    const lw = !isWaterLike(tw) && tw !== 8 && tw !== 9 && tw !== 10 && tw !== 62;",
        "    const le = !isWaterLike(te) && te !== 8 && te !== 9 && te !== 10 && te !== 62;",
        "    if (ln && lw) return CM_SHORE_NW;",
        "    if (ln && le) return CM_SHORE_NE;",
        "    if (ls && lw) return CM_SHORE_SW;",
        "    if (ls && le) return CM_SHORE_SE;",
        "    if (ln) return CM_SHORE_N;",
        "    if (ls) return CM_SHORE_S;",
        "    if (lw) return CM_SHORE_W;",
        "    if (le) return CM_SHORE_E;",
        "    return t;",
        "  }",
        "  if (isWorld && t === 54) {",
        "    const ln = !isWaterLike(tn) && tn !== 59;",
        "    const ls = !isWaterLike(ts) && ts !== 59;",
        "    const lw = !isWaterLike(tw) && tw !== 59;",
        "    const le = !isWaterLike(te) && te !== 59;",
        "    if (ln && !ls) return CM_WORLD_COAST_N;",
        "    if (ls && !ln) return CM_WORLD_COAST_S;",
        "    if (lw && !le) return CM_WORLD_COAST_W;",
        "    if (le && !lw) return CM_WORLD_COAST_E;",
        "    return t;",
        "  }",
        "  if (!isWorld) {",
        "    if (t === 5) {",
        "      if (isSolidCaster(tn)) return CM_COBBLE_SHAD_N;",
        "      if (isSolidCaster(tw)) return CM_COBBLE_SHAD_W;",
        "      if (isGrassLike(tn)) return CM_COBBLE_EDGE_N;",
        "      if (isGrassLike(ts)) return CM_COBBLE_EDGE_S;",
        "      if (isGrassLike(tw)) return CM_COBBLE_EDGE_W;",
        "      if (isGrassLike(te)) return CM_COBBLE_EDGE_E;",
        "      if (v === 1) return CM_COBBLE_V1;",
        "      if (v === 2) return CM_COBBLE_V2;",
        "      if (v === 3) return CM_COBBLE_V3;",
        "      return 5;",
        "    }",
        "    if (t === 0 || t === 1) {",
        "      if (isSolidCaster(tn)) return CM_GRASS_SHAD_N;",
        "      if (isSolidCaster(tw)) return CM_GRASS_SHAD_W;",
        "      if (ov !== 0) return CM_GRASS_DAPPLE;",
        "      if (v === 1) return CM_GRASS_V1;",
        "      if (v === 2) return CM_GRASS_V2;",
        "      if (v === 3) return CM_GRASS_V3;",
        "      return 0;",
        "    }",
        "    if (t === 32) {",
        "      if (isSolidCaster(tn)) return CM_AUTUMN_SHAD_N;",
        "      if (ov !== 0) return CM_AUTUMN_DAPPLE;",
        "      if (v === 1) return CM_AUTUMN_V1;",
        "      if (v === 2) return CM_AUTUMN_V2;",
        "      return 32;",
        "    }",
        "    if (t === 4) {",
        "      if (isSolidCaster(tn)) return CM_DIRT_SHAD_N;",
        "      if (isGrassLike(tn)) return CM_DIRT_EDGE_N;",
        "      if (isGrassLike(ts)) return CM_DIRT_EDGE_S;",
        "      if (isGrassLike(tw)) return CM_DIRT_EDGE_W;",
        "      if (isGrassLike(te)) return CM_DIRT_EDGE_E;",
        "      return (v & 1) ? CM_DIRT_V1 : 4;",
        "    }",
        "    if (t === 33) {",
        "      if (isSolidCaster(tn)) return CM_ROCKY_SHAD_N;",
        "      return (v & 1) ? CM_ROCKY_V1 : 33;",
        "    }",
        "    if (t === 38) {",
        "      if (isSolidCaster(tn)) return CM_WOOD_SHAD_N;",
        "      return (v & 1) ? CM_WOOD_FLOOR_V1 : 38;",
        "    }",
        "    if (t === 22 && (v & 1)) return CM_SHRINE_V1;",
        "  } else {",
        "    if (t === 0 && v > 0) return CM_GRASS_V1 + (v - 1);",
        "    if (t === 55 && (v & 1)) return CM_SAND_V1;",
        "    if (t === 56 && (v & 1)) return CM_SNOW_V1;",
        "    if (t === 58 && (v & 1)) return CM_JUNGLE_V1;",
        "    if (t === 46 && (v & 1)) return CM_OCEAN_V1;",
        "  }",
        "  return t < 63 ? t : 0;",
        "}",
        "",
    ])

    with open(OUT_TS, "w") as fp:
        fp.write("\n".join(lines))

def main():
    zone_pals = build_zone_palettes()
    gen_pals  = build_genesis_palettes(zone_pals)
    frames_meta = [build_all_metatiles(f) for f in range(4)]

    snes_tiles, snes_anim, snes_meta = compile_console_bank(frames_meta, max_tiles=456, is_genesis=False)
    gen_tiles,  gen_anim,  gen_meta  = compile_console_bank(frames_meta, max_tiles=392, is_genesis=True)

    emit_snes_asm(snes_tiles, snes_anim)
    emit_c_header(zone_pals, gen_pals, frames_meta, snes_tiles, snes_anim, snes_meta, gen_tiles, gen_anim, gen_meta)
    emit_web_ts(zone_pals, frames_meta)

    print(f"[OK] Baked Chrono/Mana Unified 16-Bit Tileset:")
    print(f"     Metatiles: {len(META_NAMES)}  |  Biome Moods: {len(zone_pals)} x 4 sub-palettes (60 colors!)")
    print(f"     SNES:    {len(snes_tiles)} / 512 BG1 tiles ({len(snes_tiles)*32} B ROM) + {len(snes_anim)} anim tiles ({len(snes_anim)*32} B VBlank DMA)")
    print(f"     Genesis: {len(gen_tiles)} / 400 BG tiles ({len(gen_tiles)*32} B ROM) + {len(gen_anim)} anim tiles ({len(gen_anim)*32} B VBlank DMA)")
    print(f"     Emitted: {os.path.relpath(OUT_H, ROOT)}, {os.path.relpath(OUT_ASM, ROOT)}, {os.path.relpath(OUT_TS, ROOT)}")

if __name__ == "__main__":
    main()
