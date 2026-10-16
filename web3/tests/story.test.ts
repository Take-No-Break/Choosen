import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { TapSeries } from '../src/lib/taps';
import { Drift, RESUME_MS } from '../src/lib/drift';

describe('tap series', () => {
  let log: string[];
  let s: TapSeries;
  beforeEach(() => {
    vi.useFakeTimers();
    log = [];
    s = new TapSeries({
      single: (x) => log.push(`single ${x}`),
      double: (x) => log.push(`double ${x}`),
      like: (x) => log.push(`like ${x}`),
      triple: (x) => log.push(`triple ${x}`),
    });
  });
  afterEach(() => vi.useRealTimers());

  it('two fingers drop a waiting lone tap; a waiting double tap stands at once, nothing comes later', () => {
    s.tap(10, 10, 0);
    s.interrupt();
    vi.advanceTimersByTime(1000);
    expect(log).toEqual([]);
    s.tap(10, 10, 2000);
    s.tap(11, 10, 2100);
    s.interrupt();
    expect(log).toEqual(['double 11', 'like 11']);
    vi.advanceTimersByTime(1000);
    expect(log).toEqual(['double 11', 'like 11']);
    s.tap(10, 10, 2200); // a new series after the interrupt
    vi.advanceTimersByTime(300);
    expect(log).toEqual(['double 11', 'like 11', 'single 10']);
  });
  it('a lone tap acts once no second one came', () => {
    s.tap(10, 10, 0);
    vi.advanceTimersByTime(299);
    expect(log).toEqual([]);
    vi.advanceTimersByTime(1);
    expect(log).toEqual(['single 10']);
  });
  it('a double tap shows at once and stands when no third comes; no single', () => {
    s.tap(10, 10, 0);
    vi.advanceTimersByTime(150);
    s.tap(12, 10, 150);
    expect(log).toEqual(['double 12']);
    vi.advanceTimersByTime(300);
    expect(log).toEqual(['double 12', 'like 12']);
  });
  it('a triple tap replaces the like; a fourth does nothing', () => {
    s.tap(10, 10, 0);
    s.tap(10, 10, 120);
    s.tap(10, 10, 240);
    s.tap(10, 10, 360);
    vi.advanceTimersByTime(1000);
    expect(log).toEqual(['double 10', 'triple 10']);
  });
  it('taps far apart in place or time are singles of their own', () => {
    s.tap(10, 10, 0);
    s.tap(200, 10, 100); // too far: a new series, the first one is dropped
    vi.advanceTimersByTime(300);
    s.tap(200, 10, 700); // too late
    vi.advanceTimersByTime(300);
    expect(log).toEqual(['single 200', 'single 200']);
  });
  it('a tap after a settled series starts a new one', () => {
    s.tap(10, 10, 0);
    s.tap(10, 10, 100);
    vi.advanceTimersByTime(300); // the like stands
    s.tap(10, 10, 390); // close in time to the second tap, but the series is over
    vi.advanceTimersByTime(300);
    expect(log).toEqual(['double 10', 'like 10', 'single 10']);
  });
  it('cancel drops a waiting tap', () => {
    s.tap(10, 10, 0);
    s.cancel();
    vi.advanceTimersByTime(1000);
    expect(log).toEqual([]);
  });
});

describe('autoplay drift', () => {
  const MAX = 10_000;
  it('flows at its speed from where the strip is, keeping fractions the browser rounds away', () => {
    const d = new Drift();
    let left = 100;
    for (let t = 0; t <= 1000; t += 16) {
      const to = d.frame(t, left, MAX, 50, false);
      if (to !== null) left = Math.round(to); // the browser keeps whole pixels
    }
    expect(left).toBeGreaterThanOrEqual(149);
    expect(left).toBeLessThanOrEqual(151); // 50 px in a second, not lost to rounding
  });
  it('a finger pauses it while down and a moment after, then it goes on from where the strip is', () => {
    const d = new Drift();
    let left = 0;
    const run = (from: number, to: number, held: boolean) => {
      for (let t = from; t < to; t += 16) {
        const p = d.frame(t, left, MAX, 100, held);
        if (p !== null) left = p;
      }
    };
    run(0, 1000, false);
    const before = left; // about 100 px on
    expect(before).toBeGreaterThan(95);
    run(1000, 1500, true); // the finger is down
    expect(left).toBe(before);
    left += 60; // its swipe moved the strip
    run(1500, 1500 + RESUME_MS - 50, false);
    expect(left).toBe(before + 60); // still waiting
    run(1500 + RESUME_MS - 50, 1500 + RESUME_MS + 1000, false);
    expect(left).toBeGreaterThan(before + 60 + 80); // and on again from there
  });
  it('a scroll that was not its own pauses it', () => {
    const d = new Drift();
    d.frame(0, 0, MAX, 100, false);
    const to = d.frame(100, 0, MAX, 100, false)!;
    expect(d.owns(Math.round(to))).toBe(true);
    expect(d.frame(200, 300, MAX, 100, false)).toBeNull(); // the wheel moved it
    expect(d.owns(300)).toBe(true); // it follows the strip while waiting
    expect(d.frame(200 + RESUME_MS + 1, 300, MAX, 100, false)).toBeGreaterThan(300);
  });
  it('a long stall does not jump', () => {
    const d = new Drift();
    d.frame(0, 0, MAX, 100, false);
    expect(d.frame(5000, 0, MAX, 100, false)).toBeCloseTo(10, 5); // at most 100 ms of flow
  });
  it('stops at the far end and goes on when the strip grows', () => {
    const d = new Drift();
    d.frame(0, 990, 1000, 100, false);
    expect(d.frame(100, 990, 1000, 100, false)).toBe(1000);
    expect(d.ended).toBe(true);
    expect(d.frame(200, 1000, 1000, 100, false)).toBeNull();
    expect(d.ended).toBe(true);
    expect(d.frame(300, 1000, 2000, 100, false)).toBeCloseTo(1010, 5);
    expect(d.ended).toBe(false);
  });
});
