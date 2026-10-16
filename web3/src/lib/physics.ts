// The card's physics, time-based (dt in ms) so 60, 120 and 144 Hz screens behave the same. No
// framework reactivity runs per frame: callers write the resulting transforms directly.

export type Verdict = 'like' | 'pass' | 'super' | 'archive';

export const LETTER: Record<Verdict, string> = { like: 'L', pass: 'D', super: 'S', archive: 'A' };

/** A damped spring towards 0 on x, y and rotation; underdamped, so a released card wobbles a little. */
export class Spring {
  x = 0;
  y = 0;
  r = 0;
  vx = 0;
  vy = 0;
  vr = 0;
  constructor(
    public stiffness = 420,
    public damping = 26,
  ) {}

  /** Advances by dt ms; returns false once it has settled. */
  step(dt: number): boolean {
    // fixed 4 ms substeps: the same motion at 60, 120 or 144 Hz; a long frame is capped at 32 ms
    for (let left = Math.min(dt, 32); left > 0; left -= 4) {
      const s = Math.min(4, left) / 1000;
      for (const k of ['x', 'y', 'r'] as const) {
        const v = ('v' + k) as 'vx' | 'vy' | 'vr';
        const a = -this.stiffness * this[k] - this.damping * this[v];
        this[v] += a * s;
        this[k] += this[v] * s;
      }
    }
    const still = Math.abs(this.x) + Math.abs(this.y) < 0.4 && Math.abs(this.vx) + Math.abs(this.vy) < 4 && Math.abs(this.r) < 0.05;
    if (still) this.x = this.y = this.r = this.vx = this.vy = this.vr = 0;
    return !still;
  }
}

/** Pointer samples of a drag; the release velocity is taken over the last ~80 ms (px per ms). */
export class Tracker {
  private s: { x: number; y: number; t: number }[] = [];
  add(x: number, y: number, t: number) {
    this.s.push({ x, y, t });
    while (this.s.length > 2 && t - this.s[0].t > 100) this.s.shift();
  }
  velocity(now: number): { vx: number; vy: number } {
    const recent = this.s.filter((p) => now - p.t <= 60); // the last moment counts, not the whole drag
    if (recent.length < 2) return { vx: 0, vy: 0 };
    const a = recent[0], b = recent[recent.length - 1];
    const dt = Math.max(1, b.t - a.t);
    return { vx: (b.x - a.x) / dt, vy: (b.y - a.y) / dt };
  }
  reset() {
    this.s = [];
  }
}

/** Rotation while dragging: grabbed above the middle, the card tips the way it moves; below, the other way. */
export function dragRotation(dx: number, width: number, grabY: number): number {
  const side = grabY < 0.5 ? 1 : -0.6;
  return (dx / Math.max(1, width)) * 16 * side; // degrees
}

// How far a card must be pulled to count without a flick (share of the deck's width / height).
// Low on purpose: a short, quick swipe throws ("ピューン"), it is not a gauge to fill.
const FAR_X = 0.16, FAR_UP = 0.13, FAR_DOWN = 0.18;

/** Which way the card is leaning, and how far towards the throw threshold (0..1). */
export function lean(dx: number, dy: number, w: number, h: number): { verdict: Verdict | null; amount: number } {
  if (dx === 0 && dy === 0) return { verdict: null, amount: 0 };
  if (Math.abs(dx) >= Math.abs(dy)) {
    return { verdict: dx > 0 ? 'like' : 'pass', amount: Math.min(1, Math.abs(dx) / (w * FAR_X)) };
  }
  return dy < 0
    ? { verdict: 'super', amount: Math.min(1, -dy / (h * FAR_UP)) }
    : { verdict: 'archive', amount: Math.min(1, dy / (h * FAR_DOWN)) };
}

/** The verdict of a release: far enough, or flicked fast enough; null = spring back. */
export function release(dx: number, dy: number, vx: number, vy: number, w: number, h: number): Verdict | null {
  const fast = 0.35; // px per ms: an ordinary flick of the thumb
  const min = 12; // px: a twitch at the start is not a throw
  if (Math.abs(dx) >= Math.abs(dy) || Math.abs(vx) > Math.abs(vy) * 1.4) {
    if (dx > w * FAR_X || (vx > fast && dx > min)) return 'like';
    if (dx < -w * FAR_X || (vx < -fast && dx < -min)) return 'pass';
  }
  if (dy < -h * FAR_UP || (vy < -fast && dy < -min)) return 'super';
  if (dy > h * FAR_DOWN || (vy > fast && dy > min)) return 'archive';
  return null;
}


/** The direction a thrown card leaves in: along the release velocity, at least towards its verdict. */
export function throwVelocity(v: Verdict, vx: number, vy: number): { vx: number; vy: number } {
  const min = 2.4; // px per ms: a throw always leaves within ~180 ms
  switch (v) {
    case 'like':
      return { vx: Math.max(min, vx), vy: vy * 0.6 };
    case 'pass':
      return { vx: Math.min(-min, vx), vy: vy * 0.6 };
    case 'super':
      return { vx: vx * 0.4, vy: Math.min(-min, vy) };
    case 'archive':
      return { vx: vx * 0.3, vy: Math.max(min * 0.8, vy) };
  }
}
