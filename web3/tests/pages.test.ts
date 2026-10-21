import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { NO_PAGE, pageOf, pagesOf, shownOf, stepPage } from '../src/lib/pages';
import { PageTaps } from '../src/lib/taps';
import type { Card } from '../src/lib/api';

const work = { id: 1, fp: 'aaaa', name: 'w_p5', dir: 'A', pages: [
  { id: 1, fp: 'aaaa', name: 'w_p5' }, { id: 2, fp: 'bbbb', name: 'w_p6' }, { id: 3, fp: 'cccc', name: 'w_p7' },
] } as unknown as Card;
const single = { id: 9, fp: 'zzzz', name: 'one', dir: 'B' } as unknown as Card;

describe('the pages of a work', () => {
  it('only a work of several pages has pages', () => {
    expect(pagesOf(work)?.length).toBe(3);
    expect(pagesOf(single)).toBeNull();
    expect(pagesOf({ ...single, pages: [{ id: 9, fp: 'zzzz', name: 'one' }] } as unknown as Card)).toBeNull();
  });
  it('shows the card on its first page and that page further on, keeping the card for the rest', () => {
    expect(shownOf(work, 0)).toBe(work);
    const p = shownOf(work, 2);
    expect([p.id, p.fp, p.name, p.dir]).toEqual([3, 'cccc', 'w_p7', 'A']);
    expect(shownOf(work, 99).name).toBe('w_p7');
    expect(shownOf(single, 3)).toBe(single);
  });
  it('turns on and back, coming round at both ends (a work starting at page 5 goes 6, 7, then 5)', () => {
    expect([stepPage(0, 3, 1), stepPage(1, 3, 1), stepPage(2, 3, 1)]).toEqual([1, 2, 0]);
    expect(stepPage(0, 3, -1)).toBe(2);
    expect(stepPage(0, 1, 1)).toBe(0);
  });
  it('the turned page belongs to its card only', () => {
    expect(pageOf(work, { fp: 'aaaa', k: 2 })).toBe(2);
    expect(pageOf(work, NO_PAGE)).toBe(0);
    expect(pageOf(single, { fp: 'zzzz', k: 2 })).toBe(0);
    expect(pageOf({ ...work, fp: 'other' } as Card, { fp: 'aaaa', k: 2 })).toBe(0);
  });
});

describe('a lone tap turns the page', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());
  it('after the wait for a second tap, unless cancelled', () => {
    const p = new PageTaps(300, 700);
    let turns = 0;
    p.later(() => (turns++, true));
    vi.advanceTimersByTime(299);
    expect(turns).toBe(0);
    vi.advanceTimersByTime(1);
    expect(turns).toBe(1);
    p.later(() => (turns++, true));
    p.cancel();
    vi.advanceTimersByTime(1000);
    expect(turns).toBe(1);
  });
  it('another work ends the rhythm: its double tap likes again', () => {
    const p = new PageTaps(300, 700);
    p.turned(1000);
    p.cancel(); // a touch on the same work keeps the rhythm
    expect(p.turning(1200)).toBe(true);
    p.reset(); // another work on screen
    expect(p.turning(1300)).toBe(false);
  });
  it('while turning, a tap turns at once (it never counts towards a double tap)', () => {
    const p = new PageTaps(300, 700);
    expect(p.turning(1000)).toBe(false);
    p.turned(1000);
    expect(p.turning(1500)).toBe(true);
    expect(p.turning(2100)).toBe(true); // each turn keeps the rhythm going
    expect(p.turning(2900)).toBe(false);
  });
});
