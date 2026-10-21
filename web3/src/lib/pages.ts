// A work of several pages (<id>_p0, _p1, ... or x1, x2, ... in one folder). Grouping on, it is one card;
// Card.pages lists its pages, the first first, and a rating goes to the card (the core rates every
// page of the work). Grouping off, every page is its own card (perPage) that can still turn through
// the work's pages, starting at its own (`at`); a rating goes to the page on screen. The page can be
// turned with Shift on a PC, a tap on a phone, or the page badge.
import type { Card } from './api';

export type Page = NonNullable<Card['pages']>[number];
export interface Flipped {
  fp: string; // the card whose page was turned ('' = none)
  k: number; // its page on screen
}
export const NO_PAGE: Flipped = { fp: '', k: 0 };
/** A tap on the picture turns the page that way: its right half to the next page, its left half back. */
export const tapSide = (x: number, left: number, width: number): 1 | -1 => (x - left < width / 2 ? -1 : 1);

/** The pages of a work, or null for a single picture. */
export const pagesOf = (c: Pick<Card, 'pages'> | null | undefined): Page[] | null =>
  c?.pages && c.pages.length > 1 ? c.pages : null;

/** What is on screen at page k: the card itself (its own page, or a single picture) or that page. */
export function shownOf<C extends Card>(c: C, k: number): C | (Page & Omit<C, 'id' | 'fp' | 'name'>) {
  const ps = pagesOf(c);
  return ps && k !== (c.at ?? 0) ? { ...c, ...ps[Math.min(k, ps.length - 1)] } : c;
}

/** What a rating goes to: the card, or (grouping off) the page on screen, as a card of its own. */
export function ratedOf(c: Card, k: number): Card {
  const ps = pagesOf(c);
  return c.perPage && ps && k !== (c.at ?? 0) ? { ...c, ...ps[Math.min(k, ps.length - 1)], at: k } : c;
}

/** One page on (d = 1) or back (d = -1), coming round past either end. */
export const stepPage = (k: number, n: number, d: number) => (n > 1 ? (((k + d) % n) + n) % n : 0);

/** Card c's page on screen, from the one turned place: any other card shows its own page. */
export const pageOf = (c: Card | null | undefined, at: Flipped) =>
  c && pagesOf(c) ? (at.fp === c.fp ? at.k : (c.at ?? 0)) : 0;
