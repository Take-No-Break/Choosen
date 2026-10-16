import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { Handover } from '../src/lib/handover';

const cells = (...keys: number[]) => keys.map((key) => ({ key }));

describe("play's handover between sets of cells", () => {
  beforeEach(() => vi.useFakeTimers());
  afterEach(() => vi.useRealTimers());

  it('keeps the outgoing cells until every incoming one is ready, then lets them go after the fade', () => {
    const h = new Handover<{ key: number }>(() => {}, 650);
    h.letGo(cells(1, 2));
    const next = cells(3, 4);
    h.markReady(3, next);
    vi.advanceTimersByTime(2000);
    expect(h.leaving.map((c) => c.key)).toEqual([1, 2]); // 4 is not ready yet
    expect(h.leaving.every((c) => !c.gone)).toBe(true);
    h.markReady(4, next);
    expect(h.leaving.every((c) => c.gone)).toBe(true);
    vi.advanceTimersByTime(600);
    expect(h.leaving).toHaveLength(2); // still fading in
    vi.advanceTimersByTime(100);
    expect(h.leaving).toEqual([]);
  });

  it('a turn during the fade keeps everything outgoing until the newest set is ready', () => {
    const h = new Handover<{ key: number }>(() => {}, 650);
    h.letGo(cells(1, 2)); // A -> B
    const b = cells(3, 4);
    h.markReady(3, b);
    h.markReady(4, b); // B ready: A would go in 650 ms
    vi.advanceTimersByTime(300);
    h.letGo(b); // B -> C before that
    expect(h.leaving.map((x) => !!x.gone)).toEqual([true, true, false, false]);
    const c = cells(5, 6);
    vi.advanceTimersByTime(2000); // C's pictures are slow
    expect(h.leaving.map((x) => x.key)).toEqual([1, 2, 3, 4]); // nothing taken from under C
    h.markReady(5, c);
    h.markReady(6, c);
    vi.advanceTimersByTime(700);
    expect(h.leaving).toEqual([]);
  });

  it('keeps each outgoing cell at its own place', () => {
    const h = new Handover<{ key: number }>(() => {}, 650);
    h.letGo(cells(7), [1]);
    expect(h.leaving).toEqual([{ key: 7, pos: 1 }]);
  });

  it('fades only the replaced position after its replacement is ready', () => {
    const h = new Handover<{ key: number }>(() => {}, 650);
    h.markReady(10, cells(10, 11));
    h.markReady(11, cells(10, 11));
    h.letGo(cells(11), [1]);
    const next = cells(10, 12);
    expect(h.leaving).toEqual([{ key: 11, pos: 1 }]);
    h.markReady(12, next);
    expect(h.leaving).toEqual([{ key: 11, pos: 1, gone: true }]);
    vi.advanceTimersByTime(600);
    expect(h.leaving).toHaveLength(1);
    vi.advanceTimersByTime(50);
    expect(h.leaving).toEqual([]);
  });
});
