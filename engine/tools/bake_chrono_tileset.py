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
    0xDCE6EE, 0x232E44, 0x2E623C, 0x58964C, 0x688296, 0x44ACBE, 0x9AC6DC,
    0xE4F2F8, 0xFAFEFF,
]
BASE_ARCH = [
    0x000000, 0x241B22, 0x4B4E5C, 0x7C808E, 0xB2AEA4, 0xE2DDD0, 0x4E2B1E,
    0x8C5634, 0xC89058, 0xEED8AA, 0x6E2424, 0xB04432, 0xDE7444, 0xF6CA62,
    0xFFF4BC, 0x64A652,
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
        0xDCE2EA, 0x242A3A, 0x566E3A, 0x8E8B40, 0x627488, 0x489CB4, 0x9EC2D8,
        0xE4EEF6, 0xF8FCFF,
    ]
    alder_arch = [
        0x000000, 0x241A1E, 0x484A58, 0x787C8A, 0xAEABA4, 0xDFDBD0, 0x4A291C,
        0x844E2E, 0xBE844E, 0xE8CCA0, 0x6C2220, 0xB03C2A, 0xDC6C3C, 0xF8C456,
        0xFFF0AE, 0x888642,
    ]
    alder_acc = [
        0x000000, 0x28141A, 0x6E1E22, 0xB83226, 0xE2622C, 0xF6B040, 0x566E3A,
        0x8E8B40, 0xC6A84C, 0xD8E89A, 0x4A3A44, 0x887A86, 0xE8C460, 0xF48A30,
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
         0xFFF6C2, 0x7AB45E],
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

    # 10: ZPAL_EVERTALE_WORLD — Lush Chrono Storybook Continent (Emerald Hills, Vivid Sapphire Sea, Golden Sand, Snow Peaks)
    z[10] = [
        [0x000000, 0x142622, 0x225238, 0x3A8248, 0x62B256, 0x9AD86C, 0xD2EE8C,
         0x623A20, 0xA26C3A, 0xD49D54, 0xF2D384, 0xFBF2D6, 0xF6CE4C, 0xE26258,
         0x7AD0F2, 0x2A603E],
        [0x000000, 0x164E92, 0x2A82C8, 0x62C6EE, 0x0C2E60, 0x4A4C5E, 0x8A8A94,
         0xF4F2E4, 0x1E2E4A, 0x1B4A38, 0x3A7A4E, 0xEED082, 0x38BCCE, 0x96DCF0,
         0xE2F6FC, 0xFFFFFF],
        [0x000000, 0x221A20, 0x4B505E, 0x828896, 0xB8B6AE, 0xEEEBE0, 0x522E20,
         0x925C38, 0xCE985E, 0xF4DCAC, 0x762624, 0xC04834, 0xEA7846, 0xFAD064,
         0xFFF5BC, 0x62B056],
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
         0xFFF6C2, 0x70B45A],
        [0x000000, 0x26181E, 0x7E2428, 0xC84238, 0xEE7848, 0xFAD062, 0x24587C,
         0x3E8EC0, 0x7CCAE8, 0xE0F6FC, 0x544854, 0x9C8E98, 0xF2CE62, 0xF8983C,
         0xFFE680, 0xFFFADC],
    ]

    # 12: ZPAL_DESERT — Sands of Babana (Sculpted Golden Dunes, Sapphire Sea & Oasis Jade)
    z[12] = [
        [0x000000, 0x2A1A14, 0x366032, 0x568E44, 0x88BC5A, 0xC6D872, 0xEAE498,
         0x6E3C1E, 0xB47436, 0xE0A24E, 0xF8D47C, 0xFCF0C8, 0xFCE05C, 0xDE5838,
         0x5CC8C4, 0x467238],
        [0x000000, 0x165694, 0x2A8EC4, 0x6AD4EA, 0x0C3462, 0x764828, 0xC68846,
         0xFAF0CC, 0x3A2824, 0x285636, 0x4A884C, 0xF4CE78, 0x38BCC8, 0x9AE2E8,
         0xE6F8FA, 0xFFFFFF],
        [0x000000, 0x2A1A14, 0x6E4A34, 0xAA7E54, 0xDAB27C, 0xF6E0B4, 0x5A301A,
         0x9A5E30, 0xD29450, 0xF4CE8A, 0x7A2622, 0xC44832, 0xEC7C44, 0xFAD25E,
         0xFFF5B8, 0x84B658],
        [0x000000, 0x2A1614, 0x822424, 0xCC4432, 0xF27E44, 0xFCD668, 0x1A6264,
         0x32A09A, 0x70DAD0, 0xD8FAF4, 0x6A4A36, 0xB88E64, 0xF6CE58, 0xFA9A38,
         0xFFE67C, 0xFFFAD8],
    ]

    # 13: ZPAL_PARADISE — Suncrest Paradise (Turquoise Shallows, Coral Pink, Golden Sand & Palm Green)
    z[13] = [
        [0x000000, 0x14262A, 0x1E6040, 0x349654, 0x5CC666, 0x96E47E, 0xD2F6A2,
         0x5A3A24, 0x9E6E42, 0xD8A866, 0xF6DC9E, 0xFCF6E2, 0xFCD654, 0xF0688C,
         0x62DCF0, 0x28744A],
        [0x000000, 0x105E8E, 0x229CC2, 0x6AE0EE, 0x0A3860, 0x6A4E36, 0xBA8E5C,
         0xFAF4E4, 0x1E384E, 0x1C5A44, 0x389462, 0xF4D898, 0x38D0D8, 0x98EEF4,
         0xE4FAF8, 0xFFFFFF],
        [0x000000, 0x241E22, 0x545A66, 0x8C949E, 0xC6C4BC, 0xF2EEE4, 0x563222,
         0x9A623C, 0xD69C64, 0xF6DFB0, 0x862A44, 0xD25270, 0xF688A0, 0xFAD466,
         0xFFF6C2, 0x5AC264],
        [0x000000, 0x261624, 0x88244A, 0xD44874, 0xF67CA0, 0xFCD0DC, 0x147080,
         0x2EB8C4, 0x78EAE8, 0xDCFCFA, 0x56485A, 0x9E8EA2, 0xFAD45E, 0xFA9A46,
         0xFFE888, 0xFFFCE0],
    ]

    # 14: ZPAL_FROST — Frostmere North (Crisp Snow White, Ice Cyan & Glacial Peak Backdrops)
    z[14] = [
        [0x000000, 0x142234, 0x1E4A44, 0x327462, 0x66A492, 0xB8DCE6, 0xEFF8FE,
         0x3E3444, 0x6C5C6E, 0xA498AA, 0xDCE2EE, 0xFAFDFF, 0xF8D466, 0xD8444E,
         0x78D4FA, 0x26564E],
        [0x000000, 0x1A4E8E, 0x3684C4, 0x80CCF4, 0x102E5C, 0x4A5E7C, 0x8CA2BE,
         0xF4FAFE, 0x263854, 0xB8DCE6, 0xE6F4FC, 0xDCE8F4, 0x58BEE4, 0xB4E4F8,
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
        [0x000000, 0x104E68, 0x1E8494, 0x52C8C4, 0x082E42, 0x48382C, 0x847458,
         0xD8D4BC, 0x182C32, 0x143E2C, 0x2A6E44, 0xCCA06C, 0x32B0A2, 0x8CDED2,
         0xD4F6EE, 0xF6FFFC],
        [0x000000, 0x1C1818, 0x3E4646, 0x6C7872, 0x9EA89C, 0xD4DCD0, 0x422416,
         0x7A482A, 0xB47848, 0xE0B47E, 0x6A2622, 0xB04632, 0xDE7442, 0xF6C858,
         0xFFF2B6, 0x429A46],
        [0x000000, 0x1A141E, 0x7A1E3A, 0xC83C5C, 0xF27284, 0xFCC49A, 0x165E4C,
         0x2E9E7A, 0x6ADCA8, 0xD0FAE4, 0x42404E, 0x80828E, 0xF4CC52, 0xF89438,
         0xFFE278, 0xFFF8D8],
    ]

    # 16: ZPAL_VOLCANO — Cinderpeak Caldera (Volcanic Slate-Blue Sea, Basalt Charcoal & Crimson Magma in TPAL_ACCENT!)
    z[16] = [
        [0x000000, 0x181014, 0x302226, 0x4E383A, 0x745452, 0xA27A70, 0xD4AE9C,
         0x381E1C, 0x62342C, 0x985440, 0xCE8464, 0xEAD2BC, 0xFAAC3C, 0xE84428,
         0x9A7288, 0x3E282C],
        [0x000000, 0x163858, 0x265A80, 0x5292B4, 0x0C223A, 0x342A32, 0x5E505A,
         0x9A8C94, 0x20161E, 0x362228, 0x5E3E42, 0x7C626C, 0x367C92, 0x86BED0,
         0xD8ECF2, 0xF8FCFF],
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
        #  8: cobble lit         9: pale flagstone     10: biome grass/turf match (seamless shores & cobble edges!)
        # 11: accent dark       12: accent mid         13: accent bright
        # 14: warm gold (glow/emote)  15: bright sunlit cream-cyan (also crisp PAL3 UI highlight text!)
        p3 = [
            0x000000,
            0x161826, wat[1], wat[2], wat[12], wat[15],
            arch[2], arch[3], arch[4], arch[5], arch[15],
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
    # 63..74: Overhead BG2 canopies, eaves & shrine arch (exact match with O_CANOPY_TL..O_MAPLE_TOP)
    "CM_OV_CANOPY_TL", "CM_OV_CANOPY_TR", "CM_OV_CANOPY_BL", "CM_OV_CANOPY_BR",
    "CM_OV_ROOF_EAVE", "CM_OV_SHRINE_ARCH", "CM_OV_MAPLE_TL", "CM_OV_MAPLE_TR",
    "CM_OV_PINE_TOP", "CM_OV_OAK_CROWN", "CM_OV_MAPLE_MID", "CM_OV_MAPLE_TOP",
    # 75..90: Organic Spatial Ground Variations (eliminates wallpaper tiling!)
    "CM_GRASS_V1", "CM_GRASS_V2", "CM_GRASS_V3",
    "CM_AUTUMN_V1", "CM_AUTUMN_V2",
    "CM_COBBLE_V1", "CM_COBBLE_V2", "CM_COBBLE_V3",
    "CM_DIRT_V1", "CM_ROCKY_V1", "CM_WOOD_FLOOR_V1", "CM_SAND_V1",
    "CM_SNOW_V1", "CM_JUNGLE_V1", "CM_SHRINE_V1", "CM_OCEAN_V1",
    # 91..106: Rounded Shorelines & Feathered Organic Path/Plaza Transitions
    "CM_SHORE_N", "CM_SHORE_S", "CM_SHORE_W", "CM_SHORE_E",
    "CM_SHORE_NW", "CM_SHORE_NE", "CM_SHORE_SW", "CM_SHORE_SE",
    "CM_COBBLE_EDGE_N", "CM_COBBLE_EDGE_S", "CM_COBBLE_EDGE_W", "CM_COBBLE_EDGE_E",
    "CM_DIRT_EDGE_N", "CM_DIRT_EDGE_S", "CM_DIRT_EDGE_W", "CM_DIRT_EDGE_E",
    # 107..116: Cast Ground Shadows & Canopy Dapple
    "CM_GRASS_SHAD_N", "CM_GRASS_SHAD_W", "CM_COBBLE_SHAD_N", "CM_COBBLE_SHAD_W",
    "CM_DIRT_SHAD_N", "CM_AUTUMN_SHAD_N", "CM_ROCKY_SHAD_N", "CM_WOOD_SHAD_N",
    "CM_GRASS_DAPPLE", "CM_AUTUMN_DAPPLE",
    # 117..130: Seamless Ground-Composited Props (ZERO green boxes on stone/pier/dirt!)
    "CM_LAMP_COBBLE", "CM_SIGN_COBBLE", "CM_FLOWERBED_COBBLE", "CM_BUSH_COBBLE",
    "CM_FENCE_COBBLE", "CM_LAMP_PIER", "CM_SIGN_PIER", "CM_BOULDER_PIER",
    "CM_LAMP_AUTUMN", "CM_SIGN_AUTUMN", "CM_BOULDER_AUTUMN",
    "CM_LAMP_DIRT", "CM_SIGN_DIRT", "CM_PIER_WATER_EDGE",
    # 131..135: Evertale Coastlines & Empty Vista Sky (reclaimed 4 obsolete legacy battle backdrop slots!)
    "CM_WORLD_COAST_N", "CM_WORLD_COAST_S", "CM_WORLD_COAST_W", "CM_WORLD_COAST_E",
    "CM_EMPTY",
    # 136..147: Opaque-Backed Overhead Canopies & Eaves (SNES BG1 & Genesis Parallax BG_A — zero sky bleed!)
    "CM_OPQ_CANOPY_TL", "CM_OPQ_CANOPY_TR", "CM_OPQ_CANOPY_BL", "CM_OPQ_CANOPY_BR",
    "CM_OPQ_ROOF_EAVE", "CM_OPQ_SHRINE_ARCH", "CM_OPQ_MAPLE_TL", "CM_OPQ_MAPLE_TR",
    "CM_OPQ_PINE_TOP", "CM_OPQ_OAK_CROWN", "CM_OPQ_MAPLE_MID", "CM_OPQ_MAPLE_TOP",
    # 148..151: Local Water Inner Peninsula Diagonal Corners (eliminates 90-degree land spikes!)
    "CM_SHORE_INW", "CM_SHORE_INE", "CM_SHORE_ISW", "CM_SHORE_ISE",
    # 152..159: World Map Turquoise Shallow Outer Corners & Inner Peninsula Tips
    "CM_WORLD_COAST_NW", "CM_WORLD_COAST_NE", "CM_WORLD_COAST_SW", "CM_WORLD_COAST_SE",
    "CM_WORLD_COAST_INW", "CM_WORLD_COAST_INE", "CM_WORLD_COAST_ISW", "CM_WORLD_COAST_ISE",
    # 160..171: World Map Deep Ocean Direct Coastlines (all 12 curved shapes!)
    "CM_OCEAN_COAST_N", "CM_OCEAN_COAST_S", "CM_OCEAN_COAST_W", "CM_OCEAN_COAST_E",
    "CM_OCEAN_COAST_NW", "CM_OCEAN_COAST_NE", "CM_OCEAN_COAST_SW", "CM_OCEAN_COAST_SE",
    "CM_OCEAN_COAST_INW", "CM_OCEAN_COAST_INE", "CM_OCEAN_COAST_ISW", "CM_OCEAN_COAST_ISE",
    # 172..179: Rounded Cobblestone & Dirt Path Curved Corners
    "CM_COBBLE_CORNER_NW", "CM_COBBLE_CORNER_NE", "CM_COBBLE_CORNER_SW", "CM_COBBLE_CORNER_SE",
    "CM_DIRT_CORNER_NW", "CM_DIRT_CORNER_NE", "CM_DIRT_CORNER_SW", "CM_DIRT_CORNER_SE",
    # 180..187: Organic Sand/Beach-to-Grass Feathered Biome Transitions (Edges + Corners!)
    "CM_SAND_GRASS_N", "CM_SAND_GRASS_S", "CM_SAND_GRASS_W", "CM_SAND_GRASS_E",
    "CM_SAND_GRASS_NW", "CM_SAND_GRASS_NE", "CM_SAND_GRASS_SW", "CM_SAND_GRASS_SE",
    # 188..193: Cozy Cottage Interior Furnishings, Cutaway Wall Cap & 2-Tile Bed Footboard
    "CM_LAMP_WOOD", "CM_FLOWERBED_WOOD", "CM_COUNTER_WOOD", "CM_LECTERN_WOOD",
    "CM_INTERIOR_CAP", "CM_BED_FOOT",
    # 194..203: Directional N-S River Flow, Narrow Channels, N-S Bridge & Ground-Matched Props
    "CM_WATER_V", "CM_RAPIDS_V", "CM_SHORE_V_W", "CM_SHORE_V_E",
    "CM_SHORE_WE", "CM_SHORE_NS", "CM_BRIDGE_NS",
    "CM_BOULDER_COBBLE", "CM_BOULDER_SAND", "CM_PALM_GRASS",
    # 204: Unlit Stone Shrine Lantern (Whispering Woods dark lantern before Rowan lights it!)
    "CM_LAMP_UNLIT",
    # 205..216: World Map Deep Ocean <-> Shallow Water Curved Shoal Transitions (all 12 shapes!)
    "CM_SHOAL_N", "CM_SHOAL_S", "CM_SHOAL_W", "CM_SHOAL_E",
    "CM_SHOAL_NW", "CM_SHOAL_NE", "CM_SHOAL_SW", "CM_SHOAL_SE",
    "CM_SHOAL_INW", "CM_SHOAL_INE", "CM_SHOAL_ISW", "CM_SHOAL_ISE",
    # 217..218: Biome-Matched World Mountain Peaks (Frostmere Snow & Cinderpeak Basalt under unified world palette!)
    "CM_PEAK_SNOW", "CM_PEAK_VOLCANO",
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
    """Calm, painterly golden-olive autumn meadow with sparse delicate fallen maple leaves."""
    t = make_grass(variant & 3)
    # Variant 0 stays clean & restful; variants 1 & 2 scatter 1-2 delicate fallen leaves (no noisy dot grid!)
    if variant == 1:
        t.p(4, 5, 13); t.p(5, 5, 12); t.p(4, 6, 8)
        t.p(12, 11, 12); t.p(13, 11, 10)
    elif variant == 2:
        t.p(11, 3, 13); t.p(12, 3, 12)
        t.p(3, 12, 12); t.p(4, 12, 13); t.p(3, 13, 8)
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
    """Organic flat mountain granite flagstones with soft mortar (harmonizes with cobblestone edges)."""
    t = Tile16(TPAL_ARCH, 3)
    t.stamp([
        "3344444323444432",
        "3444444434445443",
        "3445444434444443",
        "3444444323444432",
        "2344433222333322",
        "2223222344444322",
        "4432234445444433",
        "4443344444444443",
        "4543344444444332",
        "3432234444332223",
        "2223322222234444",
        "3444443234444544",
        "4454444344444443",
        "4444444344444432",
        "3444433234443322",
        "2333222222322223",
    ])
    if variant == 1:
        t = t.flip_h()
        t.p(2, 5, 15); t.p(11, 10, 15)  # subtle alpine moss in stone crevice
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

def make_water(frame=0, deep=False, shallow=False, rapids=False, vertical=False, ocean_v1=False):
    """
    Rich, layered 16-bit Chrono Trigger / Trials of Mana animated water:
      - Deep Ocean: rolling arched swells cycling abyssal cobalt (4) -> sapphire (1) ->
        cerulean swell (2) -> sunlit wave crest (3) + delicate non-grid foam glints (14)
      - Shallows: luminous aquamarine (12) flowing wavelets (smooth horizontal ribbons, zero polka dots!)
      - Creek/Lake (horizontal): layered sapphire-to-aqua flowing ribbons moving West (-4px/frame)
      - River/Rapids (vertical): downstream North->South ribbons moving South (+4px/frame) with 8px X-period
    """
    t = Tile16(TPAL_WATER, 1 if deep else (12 if shallow else 2))
    shift = (frame * 4) & 15
    for y in range(16):
        for x in range(16):
            if vertical:
                # Downstream N->S flowing river/rapids (features advance +4px in +Y per frame!)
                # 8px horizontal symmetry ensures TL==TR & BL==BR (only 2 animated 8x8 tiles on SNES/Genesis!)
                py = (y - shift) & 15
                xh = x & 7
                rib = math.sin(py * (math.pi / 8.0) + math.sin(xh * (math.pi / 4.0)) * 0.75) + math.sin(xh * (math.pi / 4.0)) * 0.45
                if rapids:
                    if rib < -0.78:
                        t.p(x, y, 1)
                    elif rib > 0.88:
                        t.p(x, y, 15 if ((xh + py) & 1) else 14)
                    elif rib > 0.36:
                        t.p(x, y, 13 if ((xh + py) & 1) else 12)
                    else:
                        t.p(x, y, 2)
                    wy = (py + (xh // 4) * 6) & 15
                    if (xh == 2 and wy in (4, 5, 6)) or (xh == 6 and wy in (11, 12)):
                        t.p(x, y, 15 if (wy == 5 or wy == 11) else 14)
                else:
                    if rib < -0.80:
                        t.p(x, y, 1)
                    elif rib > 1.02:
                        t.p(x, y, 14 if ((xh + py) & 1) else 13)
                    elif rib > 0.34:
                        t.p(x, y, 12 if ((xh + py) & 1) else 3)
                    else:
                        t.p(x, y, 2)
                    wy = (py + (xh // 4) * 7) & 15
                    if (xh == 3 and wy == 5) or (xh == 7 and wy == 12):
                        t.p(x, y, 14)
            elif rapids:
                # Flowing horizontal cerulean mountain brook with gentle sunlit riffles (seamless with T_WATER!)
                px = (x + shift) & 15
                rib = math.sin(px * (math.pi / 8.0) + math.sin(y * (math.pi / 4.0)) * 0.7) + math.sin(y * (math.pi / 4.0)) * 0.5
                if rib < -0.78:
                    t.p(x, y, 1)
                elif rib > 0.92:
                    t.p(x, y, 15 if ((px + y) & 1) else 14)
                elif rib > 0.42:
                    t.p(x, y, 13 if ((px + y) & 1) else 12)
                else:
                    t.p(x, y, 2)
                wx = (px + (y // 4) * 5) & 15
                if (y in (3, 11) and wx in (4, 5, 6)) or (y in (7, 15) and wx in (11, 12)):
                    t.p(x, y, 15 if (wx == 5 or wx == 11) else 14)
            elif deep:
                # Sweeping seamless 16x16 ocean swells (curved wave arcs, 0.0-diff tile borders!)
                px = (x + shift) & 15
                swell = math.sin(px * (math.pi / 8.0) + math.sin(y * (math.pi / 4.0)) * 0.7)
                band  = math.sin((y + 0.5) * (math.pi / 4.0) + math.cos(px * (math.pi / 8.0)) * 0.85)
                val   = swell * 0.45 + band * 0.55
                if val < -0.58:
                    t.p(x, y, 4)          # deep abyssal cobalt wave trough
                elif val > 0.74:
                    t.p(x, y, 3)          # sunlit cerulean wave ridge
                elif val > 0.22:
                    t.p(x, y, 2)          # rolling mid-ocean swell
                else:
                    t.p(x, y, 1)          # rich sapphire body
                # Delicate arched sunlight glints along wave peaks (ocean_v1 varies interior glints only!)
                if ocean_v1:
                    if 3 <= x <= 12:
                        wx = (px + (y // 8) * 5 + 3) & 15
                        if (y == 3 and wx in (5, 6)) or (y == 11 and wx in (9, 10)):
                            t.p(x, y, 14 if (wx == (5 if y == 3 else 9)) else 3)
                        elif (y == 6 and wx == 7) or (y == 14 and wx == 11):
                            t.p(x, y, 3)
                else:
                    wx = (px + (y // 8) * 7) & 15
                    if (y == 2 and wx in (3, 4)) or (y == 10 and wx in (11, 12)):
                        t.p(x, y, 14 if (wx == (3 if y == 2 else 11)) else 3)
                    elif (y == 3 and wx == 2) or (y == 11 and wx == 10):
                        t.p(x, y, 2)
            elif shallow:
                # Translucent tropical turquoise lagoon with gentle horizontal sunlit wavelets (ZERO vertical stripes!)
                px = (x + shift) & 15
                band  = math.sin((y + 0.5) * (math.pi / 4.0) + math.sin(px * (math.pi / 8.0)) * 0.75)
                swell = math.sin(px * (math.pi / 8.0) + math.cos(y * (math.pi / 4.0)) * 0.65)
                val   = band * 0.72 + swell * 0.28
                if val > 0.78:
                    t.p(x, y, 13)         # crisp sunlit aqua wavelet crest
                elif val > 0.42 and ((px + y) & 1):
                    t.p(x, y, 13)         # soft dithered shimmer shoulder
                elif val < -0.74:
                    t.p(x, y, 3)          # gentle mid-cerulean ripple trough
                else:
                    t.p(x, y, 12)         # vivid translucent turquoise lagoon body
                wx = (px + (y // 8) * 7) & 15
                if (y == 3 and wx in (4, 5)) or (y == 11 and wx in (11, 12)):
                    t.p(x, y, 14 if (wx == (4 if y == 3 else 11)) else 13)
            else:
                # Vivid creek/lake water with flowing S-curved ribbons & sunlit dapples
                px = (x + shift) & 15
                rib = math.sin(px * (math.pi / 8.0) + math.sin(y * (math.pi / 4.0)) * 0.8) + math.sin(y * (math.pi / 4.0)) * 0.6
                if rib < -0.82:
                    t.p(x, y, 1)          # cool deep creek current
                elif rib > 1.05:
                    t.p(x, y, 14 if ((px + y) & 1) else 13)
                elif rib > 0.35:
                    t.p(x, y, 12 if ((px + y) & 1) else 3)
                else:
                    t.p(x, y, 2)
                wx = (px + (y // 8) * 6) & 15
                if (y == 4 and wx == 5) or (y == 12 and wx == 13):
                    t.p(x, y, 14)
    return t

def make_shore(edge, frame=0, vertical=False):
    """
    3D top-down RPG rounded grassy/earthen shorelines curving smoothly into shimmering water!
    NEVER flips the underlying water layer horizontally — every bank (W, E, NE, SE, INE, ISE)
    animates in the EXACT same unified flow direction as adjacent water tiles!
    Boundary-exact curves guarantee pixel-perfect joins across N/S/W/E/NW/NE/SW/SE/INW/INE/ISW/ISE!
    """
    t = make_water(frame, shallow=False, vertical=(vertical or edge == 'WE'))
    foam = 0.65 if (frame in (1, 2)) else 0.0
    flip_x = edge in ('E', 'NE', 'SE', 'INE', 'ISE')
    flip_y = edge in ('ISW', 'ISE')
    base_e = {
        'E': 'W', 'NE': 'NW', 'SE': 'SW',
        'INE': 'INW', 'ISW': 'INW', 'ISE': 'INW',
    }.get(edge, edge)

    def paint_shore_d(x, y, d, north_drop=False, south_edge=False):
        if d < -2.0:
            t.p(x, y, 10 if ((x * 3 + y * 5) & 3) else 9)
        elif d < -0.9:
            t.p(x, y, 10 if (south_edge and ((x + y) & 1)) else 9)
        elif d < 0.0:
            t.p(x, y, 5 if (north_drop and ((x + y) & 1)) else 5)
        elif north_drop and d < 0.9:
            t.p(x, y, 4)
        elif d < (1.9 if north_drop else 1.2) + foam:
            t.p(x, y, 15 if ((x + y + frame) & 1) else 14)
        elif d < (3.4 if north_drop else 2.8) + foam * 0.5:
            t.p(x, y, 12 if ((x + y) & 1) else 13)

    if base_e == 'N':
        curve = [4.5, 5.1, 5.6, 5.9, 5.7, 5.2, 4.7, 4.4, 4.5, 5.0, 5.6, 5.9, 5.6, 5.1, 4.7, 4.5]
        for x in range(16):
            h = curve[x]
            for y in range(16):
                paint_shore_d(x, y, y - h, north_drop=True)
    elif base_e == 'S':
        curve = [4.5, 5.0, 5.5, 5.7, 5.4, 4.9, 4.5, 4.3, 4.5, 5.0, 5.5, 5.6, 5.3, 4.9, 4.6, 4.5]
        for x in range(16):
            h = 15.0 - curve[x]
            for y in range(16):
                paint_shore_d(x, y, h - y, north_drop=False, south_edge=True)
    elif base_e == 'W':
        curve = [4.5, 5.1, 5.6, 5.9, 5.7, 5.2, 4.7, 4.4, 4.5, 5.0, 5.6, 5.9, 5.6, 5.1, 4.7, 4.5]
        for y in range(16):
            w = curve[y]
            for x in range(16):
                sx = (15 - x) if flip_x else x
                paint_shore_d(x, y, sx - w, north_drop=False)
    elif base_e == 'NW':
        # Boundary-exact: at sx=15, 10.5 - r = y - 4.5 (matches N!); at y=15, 10.5 - r = sx - 4.5 (matches W!)
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                edge_damp = math.sin(sx * math.pi / 15.0) * math.sin(y * math.pi / 15.0)
                r = math.hypot(15.0 - sx, 15.0 - y) + math.sin((sx - y) * 0.45) * 0.35 * edge_damp
                paint_shore_d(x, y, 10.5 - r, north_drop=(y < 9 and sx > y))
    elif base_e == 'SW':
        # Boundary-exact: at sx=15, 10.5 - r = 10.5 - y (matches S!); at y=0, 10.5 - r = sx - 4.5 (matches W!)
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                edge_damp = math.sin(sx * math.pi / 15.0) * math.sin(y * math.pi / 15.0)
                r = math.hypot(15.0 - sx, float(y)) + math.sin((sx + y) * 0.45) * 0.35 * edge_damp
                paint_shore_d(x, y, 10.5 - r, north_drop=False, south_edge=(y > 8))
    elif base_e == 'INW':
        # Boundary-exact: at sx=0, r - 4.5 = sy - 4.5 (matches N/S!); at sy=0, r - 4.5 = sx - 4.5 (matches W/E!)
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                sy = (15 - y) if flip_y else y
                r = math.hypot(float(sx), float(sy))
                paint_shore_d(x, y, r - 4.5, north_drop=(not flip_y and sy > sx))
    elif base_e == 'WE':
        # Narrow 1-tile vertical N-S stream with BOTH West & East mossy banks
        curve = [3.2, 3.6, 3.9, 4.0, 3.8, 3.5, 3.2, 3.1, 3.2, 3.6, 3.9, 4.0, 3.8, 3.5, 3.3, 3.2]
        for y in range(16):
            w = curve[y]
            for x in range(16):
                d = min(x - w, (15 - x) - w)
                if d < -1.2:
                    t.p(x, y, 10 if ((x * 3 + y * 5) & 3) else 9)
                elif d < -0.3:
                    t.p(x, y, 9)
                elif d < 0.5:
                    t.p(x, y, 5)
                elif d < 1.3 + foam * 0.4:
                    t.p(x, y, 15 if ((y + frame) & 1) else 14)
                elif d < 2.2:
                    t.p(x, y, 12 if ((x + y) & 1) else 13)
    elif base_e == 'NS':
        # Narrow 1-tile horizontal W-E canal with BOTH North & South mossy banks
        curve = [3.2, 3.6, 3.9, 4.0, 3.8, 3.5, 3.2, 3.1, 3.2, 3.6, 3.9, 4.0, 3.8, 3.5, 3.3, 3.2]
        for x in range(16):
            h = curve[x]
            for y in range(16):
                dn = y - h
                ds = (15 - y) - h
                d = min(dn, ds)
                if d < -1.2:
                    t.p(x, y, 10 if ((x * 3 + y * 5) & 3) else 9)
                elif d < -0.3:
                    t.p(x, y, 9 if dn <= ds else 10)
                elif d < 0.5:
                    t.p(x, y, 5)
                elif d < 1.3 + foam * 0.4:
                    t.p(x, y, 15 if ((x + frame) & 1) else 14)
                elif d < 2.2:
                    t.p(x, y, 12 if ((x + y) & 1) else 13)
    return t

def make_world_coast(edge, frame=0, deep_ocean=False):
    """
    Sweeping 16-bit Chrono Trigger / Trials of Mana rounded coastal beaches on the Evertale Globe!
    Boundary-exact periodic curves eliminate ALL tile-border seam jumps across N/S/W/E/NW/NE/SW/SE/INW..ISE!
    """
    t = make_water(frame, deep=deep_ocean, shallow=(not deep_ocean))
    foam = 0.6 if (frame in (1, 2)) else 0.0
    flip_x = edge in ('E', 'NE', 'SE', 'INE', 'ISE')
    flip_y = edge in ('ISW', 'ISE')
    base_e = {
        'E': 'W', 'NE': 'NW', 'SE': 'SW',
        'INE': 'INW', 'ISW': 'INW', 'ISE': 'INW',
    }.get(edge, edge)

    def paint_coast_pixel(x, y, d, is_north_drop=False):
        if d < -2.2:
            w = math.sin(x * (math.pi / 8.0) + y * (math.pi / 8.0))
            t.p(x, y, 7 if w > 0.68 else 11)
        elif d < -1.0:
            t.p(x, y, 11 if ((x + y) & 3) else 6)
        elif d < 0.0:
            t.p(x, y, 5 if (is_north_drop and ((x + y) & 1)) else 6)
        elif d < 1.15 + foam:
            t.p(x, y, 15 if ((x + y + frame) & 1) else 14)
        elif d < 2.8 + foam * 0.5:
            t.p(x, y, 13 if ((x + y) & 1) else 12)
        elif deep_ocean and d < 4.6:
            t.p(x, y, 12 if ((x + y) & 1) else 3)
        elif deep_ocean and d < 6.4:
            t.p(x, y, 2 if ((x + y) & 1) else 3)

    if base_e == 'N':
        curve = [4.5, 5.2, 5.8, 6.1, 5.8, 5.2, 4.7, 4.4, 4.6, 5.2, 5.8, 6.1, 5.7, 5.2, 4.7, 4.5]
        for x in range(16):
            for y in range(16):
                paint_coast_pixel(x, y, y - curve[x], is_north_drop=True)
    elif base_e == 'S':
        curve = [4.5, 5.0, 5.6, 5.8, 5.5, 4.9, 4.5, 4.3, 4.5, 5.1, 5.6, 5.8, 5.4, 4.9, 4.6, 4.5]
        for x in range(16):
            for y in range(16):
                paint_coast_pixel(x, y, (15.0 - curve[x]) - y, is_north_drop=False)
    elif base_e == 'W':
        curve = [4.5, 5.2, 5.8, 6.0, 5.7, 5.1, 4.6, 4.3, 4.6, 5.3, 5.8, 6.0, 5.6, 5.1, 4.7, 4.5]
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                paint_coast_pixel(x, y, sx - curve[y], is_north_drop=False)
    elif base_e == 'NW':
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                edge_damp = math.sin(sx * math.pi / 15.0) * math.sin(y * math.pi / 15.0)
                r = math.hypot(15.0 - sx, 15.0 - y) + math.sin((sx - y) * 0.42) * 0.4 * edge_damp
                paint_coast_pixel(x, y, 10.5 - r, is_north_drop=(y < 9 and sx > y))
    elif base_e == 'SW':
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                edge_damp = math.sin(sx * math.pi / 15.0) * math.sin(y * math.pi / 15.0)
                r = math.hypot(15.0 - sx, float(y)) + math.sin((sx + y) * 0.42) * 0.4 * edge_damp
                paint_coast_pixel(x, y, 10.5 - r, is_north_drop=False)
    elif base_e == 'INW':
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                sy = (15 - y) if flip_y else y
                edge_damp = math.sin(sx * math.pi / 15.0) * math.sin(sy * math.pi / 15.0)
                r = math.hypot(float(sx), float(sy)) + math.sin((sx - sy) * 0.5) * 0.25 * edge_damp
                paint_coast_pixel(x, y, r - 4.5, is_north_drop=(not flip_y and sy > sx))
    return t

def make_world_shoal(edge, frame=0):
    """
    Smooth curved bathymetry drop-off where Evertale Deep Ocean (T_WORLD_OCEAN)
    meets Turquoise Shallows (T_WORLD_SHALLOW / T_WORLD_CORAL).
    Uses boundary-exact periodic curves matching T_WORLD_SHALLOW at the outer rim
    and grading seamlessly through turquoise (12) -> cerulean (3) -> sapphire (2)
    into deep ocean swells across all 12 edge/corner orientations.
    """
    t = make_water(frame, deep=True)
    shal = make_water(frame, shallow=True)
    flip_x = edge in ('E', 'NE', 'SE', 'INE', 'ISE')
    flip_y = edge in ('S', 'SW', 'SE', 'ISW', 'ISE')
    base_e = {
        'S': 'N', 'E': 'W',
        'NE': 'NW', 'SW': 'NW', 'SE': 'NW',
        'INE': 'INW', 'ISW': 'INW', 'ISE': 'INW',
    }.get(edge, edge)

    def paint_shoal_pixel(x, y, d):
        if d < 0.0:
            t.p(x, y, shal.px[y][x])
        elif d < 1.7:
            # Translucent turquoise-to-cerulean drop-off rim with soft animated shimmer
            if d < 0.75:
                t.p(x, y, 12 if ((x + y) & 1) else shal.px[y][x])
            else:
                t.p(x, y, 3 if ((x + y + frame) & 1) else 12)
        elif d < 3.4:
            # Mid-depth sunlit cerulean shelf
            t.p(x, y, 2 if (d >= 2.4 and ((x + y) & 1)) else 3)
        elif d < 5.0:
            # Deepening sapphire slope fading smoothly into deep ocean body
            if (x + y) & 1:
                t.p(x, y, 2)

    if base_e == 'N':
        curve = [3.6, 4.2, 4.7, 4.9, 4.7, 4.2, 3.8, 3.5, 3.7, 4.2, 4.7, 4.9, 4.6, 4.2, 3.8, 3.6]
        for x in range(16):
            for y in range(16):
                sy = (15 - y) if flip_y else y
                paint_shoal_pixel(x, y, sy - curve[x])
    elif base_e == 'W':
        curve = [3.6, 4.2, 4.7, 4.9, 4.6, 4.1, 3.7, 3.5, 3.7, 4.3, 4.7, 4.9, 4.6, 4.1, 3.8, 3.6]
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                paint_shoal_pixel(x, y, sx - curve[y])
    elif base_e == 'NW':
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                sy = (15 - y) if flip_y else y
                edge_damp = math.sin(sx * math.pi / 15.0) * math.sin(sy * math.pi / 15.0)
                r = math.hypot(15.0 - sx, 15.0 - sy) + math.sin((sx - sy) * 0.42) * 0.35 * edge_damp
                paint_shoal_pixel(x, y, 11.4 - r)
    elif base_e == 'INW':
        for y in range(16):
            for x in range(16):
                sx = (15 - x) if flip_x else x
                sy = (15 - y) if flip_y else y
                edge_damp = math.sin(sx * math.pi / 15.0) * math.sin(sy * math.pi / 15.0)
                r = math.hypot(float(sx), float(sy)) + math.sin((sx - sy) * 0.5) * 0.25 * edge_damp
                paint_shoal_pixel(x, y, r - 3.6)
    return t

def make_bridge():
    """Weathered arched honey-cedar footbridge planks (E-W crossing) with beveled woodgrain & iron bolts."""
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

def make_bridge_ns():
    """Weathered honey-cedar footbridge planks (N-S crossing over horizontal canals) with horizontal planks & bolts."""
    t = Tile16(TPAL_ARCH, 7)
    for y in range(0, 16, 4):
        t.hline(0, y, 16, 6)
        t.hline(0, y + 1, 16, 8)
        t.hline(0, y + 2, 16, 8 if (y & 4) else 7)
        t.hline(0, y + 3, 16, 7)
        t.p(2, y + 1, 9); t.p(13, y + 2, 9)
        t.p(1, y + 2, 2); t.p(14, y + 2, 2)  # iron nail heads
    # Subtle vertical center wear band
    t.vline(7, 0, 16, 8); t.vline(8, 0, 16, 8)
    for y in range(0, 16, 4):
        t.p(7, y, 6); t.p(8, y, 6)
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
    """Calm, warm honey-cedar cottage floorboards with soft recessed seams & gentle woodgrain."""
    t = Tile16(TPAL_ARCH, 8)
    for idx, y0 in enumerate((0, 4, 8, 12)):
        t.hline(0, y0, 16, 7)       # soft warm plank seam (no harsh black zebra stripe!)
        t.hline(0, y0 + 1, 16, 8)
        t.hline(0, y0 + 2, 16, 8)
        t.hline(0, y0 + 3, 16, 8)
        seam = ((idx * 7) + (variant * 5) + 4) & 15
        t.vline(seam, y0, 4, 7)
        t.p((seam + 1) & 15, y0 + 1, 9)
        t.p((seam + 2) & 15, y0 + 1, 9)
        t.p((seam + 9) & 15, y0 + 2, 9 if ((idx + variant) & 1) else 7)
    return t

def make_hearth_rug():
    """Seamless woven Persian burgundy & antique-gold diamond tapestry (tiles cleanly at any size!)."""
    t = Tile16(TPAL_ACCENT, 3)
    # Rich crimson-burgundy weave with subtle tonal warp
    for y in range(16):
        for x in range(16):
            t.p(x, y, 3 if ((x + y) & 1) == 0 else 2)
    # Continuous interlocking antique-gold diamond lattice & sunlit medallion center
    t.stamp([
        ".......cc.......",
        "......c55c......",
        ".....c4334c.....",
        "....c434434c....",
        "...c434cc434c...",
        "..c434c55c434c..",
        ".c434c5ff5c434c.",
        "c5343c5ff5c3435c",
        "c5343c5ff5c3435c",
        ".c434c5ff5c434c.",
        "..c434c55c434c..",
        "...c434cc434c...",
        "....c434434c....",
        ".....c4334c.....",
        "......c55c......",
        ".......cc.......",
    ], 0, 0)
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
    # Smooth sunlit cream plaster with gentle warmth along timber edges (zero diagonal stripes!)
    t.rect(2, 2, 12, 10, 9)
    t.hline(2, 5, 12, 8); t.hline(2, 11, 12, 8)
    t.p(4, 3, 8); t.p(11, 9, 8)
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
    """Cozy smooth cream-plaster cottage wall with warm cedar wainscoting (zero diagonal stripes!)."""
    t = Tile16(TPAL_ARCH, 9)
    t.hline(0, 0, 16, 6); t.hline(0, 1, 16, 7); t.hline(0, 2, 16, 8)
    # Smooth warm plaster upper half
    t.rect(0, 3, 16, 6, 9)
    t.hline(0, 8, 16, 8)
    # Wainscoting chair rail & vertical cedar tongue-and-groove panels
    t.hline(0, 9, 16, 6); t.hline(0, 10, 16, 8)
    for x in range(0, 16, 4):
        t.vline(x, 10, 5, 6)
        t.vline(x + 1, 11, 4, 8)
        t.vline(x + 2, 11, 4, 7)
        t.vline(x + 3, 11, 4, 7)
    t.hline(0, 15, 16, 1)
    return t

def make_interior_cap():
    """Rich dark walnut cutaway roof-beam cap framing cottage interiors (Chrono Trigger 3D room depth!)."""
    t = Tile16(TPAL_ARCH, 1)
    t.rect(0, 0, 16, 16, 1)
    # Subtle dark cedar-rafter texture so cutaway framing feels cozy rather than harsh void
    for y in (3, 11):
        t.hline(0, y, 16, 6)
    for x in (4, 12):
        t.p(x, 3, 1); t.p(x, 11, 1)
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
    """Upper half of Rowan's cozy cedar-framed bed: headboard, plump pillow & upper stitched quilt."""
    t = Tile16(TPAL_ACCENT, 10)
    # Cedar headboard & corner posts
    t.rect(1, 0, 14, 4, 10); t.hline(2, 1, 12, 11); t.hline(2, 2, 12, 12)
    t.vline(1, 0, 16, 10); t.vline(14, 0, 16, 10)
    # Plump cream pillow with soft shading
    t.rect(3, 3, 10, 4, 9); t.rect(4, 4, 8, 2, 15)
    # Turned-down sheet fold
    t.hline(2, 7, 12, 9); t.hline(2, 8, 12, 15)
    # Stitched patchwork quilt continuing smoothly to bottom edge (joins CM_BED_FOOT!)
    t.rect(2, 9, 12, 7, 3)
    for qy in (9, 12, 15):
        for qx in (2, 5, 8, 11):
            col = 4 if (((qx + qy) // 3) & 1) else 7
            t.rect(qx, qy, 3, min(3, 16 - qy), col)
            if qy + 1 < 16:
                t.p(qx + 1, qy + 1, 12 if col == 4 else 8)
    return t

def make_bed_foot():
    """Lower half of Rowan's 2-tile bed: stitched patchwork quilt & carved cedar footboard."""
    t = Tile16(TPAL_ACCENT, 10)
    t.vline(1, 0, 16, 10); t.vline(14, 0, 16, 10)
    # Patchwork quilt flowing seamlessly from upper bed tile
    t.rect(2, 0, 12, 13, 3)
    for qy in (0, 3, 6, 9):
        for qx in (2, 5, 8, 11):
            col = 4 if (((qx + qy) // 3) & 1) else 7
            t.rect(qx, qy, 3, 3, col)
            t.p(qx + 1, qy + 1, 12 if col == 4 else 8)
    t.hline(2, 12, 12, 2)  # soft quilt fold shadow above footboard
    # Carved cedar footboard & bottom ground shadow
    t.rect(1, 13, 14, 2, 10); t.hline(2, 13, 12, 12)
    t.hline(1, 15, 14, 1)
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
    """Bold stratified Chrono Trigger alpine granite cliff face with vertical fissures & lit ledges."""
    t = Tile16(TPAL_WATER, 5)
    t.stamp([
        "6667766566776655",
        "6677666567766658",
        "5666665566665558",
        "5566555856655558",
        "8555558855555888",
        "5566655566665566",
        "6677665667766676",
        "6666665666666665",
        "5666558566655665",
        "5555588555558558",
        "8855885555888588",
        "5666655666665565",
        "6677666677666666",
        "5666665666665665",
        "5566558566558558",
        "8855888855888888",
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
        # Lush 16-bit Chrono/Mana tropical palm tree: slender curved trunk + sweeping arched emerald fronds!
        for y in range(6, 14):
            cx = 7 + int(math.sin((y - 6) * 0.35) * 1.2)
            t.p(cx - 1, y, 7)
            t.p(cx, y, 9 if (y & 1) else 8)
            t.p(cx + 1, y, 7 if (y & 1) else 8)
        # Coconuts tucked under the frond crown
        t.p(6, 6, 7); t.p(7, 6, 8); t.p(9, 6, 7)
        # Upper canopy crown & 6 radiating arched palm fronds (staying inside x=1..14, y=0..8)
        frond_rows = [
            (0, "....455554......"),
            (1, "..3456556543...."),
            (2, ".234554455432..."),
            (3, ".2343245324322.."),
            (4, ".232..34..2342.."),
            (5, ".2....23....32.."),
            (6, "......78.7...2.."),
        ]
        for ry, row_s in frond_rows:
            for rx, ch in enumerate(row_s):
                if ch != '.':
                    t.p(rx, ry, int(ch))
    elif pine:
        # Seamless lower half of 16x32 alpine spruce (joins O_PINE_TOP w=12 cleanly at y=0!)
        t.rect(6, 10, 4, 4, 7); t.vline(7, 10, 4, 8)
        for row, w in [(0, 12), (1, 14), (2, 14), (3, 12), (4, 14), (5, 16), (6, 16), (7, 14), (8, 12), (9, 10)]:
            x0 = 8 - w // 2
            for x in range(x0, x0 + w):
                if row in (3, 7, 9):
                    col = 1 if (x == x0 or x == x0 + w - 1) else 2
                elif x < 7:
                    col = 5 if (row in (0, 1, 4, 5)) else 4
                elif x < 10:
                    col = 4 if (row in (1, 5)) else 3
                else:
                    col = 2
                t.p(x, row, col)
        t.p(4, 1, 6); t.p(3, 5, 6)
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

def paint_lantern_unlit(t, pal_mode=TPAL_ARCH):
    """Unlit dark wrought-iron stone shrine lantern (cold slate glass, zero golden flame)."""
    if pal_mode == TPAL_ARCH:
        sh, iron_d, iron_l, glass_d, glass_m, cap = 1, 2, 3, 1, 2, 1
    else:
        sh, iron_d, iron_l, glass_d, glass_m, cap = 1, 7, 8, 1, 2, 1
    # Identical base shadow, pedestal & post to paint_lantern (deduplicates bottom 8x8 tiles!)
    t.hline(5, 14, 6, sh); t.hline(6, 15, 4, sh)
    t.rect(6, 12, 4, 3, iron_d); t.hline(6, 12, 3, 4 if pal_mode == TPAL_ARCH else 9)
    t.vline(7, 6, 7, iron_d); t.vline(8, 6, 7, 4 if pal_mode == TPAL_ARCH else 9)
    # Dark cold iron housing + unlit smoky-slate glass pane (no amber flame!)
    t.rect(5, 2, 6, 5, sh)
    t.rect(6, 3, 4, 3, glass_d)
    t.p(7, 3, glass_m); t.p(8, 4, glass_m)
    # Dark iron peaked cap
    t.hline(4, 2, 8, sh); t.hline(6, 1, 4, iron_l); t.p(7, 0, cap); t.p(8, 0, cap)

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

def paint_indoor_lamp(t):
    """Cozy indoor brass-and-cedar table/floor lamp with warm parchment shade on honey floorboards."""
    t.hline(5, 14, 6, 6); t.hline(6, 15, 4, 6)  # soft floor shadow
    # Turned cedar pedestal base & brass stem
    t.rect(5, 12, 6, 2, 6); t.hline(6, 12, 4, 7)
    t.vline(7, 7, 6, 6); t.vline(8, 7, 6, 13)
    # Warm glowing parchment lampshade with brass finial
    t.rect(4, 3, 8, 4, 13)
    t.rect(5, 2, 6, 1, 12)
    t.rect(5, 3, 6, 3, 14)
    t.hline(4, 6, 8, 12)
    t.hline(4, 2, 1, 6); t.hline(11, 2, 1, 6)
    t.p(7, 1, 13); t.p(8, 1, 13)

def paint_indoor_planter(t):
    """Cozy indoor carved cedar herb-and-blossom chest planter on honey floorboards."""
    t.hline(1, 14, 14, 6)
    t.rect(2, 7, 12, 7, 6)
    t.rect(3, 8, 10, 5, 7)
    t.hline(3, 8, 10, 9); t.hline(3, 12, 10, 6)
    # Potted indoor herbs & blossoms (zero grass box!)
    t.rect(3, 4, 10, 4, 15)
    t.disc(5.5, 4.5, 2.5, 2.0, 15); t.disc(10.5, 4.5, 2.5, 2.0, 15)
    for fx, fy, fc in [(4, 3, 12), (7, 2, 14), (10, 3, 13), (6, 5, 14), (11, 5, 12)]:
        t.p(fx, fy, fc); t.p(fx + 1, fy, fc)

def paint_indoor_counter(t):
    """Polished cedar bakery/depot shop counter on honey floorboards."""
    t.hline(0, 13, 16, 1)
    t.rect(0, 3, 16, 10, 6)
    t.hline(0, 4, 16, 9); t.hline(0, 5, 16, 8); t.hline(0, 6, 16, 6)
    t.rect(0, 7, 16, 5, 7)
    for px in (0, 8):
        t.vline(px, 7, 5, 6); t.vline(px + 1, 7, 5, 8)

def paint_indoor_lectern(t):
    """Carved wooden study lectern & open book stand on honey floorboards."""
    t.hline(4, 14, 8, 6)
    t.rect(6, 9, 4, 5, 6); t.vline(7, 9, 5, 7)
    t.rect(2, 3, 12, 6, 6)
    t.rect(3, 4, 10, 4, 9)  # open parchment pages
    t.vline(7, 4, 4, 6); t.vline(8, 4, 4, 8)
    t.hline(4, 5, 2, 2); t.hline(10, 5, 2, 2); t.hline(4, 7, 2, 2); t.hline(10, 7, 2, 2)

# ------------------------------------------------------------------------------
# LUSH PAINTERLY OVERHEAD TREE CANOPIES (BG2 — SEAMLESS CHRONO TRIGGER SILHOUETTES!)
# ------------------------------------------------------------------------------
def make_canopy_quad(quad, maple=False):
    """
    Seamless Chrono Trigger / Trials of Mana multi-tile tree crown side lobes:
      - Oak (maple=False): TL/TR/BL/BR fuse seamlessly onto solid O_OAK_CROWN (48x32 lush crown!)
      - Maple (maple=True): TL/TR fuse seamlessly onto O_MAPLE_MID + O_MAPLE_TOP (48x44 dome!)
    NO dark outline pixels along internal seam edges so canopies read as ONE unified volume!
    """
    pal = TPAL_ACCENT if maple else TPAL_FOLIAGE
    t = Tile16(pal, 0, prio=1)
    c0, c1, c2, c3, c4 = (1, 2, 3, 4, 5) if maple else (1, 2, 3, 4, 6)
    c3b = 4 if maple else 5

    if quad == 'TR':
        return make_canopy_quad('TL', maple).flip_h()
    if quad == 'BR':
        return make_canopy_quad('BL', maple).flip_h()

    if maple:
        # Full billowing left crimson-amber lobe of the Autumn Maple!
        # Top curves smoothly up to (15, 0) where it meets O_MAPLE_TOP; right edge x=14..15 is solid y=0..14!
        puffs = [(15, 7, 8.2), (10, 8, 6.8), (6, 9, 5.2), (14, 11, 5.4), (15, 3, 5.2)]
        for cx, cy, r in puffs:
            t.disc(cx, cy, r, r, c0)
        for cx, cy, r in puffs:
            t.disc(cx, cy, r - 1.0, r - 1.0, c1)
        # Solid right-seam fill so TL joins O_MAPLE_MID cleanly with ZERO seam line
        t.rect(12, 1, 4, 13, c2)
        for cx, cy, r in [(14, 6.5, 6.6), (9.5, 7.5, 5.4), (6.5, 8.5, 4.0), (14, 10.0, 4.2)]:
            t.disc(cx, cy, r, r, c2)
        for cx, cy, r in [(13.5, 5.0, 4.6), (9.5, 6.0, 3.8), (6.8, 7.2, 2.6)]:
            t.disc(cx, cy, r, r, c3b)
        for x, y in [(13, 2), (14, 2), (9, 4), (10, 4), (6, 6), (12, 7), (14, 5)]:
            t.p(x, y, c4)
        # Shaded crimson underside rim
        for x in range(4, 16):
            if t.px[14][x] != 0:
                t.p(x, 14, c0 if x < 12 else c1)
        return t

    if quad == 'TL':
        # Seamless upper-left Oak lobe: solid along right x=14..15 (y=1..15) and bottom y=14..15 (x=4..15)
        puffs = [(14, 12, 8.5), (14, 6, 6.2), (7, 12, 6.0), (9, 7, 5.2)]
        for cx, cy, r in puffs:
            t.disc(cx, cy, r, r, c0)
        for cx, cy, r in puffs:
            t.disc(cx, cy, r - 1.0, r - 1.0, c1)
        t.rect(11, 2, 5, 14, c2)
        t.rect(6, 11, 10, 5, c2)
        for cx, cy, r in [(13.5, 11, 6.8), (13.5, 5.5, 4.8), (7.5, 11, 4.6), (9.0, 6.8, 4.0)]:
            t.disc(cx, cy, r, r, c2)
        for cx, cy, r in [(12.5, 9.5, 4.8), (13.0, 4.5, 3.4), (8.2, 6.0, 2.8), (7.2, 10.0, 2.6)]:
            t.disc(cx, cy, r, r, c3b)
        for x, y in [(12, 3), (13, 3), (8, 5), (9, 5), (12, 8), (13, 8), (7, 9), (14, 6)]:
            t.p(x, y, c4)
    else:  # 'BL' — lower-left Oak bough skirt: solid along top y=0..2 (x=4..15) and right x=14..15 (y=0..12)
        puffs = [(14, 4, 8.5), (7, 4, 6.0), (13, 9, 5.4), (6, 8, 4.2)]
        for cx, cy, r in puffs:
            t.disc(cx, cy, r, r, c0)
        for cx, cy, r in puffs:
            t.disc(cx, cy, r - 1.0, r - 1.0, c1)
        t.rect(11, 0, 5, 12, c2)
        t.rect(6, 0, 10, 5, c2)
        for cx, cy, r in [(13.5, 3.5, 6.4), (7.5, 4.0, 4.6), (12.5, 7.5, 4.0)]:
            t.disc(cx, cy, r, r, c2)
        for cx, cy, r in [(12.5, 2.5, 4.2), (7.5, 3.2, 2.6)]:
            t.disc(cx, cy, r, r, c3b)
        for x, y in [(12, 1), (13, 2), (7, 2)]:
            t.p(x, y, c4)
    return t

def make_pine_top():
    """Tapered upper spire of 16x32 alpine spruce (widens smoothly from tip w=2 to w=12 at y=15)."""
    t = Tile16(TPAL_FOLIAGE, 0, prio=1)
    for row, w in [(1, 2), (2, 2), (3, 4), (4, 4), (5, 6), (6, 6), (7, 8), (8, 6), (9, 8), (10, 10), (11, 10), (12, 8), (13, 10), (14, 12), (15, 12)]:
        x0 = 8 - w // 2
        for x in range(x0, x0 + w):
            if row in (7, 11, 15):
                c = 1 if (x == x0 or x == x0 + w - 1) else 2
            elif x < 7:
                c = 5 if (row in (1, 3, 5, 9, 13)) else 4
            elif x < 9:
                c = 4 if (row in (2, 6, 10, 14)) else 3
            else:
                c = 2
            t.p(x, row, c)
    t.p(7, 1, 6); t.p(6, 5, 6); t.p(5, 10, 6); t.p(4, 14, 6)
    return t

def make_oak_crown():
    """100% solid billowy sunlit emerald oak canopy center (joins TL/TR/BL/BR with ZERO seams!)."""
    t = Tile16(TPAL_FOLIAGE, 2, prio=1)
    for cx, cy, r in [(4, 5, 5.5), (12, 5, 5.5), (8, 10, 6.2), (3, 13, 4.8), (13, 13, 4.8)]:
        t.disc(cx, cy, r, r * 0.9, 3)
    for cx, cy, r in [(4, 4, 3.8), (11, 4, 3.8), (8, 8.5, 4.4), (3, 11.5, 3.0), (12, 11.5, 3.0)]:
        t.disc(cx, cy, r, r * 0.85, 4)
    for cx, cy, r in [(4, 3, 2.2), (11, 3, 2.2), (7.5, 7.5, 2.6)]:
        t.disc(cx, cy, r, r * 0.8, 5)
    for x, y in [(3, 2), (4, 2), (10, 2), (11, 2), (7, 6), (8, 6), (3, 10), (12, 10)]:
        t.p(x, y, 6)
    return t

def make_maple_mid():
    """Solid billowing crimson-orange Autumn Maple heart connecting TOP, TL, TR & TRUNK seamlessly!"""
    t = Tile16(TPAL_ACCENT, 2, prio=1)
    # Full sunlit crimson-gold center puffs across y=0..14
    t.rect(0, 0, 16, 14, 3)
    for cx, cy, r in [(4, 4, 5.2), (12, 4, 5.2), (8, 8, 6.0), (3, 11, 4.4), (13, 11, 4.4)]:
        t.disc(cx, cy, r, r * 0.88, 3)
    for cx, cy, r in [(4, 3.5, 3.4), (11.5, 3.5, 3.4), (8, 6.8, 4.2), (3.5, 9.5, 2.6), (12, 9.5, 2.6)]:
        t.disc(cx, cy, r, r * 0.8, 4)
    for x, y in [(3, 2), (4, 2), (11, 2), (12, 2), (7, 5), (8, 5), (9, 5), (4, 8), (11, 8)]:
        t.p(x, y, 5)
    # Shaded deep-crimson under-canopy rim resting directly over T_MAPLE_TRUNK
    t.hline(0, 13, 16, 2)
    t.hline(0, 14, 16, 1)
    t.hline(0, 15, 16, 0)
    for x in (2, 3, 7, 8, 12, 13):
        t.p(x, 14, 2); t.p(x, 15, 1)
    return t

def make_maple_top():
    """Rounded sunlit golden-crimson Autumn Maple crown peak curving seamlessly into O_MAPLE_MID!"""
    t = Tile16(TPAL_ACCENT, 0, prio=1)
    puffs = [(7.5, 13.5, 8.2), (4.5, 12.5, 5.4), (10.5, 12.5, 5.4), (7.5, 9.0, 5.8)]
    for cx, cy, r in puffs:
        t.disc(cx, cy, r, r * 0.86, 1)
    for cx, cy, r in puffs:
        t.disc(cx, cy, r - 1.0, (r - 1.0) * 0.86, 2)
    # Solid bottom row y=14..15 across x=0..15 so it joins O_MAPLE_MID with ZERO seam!
    t.rect(1, 13, 14, 3, 3)
    t.p(0, 14, 2); t.p(0, 15, 3); t.p(15, 14, 2); t.p(15, 15, 3)
    for cx, cy, r in [(7.5, 12.5, 6.8), (4.5, 11.8, 4.2), (10.5, 11.8, 4.2), (7.5, 8.5, 4.6)]:
        t.disc(cx, cy, r, r * 0.85, 3)
    for cx, cy, r in [(7.5, 7.6, 3.4), (5.0, 10.6, 2.8), (10.2, 10.6, 2.8), (7.5, 11.5, 3.6)]:
        t.disc(cx, cy, r, r * 0.8, 4)
    for x, y in [(6, 5), (7, 5), (8, 5), (4, 9), (10, 9), (7, 10), (8, 10)]:
        t.p(x, y, 5)
    return t

# ------------------------------------------------------------------------------
# OVERHAULED EVERTALE OPEN WORLD GLOBE TILES (CHRONO TRIGGER / TRIALS OF MANA!)
# ------------------------------------------------------------------------------
def make_world_beach():
    """
    Soft, warm sunlit golden coastal sand with organic undulating wind-ripples & tiny shells.
    Shares exact edge continuity with make_world_sand() so beach & desert blend invisibly!
    """
    t = Tile16(TPAL_FOLIAGE, 10)
    for y in range(16):
        for x in range(16):
            # Seamless 16x16 organic rolling sand swells (ZERO pinstripes!)
            s = math.sin(x * (math.pi / 8.0) + math.sin(y * (math.pi / 8.0)) * 1.1)
            c = math.cos(y * (math.pi / 8.0) - math.sin(x * (math.pi / 8.0)) * 0.7)
            val = s * 0.52 + c * 0.48
            if val > 0.66:
                t.p(x, y, 11)                 # sunlit cream-gold dune highlight
            elif val < -0.68:
                t.p(x, y, 9)                  # gentle warm ochre trough
            elif val < -0.35 and ((x + y) & 1):
                t.p(x, y, 9)                  # soft Bayer dither transition
            else:
                t.p(x, y, 10)
    # Tiny sun-bleached sea-shell & smooth beach pebble with soft 1px drop shadow
    t.p(4, 6, 8); t.p(4, 5, 11); t.p(5, 5, 11)
    t.p(11, 12, 8); t.p(11, 11, 9); t.p(12, 11, 11)
    return t

def make_world_peak(biome="grass"):
    """Snow-capped storybook mountain range with biome-matched foothills (grass, snow, or volcanic basalt)."""
    if biome == "snow":
        t = make_world_snow(0)
    elif biome == "volcano":
        t = Tile16(TPAL_WATER, 5)
        for y in range(16):
            for x in range(16):
                v = (x * 3 + y * 5) & 7
                t.p(x, y, 6 if v in (1, 5) else (8 if v == 0 else 5))
    else:
        t = Tile16(TPAL_WATER, 9)
        for y in range(16):
            for x in range(16):
                t.p(x, y, 10 if ((x * 3 + y * 5) & 3) else 9)
    peaks = [(5, 4, 8), (11, 2, 10)]
    for px, py, h in peaks:
        for dy in range(h + 4):
            y = py + dy
            if y >= 15: break
            hw = dy + 1
            for dx in range(-hw, hw + 1):
                x = px + dx
                if not (0 <= x < 16): continue
                if biome == "volcano":
                    # Dramatic jagged dark volcanic basalt peaks with warm ash crests (zero snowcap!)
                    if dy <= 1:
                        t.p(x, y, 7 if dx <= 0 else 6)
                    elif dy == 2 and ((x + dy) & 1):
                        t.p(x, y, 6)
                    elif dx < 0:
                        t.p(x, y, 6 if (dx >= -2 and dy < 7) else 5)
                    elif dx == 0:
                        t.p(x, y, 6 if (dy < 6) else 5)
                    else:
                        t.p(x, y, 8 if (dx > 1) else 5)
                elif biome == "snow":
                    # Crisp glacial mountain peaks rising directly out of Frostmere snowdrifts
                    if dy <= 3:
                        t.p(x, y, 15 if dx <= 0 else 13)
                    elif dy == 4 and ((x + dy) & 1):
                        t.p(x, y, 13)
                    elif dx < 0:
                        t.p(x, y, 14 if (dx == -1 and dy < 7) else (6 if (dx > -hw) else 5))
                    elif dx == 0:
                        t.p(x, y, 13 if (dy < 6) else 6)
                    else:
                        t.p(x, y, 5 if (dx < hw) else 8)
                else:
                    if dy <= 2:
                        t.p(x, y, 15 if dx <= 0 else 13)  # bright sunlit snowcap & cool ice shadow
                    elif dy == 3 and ((x + dy) & 1):
                        t.p(x, y, 7)
                    elif dx < 0:
                        t.p(x, y, 7 if (dx == -1 and dy < 6) else (6 if (dx > -hw) else 5))
                    elif dx == 0:
                        t.p(x, y, 7 if (dy < 6) else 6)
                    else:
                        t.p(x, y, 5 if (dx < hw) else 8)
    # Soft cast shadow base along foothills for grounded 3D perspective
    t.hline(1, 15, 14, 13 if biome == "snow" else 8)
    return t

def make_world_forest():
    """Dense lush Chrono Trigger overworld woodland cluster (3D spherical crowns + cast shadow!)."""
    t = make_grass(0)
    crowns = [(4, 5, 4.3), (11, 5, 4.3), (8, 9, 4.6), (3, 12, 3.9), (12, 12, 3.9)]
    for cx, cy, r in crowns:
        t.disc(cx + 0.8, cy + 1.4, r, r * 0.82, 1)       # directional SE cast shadow
        t.disc(cx, cy, r - 0.3, r * 0.84, 2)             # deep forest canopy body
        t.disc(cx - 0.4, cy - 0.7, r - 1.2, r * 0.68, 3) # rich emerald dome
        t.disc(cx - 0.8, cy - 1.3, r - 2.0, r * 0.48, 5) # sunlit top-left leaf crown
        t.p(int(cx - 1), int(cy - 2), 6)                 # specular leaf glint
    return t

def make_world_hill():
    """Rolling sunlit highland clover knoll with curved ridge highlight & SE ground shadow."""
    t = make_grass(0)
    t.disc(8.4, 12.2, 6.8, 3.8, 2)     # soft SE shadow under hill slope
    t.disc(7.5, 11.0, 6.8, 4.2, 3)
    t.disc(7.2, 10.0, 6.0, 3.5, 4)
    t.disc(6.4, 8.7, 4.5, 2.3, 5)
    t.hline(4, 7, 5, 6)
    t.p(10, 10, 3); t.p(11, 11, 2)
    return t

def make_world_castle():
    """Gleaming white-stone Castle Aurelia miniature keep with twin spires & banners."""
    t = make_grass(0)
    t.qpal = [TPAL_ARCH, TPAL_ARCH, TPAL_ARCH, TPAL_ARCH]
    t.rect(0, 0, 16, 16, 15)
    t.hline(1, 14, 14, 1)
    t.rect(2, 6, 12, 8, 3); t.rect(3, 7, 10, 7, 4); t.vline(3, 7, 7, 5)
    t.rect(2, 4, 3, 10, 4); t.vline(2, 4, 10, 5); t.rect(11, 4, 3, 10, 4)
    t.rect(2, 2, 3, 2, 11); t.p(3, 1, 12); t.p(4, 1, 13)
    t.rect(11, 2, 3, 2, 11); t.p(12, 1, 12); t.p(13, 1, 13)
    t.rect(6, 3, 4, 4, 5); t.rect(6, 1, 4, 2, 11); t.p(7, 0, 13); t.p(8, 0, 13)
    t.rect(6, 10, 4, 4, 1); t.hline(6, 10, 4, 6)
    return t

def make_world_town():
    """Charming terracotta-roofed storybook village cluster on the Evertale Globe."""
    t = Tile16(TPAL_ARCH, 15)
    t.hline(1, 14, 14, 1)
    t.rect(1, 7, 6, 7, 9); t.rect(1, 4, 6, 3, 11); t.hline(2, 4, 4, 12); t.rect(3, 10, 2, 4, 6)
    t.p(2, 8, 13); t.p(5, 8, 13)
    t.rect(8, 5, 7, 9, 9); t.rect(8, 2, 7, 3, 11); t.hline(9, 2, 5, 12); t.rect(10, 10, 2, 4, 6)
    t.p(9, 7, 14); t.p(13, 7, 14)
    return t

def make_world_ruins():
    """Moss-draped broken stone watchtower & ancient columns."""
    t = Tile16(TPAL_WATER, 10)
    t.hline(2, 14, 12, 8)
    t.rect(3, 4, 4, 10, 6); t.vline(3, 4, 10, 7); t.p(4, 3, 5); t.p(6, 5, 9)
    t.rect(9, 6, 4, 8, 5); t.vline(9, 6, 8, 6); t.p(10, 5, 7); t.p(11, 7, 10)
    t.hline(5, 11, 6, 5); t.p(7, 10, 12)
    return t

def make_world_sand(variant=0):
    """
    Sculpted 3D Chrono Trigger / Trials of Mana golden desert dunes!
    Broad organic S-curved wind-carved dune crests with sunlit cream-gold ridge (11),
    warm gold slope (10), amber leeward shadow (9), and recessed umber crease (8).
    Completely seamless across variants (ZERO harsh horizontal/diagonal pinstripes!).
    """
    t = Tile16(TPAL_FOLIAGE, 10)
    phase = 0.0 if variant == 0 else math.pi * 0.5
    for y in range(16):
        for x in range(16):
            # Seamless 16x16 rolling dune contour + gentle leeward slope shading
            ridge = math.sin(y * (math.pi / 8.0) + math.sin(x * (math.pi / 8.0) + phase) * 0.85)
            slope = math.cos(y * (math.pi / 8.0) + math.sin(x * (math.pi / 8.0) + phase) * 0.85)
            micro = math.sin((x * 2 + y) * (math.pi / 8.0)) * 0.16
            v = ridge + micro
            if v > 0.74:
                t.p(x, y, 11)                 # sunlit golden-cream dune crest
            elif v > 0.42 and slope > 0.0 and ((x + y) & 1):
                t.p(x, y, 11)                 # soft dithered sunlit shoulder
            elif v < -0.78:
                t.p(x, y, 8 if ((x + y) & 1) else 9)  # deep sculpted leeward dune trough!
            elif v < -0.32:
                t.p(x, y, 9)                  # warm ochre shadow slope
            elif v < -0.10 and ((x + y) & 1):
                t.p(x, y, 9)                  # organic feathered shadow blend
            else:
                t.p(x, y, 10)
    # Subtle desert micro-detail for perspective & scale (tiny sandstone pebbles / dry grass tuft)
    if variant == 0:
        t.p(3, 12, 8); t.p(4, 12, 9); t.p(3, 11, 11)
        t.p(12, 5, 8); t.p(12, 4, 11)
    else:
        t.p(10, 11, 8); t.p(11, 11, 9); t.p(10, 10, 11)
        t.p(5, 4, 8); t.p(4, 3, 9); t.p(5, 3, 11)
    return t

def make_world_snow(variant=0):
    """Sculpted winter snowdrifts with soft glacial-blue shadow troughs & sunlit sparkle."""
    t = Tile16(TPAL_WATER, 14)
    phase = 0.0 if variant == 0 else math.pi * 0.5
    for y in range(16):
        for x in range(16):
            drift = math.sin(y * (math.pi / 8.0) + math.cos(x * (math.pi / 8.0) + phase) * 0.9)
            if drift > 0.58:
                t.p(x, y, 15)                 # bright sunlit snow crest
            elif drift < -0.68:
                t.p(x, y, 13 if ((x + y) & 1) else 3)  # cool ice-blue drift shadow!
            elif drift < -0.25:
                t.p(x, y, 13)
            else:
                t.p(x, y, 14)
    if variant == 1:
        t.p(6, 5, 15); t.p(12, 11, 13)
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
    # Dense rainforest canopy with 3D spherical emerald domes, cast shadows & tropical orchids
    for cx, cy in [(4, 4), (11, 3), (8, 9), (3, 13), (12, 12)]:
        ox = (cx + variant) & 15
        t.disc(ox + 0.7, cy + 1.0, 4.1, 3.2, 1)
        t.disc(ox, cy, 4.0, 3.2, 3)
        t.disc(ox - 0.5, cy - 0.8, 2.6, 2.0, 4)
        t.disc(ox - 0.8, cy - 1.3, 1.4, 1.1, 5)
        t.p(ox, max(0, cy - 2), 6)
    t.p(6, 6, 13); t.p(12, 9, 12)
    return t

def make_world_coral(frame=0):
    """
    Seamless translucent tropical coral reef embedded directly in TPAL_WATER shallow waves!
    Outer 2px margin is untouched make_water(frame, shallow=True) for 0.0-diff border blending
    in every biome (Suncrest Paradise, Babana Shallows, Evertale Globe, SNES & Genesis).
    """
    t = make_water(frame, shallow=True)
    # 1. Soft submerged underwater shadow halo beneath the reef heads (deep cerulean/sapphire)
    for cx, cy, rx, ry in [(5.6, 6.8, 3.0, 2.3), (10.6, 10.2, 2.9, 2.3), (6.5, 11.6, 2.4, 1.9)]:
        t.disc(cx, cy, rx, ry, 2)
    for cx, cy, rx, ry in [(5.8, 7.2, 1.8, 1.4), (10.8, 10.6, 1.7, 1.3)]:
        t.disc(cx, cy, rx, ry, 1)
    # 2. Swaying emerald sea-fan / kelp fronds tucked behind the reef outcrops
    sway = 1 if (frame in (1, 2)) else 0
    for kx, ky in [(3, 4), (4, 3), (12, 7), (11, 6), (4, 10)]:
        t.p(kx, ky, 9)
        t.p(min(13, kx + sway), max(2, ky - 1), 10)
    # 3. Sculpted 3D warm terracotta/golden brain-coral & staghorn mounds (strictly interior x,y in 2..13)
    for cx, cy, rx, ry in [(5.0, 5.8, 2.5, 2.0), (10.0, 9.2, 2.4, 2.0), (6.0, 10.8, 2.0, 1.6)]:
        t.disc(cx, cy, rx, ry, 5)                   # warm umber/terracotta reef base
        t.disc(cx - 0.3, cy - 0.3, rx * 0.72, ry * 0.72, 6)  # lit terracotta-ochre dome
        t.disc(cx - 0.6, cy - 0.6, rx * 0.42, ry * 0.42, 11) # sunlit golden coral crest
        t.p(int(round(cx - 1.0)), int(round(cy - 1.0)), 7)   # ivory/pearl coral tip highlight
    # 4. Gentle lapping foam & sunlit caustic sparkles along upper-left reef rims
    glint = 15 if (frame & 1) else 14
    for fx, fy in [(3, 5), (6, 4), (8, 8), (11, 8), (5, 12)]:
        t.p(fx, fy, glint)
    return t

def make_world_tech():
    t = Tile16(TPAL_ACCENT, 10)
    t.rect(1, 1, 14, 14, 11)
    t.hline(1, 1, 14, 12); t.vline(1, 1, 14, 12)
    t.hline(2, 7, 12, 7); t.hline(2, 8, 12, 8)
    t.vline(7, 2, 12, 7); t.vline(8, 2, 12, 8)
    t.rect(6, 6, 4, 4, 8); t.rect(7, 7, 2, 2, 9)
    return t

def make_mtn_sky():
    t = Tile16(TPAL_WATER, 2)
    for y in range(16):
        c = 1 if y < 5 else (2 if y < 11 else 3)
        t.hline(0, y, 16, c)
    return t

# ------------------------------------------------------------------------------
# FEATHERED ORGANIC GRASS-TO-COBBLE, GRASS-TO-DIRT & SAND-TO-GRASS TRANSITIONS
# ------------------------------------------------------------------------------
def make_feathered_edge(base_fn, edge, moss_on_arch=False):
    if edge == 'E': return make_feathered_edge(base_fn, 'W', moss_on_arch).flip_h()
    if edge == 'S': return make_feathered_edge(base_fn, 'N', moss_on_arch).flip_v()
    t = base_fn(0)
    if moss_on_arch:
        # Slim, organic 2-3px meadow turf fringe harmonizing smoothly into cobblestone!
        tuft = [2, 3, 2, 2, 3, 3, 2, 2, 3, 2, 2, 3, 3, 2, 2, 3]
        c_deep, c_mid, c_lit = 3, 15, 15
    else:
        tuft = [4, 3, 5, 5, 3, 3, 4, 5, 4, 3, 4, 5, 4, 3, 4, 3]
        c_deep, c_mid, c_lit = 2, 4, 5
    if edge == 'N':
        for x in range(16):
            h = tuft[x]
            for y in range(h - 1):
                t.p(x, y, c_lit if (y <= 1 and (x & 1)) else c_mid)
            t.p(x, h - 1, c_deep)
    else:  # 'W'
        for y in range(16):
            w = tuft[y]
            for x in range(w - 1):
                t.p(x, y, c_lit if (x <= 1 and (y & 1)) else c_mid)
            t.p(w - 1, y, c_deep)
    return t

def make_feathered_corner(base_fn, corner, moss_on_arch=False):
    """Smooth rounded quarter-circle grass overhang for Cobble & Dirt road corners!"""
    if corner == 'NE': return make_feathered_corner(base_fn, 'NW', moss_on_arch).flip_h()
    if corner == 'SW': return make_feathered_corner(base_fn, 'NW', moss_on_arch).flip_v()
    if corner == 'SE': return make_feathered_corner(base_fn, 'NW', moss_on_arch).flip_h().flip_v()
    t = base_fn(0)
    if moss_on_arch:
        c_deep, c_mid, c_lit = 3, 15, 15
        for y in range(16):
            for x in range(16):
                r = math.hypot(15.2 - x, 15.2 - y) + math.sin((x - y) * 0.55) * 0.35
                if r > 13.5:
                    t.p(x, y, c_mid)
                elif r > 12.6:
                    t.p(x, y, c_deep)
        return t
    c_deep, c_mid, c_lit = 2, 4, 5
    for y in range(16):
        for x in range(16):
            r = math.hypot(15.2 - x, 15.2 - y) + math.sin((x - y) * 0.55) * 0.45
            if r > 12.6:
                t.p(x, y, c_lit if ((x + y) & 1) else c_mid)
            elif r > 11.4:
                t.p(x, y, c_mid)
            elif r > 10.3:
                t.p(x, y, c_deep)
    return t

def make_sand_grass_transition(edge):
    """
    Organic feathered Beach-to-Grass biome transition (Edges + Rounded Corners!).
    Built over warm sunlit beach sand (make_world_beach) so tropical coastlines blend smoothly!
    """
    if edge == 'E':  return make_sand_grass_transition('W').flip_h()
    if edge == 'S':  return make_sand_grass_transition('N').flip_v()
    if edge == 'NE': return make_sand_grass_transition('NW').flip_h()
    if edge == 'SW': return make_sand_grass_transition('NW').flip_v()
    if edge == 'SE': return make_sand_grass_transition('NW').flip_h().flip_v()

    t = make_world_beach()
    g = make_grass(0)
    if edge == 'N':
        curve = [4.5, 5.2, 5.8, 6.2, 5.6, 4.9, 4.3, 4.7, 5.5, 6.1, 5.8, 5.0, 4.4, 4.8, 5.4, 4.9]
        for x in range(16):
            h = curve[x]
            for y in range(16):
                d = y - h
                if d < -0.9:
                    t.p(x, y, g.px[y][x])
                elif d < 0.0:
                    t.p(x, y, 3 if ((x + y) & 1) else 2)  # soft mossy grass overhang shadow
                elif d < 1.1:
                    t.p(x, y, 9)                         # shaded sand rim right under turf
    elif edge == 'W':
        curve = [4.5, 5.2, 5.8, 6.2, 5.6, 4.9, 4.3, 4.7, 5.5, 6.1, 5.8, 5.0, 4.4, 4.8, 5.4, 4.9]
        for y in range(16):
            w = curve[y]
            for x in range(16):
                d = x - w
                if d < -0.9:
                    t.p(x, y, g.px[y][x])
                elif d < 0.0:
                    t.p(x, y, 3 if ((x + y) & 1) else 2)
                elif d < 1.1:
                    t.p(x, y, 9)
    elif edge == 'NW':
        for y in range(16):
            for x in range(16):
                r = math.hypot(15.2 - x, 15.2 - y) + math.sin((x - y) * 0.5) * 0.5
                if r > 12.2:
                    t.p(x, y, g.px[y][x])
                elif r > 11.1:
                    t.p(x, y, 3 if ((x + y) & 1) else 2)
                elif r > 10.0:
                    t.p(x, y, 9)
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
# 5. BUILD ALL 188 METATILES + 4 ANIMATION FRAMES FOR ANIMATED METATILES
# ==============================================================================
ANIM_META_NAMES = [
    "T_WATER", "T_FORD", "T_RAPIDS", "T_WORLD_OCEAN", "CM_OCEAN_V1",
    "T_WORLD_SHALLOW", "T_FLOWERS", "T_TALL_GRASS", "T_SHRINE_RUNE",
    "T_FIREPLACE", "T_WORLD_LAVA", "T_WORLD_CORAL",
    "CM_WATER_V", "CM_RAPIDS_V", "CM_SHORE_V_W", "CM_SHORE_V_E",
    "CM_SHORE_WE", "CM_SHORE_NS",
    "CM_SHORE_N", "CM_SHORE_S", "CM_SHORE_W", "CM_SHORE_E",
    "CM_SHORE_NW", "CM_SHORE_NE", "CM_SHORE_SW", "CM_SHORE_SE",
    "CM_SHORE_INW", "CM_SHORE_INE", "CM_SHORE_ISW", "CM_SHORE_ISE",
    "CM_WORLD_COAST_N", "CM_WORLD_COAST_S", "CM_WORLD_COAST_W", "CM_WORLD_COAST_E",
    "CM_WORLD_COAST_NW", "CM_WORLD_COAST_NE", "CM_WORLD_COAST_SW", "CM_WORLD_COAST_SE",
    "CM_WORLD_COAST_INW", "CM_WORLD_COAST_INE", "CM_WORLD_COAST_ISW", "CM_WORLD_COAST_ISE",
    "CM_OCEAN_COAST_N", "CM_OCEAN_COAST_S", "CM_OCEAN_COAST_W", "CM_OCEAN_COAST_E",
    "CM_OCEAN_COAST_NW", "CM_OCEAN_COAST_NE", "CM_OCEAN_COAST_SW", "CM_OCEAN_COAST_SE",
    "CM_OCEAN_COAST_INW", "CM_OCEAN_COAST_INE", "CM_OCEAN_COAST_ISW", "CM_OCEAN_COAST_ISE",
    "CM_SHOAL_N", "CM_SHOAL_S", "CM_SHOAL_W", "CM_SHOAL_E",
    "CM_SHOAL_NW", "CM_SHOAL_NE", "CM_SHOAL_SW", "CM_SHOAL_SE",
    "CM_SHOAL_INW", "CM_SHOAL_INE", "CM_SHOAL_ISW", "CM_SHOAL_ISE",
    "CM_PIER_WATER_EDGE",
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
    # Shallow creek ford: N-S flowing creek water with warm mossy stepping stones along the center
    t_ford = make_water(frame, vertical=True)
    for (sx, sy, rw, rh) in ((4, 2, 4, 3), (9, 6, 4, 3), (3, 10, 4, 3), (8, 12, 4, 2)):
        for dy in range(rh):
            for dx in range(rw):
                if (dx == 0 or dx == rw - 1) and (dy == 0 or dy == rh - 1):
                    continue
                t_ford.p(sx + dx, sy + dy, 11 if dy == 0 else (10 if dx == rw - 1 else 14))
    M["T_FORD"]          = t_ford
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
    M["T_WORLD_PEAK"]    = make_world_peak("grass")
    M["CM_PEAK_SNOW"]    = make_world_peak("snow")
    M["CM_PEAK_VOLCANO"] = make_world_peak("volcano")
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
    M["CM_OV_MAPLE_MID"]   = make_maple_mid()
    M["CM_OV_MAPLE_TOP"]   = make_maple_top()

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
    M["CM_OCEAN_V1"]      = make_water(frame, deep=True, ocean_v1=True)

    # Rounded Shorelines & Organic Feathered Edges + Corners
    for e in ('N', 'S', 'W', 'E', 'NW', 'NE', 'SW', 'SE', 'INW', 'INE', 'ISW', 'ISE'):
        M[f"CM_SHORE_{e}"] = make_shore(e, frame)
    for e in ('N', 'S', 'W', 'E'):
        M[f"CM_COBBLE_EDGE_{e}"] = make_feathered_edge(make_cobble, e, moss_on_arch=True)
        M[f"CM_DIRT_EDGE_{e}"]   = make_feathered_edge(make_dirt, e, moss_on_arch=False)
    for c in ('NW', 'NE', 'SW', 'SE'):
        M[f"CM_COBBLE_CORNER_{c}"] = make_feathered_corner(make_cobble, c, moss_on_arch=True)
        M[f"CM_DIRT_CORNER_{c}"]   = make_feathered_corner(make_dirt, c, moss_on_arch=False)
    for e in ('N', 'S', 'W', 'E', 'NW', 'NE', 'SW', 'SE'):
        M[f"CM_SAND_GRASS_{e}"] = make_sand_grass_transition(e)

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

    # Seamless Ground-Composited Props (ZERO green boxes on Cobblestone / Pier / Autumn / Dirt / Wood!)
    t = make_cobble(0); paint_lantern(t, TPAL_ARCH); M["CM_LAMP_COBBLE"] = t
    t = make_cobble(0); paint_lantern_unlit(t, TPAL_ARCH); M["CM_LAMP_UNLIT"] = t
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

    # Cozy Cottage & Shop Indoor Furnishings, Cutaway Wall Cap & 2-Tile Bed Footboard
    t = make_wood_floor(0); paint_indoor_lamp(t); M["CM_LAMP_WOOD"] = t
    t = make_wood_floor(0); paint_indoor_planter(t); M["CM_FLOWERBED_WOOD"] = t
    t = make_wood_floor(0); paint_indoor_counter(t); M["CM_COUNTER_WOOD"] = t
    t = make_wood_floor(0); paint_indoor_lectern(t); M["CM_LECTERN_WOOD"] = t
    M["CM_INTERIOR_CAP"] = make_interior_cap()
    M["CM_BED_FOOT"]     = make_bed_foot()

    # All 12 Evertale Shallow & Deep Ocean Coastlines + Deep-to-Shallow Shoal Transitions & Empty
    for e in ('N', 'S', 'W', 'E', 'NW', 'NE', 'SW', 'SE', 'INW', 'INE', 'ISW', 'ISE'):
        M[f"CM_WORLD_COAST_{e}"] = make_world_coast(e, frame, deep_ocean=False)
        M[f"CM_OCEAN_COAST_{e}"] = make_world_coast(e, frame, deep_ocean=True)
        M[f"CM_SHOAL_{e}"]       = make_world_shoal(e, frame)
    M["CM_EMPTY"] = Tile16(TPAL_FOLIAGE, 0)

    # Directional N-S Vertical Rivers, Narrow Channels, N-S Bridge & Ground-Matched Props
    M["CM_WATER_V"]   = make_water(frame, vertical=True)
    M["CM_RAPIDS_V"]  = make_water(frame, rapids=True, vertical=True)
    M["CM_SHORE_V_W"] = make_shore('W', frame, vertical=True)
    M["CM_SHORE_V_E"] = make_shore('E', frame, vertical=True)
    M["CM_SHORE_WE"]  = make_shore('WE', frame, vertical=True)
    M["CM_SHORE_NS"]  = make_shore('NS', frame, vertical=False)
    M["CM_BRIDGE_NS"] = make_bridge_ns()
    t = make_cobble(0); paint_boulder(t, TPAL_ARCH); M["CM_BOULDER_COBBLE"] = t
    t = make_world_beach(); paint_boulder(t, TPAL_FOLIAGE); M["CM_BOULDER_SAND"] = t
    t = make_grass(0); paint_tree_trunk(t, palm=True); M["CM_PALM_GRASS"] = t

    # Opaque-backed overhead canopies & eaves (for SNES BG1 & Genesis Parallax mode!)
    def make_opq_canopy(ov_tile, bg_tile, prio=1):
        out_t = bg_tile.clone()
        out_t.prio = prio
        if ov_tile.qpal[0] == out_t.qpal[0]:
            out_t.overlay_from(ov_tile)
        else:
            # Mirror exact dappled meadow turf pixel pattern into TPAL_ACCENT indices 6,7,8 (= fol[3,4,5]!)
            out_t = Tile16(ov_tile.qpal[0], 7, prio=prio)
            for y in range(16):
                for x in range(16):
                    bc = bg_tile.px[y][x]
                    out_t.p(x, y, 6 if bc <= 3 else (8 if bc >= 5 else 7))
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
    M["CM_OPQ_MAPLE_MID"]   = make_opq_canopy(M["CM_OV_MAPLE_MID"], dapple_g)
    M["CM_OPQ_MAPLE_TOP"]   = make_opq_canopy(M["CM_OV_MAPLE_TOP"], dapple_g)

    return [M[name] for name in META_NAMES]

# ==============================================================================
# 6. 8x8 TILE COMPILER WITH H/V FLIP DEDUP + HARDWARE BUDGET FOLDING
# ==============================================================================
# Exact color remapping from 4-palette (TPAL_*) indices into Genesis 2-palette (PAL0 / PAL3):
#   TPAL_FOLIAGE -> GEN PAL0 (slot 0)
#   TPAL_ARCH    -> GEN PAL0 (timber/roofs) OR GEN PAL3 (stone/cobble) depending on pixel content!
#   TPAL_WATER   -> GEN PAL3 (slot 3; index 11 maps to golden beach sand 14!)
#   TPAL_ACCENT  -> GEN PAL3 (slot 3)
GEN_MAP_FOLIAGE_TO_P0 = [0, 1, 2, 3, 4, 5, 5, 6, 7, 8, 9, 14, 13, 15, 14, 2]
GEN_MAP_ARCH_TO_P0    = [0, 1, 6, 7, 8, 9, 6, 7, 8, 9, 10, 11, 12, 13, 14, 3]
GEN_MAP_ARCH_TO_P3    = [0, 1, 6, 7, 8, 9, 1, 6, 8, 9, 11, 12, 13, 14, 15, 10]
GEN_MAP_WATER_TO_P3   = [0, 2, 3, 4, 2, 6, 7, 9, 2, 10, 10, 14, 3, 4, 15, 5]
GEN_MAP_ACCENT_TO_P3  = [0, 1, 11, 12, 13, 14, 2, 3, 4, 5, 6, 7, 14, 14, 15, 5]
# Maple canopies (CM_OPQ_MAPLE_* / CM_OV_MAPLE_*) use GEN PAL0: crimson/gold arch[10..13] + exact fol[3..5] turf!
GEN_MAP_MAPLE_TO_P0   = [0, 1, 10, 11, 12, 13, 3, 4, 5, 5, 6, 7, 13, 12, 13, 14]

def remap_quad_for_genesis(q8, qpal, meta_name=""):
    """Map an 8x8 tile from 4-palette space onto Genesis PAL0 (0) or PAL3 (3)."""
    if qpal == TPAL_FOLIAGE:
        return tuple(GEN_MAP_FOLIAGE_TO_P0[c] for c in q8), 0
    if qpal == TPAL_WATER:
        return tuple(GEN_MAP_WATER_TO_P3[c] for c in q8), 3
    if qpal == TPAL_ACCENT:
        if "MAPLE" in meta_name:
            return tuple(GEN_MAP_MAPLE_TO_P0[c] for c in q8), 0
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
        "CM_WATER_V", "CM_RAPIDS_V",
        "T_FLOWERS", "T_TALL_GRASS", "T_SHRINE_RUNE",
    ]
    CORE_ANIM_SET = set(CORE_ANIM)

    def get_quad(meta_id, qidx, frame_idx):
        t = frames_meta[frame_idx][meta_id]
        q8 = extract_quad8(t, qidx)
        pal = t.qpal[qidx]
        if is_genesis:
            q8, pal = remap_quad_for_genesis(q8, pal, META_NAMES[meta_id])
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

    # Step 3: If over hardware VRAM budget, fold the GLOBALLY closest non-animated tile pairs!
    # This preserves distinctive indoor furniture, canopies & corners while merging only near-identical
    # grass/stone/water sub-variants (typical Hamming distance <= 6 px out of 64!).
    if len(static_tiles) > max_tiles:
        remap = list(range(len(static_tiles)))
        # Protect visually critical state-toggle & feathered edge-transition metatiles from fuzzy folding
        protected_tids = set()
        for pname in (
            "CM_LAMP_UNLIT", "CM_LAMP_COBBLE",
            "CM_OCEAN_V1",
            "CM_COBBLE_EDGE_N", "CM_COBBLE_EDGE_S", "CM_COBBLE_EDGE_W", "CM_COBBLE_EDGE_E",
            "CM_COBBLE_CORNER_NW", "CM_COBBLE_CORNER_NE", "CM_COBBLE_CORNER_SW", "CM_COBBLE_CORNER_SE",
            "CM_SAND_GRASS_N", "CM_SAND_GRASS_S", "CM_SAND_GRASS_W", "CM_SAND_GRASS_E",
            "CM_SHOAL_N", "CM_SHOAL_S", "CM_SHOAL_W", "CM_SHOAL_E",
            "CM_SHOAL_NW", "CM_SHOAL_NE", "CM_SHOAL_SW", "CM_SHOAL_SE",
            "CM_SHOAL_INW", "CM_SHOAL_INE", "CM_SHOAL_ISW", "CM_SHOAL_ISE",
        ):
            if pname in META_IDX:
                for qinfo in meta_quads[META_IDX[pname]]:
                    protected_tids.add(qinfo[0])
        active = list(range(1 + anim_count, len(static_tiles)))
        # Weight 8x8 border pixels 3x higher so console tile folding preserves clean 16x16 edge seams!
        border_weights = [3 if (i < 8 or i >= 56 or (i & 7) in (0, 7)) else 1 for i in range(64)]
        # Precompute nearest neighbor (dist, other_idx) for each active tile
        def find_nearest(ti, pool):
            qi = static_tiles[ti]
            bd = 9999
            bo = -1
            for oj in pool:
                if oj == ti:
                    continue
                if ti in protected_tids and oj in protected_tids:
                    continue
                qo = static_tiles[oj]
                d = 0
                for idx in range(64):
                    if qi[idx] != qo[idx]:
                        d += border_weights[idx]
                        if d >= bd:
                            break
                if d < bd:
                    bd = d
                    bo = oj
                    if bd <= 1:
                        break
            return bd, bo

        nn = {ti: find_nearest(ti, active) for ti in active}
        max_folded_d = 0
        while len(active) + 1 + anim_count > max_tiles:
            # Pick active tile with smallest Hamming distance to another surviving active tile (never drop protected)
            droppable = [ti for ti in active if ti not in protected_tids]
            best_t = min(droppable, key=lambda ti: nn[ti][0])
            bd, bo = nn[best_t]
            if bd > max_folded_d:
                max_folded_d = bd
            # Fold the droppable tile into its nearest surviving partner
            if bo in protected_tids:
                drop, keep_t = best_t, bo
            else:
                drop, keep_t = (best_t, bo) if best_t > bo else (bo, best_t)
            remap[drop] = keep_t
            active.remove(drop)
            del nn[drop]
            # Recompute nearest neighbor only for tiles whose partner was dropped
            for ti in active:
                if nn[ti][1] == drop:
                    nn[ti] = find_nearest(ti, active)

        # Compact surviving tile indices (never mutate remap in-place while chasing fold chains!)
        keep = list(range(0, 1 + anim_count)) + active
        new_id = {}
        new_tiles = []
        for old in keep:
            new_id[old] = len(new_tiles)
            new_tiles.append(static_tiles[old])
        final_remap = [0] * len(remap)
        for i in range(len(remap)):
            r = i
            while remap[r] != r:
                r = remap[r]
            final_remap[i] = new_id[r]

        static_tiles = new_tiles
        for mid in range(len(meta_quads)):
            for qidx in range(4):
                tid, pal, hf, vf, prio = meta_quads[mid][qidx]
                meta_quads[mid][qidx] = (final_remap[tid], pal, hf, vf, prio)

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
    out.append("    return (t == T_WATER || t == T_FORD || t == T_RAPIDS || t == T_WORLD_OCEAN || t == T_WORLD_SHALLOW) ? 1u : 0u;")
    out.append("}")
    out.append("")
    out.append("static uint8_t hearth_is_shallow_water(uint8_t t) {")
    out.append("    return (t == T_WORLD_SHALLOW || t == T_WORLD_CORAL) ? 1u : 0u;")
    out.append("}")
    out.append("")
    out.append("static uint8_t hearth_is_grass_like(uint8_t t) {")
    out.append("    return (t == T_GRASS || t == T_GRASS_ALT || t == T_FLOWERS ||")
    out.append("            t == T_TALL_GRASS || t == T_AUTUMN_GRASS) ? 1u : 0u;")
    out.append("}")
    out.append("")
    out.append("/* Infer dominant walkable ground underneath a prop (majority vote eliminates mismatched boxes!) */")
    out.append("static uint8_t hearth_infer_prop_ground(uint8_t tn, uint8_t ts, uint8_t tw, uint8_t te) {")
    out.append("    uint8_t wood = 0u, cob = 0u, pier = 0u, aut = 0u, dirt = 0u, sand = 0u, grass = 0u;")
    out.append("    uint8_t best = 0u, best_cnt;")
    out.append("    if (tn == T_WOOD_FLOOR || tn == T_HEARTH_RUG) wood++;")
    out.append("    if (ts == T_WOOD_FLOOR || ts == T_HEARTH_RUG) wood++;")
    out.append("    if (tw == T_WOOD_FLOOR || tw == T_HEARTH_RUG) wood++;")
    out.append("    if (te == T_WOOD_FLOOR || te == T_HEARTH_RUG) wood++;")
    out.append("    if (wood >= 1u) return 5u; /* Cozy Cottage / Shop Wood Floor */")
    out.append("    if (tn == T_GRASS || tn == T_GRASS_ALT || tn == T_FLOWERS || tn == T_TALL_GRASS) grass++;")
    out.append("    if (ts == T_GRASS || ts == T_GRASS_ALT || ts == T_FLOWERS || ts == T_TALL_GRASS) grass++;")
    out.append("    if (tw == T_GRASS || tw == T_GRASS_ALT || tw == T_FLOWERS || tw == T_TALL_GRASS) grass++;")
    out.append("    if (te == T_GRASS || te == T_GRASS_ALT || te == T_FLOWERS || te == T_TALL_GRASS) grass++;")
    out.append("    if (tn == T_DOCK_PIER || tn == T_BRIDGE) pier++;")
    out.append("    if (ts == T_DOCK_PIER || ts == T_BRIDGE) pier++;")
    out.append("    if (tw == T_DOCK_PIER || tw == T_BRIDGE) pier++;")
    out.append("    if (te == T_DOCK_PIER || te == T_BRIDGE) pier++;")
    out.append("    if (tn == T_COBBLE || tn == T_ROCKY_PATH || tn == T_SHRINE_FLOOR || tn == T_SHRINE_RUNE) cob++;")
    out.append("    if (ts == T_COBBLE || ts == T_ROCKY_PATH || ts == T_SHRINE_FLOOR || ts == T_SHRINE_RUNE) cob++;")
    out.append("    if (tw == T_COBBLE || tw == T_ROCKY_PATH || tw == T_SHRINE_FLOOR || tw == T_SHRINE_RUNE) cob++;")
    out.append("    if (te == T_COBBLE || te == T_ROCKY_PATH || te == T_SHRINE_FLOOR || te == T_SHRINE_RUNE) cob++;")
    out.append("    if (tn == T_AUTUMN_GRASS) aut++;")
    out.append("    if (ts == T_AUTUMN_GRASS) aut++;")
    out.append("    if (tw == T_AUTUMN_GRASS) aut++;")
    out.append("    if (te == T_AUTUMN_GRASS) aut++;")
    out.append("    if (tn == T_WORLD_SAND || tn == T_WORLD_BEACH) sand++;")
    out.append("    if (ts == T_WORLD_SAND || ts == T_WORLD_BEACH) sand++;")
    out.append("    if (tw == T_WORLD_SAND || tw == T_WORLD_BEACH) sand++;")
    out.append("    if (te == T_WORLD_SAND || te == T_WORLD_BEACH) sand++;")
    out.append("    if (tn == T_DIRT) dirt++;")
    out.append("    if (ts == T_DIRT) dirt++;")
    out.append("    if (tw == T_DIRT) dirt++;")
    out.append("    if (te == T_DIRT) dirt++;")
    out.append("    best_cnt = grass;")
    out.append("    if (dirt > best_cnt || (dirt == best_cnt && dirt > 0u)) { best = 4u; best_cnt = dirt; }")
    out.append("    if (sand > best_cnt || (sand == best_cnt && sand > 0u)) { best = 6u; best_cnt = sand; }")
    out.append("    if (aut  > best_cnt || (aut  == best_cnt && aut  > 0u)) { best = 3u; best_cnt = aut;  }")
    out.append("    if (cob  > best_cnt || (cob  == best_cnt && cob  > 0u)) { best = 1u; best_cnt = cob;  }")
    out.append("    if (pier > best_cnt || (pier == best_cnt && pier > 0u)) { best = 2u; best_cnt = pier; }")
    out.append("    return best;")
    out.append("}")
    out.append("")
    out.append("static uint8_t hearth_is_shore_land(uint8_t t, uint8_t is_world) {")
    out.append("    if (hearth_is_water_like(t) || t == T_WORLD_CORAL || t == T_DOCK_PIER) return 0u;")
    out.append("    if (!is_world && (t == T_BRIDGE || t == T_BRIDGE_RAIL_N || t == T_BRIDGE_RAIL_S)) return 0u;")
    out.append("    return 1u;")
    out.append("}")
    out.append("")
    out.append("/* Compute local-zone directional flags: bit1=N-S vertical river, bit2=N-S bridge */")
    out.append("static uint8_t hearth_local_tile_flags(const uint8_t *bg1, uint16_t mx, uint16_t my, uint16_t mw, uint16_t mh) {")
    out.append("    uint8_t t = bg1[(uint32_t)my * mw + mx];")
    out.append("    uint8_t flags = 0u;")
    out.append("    uint16_t d;")
    out.append("    if (t == T_WATER || t == T_FORD || t == T_RAPIDS) {")
    out.append("        uint8_t land_w = 0u, land_e = 0u;")
    out.append("        uint8_t run_n = 0u, run_s = 0u;")
    out.append("        for (d = 1u; d <= 4u; d++) {")
    out.append("            if (my >= d) {")
    out.append("                uint8_t c = bg1[(uint32_t)(my - d) * mw + mx];")
    out.append("                if (hearth_is_water_like(c) || c == T_BRIDGE || c == T_BRIDGE_RAIL_N || c == T_BRIDGE_RAIL_S) run_n++;")
    out.append("                else break;")
    out.append("            }")
    out.append("        }")
    out.append("        for (d = 1u; d <= 4u; d++) {")
    out.append("            if (my + d < mh) {")
    out.append("                uint8_t c = bg1[(uint32_t)(my + d) * mw + mx];")
    out.append("                if (hearth_is_water_like(c) || c == T_BRIDGE || c == T_BRIDGE_RAIL_N || c == T_BRIDGE_RAIL_S) run_s++;")
    out.append("                else break;")
    out.append("            }")
    out.append("        }")
    out.append("        for (d = 1u; d <= 4u; d++) {")
    out.append("            if (mx >= d && hearth_is_shore_land(bg1[(uint32_t)my * mw + (mx - d)], 0u)) land_w = 1u;")
    out.append("            if (mx + d < mw && hearth_is_shore_land(bg1[(uint32_t)my * mw + (mx + d)], 0u)) land_e = 1u;")
    out.append("        }")
    out.append("        if (land_w && land_e && (uint8_t)(run_n + 1u + run_s) >= 5u) {")
    out.append("            flags |= 2u;")
    out.append("        }")
    out.append("    } else if (t == T_BRIDGE) {")
    out.append("        uint8_t tn = (my >= 1u) ? bg1[(uint32_t)(my - 1u) * mw + mx] : t;")
    out.append("        uint8_t ts = (my + 1u < mh) ? bg1[(uint32_t)(my + 1u) * mw + mx] : t;")
    out.append("        uint8_t wn = (my >= 1u && hearth_is_water_like(tn)) ? 1u : 0u;")
    out.append("        uint8_t ws = (my + 1u < mh && hearth_is_water_like(ts)) ? 1u : 0u;")
    out.append("        uint8_t rail_ns = (tn == T_BRIDGE_RAIL_N || tn == T_BRIDGE_RAIL_S || ts == T_BRIDGE_RAIL_N || ts == T_BRIDGE_RAIL_S) ? 1u : 0u;")
    out.append("        uint8_t road_ns = (tn == T_BRIDGE || ts == T_BRIDGE || tn == T_COBBLE || ts == T_COBBLE ||")
    out.append("                           tn == T_ROCKY_PATH || ts == T_ROCKY_PATH || tn == T_DIRT || ts == T_DIRT ||")
    out.append("                           tn == T_SHRINE_FLOOR || ts == T_SHRINE_FLOOR) ? 1u : 0u;")
    out.append("        uint8_t ww = 0u, we = 0u;")
    out.append("        if (!rail_ns) {")
    out.append("            if (ws && !wn) {")
    out.append("                flags |= 4u;")
    out.append("            } else if (!wn && ts == T_BRIDGE && my + 2u < mh && hearth_is_water_like(bg1[(uint32_t)(my + 2u) * mw + mx])) {")
    out.append("                flags |= 4u;")
    out.append("            } else if (road_ns) {")
    out.append("                for (d = 1u; d <= 4u; d++) {")
    out.append("                    if (mx >= d) {")
    out.append("                        uint8_t c = bg1[(uint32_t)my * mw + (mx - d)];")
    out.append("                        if (hearth_is_water_like(c)) { ww = 1u; break; }")
    out.append("                        if (c != T_BRIDGE) break;")
    out.append("                    }")
    out.append("                }")
    out.append("                for (d = 1u; d <= 4u; d++) {")
    out.append("                    if (mx + d < mw) {")
    out.append("                        uint8_t c = bg1[(uint32_t)my * mw + (mx + d)];")
    out.append("                        if (hearth_is_water_like(c)) { we = 1u; break; }")
    out.append("                        if (c != T_BRIDGE) break;")
    out.append("                    }")
    out.append("                }")
    out.append("                if ((ww || we) && ((!wn && !ws) || (ww && we))) {")
    out.append("                    flags |= 4u;")
    out.append("                }")
    out.append("            }")
    out.append("        }")
    out.append("    }")
    out.append("    return flags;")
    out.append("}")
    out.append("")
    out.append("static uint8_t hearth_resolve_chrono_meta(uint8_t t, uint8_t ov,")
    out.append("                                          uint8_t tn, uint8_t ts, uint8_t tw, uint8_t te,")
    out.append("                                          uint8_t tnw, uint8_t tne, uint8_t tsw, uint8_t tse,")
    out.append("                                          uint16_t mx, uint16_t my, uint8_t flags) {")
    out.append("    uint8_t v = (uint8_t)(((mx * 3u + my * 5u) ^ (mx >> 1)) & 3u);")
    out.append("    uint8_t is_world = (uint8_t)(flags & 1u);")
    out.append("    uint8_t is_ns_river = (uint8_t)((flags & 2u) != 0u);")
    out.append("    uint8_t is_ns_bridge = (uint8_t)((flags & 4u) != 0u);")
    out.append("    uint8_t gnd;")
    out.append("    /* 1. Seamless Ground-Composited Props, Palms & Cozy Indoor Furniture */")
    out.append("    if (t == T_LANTERN_UNLIT) return (uint8_t)CM_LAMP_UNLIT;")
    out.append("    if (t == T_WORLD_PEAK) {")
    out.append("        if ((is_world && my < 40u) || tn == T_WORLD_SNOW || ts == T_WORLD_SNOW || tw == T_WORLD_SNOW || te == T_WORLD_SNOW)")
    out.append("            return (uint8_t)CM_PEAK_SNOW;")
    out.append("        if (is_world && mx < 68u && my >= 36u && my < 90u)")
    out.append("            return (uint8_t)CM_PEAK_VOLCANO;")
    out.append("        return T_WORLD_PEAK;")
    out.append("    }")
    out.append("    if (t == T_PALM_TREE) {")
    out.append("        uint8_t gc = (uint8_t)(hearth_is_grass_like(tn) + hearth_is_grass_like(ts) + hearth_is_grass_like(tw) + hearth_is_grass_like(te));")
    out.append("        uint8_t sc = (uint8_t)(((tn == T_WORLD_BEACH || tn == T_WORLD_SAND) ? 1u : 0u) +")
    out.append("                               ((ts == T_WORLD_BEACH || ts == T_WORLD_SAND) ? 1u : 0u) +")
    out.append("                               ((tw == T_WORLD_BEACH || tw == T_WORLD_SAND) ? 1u : 0u) +")
    out.append("                               ((te == T_WORLD_BEACH || te == T_WORLD_SAND) ? 1u : 0u));")
    out.append("        if (gc >= 1u && gc >= sc) return (uint8_t)CM_PALM_GRASS;")
    out.append("        return T_PALM_TREE;")
    out.append("    }")
    out.append("    if (t == T_LANTERN_POST || t == T_SIGNPOST || t == T_FLOWER_BED || t == T_BUSH || t == T_FENCE || t == T_BOULDER) {")
    out.append("        gnd = hearth_infer_prop_ground(tn, ts, tw, te);")
    out.append("        if (gnd == 5u) { /* Cottage / Shop Wood Floorboard Interior */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_WOOD;")
    out.append("            if (t == T_FLOWER_BED)   return (uint8_t)CM_FLOWERBED_WOOD;")
    out.append("            if (t == T_BUSH)         return (uint8_t)CM_FLOWERBED_WOOD;")
    out.append("            if (t == T_FENCE)        return (uint8_t)CM_COUNTER_WOOD;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_LECTERN_WOOD;")
    out.append("        } else if (gnd == 1u) { /* Cobblestone / Stone Path / Shrine */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_COBBLE;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_COBBLE;")
    out.append("            if (t == T_FLOWER_BED)   return (uint8_t)CM_FLOWERBED_COBBLE;")
    out.append("            if (t == T_BUSH)         return (uint8_t)CM_BUSH_COBBLE;")
    out.append("            if (t == T_FENCE)        return (uint8_t)CM_FENCE_COBBLE;")
    out.append("            if (t == T_BOULDER)      return (uint8_t)CM_BOULDER_COBBLE;")
    out.append("        } else if (gnd == 2u) { /* Timber Pier */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_PIER;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_PIER;")
    out.append("            if (t == T_BOULDER)      return (uint8_t)CM_BOULDER_PIER;")
    out.append("        } else if (gnd == 3u) { /* Autumn Mountain */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_AUTUMN;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_AUTUMN;")
    out.append("            if (t == T_BOULDER)      return (uint8_t)CM_BOULDER_AUTUMN;")
    out.append("        } else if (gnd == 6u) { /* Coastal Beach / Desert Sand */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_DIRT;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_DIRT;")
    out.append("            if (t == T_BOULDER)      return (uint8_t)CM_BOULDER_SAND;")
    out.append("        } else if (gnd == 4u) { /* Dirt Road */")
    out.append("            if (t == T_LANTERN_POST) return (uint8_t)CM_LAMP_DIRT;")
    out.append("            if (t == T_SIGNPOST)     return (uint8_t)CM_SIGN_DIRT;")
    out.append("        }")
    out.append("        return t;")
    out.append("    }")
    out.append("    /* 1b. Cutaway Interior Wall Caps, 2-Tile Bed Footboard & Vertical N-S Bridges */")
    out.append("    if (t == T_INTERIOR_WALL && (ts == T_INTERIOR_WALL || ts == T_MTN_SKY || ts == T_WALL_WINDOW || ts == T_FIREPLACE || ts == T_WALL_WOOD)) {")
    out.append("        return (uint8_t)CM_INTERIOR_CAP;")
    out.append("    }")
    out.append("    if (t == T_BED_QUILT && tn == T_BED_QUILT) {")
    out.append("        return (uint8_t)CM_BED_FOOT;")
    out.append("    }")
    out.append("    if (!is_world && t == T_BRIDGE && is_ns_bridge) {")
    out.append("        return (uint8_t)CM_BRIDGE_NS;")
    out.append("    }")
    out.append("    /* 2. Timber Pier South Water-Piling Edge */")
    out.append("    if (t == T_DOCK_PIER && hearth_is_water_like(ts)) {")
    out.append("        return (uint8_t)CM_PIER_WATER_EDGE;")
    out.append("    }")
    out.append("    /* 3. Full 8-Neighbor Rounded Shorelines, Directional Rivers & Coastlines */")
    out.append("    if (t == T_WATER || t == T_FORD || t == T_RAPIDS || t == T_WORLD_SHALLOW || t == T_WORLD_OCEAN || t == T_WORLD_CORAL) {")
    out.append("        uint8_t ln  = hearth_is_shore_land(tn, is_world);")
    out.append("        uint8_t ls  = hearth_is_shore_land(ts, is_world);")
    out.append("        uint8_t lw  = hearth_is_shore_land(tw, is_world);")
    out.append("        uint8_t le  = hearth_is_shore_land(te, is_world);")
    out.append("        uint8_t lnw = hearth_is_shore_land(tnw, is_world);")
    out.append("        uint8_t lne = hearth_is_shore_land(tne, is_world);")
    out.append("        uint8_t lsw = hearth_is_shore_land(tsw, is_world);")
    out.append("        uint8_t lse = hearth_is_shore_land(tse, is_world);")
    out.append("        if (t == T_WORLD_OCEAN) {")
    out.append("            uint8_t sn  = hearth_is_shallow_water(tn);")
    out.append("            uint8_t ss  = hearth_is_shallow_water(ts);")
    out.append("            uint8_t sw  = hearth_is_shallow_water(tw);")
    out.append("            uint8_t se  = hearth_is_shallow_water(te);")
    out.append("            uint8_t snw = hearth_is_shallow_water(tnw);")
    out.append("            uint8_t sne = hearth_is_shallow_water(tne);")
    out.append("            uint8_t ssw = hearth_is_shallow_water(tsw);")
    out.append("            uint8_t sse = hearth_is_shallow_water(tse);")
    out.append("            if (ln || ls || lw || le) {")
    out.append("                if (ln && lw) return (uint8_t)CM_OCEAN_COAST_NW;")
    out.append("                if (ln && le) return (uint8_t)CM_OCEAN_COAST_NE;")
    out.append("                if (ls && lw) return (uint8_t)CM_OCEAN_COAST_SW;")
    out.append("                if (ls && le) return (uint8_t)CM_OCEAN_COAST_SE;")
    out.append("                if (ln) return (uint8_t)CM_OCEAN_COAST_N;")
    out.append("                if (ls) return (uint8_t)CM_OCEAN_COAST_S;")
    out.append("                if (lw) return (uint8_t)CM_OCEAN_COAST_W;")
    out.append("                if (le) return (uint8_t)CM_OCEAN_COAST_E;")
    out.append("            }")
    out.append("            if (sn || ss || sw || se) {")
    out.append("                if (sn && sw) return (uint8_t)CM_SHOAL_NW;")
    out.append("                if (sn && se) return (uint8_t)CM_SHOAL_NE;")
    out.append("                if (ss && sw) return (uint8_t)CM_SHOAL_SW;")
    out.append("                if (ss && se) return (uint8_t)CM_SHOAL_SE;")
    out.append("                if (sn) return (uint8_t)CM_SHOAL_N;")
    out.append("                if (ss) return (uint8_t)CM_SHOAL_S;")
    out.append("                if (sw) return (uint8_t)CM_SHOAL_W;")
    out.append("                if (se) return (uint8_t)CM_SHOAL_E;")
    out.append("            }")
    out.append("            if (lnw) return (uint8_t)CM_OCEAN_COAST_INW;")
    out.append("            if (lne) return (uint8_t)CM_OCEAN_COAST_INE;")
    out.append("            if (lsw) return (uint8_t)CM_OCEAN_COAST_ISW;")
    out.append("            if (lse) return (uint8_t)CM_OCEAN_COAST_ISE;")
    out.append("            if (snw) return (uint8_t)CM_SHOAL_INW;")
    out.append("            if (sne) return (uint8_t)CM_SHOAL_INE;")
    out.append("            if (ssw) return (uint8_t)CM_SHOAL_ISW;")
    out.append("            if (sse) return (uint8_t)CM_SHOAL_ISE;")
    out.append("            if (v & 1u) return (uint8_t)CM_OCEAN_V1;")
    out.append("            return t;")
    out.append("        }")
    out.append("        if (ln || ls || lw || le || lnw || lne || lsw || lse) {")
    out.append("            uint8_t use_sandy = (is_world || ((t == T_WORLD_SHALLOW || t == T_WORLD_CORAL) && !hearth_is_grass_like(tn) && !hearth_is_grass_like(ts) && !hearth_is_grass_like(tw) && !hearth_is_grass_like(te))) ? 1u : 0u;")
    out.append("            if (use_sandy) {")
    out.append("                if (ln && lw) return (uint8_t)CM_WORLD_COAST_NW;")
    out.append("                if (ln && le) return (uint8_t)CM_WORLD_COAST_NE;")
    out.append("                if (ls && lw) return (uint8_t)CM_WORLD_COAST_SW;")
    out.append("                if (ls && le) return (uint8_t)CM_WORLD_COAST_SE;")
    out.append("                if (ln) return (uint8_t)CM_WORLD_COAST_N;")
    out.append("                if (ls) return (uint8_t)CM_WORLD_COAST_S;")
    out.append("                if (lw) return (uint8_t)CM_WORLD_COAST_W;")
    out.append("                if (le) return (uint8_t)CM_WORLD_COAST_E;")
    out.append("                if (lnw) return (uint8_t)CM_WORLD_COAST_INW;")
    out.append("                if (lne) return (uint8_t)CM_WORLD_COAST_INE;")
    out.append("                if (lsw) return (uint8_t)CM_WORLD_COAST_ISW;")
    out.append("                if (lse) return (uint8_t)CM_WORLD_COAST_ISE;")
    out.append("            } else {")
    out.append("                if (ln && ls) return (uint8_t)CM_SHORE_NS;")
    out.append("                if (lw && le) return (uint8_t)CM_SHORE_WE;")
    out.append("                if (ln && lw) return (uint8_t)CM_SHORE_NW;")
    out.append("                if (ln && le) return (uint8_t)CM_SHORE_NE;")
    out.append("                if (ls && lw) return (uint8_t)CM_SHORE_SW;")
    out.append("                if (ls && le) return (uint8_t)CM_SHORE_SE;")
    out.append("                if (ln) return (uint8_t)CM_SHORE_N;")
    out.append("                if (ls) return (uint8_t)CM_SHORE_S;")
    out.append("                if (lw) return is_ns_river ? (uint8_t)CM_SHORE_V_W : (uint8_t)CM_SHORE_W;")
    out.append("                if (le) return is_ns_river ? (uint8_t)CM_SHORE_V_E : (uint8_t)CM_SHORE_E;")
    out.append("                if (lnw) return (uint8_t)CM_SHORE_INW;")
    out.append("                if (lne) return (uint8_t)CM_SHORE_INE;")
    out.append("                if (lsw) return (uint8_t)CM_SHORE_ISW;")
    out.append("                if (lse) return (uint8_t)CM_SHORE_ISE;")
    out.append("            }")
    out.append("        }")
    out.append("        if (!is_world && is_ns_river) {")
    out.append("            if (t == T_RAPIDS) return (uint8_t)CM_RAPIDS_V;")
    out.append("            if (t == T_WATER)  return (uint8_t)CM_WATER_V;")
    out.append("        }")
    out.append("        return t;")
    out.append("    }")
    out.append("    /* 4. Organic Rounded Sand/Beach-to-Grass Biome Transitions (World + Local Coastal Zones!) */")
    out.append("    if (t == T_WORLD_BEACH || t == T_WORLD_SAND) {")
    out.append("        uint8_t gn = hearth_is_grass_like(tn);")
    out.append("        uint8_t gs = hearth_is_grass_like(ts);")
    out.append("        uint8_t gw = hearth_is_grass_like(tw);")
    out.append("        uint8_t ge = hearth_is_grass_like(te);")
    out.append("        if (gn && gw) return (uint8_t)CM_SAND_GRASS_NW;")
    out.append("        if (gn && ge) return (uint8_t)CM_SAND_GRASS_NE;")
    out.append("        if (gs && gw) return (uint8_t)CM_SAND_GRASS_SW;")
    out.append("        if (gs && ge) return (uint8_t)CM_SAND_GRASS_SE;")
    out.append("        if (gn) return (uint8_t)CM_SAND_GRASS_N;")
    out.append("        if (gs) return (uint8_t)CM_SAND_GRASS_S;")
    out.append("        if (gw) return (uint8_t)CM_SAND_GRASS_W;")
    out.append("        if (ge) return (uint8_t)CM_SAND_GRASS_E;")
    out.append("        if (t == T_WORLD_SAND && (v & 1u)) return (uint8_t)CM_SAND_V1;")
    out.append("        return t;")
    out.append("    }")
    out.append("    /* 5. Cast Ground Shadows, Rounded Road Corners & Canopy Dapple */")
    out.append("    if (!is_world) {")
    out.append("        if (t == T_COBBLE) {")
    out.append("            uint8_t gn = hearth_is_grass_like(tn);")
    out.append("            uint8_t gs = hearth_is_grass_like(ts);")
    out.append("            uint8_t gw = hearth_is_grass_like(tw);")
    out.append("            uint8_t ge = hearth_is_grass_like(te);")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_COBBLE_SHAD_N;")
    out.append("            if (hearth_is_solid_caster(tw)) return (uint8_t)CM_COBBLE_SHAD_W;")
    out.append("            if (gn && gw) return (uint8_t)CM_COBBLE_CORNER_NW;")
    out.append("            if (gn && ge) return (uint8_t)CM_COBBLE_CORNER_NE;")
    out.append("            if (gs && gw) return (uint8_t)CM_COBBLE_CORNER_SW;")
    out.append("            if (gs && ge) return (uint8_t)CM_COBBLE_CORNER_SE;")
    out.append("            if (gn) return (uint8_t)CM_COBBLE_EDGE_N;")
    out.append("            if (gs) return (uint8_t)CM_COBBLE_EDGE_S;")
    out.append("            if (gw) return (uint8_t)CM_COBBLE_EDGE_W;")
    out.append("            if (ge) return (uint8_t)CM_COBBLE_EDGE_E;")
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
    out.append("            uint8_t gn = hearth_is_grass_like(tn);")
    out.append("            uint8_t gs = hearth_is_grass_like(ts);")
    out.append("            uint8_t gw = hearth_is_grass_like(tw);")
    out.append("            uint8_t ge = hearth_is_grass_like(te);")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_DIRT_SHAD_N;")
    out.append("            if (gn && gw) return (uint8_t)CM_DIRT_CORNER_NW;")
    out.append("            if (gn && ge) return (uint8_t)CM_DIRT_CORNER_NE;")
    out.append("            if (gs && gw) return (uint8_t)CM_DIRT_CORNER_SW;")
    out.append("            if (gs && ge) return (uint8_t)CM_DIRT_CORNER_SE;")
    out.append("            if (gn) return (uint8_t)CM_DIRT_EDGE_N;")
    out.append("            if (gs) return (uint8_t)CM_DIRT_EDGE_S;")
    out.append("            if (gw) return (uint8_t)CM_DIRT_EDGE_W;")
    out.append("            if (ge) return (uint8_t)CM_DIRT_EDGE_E;")
    out.append("            return (v & 1u) ? (uint8_t)CM_DIRT_V1 : T_DIRT;")
    out.append("        }")
    out.append("        if (t == T_ROCKY_PATH) {")
    out.append("            uint8_t gn = hearth_is_grass_like(tn);")
    out.append("            uint8_t gs = hearth_is_grass_like(ts);")
    out.append("            uint8_t gw = hearth_is_grass_like(tw);")
    out.append("            uint8_t ge = hearth_is_grass_like(te);")
    out.append("            if (hearth_is_solid_caster(tn)) return (uint8_t)CM_ROCKY_SHAD_N;")
    out.append("            if (gn && gw) return (uint8_t)CM_COBBLE_CORNER_NW;")
    out.append("            if (gn && ge) return (uint8_t)CM_COBBLE_CORNER_NE;")
    out.append("            if (gs && gw) return (uint8_t)CM_COBBLE_CORNER_SW;")
    out.append("            if (gs && ge) return (uint8_t)CM_COBBLE_CORNER_SE;")
    out.append("            if (gn) return (uint8_t)CM_COBBLE_EDGE_N;")
    out.append("            if (gs) return (uint8_t)CM_COBBLE_EDGE_S;")
    out.append("            if (gw) return (uint8_t)CM_COBBLE_EDGE_W;")
    out.append("            if (ge) return (uint8_t)CM_COBBLE_EDGE_E;")
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
    out.append("        if (t == T_WORLD_SNOW && (v & 1u)) return (uint8_t)CM_SNOW_V1;")
    out.append("        if (t == T_WORLD_JUNGLE && (v & 1u)) return (uint8_t)CM_JUNGLE_V1;")
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
        "  return t === 6 || t === 7 || t === 34 || t === 46 || t === 54;",
        "}",
        "function isGrassLike(t: number): boolean {",
        "  return t === 0 || t === 1 || t === 2 || t === 3 || t === 32;",
        "}",
        "function inferPropGround(tn: number, ts: number, tw: number, te: number): number {",
        "  let wood = 0, cob = 0, pier = 0, aut = 0, dirt = 0, sand = 0, grass = 0;",
        "  if (tn === 38 || tn === 39) wood++; if (ts === 38 || ts === 39) wood++;",
        "  if (tw === 38 || tw === 39) wood++; if (te === 38 || te === 39) wood++;",
        "  if (wood >= 1) return 5;",
        "  if (tn === 0 || tn === 1 || tn === 2 || tn === 3) grass++;",
        "  if (ts === 0 || ts === 1 || ts === 2 || ts === 3) grass++;",
        "  if (tw === 0 || tw === 1 || tw === 2 || tw === 3) grass++;",
        "  if (te === 0 || te === 1 || te === 2 || te === 3) grass++;",
        "  if (tn === 62 || tn === 8) pier++; if (ts === 62 || ts === 8) pier++;",
        "  if (tw === 62 || tw === 8) pier++; if (te === 62 || te === 8) pier++;",
        "  if (tn === 5 || tn === 33 || tn === 22 || tn === 23) cob++;",
        "  if (ts === 5 || ts === 33 || ts === 22 || ts === 23) cob++;",
        "  if (tw === 5 || tw === 33 || tw === 22 || tw === 23) cob++;",
        "  if (te === 5 || te === 33 || te === 22 || te === 23) cob++;",
        "  if (tn === 32) aut++; if (ts === 32) aut++; if (tw === 32) aut++; if (te === 32) aut++;",
        "  if (tn === 55 || tn === 47) sand++; if (ts === 55 || ts === 47) sand++;",
        "  if (tw === 55 || tw === 47) sand++; if (te === 55 || te === 47) sand++;",
        "  if (tn === 4) dirt++; if (ts === 4) dirt++; if (tw === 4) dirt++; if (te === 4) dirt++;",
        "  let best = 0, bestCnt = grass;",
        "  if (dirt > bestCnt || (dirt === bestCnt && dirt > 0)) { best = 4; bestCnt = dirt; }",
        "  if (sand > bestCnt || (sand === bestCnt && sand > 0)) { best = 6; bestCnt = sand; }",
        "  if (aut  > bestCnt || (aut  === bestCnt && aut  > 0)) { best = 3; bestCnt = aut;  }",
        "  if (cob  > bestCnt || (cob  === bestCnt && cob  > 0)) { best = 1; bestCnt = cob;  }",
        "  if (pier > bestCnt || (pier === bestCnt && pier > 0)) { best = 2; bestCnt = pier; }",
        "  return best;",
        "}",
        "function isShoreLand(t: number, isWorld: boolean): boolean {",
        "  if (isWaterLike(t) || t === 59 || t === 62) return false;",
        "  if (!isWorld && (t === 8 || t === 9 || t === 10)) return false;",
        "  return true;",
        "}",
        "",
        "export function getLocalTileFlags(bg1: ArrayLike<number>, mx: number, my: number, mw: number, mh: number): number {",
        "  const t = bg1[my * mw + mx];",
        "  let flags = 0;",
        "  if (t === 6 || t === 7 || t === 34) {",
        "    let landW = false, landE = false;",
        "    let runN = 0, runS = 0;",
        "    for (let d = 1; d <= 4; d++) {",
        "      if (my >= d) {",
        "        const c = bg1[(my - d) * mw + mx];",
        "        if (isWaterLike(c) || c === 8 || c === 9 || c === 10) runN++;",
        "        else break;",
        "      }",
        "    }",
        "    for (let d = 1; d <= 4; d++) {",
        "      if (my + d < mh) {",
        "        const c = bg1[(my + d) * mw + mx];",
        "        if (isWaterLike(c) || c === 8 || c === 9 || c === 10) runS++;",
        "        else break;",
        "      }",
        "    }",
        "    for (let d = 1; d <= 4; d++) {",
        "      if (mx >= d && isShoreLand(bg1[my * mw + (mx - d)], false)) landW = true;",
        "      if (mx + d < mw && isShoreLand(bg1[my * mw + (mx + d)], false)) landE = true;",
        "    }",
        "    if (landW && landE && (runN + 1 + runS) >= 5) {",
        "      flags |= 2;",
        "    }",
        "  } else if (t === 8) {",
        "    const tn = my >= 1 ? bg1[(my - 1) * mw + mx] : t;",
        "    const ts = my + 1 < mh ? bg1[(my + 1) * mw + mx] : t;",
        "    const wn = my >= 1 && isWaterLike(tn);",
        "    const ws = my + 1 < mh && isWaterLike(ts);",
        "    const railNs = tn === 9 || tn === 10 || ts === 9 || ts === 10;",
        "    const roadNs = tn === 8 || ts === 8 || tn === 5 || ts === 5 || tn === 33 || ts === 33 || tn === 4 || ts === 4 || tn === 22 || ts === 22;",
        "    if (!railNs) {",
        "      if (ws && !wn) {",
        "        flags |= 4;",
        "      } else if (!wn && ts === 8 && my + 2 < mh && isWaterLike(bg1[(my + 2) * mw + mx])) {",
        "        flags |= 4;",
        "      } else if (roadNs) {",
        "        let ww = false, we = false;",
        "        for (let d = 1; d <= 4; d++) {",
        "          if (mx >= d) {",
        "            const c = bg1[my * mw + (mx - d)];",
        "            if (isWaterLike(c)) { ww = true; break; }",
        "            if (c !== 8) break;",
        "          }",
        "        }",
        "        for (let d = 1; d <= 4; d++) {",
        "          if (mx + d < mw) {",
        "            const c = bg1[my * mw + (mx + d)];",
        "            if (isWaterLike(c)) { we = true; break; }",
        "            if (c !== 8) break;",
        "          }",
        "        }",
        "        if ((ww || we) && ((!wn && !ws) || (ww && we))) {",
        "          flags |= 4;",
        "        }",
        "      }",
        "    }",
        "  }",
        "  return flags;",
        "}",
        "",
        "export function resolveChronoMeta(",
        "  t: number, ov: number,",
        "  tn: number, ts: number, tw: number, te: number,",
        "  tnw: number, tne: number, tsw: number, tse: number,",
        "  mx: number, my: number, flagsOrWorld: boolean | number",
        "): number {",
        "  const flags = typeof flagsOrWorld === 'boolean' ? (flagsOrWorld ? 1 : 0) : flagsOrWorld;",
        "  const isWorld = (flags & 1) !== 0;",
        "  const isNsRiver = (flags & 2) !== 0;",
        "  const isNsBridge = (flags & 4) !== 0;",
        "  const v = (((mx * 3 + my * 5) ^ (mx >> 1)) & 3);",
        "  if (t === 63) return CM_LAMP_UNLIT;",
        "  if (t === 48) {",
        "    if ((isWorld && my < 40) || tn === 56 || ts === 56 || tw === 56 || te === 56) return CM_PEAK_SNOW;",
        "    if (isWorld && mx < 68 && my >= 36 && my < 90) return CM_PEAK_VOLCANO;",
        "    return 48;",
        "  }",
        "  if (t === 61) {",
        "    const gc = (isGrassLike(tn) ? 1 : 0) + (isGrassLike(ts) ? 1 : 0) + (isGrassLike(tw) ? 1 : 0) + (isGrassLike(te) ? 1 : 0);",
        "    const sc = ((tn === 47 || tn === 55) ? 1 : 0) + ((ts === 47 || ts === 55) ? 1 : 0) + ((tw === 47 || tw === 55) ? 1 : 0) + ((te === 47 || te === 55) ? 1 : 0);",
        "    if (gc >= 1 && gc >= sc) return CM_PALM_GRASS;",
        "    return 61;",
        "  }",
        "  if (t === 26 || t === 25 || t === 27 || t === 13 || t === 11 || t === 31) {",
        "    const gnd = inferPropGround(tn, ts, tw, te);",
        "    if (gnd === 5) {",
        "      if (t === 26) return CM_LAMP_WOOD;",
        "      if (t === 27 || t === 13) return CM_FLOWERBED_WOOD;",
        "      if (t === 11) return CM_COUNTER_WOOD;",
        "      if (t === 25) return CM_LECTERN_WOOD;",
        "    } else if (gnd === 1) {",
        "      if (t === 26) return CM_LAMP_COBBLE;",
        "      if (t === 25) return CM_SIGN_COBBLE;",
        "      if (t === 27) return CM_FLOWERBED_COBBLE;",
        "      if (t === 13) return CM_BUSH_COBBLE;",
        "      if (t === 11) return CM_FENCE_COBBLE;",
        "      if (t === 31) return CM_BOULDER_COBBLE;",
        "    } else if (gnd === 2) {",
        "      if (t === 26) return CM_LAMP_PIER;",
        "      if (t === 25) return CM_SIGN_PIER;",
        "      if (t === 31) return CM_BOULDER_PIER;",
        "    } else if (gnd === 3) {",
        "      if (t === 26) return CM_LAMP_AUTUMN;",
        "      if (t === 25) return CM_SIGN_AUTUMN;",
        "      if (t === 31) return CM_BOULDER_AUTUMN;",
        "    } else if (gnd === 6) {",
        "      if (t === 26) return CM_LAMP_DIRT;",
        "      if (t === 25) return CM_SIGN_DIRT;",
        "      if (t === 31) return CM_BOULDER_SAND;",
        "    } else if (gnd === 4) {",
        "      if (t === 26) return CM_LAMP_DIRT;",
        "      if (t === 25) return CM_SIGN_DIRT;",
        "    }",
        "    return t;",
        "  }",
        "  if (t === 45 && (ts === 45 || ts === 28 || ts === 15 || ts === 40 || ts === 14)) return CM_INTERIOR_CAP;",
        "  if (t === 41 && tn === 41) return CM_BED_FOOT;",
        "  if (!isWorld && t === 8 && isNsBridge) return CM_BRIDGE_NS;",
        "  if (t === 62 && isWaterLike(ts)) return CM_PIER_WATER_EDGE;",
        "  if (t === 6 || t === 7 || t === 34 || t === 54 || t === 46 || t === 59) {",
        "    const ln  = isShoreLand(tn, isWorld);",
        "    const ls  = isShoreLand(ts, isWorld);",
        "    const lw  = isShoreLand(tw, isWorld);",
        "    const le  = isShoreLand(te, isWorld);",
        "    const lnw = isShoreLand(tnw, isWorld);",
        "    const lne = isShoreLand(tne, isWorld);",
        "    const lsw = isShoreLand(tsw, isWorld);",
        "    const lse = isShoreLand(tse, isWorld);",
        "    if (t === 46) {",
        "      const sn  = tn === 54 || tn === 59;",
        "      const ss  = ts === 54 || ts === 59;",
        "      const sw  = tw === 54 || tw === 59;",
        "      const se  = te === 54 || te === 59;",
        "      const snw = tnw === 54 || tnw === 59;",
        "      const sne = tne === 54 || tne === 59;",
        "      const ssw = tsw === 54 || tsw === 59;",
        "      const sse = tse === 54 || tse === 59;",
        "      if (ln || ls || lw || le) {",
        "        if (ln && lw) return CM_OCEAN_COAST_NW;",
        "        if (ln && le) return CM_OCEAN_COAST_NE;",
        "        if (ls && lw) return CM_OCEAN_COAST_SW;",
        "        if (ls && le) return CM_OCEAN_COAST_SE;",
        "        if (ln) return CM_OCEAN_COAST_N;",
        "        if (ls) return CM_OCEAN_COAST_S;",
        "        if (lw) return CM_OCEAN_COAST_W;",
        "        if (le) return CM_OCEAN_COAST_E;",
        "      }",
        "      if (sn || ss || sw || se) {",
        "        if (sn && sw) return CM_SHOAL_NW;",
        "        if (sn && se) return CM_SHOAL_NE;",
        "        if (ss && sw) return CM_SHOAL_SW;",
        "        if (ss && se) return CM_SHOAL_SE;",
        "        if (sn) return CM_SHOAL_N;",
        "        if (ss) return CM_SHOAL_S;",
        "        if (sw) return CM_SHOAL_W;",
        "        if (se) return CM_SHOAL_E;",
        "      }",
        "      if (lnw) return CM_OCEAN_COAST_INW;",
        "      if (lne) return CM_OCEAN_COAST_INE;",
        "      if (lsw) return CM_OCEAN_COAST_ISW;",
        "      if (lse) return CM_OCEAN_COAST_ISE;",
        "      if (snw) return CM_SHOAL_INW;",
        "      if (sne) return CM_SHOAL_INE;",
        "      if (ssw) return CM_SHOAL_ISW;",
        "      if (sse) return CM_SHOAL_ISE;",
        "      if (v & 1) return CM_OCEAN_V1;",
        "      return t;",
        "    }",
        "    if (ln || ls || lw || le || lnw || lne || lsw || lse) {",
        "      const useSandy = isWorld || ((t === 54 || t === 59) && !isGrassLike(tn) && !isGrassLike(ts) && !isGrassLike(tw) && !isGrassLike(te));",
        "      if (useSandy) {",
        "        if (ln && lw) return CM_WORLD_COAST_NW;",
        "        if (ln && le) return CM_WORLD_COAST_NE;",
        "        if (ls && lw) return CM_WORLD_COAST_SW;",
        "        if (ls && le) return CM_WORLD_COAST_SE;",
        "        if (ln) return CM_WORLD_COAST_N;",
        "        if (ls) return CM_WORLD_COAST_S;",
        "        if (lw) return CM_WORLD_COAST_W;",
        "        if (le) return CM_WORLD_COAST_E;",
        "        if (lnw) return CM_WORLD_COAST_INW;",
        "        if (lne) return CM_WORLD_COAST_INE;",
        "        if (lsw) return CM_WORLD_COAST_ISW;",
        "        if (lse) return CM_WORLD_COAST_ISE;",
        "      } else {",
        "        if (ln && ls) return CM_SHORE_NS;",
        "        if (lw && le) return CM_SHORE_WE;",
        "        if (ln && lw) return CM_SHORE_NW;",
        "        if (ln && le) return CM_SHORE_NE;",
        "        if (ls && lw) return CM_SHORE_SW;",
        "        if (ls && le) return CM_SHORE_SE;",
        "        if (ln) return CM_SHORE_N;",
        "        if (ls) return CM_SHORE_S;",
        "        if (lw) return isNsRiver ? CM_SHORE_V_W : CM_SHORE_W;",
        "        if (le) return isNsRiver ? CM_SHORE_V_E : CM_SHORE_E;",
        "        if (lnw) return CM_SHORE_INW;",
        "        if (lne) return CM_SHORE_INE;",
        "        if (lsw) return CM_SHORE_ISW;",
        "        if (lse) return CM_SHORE_ISE;",
        "      }",
        "    }",
        "    if (!isWorld && isNsRiver) {",
        "      if (t === 34) return CM_RAPIDS_V;",
        "      if (t === 6) return CM_WATER_V;",
        "    }",
        "    return t;",
        "  }",
        "  if (t === 47 || t === 55) {",
        "    const gn = isGrassLike(tn);",
        "    const gs = isGrassLike(ts);",
        "    const gw = isGrassLike(tw);",
        "    const ge = isGrassLike(te);",
        "    if (gn && gw) return CM_SAND_GRASS_NW;",
        "    if (gn && ge) return CM_SAND_GRASS_NE;",
        "    if (gs && gw) return CM_SAND_GRASS_SW;",
        "    if (gs && ge) return CM_SAND_GRASS_SE;",
        "    if (gn) return CM_SAND_GRASS_N;",
        "    if (gs) return CM_SAND_GRASS_S;",
        "    if (gw) return CM_SAND_GRASS_W;",
        "    if (ge) return CM_SAND_GRASS_E;",
        "    if (t === 55 && (v & 1)) return CM_SAND_V1;",
        "    return t;",
        "  }",
        "  if (!isWorld) {",
        "    if (t === 5) {",
        "      const gn = isGrassLike(tn);",
        "      const gs = isGrassLike(ts);",
        "      const gw = isGrassLike(tw);",
        "      const ge = isGrassLike(te);",
        "      if (isSolidCaster(tn)) return CM_COBBLE_SHAD_N;",
        "      if (isSolidCaster(tw)) return CM_COBBLE_SHAD_W;",
        "      if (gn && gw) return CM_COBBLE_CORNER_NW;",
        "      if (gn && ge) return CM_COBBLE_CORNER_NE;",
        "      if (gs && gw) return CM_COBBLE_CORNER_SW;",
        "      if (gs && ge) return CM_COBBLE_CORNER_SE;",
        "      if (gn) return CM_COBBLE_EDGE_N;",
        "      if (gs) return CM_COBBLE_EDGE_S;",
        "      if (gw) return CM_COBBLE_EDGE_W;",
        "      if (ge) return CM_COBBLE_EDGE_E;",
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
        "      const gn = isGrassLike(tn);",
        "      const gs = isGrassLike(ts);",
        "      const gw = isGrassLike(tw);",
        "      const ge = isGrassLike(te);",
        "      if (isSolidCaster(tn)) return CM_DIRT_SHAD_N;",
        "      if (gn && gw) return CM_DIRT_CORNER_NW;",
        "      if (gn && ge) return CM_DIRT_CORNER_NE;",
        "      if (gs && gw) return CM_DIRT_CORNER_SW;",
        "      if (gs && ge) return CM_DIRT_CORNER_SE;",
        "      if (gn) return CM_DIRT_EDGE_N;",
        "      if (gs) return CM_DIRT_EDGE_S;",
        "      if (gw) return CM_DIRT_EDGE_W;",
        "      if (ge) return CM_DIRT_EDGE_E;",
        "      return (v & 1) ? CM_DIRT_V1 : 4;",
        "    }",
        "    if (t === 33) {",
        "      const gn = isGrassLike(tn);",
        "      const gs = isGrassLike(ts);",
        "      const gw = isGrassLike(tw);",
        "      const ge = isGrassLike(te);",
        "      if (isSolidCaster(tn)) return CM_ROCKY_SHAD_N;",
        "      if (gn && gw) return CM_COBBLE_CORNER_NW;",
        "      if (gn && ge) return CM_COBBLE_CORNER_NE;",
        "      if (gs && gw) return CM_COBBLE_CORNER_SW;",
        "      if (gs && ge) return CM_COBBLE_CORNER_SE;",
        "      if (gn) return CM_COBBLE_EDGE_N;",
        "      if (gs) return CM_COBBLE_EDGE_S;",
        "      if (gw) return CM_COBBLE_EDGE_W;",
        "      if (ge) return CM_COBBLE_EDGE_E;",
        "      return (v & 1) ? CM_ROCKY_V1 : 33;",
        "    }",
        "    if (t === 38) {",
        "      if (isSolidCaster(tn)) return CM_WOOD_SHAD_N;",
        "      return (v & 1) ? CM_WOOD_FLOOR_V1 : 38;",
        "    }",
        "    if (t === 22 && (v & 1)) return CM_SHRINE_V1;",
        "  } else {",
        "    if (t === 0 && v > 0) return CM_GRASS_V1 + (v - 1);",
        "    if (t === 56 && (v & 1)) return CM_SNOW_V1;",
        "    if (t === 58 && (v & 1)) return CM_JUNGLE_V1;",
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

    snes_tiles, snes_anim, snes_meta = compile_console_bank(frames_meta, max_tiles=510, is_genesis=False)
    gen_tiles,  gen_anim,  gen_meta  = compile_console_bank(frames_meta, max_tiles=399, is_genesis=True)

    emit_snes_asm(snes_tiles, snes_anim)
    emit_c_header(zone_pals, gen_pals, frames_meta, snes_tiles, snes_anim, snes_meta, gen_tiles, gen_anim, gen_meta)
    emit_web_ts(zone_pals, frames_meta)

    print(f"[OK] Baked Chrono/Mana Unified 16-Bit Tileset:")
    print(f"     Metatiles: {len(META_NAMES)}  |  Biome Moods: {len(zone_pals)} x 4 sub-palettes (60 colors!)")
    print(f"     SNES:    {len(snes_tiles)} / 510 BG1 tiles ({len(snes_tiles)*32} B ROM) + {len(snes_anim)} anim tiles ({len(snes_anim)*32} B VBlank DMA)")
    print(f"     Genesis: {len(gen_tiles)} / 399 BG tiles ({len(gen_tiles)*32} B ROM) + {len(gen_anim)} anim tiles ({len(gen_anim)*32} B VBlank DMA)")
    print(f"     Emitted: {os.path.relpath(OUT_H, ROOT)}, {os.path.relpath(OUT_ASM, ROOT)}, {os.path.relpath(OUT_TS, ROOT)}")

if __name__ == "__main__":
    main()
