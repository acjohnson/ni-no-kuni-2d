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

import collections
import importlib.util
import json
import math
import os
import re
import struct
import sys
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BAKER_PATH = os.path.join(ROOT, "engine", "tools", "bake_chrono_tileset.py")

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
    60: "T_WORLD_TECH", 61: "T_PALM_TREE", 62: "T_DOCK_PIER",
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
T_WORLD_TECH, T_PALM_TREE, T_DOCK_PIER = 60, 61, 62

O_NONE, O_CANOPY_TL, O_CANOPY_TR, O_CANOPY_BL, O_CANOPY_BR = 0, 1, 2, 3, 4
O_ROOF_EAVE, O_SHRINE_ARCH, O_MAPLE_TL, O_MAPLE_TR = 5, 6, 7, 8
O_PINE_TOP, O_OAK_CROWN, O_MAPLE_MID, O_MAPLE_TOP = 9, 10, 11, 12

COL_WALKABLE, COL_SOLID, COL_TALL_GRASS = 0, 1, 2
COL_SHALLOW, COL_WATER, COL_LAVA = 3, 4, 5

WATER_TILES = {T_WATER, T_FORD, T_RAPIDS, T_WORLD_OCEAN, T_WORLD_SHALLOW}
AQUATIC_TILES = WATER_TILES | {T_WORLD_CORAL}
GRASS_LIKE = {T_GRASS, T_GRASS_ALT, T_FLOWERS, T_TALL_GRASS, T_AUTUMN_GRASS}
PROP_TILES = {
    T_LANTERN_POST, T_SIGNPOST, T_FLOWER_BED, T_BUSH, T_FENCE,
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
    T_WORLD_RUINS, T_PALM_TREE,
}

# Region -> ZPAL index for globe rendering
REGION_TO_ZPAL = {
    0: 10,  # Heartland -> ZPAL_EVERTALE_WORLD
    1: 12,  # Desert -> ZPAL_DESERT
    2: 13,  # Paradise -> ZPAL_PARADISE
    3: 14,  # Frostmere -> ZPAL_FROST
    4: 15,  # Canopy -> ZPAL_JUNGLE
    5: 16,  # Volcano -> ZPAL_VOLCANO
    6: 17,  # Aetheris -> ZPAL_TECH
}

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


def build_tileset_cache(baker):
    zone_pals = baker.build_zone_palettes()  # dict {0..17: [4][16] of 0xRRGGBB}
    meta_names = list(baker.META_NAMES)
    name_to_id = {n: i for i, n in enumerate(meta_names)}
    frames = [baker.build_all_metatiles(f) for f in range(4)]

    # Pre-unpack palette RGB tuples: pal_rgb[zpal][subpal][ci] -> (r,g,b)
    pal_rgb = []
    for zi in range(len(zone_pals)):
        zp = zone_pals[zi]
        sp_list = []
        for sp in zp:
            sp_list.append([((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF) for c in sp])
        pal_rgb.append(sp_list)

    # Map meta_id -> FlatTile for frames 0..3
    meta_tiles = []
    for f in range(4):
        meta_tiles.append([FlatTile(t) for t in frames[f]])

    return meta_names, name_to_id, pal_rgb, meta_tiles


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
            pal_id = REGION_TO_ZPAL.get(rec[4], 10) if is_globe else default_pal_id
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
                        elif bg1 in PROP_TILES and cdiff > 38.0 and not (bg1 in (T_LANTERN_POST, T_SIGNPOST) and top_fam == "SAND"):
                            add_issue("WARNING", "SEAM", "PROP_BORDER_SEAM", x, y,
                                      f"Prop {mname} outer corner RGB differs from surrounding {top_fam} ({T_NAMES[top_list[0][0]]}) by {cdiff:.1f}")

            # Pass 4b: Cobble-to-Grass feathered edge color match
            if mname.startswith("CM_COBBLE_EDGE_") or mname.startswith("CM_COBBLE_CORNER_"):
                tA = t0_arr[meta]
                if "W" in mname and x > 0 and cells[y * w + (x - 1)][0] in (T_GRASS, T_GRASS_ALT):
                    avg_diff = tile_border_rgb_diff(t0_arr[cells[y * w + (x - 1)][3]], tA, zp, "E")
                    if avg_diff > 22.0:
                        add_issue("WARNING", "SEAM", "ROAD_EDGE_SEAM", x, y,
                                  f"Cobble grass-edge {mname} clashes with west grass (border diff {avg_diff:.1f})")
                if "E" in mname and x + 1 < w and cells[y * w + (x + 1)][0] in (T_GRASS, T_GRASS_ALT):
                    avg_diff = tile_border_rgb_diff(tA, t0_arr[cells[y * w + (x + 1)][3]], zp, "E")
                    if avg_diff > 22.0:
                        add_issue("WARNING", "SEAM", "ROAD_EDGE_SEAM", x, y,
                                  f"Cobble grass-edge {mname} clashes with east grass (border diff {avg_diff:.1f})")
                if "N" in mname and y > 0 and cells[(y - 1) * w + x][0] in (T_GRASS, T_GRASS_ALT):
                    avg_diff = tile_border_rgb_diff(t0_arr[cells[(y - 1) * w + x][3]], tA, zp, "S")
                    if avg_diff > 22.0:
                        add_issue("WARNING", "SEAM", "ROAD_EDGE_SEAM", x, y,
                                  f"Cobble grass-edge {mname} clashes with north grass (border diff {avg_diff:.1f})")
                if "S" in mname and y + 1 < h and cells[(y + 1) * w + x][0] in (T_GRASS, T_GRASS_ALT):
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

    # Pass 4-pre: Verify every Globe region's ocean palette stays cool/aquatic (never lava-red!)
    for reg_id, rname in enumerate(reg_names):
        zp_reg = pal_rgb[REGION_TO_ZPAL.get(reg_id, 10)]
        oc_rgb = tile_mean_rgb(t0_arr[T_WORLD_OCEAN], zp_reg, None)
        if oc_rgb[0] > oc_rgb[2] + 30.0:
            add_issue("ERROR", "SEAM", "OCEAN_PALETTE_HUE", 0, 0,
                      f"Region {rname} ocean palette is red/lava-tinted (mean RGB={oc_rgb[0]:.0f},{oc_rgb[1]:.0f},{oc_rgb[2]:.0f})", reg_id)

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
            pal_id = REGION_TO_ZPAL.get(reg, 10)
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


def render_globe_overview_png(out_path, w, h, cells, pal_rgb, meta_tiles, issues):
    """Render 4x-downsampled 1024x768 overview of the entire 256x192 globe (4px per tile) with issue pins."""
    img_w, img_h = w * 4, h * 4
    buf = bytearray(img_w * img_h * 3)
    t0_arr = meta_tiles[0]
    for y in range(h):
        for x in range(w):
            bg1, _, col_flag, meta, reg = cells[y * w + x]
            pal_id = REGION_TO_ZPAL.get(reg, 10)
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

    for iss in issues:
        col = CAT_COLORS.get(iss["category"], (255, 255, 255))
        ix, iy = iss["x"], iss["y"]
        if 0 <= ix < w and 0 <= iy < h:
            tint_tile(buf, img_w, img_h, ix * 4, iy * 4, 4, col, alpha=0.65)
            if iss["severity"] == "CRITICAL":
                draw_rect_outline(buf, img_w, img_h, max(0, ix * 4 - 2), max(0, iy * 4 - 2), 8, 8, col, thick=1)
    write_png_rgb(out_path, img_w, img_h, buf)


def main():
    raw_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "build", "map_audit", "raw")
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "build", "map_audit")

    with open(os.path.join(raw_dir, "world_dump.json"), "r") as fp:
        dump = json.load(fp)

    print("Loading baked 16-bit tileset & palettes from bake_chrono_tileset.py...")
    baker = load_tileset_module()
    meta_names, name_to_id, pal_rgb, meta_tiles = build_tileset_cache(baker)
    water_flows = measure_water_flow_vectors(meta_names, meta_tiles)

    # Collect inbound portal spawns across all local zones & globe landmarks
    inbound_spawns = collections.defaultdict(list)
    zones_by_id = {z["id"]: z for z in dump["zones"]}
    # Story default spawns
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

    # Audit all 23 local zones
    zones_png_dir = os.path.join(out_dir, "zones")
    for z in dump["zones"]:
        raw = open(os.path.join(raw_dir, z["bin"]), "rb").read()
        cells = [raw[i:i + 4] for i in range(0, len(raw), 4)]
        z_issues = audit_local_zone(z, cells, zones_by_id, inbound_spawns, meta_names, meta_tiles, pal_rgb, water_flows)
        all_issues.extend(z_issues)

        slug = re.sub(r"[^a-z0-9]+", "_", z["name"].lower()).strip("_")
        img_w, img_h, buf = render_map_rgb(z["w"], z["h"], cells, pal_rgb, meta_tiles, z["pal_id"], is_globe=False)
        annotate_and_save_png(os.path.join(zones_png_dir, f"{z['id']:02d}_{slug}_annotated.png"),
                              img_w, img_h, buf, z_issues)

    # Audit full 256x192 Evertale Globe
    g_info = dump["globe"]
    g_raw = open(os.path.join(raw_dir, g_info["bin"]), "rb").read()
    g_cells = [g_raw[i:i + 5] for i in range(0, len(g_raw), 5)]
    g_issues = audit_globe(g_info, g_cells, meta_names, meta_tiles, pal_rgb, water_flows)
    all_issues.extend(g_issues)

    # Export globe overview + 7 zoomed continent crops
    render_globe_overview_png(os.path.join(out_dir, "globe_overview_annotated.png"),
                              g_info["w"], g_info["h"], g_cells, pal_rgb, meta_tiles, g_issues)
    cont_dir = os.path.join(out_dir, "continents")
    for reg_id, (slug, x0, y0, x1, y1) in CONTINENT_CROPS.items():
        img_w, img_h, buf = render_map_rgb(g_info["w"], g_info["h"], g_cells, pal_rgb, meta_tiles, 10,
                                           is_globe=True, crop=(x0, y0, x1, y1))
        annotate_and_save_png(os.path.join(cont_dir, f"{slug}_annotated.png"),
                              img_w, img_h, buf, g_issues, crop=(x0, y0, x1, y1))

    # Build grouped root-cause report
    by_sev = collections.Counter(i["severity"] for i in all_issues)
    by_code = collections.Counter((i["severity"], i["category"], i["code"]) for i in all_issues)
    by_zone = collections.Counter(i["zone"] for i in all_issues if i["severity"] in ("CRITICAL", "ERROR"))

    report = {
        "summary": {
            "total_zones_audited": len(dump["zones"]),
            "globe_cells_audited": g_info["w"] * g_info["h"],
            "snes_tile_count": dump["snes_tile_count"],
            "gen_tile_count": dump["gen_tile_count"],
            "critical": by_sev["CRITICAL"],
            "error": by_sev["ERROR"],
            "warning": by_sev["WARNING"],
        },
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

    print("")
    print("========================================================================")
    print("  PROJECT HEARTH — 6-PASS WORLD, AUTOTILE & SEAM AUDIT REPORT")
    print("========================================================================")
    print(f"  Cells audited: {len(dump['zones']) * 44 * 28 + g_info['w'] * g_info['h']:,} "
          f"({len(dump['zones'])} zones + 256x192 Evertale Globe)")
    print(f"  VRAM footprint: SNES={dump['snes_tile_count']}/510 tiles | Genesis={dump['gen_tile_count']}/399 tiles")
    print(f"  Findings: CRITICAL={by_sev['CRITICAL']}  ERROR={by_sev['ERROR']}  WARNING={by_sev['WARNING']}")
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
    print(f"  JSON Report : {report_path}")
    print(f"  Zone PNGs   : {zones_png_dir}/ (23 annotated pixel-art maps)")
    print(f"  Globe PNGs  : {out_dir}/globe_overview_annotated.png + {cont_dir}/ (7 crops)")
    print("========================================================================")

    return 1 if (by_sev["CRITICAL"] > 0 or by_sev["ERROR"] > 0) else 0


if __name__ == "__main__":
    sys.exit(main())
