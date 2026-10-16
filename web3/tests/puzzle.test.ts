import { describe, expect, it } from 'vitest';
import { puzzlePage, puzzlePages, type Page } from '../src/lib/puzzle';

const W = 16 / 9; // a wide picture
const PHONE = { w: 384, h: 760 };
const GAP = 3;

// a fixed mix of common photo shapes, extremes included (they count as 0.4 and 2.5)
function mix(n: number, seed = 7): number[] {
  const shapes = [W, W, 4 / 3, 4 / 3, 3 / 2, 1, 3 / 4, 3 / 4, 2 / 3, 9 / 16, 0.2, 5];
  let s = Math.floor(seed) || 1;
  return Array.from({ length: n }, () => shapes[(s = (s * 16807) % 2147483647) % shapes.length]);
}

/** Cells in order, on half pixels, inside the page, apart by exactly the gap, filling the page. */
function check(p: Page, box: { w: number; h: number }, gap: number) {
  const w = Math.floor(box.w * 2) / 2, h = Math.floor(box.h * 2) / 2;
  const c = p.cells;
  expect(c.length).toBe(p.count);
  expect(p.count).toBeGreaterThanOrEqual(1);
  expect(p.count).toBeLessThanOrEqual(4);
  c.forEach((a, j) => {
    expect(a.i).toBe(p.start + j);
    for (const v of [a.x, a.y, a.w, a.h]) expect(Number.isInteger(v * 2)).toBe(true);
    expect(a.w).toBeGreaterThan(0);
    expect(a.h).toBeGreaterThan(0);
    expect(a.x).toBeGreaterThanOrEqual(0);
    expect(a.y).toBeGreaterThanOrEqual(0);
    expect(a.x + a.w).toBeLessThanOrEqual(w);
    expect(a.y + a.h).toBeLessThanOrEqual(h);
  });
  const across = (a: (typeof c)[0], b: (typeof c)[0]) => a.y < b.y + b.h && b.y < a.y + a.h; // side by side
  const above = (a: (typeof c)[0], b: (typeof c)[0]) => a.x < b.x + b.w && b.x < a.x + a.w; // one over the other
  for (const a of c) {
    for (const b of c)
      if (a !== b)
        expect(a.x + a.w + gap <= b.x || b.x + b.w + gap <= a.x || a.y + a.h + gap <= b.y || b.y + b.h + gap <= a.y).toBe(true);
    // every edge is the page's or meets a neighbour exactly one gap away
    expect(a.x === 0 || c.some((b) => b.x + b.w + gap === a.x && across(a, b))).toBe(true);
    expect(a.y === 0 || c.some((b) => b.y + b.h + gap === a.y && above(a, b))).toBe(true);
    expect(a.x + a.w === w || c.some((b) => a.x + a.w + gap === b.x && across(a, b))).toBe(true);
    expect(a.y + a.h === h || c.some((b) => a.y + a.h + gap === b.y && above(a, b))).toBe(true);
  }
}

describe('puzzle pages', () => {
  it("the owner's example: a 16:9 picture across the top, two squares side by side under it", () => {
    // on a phone page, followed by a picture that fills the rest
    const p = puzzlePage([W, 1, 1, 1], 0, PHONE, GAP, true)!;
    check(p, PHONE, GAP);
    const [a, b, c, d] = p.cells;
    expect(p.count).toBe(4);
    expect([a.x, a.y, a.w]).toEqual([0, 0, 384]);
    expect(a.w / a.h).toBeCloseTo(W, 0); // whole, or nearly
    expect([b.x, b.y, c.y]).toEqual([0, a.h + GAP, a.h + GAP]);
    expect([c.x, c.x + c.w, c.h]).toEqual([b.w + GAP, 384, b.h]);
    expect([d.x, d.y, d.w]).toEqual([0, b.y + b.h + GAP, 384]);
    // three alone, on a page of their shape (about half a phone screen)
    const q = puzzlePage([W, 1, 1], 0, { w: 384, h: 420 }, GAP, true)!;
    check(q, { w: 384, h: 420 }, GAP);
    expect(q.cells.map((e) => [e.x, e.y])).toEqual([[0, 0], [0, q.cells[0].h + GAP], [q.cells[1].w + GAP, q.cells[0].h + GAP]]);
    // three alone on a whole phone page stack instead: wide-over-two is a near-square block, and
    // stretched to the tall page it would cut about 46% off them, stacked about 24%
    const s = puzzlePage([W, 1, 1], 0, PHONE, GAP, true)!;
    expect(s.cells.map((e) => [e.x, e.w])).toEqual([[0, 384], [0, 384], [0, 384]]);
  });

  it('two 16:9 pictures stack on a phone; one alone fills a 16:9 screen', () => {
    const p = puzzlePage([W, W], 0, PHONE, GAP, true)!;
    check(p, PHONE, GAP);
    expect(p.cells.map((c) => [c.x, c.y, c.w, c.h])).toEqual([[0, 0, 384, 378.5], [0, 381.5, 384, 378.5]]);
    const tv = { w: 1280, h: 720 };
    expect(puzzlePage([W, 1, 1, 1], 0, tv, GAP, true)!.cells).toEqual([{ i: 0, x: 0, y: 0, w: 1280, h: 720 }]);
  });

  it('cells lie inside the page, in order, apart by exactly the gap, on half pixels', () => {
    const boxes = [PHONE, { w: 361.4, h: 751.7 }, { w: 1400, h: 800 }, { w: 1280, h: 720 }, { w: 800, h: 800 }, { w: 1600, h: 400 }];
    for (const box of boxes)
      for (const gap of [0, 3, 6]) {
        const r = mix(120, box.w + gap);
        const pages = puzzlePages(r, box, gap, true);
        expect(pages.reduce((n, p) => n + p.count, 0)).toBe(r.length);
        for (const p of pages) check(p, box, gap);
      }
    // a gap off the half-pixel grid is rounded onto it
    const p = puzzlePage([W, W], 0, PHONE, 2.8, true)!;
    expect(p.cells[1].y - p.cells[0].h).toBe(3);
  });

  it('every page mixes shapes as they fit: pages of one to four, mostly several', () => {
    const pages = puzzlePages(mix(300), { w: 361, h: 752 }, GAP, true);
    const by = [0, 0, 0, 0, 0];
    for (const p of pages) by[p.count]++;
    expect(by[1]).toBeLessThan(pages.length * 0.15);
    expect(by[3] + by[4]).toBeGreaterThan(pages.length * 0.5);
    // no cell is a thumbnail: at least 22% of the page's shorter side both ways
    for (const p of pages) for (const c of p.cells) expect(Math.min(c.w, c.h)).toBeGreaterThanOrEqual(0.22 * 361);
  });

  it('a page waits for unmeasured pictures unless the list is complete', () => {
    for (const unknown of [0, NaN, -1, undefined as unknown as number]) {
      expect(puzzlePage([W, unknown, 1, 1, 1], 0, PHONE, GAP, false)).toBeNull();
      expect(puzzlePage([W, unknown, 1, 1, 1], 0, PHONE, GAP, true)).toBeNull(); // needed even so
      expect(puzzlePage([W, unknown, 1, 1, 1], 2, PHONE, GAP, true)).not.toBeNull(); // not needed
    }
    const holes = [1, 1];
    holes[3] = 1; // index 2 never set
    expect(puzzlePage(holes, 0, PHONE, GAP, true)).toBeNull();
    // the list may still grow: fewer than four left is not enough yet
    expect(puzzlePage([1, 1, 1], 0, PHONE, GAP, false)).toBeNull();
    expect(puzzlePage([1, 1, 1], 0, PHONE, GAP, true)).not.toBeNull();
    // pages stop before the first page that needs an unmeasured picture
    const r = mix(40);
    r[25] = 0;
    const pages = puzzlePages(r, PHONE, GAP, true);
    const next = pages.at(-1)!.start + pages.at(-1)!.count;
    expect(next).toBeLessThanOrEqual(25);
    expect(next).toBeGreaterThan(21);
    expect(puzzlePage(r, next, PHONE, GAP, true)).toBeNull();
  });

  it('the end of a complete list takes the one to three pictures left', () => {
    for (let n = 1; n <= 9; n++) {
      const r = mix(n, n);
      const pages = puzzlePages(r, PHONE, GAP, true);
      let at = 0;
      for (const p of pages) {
        expect(p.start).toBe(at);
        at += p.count;
      }
      expect(at).toBe(n);
      // not complete: the pages stop while fewer than four are left after them
      const open = puzzlePages(r, PHONE, GAP, false);
      const end = open.reduce((m, p) => m + p.count, 0);
      expect(n - end).toBeLessThan(4);
      expect(open).toEqual(pages.slice(0, open.length));
    }
    expect(puzzlePage([1], 0, PHONE, GAP, true)!.cells).toEqual([{ i: 0, x: 0, y: 0, w: 384, h: 760 }]);
    expect(puzzlePages([1, 1, 1], PHONE, GAP, false)).toEqual([]);
  });

  it('is deterministic, and pages already shown never change as more pictures load', () => {
    const r = mix(200);
    const all = puzzlePages(r, PHONE, GAP, true);
    expect(puzzlePages([...r], PHONE, GAP, true)).toEqual(all);
    for (const n of [37, 120, 199]) {
      const part = puzzlePages(r.slice(0, n), PHONE, GAP, false);
      expect(part.length).toBeGreaterThan(0);
      expect(part).toEqual(all.slice(0, part.length));
    }
    // from a page's start, and a limited number of pages
    expect(puzzlePages(r, PHONE, GAP, true, all[5].start, 3)).toEqual(all.slice(5, 8));
    expect(puzzlePages(r, PHONE, GAP, true, 0, 0)).toEqual([]);
  });

  it('a landscape page puts the pictures side by side as their shapes fit', () => {
    const pc = { w: 1400, h: 800 };
    // a 16:9 over a square on the left, a big square to their right
    const p = puzzlePage([W, 1, 1], 0, pc, GAP, true)!;
    check(p, pc, GAP);
    const [a, b, c] = p.cells;
    expect([a.x, a.y, b.x, b.y]).toEqual([0, 0, 0, a.h + GAP]);
    expect([c.x, c.y, c.h]).toEqual([a.w + GAP, 0, 800]);
    // four 16:9 on a 16:9 screen: 2 x 2, left to right, then down
    const tv = { w: 1280, h: 720 };
    const g = puzzlePage([W, W, W, W, 1], 0, tv, GAP, true)!;
    check(g, tv, GAP);
    expect(g.cells.map((e) => [e.x, e.y])).toEqual([[0, 0], [641.5, 0], [0, 361.5], [641.5, 361.5]]);
  });

  it('no page for an unmeasured page box or a start past the end', () => {
    expect(puzzlePage([1, 1, 1], 0, { w: 0, h: 760 }, GAP, true)).toBeNull();
    expect(puzzlePage([1, 1, 1], 0, { w: NaN, h: 760 }, GAP, true)).toBeNull();
    expect(puzzlePages([1, 1, 1], { w: 0, h: 0 }, GAP, true)).toEqual([]);
    expect(puzzlePage([1, 1, 1], 3, PHONE, GAP, true)).toBeNull();
    expect(puzzlePage([1, 1, 1], -1, PHONE, GAP, true)).toBeNull();
    expect(puzzlePages([], PHONE, GAP, true)).toEqual([]);
  });

  it('is fast: 300 pictures in well under 50 ms', () => {
    const r = mix(300);
    puzzlePages(r, PHONE, GAP, true); // warm up
    const t = performance.now();
    puzzlePages(r, PHONE, GAP, true);
    puzzlePages(r, { w: 1400, h: 800 }, GAP, true);
    expect(performance.now() - t).toBeLessThan(50);
  });
});
