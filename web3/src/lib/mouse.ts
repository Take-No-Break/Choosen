// Mouse modes 1-3 as in Version 1 (0.7.2+) and the own layout (4) saved by Version 1, plus the
// flick motion of mode 3. Pure logic: Swipe.svelte turns the commands into throws and moves.

export type MouseCmd = 'grab' | 'flick' | 'like' | 'super' | 'pass' | 'archive' | 'back' | 'fwd' | 'none';
export type MouseInput = 'left' | 'right' | 'middle' | 'wheelUp' | 'wheelDown' | 'side1' | 'side2';

export const PRESETS: Record<number, Record<MouseInput, MouseCmd>> = {
  1: { left: 'grab', right: 'grab', middle: 'none', wheelUp: 'back', wheelDown: 'fwd', side1: 'back', side2: 'fwd' },
  2: { left: 'super', right: 'back', middle: 'pass', wheelUp: 'like', wheelDown: 'pass', side1: 'archive', side2: 'fwd' },
  3: { left: 'back', right: 'flick', middle: 'none', wheelUp: 'back', wheelDown: 'fwd', side1: 'back', side2: 'fwd' },
};

const INPUTS: MouseInput[] = ['left', 'right', 'middle', 'wheelUp', 'wheelDown', 'side1', 'side2'];
const CMDS = new Set<string>(['grab', 'flick', 'like', 'super', 'pass', 'archive', 'back', 'fwd', 'none']);

/** Mode 4: "left:super;right:back;..." on top of mode 1 (first action per input; hold -> none). */
export function mouseMap(mode: number, custom = ''): Record<MouseInput, MouseCmd> {
  if (mode !== 4) return PRESETS[mode] ?? PRESETS[1];
  const m = { ...PRESETS[1] };
  for (const part of custom.split(';')) {
    const i = part.indexOf(':');
    if (i <= 0) continue;
    const input = part.slice(0, i) as MouseInput;
    const first = part.slice(i + 1).split(',').find((a) => a) ?? '';
    if (!INPUTS.includes(input) || !first) continue;
    m[input] = first === 'hold' ? 'none' : CMDS.has(first) ? (first as MouseCmd) : 'none';
  }
  return m;
}

/** PointerEvent.button -> input (0 left, 1 middle, 2 right, 3 back side button, 4 forward side button). */
export function buttonInput(button: number): MouseInput | null {
  return (['left', 'middle', 'right', 'side1', 'side2'] as const)[button] ?? null;
}

export const RATING_CMDS = new Set<MouseCmd>(['like', 'super', 'pass', 'archive']);

/**
 * Flick (mode 3): movement "leaks away" (time constant 100 ms), so slow movement never adds up to
 * a throw; 80 px of leaking movement make a flick. After a throw the next one waits until the
 * pointer has been still for 120 ms (at least 250 ms), so the hand moving back is not a flick.
 */
export class Flick {
  static readonly T = 80;
  x = 0;
  y = 0;
  private t = 0;
  private quietUntil = 0;

  reset(now: number) {
    this.x = this.y = 0;
    this.t = now;
    this.quietUntil = 0;
  }

  leak(now: number) {
    const k = Math.exp(-Math.max(0, now - this.t) / 100);
    this.x *= k;
    this.y *= k;
    this.t = now;
  }

  /** A movement; returns the flick's direction (unit-ish vector) when it makes one, else null. */
  move(mx: number, my: number, now: number): { vx: number; vy: number } | null {
    if (now < this.quietUntil) {
      if (Math.abs(mx) + Math.abs(my) > 2) this.quietUntil = Math.max(this.quietUntil, now + 120); // still moving
      this.x = this.y = 0;
      this.t = now;
      return null;
    }
    this.leak(now);
    this.x += mx;
    this.y += my;
    if (Math.max(Math.abs(this.x), Math.abs(this.y)) < Flick.T) return null;
    const out = { vx: this.x / 40, vy: this.y / 40 }; // px/ms, roughly the hand's speed
    this.x = this.y = 0;
    this.quietUntil = now + 250;
    return out;
  }
}
