import { describe, expect, it } from 'vitest';
import { deviceLang, fill, SERVER, SERVER_TEXTS, t } from '../src/lib/i18n';
import { EN } from '../src/lib/i18n-en';
import { PT } from '../src/lib/i18n-pt';

// every source file's text, read by Vite
const SOURCES = import.meta.glob<string>('../src/**/*.{svelte,ts}', { query: '?raw', import: 'default', eager: true });

/** Every t('…') key in the sources (the first argument is always a plain string). */
function usedKeys(): Set<string> {
  const keys = new Set<string>();
  for (const src of Object.values(SOURCES)) {
    for (const m of src.matchAll(/\bt\(\s*'((?:[^'\\]|\\.)*)'/g))
      keys.add(m[1].replace(/\\(.)/g, (_: string, c: string) => (c === 'n' ? '\n' : c)));
  }
  return keys;
}

const holes = (s: string) => [...s.replace(/\{\{|\}\}/g, '').matchAll(/\{(\d+)\}/g)].map((m) => m[1]).sort().join(',');
const JAPANESE = /[぀-ヿ㐀-鿿]/;

describe('screen languages', () => {
  const keys = [...usedKeys(), ...SERVER.map(([, k]) => k), ...SERVER_TEXTS];

  it('finds the screen texts', () => {
    expect(keys.length).toBeGreaterThan(200);
  });

  for (const [name, dict] of [
    ['English', EN],
    ['Portuguese', PT],
  ] as const) {
    it(`${name} has every screen text, with the same {n} places`, () => {
      const missing = keys.filter((k) => !dict[k]);
      expect(missing).toEqual([]);
      const forms = (k: string) => [dict[k]].flat();
      const wrongHoles = keys.filter((k) => dict[k] && forms(k).some((v) => holes(v) !== holes(k)));
      expect(wrongHoles).toEqual([]);
    });

    it(`${name} has no leftover entries and no Japanese text`, () => {
      const used = new Set(keys);
      expect(Object.keys(dict).filter((k) => !used.has(k))).toEqual([]);
      // the subfolders that 「サブフォルダに分ける」 makes keep their Japanese names on disk
      const japanese = Object.values(dict).flat().filter((v) => JAPANESE.test(v.replace(/「(見送り|好評|超好評)」|"(見送り|好評|超好評)"/g, '')));
      expect(japanese).toEqual([]);
    });
  }

  it('has no Japanese screen text outside t() (comments aside)', () => {
    const found: string[] = [];
    for (const [file, text] of Object.entries(SOURCES)) {
      if (file.includes('i18n')) continue;
      const blank = (m: string) => m.replace(/[^\n]/g, ' ');
      const lines = text.replace(/\/\*[\s\S]*?\*\//g, blank).replace(/<!--[\s\S]*?-->/g, blank).split(/\r?\n/);
      lines.forEach((line, i) => {
        const code = line.replace(/(^|[^:'"`])\/\/.*$/, '$1').replace(/\bt\(\s*'(?:[^'\\]|\\.)*'/g, '');
        if (JAPANESE.test(code)) found.push(`${file}:${i + 1}`);
      });
    }
    expect(found).toEqual([]);
  });

  it('shows the Japanese text without its §note', () => {
    expect(t('サブフォルダに分ける§button')).toBe('サブフォルダに分ける');
    expect(t('{0} 枚', 3)).toBe('3 枚');
  });

  it('fills {n} places and keeps literal braces', () => {
    expect(fill('{0} {1}', ['a'])).toBe('a ');
    expect(fill('{0} of {1}', [3, 'x'])).toBe('3 of x');
    expect(fill('{{0}} {0}', [1])).toBe('{0} 1');
  });

  it('picks the device language: ja, pt, else English', () => {
    expect(deviceLang(['ja-JP'])).toBe('ja');
    expect(deviceLang(['pt-BR', 'en-US'])).toBe('pt');
    expect(deviceLang(['pt-PT'])).toBe('pt');
    expect(deviceLang(['de-DE', 'ja'])).toBe('ja');
    expect(deviceLang(['fr-FR'])).toBe('en');
    expect(deviceLang([''])).toBe('en');
  });
});
