// Ratings leave in order and are never lost: each is kept in localStorage until the server has
// confirmed it, and a retry is harmless because the server knows each rating's cid. V3 uses its
// own key, so Version 1's page (which adopts and clears "choosen.outbox.*") never touches it.

export interface Rating {
  id: number;
  fp: string;
  a: string; // L like, S super like, D pass, A archive
  cid: string;
  replace: string; // the cid of the rating this one corrects, or ""
}

export interface Saved {
  item?: { p: number };
  chain?: { name: string; depth: number; before: number; after: number; rank?: number; rankBefore?: number; artist?: boolean }[];
}

type Send = (r: Rating) => Promise<Saved>;
type Store = { get(): Rating[]; set(q: Rating[]): void };

const KEY = 'choosen3.outbox';

export const localStore: Store = {
  get() {
    try {
      return JSON.parse(localStorage.getItem(KEY) ?? '[]') as Rating[];
    } catch {
      return [];
    }
  },
  set(q) {
    try {
      localStorage.setItem(KEY, JSON.stringify(q));
    } catch {
      /* private mode: the queue lives in memory only */
    }
  },
};

export function newCid(): string {
  const a = new Uint8Array(12);
  crypto.getRandomValues(a);
  return Array.from(a, (b) => (b % 36).toString(36)).join('');
}

export class Outbox {
  private queue: Rating[];
  private busy = false;
  private waiters = new Map<string, (s: Saved | null) => void>();
  private listeners = new Set<() => void>();
  failures = 0;

  constructor(
    private send: Send,
    private store: Store = localStore,
    private retryMs = 2000,
  ) {
    this.queue = store.get(); // ratings a previous window could not send go first
  }

  get pending(): number {
    return this.queue.length;
  }

  /** A picture's ratings still on their way, oldest first. */
  pendingFor(fp: string): Rating[] {
    return this.queue.filter((r) => r.fp === fp);
  }

  /** Calls f whenever a rating is queued or saved; returns the way to stop. */
  watch(f: () => void): () => void {
    this.listeners.add(f);
    return () => void this.listeners.delete(f);
  }

  private changed() {
    for (const f of this.listeners) f();
  }

  /** Cards whose rating is not saved yet: the server still sees them as due, so they must not be asked for. */
  get pendingIds(): number[] {
    return this.queue.map((r) => r.id);
  }

  /** Queues a rating; resolves with the server's answer (null if it could not be saved yet). */
  push(r: Rating): Promise<Saved | null> {
    this.queue.push(r);
    this.store.set(this.queue);
    this.changed();
    const done = new Promise<Saved | null>((res) => this.waiters.set(r.cid, res));
    void this.pump();
    return done;
  }

  async pump(): Promise<void> {
    if (this.busy) return;
    this.busy = true;
    try {
      while (this.queue.length) {
        const r = this.queue[0];
        let saved: Saved;
        try {
          saved = await this.send(r);
        } catch {
          this.failures++;
          setTimeout(() => void this.pump(), this.retryMs);
          return;
        }
        this.failures = 0;
        this.queue.shift();
        this.store.set(this.queue);
        this.changed();
        this.waiters.get(r.cid)?.(saved);
        this.waiters.delete(r.cid);
      }
    } finally {
      this.busy = false;
    }
  }
}
