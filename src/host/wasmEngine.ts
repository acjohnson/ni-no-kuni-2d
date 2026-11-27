/**
 * Project Hearth — WebAssembly C99 Engine Host Adapter
 *
 * Loads public/hearth_core.wasm (compiled directly from engine/core/) so the
 * Studio Browser Preview runs the EXACT SAME C99 simulation, tilemaps, palettes,
 * combat physics, and cutscene bytecode as Linux, Windows, Genesis, and SNES.
 */

import {
  ATLAS_H,
  ATLAS_W,
  BattleBiome,
  BattleEnemyState,
  BattlePhase,
  BattleVfxKind,
  BgmTrackId,
  CutsceneOpExport,
  DebugBox,
  DialogueChoice,
  Direction,
  EncounterPreset,
  EngineTuning,
  FamiliarSlot,
  FishingSnapshot,
  FishSpeciesInfo,
  FloatingText,
  FP_ONE,
  fromFp,
  GameMode,
  GLOBE_H,
  GLOBE_W,
  GlobeLandmark,
  HeartAura,
  LockKind,
  MAP_H,
  MAP_W,
  MAX_MAP_CELLS,
  OamSprite,
  ParticleKind,
  PartyMemberState,
  PortraitId,
  PpuFramePacket,
  PpuMode,
  PpuParticle,
  SfxId,
  ShopItemInfo,
  ShopSnapshot,
  SpriteId,
  TILE_SIZE,
  TimeOfDay,
  UiSnapshot,
  VIEW_TILE_H,
  VIEW_TILE_W,
  WorldMapData,
  ZoneId,
} from '../core/types';

interface WasmExports {
  memory: WebAssembly.Memory;
  wasm_init(): void;
  wasm_step(padBits: number): void;
  wasm_start_story(): void;
  wasm_start_sandbox(): void;
  wasm_enter_zone(zoneId: number, tx: number, ty: number): void;
  wasm_toggle_atlas(): void;
  wasm_designer_cycle_vehicle(): void;
  wasm_designer_warp_region(regionId: number): void;
  wasm_start_cutscene_demo(): void;
  wasm_start_intro(): void;
  wasm_start_story_cg(scriptId: number): void;
  wasm_skip_intro(): void;
  wasm_toggle_cinema_auto(): void;
  wasm_start_quick_battle(preset: number): void;
  wasm_return_to_title(): void;
  wasm_heal_party(): void;
  wasm_respawn_enemies(): void;
  wasm_set_tuning(
    ppuMode: number,
    walkSpeedFp: number,
    dashSpeedFp: number,
    accelFp: number,
    followDistPx: number,
    hitstopNormalF: number,
    hitstopCritF: number,
    hitstopParryF: number,
    timedHitWindowF: number,
    parryWindowF: number,
    showHitboxes: number
  ): void;
  wasm_ui_battle_command(cmdIdx: number): void;
  wasm_ui_battle_swap(slot: number): void;
  wasm_ui_battle_select_enemy(idx: number): void;
  wasm_ui_timed_tap(): void;
  wasm_ui_select_choice(idx: number): void;
  wasm_ui_finish_battle_summary(rematch: number): void;
  wasm_ui_shop_tab(tab: number): void;
  wasm_ui_shop_select(idx: number): void;
  wasm_ui_shop_close(): void;
  wasm_get_scalars_ptr(): number;
  wasm_get_oam_ptr(): number;
  wasm_get_party_ptr(): number;
  wasm_get_enemies_ptr(): number;
  wasm_get_particles_ptr(): number;
  wasm_get_ftexts_ptr(): number;
  wasm_get_field_boxes_ptr(): number;
  wasm_get_sfx_count(): number;
  wasm_get_sfx_ptr(): number;
  wasm_clear_sfx(): void;
  wasm_get_bg1_ptr(): number;
  wasm_get_bg2_ptr(): number;
  wasm_get_col_ptr(): number;
  wasm_get_view_bg1_ptr(): number;
  wasm_get_view_bg2_ptr(): number;
  wasm_get_atlas_ptr(): number;
  wasm_get_landmark_count(): number;
  wasm_get_landmark_tx(i: number): number;
  wasm_get_landmark_ty(i: number): number;
  wasm_get_landmark_region(i: number): number;
  wasm_get_landmark_enterable(i: number): number;
  wasm_get_landmark_name(i: number): number;
  wasm_get_region_name(i: number): number;
  wasm_get_palettes_ptr(): number;
  wasm_get_zone_palettes_ptr(): number;
  wasm_get_shop_item_count(): number;
  wasm_get_shop_item_name(i: number): number;
  wasm_get_shop_item_desc(i: number): number;
  wasm_get_shop_item_price(i: number): number;
  wasm_get_fish_name(i: number): number;
  wasm_get_fish_price(i: number): number;
  wasm_get_cutscene_intro_count(): number;
  wasm_read_cutscene_intro_op(idx: number): number;
}

const MODE_MAP: readonly GameMode[] = [
  'TITLE',
  'EXPLORE',
  'CUTSCENE',
  'BATTLE',
  'BATTLE_VICTORY',
  'BATTLE_DEFEAT',
  'INTRO',
  'SHOP',
  'FISHING',
];

const BIOME_MAP: readonly BattleBiome[] = [
  'MEADOW',
  'SHRINE',
  'SEWER',
  'BOARHOLD',
  'DESERT',
  'PARADISE',
  'FOREST',
  'TOWN',
  'INTERIOR',
  'SEA',
  'FROST',
  'VOLCANO',
];

const BGM_MAP: readonly BgmTrackId[] = [
  'HAMLET',
  'ALDERBROOK',
  'WHISKERBURGH',
  'SEWERS',
  'BOARHOLD',
  'SHADOW',
  'BATTLE',
  'BOSS',
  'VICTORY',
  'CINEMA_WONDER',
  'CINEMA_LULLABY',
  'DESERT',
  'PARADISE',
  'FROST',
  'JUNGLE',
  'VOLCANO',
  'TECH',
];

const BVFX_MAP: readonly BattleVfxKind[] = [
  'NONE',
  'STAFF_ARC',
  'ACORN_SPIN',
  'SOLAR_METEOR',
  'STAR_BOLT',
  'PRISM_NOVA',
  'GUARD_SHIELD',
  'ENEMY_CLAW',
  'HEAL_BLOOM',
];

const SPRITE_MAP: readonly SpriteId[] = [
  'hero',
  'pip',
  'familiar_bramble',
  'familiar_lumina',
  'npc_elder',
  'npc_clara',
  'npc_guard',
  'enemy_gloomcap',
  'enemy_thornling',
  'enemy_dusk_moth',
  'boss_briar_warden',
  'windmill_blades',
  'waystone',
  'chest',
  'emote_bang',
  'emote_heart',
  'emote_question',
  'emote_spark',
  'heart_orb',
  'chest_open',
  'npc_mouse',
  'npc_pig',
  'npc_shadow_gran',
  'pet_milo',
  'enemy_rat',
  'enemy_slime',
  'enemy_boar',
  'boss_muckmaw',
  'boss_ironfang',
  'hollow_oak',
  'bobber',
  'npc_townsfolk',
  'npc_mara',
  'npc_barnaby',
  'npc_maple',
  'npc_jasper',
  'locomotive',
  'npc_tessa',
  'npc_corin',
  'npc_sentinel',
  'raft',
  'enemy_scorpion',
  'enemy_sandworm',
  'fauna_camel',
  'fauna_penguin',
  'fauna_monkey',
  'fauna_robot',
  'npc_shipwright',
  'babana_stall',
];

const TOD_NAMES: readonly string[] = ['Morning', 'Afternoon', 'Dusk', 'Moonlit Night'];

const AURA_MAP: readonly HeartAura[] = [
  'NONE',
  'RADIANT_COURAGE',
  'HOLLOW_FEAR',
  'MENDED',
];

const BPHASE_MAP: readonly BattlePhase[] = [
  'INTRO',
  'COMMAND',
  'SWAP_ARC',
  'PLAYER_WINDUP',
  'PLAYER_LUNGE',
  'PLAYER_IMPACT',
  'PLAYER_FOLLOWUP',
  'PLAYER_RETURN',
  'ENEMY_SELECT',
  'ENEMY_TELEGRAPH',
  'ENEMY_LUNGE',
  'ENEMY_IMPACT',
  'ENEMY_RETURN',
  'VICTORY_POSE',
  'DEFEAT_POSE',
];

const PARTICLE_MAP: readonly ParticleKind[] = [
  'DUST',
  'GRASS',
  'RIPPLE',
  'SPARK_GOLD',
  'SHATTER',
  'HEAL_MOTE',
];

const LOCK_MAP: readonly (LockKind | undefined)[] = [undefined, 'SWORD', 'SUN', 'STAR'];

const PARTY_META: readonly {
  id: string;
  role: string;
  sign: 'HEART' | 'SUN' | 'STAR';
  skillDesc: string;
}[] = [
  { id: 'rowan', role: 'Young Wizard', sign: 'HEART', skillDesc: 'Restore +38 HP to party' },
  { id: 'bramblekit', role: 'Acorn Knight', sign: 'SUN', skillDesc: 'Heavy SUN smash · breaks Sun locks' },
  { id: 'lumina', role: 'Starlight Wisp', sign: 'STAR', skillDesc: 'Hits ALL foes · breaks Star locks' },
];

const OAM_STRIDE = 12;
const PARTY_STRIDE = 16;
const ENEMY_STRIDE = 28;
const PARTICLE_STRIDE = 10;
const FTEXT_STRIDE = 6;
const FIELD_BOX_STRIDE = 3;

function portraitFromSpeaker(speaker: string): PortraitId {
  if (!speaker) return 'none';
  if (speaker.includes('Elder') || speaker.includes('Wren')) return 'elder';
  if (speaker.includes('Clara') || speaker.includes('Grandma')) {
    return speaker.includes('Shadow') ? 'shadow_gran' : 'clara';
  }
  if (speaker.includes('Guard') || speaker.includes('Bram')) return 'guard';
  if (speaker.includes('Pippo') || speaker.includes('Brie') || speaker.includes('Barnaby')) return 'mouse';
  if (speaker.includes('Tusk') || speaker.includes('Porkins') || speaker.includes('Ironfang')) return 'pig';
  if (speaker.includes('Shadow')) return 'shadow_gran';
  if (speaker.includes('Pip')) return 'pip';
  if (speaker.includes('Rowan')) return 'hero';
  return 'none';
}

function presetToCode(preset: EncounterPreset): number {
  switch (preset) {
    case 'GLADE_DUO':
      return 1;
    case 'SHRINE_BOSS':
      return 2;
    case 'SEWER_PACK':
      return 3;
    case 'SEWER_BOSS':
      return 4;
    case 'BOAR_GUARD':
      return 5;
    case 'IRONFANG_BOSS':
      return 6;
    case 'DESERT_PACK':
      return 7;
    case 'SANDWORM_BOSS':
      return 8;
    default:
      return 0;
  }
}

export class WasmHearthEngine {
  private readonly wasm: WasmExports;
  private readonly decoder = new TextDecoder('utf-8');
  public readonly palettes: readonly (readonly string[])[];
  public readonly zonePalettes: readonly (readonly string[])[];
  public readonly shopCatalog: readonly ShopItemInfo[];
  public readonly fishCatalog: readonly FishSpeciesInfo[];
  public readonly atlasCells: Uint8Array;
  public readonly landmarks: readonly GlobeLandmark[];
  public readonly regionNames: readonly string[];
  public readonly map: WorldMapData;

  private readonly scalarsIdx: number;
  private readonly oamIdx: number;
  private readonly partyIdx: number;
  private readonly enemiesIdx: number;
  private readonly particlesIdx: number;
  private readonly ftextsIdx: number;
  private readonly fieldBoxesIdx: number;
  private readonly sfxPtr: number;
  private readonly bg1Ptr: number;
  private readonly bg2Ptr: number;
  private readonly colPtr: number;
  private readonly viewBg1Ptr: number;
  private readonly viewBg2Ptr: number;
  private readonly viewBg1Buf = new Uint8Array(VIEW_TILE_W * VIEW_TILE_H);
  private readonly viewBg2Buf = new Uint8Array(VIEW_TILE_W * VIEW_TILE_H);
  private lastMapRevision = -1;

  private constructor(wasm: WasmExports) {
    this.wasm = wasm;
    this.wasm.wasm_init();

    this.scalarsIdx = (wasm.wasm_get_scalars_ptr() >>> 0) >> 2;
    this.oamIdx = (wasm.wasm_get_oam_ptr() >>> 0) >> 2;
    this.partyIdx = (wasm.wasm_get_party_ptr() >>> 0) >> 2;
    this.enemiesIdx = (wasm.wasm_get_enemies_ptr() >>> 0) >> 2;
    this.particlesIdx = (wasm.wasm_get_particles_ptr() >>> 0) >> 2;
    this.ftextsIdx = (wasm.wasm_get_ftexts_ptr() >>> 0) >> 2;
    this.fieldBoxesIdx = (wasm.wasm_get_field_boxes_ptr() >>> 0) >> 2;
    this.sfxPtr = wasm.wasm_get_sfx_ptr() >>> 0;
    this.bg1Ptr = wasm.wasm_get_bg1_ptr() >>> 0;
    this.bg2Ptr = wasm.wasm_get_bg2_ptr() >>> 0;
    this.colPtr = wasm.wasm_get_col_ptr() >>> 0;
    this.viewBg1Ptr = wasm.wasm_get_view_bg1_ptr() >>> 0;
    this.viewBg2Ptr = wasm.wasm_get_view_bg2_ptr() >>> 0;

    // Load exact 4 x 16-color palettes directly from C ROM table HEARTH_PALETTES_RGB
    const u32 = new Uint32Array(wasm.memory.buffer);
    const palBase = (wasm.wasm_get_palettes_ptr() >>> 0) >> 2;
    const loadedPalettes: string[][] = [];
    for (let p = 0; p < 4; p++) {
      const row: string[] = [];
      for (let c = 0; c < 16; c++) {
        const rgb = u32[palBase + p * 16 + c] & 0xffffff;
        row.push('#' + rgb.toString(16).toUpperCase().padStart(6, '0'));
      }
      loadedPalettes.push(row);
    }
    this.palettes = loadedPalettes;

    // Load all 18 x 16-color per-zone & per-continent terrain palettes from HEARTH_ZONE_PALETTES_RGB
    const zpalBase = (wasm.wasm_get_zone_palettes_ptr() >>> 0) >> 2;
    const loadedZonePals: string[][] = [];
    for (let z = 0; z < 18; z++) {
      const row: string[] = [];
      for (let c = 0; c < 16; c++) {
        const rgb = u32[zpalBase + z * 16 + c] & 0xffffff;
        row.push('#' + rgb.toString(16).toUpperCase().padStart(6, '0'));
      }
      loadedZonePals.push(row);
    }
    this.zonePalettes = loadedZonePals;

    // Load static Shop & Fish catalog metadata from C ROM tables
    const shopCount = wasm.wasm_get_shop_item_count();
    const shopItems: ShopItemInfo[] = [];
    for (let i = 0; i < shopCount; i++) {
      shopItems.push({
        name: this.readCString(wasm.wasm_get_shop_item_name(i)),
        desc: this.readCString(wasm.wasm_get_shop_item_desc(i)),
        price: wasm.wasm_get_shop_item_price(i),
      });
    }
    this.shopCatalog = shopItems;

    const fishItems: FishSpeciesInfo[] = [];
    for (let i = 0; i < 6; i++) {
      fishItems.push({
        name: this.readCString(wasm.wasm_get_fish_name(i)),
        sellPrice: wasm.wasm_get_fish_price(i),
      });
    }
    this.fishCatalog = fishItems;

    // Load 64x48 macro-biome atlas, 14 globe landmarks & 7 continent names from ROM
    const atlasPtr = wasm.wasm_get_atlas_ptr() >>> 0;
    const u8 = new Uint8Array(wasm.memory.buffer);
    this.atlasCells = new Uint8Array(ATLAS_W * ATLAS_H);
    this.atlasCells.set(u8.subarray(atlasPtr, atlasPtr + ATLAS_W * ATLAS_H));

    const lmCount = wasm.wasm_get_landmark_count();
    const loadedLandmarks: GlobeLandmark[] = [];
    for (let i = 0; i < lmCount; i++) {
      loadedLandmarks.push({
        tx: wasm.wasm_get_landmark_tx(i),
        ty: wasm.wasm_get_landmark_ty(i),
        regionId: wasm.wasm_get_landmark_region(i),
        enterable: wasm.wasm_get_landmark_enterable(i) !== 0,
        name: this.readCString(wasm.wasm_get_landmark_name(i)),
      });
    }
    this.landmarks = loadedLandmarks;

    const loadedRegions: string[] = [];
    for (let r = 0; r < 7; r++) {
      loadedRegions.push(this.readCString(wasm.wasm_get_region_name(r)) || `Region ${r + 1}`);
    }
    this.regionNames = loadedRegions;

    // Allocate tilemap buffers for local interior/town zones (44x28)
    this.map = {
      width: MAP_W,
      height: MAP_H,
      bg1: new Uint8Array(MAX_MAP_CELLS),
      bg2: new Uint8Array(MAX_MAP_CELLS),
      col: new Uint8Array(MAX_MAP_CELLS),
    };
    this.syncMapIfChanged();
  }

  private syncMapIfChanged(): void {
    const i32 = new Int32Array(this.wasm.memory.buffer);
    const rev = i32[this.scalarsIdx + 102];
    if (rev === this.lastMapRevision) return;
    this.lastMapRevision = rev;
    const mw = i32[this.scalarsIdx + 154] || MAP_W;
    const mh = i32[this.scalarsIdx + 155] || MAP_H;
    this.map.width = mw;
    this.map.height = mh;
    const u8 = new Uint8Array(this.wasm.memory.buffer);
    const cells = Math.min(MAX_MAP_CELLS, mw * mh);
    this.map.bg1.set(u8.subarray(this.bg1Ptr, this.bg1Ptr + cells));
    this.map.bg2.set(u8.subarray(this.bg2Ptr, this.bg2Ptr + cells));
    this.map.col.set(u8.subarray(this.colPtr, this.colPtr + cells));
  }

  public static async create(wasmUrl = '/hearth_core.wasm'): Promise<WasmHearthEngine> {
    let instance: WebAssembly.Instance;
    if (typeof WebAssembly.instantiateStreaming === 'function') {
      try {
        const res = await WebAssembly.instantiateStreaming(fetch(wasmUrl));
        instance = res.instance;
      } catch {
        const resp = await fetch(wasmUrl);
        const bytes = await resp.arrayBuffer();
        const res = await WebAssembly.instantiate(bytes);
        instance = res.instance;
      }
    } else {
      const resp = await fetch(wasmUrl);
      const bytes = await resp.arrayBuffer();
      const res = await WebAssembly.instantiate(bytes);
      instance = res.instance;
    }
    return new WasmHearthEngine(instance.exports as unknown as WasmExports);
  }

  private readCString(ptr: number): string {
    const p = ptr >>> 0;
    if (p === 0) return '';
    const u8 = new Uint8Array(this.wasm.memory.buffer);
    let end = p;
    while (end < u8.length && u8[end] !== 0) end++;
    return this.decoder.decode(u8.subarray(p, end));
  }

  private paletteColor(packedPalCol: number): string {
    const pal = (packedPalCol >> 8) & 3;
    const col = packedPalCol & 15;
    return this.palettes[pal]?.[col] ?? '#FFFFFF';
  }

  public consumePendingSfx(): SfxId[] {
    const count = this.wasm.wasm_get_sfx_count();
    if (count === 0) return [];
    const u8 = new Uint8Array(this.wasm.memory.buffer);
    const out: SfxId[] = [];
    for (let i = 0; i < count; i++) {
      out.push(u8[this.sfxPtr + i] as SfxId);
    }
    this.wasm.wasm_clear_sfx();
    return out;
  }

  public step(padBits: number): PpuFramePacket {
    this.wasm.wasm_step(padBits);
    this.syncMapIfChanged();
    return this.buildFramePacket();
  }

  public startStory(): void {
    this.wasm.wasm_start_story();
    this.syncMapIfChanged();
  }

  public startSandbox(): void {
    this.wasm.wasm_start_sandbox();
    this.syncMapIfChanged();
  }

  public enterZone(zoneId: ZoneId, tx: number, ty: number): void {
    this.wasm.wasm_enter_zone(zoneId, tx, ty);
    this.syncMapIfChanged();
  }

  public toggleAtlas(): void {
    this.wasm.wasm_toggle_atlas();
  }

  public designerCycleVehicle(): void {
    this.wasm.wasm_designer_cycle_vehicle();
  }

  public designerWarpRegion(regionId: number): void {
    this.wasm.wasm_designer_warp_region(regionId);
    this.syncMapIfChanged();
  }

  public startCutsceneDemo(): void {
    this.wasm.wasm_start_cutscene_demo();
    this.syncMapIfChanged();
  }

  public startIntro(): void {
    this.wasm.wasm_start_intro();
  }

  public startStoryCg(scriptId: number): void {
    this.wasm.wasm_start_story_cg(scriptId);
  }

  public skipIntro(): void {
    this.wasm.wasm_skip_intro();
    this.syncMapIfChanged();
  }

  public toggleCinemaAuto(): void {
    this.wasm.wasm_toggle_cinema_auto();
  }

  public startQuickBattle(preset: EncounterPreset = 'SHRINE_BOSS'): void {
    this.wasm.wasm_start_quick_battle(presetToCode(preset));
  }

  public returnToTitle(): void {
    this.wasm.wasm_return_to_title();
  }

  public healPartyFull(): void {
    this.wasm.wasm_heal_party();
  }

  public respawnFieldEnemies(): void {
    this.wasm.wasm_respawn_enemies();
  }

  public setTuning(partial: Partial<EngineTuning>): void {
    const cur = this.readTuning();
    const next: EngineTuning = { ...cur, ...partial };
    this.wasm.wasm_set_tuning(
      next.ppuMode === 'SNES_256x224' ? 1 : 0,
      next.walkSpeedFp,
      next.dashSpeedFp,
      next.accelFp,
      next.followDistPx,
      next.hitstopNormalFrames,
      next.hitstopCritFrames,
      next.hitstopParryFrames,
      next.timedHitWindowFrames,
      next.parryWindowFrames,
      next.showHitboxes ? 1 : 0
    );
  }

  public uiBattleCommand(cmdIndex: number): void {
    this.wasm.wasm_ui_battle_command(cmdIndex);
  }

  public uiBattleSelectEnemy(idx: number): void {
    this.wasm.wasm_ui_battle_select_enemy(idx);
  }

  public uiBattleSwapFamiliar(slot: FamiliarSlot): void {
    this.wasm.wasm_ui_battle_swap(slot);
  }

  public uiTriggerTimedTap(): void {
    this.wasm.wasm_ui_timed_tap();
  }

  public uiSelectDialogueChoice(idx: number): void {
    this.wasm.wasm_ui_select_choice(idx);
  }

  public uiFinishBattleSummary(rematch: boolean): void {
    this.wasm.wasm_ui_finish_battle_summary(rematch ? 1 : 0);
    this.syncMapIfChanged();
  }

  public uiShopTab(tab: number): void {
    this.wasm.wasm_ui_shop_tab(tab);
  }

  public uiShopSelect(idx: number): void {
    this.wasm.wasm_ui_shop_select(idx);
  }

  public uiShopClose(): void {
    this.wasm.wasm_ui_shop_close();
  }

  public getCutsceneIntroOps(): CutsceneOpExport[] {
    const count = this.wasm.wasm_get_cutscene_intro_count();
    const i32 = new Int32Array(this.wasm.memory.buffer);
    const out: CutsceneOpExport[] = [];
    for (let i = 0; i < count; i++) {
      const ptr = (this.wasm.wasm_read_cutscene_intro_op(i) >>> 0) >> 2;
      out.push({
        opcode: i32[ptr + 0],
        a0: i32[ptr + 1],
        a1: i32[ptr + 2],
        a2: i32[ptr + 3],
        speaker: this.readCString(i32[ptr + 4]),
        text: this.readCString(i32[ptr + 5]),
      });
    }
    return out;
  }

  private readTuning(): EngineTuning {
    const i32 = new Int32Array(this.wasm.memory.buffer);
    const s = this.scalarsIdx;
    const ppuMode: PpuMode = i32[s + 22] === 1 ? 'SNES_256x224' : 'GENESIS_320x224';
    return {
      ppuMode,
      walkSpeedFp: i32[s + 23],
      dashSpeedFp: i32[s + 24],
      accelFp: i32[s + 25],
      followDistPx: i32[s + 26],
      hitstopNormalFrames: i32[s + 27],
      hitstopCritFrames: i32[s + 28],
      hitstopParryFrames: i32[s + 29],
      timedHitWindowFrames: i32[s + 30],
      parryWindowFrames: i32[s + 31],
      showHitboxes: i32[s + 32] !== 0,
    };
  }

  private readParty(): PartyMemberState[] {
    const i32 = new Int32Array(this.wasm.memory.buffer);
    const party: PartyMemberState[] = [];
    for (let i = 0; i < 3; i++) {
      const p = this.partyIdx + i * PARTY_STRIDE;
      const slot = (i32[p + 0] as FamiliarSlot) ?? (i as FamiliarSlot);
      const meta = PARTY_META[slot] ?? PARTY_META[0];
      const skillKindRaw = i32[p + 12];
      const skillIsHeal = i32[p + 14] !== 0;
      const basicKind = LOCK_MAP[i32[p + 11]] ?? 'SWORD';
      const skillKind: LockKind | 'HEAL' = skillIsHeal
        ? 'HEAL'
        : (LOCK_MAP[skillKindRaw] ?? 'SUN');

      party.push({
        slot,
        id: meta.id,
        name: this.readCString(i32[p + 1]),
        role: meta.role,
        sign: meta.sign,
        spriteId: SPRITE_MAP[i32[p + 4]] ?? 'hero',
        hp: i32[p + 5],
        maxHp: i32[p + 6],
        mp: i32[p + 7],
        maxMp: i32[p + 8],
        atk: i32[p + 9],
        def: i32[p + 10],
        basicKind,
        basicName: this.readCString(i32[p + 2]),
        skillName: this.readCString(i32[p + 3]),
        skillDesc: meta.skillDesc,
        skillCost: i32[p + 13],
        skillKind,
      });
    }
    return party;
  }

  private readBattleEnemies(): BattleEnemyState[] {
    const i32 = new Int32Array(this.wasm.memory.buffer);
    const count = i32[this.scalarsIdx + 64];
    const enemies: BattleEnemyState[] = [];
    for (let i = 0; i < count; i++) {
      const e = this.enemiesIdx + i * ENEMY_STRIDE;
      const lockCount = i32[e + 20];
      const locks: LockKind[] = [];
      const brokenLocks: boolean[] = [];
      for (let li = 0; li < lockCount; li++) {
        const lk = LOCK_MAP[i32[e + 21 + li]];
        if (lk) {
          locks.push(lk);
          brokenLocks.push(i32[e + 24 + li] !== 0);
        }
      }
      enemies.push({
        id: `enemy_${i}`,
        fieldEntityId: '',
        name: this.readCString(i32[e + 0]),
        specialName: this.readCString(i32[e + 1]),
        spriteId: SPRITE_MAP[i32[e + 2]] ?? 'enemy_gloomcap',
        isBoss: i32[e + 3] !== 0,
        alive: i32[e + 4] !== 0,
        staggered: i32[e + 5] !== 0,
        hp: i32[e + 6],
        maxHp: i32[e + 7],
        atk: i32[e + 8],
        def: i32[e + 9],
        homeX: i32[e + 10],
        homeY: i32[e + 11],
        x: i32[e + 12],
        y: i32[e + 13],
        scaleXFp: i32[e + 14],
        scaleYFp: i32[e + 15],
        flashFrames: i32[e + 16],
        hurtFrames: i32[e + 17],
        chargeTurns: i32[e + 18],
        maxChargeTurns: i32[e + 19],
        locks,
        brokenLocks,
        animFrame: i32[e + 27],
      });
    }
    return enemies;
  }

  private buildFramePacket(): PpuFramePacket {
    const i32 = new Int32Array(this.wasm.memory.buffer);
    const s = this.scalarsIdx;

    const width = i32[s + 0];
    const height = i32[s + 1];
    const frameCount = i32[s + 2] >>> 0;
    const mode = MODE_MAP[i32[s + 3]] ?? 'TITLE';
    const camX = i32[s + 4];
    const camY = i32[s + 5];
    const oamCount = i32[s + 10];
    const oamLimit = i32[s + 11];

    const sprites: OamSprite[] = [];
    for (let i = 0; i < oamCount; i++) {
      const o = this.oamIdx + i * OAM_STRIDE;
      const flags = i32[o + 5];
      sprites.push({
        x: i32[o + 0],
        y: i32[o + 1],
        spriteId: SPRITE_MAP[i32[o + 2]] ?? 'hero',
        frame: i32[o + 3],
        dir: (i32[o + 4] & 3) as Direction,
        flipX: (flags & 1) !== 0,
        flashWhite: (flags & 2) !== 0,
        shadow: (flags & 4) !== 0,
        scaleXFp: i32[o + 6],
        scaleYFp: i32[o + 7],
        aura: AURA_MAP[i32[o + 8]] ?? 'NONE',
        layer: (Math.min(2, Math.max(0, i32[o + 9])) as 0 | 1 | 2),
        sortY: i32[o + 10],
        rotationDeg: i32[o + 11],
        tintHex: '',
      });
    }

    const pCount = i32[s + 79];
    const particles: PpuParticle[] = [];
    for (let i = 0; i < pCount; i++) {
      const p = this.particlesIdx + i * PARTICLE_STRIDE;
      particles.push({
        active: true,
        kind: PARTICLE_MAP[i32[p + 0]] ?? 'DUST',
        fx: i32[p + 1],
        fy: i32[p + 2],
        vx: i32[p + 3],
        vy: i32[p + 4],
        life: i32[p + 5],
        maxLife: i32[p + 6],
        size: i32[p + 7],
        color: this.paletteColor(i32[p + 8]),
        screenSpace: i32[p + 9] !== 0,
      });
    }

    const fCount = i32[s + 80];
    const floatingTexts: FloatingText[] = [];
    for (let i = 0; i < fCount; i++) {
      const ft = this.ftextsIdx + i * FTEXT_STRIDE;
      floatingTexts.push({
        active: true,
        x: i32[ft + 0],
        y: i32[ft + 1],
        vy: 0,
        text: this.readCString(i32[ft + 2]),
        color: this.paletteColor(i32[ft + 3]),
        scale: 1,
        life: i32[ft + 4],
        maxLife: 44,
      });
    }

    const debugBoxes: DebugBox[] = [];
    if (
      i32[s + 32] !== 0 &&
      mode !== 'BATTLE' &&
      mode !== 'BATTLE_VICTORY' &&
      mode !== 'BATTLE_DEFEAT'
    ) {
      const hx = fromFp(i32[s + 18]);
      const hy = fromFp(i32[s + 19]);
      debugBoxes.push({ x: hx - 5 - camX, y: hy - 3 - camY, w: 10, h: 6, color: '#5AD2F4' });
      for (let i = 0; i < 4; i++) {
        const fb = this.fieldBoxesIdx + i * FIELD_BOX_STRIDE;
        if (i32[fb + 0] !== 0) {
          debugBoxes.push({
            x: i32[fb + 1] - 8 - camX,
            y: i32[fb + 2] - 8 - camY,
            w: 16,
            h: 16,
            color: '#E04E4E',
          });
        }
      }
    }

    const phase = BPHASE_MAP[i32[s + 49]] ?? 'COMMAND';
    const phaseTimer = i32[s + 50];
    const ringResRaw = i32[s + 72];
    const enemies = this.readBattleEnemies();

    const readFishing = (): FishingSnapshot => ({
      active: i32[s + 121] !== 0,
      phase: i32[s + 122],
      bobberX: i32[s + 123],
      bobberY: i32[s + 124],
      fishPos: i32[s + 125],
      barPos: i32[s + 126],
      barHalfW: i32[s + 127],
      progress: i32[s + 128],
      tension: i32[s + 129],
      species: i32[s + 130],
      weightOz: i32[s + 131],
    });

    const isWorldMap = i32[s + 156] !== 0;
    const worldRegionId = i32[s + 160];
    const worldRegionName = this.regionNames[worldRegionId] ?? 'Emerald Heartland';
    if (isWorldMap) {
      const u8 = new Uint8Array(this.wasm.memory.buffer);
      const len = VIEW_TILE_W * VIEW_TILE_H;
      this.viewBg1Buf.set(u8.subarray(this.viewBg1Ptr, this.viewBg1Ptr + len));
      this.viewBg2Buf.set(u8.subarray(this.viewBg2Ptr, this.viewBg2Ptr + len));
    }

    return {
      width,
      height,
      frameCount,
      mode,
      zoneId: (i32[s + 95] as ZoneId) ?? 1,
      mapW: i32[s + 154] || MAP_W,
      mapH: i32[s + 155] || MAP_H,
      globeW: i32[s + 172] || GLOBE_W,
      globeH: i32[s + 173] || GLOBE_H,
      isWorldMap,
      worldRegionId,
      worldRegionName,
      activeVehicle: i32[s + 161],
      viewOriginX: i32[s + 166],
      viewOriginY: i32[s + 167],
      viewBg1: this.viewBg1Buf,
      viewBg2: this.viewBg2Buf,
      landmarks: this.landmarks,
      terrainPalId: i32[s + 96],
      bgmId: BGM_MAP[i32[s + 97]] ?? 'HAMLET',
      zoneIsDark: i32[s + 98] !== 0,
      zoneBannerFrames: i32[s + 99],
      zoneName: this.readCString(i32[s + 100]) || 'Alderbrook',
      timeOfDay: (Math.min(3, Math.max(0, i32[s + 147])) as TimeOfDay),
      todPhase: i32[s + 174] & 1023,
      hasMountainVista: i32[s + 148] !== 0,
      objectiveText: this.readCString(i32[s + 149]),
      heroWorldX: fromFp(i32[s + 18]),
      heroWorldY: fromFp(i32[s + 19]),
      camX,
      camY,
      shakeX: i32[s + 6],
      shakeY: i32[s + 7],
      fadeLevel: i32[s + 81],
      flashAlpha: Math.min(0.65, i32[s + 8] / 12),
      hitstopActive: i32[s + 9] !== 0,
      sprites,
      oamCount,
      oamLimit,
      particles,
      floatingTexts,
      interactPrompt: {
        active: i32[s + 12] !== 0,
        x: i32[s + 13],
        y: i32[s + 14],
        label: this.readCString(i32[s + 15]),
      },
      intro: {
        scriptId: i32[s + 145],
        beat: i32[s + 136],
        beatCount: Math.max(1, i32[s + 137]),
        scene: i32[s + 82],
        actNum: Math.max(1, i32[s + 138]),
        camMode: i32[s + 139],
        fxMode: i32[s + 140],
        autoPlay: i32[s + 141] !== 0,
        plateTimer: i32[s + 142],
        fadeDir: i32[s + 144],
        timer: i32[s + 83],
        chars: i32[s + 84],
        title: this.readCString(i32[s + 85]),
        speaker: this.readCString(i32[s + 143]),
        line1: this.readCString(i32[s + 86]),
        line2: this.readCString(i32[s + 87]),
      },
      battle: {
        active: i32[s + 47] !== 0,
        biome: BIOME_MAP[i32[s + 48]] ?? 'MEADOW',
        phase,
        activeSlot: (i32[s + 51] as FamiliarSlot) ?? 1,
        previousSlot: (i32[s + 52] as FamiliarSlot) ?? 1,
        swapArcProgress: i32[s + 53] / FP_ONE,
        heroX: i32[s + 54],
        heroY: i32[s + 55],
        heroScaleXFp: i32[s + 56],
        heroScaleYFp: i32[s + 57],
        heroFlashFrames: i32[s + 58],
        heroGuarding: i32[s + 59] !== 0,
        wizardBackX: i32[s + 60],
        wizardBackY: i32[s + 61],
        selectedCommand: i32[s + 62],
        selectedEnemy: i32[s + 63],
        enemies,
        timedRing: {
          active: i32[s + 66] !== 0,
          kind: i32[s + 67] !== 0 ? 'PARRY' : 'OFFENSE',
          x: i32[s + 68],
          y: i32[s + 69],
          progress: i32[s + 70] / FP_ONE,
          sweetStart: i32[s + 71] / FP_ONE,
          sweetEnd: 1.08,
          result: ringResRaw === 1 ? 'PERFECT' : ringResRaw === 2 ? 'GOOD' : 'NONE',
        },
        vfx: {
          kind: BVFX_MAP[i32[s + 88]] ?? 'NONE',
          timer: i32[s + 89],
          dur: i32[s + 90],
          x: i32[s + 91],
          y: i32[s + 92],
          tx: i32[s + 93],
          ty: i32[s + 94],
        },
        bannerText: this.readCString(i32[s + 73]),
        bannerFrames: i32[s + 74],
        comboCount: i32[s + 75],
        parryCount: i32[s + 76],
        locksBrokenCount: i32[s + 77],
        expEarned: i32[s + 78],
        potions: i32[s + 17],
        introProgress: phase === 'INTRO' ? Math.min(1, phaseTimer / 24) : 1,
      },
      fishing: readFishing(),
      debugBoxes,
      sfxQueue: this.consumePendingSfx(),
    };
  }

  public getUiSnapshot(): UiSnapshot {
    this.syncMapIfChanged();
    const i32 = new Int32Array(this.wasm.memory.buffer);
    const s = this.scalarsIdx;

    const tx = Math.floor(fromFp(i32[s + 18]) / TILE_SIZE);
    const ty = Math.floor(fromFp(i32[s + 19]) / TILE_SIZE);
    const locketMask = i32[s + 20];
    const hasCourage = (locketMask & 1) !== 0;
    const guardMended = (locketMask & 2) !== 0;
    const chestOpened = (locketMask & 4) !== 0;
    const elderCutsceneSeen = (locketMask & 8) !== 0;
    const bossDefeated = (locketMask & 16) !== 0;

    const zoneId = (i32[s + 95] as ZoneId) ?? 1;
    const storyStage = i32[s + 101];
    const zoneLabel = this.readCString(i32[s + 100]) || 'Alderbrook';
    const hasWand = i32[s + 134] !== 0;
    const hasSewerKey = i32[s + 135] !== 0;
    const timeOfDay = (Math.min(3, Math.max(0, i32[s + 147])) as TimeOfDay);
    const timeOfDayLabel = TOD_NAMES[timeOfDay] ?? 'Morning';
    const ch1Step = i32[s + 150];
    const hasMiloBell = i32[s + 153] !== 0;
    const isWorldMap = i32[s + 156] !== 0;
    const ch2Step = i32[s + 157];
    const hasFamiliar = i32[s + 158] !== 0;
    const hasHeartVessel = i32[s + 159] !== 0;
    const worldRegionId = i32[s + 160];
    const worldRegionName = this.regionNames[worldRegionId] ?? 'Emerald Heartland';
    const activeVehicle = i32[s + 161];
    const vehicleUnlockedMask = i32[s + 162];
    const atlasOpen = i32[s + 163] !== 0;
    const designerModeEnabled = i32[s + 164] !== 0;
    const designerSandbox = i32[s + 165] !== 0;
    const babanas = i32[s + 168];
    const planksFoundMask = i32[s + 169];
    const heroTx = i32[s + 170];
    const heroTy = i32[s + 171];
    const globeW = i32[s + 172] || GLOBE_W;
    const globeH = i32[s + 173] || GLOBE_H;
    const coreObjective = this.readCString(i32[s + 149]);

    const objectiveLabel =
      coreObjective ||
      (storyStage === 2
        ? 'Travel north to Whiskerburgh & speak to Mayor Brie'
        : storyStage === 3
          ? 'Enter the Sunken Sewers & defeat King Muckmaw'
          : storyStage === 4
            ? 'Ascend to Boarhold Citadel & challenge Lord Ironfang'
            : 'Explore Evertale, fish at piers, or visit shops');

    const speaker = this.readCString(i32[s + 34]);
    const choiceCount = i32[s + 38];
    const choices: DialogueChoice[] = [];
    for (let i = 0; i < choiceCount; i++) {
      choices.push({
        label: this.readCString(i32[s + 41 + i]),
        actionId: String(i32[s + 44 + i]),
      });
    }

    const enemies = this.readBattleEnemies();

    const shop: ShopSnapshot = {
      active: i32[s + 114] !== 0,
      shopId: i32[s + 115],
      tab: i32[s + 116],
      cursor: i32[s + 117],
      msgTimer: i32[s + 118],
      title: this.readCString(i32[s + 119]) || 'Evertale Shop',
      msg: this.readCString(i32[s + 120]),
    };

    const fishing: FishingSnapshot = {
      active: i32[s + 121] !== 0,
      phase: i32[s + 122],
      bobberX: i32[s + 123],
      bobberY: i32[s + 124],
      fishPos: i32[s + 125],
      barPos: i32[s + 126],
      barHalfW: i32[s + 127],
      progress: i32[s + 128],
      tension: i32[s + 129],
      species: i32[s + 130],
      weightOz: i32[s + 131],
    };

    return {
      mode: MODE_MAP[i32[s + 3]] ?? 'TITLE',
      zoneId,
      zoneLabel,
      timeOfDay,
      timeOfDayLabel,
      ch1Step,
      ch2Step,
      isWorldMap,
      worldRegionId,
      worldRegionName,
      regionNames: this.regionNames,
      activeVehicle,
      vehicleUnlockedMask,
      atlasOpen,
      designerModeEnabled,
      designerSandbox,
      babanas,
      planksFoundMask,
      heroTx,
      heroTy,
      globeW,
      globeH,
      atlasCells: this.atlasCells,
      landmarks: this.landmarks,
      hasMiloBell,
      hasFamiliar,
      hasHeartVessel,
      storyStage,
      objectiveLabel,
      intro: {
        scriptId: i32[s + 145],
        beat: i32[s + 136],
        beatCount: Math.max(1, i32[s + 137]),
        scene: i32[s + 82],
        actNum: Math.max(1, i32[s + 138]),
        camMode: i32[s + 139],
        fxMode: i32[s + 140],
        autoPlay: i32[s + 141] !== 0,
        plateTimer: i32[s + 142],
        fadeDir: i32[s + 144],
        timer: i32[s + 83],
        chars: i32[s + 84],
        title: this.readCString(i32[s + 85]),
        speaker: this.readCString(i32[s + 143]),
        line1: this.readCString(i32[s + 86]),
        line2: this.readCString(i32[s + 87]),
      },
      fadeLevel: i32[s + 81],
      flashAlpha: Math.min(0.65, i32[s + 8] / 12),
      dialogue: {
        active: i32[s + 33] !== 0,
        speaker,
        portrait: portraitFromSpeaker(speaker),
        portraitId: i32[s + 146],
        fullText: this.readCString(i32[s + 35]),
        visibleChars: i32[s + 36],
        finishedTyping: i32[s + 37] !== 0,
        choices,
        selectedChoice: i32[s + 39],
        sfxVoice: (i32[s + 40] as SfxId) || SfxId.TEXT_HERO,
      },
      party: this.readParty(),
      activeSlot: (i32[s + 16] as FamiliarSlot) ?? 1,
      potions: i32[s + 17],
      guilders: i32[s + 103],
      starDrops: i32[s + 104],
      snacks: i32[s + 105],
      lureTier: i32[s + 106],
      fishBag: [
        i32[s + 108],
        i32[s + 109],
        i32[s + 110],
        i32[s + 111],
        i32[s + 112],
        i32[s + 113],
      ],
      fishTotalCaught: i32[s + 107],
      hasWand,
      hasSewerKey,
      shop,
      fishing,
      shopCatalog: this.shopCatalog,
      fishCatalog: this.fishCatalog,
      locket: {
        hasCourage,
        guardMended,
        chestOpened,
        elderCutsceneSeen,
        bossDefeated,
      },
      battlePhase: BPHASE_MAP[i32[s + 49]] ?? 'COMMAND',
      battleSelectedCommand: i32[s + 62],
      battleSelectedEnemy: i32[s + 63],
      battleBanner: i32[s + 74] > 0 ? this.readCString(i32[s + 73]) : '',
      battleEnemies: enemies.map((e) => ({
        id: e.id,
        name: e.name,
        hp: e.hp,
        maxHp: e.maxHp,
        alive: e.alive,
        isBoss: e.isBoss,
        staggered: e.staggered,
        locks: e.locks,
        brokenLocks: e.brokenLocks,
      })),
      battleStats: {
        combos: i32[s + 75],
        parries: i32[s + 76],
        locksBroken: i32[s + 77],
        expEarned: i32[s + 78],
        guildersEarned: i32[s + 132],
      },
      timedRingActive: i32[s + 66] !== 0,
      timedRingKind: i32[s + 67] !== 0 ? 'PARRY' : 'OFFENSE',
      oamCount: i32[s + 10],
      oamLimit: i32[s + 11],
      activeEnemyCount: i32[s + 21],
      tuning: this.readTuning(),
      playerTileX: tx,
      playerTileY: ty,
    };
  }
}
