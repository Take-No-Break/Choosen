import { describe, expect, it } from 'vitest';
import { Spring, Tracker, release, lean, throwVelocity } from '../src/lib/physics';
import { Outbox, type Rating } from '../src/lib/outbox';
import { commandFor } from '../src/lib/keys';

describe('throw decision', () => {
  const W = 1000, H = 800;
  it('far enough or fast enough throws, a small slow move springs back', () => {
    expect(release(300, 10, 0, 0, W, H)).toBe('like');
    expect(release(-300, 10, 0, 0, W, H)).toBe('pass');
    expect(release(10, -200, 0, 0, W, H)).toBe('super');
    expect(release(10, 250, 0, 0, W, H)).toBe('archive');
    expect(release(60, 0, 1.2, 0, W, H)).toBe('like'); // a flick
    expect(release(60, 0, 0.1, 0, W, H)).toBeNull();
    expect(release(5, 5, 2, 0, W, H)).toBeNull(); // a twitch at the start is not a throw
  });
  it('a short quick swipe throws (no gauge to fill)', () => {
    expect(release(40, 3, 0.5, 0, 384, 700)).toBe('like'); // a thumb flick on a phone
    expect(release(-40, 3, -0.5, 0, 384, 700)).toBe('pass');
    expect(release(70, 0, 0, 0, 384, 700)).toBe('like'); // or a short pull, without speed
    expect(release(40, 0, 0.1, 0, 384, 700)).toBeNull(); // short and slow: back to the middle
  });
  it('the lean shows the verdict and how close it is', () => {
    expect(lean(80, 0, W, H)).toEqual({ verdict: 'like', amount: 0.5 });
    expect(lean(0, -400, W, H).verdict).toBe('super');
    expect(lean(0, 0, W, H).verdict).toBeNull();
  });
  it('a thrown card always leaves fast enough, in its verdict direction', () => {
    expect(throwVelocity('like', 0, 0).vx).toBeGreaterThanOrEqual(2.4);
    expect(throwVelocity('pass', 0, 0).vx).toBeLessThanOrEqual(-2.4);
    expect(throwVelocity('super', 0, 0).vy).toBeLessThanOrEqual(-2.4);
    expect(throwVelocity('archive', 0, 0).vy).toBeGreaterThan(0);
  });
  it('release velocity comes from the last 80 ms', () => {
    const t = new Tracker();
    t.add(0, 0, 0);
    t.add(10, 0, 100);
    t.add(90, 0, 140);
    expect(t.velocity(140).vx).toBeCloseTo(2, 5); // 80 px in 40 ms
  });
});

describe('spring', () => {
  const settle = (fps: number) => {
    const s = new Spring();
    s.x = 300;
    let t = 0;
    while (s.step(1000 / fps) && t < 5000) t += 1000 / fps;
    return t;
  };
  it('settles back to 0 in about the same time at 60, 120 and 144 Hz', () => {
    const a = settle(60), b = settle(120), c = settle(144);
    expect(a).toBeLessThan(1500);
    expect(Math.abs(a - b)).toBeLessThan(120);
    expect(Math.abs(a - c)).toBeLessThan(120);
  });
});

describe('outbox', () => {
  const r = (n: number): Rating => ({ id: n, fp: '0123456789abcdef', a: 'L', cid: 'c' + n, replace: '' });
  const memory = () => {
    let q: Rating[] = [];
    return { get: () => q, set: (x: Rating[]) => (q = [...x]) };
  };
  it('sends in order, one at a time, and empties the stored queue', async () => {
    const sent: number[] = [];
    const store = memory();
    const box = new Outbox(async (x) => (sent.push(x.id), { item: { p: x.id } }), store, 1);
    const done = await Promise.all([box.push(r(1)), box.push(r(2)), box.push(r(3))]);
    expect(sent).toEqual([1, 2, 3]);
    expect(done.map((d) => d?.item?.p)).toEqual([1, 2, 3]);
    expect(store.get()).toEqual([]);
  });
  it('quick corrections each replace the one before, also across a failed send', async () => {
    // the full view: S in place of the old rating, then L in place of that S, before either is saved
    let fail = 1;
    const sent: string[] = [];
    const box = new Outbox(async (x) => {
      if (fail-- > 0) throw new Error('offline');
      sent.push(x.a + ':' + x.cid + '<' + x.replace);
      return {};
    }, memory(), 1);
    const s = { ...r(9), a: 'S', cid: 'cS', replace: 'tick-12' };
    const l2 = { ...r(9), a: 'L', cid: 'cL', replace: s.cid };
    await Promise.all([box.push(s), box.push(l2)]);
    expect(sent).toEqual(['S:cS<tick-12', 'L:cL<cS']);
  });
  it("names a picture's ratings on their way, also after the screen was left", async () => {
    let release = () => {};
    const box = new Outbox(() => new Promise((res) => (release = () => res({}))), memory(), 1);
    const changes: number[] = [];
    const stop = box.watch(() => changes.push(box.pendingFor('fpA').length));
    const s = { ...r(1), fp: 'fpA', a: 'S', cid: 'cS', replace: 'tick-3' };
    const done = box.push(s);
    // the full view was closed and opened again: it asks the outbox, not its own memory
    expect(box.pendingFor('fpA')).toHaveLength(1);
    expect(box.pendingFor('fpB')).toHaveLength(0);
    release();
    await done;
    expect(changes).toEqual([1, 0]);
    stop();
  });
  it('keeps a rating that failed and sends it again', async () => {
    let fail = 2;
    const sent: number[] = [];
    const box = new Outbox(async (x) => {
      if (fail-- > 0) throw new Error('offline');
      sent.push(x.id);
      return {};
    }, memory(), 1);
    await box.push(r(7));
    expect(sent).toEqual([7]);
  });
  it('names the cards whose rating is not saved yet, until it is', async () => {
    let release = () => {};
    const box = new Outbox(() => new Promise((res) => (release = () => res({}))), memory(), 1);
    const done = Promise.all([box.push(r(4)), box.push(r(5))]);
    expect(box.pendingIds).toEqual([4, 5]);
    release();
    await new Promise((res) => setTimeout(res, 0));
    expect(box.pendingIds).toEqual([5]);
    release();
    await done;
    expect(box.pendingIds).toEqual([]);
  });
  it('a new window sends what an earlier one left behind first', async () => {
    const store = memory();
    store.set([r(1)]);
    const sent: number[] = [];
    const box = new Outbox(async (x) => (sent.push(x.id), {}), store, 1);
    await box.push(r(2));
    expect(sent).toEqual([1, 2]);
  });
});

describe('keyboard layouts', () => {
  it('match Version 1 (0.7.4)', () => {
    expect(commandFor(1, 'ArrowUp')).toBe('super');
    expect(commandFor(1, 'Enter')).toBe('archive');
    expect(commandFor(2, 'ArrowUp')).toBe('archive');
    expect(commandFor(2, 'ArrowRight')).toBe('likeHold');
    expect(commandFor(3, 'KeyE')).toBe('archive');
    expect(commandFor(4, 'ArrowLeft')).toBe('pass'); // an own layout falls back to B
  });
});
