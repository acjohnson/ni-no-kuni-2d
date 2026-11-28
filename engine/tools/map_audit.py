#!/usr/bin/env python3
"""
map_audit.py — Project Hearth 6-Pass Automated World, Autotile & Seam Auditor
               + Clean & Annotated 16-Bit Pixel-Art PNG Map Exporter

Runs over the exact C99 map output produced by map_audit_dump.c AND the exact
baked 16x16 tileset pixel matrices & 60-color palettes from bake_chrono_tileset.py:
  Pass 1: Full BFS Traversability & Reachability (all 23 zones + 7 globe continents)
  Pass 2: Collision <-> Visual Semantic Parity
  Pass 3: Autotile Mask Completeness, River Axis/Bank Orientation, Cliffs, Bridges, Props
  Pass 4: Pixel-Level Border Seam Continuity & 4-Frame Optical Water Flow Vector Match
  Pass 5: BG2 Overhead Canopy / Roof Eave Structural Anchor Audit
  Pass 6: Clean & Color-Coded Annotated PNG Map Exports + Structured JSON/CLI Report

Zero external dependencies (uses Python stdlib zlib + struct for fast PNG export).
"""

import argparse
import collections
import hashlib
import importlib.util
import json
import math
import os
import re
import struct
import subprocess
import sys
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BAKER_PATH = os.path.join(ROOT, "engine", "tools", "bake_chrono_tileset.py")
BASELINE_PATH = os.path.join(ROOT, "engine", "tools", "map_audit_baseline.json")
VISION_SCRIPT = os.path.join(ROOT, "engine", "tools", "map_audit_vision.py")

# --- Logical Tile IDs (mirror engine/core/hearth_data.h) ---
T_NAMES = {
    0: "T_GRASS", 1: "T_GRASS_ALT", 2: "T_FLOWERS", 3: "T_TALL_GRASS",
    4: "T_DIRT", 5: "T_COBBLE", 6: "T_WATER", 7: "T_FORD",
    8: "T_BRIDGE", 9: "T_BRIDGE_RAIL_N", 10: "T_BRIDGE_RAIL_S", 11: "T_FENCE",
    12: "T_TREE_TRUNK", 13: "T_BUSH", 14: "T_WALL_WOOD", 15: "T_WALL_WINDOW",
    16: "T_WALL_DOOR", 17: "T_ROOF_BODY", 18: "T_ROOF_TOP", 19: "T_WINDMILL_WALL",
    20: "T_WINDMILL_DOOR", 21: "T_WINDMILL_ROOF", 22: "T_SHRINE_FLOOR", 23: "T_SHRINE_RUNE",
    24: "T_SHRINE_PILLAR", 25: "T_SIGNPOST", 26: "T_LANTERN_POST", 27: "T_FLOWER_BED",
    28: "T_MTN_SKY", 29: "T_CLIFF_WALL", 30: "T_CLIFF_LEDGE", 31: "T_BOULDER",
    32: "T_AUTUMN_GRASS", 33: "T_ROCKY_PATH", 34: "T_RAPIDS", 35: "T_RAIL_TRACK",
    36: "T_MAPLE_TRUNK", 37: "T_PINE_TRUNK", 38: "T_WOOD_FLOOR", 39: "T_HEARTH_RUG",
    40: "T_FIREPLACE", 41: "T_BED_QUILT", 42: "T_ATTIC_CHEST", 43: "T_HOLLOW_OAK",
    44: "T_STONE_STAIRS", 45: "T_INTERIOR_WALL", 46: "T_WORLD_OCEAN", 47: "T_WORLD_BEACH",
    48: "T_WORLD_PEAK", 49: "T_WORLD_FOREST", 50: "T_WORLD_HILL", 51: "T_WORLD_CASTLE",
    52: "T_WORLD_TOWN", 53: "T_WORLD_RUINS", 54: "T_WORLD_SHALLOW", 55: "T_WORLD_SAND",
    56: "T_WORLD_SNOW", 57: "T_WORLD_LAVA", 58: "T_WORLD_JUNGLE", 59: "T_WORLD_CORAL",
    60: "T_WORLD_TECH", 61: "T_PALM_TREE", 62: "T_DOCK_PIER", 63: "T_LANTERN_UNLIT",
}

T_GRASS, T_GRASS_ALT, T_FLOWERS, T_TALL_GRASS = 0, 1, 2, 3
T_DIRT, T_COBBLE, T_WATER, T_FORD = 4, 5, 6, 7
T_BRIDGE, T_BRIDGE_RAIL_N, T_BRIDGE_RAIL_S, T_FENCE = 8, 9, 10, 11
T_TREE_TRUNK, T_BUSH, T_WALL_WOOD, T_WALL_WINDOW = 12, 13, 14, 15
T_WALL_DOOR, T_ROOF_BODY, T_ROOF_TOP, T_WINDMILL_WALL = 16, 17, 18, 19
T_WINDMILL_DOOR, T_WINDMILL_ROOF, T_SHRINE_FLOOR, T_SHRINE_RUNE = 20, 21, 22, 23
T_SHRINE_PILLAR, T_SIGNPOST, T_LANTERN_POST, T_FLOWER_BED = 24, 25, 26, 27
T_MTN_SKY, T_CLIFF_WALL, T_CLIFF_LEDGE, T_BOULDER = 28, 29, 30, 31
T_AUTUMN_GRASS, T_ROCKY_PATH, T_RAPIDS, T_RAIL_TRACK = 32, 33, 34, 35
T_MAPLE_TRUNK, T_PINE_TRUNK, T_WOOD_FLOOR, T_HEARTH_RUG = 36, 37, 38, 39
T_FIREPLACE, T_BED_QUILT, T_ATTIC_CHEST, T_HOLLOW_OAK = 40, 41, 42, 43
T_STONE_STAIRS, T_INTERIOR_WALL, T_WORLD_OCEAN, T_WORLD_BEACH = 44, 45, 46, 47
T_WORLD_PEAK, T_WORLD_FOREST, T_WORLD_HILL, T_WORLD_CASTLE = 48, 49, 50, 51
T_WORLD_TOWN, T_WORLD_RUINS, T_WORLD_SHALLOW, T_WORLD_SAND = 52, 53, 54, 55
T_WORLD_SNOW, T_WORLD_LAVA, T_WORLD_JUNGLE, T_WORLD_CORAL = 56, 57, 58, 59
T_WORLD_TECH, T_PALM_TREE, T_DOCK_PIER, T_LANTERN_UNLIT = 60, 61, 62, 63

O_NONE, O_CANOPY_TL, O_CANOPY_TR, O_CANOPY_BL, O_CANOPY_BR = 0, 1, 2, 3, 4
O_ROOF_EAVE, O_SHRINE_ARCH, O_MAPLE_TL, O_MAPLE_TR = 5, 6, 7, 8
O_PINE_TOP, O_OAK_CROWN, O_MAPLE_MID, O_MAPLE_TOP = 9, 10, 11, 12

COL_WALKABLE, COL_SOLID, COL_TALL_GRASS = 0, 1, 2
COL_SHALLOW, COL_WATER, COL_LAVA = 3, 4, 5

WATER_TILES = {T_WATER, T_FORD, T_RAPIDS, T_WORLD_OCEAN, T_WORLD_SHALLOW}
AQUATIC_TILES = WATER_TILES | {T_WORLD_CORAL}
GRASS_LIKE = {T_GRASS, T_GRASS_ALT, T_FLOWERS, T_TALL_GRASS, T_AUTUMN_GRASS}
PROP_TILES = {
    T_LANTERN_POST, T_LANTERN_UNLIT, T_SIGNPOST, T_FLOWER_BED, T_BUSH, T_FENCE,
    T_BOULDER, T_TREE_TRUNK, T_MAPLE_TRUNK, T_PINE_TRUNK, T_PALM_TREE,
}
WORLD_ICON_TILES = {T_WORLD_CASTLE, T_WORLD_TOWN, T_WORLD_RUINS}
BIOME_FEATURE_TILES = {T_WORLD_HILL, T_WORLD_PEAK, T_FLOWERS, T_SHRINE_PILLAR}
WALKABLE_GROUND = {
    T_GRASS, T_GRASS_ALT, T_FLOWERS, T_TALL_GRASS, T_AUTUMN_GRASS, T_DIRT,
    T_COBBLE, T_ROCKY_PATH, T_SHRINE_FLOOR, T_SHRINE_RUNE, T_WOOD_FLOOR,
    T_HEARTH_RUG, T_WORLD_BEACH, T_WORLD_SAND, T_WORLD_SNOW, T_WORLD_JUNGLE,
    T_WORLD_TECH, T_DOCK_PIER, T_BRIDGE,
}


def ground_family(t):
    if t in (T_GRASS, T_GRASS_ALT, T_FLOWERS, T_TALL_GRASS): return "GRASS"
    if t == T_AUTUMN_GRASS: return "AUTUMN"
    if t == T_DIRT: return "DIRT"
    if t in (T_COBBLE, T_ROCKY_PATH, T_SHRINE_FLOOR, T_SHRINE_RUNE): return "STONE"
    if t in (T_WOOD_FLOOR, T_HEARTH_RUG): return "WOOD"
    if t in (T_WORLD_BEACH, T_WORLD_SAND): return "SAND"
    if t == T_WORLD_SNOW: return "SNOW"
    if t == T_WORLD_JUNGLE: return "JUNGLE"
    if t == T_WORLD_TECH: return "TECH"
    if t in (T_DOCK_PIER, T_BRIDGE): return "PIER"
    return None

# Expected collision classes for semantic parity check
EXPECT_WALKABLE = {
    T_GRASS, T_GRASS_ALT, T_FLOWERS, T_DIRT, T_COBBLE, T_BRIDGE,
    T_SHRINE_FLOOR, T_SHRINE_RUNE, T_AUTUMN_GRASS, T_ROCKY_PATH,
    T_WOOD_FLOOR, T_HEARTH_RUG, T_STONE_STAIRS, T_WORLD_BEACH,
    T_WORLD_HILL, T_WORLD_SAND, T_WORLD_SNOW, T_WORLD_JUNGLE,
    T_WORLD_TECH, T_DOCK_PIER,
}
EXPECT_SOLID = {
    T_BRIDGE_RAIL_N, T_BRIDGE_RAIL_S, T_FENCE, T_TREE_TRUNK, T_BUSH,
    T_WALL_WOOD, T_WALL_WINDOW, T_ROOF_BODY, T_ROOF_TOP,
    T_WINDMILL_WALL, T_WINDMILL_ROOF, T_SHRINE_PILLAR,
    T_CLIFF_WALL, T_CLIFF_LEDGE, T_BOULDER, T_MAPLE_TRUNK, T_PINE_TRUNK,
    T_FIREPLACE, T_HOLLOW_OAK, T_INTERIOR_WALL, T_MTN_SKY,
    T_WORLD_PEAK, T_WORLD_FOREST, T_WORLD_CASTLE, T_WORLD_TOWN,
    T_WORLD_RUINS, T_PALM_TREE, T_LANTERN_UNLIT,
}

# Unified World Map Palette: all 7 continents share ZPAL_EVERTALE_WORLD (10) so sailing/flying
# between islands never snaps the hardware palette mid-travel!
GLOBE_UNIFIED_ZPAL = 10
SHALLOW_TILES = {T_WORLD_SHALLOW, T_WORLD_CORAL}

# Bounding boxes (tx0, ty0, tx1, ty1) for per-continent zoomed PNG crops
CONTINENT_CROPS = {
    0: ("00_heartland",  64,  58, 152, 120),
    1: ("01_desert",    110, 126, 174, 178),
    2: ("02_paradise",   14, 126, 110, 174),
    3: ("03_frostmere",  56,   2, 200,  38),
    4: ("04_canopy",    186,  98, 254, 158),
    5: ("05_volcano",     6,  38,  64,  86),
    6: ("06_aetheris",  192,  38, 256,  86),
}

# Severity & Category Annotation Colors (R, G, B)
CAT_COLORS = {
    "BLOCKED":   (240,  40,  40),  # Red
    "COLLISION": (255, 140,  20),  # Orange
    "AUTOTILE":  (250, 225,  30),  # Yellow
    "FLOW":      ( 40, 235, 255),  # Cyan
    "SEAM":      (235,  65, 240),  # Magenta
    "OVERLAY":   (120, 255,  90),  # Lime
    "CONSOLE":   ( 90, 210, 255),  # Sky Blue (SNES / Genesis hardware & parity)
}


# ============================================================================
# Zero-Dependency Fast PNG Writer (RGBA / RGB)
# ============================================================================
def write_png_rgb(path, width, height, rgb_bytes):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    stride = width * 3
    raw = bytearray()
    for y in range(height):
        raw.append(0)  # Filter type 0 (None)
        off = y * stride
        raw.extend(rgb_bytes[off:off + stride])

    def chunk(tag, data):
        crc = zlib.crc32(tag)
        crc = zlib.crc32(data, crc) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    idat = zlib.compress(bytes(raw), level=6)
    with open(path, "wb") as fp:
        fp.write(b"\x89PNG\r\n\x1a\n")
        fp.write(chunk(b"IHDR", ihdr))
        fp.write(chunk(b"IDAT", idat))
        fp.write(chunk(b"IEND", b""))


def draw_rect_outline(buf, img_w, img_h, x0, y0, w, h, rgb, thick=1):
    r, g, b = rgb
    for t in range(thick):
        for x in range(max(0, x0 + t), min(img_w, x0 + w - t)):
            for yy in (y0 + t, y0 + h - 1 - t):
                if 0 <= yy < img_h:
                    off = (yy * img_w + x) * 3
                    buf[off] = r; buf[off + 1] = g; buf[off + 2] = b
        for y in range(max(0, y0 + t), min(img_h, y0 + h - t)):
            for xx in (x0 + t, x0 + w - 1 - t):
                if 0 <= xx < img_w:
                    off = (y * img_w + xx) * 3
                    buf[off] = r; buf[off + 1] = g; buf[off + 2] = b


def tint_tile(buf, img_w, img_h, x0, y0, size, rgb, alpha=0.35):
    r, g, b = rgb
    inv = 1.0 - alpha
    for y in range(max(0, y0), min(img_h, y0 + size)):
        row = y * img_w * 3
        for x in range(max(0, x0), min(img_w, x0 + size)):
            off = row + x * 3
            buf[off]     = int(buf[off] * inv + r * alpha)
            buf[off + 1] = int(buf[off + 1] * inv + g * alpha)
            buf[off + 2] = int(buf[off + 2] * inv + b * alpha)


# ============================================================================
# Load Actual Baked 16x16 Tileset & Palettes directly from bake_chrono_tileset.py
# ============================================================================
def load_tileset_module():
    spec = importlib.util.spec_from_file_location("bake_chrono_tileset", BAKER_PATH)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class FlatTile:
    __slots__ = ("px", "qpal", "pal")
    def __init__(self, tile16):
        flat = []
        for row in tile16.px:
            flat.extend(row)
        self.px = flat
        self.qpal = list(tile16.qpal)
        self.pal = tile16.qpal[0]


def quantize_gen_cram_rgb(hex_rgb):
    """Exact Sega Genesis 9-bit BGR333 CRAM quantization (3 bits per channel -> 0..255)."""
    r3 = ((hex_rgb >> 16) & 0xFF) >> 5
    g3 = ((hex_rgb >> 8) & 0xFF) >> 5
    b3 = (hex_rgb & 0xFF) >> 5
    return ((r3 * 255) // 7, (g3 * 255) // 7, (b3 * 255) // 7)


def reconstruct_gen_metatile(gen_tiles, qlist):
    """Rebuild a 16x16 FlatTile from Genesis 4x(tid, pal, hf, vf, prio) quadrant descriptors."""
    px = [0] * 256
    qpal = [0, 0, 0, 0]
    for qidx, (tid, pal, hf, vf, _prio) in enumerate(qlist):
        qpal[qidx] = pal & 1
        q8 = gen_tiles[tid]
        qx = (qidx & 1) * 8
        qy = (qidx >> 1) * 8
        for y in range(8):
            sy = (7 - y) if vf else y
            for x in range(8):
                sx = (7 - x) if hf else x
                px[(qy + y) * 16 + (qx + x)] = q8[sy * 8 + sx]
    ft = FlatTile.__new__(FlatTile)
    ft.px = px
    ft.qpal = qpal
    ft.pal = qpal[0]
    return ft


def quantize_snes_cgram_rgb(hex_rgb):
    """Exact SNES 15-bit BGR555 CGRAM quantization (5 bits per channel -> 0..255)."""
    r5 = ((hex_rgb >> 16) & 0xFF) >> 3
    g5 = ((hex_rgb >> 8) & 0xFF) >> 3
    b5 = (hex_rgb & 0xFF) >> 3
    return ((r5 << 3) | (r5 >> 2), (g5 << 3) | (g5 >> 2), (b5 << 3) | (b5 >> 2))


def reconstruct_snes_metatile(snes_tiles, qlist):
    """Rebuild a 16x16 FlatTile from SNES 4x(tid, pal, hf, vf, prio) quadrant descriptors."""
    px = [0] * 256
    qpal = [0, 0, 0, 0]
    for qidx, (tid, pal, hf, vf, _prio) in enumerate(qlist):
        qpal[qidx] = pal & 3
        q8 = snes_tiles[tid]
        qx = (qidx & 1) * 8
        qy = (qidx >> 1) * 8
        for y in range(8):
            sy = (7 - y) if vf else y
            for x in range(8):
                sx = (7 - x) if hf else x
                px[(qy + y) * 16 + (qx + x)] = q8[sy * 8 + sx]
    ft = FlatTile.__new__(FlatTile)
    ft.px = px
    ft.qpal = qpal
    ft.pal = qpal[0]
    return ft


def build_tileset_cache(baker):
    zone_pals = baker.build_zone_palettes()  # dict {0..17: [4][16] of 0xRRGGBB}
    gen_pals = baker.build_genesis_palettes(zone_pals)  # list {0..17: [2][16] of 0xRRGGBB}
    meta_names = list(baker.META_NAMES)
    name_to_id = {n: i for i, n in enumerate(meta_names)}
    frames = [baker.build_all_metatiles(f) for f in range(4)]

    # Pre-unpack SNES/SDL2/Web 60-color palette RGB tuples: pal_rgb[zpal][subpal][ci] -> (r,g,b)
    pal_rgb = []
    snes_pal_rgb = []
    for zi in range(len(zone_pals)):
        zp = zone_pals[zi]
        sp_list = []
        snes_sp_list = []
        for sp in zp:
            sp_list.append([((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF) for c in sp])
            snes_sp_list.append([quantize_snes_cgram_rgb(c) for c in sp])
        pal_rgb.append(sp_list)
        snes_pal_rgb.append(snes_sp_list)

    # Pre-unpack exact Sega Genesis 9-bit CRAM palettes: gen_pal_rgb[zpal][0..1][ci] -> (r,g,b)
    gen_pal_rgb = []
    for zi in range(len(gen_pals)):
        gp = gen_pals[zi]
        gen_pal_rgb.append([[quantize_gen_cram_rgb(c) for c in sp] for sp in gp])

    # Map meta_id -> FlatTile for frames 0..3 (PC/Web master)
    meta_tiles = []
    for f in range(4):
        meta_tiles.append([FlatTile(t) for t in frames[f]])

    # Compile exact SNES 4bpp 8x8 tilebank (<= 510 tiles) & reconstruct SNES 16x16 metatiles
    snes_tiles, _snes_anim, snes_meta_quads = baker.compile_console_bank(frames, max_tiles=510, is_genesis=False)
    snes_meta_tiles = [reconstruct_snes_metatile(snes_tiles, qlist) for qlist in snes_meta_quads]

    # Compile exact Sega Genesis 8x8 tilebank (<= 399 tiles) & reconstruct Genesis 16x16 metatiles
    gen_tiles, _gen_anim, gen_meta_quads = baker.compile_console_bank(frames, max_tiles=399, is_genesis=True)
    gen_meta_tiles = [reconstruct_gen_metatile(gen_tiles, qlist) for qlist in gen_meta_quads]

    return (meta_names, name_to_id, pal_rgb, meta_tiles,
            snes_pal_rgb, snes_tiles, snes_meta_tiles,
            gen_pal_rgb, gen_tiles, gen_meta_tiles)


# ============================================================================
# Optical Water Flow Vector Measurement directly from 4-frame tileset pixels!
# ============================================================================
def measure_water_flow_vectors(meta_names, meta_tiles):
    """
    For every water/shore metatile, compare frame 0 vs frame 1 across candidate
    shifts (dx, dy) in {(-4,0), (+4,0), (0,-4), (0,+4), (0,0)} on pure water
    pixel indices ({1,2,3,4,12,13,14,15}).
    Returns dict: meta_id -> (dx, dy, confidence)
    """
    water_ci = {1, 2, 3, 4, 12, 13, 14, 15}
    candidates = [(-4, 0), (4, 0), (0, -4), (0, 4), (0, 0)]
    flows = {}
    for mid, name in enumerate(meta_names):
        if not any(k in name for k in ("WATER", "RAPIDS", "SHORE", "COAST", "OCEAN", "SHALLOW", "FORD")):
            continue
        t0 = meta_tiles[0][mid].px
        t1 = meta_tiles[1][mid].px
        # Count water pixels
        w_coords = [(x, y) for y in range(16) for x in range(16) if t0[y * 16 + x] in water_ci]
        if len(w_coords) < 24:
            continue
        best_shift = (0, 0)
        best_score = -1.0
        scores = {}
        for dx, dy in candidates:
            matches = 0
            total = 0
            for x, y in w_coords:
                nx, ny = x + dx, y + dy
                if 0 <= nx < 16 and 0 <= ny < 16:
                    c1 = t1[ny * 16 + nx]
                    if c1 in water_ci:
                        total += 1
                        if t0[y * 16 + x] == c1:
                            matches += 1
            sc = (matches / total) if total >= 16 else 0.0
            scores[(dx, dy)] = sc
            if sc > best_score:
                best_score = sc
                best_shift = (dx, dy)
        # Only report a directional flow if clearly better than static (0,0)
        if best_shift != (0, 0) and best_score > scores[(0, 0)] + 0.05:
            flows[mid] = (best_shift[0], best_shift[1], round(best_score, 3))
        else:
            flows[mid] = (0, 0, round(scores[(0, 0)], 3))
    return flows


# ============================================================================
# Helper: Is shore land (mirrors hearth_is_shore_land in hearth_terrain_rom.h)
# ============================================================================
def is_shore_land(t, is_world):
    if t in WATER_TILES or t == T_WORLD_CORAL or t == T_DOCK_PIER:
        return False
    if not is_world and t in (T_BRIDGE, T_BRIDGE_RAIL_N, T_BRIDGE_RAIL_S):
        return False
    return True


def expected_shore_edges_from_meta(meta_name):
    """Return set of cardinal edges {'N','S','W','E'} that a shore/coast metatile visually draws."""
    if not ("SHORE_" in meta_name or "COAST_" in meta_name):
        return set()
    # Extract suffix after SHORE_ or COAST_ (allows V_W, V_E, NS, WE, NW, INW, etc.)
    m = re.search(r"(?:SHORE|COAST)_([A-Z0-9_]+)$", meta_name)
    if not m:
        return set()
    suf = m.group(1)
    if suf.startswith("INW") or suf.startswith("INE") or suf.startswith("ISW") or suf.startswith("ISE"):
        return set()  # inner corner only
    edges = set()
    if "NS" in suf:
        edges.update(("N", "S"))
    if "WE" in suf:
        edges.update(("W", "E"))
    clean = suf.replace("V_", "").replace("_V", "")
    for ch in ("N", "S", "W", "E"):
        if ch in clean:
            edges.add(ch)
    return edges


def expected_shoal_edges_from_meta(meta_name):
    """Return set of cardinal edges {'N','S','W','E'} that a CM_SHOAL_* metatile visually transitions to shallow water."""
    if not meta_name.startswith("CM_SHOAL_"):
        return set()
    suf = meta_name[len("CM_SHOAL_"):]
    if suf.startswith("I"):
        return set()  # diagonal inner corner only
    return {ch for ch in ("N", "S", "W", "E") if ch in suf}


def expected_suffix_edges(prefix, meta_name):
    """Return cardinal edges {'N','S','W','E'} encoded in the suffix after prefix (ignores I* inner corners)."""
    if not meta_name.startswith(prefix):
        return set()
    suf = meta_name[len(prefix):]
    if suf.startswith("I"):
        return set()
    return {ch for ch in ("N", "S", "W", "E") if ch in suf}


def tile_border_rgb_diff(tA, tB, zp, direction):
    """Compute mean per-channel RGB difference along shared 16-pixel border ('E' or 'S')."""
    diff = 0.0
    if direction == "E":
        for py in range(16):
            qy = 2 if py >= 8 else 0
            cA = zp[tA.qpal[qy + 1]][tA.px[py * 16 + 15]]
            cB = zp[tB.qpal[qy]][tB.px[py * 16 + 0]]
            diff += abs(cA[0] - cB[0]) + abs(cA[1] - cB[1]) + abs(cA[2] - cB[2])
    else:  # 'S'
        for px in range(16):
            qx = 1 if px >= 8 else 0
            cA = zp[tA.qpal[2 + qx]][tA.px[15 * 16 + px]]
            cB = zp[tB.qpal[qx]][tB.px[0 * 16 + px]]
            diff += abs(cA[0] - cB[0]) + abs(cA[1] - cB[1]) + abs(cA[2] - cB[2])
    return diff / (16.0 * 3.0)


PROP_CORNER_COORDS = (
    (0, 0), (1, 0), (14, 0), (15, 0),
    (0, 15), (1, 15), (14, 15), (15, 15),
)
PEAK_TOP_CORNER_COORDS = (
    (0, 0), (1, 0), (2, 0), (13, 0), (14, 0), (15, 0),
    (0, 1), (15, 1),
)


def tile_mean_rgb(tile, zp, coords=None):
    """Mean (r, g, b) across given pixel coordinates or full 16x16 tile."""
    sr = sg = sb = 0.0
    if coords is None:
        n = 256.0
        for py in range(16):
            qy = 2 if py >= 8 else 0
            sp_l = zp[tile.qpal[qy]]
            sp_r = zp[tile.qpal[qy + 1]]
            row = py * 16
            for px in range(8):
                r, g, b = sp_l[tile.px[row + px]]
                sr += r; sg += g; sb += b
            for px in range(8, 16):
                r, g, b = sp_r[tile.px[row + px]]
                sr += r; sg += g; sb += b
    else:
        n = float(len(coords))
        for px, py in coords:
            qy = 2 if py >= 8 else 0
            qx = 1 if px >= 8 else 0
            r, g, b = zp[tile.qpal[qy + qx]][tile.px[py * 16 + px]]
            sr += r; sg += g; sb += b
    return (sr / n, sg / n, sb / n)


# ============================================================================
# Render a Tilemap Region to RGB Bytearray
# ============================================================================
def render_map_rgb(w, h, cells, pal_rgb, meta_tiles, default_pal_id, is_globe=False, crop=None):
    if crop:
        x0, y0, x1, y1 = crop
    else:
        x0, y0, x1, y1 = 0, 0, w, h
    cw, ch = x1 - x0, y1 - y0
    img_w, img_h = cw * 16, ch * 16
    buf = bytearray(img_w * img_h * 3)
    t_arr = meta_tiles[0]

    for cy in range(ch):
        my = (y0 + cy) % h if is_globe else (y0 + cy)
        for cx in range(cw):
            mx = (x0 + cx) % w if is_globe else (x0 + cx)
            rec = cells[my * w + mx]
            bg2 = rec[1]
            meta = rec[3]
            pal_id = GLOBE_UNIFIED_ZPAL if is_globe else default_pal_id
            tile = t_arr[meta]
            qpal = tile.qpal
            zp = pal_rgb[pal_id]
            px = tile.px

            # Draw 16x16 BG1 tile with per-quadrant sub-palette lookup
            for ty in range(16):
                row_off = ((cy * 16 + ty) * img_w + cx * 16) * 3
                t_row = ty * 16
                qy = 2 if ty >= 8 else 0
                sp_l = zp[qpal[qy]]
                sp_r = zp[qpal[qy + 1]]
                for tx in range(8):
                    r, g, b = sp_l[px[t_row + tx]]
                    off = row_off + tx * 3
                    buf[off] = r; buf[off + 1] = g; buf[off + 2] = b
                for tx in range(8, 16):
                    r, g, b = sp_r[px[t_row + tx]]
                    off = row_off + tx * 3
                    buf[off] = r; buf[off + 1] = g; buf[off + 2] = b

            # Overlay BG2 if non-empty (transparent index 0)
            if bg2 > 0:
                ov_meta = 62 + bg2  # CM_OV_CANOPY_TL = 63 == 62 + O_CANOPY_TL(1)
                if ov_meta < len(t_arr):
                    ov_tile = t_arr[ov_meta]
                    ov_qpal = ov_tile.qpal
                    ov_px = ov_tile.px
                    for ty in range(16):
                        row_off = ((cy * 16 + ty) * img_w + cx * 16) * 3
                        t_row = ty * 16
                        qy = 2 if ty >= 8 else 0
                        osp_l = zp[ov_qpal[qy]]
                        osp_r = zp[ov_qpal[qy + 1]]
                        for tx in range(16):
                            ci = ov_px[t_row + tx]
                            if ci != 0:
                                r, g, b = (osp_l if tx < 8 else osp_r)[ci]
                                off = row_off + tx * 3
                                buf[off] = r; buf[off + 1] = g; buf[off + 2] = b
    return img_w, img_h, buf


def render_genesis_zone_rgb(w, h, cells, gen_pal_rgb, gen_meta_tiles, pal_id, has_mountain_vista, name_to_id, is_globe=False, crop=None):
    """
    Render a local zone or globe crop using the EXACT Sega Genesis VDP BG_A/BG_B rules,
    remapped 399-tile bank, CM_OPQ_* parallax canopies, and 9-bit BGR333 CRAM.
    """
    if crop:
        x0, y0, x1, y1 = crop
    else:
        x0, y0, x1, y1 = 0, 0, w, h
    cw, ch = x1 - x0, y1 - y0
    img_w, img_h = cw * 16, ch * 16
    buf = bytearray(img_w * img_h * 3)
    eff_pal = GLOBE_UNIFIED_ZPAL if is_globe else pal_id
    gp = gen_pal_rgb[eff_pal]
    ov_base = name_to_id["CM_OV_CANOPY_TL"]
    opq_base = name_to_id["CM_OPQ_CANOPY_TL"]
    sky_rgb = (90, 124, 168) if eff_pal != 8 else (22, 30, 56)

    def blit_tile(cx, cy, tile, transparent0=False):
        qpal = tile.qpal
        px = tile.px
        for ty in range(16):
            row_off = ((cy * 16 + ty) * img_w + cx * 16) * 3
            t_row = ty * 16
            qy = 2 if ty >= 8 else 0
            sp_l = gp[qpal[qy]]
            sp_r = gp[qpal[qy + 1]]
            for tx in range(16):
                ci = px[t_row + tx]
                if transparent0 and ci == 0:
                    continue
                r, g, b = (sp_l if tx < 8 else sp_r)[ci]
                off = row_off + tx * 3
                buf[off] = r; buf[off + 1] = g; buf[off + 2] = b

    for cy in range(ch):
        my = (y0 + cy) % h if is_globe else (y0 + cy)
        for cx in range(cw):
            mx = (x0 + cx) % w if is_globe else (x0 + cx)
            rec = cells[my * w + mx]
            bg1, bg2, meta = rec[0], rec[1], rec[3]
            if not is_globe and has_mountain_vista:
                if bg1 == T_MTN_SKY:
                    fill_rect_rgb(buf, img_w, img_h, cx * 16, cy * 16, 16, 16, sky_rgb)
                    if 1 <= bg2 <= 12:
                        blit_tile(cx, cy, gen_meta_tiles[ov_base + (bg2 - 1)], transparent0=True)
                elif 1 <= bg2 <= 12:
                    blit_tile(cx, cy, gen_meta_tiles[opq_base + (bg2 - 1)], transparent0=False)
                else:
                    blit_tile(cx, cy, gen_meta_tiles[meta], transparent0=False)
            else:
                blit_tile(cx, cy, gen_meta_tiles[meta], transparent0=False)
                if 1 <= bg2 <= 12:
                    blit_tile(cx, cy, gen_meta_tiles[ov_base + (bg2 - 1)], transparent0=True)
    return img_w, img_h, buf


def render_snes_zone_rgb(w, h, cells, snes_pal_rgb, snes_meta_tiles, pal_id, has_mountain_vista, name_to_id, is_globe=False, crop=None):
    """
    Render a local zone or globe crop using the EXACT SNES Mode-1 PPU single-BG1 rules,
    compiled <=510-tile 4bpp bank, CM_OPQ_* opaque canopies, and 15-bit BGR555 CGRAM.
    """
    if crop:
        x0, y0, x1, y1 = crop
    else:
        x0, y0, x1, y1 = 0, 0, w, h
    cw, ch = x1 - x0, y1 - y0
    img_w, img_h = cw * 16, ch * 16
    buf = bytearray(img_w * img_h * 3)
    opq_base = name_to_id["CM_OPQ_CANOPY_TL"]
    oak_crown_opq = name_to_id["CM_OPQ_OAK_CROWN"]

    for cy in range(ch):
        my = (y0 + cy) % h if is_globe else (y0 + cy)
        for cx in range(cw):
            mx = (x0 + cx) % w if is_globe else (x0 + cx)
            rec = cells[my * w + mx]
            bg1, bg2 = rec[0], rec[1]
            meta = rec[3]
            zpal = GLOBE_UNIFIED_ZPAL if is_globe else pal_id
            sp = snes_pal_rgb[zpal]
            sky_rgb = sp[1][2] if (not is_globe and has_mountain_vista) else sp[0][1]

            if is_globe:
                # Exact SNES stream_world_row/col rule: non-zero bg2 -> CM_OPQ_OAK_CROWN
                use_meta = oak_crown_opq if bg2 > 0 else meta
            elif 1 <= bg2 <= 12:
                use_meta = opq_base + (bg2 - 1)
            else:
                use_meta = meta

            tile = snes_meta_tiles[use_meta]
            qpal = tile.qpal
            px = tile.px
            for ty in range(16):
                row_off = ((cy * 16 + ty) * img_w + cx * 16) * 3
                t_row = ty * 16
                qy = 2 if ty >= 8 else 0
                sp_l = sp[qpal[qy]]
                sp_r = sp[qpal[qy + 1]]
                for tx in range(16):
                    ci = px[t_row + tx]
                    r, g, b = sky_rgb if (ci == 0 and not is_globe and has_mountain_vista and bg1 == T_MTN_SKY) else (sp_l if tx < 8 else sp_r)[ci]
                    off = row_off + tx * 3
                    buf[off] = r; buf[off + 1] = g; buf[off + 2] = b
    return img_w, img_h, buf


# ============================================================================
# PASS 7: Console Hardware, VRAM/OAM, Portrait Parity & 816-tcc/SGDK Safety Audit
# ============================================================================
def audit_console_hardware(dump, meta_names, meta_tiles, snes_tiles, snes_meta_tiles, gen_tiles):
    """
    Verifies console-specific hardware constraints & parity invariants across SNES,
    Sega Genesis, SDL2, and Web builds so console-only regressions cannot slip through.
    """
    issues = []

    def add(sev, target, code, msg):
        issues.append({
            "severity": sev, "category": "CONSOLE", "code": code,
            "zone_id": -1, "zone": f"Console:{target}", "x": 0, "y": 0, "msg": msg,
        })

    snes_c_path = os.path.join(ROOT, "engine", "platform", "snes_main.c")
    gen_c_path = os.path.join(ROOT, "engine", "platform", "genesis_main.c")
    sdl_c_path = os.path.join(ROOT, "engine", "platform", "sdl2_main.c")
    types_h_path = os.path.join(ROOT, "engine", "core", "hearth_types.h")
    data_c_path = os.path.join(ROOT, "engine", "core", "hearth_data.c")
    terrain_rom_h = os.path.join(ROOT, "engine", "platform", "hearth_terrain_rom.h")
    terrain_snes_asm = os.path.join(ROOT, "engine", "platform", "hearth_terrain_snes.asm")
    cinema_rom_h = os.path.join(ROOT, "engine", "platform", "cinema_cg_rom.h")
    cinema_sdl_h = os.path.join(ROOT, "engine", "platform", "cinema_cg_sdl2.h")
    cinema_snes_asm = os.path.join(ROOT, "engine", "platform", "cinema_cg_snes.asm")
    portrait_tsx = os.path.join(ROOT, "src", "components", "PortraitFrame.tsx")

    snes_src = open(snes_c_path, "r").read()
    gen_src = open(gen_c_path, "r").read()
    sdl_src = open(sdl_c_path, "r").read()
    types_src = open(types_h_path, "r").read()
    data_src = open(data_c_path, "r").read()

    # 7a. Console VRAM Tilebank Budgets & SNES ROM Table Sync
    if len(snes_tiles) > 510:
        add("CRITICAL", "SNES", "SNES_VRAM_OVERFLOW",
            f"Compiled SNES 4bpp BG1 tilebank ({len(snes_tiles)}) exceeds 510-tile VRAM budget")
    if len(gen_tiles) > 399:
        add("CRITICAL", "Genesis", "GEN_VRAM_OVERFLOW",
            f"Compiled Genesis 4bpp BG tilebank ({len(gen_tiles)}) exceeds 399-tile VRAM budget")

    if os.path.isfile(terrain_rom_h) and os.path.isfile(terrain_snes_asm):
        trom = open(terrain_rom_h, "r").read()
        tasm = open(terrain_snes_asm, "r").read()
        m_cnt = re.search(r"#define\s+SNES_CHRONO_TILE_COUNT\s+(\d+)", trom)
        if m_cnt:
            rom_tcnt = int(m_cnt.group(1))
            if rom_tcnt > 510:
                add("CRITICAL", "SNES", "SNES_ROM_TILE_BUDGET",
                    f"hearth_terrain_rom.h SNES_CHRONO_TILE_COUNT={rom_tcnt} exceeds 510")
            m_blk = re.search(r"snes_chrono_tiles:(.*?)(?:snes_chrono_anim_frames:|snes_chrono_tiles_end:|\.ends)", tasm, re.S)
            if m_blk:
                n_bytes = len(re.findall(r"\$[0-9A-Fa-f]{2}", m_blk.group(1)))
                if n_bytes != rom_tcnt * 32:
                    add("ERROR", "SNES", "SNES_TERRAIN_ASM_MISMATCH",
                        f"snes_chrono_tiles ASM has {n_bytes}B, expected {rom_tcnt * 32}B ({rom_tcnt} tiles)")
            # Check SNES_META_TL/TR/BL/BR tile indices fit inside rom_tcnt
            for arr_name in ("SNES_META_TL", "SNES_META_TR", "SNES_META_BL", "SNES_META_BR"):
                m_arr = re.search(rf"{arr_name}\[.*?\]\s*=\s*\{{(.*?)\}};", trom, re.S)
                if m_arr:
                    vals = [int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+", m_arr.group(1))]
                    bad = [v & 0x03FF for v in vals if (v & 0x03FF) >= rom_tcnt]
                    if bad:
                        add("CRITICAL", "SNES", "SNES_META_TILE_OOB",
                            f"{arr_name} references out-of-range tile index {bad[0]} >= {rom_tcnt}")
            # Verify console tile folding never folds repeating ground tiles into foreground lamp props
            meta_idx_map = {nm: idx for idx, nm in enumerate(meta_names)}
            ground_check = [
                nm for nm in (
                    "T_COBBLE", "CM_COBBLE_V1", "CM_COBBLE_V2", "CM_COBBLE_V3",
                    "T_ROCKY_PATH", "CM_ROCKY_V1", "T_GRASS", "CM_GRASS_V1",
                    "T_AUTUMN_GRASS", "T_DIRT", "T_WOOD_FLOOR",
                ) if nm in meta_idx_map
            ]
            lamp_check = [nm for nm in ("CM_LAMP_UNLIT", "CM_LAMP_COBBLE") if nm in meta_idx_map]
            for target_lbl, pfx, tmask in (("SNES", "SNES_META_", 0x03FF), ("Genesis", "GEN_META_", 0x07FF)):
                q_arrs = {}
                for q in ("TL", "TR", "BL", "BR"):
                    m_q = re.search(rf"{pfx}{q}\[.*?\]\s*=\s*\{{(.*?)\}};", trom, re.S)
                    if m_q:
                        q_arrs[q] = [int(v, 0) & tmask for v in re.findall(r"0x[0-9A-Fa-f]+", m_q.group(1))]
                if len(q_arrs) == 4:
                    lamp_head_tids = set()
                    for lnm in lamp_check:
                        li = meta_idx_map[lnm]
                        lamp_head_tids.add(q_arrs["TL"][li])
                        lamp_head_tids.add(q_arrs["TR"][li])
                    for gnm in ground_check:
                        gi = meta_idx_map[gnm]
                        for q in ("TL", "TR", "BL", "BR"):
                            tid = q_arrs[q][gi]
                            if tid != 0 and tid in lamp_head_tids:
                                add("ERROR", target_lbl, f"{target_lbl.upper()}_GROUND_FOLDED_TO_PROP",
                                    f"{gnm}.{q} folded into lantern prop tile {tid} in {pfx}{q}")

    # 7b. SNES OBJ VRAM 8x8 Tile Collision & Ambient Particle Audit
    # Compute all 8x8 OBJ tile indices occupied by 16x16 slots 0..48 and 49..60
    def slot_to_tile(slot):
        return ((slot >> 3) << 5) + ((slot & 7) << 1)

    obj_owners = {}  # tile_idx -> owner label
    sprite16_tiles = set()
    # Slots 0..48 (core sprites) + 49, 55..60 (timed ring, battle vfx, atlas, fish, pip hop/squash)
    active_16x16_slots = list(range(0, 49)) + [49, 55, 56, 57, 58, 59, 60]
    for s in active_16x16_slots:
        t0 = slot_to_tile(s)
        for dt in (0, 1, 16, 17):
            ti = t0 + dt
            sprite16_tiles.add(ti)
            if ti in obj_owners:
                add("CRITICAL", "SNES", "SNES_OBJ_VRAM_COLLISION",
                    f"OBJ tile 0x{ti:02X} claimed by both {obj_owners[ti]} and 16x16 slot {s}")
            obj_owners[ti] = f"slot16:{s}"

    # Parse SPR_BOOT_TILE_LUT boot strips (each non-zero entry occupies bt and bt+1)
    m_boot = re.search(r"SPR_BOOT_TILE_LUT\[\d+\]\s*=\s*\{(.*?)\};", snes_src, re.S)
    if m_boot:
        clean_boot = re.sub(r"/\*.*?\*/", "", m_boot.group(1), flags=re.S)
        boot_vals = [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]+)u?", clean_boot)]
        for sidx, bt in enumerate(boot_vals):
            if bt == 0:
                continue
            for dt in (0, 1):
                ti = bt + dt
                if ti >= 256:
                    add("CRITICAL", "SNES", "SNES_OBJ_BOOT_OOB",
                        f"SPR_BOOT_TILE_LUT[{sidx}] boot tile 0x{ti:02X} >= 0x100")
                elif ti in obj_owners:
                    add("CRITICAL", "SNES", "SNES_OBJ_VRAM_COLLISION",
                        f"OBJ tile 0x{ti:02X} collision: SPR_BOOT_TILE_LUT[{sidx}] overlaps {obj_owners[ti]}")
                else:
                    obj_owners[ti] = f"boot:{sidx}"

    # Parse dedicated OBJ_TILE_AMBIENT_* defines and verify zero collision with any sprite/boot tile
    amb_defines = re.findall(r"#define\s+(OBJ_TILE_AMBIENT_\w+)\s+0x([0-9A-Fa-f]+)u?", snes_src)
    if not amb_defines:
        add("ERROR", "SNES", "SNES_AMBIENT_NO_DEDICATED_TILES",
            "snes_main.c defines no dedicated OBJ_TILE_AMBIENT_* 8x8 tiles for drifting particles")
    for name, hx in amb_defines:
        ti = int(hx, 16)
        if ti >= 256:
            add("CRITICAL", "SNES", "SNES_OBJ_AMBIENT_OOB",
                f"{name}=0x{ti:02X} exceeds 8-bit OBJ tile table")
        elif ti in obj_owners:
            add("CRITICAL", "SNES", "SNES_OBJ_VRAM_COLLISION",
                f"Dedicated ambient tile {name}=0x{ti:02X} collides with {obj_owners[ti]}")
        else:
            obj_owners[ti] = name

    # Verify EMIT_SNES_OAM never emits a raw 8x8 quadrant from a 16x16 sprite (like 0x42u = SPR_EMOTE_SPARK!)
    for m_oam in re.finditer(r"EMIT_SNES_OAM\([^,]+,[^,]+,\s*0x([0-9A-Fa-f]+)u?\s*,[^,]+,\s*0u?\s*\)", snes_src):
        ti = int(m_oam.group(1), 16)
        if ti in sprite16_tiles:
            add("ERROR", "SNES", "SNES_AMBIENT_SPRITE_SLICE",
                f"8x8 EMIT_SNES_OAM uses raw 16x16 sprite quadrant 0x{ti:02X} ({obj_owners.get(ti, '?')})")

    # Verify MODE_EXPLORE ambient particles explicitly suppress indoor zones (Grandma Mara's cottage & interiors)
    m_amb_blk = re.search(r"Drifting Biome Atmosphere Particles(.*?)Swimming / Trophy Fish", snes_src, re.S)
    if m_amb_blk:
        blk = m_amb_blk.group(1)
        if "ZPAL_COTTAGE_INTERIOR" not in blk or "ZONE_ROWAN_HOME" not in blk:
            add("ERROR", "SNES", "SNES_INTERIOR_PARTICLE_LEAK",
                "SNES MODE_EXPLORE ambient particle loop does not suppress ZPAL_COTTAGE_INTERIOR / ZONE_ROWAN_HOME")
    else:
        add("WARNING", "SNES", "SNES_AMBIENT_BLOCK_MISSING",
            "Could not locate SNES ambient particle emitter block for interior suppression check")

    # 7c. SNES Top HUD NTSC Overscan Safety Check
    if "u16 col = 33u;" not in snes_src or "g_bg0_map + 32u" not in snes_src:
        add("ERROR", "SNES", "SNES_HUD_OVERSCAN_CLIP",
            "SNES Explore HUD must render on Row 1 (col=33u, DMA g_bg0_map+32u) to clear NTSC top overscan")

    # 7d. SNES Globe Streaming Overlay Sentinel Parity (Emerald Heartland Hollow Oak crown)
    if "CM_OPQ_CANOPY_TL" in re.findall(r"stream_world_(?:col|row).*?return;", snes_src, re.S).__str__():
        add("ERROR", "SNES", "SNES_GLOBE_CROWN_WRONG_TILE",
            "snes_main.c globe streamer maps overlay sentinel to CM_OPQ_CANOPY_TL instead of CM_OPQ_OAK_CROWN")
    if "t == 63u" in snes_src or "(uint8_t)63u" in data_src:
        add("ERROR", "SNES", "GLOBE_SENTINEL_COLLISION",
            "Globe overlay sentinel uses tile ID 63u (collides with T_LANTERN_UNLIT) instead of 255u")
    if snes_src.count("CM_OPQ_OAK_CROWN") < 2:
        add("ERROR", "SNES", "SNES_GLOBE_CROWN_MISSING",
            "snes_main.c must map globe overlay sentinel 255u -> CM_OPQ_OAK_CROWN in both stream_world_col and stream_world_row")

    # 7e. Cross-Platform Character Portrait Parity Audit (Core, SNES, Genesis, SDL2, Web)
    m_pcount = re.search(r"PORTRAIT_COUNT\s*=\s*(\d+)", types_src)
    expected_portraits = (int(m_pcount.group(1)) - 1) if m_pcount else 0
    if expected_portraits < 10:
        add("ERROR", "Parity", "PORTRAIT_COUNT_LOW",
            f"hearth_types.h PORTRAIT_COUNT implies only {expected_portraits} painted portraits")
    if os.path.isfile(cinema_rom_h):
        crom = open(cinema_rom_h, "r").read()
        m_rc = re.search(r"#define\s+ROM_PORTRAIT_COUNT\s+(\d+)", crom)
        if not m_rc or int(m_rc.group(1)) != expected_portraits:
            add("ERROR", "Parity", "ROM_PORTRAIT_PARITY",
                f"cinema_cg_rom.h ROM_PORTRAIT_COUNT={m_rc.group(1) if m_rc else 'missing'} != {expected_portraits}")
    if os.path.isfile(cinema_sdl_h):
        csdl = open(cinema_sdl_h, "r").read()
        m_sc = re.search(r"#define\s+CINEMA_SDL_PORTRAIT_COUNT\s+(\d+)", csdl)
        if not m_sc or int(m_sc.group(1)) != expected_portraits:
            add("ERROR", "Parity", "SDL2_PORTRAIT_PARITY",
                f"cinema_cg_sdl2.h CINEMA_SDL_PORTRAIT_COUNT={m_sc.group(1) if m_sc else 'missing'} != {expected_portraits}")
    if os.path.isfile(cinema_snes_asm):
        casm = open(cinema_snes_asm, "r").read()
        m_pblk = re.search(r"snes_portrait_tiles:(.*?)(?:snes_portrait_tiles_end:|\.ends)", casm, re.S)
        if not m_pblk:
            add("CRITICAL", "SNES", "SNES_PORTRAIT_ASM_MISSING",
                "snes_portrait_tiles label missing in cinema_cg_snes.asm")
        else:
            p_bytes = len(re.findall(r"\$[0-9A-Fa-f]{2}", m_pblk.group(1)))
            if p_bytes != expected_portraits * 512:
                add("ERROR", "SNES", "SNES_PORTRAIT_ASM_SIZE",
                    f"snes_portrait_tiles has {p_bytes}B, expected {expected_portraits * 512}B ({expected_portraits} portraits)")
    if os.path.isfile(portrait_tsx):
        ptsx = open(portrait_tsx, "r").read()
        m_map = re.search(r"PAINTED_PORTRAITS_BY_ID\s*:\s*Record<number,\s*string>\s*=\s*\{(.*?)\};", ptsx, re.S)
        web_ids = set(int(k) for k in re.findall(r"(\d+)\s*:", m_map.group(1))) if m_map else set()
        missing_web = [i for i in range(1, expected_portraits + 1) if i not in web_ids]
        if missing_web:
            add("ERROR", "Web", "WEB_PORTRAIT_PARITY",
                f"PortraitFrame.tsx PAINTED_PORTRAITS_BY_ID missing portrait IDs {missing_web}")
    if "ROM_PORTRAIT_COUNT" not in snes_src or "snes_portrait_tiles" not in snes_src:
        add("ERROR", "SNES", "SNES_PORTRAIT_UNWIRED", "snes_main.c does not wire baked ROM portraits")
    if "ROM_PORTRAIT_COUNT" not in gen_src or "GEN_PORTRAIT_TILES" not in gen_src:
        add("ERROR", "Genesis", "GEN_PORTRAIT_UNWIRED", "genesis_main.c does not wire baked ROM portraits")
    if "CINEMA_SDL_PORTRAIT_COUNT" not in sdl_src:
        add("ERROR", "SDL2", "SDL2_PORTRAIT_UNWIRED", "sdl2_main.c does not wire baked SDL2 portraits")

    # 7f. WDC 65816 (816-tcc) Static Codegen Safety & Genesis SGDK Portability Audit
    # 816-tcc miscompiles compound &&/|| expressions mixing 24-bit pointers with scalar/deref tests!
    ptr_null_pat = re.compile(r"(&&|\|\|).*([!=]=\s*NULL)|([!=]=\s*NULL).*(&&|\|\|)")
    ptr_truthy_pat = re.compile(
        r"(!\s*(speaker|text|full_text|next_text|label|interact_label|zone_name|title|line1|line2|src|dst|s|p_bg\d|out_\w+)\s*(\|\||&&)"
        r"|(\|\||&&)\s*!?\s*(speaker|text|full_text|next_text|label|interact_label|zone_name|title|line1|line2|src|dst|p_bg\d|out_\w+)\b(?!\s*\[))"
    )
    c_scan_files = sorted([os.path.join(ROOT, "engine", "core", f)
                           for f in os.listdir(os.path.join(ROOT, "engine", "core")) if f.endswith(".c")])
    c_scan_files.append(snes_c_path)
    for fpath in c_scan_files:
        rel = os.path.relpath(fpath, ROOT)
        for lineno, line in enumerate(open(fpath, "r"), 1):
            code = line.split("/*")[0]
            if ptr_null_pat.search(code) or ptr_truthy_pat.search(code):
                add("ERROR", "SNES-816TCC", "SNES_816TCC_PTR_BOOL",
                    f"{rel}:{lineno} mixes 24-bit pointer test with &&/|| (miscompiles under PVSnesLib 816-tcc — split into nested if)")

    if "* 512u" in snes_src:
        add("ERROR", "SNES-816TCC", "SNES_PORTRAIT_MUL512",
            "snes_main.c uses * 512u instead of << 9 shift for portrait ROM offset")
    if "uintptr_t" in gen_src:
        add("CRITICAL", "Genesis", "GEN_UINTPTR_FORBIDDEN",
            "genesis_main.c uses uintptr_t (unavailable in SGDK m68k stdint.h)")

    # 7g. Unified World Map Palette Lock (prevents mid-travel color snapping between islands)
    core_c_path = os.path.join(ROOT, "engine", "core", "hearth_core.c")
    core_src = open(core_c_path, "r").read()
    m_stream = re.search(r"static void update_globe_region_streaming\(.*?\n\}", core_src, re.S)
    if m_stream and "terrain_pal_id" in m_stream.group(0):
        add("ERROR", "Parity", "GLOBE_PALETTE_SNAP",
            "hearth_core.c update_globe_region_streaming() mutates terrain_pal_id on region change (causes island-to-island palette snaps)")
    m_pop = re.search(r"static void populate_zone_evertale_world\(.*?\n\}", core_src, re.S)
    if m_pop and "ZPAL_EVERTALE_WORLD" not in m_pop.group(0):
        add("ERROR", "Parity", "GLOBE_PALETTE_UNIFIED",
            "hearth_core.c populate_zone_evertale_world() must pin terrain_pal_id to unified ZPAL_EVERTALE_WORLD")

    return issues


def audit_cross_build_edge_transitions(label, zone_id, w, h, cells, pal_id, is_globe,
                                       meta_names, targets, reg_names=None):
    """
    Cross-Build Edge-Transition Audit across ALL 3 rendered hardware builds:
      - Master (PC SDL2 / Web Canvas, 60-color lossless 16x16 metatiles)
      - SNES   (Mode-1 PPU, compiled <=510-tile 4bpp bank, 15-bit BGR555 CGRAM)
      - Genesis (VDP, compiled <=399-tile 4bpp bank, 9-bit BGR333 CRAM)

    Checks BOTH:
      1. Structural autotile transition completeness whenever two distinct terrain styles meet:
         - Deep Ocean (T_WORLD_OCEAN) <-> Shallow Water (T_WORLD_SHALLOW / T_WORLD_CORAL) => CM_SHOAL_*
         - Beach / Desert Sand <-> Grass => CM_SAND_GRASS_*
         - Cobble Road <-> Grass (local zones) => CM_COBBLE_EDGE_* / CM_COBBLE_CORNER_*
      2. Pixel-level border continuity across all 3 compiled hardware tilebanks & palettes
         so neither missing autotiles nor SNES/Genesis tile folding can produce jagged seams!
    """
    issues = []
    eff_pal = GLOBE_UNIFIED_ZPAL if is_globe else pal_id

    def zone_label(target_tag, x, y):
        base = label
        if is_globe and reg_names is not None:
            reg = cells[y * w + x][4]
            base = f"Globe:{reg_names[reg]}" if 0 <= reg < len(reg_names) else "Globe:Evertale"
        return f"{base} [{target_tag}]" if target_tag != "Master" else base

    def add_issue(sev, cat, code, target_tag, x, y, msg):
        issues.append({
            "severity": sev, "category": cat, "code": code,
            "zone_id": zone_id, "zone": zone_label(target_tag, x, y),
            "x": x, "y": y, "msg": msg,
        })

    def get_cell(x, y):
        if is_globe:
            return cells[(y % h) * w + (x % w)]
        if 0 <= x < w and 0 <= y < h:
            return cells[y * w + x]
        return None

    border_cache = {}
    def border_diff(tidx, mA, mB, side):
        key = (tidx, eff_pal, mA, mB, side)
        v = border_cache.get(key)
        if v is None:
            _tname, tiles, pals = targets[tidx]
            v = tile_border_rgb_diff(tiles[mA], tiles[mB], pals[eff_pal], side)
            border_cache[key] = v
        return v

    def is_solid_caster_tile(t):
        return t in (
            T_WALL_WOOD, T_WALL_WINDOW, T_WALL_DOOR, T_ROOF_BODY, T_ROOF_TOP,
            T_WINDMILL_WALL, T_WINDMILL_DOOR, T_WINDMILL_ROOF, T_CLIFF_WALL,
            T_CLIFF_LEDGE, T_TREE_TRUNK, T_MAPLE_TRUNK, T_PINE_TRUNK,
            T_HOLLOW_OAK, T_SHRINE_PILLAR, T_BOULDER,
        )

    CARDINAL = (("N", 0, -1, "S", True), ("S", 0, 1, "S", False), ("W", -1, 0, "E", True), ("E", 1, 0, "E", False))
    DIAG = (("NW", -1, -1), ("NE", 1, -1), ("SW", -1, 1), ("SE", 1, 1))

    for y in range(h):
        for x in range(w):
            rec = cells[y * w + x]
            bg1 = rec[0]
            meta = rec[3]
            mname = meta_names[meta]

            # 1. Deep Ocean <-> Shallow Water Curved Shoal Transitions (CM_SHOAL_*)
            if bg1 == T_WORLD_OCEAN:
                land_c = {}
                shal_c = {}
                for e, dx, dy, _side, _nbf in CARDINAL:
                    nb = get_cell(x + dx, y + dy)
                    nt = nb[0] if nb is not None else bg1
                    land_c[e] = is_shore_land(nt, is_globe)
                    shal_c[e] = nt in SHALLOW_TILES
                has_land_card = any(land_c.values())
                needed_shoal = {e for e, f in shal_c.items() if f}
                if not has_land_card:
                    if needed_shoal:
                        if (shal_c["N"] and shal_c["S"]) or (shal_c["W"] and shal_c["E"]):
                            add_issue("WARNING", "SEAM", "SHOAL_INCOMPLETE_MASK", "Master", x, y,
                                      f"Deep ocean tile ({mname}) pinched between opposite shallow water edges")
                        drawn_shoal = expected_shoal_edges_from_meta(mname)
                        if not mname.startswith("CM_SHOAL_") or not needed_shoal.issubset(drawn_shoal):
                            missing = "".join(sorted(needed_shoal - drawn_shoal))
                            add_issue("ERROR", "AUTOTILE", "MISSING_SHOAL_EDGE", "Master", x, y,
                                      f"Deep ocean touches shallow water on {''.join(sorted(needed_shoal))} without curved shoal transition ({mname}, missing {missing})")
                    else:
                        # Check diagonal shallow corner (when no diagonal land coast takes precedence)
                        has_land_diag = any(
                            is_shore_land(get_cell(x + dx, y + dy)[0], is_globe)
                            for _, dx, dy in DIAG if get_cell(x + dx, y + dy) is not None
                        )
                        has_shal_diag = any(
                            get_cell(x + dx, y + dy)[0] in SHALLOW_TILES
                            for _, dx, dy in DIAG if get_cell(x + dx, y + dy) is not None
                        )
                        if has_shal_diag and not has_land_diag and not mname.startswith("CM_SHOAL_I"):
                            add_issue("ERROR", "AUTOTILE", "MISSING_SHOAL_EDGE", "Master", x, y,
                                      f"Deep ocean touches diagonal shallow water without inner shoal corner ({mname})")

                # Cross-build pixel continuity on every active CM_SHOAL_* outer shallow rim & inner ocean edge
                if mname.startswith("CM_SHOAL_"):
                    drawn_shoal = expected_shoal_edges_from_meta(mname)
                    for e, dx, dy, side, nb_first in CARDINAL:
                        nb = get_cell(x + dx, y + dy)
                        if nb is None:
                            continue
                        nbg1, nmeta = nb[0], nb[3]
                        nmname = meta_names[nmeta]
                        if e in drawn_shoal and nbg1 in SHALLOW_TILES:
                            # Skip junctions where the shallow tile itself draws a perpendicular land coast
                            if expected_shore_edges_from_meta(nmname):
                                continue
                            d_master = border_diff(0, nmeta, meta, side) if nb_first else border_diff(0, meta, nmeta, side)
                            for tidx, (tname, _tiles, _pals) in enumerate(targets):
                                d = d_master if tidx == 0 else (border_diff(tidx, nmeta, meta, side) if nb_first else border_diff(tidx, meta, nmeta, side))
                                lim = 30.0 if tname != "Genesis" else 34.0
                                deg_lim = 14.0 if tname == "SNES" else 22.0
                                degraded = (tidx > 0 and d > 28.0 and (d - d_master) > deg_lim)
                                if d > lim or degraded:
                                    code = "EDGE_TRANSITION_SEAM" if tname == "Master" else (
                                        "SNES_EDGE_TRANSITION_SEAM" if tname == "SNES" else "GEN_EDGE_TRANSITION_SEAM")
                                    add_issue("ERROR", "SEAM", code, tname, x, y,
                                              f"{tname} shoal outer edge {mname} ({e}) clashes with {nmname} (border diff {d:.1f}, master {d_master:.1f})")
                        elif e not in drawn_shoal and nbg1 == T_WORLD_OCEAN and nmname in ("T_WORLD_OCEAN", "CM_OCEAN_V1"):
                            d_master = border_diff(0, nmeta, meta, side) if nb_first else border_diff(0, meta, nmeta, side)
                            for tidx, (tname, _tiles, _pals) in enumerate(targets):
                                d = d_master if tidx == 0 else (border_diff(tidx, nmeta, meta, side) if nb_first else border_diff(tidx, meta, nmeta, side))
                                lim = 34.0 if tname != "Genesis" else 38.0
                                deg_lim = 14.0 if tname == "SNES" else 22.0
                                degraded = (tidx > 0 and d > 30.0 and (d - d_master) > deg_lim)
                                if d > lim or degraded:
                                    code = "EDGE_TRANSITION_SEAM" if tname == "Master" else (
                                        "SNES_EDGE_TRANSITION_SEAM" if tname == "SNES" else "GEN_EDGE_TRANSITION_SEAM")
                                    add_issue("ERROR", "SEAM", code, tname, x, y,
                                              f"{tname} shoal inner edge {mname} ({e}) clashes with {nmname} (border diff {d:.1f}, master {d_master:.1f})")

            # 2. Sand / Beach <-> Grass Feathered Transitions (CM_SAND_GRASS_*)
            elif bg1 in (T_WORLD_BEACH, T_WORLD_SAND):
                grass_c = {}
                for e, dx, dy, _side, _nbf in CARDINAL:
                    nb = get_cell(x + dx, y + dy)
                    grass_c[e] = (nb is not None and nb[0] in GRASS_LIKE)
                needed_sg = {e for e, f in grass_c.items() if f}
                if needed_sg and not ((grass_c["N"] and grass_c["S"]) or (grass_c["W"] and grass_c["E"])):
                    drawn_sg = expected_suffix_edges("CM_SAND_GRASS_", mname)
                    if not needed_sg.issubset(drawn_sg):
                        missing = "".join(sorted(needed_sg - drawn_sg))
                        add_issue("ERROR", "AUTOTILE", "MISSING_SAND_GRASS_EDGE", "Master", x, y,
                                  f"Sand/beach touches grass on {''.join(sorted(needed_sg))} without curved transition ({mname}, missing {missing})")
                if mname.startswith("CM_SAND_GRASS_"):
                    drawn_sg = expected_suffix_edges("CM_SAND_GRASS_", mname)
                    for e, dx, dy, side, nb_first in CARDINAL:
                        if e not in drawn_sg:
                            continue
                        nb = get_cell(x + dx, y + dy)
                        if nb is not None and nb[0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                            nmeta = nb[3]
                            d_master = border_diff(0, nmeta, meta, side) if nb_first else border_diff(0, meta, nmeta, side)
                            for tidx, (tname, _tiles, _pals) in enumerate(targets):
                                d = d_master if tidx == 0 else (border_diff(tidx, nmeta, meta, side) if nb_first else border_diff(tidx, meta, nmeta, side))
                                lim = 26.0 if tname != "Genesis" else 34.0
                                deg_lim = 14.0 if tname == "SNES" else 22.0
                                degraded = (tidx > 0 and d > 28.0 and (d - d_master) > deg_lim)
                                if d > lim or degraded:
                                    code = "EDGE_TRANSITION_SEAM" if tname == "Master" else (
                                        "SNES_EDGE_TRANSITION_SEAM" if tname == "SNES" else "GEN_EDGE_TRANSITION_SEAM")
                                    add_issue("ERROR", "SEAM", code, tname, x, y,
                                              f"{tname} sand-grass edge {mname} ({e}) clashes with {meta_names[nmeta]} (border diff {d:.1f}, master {d_master:.1f})")

            # 3. Cobble Road <-> Grass Feathered Transitions (local zones, all 3 builds!)
            elif not is_globe and bg1 == T_COBBLE:
                nb_n = get_cell(x, y - 1)
                nb_w = get_cell(x - 1, y)
                cast_shadow = (nb_n is not None and is_solid_caster_tile(nb_n[0])) or (nb_w is not None and is_solid_caster_tile(nb_w[0]))
                grass_c = {}
                for e, dx, dy, _side, _nbf in CARDINAL:
                    nb = get_cell(x + dx, y + dy)
                    grass_c[e] = (nb is not None and nb[0] in GRASS_LIKE)
                needed_cg = {e for e, f in grass_c.items() if f}
                if needed_cg and not cast_shadow and not ((grass_c["N"] and grass_c["S"]) or (grass_c["W"] and grass_c["E"])):
                    drawn_cg = expected_suffix_edges("CM_COBBLE_EDGE_", mname) | expected_suffix_edges("CM_COBBLE_CORNER_", mname)
                    if not needed_cg.issubset(drawn_cg):
                        missing = "".join(sorted(needed_cg - drawn_cg))
                        add_issue("ERROR", "AUTOTILE", "MISSING_COBBLE_EDGE", "Master", x, y,
                                  f"Cobble road touches grass on {''.join(sorted(needed_cg))} without feathered edge ({mname}, missing {missing})")
                if mname.startswith("CM_COBBLE_EDGE_") or mname.startswith("CM_COBBLE_CORNER_"):
                    drawn_cg = expected_suffix_edges("CM_COBBLE_EDGE_", mname) | expected_suffix_edges("CM_COBBLE_CORNER_", mname)
                    for e, dx, dy, side, nb_first in CARDINAL:
                        if e not in drawn_cg:
                            continue
                        nb = get_cell(x + dx, y + dy)
                        if nb is not None and nb[0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                            nmeta = nb[3]
                            d_master = border_diff(0, nmeta, meta, side) if nb_first else border_diff(0, meta, nmeta, side)
                            for tidx, (tname, _tiles, _pals) in enumerate(targets):
                                d = d_master if tidx == 0 else (border_diff(tidx, nmeta, meta, side) if nb_first else border_diff(tidx, meta, nmeta, side))
                                lim = 24.0 if tname != "Genesis" else 36.0
                                deg_lim = 14.0 if tname == "SNES" else 22.0
                                degraded = (tidx > 0 and d > 28.0 and (d - d_master) > deg_lim)
                                if d > lim or degraded:
                                    code = "EDGE_TRANSITION_SEAM" if tname == "Master" else (
                                        "SNES_EDGE_TRANSITION_SEAM" if tname == "SNES" else "GEN_EDGE_TRANSITION_SEAM")
                                    add_issue("ERROR", "SEAM", code, tname, x, y,
                                              f"{tname} cobble edge {mname} ({e}) clashes with {meta_names[nmeta]} (border diff {d:.1f}, master {d_master:.1f})")

            # 4. Local Zone River Bank <-> Grass Continuity (all 3 builds; accounts for shaded bank turf lip!)
            elif not is_globe and mname.startswith("CM_SHORE_") and eff_pal != 3:
                drawn_sh = expected_shore_edges_from_meta(mname)
                for e, dx, dy, side, nb_first in CARDINAL:
                    if e not in drawn_sh:
                        continue
                    nb = get_cell(x + dx, y + dy)
                    if nb is not None and nb[0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                        nmeta = nb[3]
                        d_master = border_diff(0, nmeta, meta, side) if nb_first else border_diff(0, meta, nmeta, side)
                        for tidx, (tname, _tiles, _pals) in enumerate(targets):
                            d = d_master if tidx == 0 else (border_diff(tidx, nmeta, meta, side) if nb_first else border_diff(tidx, meta, nmeta, side))
                            # Shaded river turf rim (TPAL_WATER 9..10) vs sunlit grass can legitimately differ up to ~60;
                            # flag harsh mismatches OR console tile-folding degradation over Master!
                            lim = 60.0 if tname == "Master" else (64.0 if tname == "SNES" else 44.0)
                            deg_lim = 14.0 if tname == "SNES" else 22.0
                            degraded = (tidx > 0 and d > 30.0 and (d - d_master) > deg_lim)
                            if d > lim or degraded:
                                code = "EDGE_TRANSITION_SEAM" if tname == "Master" else (
                                    "SNES_EDGE_TRANSITION_SEAM" if tname == "SNES" else "GEN_EDGE_TRANSITION_SEAM")
                                add_issue("ERROR", "SEAM", code, tname, x, y,
                                          f"{tname} shore bank {mname} ({e}) clashes with {meta_names[nmeta]} (border diff {d:.1f}, master {d_master:.1f})")

    return issues


def audit_genesis_zone_seams(zone_info, cells, meta_names, gen_meta_tiles, gen_pal_rgb, name_to_id):
    """
    Genesis-specific CRAM visual seam audit:
      1. Opaque mountain-vista canopies (CM_OPQ_*) must blend cleanly onto adjacent grass (no blue boxes!).
      2. River banks (CM_SHORE_*) must blend cleanly onto adjacent grass in Genesis CRAM (no black/grey hatching!).
      3. Cobble/rocky feathered edges (CM_COBBLE_EDGE/CORNER) must blend cleanly onto adjacent grass (no blue seams!).
    """
    issues = []
    zid = zone_info["id"]
    zname = zone_info["name"]
    w, h = zone_info["w"], zone_info["h"]
    pal_id = zone_info["pal_id"]
    has_vista = zone_info.get("has_mountain_vista", False)
    gp = gen_pal_rgb[pal_id]
    opq_base = name_to_id["CM_OPQ_CANOPY_TL"]

    def add_issue(sev, code, x, y, msg):
        issues.append({
            "severity": sev, "category": "SEAM", "code": code,
            "zone_id": zid, "zone": f"{zname} [Genesis]", "x": x, "y": y, "msg": msg,
        })

    for y in range(h):
        for x in range(w):
            bg1, bg2, _col, meta = cells[y * w + x]
            mname = meta_names[meta]

            # 1. Opaque mountain-vista canopy corner/edge match against adjacent grass
            if has_vista and 1 <= bg2 <= 12 and bg1 != T_MTN_SKY:
                omid = opq_base + (bg2 - 1)
                omname = meta_names[omid]
                # Only check canopies that expose background corners (TL, TR, TOP, PINE_TOP)
                if omname in ("CM_OPQ_MAPLE_TL", "CM_OPQ_MAPLE_TR", "CM_OPQ_MAPLE_TOP",
                              "CM_OPQ_CANOPY_TL", "CM_OPQ_CANOPY_TR", "CM_OPQ_PINE_TOP"):
                    grass_nbs = []
                    for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                        if 0 <= nx < w and 0 <= ny < h:
                            nb = cells[ny * w + nx]
                            if nb[0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS) and nb[1] == 0:
                                grass_nbs.append(nb)
                    if grass_nbs:
                        g_rgb = tile_mean_rgb(gen_meta_tiles[grass_nbs[0][3]], gp, None)
                        c_rgb = tile_mean_rgb(gen_meta_tiles[omid], gp, ((0, 0), (15, 0), (0, 1), (15, 1)))
                        cdiff = (abs(c_rgb[0] - g_rgb[0]) + abs(c_rgb[1] - g_rgb[1]) + abs(c_rgb[2] - g_rgb[2])) / 3.0
                        if cdiff > 40.0:
                            add_issue("ERROR", "GEN_CANOPY_BG_SEAM", x, y,
                                      f"Genesis {omname} background box RGB {tuple(int(v) for v in c_rgb)} clashes with grass {tuple(int(v) for v in g_rgb)} (diff {cdiff:.1f})")

            # 2. River bank / shore edge match against adjacent grass in Genesis CRAM
            if mname.startswith("CM_SHORE_") and pal_id != 3:
                edges = expected_shore_edges_from_meta(mname)
                tA = gen_meta_tiles[meta]
                for edge, dx, dy, side in (("W", -1, 0, "E"), ("E", 1, 0, "E"), ("N", 0, -1, "S"), ("S", 0, 1, "S")):
                    if edge not in edges:
                        continue
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < w and 0 <= ny < h:
                        nb = cells[ny * w + nx]
                        if nb[0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                            tB = gen_meta_tiles[nb[3]]
                            d = tile_border_rgb_diff(tB, tA, gp, side) if (dx < 0 or dy < 0) else tile_border_rgb_diff(tA, tB, gp, side)
                            if d > 44.0:
                                add_issue("ERROR", "GEN_SHORE_BANK_SEAM", x, y,
                                          f"Genesis {mname} {edge}-bank clashes with adjacent grass (CRAM border diff {d:.1f})")

            # 3. Cobble/rocky feathered edge match against adjacent grass in Genesis CRAM
            if mname.startswith("CM_COBBLE_EDGE_") or mname.startswith("CM_COBBLE_CORNER_"):
                tA = gen_meta_tiles[meta]
                checks = []
                if "W" in mname and x > 0: checks.append((x - 1, y, "E", True))
                if "E" in mname and x + 1 < w: checks.append((x + 1, y, "E", False))
                if "N" in mname and y > 0: checks.append((x, y - 1, "S", True))
                if "S" in mname and y + 1 < h: checks.append((x, y + 1, "S", False))
                for nx, ny, side, nb_first in checks:
                    nb = cells[ny * w + nx]
                    if nb[0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                        tB = gen_meta_tiles[nb[3]]
                        d = tile_border_rgb_diff(tB, tA, gp, side) if nb_first else tile_border_rgb_diff(tA, tB, gp, side)
                        if d > 36.0:
                            add_issue("ERROR", "GEN_COBBLE_EDGE_SEAM", x, y,
                                      f"Genesis {mname} clashes with adjacent grass (CRAM border diff {d:.1f})")

    return issues


# ============================================================================
# Core 6-Pass Auditor
# ============================================================================
def audit_local_zone(zone, cells, all_zones_by_id, inbound_spawns, meta_names, meta_tiles, pal_rgb, water_flows):
    issues = []
    w, h = zone["w"], zone["h"]
    zid = zone["id"]
    zname = zone["name"]
    pal_id = zone["pal_id"]

    def add_issue(sev, cat, code, x, y, msg):
        issues.append({
            "severity": sev, "category": cat, "code": code,
            "zone_id": zid, "zone": zname, "x": x, "y": y, "msg": msg,
        })

    def cell(x, y):
        cx = max(0, min(w - 1, x))
        cy = max(0, min(h - 1, y))
        return cells[cy * w + cx]

    # --- PASS 1: BFS Traversability & Reachability ---
    walkable_set = set()
    for y in range(h):
        for x in range(w):
            col = cells[y * w + x][2]
            if col in (COL_WALKABLE, COL_TALL_GRASS, COL_SHALLOW):
                walkable_set.add((x, y))

    # Determine seed spawns: all inbound portal destinations + valid walkable center fallback
    seeds = []
    for (sx, sy, src_label) in inbound_spawns.get(zid, []):
        if 0 <= sx < w and 0 <= sy < h:
            if (sx, sy) not in walkable_set:
                add_issue("CRITICAL", "BLOCKED", "SPAWN_BLOCKED", sx, sy,
                          f"Inbound spawn from {src_label} lands on non-walkable tile {T_NAMES.get(cell(sx,sy)[0], cell(sx,sy)[0])} (col={cell(sx,sy)[2]})")
            else:
                seeds.append((sx, sy))
    if not seeds and walkable_set:
        # Pick walkable tile nearest center
        cx, cy = w // 2, h // 2
        best = min(walkable_set, key=lambda p: abs(p[0] - cx) + abs(p[1] - cy))
        seeds.append(best)

    reachable = set()
    if seeds:
        q = collections.deque([seeds[0]])
        reachable.add(seeds[0])
        while q:
            ux, uy = q.popleft()
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = ux + dx, uy + dy
                if (nx, ny) in walkable_set and (nx, ny) not in reachable:
                    reachable.add((nx, ny))
                    q.append((nx, ny))

        # Check all other inbound spawns are in same connected component
        for (sx, sy, src_label) in inbound_spawns.get(zid, []):
            if (sx, sy) in walkable_set and (sx, sy) not in reachable:
                add_issue("CRITICAL", "BLOCKED", "SPAWN_DISCONNECTED", sx, sy,
                          f"Inbound spawn from {src_label} is isolated from primary walkable region")

    # Check portals reachable & destination valid
    for p in zone["portals"]:
        px, py = p["tx"], p["ty"]
        # Portal trigger works either by stepping ON (px,py) or adjacent (doors)
        adj_reach = (px, py) in reachable or any((px + dx, py + dy) in reachable for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)))
        if not adj_reach:
            add_issue("CRITICAL", "BLOCKED", "PORTAL_UNREACHABLE", px, py,
                      f"Portal '{p['label']}' -> zone {p['dst_zone']} cannot be reached from spawn")

    # Check NPCs reachable (adjacent walkable tile)
    for npc in zone["npcs"]:
        nx, ny = npc["tx"], npc["ty"]
        adj_reach = any((nx + dx, ny + dy) in reachable for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)))
        if not adj_reach:
            add_issue("ERROR", "BLOCKED", "NPC_UNREACHABLE", nx, ny,
                      f"NPC '{npc['name']}' has no reachable adjacent tile")

    # Check chest, fishing spot, waystone, sign
    for key, label in (("chest", "Treasure Chest"), ("fish_spot", "Fishing Spot"),
                       ("waystone", "Waystone"), ("sign", "Signpost")):
        obj = zone.get(key)
        if obj:
            ox, oy = obj["tx"], obj["ty"]
            adj_reach = any((ox + dx, oy + dy) in reachable for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)))
            if not adj_reach:
                add_issue("ERROR", "BLOCKED", "PROP_UNREACHABLE", ox, oy,
                          f"{label} at ({ox},{oy}) is unreachable")

    # Detect isolated walkable pockets (excluding tiny 1-3 tile decorative cutouts)
    unreached = walkable_set - reachable
    seen_u = set()
    for p in sorted(unreached):
        if p in seen_u:
            continue
        comp = []
        q = collections.deque([p])
        seen_u.add(p)
        while q:
            ux, uy = q.popleft()
            comp.append((ux, uy))
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                np = (ux + dx, uy + dy)
                if np in unreached and np not in seen_u:
                    seen_u.add(np)
                    q.append(np)
        if len(comp) >= 6:
            # Skip interior roof/border backdrop strips (y < 4 or x < 2 or x >= w-2) if mountain vista sky
            interior_pts = [pt for pt in comp if 2 <= pt[0] < w - 2 and 3 <= pt[1] < h - 2]
            if len(interior_pts) >= 6:
                rep = interior_pts[0]
                add_issue("WARNING", "BLOCKED", "ISOLATED_POCKET", rep[0], rep[1],
                          f"Disconnected walkable pocket of {len(comp)} tiles unreachable from spawn")

    # --- PASS 2, 3, 4, 5: Per-Cell Semantic, Autotile, Flow, Seam & Overlay Checks ---
    portal_coords = {(p["tx"], p["ty"]) for p in zone["portals"]}

    for y in range(h):
        for x in range(w):
            bg1, bg2, col, meta = cells[y * w + x]
            mname = meta_names[meta]
            tn = cell(x, y - 1)[0]
            ts = cell(x, y + 1)[0]
            tw = cell(x - 1, y)[0]
            te = cell(x + 1, y)[0]

            # Pass 2: Collision <-> Visual Parity
            if bg1 in EXPECT_WALKABLE and col == COL_SOLID:
                add_issue("ERROR", "COLLISION", "WALKABLE_TILE_SOLID", x, y,
                          f"Walkable-looking tile {T_NAMES.get(bg1, bg1)} marked COL_SOLID")
            elif bg1 in EXPECT_SOLID and col == COL_WALKABLE and (x, y) not in portal_coords:
                # Note: interior doorways / secret stairs can be portals; otherwise solid tiles must be solid
                add_issue("ERROR", "COLLISION", "SOLID_TILE_WALKABLE", x, y,
                          f"Solid-looking tile {T_NAMES.get(bg1, bg1)} marked COL_WALKABLE")
            elif bg1 in (T_WATER, T_RAPIDS, T_WORLD_OCEAN) and col == COL_WALKABLE:
                add_issue("ERROR", "COLLISION", "WATER_WALKABLE", x, y,
                          f"Deep water/rapids {T_NAMES.get(bg1, bg1)} marked COL_WALKABLE")

            # Pass 3a: Water & Aquatic Feature Bank Mask Completeness + Vertical/Horizontal River Orientation
            if bg1 in AQUATIC_TILES:
                ln = is_shore_land(tn, False) if y > 0 else False
                ls = is_shore_land(ts, False) if y < h - 1 else False
                lw = is_shore_land(tw, False) if x > 0 else False
                le = is_shore_land(te, False) if x < w - 1 else False
                lnw = is_shore_land(cell(x - 1, y - 1)[0], False) if (x > 0 and y > 0) else False
                lne = is_shore_land(cell(x + 1, y - 1)[0], False) if (x < w - 1 and y > 0) else False
                lsw = is_shore_land(cell(x - 1, y + 1)[0], False) if (x > 0 and y < h - 1) else False
                lse = is_shore_land(cell(x + 1, y + 1)[0], False) if (x < w - 1 and y < h - 1) else False
                if bg1 == T_WORLD_CORAL:
                    if (ln or ls or lw or le or lnw or lne or lsw or lse) and ("SHORE_" not in mname and "COAST_" not in mname):
                        add_issue("ERROR", "AUTOTILE", "AQUATIC_FEATURE_ON_SHORE", x, y,
                                  f"Aquatic feature {T_NAMES[bg1]} touches land without a shoreline transition (resolved to {mname})")
                else:
                    needed = set()
                    if ln: needed.add("N")
                    if ls: needed.add("S")
                    if lw: needed.add("W")
                    if le: needed.add("E")
                    drawn = expected_shore_edges_from_meta(mname)
                    if needed and not needed.issubset(drawn):
                        missing = "".join(sorted(needed - drawn))
                        add_issue("ERROR", "AUTOTILE", "MISSING_RIVER_BANK", x, y,
                                  f"Water tile needs banks {''.join(sorted(needed))} but {mname} only draws {''.join(sorted(drawn)) or 'NONE'} (missing {missing})")

                # Narrow vertical N-S river channel (length >= 5 tiles, shore land within 3 tiles W & E)
                land_w3 = any(x >= d and is_shore_land(cell(x - d, y)[0], False) for d in (1, 2, 3))
                land_e3 = any(x + d < w and is_shore_land(cell(x + d, y)[0], False) for d in (1, 2, 3))
                run_n = 0
                for d in range(1, 5):
                    if y >= d and cell(x, y - d)[0] in (WATER_TILES | {T_BRIDGE, T_BRIDGE_RAIL_N, T_BRIDGE_RAIL_S}):
                        run_n += 1
                    else:
                        break
                run_s = 0
                for d in range(1, 5):
                    if y + d < h and cell(x, y + d)[0] in (WATER_TILES | {T_BRIDGE, T_BRIDGE_RAIL_N, T_BRIDGE_RAIL_S}):
                        run_s += 1
                    else:
                        break
                is_vert_river = (y > 0 and tn in WATER_TILES and y < h - 1 and ts in WATER_TILES and
                                 land_w3 and land_e3 and (run_n + 1 + run_s) >= 5 and not (ln or ls) and
                                 not mname.startswith("CM_SHORE_I"))

                # Check water optical flow direction on vertical rivers!
                if is_vert_river and meta in water_flows:
                    vx, vy, _conf = water_flows[meta]
                    if vx != 0 and vy == 0:
                        add_issue("ERROR", "FLOW", "WRONG_RIVER_FLOW_AXIS", x, y,
                                  f"Vertical N-S river tile ({mname}) animates horizontally (vx={vx}, vy={vy}) instead of vertically")

                # Check opposing animated flow with E or S water neighbor!
                if meta in water_flows:
                    vx, vy, _ = water_flows[meta]
                    if x + 1 < w and cells[y * w + (x + 1)][0] in WATER_TILES:
                        rmeta = cells[y * w + (x + 1)][3]
                        if rmeta in water_flows:
                            rvx, rvy, _ = water_flows[rmeta]
                            if (vx != 0 and rvx != 0 and vx * rvx < 0) or (vy != 0 and rvy != 0 and vy * rvy < 0):
                                add_issue("ERROR", "FLOW", "OPPOSING_WATER_FLOW", x, y,
                                          f"Adjacent water tiles {mname} (v={vx},{vy}) and {meta_names[rmeta]} (v={rvx},{rvy}) flow in opposite directions")
                    if y + 1 < h and cells[(y + 1) * w + x][0] in WATER_TILES:
                        dmeta = cells[(y + 1) * w + x][3]
                        if dmeta in water_flows:
                            dvx, dvy, _ = water_flows[dmeta]
                            if (vx != 0 and dvx != 0 and vx * dvx < 0) or (vy != 0 and dvy != 0 and vy * dvy < 0):
                                add_issue("ERROR", "FLOW", "OPPOSING_WATER_FLOW", x, y,
                                          f"Adjacent water tiles {mname} (v={vx},{vy}) and {meta_names[dmeta]} (v={dvx},{dvy}) flow in opposite directions")

            # Pass 3b: Bridge orientation check (N-S bridge crossing E-W canal must use CM_BRIDGE_NS)
            if bg1 == T_BRIDGE:
                water_w = (x > 0 and tw in WATER_TILES)
                water_e = (x < w - 1 and te in WATER_TILES)
                water_n = (y > 0 and tn in WATER_TILES)
                water_s = (y < h - 1 and ts in WATER_TILES)
                road_ns = tn in (T_BRIDGE, T_COBBLE, T_ROCKY_PATH, T_DIRT, T_SHRINE_FLOOR) or ts in (T_BRIDGE, T_COBBLE, T_ROCKY_PATH, T_DIRT, T_SHRINE_FLOOR)
                if (water_w or water_e) and road_ns and ((not water_n and not water_s) or (water_w and water_e)):
                    if mname == "T_BRIDGE":
                        add_issue("ERROR", "AUTOTILE", "BRIDGE_ORIENTATION", x, y,
                                  f"N-S bridge crossing horizontal E-W canal uses E-W plank tile ({mname})")

            # Pass 3c: Cliff wall/ledge stacking check
            if bg1 == T_CLIFF_WALL and y > 0:
                if tn not in (T_CLIFF_WALL, T_CLIFF_LEDGE, T_MTN_SKY, T_STONE_STAIRS, T_WATER, T_RAPIDS, T_HOLLOW_OAK):
                    # Walkable grass/dirt directly north of a cliff wall without a ledge rim looks cut off
                    if cell(x, y - 1)[2] in (COL_WALKABLE, COL_TALL_GRASS):
                        add_issue("WARNING", "AUTOTILE", "CLIFF_MISSING_LEDGE", x, y,
                                  f"T_CLIFF_WALL directly below walkable {T_NAMES.get(tn, tn)} without T_CLIFF_LEDGE cap")

            # Pass 3d: Prop underlay mismatch check (grass prop on sand/cobble/wood, sandy palm on grass)
            if bg1 in (T_LANTERN_POST, T_SIGNPOST, T_FLOWER_BED, T_BUSH, T_FENCE, T_BOULDER):
                nb_tiles = [tn, ts, tw, te]
                has_wood = any(nt in (T_WOOD_FLOOR, T_HEARTH_RUG) for nt in nb_tiles)
                has_sand = any(nt in (T_WORLD_SAND, T_WORLD_BEACH) for nt in nb_tiles)
                has_cob  = any(nt in (T_COBBLE, T_ROCKY_PATH, T_SHRINE_FLOOR) for nt in nb_tiles)
                if has_wood and not ("WOOD" in mname or "COUNTER" in mname or "LECTERN" in mname):
                    add_issue("ERROR", "AUTOTILE", "PROP_GROUND_MISMATCH", x, y,
                              f"Indoor prop {T_NAMES[bg1]} resolved to outdoor tile {mname} next to wood floor")
                elif has_cob and not has_wood and bg1 == T_BOULDER and mname == "T_BOULDER":
                    if not any(nt in GRASS_LIKE for nt in nb_tiles):
                        add_issue("WARNING", "AUTOTILE", "PROP_GROUND_MISMATCH", x, y,
                                  f"Prop {T_NAMES[bg1]} resolved to grassy {mname} with no grass neighbors")
                elif has_sand and not any(nt in GRASS_LIKE for nt in nb_tiles) and mname == T_NAMES[bg1]:
                    add_issue("WARNING", "AUTOTILE", "PROP_GROUND_MISMATCH", x, y,
                              f"Prop {T_NAMES[bg1]} resolved to grassy {mname} surrounded by sand/beach")
            elif bg1 == T_PALM_TREE:
                nb_tiles = [tn, ts, tw, te]
                gc = sum(1 for nt in nb_tiles if nt in GRASS_LIKE)
                sc = sum(1 for nt in nb_tiles if nt in (T_WORLD_SAND, T_WORLD_BEACH))
                if gc >= 2 and sc == 0 and mname == "T_PALM_TREE":
                    add_issue("WARNING", "AUTOTILE", "PROP_GROUND_MISMATCH", x, y,
                              f"Palm tree on lush grass resolved to sandy {mname} instead of CM_PALM_GRASS")
                elif 1 <= x < w - 1 and 1 <= y < h - 1 and gc >= 1 and sc >= 1:
                    add_issue("WARNING", "SEAM", "PALM_ON_BIOME_SEAM", x, y,
                              f"Palm tree sits directly on grass-to-sand biome seam (breaks feathered sand-grass curve)")

            # Pass 3d2: Interior wall directly north of window/fireplace/wall must resolve to cutaway cap
            if bg1 == T_INTERIOR_WALL and y + 1 < h and ts in (T_WALL_WINDOW, T_FIREPLACE, T_WALL_WOOD):
                if mname != "CM_INTERIOR_CAP":
                    add_issue("ERROR", "AUTOTILE", "INTERIOR_WALL_CAP_PROTRUSION", x, y,
                              f"Interior wall above {T_NAMES[ts]} resolved to {mname} instead of CM_INTERIOR_CAP")

            # Pass 3d3: Rigid solid grid of coral reef tiles (all 4 cardinal neighbors also coral)
            if bg1 == T_WORLD_CORAL and 1 <= x < w - 1 and 1 <= y < h - 1:
                if tn == T_WORLD_CORAL and ts == T_WORLD_CORAL and tw == T_WORLD_CORAL and te == T_WORLD_CORAL:
                    add_issue("WARNING", "SEAM", "RIGID_CORAL_GRID", x, y,
                              "Solid >=3x3 grid of identical T_WORLD_CORAL tiles (scatter organically instead)")

            # Pass 3e: Miniature overworld icons should never be stamped into human-scale local zones
            if bg1 in WORLD_ICON_TILES:
                add_issue("ERROR", "AUTOTILE", "WORLD_ICON_IN_LOCAL_ZONE", x, y,
                          f"Miniature overworld icon {T_NAMES[bg1]} placed inside local zone {zname}")

            # Pass 3f: Pier south edge must open onto water, not a shore/coast rim
            if bg1 == T_DOCK_PIER and mname == "CM_PIER_WATER_EDGE" and y + 1 < h:
                s_bg1, _, _, s_meta = cells[(y + 1) * w + x]
                if s_bg1 in AQUATIC_TILES and "N" in expected_shore_edges_from_meta(meta_names[s_meta]):
                    add_issue("ERROR", "AUTOTILE", "PIER_BLOCKED_BY_COAST", x, y + 1,
                              f"Water south of T_DOCK_PIER draws north shore ({meta_names[s_meta]}) blocking pier")

            # Pass 3g: Grass-banked shore inside purely cobblestone plaza (excluding dark sewers)
            if bg1 in WATER_TILES and mname.startswith("CM_SHORE_") and pal_id != 3:
                land_nbs = [nt for nt in (tn, ts, tw, te) if is_shore_land(nt, False)]
                if land_nbs and all(nt in (T_COBBLE, T_ROCKY_PATH, T_SHRINE_FLOOR) for nt in land_nbs):
                    add_issue("WARNING", "SEAM", "WATER_BANK_GROUND_MISMATCH", x, y,
                              f"Grass-banked shore {mname} sits directly inside stone pavement with no grass border")

            # Pass 5: BG2 Overhead Canopy / Eave Anchor Audit
            if bg2 != O_NONE:
                if bg2 == O_ROOF_EAVE:
                    if y == 0 or tn not in (T_ROOF_BODY, T_ROOF_TOP, T_WINDMILL_ROOF):
                        add_issue("ERROR", "OVERLAY", "FLOATING_ROOF_EAVE", x, y,
                                  f"O_ROOF_EAVE has no roof tile above (north={T_NAMES.get(tn, tn)})")
                elif bg2 in (O_CANOPY_TL, O_CANOPY_TR, O_CANOPY_BL, O_CANOPY_BR):
                    # Should have a tree trunk within radius 2
                    found_trunk = False
                    for dy in range(-1, 3):
                        for dx in range(-2, 3):
                            if cell(x + dx, y + dy)[0] in (T_TREE_TRUNK, T_MAPLE_TRUNK, T_PINE_TRUNK, T_HOLLOW_OAK):
                                found_trunk = True
                                break
                        if found_trunk:
                            break
                    if not found_trunk:
                        add_issue("ERROR", "OVERLAY", "FLOATING_CANOPY", x, y,
                                  f"Overhead canopy BG2={bg2} has no tree trunk anchor nearby")
                elif bg2 == O_PINE_TOP:
                    if y + 1 >= h or ts != T_PINE_TRUNK:
                        add_issue("ERROR", "OVERLAY", "FLOATING_PINE_TOP", x, y,
                                  f"O_PINE_TOP has no T_PINE_TRUNK directly south (south={T_NAMES.get(ts, ts)})")
                elif bg2 == O_OAK_CROWN:
                    found_oak = any(cell(x + dx, y + dy)[0] in (T_HOLLOW_OAK, T_TREE_TRUNK) for dy in (0, 1, 2, 3) for dx in (-2, -1, 0, 1, 2))
                    if not found_oak:
                        add_issue("ERROR", "OVERLAY", "FLOATING_OAK_CROWN", x, y,
                                  "O_OAK_CROWN has no T_HOLLOW_OAK or T_TREE_TRUNK anchor below")
                # Tree canopies must never overhang water, bridges, or stone roads (causes opaque grass box on SNES/Genesis!)
                if bg2 in (O_CANOPY_TL, O_CANOPY_TR, O_CANOPY_BL, O_CANOPY_BR,
                           O_MAPLE_TL, O_MAPLE_TR, O_MAPLE_MID, O_MAPLE_TOP, O_PINE_TOP, O_OAK_CROWN):
                    if bg1 in (WATER_TILES | {T_BRIDGE, T_COBBLE, T_ROCKY_PATH, T_RAIL_TRACK, T_STONE_STAIRS}):
                        add_issue("ERROR", "OVERLAY", "CANOPY_OVER_WATER_OR_ROAD", x, y,
                                  f"Tree canopy BG2={bg2} overhangs {T_NAMES.get(bg1, bg1)} (creates opaque grass box on SNES/Genesis)")

    # Pass 4: Pixel-Level Border Seam Continuity Check (water-water, aquatic-feature & prop-ground seams)
    t0_arr = meta_tiles[0]
    zp = pal_rgb[pal_id]
    mean_rgb_cache = {}

    def get_mean_rgb(mid, corners_only=False):
        key = (mid, corners_only)
        res = mean_rgb_cache.get(key)
        if res is None:
            res = tile_mean_rgb(t0_arr[mid], zp, PROP_CORNER_COORDS if corners_only else None)
            mean_rgb_cache[key] = res
        return res

    self_seam_cache = {}
    def get_self_seam(mid, direction):
        k = (mid, direction)
        v = self_seam_cache.get(k)
        if v is None:
            v = tile_border_rgb_diff(t0_arr[mid], t0_arr[mid], zp, direction)
            self_seam_cache[k] = v
        return v

    for y in range(h):
        for x in range(w):
            bg1, _, _, meta = cells[y * w + x]
            mname = meta_names[meta]
            if bg1 in AQUATIC_TILES:
                drawn = expected_shore_edges_from_meta(mname)
                tA = t0_arr[meta]
                # Check East neighbor if BOTH are aquatic and neither draws a bank along E/W
                if x + 1 < w and cells[y * w + (x + 1)][0] in AQUATIC_TILES:
                    rbg1, _, _, rmeta = cells[y * w + (x + 1)]
                    rdrawn = expected_shore_edges_from_meta(meta_names[rmeta])
                    if "E" not in drawn and "W" not in rdrawn:
                        if bg1 == T_WORLD_CORAL or rbg1 == T_WORLD_CORAL:
                            if bg1 != rbg1:
                                avg_diff = tile_border_rgb_diff(tA, t0_arr[rmeta], zp, "E")
                                if avg_diff > 36.0:
                                    add_issue("ERROR", "SEAM", "WATER_FEATURE_SEAM", x, y,
                                              f"Unblended E/W aquatic feature seam ({avg_diff:.1f}) between {mname} and {meta_names[rmeta]}")
                        elif bg1 == rbg1:
                            avg_diff = tile_border_rgb_diff(tA, t0_arr[rmeta], zp, "E")
                            excess = avg_diff - max(get_self_seam(meta, "E"), get_self_seam(rmeta, "E"))
                            if avg_diff > 24.0 and excess > 18.0:
                                add_issue("ERROR", "SEAM", "WATER_EDGE_SEAM", x, y,
                                          f"Harsh E/W pixel seam ({avg_diff:.1f}, +{excess:.1f} over self) between {mname} and {meta_names[rmeta]}")
                # Check South neighbor
                if y + 1 < h and cells[(y + 1) * w + x][0] in AQUATIC_TILES:
                    dbg1, _, _, dmeta = cells[(y + 1) * w + x]
                    ddrawn = expected_shore_edges_from_meta(meta_names[dmeta])
                    if "S" not in drawn and "N" not in ddrawn:
                        if bg1 == T_WORLD_CORAL or dbg1 == T_WORLD_CORAL:
                            if bg1 != dbg1:
                                avg_diff = tile_border_rgb_diff(tA, t0_arr[dmeta], zp, "S")
                                if avg_diff > 36.0:
                                    add_issue("ERROR", "SEAM", "WATER_FEATURE_SEAM", x, y,
                                              f"Unblended N/S aquatic feature seam ({avg_diff:.1f}) between {mname} and {meta_names[dmeta]}")
                        elif bg1 == dbg1:
                            avg_diff = tile_border_rgb_diff(tA, t0_arr[dmeta], zp, "S")
                            excess = avg_diff - max(get_self_seam(meta, "S"), get_self_seam(dmeta, "S"))
                            if avg_diff > 24.0 and excess > 18.0:
                                add_issue("ERROR", "SEAM", "WATER_EDGE_SEAM", x, y,
                                          f"Harsh N/S pixel seam ({avg_diff:.1f}, +{excess:.1f} over self) between {mname} and {meta_names[dmeta]}")
            elif bg1 in PROP_TILES or bg1 == T_SHRINE_PILLAR:
                fam_nbs = {}
                for nx, ny in ((x, y - 1), (x, y + 1), (x - 1, y), (x + 1, y)):
                    if 0 <= nx < w and 0 <= ny < h:
                        nb = cells[ny * w + nx]
                        fam = ground_family(nb[0])
                        if fam is not None:
                            fam_nbs.setdefault(fam, []).append(nb)
                if fam_nbs:
                    ranked = sorted(fam_nbs.items(), key=lambda kv: len(kv[1]), reverse=True)
                    top_fam, top_list = ranked[0]
                    second_cnt = len(ranked[1][1]) if len(ranked) > 1 else 0
                    if len(top_list) >= 2 and len(top_list) > second_cnt:
                        prop_rgb = get_mean_rgb(meta, True)
                        gnd_rgb = get_mean_rgb(top_list[0][3], False)
                        cdiff = (abs(prop_rgb[0] - gnd_rgb[0]) + abs(prop_rgb[1] - gnd_rgb[1]) + abs(prop_rgb[2] - gnd_rgb[2])) / 3.0
                        if bg1 == T_SHRINE_PILLAR and top_fam == "GRASS" and cdiff > 35.0:
                            add_issue("ERROR", "SEAM", "TERRAIN_BIOME_SEAM", x, y,
                                      f"Stone pillar {mname} sits on GRASS with harsh border diff {cdiff:.1f}")
                        elif bg1 in (T_TREE_TRUNK, T_MAPLE_TRUNK, T_PINE_TRUNK) and top_fam == "STONE":
                            add_issue("ERROR", "SEAM", "TREE_ON_STONE_PATH", x, y,
                                      f"Tree trunk {T_NAMES[bg1]} planted directly on stone path ({T_NAMES[top_list[0][0]]})")
                        elif bg1 in PROP_TILES and cdiff > 38.0 and not (bg1 in (T_LANTERN_POST, T_SIGNPOST) and top_fam == "SAND"):
                            add_issue("WARNING", "SEAM", "PROP_BORDER_SEAM", x, y,
                                      f"Prop {mname} outer corner RGB differs from surrounding {top_fam} ({T_NAMES[top_list[0][0]]}) by {cdiff:.1f}")

            # Pass 4b: Cobble/Rocky-to-Grass feathered edge color match
            if mname.startswith("CM_COBBLE_EDGE_") or mname.startswith("CM_COBBLE_CORNER_"):
                tA = t0_arr[meta]
                if "W" in mname and x > 0 and cells[y * w + (x - 1)][0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                    avg_diff = tile_border_rgb_diff(t0_arr[cells[y * w + (x - 1)][3]], tA, zp, "E")
                    if avg_diff > 22.0:
                        add_issue("WARNING", "SEAM", "ROAD_EDGE_SEAM", x, y,
                                  f"Cobble grass-edge {mname} clashes with west grass (border diff {avg_diff:.1f})")
                if "E" in mname and x + 1 < w and cells[y * w + (x + 1)][0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                    avg_diff = tile_border_rgb_diff(tA, t0_arr[cells[y * w + (x + 1)][3]], zp, "E")
                    if avg_diff > 22.0:
                        add_issue("WARNING", "SEAM", "ROAD_EDGE_SEAM", x, y,
                                  f"Cobble grass-edge {mname} clashes with east grass (border diff {avg_diff:.1f})")
                if "N" in mname and y > 0 and cells[(y - 1) * w + x][0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                    avg_diff = tile_border_rgb_diff(t0_arr[cells[(y - 1) * w + x][3]], tA, zp, "S")
                    if avg_diff > 22.0:
                        add_issue("WARNING", "SEAM", "ROAD_EDGE_SEAM", x, y,
                                  f"Cobble grass-edge {mname} clashes with north grass (border diff {avg_diff:.1f})")
                if "S" in mname and y + 1 < h and cells[(y + 1) * w + x][0] in (T_GRASS, T_GRASS_ALT, T_AUTUMN_GRASS):
                    avg_diff = tile_border_rgb_diff(tA, t0_arr[cells[(y + 1) * w + x][3]], zp, "S")
                    if avg_diff > 22.0:
                        add_issue("WARNING", "SEAM", "ROAD_EDGE_SEAM", x, y,
                                  f"Cobble grass-edge {mname} clashes with south grass (border diff {avg_diff:.1f})")

    return issues


def audit_globe(globe_info, cells, meta_names, meta_tiles, pal_rgb, water_flows):
    issues = []
    w, h = globe_info["w"], globe_info["h"]
    reg_names = globe_info["regions"]

    def add_issue(sev, cat, code, x, y, msg, region=-1):
        rname = reg_names[region] if 0 <= region < len(reg_names) else "Evertale Globe"
        issues.append({
            "severity": sev, "category": cat, "code": code,
            "zone_id": 8, "zone": f"Globe:{rname}", "x": x, "y": y, "msg": msg,
        })

    # --- PASS 1: Per-Continent Traversability & Coastal Access BFS ---
    reg_walkable = collections.defaultdict(set)
    reg_coastal = collections.defaultdict(set)

    for y in range(h):
        for x in range(w):
            bg1, bg2, col, meta, reg = cells[y * w + x]
            if col in (COL_WALKABLE, COL_TALL_GRASS):
                reg_walkable[reg].add((x, y))
                # Check if adjacent to shallow/water (landing access for raft/ship!)
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx = (x + dx) % w
                    ny = (y + dy) % h
                    ncol = cells[ny * w + nx][2]
                    if ncol in (COL_WATER, COL_SHALLOW):
                        reg_coastal[reg].add((x, y))
                        break

    # Check every continent has coastal landing access AND a unified connected interior!
    for reg_id, rname in enumerate(reg_names):
        wset = reg_walkable[reg_id]
        cset = reg_coastal[reg_id]
        if not wset:
            add_issue("CRITICAL", "BLOCKED", "CONTINENT_NO_WALKABLE", 0, 0,
                      f"Continent {rname} has 0 walkable tiles!", reg_id)
            continue

        # Find all connected components of walkable tiles on this continent
        seen = set()
        comps = []
        for p in sorted(wset):
            if p in seen:
                continue
            comp = []
            q = collections.deque([p])
            seen.add(p)
            while q:
                ux, uy = q.popleft()
                comp.append((ux, uy))
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    np = ((ux + dx) % w, (uy + dy) % h)
                    if np in wset and np not in seen:
                        seen.add(np)
                        q.append(np)
            comps.append(comp)
        comps.sort(key=len, reverse=True)
        main_comp = set(comps[0])

        # Does main walkable component touch the coast (ship/raft disembarkable)?
        coastal_in_main = main_comp & cset
        if not coastal_in_main:
            rep = comps[0][0]
            add_issue("CRITICAL", "BLOCKED", "CONTINENT_SEALED_COAST", rep[0], rep[1],
                      f"Continent {rname} main walkable interior ({len(main_comp)} tiles) is COMPLETELY SEALED from the ocean/coastline by solid perimeter tiles!", reg_id)

        # Flag large disconnected walkable pockets on the continent
        for extra in comps[1:]:
            if len(extra) >= 10:
                rep = extra[0]
                add_issue("ERROR", "BLOCKED", "CONTINENT_SPLIT_POCKET", rep[0], rep[1],
                          f"Continent {rname} has isolated walkable pocket of {len(extra)} tiles cut off from main land ({len(main_comp)} tiles)", reg_id)

        # Check all landmarks in this region
        for lm in globe_info["landmarks"]:
            if lm["region"] != reg_id:
                continue
            lx, ly = lm["tx"], lm["ty"]
            # Check if landmark tile OR immediate south/adjacent tile is in main_comp
            approach = [(lx, ly), (lx, (ly + 1) % h), (lx, (ly - 1) % h), ((lx + 1) % w, ly), ((lx - 1) % w, ly)]
            if not any(pt in main_comp for pt in approach):
                add_issue("CRITICAL", "BLOCKED", "LANDMARK_UNREACHABLE", lx, ly,
                          f"Globe landmark '{lm['name']}' is not reachable from {rname} main walkable land", reg_id)

    # Global Raft BFS from Timber Pier (108, 114) to verify Heartland <-> Suncrest <-> Babana
    raft_vis = set()
    q = collections.deque([(108, 114)])
    raft_vis.add((108, 114))
    while q:
        ux, uy = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = (ux + dx) % w, (uy + dy) % h
            if (nx, ny) not in raft_vis and cells[ny * w + nx][2] in (COL_WALKABLE, COL_TALL_GRASS, COL_SHALLOW):
                raft_vis.add((nx, ny))
                q.append((nx, ny))
    for lm in globe_info["landmarks"]:
        if lm["region"] in (0, 1, 2):
            lx, ly = lm["tx"], lm["ty"]
            if not any(((lx + dx) % w, (ly + dy) % h) in raft_vis for dx, dy in ((0, 0), (0, 1), (0, -1), (1, 0), (-1, 0))):
                add_issue("CRITICAL", "BLOCKED", "RAFT_TIER_UNREACHABLE", lx, ly,
                          f"Landmark '{lm['name']}' unreachable by Foot/Raft from Heartland Timber Pier", lm["region"])

    # --- PASS 2, 3, 4 on all 49,152 Globe Cells ---
    t0_arr = meta_tiles[0]
    globe_mean_cache = {}

    # Pass 4-pre: Verify unified World Map ocean palette stays cool/aquatic (never lava-red!)
    zp_world = pal_rgb[GLOBE_UNIFIED_ZPAL]
    oc_rgb = tile_mean_rgb(t0_arr[T_WORLD_OCEAN], zp_world, None)
    if oc_rgb[0] > oc_rgb[2] + 30.0:
        add_issue("ERROR", "SEAM", "OCEAN_PALETTE_HUE", 0, 0,
                  f"Unified World Map ocean palette is red/lava-tinted (mean RGB={oc_rgb[0]:.0f},{oc_rgb[1]:.0f},{oc_rgb[2]:.0f})", 0)

    # Pass 3-pre: Detect miniature landmark icon spam (connected clusters of T_WORLD_CASTLE/TOWN/RUINS > 24 tiles!)
    icon_seen = set()
    for y in range(h):
        for x in range(w):
            bg1 = cells[y * w + x][0]
            if bg1 in WORLD_ICON_TILES and (x, y) not in icon_seen:
                comp = []
                q = collections.deque([(x, y)])
                icon_seen.add((x, y))
                while q:
                    ux, uy = q.popleft()
                    comp.append((ux, uy))
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        np = ((ux + dx) % w, (uy + dy) % h)
                        if np not in icon_seen and cells[np[1] * w + np[0]][0] == bg1:
                            icon_seen.add(np)
                            q.append(np)
                if len(comp) > 24:
                    rx, ry = comp[0]
                    reg0 = cells[ry * w + rx][4]
                    add_issue("ERROR", "AUTOTILE", "LANDMARK_ICON_SPAM", rx, ry,
                              f"Oversized cluster of {len(comp)} identical {T_NAMES[bg1]} miniature icons tiled across continent", reg0)

    globe_self_seam_cache = {}
    def get_globe_self_seam(pal_id, mid, direction):
        k = (pal_id, mid, direction)
        v = globe_self_seam_cache.get(k)
        if v is None:
            v = tile_border_rgb_diff(t0_arr[mid], t0_arr[mid], pal_rgb[pal_id], direction)
            globe_self_seam_cache[k] = v
        return v

    def get_globe_mean_rgb(pal_id, mid, mode=0):
        key = (pal_id, mid, mode)
        res = globe_mean_cache.get(key)
        if res is None:
            coords = None if mode == 0 else (PROP_CORNER_COORDS if mode == 1 else PEAK_TOP_CORNER_COORDS)
            res = tile_mean_rgb(t0_arr[mid], pal_rgb[pal_id], coords)
            globe_mean_cache[key] = res
        return res

    for y in range(h):
        yn = (y - 1) % h
        ys = (y + 1) % h
        for x in range(w):
            xw = (x - 1) % w
            xe = (x + 1) % w
            bg1, bg2, col, meta, reg = cells[y * w + x]
            mname = meta_names[meta]
            pal_id = GLOBE_UNIFIED_ZPAL
            zp = pal_rgb[pal_id]

            # Pass 2: Collision <-> Visual Parity
            if bg1 in EXPECT_WALKABLE and col == COL_SOLID:
                add_issue("ERROR", "COLLISION", "WALKABLE_TILE_SOLID", x, y,
                          f"Globe tile {T_NAMES.get(bg1, bg1)} marked COL_SOLID", reg)
            elif bg1 in EXPECT_SOLID and col == COL_WALKABLE:
                add_issue("ERROR", "COLLISION", "SOLID_TILE_WALKABLE", x, y,
                          f"Globe solid tile {T_NAMES.get(bg1, bg1)} marked COL_WALKABLE", reg)

            # Pass 3f: Pier south edge must not be blocked by a sandy coastline rim!
            if bg1 == T_DOCK_PIER and mname == "CM_PIER_WATER_EDGE":
                scell = cells[ys * w + x]
                if scell[0] in AQUATIC_TILES and "N" in expected_shore_edges_from_meta(meta_names[scell[3]]):
                    add_issue("ERROR", "AUTOTILE", "PIER_BLOCKED_BY_COAST", x, ys,
                              f"Globe water south of T_DOCK_PIER draws north coast ({meta_names[scell[3]]}) blocking pier", reg)

            # Pass 3g: Incomplete sand-to-grass transition mask (pinched sand OR isolated 1x1 grass dot on beach/sand)
            if bg1 in (T_WORLD_BEACH, T_WORLD_SAND):
                gn = cells[yn * w + x][0] in GRASS_LIKE
                gs = cells[ys * w + x][0] in GRASS_LIKE
                gw = cells[y * w + xw][0] in GRASS_LIKE
                ge = cells[y * w + xe][0] in GRASS_LIKE
                if (gn and gs) or (gw and ge):
                    add_issue("WARNING", "SEAM", "SAND_GRASS_INCOMPLETE_MASK", x, y,
                              f"Sand/beach tile {mname} pinched between opposite grass tiles with no dual-edge transition", reg)
            elif bg1 in (T_GRASS, T_GRASS_ALT):
                sand_cnt = sum(1 for nb in (cells[yn * w + x], cells[ys * w + x], cells[y * w + xw], cells[y * w + xe])
                               if nb[0] in (T_WORLD_BEACH, T_WORLD_SAND))
                if sand_cnt >= 3:
                    add_issue("WARNING", "SEAM", "SAND_GRASS_INCOMPLETE_MASK", x, y,
                              f"Isolated 1x1 {T_NAMES[bg1]} polka-dot surrounded by {sand_cnt} beach/sand tiles", reg)

            # Pass 3 & 4: Coastline bank completeness, aquatic feature overlap, opposing flow & pixel seams
            if bg1 in AQUATIC_TILES:
                tn = cells[yn * w + x][0]
                ts = cells[ys * w + x][0]
                tw = cells[y * w + xw][0]
                te = cells[y * w + xe][0]
                tnw = cells[yn * w + xw][0]
                tne = cells[yn * w + xe][0]
                tsw = cells[ys * w + xw][0]
                tse = cells[ys * w + xe][0]
                ln = is_shore_land(tn, True)
                ls = is_shore_land(ts, True)
                lw = is_shore_land(tw, True)
                le = is_shore_land(te, True)
                lnw = is_shore_land(tnw, True)
                lne = is_shore_land(tne, True)
                lsw = is_shore_land(tsw, True)
                lse = is_shore_land(tse, True)
                drawn = expected_shore_edges_from_meta(mname)
                if bg1 == T_WORLD_CORAL:
                    if (ln or ls or lw or le or lnw or lne or lsw or lse) and ("SHORE_" not in mname and "COAST_" not in mname):
                        add_issue("ERROR", "AUTOTILE", "AQUATIC_FEATURE_ON_SHORE", x, y,
                                  f"Globe aquatic feature {T_NAMES[bg1]} touches land without a coastline transition (resolved to {mname})", reg)
                else:
                    needed = set()
                    if ln: needed.add("N")
                    if ls: needed.add("S")
                    if lw: needed.add("W")
                    if le: needed.add("E")
                    if needed and not needed.issubset(drawn):
                        missing = "".join(sorted(needed - drawn))
                        add_issue("ERROR", "AUTOTILE", "MISSING_COAST_EDGE", x, y,
                                  f"Globe coast needs {''.join(sorted(needed))} but {mname} draws {''.join(sorted(drawn)) or 'NONE'} (missing {missing})", reg)

                # Opposing flow with E neighbor (sample only E/S to avoid double counting)
                if meta in water_flows:
                    vx, vy, _ = water_flows[meta]
                    rmeta = cells[y * w + xe][3]
                    if cells[y * w + xe][0] in WATER_TILES and rmeta in water_flows:
                        rvx, rvy, _ = water_flows[rmeta]
                        if (vx != 0 and rvx != 0 and vx * rvx < 0) or (vy != 0 and rvy != 0 and vy * rvy < 0):
                            add_issue("ERROR", "FLOW", "OPPOSING_COAST_FLOW", x, y,
                                      f"Opposing water animation: {mname} (v={vx},{vy}) next to {meta_names[rmeta]} (v={rvx},{rvy})", reg)

                # Pass 4 on Globe: E & S pixel border seam checks (within same biome palette)
                tA = t0_arr[meta]
                rcell = cells[y * w + xe]
                if rcell[0] in AQUATIC_TILES and rcell[4] == reg:
                    rmeta = rcell[3]
                    rdrawn = expected_shore_edges_from_meta(meta_names[rmeta])
                    if "E" not in drawn and "W" not in rdrawn:
                        if bg1 == T_WORLD_CORAL or rcell[0] == T_WORLD_CORAL:
                            if bg1 != rcell[0]:
                                avg_diff = tile_border_rgb_diff(tA, t0_arr[rmeta], zp, "E")
                                if avg_diff > 36.0:
                                    add_issue("ERROR", "SEAM", "WATER_FEATURE_SEAM", x, y,
                                              f"Unblended E/W aquatic feature seam ({avg_diff:.1f}) between {mname} and {meta_names[rmeta]}", reg)
                        elif bg1 == rcell[0]:
                            avg_diff = tile_border_rgb_diff(tA, t0_arr[rmeta], zp, "E")
                            excess = avg_diff - max(get_globe_self_seam(pal_id, meta, "E"), get_globe_self_seam(pal_id, rmeta, "E"))
                            if avg_diff > 24.0 and excess > 18.0:
                                add_issue("ERROR", "SEAM", "WATER_EDGE_SEAM", x, y,
                                          f"Harsh E/W globe water seam ({avg_diff:.1f}, +{excess:.1f} over self) between {mname} and {meta_names[rmeta]}", reg)
                dcell = cells[ys * w + x]
                if dcell[0] in AQUATIC_TILES and dcell[4] == reg:
                    dmeta = dcell[3]
                    ddrawn = expected_shore_edges_from_meta(meta_names[dmeta])
                    if "S" not in drawn and "N" not in ddrawn:
                        if bg1 == T_WORLD_CORAL or dcell[0] == T_WORLD_CORAL:
                            if bg1 != dcell[0]:
                                avg_diff = tile_border_rgb_diff(tA, t0_arr[dmeta], zp, "S")
                                if avg_diff > 36.0:
                                    add_issue("ERROR", "SEAM", "WATER_FEATURE_SEAM", x, y,
                                              f"Unblended N/S aquatic feature seam ({avg_diff:.1f}) between {mname} and {meta_names[dmeta]}", reg)
                        elif bg1 == dcell[0]:
                            avg_diff = tile_border_rgb_diff(tA, t0_arr[dmeta], zp, "S")
                            excess = avg_diff - max(get_globe_self_seam(pal_id, meta, "S"), get_globe_self_seam(pal_id, dmeta, "S"))
                            if avg_diff > 24.0 and excess > 18.0:
                                add_issue("ERROR", "SEAM", "WATER_EDGE_SEAM", x, y,
                                          f"Harsh N/S globe water seam ({avg_diff:.1f}, +{excess:.1f} over self) between {mname} and {meta_names[dmeta]}", reg)
            elif bg1 in PROP_TILES or bg1 in BIOME_FEATURE_TILES:
                fam_nbs = {}
                for nb in (cells[yn * w + x], cells[ys * w + x], cells[y * w + xw], cells[y * w + xe]):
                    if nb[4] == reg:
                        fam = ground_family(nb[0])
                        if fam is not None:
                            fam_nbs.setdefault(fam, []).append(nb)
                if fam_nbs:
                    ranked = sorted(fam_nbs.items(), key=lambda kv: len(kv[1]), reverse=True)
                    top_fam, top_list = ranked[0]
                    second_cnt = len(ranked[1][1]) if len(ranked) > 1 else 0
                    min_nbs = 1 if bg1 == T_WORLD_PEAK else 2
                    if len(top_list) >= min_nbs and (bg1 == T_WORLD_PEAK or len(top_list) > second_cnt):
                        if bg1 == T_WORLD_PEAK and top_fam == "SNOW":
                            feat_rgb = get_globe_mean_rgb(pal_id, meta, 2)
                            gnd_rgb = get_globe_mean_rgb(pal_id, top_list[0][3], 0)
                            cdiff = (abs(feat_rgb[0] - gnd_rgb[0]) + abs(feat_rgb[1] - gnd_rgb[1]) + abs(feat_rgb[2] - gnd_rgb[2])) / 3.0
                            if cdiff > 52.0:
                                add_issue("ERROR", "SEAM", "TERRAIN_BIOME_SEAM", x, y,
                                          f"Snow-biome {mname} top backdrop RGB differs from SNOW by {cdiff:.1f}", reg)
                        elif bg1 in (T_WORLD_HILL, T_FLOWERS) and top_fam in ("SNOW", "JUNGLE"):
                            feat_rgb = get_globe_mean_rgb(pal_id, meta, 1)
                            gnd_rgb = get_globe_mean_rgb(pal_id, top_list[0][3], 0)
                            cdiff = (abs(feat_rgb[0] - gnd_rgb[0]) + abs(feat_rgb[1] - gnd_rgb[1]) + abs(feat_rgb[2] - gnd_rgb[2])) / 3.0
                            lim = 20.0 if (bg1 == T_FLOWERS and top_fam == "JUNGLE") else 32.0
                            if cdiff > lim:
                                add_issue("ERROR", "SEAM", "TERRAIN_BIOME_SEAM", x, y,
                                          f"Globe {mname} clashes with surrounding {top_fam} (corner diff {cdiff:.1f})", reg)
                        elif bg1 in PROP_TILES and len(top_list) >= 2 and len(top_list) > second_cnt:
                            prop_rgb = get_globe_mean_rgb(pal_id, meta, 1)
                            gnd_rgb = get_globe_mean_rgb(pal_id, top_list[0][3], 0)
                            cdiff = (abs(prop_rgb[0] - gnd_rgb[0]) + abs(prop_rgb[1] - gnd_rgb[1]) + abs(prop_rgb[2] - gnd_rgb[2])) / 3.0
                            if cdiff > 45.0:
                                add_issue("WARNING", "SEAM", "PROP_BORDER_SEAM", x, y,
                                          f"Globe prop {mname} outer corner RGB differs from surrounding {top_fam} ({T_NAMES[top_list[0][0]]}) by {cdiff:.1f}", reg)

            # Pass 5: BG2 Overlay Anchor on Globe
            if bg2 == O_OAK_CROWN:
                found = any(cells[((y + dy) % h) * w + ((x + dx) % w)][0] == T_HOLLOW_OAK for dy in (0, 1, 2) for dx in (-2, -1, 0, 1, 2))
                if not found:
                    add_issue("ERROR", "OVERLAY", "FLOATING_OAK_CROWN", x, y,
                              "Globe O_OAK_CROWN has no T_HOLLOW_OAK anchor below", reg)

    return issues


def annotate_and_save_png(out_path, img_w, img_h, buf, issues, crop=None, scale=1):
    # Overlay issue highlights onto RGB buffer
    x0, y0 = (crop[0], crop[1]) if crop else (0, 0)
    x1, y1 = (crop[2], crop[3]) if crop else (img_w // 16, img_h // 16)
    seen_cell = {}
    sev_rank = {"CRITICAL": 3, "ERROR": 2, "WARNING": 1}
    for iss in issues:
        ix, iy = iss["x"], iss["y"]
        if x0 <= ix < x1 and y0 <= iy < y1:
            k = (ix, iy)
            if k not in seen_cell or sev_rank[iss["severity"]] > sev_rank[seen_cell[k]["severity"]]:
                seen_cell[k] = iss

    for (ix, iy), iss in seen_cell.items():
        col = CAT_COLORS.get(iss["category"], (255, 255, 255))
        px = (ix - x0) * 16
        py = (iy - y0) * 16
        tint_tile(buf, img_w, img_h, px, py, 16, col, alpha=0.38)
        thick = 2 if iss["severity"] == "CRITICAL" else 1
        draw_rect_outline(buf, img_w, img_h, px, py, 16, 16, col, thick=thick)

    write_png_rgb(out_path, img_w, img_h, buf)


# ============================================================================
# Crisp 5x7 Bitmap Font & Labeled Contact-Sheet Builder (Zero External Deps)
# ============================================================================
FONT_5X7 = {
    " ": (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    "-": (0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00),
    "_": (0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F),
    ".": (0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C),
    ":": (0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00),
    "/": (0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00),
    "(": (0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04),
    ")": (0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08),
    "!": (0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04),
    "'": (0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00),
    "[": (0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E),
    "]": (0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E),
    "&": (0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D),
    "0": (0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E),
    "1": (0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E),
    "2": (0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F),
    "3": (0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E),
    "4": (0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02),
    "5": (0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E),
    "6": (0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E),
    "7": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08),
    "8": (0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E),
    "9": (0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C),
    "A": (0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "B": (0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E),
    "C": (0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E),
    "D": (0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C),
    "E": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F),
    "F": (0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10),
    "G": (0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F),
    "H": (0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11),
    "I": (0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E),
    "J": (0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C),
    "K": (0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11),
    "L": (0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F),
    "M": (0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11),
    "N": (0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11),
    "O": (0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    "P": (0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10),
    "Q": (0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D),
    "R": (0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11),
    "S": (0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E),
    "T": (0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04),
    "U": (0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E),
    "V": (0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04),
    "W": (0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A),
    "X": (0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11),
    "Y": (0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04),
    "Z": (0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F),
}


def draw_text_5x7(buf, img_w, img_h, x0, y0, text, color, scale=2):
    r, g, b = color
    cx = x0
    for ch in text.upper():
        glyph = FONT_5X7.get(ch, FONT_5X7[" "])
        for row_idx, bits in enumerate(glyph):
            for col_idx in range(5):
                if bits & (1 << (4 - col_idx)):
                    for sy in range(scale):
                        py = y0 + row_idx * scale + sy
                        if py < 0 or py >= img_h:
                            continue
                        for sx in range(scale):
                            px = cx + col_idx * scale + sx
                            if 0 <= px < img_w:
                                off = (py * img_w + px) * 3
                                buf[off] = r
                                buf[off + 1] = g
                                buf[off + 2] = b
        cx += 6 * scale


def fill_rect_rgb(buf, img_w, img_h, x0, y0, w, h, color):
    r, g, b = color
    x1 = min(img_w, x0 + w)
    y1 = min(img_h, y0 + h)
    x0 = max(0, x0)
    y0 = max(0, y0)
    if x1 <= x0 or y1 <= y0:
        return
    row_bytes = bytes((r, g, b)) * (x1 - x0)
    for y in range(y0, y1):
        off = (y * img_w + x0) * 3
        buf[off:off + len(row_bytes)] = row_bytes


def resize_rgb_nearest(src_w, src_h, src_buf, dst_w, dst_h):
    """Fast crisp nearest-neighbor pixel-art resizer preserving sharp tile edges."""
    out = bytearray(dst_w * dst_h * 3)
    x_map = [min(src_w - 1, (x * src_w) // dst_w) for x in range(dst_w)]
    for y in range(dst_h):
        sy = min(src_h - 1, (y * src_h) // dst_h)
        srow = sy * src_w * 3
        drow = y * dst_w * 3
        for x in range(dst_w):
            soff = srow + x_map[x] * 3
            doff = drow + x * 3
            out[doff] = src_buf[soff]
            out[doff + 1] = src_buf[soff + 1]
            out[doff + 2] = src_buf[soff + 2]
    return out


def build_contact_sheet(out_path, sheet_title, entries, cols, cell_w, cell_h):
    """
    Compose a labeled high-resolution contact-sheet PNG from rendered RGB maps.
    entries: list of (label, src_w, src_h, src_buf, changed_flag, issue_count)
    """
    if not entries:
        return
    rows = (len(entries) + cols - 1) // cols
    pad = 12
    title_h = 34
    header_h = 22
    slot_w = cell_w + 4
    slot_h = cell_h + header_h + 4
    img_w = pad + cols * (slot_w + pad)
    img_h = title_h + pad + rows * (slot_h + pad)
    buf = bytearray(img_w * img_h * 3)
    # Deep twilight slate backdrop
    fill_rect_rgb(buf, img_w, img_h, 0, 0, img_w, img_h, (20, 22, 30))
    fill_rect_rgb(buf, img_w, img_h, 0, 0, img_w, title_h, (32, 36, 48))
    draw_text_5x7(buf, img_w, img_h, pad, 10, sheet_title, (245, 206, 102), scale=2)

    for idx, (label, sw, sh, sbuf, is_changed, issue_cnt) in enumerate(entries):
        gx = idx % cols
        gy = idx // cols
        sx0 = pad + gx * (slot_w + pad)
        sy0 = title_h + pad + gy * (slot_h + pad)

        # Card frame (highlight border if changed or has issues)
        border_col = (240, 80, 80) if issue_cnt > 0 else ((85, 205, 255) if is_changed else (78, 86, 108))
        fill_rect_rgb(buf, img_w, img_h, sx0, sy0, slot_w, slot_h, (14, 16, 22))
        fill_rect_rgb(buf, img_w, img_h, sx0 + 2, sy0 + 2, cell_w, header_h, (34, 38, 52))
        draw_rect_outline(buf, img_w, img_h, sx0, sy0, slot_w, slot_h, border_col, thick=2)

        tag = label
        if issue_cnt > 0:
            tag = f"{label} ({issue_cnt}!)"
        elif is_changed:
            tag = f"{label} [DIFF]"
        max_chars = max(8, (cell_w - 10) // 6)
        draw_text_5x7(buf, img_w, img_h, sx0 + 6, sy0 + 8, tag[:max_chars],
                      (245, 238, 220) if issue_cnt == 0 else (255, 130, 120), scale=1)

        # Aspect-preserving fit inside cell_w x cell_h
        scale = min(cell_w / float(sw), cell_h / float(sh))
        dw = max(1, int(round(sw * scale)))
        dh = max(1, int(round(sh * scale)))
        resized = resize_rgb_nearest(sw, sh, sbuf, dw, dh)
        ox = sx0 + 2 + (cell_w - dw) // 2
        oy = sy0 + 2 + header_h + (cell_h - dh) // 2
        for y in range(dh):
            doff = ((oy + y) * img_w + ox) * 3
            soff = y * dw * 3
            buf[doff:doff + dw * 3] = resized[soff:soff + dw * 3]

    write_png_rgb(out_path, img_w, img_h, buf)


def render_globe_overview_png(out_path, w, h, cells, pal_rgb, meta_tiles, issues):
    """Render 4x-downsampled 1024x768 overview of the entire 256x192 globe (4px per tile) with issue pins."""
    img_w, img_h = w * 4, h * 4
    buf = bytearray(img_w * img_h * 3)
    t0_arr = meta_tiles[0]
    for y in range(h):
        for x in range(w):
            bg1, _, col_flag, meta, reg = cells[y * w + x]
            pal_id = GLOBE_UNIFIED_ZPAL
            tile = t0_arr[meta]
            zp = pal_rgb[pal_id]
            px = tile.px
            for sy in range(4):
                row_off = ((y * 4 + sy) * img_w + x * 4) * 3
                src_y = sy * 4 + 1
                qy = 2 if src_y >= 8 else 0
                for sx in range(4):
                    src_x = sx * 4 + 1
                    sp = zp[tile.qpal[qy + (1 if src_x >= 8 else 0)]]
                    r, g, b = sp[px[src_y * 16 + src_x]]
                    off = row_off + sx * 3
                    buf[off] = r; buf[off + 1] = g; buf[off + 2] = b

    clean_hash = hashlib.sha256(buf).hexdigest()[:16]
    for iss in issues:
        col = CAT_COLORS.get(iss["category"], (255, 255, 255))
        ix, iy = iss["x"], iss["y"]
        if 0 <= ix < w and 0 <= iy < h:
            tint_tile(buf, img_w, img_h, ix * 4, iy * 4, 4, col, alpha=0.65)
            if iss["severity"] == "CRITICAL":
                draw_rect_outline(buf, img_w, img_h, max(0, ix * 4 - 2), max(0, iy * 4 - 2), 8, 8, col, thick=1)
    write_png_rgb(out_path, img_w, img_h, buf)
    return img_w, img_h, buf, clean_hash


def main():
    parser = argparse.ArgumentParser(
        description="Project Hearth 6-Pass World, Autotile & Seam Auditor + Contact Sheets & Optional Gemini Vision QA"
    )
    parser.add_argument("raw_dir", nargs="?", default=os.path.join(ROOT, "build", "map_audit", "raw"))
    parser.add_argument("out_dir", nargs="?", default=os.path.join(ROOT, "build", "map_audit"))
    parser.add_argument("--bless", action="store_true",
                        help="Record current clean map pixel hashes into engine/tools/map_audit_baseline.json.")
    parser.add_argument("--vision", action="store_true",
                        help="Run optional Gemini Vision QA pass on contact sheets (requires GEMINI_API_KEY).")
    parser.add_argument("--changed-only", action="store_true",
                        help="With --vision, send only maps whose pixels changed since the blessed baseline.")
    args = parser.parse_args()

    raw_dir = args.raw_dir
    out_dir = args.out_dir

    with open(os.path.join(raw_dir, "world_dump.json"), "r") as fp:
        dump = json.load(fp)

    print("Loading baked 16-bit tileset & palettes from bake_chrono_tileset.py...")
    baker = load_tileset_module()
    (meta_names, name_to_id, pal_rgb, meta_tiles,
     snes_pal_rgb, snes_tiles, snes_meta_tiles,
     gen_pal_rgb, gen_tiles, gen_meta_tiles) = build_tileset_cache(baker)
    water_flows = measure_water_flow_vectors(meta_names, meta_tiles)

    # Load existing visual baseline hashes if present
    baseline_hashes = {}
    baseline_exists = os.path.isfile(BASELINE_PATH)
    if baseline_exists:
        try:
            with open(BASELINE_PATH, "r") as fp:
                baseline_hashes = json.load(fp).get("maps", {})
        except Exception:
            baseline_hashes = {}

    # Collect inbound portal spawns across all local zones & globe landmarks
    inbound_spawns = collections.defaultdict(list)
    zones_by_id = {z["id"]: z for z in dump["zones"]}
    inbound_spawns[5].append((19, 14, "Chapter 1 Start (Rowan Home)"))
    inbound_spawns[0].append((18, 12, "Alderbrook Arrival"))
    for z in dump["zones"]:
        for p in z["portals"]:
            if p["dst_zone"] != 8:
                inbound_spawns[p["dst_zone"]].append((p["dst_tx"], p["dst_ty"], f"{z['name']} portal '{p['label']}'"))
    for lm in dump["globe"]["landmarks"]:
        if lm["dst_zone"] != 255 and lm["dst_zone"] != 8:
            inbound_spawns[lm["dst_zone"]].append((lm["dst_tx"], lm["dst_ty"], f"Globe landmark '{lm['name']}'"))

    all_issues = []
    current_hashes = {}
    map_png_paths = {}
    zone_sheet_entries = []
    snes_sheet_entries = []
    genesis_sheet_entries = []
    globe_sheet_entries = []

    # Key Console showcase zones (12 diverse biomes incl. Alderbrook, Whispering Woods, Rowan Home, Mara Bedroom, Coral Cove)
    CONSOLE_SHEET_ZONE_IDS = {0, 1, 2, 5, 24, 6, 9, 11, 12, 18, 20, 21}

    # Pass 7: Console Hardware, VRAM/OAM, Portrait Parity & 816-tcc/SGDK Safety Audit
    console_issues = audit_console_hardware(dump, meta_names, meta_tiles, snes_tiles, snes_meta_tiles, gen_tiles)
    all_issues.extend(console_issues)

    # Build target list for Cross-Build Edge-Transition Audit (PC/Web Master + SNES BGR555 + Genesis BGR333)
    edge_targets = [
        ("Master",  meta_tiles[0],   pal_rgb),
        ("SNES",    snes_meta_tiles, snes_pal_rgb),
        ("Genesis", gen_meta_tiles,  gen_pal_rgb),
    ]
    edge_issues_all = []

    # Audit all local zones (PC/Web master, SNES 4bpp BGR555, AND Sega Genesis 9-bit CRAM!)
    zones_png_dir = os.path.join(out_dir, "zones")
    snes_png_dir = os.path.join(out_dir, "snes_zones")
    gen_png_dir = os.path.join(out_dir, "genesis_zones")
    for z in dump["zones"]:
        raw = open(os.path.join(raw_dir, z["bin"]), "rb").read()
        cells = [raw[i:i + 4] for i in range(0, len(raw), 4)]
        z_issues = audit_local_zone(z, cells, zones_by_id, inbound_spawns, meta_names, meta_tiles, pal_rgb, water_flows)
        all_issues.extend(z_issues)

        # Cross-Build Edge-Transition Audit (Master + SNES + Genesis)
        ez_issues = audit_cross_build_edge_transitions(
            z["name"], z["id"], z["w"], z["h"], cells, z["pal_id"], False, meta_names, edge_targets
        )
        edge_issues_all.extend(ez_issues)
        all_issues.extend(ez_issues)
        ez_master = [i for i in ez_issues if not i["zone"].endswith("]") or i["zone"].endswith("[Master]")]
        ez_snes = [i for i in ez_issues if i["zone"].endswith("[SNES]")]
        ez_gen = [i for i in ez_issues if i["zone"].endswith("[Genesis]")]

        slug = re.sub(r"[^a-z0-9]+", "_", z["name"].lower()).strip("_")
        map_key = f"zone:{z['id']:02d}_{slug}"
        png_path = os.path.join(zones_png_dir, f"{z['id']:02d}_{slug}_annotated.png")
        img_w, img_h, buf = render_map_rgb(z["w"], z["h"], cells, pal_rgb, meta_tiles, z["pal_id"], is_globe=False)
        clean_hash = hashlib.sha256(buf).hexdigest()[:16]
        current_hashes[map_key] = clean_hash
        map_png_paths[map_key] = png_path
        is_changed = baseline_exists and (baseline_hashes.get(map_key) != clean_hash)

        z_annot = z_issues + ez_master
        annotate_and_save_png(png_path, img_w, img_h, buf, z_annot)
        z_crit_err = sum(1 for i in z_annot if i["severity"] in ("CRITICAL", "ERROR"))
        zone_sheet_entries.append((f"{z['id']:02d} {z['name']}", img_w, img_h, buf, is_changed, z_crit_err))

        # SNES 4bpp Mode-1 Hardware Render (single-BG1 + CM_OPQ_* + 15-bit BGR555 CGRAM)
        if z["id"] in CONSOLE_SHEET_ZONE_IDS or ez_snes:
            s_map_key = f"snes:{z['id']:02d}_{slug}"
            s_png_path = os.path.join(snes_png_dir, f"{z['id']:02d}_{slug}_snes.png")
            sw_z, sh_z, sbuf_z = render_snes_zone_rgb(
                z["w"], z["h"], cells, snes_pal_rgb, snes_meta_tiles,
                z["pal_id"], z.get("has_mountain_vista", False), name_to_id, is_globe=False
            )
            s_clean_hash = hashlib.sha256(sbuf_z).hexdigest()[:16]
            current_hashes[s_map_key] = s_clean_hash
            map_png_paths[s_map_key] = s_png_path
            s_is_changed = baseline_exists and (baseline_hashes.get(s_map_key) != s_clean_hash)
            annotate_and_save_png(s_png_path, sw_z, sh_z, sbuf_z, ez_snes)
            sz_crit_err = sum(1 for i in ez_snes if i["severity"] in ("CRITICAL", "ERROR"))
            snes_sheet_entries.append((f"{z['id']:02d} {z['name']}", sw_z, sh_z, sbuf_z, s_is_changed, sz_crit_err))

        # Sega Genesis CRAM seam audit (all local zones!) + PNG render for showcase sheet
        gz_issues = audit_genesis_zone_seams(z, cells, meta_names, gen_meta_tiles, gen_pal_rgb, name_to_id)
        all_issues.extend(gz_issues)
        gz_all = gz_issues + ez_gen
        if z["id"] in CONSOLE_SHEET_ZONE_IDS or gz_all:
            g_map_key = f"genesis:{z['id']:02d}_{slug}"
            g_png_path = os.path.join(gen_png_dir, f"{z['id']:02d}_{slug}_genesis.png")
            gw_z, gh_z, gbuf_z = render_genesis_zone_rgb(
                z["w"], z["h"], cells, gen_pal_rgb, gen_meta_tiles,
                z["pal_id"], z.get("has_mountain_vista", False), name_to_id
            )
            g_clean_hash = hashlib.sha256(gbuf_z).hexdigest()[:16]
            current_hashes[g_map_key] = g_clean_hash
            map_png_paths[g_map_key] = g_png_path
            g_is_changed = baseline_exists and (baseline_hashes.get(g_map_key) != g_clean_hash)
            annotate_and_save_png(g_png_path, gw_z, gh_z, gbuf_z, gz_all)
            gz_crit_err = sum(1 for i in gz_all if i["severity"] in ("CRITICAL", "ERROR"))
            genesis_sheet_entries.append((f"{z['id']:02d} {z['name']}", gw_z, gh_z, gbuf_z, g_is_changed, gz_crit_err))

    # Audit full 256x192 Evertale Globe (Master + SNES + Genesis Edge Transitions!)
    g_info = dump["globe"]
    g_raw = open(os.path.join(raw_dir, g_info["bin"]), "rb").read()
    g_cells = [g_raw[i:i + 5] for i in range(0, len(g_raw), 5)]
    g_issues = audit_globe(g_info, g_cells, meta_names, meta_tiles, pal_rgb, water_flows)
    all_issues.extend(g_issues)
    eg_issues = audit_cross_build_edge_transitions(
        "Globe", 8, g_info["w"], g_info["h"], g_cells, GLOBE_UNIFIED_ZPAL, True,
        meta_names, edge_targets, reg_names=g_info["regions"]
    )
    edge_issues_all.extend(eg_issues)
    all_issues.extend(eg_issues)
    eg_master = [i for i in eg_issues if not i["zone"].endswith("]") or i["zone"].endswith("[Master]")]
    eg_snes = [i for i in eg_issues if i["zone"].endswith("[SNES]")]
    eg_gen = [i for i in eg_issues if i["zone"].endswith("[Genesis]")]
    g_issues_master = g_issues + eg_master

    # Render SNES & Genesis Hardware Emerald Heartland Globe crops (verifies shoals, Hollow Oak & unified palette!)
    hx0, hy0, hx1, hy1 = CONTINENT_CROPS[0][1:]
    s_hl_key = "snes:globe_00_heartland"
    s_hl_path = os.path.join(snes_png_dir, "globe_00_heartland_snes.png")
    shw, shh, shbuf = render_snes_zone_rgb(
        g_info["w"], g_info["h"], g_cells, snes_pal_rgb, snes_meta_tiles,
        GLOBE_UNIFIED_ZPAL, False, name_to_id, is_globe=True, crop=(hx0, hy0, hx1, hy1)
    )
    sh_hash = hashlib.sha256(shbuf).hexdigest()[:16]
    current_hashes[s_hl_key] = sh_hash
    map_png_paths[s_hl_key] = s_hl_path
    sh_changed = baseline_exists and (baseline_hashes.get(s_hl_key) != sh_hash)
    annotate_and_save_png(s_hl_path, shw, shh, shbuf, eg_snes, crop=(hx0, hy0, hx1, hy1))
    s_hl_err = sum(1 for i in eg_snes if i["severity"] in ("CRITICAL", "ERROR") and hx0 <= i["x"] < hx1 and hy0 <= i["y"] < hy1)
    snes_sheet_entries.append(("GLOBE HEARTLAND", shw, shh, shbuf, sh_changed, s_hl_err))

    g_hl_key = "genesis:globe_00_heartland"
    g_hl_path = os.path.join(gen_png_dir, "globe_00_heartland_genesis.png")
    ghw, ghh, ghbuf = render_genesis_zone_rgb(
        g_info["w"], g_info["h"], g_cells, gen_pal_rgb, gen_meta_tiles,
        GLOBE_UNIFIED_ZPAL, False, name_to_id, is_globe=True, crop=(hx0, hy0, hx1, hy1)
    )
    gh_hash = hashlib.sha256(ghbuf).hexdigest()[:16]
    current_hashes[g_hl_key] = gh_hash
    map_png_paths[g_hl_key] = g_hl_path
    gh_changed = baseline_exists and (baseline_hashes.get(g_hl_key) != gh_hash)
    annotate_and_save_png(g_hl_path, ghw, ghh, ghbuf, eg_gen, crop=(hx0, hy0, hx1, hy1))
    g_hl_err = sum(1 for i in eg_gen if i["severity"] in ("CRITICAL", "ERROR") and hx0 <= i["x"] < hx1 and hy0 <= i["y"] < hy1)
    genesis_sheet_entries.append(("GLOBE HEARTLAND", ghw, ghh, ghbuf, gh_changed, g_hl_err))

    # Export globe overview + 7 zoomed continent crops
    globe_overview_path = os.path.join(out_dir, "globe_overview_annotated.png")
    gw, gh, gbuf, ghash = render_globe_overview_png(
        globe_overview_path, g_info["w"], g_info["h"], g_cells, pal_rgb, meta_tiles, g_issues_master
    )
    current_hashes["globe:overview"] = ghash
    map_png_paths["globe:overview"] = globe_overview_path
    g_changed = baseline_exists and (baseline_hashes.get("globe:overview") != ghash)
    g_crit_err = sum(1 for i in g_issues_master if i["severity"] in ("CRITICAL", "ERROR"))
    globe_sheet_entries.append(("GLOBE OVERVIEW", gw, gh, gbuf, g_changed, g_crit_err))

    cont_dir = os.path.join(out_dir, "continents")
    for reg_id, (slug, x0, y0, x1, y1) in CONTINENT_CROPS.items():
        map_key = f"continent:{slug}"
        cpath = os.path.join(cont_dir, f"{slug}_annotated.png")
        img_w, img_h, buf = render_map_rgb(g_info["w"], g_info["h"], g_cells, pal_rgb, meta_tiles, GLOBE_UNIFIED_ZPAL,
                                           is_globe=True, crop=(x0, y0, x1, y1))
        chash = hashlib.sha256(buf).hexdigest()[:16]
        current_hashes[map_key] = chash
        map_png_paths[map_key] = cpath
        c_changed = baseline_exists and (baseline_hashes.get(map_key) != chash)
        annotate_and_save_png(cpath, img_w, img_h, buf, g_issues_master, crop=(x0, y0, x1, y1))
        c_crit_err = sum(1 for i in g_issues_master if i["severity"] in ("CRITICAL", "ERROR")
                         and x0 <= i["x"] < x1 and y0 <= i["y"] < y1)
        label = slug.replace("_", " ").upper()
        globe_sheet_entries.append((label, img_w, img_h, buf, c_changed, c_crit_err))

    # Export 5 labeled contact-sheet PNGs (Master zones, Evertale Globe, SNES 4bpp Hardware, Genesis 9-bit CRAM)
    sheet1_path = os.path.join(out_dir, "contact_sheet_zones_1.png")
    sheet2_path = os.path.join(out_dir, "contact_sheet_zones_2.png")
    sheet3_path = os.path.join(out_dir, "contact_sheet_globe.png")
    sheet4_path = os.path.join(out_dir, "contact_sheet_genesis_zones.png")
    sheet5_path = os.path.join(out_dir, "contact_sheet_snes_zones.png")
    half = (len(zone_sheet_entries) + 1) // 2
    build_contact_sheet(sheet1_path, "PROJECT HEARTH - LOCAL ZONES (SHEET 1 OF 2)",
                        zone_sheet_entries[:half], cols=4, cell_w=352, cell_h=224)
    build_contact_sheet(sheet2_path, "PROJECT HEARTH - LOCAL ZONES (SHEET 2 OF 2)",
                        zone_sheet_entries[half:], cols=4, cell_w=352, cell_h=224)
    build_contact_sheet(sheet3_path, "PROJECT HEARTH - EVERTALE GLOBE & 7 CONTINENTS",
                        globe_sheet_entries, cols=4, cell_w=420, cell_h=280)
    build_contact_sheet(sheet4_path, "PROJECT HEARTH - SEGA GENESIS CRAM ZONES (9-BIT VDP)",
                        genesis_sheet_entries, cols=4, cell_w=352, cell_h=224)
    build_contact_sheet(sheet5_path, "PROJECT HEARTH - SNES MODE-1 HARDWARE ZONES (15-BIT BGR555)",
                        snes_sheet_entries, cols=4, cell_w=352, cell_h=224)

    # Pixel-diff baseline comparison
    changed_maps = sorted(k for k, h in current_hashes.items() if baseline_hashes.get(k) != h) if baseline_exists else []
    removed_maps = sorted(k for k in baseline_hashes if k not in current_hashes) if baseline_exists else []

    # Build grouped root-cause report
    by_sev = collections.Counter(i["severity"] for i in all_issues)
    by_code = collections.Counter((i["severity"], i["category"], i["code"]) for i in all_issues)
    by_zone = collections.Counter(i["zone"] for i in all_issues if i["severity"] in ("CRITICAL", "ERROR"))

    blessed_now = False
    if args.bless:
        if by_sev["CRITICAL"] > 0 or by_sev["ERROR"] > 0:
            print("ERROR: Refusing to bless baseline while CRITICAL or ERROR audit findings remain.", file=sys.stderr)
        else:
            with open(BASELINE_PATH, "w") as fp:
                json.dump({"maps": dict(sorted(current_hashes.items()))}, fp, indent=2)
            blessed_now = True
            baseline_exists = True
            changed_maps = []
            removed_maps = []

    report = {
        "summary": {
            "total_zones_audited": len(dump["zones"]),
            "globe_cells_audited": g_info["w"] * g_info["h"],
            "snes_tile_count": dump["snes_tile_count"],
            "gen_tile_count": dump["gen_tile_count"],
            "console_checks_failed": sum(1 for i in console_issues if i["severity"] in ("CRITICAL", "ERROR")),
            "edge_transition_checks_failed": sum(1 for i in edge_issues_all if i["severity"] in ("CRITICAL", "ERROR")),
            "critical": by_sev["CRITICAL"],
            "error": by_sev["ERROR"],
            "warning": by_sev["WARNING"],
        },
        "visual_diff": {
            "baseline_exists": baseline_exists,
            "blessed_now": blessed_now,
            "total_maps_hashed": len(current_hashes),
            "changed_count": len(changed_maps),
            "changed_maps": changed_maps,
            "removed_maps": removed_maps,
        },
        "contact_sheets": [sheet1_path, sheet2_path, sheet3_path, sheet4_path, sheet5_path],
        "root_causes": [
            {"severity": sev, "category": cat, "code": code, "count": cnt}
            for (sev, cat, code), cnt in by_code.most_common()
        ],
        "water_flows_measured": {meta_names[k]: list(v) for k, v in sorted(water_flows.items())},
        "issues": all_issues,
    }
    report_path = os.path.join(out_dir, "audit_report.json")
    with open(report_path, "w") as fp:
        json.dump(report, fp, indent=2)

    edge_fail_cnt = sum(1 for i in edge_issues_all if i["severity"] in ("CRITICAL", "ERROR"))
    print("")
    print("========================================================================")
    print("  PROJECT HEARTH — 7-PASS WORLD, AUTOTILE, SEAM & CONSOLE AUDIT REPORT")
    print("========================================================================")
    print(f"  Cells audited: {len(dump['zones']) * 44 * 28 + g_info['w'] * g_info['h']:,} "
          f"({len(dump['zones'])} zones + 256x192 Globe [Master + SNES + Genesis])")
    print(f"  VRAM footprint: SNES={dump['snes_tile_count']}/510 tiles | Genesis={dump['gen_tile_count']}/399 tiles")
    print(f"  Console Audit : SNES/Genesis VRAM, OAM, Particles, HUD, Portraits & 816-tcc = "
          f"{'PASS (0 issues)' if not console_issues else f'{len(console_issues)} finding(s)'}")
    print(f"  Edge Transit. : Master + SNES BGR555 + Genesis BGR333 (Shoals/Sand/Cobble/Shores) = "
          f"{'PASS (0 issues)' if edge_fail_cnt == 0 else f'{edge_fail_cnt} error(s)'}")
    print(f"  Findings: CRITICAL={by_sev['CRITICAL']}  ERROR={by_sev['ERROR']}  WARNING={by_sev['WARNING']}")
    if blessed_now:
        print(f"  Visual Diff : BLESSED {len(current_hashes)} map pixel hashes -> {BASELINE_PATH}")
    elif not baseline_exists:
        print("  Visual Diff : No baseline yet (run `make map-audit-bless` after visually verifying contact sheets)")
    elif not changed_maps and not removed_maps:
        print(f"  Visual Diff : 0/{len(current_hashes)} maps changed visually since last blessed review")
    else:
        print(f"  Visual Diff : {len(changed_maps)}/{len(current_hashes)} map(s) visually changed since baseline:")
        for mk in changed_maps[:15]:
            print(f"    * {mk}")
        if len(changed_maps) > 15:
            print(f"    ... and {len(changed_maps) - 15} more")
    print("------------------------------------------------------------------------")
    print("  ROOT-CAUSE BREAKDOWN:")
    for (sev, cat, code), cnt in by_code.most_common():
        print(f"    [{sev:8s}] {cat:9s} | {code:24s} : {cnt:5d}")
    if by_zone:
        print("------------------------------------------------------------------------")
        print("  TOP AFFECTED ZONES / CONTINENTS (CRITICAL + ERROR):")
        for zname, cnt in by_zone.most_common(15):
            print(f"    {zname:32s} : {cnt:5d}")
    print("------------------------------------------------------------------------")
    print("  SAMPLE CRITICAL & ERROR DETAILS (up to 30):")
    shown = 0
    seen_keys = set()
    for iss in all_issues:
        if iss["severity"] not in ("CRITICAL", "ERROR"):
            continue
        k = (iss["zone"], iss["code"])
        if k in seen_keys and iss["severity"] != "CRITICAL":
            continue
        seen_keys.add(k)
        print(f"    [{iss['severity']:8s}] {iss['zone']} ({iss['x']},{iss['y']}) {iss['code']}: {iss['msg']}")
        shown += 1
        if shown >= 30:
            break
    print("------------------------------------------------------------------------")
    print(f"  JSON Report    : {report_path}")
    print(f"  Contact Sheets : {sheet1_path}, {sheet2_path}, {sheet3_path}, {sheet4_path}, {sheet5_path}")
    print(f"  Zone PNGs      : {zones_png_dir}/ ({len(dump['zones'])} annotated pixel-art maps)")
    print(f"  SNES PNGs      : {snes_png_dir}/ ({len(snes_sheet_entries)} SNES Mode-1 BGR555 maps)")
    print(f"  Genesis PNGs   : {gen_png_dir}/ ({len(genesis_sheet_entries)} Sega Genesis CRAM maps)")
    print(f"  Globe PNGs     : {globe_overview_path} + {cont_dir}/ (7 crops)")
    print("========================================================================")

    exit_code = 1 if (by_sev["CRITICAL"] > 0 or by_sev["ERROR"] > 0) else 0

    if args.vision:
        if args.changed_only:
            if not baseline_exists:
                vision_targets = [sheet1_path, sheet2_path, sheet3_path, sheet4_path, sheet5_path]
            elif not changed_maps:
                print("[VISION] 0 maps changed visually since blessed baseline — skipping Gemini Vision call.")
                return exit_code
            else:
                vision_targets = [map_png_paths[k] for k in changed_maps if k in map_png_paths]
        else:
            vision_targets = [sheet1_path, sheet2_path, sheet3_path, sheet4_path, sheet5_path]

        vision_out = os.path.join(out_dir, "vision_report.json")
        cmd = [sys.executable, VISION_SCRIPT, "--out", vision_out] + vision_targets
        proc = subprocess.run(cmd)
        if proc.returncode != 0:
            exit_code = proc.returncode

    return exit_code


if __name__ == "__main__":
    sys.exit(main())

