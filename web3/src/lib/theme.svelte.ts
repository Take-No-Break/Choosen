// The look: 黒 (dark), 白 (light) or 自動 (as the device is set). Kept per device. Photos stay on dark
// ground in the full view and on the start screen whichever is chosen.
export type Theme = 'dark' | 'light' | 'auto';

function read(): Theme {
  try {
    const v = localStorage.getItem('choosen3.theme');
    return v === 'light' || v === 'auto' ? v : 'dark';
  } catch {
    return 'dark';
  }
}

export const theme = $state({ value: read() });

const deviceLight = typeof matchMedia === 'function' ? matchMedia('(prefers-color-scheme: light)') : null;

/** Puts the chosen look on the page (data-theme on <html>, read by ui/tokens.css). */
export function applyTheme() {
  const light = theme.value === 'light' || (theme.value === 'auto' && !!deviceLight?.matches);
  document.documentElement.dataset.theme = light ? 'light' : 'dark';
}
deviceLight?.addEventListener('change', applyTheme);

export function setTheme(v: Theme) {
  theme.value = v;
  try {
    localStorage.setItem('choosen3.theme', v);
  } catch {
    /* private mode: for this visit only */
  }
  applyTheme();
}
