import { describe, expect, it } from 'vitest';
import { summarize, weeks, shortDay, type Day } from '../src/lib/stats';

const day = (n: number, like = 0, pass = 0, extra: Partial<Day> = {}): Day => ({
  day: `2026-10-${String(n).padStart(2, '0')}`,
  like,
  super: 0,
  pass,
  hold: 0,
  archive: 0,
  ...extra,
});

describe('statistics', () => {
  it('sums the period, the best day and the like rate', () => {
    const s = summarize([day(1, 3, 1), day(2), day(3, 6, 2, { super: 2, archive: 1 })]);
    expect(s.total).toBe(4 + 11);
    expect(s.activeDays).toBe(2);
    expect(s.perActiveDay).toBe(8); // 7.5 rounded
    expect(s.best?.day).toBe('2026-10-03');
    expect(s.max).toBe(11);
    expect(s.likeRate).toBeCloseTo(11 / 14); // archive does not count
  });
  it('a streak ends today, or yesterday when today has no swipes yet', () => {
    expect(summarize([day(1, 1), day(2), day(3, 1), day(4, 1)]).streak).toBe(2);
    expect(summarize([day(1, 1), day(2, 1), day(3)]).streak).toBe(2);
    expect(summarize([day(1, 1), day(2), day(3)]).streak).toBe(0);
  });
  it('an empty period has no like rate and no best day', () => {
    const s = summarize([day(1), day(2)]);
    expect(s.likeRate).toBeNull();
    expect(s.best).toBeNull();
    expect(s.perActiveDay).toBe(0);
  });
  it('weeks end at the last day and keep every swipe', () => {
    const days = Array.from({ length: 10 }, (_, i) => day(i + 1, 1));
    const w = weeks(days);
    expect(w.map((x) => x.like)).toEqual([3, 7]);
    expect(w[1].day).toBe('2026-10-04');
  });
  it('short dates drop the year and leading zeros', () => {
    expect(shortDay('2026-01-05')).toBe('1/5');
  });
});
