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
  O_ROOF_EAVE,
  O_SHRINE_ARCH,
  SpriteId,
  T_BRIDGE,
  T_BRIDGE_RAIL_N,
  T_BRIDGE_RAIL_S,
  T_BUSH,
  T_COBBLE,
  T_DIRT,
  T_FENCE,
  T_FLOWER_BED,
  T_FLOWERS,
  T_FORD,
  T_GRASS,
  T_GRASS_ALT,
  T_LANTERN_POST,
  T_ROOF_BODY,
  T_ROOF_TOP,
  T_SHRINE_FLOOR,
  T_SHRINE_PILLAR,
  T_SHRINE_RUNE,
  T_SIGNPOST,
  T_TALL_GRASS,
  T_TREE_TRUNK,
  T_WALL_DOOR,
  T_WALL_WINDOW,
  T_WALL_WOOD,
  T_WATER,
  T_WINDMILL_DOOR,
  T_WINDMILL_ROOF,
  T_WINDMILL_WALL,
  TILE_SIZE,
} from '../core/types';

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
  private readonly palettes: readonly (readonly string[])[];
  private bg1Tiles = new Map<number, HTMLCanvasElement[]>(); // tileId -> animation frames
  private bg2Tiles = new Map<number, HTMLCanvasElement>();
  private sprites = new Map<string, HTMLCanvasElement>(); // key: `${spriteId}_${dir}_${frame}`
  private overrides = new Map<SwappableSlotId, HTMLImageElement>();

  constructor(palettes: readonly (readonly string[])[]) {
    this.palettes = palettes;
    this.buildAllTiles();
    this.buildAllSprites();
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
   * 16x16 TERRAIN & ARCHITECTURE TILES (PALETTE 0)
   * ======================================================================== */
  private buildAllTiles(): void {
    const PALETTES = this.palettes;
    const p0 = PALETTES[0];
    const makeTile = (fn: (ctx: CanvasRenderingContext2D) => void): HTMLCanvasElement => {
      const c = makeCanvas(TILE_SIZE, TILE_SIZE);
      const ctx = c.getContext('2d')!;
      fn(ctx);
      return c;
    };

    // Grass base
    const grass0 = makeTile((ctx) => {
      ctx.fillStyle = p0[3];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[4];
      ctx.fillRect(2, 3, 2, 1);
      ctx.fillRect(10, 8, 2, 1);
      ctx.fillRect(5, 13, 1, 2);
      ctx.fillStyle = p0[2];
      ctx.fillRect(3, 4, 1, 1);
      ctx.fillRect(11, 9, 1, 1);
      ctx.fillRect(13, 2, 1, 2);
    });
    this.bg1Tiles.set(T_GRASS, [grass0]);

    // Grass Alt (sunlit tufts)
    const grassAlt = makeTile((ctx) => {
      ctx.drawImage(grass0, 0, 0);
      ctx.fillStyle = p0[4];
      ctx.fillRect(4, 6, 1, 2);
      ctx.fillRect(6, 5, 1, 3);
      ctx.fillRect(8, 6, 1, 2);
      ctx.fillStyle = p0[5];
      ctx.fillRect(6, 4, 1, 1);
      ctx.fillStyle = p0[2];
      ctx.fillRect(4, 8, 5, 1);
    });
    this.bg1Tiles.set(T_GRASS_ALT, [grassAlt]);

    // Wildflowers (2-frame gentle breeze!)
    const flowerFrames = [0, 1].map((f) =>
      makeTile((ctx) => {
        ctx.drawImage(grass0, 0, 0);
        const shift = f;
        // Little red & cream daisies
        ctx.fillStyle = p0[2];
        ctx.fillRect(3, 6, 2, 2);
        ctx.fillRect(10, 11, 2, 2);
        ctx.fillStyle = p0[15];
        ctx.fillRect(3 + shift, 4, 2, 2);
        ctx.fillStyle = p0[8];
        ctx.fillRect(4 + shift, 4, 1, 1);
        ctx.fillStyle = p0[11];
        ctx.fillRect(10 - shift, 9, 2, 2);
        ctx.fillStyle = PALETTES[1][10];
        ctx.fillRect(11 - shift, 9, 1, 1);
      })
    );
    this.bg1Tiles.set(T_FLOWERS, flowerFrames);

    // Tall Rustling Grass (2-frame wind sway!)
    const tallFrames = [0, 1].map((f) =>
      makeTile((ctx) => {
        ctx.fillStyle = p0[2];
        ctx.fillRect(0, 0, 16, 16);
        for (let bx = 1; bx < 15; bx += 3) {
          for (let by = 4; by <= 13; by += 5) {
            const lean = ((bx + by) & 1) === 0 ? f : -f;
            ctx.fillStyle = p0[3];
            ctx.fillRect(bx, by, 2, 4);
            ctx.fillStyle = p0[4];
            ctx.fillRect(bx + lean, by - 2, 1, 3);
            ctx.fillStyle = p0[5];
            ctx.fillRect(bx + lean, by - 3, 1, 1);
          }
        }
      })
    );
    this.bg1Tiles.set(T_TALL_GRASS, tallFrames);

    // Warm Ochre Dirt Road
    const dirt = makeTile((ctx) => {
      ctx.fillStyle = p0[7];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[8];
      ctx.fillRect(2, 2, 3, 1);
      ctx.fillRect(10, 7, 3, 1);
      ctx.fillRect(4, 12, 2, 1);
      ctx.fillStyle = p0[6];
      ctx.fillRect(6, 5, 2, 1);
      ctx.fillRect(12, 13, 2, 1);
      ctx.fillRect(1, 9, 1, 1);
    });
    this.bg1Tiles.set(T_DIRT, [dirt]);

    // Cobblestone Plaza
    const cobble = makeTile((ctx) => {
      ctx.fillStyle = p0[9];
      ctx.fillRect(0, 0, 16, 16);
      const stones = [
        [1, 1, 6, 5],
        [8, 1, 7, 5],
        [0, 7, 4, 4],
        [5, 7, 6, 4],
        [12, 7, 4, 4],
        [1, 12, 7, 3],
        [9, 12, 6, 3],
      ];
      for (const [x, y, w, h] of stones) {
        ctx.fillStyle = p0[10];
        ctx.fillRect(x, y, w, h);
        ctx.fillStyle = p0[11];
        ctx.fillRect(x, y, w - 1, 1);
      }
    });
    this.bg1Tiles.set(T_COBBLE, [cobble]);

    // Animated Creek Water (4 frames scrolling shimmer!)
    const waterFrames = [0, 1, 2, 3].map((f) =>
      makeTile((ctx) => {
        ctx.fillStyle = p0[12];
        ctx.fillRect(0, 0, 16, 16);
        ctx.fillStyle = p0[13];
        ctx.fillRect(0, (f * 4) % 16, 16, 4);
        ctx.fillRect(0, (f * 4 + 8) % 16, 16, 3);
        ctx.fillStyle = p0[14];
        ctx.fillRect((2 + f * 2) % 13, (3 + f * 4) % 15, 4, 1);
        ctx.fillRect((9 - f + 16) % 13, (10 + f * 4) % 15, 3, 1);
      })
    );
    this.bg1Tiles.set(T_WATER, waterFrames);

    // Shallow Pebble Ford (Walkable water with stepping stones!)
    const fordFrames = [0, 1].map((f) =>
      makeTile((ctx) => {
        ctx.fillStyle = p0[13];
        ctx.fillRect(0, 0, 16, 16);
        ctx.fillStyle = p0[14];
        ctx.fillRect(1 + f * 2, 2, 4, 1);
        ctx.fillRect(9 - f, 12, 4, 1);
        // Submerged pebbles
        ctx.fillStyle = p0[10];
        ctx.fillRect(3, 5, 4, 3);
        ctx.fillRect(9, 8, 5, 3);
        ctx.fillStyle = p0[11];
        ctx.fillRect(4, 5, 2, 1);
        ctx.fillRect(10, 8, 3, 1);
      })
    );
    this.bg1Tiles.set(T_FORD, fordFrames);

    // Timber Bridge Planks
    const bridge = makeTile((ctx) => {
      ctx.fillStyle = p0[6];
      ctx.fillRect(0, 0, 16, 16);
      for (let x = 0; x < 16; x += 4) {
        ctx.fillStyle = p0[7];
        ctx.fillRect(x, 1, 3, 14);
        ctx.fillStyle = p0[8];
        ctx.fillRect(x, 1, 1, 14);
        ctx.fillStyle = p0[1];
        ctx.fillRect(x + 1, 2, 1, 1);
        ctx.fillRect(x + 1, 13, 1, 1);
      }
    });
    this.bg1Tiles.set(T_BRIDGE, [bridge]);

    // Bridge Rail North & South
    const makeRail = (isNorth: boolean) =>
      makeTile((ctx) => {
        ctx.drawImage(waterFrames[0], 0, 0);
        const y0 = isNorth ? 8 : 0;
        ctx.fillStyle = p0[1];
        ctx.fillRect(0, y0, 16, 8);
        ctx.fillStyle = p0[6];
        ctx.fillRect(0, y0 + 1, 16, 6);
        ctx.fillStyle = p0[8];
        ctx.fillRect(0, y0 + 2, 16, 2);
        // Posts
        ctx.fillStyle = p0[7];
        ctx.fillRect(2, y0, 3, 8);
        ctx.fillRect(11, y0, 3, 8);
      });
    this.bg1Tiles.set(T_BRIDGE_RAIL_N, [makeRail(true)]);
    this.bg1Tiles.set(T_BRIDGE_RAIL_S, [makeRail(false)]);

    // Wooden Fence
    const fence = makeTile((ctx) => {
      ctx.drawImage(grass0, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 5, 16, 8);
      ctx.fillStyle = p0[6];
      ctx.fillRect(0, 6, 16, 6);
      ctx.fillStyle = p0[8];
      ctx.fillRect(0, 7, 16, 2);
      ctx.fillStyle = p0[7];
      ctx.fillRect(2, 3, 3, 11);
      ctx.fillRect(11, 3, 3, 11);
      ctx.fillStyle = p0[8];
      ctx.fillRect(3, 3, 1, 10);
      ctx.fillRect(12, 3, 1, 10);
    });
    this.bg1Tiles.set(T_FENCE, [fence]);

    // Tree Trunk (BG1 base — lush canopy drawn overhead in BG2!)
    const trunk = makeTile((ctx) => {
      ctx.drawImage(grass0, 0, 0);
      ctx.fillStyle = p0[2];
      ctx.fillRect(2, 11, 12, 4);
      ctx.fillStyle = p0[1];
      ctx.fillRect(4, 0, 8, 13);
      ctx.fillStyle = p0[6];
      ctx.fillRect(5, 0, 6, 12);
      ctx.fillStyle = p0[7];
      ctx.fillRect(6, 0, 2, 12);
    });
    this.bg1Tiles.set(T_TREE_TRUNK, [trunk]);

    // Lush Perimeter Bush
    const bush = makeTile((ctx) => {
      ctx.fillStyle = p0[2];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[1];
      ctx.fillRect(1, 2, 14, 12);
      ctx.fillStyle = p0[2];
      ctx.fillRect(2, 3, 12, 10);
      ctx.fillStyle = p0[3];
      ctx.fillRect(3, 3, 10, 7);
      ctx.fillStyle = p0[4];
      ctx.fillRect(4, 3, 6, 4);
      ctx.fillStyle = p0[5];
      ctx.fillRect(5, 4, 2, 2);
      ctx.fillRect(10, 5, 2, 1);
    });
    this.bg1Tiles.set(T_BUSH, [bush]);

    // Cottage Timber Walls, Warm Lit Window, Wooden Door & Clay Shingle Roof
    const wallWood = makeTile((ctx) => {
      ctx.fillStyle = PALETTES[2][15];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[6];
      ctx.fillRect(0, 0, 2, 16);
      ctx.fillRect(14, 0, 2, 16);
      ctx.fillRect(0, 13, 16, 3);
      ctx.fillStyle = p0[9];
      ctx.fillRect(0, 14, 16, 2);
    });
    this.bg1Tiles.set(T_WALL_WOOD, [wallWood]);

    const wallWindow = makeTile((ctx) => {
      ctx.drawImage(wallWood, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(3, 3, 10, 9);
      ctx.fillStyle = PALETTES[1][10];
      ctx.fillRect(4, 4, 8, 7);
      ctx.fillStyle = PALETTES[1][11];
      ctx.fillRect(5, 5, 3, 3);
      ctx.fillStyle = p0[6];
      ctx.fillRect(7, 4, 2, 7);
      ctx.fillRect(4, 7, 8, 1);
    });
    this.bg1Tiles.set(T_WALL_WINDOW, [wallWindow]);

    const wallDoor = makeTile((ctx) => {
      ctx.drawImage(wallWood, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(3, 2, 10, 14);
      ctx.fillStyle = p0[6];
      ctx.fillRect(4, 3, 8, 13);
      ctx.fillStyle = p0[7];
      ctx.fillRect(5, 4, 6, 12);
      ctx.fillStyle = PALETTES[1][10];
      ctx.fillRect(9, 9, 2, 2);
    });
    this.bg1Tiles.set(T_WALL_DOOR, [wallDoor]);

    const roofBody = makeTile((ctx) => {
      ctx.fillStyle = p0[15];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[1];
      for (let y = 3; y < 16; y += 4) ctx.fillRect(0, y, 16, 1);
      ctx.fillStyle = PALETTES[1][9];
      for (let y = 0; y < 16; y += 4) {
        ctx.fillRect(1 + ((y >> 2) & 1) * 3, y + 1, 4, 1);
        ctx.fillRect(9 + ((y >> 2) & 1) * 2, y + 1, 4, 1);
      }
    });
    this.bg1Tiles.set(T_ROOF_BODY, [roofBody]);

    const roofTop = makeTile((ctx) => {
      ctx.drawImage(grass0, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 4, 16, 12);
      ctx.fillStyle = p0[15];
      ctx.fillRect(0, 5, 16, 11);
      ctx.fillStyle = PALETTES[1][9];
      ctx.fillRect(0, 6, 16, 2);
    });
    this.bg1Tiles.set(T_ROOF_TOP, [roofTop]);

    // Stone Windmill Masonry & Door & Roof
    const wmWall = makeTile((ctx) => {
      ctx.fillStyle = p0[9];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[10];
      ctx.fillRect(1, 1, 6, 6);
      ctx.fillRect(8, 1, 7, 6);
      ctx.fillRect(0, 8, 4, 7);
      ctx.fillRect(5, 8, 6, 7);
      ctx.fillRect(12, 8, 4, 7);
      ctx.fillStyle = p0[11];
      ctx.fillRect(1, 1, 5, 1);
      ctx.fillRect(8, 1, 6, 1);
      ctx.fillRect(5, 8, 5, 1);
    });
    this.bg1Tiles.set(T_WINDMILL_WALL, [wmWall]);

    const wmDoor = makeTile((ctx) => {
      ctx.drawImage(wmWall, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(3, 3, 10, 13);
      ctx.fillStyle = p0[6];
      ctx.fillRect(4, 4, 8, 12);
      ctx.fillStyle = p0[7];
      ctx.fillRect(5, 5, 6, 11);
    });
    this.bg1Tiles.set(T_WINDMILL_DOOR, [wmDoor]);

    const wmRoof = makeTile((ctx) => {
      ctx.fillStyle = PALETTES[1][12];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 14, 16, 2);
      ctx.fillStyle = PALETTES[1][13];
      ctx.fillRect(2, 3, 12, 2);
      ctx.fillRect(4, 8, 8, 2);
    });
    this.bg1Tiles.set(T_WINDMILL_ROOF, [wmRoof]);

    // Ancient Shrine Stone Floor, Glowing Rune, and Mossy Pillar
    const shrineFloor = makeTile((ctx) => {
      ctx.fillStyle = PALETTES[3][13];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = PALETTES[3][14];
      ctx.fillRect(1, 1, 14, 14);
      ctx.fillStyle = p0[11];
      ctx.fillRect(1, 1, 13, 1);
      ctx.fillStyle = PALETTES[3][7];
      ctx.fillRect(12, 12, 3, 2);
    });
    this.bg1Tiles.set(T_SHRINE_FLOOR, [shrineFloor]);

    const shrineRuneFrames = [0, 1].map((f) =>
      makeTile((ctx) => {
        ctx.drawImage(shrineFloor, 0, 0);
        ctx.fillStyle = f === 0 ? PALETTES[3][15] : PALETTES[1][11];
        ctx.fillRect(4, 4, 8, 1);
        ctx.fillRect(4, 11, 8, 1);
        ctx.fillRect(4, 4, 1, 8);
        ctx.fillRect(11, 4, 1, 8);
        ctx.fillRect(7, 6, 2, 4);
      })
    );
    this.bg1Tiles.set(T_SHRINE_RUNE, shrineRuneFrames);

    const shrinePillar = makeTile((ctx) => {
      ctx.drawImage(shrineFloor, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(2, 0, 12, 15);
      ctx.fillStyle = PALETTES[3][13];
      ctx.fillRect(3, 1, 10, 13);
      ctx.fillStyle = PALETTES[3][14];
      ctx.fillRect(4, 2, 7, 10);
      ctx.fillStyle = PALETTES[3][7];
      ctx.fillRect(4, 3, 3, 4);
      ctx.fillRect(9, 8, 3, 3);
    });
    this.bg1Tiles.set(T_SHRINE_PILLAR, [shrinePillar]);

    // Signpost, Lantern Post, Flower Bed
    const signpost = makeTile((ctx) => {
      ctx.drawImage(grass0, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(6, 9, 4, 6);
      ctx.fillRect(2, 2, 12, 8);
      ctx.fillStyle = p0[6];
      ctx.fillRect(7, 9, 2, 5);
      ctx.fillStyle = p0[7];
      ctx.fillRect(3, 3, 10, 6);
      ctx.fillStyle = p0[8];
      ctx.fillRect(4, 4, 8, 1);
      ctx.fillRect(4, 6, 6, 1);
    });
    this.bg1Tiles.set(T_SIGNPOST, [signpost]);

    const lanternFrames = [0, 1].map((f) =>
      makeTile((ctx) => {
        ctx.drawImage(cobble, 0, 0);
        ctx.fillStyle = p0[1];
        ctx.fillRect(6, 6, 4, 9);
        ctx.fillRect(4, 1, 8, 6);
        ctx.fillStyle = p0[9];
        ctx.fillRect(7, 6, 2, 8);
        ctx.fillStyle = f === 0 ? PALETTES[1][10] : PALETTES[1][11];
        ctx.fillRect(5, 2, 6, 4);
      })
    );
    this.bg1Tiles.set(T_LANTERN_POST, lanternFrames);

    const flowerBed = makeTile((ctx) => {
      ctx.drawImage(grass0, 0, 0);
      ctx.fillStyle = p0[6];
      ctx.fillRect(1, 5, 14, 9);
      ctx.fillStyle = p0[3];
      ctx.fillRect(2, 4, 12, 7);
      ctx.fillStyle = p0[15];
      ctx.fillRect(3, 5, 2, 2);
      ctx.fillRect(10, 6, 2, 2);
      ctx.fillStyle = PALETTES[1][10];
      ctx.fillRect(7, 4, 2, 2);
    });
    this.bg1Tiles.set(T_FLOWER_BED, [flowerBed]);

    /* BG2 OVERHEAD FOREGROUND CANOPIES & ARCHES */
    const makeCanopy = (quad: 'TL' | 'TR' | 'BL' | 'BR') =>
      makeTile((ctx) => {
        ctx.fillStyle = p0[1];
        ctx.fillRect(quad.includes('L') ? 2 : 0, quad.includes('T') ? 2 : 0, 14, 14);
        ctx.fillStyle = p0[2];
        ctx.fillRect(quad.includes('L') ? 3 : 0, quad.includes('T') ? 3 : 0, 13, 12);
        ctx.fillStyle = p0[3];
        ctx.fillRect(quad.includes('L') ? 4 : 1, quad.includes('T') ? 3 : 1, 11, 10);
        if (quad.includes('T')) {
          ctx.fillStyle = p0[4];
          ctx.fillRect(4, 4, 7, 5);
          ctx.fillStyle = p0[5];
          ctx.fillRect(5, 5, 3, 2);
        }
      });
    this.bg2Tiles.set(O_CANOPY_TL, makeCanopy('TL'));
    this.bg2Tiles.set(O_CANOPY_TR, makeCanopy('TR'));
    this.bg2Tiles.set(O_CANOPY_BL, makeCanopy('BL'));
    this.bg2Tiles.set(O_CANOPY_BR, makeCanopy('BR'));

    const roofEave = makeTile((ctx) => {
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 12, 16, 4);
      ctx.fillStyle = p0[15];
      ctx.fillRect(0, 12, 16, 3);
    });
    this.bg2Tiles.set(O_ROOF_EAVE, roofEave);

    const shrineArch = makeTile((ctx) => {
      ctx.fillStyle = p0[1];
      ctx.fillRect(2, 0, 12, 16);
      ctx.fillStyle = PALETTES[3][14];
      ctx.fillRect(3, 0, 10, 16);
      ctx.fillStyle = PALETTES[3][15];
      ctx.fillRect(7, 3, 2, 10);
    });
    this.bg2Tiles.set(O_SHRINE_ARCH, shrineArch);
  }

  /* ========================================================================
   * HANDCRAFTED 16-BIT CHARACTER, FAMILIAR, NPC & MONSTER SPRITES
   * ======================================================================== */
  private buildAllSprites(): void {
    const PALETTES = this.palettes;
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

    /* 1. HERO (ROWAN) — 16x22 pixel art across 4 directions x 4 walk frames */
    for (let dir = 0 as Direction; dir < 4; dir = (dir + 1) as Direction) {
      for (let f = 0; f < 4; f++) {
        const c = makeCanvas(16, 22);
        const ctx = c.getContext('2d')!;
        const bob = f === 1 || f === 3 ? -1 : 0;
        const stepL = f === 1 ? -1 : f === 3 ? 1 : 0;
        const stepR = -stepL;

        // Head & Tousled Chestnut Hair
        const headPattern =
          dir === 1
            ? [
                '....11111111....',
                '...1445544441...',
                '..145544444441..',
                '..144444444441..',
                '..144444444441..',
                '...1444444441...',
                '....11111111....',
              ]
            : dir === 2
              ? [
                  '....11111111....',
                  '...1455444441...',
                  '..144444444441..',
                  '..12224444441...',
                  '..1212244441....',
                  '...12223441.....',
                  '....111111......',
                ]
              : dir === 3
                ? [
                    '....11111111....',
                    '...1444445541...',
                    '..144444444441..',
                    '...14444442221..',
                    '....1444422121..',
                    '.....14432221...',
                    '......111111....',
                  ]
                : [
                    '....11111111....',
                    '...1445544441...',
                    '..145444445441..',
                    '..142222222241..',
                    '..122122221221..',
                    '...1222222221...',
                    '....11111111....',
                  ];

        paintPattern(ctx, headPattern, 1, 0, 1 + bob);

        // Terracotta Scarf + Forest Green Tunic + Brass Heart Locket
        const bodyPattern = [
          '...1889999881...',
          '..16888aa88861..',
          '..16676aa67661..',
          '..166766667661..',
          '..126666666621..',
          '...1155555511...',
          '...1cccccccc1...',
        ];
        paintPattern(ctx, bodyPattern, 1, 0, 8 + bob);

        // Animated Boots
        const pal1 = PALETTES[1];
        ctx.fillStyle = pal1[1];
        ctx.fillRect(4, 15 + stepL, 4, 5);
        ctx.fillRect(8, 15 + stepR, 4, 5);
        ctx.fillStyle = pal1[15];
        ctx.fillRect(5, 16 + stepL, 2, 3);
        ctx.fillRect(9, 16 + stepR, 2, 3);

        register('hero', dir, f, c);
      }
    }

    /* 2. PIP THE LANTERN SPRITE — 16x18 (2 glowing hover frames) */
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(16, 18);
      const ctx = c.getContext('2d')!;
      const pat =
        f === 0
          ? [
              '.....111111.....',
              '....1a1111a1....',
              '...1667777661...',
              '..11aaaaaaaa11..',
              '..1a22b22b22a1..',
              '.1ea22288222ae1.',
              '..188998899881..',
              '...1a1abaa1a1...',
              '...1a1abba1a1...',
              '...1a1aaaa1a1...',
              '....11aaaa11....',
              '.....11..11.....',
            ]
          : [
              '.....111111.....',
              '....1a1111a1....',
              '...1677667761...',
              '..11aaaaaaaa11..',
              '.1ea22b22b22ae1.',
              '..1a22288222a1..',
              '..188998899881..',
              '...1a1abba1a1...',
              '...1a1bbbb1a1...',
              '...1a1aaaa1a1...',
              '....11aaaa11....',
              '.....11..11.....',
            ];
      paintPattern(ctx, pat, 1, 0, 2);
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

    /* 5. NPCS (ELDER WREN, CLARA, GUARD BRAM) — Palette 2 */
    const buildNpc = (
      id: SpriteId,
      hairC: string,
      shirtC: string,
      accentC: string,
      spear = false
    ) => {
      for (let f = 0; f < 2; f++) {
        const c = makeCanvas(18, 22);
        const ctx = c.getContext('2d')!;
        const pat = [
          `....11111111....`,
          `...1${hairC}${hairC}${hairC}${hairC}${hairC}${hairC}${hairC}${hairC}1...`,
          `..1${hairC}${hairC}222222${hairC}${hairC}1..`,
          `..1${hairC}21222212${hairC}1..`,
          `...1222222221...`,
          `...1${accentC}${accentC}${accentC}${accentC}${accentC}${accentC}${accentC}${accentC}1...`,
          `..1${shirtC}${shirtC}${shirtC}${accentC}${accentC}${shirtC}${shirtC}${shirtC}1..`,
          `..1${shirtC}${shirtC}${shirtC}${shirtC}${shirtC}${shirtC}${shirtC}${shirtC}1..`,
          `..12${shirtC}${shirtC}${shirtC}${shirtC}${shirtC}${shirtC}21..`,
          `...1111111111...`,
          `....1b1..1b1....`,
          `....111..111....`,
        ];
        paintPattern(ctx, pat, 2, 1, 5 + f);
        if (spear) {
          ctx.fillStyle = PALETTES[2][1];
          ctx.fillRect(15, 2, 2, 18);
          ctx.fillStyle = PALETTES[2][12];
          ctx.fillRect(15, 6, 1, 13);
          ctx.fillStyle = PALETTES[2][4];
          ctx.fillRect(15, 2, 2, 4);
        }
        register(id, 0, f, c);
      }
    };
    buildNpc('npc_elder', '4', '6', 'f', false);
    buildNpc('npc_clara', '7', 'a', '4', false);
    buildNpc('npc_guard', 'b', '8', '5', true);

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
  }
}
