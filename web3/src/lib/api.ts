// The core's JSON routes (src/api_routes.cpp), same origin. POSTs carry X-Choosen: 1, which the
// server requires so other web pages cannot post to it.

export interface Card {
  id: number;
  fp: string;
  name: string;
  dir: string;
  ext: string;
  rated: boolean;
  p: number; // points
  n: number; // how often rated
  last: string; // latest rating letter (L, S, D) or ""
  nL?: number; // how many likes it had (absent: none)
  nS?: number; // how many super likes
  kind?: string;
  // a work's pages; grouping off, each page is its own card (perPage) and brings its own state, and
  // `at` is this card's place among them
  pages?: { id: number; fp: string; name: string; rated?: boolean; p?: number; n?: number; last?: string }[];
  perPage?: boolean;
  at?: number;
}

export interface State {
  version: number;
  instance: string;
  hasRoot: boolean;
  items: number;
  rated: number;
  fresh?: number;
  keyMode: number;
  mouseMode: number;
  reviewShare: number;
  swipeButtons?: boolean;
  customMouse?: string; // an own mouse layout saved by Version 1 (mode 4)
  artistDepth: number;
  remote?: boolean;
  scanning?: boolean;
  root?: string; // the first photo folder
  moreRoots?: string[]; // the further photo folders
  maxDepths?: number[]; // one subfolder limit per photo folder (-1 = all)
  overPoints?: boolean; // show the points above 100 as "100（+N）"
  skipPassed?: boolean; // a passed picture never comes back in the swipe order
}

/** Points as the normal page shows them: up to 100 as they are; above, "100（+N）" or just 100. */
export function fmtPts(p: number, over: boolean | undefined): string {
  const n = Math.round(Number(p) || 0);
  if (n <= 100) return String(n);
  return over === false ? '100' : `100（+${n - 100}）`;
}

// Inside the Android app (MainActivity): no server; a request body cannot pass through the WebView,
// so POSTs with a body go over the app's bridge, which answers "<status>\n<json>".
interface AndroidBridge {
  post(url: string, body: string): string;
  setUi(ui: string): void;
  openPc(addr: string, code: string): string; // "ok", or why not ("address")
  leavePc(): void;
  lastPc(): string;
  saveImage?(base64Jpeg: string, name: string): boolean; // into Pictures/Choosen (newer apps)
  canMove?(): boolean; // all files access is on (sorting into subfolders)
  askMove?(): void; // opens Android's page to switch it on
  mediaScan?(paths: string): void; // moved files: old and new paths, one per line
  iconColor?(): string; // the home screen icon: "blue" or "purple" (newer apps)
  setIconColor?(color: string): void;
}
const bridge = typeof window === 'undefined' ? undefined : (window as unknown as { ChoosenAndroid?: AndroidBridge }).ChoosenAndroid;
export const ANDROID = !!bridge;

// Inside the iPhone app (ios/): no server either. The page's requests are answered by the app
// (choosen://app/…); POSTs with a body, saving a picture and opening the Settings app go over
// its message bridge, which answers asynchronously.
interface IosBridge {
  postMessage(
    m:
      | { op: 'post'; url: string; body: string }
      | { op: 'saveImage'; data: string; name: string }
      | { op: 'openSettings' }
      | { op: 'iconColor' }
      | { op: 'setIconColor'; color: string },
  ): Promise<string>;
}
const ios =
  typeof window === 'undefined'
    ? undefined
    : (window as unknown as { webkit?: { messageHandlers?: { choosen?: IosBridge } } }).webkit?.messageHandlers?.choosen;
export const IOS = !!ios;
/** In a phone app (Android or iPhone) rather than a browser. */
export const PHONE_APP = ANDROID || IOS;
/** iPhone: opens this app's page in the Settings app (photo access). */
export const openIosSettings = () => void ios?.postMessage({ op: 'openSettings' }).catch(() => {});
/** Android: which page the app opens from now on ("v1" = the normal page). */
export const setAndroidUi = (ui: 'v1' | 'v3') => bridge?.setUi(ui);
/** Android: show the library of the PC at addr (its 6-digit code pairs a new phone). */
export const openPc = (addr: string, code: string) => bridge?.openPc(addr, code) ?? 'no';
/** Android: back from a PC's library to the phone's own. */
export const leavePc = () => bridge?.leavePc();
/** Android: whether pictures may be moved (all files access); never on iPhone (the Photos library
 * is only read); true elsewhere (a PC moves its own). */
export const canMove = () => (IOS ? false : bridge ? (bridge.canMove?.() ?? false) : true);
/** Android: opens the page that allows all files access (once). */
export const askMove = () => bridge?.askMove?.();
/** Android: tells the phone's gallery about moved files. */
export const mediaScan = (paths: string[]) => bridge?.mediaScan?.(paths.join('\n'));
/** The phone app's home screen icon colour ("" when the app cannot change it, e.g. a browser). */
export async function iconColor(): Promise<string> {
  if (ios) return ios.postMessage({ op: 'iconColor' }).catch(() => '');
  return bridge?.iconColor?.() ?? '';
}
export function setIconColor(c: 'blue' | 'purple') {
  if (ios) void ios.postMessage({ op: 'setIconColor', color: c }).catch(() => '');
  else bridge?.setIconColor?.(c);
}
/** Android: how the screen turns ("" when the app cannot choose). */
/** Android: the PC used last, "" if none. */
export const lastPc = () => bridge?.lastPc() ?? '';

/**
 * Saves a picture made here (a collage) on this device: the Android app puts it into the phone's
 * Pictures/Choosen, a browser downloads it. Nothing is sent anywhere. False if it could not.
 */
export async function saveImage(blob: Blob, name: string): Promise<boolean> {
  if (bridge || ios) {
    if (bridge && !bridge.saveImage) return false; // an older app
    const b64 = await new Promise<string>((done) => {
      const r = new FileReader();
      r.onload = () => done(String(r.result).replace(/^data:[^,]*,/, ''));
      r.onerror = () => done('');
      r.readAsDataURL(blob);
    });
    if (!b64) return false;
    if (ios) return (await ios.postMessage({ op: 'saveImage', data: b64, name }).catch(() => '')) === 'ok';
    return !!bridge?.saveImage?.(b64, name);
  }
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = name;
  document.body.append(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
  return true;
}

// A phone that is not paired gets 401 from every route: the app then shows the pairing screen.
let onUnpaired: (() => void) | null = null;
export function whenUnpaired(f: () => void) {
  onUnpaired = f;
}

export async function getJ<T>(path: string, query: Record<string, string | number> = {}): Promise<T> {
  // each=1: grouping off, a card still brings its work's pages (this page turns them per picture)
  const q = new URLSearchParams([...Object.entries(query).map(([k, v]) => [k, String(v)]), ['each', '1']]).toString();
  const r = await fetch(q ? `${path}?${q}` : path);
  if (r.status === 401) onUnpaired?.();
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json() as Promise<T>;
}

export async function postJ<T>(path: string, query: Record<string, string | number> = {}, body?: string): Promise<T> {
  const q = new URLSearchParams(Object.entries(query).map(([k, v]) => [k, String(v)])).toString();
  const url = q ? `${path}?${q}` : path;
  let r: Response;
  if ((bridge || ios) && body != null) {
    const res = bridge ? bridge.post(url, body) : await ios!.postMessage({ op: 'post', url, body });
    const nl = res.indexOf('\n');
    r = new Response(res.slice(nl + 1), { status: Number(res.slice(0, nl)), headers: { 'Content-Type': 'application/json' } });
  } else r = await fetch(url, { method: 'POST', headers: { 'X-Choosen': '1' }, body });
  if (r.status === 401) onUnpaired?.();
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json() as Promise<T>;
}

/** Pairs this phone with the 6-digit code shown on the PC; the server sets the token cookie. */
export async function pair(code: string): Promise<boolean> {
  const r = await fetch(`/api/pair?code=${encodeURIComponent(code)}`, { method: 'POST', headers: { 'X-Choosen': '1' } }).catch(() => null);
  return !!r?.ok;
}

/** A phone that scanned the PC's QR code opens ".../v3/#pair=123456". The code works once, so it
 * leaves the address either way. null: no code in the address. */
export async function pairFromLink(): Promise<boolean | null> {
  const m = /^#pair=(\d{6})$/.exec(location.hash);
  if (!m) return null;
  history.replaceState(null, '', location.pathname + location.search);
  return pair(m[1]);
}

// Phones get screen-fit pictures (/img/<id>?w=): a 24 MP original decodes to ~96 MB, too much for a
// phone. A few fixed widths, so the PC's cache serves every phone of the same class.
const FIT = [1280, 1600, 2048, 2560];
export const fitWidth = (screenLong: number, dpr: number) => FIT.find((b) => b >= screenLong * dpr) ?? FIT[FIT.length - 1];
let fitPx = 0;
export function useScreenFit(on: boolean) {
  fitPx = on ? fitWidth(Math.max(screen.width, screen.height), devicePixelRatio || 1) : 0;
}

export const imgUrl = (c: { id: number; fp: string }) => `/img/${c.id}?v=${c.fp}${fitPx ? `&w=${fitPx}` : ''}`;
export const thumbUrl = (c: { id: number; fp: string }, s: number) => `/thumb/${c.id}?s=${s}&v=${c.fp}`;

/** The folder shown above a card: "Artist (1)/sub" -> "Artist (1) › sub"; a further photo folder by name. */
export function placeOf(dir: string): string {
  const parts = dir.split('/');
  if (/[:\\]/.test(parts[0])) parts[0] = parts[0].split(/[\\/]/).filter(Boolean).pop() ?? parts[0];
  return parts.filter(Boolean).join(' › ');
}

/** Live state pushes from the server (server-sent events). The iPhone app has no event stream: the
 * page asks for the core's change counter instead and fetches the state when it moves. */
export function watchState(onState: (s: State) => void): () => void {
  if (IOS) {
    let seen = -1;
    let stopped = false;
    const tick = async () => {
      const v = await getJ<{ v: number }>('/api/version').catch(() => null);
      if (!stopped && v && v.v !== seen) {
        const s = await getJ<State>('/api/state').catch(() => null);
        if (s) {
          seen = v.v;
          if (!stopped) onState(s);
        }
      }
      if (!stopped) setTimeout(tick, 400);
    };
    tick();
    return () => void (stopped = true);
  }
  const es = new EventSource('/api/events');
  es.onmessage = (e) => onState(JSON.parse(e.data) as State);
  return () => es.close();
}
