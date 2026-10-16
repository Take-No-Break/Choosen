// Statistics from /api/stats: per-day swipe counts and a few summary numbers. Pure functions, so the
// screen only draws.
import { lang } from './i18n';

export interface Day {
  day: string; // YYYY-MM-DD, local time
  like: number;
  super: number;
  pass: number;
  hold: number; // only in old data (保留 was removed in 0.7.2)
  archive: number;
}

export const swipes = (d: Day) => d.like + d.super + d.pass + d.hold + d.archive;

export interface Summary {
  total: number; // swipes in the period
  activeDays: number;
  perActiveDay: number; // rounded
  best: Day | null; // the day with the most swipes
  max: number; // that day's swipes (the chart's scale)
  streak: number; // days in a row with swipes, ending today (or yesterday when today has none yet)
  likeRate: number | null; // (like + super) / (like + super + pass) in the period; null without ratings
}

export function summarize(days: Day[]): Summary {
  let total = 0, activeDays = 0, max = 0, liked = 0, judged = 0;
  let best: Day | null = null;
  for (const d of days) {
    const n = swipes(d);
    total += n;
    if (n > 0) activeDays++;
    if (n > max) (max = n), (best = d);
    liked += d.like + d.super;
    judged += d.like + d.super + d.pass;
  }
  let streak = 0;
  let i = days.length - 1;
  if (i >= 0 && swipes(days[i]) === 0) i--; // today may simply not have started
  for (; i >= 0 && swipes(days[i]) > 0; i--) streak++;
  return {
    total,
    activeDays,
    perActiveDay: activeDays ? Math.round(total / activeDays) : 0,
    best,
    max,
    streak,
    likeRate: judged ? liked / judged : null,
  };
}

/** Groups days into weeks (for the year view), keeping the last group's end at the last day. */
export function weeks(days: Day[]): Day[] {
  const out: Day[] = [];
  for (let end = days.length; end > 0; end -= 7) {
    const part = days.slice(Math.max(0, end - 7), end);
    const w: Day = { day: part[0].day, like: 0, super: 0, pass: 0, hold: 0, archive: 0 };
    for (const d of part) {
      w.like += d.like;
      w.super += d.super;
      w.pass += d.pass;
      w.hold += d.hold;
      w.archive += d.archive;
    }
    out.unshift(w);
  }
  return out;
}

/** "2026-10-06" → "10/6" (Portuguese puts the day first: "6/10") */
export const shortDay = (day: string, l: string = lang) =>
  l === 'pt' ? `${+day.slice(8, 10)}/${+day.slice(5, 7)}` : `${+day.slice(5, 7)}/${+day.slice(8, 10)}`;
