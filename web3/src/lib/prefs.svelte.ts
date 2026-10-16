// How this device shows the swipe screen, kept per device (a phone and a PC can differ):
//   layout: "cards" (a card stack: swipe, or double tap = 好評) or "scroll" (one picture after
//           another like short-video apps: up = next, down = back, double tap = 好評, triple = 超好評)
//   autoNext: scroll layout, a like / super like moves on by itself (off: it only marks the
//           picture, as in photo apps; the swipe up saves the mark)
//   info:   the folder and file name on the picture (the eye button)
//   buttons: the look of ↩ ✕ ★ ♥ — line (a thin white ring, see-through, its colour shows the moment it
//           is pressed; the default, and every device switched to it once on 2026-10-08), glass
//           (frosted), classic (coloured rings)
export type Layout = 'cards' | 'scroll';
export type ButtonStyle = 'glass' | 'line' | 'classic';

function read<T extends string>(key: string, allowed: readonly T[], fallback: T): T {
  try {
    const v = localStorage.getItem(key) as T | null;
    return v && allowed.includes(v) ? v : fallback;
  } catch {
    return fallback;
  }
}

export const prefs = $state({
  layout: read<Layout>('choosen3.layout', ['cards', 'scroll'], 'cards'),
  autoNext: read('choosen3.autoNext', ['1', '0'], '1') === '1',
  info: read('choosen3.info', ['1', '0'], '1') === '1',
  buttons: lineOnce(),
});

function lineOnce(): ButtonStyle {
  try {
    if (localStorage.getItem('choosen3.buttonStyleLine') !== '1') {
      localStorage.setItem('choosen3.buttonStyleLine', '1');
      localStorage.setItem('choosen3.buttonStyle', 'line');
      return 'line';
    }
  } catch {
    return 'line';
  }
  return read<ButtonStyle>('choosen3.buttonStyle', ['glass', 'line', 'classic'], 'line');
}

export function setButtonStyle(v: ButtonStyle) {
  prefs.buttons = v;
  save('buttonStyle', v);
}

export function setLayout(v: Layout) {
  prefs.layout = v;
  save('layout', v);
}

export function toggleAutoNext() {
  prefs.autoNext = !prefs.autoNext;
  save('autoNext', prefs.autoNext ? '1' : '0');
}

export function toggleInfo() {
  prefs.info = !prefs.info;
  save('info', prefs.info ? '1' : '0');
}

function save(key: string, value: string) {
  try {
    localStorage.setItem('choosen3.' + key, value);
  } catch {
    /* private mode: for this visit only */
  }
}
