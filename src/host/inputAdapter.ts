/**
 * Project Hearth — 12-Button Retro Gamepad + Keyboard Input Adapter
 *
 * Polls both browser keyboard keys and physical USB/Bluetooth controllers
 * (HTML5 Gamepad API) into the exact 12-bit uint16_t retro bitmask.
 */

import {
  PAD_A,
  PAD_B,
  PAD_DOWN,
  PAD_L,
  PAD_LEFT,
  PAD_R,
  PAD_RIGHT,
  PAD_SELECT,
  PAD_START,
  PAD_UP,
  PAD_X,
  PAD_Y,
} from '../core/types';

export class RetroInputAdapter {
  private keyBits = 0;
  private virtualBits = 0;
  private attached = false;

  private onKeyDown = (e: KeyboardEvent) => {
    // Do not hijack typing inside workbench text inputs
    const tag = (e.target as HTMLElement | null)?.tagName;
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') return;

    const bit = this.mapKey(e.code);
    if (bit !== 0) {
      e.preventDefault();
      this.keyBits |= bit;
    }
  };

  private onKeyUp = (e: KeyboardEvent) => {
    const bit = this.mapKey(e.code);
    if (bit !== 0) {
      this.keyBits &= ~bit;
    }
  };

  private onBlur = () => {
    this.keyBits = 0;
    this.virtualBits = 0;
  };

  private mapKey(code: string): number {
    switch (code) {
      case 'ArrowUp':
      case 'KeyW':
        return PAD_UP;
      case 'ArrowDown':
      case 'KeyS':
        return PAD_DOWN;
      case 'ArrowLeft':
      case 'KeyA':
        return PAD_LEFT;
      case 'ArrowRight':
      case 'KeyD':
        return PAD_RIGHT;
      case 'Space':
      case 'Enter':
      case 'KeyZ':
      case 'KeyJ':
        return PAD_A;
      case 'ShiftLeft':
      case 'ShiftRight':
      case 'KeyX':
      case 'KeyK':
        return PAD_B;
      case 'KeyC':
        return PAD_X;
      case 'KeyV':
      case 'KeyY':
        return PAD_Y;
      case 'KeyQ':
      case 'BracketLeft':
        return PAD_L;
      case 'KeyE':
      case 'BracketRight':
        return PAD_R;
      case 'Escape':
        return PAD_START;
      case 'Backspace':
        return PAD_SELECT;
      default:
        return 0;
    }
  }

  public attach(): void {
    if (this.attached) return;
    window.addEventListener('keydown', this.onKeyDown, { passive: false });
    window.addEventListener('keyup', this.onKeyUp);
    window.addEventListener('blur', this.onBlur);
    this.attached = true;
  }

  public detach(): void {
    if (!this.attached) return;
    window.removeEventListener('keydown', this.onKeyDown);
    window.removeEventListener('keyup', this.onKeyUp);
    window.removeEventListener('blur', this.onBlur);
    this.attached = false;
  }

  public setVirtualBit(bit: number, pressed: boolean): void {
    if (pressed) this.virtualBits |= bit;
    else this.virtualBits &= ~bit;
  }

  public poll(): number {
    let bits = this.keyBits | this.virtualBits;

    // Poll physical SNES / Xbox / PlayStation gamepads if connected!
    if (typeof navigator !== 'undefined' && navigator.getGamepads) {
      const pads = navigator.getGamepads();
      const gp = pads?.[0];
      if (gp) {
        const axX = gp.axes[0] ?? 0;
        const axY = gp.axes[1] ?? 0;
        if (axX < -0.35 || gp.buttons[14]?.pressed) bits |= PAD_LEFT;
        if (axX > 0.35 || gp.buttons[15]?.pressed) bits |= PAD_RIGHT;
        if (axY < -0.35 || gp.buttons[12]?.pressed) bits |= PAD_UP;
        if (axY > 0.35 || gp.buttons[13]?.pressed) bits |= PAD_DOWN;
        if (gp.buttons[0]?.pressed) bits |= PAD_A;
        if (gp.buttons[1]?.pressed) bits |= PAD_B;
        if (gp.buttons[2]?.pressed) bits |= PAD_Y;
        if (gp.buttons[3]?.pressed) bits |= PAD_X;
        if (gp.buttons[4]?.pressed) bits |= PAD_L;
        if (gp.buttons[5]?.pressed) bits |= PAD_R;
        if (gp.buttons[9]?.pressed) bits |= PAD_START;
        if (gp.buttons[8]?.pressed) bits |= PAD_SELECT;
      }
    }

    return bits;
  }
}
