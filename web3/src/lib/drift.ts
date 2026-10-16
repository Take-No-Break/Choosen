// The slide view's autoplay (▶ in スクロール): the strip flows left by itself at a steady speed. A
// finger on it, or a scroll that was not the flow's own (a swipe and its momentum, the wheel, a key),
// pauses the flow until RESUME_MS after; then it goes on from wherever the strip is. Pure: Story.svelte
// feeds it the time and the strip's scroll position every frame and scrolls to what it returns.

export const RESUME_MS = 800;
const SLACK = 1.5; // px: the browser rounds a scroll position it is given

export class Drift {
  private pos = NaN; // where the flow has the strip, in fractional px (the browser may round it)
  private last = NaN; // the previous frame's time
  private wait = -Infinity; // paused until then
  /** The flow is at the strip's far end. */
  ended = false;

  /** Whether the strip at `left` is where the flow put it (so a scroll event is the flow's own). */
  owns(left: number): boolean {
    return Math.abs(left - this.pos) <= SLACK;
  }

  /**
   * One frame at `now` ms with the strip at `left` px (at most `max`), flowing `speed` px a second
   * unless `held`. Returns the position to scroll to, or null to leave the strip alone.
   */
  frame(now: number, left: number, max: number, speed: number, held: boolean): number | null {
    const dt = Number.isNaN(this.last) ? 0 : Math.min(100, Math.max(0, now - this.last)) / 1000; // a stall does not jump
    this.last = now;
    if (Number.isNaN(this.pos)) this.pos = left;
    else if (!this.owns(left)) this.wait = now + RESUME_MS; // someone else moved it
    if (held) this.wait = now + RESUME_MS;
    if (now < this.wait) {
      this.pos = left; // it goes on from wherever the strip is then
      this.ended = left >= max - 0.5;
      return null;
    }
    const to = Math.min(max, this.pos + speed * dt);
    this.ended = to >= max - 0.5;
    if (to === this.pos) return null;
    this.pos = to;
    return to;
  }
}
