import { describe, expect, it } from 'vitest';
import { slantPage, type Page, type SlantCell } from '../src/lib/puzzle';

// absolute corner points of a slanted cell (its clip-path is relative to its box)
const corners = (c: SlantCell) =>
  [...c.clip.matchAll(/(-?[\d.]+)px (-?[\d.]+)px/g)].map((m) => [c.x + Number(m[1]), c.y + Number(m[2])]);

// a wide picture on top, two below it: the shape the owner described
const page: Page = {
  start: 0,
  count: 3,
  cells: [
    { i: 0, x: 0, y: 0, w: 384, h: 300 },
    { i: 1, x: 0, y: 303, w: 190.5, h: 297 },
    { i: 2, x: 193.5, y: 303, w: 190.5, h: 297 },
  ],
};

describe('slanted puzzle pages', () => {
  const [top, left, right] = slantPage(page, 3) as SlantCell[];

  it('gives every cell a four-cornered shape', () => {
    for (const c of [top, left, right]) expect(corners(c)).toHaveLength(4);
  });

  it('keeps the page sides straight', () => {
    const [tl, tr] = corners(top);
    expect(tl).toEqual([0, 0]);
    expect(tr).toEqual([384, 0]);
    expect(corners(left)[0][0]).toBe(0);
    expect(corners(left)[3]).toEqual([0, 600]);
    expect(corners(right)[2]).toEqual([384, 600]);
  });

  it('tilts the cuts', () => {
    const [, , br, bl] = corners(top);
    expect(Math.abs(br[1] - bl[1])).toBeGreaterThan(5); // the cut under the top picture is not level
    const [, tr, rb] = corners(left);
    expect(Math.abs(tr[0] - rb[0])).toBeGreaterThan(5); // the cut between the two below leans
  });

  it('lets neighbours meet along the same cut, one gap apart', () => {
    // under the top picture: its bottom corners and the tops of the two below lie on one line
    const [, , topBR, topBL] = corners(top);
    const slope = (topBR[1] - topBL[1]) / (topBR[0] - topBL[0]);
    const yTop = (x: number) => topBL[1] + slope * (x - topBL[0]);
    for (const p of [corners(left)[0], corners(left)[1], corners(right)[0], corners(right)[1]])
      expect(p[1] - yTop(p[0])).toBeCloseTo(3, 0);
    // between the two below: left's right side and right's left side are parallel, one gap apart
    const [, lTR, lBR] = corners(left);
    const [rTL, , , rBL] = corners(right);
    expect(rTL[0] - lTR[0]).toBeCloseTo(3, 0);
    expect(rBL[0] - lBR[0]).toBeCloseTo(3, 0);
  });

  it('leaves a single picture as it is', () => {
    const one: Page = { start: 0, count: 1, cells: [{ i: 0, x: 0, y: 0, w: 384, h: 600 }] };
    expect(slantPage(one, 3)).toEqual(one.cells);
  });
});
