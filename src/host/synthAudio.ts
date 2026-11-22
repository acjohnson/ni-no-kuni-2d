/**
 * Project Hearth — Expressive Orchestral 16-Bit Web Audio Synth Engine
 *
 * Studio Ghibli / 16-bit JRPG inspired synthesis with warm reverb/delay bus,
 * lyrical vibrato woodwind leads, celesta/music-box harmonics, plucked harp/piano
 * arpeggios, bowed cello/string pads, and full multi-section A/B/Bridge/Coda suites.
 *
 * Consumes pure BgmTrackId & SfxId commands emitted by the shared C99 core.
 */

import { BgmTrackId, GameMode, SfxId } from '../core/types';

export type { BgmTrackId };

const REST = 0;
const HOLD = 255;

const N = {
  C2: 36, Cs2: 37, D2: 38, Eb2: 39, E2: 40, F2: 41, Fs2: 42, G2: 43, Gs2: 44, A2: 45, Bb2: 46, B2: 47,
  C3: 48, Cs3: 49, D3: 50, Eb3: 51, E3: 52, F3: 53, Fs3: 54, G3: 55, Gs3: 56, A3: 57, Bb3: 58, B3: 59,
  C4: 60, Cs4: 61, D4: 62, Eb4: 63, E4: 64, F4: 65, Fs4: 66, G4: 67, Gs4: 68, A4: 69, Bb4: 70, B4: 71,
  C5: 72, Cs5: 73, D5: 74, Eb5: 75, E5: 76, F5: 77, Fs5: 78, G5: 79, Gs5: 80, A5: 81, Bb5: 82, B5: 83,
  C6: 84, D6: 86, E6: 88,
} as const;

type VoiceStyle = 'FLUTE' | 'CELESTA' | 'LULLABY' | 'EERIE_BELL' | 'STRINGS' | 'ACTION';

interface BgmTrackData {
  stepMs: number;
  style: VoiceStyle;
  lead: readonly number[];
  counter?: readonly number[];
  arp: readonly number[];
  bass: readonly number[];
  drum: readonly number[];
}

const BGM_TRACKS: Record<BgmTrackId, BgmTrackData> = {
  /* ==========================================================================
   * "STEAM & PINE" — Alderbrook Mountain Rail Town Overture & Title Suite
   * 6/8 nostalgic Hisaishi-style waltz in F major (A -> A' -> Dm Bridge -> Soaring Coda)
   * 96 steps (~25 seconds per full loop with rich harmonic development)
   * ========================================================================== */
  ALDERBROOK: {
    stepMs: 255,
    style: 'FLUTE',
    lead: [
      /* A Section: Warm sunset over the rail trestle (F -> Am -> Bb -> C) */
      N.C5, HOLD, N.A4, N.F4, HOLD, N.G4,
      N.A4, HOLD, N.C5, N.E5, HOLD, N.D5,
      N.D5, HOLD, N.C5, N.Bb4, HOLD, N.A4,
      N.G4, HOLD, HOLD, HOLD, N.A4, N.Bb4,

      /* A' Section: Remembering Grandma's hearth (F -> Dm -> Gm7 -> C7) */
      N.C5, HOLD, N.A4, N.F4, HOLD, N.C5,
      N.D5, HOLD, N.F5, N.E5, HOLD, N.D5,
      N.C5, HOLD, N.A4, N.G4, HOLD, N.A4,
      N.F4, HOLD, HOLD, HOLD, REST, N.A4,

      /* B Section (Wistful Bridge): Mist on the pines, parents' memory (Dm -> Bb -> Gm -> A7) */
      N.D5, HOLD, N.E5, N.F5, HOLD, N.E5,
      N.D5, HOLD, N.C5, N.Bb4, HOLD, N.A4,
      N.G4, HOLD, N.A4, N.Bb4, HOLD, N.D5,
      N.Cs5, HOLD, HOLD, N.A4, N.C5, N.E5,

      /* Coda: Dawn train whistle & hope (Bb -> F/A -> Gm7 -> C9 -> F) */
      N.F5, HOLD, N.E5, N.D5, HOLD, N.C5,
      N.C5, HOLD, N.A4, N.F4, HOLD, N.A4,
      N.Bb4, N.C5, N.D5, N.C5, HOLD, N.G4,
      N.A4, HOLD, N.F4, N.F4, HOLD, REST,
    ],
    counter: [
      REST, REST, REST, N.A3, HOLD, N.Bb3,
      N.C4, HOLD, N.E4, N.G4, HOLD, N.F4,
      N.F4, HOLD, N.E4, N.D4, HOLD, N.C4,
      N.E4, HOLD, N.D4, N.C4, HOLD, REST,

      REST, REST, REST, N.A3, HOLD, N.F4,
      N.F4, HOLD, N.A4, N.G4, HOLD, N.F4,
      N.E4, HOLD, N.C4, N.Bb3, HOLD, N.C4,
      N.A3, HOLD, HOLD, REST, REST, REST,

      N.F4, HOLD, N.G4, N.A4, HOLD, N.G4,
      N.F4, HOLD, N.E4, N.D4, HOLD, N.F4,
      N.D4, HOLD, N.E4, N.F4, HOLD, N.G4,
      N.E4, HOLD, HOLD, N.E4, HOLD, N.G4,

      N.A4, HOLD, N.G4, N.F4, HOLD, N.E4,
      N.F4, HOLD, N.C4, N.A3, HOLD, N.C4,
      N.D4, HOLD, N.F4, N.E4, HOLD, N.Bb3,
      N.C4, HOLD, N.A3, N.A3, HOLD, REST,
    ],
    arp: [
      N.F3, N.A3, N.C4, N.F4, N.C4, N.A3,
      N.A3, N.C4, N.E4, N.A4, N.E4, N.C4,
      N.Bb3, N.D4, N.F4, N.Bb4, N.F4, N.D4,
      N.C4, N.E4, N.G4, N.Bb4, N.G4, N.E4,

      N.F3, N.A3, N.C4, N.F4, N.C4, N.A3,
      N.D3, N.A3, N.D4, N.F4, N.D4, N.A3,
      N.G3, N.Bb3, N.D4, N.C4, N.E4, N.G4,
      N.F3, N.A3, N.C4, N.F4, N.C4, N.A3,

      N.D3, N.F3, N.A3, N.D4, N.A3, N.F3,
      N.Bb2, N.F3, N.Bb3, N.D4, N.Bb3, N.F3,
      N.G3, N.Bb3, N.D4, N.G4, N.D4, N.Bb3,
      N.A3, N.Cs4, N.E4, N.A4, N.E4, N.Cs4,

      N.Bb3, N.D4, N.F4, N.Bb4, N.F4, N.D4,
      N.A3, N.C4, N.F4, N.A4, N.F4, N.C4,
      N.G3, N.Bb3, N.D4, N.C4, N.E4, N.Bb4,
      N.F3, N.A3, N.C4, N.F4, N.A4, N.C5,
    ],
    bass: [
      N.F2, HOLD, HOLD, N.C3, HOLD, REST,
      N.A2, HOLD, HOLD, N.E3, HOLD, REST,
      N.Bb2, HOLD, HOLD, N.F3, HOLD, REST,
      N.C3, HOLD, HOLD, N.G2, HOLD, REST,

      N.F2, HOLD, HOLD, N.C3, HOLD, REST,
      N.D2, HOLD, HOLD, N.A2, HOLD, REST,
      N.G2, HOLD, N.D3, N.C3, HOLD, N.G2,
      N.F2, HOLD, HOLD, N.C3, HOLD, REST,

      N.D2, HOLD, HOLD, N.A2, HOLD, REST,
      N.Bb2, HOLD, HOLD, N.F2, HOLD, REST,
      N.G2, HOLD, HOLD, N.D3, HOLD, REST,
      N.A2, HOLD, HOLD, N.E3, HOLD, REST,

      N.Bb2, HOLD, HOLD, N.F3, HOLD, REST,
      N.A2, HOLD, HOLD, N.C3, HOLD, REST,
      N.G2, HOLD, N.D3, N.C3, HOLD, N.G2,
      N.F2, HOLD, HOLD, N.F2, HOLD, REST,
    ],
    drum: [
      3, 0, 0, 1, 0, 0,  3, 0, 0, 1, 0, 0,
      3, 0, 0, 1, 0, 0,  3, 0, 1, 0, 1, 0,
      3, 0, 0, 1, 0, 0,  3, 0, 0, 1, 0, 0,
      3, 0, 0, 3, 0, 1,  3, 0, 0, 0, 0, 0,
      3, 0, 0, 1, 0, 0,  3, 0, 0, 1, 0, 0,
      3, 0, 0, 1, 0, 0,  3, 0, 1, 2, 0, 1,
      3, 0, 0, 1, 0, 0,  3, 0, 0, 1, 0, 0,
      3, 0, 0, 3, 0, 1,  3, 0, 0, 0, 0, 0,
    ],
  },

  /* ==========================================================================
   * "BEYOND THE HOLLOW OAK" — Act III Evertale Vista Wonder Suite
   * Soaring G Lydian / G Major fantasy theme with celesta sparkles & sweeping flute
   * ========================================================================== */
  CINEMA_WONDER: {
    stepMs: 225,
    style: 'CELESTA',
    lead: [
      /* Wonder rising: G -> A/G -> Bm7 -> Em9 */
      N.D5, HOLD, N.G5, N.A5, N.B5, HOLD, N.A5, N.G5,
      N.Fs5, HOLD, N.E5, N.D5, N.E5, HOLD, HOLD, REST,
      N.D5, HOLD, N.G5, N.A5, N.B5, HOLD, N.D6, N.B5,
      N.A5, HOLD, N.G5, N.Fs5, N.G5, HOLD, HOLD, REST,

      /* Soaring Panorama: Cmaj7 -> D/C -> Bm7 -> Em7 -> Am7 -> D7 -> G */
      N.E5, HOLD, N.G5, N.B5, N.A5, HOLD, N.G5, N.Fs5,
      N.D5, HOLD, N.Fs5, N.A5, N.G5, HOLD, N.E5, REST,
      N.C5, N.D5, N.E5, N.G5, N.Fs5, HOLD, N.E5, N.D5,
      N.G5, HOLD, N.B5, N.D6, N.G5, HOLD, HOLD, REST,
    ],
    counter: [
      N.B4, HOLD, N.D5, N.E5, N.G5, HOLD, N.Fs5, N.E5,
      N.Cs5, HOLD, N.B4, N.A4, N.B4, HOLD, HOLD, REST,
      N.B4, HOLD, N.D5, N.E5, N.G5, HOLD, N.A5, N.G5,
      N.Fs5, HOLD, N.E5, N.D5, N.D5, HOLD, HOLD, REST,

      N.C5, HOLD, N.E5, N.G5, N.Fs5, HOLD, N.E5, N.D5,
      N.B4, HOLD, N.D5, N.Fs5, N.E5, HOLD, N.B4, REST,
      N.A4, HOLD, N.C5, N.E5, N.D5, HOLD, N.C5, N.A4,
      N.B4, HOLD, N.D5, N.G5, N.B4, HOLD, HOLD, REST,
    ],
    arp: [
      N.G3, N.D4, N.G4, N.B4, N.D5, N.B4, N.G4, N.D4,
      N.G3, N.Cs4, N.E4, N.A4, N.Cs5, N.A4, N.E4, N.Cs4,
      N.B3, N.D4, N.Fs4, N.A4, N.D5, N.A4, N.Fs4, N.D4,
      N.E3, N.B3, N.E4, N.G4, N.B4, N.G4, N.E4, N.B3,

      N.C4, N.E4, N.G4, N.B4, N.D4, N.Fs4, N.A4, N.D5,
      N.B3, N.D4, N.Fs4, N.A4, N.E3, N.B3, N.E4, N.G4,
      N.A3, N.C4, N.E4, N.G4, N.D3, N.A3, N.D4, N.Fs4,
      N.G3, N.D4, N.G4, N.B4, N.D5, N.G5, N.D5, N.B4,
    ],
    bass: [
      N.G2, HOLD, HOLD, HOLD, N.G2, HOLD, N.D3, HOLD,
      N.G2, HOLD, HOLD, HOLD, N.A2, HOLD, N.E3, HOLD,
      N.B2, HOLD, HOLD, HOLD, N.Fs3, HOLD, N.D3, HOLD,
      N.E2, HOLD, HOLD, HOLD, N.B2, HOLD, N.G2, HOLD,

      N.C3, HOLD, HOLD, HOLD, N.D3, HOLD, HOLD, HOLD,
      N.B2, HOLD, HOLD, HOLD, N.E2, HOLD, HOLD, HOLD,
      N.A2, HOLD, HOLD, HOLD, N.D2, HOLD, N.A2, HOLD,
      N.G2, HOLD, HOLD, HOLD, N.G2, HOLD, REST, REST,
    ],
    drum: [
      3, 0, 0, 1, 0, 0, 1, 0,  3, 0, 0, 1, 0, 0, 1, 0,
      3, 0, 0, 1, 0, 0, 1, 0,  3, 0, 0, 1, 0, 1, 0, 0,
      3, 0, 0, 1, 0, 0, 1, 0,  3, 0, 0, 1, 0, 0, 1, 0,
      3, 0, 1, 0, 3, 0, 1, 0,  3, 0, 0, 0, 0, 0, 0, 0,
    ],
  },

  /* ==========================================================================
   * "THE HOLLOW MIRROR" — Act IV Violet Eclipse & Shadow Grandma Suite
   * Haunting C harmonic minor music-box & bowed dark cello drone
   * ========================================================================== */
  SHADOW: {
    stepMs: 285,
    style: 'EERIE_BELL',
    lead: [
      /* Cold cracked music box motif (Cm -> Abmaj7 -> Fm7 -> G7b9) */
      N.G5, HOLD, N.Eb5, HOLD, N.D5, HOLD, N.B4, HOLD,
      N.C5, HOLD, N.Gs4, HOLD, N.G4, HOLD, REST, REST,
      N.Eb5, HOLD, N.G5, N.Fs5, N.F5, HOLD, N.D5, HOLD,
      N.Eb5, HOLD, N.D5, N.B4, N.C5, HOLD, HOLD, REST,

      /* Shadow Grandma's temptation: Descending chromatic dread */
      N.C5, HOLD, N.B4, HOLD, N.Bb4, HOLD, N.A4, HOLD,
      N.Gs4, HOLD, N.G4, N.F4, N.Eb4, HOLD, N.D4, HOLD,
      N.Eb5, HOLD, N.D5, N.C5, N.B4, HOLD, N.G4, N.B4,
      N.C5, HOLD, N.G4, HOLD, N.C5, HOLD, REST, REST,
    ],
    counter: [
      N.C4, HOLD, HOLD, HOLD, N.B3, HOLD, HOLD, HOLD,
      N.Gs3, HOLD, HOLD, HOLD, N.G3, HOLD, HOLD, REST,
      N.C4, HOLD, HOLD, N.Eb4, N.D4, HOLD, HOLD, HOLD,
      N.Gs3, HOLD, N.G3, HOLD, N.C4, HOLD, HOLD, REST,

      N.Eb4, HOLD, N.D4, HOLD, N.C4, HOLD, N.C4, HOLD,
      N.C4, HOLD, N.B3, HOLD, N.C4, HOLD, N.B3, HOLD,
      N.C4, HOLD, N.Gs3, HOLD, N.G3, HOLD, N.F3, HOLD,
      N.Eb3, HOLD, N.G3, HOLD, N.C4, HOLD, REST, REST,
    ],
    arp: [
      N.C4, N.Eb4, N.G4, REST, N.B3, N.D4, N.G4, REST,
      N.Gs3, N.C4, N.Eb4, REST, N.G3, N.B3, N.D4, REST,
      N.C4, N.Eb4, N.Fs4, REST, N.B3, N.D4, N.F4, REST,
      N.Gs3, N.C4, N.G3, N.B3, N.C4, REST, REST, REST,

      N.C4, N.G4, N.Eb4, REST, N.B3, N.G4, N.D4, REST,
      N.Gs3, N.Eb4, N.C4, REST, N.F3, N.C4, N.Gs3, REST,
      N.G3, N.D4, N.B3, REST, N.G3, N.F4, N.D4, REST,
      N.C4, N.Eb4, N.G4, REST, N.C4, REST, REST, REST,
    ],
    bass: [
      N.C2, HOLD, HOLD, HOLD, N.B2, HOLD, HOLD, HOLD,
      N.Gs2, HOLD, HOLD, HOLD, N.G2, HOLD, HOLD, HOLD,
      N.C2, HOLD, HOLD, HOLD, N.F2, HOLD, HOLD, HOLD,
      N.Gs2, HOLD, N.G2, HOLD, N.C2, HOLD, REST, REST,

      N.C2, HOLD, HOLD, HOLD, N.Bb2, HOLD, HOLD, HOLD,
      N.Gs2, HOLD, HOLD, HOLD, N.F2, HOLD, HOLD, HOLD,
      N.G2, HOLD, HOLD, HOLD, N.G2, HOLD, N.B2, HOLD,
      N.C2, HOLD, HOLD, HOLD, N.C2, HOLD, REST, REST,
    ],
    drum: [
      3, 0, 0, 0, 0, 0, 0, 0,  3, 0, 0, 0, 0, 0, 1, 0,
      3, 0, 0, 0, 0, 0, 0, 0,  3, 0, 0, 0, 3, 0, 0, 0,
      3, 0, 0, 0, 3, 0, 0, 0,  3, 0, 0, 0, 0, 0, 1, 0,
      3, 0, 0, 1, 3, 0, 0, 0,  3, 0, 0, 0, 0, 0, 0, 0,
    ],
  },

  /* ==========================================================================
   * "LANTERN LULLABY" — Acts V & VI (Grandma Mara's Comfort & Pip at the Window)
   * Tender D Major / B Minor cradle melody on soft celesta, harp, and lullaby flute
   * ========================================================================== */
  CINEMA_LULLABY: {
    stepMs: 280,
    style: 'LULLABY',
    lead: [
      /* Cradle theme: D -> F#m -> Gmaj7 -> A7 */
      N.Fs4, HOLD, N.A4, N.D5, N.Cs5, HOLD, N.A4, HOLD,
      N.B4, HOLD, N.D5, N.B4, N.A4, HOLD, HOLD, REST,
      N.G4, HOLD, N.B4, N.D5, N.Cs5, HOLD, N.B4, N.A4,
      N.E4, HOLD, N.Fs4, N.G4, N.A4, HOLD, HOLD, REST,

      /* Pip's midnight promise: Bm7 -> F#m7 -> Gmaj7 -> Em7 -> A7 -> D */
      N.D5, HOLD, N.E5, N.Fs5, N.E5, HOLD, N.D5, N.Cs5,
      N.B4, HOLD, N.Cs5, N.D5, N.A4, HOLD, HOLD, REST,
      N.B4, HOLD, N.G4, N.A4, N.Fs4, HOLD, N.E4, N.D4,
      N.E4, HOLD, N.Cs4, N.E4, N.D4, HOLD, HOLD, REST,
    ],
    counter: [
      REST, REST, N.Fs4, HOLD, N.A4, HOLD, N.Fs4, HOLD,
      N.G4, HOLD, N.B4, HOLD, N.Fs4, HOLD, HOLD, REST,
      N.D4, HOLD, N.G4, HOLD, N.E4, HOLD, N.G4, N.Fs4,
      N.Cs4, HOLD, N.D4, N.E4, N.E4, HOLD, HOLD, REST,

      N.Fs4, HOLD, N.B4, HOLD, N.A4, HOLD, N.Fs4, HOLD,
      N.G4, HOLD, N.A4, N.B4, N.Fs4, HOLD, HOLD, REST,
      N.G4, HOLD, N.E4, HOLD, N.D4, HOLD, N.B3, HOLD,
      N.Cs4, HOLD, N.A3, HOLD, N.A3, HOLD, HOLD, REST,
    ],
    arp: [
      N.D3, N.A3, N.D4, N.Fs4, N.Fs3, N.Cs4, N.Fs4, N.A4,
      N.G3, N.D4, N.G4, N.B4, N.D3, N.A3, N.D4, N.Fs4,
      N.G3, N.D4, N.G4, N.B4, N.A3, N.E4, N.A4, N.Cs5,
      N.E3, N.B3, N.E4, N.G4, N.A3, N.E4, N.A4, N.G4,

      N.B3, N.Fs4, N.B4, N.D5, N.Fs3, N.Cs4, N.Fs4, N.A4,
      N.G3, N.D4, N.G4, N.B4, N.D3, N.A3, N.D4, N.Fs4,
      N.G3, N.D4, N.G4, N.B4, N.D3, N.A3, N.D4, N.Fs4,
      N.A3, N.E4, N.G4, N.Cs5, N.D3, N.A3, N.D4, N.Fs4,
    ],
    bass: [
      N.D2, HOLD, HOLD, HOLD, N.Fs2, HOLD, HOLD, HOLD,
      N.G2, HOLD, HOLD, HOLD, N.D2, HOLD, HOLD, HOLD,
      N.G2, HOLD, HOLD, HOLD, N.A2, HOLD, HOLD, HOLD,
      N.E2, HOLD, HOLD, HOLD, N.A2, HOLD, HOLD, HOLD,

      N.B2, HOLD, HOLD, HOLD, N.Fs2, HOLD, HOLD, HOLD,
      N.G2, HOLD, HOLD, HOLD, N.D2, HOLD, HOLD, HOLD,
      N.G2, HOLD, HOLD, HOLD, N.D2, HOLD, HOLD, HOLD,
      N.A2, HOLD, HOLD, HOLD, N.D2, HOLD, HOLD, REST,
    ],
    drum: [
      0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0,
    ],
  },

  HAMLET: {
    stepMs: 235,
    style: 'FLUTE',
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

  WHISKERBURGH: {
    stepMs: 185,
    style: 'CELESTA',
    lead: [
      N.F4, N.A4, N.C5, N.A4, N.D5, N.C5, N.Bb4, N.A4,
      N.G4, N.Bb4, N.D5, N.C5, N.A4, HOLD, N.F4, REST,
      N.C5, N.D5, N.F5, N.E5, N.D5, N.C5, N.Bb4, N.G4,
      N.A4, N.C5, N.Bb4, N.G4, N.F4, HOLD, REST, REST,
    ],
    arp: [
      N.F3, N.A3, N.C4, N.A3, N.Bb3, N.D4, N.F4, N.D4,
      N.C4, N.E4, N.G4, N.E4, N.F3, N.A3, N.C4, N.A3,
      N.A3, N.C4, N.F4, N.C4, N.Bb3, N.D4, N.G4, N.D4,
      N.C4, N.E4, N.G4, N.E4, N.F3, N.A3, N.C4, N.F4,
    ],
    bass: [
      N.F2, N.C3, N.F2, N.C3, N.Bb2, N.F3, N.Bb2, N.F2,
      N.C3, N.G2, N.C3, N.G2, N.F2, N.C3, N.F2, REST,
      N.F2, N.C3, N.A2, N.C3, N.Bb2, N.F3, N.G2, N.D3,
      N.C3, N.G2, N.C3, N.G2, N.F2, HOLD, REST, REST,
    ],
    drum: [
      3, 1, 2, 1, 3, 1, 2, 1,  3, 1, 2, 1, 3, 1, 2, 0,
      3, 1, 2, 1, 3, 1, 2, 1,  3, 1, 2, 1, 3, 2, 1, 0,
    ],
  },

  SEWERS: {
    stepMs: 235,
    style: 'EERIE_BELL',
    lead: [
      N.E4, REST, N.G4, REST, N.Fs4, N.E4, N.B3, HOLD,
      N.C4, REST, N.B3, REST, N.A3, HOLD, REST, REST,
      N.E4, N.Fs4, N.G4, REST, N.A4, N.G4, N.Fs4, HOLD,
      N.D4, REST, N.Fs4, N.E4, N.E4, HOLD, REST, REST,
    ],
    arp: [
      N.E3, N.B3, N.G3, N.B3, N.E3, N.B3, N.Fs3, N.B3,
      N.C3, N.G3, N.E3, N.G3, N.B2, N.Fs3, N.D3, N.Fs3,
      N.E3, N.B3, N.G3, N.B3, N.A2, N.E3, N.C3, N.E3,
      N.B2, N.Fs3, N.Eb3, N.Fs3, N.E3, N.B3, N.G3, REST,
    ],
    bass: [
      N.E2, REST, REST, N.E2, N.G2, REST, N.Fs2, REST,
      N.C2, REST, REST, N.C2, N.B2, REST, N.B2, REST,
      N.E2, REST, N.G2, REST, N.A2, REST, N.Fs2, REST,
      N.B2, REST, N.B2, REST, N.E2, HOLD, REST, REST,
    ],
    drum: [
      3, 0, 0, 1, 0, 0, 2, 0,  3, 0, 0, 1, 0, 0, 1, 0,
      3, 0, 1, 0, 0, 0, 2, 0,  3, 0, 1, 0, 3, 0, 0, 0,
    ],
  },

  BOARHOLD: {
    stepMs: 175,
    style: 'STRINGS',
    lead: [
      N.G4, HOLD, N.G4, N.A4, N.Bb4, HOLD, N.A4, N.G4,
      N.F4, HOLD, N.G4, N.A4, N.D4, HOLD, REST, N.D4,
      N.Bb4, HOLD, N.C5, N.D5, N.C5, N.Bb4, N.A4, N.G4,
      N.A4, HOLD, N.Fs4, N.A4, N.G4, HOLD, REST, REST,
    ],
    arp: [
      N.G3, N.Bb3, N.D4, N.Bb3, N.G3, N.Bb3, N.D4, N.Bb3,
      N.F3, N.A3, N.C4, N.A3, N.D3, N.Fs3, N.A3, N.Fs3,
      N.Eb3, N.G3, N.Bb3, N.G3, N.C3, N.Eb3, N.G3, N.Eb3,
      N.D3, N.Fs3, N.A3, N.Fs3, N.G3, N.Bb3, N.D4, N.G3,
    ],
    bass: [
      N.G2, N.G2, N.D2, N.G2, N.G2, N.G2, N.F2, N.G2,
      N.F2, N.F2, N.C2, N.F2, N.D2, N.D2, N.A2, N.D2,
      N.Eb2, N.Eb2, N.Bb2, N.Eb2, N.C2, N.C2, N.G2, N.C2,
      N.D2, N.D2, N.D2, N.D2, N.G2, HOLD, N.D2, N.G2,
    ],
    drum: [
      3, 1, 2, 3, 3, 1, 2, 1,  3, 1, 2, 3, 3, 2, 2, 1,
      3, 1, 2, 3, 3, 1, 2, 1,  3, 2, 2, 2, 3, 1, 2, 2,
    ],
  },

  BATTLE: {
    stepMs: 152,
    style: 'ACTION',
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
    stepMs: 138,
    style: 'ACTION',
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
    style: 'CELESTA',
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
  private masterBus: GainNode | null = null;
  private reverbSend: GainNode | null = null;
  private muted = false;
  private bgmEnabled = true;
  private bgmTimer: number | null = null;
  private bgmStep = 0;
  private currentTrack: BgmTrackId = 'ALDERBROOK';

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

  private initAudioGraph(ctx: AudioContext): void {
    const comp = ctx.createDynamicsCompressor();
    comp.threshold.value = -14;
    comp.knee.value = 18;
    comp.ratio.value = 4;
    comp.attack.value = 0.01;
    comp.release.value = 0.2;

    const master = ctx.createGain();
    master.gain.value = 0.92;
    master.connect(comp);
    comp.connect(ctx.destination);

    /* Lush warm orchestral chamber echo/reverb bus (twin cross-damped delays) */
    const send = ctx.createGain();
    send.gain.value = 0.34;

    const d1 = ctx.createDelay(1.0);
    d1.delayTime.value = 0.23;
    const d2 = ctx.createDelay(1.0);
    d2.delayTime.value = 0.37;

    const lp1 = ctx.createBiquadFilter();
    lp1.type = 'lowpass';
    lp1.frequency.value = 2200;
    const lp2 = ctx.createBiquadFilter();
    lp2.type = 'lowpass';
    lp2.frequency.value = 1800;

    const fb1 = ctx.createGain();
    fb1.gain.value = 0.36;
    const fb2 = ctx.createGain();
    fb2.gain.value = 0.32;

    send.connect(d1);
    d1.connect(lp1);
    lp1.connect(master);
    lp1.connect(fb1);
    fb1.connect(d2);

    d2.connect(lp2);
    lp2.connect(master);
    lp2.connect(fb2);
    fb2.connect(d1);

    this.masterBus = master;
    this.reverbSend = send;
  }

  public unlock(): void {
    if (!this.ctx && typeof window !== 'undefined') {
      const AudioCtx =
        window.AudioContext ||
        (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      if (AudioCtx) {
        this.ctx = new AudioCtx();
        this.initAudioGraph(this.ctx);
      }
    }
    if (this.ctx && this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }
    if (!this.muted && this.bgmEnabled && this.bgmTimer === null) {
      this.startBgmLoop();
    }
  }

  public setTrack(want: BgmTrackId): void {
    if (this.currentTrack !== want) {
      this.currentTrack = want;
      this.bgmStep = 0;
      if (this.bgmTimer !== null) {
        this.stopBgmLoop();
        this.startBgmLoop();
      }
    }
  }

  public setMusicMode(mode: GameMode, isBossBattle = false): void {
    let want: BgmTrackId = 'ALDERBROOK';
    if (mode === 'BATTLE') {
      want = isBossBattle ? 'BOSS' : 'BATTLE';
    } else if (mode === 'BATTLE_VICTORY') {
      want = 'VICTORY';
    } else if (mode === 'EXPLORE') {
      want = 'HAMLET';
    }
    this.setTrack(want);
  }

  public processQueue(queue: SfxId[]): void {
    if (this.muted || queue.length === 0) return;
    if (!this.ctx || this.ctx.state !== 'running') return;
    for (const sfx of queue) {
      this.playSfx(sfx);
    }
  }

  /* Expressive multi-harmonic orchestral voice */
  private playMusicalVoice(
    hz: number,
    dur: number,
    role: 'LEAD' | 'COUNTER' | 'ARP' | 'BASS',
    style: VoiceStyle
  ): void {
    if (!this.ctx || !this.masterBus || !this.reverbSend || this.muted) return;
    const t0 = this.ctx.currentTime;
    const dest = this.masterBus;
    const rev = this.reverbSend;

    if (role === 'LEAD' && (style === 'FLUTE' || style === 'LULLABY')) {
      /* Lyrical Ghibli-style flute / mellow woodwind with gentle vibrato */
      const osc1 = this.ctx.createOscillator();
      const osc2 = this.ctx.createOscillator();
      const vib = this.ctx.createOscillator();
      const vibGain = this.ctx.createGain();
      const filter = this.ctx.createBiquadFilter();
      const gain = this.ctx.createGain();

      osc1.type = 'sine';
      osc2.type = 'triangle';
      osc1.frequency.setValueAtTime(hz, t0);
      osc2.frequency.setValueAtTime(hz * 2, t0);

      vib.type = 'sine';
      vib.frequency.setValueAtTime(5.1, t0);
      vibGain.gain.setValueAtTime(0.01, t0);
      vibGain.gain.linearRampToValueAtTime(hz * 0.0055, t0 + Math.min(0.28, dur * 0.6));
      vib.connect(vibGain);
      vibGain.connect(osc1.frequency);

      const g2 = this.ctx.createGain();
      g2.gain.value = style === 'LULLABY' ? 0.14 : 0.22;

      filter.type = 'lowpass';
      filter.frequency.setValueAtTime(style === 'LULLABY' ? 1900 : 2800, t0);

      const peak = style === 'LULLABY' ? 0.056 : 0.068;
      const atk = Math.min(0.055, dur * 0.25);
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.linearRampToValueAtTime(peak, t0 + atk);
      gain.gain.setValueAtTime(peak * 0.88, t0 + Math.max(atk, dur * 0.72));
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur + 0.14);

      osc1.connect(filter);
      osc2.connect(g2);
      g2.connect(filter);
      filter.connect(gain);
      gain.connect(dest);
      gain.connect(rev);

      osc1.start(t0);
      osc2.start(t0);
      vib.start(t0);
      osc1.stop(t0 + dur + 0.18);
      osc2.stop(t0 + dur + 0.18);
      vib.stop(t0 + dur + 0.18);
      return;
    }

    if (role === 'LEAD' && (style === 'CELESTA' || style === 'EERIE_BELL')) {
      /* Shimmering celesta / haunting music box bell */
      const osc1 = this.ctx.createOscillator();
      const osc2 = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      const g2 = this.ctx.createGain();

      osc1.type = 'sine';
      osc2.type = 'sine';
      osc1.frequency.setValueAtTime(hz, t0);
      /* Subtle detuned overtone for haunted music box vs crisp celesta */
      osc2.frequency.setValueAtTime(style === 'EERIE_BELL' ? hz * 2.98 : hz * 2.0, t0);
      g2.gain.value = style === 'EERIE_BELL' ? 0.18 : 0.24;

      const tail = style === 'EERIE_BELL' ? Math.max(0.65, dur * 1.45) : Math.max(0.45, dur * 1.15);
      const peak = style === 'EERIE_BELL' ? 0.055 : 0.064;
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.linearRampToValueAtTime(peak, t0 + 0.008);
      gain.gain.exponentialRampToValueAtTime(peak * 0.35, t0 + tail * 0.45);
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + tail);

      osc1.connect(gain);
      osc2.connect(g2);
      g2.connect(gain);
      gain.connect(dest);
      gain.connect(rev);

      osc1.start(t0);
      osc2.start(t0);
      osc1.stop(t0 + tail + 0.04);
      osc2.stop(t0 + tail + 0.04);
      return;
    }

    if (role === 'COUNTER' || (role === 'LEAD' && style === 'STRINGS')) {
      /* Lush detuned warm string ensemble */
      const osc1 = this.ctx.createOscillator();
      const osc2 = this.ctx.createOscillator();
      const filter = this.ctx.createBiquadFilter();
      const gain = this.ctx.createGain();

      osc1.type = 'sawtooth';
      osc2.type = 'triangle';
      osc1.frequency.setValueAtTime(hz * 0.998, t0);
      osc2.frequency.setValueAtTime(hz * 1.002, t0);

      filter.type = 'lowpass';
      filter.frequency.setValueAtTime(role === 'COUNTER' ? 1150 : 1850, t0);

      const peak = role === 'COUNTER' ? 0.026 : 0.046;
      const atk = Math.min(0.08, dur * 0.3);
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.linearRampToValueAtTime(peak, t0 + atk);
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur + 0.16);

      osc1.connect(filter);
      osc2.connect(filter);
      filter.connect(gain);
      gain.connect(dest);
      gain.connect(rev);

      osc1.start(t0);
      osc2.start(t0);
      osc1.stop(t0 + dur + 0.2);
      osc2.stop(t0 + dur + 0.2);
      return;
    }

    if (role === 'ARP') {
      /* Plucked warm harp / soft piano arpeggio */
      const osc = this.ctx.createOscillator();
      const filter = this.ctx.createBiquadFilter();
      const gain = this.ctx.createGain();
      osc.type = style === 'ACTION' ? 'triangle' : 'sine';
      osc.frequency.setValueAtTime(hz, t0);

      filter.type = 'lowpass';
      filter.frequency.setValueAtTime(style === 'ACTION' ? 2400 : 1800, t0);
      filter.frequency.exponentialRampToValueAtTime(600, t0 + dur * 1.2);

      const peak = style === 'LULLABY' ? 0.032 : style === 'ACTION' ? 0.022 : 0.036;
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.linearRampToValueAtTime(peak, t0 + 0.006);
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur * 1.35);

      osc.connect(filter);
      filter.connect(gain);
      gain.connect(dest);
      if (style !== 'ACTION') gain.connect(rev);

      osc.start(t0);
      osc.stop(t0 + dur * 1.4 + 0.02);
      return;
    }

    if (role === 'BASS') {
      /* Warm bowed cello / upright bass */
      const osc = this.ctx.createOscillator();
      const sub = this.ctx.createOscillator();
      const filter = this.ctx.createBiquadFilter();
      const gain = this.ctx.createGain();
      osc.type = style === 'EERIE_BELL' || style === 'STRINGS' ? 'sawtooth' : 'triangle';
      sub.type = 'sine';
      osc.frequency.setValueAtTime(hz, t0);
      sub.frequency.setValueAtTime(hz, t0);

      filter.type = 'lowpass';
      filter.frequency.setValueAtTime(style === 'ACTION' ? 950 : 520, t0);

      const peak = style === 'EERIE_BELL' ? 0.055 : style === 'ACTION' ? 0.05 : 0.048;
      gain.gain.setValueAtTime(0.0001, t0);
      gain.gain.linearRampToValueAtTime(peak, t0 + (style === 'ACTION' ? 0.01 : 0.045));
      gain.gain.exponentialRampToValueAtTime(0.0001, t0 + dur * 1.15);

      osc.connect(filter);
      sub.connect(filter);
      filter.connect(gain);
      gain.connect(dest);

      osc.start(t0);
      sub.start(t0);
      osc.stop(t0 + dur * 1.2 + 0.03);
      sub.stop(t0 + dur * 1.2 + 0.03);
      return;
    }

    /* Action / Energetic 16-bit lead */
    this.tone(hz, hz, dur, 'square', 0.036);
  }

  private tone(
    freq: number,
    endFreq: number,
    dur: number,
    type: OscillatorType,
    vol: number,
    delay = 0
  ): void {
    if (!this.ctx || !this.masterBus || this.muted) return;
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
    gain.connect(this.masterBus);

    osc.start(t0);
    osc.stop(t0 + dur + 0.02);
  }

  private noiseBurst(dur: number, cutoff: number, vol: number, delay = 0): void {
    if (!this.ctx || !this.masterBus || this.muted) return;
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
    gain.connect(this.masterBus);

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
        this.tone(540, 660, 0.045, 'triangle', 0.075);
        break;
      case SfxId.CONFIRM:
        this.tone(523, 784, 0.075, 'triangle', 0.09);
        this.tone(784, 1046, 0.08, 'sine', 0.065, 0.045);
        break;
      case SfxId.CANCEL:
        this.tone(330, 210, 0.09, 'triangle', 0.085);
        break;
      case SfxId.TEXT_HERO:
        this.tone(440, 460, 0.028, 'triangle', 0.042);
        break;
      case SfxId.TEXT_PIP:
        this.tone(720, 790, 0.032, 'sine', 0.048);
        break;
      case SfxId.TEXT_ELDER:
        this.tone(260, 240, 0.032, 'triangle', 0.045);
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
      case SfxId.COIN:
        this.tone(988, 988, 0.06, 'triangle', 0.1, 0);
        this.tone(1319, 1319, 0.12, 'sine', 0.1, 0.055);
        break;
      case SfxId.FISH_BITE:
        this.tone(880, 1320, 0.08, 'square', 0.09);
        this.noiseBurst(0.07, 1400, 0.08);
        break;
      case SfxId.FISH_REEL:
        this.tone(480, 620, 0.03, 'triangle', 0.06);
        break;
      case SfxId.FISH_CATCH:
        this.tone(587, 587, 0.1, 'triangle', 0.1, 0);
        this.tone(740, 740, 0.1, 'triangle', 0.1, 0.075);
        this.tone(880, 1175, 0.24, 'sine', 0.11, 0.15);
        break;
      case SfxId.PORTAL:
        this.tone(392, 784, 0.22, 'sine', 0.09);
        this.tone(587, 1175, 0.22, 'triangle', 0.07, 0.05);
        break;
    }
  }

  private countHold(seq: readonly number[], idx: number): number {
    let holdSteps = 1;
    while (idx + holdSteps < seq.length && seq[idx + holdSteps] === HOLD) {
      holdSteps++;
    }
    return holdSteps;
  }

  private startBgmLoop(): void {
    if (this.bgmTimer !== null) return;
    const track = BGM_TRACKS[this.currentTrack];
    const stepSec = track.stepMs / 1000;

    this.bgmTimer = window.setInterval(() => {
      if (this.muted || !this.bgmEnabled || !this.ctx || this.ctx.state !== 'running') return;

      const cur = BGM_TRACKS[this.currentTrack];
      const idx = this.bgmStep % cur.lead.length;
      const isAction = cur.style === 'ACTION';

      const leadNote = cur.lead[idx];
      if (leadNote !== REST && leadNote !== HOLD) {
        const dur = Math.min(1.6, this.countHold(cur.lead, idx) * stepSec * 0.96);
        this.playMusicalVoice(midiToHz(leadNote), dur, 'LEAD', cur.style);
      }

      if (cur.counter) {
        const cNote = cur.counter[idx];
        if (cNote !== REST && cNote !== HOLD) {
          const dur = Math.min(1.6, this.countHold(cur.counter, idx) * stepSec * 0.96);
          this.playMusicalVoice(midiToHz(cNote), dur, 'COUNTER', cur.style);
        }
      }

      const arpNote = cur.arp[idx];
      if (arpNote !== REST && arpNote !== HOLD) {
        this.playMusicalVoice(midiToHz(arpNote), stepSec * 0.95, 'ARP', cur.style);
      }

      const bassNote = cur.bass[idx];
      if (bassNote !== REST && bassNote !== HOLD) {
        const dur = Math.min(1.5, this.countHold(cur.bass, idx) * stepSec * 0.95);
        this.playMusicalVoice(midiToHz(bassNote), dur, 'BASS', cur.style);
      }

      const drum = cur.drum[idx];
      if (drum === 3) {
        this.tone(118, 44, 0.095, 'sine', isAction ? 0.058 : 0.028);
      } else if (drum === 2) {
        this.noiseBurst(0.065, 1600, isAction ? 0.036 : 0.016);
      } else if (drum === 1) {
        this.noiseBurst(0.032, 3400, isAction ? 0.016 : 0.008);
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
      this.masterBus = null;
      this.reverbSend = null;
    }
  }
}
