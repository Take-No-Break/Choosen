import { describe, expect, it } from 'vitest';
import { buildEntries, scopeLabel, knownTokens, type FolderRow } from '../src/lib/scope';
import { fitWidth } from '../src/lib/api';

const row = (path: string, depth: number, name = path.split('/').pop() || 'root'): FolderRow => ({ path, name, depth, total: 1, liked: 0 });

describe('scope (範囲)', () => {
  const one = [row('', 0), row('b', 1), row('a', 1), row('a/x', 2)];
  const two = [row('', 0, 'Photos'), row('a', 1), row('D:/More', 0, 'More'), row('D:/More/c', 1, 'c')];

  it('one photo folder: its folders as a tree in name order', () => {
    expect(buildEntries(one).map((e) => [e.token, e.indent])).toEqual([
      ['a', 0],
      ['a/x', 1],
      ['b', 0],
    ]);
  });
  it('several photo folders: each on top, the first one as "/*"', () => {
    const e = buildEntries(two);
    expect(e.map((x) => [x.token, x.indent, x.top])).toEqual([
      ['/*', 0, true],
      ['a', 1, false],
      ['D:/More', 0, true],
      ['D:/More/c', 1, false],
    ]);
  });
  it('labels: すべて, names, "ほか" and V1\'s direct-only tokens', () => {
    const e = buildEntries(one);
    expect(scopeLabel([], e)).toBe('すべて');
    expect(scopeLabel(['a', 'b'], e)).toBe('a、b');
    expect(scopeLabel(['a', 'b', 'a/x', 'gone'], e)).toBe('a、b ほか2');
    expect(scopeLabel(['a/'], e)).toBe('a の直下');
    expect(scopeLabel(['/'], e)).toBe('写真フォルダの直下');
    expect(scopeLabel(['gone/deep'], e)).toBe('deep');
  });
  it('a saved condition keeps only folders that still exist', () => {
    expect(knownTokens(['a', 'a/x/', 'gone', '/'], buildEntries(one))).toEqual(['a', 'a/x/', '/']);
    expect(knownTokens(['/*', 'D:/More/c'], buildEntries(two))).toEqual(['/*', 'D:/More/c']);
  });
});

describe('screen-fit pictures for phones', () => {
  it('the long screen edge in device pixels, rounded up to a fixed width', () => {
    expect(fitWidth(844, 3)).toBe(2560); // 2532 px
    expect(fitWidth(800, 2)).toBe(1600);
    expect(fitWidth(640, 2)).toBe(1280);
    expect(fitWidth(1440, 3)).toBe(2560); // capped
  });
});
