/**
 * Deterministic 16-Bit Galois LFSR PRNG (C99-identical)
 *
 * Replaces Math.random() inside the engine core so replays, combat rolls,
 * and console ports behave identically frame-for-frame.
 */

export class RetroPrng {
  private state: number;

  constructor(seed: number = 0xACE1) {
    this.state = (seed & 0xFFFF) || 0xACE1;
  }

  public seed(val: number): void {
    this.state = (val & 0xFFFF) || 0xACE1;
  }

  /** Returns unsigned 16-bit integer [0..65535] */
  public next16(): number {
    const lsb = this.state & 1;
    this.state >>>= 1;
    if (lsb !== 0) {
      this.state ^= 0xB400;
    }
    return this.state & 0xFFFF;
  }

  /** Returns integer in inclusive range [min..max] */
  public range(min: number, max: number): number {
    if (max <= min) return min;
    const span = (max - min + 1) | 0;
    return min + (this.next16() % span);
  }
}
