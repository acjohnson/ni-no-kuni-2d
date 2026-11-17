/**
 * Project Hearth — 16-Bit Web Audio Synth Driver (PSG / Warm FM-Inspired)
 *
 * Consumes pure SfxId commands emitted by the platform-agnostic core engine,
 * mirroring how a Sega Genesis YM2612/PSG or SNES SPC700 audio driver works.
 */

import { GameMode, SfxId } from '../core/types';

export class RetroSynthAudio {
  private ctx: AudioContext | null = null;
  private muted = false;
  private bgmEnabled = true;
  private bgmTimer: number | null = null;
  private bgmStep = 0;
  private currentMode: GameMode = 'TITLE';

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

  public setMusicMode(mode: GameMode): void {
    if (this.currentMode !== mode) {
      this.currentMode = mode;
      this.bgmStep = 0;
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
        // Warm magical Ni No Kuni familiar summon arpeggio!
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
        // Crunchy Sea of Stars timed-hit harmonic impact!
        this.noiseBurst(0.09, 1100, 0.18);
        this.tone(240, 80, 0.13, 'sawtooth', 0.13);
        this.tone(880, 1318, 0.15, 'triangle', 0.13, 0.02);
        this.tone(1318, 1760, 0.18, 'sine', 0.1, 0.06);
        break;
      case SfxId.PARRY_CLANG:
        // Crisp metallic shield parry chime!
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
    // Gentle pastoral waltz/harp motif in Explore, rhythmic 16-bit bass+lead pulse in Battle
    const fieldNotes = [261.6, 329.6, 392.0, 493.9, 440.0, 392.0, 329.6, 293.7, 261.6, 329.6, 392.0, 523.3, 493.9, 392.0, 329.6, 293.7];
    const battleNotes = [220.0, 261.6, 329.6, 261.6, 293.7, 349.2, 440.0, 349.2, 196.0, 246.9, 293.7, 392.0, 329.6, 293.7, 261.6, 246.9];

    this.bgmTimer = window.setInterval(() => {
      if (this.muted || !this.bgmEnabled || !this.ctx || this.ctx.state !== 'running') return;
      if (this.currentMode === 'TITLE') return;

      const isBattle = this.currentMode === 'BATTLE';
      const seq = isBattle ? battleNotes : fieldNotes;
      const freq = seq[this.bgmStep % seq.length];

      if (isBattle) {
        this.tone(freq, freq, 0.15, 'triangle', 0.028);
        if (this.bgmStep % 2 === 0) {
          this.tone(freq * 0.5, freq * 0.5, 0.16, 'square', 0.016);
        }
      } else if (this.bgmStep % 2 === 0) {
        this.tone(freq, freq, 0.34, 'sine', 0.022);
      }

      this.bgmStep++;
    }, 240);
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
