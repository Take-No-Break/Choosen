// A long press on a row or a tile (selecting it): held still for `ms`, it fires once and the click
// that follows is swallowed. A scroll (the browser cancels the pointer) or a move of more than 10 px
// is not a long press.

export interface LongPress {
  ms?: number;
  onlong: () => void;
}

export function longpress(node: HTMLElement, opts: LongPress) {
  let o = opts;
  let timer = 0;
  let x = 0;
  let y = 0;
  let fired = false;
  const down = (e: PointerEvent) => {
    if (e.button > 0) return;
    fired = false;
    x = e.clientX;
    y = e.clientY;
    clearTimeout(timer);
    timer = window.setTimeout(() => {
      fired = true;
      navigator.vibrate?.(10);
      o.onlong();
    }, o.ms ?? 500);
  };
  const move = (e: PointerEvent) => {
    if (Math.hypot(e.clientX - x, e.clientY - y) > 10) clearTimeout(timer);
  };
  const stop = () => clearTimeout(timer);
  const click = (e: MouseEvent) => {
    if (!fired) return;
    fired = false;
    e.stopPropagation();
    e.preventDefault();
  };
  const menu = (e: Event) => e.preventDefault(); // no "save image" menu on a long press
  node.addEventListener('pointerdown', down);
  node.addEventListener('pointermove', move);
  node.addEventListener('pointerup', stop);
  node.addEventListener('pointercancel', stop);
  node.addEventListener('click', click, true);
  node.addEventListener('contextmenu', menu);
  return {
    update(next: LongPress) {
      o = next;
    },
    destroy() {
      clearTimeout(timer);
      node.removeEventListener('pointerdown', down);
      node.removeEventListener('pointermove', move);
      node.removeEventListener('pointerup', stop);
      node.removeEventListener('pointercancel', stop);
      node.removeEventListener('click', click, true);
      node.removeEventListener('contextmenu', menu);
    },
  };
}
