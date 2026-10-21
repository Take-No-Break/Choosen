// Keyboard layouts A, B, C as in Version 1 (0.7.4); an own layout (4) falls back to B in this
// preview. Keys are matched by position (KeyboardEvent.code), so they work with the Japanese IME on.

export type Command = 'like' | 'likeHold' | 'super' | 'pass' | 'archive' | 'back' | 'pageNext' | 'pagePrev';

export const LAYOUTS: Record<number, Record<string, Command>> = {
  1: { ArrowRight: 'like', ArrowLeft: 'pass', ArrowUp: 'super', ArrowDown: 'back', Enter: 'archive', NumpadEnter: 'archive', Delete: 'archive' },
  2: { ArrowRight: 'likeHold', ArrowLeft: 'pass', ArrowUp: 'archive', ArrowDown: 'back', Delete: 'archive' },
  3: { KeyD: 'like', KeyA: 'pass', KeyW: 'super', ShiftLeft: 'back', KeyE: 'archive', Delete: 'archive' },
};

export const HOLD_MS = 400; // likeHold: a press held this long is a super like

// a work of several pages, in every layout: Shift = the next page (the layout comes first, so in C
// the left Shift stays 戻る and the right Shift turns pages); . and PageDown next, , and PageUp back
export const PAGE_KEYS: Record<string, Command> = {
  ShiftLeft: 'pageNext',
  ShiftRight: 'pageNext',
  Period: 'pageNext',
  PageDown: 'pageNext',
  Comma: 'pagePrev',
  PageUp: 'pagePrev',
};

const LEGEND: Command[] = ['like', 'likeHold', 'super', 'pass', 'back', 'archive'];
/** A layout's commands with their keys, for the settings' legend (テンキー Enter is Enter's twin). */
export function keyLegend(keyMode: number): [Command, string[]][] {
  const m = LAYOUTS[keyMode] ?? LAYOUTS[2];
  return LEGEND.map((c) => [c, Object.keys(m).filter((k) => m[k] === c && k !== 'NumpadEnter')] as [Command, string[]]).filter(
    ([, k]) => k.length > 0,
  );
}
/** The keys that turn to a work's next / previous page in this layout (a layout's own keys win). */
export function pageKeys(keyMode: number, cmd: 'pageNext' | 'pagePrev'): string[] {
  const m = LAYOUTS[keyMode] ?? LAYOUTS[2];
  return Object.keys(PAGE_KEYS).filter((k) => PAGE_KEYS[k] === cmd && !m[k]);
}

export function commandFor(keyMode: number, code: string): Command | undefined {
  return (LAYOUTS[keyMode] ?? LAYOUTS[2])[code] ?? PAGE_KEYS[code];
}
