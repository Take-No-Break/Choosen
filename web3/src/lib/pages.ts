// A work of several pages (グループ on: <id>_p0, _p1, ... in one folder) is one card; Card.pages lists
// them, its first page first. The page on screen can be turned (Shift on a PC, a tap on a phone, or
// the page badge); a rating always goes to the card itself (the core rates every page of the work,
// and the outbox and exclude lists match by the card).
import type { Card } from './api';

export type Page = NonNullable<Card['pages']>[number];
export interface Flipped {
  fp: string; // the card whose page was turned ('' = none)
  k: number; // its page on screen
}
export const NO_PAGE: Flipped = { fp: '', k: 0 };

/** The pages of a work, or null for a single picture. */
export const pagesOf = (c: Pick<Card, 'pages'> | null | undefined): Page[] | null =>
  c?.pages && c.pages.length > 1 ? c.pages : null;

/** What is on screen at page k: the card itself (page 0, or a single picture) or that page. */
export function shownOf<C extends Card>(c: C, k: number): C | (Page & Omit<C, 'id' | 'fp' | 'name'>) {
  const ps = pagesOf(c);
  return ps && k > 0 ? { ...c, ...ps[Math.min(k, ps.length - 1)] } : c;
}

/** One page on (d = 1) or back (d = -1), coming round past either end. */
export const stepPage = (k: number, n: number, d: number) => (n > 1 ? (((k + d) % n) + n) % n : 0);

/** Card c's page on screen, from the one turned place: any other card shows its first page. */
export const pageOf = (c: Card | null | undefined, at: Flipped) => (c && at.fp === c.fp && pagesOf(c) ? at.k : 0);
