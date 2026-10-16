// Screen language: Japanese (the source text), English or Brazilian Portuguese. The Japanese text is
// the key: t('閉じる') returns it in the chosen language, falling back to Japanese when a translation is
// missing (a test makes sure none is). '{0}', '{1}' … are filled from the extra arguments; '{{' and
// '}}' stand for literal braces. A translation with a number in it can have two forms, [one, other],
// chosen by the first number among the arguments. 'text§note' tells apart two uses of the same Japanese
// text that need different translations (a short one for a narrow button); Japanese shows 'text'.
//
// The language is chosen once when the page loads: the one saved on this device (設定 → 言語), else
// the device's own language (ja, pt; anything else gets English). Changing it reloads the page, so
// texts made at load time (menus, labels) follow too.
import { EN } from './i18n-en';
import { PT } from './i18n-pt';

export type Lang = 'ja' | 'en' | 'pt';
export type Dict = Record<string, string | readonly [string, string]>;
export const LANGS: [Lang, string][] = [
  ['ja', '日本語'],
  ['en', 'English'],
  ['pt', 'Português'],
];
const KEY = 'choosen3.lang';

export function deviceLang(list: readonly string[]): Lang {
  for (const l of list) {
    const c = l.toLowerCase().slice(0, 2);
    if (c === 'ja') return 'ja';
    if (c === 'pt') return 'pt';
    if (c === 'en') return 'en';
  }
  return 'en';
}

const known = (v: string | null): Lang | null => (v === 'ja' || v === 'en' || v === 'pt' ? v : null);

function saved(): Lang | null {
  // ?lang= carries a choice that could not be stored (storage blocked) through the reload
  const asked = known(new URLSearchParams(location.search).get('lang'));
  if (asked) return asked;
  try {
    return known(localStorage.getItem(KEY));
  } catch {
    return null;
  }
}

// outside a browser (the unit tests) the texts stay in Japanese, the source language
export const lang: Lang =
  typeof window === 'undefined' ? 'ja' : (saved() ?? deviceLang(navigator.languages?.length ? navigator.languages : [navigator.language ?? '']));
const dict: Dict | null = lang === 'en' ? EN : lang === 'pt' ? PT : null;
if (typeof document !== 'undefined') document.documentElement.lang = lang === 'pt' ? 'pt-BR' : lang;

export function setLang(l: Lang): void {
  const url = new URL(location.href);
  try {
    localStorage.setItem(KEY, l);
    url.searchParams.delete('lang');
  } catch {
    url.searchParams.set('lang', l); // storage blocked: this page view still changes
  }
  if (l !== lang || url.href !== location.href) location.replace(url.href);
}

export function fill(text: string, args: readonly unknown[]): string {
  return text.replace(/\{\{|\}\}|\{(\d+)\}/g, (m, i) => (m === '{{' ? '{' : m === '}}' ? '}' : String(args[Number(i)] ?? '')));
}

export function t(ja: string, ...args: unknown[]): string {
  const e = dict?.[ja];
  if (!e) return fill(ja.split('§')[0], args);
  if (typeof e === 'string') return fill(e, args);
  return fill(args.find((a) => typeof a === 'number') === 1 ? e[0] : e[1], args);
}

// Messages the core (Choosen.exe, or the Android app's copy of it) sends in Japanese with a name or a
// number in them: each pattern's captures fill the key's {0}, {1} ….
export const SERVER: [RegExp, string][] = [
  [/^フォルダが見つかりません: (.*)$/s, 'フォルダが見つかりません: {0}'],
  [/^サブフォルダの階層は 0〜(\d+) で指定してください$/, 'サブフォルダの階層は 0〜{0} で指定してください'],
  [/^同じフォルダが 2 回入っています: (.*)$/s, '同じフォルダが 2 回入っています: {0}'],
  [/^「(.*)」は写真フォルダ「(.*)」の中にあります。重なるフォルダは追加できません$/s, '「{0}」は写真フォルダ「{1}」の中にあります。重なるフォルダは追加できません'],
  [/^「(.*)」の中に写真フォルダ「(.*)」があります。重なるフォルダは追加できません$/s, '「{0}」の中に写真フォルダ「{1}」があります。重なるフォルダは追加できません'],
  [/^バックアップ先のフォルダを作れません: (.*)$/s, 'バックアップ先のフォルダを作れません: {0}'],
  [/^ポート (\d+) を開けません（他のプログラムが使っている可能性があります）$/, 'ポート {0} を開けません（他のプログラムが使っている可能性があります）'],
  [/^SD カード \((.*)\)$/, 'SD カード ({0})'],
];

/** The core's fixed messages that reach the screen (the test makes sure each one is translated). */
export const SERVER_TEXTS = [
  '書き出し方が正しくありません',
  '操作の割り当てを保存できません',
  '書き出す種類が正しくありません',
  '保存先を選んでください',
  '保存先のフォルダが見つかりません',
  '移動できるのはアーカイブと見送りだけです',
  '書き出しの途中です',
  '写真フォルダの中には書き出せません',
  '該当する画像がありません',
  '保存先にフォルダを作れません',
  'フォルダか画像を選んでください',
  'ほかのファイル操作の途中です',
  '元に戻す移動がありません',
  'フォルダが見つかりません',
  '内部ストレージ',
];

/** A message from the core in the chosen language (one it does not know is shown as it came). */
export function server(msg: string): string {
  if (!dict || !msg) return msg;
  if (dict[msg]) return t(msg);
  for (const [re, key] of SERVER) {
    const m = re.exec(msg);
    if (m) return t(key, ...m.slice(1));
  }
  return msg;
}

/** The list separator: 「A、B」 / "A, B". */
export const SEP = lang === 'ja' ? '、' : ', ';
