// The photo folders (the first one, then the further ones) and which of them this device swipes.
// A folder switched off stays in the library (gallery, ranking); only the swipe screen leaves it out.
// The switches are kept per device, by folder path.
import { SEP, server, t } from './i18n';
import { postJ, type State } from './api';

/** The photo folders in their order: the first one, then the further ones. */
export const photoFolders = (st: State): string[] => [st.root ?? '', ...(st.moreRoots ?? [])].filter(Boolean);

/** A photo folder's own name (the last part of its path). */
export const folderName = (path: string): string => path.split(/[\\/]/).filter(Boolean).pop() ?? path;

/** Match the core's saved path format, including Android's forward-slash picker paths. */
export function photoFolderPath(path: string): string {
  let p = path.replaceAll('/', '\\');
  while (p.length > 3 && p.endsWith('\\')) p = p.slice(0, -1);
  return p;
}

/**
 * A picked folder as the list keeps it: the core's path format, and the stored spelling when the list
 * has the same folder with other upper / lower case letters (the core compares A-Z without case).
 */
export function matchFolder(list: readonly string[], path: string): string {
  const p = photoFolderPath(path);
  const key = (s: string) => s.replace(/[A-Z]/g, (c) => c.toLowerCase());
  return list.find((r) => key(r) === key(p)) ?? p;
}

/** The scope token of the i-th photo folder ("/*" = the first one as a whole). */
export const rootToken = (i: number, path: string): string => (i === 0 ? '/*' : path);

const OFF_KEY = 'choosen3.offFolders';
export function readOff(): Set<string> {
  try {
    const v = JSON.parse(localStorage.getItem(OFF_KEY) ?? '[]');
    return new Set(Array.isArray(v) ? v.filter((x) => typeof x === 'string') : []);
  } catch {
    return new Set();
  }
}
export function saveOff(off: Set<string>) {
  try {
    localStorage.setItem(OFF_KEY, JSON.stringify([...off]));
  } catch {
    /* private mode: for this visit only */
  }
}

/** The swipe scope of the folders that are on: [] = all of them (none off, or all off). */
export function scopeOfOn(roots: string[], off: Set<string>): { tokens: string[]; label: string } {
  const on = roots.map((p, i) => ({ p, i })).filter(({ p }) => !off.has(p));
  if (!on.length || on.length === roots.length) return { tokens: [], label: t('すべて') };
  const names = on.map(({ p }) => folderName(p));
  return {
    tokens: on.map(({ p, i }) => rootToken(i, p)),
    label: names.length <= 2 ? names.join(SEP) : t('{0} ほか{1}', names[0], names.length - 1),
  };
}

/** The selected folder joins the active swipe range after a successful add. */
export function scopeAfterFolderAdd(st: State, path: string, off: Set<string>): { off: Set<string>; tokens: string[]; label: string } {
  const roots = photoFolders(st);
  path = matchFolder(roots, path);
  if (path && !roots.includes(path)) roots.push(path);
  const nextOff = new Set([...off].filter((p) => roots.includes(p) && p !== path));
  return { off: nextOff, ...scopeOfOn(roots, nextOff) };
}

/** Adds a photo folder at the end without replacing existing folders. */
export async function addPhotoFolder(st: State, path: string): Promise<string> {
  const list = photoFolders(st);
  path = matchFolder(list, path);
  if (!path || list.includes(path)) return '';
  const depths = st.maxDepths ?? [];
  const next = [...list, path];
  try {
    const r = await postJ<{ ok: boolean; error?: string }>('/api/config', {
      root: next[0],
      more_roots: next.slice(1).join('|'),
      max_depths: next.map((_, i) => (i < list.length ? (depths[i] ?? -1) : -1)).join(','),
    });
    return r.ok ? '' : r.error ? server(r.error) : t('追加できませんでした');
  } catch {
    return t('追加できませんでした');
  }
}
