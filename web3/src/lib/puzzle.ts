// Puzzle pages (パズル), like manga panels or a photo collage: the next one to four pictures, in
// order, share one page in the arrangement their shapes suit best. Each arrangement slices the page
// into a row or a column of pictures, or of smaller columns / rows, sized from the pictures' shapes
// to fill the page; the one that cuts the least off its pictures wins, with no cell too small and a
// lean towards three a page. A page looks only at its own few pictures, so once decided it never
// changes as more load.

export interface Cell { i: number; x: number; y: number; w: number; h: number; clip?: string } // i = index into ratios; x,y,w,h in px inside the page box; clip: slantPage's shape
export interface Page { start: number; count: number; cells: Cell[] } // pictures start .. start+count-1, in order

const MAX = 4; // pictures a page
const MIN_RATIO = 0.4; // taller or wider pictures are laid out as if they were this tall / wide
const MAX_RATIO = 2.5;
const MIN_CELL = 0.22; // no cell narrower or lower than this share of the page's shorter side
const SMALL_CELL = 0.35; // below this share a cell costs more, the smaller the more
const SMALL = 0.2; // the cost of a cell at MIN_CELL
// the lean, by pictures on the page: three is the sweet spot; a picture alone costs LONE unless it
// fills the page cutting off at most FITS
const LEAN = [0, 0.05, 0.03, 0, 0.02];
const LONE = 0.15;
const FITS = 0.12;
const EPS = 1e-4; // scores this close are a tie (half-pixel rounding), so the fixed order decides

// a leaf is a picture's place on the page (0 .. k-1); a node lays its kids side by side or stacked
type Node = number | { row: boolean; kids: Node[] };

/** Every slicing of pictures lo .. hi-1 whose top level runs `row`-wise (a leaf when only one). */
function trees(lo: number, hi: number, row: boolean): Node[] {
  if (hi - lo === 1) return [lo];
  return runs(lo, lo, hi, row).map((kids) => ({ row, kids }));
}
/** Every way to cut from .. hi-1 into runs, each run turned the other way (two or more runs from lo). */
function runs(lo: number, from: number, hi: number, row: boolean): Node[][] {
  if (from === hi) return [[]];
  const out: Node[][] = [];
  for (let end = from === lo ? hi - 1 : hi; end > from; end--) // longer runs first
    for (const head of trees(from, end, !row)) for (const rest of runs(lo, end, hi, row)) out.push([head, ...rest]);
  return out;
}
// 1 + 2 + 6 + 22 arrangements. Their order settles ties: columns first, so 2 x 2 reads left to right,
// then down; longer runs first, so a pair sits right under a wide picture and a bigger one follows
const LAYOUTS: Node[][] = [[], [0], ...[2, 3, 4].map((k) => [...trees(0, k, false), ...trees(0, k, true)])];

const half = (v: number) => Math.round(v * 2) / 2;
const known = (v: number) => v > 0 && v < Infinity; // 0, NaN, undefined: not measured yet

/** Width / height of the arrangement with no gaps: a row adds widths, a column adds heights. */
function aspect(n: Node, r: readonly number[]): number {
  if (typeof n === 'number') return r[n];
  let s = 0;
  for (const k of n.kids) s += n.row ? aspect(k, r) : 1 / aspect(k, r);
  return n.row ? s : 1 / s;
}

/** Lays `n` into the box x, y, w, h: kids share its length after the gaps, as their shapes ask. */
function place(n: Node, r: readonly number[], x: number, y: number, w: number, h: number, gap: number, out: Cell[]) {
  if (typeof n === 'number') return void out.push({ i: n, x, y, w, h });
  const share = n.kids.map((k) => (n.row ? aspect(k, r) : 1 / aspect(k, r)));
  const sum = share.reduce((a, b) => a + b, 0);
  const from = n.row ? x : y;
  const len = (n.row ? w : h) - gap * (share.length - 1);
  let at = from;
  let acc = 0;
  for (let i = 0; i < share.length; i++) {
    acc += share[i];
    // edges from the running total, on half pixels, so rounding never adds up along the run
    const end = i === share.length - 1 ? from + (n.row ? w : h) : half(from + gap * i + (len * acc) / sum);
    if (n.row) place(n.kids[i], r, at, y, end - at, h, gap, out);
    else place(n.kids[i], r, x, at, w, end - at, gap, out);
    at = end + gap;
  }
}

/** The share of the cells' area cut off their pictures to fill them, plus a cost for small cells; Infinity if one is too small. */
function cost(cells: Cell[], r: readonly number[], side: number): number {
  let area = 0;
  let lost = 0;
  let least = Infinity;
  for (const c of cells) {
    least = Math.min(least, c.w, c.h);
    const q = c.w / c.h / r[c.i];
    lost += c.w * c.h * (1 - Math.min(q, 1 / q));
    area += c.w * c.h;
  }
  const s = least / side;
  if (s < MIN_CELL) return Infinity;
  return lost / area + (s < SMALL_CELL ? (SMALL * (SMALL_CELL - s)) / (SMALL_CELL - MIN_CELL) : 0);
}

/**
 * One page from `start`, or null when it cannot be decided yet (a needed ratio is unknown, i.e.
 * 0/NaN/undefined, and the list may still grow). Also null when the box has no size yet.
 * While the list is not `complete`, a page needs the ratios of the next four pictures.
 */
export function puzzlePage(ratios: readonly number[], start: number, box: { w: number; h: number }, gap: number, complete: boolean): Page | null {
  const w = Math.floor(box.w * 2) / 2;
  const h = Math.floor(box.h * 2) / 2;
  if (!(w > 0 && h > 0) || !(start >= 0) || start >= ratios.length) return null;
  const need = complete ? Math.min(MAX, ratios.length - start) : MAX;
  const r: number[] = [];
  for (let i = 0; i < need; i++) {
    const v = ratios[start + i];
    if (!known(v)) return null;
    r.push(Math.min(MAX_RATIO, Math.max(MIN_RATIO, v)));
  }
  const g = Math.max(0, half(gap)) || 0;
  const side = Math.min(w, h);
  let best: Cell[] = [];
  let bestScore = Infinity;
  for (let k = 1; k <= need; k++)
    for (const t of LAYOUTS[k]) {
      const cells: Cell[] = [];
      place(t, r, 0, 0, w, h, g, cells);
      const c = cost(cells, r, side); // alone, the cell is the page: no small-cell cost, only the cut
      const score = c + (k === 1 && c > FITS ? LONE : LEAN[k]);
      if (score < bestScore - EPS) {
        bestScore = score;
        best = cells;
      }
    }
  // one picture alone always fits, so best is never empty
  return { start, count: best.length, cells: best.map((c) => ({ ...c, i: start + c.i })) };
}

/** Pages one after another from `from` until a page cannot be decided or maxPages is reached. */
export function puzzlePages(ratios: readonly number[], box: { w: number; h: number }, gap: number, complete: boolean, from = 0, maxPages = Infinity): Page[] {
  const out: Page[] = [];
  for (let at = from; out.length < maxPages; ) {
    const p = puzzlePage(ratios, at, box, gap, complete);
    if (!p) break;
    out.push(p);
    at += p.count;
  }
  return out;
}

export interface SlantCell extends Cell {
  clip: string; // CSS clip-path polygon for the cell's box (x, y, w, h, grown to hold the slanted shape)
}

/**
 * The page with slanted cuts, like manga panels: every cut between pictures tilts a little (the
 * cuts in turn one way and the other), the page's own sides stay straight. Each cell gets a box
 * large enough for its four-sided shape and the shape as a clip-path. Neighbours take their shared
 * corners from the same cut lines, so they meet along them with the same gap as before.
 */
export function slantPage(page: Page, gap: number, tilt = 0.05): (SlantCell | Cell)[] {
  const cells = page.cells;
  if (cells.length < 2) return cells;
  const W = Math.max(...cells.map((c) => c.x + c.w));
  const H = Math.max(...cells.map((c) => c.y + c.h));
  const near = (a: number, b: number) => Math.abs(a - b) < 1;
  // the cut lines: x (or y) in the middle of a gap, and how far along the other axis they run
  type Line = { at: number; lo: number; hi: number; sign: number };
  const lines = (vertical: boolean): Line[] => {
    const out: Line[] = [];
    for (const c of cells) {
      const end = vertical ? c.x + c.w : c.y + c.h;
      if (near(end, vertical ? W : H)) continue; // a page side
      const at = end + gap / 2;
      const lo = vertical ? c.y : c.x, hi = vertical ? c.y + c.h : c.x + c.w;
      const l = out.find((x) => near(x.at, at) && lo <= x.hi + gap + 1 && hi >= x.lo - gap - 1);
      if (l) (l.lo = Math.min(l.lo, lo)), (l.hi = Math.max(l.hi, hi));
      else out.push({ at, lo, hi, sign: 0 });
    }
    // the cells on the other side of a cut can reach further along it
    for (const l of out)
      for (const c of cells) {
        const start = vertical ? c.x : c.y;
        if (near(start - gap / 2, l.at)) {
          const lo = vertical ? c.y : c.x, hi = vertical ? c.y + c.h : c.x + c.w;
          if (lo <= l.hi + gap + 1 && hi >= l.lo - gap - 1) (l.lo = Math.min(l.lo, lo)), (l.hi = Math.max(l.hi, hi));
        }
      }
    out.sort((a, b) => a.at - b.at || a.lo - b.lo);
    out.forEach((l, k) => (l.sign = k % 2 ? -1 : 1));
    return out;
  };
  const vs = lines(true), hs = lines(false);
  const least = Math.min(...cells.map((c) => Math.min(c.w, c.h)));
  const s = Math.min(Math.min(W, H) * tilt, least * 0.2); // how far a cut leans from end to end
  // Each side of a cell is a straight line: x = a + b*y for the left and right ones, y = a + b*x
  // for the top and bottom ones. A cut leans by s from one end to the other (its sign alternates);
  // a page side is straight. `inward` moves it half a gap towards the cell.
  const lineAt = (ls: Line[], at: number, t: number) => ls.find((l) => near(l.at, at) && t >= l.lo - gap - 1 && t <= l.hi + gap + 1);
  const side = (l: Line | undefined, edge: number, inward: number) => {
    if (!l) return { a: edge, b: 0 };
    const span = Math.max(1, l.hi - l.lo);
    return { a: l.at + l.sign * s * (1 + (2 * l.lo) / span) + inward, b: (-2 * l.sign * s) / span };
  };
  // where a left/right side meets a top/bottom side
  const meet = (v: { a: number; b: number }, h: { a: number; b: number }) => {
    const x = (v.a + v.b * h.a) / (1 - v.b * h.b);
    return [x, h.a + h.b * x];
  };
  const r = (v: number) => Math.round(v * 10) / 10;
  return cells.map((c) => {
    const left = side(lineAt(vs, c.x - gap / 2, c.y + c.h / 2), c.x, gap / 2);
    const right = side(lineAt(vs, c.x + c.w + gap / 2, c.y + c.h / 2), c.x + c.w, -gap / 2);
    const top = side(lineAt(hs, c.y - gap / 2, c.x + c.w / 2), c.y, gap / 2);
    const bottom = side(lineAt(hs, c.y + c.h + gap / 2, c.x + c.w / 2), c.y + c.h, -gap / 2);
    const pts = [meet(left, top), meet(right, top), meet(right, bottom), meet(left, bottom)].map((p) => [r(p[0]), r(p[1])]);
    const x = Math.min(...pts.map((p) => p[0])), y = Math.min(...pts.map((p) => p[1]));
    const w = r(Math.max(...pts.map((p) => p[0])) - x), h = r(Math.max(...pts.map((p) => p[1])) - y);
    const clip = `polygon(${pts.map((p) => `${r(p[0] - x)}px ${r(p[1] - y)}px`).join(', ')})`;
    return { i: c.i, x, y, w, h, clip };
  });
}

/**
 * A slanted cell's clip-path (`polygon(x y, ...)` in px, from slantPage) with rounded corners, as a
 * `path()`: each corner is cut back by up to `r` px along both of its edges (never more than half of
 * the shorter edge) and joined with a curve through the corner. Anything else comes back unchanged.
 */
export function roundClip(clip: string | undefined, r: number): string | undefined {
  if (!clip || r <= 0) return clip;
  const pts = [...clip.matchAll(/(-?[\d.]+)px\s+(-?[\d.]+)px/g)].map((m) => [+m[1], +m[2]] as const);
  if (pts.length < 3 || !clip.startsWith('polygon(')) return clip;
  const n = pts.length;
  const f = (v: number) => +v.toFixed(1);
  const toward = (a: readonly [number, number], b: readonly [number, number], d: number) => {
    const len = Math.hypot(b[0] - a[0], b[1] - a[1]) || 1;
    return [a[0] + ((b[0] - a[0]) * d) / len, a[1] + ((b[1] - a[1]) * d) / len] as const;
  };
  let d = '';
  for (let i = 0; i < n; i++) {
    const prev = pts[(i + n - 1) % n], p = pts[i], next = pts[(i + 1) % n];
    const cut = Math.min(r, Math.hypot(p[0] - prev[0], p[1] - prev[1]) / 2, Math.hypot(next[0] - p[0], next[1] - p[1]) / 2);
    const a = toward(p, prev, cut), b = toward(p, next, cut);
    d += `${i ? 'L' : 'M'} ${f(a[0])} ${f(a[1])} Q ${f(p[0])} ${f(p[1])} ${f(b[0])} ${f(b[1])} `;
  }
  return `path('${d}Z')`;
}
