// Soft picture edges: the picture fades into its glow and blurred colours instead of ending on a
// hard line. The image element is as large as its box (object-fit contain or cover), so the mask has
// to know where the picture itself is drawn: this action works it out (on load and whenever the box
// changes size) and hands it to the .feather class's mask (tokens.css) as --fx --fy --fw --fh --ff.
// Transforms (zoom) move the mask with the picture.

/** Where a picture of iw x ih is drawn in a W x H box, clipped to the box, and how wide its soft edge is. */
export function drawnRect(iw: number, ih: number, W: number, H: number, cover: boolean) {
  const s = cover ? Math.max(W / iw, H / ih) : Math.min(W / iw, H / ih);
  let w = iw * s, h = ih * s, x = (W - w) / 2, y = (H - h) / 2;
  if (x < 0) (x = 0), (w = W);
  if (y < 0) (y = 0), (h = H);
  const f = Math.max(8, Math.min(w, h) * 0.035);
  return { x, y, w, h, f };
}

/**
 * Soft edges on a box that clips its pictures (a cell's frame). A picture that fills the box and is
 * slowly zoomed grows past its own soft edge, which the box then cuts off; the box's edges stay
 * where they are, so they keep the picture soft at its sides however far it is zoomed.
 */
export function featherBox(box: HTMLElement) {
  const set = () => {
    const W = box.clientWidth, H = box.clientHeight;
    if (W && H) box.style.setProperty('--ff', `${Math.max(8, Math.min(W, H) * 0.035)}px`);
  };
  const ro = new ResizeObserver(set);
  ro.observe(box);
  set();
  return { destroy: () => ro.disconnect() };
}

/** A blurred layer appears softly once its picture is in (class "in"), instead of popping in. */
export function fadeIn(img: HTMLImageElement) {
  const on = () => img.classList.add('in');
  if (img.complete && img.naturalWidth) on();
  else img.addEventListener('load', on, { once: true });
  return { destroy: () => img.removeEventListener('load', on) };
}

export function feather(img: HTMLImageElement, cover = false) {
  let fill = cover;
  const set = () => {
    const W = img.clientWidth, H = img.clientHeight, iw = img.naturalWidth, ih = img.naturalHeight;
    if (!W || !H || !iw || !ih) return;
    const r = drawnRect(iw, ih, W, H, fill);
    img.style.setProperty('--fx', `${r.x}px`);
    img.style.setProperty('--fy', `${r.y}px`);
    img.style.setProperty('--fw', `${r.w}px`);
    img.style.setProperty('--fh', `${r.h}px`);
    img.style.setProperty('--ff', `${r.f}px`);
  };
  img.addEventListener('load', set);
  const ro = new ResizeObserver(set);
  ro.observe(img);
  set();
  return {
    update(next: boolean) {
      fill = next;
      set();
    },
    destroy() {
      img.removeEventListener('load', set);
      ro.disconnect();
    },
  };
}
