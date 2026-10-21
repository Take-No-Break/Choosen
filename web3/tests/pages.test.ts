import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { NO_PAGE, pageOf, pagesOf, ratedOf, shownOf, stepPage, tapSide } from '../src/lib/pages';
import { PageKeyHold, PageTaps } from '../src/lib/taps';
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

describe('a held page key turns page after page', () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());
  // a work of 5 pages; turn(first) as the screens do: a held key stops at the last page
  const run = (start: number) => {
    let k = start;
    const turn = (first: boolean) => {
      if (!first && k + 1 >= 5) return false;
      k = stepPage(k, 5, 1);
      return true;
    };
    return { turn, at: () => k };
  };

  it('turns once at once, then every 100 ms after 300 ms, until the key goes up', () => {
    const h = new PageKeyHold(), w = run(0);
    h.press('ShiftLeft', w.turn);
    expect(w.at()).toBe(1);
    vi.advanceTimersByTime(299);
    expect(w.at()).toBe(1);
    vi.advanceTimersByTime(1);
    expect(w.at()).toBe(2);
    vi.advanceTimersByTime(100);
    expect(w.at()).toBe(3);
    h.release('ShiftRight'); // another key: still held
    h.release('ShiftLeft');
    vi.advanceTimersByTime(1000);
    expect(w.at()).toBe(3);
  });
  it('stops at the last page; a single press still goes round to the first', () => {
    const h = new PageKeyHold(), w = run(0);
    h.press('ShiftLeft', w.turn);
    vi.advanceTimersByTime(5000);
    expect(w.at()).toBe(4);
    h.release('ShiftLeft');
    h.press('ShiftLeft', w.turn);
    expect(w.at()).toBe(0);
  });
});

describe('a tap turns the page by the half it lands on', () => {
  it('right half: the next page; left half: back', () => {
    expect(tapSide(300, 0, 400)).toBe(1);
    expect(tapSide(100, 0, 400)).toBe(-1);
    expect(tapSide(250, 100, 400)).toBe(-1); // measured from the picture's own left edge
    expect(tapSide(300, 100, 400)).toBe(1);
  });
});

describe('grouping off: each page is its own card and can still turn', () => {
  const page2 = {
    ...work, id: 2, fp: 'bbbb', name: 'w_p6', perPage: true, at: 1, rated: true, p: 67, n: 1, last: 'L',
    pages: [
      { id: 1, fp: 'aaaa', name: 'w_p5', rated: false, p: 0, n: 0, last: '' },
      { id: 2, fp: 'bbbb', name: 'w_p6', rated: true, p: 67, n: 1, last: 'L' },
      { id: 3, fp: 'cccc', name: 'w_p7', rated: false, p: 0, n: 0, last: '' },
    ],
  } as unknown as Card;
  it('starts on its own page', () => {
    expect(pageOf(page2, NO_PAGE)).toBe(1);
    expect(shownOf(page2, 1)).toBe(page2);
    expect(shownOf(page2, 0).fp).toBe('aaaa');
  });
  it('a rating goes to the page on screen, with that page state', () => {
    expect(ratedOf(page2, 1)).toBe(page2);
    const r = ratedOf(page2, 2);
    expect([r.id, r.fp, r.rated, r.last, r.at]).toEqual([3, 'cccc', false, '', 2]);
  });
  it('grouping on, the card itself is rated whatever page is on screen', () => {
    expect(ratedOf(work, 2)).toBe(work);
  });
});
