// A collage: the pictures on the play screen drawn into one JPEG as they are laid out (two or four
// cells, whole over their blurred colours or filled), at the screen's own sharpness.

/** Where a picture lands in a w x h box: whole (contain) or filled (cover), centred. */
export function fitRect(iw: number, ih: number, w: number, h: number, cover: boolean): [number, number, number, number] {
  const s = cover ? Math.max(w / iw, h / ih) : Math.min(w / iw, h / ih);
  const dw = iw * s, dh = ih * s;
  return [(w - dw) / 2, (h - dh) / 2, dw, dh];
}

/** The cells (each with its .pic and .blur images) as one JPEG, or null if a picture is not ready. */
export async function collage(cells: HTMLElement[], fill: boolean): Promise<Blob | null> {
  if (!cells.length) return null;
  const rects = cells.map((c) => c.getBoundingClientRect());
  const left = Math.min(...rects.map((r) => r.left)), top = Math.min(...rects.map((r) => r.top));
  const right = Math.max(...rects.map((r) => r.right)), bottom = Math.max(...rects.map((r) => r.bottom));
  const pad = 6; // a thin dark frame, as the cells sit on the screen
  const w = right - left + 2 * pad, h = bottom - top + 2 * pad;
  const k = Math.min(Math.max(2, devicePixelRatio || 1), 4096 / Math.max(w, h)); // sharp, within a phone's canvas limits
  const canvas = document.createElement('canvas');
  canvas.width = Math.round(w * k);
  canvas.height = Math.round(h * k);
  const g = canvas.getContext('2d');
  if (!g) return null;
  g.scale(k, k);
  g.fillStyle = '#0b0d10';
  g.fillRect(0, 0, w, h);
  for (let i = 0; i < cells.length; i++) {
    const pic = cells[i].querySelector<HTMLImageElement>('img.pic');
    const blur = cells[i].querySelector<HTMLImageElement>('img.blur');
    if (!pic || !pic.complete || !pic.naturalWidth) return null;
    const r = rects[i];
    const x = r.left - left + pad, y = r.top - top + pad;
    g.save();
    g.beginPath();
    g.rect(x, y, r.width, r.height);
    g.clip();
    g.translate(x, y);
    if (!fill && blur?.naturalWidth) {
      // the picture's own colours behind it, as on the screen
      // as on the screen: covering a box 28 px wider on every side, blur 14px, a little more colour
      const [bx, by, bw, bh] = fitRect(blur.naturalWidth, blur.naturalHeight, r.width + 56, r.height + 56, true);
      g.filter = 'blur(14px) saturate(1.25)';
      g.globalAlpha = 0.85;
      g.drawImage(blur, bx - 28, by - 28, bw, bh);
      g.filter = 'none';
      g.globalAlpha = 1;
    }
    const [px, py, pw, ph] = fitRect(pic.naturalWidth, pic.naturalHeight, r.width, r.height, fill);
    g.drawImage(pic, px, py, pw, ph);
    g.restore();
  }
  return new Promise((done) => canvas.toBlob((b) => done(b), 'image/jpeg', 0.92));
}

/** "Choosen-collage-20261008-153012.jpg" */
export function collageName(d = new Date()): string {
  const p = (n: number) => String(n).padStart(2, '0');
  return `Choosen-collage-${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}.jpg`;
}
