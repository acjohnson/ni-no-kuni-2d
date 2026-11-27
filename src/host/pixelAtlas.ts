/**
 * Project Hearth — Handcrafted 16-Bit Indexed-Palette Pixel Art Atlas
 *
 * Generates crisp 16x16 tiles and multi-frame character/familiar/enemy sprites
 * strictly from our 4 x 16-color palettes, with live PNG override slots so
 * custom user pixel art can replace any asset without changing core code.
 */

import {
  Direction,
  O_CANOPY_BL,
  O_CANOPY_BR,
  O_CANOPY_TL,
  O_CANOPY_TR,
  O_MAPLE_MID,
  O_MAPLE_TL,
  O_MAPLE_TOP,
  O_MAPLE_TR,
  O_OAK_CROWN,
  O_PINE_TOP,
  O_ROOF_EAVE,
  O_SHRINE_ARCH,
  SpriteId,
  T_GRASS,
  TILE_SIZE,
} from '../core/types';
import {
  CHRONO_ANIM_META_IDS,
  CHRONO_ANIM_PX_B64,
  CHRONO_BASE_PX_B64,
  CHRONO_META_COUNT,
  CHRONO_META_QPAL,
  CHRONO_ZONE_PALS,
  CM_OV_CANOPY_BL,
  CM_OV_CANOPY_BR,
  CM_OV_CANOPY_TL,
  CM_OV_CANOPY_TR,
  CM_OV_MAPLE_MID,
  CM_OV_MAPLE_TL,
  CM_OV_MAPLE_TOP,
  CM_OV_MAPLE_TR,
  CM_OV_OAK_CROWN,
  CM_OV_PINE_TOP,
  CM_OV_ROOF_EAVE,
  CM_OV_SHRINE_ARCH,
} from './chronoTilesetData';

function decodeB64Bytes(b64: string): Uint8Array {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

let cachedChronoBasePx: Uint8Array | null = null;
let cachedChronoAnimPx: Uint8Array | null = null;

function getChronoBasePx(): Uint8Array {
  if (!cachedChronoBasePx) cachedChronoBasePx = decodeB64Bytes(CHRONO_BASE_PX_B64);
  return cachedChronoBasePx;
}

function getChronoAnimPx(): Uint8Array {
  if (!cachedChronoAnimPx) cachedChronoAnimPx = decodeB64Bytes(CHRONO_ANIM_PX_B64);
  return cachedChronoAnimPx;
}

function hexToRgbaTuple(hex: string): [number, number, number, number] {
  if (!hex || hex.startsWith('rgba')) return [0, 0, 0, 0];
  const v = parseInt(hex.slice(1), 16);
  return [(v >> 16) & 255, (v >> 8) & 255, v & 255, 255];
}

function lerpRgbaTuple(
  a: [number, number, number, number],
  b: [number, number, number, number],
  frac16: number
): [number, number, number, number] {
  const inv = 16 - frac16;
  return [
    (a[0] * inv + b[0] * frac16 + 8) >> 4,
    (a[1] * inv + b[1] * frac16 + 8) >> 4,
    (a[2] * inv + b[2] * frac16 + 8) >> 4,
    a[3],
  ];
}

const TOD_PAL_KEYS = [6, 1, 7, 8] as const; // Morning, Afternoon, Dusk, Night

export type SwappableSlotId =
  | 'hero'
  | 'pip'
  | 'familiar_bramble'
  | 'familiar_lumina'
  | 'enemy_gloomcap'
  | 'boss_briar_warden'
  | 'tileset_grass';

function makeCanvas(w: number, h: number): HTMLCanvasElement {
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  return c;
}

/** Paint a pixel string pattern using an indexed 16-color palette ('.' = transparent, '1'-'f' = palette index) */
function drawPattern(
  ctx: CanvasRenderingContext2D,
  palettes: readonly (readonly string[])[],
  pattern: string[],
  palIdx: number,
  ox = 0,
  oy = 0
): void {
  const pal = palettes[palIdx];
  for (let y = 0; y < pattern.length; y++) {
    const row = pattern[y];
    for (let x = 0; x < row.length; x++) {
      const ch = row[x];
      if (ch === '.' || ch === ' ') continue;
      const colorIdx = parseInt(ch, 16);
      if (!isNaN(colorIdx) && colorIdx > 0 && colorIdx < pal.length) {
        ctx.fillStyle = pal[colorIdx];
        ctx.fillRect(ox + x, oy + y, 1, 1);
      }
    }
  }
}

export class PixelAtlas {
  private activePalettes: (readonly string[])[];
  private readonly zonePalettes: readonly (readonly string[])[];
  private currentTerrainPalId = 0;
  private currentTodPhase = -1;
  private currentMountainVista = false;
  private bg1Tiles = new Map<number, HTMLCanvasElement[]>(); // tileId -> animation frames
  private bg2Tiles = new Map<number, HTMLCanvasElement>();
  private sprites = new Map<string, HTMLCanvasElement>(); // key: `${spriteId}_${dir}_${frame}`
  private overrides = new Map<SwappableSlotId, HTMLImageElement>();

  constructor(
    palettes: readonly (readonly string[])[],
    zonePalettes: readonly (readonly string[])[] = []
  ) {
    this.activePalettes = [...palettes];
    this.zonePalettes = zonePalettes;
    this.buildAllTiles();
    this.buildAllSprites();
  }

  public setTerrainPalette(palId: number, todPhase = 0, hasMountainVista = false): void {
    const isTodBlend =
      hasMountainVista && (palId === 6 || palId === 1 || palId === 7 || palId === 8);
    const effPhase = isTodBlend ? (todPhase & 63) : 0;
    if (
      palId === this.currentTerrainPalId &&
      effPhase === this.currentTodPhase &&
      isTodBlend === this.currentMountainVista
    ) {
      return;
    }
    if (palId >= 0 && palId < CHRONO_ZONE_PALS.length) {
      this.currentTerrainPalId = palId;
      this.currentTodPhase = effPhase;
      this.currentMountainVista = isTodBlend;
      if (palId < this.zonePalettes.length) {
        this.activePalettes[0] = this.zonePalettes[palId];
      }
      this.buildAllTiles();
    }
  }

  public setOverride(slot: SwappableSlotId, img: HTMLImageElement): void {
    this.overrides.set(slot, img);
  }

  public clearOverride(slot: SwappableSlotId): void {
    this.overrides.delete(slot);
  }

  public hasOverride(slot: SwappableSlotId): boolean {
    return this.overrides.has(slot);
  }

  public getOverride(slot: SwappableSlotId): HTMLImageElement | undefined {
    return this.overrides.get(slot);
  }

  public getBg1Tile(tileId: number, animTick: number): HTMLCanvasElement | HTMLImageElement {
    if (tileId === T_GRASS && this.overrides.has('tileset_grass')) {
      return this.overrides.get('tileset_grass')!;
    }
    const frames = this.bg1Tiles.get(tileId);
    if (!frames || frames.length === 0) return this.bg1Tiles.get(T_GRASS)![0];
    return frames[animTick % frames.length];
  }

  public getBg2Tile(tileId: number): HTMLCanvasElement | undefined {
    return this.bg2Tiles.get(tileId);
  }

  public getSprite(spriteId: SpriteId, dir: Direction, frame: number): HTMLCanvasElement | HTMLImageElement {
    if (
      (spriteId === 'hero' ||
        spriteId === 'pip' ||
        spriteId === 'familiar_bramble' ||
        spriteId === 'familiar_lumina' ||
        spriteId === 'enemy_gloomcap' ||
        spriteId === 'boss_briar_warden') &&
      this.overrides.has(spriteId)
    ) {
      return this.overrides.get(spriteId)!;
    }
    const exact = this.sprites.get(`${spriteId}_${dir}_${frame}`);
    if (exact) return exact;
    const fallbackDir = this.sprites.get(`${spriteId}_0_${frame}`);
    if (fallbackDir) return fallbackDir;
    const fallback0 = this.sprites.get(`${spriteId}_0_0`);
    if (fallback0) return fallback0;
    return this.bg1Tiles.get(T_GRASS)![0];
  }

  public getSlotPreviewDataUrl(slot: SwappableSlotId): string {
    const ov = this.overrides.get(slot);
    if (ov) return ov.src;
    if (slot === 'tileset_grass') {
      return this.bg1Tiles.get(T_GRASS)![0].toDataURL();
    }
    const c = this.sprites.get(`${slot}_0_0`);
    return c ? c.toDataURL() : '';
  }

  /* ========================================================================
   * UNIFIED 60-COLOR CHRONO TRIGGER / TRIALS OF MANA 16x16 MASTER TILESET
   * ======================================================================== */
  private buildAllTiles(): void {
    const palId =
      this.currentTerrainPalId >= 0 && this.currentTerrainPalId < CHRONO_ZONE_PALS.length
        ? this.currentTerrainPalId
        : 0;
    let rgbaPals: [number, number, number, number][][];
    if (this.currentMountainVista) {
      const seg = (this.currentTodPhase >> 4) & 3;
      const frac = this.currentTodPhase & 15;
      const pA = CHRONO_ZONE_PALS[TOD_PAL_KEYS[seg]];
      const pB = CHRONO_ZONE_PALS[TOD_PAL_KEYS[(seg + 1) & 3]];
      rgbaPals = pA.map((subA, sp) =>
        subA.map((hexA, ci) =>
          lerpRgbaTuple(hexToRgbaTuple(hexA), hexToRgbaTuple(pB[sp][ci]), frac)
        )
      );
    } else {
      const fourPals = CHRONO_ZONE_PALS[palId];
      rgbaPals = fourPals.map((sub) => sub.map(hexToRgbaTuple));
    }

    const basePx = getChronoBasePx();
    const animPx = getChronoAnimPx();

    const renderPacked16 = (
      src: Uint8Array,
      byteOffset: number,
      qpal: readonly [number, number, number, number]
    ): HTMLCanvasElement => {
      const c = makeCanvas(TILE_SIZE, TILE_SIZE);
      const ctx = c.getContext('2d')!;
      const imgData = ctx.createImageData(TILE_SIZE, TILE_SIZE);
      const data = imgData.data;
      for (let y = 0; y < 16; y++) {
        const qRow = y >= 8 ? 2 : 0;
        const rowByte = byteOffset + y * 8;
        for (let x = 0; x < 16; x += 2) {
          const b = src[rowByte + (x >> 1)];
          const c0 = (b >> 4) & 15;
          const c1 = b & 15;
          const pIdx0 = qpal[qRow + (x >= 8 ? 1 : 0)];
          const pIdx1 = qpal[qRow + (x + 1 >= 8 ? 1 : 0)];
          if (c0 > 0) {
            const rgba = rgbaPals[pIdx0][c0];
            const dst = (y * 16 + x) * 4;
            data[dst] = rgba[0];
            data[dst + 1] = rgba[1];
            data[dst + 2] = rgba[2];
            data[dst + 3] = rgba[3];
          }
          if (c1 > 0) {
            const rgba = rgbaPals[pIdx1][c1];
            const dst = (y * 16 + x + 1) * 4;
            data[dst] = rgba[0];
            data[dst + 1] = rgba[1];
            data[dst + 2] = rgba[2];
            data[dst + 3] = rgba[3];
          }
        }
      }
      ctx.putImageData(imgData, 0, 0);
      return c;
    };

    this.bg1Tiles.clear();
    for (let mid = 0; mid < CHRONO_META_COUNT; mid++) {
      const canvas = renderPacked16(basePx, mid * 128, CHRONO_META_QPAL[mid]);
      this.bg1Tiles.set(mid, [canvas]);
    }

    // Attach 4-frame animated sequences for shimmering water, foaming shorelines, wildflowers, rapids & hearth
    for (let ai = 0; ai < CHRONO_ANIM_META_IDS.length; ai++) {
      const mid = CHRONO_ANIM_META_IDS[ai];
      const qpal = CHRONO_META_QPAL[mid];
      const frames: HTMLCanvasElement[] = [];
      for (let f = 0; f < 4; f++) {
        frames.push(renderPacked16(animPx, (ai * 4 + f) * 128, qpal));
      }
      this.bg1Tiles.set(mid, frames);
    }

    // Bind BG2 transparent overhead canopies, overhanging terracotta roof eaves & shrine arch
    this.bg2Tiles.clear();
    const bindOv = (ovId: number, cmId: number) => {
      const frames = this.bg1Tiles.get(cmId);
      if (frames && frames[0]) this.bg2Tiles.set(ovId, frames[0]);
    };
    bindOv(O_CANOPY_TL, CM_OV_CANOPY_TL);
    bindOv(O_CANOPY_TR, CM_OV_CANOPY_TR);
    bindOv(O_CANOPY_BL, CM_OV_CANOPY_BL);
    bindOv(O_CANOPY_BR, CM_OV_CANOPY_BR);
    bindOv(O_ROOF_EAVE, CM_OV_ROOF_EAVE);
    bindOv(O_SHRINE_ARCH, CM_OV_SHRINE_ARCH);
    bindOv(O_MAPLE_TL, CM_OV_MAPLE_TL);
    bindOv(O_MAPLE_TR, CM_OV_MAPLE_TR);
    bindOv(O_PINE_TOP, CM_OV_PINE_TOP);
    bindOv(O_OAK_CROWN, CM_OV_OAK_CROWN);
    bindOv(O_MAPLE_MID, CM_OV_MAPLE_MID);
    bindOv(O_MAPLE_TOP, CM_OV_MAPLE_TOP);
  }

  /* ========================================================================
   * HANDCRAFTED 16-BIT CHARACTER, FAMILIAR, NPC & MONSTER SPRITES
   * ======================================================================== */
  private buildAllSprites(): void {
    const PALETTES = this.activePalettes;
    const paintPattern = (
      ctx: CanvasRenderingContext2D,
      pattern: string[],
      palIdx: number,
      ox = 0,
      oy = 0
    ) => drawPattern(ctx, PALETTES, pattern, palIdx, ox, oy);
    const register = (id: SpriteId, dir: Direction, frame: number, c: HTMLCanvasElement) => {
      this.sprites.set(`${id}_${dir}_${frame}`, c);
    };

    /* 1. HERO (ROWAN) — Expressive 16x24 JRPG Hero with Flowing Terracotta Scarf
     *    4 directions x 12 poses (Idle, 3-beat Walk, Idle Blink, 2 Dash, 5 Battle) */
    const heroHeadDown = [
      '.....111111.....',
      '...1145554411...',
      '..145544445441..',
      '.14544444444441.',
      '.14414422441441.',
      '.14222222222241.',
      '..12e122221e21..',
      '..121122221121..',
      '...1232222321...',
      '....11222211....',
    ];
    const heroHeadDownBlink = [
      '.....111111.....',
      '...1145554411...',
      '..145544445441..',
      '.14544444444441.',
      '.14414422441441.',
      '.14222222222241.',
      '..122222222221..',
      '..121122221121..',
      '...1232222321...',
      '....11222211....',
    ];
    const heroHeadUp = [
      '.....111111.....',
      '...1145554411...',
      '..145544445541..',
      '.14544444445441.',
      '.14444555444441.',
      '.14444444444441.',
      '..144444444441..',
      '..144444444441..',
      '...1144444411...',
      '....11111111....',
    ];
    const heroHeadSide = [
      '....1111111.....',
      '..11455544411...',
      '.1455444454441..',
      '.14444444445441.',
      '..1444444422441.',
      '..144444222221..',
      '...14442221e21..',
      '...14432221121..',
      '....114222231...',
      '......112211....',
    ];
    const heroHeadSideBlink = [
      '....1111111.....',
      '..11455544411...',
      '.1455444454441..',
      '.14444444445441.',
      '..1444444422441.',
      '..144444222221..',
      '...14442222221..',
      '...14432221121..',
      '....114222231...',
      '......112211....',
    ];
    const heroTorsoFront = [
      '...118899998811.',
      '..1899888888981.',
      '..168889aa98861.',
      '..167666bb66761.',
      '.1676667aa766761',
      '.126666666666621',
      '..114544aa44541.',
      '...16666666661..',
      '...1cccccccc1...',
    ];
    const heroTorsoUp = [
      '...118899998811.',
      '..1899888888981.',
      '..1688888888861.',
      '..1676666666761.',
      '.167666666666761',
      '.126666666666621',
      '..1144444444441.',
      '...16666666661..',
      '...1cccccccc1...',
    ];
    const heroTorsoSide = [
      '....1188999881..',
      '...18998888981..',
      '...16688998861..',
      '...1676666aa61..',
      '...16766676761..',
      '...16662266661..',
      '....145444aa1...',
      '....166666661...',
      '....1cccccc1....',
    ];

    const pal1 = PALETTES[1];
    const paintHeroFrame = (effDir: Direction, pose: number): HTMLCanvasElement => {
      const base = makeCanvas(16, 24);
      const ctx = base.getContext('2d')!;
      const isDash = pose === 5 || pose === 6;
      const isBattle = pose >= 7;
      const drawDir: Direction = isBattle ? 3 : effDir === 2 ? 3 : effDir;
      let bob = 0;
      let leanX = 0;
      let lLegX = 0;
      let rLegX = 0;
      let lLegY = 0;
      let rLegY = 0;
      let scarfWave = 0;

      if (pose === 1) {
        bob = -1; lLegX = -1; rLegX = 1; lLegY = -1; scarfWave = -1;
      } else if (pose === 3) {
        bob = -1; lLegX = 1; rLegX = -1; rLegY = -1; scarfWave = 1;
      } else if (pose === 5) {
        bob = -1; leanX = 1; lLegX = -2; rLegX = 2; lLegY = -1; scarfWave = -2;
      } else if (pose === 6) {
        bob = 0; leanX = 1; lLegX = 2; rLegX = -2; rLegY = -1; scarfWave = -1;
      } else if (pose === 8) {
        leanX = 2; lLegX = -2; rLegX = 2; scarfWave = -2;
      } else if (pose === 9) {
        bob = -1; scarfWave = -2;
      } else if (pose === 10) {
        bob = 1; leanX = -2; scarfWave = 1;
      } else if (pose === 11) {
        bob = -1; scarfWave = -1;
      }

      const rect = (x: number, y: number, w: number, h: number, idx: number) => {
        ctx.fillStyle = pal1[idx];
        ctx.fillRect(x, y, w, h);
      };

      // 1. Legs & Boots
      if (drawDir === 0 || drawDir === 1) {
        rect(4, 18 + lLegY, 3, 2, 12);
        rect(3, 20 + lLegY, 4, 4, 1);
        rect(4, 20 + lLegY, 2, 3, 15);
        rect(4, 20 + lLegY, 1, 1, 5);
        rect(9, 18 + rLegY, 3, 2, 12);
        rect(9, 20 + rLegY, 4, 4, 1);
        rect(10, 20 + rLegY, 2, 3, 15);
        rect(10, 20 + rLegY, 1, 1, 5);
      } else {
        const bx = 5 + lLegX;
        const fx = 8 + rLegX;
        rect(bx, 18 + lLegY, 3, 2, 12);
        rect(bx, 20 + lLegY, 4, 4, 1);
        rect(bx + 1, 20 + lLegY, 2, 3, 15);
        rect(fx, 18 + rLegY, 3, 2, 12);
        rect(fx, 20 + rLegY, 4, 4, 1);
        rect(fx + 1, 20 + rLegY, 2, 3, 15);
        rect(fx + 2, 21 + rLegY, 1, 1, 5);
      }

      // 2. Animated Flowing Terracotta Scarf Tail
      if (drawDir === 0) {
        const sy = 11 + bob;
        rect(11, sy, 3, 4, 1);
        rect(11, sy + 1, 2, 2, 8);
        rect(12, sy + 1, 1, 1, 9);
        const sx = 12 + (scarfWave < 0 ? 1 : 0);
        rect(sx, sy + 3, 2, 3, 8);
        rect(sx + 1, sy + 4, 1, 1, 9);
      } else if (drawDir === 1) {
        const sy = 11 + bob;
        rect(5 + (scarfWave >> 1), sy, 6, 5, 1);
        rect(6 + (scarfWave >> 1), sy, 4, 4, 8);
        rect(7 + (scarfWave >> 1), sy, 2, 3, 9);
        rect(5 + scarfWave, sy + 4, 2, 3, 8);
        rect(9 + scarfWave, sy + 4, 2, 2, 9);
      } else {
        const sy = 10 + bob;
        const tailY = sy + 1 + scarfWave;
        if (isDash || pose === 8 || pose === 9) {
          rect(0, tailY + 1, 5, 4, 1);
          rect(0, tailY + 2, 4, 2, 8);
          rect(1, tailY + 2, 3, 1, 9);
          rect(0, tailY + 1, 1, 1, 9);
        } else {
          rect(1, sy + 1, 4, 4, 1);
          rect(1, sy + 2, 3, 2, 8);
          rect(2, sy + 2, 1, 1, 9);
          const sx = 1 + (scarfWave > 0 ? 1 : 0);
          rect(sx, sy + 4, 2, 3, 8);
          rect(sx, sy + 5, 1, 1, 9);
        }
      }

      // 3. Layered Tunic Torso
      if (drawDir === 0) {
        paintPattern(ctx, heroTorsoFront, 1, 0, 10 + bob);
        if (pose === 1 || pose === 5) {
          rect(1, 14 + bob, 1, 1, 2);
          rect(14, 16 + bob, 1, 1, 2);
        } else if (pose === 3 || pose === 6) {
          rect(1, 16 + bob, 1, 1, 2);
          rect(14, 14 + bob, 1, 1, 2);
        }
      } else if (drawDir === 1) {
        paintPattern(ctx, heroTorsoUp, 1, 0, 10 + bob);
      } else {
        paintPattern(ctx, heroTorsoSide, 1, leanX, 10 + bob);
      }

      // 4. Sculpted Head & Blink
      if (drawDir === 0) {
        paintPattern(ctx, pose === 4 ? heroHeadDownBlink : heroHeadDown, 1, 0, 1 + bob);
      } else if (drawDir === 1) {
        paintPattern(ctx, heroHeadUp, 1, 0, 1 + bob);
      } else {
        paintPattern(
          ctx,
          pose === 4 || pose === 10 ? heroHeadSideBlink : heroHeadSide,
          1,
          leanX,
          1 + bob
        );
      }

      // 5. Battle Hearthwood Wand & Heroic Action Arms
      if (pose === 7) {
        rect(11, 13 + bob, 2, 2, 2);
        rect(12, 12 + bob, 1, 1, 4);
        rect(13, 11 + bob, 1, 1, 5);
        rect(14, 10 + bob, 1, 1, 10);
        rect(14, 9 + bob, 1, 1, 11);
      } else if (pose === 8) {
        rect(11, 12 + bob, 3, 2, 2);
        rect(13, 11 + bob, 2, 2, 5);
        rect(15, 10 + bob, 1, 1, 11);
        rect(15, 11 + bob, 1, 1, 14);
        rect(14, 9 + bob, 1, 1, 10);
      } else if (pose === 9) {
        rect(11, 9 + bob, 2, 4, 6);
        rect(11, 7 + bob, 2, 2, 2);
        rect(12, 3 + bob, 1, 5, 5);
        rect(11, 1 + bob, 3, 3, 10);
        rect(12, 2 + bob, 1, 1, 11);
        rect(14, 2 + bob, 1, 1, 14);
      } else if (pose === 11) {
        rect(11, 8 + bob, 2, 5, 7);
        rect(11, 6 + bob, 2, 2, 2);
        rect(12, 2 + bob, 1, 4, 5);
        rect(12, 1 + bob, 1, 1, 11);
        rect(13, 2 + bob, 1, 1, 10);
      }

      if (!isBattle && effDir === 2) {
        const flipped = makeCanvas(16, 24);
        const fctx = flipped.getContext('2d')!;
        fctx.translate(16, 0);
        fctx.scale(-1, 1);
        fctx.drawImage(base, 0, 0);
        return flipped;
      }
      return base;
    };

    for (let dir = 0 as Direction; dir < 4; dir = (dir + 1) as Direction) {
      for (let pose = 0; pose < 12; pose++) {
        register('hero', dir, pose, paintHeroFrame(dir, pose));
      }
    }

    /* 2. PETITE BOUNCY GARDEN-GNOME PIP — ~11x13 inside 16x16 (Oliver & Mr. Drippy scale!) */
    const pipPoses: string[][] = [
      /* Pose 0: Grounded / Idle (petite moss-green leaf cap, terracotta capelet, lantern, ZERO wings) */
      [
        '................',
        '................',
        '................',
        '.......11.......',
        '......1671.11...',
        '.....167761771..',
        '....166776661...',
        '....14555541....',
        '...1212222121...',
        '....12922921....',
        '...1898888981...',
        '...18a1bb1a81...',
        '....1a1bb1a1....',
        '....1aaaaaa1....',
        '.....141141.....',
        '....1ff11ff1....',
      ],
      /* Pose 1: Mid-Air Stretch Hop */
      [
        '................',
        '.......11..11...',
        '......16711771..',
        '.....16776661...',
        '.....1667761....',
        '....14555541....',
        '....12122121....',
        '....12922921....',
        '...1898888981...',
        '....18a1b1a81...',
        '....1a1bb1a1....',
        '.....1aaaa1.....',
        '.....141141.....',
        '....1f1..1f1....',
        '................',
        '................',
      ],
      /* Pose 2: Landing / Takeoff Squash */
      [
        '................',
        '................',
        '................',
        '................',
        '................',
        '......1111......',
        '.....16776111...',
        '....1667766771..',
        '...11455555411..',
        '..121222222121..',
        '...1292222921...',
        '..1898a11a8981..',
        '..18aa1bb1aa81..',
        '...1aaaaaaaa1...',
        '...1441111441...',
        '..1fff1..1fff1..',
      ],
    ];
    for (let f = 0; f < 3; f++) {
      const c = makeCanvas(16, 16);
      const ctx = c.getContext('2d')!;
      paintPattern(ctx, pipPoses[f], 1, 0, 0);
      register('pip', 0, f, c);
    }

    /* 3. FAMILIAR A: BRAMBLEKIT (Sun-Sign Acorn Knight) — 20x22 */
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(20, 22);
      const ctx = c.getContext('2d')!;
      const bob = f;
      const pat = [
        '........11..........',
        '.....11144111.......',
        '...114554444411.....',
        '..14544444444441....',
        '..11111111111111....',
        '...122222222221.....',
        '...121a2222a121..1..',
        '...122222222221.1a1.',
        '..16676888867661151.',
        '..167768aa86776151..',
        '...166666666661151..',
        '....1444114441.151..',
        '....1111..1111..1...',
      ];
      paintPattern(ctx, pat, 1, 0, 5 + bob);
      register('familiar_bramble', 0, f, c);
    }

    /* 4. FAMILIAR B: LUMINA (Star-Sign Celestial Wisp) — 20x22 */
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(20, 22);
      const ctx = c.getContext('2d')!;
      const bob = f === 0 ? 0 : -2;
      const pat = [
        '.........11.........',
        '........1bb1........',
        '......111dd111......',
        '....11dddddddd11....',
        '...1ddeeeeeeeedd1...',
        '..1dde1bddddb1edd1..',
        '..1ddeeeddddeeedd1..',
        '...1ccddddddddcc1...',
        '....11cccccccc11....',
        '...1cc11cccc11cc1...',
        '..1dd1..1dd1..1dd1..',
        '..11.....11.....11..',
      ];
      paintPattern(ctx, pat, 1, 0, 4 + bob);
      register('familiar_lumina', 0, f, c);
    }

    /* 5. TALL 16x24 KEY ADULT NPCS (ELDER WREN, CLARA, GUARD BRAM & CAST) — Palette 2 */
    const registerTallNpc = (
      id: SpriteId,
      art: string[],
      palIdx = 2,
      prop?: 'spear' | 'staff' | 'scroll' | 'mallet'
    ) => {
      for (let f = 0; f < 2; f++) {
        const c = makeCanvas(16, 24);
        const ctx = c.getContext('2d')!;
        // Subtle 1px idle breath on upper body while keeping boots firmly planted!
        const rows = f === 1 ? ['................', ...art.slice(0, 15), ...art.slice(16)] : art;
        paintPattern(ctx, rows, palIdx, 0, 0);
        if (prop === 'spear') {
          ctx.fillStyle = PALETTES[2][1];
          ctx.fillRect(14, 1, 2, 22);
          ctx.fillStyle = PALETTES[2][4];
          ctx.fillRect(14, 2, 2, 4);
          ctx.fillStyle = PALETTES[2][7];
          ctx.fillRect(14, 6, 1, 16);
        } else if (prop === 'staff') {
          ctx.fillStyle = PALETTES[2][1];
          ctx.fillRect(14, 5, 2, 18);
          ctx.fillStyle = PALETTES[2][12];
          ctx.fillRect(14, 6, 1, 16);
          ctx.fillStyle = PALETTES[2][7];
          ctx.fillRect(14, 5, 1, 1);
        } else if (prop === 'scroll') {
          ctx.fillStyle = PALETTES[2][1];
          ctx.fillRect(13, 11 + f, 3, 6);
          ctx.fillStyle = PALETTES[2][15];
          ctx.fillRect(14, 12 + f, 1, 4);
        } else if (prop === 'mallet') {
          ctx.fillStyle = PALETTES[2][1];
          ctx.fillRect(13, 9 + f, 3, 9);
          ctx.fillStyle = PALETTES[2][7];
          ctx.fillRect(13, 8 + f, 3, 3);
          ctx.fillStyle = PALETTES[2][12];
          ctx.fillRect(14, 11 + f, 1, 6);
        }
        register(id, 0, f, c);
      }
    };

    registerTallNpc(
      'npc_elder',
      [
        '....11111111....',
        '...1444ff4441...',
        '..145444444541..',
        '..144222222441..',
        '..142122221241..',
        '..142222222241..',
        '..14444ff44441..',
        '...1444444441...',
        '...1144444411...',
        '..166744447661..',
        '.16677444477661.',
        '.16667777776661.',
        '.16266777766261.',
        '.16666677666661.',
        '..166667766661..',
        '..166667766661..',
        '..166667766661..',
        '..166666666661..',
        '..166666666661..',
        '..177777777771..',
        '...1111111111...',
        '....1b1..1b1....',
        '...1bb1..1bb1...',
        '...1111..1111...',
      ],
      2,
      'staff'
    );
    registerTallNpc('npc_clara', [
      '....11111111....',
      '...1cc7777cc1...',
      '..1ccc7777ccc1..',
      '..1cc222222cc1..',
      '..1c22222222c1..',
      '..1c21222212c1..',
      '..1c23222232c1..',
      '...1c222222c1...',
      '...11ffffff11...',
      '..1777ffff7771..',
      '.1777ffffff7771.',
      '.1727ffffff7271.',
      '.1777ffffff7771.',
      '..177ffffff771..',
      '..1677ffff7761..',
      '..1677ffff7761..',
      '..166777777661..',
      '..166777777661..',
      '..166666666661..',
      '..1ffffffffff1..',
      '...1111111111...',
      '....1b1..1b1....',
      '...1bb1..1bb1...',
      '...1111..1111...',
    ]);
    registerTallNpc(
      'npc_guard',
      [
        '.....11aa11.....',
        '....1aaaaaa1....',
        '...1554444551...',
        '..154444444451..',
        '..154111111451..',
        '..154212212451..',
        '..154222222451..',
        '...1552222551...',
        '...1155555511...',
        '..158887788851..',
        '.15588877888551.',
        '.15288877888251.',
        '.15588888888551.',
        '..11b777777b11..',
        '..188887788881..',
        '..188887788881..',
        '..188888888881..',
        '...1555115551...',
        '...1555115551...',
        '...1551..1551...',
        '...1111..1111...',
        '...1bb1..1bb1...',
        '..1bbb1..1bbb1..',
        '..11111..11111..',
      ],
      2,
      'spear'
    );

    /* 6. ENEMIES (GLOOMCAP, THORNLING, DUSK MOTH) & BOSS (BRIAR WARDEN) — Palette 3 */
    // Gloomcap Mushroom Fiend (20x20)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(20, 20);
      const ctx = c.getContext('2d')!;
      const pat = [
        '.....1111111111.....',
        '...11334433333311...',
        '..1333443333443331..',
        '.123333333334433321.',
        '.122333333333333221.',
        '..1111111111111111..',
        '....155555555551....',
        '....151a5555a151....',
        '....155555555551....',
        '.....1555555551.....',
        '.....111....111.....',
      ];
      paintPattern(ctx, pat, 3, 0, 6 + f);
      register('enemy_gloomcap', 0, f, c);
    }

    // Thornling Bramble Imp (20x22)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(20, 22);
      const ctx = c.getContext('2d')!;
      const pat = [
        '....11........11....',
        '...18711111111781...',
        '....177777777771....',
        '...16777777777761...',
        '...161b777777b161...',
        '....167711117761....',
        '...199aa9999aa991...',
        '..1667777777777661..',
        '...16677666677661...',
        '....1661....1661....',
        '....111......111....',
      ];
      paintPattern(ctx, pat, 3, 0, 6 + f);
      register('enemy_thornling', 0, f, c);
    }

    // Dusk Moth (22x18)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(22, 18);
      const ctx = c.getContext('2d')!;
      const pat =
        f === 0
          ? [
              '.11111..........11111.',
              '12334311..11..11343321',
              '1234433211221123344321',
              '1223332212b2b212233221',
              '.11222211222211222211.',
              '...1111..1221..1111...',
              '..........11..........',
            ]
          : [
              '....111........111....',
              '..113431..11..134311..',
              '.12344321122112344321.',
              '..12232212b2b21223221.',
              '...1122112222112211...',
              '.....11..1221..11.....',
              '..........11..........',
            ];
      paintPattern(ctx, pat, 3, 0, 4 + f);
      register('enemy_dusk_moth', 0, f, c);
    }

    // BOSS: BRIAR WARDEN (32x34 Mossy Stone Shrine Guardian!)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(32, 34);
      const ctx = c.getContext('2d')!;
      const bob = f;
      const pat = [
        '.....111................111.....',
        '....18761....111111....16781....',
        '.....16711111deeeed11111761.....',
        '......16dddddeeeeeddddd61.......',
        '.....1d78dddddddddddd87d1.......',
        '....1dd77dddddddddddd77dd1......',
        '....1ddd11bb1dddd1bb11ddd1......',
        '....1ddd1bccc1dd1cccb1ddd1......',
        '....1dddd1111dddd1111dddd1......',
        '...166dddddddd66dddddddd661.....',
        '..16786dddd66788766dddd68761....',
        '.1dd77dddd678ffff876dddd77dd1...',
        '.1dddddddd67fccccf76dddddddd1...',
        '.1ddd1dddd678ffff876dddd1ddd1...',
        '.1ddd1ddddd66788766ddddd1ddd1...',
        '..111.1ddddddd66ddddddd1.111....',
        '......1dddddddddddddddd1........',
        '......1ddddd111111ddddd1........',
        '......1dddd1......1dddd1........',
        '......16771........17761........',
        '......1111..........1111........',
      ];
      paintPattern(ctx, pat, 3, 0, 7 + bob);
      register('boss_briar_warden', 0, f, c);
    }

    /* 7. PROPS & EMOTE BUBBLES */
    // Windmill Rotating Blades (36x36 centered)
    const blades = makeCanvas(36, 36);
    const bctx = blades.getContext('2d')!;
    bctx.fillStyle = PALETTES[0][6];
    bctx.fillRect(17, 1, 2, 34);
    bctx.fillRect(1, 17, 34, 2);
    bctx.fillStyle = PALETTES[2][15];
    bctx.fillRect(19, 2, 5, 12);
    bctx.fillRect(12, 22, 5, 12);
    bctx.fillRect(2, 12, 12, 5);
    bctx.fillRect(22, 19, 12, 5);
    bctx.fillStyle = PALETTES[0][1];
    bctx.fillRect(16, 16, 4, 4);
    register('windmill_blades', 0, 0, blades);

    // Healing Waystone Crystal (16x22, 4 shimmer frames)
    for (let f = 0; f < 4; f++) {
      const c = makeCanvas(16, 22);
      const ctx = c.getContext('2d')!;
      const bob = f === 1 || f === 2 ? -1 : 0;
      paintPattern(
        ctx,
        [
          '.......11.......',
          '......1ff1......',
          '.....1fddf1.....',
          '....1fdccdf1....',
          '....1fdccdf1....',
          '.....1fddf1.....',
          '......1ff1......',
          '.......11.......',
        ],
        3,
        0,
        2 + bob
      );
      paintPattern(
        ctx,
        [
          '...1111111111...',
          '..1dddddddddd1..',
          '..1dccccccccd1..',
          '..1dddddddddd1..',
          '...1111111111...',
        ],
        3,
        0,
        14
      );
      register('waystone', 0, f, c);
    }

    // Treasure Chest (closed = frame 0, open = frame 1)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(16, 16);
      const ctx = c.getContext('2d')!;
      const pat =
        f === 0
          ? [
              '..111111111111..',
              '.1cccccccccccc1.',
              '.1cbbbbbbbbbbc1.',
              '.11111177111111.',
              '.1bbbb1771bbbb1.',
              '.1bbbbbbbbbbbb1.',
              '.1bbbbbbbbbbbb1.',
              '..111111111111..',
            ]
          : [
              '..111111111111..',
              '.1b1111111111b1.',
              '.11111111111111.',
              '.1bbbb1771bbbb1.',
              '.1bbbbbbbbbbbb1.',
              '.1bbbbbbbbbbbb1.',
              '..111111111111..',
            ];
      paintPattern(ctx, pat, 2, 0, 5);
      register('chest', 0, f, c);
      if (f === 1) {
        register('chest_open', 0, 0, c);
        register('chest_open', 0, 1, c);
      }
    }

    // Pixel Emote Bubbles (!, ?, heart, spark)
    const makeEmote = (id: SpriteId, sym: string[], palIdx: number) => {
      const c = makeCanvas(12, 12);
      const ctx = c.getContext('2d')!;
      ctx.fillStyle = '#1D1B22';
      ctx.fillRect(1, 0, 10, 9);
      ctx.fillRect(4, 9, 3, 2);
      ctx.fillStyle = '#F4E7C8';
      ctx.fillRect(2, 1, 8, 7);
      ctx.fillRect(5, 8, 2, 2);
      paintPattern(ctx, sym, palIdx, 3, 2);
      register(id, 0, 0, c);
      register(id, 0, 1, c);
    };
    makeEmote('emote_bang', ['..aa..', '..aa..', '..aa..', '......', '..aa..'], 3);
    makeEmote('emote_heart', ['.a..a.', 'aaaaaa', 'aaaaaa', '.aaaa.', '..aa..'], 3);
    makeEmote('emote_question', ['.ffff.', 'f...ff', '...ff.', '..ff..', '..ff..'], 3);
    makeEmote('emote_spark', ['..bb..', '.bccb.', 'bbbbbb', '.bccb.', '..bb..'], 3);

    // Heart Orb Projectile
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(12, 12);
      const ctx = c.getContext('2d')!;
      paintPattern(
        ctx,
        f === 0
          ? ['...bbbb...', '..bccccb..', '.bcbbbbcb.', '.bccccccb.', '..bccccb..', '...bbbb...']
          : ['..bbbbbb..', '.bccccccb.', 'bccccccccb', 'bccccccccb', '.bccccccb.', '..bbbbbb..'],
        3,
        1,
        3
      );
      register('heart_orb', 0, f, c);
    }

    /* 8. EXPANDED WORLD CAST: ANTHROPOMORPHIC MOUSE, PIG, SHADOW GRANDMA, MILO, TOWNSFOLK */
    // Mousefolk Villager / Shopkeeper (Whiskerburgh) — 18x22
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(18, 22);
      const ctx = c.getContext('2d')!;
      const pat = [
        '..1111......1111..',
        '.1443311111133441.',
        '.1433444444443341.',
        '..11444444444411..',
        '...144144441441...',
        '...14444aa44441...',
        '....1144444411....',
        '...18888ff88881...',
        '..18888ffff88881..',
        '..148888ff888841..',
        '...111111111111...',
        '....1b1....1b1....',
        '....111....111....',
      ];
      paintPattern(ctx, pat, 2, 0, 5 + f);
      register('npc_mouse', 0, f, c);
    }

    // Boarfolk / Pig Knight / Merchant (Boarhold) — 18x22
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(18, 22);
      const ctx = c.getContext('2d')!;
      const pat = [
        '..111........111..',
        '.1222111111112221.',
        '..12222222222221..',
        '..12212222221221..',
        '..12222333322221..',
        '..12223133132221..',
        '...112233332211...',
        '..155551ff155551..',
        '.155555ffff555551.',
        '..12555555555521..',
        '...111111111111...',
        '....111....111....',
      ];
      paintPattern(ctx, pat, 2, 0, 5 + f);
      register('npc_pig', 0, f, c);
    }

    // Shadow Grandma (Tall 16x24 Trauma Vision in Evertale Flashback — Palette 3)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(16, 24);
      const ctx = c.getContext('2d')!;
      const bob = f === 0 ? 0 : -1;
      const pat = [
        '....11111111....',
        '...1225555221...',
        '..122333333221..',
        '.12233222233221.',
        '.12322111122321.',
        '.12321441441231.',
        '.12321111111231.',
        '..123221122321..',
        '..122333333221..',
        '.12223344332221.',
        '.12223344332221.',
        '.12222333322221.',
        '.12222333322221.',
        '.12222233222221.',
        '.12222233222221.',
        '.12222233222221.',
        '.12222222222221.',
        '.12222222222221.',
        '.12222222222221.',
        '.12221222212221.',
        '..122122221221..',
        '..121.1221.121..',
        '...1...11...1...',
      ];
      paintPattern(ctx, pat, 3, 0, Math.max(0, 1 + bob));
      register('npc_shadow_gran', 0, f, c);
    }

    // Mido the Amber Mountain Fox-Kit (pointed dark-tipped ears, cream chest tuft, crimson scarf, brass bell, bushy fox tail) — 16x16
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(16, 16);
      const ctx = c.getContext('2d')!;
      const bob = f === 0 ? 0 : -1;
      const pat = [
        '.11....11.......',
        '141111141.......',
        '199999991.......',
        '199999991...111.',
        '191999191..199b1',
        '.192229111199bb1',
        '..188a889999bb1.',
        '..19bb99999911..',
        '..1999999991....',
        '..111...111.....',
      ];
      paintPattern(ctx, pat, 1, 0, 5 + bob);
      register('pet_milo', 0, f, c);
    }

    // Alderbrook / Sunhill Villager — Tall 16x24
    registerTallNpc('npc_townsfolk', [
      '....11111111....',
      '...1bbccccbb1...',
      '..1bbccccccbb1..',
      '..1b22222222b1..',
      '..1b22222222b1..',
      '..1b21222212b1..',
      '...1232222321...',
      '...11ffffff11...',
      '..18889ff98881..',
      '.188999ff999881.',
      '.18899999999881.',
      '.18288888888281.',
      '.18888888888881.',
      '..11bbbbbbbb11..',
      '..188888888881..',
      '..188888888881..',
      '...1bb1111bb1...',
      '...1bb1..1bb1...',
      '...1bb1..1bb1...',
      '...1bb1..1bb1...',
      '...1111..1111...',
      '...1bb1..1bb1...',
      '..1bbb1..1bbb1..',
      '..11111..11111..',
    ]);

    /* 9. NEW ENEMIES & BOSSES (RAT, SLIME, BOAR, KING MUCKMAW, LORD IRONFANG) */
    // Gutter Rat (Sewers) — 20x18
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(20, 18);
      const ctx = c.getContext('2d')!;
      const pat = [
        '...111....111.......',
        '..1dd11111dd1.......',
        '.1dddeeeedddd1......',
        '.1de1aeeee1add1.....',
        '1aaeeeedddddddd11...',
        '.11dddddddddddddd11.',
        '...1dddddddddddd1..1',
        '...1111....1111...1.',
      ];
      paintPattern(ctx, pat, 3, 0, 7 + f);
      register('enemy_rat', 0, f, c);
    }

    // Mire Slime (Sewers) — 20x18
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(20, 18);
      const ctx = c.getContext('2d')!;
      const pat =
        f === 0
          ? [
              '......11111111......',
              '....117788887711....',
              '...17788ffff88771...',
              '..1778888888888771..',
              '..16781b8888b18761..',
              '.166778888888877661.',
              '.166677777777776661.',
              '..1111111111111111..',
            ]
          : [
              '....................',
              '.....1111111111.....',
              '...117788ff887711...',
              '..1778888888888771..',
              '.16781b888888b18761.',
              '.166778888888877661.',
              '16666777777777766661',
              '.111111111111111111.',
            ];
      paintPattern(ctx, pat, 3, 0, 7);
      register('enemy_slime', 0, f, c);
    }

    // Boar Halberdier (Boarhold) — 22x24
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(22, 24);
      const ctx = c.getContext('2d')!;
      const pat = [
        '....11........11...11.',
        '...19911111111991.1ee1',
        '....199aaaaaa991..1ed1',
        '...19a1b9999b1a91..11.',
        '...19ac199991ca91..11.',
        '....111cccccc111...11.',
        '..11dddd1991dddd11.11.',
        '.1dddeed9aa9deeddd111.',
        '..1ddddd9999ddddd1.11.',
        '...11dddddddddd11..11.',
        '....19991..19991...11.',
        '....1111....1111...11.',
      ];
      paintPattern(ctx, pat, 3, 0, 7 + f);
      register('enemy_boar', 0, f, c);
    }

    // BOSS: KING MUCKMAW (32x34 Crowned Sewer Toad-King)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(32, 34);
      const ctx = c.getContext('2d')!;
      const pat = [
        '..........11..11..11............',
        '.........1bb11bb11bb1...........',
        '.........1bbbbbbbbbb1...........',
        '.......1111111111111111.........',
        '.....11667788888888776611.......',
        '....1667788ff8888ff8877661......',
        '...166781bb18888881bb187661.....',
        '...166781111888888111187661.....',
        '..16677888888888888888877661....',
        '..16671111111111111111117661....',
        '..16671cc11aa1111aa11cc17661....',
        '..16671111111111111111117661....',
        '.16677778888ffffff88887777661...',
        '.1666777888ffffffff8887776661...',
        '.16666778888ffffff88887766661...',
        '..16666777788888888777766661....',
        '...116666667777777766666611.....',
        '....1666111111111111116661......',
        '....1111..............1111......',
      ];
      paintPattern(ctx, pat, 3, 0, 9 + f);
      register('boss_muckmaw', 0, f, c);
    }

    // BOSS: LORD IRONFANG (32x34 Armored Boar Warlord)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(32, 34);
      const ctx = c.getContext('2d')!;
      const pat = [
        '......111............111........',
        '.....1bb11111111111111bb1.......',
        '......11dddddeeeeddddd11........',
        '.....1ddd999999999999ddd1.......',
        '....1ddd991bb9999bb199ddd1......',
        '....1ddd99999aaaa99999ddd1......',
        '...1cc1d99911aaaa11999d1cc1.....',
        '...1ccc1111cccccccc1111ccc1.....',
        '..11dddddddd1bbbb1dddddddd11....',
        '.1dddeeedddd9aaaa9ddddeeeddd1...',
        '.1dddddd99999aaaa99999dddddd1...',
        '.1dd1ddd9999bbbbbb9999ddd1dd1...',
        '.1dd1ddd99999aaaa99999ddd1dd1...',
        '..11.1dddddd999999dddddd1.11....',
        '.....1dddddddddddddddddd1.......',
        '.....1ddddd11111111ddddd1.......',
        '.....1dddd1........1dddd1.......',
        '.....199991........199991.......',
        '.....11111..........11111.......',
      ];
      paintPattern(ctx, pat, 3, 0, 9 + f);
      register('boss_ironfang', 0, f, c);
    }

    /* 10. HOLLOW OAK PORTAL & FISHING BOBBER */
    // Ancient Hollow Oak Portal (28x32)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(28, 32);
      const ctx = c.getContext('2d')!;
      const glow = f === 0 ? 'f' : 'b';
      const pat = [
        '......1111111111111111......',
        '....11667788888888776611....',
        '...1667788888888888877661...',
        '..166777777777777777777661..',
        '..166667777777777777766661..',
        '...1166666666666666666611...',
        '.....111111111111111111.....',
        '......1999999999999991......',
        '.....199991111111199991.....',
        '.....19991' + glow + glow + 'cccc' + glow + glow + '19991.....',
        '.....19991' + glow + 'cccccc' + glow + '19991.....',
        '.....19991' + glow + 'c' + glow + glow + glow + glow + 'c' + glow + '19991.....',
        '.....19991' + glow + 'cccccc' + glow + '19991.....',
        '....199991' + glow + glow + 'cccc' + glow + glow + '199991....',
        '...199999111111111111999991.',
        '...1111111..........1111111.',
      ];
      paintPattern(ctx, pat, 3, 0, 12);
      register('hollow_oak', 0, f, c);
    }

    // Fishing Bobber (10x10)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(10, 10);
      const ctx = c.getContext('2d')!;
      const bob = f;
      const pat = [
        '...1111...',
        '..188991..',
        '..188991..',
        '..1eeee1..',
        '...1111...',
      ];
      paintPattern(ctx, pat, 1, 0, 2 + bob);
      register('bobber', 0, f, c);
    }

    /* 11. CHAPTER 1 BESPOKE TALL 16x24 ALDERBROOK CAST & MOUNTAIN STEAM LOCOMOTIVE */
    // Grandma Mara — Tall 16x24 silver braided bun, rose-burgundy wool shawl, long cream hearth apron
    registerTallNpc('npc_mara', [
      '.....111111.....',
      '....144ff441....',
      '...1444444441...',
      '..145444444541..',
      '..144222222441..',
      '..142222222241..',
      '..142122221241..',
      '...1232222321...',
      '...1122332211...',
      '..1aaaaffaaaa1..',
      '.1aaaddffddaaa1.',
      '.1aaaffffffaaa1.',
      '.1a2affffffa2a1.',
      '.1aaaffffffaaa1.',
      '..1ddffffffdd1..',
      '..1ddffffffdd1..',
      '..1d1ffffff1d1..',
      '..1d1ffffff1d1..',
      '..1dddddddddd1..',
      '..1dddddddddd1..',
      '...1111111111...',
      '....1b1..1b1....',
      '...1bb1..1bb1...',
      '...1111..1111...',
    ]);

    // Stationmaster Barnaby — Tall 16x24 navy peaked rail cap, brass badge, walrus mustache, slate coat
    registerTallNpc('npc_barnaby', [
      '...1111111111...',
      '..188897798881..',
      '.11111111111111.',
      '..188222222881..',
      '..122222222221..',
      '..122122221221..',
      '..124444444421..',
      '...1444224441...',
      '...11ffffff11...',
      '..188897798881..',
      '.18888877888881.',
      '.18888877888881.',
      '.18288877888281.',
      '.18888877888881.',
      '..111777777111..',
      '..188888888881..',
      '..188888888881..',
      '...1888118881...',
      '...1888118881...',
      '...1881..1881...',
      '...1111..1111...',
      '...1bb1..1bb1...',
      '..1bbb1..1bbb1..',
      '..11111..11111..',
    ]);

    // Baker Maple — Tall 16x24 cream baker's kerchief, auburn braids, flour-dusted long apron
    registerTallNpc('npc_maple', [
      '....11111111....',
      '...1ffffffff1...',
      '..1ff444444ff1..',
      '..1cc222222cc1..',
      '..1c22222222c1..',
      '..1c21222212c1..',
      '..1c23222232c1..',
      '...1c222222c1...',
      '...11ffffff11...',
      '..1aaaffffaaa1..',
      '.1aaaaffffaaaa1.',
      '.1a2aaffffaa2a1.',
      '.1aaaaffffaaaa1.',
      '..1aaaffffaaa1..',
      '..1aaaffffaaa1..',
      '..1aaaffffaaa1..',
      '..1aaaffffaaa1..',
      '..1aaaaaaaaaa1..',
      '..1aaaaaaaaaa1..',
      '..1dddddddddd1..',
      '...1111111111...',
      '....1b1..1b1....',
      '...1bb1..1bb1...',
      '...1111..1111...',
    ]);

    // Old Woodsman Jasper — Tall 16x24 knit cap, silver beard, weathered timber coat & walking staff
    registerTallNpc(
      'npc_jasper',
      [
        '....11111111....',
        '...1bbccccbb1...',
        '..1bbccccccbb1..',
        '..1bb222222bb1..',
        '..1b21222212b1..',
        '..1b44444444b1..',
        '..1b444ff444b1..',
        '...1444444441...',
        '...11aaaaaaa1...',
        '..1bbbaaaabbb1..',
        '.1bbbccccccbbb1.',
        '.1bbcbbbbbbcbb1.',
        '.1b2bbbbbbbb2b1.',
        '.1bbbbbbbbbbbb1.',
        '..11b777777b11..',
        '..1bbbbbbbbbb1..',
        '..1bbbbbbbbbb1..',
        '...1888118881...',
        '...1888118881...',
        '...1881..1881...',
        '...1111..1111...',
        '...1bb1..1bb1...',
        '..1bbb1..1bbb1..',
        '..11111..11111..',
      ],
      2,
      'staff'
    );

    // Alderbrook Vintage Mountain Steam Locomotive (48x28, 2 animated steam puff frames!)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(48, 28);
      const ctx = c.getContext('2d')!;
      // Puffing white alpine steam cloud above funnel
      ctx.fillStyle = '#F2EFE6';
      ctx.fillRect(9 - f * 2, 1, 6, 4);
      ctx.fillRect(13 - f * 2, 0, 7, 3);
      ctx.fillStyle = '#D4D8E2';
      ctx.fillRect(8 - f * 2, 4, 5, 2);

      // Funnel & brass dome
      ctx.fillStyle = '#181A22';
      ctx.fillRect(9, 5, 5, 7);
      ctx.fillRect(21, 7, 6, 5);
      ctx.fillStyle = '#D4A042';
      ctx.fillRect(10, 6, 3, 2);
      ctx.fillRect(22, 8, 4, 3);

      // Emerald iron boiler body
      ctx.fillStyle = '#181A22';
      ctx.fillRect(6, 11, 28, 10);
      ctx.fillStyle = '#265645';
      ctx.fillRect(7, 12, 26, 8);
      ctx.fillStyle = '#3A7A62';
      ctx.fillRect(8, 13, 24, 2);
      // Brass boiler bands & headlamp
      ctx.fillStyle = '#E8BE5E';
      ctx.fillRect(14, 12, 2, 8);
      ctx.fillRect(25, 12, 2, 8);
      ctx.fillStyle = f === 0 ? '#FFF3B0' : '#F5C45A';
      ctx.fillRect(5, 13, 2, 3);

      // Engineer cab with glowing warm window
      ctx.fillStyle = '#181A22';
      ctx.fillRect(32, 6, 14, 15);
      ctx.fillStyle = '#6E2A2A';
      ctx.fillRect(31, 5, 16, 3);
      ctx.fillStyle = '#2B4C42';
      ctx.fillRect(33, 8, 12, 12);
      ctx.fillStyle = '#F5C45A';
      ctx.fillRect(35, 10, 8, 5);
      ctx.fillStyle = '#FFF6CC';
      ctx.fillRect(36, 11, 3, 2);

      // Cowcatcher & undercarriage
      ctx.fillStyle = '#181A22';
      ctx.fillRect(2, 18, 5, 4);
      ctx.fillRect(6, 20, 40, 3);
      ctx.fillStyle = '#8C3B34';
      ctx.fillRect(3, 19, 3, 2);

      // Crimson spoked iron drive wheels
      const wheelXs = [10, 19, 28, 38];
      for (const wx of wheelXs) {
        ctx.fillStyle = '#181A22';
        ctx.fillRect(wx, 20, 6, 6);
        ctx.fillStyle = '#A83A36';
        ctx.fillRect(wx + 1, 21, 4, 4);
        ctx.fillStyle = '#E8BE5E';
        ctx.fillRect(wx + 2 + (f & 1), 22, 1, 2);
      }
      // Side connecting rod
      ctx.fillStyle = '#CFD6E0';
      ctx.fillRect(12, 22 + f, 20, 1);

      register('locomotive', 0, f, c);
    }

    /* 12. MILESTONE 3 TALL 16x24 SUNHILL MARKET & CASTLE AURELIA CAST */
    registerTallNpc('npc_tessa', [
      '.....111111.....',
      '....1bbccbb1....',
      '...1ffffffff1...',
      '..1bb222222bb1..',
      '..1b22222222b1..',
      '..1b21222212b1..',
      '..1b23222232b1..',
      '...1222222221...',
      '...11ffffff11...',
      '..1777ffff7771..',
      '.17777ffff77771.',
      '.17277ffff77271.',
      '.17777ffff77771.',
      '..1666ffff6661..',
      '..1666ffff6661..',
      '..1666ffff6661..',
      '..1666ffff6661..',
      '..166666666661..',
      '..166666666661..',
      '..1aaaaaaaaaa1..',
      '...1111111111...',
      '....1b1..1b1....',
      '...1bb1..1bb1...',
      '...1111..1111...',
    ]);

    registerTallNpc(
      'npc_corin',
      [
        '....11111111....',
        '...1445555441...',
        '..145444444541..',
        '..144222222441..',
        '..142f1221f241..',
        '..142222222241..',
        '...1222332221...',
        '...1122222211...',
        '..188877778881..',
        '.18899777799881.',
        '.18888777788881.',
        '.18288877888281.',
        '.18888877888881.',
        '..188887788881..',
        '..188887788881..',
        '..188887788881..',
        '..188887788881..',
        '..188887788881..',
        '..188888888881..',
        '..177777777771..',
        '...1111111111...',
        '....1b1..1b1....',
        '...1bb1..1bb1...',
        '...1111..1111...',
      ],
      2,
      'scroll'
    );

    registerTallNpc(
      'npc_sentinel',
      [
        '.....117711.....',
        '....17777771....',
        '...1444ff4441...',
        '..144555555441..',
        '..144111111441..',
        '..144212212441..',
        '..144555555441..',
        '...1444444441...',
        '..148887788841..',
        '.14488877888441.',
        '.14488877888441.',
        '.14498877889441.',
        '.14488877888441.',
        '..114777777411..',
        '..148887788841..',
        '..148887788841..',
        '..144888888441..',
        '...1444114441...',
        '...1444114441...',
        '...1551..1551...',
        '...1111..1111...',
        '...1441..1441...',
        '..14441..14441..',
        '..11111..11111..',
      ],
      2,
      'spear'
    );

    /* 13. MILESTONE 4: CEDAR RAFT, DESERT ENEMIES, BIOME FAUNA, SHIPWRIGHT ORIN & BABANA STALL */
    // Cedar Raft (24x20 hand-lashed logs + fluttering sunlit canvas pennant)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(24, 20);
      const ctx = c.getContext('2d')!;
      const bob = f;
      ctx.fillStyle = '#182430';
      ctx.fillRect(2, 7 + bob, 20, 11);
      // Lashed cedar logs
      for (let lx = 3; lx <= 18; lx += 4) {
        ctx.fillStyle = '#9A6334';
        ctx.fillRect(lx, 8 + bob, 3, 9);
        ctx.fillStyle = '#C98A4E';
        ctx.fillRect(lx, 8 + bob, 2, 8);
      }
      // Cross rope ties
      ctx.fillStyle = '#E8D4A2';
      ctx.fillRect(3, 10 + bob, 18, 2);
      ctx.fillRect(3, 14 + bob, 18, 2);
      // Short mast & fluttering pennant
      ctx.fillStyle = '#52341A';
      ctx.fillRect(11, 2 + bob, 2, 10);
      ctx.fillStyle = '#F5C45A';
      ctx.fillRect(13, 2 + bob, 6 + f, 4);
      ctx.fillStyle = '#FFF4D0';
      ctx.fillRect(14, 3 + bob, 3, 2);
      register('raft', 0, f, c);
    }

    // Dune Scorpion (22x18 armored crimson desert pincer foe — 2-frame snapping claws & legs)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(22, 18);
      const ctx = c.getContext('2d')!;
      const pat = f === 0 ? [
        '...........1111.......',
        '.........11cb9a11.....',
        '........1cb1119aa1....',
        '.........11...19aa1...',
        '..111..........19a91..',
        '.1aa91.........19aa1..',
        '1a911..111......19a91.',
        '1a1...1aa91.1111199a1.',
        '.1a91.1a91119baab99a1.',
        '..1991199199aa9aa9991.',
        '...119a9999c1a9aa9a91.',
        '....111999b1a99aa991..',
        '.......119999999991...',
        '......1911911911911...',
        '.....191.191.191.191..',
        '....11..11...11...11..',
      ] : [
        '..........1111........',
        '........11cb9a11......',
        '.......1cb1119aa1.....',
        '........11...19aa1....',
        '..............19a91...',
        '..111..........19aa1..',
        '.1aa91.111.....19a91..',
        '1aa91.1aa91.111199a1..',
        '1a99111a91119baab99a1.',
        '.11999999199aa9aa9991.',
        '...119a9999c1a9aa9a91.',
        '....111999b1a99aa991..',
        '.......119999999991...',
        '.......191191191191...',
        '.......191.191.191.191',
        '......11...11..11...11',
      ];
      paintPattern(ctx, pat, 3, 0, 2);
      register('enemy_scorpion', 0, f, c);
    }

    // Giant Sandworm (32x34 coiled armored desert leviathan boss!)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(32, 34);
      const ctx = c.getContext('2d')!;
      const pat = f === 0 ? [
        '........111111111111............',
        '......1199bbbbbbbb9911..........',
        '.....19bbccbbbbbbccbb91.........',
        '....19bb111111111111bb91........',
        '...19bb115c115c115c11bb91.......',
        '...19b11c11222222221c11b91......',
        '...19b11512211111122151b91......',
        '...19b11c122119911221c1b91......',
        '...19b11512211111122151b91......',
        '...19bb115c115c115c11bb91.......',
        '....19bb111111111111bb91........',
        '.....199bbbbccccbbbb991.........',
        '......1199999999999911..........',
        '.....199bbbccccccbbb991.........',
        '....19bbbbbbbbbbbbbbbb91........',
        '....199999999999999999911.......',
        '...199bbbccccccccccbbb99911.....',
        '...19bbbbbbbbbbbbbbbbbbbb991....',
        '...1999999999999999999999991....',
        '..199bbbccccccccccbbbbbbbb991...',
        '..19bbbbbbbbbbbbbbbbbbbbbbb91...',
        '.15599999999999999999999999551..',
        '1555555555555555555555555555551.',
        '.11111111111111111111111111111..',
      ] : [
        '.........111111111111...........',
        '.......1199bbbbbbbb9911.........',
        '......19bbccbbbbbbccbb91........',
        '.....19bb111111111111bb91.......',
        '....19bb11c511c511c511bb91......',
        '....19b11511222222221511b91.....',
        '....19b11c122111111221c1b91.....',
        '....19b11512211aa1122151b91.....',
        '....19b11c122111111221c1b91.....',
        '....19bb11c511c511c511bb91......',
        '.....19bb111111111111bb91.......',
        '......199bbbbccccbbbb991........',
        '......1199999999999911..........',
        '.....199bbbccccccbbb991.........',
        '....19bbbbbbbbbbbbbbbb91........',
        '....199999999999999999911.......',
        '...199bbbccccccccccbbb99911.....',
        '...19bbbbbbbbbbbbbbbbbbbb991....',
        '...1999999999999999999999991....',
        '..199bbbccccccccccbbbbbbbb991...',
        '..19bbbbbbbbbbbbbbbbbbbbbbb91...',
        '.15599999999999999999999999551..',
        '1555555555555555555555555555551.',
        '.11111111111111111111111111111..',
      ];
      paintPattern(ctx, pat, 3, 0, 8 + f);
      register('enemy_sandworm', 0, f, c);
    }

    // Desert Camel Fauna (20x18)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(20, 18);
      const ctx = c.getContext('2d')!;
      ctx.fillStyle = '#1D1B22';
      ctx.fillRect(2, 4, 5, 5);
      ctx.fillRect(5, 6, 3, 5);
      ctx.fillRect(7, 4, 9, 8);
      ctx.fillRect(8, 12, 2, 5 - f);
      ctx.fillRect(14, 12, 2, 4 + f);
      ctx.fillStyle = '#D8A25A';
      ctx.fillRect(3, 5, 3, 3);
      ctx.fillRect(6, 7, 2, 4);
      ctx.fillRect(8, 5, 7, 6);
      ctx.fillStyle = '#F0C478';
      ctx.fillRect(9, 5, 5, 3);
      ctx.fillStyle = '#D6483C';
      ctx.fillRect(10, 7, 3, 2);
      register('fauna_camel', 0, f, c);
    }

    // Frostmere Penguin Fauna (16x18)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(16, 18);
      const ctx = c.getContext('2d')!;
      ctx.fillStyle = '#181E2E';
      ctx.fillRect(4, 3 + f, 8, 12);
      ctx.fillRect(3, 7 + f, 10, 6);
      ctx.fillStyle = '#F4F8FF';
      ctx.fillRect(6, 6 + f, 4, 8);
      ctx.fillStyle = '#E8523A';
      ctx.fillRect(5, 4 + f, 6, 2); // Festive scarf
      ctx.fillStyle = '#F5A623';
      ctx.fillRect(7, 5 + f, 2, 1); // Beak
      ctx.fillRect(5, 15, 2, 2);
      ctx.fillRect(9, 15, 2, 2);
      register('fauna_penguin', 0, f, c);
    }

    // Canopy Wilds Monkey Fauna (16x18)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(16, 18);
      const ctx = c.getContext('2d')!;
      ctx.fillStyle = '#1D1816';
      ctx.fillRect(4, 3 + f, 8, 7);
      ctx.fillRect(5, 10 + f, 6, 5);
      ctx.fillRect(11, 8 + f, 3, 2);
      ctx.fillRect(13, 6 + f, 2, 3);
      ctx.fillStyle = '#7A4C2A';
      ctx.fillRect(5, 4 + f, 6, 5);
      ctx.fillRect(6, 10 + f, 4, 4);
      ctx.fillStyle = '#E8C294';
      ctx.fillRect(6, 5 + f, 4, 3);
      ctx.fillStyle = '#181818';
      ctx.fillRect(6, 6 + f, 1, 1);
      ctx.fillRect(9, 6 + f, 1, 1);
      register('fauna_monkey', 0, f, c);
    }

    // Aetheris Citadel Hover Robot Fauna (18x20)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(18, 20);
      const ctx = c.getContext('2d')!;
      const bob = f === 0 ? 0 : -1;
      ctx.fillStyle = '#161C2A';
      ctx.fillRect(4, 3 + bob, 10, 12);
      ctx.fillRect(8, 1 + bob, 2, 2);
      ctx.fillStyle = '#9CB0C8';
      ctx.fillRect(5, 4 + bob, 8, 10);
      ctx.fillStyle = '#DCE8F8';
      ctx.fillRect(6, 4 + bob, 3, 9);
      ctx.fillStyle = '#182438';
      ctx.fillRect(6, 6 + bob, 6, 3);
      ctx.fillStyle = f === 0 ? '#5AD2F4' : '#B8F6FF';
      ctx.fillRect(7, 7 + bob, 4, 1);
      ctx.fillRect(7, 16, 4, 2); // Hover thruster glow
      register('fauna_robot', 0, f, c);
    }

    // Shipwright Orin (Heartland Timber Pier — Tall 16x24 weathered cedar vest, navy cap, mallet)
    registerTallNpc(
      'npc_shipwright',
      [
        '....11111111....',
        '...1888998881...',
        '..188888888881..',
        '..1bb222222bb1..',
        '..1b21222212b1..',
        '..1b22bbbb22b1..',
        '...1bbbbbbbb1...',
        '...11cccccc11...',
        '..1ccbbbbbbcc1..',
        '.1cccbbbbbbccc1.',
        '.1cc2bbbbbb2cc1.',
        '.1cccbbbbbbccc1.',
        '.1cc2bbbbbb2cc1.',
        '..111777777111..',
        '..188888888881..',
        '..188888888881..',
        '..188888888881..',
        '...1888118881...',
        '...1888118881...',
        '...1881..1881...',
        '...1111..1111...',
        '...1bb1..1bb1...',
        '..1bbb1..1bbb1..',
        '..11111..11111..',
      ],
      2,
      'mallet'
    );

    // Golden Babana Market Stall (24x22 striped oasis awning & radiant Babana pyramid!)
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(24, 22);
      const ctx = c.getContext('2d')!;
      ctx.fillStyle = '#1C1820';
      ctx.fillRect(1, 2, 22, 19);
      // Striped crimson & cream sun awning
      ctx.fillStyle = '#D6483C';
      ctx.fillRect(2, 3, 20, 6);
      ctx.fillStyle = '#FFF4DC';
      ctx.fillRect(5, 3, 4, 6);
      ctx.fillRect(13, 3, 4, 6);
      // Timber counter & golden Babana bunches
      ctx.fillStyle = '#6E462C';
      ctx.fillRect(2, 13, 20, 7);
      ctx.fillStyle = '#9C6838';
      ctx.fillRect(3, 14, 18, 2);
      ctx.fillStyle = f === 0 ? '#F5D042' : '#FFE878';
      ctx.fillRect(5, 10, 4, 3);
      ctx.fillRect(10, 9, 5, 4);
      ctx.fillRect(16, 10, 4, 3);
      register('babana_stall', 0, f, c);
    }
  }
}
