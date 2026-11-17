/**
 * Project Hearth — 16-Bit Tilemap, Indexed 16-Color Sub-Palettes & Collision Tables
 *
 * Stored as flat Uint8Arrays mirroring C99 ROM tables (`const uint8_t map_bg1[]`).
 * Tile size: 16x16 metatiles (composed of four 8x8 hardware tiles on SNES/Genesis).
 */

export const TILE_SIZE = 16;
export const MAP_W = 44;
export const MAP_H = 28;

/* Authentic 4 x 16-Color Hardware Sub-Palettes (Index 0 = Transparent) */
export const PALETTES: readonly (readonly string[])[] = [
  // Palette 0: Terrain & Architecture (Pastoral 16-bit sunlit palette)
  [
    '#000000', // 0 transparent
    '#1B241E', // 1 deep outline / ink
    '#2F5C39', // 2 deep grass shadow
    '#4A884E', // 3 meadow grass mid
    '#74B461', // 4 sunlit grass highlight
    '#A8D678', // 5 bright sprout / flower leaf
    '#7A5438', // 6 dark earth / bark
    '#B88454', // 7 warm dirt path / wood
    '#DFBA84', // 8 light sand / plank highlight
    '#4C5868', // 9 stone shadow
    '#8591A0', // 10 cobble stone mid
    '#C4CBD4', // 11 stone highlight
    '#2D699E', // 12 creek deep water
    '#4D9CD0', // 13 creek water mid
    '#9AD8F4', // 14 water foam / shimmer
    '#B5483A', // 15 cottage clay roof / wildflower red
  ],
  // Palette 1: Hero, Pip & Familiars
  [
    '#000000', // 0 transparent
    '#1F171A', // 1 warm ink outline
    '#F5D2B2', // 2 skin light
    '#D49C76', // 3 skin shade
    '#6B3E26', // 4 chestnut hair / acorn cap
    '#9C613B', // 5 hair highlight / leather
    '#2F6E49', // 6 forest tunic mid
    '#4FA167', // 7 forest tunic light
    '#B84333', // 8 terracotta scarf / cape
    '#E86A4A', // 9 scarf highlight
    '#E8B43C', // 10 locket / lantern gold
    '#FFF1A8', // 11 bright flame / star core
    '#3B5998', // 12 trousers / lumina deep blue
    '#67B7E8', // 13 lumina cyan glow
    '#DDF5FF', // 14 starlight white-blue
    '#4A362C', // 15 boots / dark leather
  ],
  // Palette 2: Villagers & Props
  [
    '#000000', // 0 transparent
    '#1D1B22', // 1 dark outline
    '#F3D1B0', // 2 skin
    '#CE9874', // 3 skin shade
    '#E6E2D8', // 4 silver hair / apron white
    '#A5A29E', // 5 steel / silver shade
    '#C4852D', // 6 elder ochre vest / straw
    '#E8B852', // 7 warm gold hair / lantern glow
    '#385888', // 8 guard blue tunic
    '#5C84B8', // 9 guard tunic highlight
    '#8C3B58', // 10 berry / dress trim
    '#68432E', // 11 dark timber / crate
    '#A87248', // 12 chest wood / bridge beam
    '#52386E', // 13 hollow mist dark violet
    '#9E76CC', // 14 hollow mist glow
    '#F4E7C8', // 15 parchment cream
  ],
  // Palette 3: Enemies, Bosses & FX
  [
    '#000000', // 0 transparent
    '#191421', // 1 ink outline
    '#462A5E', // 2 gloomcap dark purple
    '#7E4AA8', // 3 gloomcap cap mid
    '#C48AF0', // 4 gloomcap spots / dusk glow
    '#EAE0C8', // 5 mushroom stem / fang
    '#335A36', // 6 briar dark vine
    '#578E4A', // 7 briar moss
    '#8BC45E', // 8 spore / thorn bright
    '#8E2D36', // 9 danger crimson dark
    '#E04E4E', // 10 danger crimson bright
    '#F5B841', // 11 critical gold
    '#FFF6CC', // 12 pure spark white-gold
    '#3E4656', // 13 shrine stone dark
    '#748094', // 14 shrine stone mid
    '#5AD2F4', // 15 parry cyan
  ],
];

/* BG1 Ground & Architecture Tile IDs */
export const T_GRASS          = 0;
export const T_GRASS_ALT      = 1;
export const T_FLOWERS        = 2;
export const T_TALL_GRASS     = 3;
export const T_DIRT           = 4;
export const T_COBBLE         = 5;
export const T_WATER          = 6;
export const T_FORD           = 7;
export const T_BRIDGE         = 8;
export const T_BRIDGE_RAIL_N  = 9;
export const T_BRIDGE_RAIL_S  = 10;
export const T_FENCE          = 11;
export const T_TREE_TRUNK     = 12;
export const T_BUSH           = 13;
export const T_WALL_WOOD      = 14;
export const T_WALL_WINDOW    = 15;
export const T_WALL_DOOR      = 16;
export const T_ROOF_BODY      = 17;
export const T_ROOF_TOP       = 18;
export const T_WINDMILL_WALL  = 19;
export const T_WINDMILL_DOOR  = 20;
export const T_WINDMILL_ROOF  = 21;
export const T_SHRINE_FLOOR   = 22;
export const T_SHRINE_RUNE    = 23;
export const T_SHRINE_PILLAR  = 24;
export const T_SIGNPOST       = 25;
export const T_LANTERN_POST   = 26;
export const T_FLOWER_BED     = 27;

/* BG2 Foreground Overhead Tile IDs (Rendered ABOVE actors for authentic 16-bit depth!) */
export const O_NONE           = 0;
export const O_CANOPY_TL      = 1;
export const O_CANOPY_TR      = 2;
export const O_CANOPY_BL      = 3;
export const O_CANOPY_BR      = 4;
export const O_ROOF_EAVE      = 5;
export const O_SHRINE_ARCH    = 6;

/* Collision Codes */
export const COL_WALKABLE   = 0;
export const COL_SOLID      = 1;
export const COL_TALL_GRASS = 2;
export const COL_SHALLOW    = 3;
export const COL_WATER      = 4;

export interface WorldMapData {
  width: number;
  height: number;
  bg1: Uint8Array;
  bg2: Uint8Array;
  col: Uint8Array;
}

export function buildSandboxMap(): WorldMapData {
  const bg1 = new Uint8Array(MAP_W * MAP_H);
  const bg2 = new Uint8Array(MAP_W * MAP_H);
  const col = new Uint8Array(MAP_W * MAP_H);

  const idx = (x: number, y: number) => y * MAP_W + x;
  const setBg1 = (x: number, y: number, tile: number, c: number = COL_WALKABLE) => {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
    bg1[idx(x, y)] = tile;
    col[idx(x, y)] = c;
  };
  const setBg2 = (x: number, y: number, tile: number) => {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return;
    bg2[idx(x, y)] = tile;
  };
  const fillRect = (
    x0: number,
    y0: number,
    w: number,
    h: number,
    tile: number,
    c: number = COL_WALKABLE
  ) => {
    for (let y = y0; y < y0 + h; y++) {
      for (let x = x0; x < x0 + w; x++) {
        setBg1(x, y, tile, c);
      }
    }
  };
  const placeTree = (trunkX: number, trunkY: number) => {
    setBg1(trunkX, trunkY, T_TREE_TRUNK, COL_SOLID);
    setBg2(trunkX - 1, trunkY - 2, O_CANOPY_TL);
    setBg2(trunkX, trunkY - 2, O_CANOPY_TR);
    setBg2(trunkX - 1, trunkY - 1, O_CANOPY_BL);
    setBg2(trunkX, trunkY - 1, O_CANOPY_BR);
    setBg2(trunkX + 1, trunkY - 2, O_CANOPY_TR);
    setBg2(trunkX + 1, trunkY - 1, O_CANOPY_BR);
  };

  // 1. Base varied sunlit meadow grass
  for (let y = 0; y < MAP_H; y++) {
    for (let x = 0; x < MAP_W; x++) {
      const hash = (x * 37 + y * 73) & 15;
      if (hash === 0 || hash === 5) {
        setBg1(x, y, T_GRASS_ALT, COL_WALKABLE);
      } else if (hash === 11 && x > 3 && x < MAP_W - 4 && y > 3 && y < MAP_H - 4) {
        setBg1(x, y, T_FLOWERS, COL_WALKABLE);
      } else {
        setBg1(x, y, T_GRASS, COL_WALKABLE);
      }
    }
  }

  // 2. Perimeter forest border (trees & bushes)
  for (let x = 0; x < MAP_W; x++) {
    setBg1(x, 0, T_BUSH, COL_SOLID);
    setBg1(x, 1, T_BUSH, COL_SOLID);
    setBg1(x, MAP_H - 1, T_BUSH, COL_SOLID);
    setBg1(x, MAP_H - 2, T_BUSH, COL_SOLID);
  }
  for (let y = 0; y < MAP_H; y++) {
    setBg1(0, y, T_BUSH, COL_SOLID);
    setBg1(1, y, T_BUSH, COL_SOLID);
    setBg1(MAP_W - 1, y, T_BUSH, COL_SOLID);
    setBg1(MAP_W - 2, y, T_BUSH, COL_SOLID);
  }

  // 3. Winding Creek (Cols 18-20 with organic curve)
  for (let y = 2; y < MAP_H - 2; y++) {
    const curve = y < 10 ? -1 : y > 19 ? 1 : 0;
    const rx = 19 + curve;
    setBg1(rx - 1, y, T_WATER, COL_WATER);
    setBg1(rx, y, T_WATER, COL_WATER);
    setBg1(rx + 1, y, T_WATER, COL_WATER);
  }

  // 4. Hamlet Cobblestone Plaza (West)
  fillRect(6, 10, 10, 8, T_COBBLE, COL_WALKABLE);
  fillRect(7, 17, 5, 4, T_COBBLE, COL_WALKABLE);

  // Dirt Road leading from Plaza across Timber Bridge into Eastern Meadow & Shrine
  fillRect(15, 12, 3, 3, T_DIRT, COL_WALKABLE);
  fillRect(21, 12, 14, 3, T_DIRT, COL_WALKABLE);
  // Spur paths to Shrine, Chest Grove, and Ford
  fillRect(25, 8, 2, 5, T_DIRT, COL_WALKABLE);
  fillRect(13, 18, 3, 5, T_DIRT, COL_WALKABLE);
  fillRect(15, 21, 13, 2, T_DIRT, COL_WALKABLE);
  fillRect(27, 14, 2, 8, T_DIRT, COL_WALKABLE);

  // 5. Timber Bridge spanning the creek at Rows 12-14
  for (let x = 18; x <= 20; x++) {
    setBg1(x, 11, T_BRIDGE_RAIL_N, COL_SOLID);
    setBg1(x, 12, T_BRIDGE, COL_WALKABLE);
    setBg1(x, 13, T_BRIDGE, COL_WALKABLE);
    setBg1(x, 14, T_BRIDGE, COL_WALKABLE);
    setBg1(x, 15, T_BRIDGE_RAIL_S, COL_SOLID);
  }

  // 6. Shallow Pebble Ford (South crossing — splashes underfoot!)
  for (let x = 19; x <= 21; x++) {
    setBg1(x, 21, T_FORD, COL_SHALLOW);
    setBg1(x, 22, T_FORD, COL_SHALLOW);
  }

  // 7. Stone Windmill (Cols 5-7, Rows 4-8)
  fillRect(5, 4, 3, 2, T_WINDMILL_ROOF, COL_SOLID);
  fillRect(5, 6, 3, 3, T_WINDMILL_WALL, COL_SOLID);
  setBg1(6, 8, T_WINDMILL_DOOR, COL_SOLID);

  // 8. Cozy Timber Cottage (Cols 11-15, Rows 4-7)
  fillRect(11, 4, 5, 1, T_ROOF_TOP, COL_SOLID);
  fillRect(11, 5, 5, 1, T_ROOF_BODY, COL_SOLID);
  setBg1(11, 6, T_WALL_WOOD, COL_SOLID);
  setBg1(12, 6, T_WALL_WINDOW, COL_SOLID);
  setBg1(13, 6, T_WALL_DOOR, COL_SOLID);
  setBg1(14, 6, T_WALL_WINDOW, COL_SOLID);
  setBg1(15, 6, T_WALL_WOOD, COL_SOLID);
  // Roof overhang on BG2
  for (let x = 11; x <= 15; x++) {
    setBg2(x, 5, O_ROOF_EAVE);
  }

  // Flower beds & Hamlet lamps & fence accents
  setBg1(11, 7, T_FLOWER_BED, COL_SOLID);
  setBg1(15, 7, T_FLOWER_BED, COL_SOLID);
  setBg1(10, 9, T_LANTERN_POST, COL_SOLID);
  setBg1(16, 11, T_LANTERN_POST, COL_SOLID);
  setBg1(16, 15, T_SIGNPOST, COL_SOLID);
  for (let x = 3; x <= 6; x++) {
    setBg1(x, 22, T_FENCE, COL_SOLID);
  }
  for (let x = 11; x <= 13; x++) {
    setBg1(x, 23, T_FENCE, COL_SOLID);
  }

  // 9. Tall Rustling Wildgrass patches in Whispering Meadow (East)
  fillRect(23, 6, 4, 3, T_TALL_GRASS, COL_TALL_GRASS);
  fillRect(29, 5, 5, 4, T_TALL_GRASS, COL_TALL_GRASS);
  fillRect(23, 16, 4, 4, T_TALL_GRASS, COL_TALL_GRASS);
  fillRect(29, 17, 5, 5, T_TALL_GRASS, COL_TALL_GRASS);
  fillRect(35, 20, 4, 3, T_TALL_GRASS, COL_TALL_GRASS);
  // Small grass patch in hamlet west edge for immediate feel testing
  fillRect(3, 14, 3, 4, T_TALL_GRASS, COL_TALL_GRASS);

  // 10. Ancient Stone Training Shrine (Far East, Cols 35-41, Rows 9-17)
  fillRect(35, 9, 6, 9, T_SHRINE_FLOOR, COL_WALKABLE);
  fillRect(37, 12, 3, 3, T_SHRINE_RUNE, COL_WALKABLE);
  // Shrine stone pillars
  setBg1(35, 9, T_SHRINE_PILLAR, COL_SOLID);
  setBg1(40, 9, T_SHRINE_PILLAR, COL_SOLID);
  setBg1(35, 17, T_SHRINE_PILLAR, COL_SOLID);
  setBg1(40, 17, T_SHRINE_PILLAR, COL_SOLID);
  setBg1(35, 11, T_SHRINE_PILLAR, COL_SOLID);
  setBg1(35, 15, T_SHRINE_PILLAR, COL_SOLID);
  // Overhead archway lintel on BG2 so player walks underneath!
  setBg2(35, 12, O_SHRINE_ARCH);
  setBg2(35, 13, O_SHRINE_ARCH);
  setBg2(35, 14, O_SHRINE_ARCH);

  // 11. Scenic Trees with Y-sorted Trunks & BG2 Overhead Canopies
  const trees: [number, number][] = [
    [3, 5],
    [3, 10],
    [3, 20],
    [8, 24],
    [16, 5],
    [22, 5],
    [28, 4],
    [34, 5],
    [39, 5],
    [22, 18],
    [35, 24],
    [40, 23],
  ];
  for (const [tx, ty] of trees) {
    placeTree(tx, ty);
  }

  return { width: MAP_W, height: MAP_H, bg1, bg2, col };
}
