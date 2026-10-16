import { describe, expect, it } from 'vitest';
import { roundClip } from '../src/lib/puzzle';

describe('rounded パズル corners', () => {
  it('rounds every corner of a slanted shape, cut back along both edges', () => {
    const p = roundClip('polygon(0px 0px, 100px 10px, 100px 100px, 0px 90px)', 10)!;
    expect(p.startsWith("path('M ")).toBe(true);
    expect(p.endsWith("Z')")).toBe(true);
    expect(p.match(/Q /g)?.length).toBe(4);
    // the first corner (0,0): from 10 px down its left edge, through the corner, to 10 px along the top
    expect(p).toContain('M 0 10 Q 0 0 10 1');
  });
  it('never cuts more than half of a short edge', () => {
    const p = roundClip('polygon(0px 0px, 6px 0px, 6px 100px, 0px 100px)', 10)!;
    expect(p).toContain('Q 0 0 3 0');
  });
  it('leaves missing or unknown clips, and a zero radius, alone', () => {
    expect(roundClip(undefined, 10)).toBeUndefined();
    expect(roundClip('inset(0px)', 10)).toBe('inset(0px)');
    expect(roundClip('polygon(0px 0px, 10px 0px, 10px 10px)', 0)).toBe('polygon(0px 0px, 10px 0px, 10px 10px)');
  });
});
