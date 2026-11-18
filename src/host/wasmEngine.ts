/**
 * Project Hearth — WebAssembly C99 Engine Host Adapter
 *
 * Loads public/hearth_core.wasm (compiled directly from engine/core/) so the
 * Studio Browser Preview runs the EXACT SAME C99 simulation, tilemaps, palettes,
 * combat physics, and cutscene bytecode as Linux, Windows, Genesis, and SNES.
 */

import {
  BattleBiome,
  BattleEnemyState,
  BattlePhase,
  CutsceneOpExport,
  DebugBox,
  DialogueChoice,
  Direction,
  EncounterPreset,
  EngineTuning,
  FamiliarSlot,
  FloatingText,
  FP_ONE,
  fromFp,
  GameMode,
  HeartAura,
  LockKind,
  MAP_H,
  MAP_W,
  OamSprite,
  ParticleKind,
  PartyMemberState,
  PortraitId,
  PpuFramePacket,
  PpuMode,
  PpuParticle,
  SfxId,
  SpriteId,
  TILE_SIZE,
  UiSnapshot,
  WorldMapData,
} from '../core/types';

interface WasmExports {
  memory: WebAssembly.Memory;
  wasm_init(): void;
  wasm_step(padBits: number): void;
  wasm_start_sandbox(): void;
  wasm_start_cutscene_demo(): void;
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
  wasm_get_palettes_ptr(): number;
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
];

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
  switch (speaker) {
    case 'Elder Wren':
      return 'elder';
    case 'Clara':
      return 'clara';
    case 'Guard Bram':
      return 'guard';
    case 'Pip':
      return 'pip';
    case 'Rowan':
      return 'hero';
    default:
      return 'none';
  }
}

function presetToCode(preset: EncounterPreset): number {
  if (preset === 'SHRINE_BOSS') return 2;
  if (preset === 'GLADE_DUO') return 1;
  return 0;
}

export class WasmHearthEngine {
  private readonly wasm: WasmExports;
  private readonly decoder = new TextDecoder('utf-8');
  public readonly palettes: readonly (readonly string[])[];
  public readonly map: WorldMapData;

  private readonly scalarsIdx: number;
  private readonly oamIdx: number;
  private readonly partyIdx: number;
  private readonly enemiesIdx: number;
  private readonly particlesIdx: number;
  private readonly ftextsIdx: number;
  private readonly fieldBoxesIdx: number;
  private readonly sfxPtr: number;

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

    // View/copy generated 44x28 sandbox tilemaps directly from C core memory
    const u8 = new Uint8Array(wasm.memory.buffer);
    const cells = MAP_W * MAP_H;
    const bg1Ptr = wasm.wasm_get_bg1_ptr() >>> 0;
    const bg2Ptr = wasm.wasm_get_bg2_ptr() >>> 0;
    const colPtr = wasm.wasm_get_col_ptr() >>> 0;
    this.map = {
      width: MAP_W,
      height: MAP_H,
      bg1: u8.slice(bg1Ptr, bg1Ptr + cells),
      bg2: u8.slice(bg2Ptr, bg2Ptr + cells),
      col: u8.slice(colPtr, colPtr + cells),
    };
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
    return this.buildFramePacket();
  }

  public startSandbox(): void {
    this.wasm.wasm_start_sandbox();
  }

  public startCutsceneDemo(): void {
    this.wasm.wasm_start_cutscene_demo();
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

    return {
      width,
      height,
      frameCount,
      mode,
      camX,
      camY,
      shakeX: i32[s + 6],
      shakeY: i32[s + 7],
      fadeLevel: 0,
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
      battle: {
        active: i32[s + 47] !== 0,
        biome: (i32[s + 48] === 1 ? 'SHRINE' : 'MEADOW') as BattleBiome,
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
          sweetEnd: 1.05,
          result: ringResRaw === 1 ? 'PERFECT' : ringResRaw === 2 ? 'GOOD' : 'NONE',
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
      debugBoxes,
      sfxQueue: this.consumePendingSfx(),
    };
  }

  public getUiSnapshot(): UiSnapshot {
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

    const zoneLabel =
      tx >= 35
        ? 'Ancient Stone Shrine'
        : tx >= 21
          ? 'Whispering Meadow'
          : 'Windmill Hamlet';

    let objectiveLabel = 'Talk to Clara or cross the bridge to test combat';
    if (bossDefeated && guardMended) {
      objectiveLabel = 'Sandbox complete — tune feel, swap assets, or rematch!';
    } else if (hasCourage && !guardMended) {
      objectiveLabel = 'Bring Courage from your Locket to Guard Bram at the bridge';
    } else if (guardMended && !bossDefeated) {
      objectiveLabel = 'Challenge the Briar Warden at the eastern Stone Shrine';
    }

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

    return {
      mode: MODE_MAP[i32[s + 3]] ?? 'TITLE',
      zoneLabel,
      objectiveLabel,
      dialogue: {
        active: i32[s + 33] !== 0,
        speaker,
        portrait: portraitFromSpeaker(speaker),
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
