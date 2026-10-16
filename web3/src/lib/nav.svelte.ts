// Which screen is shown, shared so a screen can open another (the ⚙ in a corner opens 設定).
export type Tab = 'swipe' | 'ranking' | 'gallery' | 'settings';

function read(): Tab {
  try {
    const t = localStorage.getItem('choosen3.tab');
    return t === 'gallery' || t === 'ranking' || t === 'settings' ? t : 'swipe';
  } catch {
    return 'swipe';
  }
}

export const nav = $state({
  tab: read(),
  go(t: Tab) {
    nav.tab = t;
    try {
      localStorage.setItem('choosen3.tab', t);
    } catch {
      /* private mode */
    }
  },
});
