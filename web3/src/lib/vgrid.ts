// The gallery's grid as numbers, for every picture of the list whether it is loaded yet or not: the
// scroll is as long as the whole list from the start, only the tiles near the screen are drawn,
// and the pages under them load wherever the scroll stops. Square tiles, or the pictures' own
// shapes in justified rows (as lib/justify.ts, with positions).

export interface Place {
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface Grid {
  count: number;
  height: number;
  at(i: number): Place;
  /** The tiles that meet the band y0..y1 (grid coordinates), as [first, last], or null. */
  band(y0: number, y1: number): [number, number] | null;
}

/** Square tiles in as many columns of at least `minTile` px as fit `width`, with an optional cap. */
export function squareGrid(count: number, width: number, minTile: number, gap: number, maxColumns = Infinity): Grid {
  const cols = Math.max(1, Math.min(Math.max(1, Math.floor(maxColumns)), Math.floor((width + gap) / (Math.max(1, minTile) + gap))));
  const cell = Math.max(1, (width - gap * (cols - 1)) / cols);
  const step = cell + gap;
  const rows = Math.ceil(count / cols);
  return {
    count,
    height: rows ? rows * step - gap : 0,
    at: (i) => ({ x: (i % cols) * step, y: Math.floor(i / cols) * step, w: cell, h: cell }),
    band(y0, y1) {
      if (!rows || y1 <= y0) return null;
      const r0 = Math.max(0, Math.floor(y0 / step));
      const r1 = Math.min(rows - 1, Math.floor(y1 / step));
      if (r0 > r1) return null;
      return [r0 * cols, Math.min(count - 1, (r1 + 1) * cols - 1)];
    },
  };
}

/**
 * The pictures' own shapes (ratio = width / height) in rows about `rowH` high: a row takes pictures
 * until it is full, then fills the width exactly; the last row keeps the plain height.
 */
export function shapesGrid(ratios: readonly number[], width: number, rowH: number, gap: number, maxPerRow = Infinity): Grid {
  const n = ratios.length;
  const rowLimit = Math.max(1, Math.floor(maxPerRow));
  const xs = new Float64Array(n), ys = new Float64Array(n), ws = new Float64Array(n), hs = new Float64Array(n);
  let start = 0;
  let sum = 0;
  let y = 0;
  const close = (end: number, h: number) => {
    const hh = Math.floor(h * 100) / 100;
    let x = 0;
    for (let i = start; i < end; i++) {
      const w = Math.floor(ratios[i] * h * 100) / 100;
      xs[i] = x;
      ys[i] = y;
      ws[i] = w;
      hs[i] = hh;
      x += w + gap;
    }
    y += hh + gap;
    start = end;
    sum = 0;
  };
  for (let i = 0; i < n; i++) {
    sum += ratios[i];
    const gaps = gap * (i - start);
    if (sum * rowH + gaps >= width || i - start + 1 >= rowLimit) close(i + 1, (width - gaps - 0.5) / sum); // full: fit it exactly
  }
  if (start < n) close(n, rowH);
  return {
    count: n,
    height: n ? y - gap : 0,
    at: (i) => ({ x: xs[i], y: ys[i], w: ws[i], h: hs[i] }),
    band(y0, y1) {
      if (!n || y1 <= y0) return null;
      // the first tile whose bottom is below y0 (rows go down, so bottoms never go up)
      let lo = 0;
      let hi = n;
      while (lo < hi) {
        const mid = (lo + hi) >> 1;
        if (ys[mid] + hs[mid] <= y0) lo = mid + 1;
        else hi = mid;
      }
      if (lo >= n || ys[lo] >= y1) return null;
      let a = lo;
      let b = n - 1;
      while (a < b) {
        const mid = (a + b + 1) >> 1;
        if (ys[mid] < y1) a = mid;
        else b = mid - 1;
      }
      return [lo, a];
    },
  };
}
