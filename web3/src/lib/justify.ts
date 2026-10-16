// Justified rows (as photo apps lay out pictures in their own shapes): pictures join a row until
// the row at the target height is as wide as the space, then the row is sized to fill the width
// exactly (a little lower than the target). The last row, not full, keeps the target height.

export interface Box {
  w: number;
  h: number;
}

/** Sizes for pictures of the given aspect ratios (width / height) in rows `width` px wide. */
export function justify(ratios: number[], width: number, height: number, gap: number): Box[] {
  const out: Box[] = new Array(ratios.length);
  let start = 0;
  let sum = 0;
  const close = (end: number, h: number) => {
    for (let i = start; i < end; i++) out[i] = { w: Math.floor(ratios[i] * h * 100) / 100, h: Math.floor(h * 100) / 100 };
    start = end;
    sum = 0;
  };
  for (let i = 0; i < ratios.length; i++) {
    sum += ratios[i];
    const gaps = gap * (i - start);
    if (sum * height + gaps >= width) close(i + 1, (width - gaps - 0.5) / sum); // full: fit it exactly
  }
  if (start < ratios.length) close(ratios.length, height);
  return out;
}
