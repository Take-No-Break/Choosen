import { describe, expect, it } from 'vitest';
import { storyTiming } from '../src/lib/story-timing';

describe('selected picture display time', () => {
  it('does not start the first staggered change before five seconds', () => {
    const t = storyTiming(5, true);
    expect(t.minimum).toBe(5000);
    expect(t.spread).toBeGreaterThan(0);
    expect(t.minimum + t.spread).toBeLessThanOrEqual(t.round);
  });
  it('keeps every later picture for the selected minimum even with opposite random phases', () => {
    for (const seconds of [3, 5, 8, 12]) {
      const t = storyTiming(seconds, true);
      const latestPreviousChange = t.minimum + t.spread;
      const earliestNextChange = t.round + t.minimum;
      expect(earliestNextChange - latestPreviousChange).toBeGreaterThanOrEqual(seconds * 1000);
    }
  });
  it('keeps single-photo and puzzle rounds at the selected duration', () => {
    expect(storyTiming(5, false)).toEqual({ minimum: 5000, spread: 0, round: 5000 });
  });
});
