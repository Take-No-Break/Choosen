import { describe, expect, it } from 'vitest';
import { commandFor, keyLegend, pageKeys } from '../src/lib/keys';
import { mouseLegend, mouseMap } from '../src/lib/mouse';
import { buttonStyleFrom } from '../src/lib/buttonstyle';

describe('keys for the pages of a work', () => {
  it('Shift turns the page in every layout, except where the layout uses it (C: left Shift = back)', () => {
    expect(commandFor(1, 'ShiftLeft')).toBe('pageNext');
    expect(commandFor(2, 'ShiftRight')).toBe('pageNext');
    expect(commandFor(3, 'ShiftLeft')).toBe('back');
    expect(commandFor(3, 'ShiftRight')).toBe('pageNext');
    expect(commandFor(1, 'Comma')).toBe('pagePrev');
    expect(commandFor(1, 'ArrowRight')).toBe('like');
  });
  it('the legend lists what a layout does and its page keys', () => {
    expect(keyLegend(1)).toEqual([['like', ['ArrowRight']], ['super', ['ArrowUp']], ['pass', ['ArrowLeft']], ['back', ['ArrowDown']], ['archive', ['Enter', 'Delete']]]);
    expect(keyLegend(2).find(([c]) => c === 'likeHold')?.[1]).toEqual(['ArrowRight']);
    expect(pageKeys(3, 'pageNext')).toEqual(['ShiftRight', 'Period', 'PageDown']);
    expect(pageKeys(1, 'pageNext')).toEqual(['ShiftLeft', 'ShiftRight', 'Period', 'PageDown']);
  });
  it('the mouse legend lists each command with its buttons', () => {
    const m = mouseLegend(mouseMap(2));
    expect(m.find(([c]) => c === 'super')?.[1]).toEqual(['left']);
    expect(m.find(([c]) => c === 'pass')?.[1]).toEqual(['middle', 'wheelDown']);
    expect(mouseLegend(mouseMap(1))[0]).toEqual(['grab', ['left', 'right']]);
  });
});

describe('the buttons look グラス by default', () => {
  const store = (init: Record<string, string>) => {
    const m = new Map(Object.entries(init));
    return { m, getItem: (k: string) => m.get(k) ?? null, setItem: (k: string, v: string) => void m.set(k, v), removeItem: (k: string) => void m.delete(k) };
  };
  it('a device given "line" on 2026-10-08 becomes グラス once', () => {
    const s = store({ 'choosen3.buttonStyle': 'line', 'choosen3.buttonStyleLine': '1' });
    expect(buttonStyleFrom(s)).toBe('glass');
    expect(s.m.get('choosen3.buttonStyleGlass')).toBe('1');
    expect(s.m.get('choosen3.buttonStyleLine')).toBe('1'); // 0.13 after a rollback must not store "line" again
  });
  it('a choice made after that, or classic, stays', () => {
    const s = store({ 'choosen3.buttonStyleGlass': '1', 'choosen3.buttonStyle': 'line' });
    expect(buttonStyleFrom(s)).toBe('line');
    expect(buttonStyleFrom(store({ 'choosen3.buttonStyle': 'classic' }))).toBe('classic');
    expect(buttonStyleFrom(store({}))).toBe('glass');
  });
  it('a new device is marked for 0.13 too, so a rollback keeps its look', () => {
    const s = store({});
    buttonStyleFrom(s);
    expect(s.m.get('choosen3.buttonStyleLine')).toBe('1');
    expect(s.m.has('choosen3.buttonStyle')).toBe(false);
  });
});
