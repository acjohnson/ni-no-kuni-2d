/**
 * Project Hearth — Tactile Turn-Based Familiar Combat Engine
 *
 * Pure 60Hz frame-counted state machine featuring:
 *  - Instant mid-turn Ni No Kuni-style Familiar Soul-Arc Hot-Swapping (L / R)
 *  - Sea of Stars-grade Timed Hits with Hitstop, Flash Frames, & Follow-Up Strikes
 *  - Enemy Telegraph + Timed Parry Blocks (+MP counter-spark)
 *  - Multi-element Spell Lock Breaking (SWORD / SUN / STAR) & Boss Staggering
 */

import { RetroPrng } from './prng';
import {
  BattleBiome,
  BattleEnemyState,
  BattleRenderState,
  EngineTuning,
  FamiliarSlot,
  FP_ONE,
  LockKind,
  PAD_A,
  PAD_B,
  PAD_DOWN,
  PAD_L,
  PAD_LEFT,
  PAD_R,
  PAD_RIGHT,
  PAD_UP,
  PartyMemberState,
  ParticleKind,
  SfxId,
} from './types';

export interface BattleCallbacks {
  playSfx: (sfx: SfxId) => void;
  addShake: (intensity: number, frames: number) => void;
  setHitstop: (frames: number) => void;
  setFlash: (alpha: number) => void;
  spawnParticles: (
    kind: ParticleKind,
    x: number,
    y: number,
    count: number,
    color: string,
    screenSpace: boolean
  ) => void;
  spawnText: (x: number, y: number, text: string, color: string, scale?: number) => void;
  onBattleEnd: (won: boolean, defeatedFieldId: string, isBoss: boolean) => void;
}

export function createInitialParty(): PartyMemberState[] {
  return [
    {
      slot: 0,
      id: 'rowan',
      name: 'Rowan',
      role: 'Heart Wizard',
      sign: 'HEART',
      spriteId: 'hero',
      hp: 84,
      maxHp: 84,
      mp: 32,
      maxMp: 32,
      atk: 15,
      def: 8,
      basicName: 'Locket Staff',
      basicKind: 'SWORD',
      skillName: 'Mending Dew',
      skillDesc: 'Restore 36 HP to active party & cure stagger',
      skillCost: 7,
      skillKind: 'HEAL',
    },
    {
      slot: 1,
      id: 'bramblekit',
      name: 'Bramblekit',
      role: 'Sun Familiar',
      sign: 'SUN',
      spriteId: 'familiar_bramble',
      hp: 96,
      maxHp: 96,
      mp: 24,
      maxMp: 24,
      atk: 19,
      def: 11,
      basicName: 'Acorn Rush',
      basicKind: 'SWORD',
      skillName: 'Solar Slam',
      skillDesc: 'Heavy Sun strike · Shatters [SUN] locks',
      skillCost: 6,
      skillKind: 'SUN',
    },
    {
      slot: 2,
      id: 'lumina',
      name: 'Lumina',
      role: 'Star Familiar',
      sign: 'STAR',
      spriteId: 'familiar_lumina',
      hp: 72,
      maxHp: 72,
      mp: 40,
      maxMp: 40,
      atk: 17,
      def: 7,
      basicName: 'Starlight Bolt',
      basicKind: 'STAR',
      skillName: 'Prism Nova',
      skillDesc: 'Radiant Star burst hitting ALL foes · Breaks [STAR]',
      skillCost: 9,
      skillKind: 'STAR',
    },
  ];
}

export type EncounterPreset = 'MEADOW_PACK' | 'GLADE_DUO' | 'SHRINE_BOSS';

export class BattleSystem {
  public state: BattleRenderState;
  private phaseTimer = 0;
  private activeAction: 'STRIKE' | 'SKILL' | 'ITEM' = 'STRIKE';
  private timedInputBuffered = 0; // 6-frame early input buffer so taps never feel eaten!
  private timedHitResult: 'NONE' | 'PERFECT' | 'GOOD' | 'MISS' = 'NONE';
  private activeEnemyTurnIdx = 0;
  private defeatedFieldEntityId = '';
  private isBossBattle = false;
  private heroHomeX = 76;
  private heroHomeY = 128;

  constructor() {
    this.state = this.createEmptyState();
  }

  private createEmptyState(): BattleRenderState {
    return {
      active: false,
      biome: 'MEADOW',
      phase: 'INTRO',
      activeSlot: 1, // Default start with Bramblekit summoned for immediate familiar feel!
      previousSlot: 0,
      swapArcProgress: 1,
      heroX: 76,
      heroY: 128,
      heroScaleXFp: FP_ONE,
      heroScaleYFp: FP_ONE,
      heroFlashFrames: 0,
      heroGuarding: false,
      wizardBackX: 38,
      wizardBackY: 132,
      selectedCommand: 0,
      selectedEnemy: 0,
      enemies: [],
      timedRing: {
        active: false,
        kind: 'OFFENSE',
        x: 0,
        y: 0,
        progress: 0,
        sweetStart: 0.78,
        sweetEnd: 1.05,
        result: 'NONE',
      },
      bannerText: '',
      bannerFrames: 0,
      comboCount: 0,
      parryCount: 0,
      locksBrokenCount: 0,
      expEarned: 0,
      potions: 3,
      introProgress: 0,
    };
  }

  public startEncounter(
    preset: EncounterPreset,
    biome: BattleBiome,
    fieldEntityId: string,
    activeSlot: FamiliarSlot,
    potions: number,
    ppuWidth: number,
    cb: BattleCallbacks
  ): void {
    const rightBase = ppuWidth === 256 ? 190 : 236;
    const leftBase = ppuWidth === 256 ? 68 : 84;
    this.heroHomeX = leftBase;
    this.heroHomeY = 126;

    this.defeatedFieldEntityId = fieldEntityId;
    this.isBossBattle = preset === 'SHRINE_BOSS';
    this.phaseTimer = 0;
    this.timedInputBuffered = 0;
    this.timedHitResult = 'NONE';

    let enemies: BattleEnemyState[] = [];
    if (preset === 'SHRINE_BOSS') {
      enemies = [
        {
          id: 'boss_briar',
          fieldEntityId,
          name: 'Briar Warden',
          spriteId: 'boss_briar_warden',
          isBoss: true,
          hp: 165,
          maxHp: 165,
          atk: 21,
          def: 5,
          homeX: rightBase,
          homeY: 126,
          x: rightBase + 60,
          y: 126,
          scaleXFp: FP_ONE,
          scaleYFp: FP_ONE,
          flashFrames: 0,
          hurtFrames: 0,
          staggered: false,
          chargeTurns: 2,
          maxChargeTurns: 2,
          specialName: 'Bramble Tempest',
          locks: ['SWORD', 'SUN', 'STAR'],
          brokenLocks: [false, false, false],
          alive: true,
          animFrame: 0,
        },
      ];
    } else if (preset === 'GLADE_DUO') {
      enemies = [
        {
          id: 'thorn_1',
          fieldEntityId,
          name: 'Thornling',
          spriteId: 'enemy_thornling',
          isBoss: false,
          hp: 58,
          maxHp: 58,
          atk: 14,
          def: 3,
          homeX: rightBase - 14,
          homeY: 108,
          x: rightBase + 56,
          y: 108,
          scaleXFp: FP_ONE,
          scaleYFp: FP_ONE,
          flashFrames: 0,
          hurtFrames: 0,
          staggered: false,
          chargeTurns: 2,
          maxChargeTurns: 2,
          specialName: 'Briar Flurry',
          locks: ['SWORD', 'SUN'],
          brokenLocks: [false, false],
          alive: true,
          animFrame: 0,
        },
        {
          id: 'moth_1',
          fieldEntityId,
          name: 'Dusk Moth',
          spriteId: 'enemy_dusk_moth',
          isBoss: false,
          hp: 48,
          maxHp: 48,
          atk: 13,
          def: 2,
          homeX: rightBase + 16,
          homeY: 142,
          x: rightBase + 80,
          y: 142,
          scaleXFp: FP_ONE,
          scaleYFp: FP_ONE,
          flashFrames: 0,
          hurtFrames: 0,
          staggered: false,
          chargeTurns: 1,
          maxChargeTurns: 2,
          specialName: 'Hollow Dust',
          locks: ['STAR'],
          brokenLocks: [false],
          alive: true,
          animFrame: 0,
        },
      ];
    } else {
      enemies = [
        {
          id: 'gloom_1',
          fieldEntityId,
          name: 'Gloomcap',
          spriteId: 'enemy_gloomcap',
          isBoss: false,
          hp: 52,
          maxHp: 52,
          atk: 13,
          def: 2,
          homeX: rightBase - 12,
          homeY: 112,
          x: rightBase + 52,
          y: 112,
          scaleXFp: FP_ONE,
          scaleYFp: FP_ONE,
          flashFrames: 0,
          hurtFrames: 0,
          staggered: false,
          chargeTurns: 2,
          maxChargeTurns: 2,
          specialName: 'Spore Burst',
          locks: ['SUN', 'STAR'],
          brokenLocks: [false, false],
          alive: true,
          animFrame: 0,
        },
        {
          id: 'thorn_2',
          fieldEntityId,
          name: 'Thornling',
          spriteId: 'enemy_thornling',
          isBoss: false,
          hp: 54,
          maxHp: 54,
          atk: 14,
          def: 3,
          homeX: rightBase + 14,
          homeY: 142,
          x: rightBase + 76,
          y: 142,
          scaleXFp: FP_ONE,
          scaleYFp: FP_ONE,
          flashFrames: 0,
          hurtFrames: 0,
          staggered: false,
          chargeTurns: 0,
          maxChargeTurns: 2,
          specialName: 'Briar Flurry',
          locks: [],
          brokenLocks: [],
          alive: true,
          animFrame: 0,
        },
      ];
    }

    this.state = {
      ...this.createEmptyState(),
      active: true,
      biome,
      phase: 'INTRO',
      activeSlot,
      previousSlot: activeSlot,
      heroX: leftBase - 54,
      heroY: this.heroHomeY,
      wizardBackX: Math.max(24, leftBase - 42),
      wizardBackY: this.heroHomeY + 4,
      enemies,
      potions,
      bannerText: this.isBossBattle ? 'SHRINE GUARDIAN: BRIAR WARDEN!' : 'ENCOUNTER!',
      bannerFrames: 64,
    };

    cb.playSfx(SfxId.BATTLE_SWOOP);
  }

  /** Allows mouse/UI buttons to issue direct battle commands cleanly */
  public triggerCommand(
    cmdIndex: number,
    party: PartyMemberState[],
    cb: BattleCallbacks
  ): void {
    if (this.state.phase !== 'COMMAND') return;
    this.state.selectedCommand = cmdIndex;
    this.executeSelectedCommand(party, cb);
  }

  public selectEnemy(idx: number, cb: BattleCallbacks): void {
    if (this.state.phase !== 'COMMAND') return;
    if (this.state.enemies[idx]?.alive) {
      this.state.selectedEnemy = idx;
      cb.playSfx(SfxId.CURSOR);
    }
  }

  public swapFamiliar(
    delta: -1 | 1 | 0,
    explicitSlot: FamiliarSlot | null,
    cb: BattleCallbacks
  ): void {
    if (this.state.phase !== 'COMMAND') return;
    const prev = this.state.activeSlot;
    let next: FamiliarSlot;
    if (explicitSlot !== null) {
      next = explicitSlot;
    } else {
      next = (((prev + delta) % 3 + 3) % 3) as FamiliarSlot;
    }
    if (next === prev) return;

    this.state.previousSlot = prev;
    this.state.activeSlot = next;
    this.state.phase = 'SWAP_ARC';
    this.phaseTimer = 0;
    this.state.swapArcProgress = 0;
    this.state.heroGuarding = false;
    cb.playSfx(SfxId.SUMMON_ARC);
    cb.spawnParticles('SOUL_ARC', this.state.wizardBackX + 8, this.state.wizardBackY - 12, 12, '#F5B841', true);
  }

  /** External click/tap anywhere on canvas during timed windows acts as PAD_A! */
  public triggerTimedTap(): void {
    this.timedInputBuffered = 7;
  }

  private getFirstAliveEnemy(): number {
    const idx = this.state.enemies.findIndex((e) => e.alive);
    return idx >= 0 ? idx : 0;
  }

  private executeSelectedCommand(party: PartyMemberState[], cb: BattleCallbacks): void {
    const actor = party[this.state.activeSlot];
    const cmd = this.state.selectedCommand;

    if (cmd === 0) {
      // STRIKE
      this.activeAction = 'STRIKE';
      this.state.heroGuarding = false;
      this.state.phase = 'PLAYER_WINDUP';
      this.phaseTimer = 0;
      this.timedHitResult = 'NONE';
      this.timedInputBuffered = 0;
      cb.playSfx(SfxId.CONFIRM);
    } else if (cmd === 1) {
      // SKILL
      if (actor.mp < actor.skillCost) {
        cb.playSfx(SfxId.CANCEL);
        cb.spawnText(this.state.heroX, this.state.heroY - 26, 'LOW MP!', '#E04E4E', 1);
        return;
      }
      actor.mp -= actor.skillCost;
      this.activeAction = 'SKILL';
      this.state.heroGuarding = false;
      this.state.phase = 'PLAYER_WINDUP';
      this.phaseTimer = 0;
      this.timedHitResult = 'NONE';
      this.timedInputBuffered = 0;
      cb.playSfx(SfxId.SPELL_CAST);
      this.state.bannerText = actor.skillName.toUpperCase() + '!';
      this.state.bannerFrames = 46;
    } else if (cmd === 2) {
      // GUARD (+6 MP Focus)
      this.state.heroGuarding = true;
      actor.mp = Math.min(actor.maxMp, actor.mp + 6);
      cb.playSfx(SfxId.PARRY_CLANG);
      cb.spawnParticles('SPARK_CYAN', this.state.heroX, this.state.heroY - 12, 10, '#5AD2F4', true);
      cb.spawnText(this.state.heroX, this.state.heroY - 28, 'GUARD +6 MP', '#5AD2F4', 1);
      this.beginEnemyTurnPhase();
    } else if (cmd === 3) {
      // STAR TONIC ITEM
      if (this.state.potions <= 0) {
        cb.playSfx(SfxId.CANCEL);
        cb.spawnText(this.state.heroX, this.state.heroY - 26, 'NO TONICS', '#E04E4E', 1);
        return;
      }
      this.state.potions -= 1;
      for (const m of party) {
        m.hp = Math.min(m.maxHp, m.hp + 45);
        m.mp = Math.min(m.maxMp, m.mp + 10);
      }
      cb.playSfx(SfxId.HEAL);
      cb.spawnParticles('HEAL_MOTE', this.state.heroX, this.state.heroY - 10, 14, '#74B461', true);
      cb.spawnText(this.state.heroX, this.state.heroY - 28, '+45 HP / +10 MP', '#74B461', 1);
      this.beginEnemyTurnPhase();
    }
  }

  private beginEnemyTurnPhase(): void {
    this.activeEnemyTurnIdx = 0;
    this.state.phase = 'ENEMY_SELECT';
    this.phaseTimer = 0;
    this.state.timedRing.active = false;
  }

  public step(
    justPressed: number,
    party: PartyMemberState[],
    tuning: EngineTuning,
    prng: RetroPrng,
    cb: BattleCallbacks
  ): number {
    const s = this.state;
    if (!s.active) return s.potions;

    // Decay banner, flash, and spring-scale recovery
    if (s.bannerFrames > 0) s.bannerFrames--;
    if (s.heroFlashFrames > 0) s.heroFlashFrames--;
    s.heroScaleXFp += ((FP_ONE - s.heroScaleXFp) >> 2);
    s.heroScaleYFp += ((FP_ONE - s.heroScaleYFp) >> 2);

    for (const e of s.enemies) {
      if (e.flashFrames > 0) e.flashFrames--;
      if (e.hurtFrames > 0) e.hurtFrames--;
      e.scaleXFp += ((FP_ONE - e.scaleXFp) >> 2);
      e.scaleYFp += ((FP_ONE - e.scaleYFp) >> 2);
      if (e.alive) e.animFrame++;
    }

    // Track early input buffer for ultra-responsive timed hits & parries
    if (justPressed & PAD_A) {
      this.timedInputBuffered = 6;
    } else if (this.timedInputBuffered > 0) {
      this.timedInputBuffered--;
    }

    const activeActor = party[s.activeSlot];
    this.phaseTimer++;

    switch (s.phase) {
      case 'INTRO': {
        const dur = 26;
        const t = Math.min(1, this.phaseTimer / dur);
        s.introProgress = t;
        // Ease-out cubic jump-in for hero and enemies
        const ease = 1 - Math.pow(1 - t, 3);
        s.heroX = Math.round(this.heroHomeX - 54 * (1 - ease));
        s.heroY = Math.round(this.heroHomeY - Math.sin(t * Math.PI) * 18);
        for (const e of s.enemies) {
          e.x = Math.round(e.homeX + 60 * (1 - ease));
          e.y = Math.round(e.homeY - Math.sin(t * Math.PI) * 14);
        }
        if (this.phaseTimer >= dur) {
          s.heroX = this.heroHomeX;
          s.heroY = this.heroHomeY;
          s.heroScaleXFp = 320;
          s.heroScaleYFp = 200;
          for (const e of s.enemies) {
            e.x = e.homeX;
            e.y = e.homeY;
          }
          s.phase = 'COMMAND';
          this.phaseTimer = 0;
          s.selectedEnemy = this.getFirstAliveEnemy();
        }
        break;
      }

      case 'COMMAND': {
        // Ensure target enemy is alive
        if (!s.enemies[s.selectedEnemy]?.alive) {
          s.selectedEnemy = this.getFirstAliveEnemy();
        }

        // Instant Familiar Hot-Swap via L / R!
        if (justPressed & PAD_L) {
          this.swapFamiliar(-1, null, cb);
          break;
        }
        if (justPressed & PAD_R) {
          this.swapFamiliar(1, null, cb);
          break;
        }

        // Command navigation (Left/Right or Up/Down)
        if (justPressed & PAD_LEFT) {
          s.selectedCommand = (s.selectedCommand + 3) % 4;
          cb.playSfx(SfxId.CURSOR);
        } else if (justPressed & PAD_RIGHT) {
          s.selectedCommand = (s.selectedCommand + 1) % 4;
          cb.playSfx(SfxId.CURSOR);
        } else if (justPressed & (PAD_UP | PAD_DOWN)) {
          // Cycle living target enemies
          const aliveIndices = s.enemies
            .map((e, i) => (e.alive ? i : -1))
            .filter((i) => i >= 0);
          if (aliveIndices.length > 1) {
            const cur = aliveIndices.indexOf(s.selectedEnemy);
            const stepDir = justPressed & PAD_UP ? -1 : 1;
            s.selectedEnemy =
              aliveIndices[(cur + stepDir + aliveIndices.length) % aliveIndices.length];
            cb.playSfx(SfxId.CURSOR);
          }
        }

        if (justPressed & PAD_A) {
          this.executeSelectedCommand(party, cb);
        }
        break;
      }

      case 'SWAP_ARC': {
        const dur = 16;
        const t = Math.min(1, this.phaseTimer / dur);
        s.swapArcProgress = t;
        // Parabolic summon burst from wizard back spot to active combat spot
        const arcX = s.wizardBackX + (this.heroHomeX - s.wizardBackX) * t;
        const arcY = this.heroHomeY - Math.sin(t * Math.PI) * 34;
        if (this.phaseTimer % 2 === 0) {
          cb.spawnParticles('SOUL_ARC', Math.round(arcX), Math.round(arcY - 10), 2, '#5AD2F4', true);
        }
        if (this.phaseTimer >= dur) {
          // Tactile squash-and-stretch landing!
          s.heroX = this.heroHomeX;
          s.heroY = this.heroHomeY;
          s.heroScaleXFp = 345;
          s.heroScaleYFp = 185;
          s.heroFlashFrames = 4;
          cb.spawnParticles('SPARK_GOLD', s.heroX, s.heroY - 4, 8, '#FFF6CC', true);
          cb.spawnText(s.heroX, s.heroY - 28, activeActor.name.toUpperCase() + '!', '#F5B841', 1);
          s.phase = 'COMMAND';
          this.phaseTimer = 0;
        }
        break;
      }

      case 'PLAYER_WINDUP': {
        const dur = 12;
        const t = this.phaseTimer / dur;
        // Crouch / pull back anticipation pose
        s.heroX = Math.round(this.heroHomeX - Math.sin(t * Math.PI) * 8);
        s.heroScaleXFp = 290;
        s.heroScaleYFp = 215;

        if (this.activeAction === 'SKILL' && activeActor.skillKind === 'HEAL') {
          if (this.phaseTimer >= dur) {
            // Instant party heal skill
            for (const m of party) {
              m.hp = Math.min(m.maxHp, m.hp + 38);
            }
            cb.playSfx(SfxId.HEAL);
            cb.spawnParticles('HEAL_MOTE', s.heroX, s.heroY - 10, 16, '#74B461', true);
            cb.spawnText(s.heroX, s.heroY - 30, '+38 HP!', '#74B461', 1);
            s.heroX = this.heroHomeX;
            this.beginEnemyTurnPhase();
          }
          break;
        }

        if (this.phaseTimer >= dur) {
          s.phase = 'PLAYER_LUNGE';
          this.phaseTimer = 0;
          this.timedHitResult = 'NONE';
          this.timedInputBuffered = 0;
        }
        break;
      }

      case 'PLAYER_LUNGE': {
        const target = s.enemies[s.selectedEnemy];
        const dur = 18;
        const t = Math.min(1, this.phaseTimer / dur);
        const ease = t * t * (3 - 2 * t);

        // Melee rushes right up to target; Star/Lumina glides mid-arena and fires lance
        const destX =
          activeActor.slot === 2
            ? Math.round((this.heroHomeX + target.homeX) * 0.45)
            : target.homeX - 24;
        const destY = activeActor.slot === 2 ? this.heroHomeY - 6 : target.homeY;

        s.heroX = Math.round(this.heroHomeX + (destX - this.heroHomeX) * ease);
        s.heroY = Math.round(this.heroHomeY + (destY - this.heroHomeY) * ease - Math.sin(t * Math.PI) * 14);
        s.heroScaleXFp = 230;
        s.heroScaleYFp = 282;

        // Show diegetic Golden Timed-Hit Convergence Ring on target!
        const halfWin = Math.max(4, tuning.timedHitWindowFrames);
        const sweetStartFrame = dur - halfWin;
        s.timedRing = {
          active: true,
          kind: 'OFFENSE',
          x: target.homeX,
          y: target.homeY - 14,
          progress: this.phaseTimer / dur,
          sweetStart: sweetStartFrame / dur,
          sweetEnd: 1.15,
          result: this.timedHitResult,
        };

        // Check for timed-hit confirm inside sweet window!
        if (this.timedHitResult === 'NONE' && this.timedInputBuffered > 0) {
          if (this.phaseTimer >= sweetStartFrame) {
            this.timedHitResult = 'PERFECT';
            s.timedRing.result = 'PERFECT';
            this.timedInputBuffered = 0;
          } else if (this.phaseTimer >= sweetStartFrame - 4) {
            this.timedHitResult = 'GOOD';
            s.timedRing.result = 'GOOD';
            this.timedInputBuffered = 0;
          }
        }

        if (this.phaseTimer >= dur) {
          s.phase = 'PLAYER_IMPACT';
          this.phaseTimer = 0;
          this.resolvePlayerStrike(party, tuning, prng, cb, false);
        }
        break;
      }

      case 'PLAYER_IMPACT': {
        // Brief post-impact window (allows slightly late taps too!)
        const lateFrames = Math.max(3, Math.floor(tuning.timedHitWindowFrames * 0.55));
        if (
          this.phaseTimer <= lateFrames &&
          this.timedHitResult === 'NONE' &&
          this.timedInputBuffered > 0
        ) {
          this.timedHitResult = 'PERFECT';
          s.timedRing.result = 'PERFECT';
          this.timedInputBuffered = 0;
          // Upgrade directly into acrobatic Timed Follow-Up!
          s.phase = 'PLAYER_FOLLOWUP';
          this.phaseTimer = 0;
          cb.playSfx(SfxId.HIT_CRITICAL);
          cb.spawnText(s.heroX, s.heroY - 32, 'PERFECT!', '#F5B841', 1.2);
          break;
        }

        if (this.phaseTimer >= 10) {
          s.timedRing.active = false;
          if (this.timedHitResult === 'PERFECT' && s.enemies.some((e) => e.alive)) {
            s.phase = 'PLAYER_FOLLOWUP';
            this.phaseTimer = 0;
          } else {
            s.phase = 'PLAYER_RETURN';
            this.phaseTimer = 0;
          }
        }
        break;
      }

      case 'PLAYER_FOLLOWUP': {
        // Signature Sea of Stars timed follow-up bounce / double strike!
        const target = s.enemies[s.selectedEnemy]?.alive
          ? s.enemies[s.selectedEnemy]
          : s.enemies[this.getFirstAliveEnemy()];
        const dur = 16;
        const t = Math.min(1, this.phaseTimer / dur);

        // Acrobatic backflip / spin arc right back onto enemy
        const flipOffset = Math.sin(t * Math.PI) * 28;
        s.heroX = Math.round(
          (activeActor.slot === 2 ? this.heroHomeX + 36 : target.homeX - 24) - Math.sin(t * Math.PI) * 14
        );
        s.heroY = Math.round(target.homeY - flipOffset);
        s.heroScaleXFp = 310;
        s.heroScaleYFp = 210;

        if (this.phaseTimer === 10) {
          this.resolvePlayerStrike(party, tuning, prng, cb, true);
        }
        if (this.phaseTimer >= dur) {
          s.phase = 'PLAYER_RETURN';
          this.phaseTimer = 0;
        }
        break;
      }

      case 'PLAYER_RETURN': {
        const dur = 12;
        const t = Math.min(1, this.phaseTimer / dur);
        s.heroX = Math.round(s.heroX + (this.heroHomeX - s.heroX) * t);
        s.heroY = Math.round(s.heroY + (this.heroHomeY - s.heroY) * t - Math.sin(t * Math.PI) * 10);

        if (this.phaseTimer >= dur) {
          s.heroX = this.heroHomeX;
          s.heroY = this.heroHomeY;
          s.timedRing.active = false;

          // Check Victory!
          if (s.enemies.every((e) => !e.alive)) {
            s.expEarned = s.enemies.reduce((acc, e) => acc + (e.isBoss ? 120 : 32), 0);
            s.phase = 'VICTORY_POSE';
            this.phaseTimer = 0;
            cb.playSfx(SfxId.VICTORY_FANFARE);
            s.bannerText = 'VICTORY!';
            s.bannerFrames = 90;
          } else {
            this.beginEnemyTurnPhase();
          }
        }
        break;
      }

      case 'ENEMY_SELECT': {
        // Find next living enemy to act
        while (
          this.activeEnemyTurnIdx < s.enemies.length &&
          !s.enemies[this.activeEnemyTurnIdx].alive
        ) {
          this.activeEnemyTurnIdx++;
        }

        if (this.activeEnemyTurnIdx >= s.enemies.length) {
          // All enemies acted -> Back to Player Command!
          s.phase = 'COMMAND';
          this.phaseTimer = 0;
          s.heroGuarding = false;
          // Refresh broken lock enemy charges when ready
          for (const e of s.enemies) {
            if (!e.alive) continue;
            if (e.staggered) {
              e.staggered = false;
            } else if (e.chargeTurns === 0 && prng.range(0, 2) === 0) {
              // Start charging a new lock-breakable special!
              e.chargeTurns = e.maxChargeTurns;
              if (e.isBoss) {
                e.locks = ['SWORD', 'SUN', 'STAR'];
              } else if (e.spriteId === 'enemy_gloomcap') {
                e.locks = ['SUN', 'STAR'];
              } else if (e.spriteId === 'enemy_thornling') {
                e.locks = ['SWORD', 'SUN'];
              } else {
                e.locks = ['STAR'];
              }
              e.brokenLocks = e.locks.map(() => false);
            }
          }
          break;
        }

        const enemy = s.enemies[this.activeEnemyTurnIdx];
        if (enemy.staggered) {
          cb.spawnText(enemy.homeX, enemy.homeY - 26, 'STAGGERED!', '#F5B841', 1);
          this.activeEnemyTurnIdx++;
          this.phaseTimer = 0;
          break;
        }

        // Countdown charge if holding locks
        if (enemy.chargeTurns > 1) {
          enemy.chargeTurns--;
          cb.spawnText(enemy.homeX, enemy.homeY - 28, `CHARGING (${enemy.chargeTurns})`, '#C48AF0', 1);
          cb.spawnParticles('SPARK_RED', enemy.homeX, enemy.homeY - 12, 6, '#C48AF0', true);
          this.activeEnemyTurnIdx++;
          this.phaseTimer = 0;
          break;
        }

        // Otherwise telegraph & attack!
        s.phase = 'ENEMY_TELEGRAPH';
        this.phaseTimer = 0;
        this.timedHitResult = 'NONE';
        this.timedInputBuffered = 0;
        if (enemy.chargeTurns === 1) {
          s.bannerText = enemy.specialName.toUpperCase() + '!';
          s.bannerFrames = 44;
        }
        break;
      }

      case 'ENEMY_TELEGRAPH': {
        const enemy = s.enemies[this.activeEnemyTurnIdx];
        const dur = 20;
        // Pull back and glint crimson to give a super clear parry read!
        enemy.x = Math.round(enemy.homeX + Math.sin((this.phaseTimer / dur) * Math.PI) * 9);
        enemy.scaleXFp = 290;
        enemy.scaleYFp = 215;
        if (this.phaseTimer === 8) {
          cb.spawnParticles('SPARK_RED', enemy.x - 6, enemy.y - 14, 6, '#E04E4E', true);
        }
        if (this.phaseTimer >= dur) {
          s.phase = 'ENEMY_LUNGE';
          this.phaseTimer = 0;
          this.timedHitResult = 'NONE';
          this.timedInputBuffered = 0;
        }
        break;
      }

      case 'ENEMY_LUNGE': {
        const enemy = s.enemies[this.activeEnemyTurnIdx];
        const dur = 16;
        const t = Math.min(1, this.phaseTimer / dur);
        const ease = t * t * (3 - 2 * t);
        const destX = this.heroHomeX + 24;
        enemy.x = Math.round(enemy.homeX + (destX - enemy.homeX) * ease);
        enemy.y = Math.round(enemy.homeY + (this.heroHomeY - enemy.homeY) * ease - Math.sin(t * Math.PI) * 10);

        const halfWin = Math.max(4, tuning.parryWindowFrames);
        const sweetStartFrame = dur - halfWin;
        s.timedRing = {
          active: true,
          kind: 'PARRY',
          x: this.heroHomeX,
          y: this.heroHomeY - 14,
          progress: this.phaseTimer / dur,
          sweetStart: sweetStartFrame / dur,
          sweetEnd: 1.15,
          result: this.timedHitResult,
        };

        if (this.timedHitResult === 'NONE' && this.timedInputBuffered > 0 && this.phaseTimer >= sweetStartFrame) {
          this.timedHitResult = 'PERFECT';
          s.timedRing.result = 'PERFECT';
          this.timedInputBuffered = 0;
        }

        if (this.phaseTimer >= dur) {
          s.phase = 'ENEMY_IMPACT';
          this.phaseTimer = 0;
          this.resolveEnemyHit(enemy, activeActor, tuning, prng, cb);
        }
        break;
      }

      case 'ENEMY_IMPACT': {
        if (this.phaseTimer >= 12) {
          s.timedRing.active = false;
          s.phase = 'ENEMY_RETURN';
          this.phaseTimer = 0;
        }
        break;
      }

      case 'ENEMY_RETURN': {
        const enemy = s.enemies[this.activeEnemyTurnIdx];
        const dur = 12;
        const t = Math.min(1, this.phaseTimer / dur);
        enemy.x = Math.round(enemy.x + (enemy.homeX - enemy.x) * t);
        enemy.y = Math.round(enemy.y + (enemy.homeY - enemy.y) * t);

        if (this.phaseTimer >= dur) {
          enemy.x = enemy.homeX;
          enemy.y = enemy.homeY;
          if (enemy.chargeTurns === 1) {
            enemy.chargeTurns = 0;
            enemy.locks = [];
            enemy.brokenLocks = [];
          }

          // Check if active party member fell -> auto-swap to healthy member, or defeat!
          if (activeActor.hp <= 0) {
            const healthyIdx = party.findIndex((m) => m.hp > 0);
            if (healthyIdx >= 0) {
              s.activeSlot = healthyIdx as FamiliarSlot;
              cb.playSfx(SfxId.SUMMON_ARC);
              cb.spawnText(this.heroHomeX, this.heroHomeY - 30, `${party[healthyIdx].name} STEPS IN!`, '#F5B841', 1);
            } else {
              s.phase = 'DEFEAT_POSE';
              this.phaseTimer = 0;
              s.bannerText = 'RETREATING TO WAYSTONE...';
              s.bannerFrames = 80;
              break;
            }
          }

          this.activeEnemyTurnIdx++;
          s.phase = 'ENEMY_SELECT';
          this.phaseTimer = 0;
        }
        break;
      }

      case 'VICTORY_POSE': {
        // Celebratory hops!
        if (this.phaseTimer % 18 === 0) {
          s.heroScaleXFp = 225;
          s.heroScaleYFp = 295;
        }
        if (this.phaseTimer === 48) {
          cb.onBattleEnd(true, this.defeatedFieldEntityId, this.isBossBattle);
        }
        break;
      }

      case 'DEFEAT_POSE': {
        if (this.phaseTimer === 52) {
          cb.onBattleEnd(false, this.defeatedFieldEntityId, this.isBossBattle);
        }
        break;
      }
    }

    return s.potions;
  }

  private resolvePlayerStrike(
    party: PartyMemberState[],
    tuning: EngineTuning,
    prng: RetroPrng,
    cb: BattleCallbacks,
    isFollowUp: boolean
  ): void {
    const s = this.state;
    const actor = party[s.activeSlot];
    const isAoeSkill = this.activeAction === 'SKILL' && actor.slot === 2;
    const targets = isAoeSkill
      ? s.enemies.filter((e) => e.alive)
      : [s.enemies[s.selectedEnemy]].filter((e) => e && e.alive);

    const strikeKind: LockKind =
      this.activeAction === 'SKILL' && actor.skillKind !== 'HEAL'
        ? actor.skillKind
        : actor.basicKind;

    const isPerfect = this.timedHitResult === 'PERFECT';
    const isGood = this.timedHitResult === 'GOOD';

    if (isFollowUp) {
      s.comboCount++;
      actor.mp = Math.min(actor.maxMp, actor.mp + 4);
      cb.playSfx(SfxId.HIT_CRITICAL);
      cb.setHitstop(tuning.hitstopCritFrames);
      cb.addShake(4, 10);
      cb.setFlash(0.28);
      cb.spawnText(this.heroHomeX, this.heroHomeY - 34, '+4 MP', '#5AD2F4', 1);
    } else if (isPerfect) {
      cb.playSfx(SfxId.HIT_CRITICAL);
      cb.setHitstop(tuning.hitstopCritFrames);
      cb.addShake(3, 8);
      cb.setFlash(0.18);
      cb.spawnText(s.heroX, s.heroY - 30, 'PERFECT!', '#F5B841', 1.15);
    } else {
      cb.playSfx(SfxId.HIT_NORMAL);
      cb.setHitstop(tuning.hitstopNormalFrames);
      cb.addShake(2, 5);
    }

    for (const target of targets) {
      const baseMult = this.activeAction === 'SKILL' ? 1.45 : 1.0;
      const followMult = isFollowUp ? 0.55 : isGood ? 1.15 : 1.0;
      const staggerMult = target.staggered ? 1.25 : 1.0;
      const raw = (actor.atk * baseMult * followMult * staggerMult) - target.def + prng.range(-1, 2);
      const dmg = Math.max(4, Math.round(raw));

      target.hp = Math.max(0, target.hp - dmg);
      target.flashFrames = 8;
      target.hurtFrames = 14;
      target.scaleXFp = 325;
      target.scaleYFp = 195;
      target.x = target.homeX + 6;

      cb.spawnParticles(
        isPerfect || isFollowUp ? 'SPARK_GOLD' : 'SPARK_CYAN',
        target.homeX,
        target.homeY - 12,
        isPerfect || isFollowUp ? 14 : 8,
        isPerfect || isFollowUp ? '#F5B841' : '#FFF6CC',
        true
      );
      cb.spawnText(
        target.homeX + prng.range(-6, 6),
        target.homeY - (isFollowUp ? 34 : 24),
        isFollowUp ? `${dmg}!` : `${dmg}`,
        isFollowUp || isPerfect ? '#F5B841' : '#FFFFFF',
        isFollowUp ? 1.25 : 1
      );

      // Check Spell Lock shattering!
      if (target.chargeTurns > 0 && target.locks.length > 0 && target.hp > 0) {
        const lockIdx = target.locks.findIndex(
          (lk, idx) => lk === strikeKind && !target.brokenLocks[idx]
        );
        if (lockIdx >= 0) {
          target.brokenLocks[lockIdx] = true;
          s.locksBrokenCount++;
          cb.spawnParticles('SHATTER', target.homeX, target.homeY - 34, 12, '#74B461', true);

          const allBroken = target.brokenLocks.every(Boolean);
          if (allBroken) {
            target.chargeTurns = 0;
            target.staggered = true;
            target.locks = [];
            target.brokenLocks = [];
            cb.playSfx(SfxId.LOCK_ALL_BREAK);
            cb.setHitstop(tuning.hitstopCritFrames + 2);
            cb.addShake(5, 12);
            cb.spawnText(target.homeX, target.homeY - 46, 'SHATTERED!', '#74B461', 1.25);
          } else {
            cb.playSfx(SfxId.LOCK_BREAK);
            cb.spawnText(target.homeX, target.homeY - 42, 'LOCK BREAK!', '#5AD2F4', 1);
          }
        }
      }

      if (target.hp <= 0) {
        target.alive = false;
        cb.spawnParticles('SHATTER', target.homeX, target.homeY - 12, 18, '#C48AF0', true);
      }
    }
  }

  private resolveEnemyHit(
    enemy: BattleEnemyState,
    activeActor: PartyMemberState,
    tuning: EngineTuning,
    prng: RetroPrng,
    cb: BattleCallbacks
  ): void {
    const s = this.state;
    const isSpecial = enemy.chargeTurns === 1;
    const isParried = this.timedHitResult === 'PERFECT';

    let dmg = Math.max(3, Math.round(enemy.atk * (isSpecial ? 1.55 : 1.0) - activeActor.def * 0.6 + prng.range(-1, 2)));
    if (s.heroGuarding) {
      dmg = Math.max(2, Math.round(dmg * 0.55));
    }
    if (isParried) {
      dmg = Math.max(1, Math.round(dmg * 0.45));
      s.parryCount++;
      activeActor.mp = Math.min(activeActor.maxMp, activeActor.mp + 3);
      cb.playSfx(SfxId.PARRY_CLANG);
      cb.setHitstop(tuning.hitstopParryFrames);
      cb.addShake(2, 6);
      cb.spawnParticles('SPARK_CYAN', this.heroHomeX, this.heroHomeY - 12, 14, '#5AD2F4', true);
      cb.spawnText(this.heroHomeX, this.heroHomeY - 34, 'GUARD!', '#5AD2F4', 1.2);
      cb.spawnText(this.heroHomeX + 12, this.heroHomeY - 22, `-${dmg}`, '#A8D6F8', 1);
    } else {
      cb.playSfx(SfxId.HURT);
      cb.setHitstop(tuning.hitstopNormalFrames);
      cb.addShake(isSpecial ? 5 : 3, 9);
      cb.spawnParticles('SPARK_RED', this.heroHomeX, this.heroHomeY - 12, 10, '#E04E4E', true);
      cb.spawnText(this.heroHomeX, this.heroHomeY - 28, `-${dmg}`, '#E04E4E', 1.1);
    }

    activeActor.hp = Math.max(0, activeActor.hp - dmg);
    s.heroFlashFrames = 8;
    s.heroScaleXFp = 310;
    s.heroScaleYFp = 205;
  }
}
