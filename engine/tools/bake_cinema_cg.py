#!/usr/bin/env python3
"""
Project Hearth — Dual-Target Cinema CG Baker (Pier Solar Multi-Palette Edition)
Generates:
  1. engine/platform/cinema_cg_sdl2.h : 340x156 high-color RGB565 painted plates & portraits for SDL2 PC
  2. engine/platform/cinema_cg_rom.h  : 256x120 (32x15) multi-palette 4bpp Genesis plates (3 palettes = 45 colors)
                                        with H/V-flip tile deduplication + SNES extern declarations
  3. engine/platform/cinema_cg_snes.asm: 256x120 (32x15) multi-palette 4bpp SNES plates (4 palettes = 60 colors)
                                        packed into SUPERFREE LoROM banks with H/V-flip tile deduplication

Uses ImageMagick `convert` only at bake time; generated C/ASM outputs have zero runtime dependencies.
"""

import os
import struct
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
IMG_DIR = os.path.join(ROOT, "src", "assets", "images")
OUT_SDL2 = os.path.join(ROOT, "engine", "platform", "cinema_cg_sdl2.h")
OUT_ROM = os.path.join(ROOT, "engine", "platform", "cinema_cg_rom.h")
OUT_SNES_ASM = os.path.join(ROOT, "engine", "platform", "cinema_cg_snes.asm")

# High-color painted storybook plates preserved for Native PC (SDL2) & Web
SDL_PLATES = [
    ("ALDERBROOK_DUSK",      "cinema_alderbrook_dusk_1795309262024.jpg"),
    ("HOLLOW_OAK_CHASE",     "cinema_hollow_oak_chase_1795309261000.jpg"),
    ("EVERTALE_PANORAMA",    "cinema_evertale_panorama_1795309261863.jpg"),
    ("SHADOW_GRANDMA",       "cinema_shadow_grandma_1795309261174.jpg"),
    ("BEDROOM_COMFORT",      "cinema_bedroom_comfort_v2_1795309287400.jpg"),
    ("PIP_WINDOW",           "cinema_pip_window_1795309260734.jpg"),
    ("ATTIC_KEEPSAKE",       "cg_attic_keepsake_1795315522510.jpg"),
    ("PIP_DREAM_RETURN",     "cg_pip_dream_return_gnome_1795450641504.jpg"),
    ("HOLLOW_OAK_AWAKENS",   "cg_hollow_oak_awakens_gnome_1795450642131.jpg"),
    ("EVERTALE_WORLD_VISTA", "cg_evertale_world_vista_1795319776319.jpg"),
]

# Dedicated Pier Solar-style crisp 16-bit pixel-art plates for SNES (60-color) & Genesis (45-color)
ROM_PLATES = [
    ("ALDERBROOK_DUSK",      "pixel_cg_alderbrook_1795404215187.jpg"),
    ("HOLLOW_OAK_CHASE",     "pixel_cg_oak_chase_1795404215403.jpg"),
    ("EVERTALE_PANORAMA",    "pixel_cg_evertale_1795404214742.jpg"),
    ("SHADOW_GRANDMA",       "pixel_cg_shadow_gma_1795404214799.jpg"),
    ("BEDROOM_COMFORT",      "pixel_cg_comfort_1795404215947.jpg"),
    ("PIP_WINDOW",           "pixel_cg_pip_win_1795404215326.jpg"),
    ("ATTIC_KEEPSAKE",       "pixel_cg_attic_1795404216431.jpg"),
    ("PIP_DREAM_RETURN",     "pixel_cg_pip_dream_sill_1795526954906.jpg"),
    ("HOLLOW_OAK_AWAKENS",   "pixel_cg_oak_wake_v2_1795404236718.jpg"),
    ("EVERTALE_WORLD_VISTA", "pixel_cg_world_vista_1795404215399.jpg"),
]

PORTRAITS = [
    ("ROWAN",        "portrait_rowan_1795315522922.jpg"),
    ("GRANDMA_MARA", "portrait_grandma_mara_1795315523861.jpg"),
    ("PIP",          "portrait_pip_gnome_1795404214797.jpg"),
    ("BARNABY",      "portrait_barnaby_1795315522478.jpg"),
    ("MAPLE",        "portrait_maple_1795315522927.jpg"),
    ("JASPER",       "portrait_jasper_1795315522636.jpg"),
    ("MIDO",         "portrait_mido_1795319777822.jpg"),
    ("TESSA",        "portrait_tessa_1795319779391.jpg"),
    ("CORIN",        "portrait_corin_1795319780579.jpg"),
    ("SENTINEL",     "portrait_sentinel_1795319781052.jpg"),
]

SDL_W, SDL_H = 340, 156
PORT_W, PORT_H = 40, 40
ROM_W, ROM_H = 256, 120   # 32 x 15 tiles of 8x8 = 480 map entries
ROM_TW, ROM_TH = ROM_W // 8, ROM_H // 8
MAX_ROM_TILES = 480       # Zero lossy tile merging! Fits cleanly in SNES BG1 (512 slots) & Genesis VRAM
SNES_CG_TILE_BASE = 0     # Loaded at VRAM_BG1_GFX + 0 during MODE_INTRO (terrain re-baked on exit)

# Genesis CRAM slots assigned to multi-palette cinema plates (PAL2 reserved for crisp WINDOW text)
GEN_PAL_SLOTS = (0, 1, 3)

# Gentle 2x2 ordered Bayer offsets (smooth 16-bit shading without harsh checkerboard patterns)
BAYER_2X2_SNES = (
    (-3,  1),
    ( 2, -2),
)
BAYER_2X2_GEN = (
    (-5,  2),
    ( 4, -3),
)


def read_rgb(path, w, h, crisp_16bit=False):
    cmd = ["convert", path]
    if crisp_16bit:
        # Mild contrast & saturation boost tailored for 16-bit CRT/RGB333/RGB555 vibrancy
        cmd += ["-filter", "Lanczos", "-resize", f"{w}x{h}^", "-gravity", "center", "-extent", f"{w}x{h}",
                "-modulate", "102,112,100", "-unsharp", "0x0.7+0.6+0.02"]
    else:
        cmd += ["-resize", f"{w}x{h}^", "-gravity", "center", "-extent", f"{w}x{h}"]
    cmd += ["-depth", "8", "rgb:-"]
    raw = subprocess.check_output(cmd)
    assert len(raw) == w * h * 3, f"Expected {w*h*3} bytes, got {len(raw)}"
    return raw


def rgb888_to_rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def rgb888_to_bgr555(r, g, b):
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


def rgb888_to_bgr333(r, g, b):
    return (((b >> 5) & 7) << 9) | (((g >> 5) & 7) << 5) | (((r >> 5) & 7) << 1)


def snap_rgb(r, g, b, bits):
    """Snap 8-bit RGB to target hardware channel depth (5 for SNES, 3 for Genesis)."""
    if bits == 5:
        r5 = r >> 3; g5 = g >> 3; b5 = b >> 3
        return ((r5 << 3) | (r5 >> 2), (g5 << 3) | (g5 >> 2), (b5 << 3) | (b5 >> 2))
    r3 = r >> 5; g3 = g >> 5; b3 = b >> 5
    return (r3 * 36, g3 * 36, b3 * 36)


def color_dist2(c1, c2):
    dr = c1[0] - c2[0]
    dg = c1[1] - c2[1]
    db = c1[2] - c2[2]
    # Perceptual RGB weighting (human eye is most sensitive to green, then red, then blue)
    return dr * dr * 3 + dg * dg * 6 + db * db * 2


def median_cut_palette(color_counts, max_colors, bits):
    """Build up to max_colors opaque palette entries (indices 1..15) via weighted median cut."""
    if not color_counts:
        return [(11, 13, 20)] * (max_colors + 1)

    items = list(color_counts.items())  # [((r,g,b), count), ...]
    boxes = [items]

    while len(boxes) < max_colors:
        # Pick box with largest weighted color spread that has >1 unique color
        best_i = -1
        best_score = -1
        best_ch = 0
        for idx, box in enumerate(boxes):
            if len(box) <= 1:
                continue
            rmin = gmin = bmin = 255
            rmax = gmax = bmax = 0
            tot = 0
            for (r, g, b), cnt in box:
                if r < rmin: rmin = r
                if r > rmax: rmax = r
                if g < gmin: gmin = g
                if g > gmax: gmax = g
                if b < bmin: bmin = b
                if b > bmax: bmax = b
                tot += cnt
            dr = (rmax - rmin) * 3
            dg = (gmax - gmin) * 6
            db = (bmax - bmin) * 2
            span = max(dr, dg, db)
            score = span * (tot ** 0.5)
            if score > best_score:
                best_score = score
                best_i = idx
                best_ch = 1 if dg >= dr and dg >= db else (0 if dr >= db else 2)
        if best_i < 0 or best_score <= 0:
            break
        box = boxes.pop(best_i)
        box.sort(key=lambda it: it[0][best_ch])
        half = sum(cnt for _, cnt in box) / 2.0
        acc = 0
        split = 1
        for k, (_, cnt) in enumerate(box):
            acc += cnt
            if acc >= half and k + 1 < len(box):
                split = k + 1
                break
        boxes.append(box[:split])
        boxes.append(box[split:])

    pal = [(11, 13, 20)]  # Index 0 reserved for matte backdrop
    used = set()
    for box in boxes:
        rs = gs = bs = tot = 0
        for (r, g, b), cnt in box:
            rs += r * cnt; gs += g * cnt; bs += b * cnt; tot += cnt
        if tot == 0:
            c = (11, 13, 20)
        else:
            c = snap_rgb(int(round(rs / tot)), int(round(gs / tot)), int(round(bs / tot)), bits)
        if c not in used:
            used.add(c)
            pal.append(c)
    # Fill any spare slots with highest-count uncovered colors
    if len(pal) < max_colors + 1:
        for rgb, _cnt in sorted(items, key=lambda it: -it[1]):
            if rgb not in used:
                used.add(rgb)
                pal.append(rgb)
                if len(pal) == max_colors + 1:
                    break
    while len(pal) < max_colors + 1:
        pal.append((11, 13, 20))
    return pal[:max_colors + 1]


def extract_rgb_tiles(raw):
    """Split 256x120 RGB buffer into 480 tiles, each a list of 64 (r,g,b) tuples."""
    tiles = []
    for ty in range(ROM_TH):
        for tx in range(ROM_TW):
            t = []
            for py in range(8):
                base = ((ty * 8 + py) * ROM_W + tx * 8) * 3
                for px in range(8):
                    o = base + px * 3
                    t.append((raw[o], raw[o + 1], raw[o + 2]))
            tiles.append(t)
    return tiles


def tile_feature(tile):
    """Compact 6D color signature per tile for deterministic k-means++ palette clustering."""
    rs = gs = bs = 0
    best_sat = -1
    sr = sg = sb = 0
    for r, g, b in tile:
        rs += r; gs += g; bs += b
        sat = max(r, g, b) - min(r, g, b)
        if sat > best_sat:
            best_sat = sat; sr = r; sg = g; sb = b
    return (rs >> 6, gs >> 6, bs >> 6, sr, sg, sb)


def feat_dist2(a, b):
    return (
        (a[0] - b[0]) ** 2 * 3 + (a[1] - b[1]) ** 2 * 6 + (a[2] - b[2]) ** 2 * 2 +
        (a[3] - b[3]) ** 2 * 2 + (a[4] - b[4]) ** 2 * 3 + (a[5] - b[5]) ** 2
    )


def flip_tile_h(t):
    out = bytearray(64)
    for y in range(8):
        ro = y * 8
        for x in range(8):
            out[ro + (7 - x)] = t[ro + x]
    return bytes(out)


def flip_tile_v(t):
    out = bytearray(64)
    for y in range(8):
        ro_src = y * 8
        ro_dst = (7 - y) * 8
        out[ro_dst:ro_dst + 8] = t[ro_src:ro_src + 8]
    return bytes(out)


def tile_mse(t1, t2, pal):
    err = 0
    for i in range(64):
        c1 = pal[t1[i]]
        c2 = pal[t2[i]]
        err += color_dist2(c1, c2)
    return err


def quantize_plate_multipal(raw, num_pals, bits):
    """
    Pier Solar-style multi-palette quantizer:
      1. K-means++ clusters 480 tiles into `num_pals` color groups.
      2. Weighted median cut generates a hardware-snapped 15-color palette per group.
      3. 3 iterative refinement passes assign each tile to its lowest-MSE palette and rebuild palettes.
      4. Clean ordered-Bayer quantization encodes each tile with zero muddy Floyd-Steinberg grain.
      5. H/V-flip-aware deduplication + same-palette perceptual merging packs into <= MAX_ROM_TILES.
    """
    rgb_tiles = extract_rgb_tiles(raw)
    snapped_tiles = [
        [snap_rgb(r, g, b, bits) for (r, g, b) in t]
        for t in rgb_tiles
    ]
    feats = [tile_feature(t) for t in rgb_tiles]

    # Deterministic farthest-point k-means++ initialization
    centers = [feats[0]]
    while len(centers) < num_pals:
        best_idx = 0
        best_d = -1
        for i, f in enumerate(feats):
            d = min(feat_dist2(f, c) for c in centers)
            if d > best_d:
                best_d = d
                best_idx = i
        centers.append(feats[best_idx])

    assign = [0] * len(rgb_tiles)
    for _ in range(10):
        for i, f in enumerate(feats):
            best_c = 0
            best_d = feat_dist2(f, centers[0])
            for c_idx in range(1, num_pals):
                d = feat_dist2(f, centers[c_idx])
                if d < best_d:
                    best_d = d
                    best_c = c_idx
            assign[i] = best_c
        for c_idx in range(num_pals):
            members = [feats[i] for i, a in enumerate(assign) if a == c_idx]
            if members:
                n = len(members)
                centers[c_idx] = tuple(sum(m[k] for m in members) // n for k in range(6))

    # Iterative palette optimization (8 passes: build 15-color palettes -> reassign tiles by true RGB MSE)
    pals = None
    for _pass in range(8):
        pals = []
        for p in range(num_pals):
            counts = {}
            for ti, a in enumerate(assign):
                if a != p:
                    continue
                for rgb in snapped_tiles[ti]:
                    counts[rgb] = counts.get(rgb, 0) + 1
            pal = median_cut_palette(counts, 15, bits)
            # 2 passes of weighted k-means refinement on the 15 opaque colors to minimize perceptual MSE
            if len(counts) > 15:
                centers_rgb = list(pal[1:16])
                for _km in range(2):
                    sums = [[0, 0, 0, 0] for _ in range(15)]
                    for rgb, cnt in counts.items():
                        bi = 0
                        bd = color_dist2(rgb, centers_rgb[0])
                        for ci in range(1, 15):
                            d = color_dist2(rgb, centers_rgb[ci])
                            if d < bd:
                                bd = d
                                bi = ci
                        s = sums[bi]
                        s[0] += rgb[0] * cnt
                        s[1] += rgb[1] * cnt
                        s[2] += rgb[2] * cnt
                        s[3] += cnt
                    for ci in range(15):
                        s = sums[ci]
                        if s[3] > 0:
                            centers_rgb[ci] = snap_rgb(
                                int(round(s[0] / s[3])),
                                int(round(s[1] / s[3])),
                                int(round(s[2] / s[3])),
                                bits,
                            )
                pal = [(11, 13, 20)] + centers_rgb
            pals.append(pal)

        # Build fast lookup cache per palette: snapped_rgb -> (pal_idx, dist2)
        caches = []
        for p in range(num_pals):
            pal = pals[p]
            cache = {}
            for ti in range(len(snapped_tiles)):
                for rgb in snapped_tiles[ti]:
                    if rgb not in cache:
                        bi = 1
                        bd = color_dist2(rgb, pal[1])
                        for pi in range(2, 16):
                            d = color_dist2(rgb, pal[pi])
                            if d < bd:
                                bd = d
                                bi = pi
                        cache[rgb] = (bi, bd)
            caches.append(cache)

        # Reassign each tile to the palette with minimum total perceptual pixel error
        changed = 0
        for ti, st in enumerate(snapped_tiles):
            best_p = 0
            best_err = 1 << 60
            for p in range(num_pals):
                cache = caches[p]
                e = 0
                for rgb in st:
                    e += cache[rgb][1]
                if e < best_err:
                    best_err = e
                    best_p = p
            if assign[ti] != best_p:
                assign[ti] = best_p
                changed += 1
        if changed == 0:
            break

    # Final tile encoding with gentle 2x2 ordered Bayer dithering (clean Pier Solar shading)
    bayer = BAYER_2X2_SNES if bits == 5 else BAYER_2X2_GEN
    encoded_tiles = []
    total_mse = 0
    for ti, t in enumerate(rgb_tiles):
        p = assign[ti]
        pal = pals[p]
        buf = bytearray(64)
        for py in range(8):
            brow = bayer[py & 1]
            ro = py * 8
            for px in range(8):
                r, g, b = t[ro + px]
                off = brow[px & 1]
                rr = max(0, min(255, r + off))
                gg = max(0, min(255, g + off))
                bb = max(0, min(255, b + off))
                target = (rr, gg, bb)
                bi = 1
                bd = color_dist2(target, pal[1])
                for pi in range(2, 16):
                    d = color_dist2(target, pal[pi])
                    if d < bd:
                        bd = d
                        bi = pi
                buf[ro + px] = bi
                total_mse += color_dist2((r, g, b), pal[bi])
        encoded_tiles.append(bytes(buf))

    # Lossless exact + H/V-flip deduplication ONLY (zero lossy tile merging -> zero blocky artifacts!)
    unique_tiles = []
    tile_lookup = {}  # (bytes, pal_id) -> (tid, hflip, vflip)
    tilemap = []      # [(tid, pal_id, hflip, vflip), ...]

    for ti, raw_t in enumerate(encoded_tiles):
        p = assign[ti]
        hit = tile_lookup.get((raw_t, p))
        if hit is not None:
            tid, hf, vf = hit
            tilemap.append((tid, p, hf, vf))
        else:
            tid = len(unique_tiles)
            unique_tiles.append(raw_t)
            th = flip_tile_h(raw_t)
            tv = flip_tile_v(raw_t)
            thv = flip_tile_v(th)
            tile_lookup[(raw_t, p)] = (tid, 0, 0)
            tile_lookup.setdefault((th, p), (tid, 1, 0))
            tile_lookup.setdefault((tv, p), (tid, 0, 1))
            tile_lookup.setdefault((thv, p), (tid, 1, 1))
            tilemap.append((tid, p, 0, 0))

    assert len(unique_tiles) <= MAX_ROM_TILES, f"Unexpected tile count {len(unique_tiles)}"
    avg_err = (total_mse / (ROM_W * ROM_H * 11.0)) ** 0.5
    return pals, unique_tiles, tilemap, avg_err


def pack_genesis_tile(tile_64):
    """8x8 4bpp packed nibbles -> 32 bytes (big-endian u32 rows for SGDK)."""
    out = bytearray(32)
    for y in range(8):
        for x in range(0, 8, 2):
            p0 = tile_64[y * 8 + x] & 0x0F
            p1 = tile_64[y * 8 + x + 1] & 0x0F
            out[y * 4 + (x >> 1)] = (p0 << 4) | p1
    return out


def pack_snes_tile(tile_64):
    """8x8 4bpp planar -> 32 bytes (planes 0/1 interleaved first 16B, planes 2/3 next 16B)."""
    out = bytearray(32)
    for y in range(8):
        bp0 = bp1 = bp2 = bp3 = 0
        for x in range(8):
            c = tile_64[y * 8 + x] & 0x0F
            bit = 7 - x
            if c & 1: bp0 |= (1 << bit)
            if c & 2: bp1 |= (1 << bit)
            if c & 4: bp2 |= (1 << bit)
            if c & 8: bp3 |= (1 << bit)
        out[y * 2 + 0] = bp0
        out[y * 2 + 1] = bp1
        out[16 + y * 2 + 0] = bp2
        out[16 + y * 2 + 1] = bp3
    return out


def build_sdl2_header():
    print(f"Baking {len(SDL_PLATES)} painted RGB565 plates ({SDL_W}x{SDL_H}) + {len(PORTRAITS)} portraits -> {OUT_SDL2}")
    with open(OUT_SDL2, "w") as f:
        f.write("/* Auto-generated by engine/tools/bake_cinema_cg.py — DO NOT EDIT */\n")
        f.write("#ifndef HEARTH_CINEMA_CG_SDL2_H\n#define HEARTH_CINEMA_CG_SDL2_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"#define CINEMA_SDL_PLATE_COUNT {len(SDL_PLATES)}\n")
        f.write(f"#define CINEMA_SDL_W {SDL_W}\n")
        f.write(f"#define CINEMA_SDL_H {SDL_H}\n\n")
        f.write(f"static const uint16_t CINEMA_SDL_PLATES[{len(SDL_PLATES)}][{SDL_W * SDL_H}] = {{\n")
        for idx, (name, fname) in enumerate(SDL_PLATES):
            raw = read_rgb(os.path.join(IMG_DIR, fname), SDL_W, SDL_H, crisp_16bit=False)
            f.write(f"    /* {idx}: {name} */\n    {{\n")
            vals = [rgb888_to_rgb565(raw[i], raw[i + 1], raw[i + 2]) for i in range(0, len(raw), 3)]
            for row in range(0, len(vals), 16):
                f.write("        " + ",".join(f"0x{v:04X}" for v in vals[row:row + 16]) + ",\n")
            f.write("    },\n")
        f.write("};\n\n")
        f.write(f"#define CINEMA_SDL_PORTRAIT_COUNT {len(PORTRAITS)}\n")
        f.write(f"#define CINEMA_SDL_PORT_W {PORT_W}\n")
        f.write(f"#define CINEMA_SDL_PORT_H {PORT_H}\n\n")
        f.write(f"static const uint16_t CINEMA_SDL_PORTRAITS[{len(PORTRAITS)}][{PORT_W * PORT_H}] = {{\n")
        for idx, (name, fname) in enumerate(PORTRAITS):
            raw = read_rgb(os.path.join(IMG_DIR, fname), PORT_W, PORT_H, crisp_16bit=False)
            f.write(f"    /* {idx}: {name} */\n    {{\n")
            vals = [rgb888_to_rgb565(raw[i], raw[i + 1], raw[i + 2]) for i in range(0, len(raw), 3)]
            for row in range(0, len(vals), 16):
                f.write("        " + ",".join(f"0x{v:04X}" for v in vals[row:row + 16]) + ",\n")
            f.write("    },\n")
        f.write("};\n\n#endif /* HEARTH_CINEMA_CG_SDL2_H */\n")


def build_rom_headers():
    print(f"Baking {len(ROM_PLATES)} Pier Solar-style multi-palette ROM plates ({ROM_W}x{ROM_H}, <= {MAX_ROM_TILES} tiles)")
    snes_baked = []
    gen_baked = []

    for idx, (name, fname) in enumerate(ROM_PLATES):
        raw = read_rgb(os.path.join(IMG_DIR, fname), ROM_W, ROM_H, crisp_16bit=True)
        # SNES: 4 independent palettes (60 colors, BGR555)
        s_pals, s_tiles, s_map, s_err = quantize_plate_multipal(raw, num_pals=4, bits=5)
        s_pals_bgr = [[rgb888_to_bgr555(*c) for c in pal] for pal in s_pals]
        snes_baked.append((idx, name, s_tiles, s_map, s_pals_bgr))

        # Genesis: 3 independent palettes (45 colors, BGR333 -> PAL0, PAL1, PAL3; PAL2 stays UI!)
        g_pals, g_tiles, g_map, g_err = quantize_plate_multipal(raw, num_pals=3, bits=3)
        g_pals_bgr = [[rgb888_to_bgr333(*c) for c in pal] for pal in g_pals]
        gen_baked.append((idx, name, g_tiles, g_map, g_pals_bgr))

        print(f"  Plate {idx:2d} ({name:20s}): SNES={len(s_tiles):3d}t/60c (rms={s_err:.1f}) | GEN={len(g_tiles):3d}t/45c (rms={g_err:.1f})")

    # 1. Write C header: SNES externs + tile counts; Genesis 3-palette arrays + pre-encoded map attr words
    with open(OUT_ROM, "w") as f:
        f.write("/* Auto-generated by engine/tools/bake_cinema_cg.py — DO NOT EDIT */\n")
        f.write("/* Pier Solar-style Multi-Palette 16-bit Cinema CGs (SNES: 60-col / Genesis: 45-col) */\n")
        f.write("#ifndef HEARTH_CINEMA_CG_ROM_H\n#define HEARTH_CINEMA_CG_ROM_H\n\n")
        f.write("#include \"hearth_types.h\"\n\n")
        f.write(f"#define CINEMA_ROM_PLATE_COUNT {len(ROM_PLATES)}\n")
        f.write(f"#define CINEMA_ROM_TW {ROM_TW}\n")
        f.write(f"#define CINEMA_ROM_TH {ROM_TH}\n")
        f.write(f"#define CINEMA_ROM_MAP_LEN {ROM_TW * ROM_TH}\n")
        f.write("#define CINEMA_SNES_PAL_COUNT 4u\n")
        f.write("#define CINEMA_GEN_PAL_COUNT 3u\n\n")

        f.write("#if defined(HEARTH_TARGET_SNES)\n")
        f.write("static const uint16_t CINEMA_ROM_TILE_COUNTS[CINEMA_ROM_PLATE_COUNT] = {\n    ")
        f.write(", ".join(f"{len(b[2])}u" for b in snes_baked))
        f.write("\n};\n\n")
        f.write("/* SNES places each 60-color plate in its own SUPERFREE LoROM bank (cinema_cg_snes.asm) */\n")
        for idx, name, _tiles, _map, _pals in snes_baked:
            f.write(f"extern char cg_plate{idx}_tiles, cg_plate{idx}_map, cg_plate{idx}_pal; /* {name} */\n")
        f.write("#else\n\n")

        f.write("static const uint16_t CINEMA_ROM_TILE_COUNTS[CINEMA_ROM_PLATE_COUNT] = {\n    ")
        f.write(", ".join(f"{len(b[2])}u" for b in gen_baked))
        f.write("\n};\n\n")

        for idx, name, tiles, tilemap, _pals in gen_baked:
            f.write(f"/* Plate {idx}: {name} (45 colors across PAL0/PAL1/PAL3, H/V-flip encoded) */\n")
            # Encode Genesis attr bits: bits 14..13 = PAL slot (0,1,3), bit 12 = VFLIP, bit 11 = HFLIP, bits 10..0 = tid
            enc_map = []
            for tid, p, hf, vf in tilemap:
                pal_slot = GEN_PAL_SLOTS[p]
                word = (pal_slot << 13) | (vf << 12) | (hf << 11) | (tid & 0x07FF)
                enc_map.append(word)
            f.write(f"static const uint16_t CINEMA_ROM_MAP_{idx}[CINEMA_ROM_MAP_LEN] = {{\n")
            for r in range(0, len(enc_map), 16):
                f.write("    " + ",".join(f"0x{v:04X}u" for v in enc_map[r:r + 16]) + ",\n")
            f.write("};\n\n")

            f.write(f"static const uint32_t CINEMA_GEN_TILES_{idx}[{len(tiles) * 8}] = {{\n")
            for t in tiles:
                packed = pack_genesis_tile(t)
                u32s = struct.unpack(">8I", packed)
                f.write("    " + ",".join(f"0x{v:08X}u" for v in u32s) + ",\n")
            f.write("};\n\n")

        f.write("/* 3 x 16-color BGR333 palettes per plate -> loaded into CRAM PAL0, PAL1, PAL3 */\n")
        f.write("static const uint16_t CINEMA_ROM_PALS[CINEMA_ROM_PLATE_COUNT][3][16] = {\n")
        for _idx, name, _tiles, _map, pals_bgr in gen_baked:
            f.write(f"    /* {name} */\n    {{\n")
            for p in range(3):
                f.write("        {" + ",".join(f"0x{v:04X}u" for v in pals_bgr[p]) + "},\n")
            f.write("    },\n")
        f.write("};\n")
        f.write("static const uint32_t * const CINEMA_GEN_TILE_PTRS[CINEMA_ROM_PLATE_COUNT] = {\n")
        for idx in range(len(ROM_PLATES)):
            f.write(f"    CINEMA_GEN_TILES_{idx},\n")
        f.write("};\n")
        f.write("static const uint16_t * const CINEMA_ROM_MAP_PTRS[CINEMA_ROM_PLATE_COUNT] = {\n")
        for idx in range(len(ROM_PLATES)):
            f.write(f"    CINEMA_ROM_MAP_{idx},\n")
        f.write("};\n")
        f.write("#endif /* HEARTH_TARGET_SNES */\n\n")
        f.write("#endif /* HEARTH_CINEMA_CG_ROM_H */\n")

    # 2. Write WLA-DX assembly file for SNES (each 60-color plate in its own SUPERFREE LoROM bank)
    with open(OUT_SNES_ASM, "w") as f:
        f.write("; Auto-generated by engine/tools/bake_cinema_cg.py — DO NOT EDIT\n")
        f.write("; Pier Solar-style 60-color (4-palette) SNES 4bpp Cinema CG plates in SUPERFREE LoROM banks\n\n")
        f.write('.include "hdr.asm"\n\n')
        for idx, name, tiles, tilemap, pals_bgr in snes_baked:
            total_bytes = len(tiles) * 32 + len(tilemap) * 2 + 128
            f.write(f'; --- Plate {idx}: {name} ({len(tiles)} tiles, 4 palettes = 60 colors, {total_bytes} bytes) ---\n')
            f.write(f'.section ".rodata_cg{idx}" superfree\n\n')
            f.write(f'cg_plate{idx}_tiles:\n')
            for t in tiles:
                packed = pack_snes_tile(t)
                f.write('.db ' + ','.join(f'${b:02X}' for b in packed[:16]) + '\n')
                f.write('.db ' + ','.join(f'${b:02X}' for b in packed[16:]) + '\n')
            f.write(f'\ncg_plate{idx}_map:\n')
            # Pre-encoded SNES BG1 tilemap words: bit15=VFLIP, bit14=HFLIP, bit12..10=PAL(0..3), bit9..0=TILE
            snes_words = [
                (vf << 15) | (hf << 14) | (p << 10) | ((SNES_CG_TILE_BASE + tid) & 0x03FF)
                for (tid, p, hf, vf) in tilemap
            ]
            for r in range(0, len(snes_words), 8):
                chunk = snes_words[r:r + 8]
                f.write('.dw ' + ','.join(f'${w:04X}' for w in chunk) + '\n')
            f.write(f'\ncg_plate{idx}_pal:\n')
            for p in range(4):
                f.write('.dw ' + ','.join(f'${v:04X}' for v in pals_bgr[p][:8]) + '\n')
                f.write('.dw ' + ','.join(f'${v:04X}' for v in pals_bgr[p][8:]) + '\n')
            f.write('.ends\n\n')


if __name__ == "__main__":
    if "--rom-only" not in sys.argv:
        build_sdl2_header()
    build_rom_headers()
    print("Done baking all Pier Solar-style multi-palette cinema CG & portrait assets!")
