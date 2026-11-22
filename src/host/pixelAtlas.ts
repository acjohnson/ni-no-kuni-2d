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
  O_MAPLE_TL,
  O_MAPLE_TR,
  O_OAK_CROWN,
  O_PINE_TOP,
  O_ROOF_EAVE,
  O_SHRINE_ARCH,
  SpriteId,
  T_ATTIC_CHEST,
  T_AUTUMN_GRASS,
  T_BED_QUILT,
  T_BOULDER,
  T_BRIDGE,
  T_BRIDGE_RAIL_N,
  T_BRIDGE_RAIL_S,
  T_BUSH,
  T_CLIFF_LEDGE,
  T_CLIFF_WALL,
  T_COBBLE,
  T_DIRT,
  T_FENCE,
  T_FIREPLACE,
  T_FLOWER_BED,
  T_FLOWERS,
  T_FORD,
  T_GRASS,
  T_GRASS_ALT,
  T_HEARTH_RUG,
  T_HOLLOW_OAK,
  T_INTERIOR_WALL,
  T_LANTERN_POST,
  T_MAPLE_TRUNK,
  T_MTN_SKY,
  T_PINE_TRUNK,
  T_RAIL_TRACK,
  T_RAPIDS,
  T_ROCKY_PATH,
  T_ROOF_BODY,
  T_ROOF_TOP,
  T_SHRINE_FLOOR,
  T_SHRINE_PILLAR,
  T_SHRINE_RUNE,
  T_SIGNPOST,
  T_STONE_STAIRS,
  T_TALL_GRASS,
  T_TREE_TRUNK,
  T_WALL_DOOR,
  T_WALL_WINDOW,
  T_WALL_WOOD,
  T_WATER,
  T_WINDMILL_DOOR,
  T_WINDMILL_ROOF,
  T_WINDMILL_WALL,
  T_WOOD_FLOOR,
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
  private activePalettes: (readonly string[])[];
  private readonly zonePalettes: readonly (readonly string[])[];
  private currentTerrainPalId = 0;
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

  public setTerrainPalette(palId: number): void {
    if (palId === this.currentTerrainPalId) return;
    if (palId >= 0 && palId < this.zonePalettes.length) {
      this.currentTerrainPalId = palId;
      this.activePalettes[0] = this.zonePalettes[palId];
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
   * 16x16 TERRAIN & ARCHITECTURE TILES (PALETTE 0)
   * ======================================================================== */
  private buildAllTiles(): void {
    const PALETTES = this.activePalettes;
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

    /* ======================================================================
     * CHAPTER 1 GROUND-UP AUTUMN MOUNTAIN & COTTAGE INTERIOR TILES (28..45)
     * ====================================================================== */
    // 28. T_MTN_SKY: Transparent aperture revealing live parallax alpine vista behind BG1
    const mtnSky = makeTile(() => {
      /* intentionally transparent */
    });
    this.bg1Tiles.set(T_MTN_SKY, [mtnSky]);

    // 29. T_CLIFF_WALL: Steep stratified slate-granite mountain cliff face
    const cliffWall = makeTile((ctx) => {
      ctx.fillStyle = p0[9];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[10];
      ctx.fillRect(1, 1, 6, 7);
      ctx.fillRect(8, 2, 7, 6);
      ctx.fillRect(0, 9, 5, 6);
      ctx.fillRect(6, 10, 6, 5);
      ctx.fillRect(13, 9, 3, 6);
      ctx.fillStyle = p0[11];
      ctx.fillRect(1, 1, 5, 1);
      ctx.fillRect(8, 2, 6, 1);
      ctx.fillRect(6, 10, 5, 1);
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 8, 16, 1);
      ctx.fillRect(7, 0, 1, 9);
      ctx.fillRect(5, 9, 1, 7);
      ctx.fillRect(12, 9, 1, 7);
    });
    this.bg1Tiles.set(T_CLIFF_WALL, [cliffWall]);

    // 30. T_CLIFF_LEDGE: Moss-crowned granite cliff escarpment edge
    const cliffLedge = makeTile((ctx) => {
      ctx.drawImage(cliffWall, 0, 0);
      ctx.fillStyle = p0[3];
      ctx.fillRect(0, 0, 16, 5);
      ctx.fillStyle = p0[4];
      ctx.fillRect(1, 1, 5, 2);
      ctx.fillRect(9, 0, 6, 3);
      ctx.fillStyle = p0[5];
      ctx.fillRect(2, 1, 2, 1);
      ctx.fillRect(11, 1, 3, 1);
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 5, 16, 1);
      ctx.fillStyle = p0[11];
      ctx.fillRect(1, 6, 14, 1);
    });
    this.bg1Tiles.set(T_CLIFF_LEDGE, [cliffLedge]);

    // 31. T_BOULDER: Mossy alpine granite boulder
    const boulder = makeTile((ctx) => {
      ctx.fillStyle = p0[3];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[2];
      ctx.fillRect(2, 12, 12, 3);
      ctx.fillStyle = p0[1];
      ctx.fillRect(2, 3, 12, 10);
      ctx.fillRect(4, 2, 8, 12);
      ctx.fillStyle = p0[9];
      ctx.fillRect(3, 4, 10, 8);
      ctx.fillStyle = p0[10];
      ctx.fillRect(4, 3, 8, 7);
      ctx.fillStyle = p0[11];
      ctx.fillRect(5, 3, 5, 3);
      ctx.fillRect(4, 5, 2, 2);
      // Autumn moss tuft on stone
      ctx.fillStyle = p0[4];
      ctx.fillRect(9, 4, 3, 2);
      ctx.fillStyle = p0[15];
      ctx.fillRect(11, 10, 2, 1);
    });
    this.bg1Tiles.set(T_BOULDER, [boulder]);

    // 32. T_AUTUMN_GRASS: Golden-ochre meadow scattered with fallen crimson & gold maple leaves
    const autumnGrass = makeTile((ctx) => {
      ctx.fillStyle = p0[3];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[4];
      ctx.fillRect(1, 2, 4, 2);
      ctx.fillRect(9, 7, 5, 2);
      ctx.fillRect(3, 12, 4, 2);
      ctx.fillStyle = p0[2];
      ctx.fillRect(6, 4, 2, 1);
      ctx.fillRect(12, 13, 2, 1);
      // Fallen crimson & amber maple leaves
      ctx.fillStyle = p0[15];
      ctx.fillRect(3, 5, 2, 2);
      ctx.fillRect(12, 3, 2, 1);
      ctx.fillStyle = p0[5];
      ctx.fillRect(10, 11, 2, 2);
      ctx.fillRect(5, 13, 2, 1);
    });
    this.bg1Tiles.set(T_AUTUMN_GRASS, [autumnGrass]);

    // 33. T_ROCKY_PATH: Mountain gravel trail with flat slate stepping stones
    const rockyPath = makeTile((ctx) => {
      ctx.fillStyle = p0[7];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[6];
      ctx.fillRect(0, 0, 2, 1);
      ctx.fillRect(13, 14, 3, 2);
      ctx.fillStyle = p0[10];
      ctx.fillRect(2, 2, 5, 4);
      ctx.fillRect(9, 8, 5, 4);
      ctx.fillRect(3, 10, 4, 3);
      ctx.fillStyle = p0[11];
      ctx.fillRect(2, 2, 4, 1);
      ctx.fillRect(9, 8, 4, 1);
      ctx.fillStyle = p0[8];
      ctx.fillRect(10, 3, 2, 1);
      ctx.fillRect(1, 7, 2, 1);
    });
    this.bg1Tiles.set(T_ROCKY_PATH, [rockyPath]);

    // 34. T_RAPIDS: 4-frame animated whitewater alpine rapids
    const rapidsFrames = [0, 1, 2, 3].map((f) =>
      makeTile((ctx) => {
        ctx.fillStyle = p0[12];
        ctx.fillRect(0, 0, 16, 16);
        ctx.fillStyle = p0[13];
        ctx.fillRect(0, (f * 4) % 16, 16, 5);
        ctx.fillRect(0, (f * 4 + 9) % 16, 16, 4);
        // Foaming whitewater streaks
        ctx.fillStyle = p0[14];
        ctx.fillRect((1 + f * 3) % 12, (2 + f * 4) % 14, 5, 2);
        ctx.fillRect((8 + f * 2) % 11, (9 + f * 4) % 14, 6, 2);
        ctx.fillStyle = '#FFFFFF';
        ctx.fillRect((2 + f * 3) % 13, (2 + f * 4) % 14, 3, 1);
        ctx.fillRect((9 + f * 2) % 12, (10 + f * 4) % 14, 4, 1);
      })
    );
    this.bg1Tiles.set(T_RAPIDS, rapidsFrames);

    // 35. T_RAIL_TRACK: Mountain railway ballast, cedar ties, and glinting twin steel rails
    const railTrack = makeTile((ctx) => {
      ctx.fillStyle = p0[9];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[10];
      for (let x = 1; x < 16; x += 3) {
        ctx.fillRect(x, 1, 2, 2);
        ctx.fillRect((x + 1) % 15, 13, 2, 2);
      }
      // Cedar sleepers (crossties)
      ctx.fillStyle = p0[6];
      ctx.fillRect(2, 2, 3, 12);
      ctx.fillRect(10, 2, 3, 12);
      ctx.fillStyle = p0[7];
      ctx.fillRect(3, 3, 1, 10);
      ctx.fillRect(11, 3, 1, 10);
      // Twin iron rails
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 3, 16, 3);
      ctx.fillRect(0, 10, 16, 3);
      ctx.fillStyle = p0[11];
      ctx.fillRect(0, 4, 16, 1);
      ctx.fillRect(0, 11, 16, 1);
    });
    this.bg1Tiles.set(T_RAIL_TRACK, [railTrack]);

    // 36. T_MAPLE_TRUNK: Flared dark trunk with crimson & gold autumn leaf litter
    const mapleTrunk = makeTile((ctx) => {
      ctx.drawImage(autumnGrass, 0, 0);
      ctx.fillStyle = p0[15];
      ctx.fillRect(2, 11, 4, 3);
      ctx.fillRect(10, 12, 4, 2);
      ctx.fillStyle = p0[5];
      ctx.fillRect(3, 12, 2, 1);
      ctx.fillRect(11, 11, 2, 2);
      ctx.fillStyle = p0[1];
      ctx.fillRect(4, 0, 8, 13);
      ctx.fillStyle = p0[6];
      ctx.fillRect(5, 0, 6, 12);
      ctx.fillStyle = p0[7];
      ctx.fillRect(6, 0, 2, 11);
    });
    this.bg1Tiles.set(T_MAPLE_TRUNK, [mapleTrunk]);

    // 37. T_PINE_TRUNK: Rugged alpine spruce base with dark evergreen boughs
    const pineTrunk = makeTile((ctx) => {
      ctx.drawImage(grass0, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(5, 8, 6, 7);
      ctx.fillStyle = p0[6];
      ctx.fillRect(6, 8, 4, 6);
      // Lower evergreen boughs
      ctx.fillStyle = p0[1];
      ctx.fillRect(1, 2, 14, 8);
      ctx.fillStyle = p0[2];
      ctx.fillRect(2, 2, 12, 7);
      ctx.fillStyle = p0[3];
      ctx.fillRect(3, 3, 10, 4);
      ctx.fillStyle = p0[4];
      ctx.fillRect(5, 3, 5, 2);
    });
    this.bg1Tiles.set(T_PINE_TRUNK, [pineTrunk]);

    // 38. T_WOOD_FLOOR: Warm cedar cottage floorboards
    const woodFloor = makeTile((ctx) => {
      ctx.fillStyle = p0[7];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[8];
      ctx.fillRect(0, 1, 16, 2);
      ctx.fillRect(0, 9, 16, 2);
      ctx.fillStyle = p0[6];
      ctx.fillRect(0, 7, 16, 1);
      ctx.fillRect(0, 15, 16, 1);
      ctx.fillRect(7, 0, 1, 8);
      ctx.fillRect(14, 8, 1, 8);
    });
    this.bg1Tiles.set(T_WOOD_FLOOR, [woodFloor]);

    // 39. T_HEARTH_RUG: Cozy woven burgundy & cream hearth rug
    const hearthRug = makeTile((ctx) => {
      ctx.fillStyle = p0[15];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[8];
      ctx.fillRect(1, 1, 14, 1);
      ctx.fillRect(1, 14, 14, 1);
      ctx.fillRect(1, 1, 1, 14);
      ctx.fillRect(14, 1, 1, 14);
      ctx.fillStyle = p0[5];
      ctx.fillRect(4, 4, 8, 8);
      ctx.fillStyle = p0[15];
      ctx.fillRect(6, 6, 4, 4);
    });
    this.bg1Tiles.set(T_HEARTH_RUG, [hearthRug]);

    // 40. T_FIREPLACE: 2-frame crackling stone cottage hearth
    const fireplaceFrames = [0, 1].map((f) =>
      makeTile((ctx) => {
        ctx.fillStyle = p0[9];
        ctx.fillRect(0, 0, 16, 16);
        ctx.fillStyle = p0[10];
        ctx.fillRect(1, 1, 14, 4);
        ctx.fillRect(1, 6, 3, 9);
        ctx.fillRect(12, 6, 3, 9);
        ctx.fillStyle = p0[11];
        ctx.fillRect(1, 1, 14, 1);
        // Firebox opening
        ctx.fillStyle = p0[1];
        ctx.fillRect(4, 6, 8, 9);
        // Logs & dancing flame
        ctx.fillStyle = p0[6];
        ctx.fillRect(5, 12, 6, 2);
        ctx.fillStyle = p0[15];
        ctx.fillRect(5, 8 + f, 6, 4 - f);
        ctx.fillStyle = p0[5];
        ctx.fillRect(6, 7 + (1 - f), 4, 4);
        ctx.fillStyle = p0[8];
        ctx.fillRect(7, 9, 2, 3);
      })
    );
    this.bg1Tiles.set(T_FIREPLACE, fireplaceFrames);

    // 41. T_BED_QUILT: Rowan's patchwork mountain quilt & cream pillow
    const bedQuilt = makeTile((ctx) => {
      ctx.fillStyle = p0[6];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[8];
      ctx.fillRect(2, 2, 12, 4);
      ctx.fillStyle = p0[11];
      ctx.fillRect(3, 3, 10, 2);
      ctx.fillStyle = p0[15];
      ctx.fillRect(2, 6, 12, 8);
      ctx.fillStyle = p0[5];
      ctx.fillRect(2, 7, 4, 3);
      ctx.fillRect(10, 10, 4, 3);
      ctx.fillStyle = p0[3];
      ctx.fillRect(6, 10, 4, 3);
    });
    this.bg1Tiles.set(T_BED_QUILT, [bedQuilt]);

    // 42. T_ATTIC_CHEST: Heirloom brass-banded cedar keepsake chest
    const atticChest = makeTile((ctx) => {
      ctx.drawImage(woodFloor, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(1, 2, 14, 12);
      ctx.fillStyle = p0[6];
      ctx.fillRect(2, 3, 12, 10);
      ctx.fillStyle = p0[7];
      ctx.fillRect(3, 4, 10, 4);
      ctx.fillStyle = p0[5];
      ctx.fillRect(2, 7, 12, 1);
      ctx.fillRect(7, 6, 2, 3);
      ctx.fillStyle = p0[8];
      ctx.fillRect(3, 3, 10, 1);
    });
    this.bg1Tiles.set(T_ATTIC_CHEST, [atticChest]);

    // 43. T_HOLLOW_OAK: Ancient mossy oak root ring with subtle golden runes
    const hollowOakTile = makeTile((ctx) => {
      ctx.drawImage(autumnGrass, 0, 0);
      ctx.fillStyle = p0[1];
      ctx.fillRect(1, 1, 14, 14);
      ctx.fillStyle = p0[6];
      ctx.fillRect(2, 2, 12, 12);
      ctx.fillStyle = p0[7];
      ctx.fillRect(3, 3, 10, 10);
      ctx.fillStyle = p0[5];
      ctx.fillRect(6, 5, 4, 6);
      ctx.fillStyle = p0[8];
      ctx.fillRect(7, 6, 2, 4);
    });
    this.bg1Tiles.set(T_HOLLOW_OAK, [hollowOakTile]);

    // 44. T_STONE_STAIRS: Carved highland stone steps
    const stoneStairs = makeTile((ctx) => {
      ctx.fillStyle = p0[9];
      ctx.fillRect(0, 0, 16, 16);
      for (let y = 0; y < 16; y += 4) {
        ctx.fillStyle = p0[10];
        ctx.fillRect(1, y, 14, 3);
        ctx.fillStyle = p0[11];
        ctx.fillRect(1, y, 14, 1);
        ctx.fillStyle = p0[1];
        ctx.fillRect(0, y + 3, 16, 1);
      }
    });
    this.bg1Tiles.set(T_STONE_STAIRS, [stoneStairs]);

    // 45. T_INTERIOR_WALL: Warm cedar-beamed cottage wall
    const interiorWall = makeTile((ctx) => {
      ctx.fillStyle = p0[8];
      ctx.fillRect(0, 0, 16, 16);
      ctx.fillStyle = p0[6];
      ctx.fillRect(0, 0, 16, 2);
      ctx.fillRect(0, 10, 16, 6);
      ctx.fillRect(0, 0, 2, 16);
      ctx.fillRect(14, 0, 2, 16);
      ctx.fillStyle = p0[7];
      ctx.fillRect(2, 11, 12, 4);
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 15, 16, 1);
    });
    this.bg1Tiles.set(T_INTERIOR_WALL, [interiorWall]);

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

    // 7 & 8. O_MAPLE_TL / O_MAPLE_TR: Radiant crimson, amber & gold autumn maple foliage!
    const makeMapleCanopy = (left: boolean) =>
      makeTile((ctx) => {
        ctx.fillStyle = p0[1];
        ctx.fillRect(left ? 1 : 0, 1, 15, 14);
        ctx.fillStyle = p0[15];
        ctx.fillRect(left ? 2 : 0, 2, 14, 12);
        ctx.fillStyle = p0[7];
        ctx.fillRect(left ? 3 : 1, 3, 12, 9);
        ctx.fillStyle = p0[15];
        ctx.fillRect(left ? 2 : 3, 5, 10, 7);
        ctx.fillStyle = p0[5];
        ctx.fillRect(left ? 4 : 2, 2, 7, 5);
        ctx.fillStyle = p0[8];
        ctx.fillRect(left ? 5 : 4, 3, 3, 2);
      });
    this.bg2Tiles.set(O_MAPLE_TL, makeMapleCanopy(true));
    this.bg2Tiles.set(O_MAPLE_TR, makeMapleCanopy(false));

    // 9. O_PINE_TOP: Pointed tiered alpine spruce spire
    const pineTop = makeTile((ctx) => {
      ctx.fillStyle = p0[1];
      ctx.fillRect(6, 1, 4, 4);
      ctx.fillRect(4, 4, 8, 5);
      ctx.fillRect(1, 8, 14, 8);
      ctx.fillStyle = p0[2];
      ctx.fillRect(7, 2, 2, 3);
      ctx.fillRect(5, 5, 6, 4);
      ctx.fillRect(2, 9, 12, 6);
      ctx.fillStyle = p0[3];
      ctx.fillRect(6, 5, 4, 3);
      ctx.fillRect(3, 10, 9, 4);
      ctx.fillStyle = p0[4];
      ctx.fillRect(5, 10, 4, 2);
    });
    this.bg2Tiles.set(O_PINE_TOP, pineTop);

    // 10. O_OAK_CROWN: Monumental golden-amber Hollow Oak canopy crown
    const oakCrown = makeTile((ctx) => {
      ctx.fillStyle = p0[1];
      ctx.fillRect(0, 1, 16, 14);
      ctx.fillStyle = p0[6];
      ctx.fillRect(1, 2, 14, 12);
      ctx.fillStyle = p0[4];
      ctx.fillRect(1, 2, 14, 9);
      ctx.fillStyle = p0[5];
      ctx.fillRect(3, 3, 10, 5);
      ctx.fillStyle = p0[8];
      ctx.fillRect(5, 4, 5, 2);
    });
    this.bg2Tiles.set(O_OAK_CROWN, oakCrown);
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

    // Shadow Grandma (Trauma Vision in Evertale Flashback) — 20x24
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(20, 24);
      const ctx = c.getContext('2d')!;
      const bob = f === 0 ? 0 : -1;
      const pat = [
        '.....1111111111.....',
        '...11222333322211...',
        '..1223344444433221..',
        '..1234455555544321..',
        '..12341b4444b14321..',
        '..1234444444444321..',
        '...12233111133221...',
        '..1222333ff3332221..',
        '.1222333ffff3332221.',
        '.12222333ff33322221.',
        '.122222333333222221.',
        '..1222222332222221..',
        '..1221222222221221..',
        '...11..111111..11...',
      ];
      paintPattern(ctx, pat, 3, 0, 4 + bob);
      register('npc_shadow_gran', 0, f, c);
    }

    // Milo the Runaway Fox-Pup — 14x14
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(14, 14);
      const ctx = c.getContext('2d')!;
      const pat = [
        '.11....11.....',
        '1991111991....',
        '1999999991....',
        '191999919111..',
        '.199aa99999b1.',
        '..19999999bb1.',
        '..1999999911..',
        '..111..111....',
      ];
      paintPattern(ctx, pat, 1, 0, 4 + f);
      register('pet_milo', 0, f, c);
    }

    // Alderbrook Townsfolk (Grandma / Villager) — Palette 2
    buildNpc('npc_townsfolk', 'f', '7', '4', false);

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

    /* 11. CHAPTER 1 BESPOKE ALDERBROOK CAST & MOUNTAIN STEAM LOCOMOTIVE */
    // Grandma Mara — silver bun, rose-burgundy knitted shawl, warm cream hearth apron
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(18, 22);
      const ctx = c.getContext('2d')!;
      const pat = [
        '.....111111.....',
        '....14444441....',
        '...1444444441...',
        '..144222222441..',
        '..142122221241..',
        '...1222332221...',
        '..1aaaaccaaaa1..',
        '.1aaaaffffaaaa1.',
        '.1a666ffff666a1.',
        '..1666ffff6661..',
        '...1111111111...',
        '....1b1..1b1....',
      ];
      paintPattern(ctx, pat, 2, 1, 5 + f);
      register('npc_mara', 0, f, c);
    }

    // Stationmaster Barnaby — navy peaked rail cap, brass badge, walrus mustache, slate uniform
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(18, 22);
      const ctx = c.getContext('2d')!;
      const pat = [
        '...1111111111...',
        '..188887788881..',
        '.11111111111111.',
        '..122222222221..',
        '..121222222121..',
        '..124444444421..',
        '..188887788881..',
        '.18888877888881.',
        '.18888888888881.',
        '..128888888821..',
        '...1111111111...',
        '....111..111....',
      ];
      paintPattern(ctx, pat, 2, 1, 5 + f);
      register('npc_barnaby', 0, f, c);
    }

    // Baker Maple — cream baker's kerchief, auburn hair, flour-dusted apron over rose dress
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(18, 22);
      const ctx = c.getContext('2d')!;
      const pat = [
        '....11111111....',
        '...1ffffffff1...',
        '..1bb222222bb1..',
        '..1b21222212b1..',
        '...1222222221...',
        '..1aaaffffaaa1..',
        '.1aaaaffffaaaa1.',
        '.1a2aaffffaa2a1.',
        '..1aaaffffaaa1..',
        '...1111111111...',
        '....1b1..1b1....',
      ];
      paintPattern(ctx, pat, 2, 1, 6 + f);
      register('npc_maple', 0, f, c);
    }

    // Old Woodsman Jasper — pine-green hood, silver beard, weathered timber cloak & axe
    for (let f = 0; f < 2; f++) {
      const c = makeCanvas(18, 22);
      const ctx = c.getContext('2d')!;
      const pat = [
        '....11111111....',
        '...1666666661...',
        '..166222222661..',
        '..162122221261..',
        '..164444444461..',
        '..166444444661..',
        '.1666bbbbbb6661.',
        '.16666666666661.',
        '..126666666621..',
        '...1111111111...',
        '....1b1..1b1....',
      ];
      paintPattern(ctx, pat, 2, 1, 6 + f);
      // Woodcutter's walking staff
      ctx.fillStyle = PALETTES[2][1];
      ctx.fillRect(15, 3, 2, 17);
      ctx.fillStyle = PALETTES[2][11];
      ctx.fillRect(15, 4, 1, 15);
      register('npc_jasper', 0, f, c);
    }

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
  }
}
