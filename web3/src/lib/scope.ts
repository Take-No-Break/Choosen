// Scope tokens (範囲), the same as V1 and /api/next|items read: a folder path ("a/b") = the folder
// with its subfolders, "a/b/" = only the images directly in it, "/" = only the images directly in the
// photo folder, "/*" = the first photo folder itself when there are several.
import { SEP, t } from './i18n';

export interface FolderRow {
  path: string;
  name: string;
  depth: number;
  total: number;
  liked: number;
}

export interface Entry {
  row: FolderRow;
  token: string;
  indent: number;
  top: boolean; // a photo folder itself (only shown when there are several)
}

/** The picker's rows: the folder tree in name order, photo folders on top when there are several. */
export function buildEntries(folders: FolderRow[]): Entry[] {
  const parentOf = (p: string) => (p.includes('/') ? p.slice(0, p.lastIndexOf('/')) : '');
  const kids = new Map<string, FolderRow[]>();
  for (const f of folders) {
    if (f.depth === 0) continue;
    const p = parentOf(f.path);
    if (!kids.has(p)) kids.set(p, []);
    kids.get(p)!.push(f);
  }
  for (const list of kids.values()) list.sort((a, b) => a.name.localeCompare(b.name, 'ja'));
  const tops = folders.filter((f) => f.depth === 0);
  const multi = tops.length > 1;
  const out: Entry[] = [];
  const walk = (p: string) => {
    for (const f of kids.get(p) ?? []) {
      out.push({ row: f, token: f.path, indent: f.depth - 1 + (multi ? 1 : 0), top: false });
      walk(f.path);
    }
  };
  if (multi)
    for (const t of tops) {
      out.push({ row: t, token: t.path === '' ? '/*' : t.path, indent: 0, top: true });
      walk(t.path);
    }
  else walk('');
  return out;
}

function nameOf(token: string, entries: Entry[]): string | null {
  if (token === '/') return t('写真フォルダの直下');
  const direct = token.endsWith('/') && token !== '/';
  const tok = direct ? token.slice(0, -1) : token;
  const e = entries.find((x) => x.token === tok || (tok !== '/*' && x.row.path === tok));
  if (!e) return null;
  return direct ? t('{0} の直下', e.row.name) : e.row.name;
}

/** "すべて", one or two names, or "A、B ほか3". Unknown tokens are named by their last part. */
export function scopeLabel(tokens: string[], entries: Entry[]): string {
  if (!tokens.length) return t('すべて');
  const names = tokens.map((tok) => nameOf(tok, entries) ?? tok.replace(/\/$/, '').split('/').pop() ?? tok);
  return names.length <= 2 ? names.join(SEP) : t('{0} ほか{1}', names.slice(0, 2).join(SEP), names.length - 2);
}

/** The tokens whose folders still exist (a saved condition may name a folder that was renamed). */
export const knownTokens = (tokens: string[], entries: Entry[]) => tokens.filter((t) => nameOf(t, entries) !== null);
