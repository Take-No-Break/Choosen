// Taps and the scroll layout's swipes. Pure logic: Swipe.svelte feeds it pointer positions and times.

/** A finger that moved less than this (px) still tapped. */
export const TAP_SLOP = 10;

/** Counts taps close in time and place: 2 = double tap (好評), 3 = triple tap (超好評, scroll layout). */
export class Taps {
  private n = 0;
  private t = -1e9;
  private x = 0;
  private y = 0;
  constructor(
    private window = 300, // ms between two taps of one series
    private reach = 40, // px between them
  ) {}
  /** Records a tap; returns how many taps the series has now (1, 2, 3, ...). */
  tap(x: number, y: number, t: number): number {
    const same = t - this.t <= this.window && Math.hypot(x - this.x, y - this.y) <= this.reach;
    this.n = same ? this.n + 1 : 1;
    this.t = t;
    this.x = x;
    this.y = y;
    return this.n;
  }
  /** The series is used up (the card it was on is gone). */
  reset() {
    this.n = 0;
    this.t = -1e9;
  }
}

/** The way a drag goes once it is clear (10 px), so a scroll never turns into something else. */
export function axisOf(dx: number, dy: number): 'x' | 'y' | null {
  if (Math.max(Math.abs(dx), Math.abs(dy)) < 10) return null;
  return Math.abs(dy) >= Math.abs(dx) ? 'y' : 'x';
}

/** Scroll layout: "next" (up), "back" (down) or null (snap back). Only the vertical moves count. */
export function scrollRelease(dy: number, vy: number, h: number): 'next' | 'back' | null {
  const fast = 0.35, min = 12;
  if (dy < -h * 0.18 || (vy < -fast && dy < -min)) return 'next';
  if (dy > h * 0.18 || (vy > fast && dy > min)) return 'back';
  return null;
}

/** Two fingers: how far apart and where between them. */
export interface Pair {
  d: number;
  cx: number;
  cy: number;
}
export function pairOf(a: { x: number; y: number }, b: { x: number; y: number }): Pair {
  return { d: Math.hypot(a.x - b.x, a.y - b.y), cx: (a.x + b.x) / 2, cy: (a.y + b.y) / 2 };
}

/** A zoomed picture: scale and offset, with the top-left corner of its box as the origin. */
export interface Zoom {
  s: number;
  tx: number;
  ty: number;
}
export const NO_ZOOM: Zoom = { s: 1, tx: 0, ty: 0 };

/**
 * Pinch zoom, in the picture box's own coordinates: the point of the picture that was between the
 * fingers when they came down stays between them, at a scale from 1 to 5 times.
 */
export function pinchZoom(z0: Zoom, start: Pair, now: Pair): Zoom {
  const s = Math.min(5, Math.max(1, (z0.s * now.d) / Math.max(1, start.d)));
  const px = (start.cx - z0.tx) / z0.s;
  const py = (start.cy - z0.ty) / z0.s;
  return { s, tx: now.cx - s * px, ty: now.cy - s * py };
}

/** Play's pictures at a time, from fewest to most. */
export const PLAY_COUNTS = [1, 2, 4];

/**
 * Play: how many pictures at a time after two fingers scaled the set by k (Story.svelte). Spread
 * (k > 1) = fewer, larger ones; pinched (k < 1) = more; past 1.8x (under 0.55x) two steps. `now`: the
 * pictures on screen (a パズル page: 1 to 4). null = no change; with パズル on, a pinch always gives a
 * count (パズル gives way), the nearest one when no step is left.
 */
export function pinchCount(now: number, k: number, puzzle = false, counts: readonly number[] = PLAY_COUNTS): number | null {
  const steps = k >= 1.8 ? 2 : k >= 1.25 ? 1 : k <= 0.55 ? -2 : k <= 0.8 ? -1 : 0; // + = fewer
  if (!steps) return null;
  const way = steps > 0 ? counts.filter((c) => c < now).reverse() : counts.filter((c) => c > now);
  if (!way.length) return puzzle ? (steps > 0 ? counts[0] : counts[counts.length - 1]) : null;
  return way[Math.min(Math.abs(steps), way.length) - 1];
}

/** The set's scale while two fingers pinch it: 0.5 to 2, or only a little give where no step is left. */
export function pinchScale(k: number, now: number, puzzle = false, counts: readonly number[] = PLAY_COUNTS): number {
  const hi = now > counts[0] || puzzle ? 2 : 1.06;
  const lo = now < counts[counts.length - 1] || puzzle ? 0.5 : 0.94;
  return Math.min(hi, Math.max(lo, k));
}
