import { describe, expect, it } from 'vitest';
import { drawnRect } from '../src/lib/feather';

describe('soft picture edges: where the picture is drawn', () => {
  it('a tall picture shown whole in a wide box is centred with room at the sides', () => {
    const r = drawnRect(600, 1200, 400, 400, false);
    expect([r.x, r.y, r.w, r.h]).toEqual([100, 0, 200, 400]);
  });
  it('a filled picture fills the box (the edge fades at the box)', () => {
    const r = drawnRect(600, 1200, 400, 400, true);
    expect([r.x, r.y, r.w, r.h]).toEqual([0, 0, 400, 400]);
  });
  it('the soft edge is a few percent of the picture, at least 8 px', () => {
    expect(drawnRect(1000, 1000, 1000, 1000, false).f).toBe(35);
    expect(drawnRect(100, 100, 100, 100, false).f).toBe(8);
  });
});
