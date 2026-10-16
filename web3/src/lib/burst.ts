// The verdict colours and the effects level (演出: 標準 / 控えめ / なし, localStorage "choosen3.fx").
// The effects themselves are CSS on the swipe screen (light around the picture, a sweep from the
// side a card leaves by); there are no particle bursts any more.

import type { Verdict } from './physics';

export const COLORS: Record<Verdict, string> = { like: '#3CC47C', super: '#F4B73A', pass: '#E0566A', archive: '#8C919B' };

export type Intensity = 'normal' | 'calm' | 'off';

export function intensity(): Intensity {
  try {
    const v = localStorage.getItem('choosen3.fx');
    return v === 'calm' || v === 'off' ? v : 'normal';
  } catch {
    return 'normal';
  }
}
