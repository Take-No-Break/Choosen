// The effects level (演出), shared by the swipe screen's sparkle switch and the settings, so a change
// in one shows in the other at once. Kept in localStorage per window.
import { intensity, type Intensity } from './burst';

export const fx = $state({ level: intensity() as Intensity });

export function setFx(v: Intensity) {
  fx.level = v;
  try {
    localStorage.setItem('choosen3.fx', v);
  } catch {
    /* private mode */
  }
}
