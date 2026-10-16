// The gallery's tile size changes smoothly (FLIP): note where each tile on screen is drawn (First),
// apply the new size (Last), draw each tile back where it was (Invert), then let it glide to its new
// place (Play). Pure geometry: Gallery.svelte measures the tiles and runs the animations.

export interface Rect {
  left: number;
  top: number;
  width: number;
  height: number;
}

/**
 * The tiles that meet the band y0..y1, as [first, last] (inclusive), or null when none does. The
 * tiles are in reading order (rows top to bottom, so their tops and bottoms never go up); a binary
 * search finds the first, so a long grid costs only the tiles on screen.
 */
export function inBand(n: number, at: (i: number) => Rect, y0: number, y1: number): [number, number] | null {
  let lo = 0;
  let hi = n;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    const r = at(mid);
    if (r.top + r.height <= y0) lo = mid + 1;
    else hi = mid;
  }
  if (lo >= n || at(lo).top >= y1) return null;
  let last = lo;
  while (last + 1 < n && at(last + 1).top < y1) last++;
  return [lo, last];
}

/** Whether two boxes overlap (a tile on screen: its box and the scroller's). */
export const meets = (a: Rect, b: Rect) =>
  a.left < b.left + b.width && b.left < a.left + a.width && a.top < b.top + b.height && b.top < a.top + a.height;

/** The index of the box nearest to the point (0 away when inside it), or -1 when there is none. */
export function nearest(boxes: Rect[], x: number, y: number): number {
  let best = -1;
  let bestD = Infinity;
  boxes.forEach((r, i) => {
    const dx = Math.max(r.left - x, 0, x - (r.left + r.width));
    const dy = Math.max(r.top - y, 0, y - (r.top + r.height));
    const d = dx * dx + dy * dy;
    if (d < bestD) {
      best = i;
      bestD = d;
    }
  });
  return best;
}

/**
 * How far to scroll down (negative: up) so the part of the anchor tile that was at height y, as it
 * was drawn (`from`), is at y again now that it is laid out at `to`. A point beside the tile counts
 * as its nearest edge.
 */
export function keepAt(from: Rect, to: Rect, y: number): number {
  const f = from.height > 0 ? Math.min(1, Math.max(0, (y - from.top) / from.height)) : 0.5;
  return to.top + f * to.height - y;
}

/** The transform (origin top left) that draws a tile laid out at `to` where `from` was. */
export function invert(from: Rect, to: Rect): string {
  const sx = to.width > 0 ? from.width / to.width : 1;
  const sy = to.height > 0 ? from.height / to.height : 1;
  return `translate(${from.left - to.left}px, ${from.top - to.top}px) scale(${sx}, ${sy})`;
}

/** Whether a tile drawn at `from` is (within half a pixel) already at `to`: nothing to animate. */
export const still = (from: Rect, to: Rect) =>
  Math.abs(from.left - to.left) < 0.5 &&
  Math.abs(from.top - to.top) < 0.5 &&
  Math.abs(from.width - to.width) < 0.5 &&
  Math.abs(from.height - to.height) < 0.5;
