import { describe, expect, it } from 'vitest';
import { inBand, meets, nearest, keepAt, invert, still, type Rect } from '../src/lib/flip';

// a square grid: `cols` tiles of `s` px per row, `gap` px apart, the first row at `top`
const grid = (n: number, cols: number, s: number, gap: number, top = 0): Rect[] =>
  Array.from({ length: n }, (_, i) => ({ left: (i % cols) * (s + gap), top: top + Math.floor(i / cols) * (s + gap), width: s, height: s }));

describe('the tiles on screen', () => {
  it('finds the rows that meet the band, partly shown ones too', () => {
    const g = grid(40, 4, 100, 4); // rows at 0, 104, 208, ...
    expect(inBand(g.length, (i) => g[i], 150, 300)).toEqual([4, 11]); // rows 1 (104..204) and 2 (208..308)
    expect(inBand(g.length, (i) => g[i], 0, 104)).toEqual([0, 3]); // row 1 starts exactly at the end
    expect(inBand(g.length, (i) => g[i], 99, 105)).toEqual([0, 7]); // across the gap between rows 0 and 1
  });
  it('touches only a few tiles of a long grid', () => {
    const g = grid(5000, 6, 60, 3);
    let reads = 0;
    const r = inBand(g.length, (i) => (reads++, g[i]), 63 * 400, 63 * 400 + 800);
    expect(r).toEqual([2400, 2477]); // 13 rows of 6
    expect(reads).toBeLessThan(120);
  });
  it('none when the band is above, below or between the tiles, or there are none', () => {
    const g = grid(8, 4, 100, 4, 500);
    expect(inBand(g.length, (i) => g[i], 0, 400)).toBeNull();
    expect(inBand(g.length, (i) => g[i], 800, 900)).toBeNull();
    expect(inBand(g.length, (i) => g[i], 600.5, 603.5)).toBeNull(); // in the gap
    expect(inBand(0, (i) => g[i], 0, 1000)).toBeNull();
  });
  it('rows of different heights (own shapes) work the same', () => {
    const rows: Rect[] = [
      { left: 0, top: 0, width: 200, height: 150 },
      { left: 203, top: 0, width: 160, height: 150 },
      { left: 0, top: 153, width: 363, height: 90 },
      { left: 0, top: 246, width: 120, height: 200 },
      { left: 123, top: 246, width: 120, height: 200 },
    ];
    expect(inBand(rows.length, (i) => rows[i], 200, 250)).toEqual([2, 4]);
  });
  it('boxes meet when they overlap, not when they only touch', () => {
    const view = { left: 0, top: 0, width: 400, height: 800 };
    expect(meets({ left: 390, top: 790, width: 50, height: 50 }, view)).toBe(true);
    expect(meets({ left: 400, top: 10, width: 50, height: 50 }, view)).toBe(false);
    expect(meets({ left: 10, top: -50, width: 50, height: 50 }, view)).toBe(false);
  });
});

describe('the picture under the fingers', () => {
  it('is the box under the point, else the nearest one', () => {
    const g = grid(8, 4, 100, 4);
    expect(nearest(g, 150, 150)).toBe(5);
    expect(nearest(g, 101, 50)).toBe(0); // in a gap: the closer side
    expect(nearest(g, 1000, 120)).toBe(7); // right of the grid
    expect(nearest([], 0, 0)).toBe(-1);
  });
  it('stays at the same height: the same part of it comes back under the point', () => {
    const from = { left: 0, top: 300, width: 160, height: 160 }; // drawn bigger during the pinch
    const to = { left: 0, top: 500, width: 180, height: 180 }; // where it is laid out now
    const d = keepAt(from, to, 340); // a quarter down the tile
    expect(d).toBe(500 + 45 - 340);
    expect(to.top - d + 0.25 * to.height).toBe(340);
  });
  it('a point beside the tile keeps its nearest edge', () => {
    const from = { left: 0, top: 300, width: 100, height: 100 };
    const to = { left: 0, top: 200, width: 120, height: 120 };
    expect(keepAt(from, to, 250)).toBe(200 - 250); // above: its top comes to y
    expect(keepAt(from, to, 450)).toBe(320 - 450); // below: its bottom
  });
});

describe('the invert step', () => {
  it('draws the tile where it was', () => {
    const from = { left: 10, top: 20, width: 50, height: 60 };
    const to = { left: 110, top: 220, width: 100, height: 120 };
    expect(invert(from, to)).toBe('translate(-100px, -200px) scale(0.5, 0.5)');
    expect(invert(to, { ...to, width: 0, height: 0 })).toBe('translate(0px, 0px) scale(1, 1)');
  });
  it('a tile that does not move needs no animation', () => {
    const r = { left: 10, top: 20, width: 50, height: 60 };
    expect(still(r, { ...r, left: 10.3 })).toBe(true);
    expect(still(r, { ...r, width: 51 })).toBe(false);
  });
});
