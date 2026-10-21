// What a series of taps means, once it is clear: one tap waits a moment in case a second comes; a
// second shows its ♥ at once but waits as well, in case a third (超好評) comes. Story.svelte feeds it
// the taps (pointer positions and times) that were neither a drag nor a scroll.
import { Taps } from './gesture';

export interface TapActions {
  /** A lone tap, once no second one came in time. */
  single(x: number, y: number): void;
  /** The second tap, at once (show the ♥; like() or triple() follows). */
  double(x: number, y: number): void;
  /** The double tap stands: no third came in time. */
  like(x: number, y: number): void;
  /** The third tap. Further taps of the same series do nothing. */
  triple(x: number, y: number): void;
}

export class TapSeries {
  private taps: Taps;
  private timer: ReturnType<typeof setTimeout> | undefined;
  private due: { n: number; f: () => void } | undefined; // what the waiting tap will do
  constructor(
    private on: TapActions,
    private wait = 300, // ms a tap waits for the next one, and the most between two taps of a series
    reach = 40, // px between two taps of a series
  ) {
    this.taps = new Taps(wait, reach);
  }

  tap(x: number, y: number, t: number) {
    const n = this.taps.tap(x, y, t);
    clearTimeout(this.timer);
    this.timer = undefined;
    this.due = undefined;
    if (n === 1) this.later(1, () => this.on.single(x, y));
    else if (n === 2) {
      this.on.double(x, y);
      this.later(2, () => this.on.like(x, y));
    } else if (n === 3) this.on.triple(x, y);
  }

  /** Two fingers came down: a waiting lone tap is dropped; a waiting double tap stands now (its ♥ shows). */
  interrupt() {
    const d = this.due;
    this.cancel();
    if (d?.n === 2) d.f();
  }

  /** Drops a waiting tap (the view closes). */
  cancel() {
    clearTimeout(this.timer);
    this.timer = undefined;
    this.due = undefined;
    this.taps.reset();
  }

  private later(n: number, f: () => void) {
    this.due = { n, f };
    this.timer = setTimeout(() => this.settle(f), this.wait);
  }

  // the series is over: a late tap starts a new one instead of counting on
  private settle(f: () => void) {
    this.timer = undefined;
    this.due = undefined;
    this.taps.reset();
    f();
  }
}

/**
 * A lone tap on a work of several pages turns its page once no second tap came (a double tap stays
 * 好評). While pages are being turned (the last turn at most `rhythm` ms ago) a tap turns at once and
 * never counts towards a double or triple tap, so quick taps through a work never rate it.
 */
export class PageTaps {
  private timer: ReturnType<typeof setTimeout> | undefined;
  private lastTurn = -1e9;
  constructor(
    private wait = 300, // as TapSeries: how long a lone tap waits for a second one
    private rhythm = 700,
  ) {}
  /** A tap at `now` while turning pages: true = turn now (the caller turns and resets its taps). */
  turning(now: number): boolean {
    if (now - this.lastTurn > this.rhythm) return false;
    this.lastTurn = now;
    return true;
  }
  /** A lone tap: `turn` runs after the wait, unless cancelled; it returns whether it turned. */
  later(turn: () => boolean) {
    this.cancel();
    this.timer = setTimeout(() => {
      this.timer = undefined;
      if (turn()) this.lastTurn = performance.now();
    }, this.wait);
  }
  /** A turn by key or badge also keeps the rhythm going. */
  turned(now: number) {
    this.lastTurn = now;
  }
  /** Another touch on the same work: the waiting tap is dropped (the turning rhythm goes on). */
  cancel() {
    clearTimeout(this.timer);
    this.timer = undefined;
  }
  /** Another work on screen: nothing waits and the rhythm is over (its double tap likes again). */
  reset() {
    this.cancel();
    this.lastTurn = -1e9;
  }
}
