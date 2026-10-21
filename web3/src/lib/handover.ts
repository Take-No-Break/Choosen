// Play's handover from one set of cells to the next (Story.svelte): the outgoing cells stay, at their
// places, until every incoming cell has its pictures in; then they go once the new ones have faded in.
// A new turn while one is under way keeps everything outgoing until the newest set is ready.

export interface Keyed {
  key: number;
}

export class Handover<T extends Keyed> {
  leaving: (T & { pos: number; gone?: boolean })[] = [];
  private ready = new Set<number>();
  private timer: ReturnType<typeof setTimeout> | undefined;

  constructor(
    private onChange: (leaving: (T & { pos: number; gone?: boolean })[]) => void,
    private fadeMs = 650, // the incoming cells' fade, and a little more
  ) {}

  /** These cells are being replaced; pos: each one's place (default: its index). */
  letGo(old: T[], pos?: number[]) {
    clearTimeout(this.timer); // an earlier turn's cleanup must not take cells the newest still needs
    this.timer = undefined;
    if (!old.length) return;
    this.leaving = [...this.leaving, ...old.map((s, k) => ({ ...s, pos: pos?.[k] ?? k }))];
    this.onChange(this.leaving);
  }

  /** A cell has all its pictures; when every current cell has, the outgoing ones go after the fade. */
  markReady(key: number, current: T[]) {
    this.ready.add(key);
    if (!this.leaving.length || !current.every((s) => this.ready.has(s.key))) return;
    clearTimeout(this.timer);
    // Transparent contain-mode cells cannot cover the previous picture. Fade it explicitly,
    // but only after the replacement is ready. Preserve an earlier generation's fading state.
    this.leaving = this.leaving.map((s) => ({ ...s, gone: true }));
    this.onChange(this.leaving);
    this.timer = setTimeout(() => {
      this.timer = undefined;
      this.leaving = [];
      this.onChange(this.leaving);
    }, this.fadeMs);
  }

  /** Whether a cell has had all its pictures (it was shown). */
  isReady(key: number) {
    return this.ready.has(key);
  }

  /** A cell left the page. */
  forget(key: number) {
    this.ready.delete(key);
  }
}
