/**
 * Project Hearth — C99-Mirrored Core Types & Constants
 *
 * STRICT PORTABILITY RULE:
 * Zero DOM, zero React, zero browser globals, zero floating-point drift.
 * Every type here maps cleanly to C99 structs and fixed-width integers
 * for future ports to Sega Genesis (SGDK), SNES (PVSnesLib), or SDL2.
 */

export const HEARTH_VERSION = 'v0.6.0';

/* 16.8 Fixed-Point Arithmetic (1 pixel = 256 subpixels) */
export const FP_SHIFT = 8;
export const FP_ONE = 1 << FP_SHIFT; // 256

export function toFp(px: number): number {
  return (px * FP_ONE) | 0;
}

export function fromFp(fp: number): number {
  return fp >> FP_SHIFT;
}

export function fpMul(a: number, b: number): number {
  return ((a * b) >> FP_SHIFT) | 0;
}

/* 12-Button Retro Gamepad Bitmask (uint16_t) */
export const PAD_UP     = 1 << 0;
export const PAD_DOWN   = 1 << 1;
export const PAD_LEFT   = 1 << 2;
export const PAD_RIGHT  = 1 << 3;
export const PAD_A      = 1 << 4; // Confirm / Interact / Timed Hit
export const PAD_B      = 1 << 5; // Cancel / Dash
export const PAD_X      = 1 << 6; // Companion / Info
export const PAD_Y      = 1 << 7; // Quick Cast / Inspect
export const PAD_L      = 1 << 8; // Prev Familiar
export const PAD_R      = 1 << 9; // Next Familiar
export const PAD_START  = 1 << 10;
export const PAD_SELECT = 1 << 11;

export type PpuMode = 'GENESIS_320x224' | 'SNES_256x224';

export type Direction = 0 | 1 | 2 | 3; // 0=DOWN, 1=UP, 2=LEFT, 3=RIGHT
export const DIR_DOWN: Direction = 0;
export const DIR_UP: Direction = 1;
export const DIR_LEFT: Direction = 2;
export const DIR_RIGHT: Direction = 3;

export type GameMode =
  | 'INTRO'
  | 'TITLE'
  | 'EXPLORE'
  | 'CUTSCENE'
  | 'BATTLE_INTRO'
  | 'BATTLE'
  | 'BATTLE_VICTORY'
  | 'BATTLE_DEFEAT'
  | 'SHOP'
  | 'FISHING';

export type ZoneId =
  | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12
  | 13 | 14 | 15 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 23 | 24;
export type TimeOfDay = 0 | 1 | 2 | 3; // 0=Morning, 1=Afternoon, 2=Dusk, 3=Night

export type BgmTrackId =
  | 'HAMLET'
  | 'ALDERBROOK'
  | 'WHISKERBURGH'
  | 'SEWERS'
  | 'BOARHOLD'
  | 'SHADOW'
  | 'BATTLE'
  | 'BOSS'
  | 'VICTORY'
  | 'CINEMA_WONDER'
  | 'CINEMA_LULLABY'
  | 'DESERT'
  | 'PARADISE'
  | 'FROST'
  | 'JUNGLE'
  | 'VOLCANO'
  | 'TECH';

export type BattleBiome =
  | 'MEADOW'
  | 'SHRINE'
  | 'SEWER'
  | 'BOARHOLD'
  | 'DESERT'
  | 'PARADISE'
  | 'FOREST'
  | 'TOWN'
  | 'INTERIOR'
  | 'SEA'
  | 'FROST'
  | 'VOLCANO';

export type EncounterPreset =
  | 'MEADOW_PACK'
  | 'GLADE_DUO'
  | 'SHRINE_BOSS'
  | 'SEWER_PACK'
  | 'SEWER_BOSS'
  | 'BOAR_GUARD'
  | 'IRONFANG_BOSS'
  | 'DESERT_PACK'
  | 'SANDWORM_BOSS';

export type LockKind = 'SWORD' | 'SUN' | 'STAR';

export type FamiliarSlot = 0 | 1 | 2; // 0=Rowan (Wizard), 1=Bramblekit, 2=Lumina

export const TILE_SIZE = 16;
export const MAP_W = 44;
export const MAP_H = 28;
export const WORLD_W = 88;
export const WORLD_H = 56;
export const MAX_MAP_CELLS = WORLD_W * WORLD_H;

/* Milestone 4: 256x192 Full-Toroidal Evertale Globe + 64x48 Macro Atlas + 24x18 Live Stream Window */
export const GLOBE_W = 256;
export const GLOBE_H = 192;
export const ATLAS_W = 64;
export const ATLAS_H = 48;
export const VIEW_TILE_W = 24;
export const VIEW_TILE_H = 18;

export const VEHICLE_NAMES: readonly string[] = [
  'On Foot',
  'Cedar Raft',
  'Sloop Ship',
  'Hot Air Balloon',
  'Sky Airship',
  'Star Vessel',
];

export interface GlobeLandmark {
  tx: number;
  ty: number;
  regionId: number;
  enterable: boolean;
  name: string;
}

export const T_GRASS = 0;
export const T_GRASS_ALT = 1;
export const T_FLOWERS = 2;
export const T_TALL_GRASS = 3;
export const T_DIRT = 4;
export const T_COBBLE = 5;
export const T_WATER = 6;
export const T_FORD = 7;
export const T_BRIDGE = 8;
export const T_BRIDGE_RAIL_N = 9;
export const T_BRIDGE_RAIL_S = 10;
export const T_FENCE = 11;
export const T_TREE_TRUNK = 12;
export const T_BUSH = 13;
export const T_WALL_WOOD = 14;
export const T_WALL_WINDOW = 15;
export const T_WALL_DOOR = 16;
export const T_ROOF_BODY = 17;
export const T_ROOF_TOP = 18;
export const T_WINDMILL_WALL = 19;
export const T_WINDMILL_DOOR = 20;
export const T_WINDMILL_ROOF = 21;
export const T_SHRINE_FLOOR = 22;
export const T_SHRINE_RUNE = 23;
export const T_SHRINE_PILLAR = 24;
export const T_SIGNPOST = 25;
export const T_LANTERN_POST = 26;
export const T_FLOWER_BED = 27;
export const T_MTN_SKY = 28;
export const T_CLIFF_WALL = 29;
export const T_CLIFF_LEDGE = 30;
export const T_BOULDER = 31;
export const T_AUTUMN_GRASS = 32;
export const T_ROCKY_PATH = 33;
export const T_RAPIDS = 34;
export const T_RAIL_TRACK = 35;
export const T_MAPLE_TRUNK = 36;
export const T_PINE_TRUNK = 37;
export const T_WOOD_FLOOR = 38;
export const T_HEARTH_RUG = 39;
export const T_FIREPLACE = 40;
export const T_BED_QUILT = 41;
export const T_ATTIC_CHEST = 42;
export const T_HOLLOW_OAK = 43;
export const T_STONE_STAIRS = 44;
export const T_INTERIOR_WALL = 45;
export const T_WORLD_OCEAN = 46;
export const T_WORLD_BEACH = 47;
export const T_WORLD_PEAK = 48;
export const T_WORLD_FOREST = 49;
export const T_WORLD_HILL = 50;
export const T_WORLD_CASTLE = 51;
export const T_WORLD_TOWN = 52;
export const T_WORLD_RUINS = 53;
export const T_WORLD_SHALLOW = 54;
export const T_WORLD_SAND = 55;
export const T_WORLD_SNOW = 56;
export const T_WORLD_LAVA = 57;
export const T_WORLD_JUNGLE = 58;
export const T_WORLD_CORAL = 59;
export const T_WORLD_TECH = 60;
export const T_PALM_TREE = 61;
export const T_DOCK_PIER = 62;
export const T_LANTERN_UNLIT = 63;

export const O_NONE = 0;
export const O_CANOPY_TL = 1;
export const O_CANOPY_TR = 2;
export const O_CANOPY_BL = 3;
export const O_CANOPY_BR = 4;
export const O_ROOF_EAVE = 5;
export const O_SHRINE_ARCH = 6;
export const O_MAPLE_TL = 7;
export const O_MAPLE_TR = 8;
export const O_PINE_TOP = 9;
export const O_OAK_CROWN = 10;
export const O_MAPLE_MID = 11;
export const O_MAPLE_TOP = 12;

export const COL_WALKABLE = 0;
export const COL_SOLID = 1;
export const COL_TALL_GRASS = 2;
export const COL_SHALLOW = 3;
export const COL_WATER = 4;
export const COL_LAVA = 5;

export interface WorldMapData {
  width: number;
  height: number;
  bg1: Uint8Array;
  bg2: Uint8Array;
  col: Uint8Array;
}

export interface CutsceneOpExport {
  opcode: number;
  a0: number;
  a1: number;
  a2: number;
  speaker: string;
  text: string;
}

export type HeartAura = 'NONE' | 'RADIANT_COURAGE' | 'HOLLOW_FEAR' | 'MENDED';

export enum SfxId {
  NONE = 0,
  STEP = 1,
  GRASS_RUSTLE = 2,
  WATER_SPLASH = 3,
  DASH = 4,
  CURSOR = 5,
  CONFIRM = 6,
  CANCEL = 7,
  TEXT_HERO = 8,
  TEXT_PIP = 9,
  TEXT_ELDER = 10,
  EMOTE = 11,
  ENEMY_ALERT = 12,
  BATTLE_SWOOP = 13,
  SUMMON_ARC = 14,
  HIT_NORMAL = 15,
  HIT_CRITICAL = 16,
  PARRY_CLANG = 17,
  LOCK_BREAK = 18,
  LOCK_ALL_BREAK = 19,
  SPELL_CAST = 20,
  HEAL = 21,
  HEART_TAKE = 22,
  HEART_GIVE = 23,
  CHEST_OPEN = 24,
  VICTORY_FANFARE = 25,
  HURT = 26,
  COIN = 27,
  FISH_BITE = 28,
  FISH_REEL = 29,
  FISH_CATCH = 30,
  PORTAL = 31,
}

export type SpriteId =
  | 'hero'
  | 'pip'
  | 'familiar_bramble'
  | 'familiar_lumina'
  | 'npc_elder'
  | 'npc_clara'
  | 'npc_guard'
  | 'enemy_gloomcap'
  | 'enemy_thornling'
  | 'enemy_dusk_moth'
  | 'boss_briar_warden'
  | 'windmill_blades'
  | 'chest'
  | 'waystone'
  | 'emote_bang'
  | 'emote_heart'
  | 'emote_question'
  | 'emote_spark'
  | 'heart_orb'
  | 'chest_open'
  | 'npc_mouse'
  | 'npc_pig'
  | 'npc_shadow_gran'
  | 'pet_milo'
  | 'enemy_rat'
  | 'enemy_slime'
  | 'enemy_boar'
  | 'boss_muckmaw'
  | 'boss_ironfang'
  | 'hollow_oak'
  | 'bobber'
  | 'npc_townsfolk'
  | 'npc_mara'
  | 'npc_barnaby'
  | 'npc_maple'
  | 'npc_jasper'
  | 'locomotive'
  | 'npc_tessa'
  | 'npc_corin'
  | 'npc_sentinel'
  | 'raft'
  | 'enemy_scorpion'
  | 'enemy_sandworm'
  | 'fauna_camel'
  | 'fauna_penguin'
  | 'fauna_monkey'
  | 'fauna_robot'
  | 'npc_shipwright'
  | 'babana_stall';

export type PortraitId =
  | 'hero'
  | 'pip'
  | 'elder'
  | 'clara'
  | 'guard'
  | 'mouse'
  | 'pig'
  | 'shadow_gran'
  | 'none';

export interface OamSprite {
  x: number; // Screen-space integer pixel X (anchor center-bottom or top-left per sprite)
  y: number;
  spriteId: SpriteId;
  frame: number;
  dir: Direction;
  flipX: boolean;
  scaleXFp: number; // 256 = 1.0x (for squash & stretch!)
  scaleYFp: number;
  flashWhite: boolean;
  tintHex: string; // Optional palette tint or ''
  shadow: boolean;
  aura: HeartAura;
  sortY: number;
  layer: 0 | 1 | 2; // 0=below actors, 1=Y-sorted world, 2=UI/overlay/effects
  rotationDeg: number; // Used for windmill blades or spinning projectiles
}

export type ParticleKind =
  | 'DUST'
  | 'GRASS'
  | 'RIPPLE'
  | 'SPARK_GOLD'
  | 'SPARK_CYAN'
  | 'SPARK_RED'
  | 'SHATTER'
  | 'HEART_MOTE'
  | 'SOUL_ARC'
  | 'HEAL_MOTE'
  | 'POLLEN';

export interface PpuParticle {
  active: boolean;
  kind: ParticleKind;
  fx: number; // Fixed-point 16.8 world/screen X
  fy: number;
  vx: number;
  vy: number;
  life: number;
  maxLife: number;
  size: number;
  color: string;
  screenSpace: boolean;
}

export interface FloatingText {
  active: boolean;
  x: number;
  y: number;
  vy: number;
  text: string;
  color: string;
  scale: number;
  life: number;
  maxLife: number;
}

export interface EngineTuning {
  ppuMode: PpuMode;
  walkSpeedFp: number; // default 360 (~1.4 px/frame)
  dashSpeedFp: number; // default 580 (~2.26 px/frame)
  accelFp: number; // default 110
  followDistPx: number; // default 20
  hitstopNormalFrames: number; // default 3
  hitstopCritFrames: number; // default 7
  hitstopParryFrames: number; // default 5
  timedHitWindowFrames: number; // default 8 (half-width around impact peak)
  parryWindowFrames: number; // default 8
  showHitboxes: boolean;
}

export const DEFAULT_TUNING: EngineTuning = {
  ppuMode: 'GENESIS_320x224',
  walkSpeedFp: 384, // 1.5 px/frame at 60Hz
  dashSpeedFp: 614, // 2.4 px/frame
  accelFp: 128,
  followDistPx: 22,
  hitstopNormalFrames: 4,
  hitstopCritFrames: 8,
  hitstopParryFrames: 6,
  timedHitWindowFrames: 15,
  parryWindowFrames: 16,
  showHitboxes: false,
};

export interface DialogueChoice {
  label: string;
  actionId: string;
}

export interface DialogueState {
  active: boolean;
  speaker: string;
  portrait: PortraitId;
  portraitId: number; // 0=None, 1=Rowan, 2=Mara, 3=Pip, 4=Barnaby, 5=Maple, 6=Jasper
  fullText: string;
  visibleChars: number;
  finishedTyping: boolean;
  choices: DialogueChoice[];
  selectedChoice: number;
  sfxVoice: SfxId;
}

export interface PartyMemberState {
  slot: FamiliarSlot;
  id: string;
  name: string;
  role: string;
  sign: 'SUN' | 'STAR' | 'HEART';
  spriteId: SpriteId;
  hp: number;
  maxHp: number;
  mp: number;
  maxMp: number;
  atk: number;
  def: number;
  skillName: string;
  skillDesc: string;
  skillCost: number;
  skillKind: LockKind | 'HEAL';
  basicKind: LockKind;
  basicName: string;
}

export interface BattleEnemyState {
  id: string;
  fieldEntityId: string;
  name: string;
  spriteId: SpriteId;
  isBoss: boolean;
  hp: number;
  maxHp: number;
  atk: number;
  def: number;
  homeX: number;
  homeY: number;
  x: number;
  y: number;
  scaleXFp: number;
  scaleYFp: number;
  flashFrames: number;
  hurtFrames: number;
  staggered: boolean;
  chargeTurns: number; // 0 = normal attack, >0 = charging special with locks!
  maxChargeTurns: number;
  specialName: string;
  locks: LockKind[];
  brokenLocks: boolean[];
  alive: boolean;
  animFrame: number;
}

export type BattlePhase =
  | 'INTRO'
  | 'COMMAND'
  | 'SWAP_ARC'
  | 'PLAYER_WINDUP'
  | 'PLAYER_LUNGE'
  | 'PLAYER_IMPACT'
  | 'PLAYER_FOLLOWUP'
  | 'PLAYER_RETURN'
  | 'ENEMY_SELECT'
  | 'ENEMY_TELEGRAPH'
  | 'ENEMY_LUNGE'
  | 'ENEMY_IMPACT'
  | 'ENEMY_RETURN'
  | 'VICTORY_POSE'
  | 'DEFEAT_POSE';

export type BattleCommand = 'STRIKE' | 'SKILL' | 'GUARD' | 'ITEM';

export interface TimedRingVisual {
  active: boolean;
  kind: 'OFFENSE' | 'PARRY';
  x: number;
  y: number;
  progress: number; // 0..1 (1.0 = exact peak, sweet spot near 0.80..1.05)
  sweetStart: number;
  sweetEnd: number;
  result: 'NONE' | 'PERFECT' | 'GOOD' | 'MISS';
}

export type BattleVfxKind =
  | 'NONE'
  | 'STAFF_ARC'
  | 'ACORN_SPIN'
  | 'SOLAR_METEOR'
  | 'STAR_BOLT'
  | 'PRISM_NOVA'
  | 'ENEMY_CLAW'
  | 'GUARD_SHIELD'
  | 'HEAL_BLOOM';

export interface BattleVfxState {
  kind: BattleVfxKind;
  timer: number;
  dur: number;
  x: number;
  y: number;
  tx: number;
  ty: number;
}

export interface IntroState {
  scriptId: number;
  beat: number;
  beatCount: number;
  scene: number;
  actNum: number;
  camMode: number;
  fxMode: number;
  autoPlay: boolean;
  plateTimer: number;
  fadeDir: number;
  timer: number;
  chars: number;
  title: string;
  speaker: string;
  line1: string;
  line2: string;
}

export interface BattleRenderState {
  active: boolean;
  biome: BattleBiome;
  phase: BattlePhase;
  activeSlot: FamiliarSlot;
  previousSlot: FamiliarSlot;
  swapArcProgress: number; // 0..1
  heroX: number;
  heroY: number;
  heroScaleXFp: number;
  heroScaleYFp: number;
  heroFlashFrames: number;
  heroGuarding: boolean;
  wizardBackX: number;
  wizardBackY: number;
  selectedCommand: number; // 0..3
  selectedEnemy: number;
  enemies: BattleEnemyState[];
  timedRing: TimedRingVisual;
  vfx: BattleVfxState;
  bannerText: string;
  bannerFrames: number;
  comboCount: number;
  parryCount: number;
  locksBrokenCount: number;
  expEarned: number;
  potions: number;
  introProgress: number; // 0..1
}

export interface HeartLocketState {
  hasCourage: boolean;
  guardMended: boolean;
  chestOpened: boolean;
  elderCutsceneSeen: boolean;
  bossDefeated: boolean;
}

export interface InteractPrompt {
  active: boolean;
  x: number;
  y: number;
  label: string;
}

export interface DebugBox {
  x: number;
  y: number;
  w: number;
  h: number;
  color: string;
}

export interface ShopItemInfo {
  name: string;
  desc: string;
  price: number;
}

export interface FishSpeciesInfo {
  name: string;
  sellPrice: number;
}

export interface ShopSnapshot {
  active: boolean;
  shopId: number;
  tab: number; // 0=Buy, 1=Sell Fish
  cursor: number;
  msgTimer: number;
  title: string;
  msg: string;
}

export interface FishingSnapshot {
  active: boolean;
  phase: number; // 0=cast, 1=wait, 2=bite, 3=reeling, 4=caught, 5=escaped
  bobberX: number;
  bobberY: number;
  fishPos: number;
  barPos: number;
  barHalfW: number;
  progress: number;
  tension: number;
  species: number;
  weightOz: number;
}

export interface PpuFramePacket {
  width: number;
  height: number;
  frameCount: number;
  mode: GameMode;
  zoneId: ZoneId;
  mapW: number;
  mapH: number;
  globeW: number;
  globeH: number;
  isWorldMap: boolean;
  worldRegionId: number;
  worldRegionName: string;
  activeVehicle: number;
  viewOriginX: number;
  viewOriginY: number;
  viewBg1: Uint8Array;
  viewBg2: Uint8Array;
  landmarks: readonly GlobeLandmark[];
  terrainPalId: number;
  bgmId: BgmTrackId;
  zoneIsDark: boolean;
  zoneBannerFrames: number;
  zoneName: string;
  timeOfDay: TimeOfDay;
  todPhase: number; // 0..1023 smooth real-time day/night progression
  hasMountainVista: boolean;
  hasOutdoorTod: boolean;
  objectiveText: string;
  heroWorldX: number;
  heroWorldY: number;
  camX: number;
  camY: number;
  shakeX: number;
  shakeY: number;
  fadeLevel: number; // 0 = normal, 15 = solid black
  flashAlpha: number; // 0..1 white impact flash
  hitstopActive: boolean;
  sprites: OamSprite[];
  oamCount: number;
  oamLimit: number;
  particles: PpuParticle[];
  floatingTexts: FloatingText[];
  interactPrompt: InteractPrompt;
  intro: IntroState;
  battle: BattleRenderState;
  fishing: FishingSnapshot;
  debugBoxes: DebugBox[];
  sfxQueue: SfxId[];
}

export interface UiSnapshot {
  mode: GameMode;
  zoneId: ZoneId;
  zoneLabel: string;
  timeOfDay: TimeOfDay;
  timeOfDayLabel: string;
  ch1Step: number;
  ch2Step: number;
  isWorldMap: boolean;
  worldRegionId: number;
  worldRegionName: string;
  regionNames: readonly string[];
  activeVehicle: number;
  vehicleUnlockedMask: number;
  atlasOpen: boolean;
  designerModeEnabled: boolean;
  designerSandbox: boolean;
  babanas: number;
  planksFoundMask: number;
  heroTx: number;
  heroTy: number;
  globeW: number;
  globeH: number;
  atlasCells: Uint8Array;
  landmarks: readonly GlobeLandmark[];
  hasMiloBell: boolean;
  hasFamiliar: boolean;
  hasHeartVessel: boolean;
  storyStage: number;
  objectiveLabel: string;
  intro: IntroState;
  fadeLevel: number;
  flashAlpha: number;
  dialogue: DialogueState;
  party: PartyMemberState[];
  activeSlot: FamiliarSlot;
  potions: number;
  guilders: number;
  starDrops: number;
  snacks: number;
  lureTier: number;
  fishBag: number[];
  fishTotalCaught: number;
  hasWand: boolean;
  hasSewerKey: boolean;
  shop: ShopSnapshot;
  fishing: FishingSnapshot;
  shopCatalog: readonly ShopItemInfo[];
  fishCatalog: readonly FishSpeciesInfo[];
  locket: HeartLocketState;
  battlePhase: BattlePhase;
  battleSelectedCommand: number;
  battleSelectedEnemy: number;
  battleBanner: string;
  battleEnemies: {
    id: string;
    name: string;
    hp: number;
    maxHp: number;
    alive: boolean;
    isBoss: boolean;
    staggered: boolean;
    locks: LockKind[];
    brokenLocks: boolean[];
  }[];
  battleStats: {
    combos: number;
    parries: number;
    locksBroken: number;
    expEarned: number;
    guildersEarned: number;
  };
  timedRingActive: boolean;
  timedRingKind: 'OFFENSE' | 'PARRY';
  oamCount: number;
  oamLimit: number;
  activeEnemyCount: number;
  tuning: EngineTuning;
  playerTileX: number;
  playerTileY: number;
}
