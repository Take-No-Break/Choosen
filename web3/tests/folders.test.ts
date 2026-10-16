import { describe, expect, it } from 'vitest';
import { photoFolders, folderName, rootToken, scopeOfOn } from '../src/lib/folders';
import { fitRect, collageName } from '../src/lib/collage';
import { justify } from '../src/lib/justify';
import type { State } from '../src/lib/api';

const st = (root: string, more: string[] = []) => ({ root, moreRoots: more }) as unknown as State;

describe('photo folders on / off (swipe screen)', () => {
  const roots = ['D:\\Photos', 'E:\\More', '\\storage\\emulated\\0\\Pictures'];

  it('lists the first photo folder, then the further ones', () => {
    expect(photoFolders(st('D:\\Photos', ['E:\\More']))).toEqual(['D:\\Photos', 'E:\\More']);
    expect(photoFolders(st(''))).toEqual([]);
  });
  it('names a folder by the last part of its path (Windows or phone)', () => {
    expect(folderName('D:\\Photos')).toBe('Photos');
    expect(folderName('\\storage\\emulated\\0\\Pictures')).toBe('Pictures');
    expect(folderName('D:\\')).toBe('D:');
  });
  it('the first photo folder as a whole is "/*", a further one its path', () => {
    expect(rootToken(0, 'D:\\Photos')).toBe('/*');
    expect(rootToken(1, 'E:\\More')).toBe('E:\\More');
  });
  it('all on (or nothing on): everything', () => {
    expect(scopeOfOn(roots, new Set())).toEqual({ tokens: [], label: 'すべて' });
    expect(scopeOfOn(roots, new Set(roots))).toEqual({ tokens: [], label: 'すべて' });
  });
  it('some off: the scope of the ones that are on, named', () => {
    expect(scopeOfOn(roots, new Set(['E:\\More']))).toEqual({
      tokens: ['/*', '\\storage\\emulated\\0\\Pictures'],
      label: 'Photos、Pictures',
    });
    expect(scopeOfOn(roots, new Set(['D:\\Photos', 'E:\\More']))).toEqual({ tokens: ['\\storage\\emulated\\0\\Pictures'], label: 'Pictures' });
  });
  it('more than two on: the first name and how many more', () => {
    const four = [...roots, 'F:\\Art'];
    expect(scopeOfOn(four, new Set(['E:\\More'])).label).toBe('Photos ほか2');
  });
  it('a folder that is no longer a photo folder does not count', () => {
    expect(scopeOfOn(roots, new Set(['X:\\Gone']))).toEqual({ tokens: [], label: 'すべて' });
  });
});

describe('collage', () => {
  it('a whole picture fits inside its cell, centred', () => {
    expect(fitRect(200, 100, 100, 100, false)).toEqual([0, 25, 100, 50]);
  });
  it('a filled picture covers its cell, centred', () => {
    expect(fitRect(200, 100, 100, 100, true)).toEqual([-50, 0, 200, 100]);
  });
  it('is named by the time it was made', () => {
    expect(collageName(new Date(2026, 9, 8, 9, 5, 7))).toBe('Choosen-collage-20261008-090507.jpg');
  });
});

describe('gallery in the pictures own shapes (justified rows)', () => {
  it('a full row fills the width exactly; the last row keeps the plain height', () => {
    const b = justify([0.7, 0.7, 0.7, 0.7, 1.6], 364, 171, 6);
    const row1 = b.slice(0, 3);
    expect(new Set(row1.map((x) => x.h)).size).toBe(1);
    expect(row1.reduce((s, x) => s + x.w, 0) + 2 * 6).toBeGreaterThan(362);
    expect(row1.reduce((s, x) => s + x.w, 0) + 2 * 6).toBeLessThanOrEqual(364);
  });
  it('a lone picture before a wide one is not stretched across a row', () => {
    const b = justify([0.7, 0.7, 0.7, 0.7, 1.6, 0.7], 364, 171, 6);
    expect(b[3].h).toBe(b[4].h); // the fourth shares its row with the wide one
    expect(b[3].h).toBeLessThanOrEqual(171);
  });
  it('the last row is not stretched', () => {
    const b = justify([1, 1, 1, 1], 364, 171, 6);
    expect(b[3]).toEqual({ w: 171, h: 171 });
  });
});
