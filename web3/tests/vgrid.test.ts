import { describe, expect, it } from 'vitest';
import { squareGrid, shapesGrid } from '../src/lib/vgrid';
import { justify } from '../src/lib/justify';

describe('gallery grid for the whole list (virtual)', () => {
  it('square tiles: columns, places and the full height', () => {
    const g = squareGrid(10, 364, 150, 3);
    expect(g.at(0)).toEqual({ x: 0, y: 0, w: 180.5, h: 180.5 });
    expect(g.at(3)).toEqual({ x: 183.5, y: 183.5, w: 180.5, h: 180.5 });
    expect(g.height).toBe(5 * 183.5 - 3);
  });
  it('square tiles: the band gives whole rows, within the list', () => {
    const g = squareGrid(10, 364, 150, 3);
    expect(g.band(0, 100)).toEqual([0, 1]);
    expect(g.band(190, 400)).toEqual([2, 5]);
    expect(g.band(700, 5000)).toEqual([6, 9]);
    expect(g.band(5000, 6000)).toBeNull();
    expect(squareGrid(0, 364, 150, 3).band(0, 100)).toBeNull();
  });
  it('a very long list costs nothing to place (the scroll is as long as the list)', () => {
    const g = squareGrid(200000, 364, 150, 3);
    expect(g.height).toBeGreaterThan(1e7);
    expect(g.band(5e6, 5e6 + 800)?.[0]).toBe(Math.floor(5e6 / 183.5) * 2);
  });
  it('own shapes: the same sizes as justify(), with positions row by row', () => {
    const r = [0.7, 0.7, 0.7, 0.7, 1.6, 0.7, 1, 1];
    const g = shapesGrid(r, 364, 137, 3);
    const j = justify(r, 364, 137, 3);
    r.forEach((_, i) => expect([g.at(i).w, g.at(i).h]).toEqual([j[i].w, j[i].h]));
    expect(g.at(0).x).toBe(0);
    expect(g.at(1).x).toBeCloseTo(j[0].w + 3, 5);
    expect(g.at(4)).toMatchObject({ x: 0 }); // the fourth filled the first row
    expect(g.at(4).y).toBeCloseTo(j[0].h + 3, 5);
  });
  it('own shapes: the band finds the rows on screen', () => {
    const g = shapesGrid(new Array(1000).fill(1), 364, 120, 3);
    const b = g.band(1000, 1400)!;
    expect(g.at(b[0]).y + g.at(b[0]).h).toBeGreaterThan(1000);
    expect(g.at(b[1]).y).toBeLessThan(1400);
    expect(g.at(b[1] + 1).y).toBeGreaterThanOrEqual(1400);
  });
});
