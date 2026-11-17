/**
 * Project Hearth — 16-Bit Web Audio Synth Driver (PSG / Warm FM-Inspired)
 *
 * Consumes pure SfxId commands emitted by the platform-agnostic core engine,
 * mirroring how a Sega Genesis YM2612/PSG or SNES SPC700 audio driver works.
 */

import { GameMode, SfxId } from '../core/types';

export type BgmTrackId = 'HAMLET' | 'BATTLE' | 'BOSS' | 'VICTORY';

const REST = 0;
const HOLD = 255;

const N = {
  C2: 36, Cs2: 37, D2: 38, Eb2: 39, E2: 40, F2: 41, Fs2: 42, G2: 43, Gs2: 44, A2: 45, Bb2: 46, B2: 47,
  C3: 48, Cs3: 49, D3: 50, Eb3: 51, E3: 52, F3: 53, Fs3: 54, G3: 55, Gs3: 56, A3: 57, Bb3: 58, B3: 59,
  C4: 60, Cs4: 61, D4: 62, Eb4: 63, E4: 64, F4: 65, Fs4: 66, G4: 67, Gs4: 68, A4: 69, Bb4: 70, B4: 71,
  C5: 72, Cs5: 73, D5: 74, Eb5: 75, E5: 76, F5: 77, Fs5: 78, G5: 79, Gs5: 80, A5: 81, Bb5: 82, B5: 83,
  C6: 84,
} as const;

interface BgmTrackData {
  stepMs: number;
  lead: readonly number[];
  arp: readonly number[];
  bass: readonly number[];
  drum: readonly number[];
}

const BGM_TRACKS: Record<BgmTrackId, BgmTrackData> = {
  HAMLET: {
    stepMs: 240,
    lead: [
      N.G4, N.B4, N.D5, HOLD, N.E5, N.D5,
      N.B4, HOLD, N.G4, HOLD, N.A4, N.B4,
      N.C5, HOLD, N.E5, N.D5, N.C5, N.B4,
      N.A4, HOLD, HOLD, HOLD, N.B4, N.C5,
      N.D5, HOLD, N.G5, N.Fs5, N.E5, N.D5,
      N.B4, HOLD, N.E5, N.D5, N.C5, N.B4,
      N.A4, N.B4, N.C5, N.D5, N.B4, N.A4,
      N.G4, HOLD, HOLD, HOLD, REST, REST,
    ],
    arp: [
      N.G3, N.B3, N.D4, N.G4, N.D4, N.B3,
      N.E3, N.G3, N.B3, N.E4, N.B3, N.G3,
      N.C4, N.E4, N.G4, N.E4, N.C4, N.G3,
      N.D3, N.Fs3, N.A3, N.D4, N.C4, N.A3,
      N.B3, N.D4, N.Fs4, N.D4, N.B3, N.Fs3,
      N.E3, N.G3, N.B3, N.E4, N.B3, N.G3,
      N.A3, N.C4, N.E4, N.D4, N.Fs4, N.C4,
      N.G3, N.B3, N.D4, N.G4, N.D4, N.B3,
    ],
    bass: [
      N.G2, HOLD, N.D3, REST, N.D3, REST,
      N.E2, HOLD, N.B2, REST, N.B2, REST,
      N.C3, HOLD, N.G3, REST, N.G3, REST,
      N.D2, HOLD, N.A2, REST, N.A2, REST,
      N.B2, HOLD, N.Fs3, REST, N.Fs3, REST,
      N.E2, HOLD, N.B2, REST, N.B2, REST,
      N.A2, HOLD, N.E3, N.D2, HOLD, N.A2,
      N.G2, HOLD, N.D3, N.G2, HOLD, REST,
    ],
    drum: [
      3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
      3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
      3, 0, 1, 0, 1, 0,  3, 0, 1, 0, 1, 0,
      3, 0, 1, 3, 0, 1,  3, 0, 1, 0, 0, 0,
    ],
  },
  BATTLE: {
    stepMs: 155,
    lead: [
      N.A4, HOLD, N.C5, N.E5, N.A5, HOLD, N.G5, N.E5,
      N.D5, N.E5, N.F5, N.E5, N.D5, N.C5, N.B4, HOLD,
      N.C5, HOLD, N.E5, N.G5, N.C6, HOLD, N.B5, N.G5,
      N.A5, N.G5, N.E5, N.D5, N.E5, HOLD, REST, N.E5,
      N.F5, HOLD, N.E5, N.D5, N.C5, HOLD, N.D5, N.E5,
      N.G5, HOLD, N.F5, N.E5, N.D5, HOLD, N.C5, N.B4,
      N.A4, N.C5, N.E5, N.A5, N.Gs5, N.E5, N.B4, N.Gs5,
      N.A5, HOLD, N.E5, N.C5, N.A4, HOLD, REST, REST,
    ],
    arp: [
      N.A3, N.E4, N.C4, N.E4, N.A3, N.E4, N.C4, N.E4,
      N.G3, N.D4, N.B3, N.D4, N.G3, N.D4, N.B3, N.D4,
      N.C4, N.G4, N.E4, N.G4, N.C4, N.G4, N.E4, N.G4,
      N.E3, N.B3, N.Gs3, N.B3, N.E3, N.B3, N.E4, N.B3,
      N.F3, N.C4, N.A3, N.C4, N.C4, N.G4, N.E4, N.G4,
      N.G3, N.D4, N.B3, N.D4, N.D3, N.A3, N.F3, N.A3,
      N.A3, N.E4, N.C4, N.E4, N.E3, N.B3, N.Gs3, N.B3,
      N.A3, N.E4, N.C4, N.E4, N.A3, N.C4, N.E4, N.G4,
    ],
    bass: [
      N.A2, N.A2, N.A3, N.A2, N.A2, N.A2, N.G2, N.A2,
      N.G2, N.G2, N.G3, N.G2, N.G2, N.G2, N.F2, N.G2,
      N.C3, N.C3, N.C4, N.C3, N.C3, N.C3, N.B2, N.C3,
      N.E2, N.E2, N.E3, N.E2, N.E2, N.Fs2, N.G2, N.Gs2,
      N.F2, N.F2, N.F3, N.F2, N.C3, N.C3, N.C4, N.C3,
      N.G2, N.G2, N.G3, N.G2, N.D2, N.D2, N.D3, N.D2,
      N.A2, N.A2, N.A3, N.A2, N.E2, N.E2, N.E3, N.E2,
      N.A2, N.A2, N.E2, N.G2, N.A2, HOLD, REST, N.E2,
    ],
    drum: [
      3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 1, 2, 2,
      3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 2, 2, 1,
      3, 1, 2, 1, 3, 3, 2, 1,  3, 1, 2, 1, 3, 1, 2, 1,
      3, 1, 2, 1, 3, 3, 2, 1,  3, 2, 3, 2, 3, 0, 2, 2,
    ],
  },
  BOSS: {
    stepMs: 140,
    lead: [
      N.D5, HOLD, N.Eb5, N.D5, N.C5, N.Bb4, N.A4, HOLD,
      N.D5, N.F5, N.A5, HOLD, N.G5, N.F5, N.E5, HOLD,
      N.F5, HOLD, N.G5, N.F5, N.E5, N.D5, N.Cs5, HOLD,
      N.A4, N.Cs5, N.E5, N.G5, N.F5, N.E5, N.D5, HOLD,
      N.Bb5, HOLD, N.A5, N.G5, N.F5, HOLD, N.G5, N.A5,
      N.G5, HOLD, N.F5, N.E5, N.D5, HOLD, N.E5, N.F5,
      N.E5, N.F5, N.G5, N.A5, N.Bb5, N.A5, N.G5, N.E5,
      N.A5, HOLD, HOLD, N.G5, N.F5, N.E5, N.D5, HOLD,
    ],
    arp: [
      N.D4, N.A3, N.F4, N.A3, N.D4, N.A3, N.Eb4, N.A3,
      N.D4, N.A3, N.F4, N.A3, N.C4, N.G3, N.E4, N.G3,
      N.Bb3, N.F3, N.D4, N.F3, N.A3, N.E3, N.Cs4, N.E3,
      N.A3, N.E3, N.Cs4, N.E3, N.D4, N.A3, N.F4, N.A3,
      N.Bb3, N.F4, N.D4, N.F4, N.F3, N.C4, N.A3, N.C4,
      N.G3, N.D4, N.Bb3, N.D4, N.D4, N.A3, N.F4, N.A3,
      N.C4, N.G4, N.E4, N.G4, N.A3, N.E4, N.Cs4, N.E4,
      N.A3, N.Cs4, N.E4, N.G4, N.F4, N.E4, N.D4, N.A3,
    ],
    bass: [
      N.D2, N.D2, N.D3, N.D2, N.Eb2, N.D2, N.C2, N.D2,
      N.D2, N.D2, N.D3, N.D2, N.C2, N.C2, N.C3, N.C2,
      N.Bb2, N.Bb2, N.Bb2, N.A2, N.A2, N.A2, N.A3, N.A2,
      N.A2, N.G2, N.F2, N.E2, N.D2, N.D2, N.A2, N.D2,
      N.Bb2, N.Bb2, N.Bb2, N.Bb2, N.F2, N.F2, N.F3, N.F2,
      N.G2, N.G2, N.G3, N.G2, N.D2, N.D2, N.D3, N.D2,
      N.C3, N.C3, N.C3, N.C3, N.A2, N.A2, N.A3, N.A2,
      N.A2, N.A2, N.A2, N.G2, N.F2, N.E2, N.D2, N.A2,
    ],
    drum: [
      3, 3, 2, 1, 3, 1, 2, 3,  3, 3, 2, 1, 3, 2, 2, 2,
      3, 3, 2, 1, 3, 1, 2, 3,  3, 1, 2, 1, 3, 2, 3, 2,
      3, 3, 2, 1, 3, 1, 2, 3,  3, 3, 2, 1, 3, 2, 2, 1,
      3, 1, 2, 1, 3, 3, 2, 2,  3, 2, 3, 2, 2, 2, 3, 2,
    ],
  },
  VICTORY: {
    stepMs: 165,
    lead: [
      N.C5, N.C5, N.C5, HOLD, N.C5, HOLD, N.G4, N.A4,
      N.B4, HOLD, N.C5, HOLD, N.D5, N.E5, N.G5, HOLD,
      N.E5, N.C5, N.D5, N.E5, N.F5, HOLD, N.E5, N.D5,
      N.C5, HOLD, N.E5, N.G5, N.C6, HOLD, HOLD, REST,
    ],
    arp: [
      N.C4, N.E4, N.G4, N.E4, N.C4, N.E4, N.G4, N.E4,
      N.G3, N.B3, N.D4, N.B3, N.G3, N.B3, N.D4, N.B3,
      N.A3, N.C4, N.E4, N.C4, N.F3, N.A3, N.C4, N.A3,
      N.G3, N.C4, N.E4, N.G4, N.C5, N.G4, N.E4, N.C4,
    ],
    bass: [
      N.C3, REST, N.G2, REST, N.C3, REST, N.G2, REST,
      N.G2, REST, N.D3, REST, N.G2, REST, N.B2, REST,
      N.A2, REST, N.E3, REST, N.F2, REST, N.C3, REST,
      N.G2, REST, N.G2, REST, N.C3, HOLD, N.G2, N.C3,
    ],
    drum: [
      3, 2, 2, 1, 3, 1, 2, 1,  3, 1, 2, 1, 3, 2, 2, 1,
      3, 1, 2, 1, 3, 1, 2, 1,  3, 1, 2, 2, 3, 1, 2, 1,
    ],
  },
};

function midiToHz(midi: number): number {
  return 440 * Math.pow(2, (midi - 69) / 12);
}

export class RetroSynthAudio {
  private ctx: AudioContext | null = null;
  private muted = false;
  private bgmEnabled = true;
  private bgmTimer: number | null = null;
  private bgmStep = 0;
  private currentTrack: BgmTrackId = 'HAMLET';

  public isMuted(): boolean {
    return this.muted;
  }

  public isBgmEnabled(): boolean {
    return this.bgmEnabled;
  }

  public setMuted(m: boolean): void {
    this.muted = m;
    if (m) this.stopBgmLoop();
    else this.startBgmLoop();
  }

  public setBgmEnabled(enabled: boolean): void {
    this.bgmEnabled = enabled;
    if (!enabled) this.stopBgmLoop();
    else if (!this.muted) this.startBgmLoop();
  }

  public unlock(): void {
    if (!this.ctx && typeof window !== 'undefined') {
      const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      if (AudioCtx) {
        this.ctx = new AudioCtx();
      }
    }
    if (this.ctx && this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }
    if (!this.muted && this.bgmEnabled && this.bgmTimer === null) {
      this.startBgmLoop();
    }
  }

  public setMusicMode(mode: GameMode, isBossBattle = false): void {
    let want: BgmTrackId = 'HAMLET';
    if (mode === 'BATTLE') {
      want = isBossBattle ? 'BOSS' : 'BATTLE';
    } else if (mode === 'BATTLE_VICTORY') {
      want = 'VICTORY';
    }
    if (this.currentTrack !== want) {
      this.currentTrack = want;
      this.bgmStep = 0;
      if (this.bgmTimer !== null) {
        this.stopBgmLoop();
        this.startBgmLoop();
      }
    }
  }

  public processQueue(queue: SfxId[]): void {
    if (this.muted || queue.length === 0) return;
    if (!this.ctx || this.ctx.state !== 'running') return;
    for (const sfx of queue) {
      this.playSfx(sfx);
    }
  }

  private tone(
    freq: number,
    endFreq: number,
    dur: number,
    type: OscillatorType,
    vol: number,
    delay = 0
  ): void {
    if (!this.ctx || this.muted) return;
    const t0 = this.ctx.currentTime + delay;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    const filter = this.ctx.createBiquadFilter();

    osc.type = type;
    osc.frequency.setValueAtTime(Math.max(40, freq), t0);
    if (endFreq !== freq) {
      osc.frequency.exponentialRampToValueAtTime(Math.max(40, endFreq), t0 + dur);
    }

    filter.type = 'lowpass';
    filter.frequency.setValueAtTime(type === 'square' || type === 'sawtooth' ? 2200 : 4800, t0);

    gain.gain.setValueAtTime(0.0001, t0);
    gain.gain.linearRampToValueAtTime(vol, t0 + 0.008);
    gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);

    osc.connect(filter);
    filter.connect(gain);
    gain.connect(this.ctx.destination);

    osc.start(t0);
    osc.stop(t0 + dur + 0.02);
  }

  private noiseBurst(dur: number, cutoff: number, vol: number, delay = 0): void {
    if (!this.ctx || this.muted) return;
    const sampleRate = this.ctx.sampleRate;
    const len = Math.max(1, Math.floor(sampleRate * dur));
    const buffer = this.ctx.createBuffer(1, len, sampleRate);
    const data = buffer.getChannelData(0);
    for (let i = 0; i < len; i++) {
      data[i] = Math.random() * 2 - 1;
    }

    const t0 = this.ctx.currentTime + delay;
    const src = this.ctx.createBufferSource();
    src.buffer = buffer;

    const filter = this.ctx.createBiquadFilter();
    filter.type = 'bandpass';
    filter.frequency.setValueAtTime(cutoff, t0);

    const gain = this.ctx.createGain();
    gain.gain.setValueAtTime(vol, t0);
    gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur);

    src.connect(filter);
    filter.connect(gain);
    gain.connect(this.ctx.destination);

    src.start(t0);
    src.stop(t0 + dur + 0.02);
  }

  private playSfx(sfx: SfxId): void {
    switch (sfx) {
      case SfxId.STEP:
        this.tone(130, 82, 0.045, 'triangle', 0.06);
        break;
      case SfxId.DASH:
        this.noiseBurst(0.06, 950, 0.05);
        this.tone(190, 110, 0.05, 'triangle', 0.07);
        break;
      case SfxId.GRASS_RUSTLE:
        this.noiseBurst(0.075, 2100, 0.055);
        break;
      case SfxId.WATER_SPLASH:
        this.noiseBurst(0.09, 1500, 0.06);
        this.tone(420, 680, 0.06, 'sine', 0.05);
        break;
      case SfxId.CURSOR:
        this.tone(540, 660, 0.045, 'triangle', 0.08);
        break;
      case SfxId.CONFIRM:
        this.tone(523, 784, 0.075, 'triangle', 0.1);
        this.tone(784, 1046, 0.08, 'sine', 0.07, 0.045);
        break;
      case SfxId.CANCEL:
        this.tone(330, 210, 0.09, 'triangle', 0.09);
        break;
      case SfxId.TEXT_HERO:
        this.tone(440, 460, 0.03, 'triangle', 0.05);
        break;
      case SfxId.TEXT_PIP:
        this.tone(680, 740, 0.03, 'sine', 0.055);
        break;
      case SfxId.TEXT_ELDER:
        this.tone(250, 230, 0.035, 'triangle', 0.06);
        break;
      case SfxId.EMOTE:
        this.tone(587, 880, 0.09, 'triangle', 0.09);
        break;
      case SfxId.ENEMY_ALERT:
        this.tone(620, 920, 0.08, 'square', 0.07);
        this.tone(920, 1180, 0.1, 'square', 0.06, 0.06);
        break;
      case SfxId.BATTLE_SWOOP:
        this.tone(220, 660, 0.24, 'sawtooth', 0.07);
        this.tone(330, 880, 0.24, 'triangle', 0.08, 0.06);
        break;
      case SfxId.SUMMON_ARC:
        this.tone(523.25, 523.25, 0.12, 'triangle', 0.11, 0);
        this.tone(659.25, 659.25, 0.12, 'triangle', 0.11, 0.045);
        this.tone(783.99, 783.99, 0.14, 'sine', 0.12, 0.09);
        this.tone(1046.5, 1174.6, 0.18, 'sine', 0.1, 0.13);
        break;
      case SfxId.HIT_NORMAL:
        this.noiseBurst(0.07, 700, 0.14);
        this.tone(180, 68, 0.11, 'square', 0.11);
        break;
      case SfxId.HIT_CRITICAL:
        this.noiseBurst(0.09, 1100, 0.18);
        this.tone(240, 80, 0.13, 'sawtooth', 0.13);
        this.tone(880, 1318, 0.15, 'triangle', 0.13, 0.02);
        this.tone(1318, 1760, 0.18, 'sine', 0.1, 0.06);
        break;
      case SfxId.PARRY_CLANG:
        this.tone(988, 1480, 0.12, 'triangle', 0.12);
        this.tone(1480, 1975, 0.16, 'sine', 0.09, 0.03);
        this.noiseBurst(0.05, 3200, 0.09);
        break;
      case SfxId.LOCK_BREAK:
        this.tone(660, 990, 0.11, 'triangle', 0.12);
        this.tone(1100, 1480, 0.14, 'sine', 0.1, 0.05);
        break;
      case SfxId.LOCK_ALL_BREAK:
        this.noiseBurst(0.12, 2400, 0.16);
        this.tone(523, 1046, 0.14, 'square', 0.1, 0);
        this.tone(784, 1568, 0.18, 'triangle', 0.12, 0.06);
        this.tone(1046, 2093, 0.25, 'sine', 0.12, 0.12);
        break;
      case SfxId.SPELL_CAST:
        this.tone(440, 880, 0.18, 'sine', 0.09);
        this.tone(660, 1174, 0.2, 'triangle', 0.08, 0.05);
        break;
      case SfxId.HEAL:
        this.tone(523, 523, 0.12, 'sine', 0.09, 0);
        this.tone(659, 659, 0.14, 'sine', 0.09, 0.07);
        this.tone(784, 1046, 0.22, 'sine', 0.1, 0.14);
        break;
      case SfxId.HEART_TAKE:
      case SfxId.HEART_GIVE: {
        const notes = sfx === SfxId.HEART_TAKE ? [392, 493.9, 587.3, 784, 987.8] : [523.3, 659.3, 784, 1046.5, 1318.5];
        notes.forEach((n, i) => {
          this.tone(n, n, 0.24, 'sine', 0.09, i * 0.075);
          this.tone(n * 0.5, n * 0.5, 0.24, 'triangle', 0.05, i * 0.075);
        });
        break;
      }
      case SfxId.CHEST_OPEN:
        this.tone(523, 523, 0.1, 'triangle', 0.1, 0);
        this.tone(659, 659, 0.1, 'triangle', 0.1, 0.08);
        this.tone(784, 784, 0.12, 'triangle', 0.1, 0.16);
        this.tone(1046, 1046, 0.28, 'sine', 0.12, 0.24);
        break;
      case SfxId.VICTORY_FANFARE:
        [523.25, 659.25, 783.99, 1046.5, 783.99, 1046.5].forEach((n, i) => {
          const delay = i < 4 ? i * 0.09 : 0.38 + (i - 4) * 0.12;
          this.tone(n, n, i === 5 ? 0.42 : 0.14, 'triangle', 0.1, delay);
        });
        break;
      case SfxId.HURT:
        this.tone(170, 75, 0.14, 'sawtooth', 0.12);
        this.noiseBurst(0.08, 520, 0.11);
        break;
    }
  }

  private startBgmLoop(): void {
    if (this.bgmTimer !== null) return;
    const track = BGM_TRACKS[this.currentTrack];
    const stepSec = track.stepMs / 1000;

    this.bgmTimer = window.setInterval(() => {
      if (this.muted || !this.bgmEnabled || !this.ctx || this.ctx.state !== 'running') return;

      const cur = BGM_TRACKS[this.currentTrack];
      const idx = this.bgmStep % cur.lead.length;
      const isAction = this.currentTrack === 'BATTLE' || this.currentTrack === 'BOSS';

      const leadNote = cur.lead[idx];
      if (leadNote !== REST && leadNote !== HOLD) {
        let holdSteps = 1;
        while (
          idx + holdSteps < cur.lead.length &&
          cur.lead[idx + holdSteps] === HOLD
        ) {
          holdSteps++;
        }
        const dur = Math.min(1.1, holdSteps * stepSec * 0.95);
        const hz = midiToHz(leadNote);
        this.tone(hz, hz, dur, isAction ? 'square' : 'triangle', isAction ? 0.032 : 0.048);
      }

      const arpNote = cur.arp[idx];
      if (arpNote !== REST && arpNote !== HOLD) {
        const hz = midiToHz(arpNote);
        this.tone(hz, hz, stepSec * 0.85, 'triangle', isAction ? 0.018 : 0.022);
      }

      const bassNote = cur.bass[idx];
      if (bassNote !== REST && bassNote !== HOLD) {
        const hz = midiToHz(bassNote);
        this.tone(hz, hz, stepSec * 1.15, 'triangle', isAction ? 0.042 : 0.036);
      }

      const drum = cur.drum[idx];
      if (drum === 3) {
        this.tone(135, 48, 0.08, 'sine', isAction ? 0.06 : 0.035);
      } else if (drum === 2) {
        this.noiseBurst(0.07, 1600, isAction ? 0.04 : 0.02);
      } else if (drum === 1) {
        this.noiseBurst(0.035, 3600, isAction ? 0.018 : 0.01);
      }

      this.bgmStep++;
    }, track.stepMs);
  }

  private stopBgmLoop(): void {
    if (this.bgmTimer !== null) {
      window.clearInterval(this.bgmTimer);
      this.bgmTimer = null;
    }
  }

  public dispose(): void {
    this.stopBgmLoop();
    if (this.ctx) {
      this.ctx.close().catch(() => {});
      this.ctx = null;
    }
  }
}
