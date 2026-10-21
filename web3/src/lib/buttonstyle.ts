// The look of the swipe screen's ↩ ✕ ★ ♥ on this device: its own choice, else グラス (frosted; the
// main look since 2026-10-20). On 2026-10-08 every device was given "line" without choosing it, so
// that one stored "line" is dropped once; any choice made after this sticks. 0.13's own one-time
// flag is set too (never removed): after a rollback, 0.13 would otherwise store "line" again.
export type ButtonStyle = 'glass' | 'line' | 'classic';
export const BUTTONS_DEFAULT: ButtonStyle = 'glass';

type Store = Pick<Storage, 'getItem' | 'setItem' | 'removeItem'>;

export function buttonStyleFrom(store: Store): ButtonStyle {
  if (store.getItem('choosen3.buttonStyleGlass') !== '1') {
    if (store.getItem('choosen3.buttonStyle') === 'line') store.removeItem('choosen3.buttonStyle');
    store.setItem('choosen3.buttonStyleLine', '1');
    store.setItem('choosen3.buttonStyleGlass', '1');
  }
  const v = store.getItem('choosen3.buttonStyle');
  return v === 'glass' || v === 'line' || v === 'classic' ? v : BUTTONS_DEFAULT;
}
