import { describe, expect, it } from 'vitest';
import { pinchCount, pinchScale } from '../src/lib/gesture';

describe('horizontal gallery count pinch', () => {
  const wide = [1, 2, 4, 6, 8, 10];
  it('uses all available landscape counts in both directions', () => {
    expect(pinchCount(4, .75, false, wide)).toBe(6);
    expect(pinchCount(6, .75, false, wide)).toBe(8);
    expect(pinchCount(8, .75, false, wide)).toBe(10);
    expect(pinchCount(10, 1.3, false, wide)).toBe(8);
    expect(pinchCount(8, 1.3, false, wide)).toBe(6);
    expect(pinchCount(6, 1.3, false, wide)).toBe(4);
  });
  it('clamps at the actual mode limits and ignores small movement', () => {
    expect(pinchCount(10, .5, false, wide)).toBeNull();
    expect(pinchCount(1, 2, false, wide)).toBeNull();
    expect(pinchCount(4, 1.1, false, wide)).toBeNull();
    expect(pinchScale(.2, 10, false, wide)).toBe(.94);
    expect(pinchScale(.2, 6, false, wide)).toBe(.5);
  });
  it('preserves the existing play/portrait steps', () => {
    expect(pinchCount(1, .75)).toBe(2);
    expect(pinchCount(2, .75)).toBe(4);
    expect(pinchCount(4, .75)).toBeNull();
    expect(pinchCount(4, 1.3)).toBe(2);
    expect(pinchCount(2, 1.3)).toBe(1);
  });
});
