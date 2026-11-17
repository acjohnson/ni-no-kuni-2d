/**
 * Project Hearth — Platform-Agnostic 16-Bit Engine Core (C99-Mirrored)
 *
 * ZERO DOM, ZERO Canvas, ZERO React, ZERO browser globals.
 * Runs on fixed-point 16.8 integer math, deterministic 60Hz ticks,
 * flat Uint8Array tilemaps, and outputs a pure PpuFramePacket + SfxId queue.
 */

import { BattleCallbacks, BattleSystem, createInitialParty, EncounterPreset } from './battleSystem';
import {
  ActorTargetId,
  CUTSCENE_ELDER_TALK,
  CUTSCENE_GIVE_COURAGE,
  CUTSCENE_INTRO_DEMO,
  CUTSCENE_TAKE_COURAGE,
  CutsceneOp,
} from './cutsceneData';
import {
  buildSandboxMap,
  COL_SHALLOW,
  COL_SOLID,
  COL_TALL_GRASS,
  COL_WATER,
  MAP_H,
  MAP_W,
  TILE_SIZE,
  WorldMapData,
} from './mapData';
import { RetroPrng } from './prng';
import {
  BattleBiome,
  DebugBox,
  DEFAULT_TUNING,
  DIR_DOWN,
  DIR_LEFT,
  DIR_RIGHT,
  DIR_UP,
  Direction,
  DialogueChoice,
  DialogueState,
  EngineTuning,
  FamiliarSlot,
  FloatingText,
  FP_ONE,
  fromFp,
  GameMode,
  HeartAura,
  HeartLocketState,
  OamSprite,
  PAD_A,
  PAD_B,
  PAD_DOWN,
  PAD_L,
  PAD_LEFT,
  PAD_R,
  PAD_RIGHT,
  PAD_UP,
  ParticleKind,
  PartyMemberState,
  PortraitId,
  PpuFramePacket,
  PpuParticle,
  SfxId,
  SpriteId,
  toFp,
  UiSnapshot,
} from './types';

interface NpcEntity {
  id: 'elder' | 'clara' | 'guard';
  name: string;
  spriteId: SpriteId;
  fx: number;
  fy: number;
  dir: Direction;
  aura: HeartAura;
  emote: 'none' | 'bang' | 'heart' | 'question' | 'spark';
  emoteFrames: number;
}

interface FieldEnemy {
  id: string;
  name: string;
  spriteId: SpriteId;
  preset: EncounterPreset;
  biome: BattleBiome;
  isBoss: boolean;
  homeFx: number;
  homeFy: number;
  fx: number;
  fy: number;
  vx: number;
  vy: number;
  dir: Direction;
  alive: boolean;
  alerted: boolean;
  alertFrames: number;
  patrolTimer: number;
  animFrame: number;
}

interface HeartOrbEffect {
  active: boolean;
  startX: number;
  startY: number;
  endX: number;
  endY: number;
  frame: number;
  duration: number;
  color: string;
}

const PARTICLE_POOL_SIZE = 72;
const FLOAT_TEXT_POOL_SIZE = 18;
const TRAIL_SIZE = 48;

export class HearthEngine {
  public readonly map: WorldMapData;
  public tuning: EngineTuning;
  private prng: RetroPrng;
  private battle: BattleSystem;

  private mode: GameMode = 'TITLE';
  private frameCount = 0;
  private prevPad = 0;

  /* Hero Fixed-Point State */
  private heroFx = toFp(10 * TILE_SIZE + 8);
  private heroFy = toFp(14 * TILE_SIZE + 8);
  private heroVx = 0;
  private heroVy = 0;
  private heroDir: Direction = DIR_DOWN;
  private heroAnimTick = 0;
  private heroStepTimer = 0;
  private heroDashing = false;
  private heroEmote: 'none' | 'bang' | 'heart' | 'question' | 'spark' = 'none';
  private heroEmoteFrames = 0;
  private heroSafeFrames = 0;

  /* Trailing Companion (Pip) Breadcrumb Buffer */
  private trailX = new Int32Array(TRAIL_SIZE);
  private trailY = new Int32Array(TRAIL_SIZE);
  private trailHead = 0;
  private pipFx = toFp(9 * TILE_SIZE + 4);
  private pipFy = toFp(14 * TILE_SIZE + 6);
  private pipDir: Direction = DIR_RIGHT;
  private pipEmote: 'none' | 'bang' | 'heart' | 'question' | 'spark' = 'none';
  private pipEmoteFrames = 0;

  /* Camera & Screen Juice Registers */
  private camFx = toFp(10 * TILE_SIZE);
  private camFy = toFp(14 * TILE_SIZE);
  private camOverride = false;
  private camStartFx = 0;
  private camStartFy = 0;
  private camTargetFx = 0;
  private camTargetFy = 0;
  private camPanFrame = 0;
  private camPanDur = 1;
  private shakeIntensity = 0;
  private shakeFrames = 0;
  private hitstopFrames = 0;
  private fadeLevel = 0;
  private flashAlpha = 0;

  /* Party, World Flags & Entities */
  private party: PartyMemberState[];
  private activeSlot: FamiliarSlot = 1;
  private potions = 3;
  private locket: HeartLocketState = {
    hasCourage: false,
    guardMended: false,
    chestOpened: false,
    elderCutsceneSeen: false,
    bossDefeated: false,
  };

  private npcs: NpcEntity[] = [];
  private fieldEnemies: FieldEnemy[] = [];
  private heartOrb: HeartOrbEffect = {
    active: false,
    startX: 0,
    startY: 0,
    endX: 0,
    endY: 0,
    frame: 0,
    duration: 40,
    color: '#F5B841',
  };

  /* Dialogue & Bytecode Cutscene VM State */
  private dialogue: DialogueState = {
    active: false,
    speaker: '',
    portrait: 'none',
    fullText: '',
    visibleChars: 0,
    finishedTyping: true,
    choices: [],
    selectedChoice: 0,
    sfxVoice: SfxId.TEXT_HERO,
  };
  private dialoguePauseFrames = 0;
  private dialogueOnClose: (() => void) | null = null;

  private cutsceneQueue: CutsceneOp[] = [];
  private cutsceneIp = 0;
  private cutsceneWaitFrames = 0;
  private returnToTitleAfterCutscene = false;

  /* Fixed-Capacity Zero-Allocation Pools */
  private particles: PpuParticle[] = [];
  private floatingTexts: FloatingText[] = [];
  private sfxQueue: SfxId[] = [];
  private currentInteractLabel = '';
  private currentInteractX = 0;
  private currentInteractY = 0;
  private lastOamCount = 0;

  constructor(initialTuning?: Partial<EngineTuning>) {
    this.map = buildSandboxMap();
    this.tuning = { ...DEFAULT_TUNING, ...initialTuning };
    this.prng = new RetroPrng(0x5EED);
    this.battle = new BattleSystem();
    this.party = createInitialParty();

    for (let i = 0; i < PARTICLE_POOL_SIZE; i++) {
      this.particles.push({
        active: false,
        kind: 'DUST',
        fx: 0,
        fy: 0,
        vx: 0,
        vy: 0,
        life: 0,
        maxLife: 1,
        size: 2,
        color: '#FFFFFF',
        screenSpace: false,
      });
    }
    for (let i = 0; i < FLOAT_TEXT_POOL_SIZE; i++) {
      this.floatingTexts.push({
        active: false,
        x: 0,
        y: 0,
        vy: 0,
        text: '',
        color: '#FFFFFF',
        scale: 1,
        life: 0,
        maxLife: 1,
      });
    }

    this.resetWorld();
  }

  public getPpuSize(): { width: number; height: number; oamLimit: number } {
    if (this.tuning.ppuMode === 'SNES_256x224') {
      return { width: 256, height: 224, oamLimit: 128 };
    }
    return { width: 320, height: 224, oamLimit: 80 };
  }

  public resetWorld(): void {
    this.party = createInitialParty();
    this.activeSlot = 1;
    this.potions = 3;
    this.locket = {
      hasCourage: false,
      guardMended: false,
      chestOpened: false,
      elderCutsceneSeen: false,
      bossDefeated: false,
    };

    this.heroFx = toFp(10 * TILE_SIZE + 8);
    this.heroFy = toFp(14 * TILE_SIZE + 8);
    this.heroVx = 0;
    this.heroVy = 0;
    this.heroDir = DIR_DOWN;
    this.heroSafeFrames = 0;

    for (let i = 0; i < TRAIL_SIZE; i++) {
      this.trailX[i] = this.heroFx - toFp(18);
      this.trailY[i] = this.heroFy - toFp(4);
    }
    this.trailHead = 0;
    this.pipFx = this.heroFx - toFp(18);
    this.pipFy = this.heroFy - toFp(4);

    this.npcs = [
      {
        id: 'elder',
        name: 'Elder Wren',
        spriteId: 'npc_elder',
        fx: toFp(8 * TILE_SIZE + 8),
        fy: toFp(11 * TILE_SIZE + 8),
        dir: DIR_DOWN,
        aura: 'NONE',
        emote: 'none',
        emoteFrames: 0,
      },
      {
        id: 'clara',
        name: 'Clara',
        spriteId: 'npc_clara',
        fx: toFp(13 * TILE_SIZE + 8),
        fy: toFp(16 * TILE_SIZE + 8),
        dir: DIR_DOWN,
        aura: 'RADIANT_COURAGE',
        emote: 'none',
        emoteFrames: 0,
      },
      {
        id: 'guard',
        name: 'Guard Bram',
        spriteId: 'npc_guard',
        fx: toFp(22 * TILE_SIZE + 8),
        fy: toFp(13 * TILE_SIZE + 8),
        dir: DIR_LEFT,
        aura: 'HOLLOW_FEAR',
        emote: 'none',
        emoteFrames: 0,
      },
    ];

    this.respawnFieldEnemies();
    this.snapCameraToHero();
  }

  public respawnFieldEnemies(): void {
    this.fieldEnemies = [
      {
        id: 'field_gloom_1',
        name: 'Gloomcap',
        spriteId: 'enemy_gloomcap',
        preset: 'MEADOW_PACK',
        biome: 'MEADOW',
        isBoss: false,
        homeFx: toFp(26 * TILE_SIZE + 8),
        homeFy: toFp(15 * TILE_SIZE + 8),
        fx: toFp(26 * TILE_SIZE + 8),
        fy: toFp(15 * TILE_SIZE + 8),
        vx: 0,
        vy: 0,
        dir: DIR_LEFT,
        alive: true,
        alerted: false,
        alertFrames: 0,
        patrolTimer: 30,
        animFrame: 0,
      },
      {
        id: 'field_thorn_1',
        name: 'Thornling',
        spriteId: 'enemy_thornling',
        preset: 'GLADE_DUO',
        biome: 'MEADOW',
        isBoss: false,
        homeFx: toFp(30 * TILE_SIZE + 8),
        homeFy: toFp(10 * TILE_SIZE + 8),
        fx: toFp(30 * TILE_SIZE + 8),
        fy: toFp(10 * TILE_SIZE + 8),
        vx: 0,
        vy: 0,
        dir: DIR_DOWN,
        alive: true,
        alerted: false,
        alertFrames: 0,
        patrolTimer: 60,
        animFrame: 0,
      },
      {
        id: 'field_moth_1',
        name: 'Dusk Moth',
        spriteId: 'enemy_dusk_moth',
        preset: 'GLADE_DUO',
        biome: 'MEADOW',
        isBoss: false,
        homeFx: toFp(31 * TILE_SIZE + 8),
        homeFy: toFp(20 * TILE_SIZE + 8),
        fx: toFp(31 * TILE_SIZE + 8),
        fy: toFp(20 * TILE_SIZE + 8),
        vx: 0,
        vy: 0,
        dir: DIR_LEFT,
        alive: true,
        alerted: false,
        alertFrames: 0,
        patrolTimer: 90,
        animFrame: 0,
      },
      {
        id: 'field_boss_shrine',
        name: 'Briar Warden',
        spriteId: 'boss_briar_warden',
        preset: 'SHRINE_BOSS',
        biome: 'SHRINE',
        isBoss: true,
        homeFx: toFp(38 * TILE_SIZE + 8),
        homeFy: toFp(13 * TILE_SIZE + 8),
        fx: toFp(38 * TILE_SIZE + 8),
        fy: toFp(13 * TILE_SIZE + 8),
        vx: 0,
        vy: 0,
        dir: DIR_LEFT,
        alive: true,
        alerted: false,
        alertFrames: 0,
        patrolTimer: 0,
        animFrame: 0,
      },
    ];
  }

  /* Public High-Level Controls for Host / Title Screen / Workbench */

  public startSandbox(): void {
    this.mode = 'EXPLORE';
    this.camOverride = false;
    this.dialogue.active = false;
    this.battle.state.active = false;
    this.snapCameraToHero();
    this.sfxQueue.push(SfxId.CONFIRM);
  }

  public startCutsceneDemo(returnToTitle = false): void {
    this.returnToTitleAfterCutscene = returnToTitle;
    this.battle.state.active = false;
    this.dialogue.active = false;
    this.heroFx = toFp(10 * TILE_SIZE + 8);
    this.heroFy = toFp(14 * TILE_SIZE + 8);
    this.snapCameraToHero();
    this.runCutscene(CUTSCENE_INTRO_DEMO);
  }

  public startQuickBattle(preset: EncounterPreset = 'SHRINE_BOSS'): void {
    const biome: BattleBiome = preset === 'SHRINE_BOSS' ? 'SHRINE' : 'MEADOW';
    // Restore party nicely when launching Battle Test shortcut so it's always crisp to play
    for (const m of this.party) {
      m.hp = m.maxHp;
      m.mp = m.maxMp;
    }
    this.dialogue.active = false;
    this.mode = 'BATTLE';
    const { width } = this.getPpuSize();
    this.battle.startEncounter(
      preset,
      biome,
      '',
      this.activeSlot,
      Math.max(this.potions, 2),
      width,
      this.getBattleCallbacks()
    );
  }

  public returnToTitle(): void {
    this.mode = 'TITLE';
    this.battle.state.active = false;
    this.dialogue.active = false;
    this.camOverride = false;
  }

  public setTuning(partial: Partial<EngineTuning>): void {
    this.tuning = { ...this.tuning, ...partial };
  }

  public healPartyFull(): void {
    for (const m of this.party) {
      m.hp = m.maxHp;
      m.mp = m.maxMp;
    }
    this.sfxQueue.push(SfxId.HEAL);
    this.spawnParticles('HEAL_MOTE', fromFp(this.heroFx), fromFp(this.heroFy) - 10, 14, '#74B461', false);
    this.spawnFloatingText(fromFp(this.heroFx), fromFp(this.heroFy) - 24, 'FULL RESTORE!', '#74B461', 1);
  }

  /* Mouse / Touch UI Bridge Methods */
  public uiBattleCommand(cmdIndex: number): void {
    if (this.mode !== 'BATTLE') return;
    this.battle.triggerCommand(cmdIndex, this.party, this.getBattleCallbacks());
  }

  public uiBattleSelectEnemy(idx: number): void {
    if (this.mode !== 'BATTLE') return;
    this.battle.selectEnemy(idx, this.getBattleCallbacks());
  }

  public uiBattleSwapFamiliar(slot: FamiliarSlot): void {
    if (this.mode !== 'BATTLE') return;
    this.battle.swapFamiliar(0, slot, this.getBattleCallbacks());
  }

  public uiTriggerTimedTap(): void {
    if (this.mode === 'BATTLE') {
      this.battle.triggerTimedTap();
    } else if (this.dialogue.active) {
      this.advanceDialogue();
    }
  }

  public uiSelectDialogueChoice(idx: number): void {
    if (!this.dialogue.active || !this.dialogue.finishedTyping) return;
    const choice = this.dialogue.choices[idx];
    if (!choice) return;
    this.dialogue.selectedChoice = idx;
    this.confirmDialogueChoice();
  }

  public uiFinishBattleSummary(rematch: boolean): void {
    if (rematch) {
      const wasBoss = this.battle.state.enemies.some((e) => e.isBoss);
      this.startQuickBattle(wasBoss ? 'SHRINE_BOSS' : 'MEADOW_PACK');
    } else {
      this.battle.state.active = false;
      this.mode = 'EXPLORE';
      this.heroSafeFrames = 90;
      this.sfxQueue.push(SfxId.CONFIRM);
    }
  }

  private getBattleCallbacks(): BattleCallbacks {
    return {
      playSfx: (sfx) => this.sfxQueue.push(sfx),
      addShake: (intensity, frames) => {
        this.shakeIntensity = Math.max(this.shakeIntensity, intensity);
        this.shakeFrames = Math.max(this.shakeFrames, frames);
      },
      setHitstop: (frames) => {
        this.hitstopFrames = Math.max(this.hitstopFrames, frames);
      },
      setFlash: (alpha) => {
        this.flashAlpha = Math.max(this.flashAlpha, alpha);
      },
      spawnParticles: (kind, x, y, count, color, screenSpace) => {
        this.spawnParticles(kind, x, y, count, color, screenSpace);
      },
      spawnText: (x, y, text, color, scale = 1) => {
        this.spawnFloatingText(x, y, text, color, scale);
      },
      onBattleEnd: (won, defeatedFieldId, isBoss) => {
        if (won) {
          this.activeSlot = this.battle.state.activeSlot;
          if (defeatedFieldId) {
            const fe = this.fieldEnemies.find((e) => e.id === defeatedFieldId);
            if (fe) fe.alive = false;
          }
          if (isBoss) {
            this.locket.bossDefeated = true;
          }
          this.mode = 'BATTLE_VICTORY';
        } else {
          // Gentle recovery back to Waystone fountain (never a dead-end)
          for (const m of this.party) {
            m.hp = m.maxHp;
            m.mp = m.maxMp;
          }
          this.heroFx = toFp(8 * TILE_SIZE + 8);
          this.heroFy = toFp(18 * TILE_SIZE + 8);
          this.snapCameraToHero();
          this.mode = 'BATTLE_DEFEAT';
        }
      },
    };
  }

  /* ========================================================================
   * MASTER 60Hz DETERMINISTIC SIMULATION TICK
   * ======================================================================== */
  public step(padBits: number): PpuFramePacket {
    this.sfxQueue.length = 0;
    const justPressed = padBits & ~this.prevPad;
    this.prevPad = padBits;
    this.frameCount++;

    // Decay screen shake & white impact flash even during hitstop!
    if (this.shakeFrames > 0) this.shakeFrames--;
    if (this.flashAlpha > 0) this.flashAlpha = Math.max(0, this.flashAlpha - 0.06);

    // Hitstop Freeze-Frame! (Pauses gameplay motion for crisp impact crunch while keeping shake alive!)
    if (this.hitstopFrames > 0) {
      this.hitstopFrames--;
      return this.buildFramePacket();
    }

    // Update particles & floating texts
    this.stepParticles();
    this.stepFloatingTexts();

    // Ambient sunlit meadow pollen motes
    if ((this.mode === 'TITLE' || this.mode === 'EXPLORE' || this.mode === 'CUTSCENE') && this.frameCount % 14 === 0) {
      const { width, height } = this.getPpuSize();
      const px = fromFp(this.camFx) + this.prng.range(0, width);
      const py = fromFp(this.camFy) + this.prng.range(0, height);
      this.spawnSingleParticle('POLLEN', px, py, this.prng.range(-24, 32), this.prng.range(-18, -6), 95, 1, '#FFF1A8', false);
    }

    // NPC radiant/hollow auras emit subtle atmospheric motes
    if ((this.mode === 'EXPLORE' || this.mode === 'CUTSCENE' || this.mode === 'TITLE') && this.frameCount % 16 === 0) {
      for (const npc of this.npcs) {
        if (npc.aura === 'RADIANT_COURAGE') {
          this.spawnSingleParticle(
            'HEART_MOTE',
            fromFp(npc.fx) + this.prng.range(-7, 7),
            fromFp(npc.fy) - 10,
            this.prng.range(-10, 10),
            -38,
            34,
            2,
            '#F5B841',
            false
          );
        } else if (npc.aura === 'HOLLOW_FEAR') {
          this.spawnSingleParticle(
            'HEART_MOTE',
            fromFp(npc.fx) + this.prng.range(-7, 7),
            fromFp(npc.fy) - 8,
            this.prng.range(-8, 8),
            -26,
            38,
            2,
            '#9E76CC',
            false
          );
        }
      }
    }

    // Tick emote timers
    if (this.heroEmoteFrames > 0) this.heroEmoteFrames--;
    else this.heroEmote = 'none';
    if (this.pipEmoteFrames > 0) this.pipEmoteFrames--;
    else this.pipEmote = 'none';
    for (const npc of this.npcs) {
      if (npc.emoteFrames > 0) npc.emoteFrames--;
      else npc.emote = 'none';
    }

    switch (this.mode) {
      case 'TITLE': {
        // Gentle scenic camera drift around Windmill Hamlet on title screen!
        const { width, height } = this.getPpuSize();
        const cx = 14 * TILE_SIZE + Math.round(Math.sin(this.frameCount * 0.008) * 64);
        const cy = 12 * TILE_SIZE + Math.round(Math.cos(this.frameCount * 0.006) * 26);
        this.camFx = toFp(Math.max(0, Math.min(MAP_W * TILE_SIZE - width, cx - (width >> 1))));
        this.camFy = toFp(Math.max(0, Math.min(MAP_H * TILE_SIZE - height, cy - (height >> 1))));
        this.stepFieldEnemies(false);
        break;
      }

      case 'EXPLORE': {
        if (this.heroSafeFrames > 0) this.heroSafeFrames--;
        if (this.dialogue.active) {
          this.stepDialogue(justPressed);
        } else {
          this.stepHeroMovement(padBits);
          this.stepPipFollower();
          this.stepFieldEnemies(true);
          this.updateInteractPrompt();
          if (justPressed & PAD_A) {
            this.tryInteract();
          }
          // Quick Familiar Cycle out in field updates active combat lead!
          if (justPressed & PAD_L) {
            this.activeSlot = (((this.activeSlot + 2) % 3) as FamiliarSlot);
            this.sfxQueue.push(SfxId.CURSOR);
          } else if (justPressed & PAD_R) {
            this.activeSlot = (((this.activeSlot + 1) % 3) as FamiliarSlot);
            this.sfxQueue.push(SfxId.CURSOR);
          }
        }
        this.stepCameraFollow();
        break;
      }

      case 'CUTSCENE': {
        this.stepPipFollower();
        this.stepCutscene(justPressed);
        this.stepCameraFollow();
        break;
      }

      case 'BATTLE': {
        this.potions = this.battle.step(
          justPressed,
          this.party,
          this.tuning,
          this.prng,
          this.getBattleCallbacks()
        );
        this.activeSlot = this.battle.state.activeSlot;
        break;
      }

      case 'BATTLE_VICTORY':
      case 'BATTLE_DEFEAT': {
        if (justPressed & PAD_A) {
          this.uiFinishBattleSummary(false);
        }
        break;
      }
    }

    return this.buildFramePacket();
  }

  /* ========================================================================
   * FLUID 8-WAY FIXED-POINT MOVEMENT WITH CORNER-NUDGING
   * ======================================================================== */
  private stepHeroMovement(padBits: number): void {
    let ix = 0;
    let iy = 0;
    if (padBits & PAD_LEFT) ix -= 1;
    if (padBits & PAD_RIGHT) ix += 1;
    if (padBits & PAD_UP) iy -= 1;
    if (padBits & PAD_DOWN) iy += 1;

    this.heroDashing = (padBits & PAD_B) !== 0 && (ix !== 0 || iy !== 0);
    const maxSpeed = this.heroDashing ? this.tuning.dashSpeedFp : this.tuning.walkSpeedFp;

    let targetVx = ix * maxSpeed;
    let targetVy = iy * maxSpeed;
    if (ix !== 0 && iy !== 0) {
      // 181 / 256 ≈ 0.7071 diagonal normalization in pure integer math
      targetVx = (targetVx * 181) >> 8;
      targetVy = (targetVy * 181) >> 8;
    }

    const accel = this.tuning.accelFp;
    this.heroVx = this.approachFp(this.heroVx, targetVx, accel);
    this.heroVy = this.approachFp(this.heroVy, targetVy, accel);

    // Update facing direction cleanly
    if (ix !== 0 || iy !== 0) {
      if (Math.abs(ix) >= Math.abs(iy) && ix !== 0) {
        this.heroDir = ix < 0 ? DIR_LEFT : DIR_RIGHT;
      } else if (iy !== 0) {
        this.heroDir = iy < 0 ? DIR_UP : DIR_DOWN;
      }
    }

    // Resolve X axis + automatic Y corner-nudging around edges!
    if (this.heroVx !== 0) {
      const nextFx = this.heroFx + this.heroVx;
      if (!this.collidesWorld(nextFx, this.heroFy)) {
        this.heroFx = nextFx;
      } else {
        // Check gentle corner slip (±5 px)
        const nudge = toFp(5);
        if (!this.collidesWorld(nextFx, this.heroFy - nudge)) {
          this.heroFy -= 192;
        } else if (!this.collidesWorld(nextFx, this.heroFy + nudge)) {
          this.heroFy += 192;
        } else {
          this.heroVx = 0;
        }
      }
    }

    // Resolve Y axis + automatic X corner-nudging around edges!
    if (this.heroVy !== 0) {
      const nextFy = this.heroFy + this.heroVy;
      if (!this.collidesWorld(this.heroFx, nextFy)) {
        this.heroFy = nextFy;
      } else {
        const nudge = toFp(5);
        if (!this.collidesWorld(this.heroFx - nudge, nextFy)) {
          this.heroFx -= 192;
        } else if (!this.collidesWorld(this.heroFx + nudge, nextFy)) {
          this.heroFx += 192;
        } else {
          this.heroVy = 0;
        }
      }
    }

    // Animation, footsteps, dust puffs, tall grass rustle & water ford ripples!
    const moving = Math.abs(this.heroVx) > 24 || Math.abs(this.heroVy) > 24;
    if (moving) {
      this.heroAnimTick += this.heroDashing ? 2 : 1;
      this.heroStepTimer++;

      // Record breadcrumb for Pip whenever hero moved >= 2px from last trail point
      const lastX = this.trailX[this.trailHead];
      const lastY = this.trailY[this.trailHead];
      const dx = fromFp(this.heroFx - lastX);
      const dy = fromFp(this.heroFy - lastY);
      if (dx * dx + dy * dy >= 4) {
        this.trailHead = (this.trailHead + 1) % TRAIL_SIZE;
        this.trailX[this.trailHead] = this.heroFx;
        this.trailY[this.trailHead] = this.heroFy;
      }

      const footTile = this.getColAtPixel(fromFp(this.heroFx), fromFp(this.heroFy));
      const cadence = this.heroDashing ? 10 : 16;

      if (this.heroStepTimer >= cadence) {
        this.heroStepTimer = 0;
        if (footTile === COL_TALL_GRASS) {
          this.sfxQueue.push(SfxId.GRASS_RUSTLE);
          this.spawnParticles('GRASS', fromFp(this.heroFx), fromFp(this.heroFy), 4, '#74B461', false);
        } else if (footTile === COL_SHALLOW) {
          this.sfxQueue.push(SfxId.WATER_SPLASH);
          this.spawnParticles('RIPPLE', fromFp(this.heroFx), fromFp(this.heroFy), 4, '#9AD8F4', false);
        } else {
          this.sfxQueue.push(this.heroDashing ? SfxId.DASH : SfxId.STEP);
          if (this.heroDashing) {
            this.spawnParticles('DUST', fromFp(this.heroFx), fromFp(this.heroFy), 2, '#DFBA84', false);
          }
        }
      }
    } else {
      this.heroAnimTick = 0;
      this.heroStepTimer = 10;
    }
  }

  private stepPipFollower(): void {
    const stepsBack = Math.max(4, Math.min(TRAIL_SIZE - 2, Math.round(this.tuning.followDistPx / 2)));
    const targetIdx = (this.trailHead - stepsBack + TRAIL_SIZE) % TRAIL_SIZE;
    const goalX = this.trailX[targetIdx];
    const goalY = this.trailY[targetIdx] - toFp(4);

    // Smooth fixed-point spring follow (1/7th per frame)
    const dx = goalX - this.pipFx;
    const dy = goalY - this.pipFy;
    this.pipFx += (dx / 7) | 0;
    this.pipFy += (dy / 7) | 0;

    if (Math.abs(dx) > 40) {
      this.pipDir = dx < 0 ? DIR_LEFT : DIR_RIGHT;
    }
  }

  private approachFp(cur: number, target: number, step: number): number {
    if (cur < target) return Math.min(target, cur + step);
    if (cur > target) return Math.max(target, cur - step);
    return cur;
  }

  private getColAtPixel(px: number, py: number): number {
    const tx = Math.floor(px / TILE_SIZE);
    const ty = Math.floor(py / TILE_SIZE);
    if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) return COL_SOLID;
    return this.map.col[ty * MAP_W + tx];
  }

  private collidesWorld(fx: number, fy: number): boolean {
    const cx = fromFp(fx);
    const cy = fromFp(fy);
    // Crisp 10x7 foot hitbox (feels generous and clean like Chrono Trigger / Sea of Stars)
    const corners: [number, number][] = [
      [cx - 5, cy - 3],
      [cx + 5, cy - 3],
      [cx - 5, cy + 3],
      [cx + 5, cy + 3],
    ];
    for (const [px, py] of corners) {
      const c = this.getColAtPixel(px, py);
      if (c === COL_SOLID || c === COL_WATER) return true;
    }
    // Check solid NPCs, Waystone (8,17), and Chest (26,6)
    for (const npc of this.npcs) {
      const nx = fromFp(npc.fx);
      const ny = fromFp(npc.fy);
      if (Math.abs(cx - nx) < 11 && Math.abs(cy - ny) < 9) return true;
    }
    // Waystone pedestal at tile (8, 17)
    if (Math.abs(cx - (8 * TILE_SIZE + 8)) < 11 && Math.abs(cy - (17 * TILE_SIZE + 8)) < 9) {
      return true;
    }
    // Treasure chest at tile (26, 6)
    if (Math.abs(cx - (26 * TILE_SIZE + 8)) < 10 && Math.abs(cy - (6 * TILE_SIZE + 8)) < 8) {
      return true;
    }
    return false;
  }

  /* ========================================================================
   * ROAMING VISIBLE FIELD ENEMY AI & SEAMLESS COMBAT ENGAGEMENT
   * ======================================================================== */
  private stepFieldEnemies(canEngage: boolean): void {
    const hx = fromFp(this.heroFx);
    const hy = fromFp(this.heroFy);

    for (const e of this.fieldEnemies) {
      if (!e.alive) continue;
      e.animFrame++;
      if (e.alertFrames > 0) e.alertFrames--;

      const ex = fromFp(e.fx);
      const ey = fromFp(e.fy);
      const dx = hx - ex;
      const dy = hy - ey;
      const distSq = dx * dx + dy * dy;

      const sightRadius = e.isBoss ? 54 : 68;
      if (canEngage && this.heroSafeFrames === 0 && distSq < sightRadius * sightRadius) {
        if (!e.alerted) {
          e.alerted = true;
          e.alertFrames = 28;
          this.sfxQueue.push(SfxId.ENEMY_ALERT);
        }
        // Pursue hero fairly (slower than hero dash!)
        const dist = Math.max(1, Math.sqrt(distSq));
        const chaseSpeed = e.isBoss ? 140 : 210;
        const nvx = Math.round((dx / dist) * chaseSpeed);
        const nvy = Math.round((dy / dist) * chaseSpeed);
        const nextFx = e.fx + nvx;
        const nextFy = e.fy + nvy;
        if (this.getColAtPixel(fromFp(nextFx), fromFp(e.fy)) !== COL_SOLID &&
            this.getColAtPixel(fromFp(nextFx), fromFp(e.fy)) !== COL_WATER) {
          e.fx = nextFx;
        }
        if (this.getColAtPixel(fromFp(e.fx), fromFp(nextFy)) !== COL_SOLID &&
            this.getColAtPixel(fromFp(e.fx), fromFp(nextFy)) !== COL_WATER) {
          e.fy = nextFy;
        }
        e.dir = Math.abs(dx) > Math.abs(dy) ? (dx < 0 ? DIR_LEFT : DIR_RIGHT) : (dy < 0 ? DIR_UP : DIR_DOWN);

        // Engage seamless in-place combat on contact!
        if (distSq < 15 * 15) {
          this.mode = 'BATTLE';
          const { width } = this.getPpuSize();
          this.battle.startEncounter(
            e.preset,
            e.biome,
            e.id,
            this.activeSlot,
            this.potions,
            width,
            this.getBattleCallbacks()
          );
          return;
        }
      } else {
        e.alerted = false;
        if (!e.isBoss) {
          // Gentle patrol loop around home anchor
          e.patrolTimer--;
          if (e.patrolTimer <= 0) {
            e.patrolTimer = this.prng.range(55, 110);
            const hdx = fromFp(e.homeFx - e.fx);
            const hdy = fromFp(e.homeFy - e.fy);
            if (hdx * hdx + hdy * hdy > 32 * 32) {
              e.vx = Math.sign(hdx) * 90;
              e.vy = Math.sign(hdy) * 90;
            } else {
              const dirs = [
                [0, 0],
                [85, 0],
                [-85, 0],
                [0, 85],
                [0, -85],
              ];
              const pick = dirs[this.prng.range(0, dirs.length - 1)];
              e.vx = pick[0];
              e.vy = pick[1];
            }
          }
          const nfx = e.fx + e.vx;
          const nfy = e.fy + e.vy;
          const c = this.getColAtPixel(fromFp(nfx), fromFp(nfy));
          if (c !== COL_SOLID && c !== COL_WATER) {
            e.fx = nfx;
            e.fy = nfy;
          } else {
            e.vx = 0;
            e.vy = 0;
          }
        }
      }
    }
  }

  /* ========================================================================
   * INTERACTION QUERIES, HEART-ORB TRANSFERS & BYTECODE CUTSCENES
   * ======================================================================== */
  private getFacingTargetPoint(): { px: number; py: number } {
    const cx = fromFp(this.heroFx);
    const cy = fromFp(this.heroFy);
    const reach = 15;
    if (this.heroDir === DIR_UP) return { px: cx, py: cy - reach };
    if (this.heroDir === DIR_DOWN) return { px: cx, py: cy + reach };
    if (this.heroDir === DIR_LEFT) return { px: cx - reach, py: cy };
    return { px: cx + reach, py: cy };
  }

  private findNearestInteractable(): {
    kind: 'npc' | 'waystone' | 'chest' | 'sign';
    id: string;
    label: string;
    x: number;
    y: number;
  } | null {
    const { px, py } = this.getFacingTargetPoint();
    const hx = fromFp(this.heroFx);
    const hy = fromFp(this.heroFy);

    for (const npc of this.npcs) {
      const nx = fromFp(npc.fx);
      const ny = fromFp(npc.fy);
      if (Math.hypot(px - nx, py - ny) < 18 || Math.hypot(hx - nx, hy - ny) < 22) {
        return { kind: 'npc', id: npc.id, label: npc.name, x: nx, y: ny - 24 };
      }
    }

    // Healing Waystone at (8, 17)
    const wx = 8 * TILE_SIZE + 8;
    const wy = 17 * TILE_SIZE + 8;
    if (Math.hypot(px - wx, py - wy) < 18 || Math.hypot(hx - wx, hy - wy) < 22) {
      return { kind: 'waystone', id: 'waystone', label: 'Waystone', x: wx, y: wy - 22 };
    }

    // Treasure Chest at (26, 6)
    const chx = 26 * TILE_SIZE + 8;
    const chy = 6 * TILE_SIZE + 8;
    if (!this.locket.chestOpened && (Math.hypot(px - chx, py - chy) < 18 || Math.hypot(hx - chx, hy - chy) < 22)) {
      return { kind: 'chest', id: 'chest', label: 'Chest', x: chx, y: chy - 18 };
    }

    // Signpost at (16, 15)
    const sx = 16 * TILE_SIZE + 8;
    const sy = 15 * TILE_SIZE + 8;
    if (Math.hypot(px - sx, py - sy) < 18 || Math.hypot(hx - sx, hy - sy) < 21) {
      return { kind: 'sign', id: 'sign', label: 'Read Sign', x: sx, y: sy - 18 };
    }

    return null;
  }

  private updateInteractPrompt(): void {
    const hit = this.findNearestInteractable();
    if (hit) {
      this.currentInteractLabel = hit.label;
      this.currentInteractX = hit.x;
      this.currentInteractY = hit.y;
    } else {
      this.currentInteractLabel = '';
    }
  }

  private tryInteract(): void {
    const hit = this.findNearestInteractable();
    if (!hit) return;

    if (hit.kind === 'waystone') {
      this.healPartyFull();
      this.openDialogue(
        'Waystone',
        'pip',
        SfxId.TEXT_PIP,
        'Warm spring light fills Rowan, Bramblekit, and Lumina! Full HP & MP restored.',
        []
      );
      return;
    }

    if (hit.kind === 'chest') {
      this.locket.chestOpened = true;
      this.potions += 2;
      this.sfxQueue.push(SfxId.CHEST_OPEN);
      this.spawnParticles('SPARK_GOLD', hit.x, hit.y + 10, 14, '#F5B841', false);
      this.spawnFloatingText(hit.x, hit.y, '+2 STAR TONICS!', '#F5B841', 1);
      this.openDialogue(
        'Treasure Chest',
        'hero',
        SfxId.TEXT_HERO,
        'Found 2 Star Tonics! Press Use Item during combat to restore HP and MP to the whole team.',
        []
      );
      return;
    }

    if (hit.kind === 'sign') {
      this.sfxQueue.push(SfxId.CONFIRM);
      this.openDialogue(
        'Hamlet Signpost',
        'none',
        SfxId.TEXT_ELDER,
        'WEST: Windmill Hamlet & Waystone   |   EAST: Timber Bridge, Whispering Meadow & Briar Shrine   |   SOUTH: Pebble Ford.',
        []
      );
      return;
    }

    if (hit.kind === 'npc') {
      const npc = this.npcs.find((n) => n.id === hit.id);
      if (!npc) return;

      // Turn NPC smoothly to face Rowan!
      const dx = fromFp(this.heroFx - npc.fx);
      const dy = fromFp(this.heroFy - npc.fy);
      npc.dir = Math.abs(dx) > Math.abs(dy) ? (dx < 0 ? DIR_LEFT : DIR_RIGHT) : (dy < 0 ? DIR_UP : DIR_DOWN);

      if (npc.id === 'elder') {
        if (!this.locket.elderCutsceneSeen) {
          this.runCutscene(CUTSCENE_ELDER_TALK);
        } else {
          this.openDialogue(
            'Elder Wren',
            'elder',
            SfxId.TEXT_ELDER,
            'Try pressing L / R (or Q / E) mid-combat to hot-swap between Rowan, Bramblekit, and Lumina without spending your turn—and tap Space right on impact for a double follow-up!',
            [
              { label: 'Replay Hamlet Cutscene', actionId: 'REPLAY_INTRO' },
              { label: 'Jump to Shrine Boss', actionId: 'JUMP_BOSS' },
              { label: 'Keep Exploring', actionId: 'CLOSE' },
            ]
          );
        }
      } else if (npc.id === 'clara') {
        if (!this.locket.hasCourage && !this.locket.guardMended) {
          this.openDialogue(
            'Clara',
            'clara',
            SfxId.TEXT_PIP,
            "My heart's brimming with Courage today! Need a spark for your brass locket to help poor Guard Bram over by the bridge?",
            [
              { label: 'Cast Take Heart (Courage)', actionId: 'TAKE_COURAGE' },
              { label: 'Maybe Later', actionId: 'CLOSE' },
            ]
          );
        } else {
          this.openDialogue(
            'Clara',
            'clara',
            SfxId.TEXT_PIP,
            this.locket.guardMended
              ? "I saw Guard Bram standing tall again from here! Isn't it amazing how sharing a little courage lifts two hearts at once?"
              : "You've got a bright spark of Courage tucked in your locket—carry it east across the bridge to Guard Bram!",
            []
          );
        }
      } else if (npc.id === 'guard') {
        if (this.locket.guardMended) {
          this.openDialogue(
            'Guard Bram',
            'guard',
            SfxId.TEXT_ELDER,
            "My knees feel solid as oak again! Go test your mettle against the roaming monsters in Whispering Meadow—or the Briar Warden inside the eastern Shrine!",
            []
          );
        } else if (this.locket.hasCourage) {
          this.openDialogue(
            'Guard Bram',
            'guard',
            SfxId.TEXT_ELDER,
            "B-brr... the mist from the Shrine chills me to the bone. Wait—is that golden light coming from your locket?",
            [
              { label: 'Cast Give Heart (Courage)', actionId: 'GIVE_COURAGE' },
              { label: 'Hold On', actionId: 'CLOSE' },
            ]
          );
        } else {
          this.openDialogue(
            'Guard Bram',
            'guard',
            SfxId.TEXT_ELDER,
            "U-uh... my heart feels hollow with dread ever since the Briar Warden stirred. You can still slip past me into the Meadow, or ask Clara back in the square if she has Courage to spare!",
            []
          );
        }
      }
    }
  }

  private openDialogue(
    speaker: string,
    portrait: PortraitId,
    voice: SfxId,
    text: string,
    choices: DialogueChoice[],
    onClose?: () => void
  ): void {
    this.dialogue = {
      active: true,
      speaker,
      portrait,
      fullText: text,
      visibleChars: 0,
      finishedTyping: false,
      choices,
      selectedChoice: 0,
      sfxVoice: voice,
    };
    this.dialoguePauseFrames = 0;
    this.dialogueOnClose = onClose || null;
  }

  private stepDialogue(justPressed: number): void {
    const d = this.dialogue;
    if (!d.active) return;

    if (!d.finishedTyping) {
      if (justPressed & (PAD_A | PAD_B)) {
        d.visibleChars = d.fullText.length;
        d.finishedTyping = true;
        return;
      }
      if (this.dialoguePauseFrames > 0) {
        this.dialoguePauseFrames--;
        return;
      }
      // Crisp 16-bit typewriter cadence with natural punctuation pauses!
      d.visibleChars = Math.min(d.fullText.length, d.visibleChars + 1);
      const ch = d.fullText[d.visibleChars - 1];
      if (ch === ',' || ch === '—') {
        this.dialoguePauseFrames = 4;
      } else if (ch === '.' || ch === '!' || ch === '?') {
        this.dialoguePauseFrames = 8;
      } else if (d.visibleChars % 3 === 0 && ch !== ' ') {
        this.sfxQueue.push(d.sfxVoice);
      }
      if (d.visibleChars >= d.fullText.length) {
        d.finishedTyping = true;
      }
    } else {
      if (d.choices.length > 0) {
        if (justPressed & (PAD_UP | PAD_LEFT)) {
          d.selectedChoice = (d.selectedChoice + d.choices.length - 1) % d.choices.length;
          this.sfxQueue.push(SfxId.CURSOR);
        } else if (justPressed & (PAD_DOWN | PAD_RIGHT)) {
          d.selectedChoice = (d.selectedChoice + 1) % d.choices.length;
          this.sfxQueue.push(SfxId.CURSOR);
        } else if (justPressed & PAD_A) {
          this.confirmDialogueChoice();
        } else if (justPressed & PAD_B) {
          d.active = false;
          this.sfxQueue.push(SfxId.CANCEL);
        }
      } else if (justPressed & (PAD_A | PAD_B)) {
        this.advanceDialogue();
      }
    }
  }

  private advanceDialogue(): void {
    const d = this.dialogue;
    if (!d.finishedTyping) {
      d.visibleChars = d.fullText.length;
      d.finishedTyping = true;
      return;
    }
    if (d.choices.length > 0) {
      this.confirmDialogueChoice();
      return;
    }
    d.active = false;
    this.sfxQueue.push(SfxId.CONFIRM);
    const cb = this.dialogueOnClose;
    this.dialogueOnClose = null;
    if (cb) cb();
  }

  private confirmDialogueChoice(): void {
    const choice = this.dialogue.choices[this.dialogue.selectedChoice];
    this.dialogue.active = false;
    this.sfxQueue.push(SfxId.CONFIRM);
    if (!choice) return;

    if (choice.actionId === 'TAKE_COURAGE') {
      this.runCutscene(CUTSCENE_TAKE_COURAGE);
    } else if (choice.actionId === 'GIVE_COURAGE') {
      this.runCutscene(CUTSCENE_GIVE_COURAGE);
    } else if (choice.actionId === 'REPLAY_INTRO') {
      this.runCutscene(CUTSCENE_INTRO_DEMO);
    } else if (choice.actionId === 'JUMP_BOSS') {
      this.startQuickBattle('SHRINE_BOSS');
    }
  }

  /* Bytecode Cutscene VM */
  private runCutscene(script: CutsceneOp[]): void {
    this.mode = 'CUTSCENE';
    this.cutsceneQueue = script;
    this.cutsceneIp = 0;
    this.cutsceneWaitFrames = 0;
    this.dialogue.active = false;
  }

  private getActorPixelPos(id: ActorTargetId): { x: number; y: number } {
    if (id === 'hero') return { x: fromFp(this.heroFx), y: fromFp(this.heroFy) };
    if (id === 'pip') return { x: fromFp(this.pipFx), y: fromFp(this.pipFy) };
    const npc = this.npcs.find((n) => n.id === id);
    if (npc) return { x: fromFp(npc.fx), y: fromFp(npc.fy) };
    return { x: fromFp(this.heroFx), y: fromFp(this.heroFy) };
  }

  private stepCutscene(justPressed: number): void {
    // Update active Heart-Orb arc animation if running
    if (this.heartOrb.active) {
      this.heartOrb.frame++;
      const t = Math.min(1, this.heartOrb.frame / this.heartOrb.duration);
      const ox = Math.round(this.heartOrb.startX + (this.heartOrb.endX - this.heartOrb.startX) * t);
      const oy = Math.round(
        this.heartOrb.startY + (this.heartOrb.endY - this.heartOrb.startY) * t - Math.sin(t * Math.PI) * 32
      );
      if (this.heartOrb.frame % 2 === 0) {
        this.spawnSingleParticle('HEART_MOTE', ox, oy, this.prng.range(-25, 25), this.prng.range(-30, 10), 26, 2, this.heartOrb.color, false);
      }
      if (this.heartOrb.frame >= this.heartOrb.duration) {
        this.heartOrb.active = false;
        this.spawnParticles('SPARK_GOLD', this.heartOrb.endX, this.heartOrb.endY - 8, 16, this.heartOrb.color, false);
        this.flashAlpha = 0.28;
      }
    }

    if (this.dialogue.active) {
      this.stepDialogue(justPressed);
      return;
    }

    if (this.cutsceneWaitFrames > 0) {
      this.cutsceneWaitFrames--;
      return;
    }

    // Execute non-blocking opcodes until one blocks or finishes
    while (this.cutsceneIp < this.cutsceneQueue.length) {
      const op = this.cutsceneQueue[this.cutsceneIp++];
      switch (op.op) {
        case 'WAIT':
          this.cutsceneWaitFrames = op.frames ?? 20;
          return;

        case 'PAN_CAM': {
          const { width, height } = this.getPpuSize();
          const tx = Math.max(0, Math.min(MAP_W * TILE_SIZE - width, (op.x ?? 10) * TILE_SIZE - (width >> 1)));
          const ty = Math.max(0, Math.min(MAP_H * TILE_SIZE - height, (op.y ?? 12) * TILE_SIZE - (height >> 1)));
          this.camOverride = true;
          this.camStartFx = this.camFx;
          this.camStartFy = this.camFy;
          this.camTargetFx = toFp(tx);
          this.camTargetFy = toFp(ty);
          this.camPanFrame = 0;
          this.camPanDur = Math.max(1, op.frames ?? 30);
          this.cutsceneWaitFrames = this.camPanDur;
          return;
        }

        case 'CAM_FOLLOW': {
          const { width, height } = this.getPpuSize();
          const tx = Math.max(0, Math.min(MAP_W * TILE_SIZE - width, fromFp(this.heroFx) - (width >> 1)));
          const ty = Math.max(0, Math.min(MAP_H * TILE_SIZE - height, fromFp(this.heroFy) - (height >> 1)));
          this.camStartFx = this.camFx;
          this.camStartFy = this.camFy;
          this.camTargetFx = toFp(tx);
          this.camTargetFy = toFp(ty);
          this.camPanFrame = 0;
          this.camPanDur = Math.max(1, op.frames ?? 24);
          this.cutsceneWaitFrames = this.camPanDur;
          this.camOverride = true;
          return;
        }

        case 'FACE_ACTOR': {
          if (op.target === 'hero' && op.dir !== undefined) this.heroDir = op.dir;
          else if (op.target === 'pip' && op.dir !== undefined) this.pipDir = op.dir;
          else {
            const npc = this.npcs.find((n) => n.id === op.target);
            if (npc && op.dir !== undefined) npc.dir = op.dir;
          }
          break;
        }

        case 'EMOTE': {
          const dur = op.frames ?? 30;
          const em = op.emote ?? 'spark';
          if (op.target === 'hero') {
            this.heroEmote = em;
            this.heroEmoteFrames = dur;
          } else if (op.target === 'pip') {
            this.pipEmote = em;
            this.pipEmoteFrames = dur;
          } else {
            const npc = this.npcs.find((n) => n.id === op.target);
            if (npc) {
              npc.emote = em;
              npc.emoteFrames = dur;
            }
          }
          this.cutsceneWaitFrames = Math.min(18, dur);
          return;
        }

        case 'SFX':
          if (op.sfx) this.sfxQueue.push(op.sfx);
          break;

        case 'SHAKE':
          this.shakeIntensity = op.intensity ?? 2;
          this.shakeFrames = op.frames ?? 12;
          break;

        case 'HEART_ORB': {
          const fromPos = this.getActorPixelPos(op.target ?? 'clara');
          const toPos = this.getActorPixelPos(op.toTarget ?? 'hero');
          this.heartOrb = {
            active: true,
            startX: fromPos.x,
            startY: fromPos.y - 12,
            endX: toPos.x,
            endY: toPos.y - 12,
            frame: 0,
            duration: op.frames ?? 42,
            color: op.orbColor ?? '#F5B841',
          };
          this.cutsceneWaitFrames = op.frames ?? 42;
          return;
        }

        case 'SET_FLAG': {
          if (op.flag === 'hasCourage') {
            this.locket.hasCourage = true;
            const clara = this.npcs.find((n) => n.id === 'clara');
            if (clara) clara.aura = 'MENDED';
          } else if (op.flag === 'guardMended') {
            this.locket.guardMended = true;
            this.locket.hasCourage = false;
            this.potions += 1;
            const guard = this.npcs.find((n) => n.id === 'guard');
            if (guard) {
              guard.aura = 'MENDED';
              this.spawnFloatingText(fromFp(guard.fx), fromFp(guard.fy) - 24, 'HEART MENDED!', '#F5B841', 1.15);
            }
          } else if (op.flag === 'elderCutsceneSeen') {
            this.locket.elderCutsceneSeen = true;
          }
          break;
        }

        case 'SAY': {
          this.openDialogue(
            op.speaker ?? 'Pip',
            op.portrait ?? 'pip',
            op.voice ?? SfxId.TEXT_PIP,
            op.text ?? '',
            []
          );
          return;
        }

        case 'END': {
          this.camOverride = false;
          this.mode = this.returnToTitleAfterCutscene ? 'TITLE' : 'EXPLORE';
          this.returnToTitleAfterCutscene = false;
          return;
        }
      }
    }

    this.camOverride = false;
    this.mode = 'EXPLORE';
  }

  /* ========================================================================
   * CAMERA SPRING & PARTICLE POOLS
   * ======================================================================== */
  private snapCameraToHero(): void {
    const { width, height } = this.getPpuSize();
    const tx = Math.max(0, Math.min(MAP_W * TILE_SIZE - width, fromFp(this.heroFx) - (width >> 1)));
    const ty = Math.max(0, Math.min(MAP_H * TILE_SIZE - height, fromFp(this.heroFy) - (height >> 1)));
    this.camFx = toFp(tx);
    this.camFy = toFp(ty);
  }

  private stepCameraFollow(): void {
    const { width, height } = this.getPpuSize();
    if (this.camOverride) {
      if (this.camPanFrame < this.camPanDur) {
        this.camPanFrame++;
        const t = this.camPanFrame / this.camPanDur;
        const ease = t * t * (3 - 2 * t);
        this.camFx = Math.round(this.camStartFx + (this.camTargetFx - this.camStartFx) * ease);
        this.camFy = Math.round(this.camStartFy + (this.camTargetFy - this.camStartFy) * ease);
      }
      return;
    }

    const goalX = toFp(Math.max(0, Math.min(MAP_W * TILE_SIZE - width, fromFp(this.heroFx) - (width >> 1))));
    const goalY = toFp(Math.max(0, Math.min(MAP_H * TILE_SIZE - height, fromFp(this.heroFy) - (height >> 1))));
    // Silky fixed-point spring follow
    this.camFx += ((goalX - this.camFx) >> 3);
    this.camFy += ((goalY - this.camFy) >> 3);
  }

  private spawnSingleParticle(
    kind: ParticleKind,
    x: number,
    y: number,
    vx: number,
    vy: number,
    maxLife: number,
    size: number,
    color: string,
    screenSpace: boolean
  ): void {
    for (let i = 0; i < PARTICLE_POOL_SIZE; i++) {
      const p = this.particles[i];
      if (!p.active) {
        p.active = true;
        p.kind = kind;
        p.fx = toFp(x);
        p.fy = toFp(y);
        p.vx = vx;
        p.vy = vy;
        p.life = maxLife;
        p.maxLife = maxLife;
        p.size = size;
        p.color = color;
        p.screenSpace = screenSpace;
        return;
      }
    }
  }

  private spawnParticles(
    kind: ParticleKind,
    x: number,
    y: number,
    count: number,
    color: string,
    screenSpace: boolean
  ): void {
    for (let i = 0; i < count; i++) {
      const vx = this.prng.range(-360, 360);
      const vy =
        kind === 'HEAL_MOTE' || kind === 'HEART_MOTE'
          ? this.prng.range(-260, -70)
          : this.prng.range(-380, 160);
      const life = this.prng.range(16, 32);
      const size = kind === 'SPARK_GOLD' || kind === 'SHATTER' ? this.prng.range(2, 3) : 2;
      this.spawnSingleParticle(kind, x + this.prng.range(-4, 4), y + this.prng.range(-4, 4), vx, vy, life, size, color, screenSpace);
    }
  }

  private stepParticles(): void {
    for (let i = 0; i < PARTICLE_POOL_SIZE; i++) {
      const p = this.particles[i];
      if (!p.active) continue;
      p.life--;
      if (p.life <= 0) {
        p.active = false;
        continue;
      }
      p.fx += p.vx;
      p.fy += p.vy;
      if (p.kind === 'GRASS' || p.kind === 'SHATTER') {
        p.vy += 22; // crisp gravity
      } else if (p.kind === 'SPARK_GOLD' || p.kind === 'SPARK_CYAN' || p.kind === 'SPARK_RED') {
        p.vx = (p.vx * 235) >> 8;
        p.vy = (p.vy * 235) >> 8;
      }
    }
  }

  private spawnFloatingText(x: number, y: number, text: string, color: string, scale = 1): void {
    for (let i = 0; i < FLOAT_TEXT_POOL_SIZE; i++) {
      const ft = this.floatingTexts[i];
      if (!ft.active) {
        ft.active = true;
        ft.x = x;
        ft.y = y;
        ft.vy = -1.1;
        ft.text = text;
        ft.color = color;
        ft.scale = scale;
        ft.life = 44;
        ft.maxLife = 44;
        return;
      }
    }
  }

  private stepFloatingTexts(): void {
    for (let i = 0; i < FLOAT_TEXT_POOL_SIZE; i++) {
      const ft = this.floatingTexts[i];
      if (!ft.active) continue;
      ft.life--;
      if (ft.life <= 0) {
        ft.active = false;
        continue;
      }
      ft.y += ft.vy;
      ft.vy *= 0.92;
    }
  }

  /* ========================================================================
   * BUILD HARDWARE-FAITHFUL PPU FRAME PACKET (OAM + CAM + BATTLE + DEBUG)
   * ======================================================================== */
  private buildFramePacket(): PpuFramePacket {
    const { width, height, oamLimit } = this.getPpuSize();
    const camX = fromFp(this.camFx);
    const camY = fromFp(this.camFy);

    let shakeX = 0;
    let shakeY = 0;
    if (this.shakeFrames > 0) {
      shakeX = this.prng.range(-this.shakeIntensity, this.shakeIntensity);
      shakeY = this.prng.range(-this.shakeIntensity, this.shakeIntensity);
    }

    const sprites: OamSprite[] = [];
    const debugBoxes: DebugBox[] = [];

    if (this.mode !== 'BATTLE' && this.mode !== 'BATTLE_VICTORY' && this.mode !== 'BATTLE_DEFEAT') {
      // Windmill rotating blades sprite at tile (6, 5)
      sprites.push({
        x: 6 * TILE_SIZE + 8 - camX,
        y: 5 * TILE_SIZE + 4 - camY,
        spriteId: 'windmill_blades',
        frame: 0,
        dir: DIR_DOWN,
        flipX: false,
        scaleXFp: FP_ONE,
        scaleYFp: FP_ONE,
        flashWhite: false,
        tintHex: '',
        shadow: false,
        aura: 'NONE',
        sortY: 2000,
        layer: 2,
        rotationDeg: (this.frameCount * 0.7) % 360,
      });

      // Healing Waystone sprite at (8, 17)
      sprites.push({
        x: 8 * TILE_SIZE + 8 - camX,
        y: 17 * TILE_SIZE + 12 - camY,
        spriteId: 'waystone',
        frame: (this.frameCount >> 4) & 3,
        dir: DIR_DOWN,
        flipX: false,
        scaleXFp: FP_ONE,
        scaleYFp: FP_ONE,
        flashWhite: false,
        tintHex: '',
        shadow: true,
        aura: 'NONE',
        sortY: 17 * TILE_SIZE + 12,
        layer: 1,
        rotationDeg: 0,
      });

      // Treasure Chest at (26, 6)
      sprites.push({
        x: 26 * TILE_SIZE + 8 - camX,
        y: 6 * TILE_SIZE + 12 - camY,
        spriteId: 'chest',
        frame: this.locket.chestOpened ? 1 : 0,
        dir: DIR_DOWN,
        flipX: false,
        scaleXFp: FP_ONE,
        scaleYFp: FP_ONE,
        flashWhite: false,
        tintHex: '',
        shadow: true,
        aura: 'NONE',
        sortY: 6 * TILE_SIZE + 12,
        layer: 1,
        rotationDeg: 0,
      });

      // NPCs
      for (const npc of this.npcs) {
        const nx = fromFp(npc.fx);
        const ny = fromFp(npc.fy);
        sprites.push({
          x: nx - camX,
          y: ny - camY,
          spriteId: npc.spriteId,
          frame: (this.frameCount >> 5) & 1,
          dir: npc.dir,
          flipX: false,
          scaleXFp: FP_ONE,
          scaleYFp: FP_ONE,
          flashWhite: false,
          tintHex: '',
          shadow: true,
          aura: npc.aura,
          sortY: ny,
          layer: 1,
          rotationDeg: 0,
        });
        if (npc.emote !== 'none') {
          sprites.push(this.makeEmoteSprite(nx - camX, ny - 25 - camY, npc.emote));
        }
      }

      // Field Roaming Enemies
      for (const fe of this.fieldEnemies) {
        if (!fe.alive) continue;
        const ex = fromFp(fe.fx);
        const ey = fromFp(fe.fy);
        sprites.push({
          x: ex - camX,
          y: ey - camY,
          spriteId: fe.spriteId,
          frame: (fe.animFrame >> 4) & 1,
          dir: fe.dir,
          flipX: fe.dir === DIR_RIGHT,
          scaleXFp: FP_ONE,
          scaleYFp: FP_ONE,
          flashWhite: false,
          tintHex: '',
          shadow: true,
          aura: 'NONE',
          sortY: ey,
          layer: 1,
          rotationDeg: 0,
        });
        if (fe.alertFrames > 0) {
          sprites.push(this.makeEmoteSprite(ex - camX, ey - 26 - camY, 'bang'));
        }
        if (this.tuning.showHitboxes) {
          debugBoxes.push({ x: ex - 8 - camX, y: ey - 8 - camY, w: 16, h: 16, color: '#E04E4E' });
        }
      }

      // Hero (Rowan)
      const hx = fromFp(this.heroFx);
      const hy = fromFp(this.heroFy);
      const walkFrame = this.heroAnimTick === 0 ? 0 : ((this.heroAnimTick >> 3) & 3);
      if (this.heroSafeFrames === 0 || (this.heroSafeFrames & 4) === 0) {
        sprites.push({
          x: hx - camX,
          y: hy - camY,
          spriteId: 'hero',
          frame: walkFrame,
          dir: this.heroDir,
          flipX: false,
          scaleXFp: this.heroDashing ? 270 : FP_ONE,
          scaleYFp: this.heroDashing ? 244 : FP_ONE,
          flashWhite: false,
          tintHex: '',
          shadow: true,
          aura: 'NONE',
          sortY: hy,
          layer: 1,
          rotationDeg: 0,
        });
      }
      if (this.heroEmote !== 'none') {
        sprites.push(this.makeEmoteSprite(hx - camX, hy - 26 - camY, this.heroEmote));
      }
      if (this.tuning.showHitboxes) {
        debugBoxes.push({ x: hx - 5 - camX, y: hy - 3 - camY, w: 10, h: 6, color: '#5AD2F4' });
      }

      // Pip the Hovering Lantern Companion
      const px = fromFp(this.pipFx);
      const py = fromFp(this.pipFy);
      const hoverBob = Math.round(Math.sin(this.frameCount * 0.12) * 2.5);
      sprites.push({
        x: px - camX,
        y: py - 4 + hoverBob - camY,
        spriteId: 'pip',
        frame: (this.frameCount >> 3) & 1,
        dir: this.pipDir,
        flipX: this.pipDir === DIR_LEFT,
        scaleXFp: FP_ONE,
        scaleYFp: FP_ONE,
        flashWhite: false,
        tintHex: '',
        shadow: true,
        aura: 'NONE',
        sortY: py,
        layer: 1,
        rotationDeg: 0,
      });
      if (this.pipEmote !== 'none') {
        sprites.push(this.makeEmoteSprite(px - camX, py - 24 - camY, this.pipEmote));
      }

      // Active Heart-Orb Transfer Projectile
      if (this.heartOrb.active) {
        const t = Math.min(1, this.heartOrb.frame / this.heartOrb.duration);
        const ox = Math.round(this.heartOrb.startX + (this.heartOrb.endX - this.heartOrb.startX) * t);
        const oy = Math.round(
          this.heartOrb.startY + (this.heartOrb.endY - this.heartOrb.startY) * t - Math.sin(t * Math.PI) * 32
        );
        sprites.push({
          x: ox - camX,
          y: oy - camY,
          spriteId: 'heart_orb',
          frame: (this.frameCount >> 2) & 1,
          dir: DIR_DOWN,
          flipX: false,
          scaleXFp: FP_ONE,
          scaleYFp: FP_ONE,
          flashWhite: false,
          tintHex: this.heartOrb.color,
          shadow: false,
          aura: 'NONE',
          sortY: 3000,
          layer: 2,
          rotationDeg: 0,
        });
      }

      // Sort layer 1 sprites strictly by Y-depth
      sprites.sort((a, b) => (a.layer !== b.layer ? a.layer - b.layer : a.sortY - b.sortY));
    }

    const oamCount =
      this.mode === 'BATTLE'
        ? 6 + this.battle.state.enemies.filter((e) => e.alive).length * 4
        : sprites.length;
    this.lastOamCount = oamCount;

    return {
      width,
      height,
      frameCount: this.frameCount,
      mode: this.mode,
      camX,
      camY,
      shakeX,
      shakeY,
      fadeLevel: this.fadeLevel,
      flashAlpha: this.flashAlpha,
      hitstopActive: this.hitstopFrames > 0,
      sprites,
      oamCount,
      oamLimit,
      particles: this.particles,
      floatingTexts: this.floatingTexts,
      interactPrompt: {
        active: this.mode === 'EXPLORE' && !this.dialogue.active && this.currentInteractLabel !== '',
        x: this.currentInteractX - camX,
        y: this.currentInteractY - camY,
        label: this.currentInteractLabel,
      },
      battle: this.battle.state,
      debugBoxes,
      sfxQueue: [...this.sfxQueue],
    };
  }

  private makeEmoteSprite(x: number, y: number, emote: 'bang' | 'heart' | 'question' | 'spark'): OamSprite {
    const idMap: Record<typeof emote, SpriteId> = {
      bang: 'emote_bang',
      heart: 'emote_heart',
      question: 'emote_question',
      spark: 'emote_spark',
    };
    return {
      x,
      y,
      spriteId: idMap[emote],
      frame: (this.frameCount >> 3) & 1,
      dir: DIR_DOWN,
      flipX: false,
      scaleXFp: FP_ONE,
      scaleYFp: FP_ONE,
      flashWhite: false,
      tintHex: '',
      shadow: false,
      aura: 'NONE',
      sortY: 2500,
      layer: 2,
      rotationDeg: 0,
    };
  }

  /* ========================================================================
   * LIGHTWEIGHT REACT UI SNAPSHOT
   * ======================================================================== */
  public getUiSnapshot(): UiSnapshot {
    const tx = Math.floor(fromFp(this.heroFx) / TILE_SIZE);
    const ty = Math.floor(fromFp(this.heroFy) / TILE_SIZE);
    const zoneLabel =
      tx >= 35
        ? 'Ancient Stone Shrine'
        : tx >= 21
          ? 'Whispering Meadow'
          : 'Windmill Hamlet';

    let objectiveLabel = 'Talk to Clara or cross the bridge to test combat';
    if (this.locket.bossDefeated && this.locket.guardMended) {
      objectiveLabel = 'Sandbox complete — tune feel, swap assets, or rematch!';
    } else if (this.locket.hasCourage && !this.locket.guardMended) {
      objectiveLabel = 'Bring Courage from your Locket to Guard Bram at the bridge';
    } else if (this.locket.guardMended && !this.locket.bossDefeated) {
      objectiveLabel = 'Challenge the Briar Warden at the eastern Stone Shrine';
    }

    const { oamLimit } = this.getPpuSize();
    const b = this.battle.state;

    return {
      mode: this.mode,
      zoneLabel,
      objectiveLabel,
      dialogue: { ...this.dialogue, choices: [...this.dialogue.choices] },
      party: this.party.map((m) => ({ ...m })),
      activeSlot: this.activeSlot,
      potions: this.potions,
      locket: { ...this.locket },
      battlePhase: b.phase,
      battleSelectedCommand: b.selectedCommand,
      battleSelectedEnemy: b.selectedEnemy,
      battleBanner: b.bannerFrames > 0 ? b.bannerText : '',
      battleEnemies: b.enemies.map((e) => ({
        id: e.id,
        name: e.name,
        hp: e.hp,
        maxHp: e.maxHp,
        alive: e.alive,
        isBoss: e.isBoss,
        staggered: e.staggered,
        locks: [...e.locks],
        brokenLocks: [...e.brokenLocks],
      })),
      battleStats: {
        combos: b.comboCount,
        parries: b.parryCount,
        locksBroken: b.locksBrokenCount,
        expEarned: b.expEarned,
      },
      timedRingActive: b.timedRing.active,
      timedRingKind: b.timedRing.kind,
      oamCount: this.lastOamCount,
      oamLimit,
      activeEnemyCount: this.fieldEnemies.filter((e) => e.alive).length,
      tuning: { ...this.tuning },
      playerTileX: tx,
      playerTileY: ty,
    };
  }
}
