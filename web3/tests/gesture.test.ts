import { describe, expect, it } from 'vitest';
import { Taps, axisOf, scrollRelease, pairOf, pinchZoom, NO_ZOOM, pinchCount, pinchScale } from '../src/lib/gesture';

describe('taps', () => {
  it('taps close in time and place count up: double, then triple', () => {
    const t = new Taps();
    expect(t.tap(100, 100, 0)).toBe(1);
    expect(t.tap(110, 105, 200)).toBe(2);
    expect(t.tap(105, 100, 420)).toBe(3);
  });
  it('a slow or far tap starts a new series, and so does a reset', () => {
    const t = new Taps();
    t.tap(100, 100, 0);
    expect(t.tap(100, 100, 500)).toBe(1); // too slow
    expect(t.tap(300, 100, 600)).toBe(1); // too far
    expect(t.tap(300, 100, 700)).toBe(2);
    t.reset();
    expect(t.tap(300, 100, 750)).toBe(1);
  });
  it('after a reset, two quick pairs are two double taps (the full view likes each)', () => {
    const t = new Taps();
    const seen: number[] = [];
    for (const ms of [0, 100, 350, 450]) {
      const n = t.tap(100, 100, ms);
      seen.push(n);
      if (n === 2) t.reset();
    }
    expect(seen).toEqual([1, 2, 1, 2]);
  });
});

describe('the scroll layout', () => {
  const H = 800;
  it('a drag picks its way once it is clear', () => {
    expect(axisOf(4, 6)).toBeNull();
    expect(axisOf(3, -30)).toBe('y');
    expect(axisOf(30, 12)).toBe('x');
  });
  it('up is next, down is back, a short slow drag snaps back', () => {
    expect(scrollRelease(-200, 0, H)).toBe('next');
    expect(scrollRelease(-40, -0.6, H)).toBe('next'); // a flick
    expect(scrollRelease(200, 0, H)).toBe('back');
    expect(scrollRelease(-40, -0.1, H)).toBeNull();
  });
});

describe('pinch zoom', () => {
  it('scales with the distance between the fingers, between 1 and 5', () => {
    const start = pairOf({ x: 100, y: 300 }, { x: 200, y: 300 });
    expect(start).toEqual({ d: 100, cx: 150, cy: 300 });
    expect(pinchZoom(NO_ZOOM, start, pairOf({ x: 50, y: 300 }, { x: 250, y: 300 })).s).toBe(2);
    expect(pinchZoom(NO_ZOOM, start, pairOf({ x: 140, y: 300 }, { x: 160, y: 300 })).s).toBe(1); // never smaller
    expect(pinchZoom(NO_ZOOM, start, pairOf({ x: 0, y: 0 }, { x: 900, y: 0 })).s).toBe(5);
  });
  it('the point between the fingers stays under them', () => {
    const start = pairOf({ x: 100, y: 300 }, { x: 200, y: 300 }); // midpoint 150, 300
    const z = pinchZoom(NO_ZOOM, start, pairOf({ x: 80, y: 340 }, { x: 280, y: 340 })); // 2x, midpoint 180, 340
    expect(z).toEqual({ s: 2, tx: 180 - 2 * 150, ty: 340 - 2 * 300 });
  });
  it('a second pinch goes on from the kept zoom', () => {
    const first = { s: 2, tx: -150, ty: -300 }; // picture point (150, 300) at (150, 300)
    const start = pairOf({ x: 100, y: 300 }, { x: 200, y: 300 });
    const z = pinchZoom(first, start, pairOf({ x: 50, y: 300 }, { x: 250, y: 300 }));
    expect(z.s).toBe(4);
    expect([z.tx + z.s * 150, z.ty + z.s * 300]).toEqual([150, 300]); // the same picture point stays put
  });
});

describe("play's pictures at a time by pinching", () => {
  it('spreading shows fewer, pinching more; a big pinch goes two steps', () => {
    expect(pinchCount(4, 1.3)).toBe(2);
    expect(pinchCount(4, 1.9)).toBe(1);
    expect(pinchCount(2, 1.3)).toBe(1);
    expect(pinchCount(1, 0.75)).toBe(2);
    expect(pinchCount(1, 0.5)).toBe(4);
    expect(pinchCount(2, 0.5)).toBe(4); // only one step is left
  });
  it('a small pinch, or none left that way, changes nothing', () => {
    expect(pinchCount(2, 1.2)).toBeNull();
    expect(pinchCount(2, 0.85)).toBeNull();
    expect(pinchCount(1, 2)).toBeNull();
    expect(pinchCount(4, 0.5)).toBeNull();
  });
  it('パズル gives way to a count, from the pictures on its page', () => {
    expect(pinchCount(3, 1.3, true)).toBe(2);
    expect(pinchCount(3, 0.75, true)).toBe(4);
    expect(pinchCount(1, 1.3, true)).toBe(1);
    expect(pinchCount(4, 0.75, true)).toBe(4);
    expect(pinchCount(3, 1.1, true)).toBeNull();
  });
  it('the set follows the fingers, with only a little give where no step is left', () => {
    expect(pinchScale(3, 2)).toBe(2);
    expect(pinchScale(3, 1)).toBe(1.06);
    expect(pinchScale(0.2, 4)).toBe(0.94);
    expect(pinchScale(0.2, 2)).toBe(0.5);
    expect(pinchScale(1.3, 1, true)).toBe(1.3);
  });
});
