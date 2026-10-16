// Keyboard layouts A, B, C as in Version 1 (0.7.4); an own layout (4) falls back to B in this
// preview. Keys are matched by position (KeyboardEvent.code), so they work with the Japanese IME on.

export type Command = 'like' | 'likeHold' | 'super' | 'pass' | 'archive' | 'back';

export const LAYOUTS: Record<number, Record<string, Command>> = {
  1: { ArrowRight: 'like', ArrowLeft: 'pass', ArrowUp: 'super', ArrowDown: 'back', Enter: 'archive', NumpadEnter: 'archive', Delete: 'archive' },
  2: { ArrowRight: 'likeHold', ArrowLeft: 'pass', ArrowUp: 'archive', ArrowDown: 'back', Delete: 'archive' },
  3: { KeyD: 'like', KeyA: 'pass', KeyW: 'super', ShiftLeft: 'back', KeyE: 'archive', Delete: 'archive' },
};

export const HOLD_MS = 400; // likeHold: a press held this long is a super like

export function commandFor(keyMode: number, code: string): Command | undefined {
  return (LAYOUTS[keyMode] ?? LAYOUTS[2])[code];
}
