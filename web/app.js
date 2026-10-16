'use strict';
// Choosen UI. Talks to the local C++ server; no frameworks so it starts instantly.

const $ = (s, el = document) => el.querySelector(s);
const $$ = (s, el = document) => [...el.querySelectorAll(s)];
const esc = (s) => String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
const qs = (o) => Object.entries(o)
  .filter(([, v]) => v !== undefined && v !== null && v !== '')
  .map(([k, v]) => `${k}=${encodeURIComponent(v)}`).join('&');
const num = (n) => Number(n).toLocaleString('ja-JP');

async function getJ(url) {
  const r = await fetch(url);
  if (r.status === 401) showPairing();
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json();
}
// In the Android app a request body cannot pass through the WebView: it goes over the app's bridge.
const ANDROID = typeof window.ChoosenAndroid !== 'undefined';
function androidPost(url, body) {
  const res = window.ChoosenAndroid.post(url, body);
  const nl = res.indexOf('\n');
  return new Response(res.slice(nl + 1), { status: Number(res.slice(0, nl)), headers: { 'Content-Type': 'application/json' } });
}
async function postJ(url, body) {
  const r = ANDROID && body != null ? androidPost(url, body) : await fetch(url, { method: 'POST', headers: { 'X-Choosen': '1' }, body });
  if (r.status === 401) showPairing();
  if (!r.ok) throw new Error(`HTTP ${r.status}`);
  return r.json();
}

// A phone that scanned the QR code on the PC opens ".../#pair=123456": it pairs with that code at
// once. The code works once, so it leaves the address either way.
let pairLinkFailed = false;
async function pairFromLink() {
  const m = /^#pair=(\d{6})$/.exec(location.hash);
  if (!m) return;
  history.replaceState(null, '', location.pathname + location.search);
  const r = await fetch('/api/pair?code=' + m[1], { method: 'POST', headers: { 'X-Choosen': '1' } }).catch(() => null);
  pairLinkFailed = !(r && r.ok);
}

// A phone that is not paired yet: ask for the 6-digit code shown on the PC.
function showPairing() {
  const m = document.getElementById('pairModal');
  if (!m || !m.classList.contains('hidden')) return;
  m.classList.remove('hidden');
  if (pairLinkFailed) document.getElementById('pairMsg').textContent = 'この QR コードは期限切れか使用済みです。PC でもう一度「スマホでスワイプ」を押してください。';
  document.getElementById('offline').classList.add('hidden');
  const input = document.getElementById('pairInput');
  const send = async () => {
    const code = input.value.replace(/\D/g, '');
    if (code.length !== 6) { document.getElementById('pairMsg').textContent = '6 桁の番号を入れてください'; return; }
    const r = await fetch('/api/pair?code=' + code, { method: 'POST', headers: { 'X-Choosen': '1' } }).catch(() => null);
    if (r && r.ok) { location.reload(); return; }
    document.getElementById('pairMsg').textContent = '番号が違うか、期限が切れています。PC で新しい番号を表示してください。';
    input.select();
  };
  document.getElementById('pairOk').onclick = send;
  input.onkeydown = (e) => { if (e.key === 'Enter') send(); };
  input.focus();
}
const store = {
  get(k, d) {
    try { const v = localStorage.getItem('choosen.' + k); return v == null ? d : JSON.parse(v); } catch { return d; }
  },
  set(k, v) { try { localStorage.setItem('choosen.' + k, JSON.stringify(v)); } catch { /* private mode */ } },
  del(k) { try { localStorage.removeItem('choosen.' + k); } catch { /* ignore */ } },
};

const LABEL = { L: '好評', S: '超好評', D: '見送り', K: '保留', A: 'アーカイブ', R: '元に戻す' };
const imgUrl = (it) => `/img/${it.id}?v=${it.fp}`;
const thumbUrl = (it, s = 320) => `/thumb/${it.id}?s=${s}&v=${it.fp}`;

// Points (like 67, +2 per further like; super 86, then +1 per like and +2 per super). Above 100
// they show as "100 (+N)" with the over-point display on, or just 100 with it off; sorting always
// uses the full value.
function fmtPts(p) {
  p = Math.round(Number(p) || 0);
  if (p <= 100) return String(p);
  return A.st && !A.st.overPoints ? '100' : `100（+${p - 100}）`;
}
// Likes as thumbs, super likes as stars, overlapping like a small stack; long runs as "×N".
function marks(it, max = 5) {
  const run = (n, cls, id) => {
    if (!n) return '';
    const icon = `<svg><use href="#${id}"/></svg>`;
    return `<span class="mk ${cls}" title="${cls === 'S' ? '超好評' : '好評'} ${n}回">${n <= max ? icon.repeat(n) : `${icon}<i>×${n}</i>`}</span>`;
  };
  return run(it && it.nS, 'S', 'i-super') + run(it && it.nL, 'L', 'i-like');
}

const A = {
  st: null,          // last server state
  tab: 'swipe',
  mouse: 1,          // mouse mode 1 drag / 2 wheel+click / 3 flick / 4 custom
  keys: 1,           // keyboard mode 1 (A) / 2 (B) / 3 (C) / 4 custom
  pending: {},       // setting changes not yet confirmed by the server: ignore stale echoes
  folders: [],
  fmap: new Map(),   // folder path -> folder
  foldersKey: '',
  foldersLoading: null,
  foldersLoadingKey: '',
  focusAt: 0,
  instanceAt: 0,     // when the current server instance was first seen
  sinceDays: store.get('rkSince', 0),  // ranking compares with this many days ago (0 = session start)
};

// ---------------------------------------------------------------- folders

function loadFolders(force) {
  const ver = A.st ? A.st.version : 0;
  const key = `${ver}/${A.sinceDays}`;
  if (A.foldersLoading) {
    if (A.foldersLoadingKey === key || !force) return A.foldersLoading;
    return A.foldersLoading.catch(() => {}).then(() => loadFolders(true));  // fetch again with the new key
  }
  if (!force && A.foldersKey === key && A.folders.length) return Promise.resolve();
  const since = A.sinceDays ? Date.now() - A.sinceDays * 86400000 : 0;
  A.foldersLoadingKey = key;
  A.foldersLoading = getJ('/api/folders?' + qs({ since })).then((r) => {
    A.folders = r.folders;
    A.fmap = new Map(r.folders.map((f) => [f.path, f]));
    A.foldersKey = key;
  }).finally(() => { A.foldersLoading = null; });
  return A.foldersLoading;
}

const parentOf = (p) => (p.includes('/') ? p.slice(0, p.lastIndexOf('/')) : '');
// Folder paths: the first photo folder is "" and its folders "Artist/sub"; a further photo folder
// is keyed by its full path ("D:\More", "D:\More/Artist"), which relative paths can never look like.
const isTopKey = (s) => /[:\\]/.test(s) && !s.includes('/');
const topOf = (p) => { const s = p.split('/')[0]; return isTopKey(s) ? s : ''; };
const multiRoot = () => !!(A.st && A.st.moreRoots && A.st.moreRoots.length);
const topName = (t) => (A.fmap.get(t) || { name: t }).name;
// "D:\More/Artist" -> "More/Artist"; with several photo folders the first one is named too.
const placeOf = (p) => {
  const t = topOf(p);
  const rest = t ? p.slice(t.length + 1) : p;
  return t || multiRoot() ? [topName(t), rest].filter(Boolean).join('/') : rest;
};
const artistPathOf = (dir) => {
  const d = A.st ? A.st.artistDepth : 1;
  if (!dir) return '';
  const segs = dir.split('/');
  return segs.slice(0, d + (isTopKey(segs[0]) ? 1 : 0)).join('/');
};
// Scope tokens besides folder paths: a trailing '/' = only the images directly in that folder
// ("/" for the first photo folder); "/*" = the whole first photo folder.
const ROOT_ONLY = '/';
const FIRST_ROOT = '/*';
const scopeKnown = (p) => p === ROOT_ONLY || p === FIRST_ROOT || A.fmap.has(p.endsWith('/') ? p.slice(0, -1) : p);
function scopeLabel(scope) {
  if (!scope.length) return '範囲: すべて';
  const names = scope.map((p) => {
    if (p === FIRST_ROOT) return topName('');
    if (p.endsWith('/')) return (multiRoot() ? topName(p.slice(0, -1)) + ' の' : '写真フォルダの') + '直下';
    return (A.fmap.get(p) || { name: p.split('/').pop() }).name;
  });
  return '範囲: ' + (names.length <= 2 ? names.join('、') : `${names.slice(0, 2).join('、')} ほか${names.length - 2}件`);
}

// ---------------------------------------------------------------- tabs + state

function showTab(t) {
  if (t !== 'swipe') flickOff();
  A.tab = t;
  $$('#tabs button').forEach((b) => b.classList.toggle('on', b.dataset.tab === t));
  $$('.view').forEach((v) => v.classList.toggle('on', v.id === 'view-' + t));
  if (t === 'ranking') renderRanking();
  // the gallery reloads when anything was rated or scanned since it was last shown
  if (t === 'gallery' && (!Ga.loaded || (A.st && Ga.ver !== A.st.version))) galleryReload();
  if (t === 'settings') fillSettings();
  if (t === 'swipe') renderSwipe();
  store.set('tab', t);
}

function onState(st) {
  const prev = A.st;
  // Only /api/state marks a phone; the event stream sends the core's state without it, so it stays.
  if (prev && prev.remote) st.remote = true;
  A.st = st;
  document.body.classList.toggle('remote', !!st.remote);  // on a phone: no settings, no Explorer
  if (!prev || prev.instance !== st.instance) A.instanceAt = Date.now();
  // A new server instance means item ids may have changed: start the swipe queue over.
  if (prev && prev.instance !== st.instance) {
    commitThrow();
    Sw.gen++;
    Sw.queue = [];
    Sw.hpos = -1;
    Ga.loaded = false;
    loadFolders(true).catch(() => {});
    Outbox.pump();
    renderSwipe();
    refill();
  }
  // Regrouped cards: history entries have the old shape, so drop them.
  if (prev && prev.groupWorks !== st.groupWorks) {
    commitThrow();
    Sw.hist = [];
    Sw.hpos = -1;
    Sw.page.clear();
    Sw.gen++;
    Sw.queue = [];
    Ga.loaded = false;
    refill();
  }
  // follow the server's modes only when they changed there (not a stale echo of our own change)
  if (!prev || prev.mouseMode !== st.mouseMode || prev.keyMode !== st.keyMode ||
      prev.customKeys !== st.customKeys || prev.customMouse !== st.customMouse) {
    if (!A.pending.mouse) A.mouse = st.mouseMode;
    if (!A.pending.keys) A.keys = st.keyMode || 1;
    if (A.mouse !== 3) flickOff();
    updateModeUI();
  }
  if (!prev || prev.reviewShare !== st.reviewShare) renderFilterLabels();
  if (prev && (prev.overPoints !== st.overPoints)) { renderInfo(); Ga.loaded = false; }
  if (!prev || prev.advanced !== st.advanced) document.body.classList.toggle('advanced', !!st.advanced);
  renderStatus();
  if (!st.hasRoot && A.tab !== 'settings') showTab('settings');
  if (A.tab === 'swipe' && !cur() && st.items > 0 && !Sw.filling) refill();
  if (prev && prev.scanning && !st.scanning) {
    loadFolders(true).then(() => { if (A.tab === 'ranking') renderRanking(); }).catch(() => {});
  }
  if (A.tab === 'settings') renderSettingsNotice();
}

function renderStatus() {
  const st = A.st;
  if (!st) return;
  let s = '';
  if (st.warning) s += `<span class="warn" title="${esc(st.warning)}">⚠ バックアップ注意</span> · `;
  if (Outbox.q.length > 1 || Outbox.failures) s += `<span class="warn">未送信 ${Outbox.q.length}件</span> · `;
  if (st.scanning) {
    const prog = st.toHash ? `新しい画像を登録中 ${num(st.hashed)} / ${num(st.toHash)}` : `フォルダを確認中 ${num(st.found)}`;
    s += `<span class="scan">● ${prog}</span> · `;
  }
  s += st.groupWorks ? `${num(st.items)}枚（${num(st.works)}作品） · 評価済 ${num(st.rated)}作品` : `${num(st.items)}枚 · 評価済 ${num(st.rated)}`;
  s += ` · 今回 ${num(st.session)}`;
  $('#status').innerHTML = s;
}

function connect() {
  const es = new EventSource('/api/events');
  es.onopen = () => $('#offline').classList.add('hidden');
  es.onmessage = (e) => onState(JSON.parse(e.data));
  // (an unpaired phone gets 401 here too: the pairing dialog explains that, not the offline banner)
  es.onerror = () => { if (es.readyState !== 1 && $('#pairModal').classList.contains('hidden')) $('#offline').classList.remove('hidden'); };
}

// ---------------------------------------------------------------- outbox
// Ratings leave through an outbox: one request at a time, in order, kept in localStorage until
// the server confirms it (nothing is lost if the server restarts or the window is closed).
// Each window has its own key; a new window adopts ratings that other windows left behind (the
// server ignores a rating it already has, so sending one twice is harmless).

const WIN = Math.random().toString(36).slice(2, 10);
const newCid = () => Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 10);
const Outbox = {
  q: [],
  busy: false,
  failures: 0,
  timer: 0,
  adopt() {
    const seen = new Set();
    const all = [];
    try {
      for (let i = localStorage.length - 1; i >= 0; i--) {
        const k = localStorage.key(i);
        if (!k || !(k === 'choosen.outbox' || k.startsWith('choosen.outbox.'))) continue;
        let arr = [];
        try { arr = JSON.parse(localStorage.getItem(k)) || []; } catch { arr = []; }
        for (const e of arr) if (e && e.cid && !seen.has(e.cid)) { seen.add(e.cid); all.push(e); }
        localStorage.removeItem(k);
      }
    } catch { /* storage unavailable */ }
    all.sort((a, b) => (a.t || 0) - (b.t || 0));
    this.q = all;
    this.save();
  },
  push(e) {
    e.t = Date.now();
    this.q.push(e);
    this.save();
    this.pump();
  },
  save() {
    if (this.q.length) store.set('outbox.' + WIN, this.q);
    else store.del('outbox.' + WIN);
    renderStatus();
  },
  later() {
    this.failures++;
    if (this.failures === 3) toast('<span class="down">評価を送れません。再接続したら自動で送ります</span>');
    this.timer = setTimeout(() => this.pump(), Math.min(15000, 500 * 2 ** Math.min(5, this.failures)));
  },
  async pump() {
    if (this.busy) return;
    this.busy = true;
    clearTimeout(this.timer);
    try {
      while (this.q.length) {
        const e = this.q[0];
        let res;
        try {
          res = await fetch('/api/rate?' + qs({ id: e.id, fp: e.fp, a: e.a, cid: e.cid, replace: e.replace }),
            { method: 'POST', headers: { 'X-Choosen': '1' } });
        } catch {
          res = null;
        }
        // away, disk trouble, or the same rating is still being written: keep it and retry later
        if (!res || res.status >= 500 || res.status === 409) { this.later(); break; }
        // right after a restart the server may not know a new file yet: wait for its scan
        if (res.status === 404 && A.st && (A.st.scanning || Date.now() - A.instanceAt < 60000)) { this.later(); break; }
        this.failures = 0;
        this.q.shift();
        this.save();
        if (res.ok) onRated(e, await res.json());
        else if (res.status === 404) toast('<span class="down">この画像は見つかりませんでした（移動または削除）</span>');
      }
    } finally {
      this.busy = false;
      renderStatus();
    }
  },
};

function onRated(e, r) {
  // history cards follow the server (points, counts); `kind` and the rating before this session's
  // (h.prev) stay as they were when the card was first shown
  for (const h of Sw.hist) if (h.item.fp === e.fp) Object.assign(h.item, r.item, { kind: h.item.kind });
  if (e.a === 'R') toast('元に戻しました');
  if (r.chain && r.chain.length) updateFolders(r.chain);
  Last.saved(e, r);
  const c = cur();
  if (c && c.fp === e.fp) renderInfo();
}

// ---------------------------------------------------------------- swipe

const PREFETCH = 6;
const Sw = {
  queue: [],        // upcoming cards from the scheduler
  hist: [],         // {item, a, cid} rated this session, oldest first
  hpos: -1,         // -1 = live; otherwise index into hist while going back
  pre: new Map(),   // fingerprint -> {img, broken, ready, readyP}
  page: new Map(),  // card fingerprint -> page index shown (multi-page works)
  filling: null,
  gen: 0,           // bumps when the scope changes
  throw: null,      // a card in the air: {a, card, h, timer}
  count: 0,
  counts: null,
  scope: store.get('swipeScope', []),
  filter: store.get('swipeFilter', ''),
  drag: null,
  wheelLock: 0,
};

const cur = () => (Sw.hpos >= 0 ? Sw.hist[Sw.hpos].item : Sw.queue[0]);
function nextShown() {
  if (Sw.hpos >= 0) return Sw.hpos + 1 < Sw.hist.length ? Sw.hist[Sw.hpos + 1].item : Sw.queue[0];
  return Sw.queue[1];
}

// Multi-page works (work grouping on): a card carries its pages; the page shown can be flipped.
const pagesOf = (it) => (it && it.pages && it.pages.length > 1 ? it.pages : null);
function shownOf(it) {
  const ps = pagesOf(it);
  if (!ps) return it;
  return ps[Math.min(Sw.page.get(it.fp) || 0, ps.length - 1)];
}
function flipPage(d) {
  if (Sw.throw) return;
  const c = cur();
  const ps = pagesOf(c);
  if (!ps) return;
  const k = Math.max(0, Math.min(ps.length - 1, (Sw.page.get(c.fp) || 0) + d));
  Sw.page.set(c.fp, k);
  place($('#cardTop'), c);
  if (ps[k + 1]) preload(ps[k + 1]);  // the next page is ready before it is asked for
  renderInfo();
}
function renderStrip(c) {
  const strip = $('#pageStrip');
  const ps = pagesOf(c);
  if (!ps) { strip.classList.add('hidden'); strip.dataset.card = ''; return; }
  const k = Sw.page.get(c.fp) || 0;
  if (strip.dataset.card !== c.fp) {
    strip.dataset.card = c.fp;
    strip.innerHTML = ps.map((p, i) => `<img loading="lazy" decoding="async" data-k="${i}" src="${thumbUrl(p, 96)}" alt="${i + 1}">`).join('');
  }
  $$('img', strip).forEach((im, i) => im.classList.toggle('on', i === k));
  const on = strip.children[k];
  if (on) on.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  strip.classList.remove('hidden');
}

function preload(it) {
  if (!it || Sw.pre.has(it.fp)) return;
  const img = new Image();
  img.decoding = 'async';
  img.draggable = false;
  img.alt = '';
  const entry = { img, broken: false, ready: false };
  img.onerror = () => {
    entry.broken = true;
    const c = cur();
    if (c && shownOf(c).fp === it.fp) renderSwipe();
  };
  img.src = imgUrl(it);
  entry.readyP = img.decode().catch(() => {}).then(() => { entry.ready = true; });
  Sw.pre.set(it.fp, entry);
}

// Measures what the owner feels: after a swipe, how long until the next image is decoded and
// visible (0 when the read-ahead was in time). Samples go to data\perf.log and the settings tab.
const Perf = {
  samples: [],
  late: 0,
  buf: [],
  acted() {
    const n = nextShown();
    const s = n && shownOf(n);
    const e = s && Sw.pre.get(s.fp);
    if (!e) return;
    const t0 = performance.now();
    const record = (ms, ready) => {
      this.samples.push(ms);
      if (this.samples.length > 5000) this.samples.shift();
      if (!ready) this.late++;
      this.buf.push(`${new Date().toISOString()}\t${ms}\t${ready ? 'ready' : 'waited'}\t${s.ext}\n`);
      if (this.buf.length >= 50) this.flush();
    };
    if (e.ready) record(0, true);
    else e.readyP.then(() => record(Math.round(performance.now() - t0), false));
  },
  flush() {
    if (!this.buf.length) return;
    const body = this.buf.join('');
    this.buf = [];
    if (ANDROID) { try { androidPost('/api/perf', body); } catch { /* timing samples are optional */ } return; }
    fetch('/api/perf', { method: 'POST', headers: { 'X-Choosen': '1' }, body, keepalive: true }).catch(() => {});
  },
  summary() {
    if (!this.samples.length) return 'まだ計測していません（スワイプすると記録されます）';
    const s = [...this.samples].sort((a, b) => a - b);
    const q = (p) => s[Math.min(s.length - 1, Math.floor(s.length * p))];
    return `次の画像までの待ち時間: 中央値 ${q(0.5)} ms · 95% ${q(0.95)} ms · 最大 ${s[s.length - 1]} ms（${num(s.length)}枚中 ${num(this.late)}枚は先読みが間に合わず）`;
  },
};

function gcPreload() {
  const keep = new Set(Sw.queue.map((i) => i.fp));
  Sw.hist.slice(-10).forEach((h) => keep.add(h.item.fp));
  if (Sw.hpos >= 0) {
    for (let i = Math.max(0, Sw.hpos - 3); i <= Math.min(Sw.hist.length - 1, Sw.hpos + 3); i++) keep.add(Sw.hist[i].item.fp);
  }
  for (const it of [cur(), nextShown()]) {
    const s = it && shownOf(it);
    if (s) keep.add(s.fp);
    const ps = pagesOf(it);
    const k = it ? Sw.page.get(it.fp) || 0 : 0;
    if (ps && ps[k + 1]) keep.add(ps[k + 1].fp);
  }
  for (const fp of Sw.pre.keys()) if (!keep.has(fp)) Sw.pre.delete(fp);
}

function refill() {
  if (Sw.filling) return Sw.filling;
  Sw.filling = (async () => {
    try {
      for (let guard = 0; guard < 5 && Sw.queue.length < PREFETCH; guard++) {
        const gen = Sw.gen;
        // never repeat a queued card or one whose rating is still on its way; just-seen cards only
        // when nothing else is left
        const exclude = [...Sw.queue.map((i) => i.id), ...Outbox.q.map((e) => e.id)];
        const recent = Sw.hist.slice(-5).map((h) => h.item.id);
        const r = await getJ('/api/next?' + qs({
          n: PREFETCH - Sw.queue.length, scope: Sw.scope.join('|'), filter: Sw.filter,
          exclude: exclude.join(','), recent: recent.join(','),
        }));
        if (gen !== Sw.gen) continue;
        Sw.counts = r.counts;
        const have = new Set(Sw.queue.map((i) => i.id));
        let added = 0;
        for (const it of r.items) {
          if (have.has(it.id)) continue;
          Sw.queue.push(it);
          preload(shownOf(it));
          added++;
        }
        if (!added) break;
      }
    } catch (e) {
      console.warn('refill failed', e);
    } finally {
      Sw.filling = null;
    }
    // never cut a throw or a drag short: their own end will render
    if (Sw.throw) return;
    if (Sw.drag) {
      const n = nextShown();
      place($('#cardUnder'), n && n !== cur() ? n : null);
      return;
    }
    renderSwipe();
  })();
  return Sw.filling;
}

// The "target" list says what it serves; the normal mix follows the review-share setting.
function renderFilterLabels() {
  const share = A.st ? A.st.reviewShare : 0.1;
  const every = { 0.05: 20, 0.067: 15, 0.1: 10, 0.2: 5, 0.35: 3, 0.5: 2 };
  const near = Object.keys(every).map(Number).reduce((a, b) => (Math.abs(b - share) < Math.abs(a - share) ? b : a), 0.1);
  const normal = share >= 0.999 ? '再評価だけ（未評価の画像は出さない）'
    : share <= 0.001 ? '未評価（なくなったら再評価）'
    : `未評価 ＋ 再評価（${every[near]}枚に1枚）`;
  const labels = { '': normal, fresh: '未評価だけ', liked: '好評・超好評を厳選（もう一度評価）', super: '超好評を厳選', passed: '見送りを見直す' };
  for (const o of $('#swFilter').options) if (labels[o.value] != null) o.textContent = labels[o.value];
}

function resetScope(scope, filter) {
  commitThrow();
  Sw.scope = scope;
  if (filter !== undefined) Sw.filter = filter;
  store.set('swipeScope', scope);
  store.set('swipeFilter', Sw.filter);
  Sw.gen++;
  Sw.queue = [];
  Sw.hpos = -1;
  $('#swScope').textContent = scopeLabel(scope);
  $('#swScope').classList.toggle('active', scope.length > 0);
  $('#swFilter').value = Sw.filter;
  renderSwipe();
  refill();
}

function place(card, cardItem) {
  const ph = card.querySelector('.ph');
  if (!cardItem) { ph.replaceChildren(); return; }
  const it = shownOf(cardItem);
  preload(it);
  const e = Sw.pre.get(it.fp);
  if (e.broken) {
    const d = document.createElement('div');
    d.className = 'broken';
    d.textContent = `表示できません: ${it.name}`;
    ph.replaceChildren(d);
  } else {
    ph.replaceChildren(e.img);
  }
}

function setStamps(on) {
  const s = $('#cardTop');
  for (const k of ['like', 'pass', 'super', 'archive']) $('.stamp.' + k, s).style.opacity = on[k] || 0;
}

function renderSwipe() {
  if (A.tab !== 'swipe') return;
  const top = $('#cardTop');
  top.style.transition = 'none';
  top.style.transform = '';
  top.style.opacity = '';
  $('#stage').classList.remove('moving');
  setStamps({});
  const c = cur();
  const n = nextShown();
  place(top, c);
  place($('#cardUnder'), n && (!c || n.id !== c.id) ? n : null);
  const empty = $('#empty');
  if (!c) {
    const st = A.st;
    let msg;
    if (!st || !st.hasRoot) msg = '<div class="big">写真フォルダが設定されていません</div>設定タブでフォルダを選んでください。';
    else if (st.scanning && !st.items) msg = '<div class="big">フォルダを読み込み中…</div>画像が見つかり次第ここに表示されます。';
    else if (Sw.filling) msg = '';
    else if (Sw.filter) msg = '<div class="big">この対象の画像はありません</div>「対象」か「範囲」を変えてください。';
    else msg = '<div class="big">この範囲に画像がありません</div>範囲を変更してください。';
    empty.innerHTML = msg;
    empty.classList.toggle('hidden', !msg);
  } else {
    empty.classList.add('hidden');
  }
  renderInfo();
  const cn = Sw.counts;
  $('#swCounts').textContent = !cn ? '' : Sw.filter && Sw.filter !== 'fresh'
    ? `この対象 ${num(cn.total)}枚 · 今回 ${num(Sw.count)}枚`
    : `未評価 ${num(cn.fresh)} · 再評価の候補 ${num(cn.due)} · この範囲 ${num(cn.total)}枚 · 今回 ${num(Sw.count)}枚`;
  gcPreload();
}

function renderInfo() {
  const c = cur();
  renderStrip(c);
  if (!c) { $('#swPath').innerHTML = ''; $('#swMeta').innerHTML = ''; return; }
  const place = placeOf(c.dir);
  const segs = place ? place.split('/') : [];
  const ad = (A.st ? A.st.artistDepth : 1) + (topOf(c.dir) || multiRoot() ? 1 : 0);  // photo folder name first
  const sep = ' <span class="sep">›</span> ';
  const pathHtml = segs.map((s, i) => (i === ad - 1 ? `<b>${esc(s)}</b>` : esc(s))).join(sep);
  $('#swPath').innerHTML = (pathHtml ? pathHtml + sep : '') + `<span class="file">${esc(shownOf(c).name)}</span>`;

  const tags = [];
  const ps = pagesOf(c);
  if (ps) tags.push(`<span class="tag pages">${(Sw.page.get(c.fp) || 0) + 1} / ${ps.length}ページ（, . でめくる）</span>`);
  const h = Sw.hpos >= 0 ? Sw.hist[Sw.hpos] : null;
  if (h) {
    tags.push(`<span class="tag back">↩ ${Sw.hist.length - Sw.hpos}枚前 · 今回の評価: ${LABEL[h.a] || ''}</span>`);
  } else if (c.kind === 'curate') {
    tags.push(`<span class="tag review">厳選 · ${c.n + 1}回目の評価</span>`);
  } else if (c.kind === 'hold') {
    tags.push('<span class="tag review">保留していた画像</span>');
  } else if (c.kind === 'new' || !c.rated) {
    tags.push('<span class="tag new">新規</span>');
  } else if (c.kind === 'ahead') {
    tags.push(`<span class="tag review">先取り再評価 · ${c.n + 1}回目</span>`);
  } else {
    tags.push(`<span class="tag review">再評価 · ${c.n + 1}回目</span>`);
  }
  // going back: the rating before this session's; otherwise the card's latest rating
  const prev = h ? h.prev : c.last;
  if (prev) tags.push(`<span class="tag ${prev}">${h ? 'その前' : '前回'}: ${LABEL[prev] || ''}</span>`);
  if (c.rated) tags.push(`<span class="tag pts">${marks(c)}${fmtPts(c.p)}点</span>`);
  const f = A.fmap.get(artistPathOf(c.dir));
  if (f && (f.depth > 0 || f.artist)) {
    tags.push(`<span class="tag">${esc(f.name)}: ${f.rank ? f.rank + '位 · ' : ''}スコア ${Number(f.score).toFixed(1)} · 評価 ${num(f.rated)}/${num(f.total)}</span>`);
  }
  $('#swMeta').innerHTML = tags.join('');
}

let toastTimer = 0;
function toast(html) {
  const t = $('#toast');
  t.innerHTML = html;
  t.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => t.classList.remove('show'), 1800);
}

function updateFolders(chain) {
  for (const x of chain) {
    const f = A.fmap.get(x.path);
    if (f) { f.score = x.after; f.rank = x.rank; f.rated = x.rated; f.total = x.total; }
  }
}

// The artist's score change of one rating, e.g. "Name 52.3 → 53.1 ▲ · rank 12 (↑2)".
function chainHtml(chain) {
  const ad = A.st ? A.st.artistDepth : 1;
  const f = chain && (chain.find((x) => x.artist) || chain.find((x) => x.depth === ad) || chain[0]);
  if (!f || (f.depth === 0 && !f.artist)) return '';
  const d = f.after - f.before;
  const dcls = d > 0.005 ? 'up' : d < -0.005 ? 'down' : '';
  const arrow = dcls === 'up' ? '▲' : dcls === 'down' ? '▼' : '';
  let rk = f.rank ? ` · ${f.rank}位` : '';
  if (f.rank && f.rankBefore && f.rank !== f.rankBefore) {
    rk += f.rank < f.rankBefore ? ` <span class="up">(↑${f.rankBefore - f.rank})</span>` : ` <span class="down">(↓${f.rank - f.rankBefore})</span>`;
  }
  return `${esc(f.name)} ${f.before.toFixed(1)} → <b>${f.after.toFixed(1)}</b> <span class="${dcls}">${arrow}</span>${rk}`;
}

// ---- the previous image's rating, always visible at the top right of the swipe tab
// "like" for a new image, "re-rated: pass → like" when a rated image comes back, "corrected: like → super"
// after going back and changing this session's rating. Drawn the moment the card lands; the
// points and the artist's score follow when the server has saved it.
const Last = {
  e: null,  // {cid, h, label, a, before}
  shown(h, a, label, before) {
    this.e = { cid: h.cid, h, a, label, before, after: null, chain: '' };
    this.render();
  },
  saved(e, r) {
    if (!this.e || this.e.cid !== e.cid) return;
    this.e.after = r.item ? r.item.p : null;
    this.e.item = r.item;
    this.e.chain = chainHtml(r.chain);
    this.render();
  },
  render() {
    const box = $('#lastRate');
    const x = this.e;
    if (!x) { box.classList.add('hidden'); return; }
    const it = x.item || x.h.item;
    const pts = x.a === 'A' || x.a === 'K' ? ''
      : x.after == null ? '<span class="muted">保存中…</span>'
      : x.before != null && x.before !== x.after ? `${fmtPts(x.before)} → <b>${fmtPts(x.after)}</b>点` : `<b>${fmtPts(x.after)}</b>点`;
    box.innerHTML = `<img src="${thumbUrl(shownOf(x.h.item), 96)}" alt="">
      <div class="lr-text"><div class="lr-main"><span class="lr-act ${x.a}">${x.label}</span>${x.after != null ? marks(it, 4) : ''}<span class="lr-pts">${pts}</span></div>
      <div class="lr-sub">${x.chain || (x.a === 'A' ? 'ギャラリーの「アーカイブ」から戻せます' : '')}</div></div>`;
    box.classList.toggle('pending', x.after == null && x.a !== 'A' && x.a !== 'K');
    box.classList.remove('hidden');
    box.title = 'クリックでこの画像に戻る';
  },
};

function sendRate(h, a) {
  const cid = newCid();
  Outbox.push({ id: h.item.id, fp: h.item.fp, a, cid, replace: h.cid || '' });
  h.cid = cid;
  h.a = a;
}

// ---- throws
// A thrown card stays "in the air" for the configured time: another direction redirects it, the
// same direction again confirms it at once, "back" cancels it. Then it is committed.

const DIR = { L: 'right', D: 'left', S: 'up', K: 'down', A: 'archive' };
const STAMP = { L: 'like', D: 'pass', S: 'super', A: 'archive' };

function flyTo(a, ms) {
  const top = $('#cardTop');
  const W = innerWidth, H = innerHeight;
  const to = {
    right: `translate(${W}px, 60px) rotate(20deg)`,
    left: `translate(${-W}px, 60px) rotate(-20deg)`,
    up: `translate(0, ${-H}px) scale(.9)`,
    down: `translate(0, ${H * 0.25}px) scale(.85)`,
    archive: `translate(0, ${H}px) scale(.6) rotate(8deg)`,
  }[DIR[a]];
  setStamps({ [STAMP[a]]: 1 });
  $('#stage').classList.add('moving');
  top.style.transition = `transform ${ms}ms cubic-bezier(.2,.75,.3,1), opacity ${ms}ms ease-in`;
  top.style.transform = to;
  top.style.opacity = a === 'K' || a === 'A' ? '0' : '1';
}

// Fast / Slow (or the exact time in advanced mode), as the server resolved it.
function throwMs() {
  const d = A.st ? A.st.throwMs : 0;
  return Math.max(140, d || 0);
}
// Fast: a throw is final at once; the short flight is only the animation.
function redirectable() { return !!(A.st && A.st.throwMs > 0); }

// a: L like (right), D pass (left), S super like (up), K hold, A archive (never show again)
function act(a) {
  if (Sw.throw) {
    if (Sw.throw.a !== a && redirectable()) {  // changed its mind: redirect in the air
      Sw.throw.a = a;
      clearTimeout(Sw.throw.timer);
      flyTo(a, throwMs());
      Sw.throw.timer = setTimeout(commitThrow, throwMs());
      return;
    }
    // The same direction again lands this card and rates the next one too, so a fast run of
    // ← ← ← rates one image per press.
    commitThrow();
  }
  const c = cur();
  if (!c) return;
  Perf.acted();
  Sw.throw = { a, card: c, h: Sw.hpos >= 0 ? Sw.hist[Sw.hpos] : null, timer: 0 };
  flyTo(a, throwMs());
  Sw.throw.timer = setTimeout(commitThrow, throwMs());
}

function commitThrow() {
  const t = Sw.throw;
  if (!t) return;
  Sw.throw = null;
  clearTimeout(t.timer);
  const before = t.card.rated ? t.card.p : null;
  if (t.h) {  // re-rating a card from history: replaces exactly that entry's earlier rating
    const was = t.h.a;
    sendRate(t.h, t.a);
    const k = Sw.hist.indexOf(t.h);
    Sw.hpos = k >= 0 && k + 1 < Sw.hist.length ? k + 1 : -1;
    Last.shown(t.h, t.a, was && was !== t.a ? `訂正：${LABEL[was]} → ${LABEL[t.a]}` : LABEL[t.a], null);
  } else {
    const k = Sw.queue.indexOf(t.card);  // that exact card, even if the queue changed meanwhile
    if (k >= 0) Sw.queue.splice(k, 1);
    // prev: the rating this image had before this session's (for "re-rated: A → B" and going back)
    const h = { item: t.card, a: '', cid: '', prev: t.card.last || '' };
    Sw.hist.push(h);
    if (Sw.hist.length > 400) Sw.hist.shift();
    sendRate(h, t.a);
    Sw.count++;
    const again = h.prev && t.a !== 'A' && t.a !== 'K';
    Last.shown(h, t.a, again ? `再評価：${LABEL[h.prev]} → ${LABEL[t.a]}` : LABEL[t.a], before);
    refill();
  }
  renderSwipe();
}

function cancelThrow() {
  const t = Sw.throw;
  if (!t) return false;
  Sw.throw = null;
  clearTimeout(t.timer);
  const top = $('#cardTop');
  top.style.transition = 'transform 180ms ease-out, opacity 180ms ease-out';
  top.style.transform = '';
  top.style.opacity = '';
  setStamps({});
  setTimeout(() => { if (!Sw.drag && !Sw.throw) $('#stage').classList.remove('moving'); }, 190);
  return true;
}

function back() {
  if (!redirectable()) commitThrow();  // delay off: the throw already counts; go back to it like any other
  else if (cancelThrow()) return;      // "back" while a card is in the air takes the throw back
  if (!Sw.hist.length) return;
  if (Sw.hpos === -1) Sw.hpos = Sw.hist.length - 1;
  else if (Sw.hpos > 0) Sw.hpos--;
  else return;
  renderSwipe();
}

function forward() {
  commitThrow();
  if (Sw.hpos === -1) return;
  Sw.hpos++;
  if (Sw.hpos >= Sw.hist.length) Sw.hpos = -1;
  renderSwipe();
}

function reveal(it) {
  if (it && !(A.st && A.st.remote)) postJ('/api/reveal?' + qs({ id: it.id, fp: it.fp })).catch(() => {});
}

// ---- mouse / pointer on the stage
// Every mouse mode is a table from input to command: modes 1-3 are fixed, mode 4 is the owner's
// own (settings → "create bindings").

const MOUSE_INPUTS = ['left', 'right', 'middle', 'wheelUp', 'wheelDown', 'side1', 'side2'];
const BUTTON_INPUT = ['left', 'middle', 'right', 'side1', 'side2'];  // index = PointerEvent.button
// There is no "hold" (rate now or pass); a wheel click that used to hold does nothing.
const MOUSE_PRESETS = {
  1: { left: 'grab', right: 'grab', middle: 'none', wheelUp: 'back', wheelDown: 'fwd', side1: 'back', side2: 'fwd' },
  2: { left: 'super', right: 'back', middle: 'pass', wheelUp: 'like', wheelDown: 'pass', side1: 'archive', side2: 'fwd' },
  3: { left: 'back', right: 'flick', middle: 'none', wheelUp: 'back', wheelDown: 'fwd', side1: 'back', side2: 'fwd' },
};
const RATINGS = new Set(['like', 'super', 'pass', 'archive', 'likeHold']);

// "name:value,value;name:value" as stored in config.txt (custom_keys / custom_mouse)
function parseBindings(s) {
  const out = {};
  for (const part of (s || '').split(';')) {
    const i = part.indexOf(':');
    if (i > 0) out[part.slice(0, i)] = part.slice(i + 1).split(',').filter(Boolean);
  }
  return out;
}
function customMouse() {
  const b = parseBindings(A.st && A.st.customMouse);
  const m = { ...MOUSE_PRESETS[1] };
  for (const k of MOUSE_INPUTS) if (b[k] && b[k][0]) m[k] = b[k][0] === 'hold' ? 'none' : b[k][0];  // saved before hold was removed
  return m;
}
const mouseMap = () => (A.mouse === 4 ? customMouse() : MOUSE_PRESETS[A.mouse] || MOUSE_PRESETS[1]);

function onPointerDown(e) {
  if (A.tab !== 'swipe' || modalOpen()) return;
  const input = BUTTON_INPUT[e.button];
  const cmd = input && mouseMap()[input];
  if (!cmd || cmd === 'none') return;
  e.preventDefault();
  if (cmd === 'grab') return startDrag(e);
  // the click that focused the window does nothing else (mode 3: a left click goes back)
  if (performance.now() - A.focusAt < 250) return;
  runCommand(cmd);
}

function startDrag(e) {
  if (Fl.on) return;
  commitThrow();
  if (!cur()) return;
  const stage = $('#stage');
  try { stage.setPointerCapture(e.pointerId); } catch { /* synthetic pointer */ }
  stage.classList.add('dragging', 'moving');
  $('#cardTop').style.transition = 'none';
  Sw.drag = { id: e.pointerId, x0: e.clientX, y0: e.clientY, dx: 0, dy: 0, vx: 0, vy: 0, lx: e.clientX, ly: e.clientY, lt: performance.now() };
}

function dragDir(d) {
  const tx = Math.max(110, innerWidth * 0.12), ty = Math.max(110, innerHeight * 0.15);
  const vertical = Math.abs(d.dy) > Math.abs(d.dx);
  return { tx, ty, up: vertical && d.dy < 0, down: vertical && d.dy > 0 };
}

function onPointerMove(e) {
  const d = Sw.drag;
  if (!d || e.pointerId !== d.id) return;
  const now = performance.now();
  const dt = Math.max(1, now - d.lt);
  d.vx = 0.7 * d.vx + 0.3 * ((e.clientX - d.lx) / dt);
  d.vy = 0.7 * d.vy + 0.3 * ((e.clientY - d.ly) / dt);
  d.lx = e.clientX; d.ly = e.clientY; d.lt = now;
  d.dx = e.clientX - d.x0;
  d.dy = e.clientY - d.y0;
  $('#cardTop').style.transform = `translate(${d.dx}px, ${d.dy}px) rotate(${d.dx * 0.03}deg)`;
  const { tx, ty, up, down } = dragDir(d);
  setStamps({
    like: !up && !down && d.dx > 0 ? Math.min(1, d.dx / tx) : 0,
    pass: !up && !down && d.dx < 0 ? Math.min(1, -d.dx / tx) : 0,
    super: up ? Math.min(1, -d.dy / ty) : 0,
    archive: down ? Math.min(1, d.dy / ty) : 0,
  });
}

function onPointerUp(e) {
  const d = Sw.drag;
  if (!d || e.pointerId !== d.id) return;
  Sw.drag = null;
  $('#stage').classList.remove('dragging');
  const { tx, ty, up, down } = dragDir(d);
  if (up && (-d.dy > ty || (d.vy < -0.8 && -d.dy > 40))) return act('S');
  if (down && (d.dy > ty || (d.vy > 0.8 && d.dy > 40))) return act('A');
  if (!up && !down && (d.dx > tx || (d.vx > 0.8 && d.dx > 40))) return act('L');
  if (!up && !down && (d.dx < -tx || (d.vx < -0.8 && d.dx < -40))) return act('D');
  const top = $('#cardTop');
  top.style.transition = 'transform 180ms ease-out';
  top.style.transform = '';
  setStamps({});
  setTimeout(() => { if (!Sw.drag && !Sw.throw) $('#stage').classList.remove('moving'); }, 190);
}

function onWheel(e) {
  if (A.tab !== 'swipe' || modalOpen()) return;
  e.preventDefault();
  if (Math.abs(e.deltaY) < 1 || e.ctrlKey) return;  // ctrl = pinch zoom on a touchpad
  const cmd = mouseMap()[e.deltaY < 0 ? 'wheelUp' : 'wheelDown'];
  if (!cmd || cmd === 'none') return;
  const now = performance.now();
  if (!RATINGS.has(cmd)) {  // going back / forward may repeat while scrolling
    if (now < Sw.wheelLock) return;
    Sw.wheelLock = now + 150;
    runCommand(cmd);
    return;
  }
  // Rating with the wheel: one flick = one rating. The lock only ends after the wheel has been
  // still for a moment, so a free-spinning wheel cannot rate several images by accident.
  const locked = now < Sw.wheelLock;
  Sw.wheelLock = now + 220;
  if (!locked) runCommand(cmd);
}

// ---- flick (mouse mode 3)
// A right click starts it and another right click (or Esc) ends it; a left click goes back to the
// previous image, also while flicking. In between, a quick flick of the mouse (or of one finger on
// a touchpad) rates without holding a button: right like, left pass, up super like, down archive.
// The pointer is locked to the window, so it never runs into the screen edge.
// Movement "leaks away" (time constant FLICK_LEAK), so slow movement never adds up to a throw;
// after a throw the next flick waits until the pointer has been still for a moment, so the hand
// moving back is not read as a flick the other way.
const FLICK_T = 80;        // px of (leaking) movement that make a flick
const FLICK_LEAK = 100;    // ms
const FLICK_QUIET = 250;   // ms after a throw before the next flick can count
const FLICK_STILL = 120;   // ms without movement that re-arm it
const Fl = { on: false, x: 0, y: 0, t: 0, quiet: 0, raf: 0 };

function toggleFlick() { if (Fl.on) flickOff(); else flickOn(); }
function flickOn() {
  if (Fl.on || A.tab !== 'swipe') return;
  Fl.on = true;
  Fl.x = Fl.y = 0;
  Fl.t = performance.now();
  Fl.quiet = 0;
  const stage = $('#stage');
  stage.classList.add('flicking');
  try {
    const p = stage.requestPointerLock();
    if (p && p.catch) p.catch(() => {});  // without the lock the pointer can reach the edge; flicks still work
  } catch { /* same */ }
  $('#flickBadge').classList.remove('hidden');
}
function flickOff() {
  if (!Fl.on) return;
  Fl.on = false;
  cancelAnimationFrame(Fl.raf);
  $('#stage').classList.remove('flicking');
  $('#flickBadge').classList.add('hidden');
  if (document.pointerLockElement) document.exitPointerLock();
  if (!Sw.throw && !Sw.drag) {
    const top = $('#cardTop');
    top.style.transition = 'transform 160ms ease-out';
    top.style.transform = '';
    setStamps({});
    $('#stage').classList.remove('moving');
  }
}
function flickLeak(now) {
  const k = Math.exp(-Math.max(0, now - Fl.t) / FLICK_LEAK);
  Fl.x *= k;
  Fl.y *= k;
  Fl.t = now;
}
function onFlickMove(e) {
  if (!Fl.on) return;
  if (A.tab !== 'swipe' || modalOpen()) { flickOff(); return; }
  const now = performance.now();
  const mx = e.movementX || 0, my = e.movementY || 0;
  if (now < Fl.quiet || Sw.throw) {
    if (Math.abs(mx) + Math.abs(my) > 2) Fl.quiet = Math.max(Fl.quiet, now + FLICK_STILL);  // still moving
    Fl.x = Fl.y = 0;
    Fl.t = now;
    return;
  }
  flickLeak(now);
  Fl.x += mx;
  Fl.y += my;
  const ax = Math.abs(Fl.x), ay = Math.abs(Fl.y);
  if (Math.max(ax, ay) >= FLICK_T && cur()) {
    const a = ax >= ay ? (Fl.x > 0 ? 'L' : 'D') : (Fl.y < 0 ? 'S' : 'A');
    Fl.x = Fl.y = 0;
    Fl.quiet = now + Math.max(FLICK_QUIET, throwMs());
    cancelAnimationFrame(Fl.raf);
    act(a);
    return;
  }
  flickDraw();
}
// The card leans the way the pointer moves and settles back as the movement leaks away.
function flickDraw() {
  cancelAnimationFrame(Fl.raf);
  if (!Fl.on || Sw.throw || Sw.drag || !cur()) return;
  const top = $('#cardTop');
  const ax = Math.abs(Fl.x), ay = Math.abs(Fl.y);
  const f = (v) => Math.min(1, v / FLICK_T);
  top.style.transition = 'none';
  top.style.transform = `translate(${Fl.x * 1.6}px, ${Fl.y * 1.6}px) rotate(${Fl.x * 0.05}deg)`;
  setStamps({
    like: ax >= ay && Fl.x > 0 ? f(ax) : 0,
    pass: ax >= ay && Fl.x < 0 ? f(ax) : 0,
    super: ay > ax && Fl.y < 0 ? f(ay) : 0,
    archive: ay > ax && Fl.y > 0 ? f(ay) : 0,
  });
  $('#stage').classList.toggle('moving', ax + ay > 1);
  if (ax + ay > 0.5) Fl.raf = requestAnimationFrame(() => { flickLeak(performance.now()); flickDraw(); });
  else { top.style.transform = ''; setStamps({}); }
}

// ---- keyboard
// Layouts A-C plus the owner's own (4). "likeHold" = like on a short press, super like when held.
// No layout has "forward" or "hold": rating now beats putting it off (the mouse modes keep both).
// Keys a layout leaves free (↑ in A and B, S in C) stay free on purpose.

const KEYMAPS = {
  1: { ArrowRight: 'like', ArrowLeft: 'pass', ArrowUp: 'super', ArrowDown: 'back', Enter: 'archive', NumpadEnter: 'archive', Delete: 'archive' },
  2: { ArrowRight: 'likeHold', ArrowLeft: 'pass', ArrowUp: 'archive', ArrowDown: 'back', Delete: 'archive' },
  3: { KeyD: 'like', KeyA: 'pass', KeyW: 'super', ShiftLeft: 'back', KeyE: 'archive', Delete: 'archive' },  // E here, not Explorer
};
const NO_KEY_COMMANDS = new Set(['hold', 'fwd']);  // never on a key, not even in a saved own layout
const COMMON_KEYS = {
  KeyE: 'reveal', Comma: 'pgPrev', BracketLeft: 'pgPrev', PageUp: 'pgPrev', Period: 'pgNext',
  BracketRight: 'pgNext', PageDown: 'pgNext', Home: 'pgFirst', End: 'pgLast',
};
const REPEATABLE = new Set(['back', 'fwd', 'pgPrev', 'pgNext']);
const RESERVED_KEYS = new Set(['KeyH', 'Escape']);  // help and "close" work in every layout

// Layout 4: "like:ArrowRight,KeyL;pass:ArrowLeft;..." from the settings; B until one is saved.
let customKeyCache = { src: null, map: null };
function customKeymap() {
  const src = (A.st && A.st.customKeys) || '';
  if (customKeyCache.src !== src) {
    const map = {};
    for (const [cmd, codes] of Object.entries(parseBindings(src))) {
      if (NO_KEY_COMMANDS.has(cmd)) continue;
      for (const c of codes) if (!RESERVED_KEYS.has(c)) map[c] = cmd;
    }
    customKeyCache = { src, map: Object.keys(map).length ? map : { ...KEYMAPS[2] } };
  }
  return customKeyCache.map;
}
const keymap = () => (A.keys === 4 ? customKeymap() : KEYMAPS[A.keys] || KEYMAPS[1]);

const Press = { code: '', timer: 0, fired: false };
function pressStart(code) {
  if (Press.code) return;
  Press.code = code;
  Press.fired = false;
  const ms = (A.st && A.st.holdMs) || 400;
  const stage = $('#stage');
  stage.style.setProperty('--hold', ms + 'ms');
  stage.classList.add('charging');
  Press.timer = setTimeout(() => {
    Press.fired = true;
    stage.classList.remove('charging');
    act('S');
  }, ms);
}
function pressEnd(code, cancel) {
  if (!Press.code || (code && Press.code !== code)) return;
  clearTimeout(Press.timer);
  $('#stage').classList.remove('charging');
  const fired = Press.fired;
  Press.code = '';
  if (!fired && !cancel) act('L');
}

function runCommand(cmd) {
  switch (cmd) {
    case 'like': return act('L');
    case 'pass': return act('D');
    case 'super': return act('S');
    case 'archive': return act('A');
    case 'back': return back();
    case 'fwd': return forward();
    case 'flick': return toggleFlick();
    case 'reveal': return reveal(shownOf(cur()));
    case 'pgPrev': return flipPage(-1);
    case 'pgNext': return flipPage(1);
    case 'pgFirst': return flipPage(-1e6);
    case 'pgLast': return flipPage(1e6);
    default: return undefined;
  }
}

function onKey(e) {
  if (e.key === 'Escape') {  // before the input check: Esc also closes a dialog whose search box has focus
    if (!$('#lightbox').classList.contains('hidden')) lbClose();
    else if (modalOpen()) closeModals();
    return;
  }
  if (e.target instanceof Element && e.target.matches('input, textarea, select')) return;
  if (!$('#lightbox').classList.contains('hidden')) return lbKey(e);
  if (e.code === 'KeyH' || e.key === '?') { toggleHelp(); e.preventDefault(); return; }
  if (A.tab !== 'swipe' || modalOpen()) return;
  const cmd = keymap()[e.code] || COMMON_KEYS[e.code];
  if (!cmd) return;
  e.preventDefault();
  if (cmd === 'likeHold') { if (!e.repeat) pressStart(e.code); return; }
  if (e.repeat && !REPEATABLE.has(cmd)) return;
  runCommand(cmd);
}

function onKeyUp(e) {
  if (Press.code && e.code === Press.code) { e.preventDefault(); pressEnd(e.code, false); }
}

// ---- controls (mouse + keyboard layouts), help and the owner's own bindings

const CMD_LABEL = {
  like: '好評', likeHold: '好評（長押しで超好評）', super: '超好評', pass: '見送り', archive: 'アーカイブ',
  back: '前の画像に戻る', fwd: '進む', grab: 'つかんで投げる', flick: 'フリックの開始 / 終了', none: 'なし',
};
const INPUT_LABEL = {
  left: '左ボタン', right: '右ボタン', middle: 'ホイールを押す', wheelUp: 'ホイール ↑', wheelDown: 'ホイール ↓',
  side1: 'サイドボタン 戻る', side2: 'サイドボタン 進む',
};
const KEY_NAMES = {
  ArrowRight: '→', ArrowLeft: '←', ArrowUp: '↑', ArrowDown: '↓', Space: 'Space', Enter: 'Enter', NumpadEnter: 'テンキー Enter',
  Delete: 'Delete', Backspace: 'Backspace', Tab: 'Tab', Insert: 'Insert', Home: 'Home', End: 'End', PageUp: 'PageUp',
  PageDown: 'PageDown', ShiftLeft: '左 Shift', ShiftRight: '右 Shift', ControlLeft: '左 Ctrl', ControlRight: '右 Ctrl',
  AltLeft: '左 Alt', AltRight: '右 Alt',
};
// e.code names the key's position; the character printed on it comes from the keyboard layout
// Windows uses (Japanese, Brazilian, US...), read once at start.
let keyLayout = null;
function loadKeyLayout() {
  try {
    if (navigator.keyboard && navigator.keyboard.getLayoutMap) {
      navigator.keyboard.getLayoutMap().then((m) => { keyLayout = m; updateModeUI(); }).catch(() => {});
    }
  } catch { /* not available: fall back to the code names */ }
}
function keyName(code) {
  if (KEY_NAMES[code]) return KEY_NAMES[code];
  let m = /^Numpad(.+)$/.exec(code);
  if (m) return 'テンキー ' + m[1];
  const ch = keyLayout && keyLayout.get(code);
  if (ch) return ch.toUpperCase();
  m = /^Key([A-Z])$/.exec(code) || /^Digit(\d)$/.exec(code);
  return m ? m[1] : code;
}
const kbd = (code) => `<kbd>${esc(keyName(code))}</kbd>`;

const CONTROLS = {
  mouse: {
    1: { title: 'マウス モード1 — つかんで投げる', note: '左ボタンか右ボタンを押したまま画像をつかんで投げます。', rows: [
      ['右へ投げる', '好評'], ['左へ投げる', '見送り'], ['上へ投げる', '超好評'], ['下へ投げる', 'アーカイブ'],
      ['ホイール ↑', '前の画像に戻る（再評価）'], ['ホイール ↓', '進む'],
      ['サイドボタン 戻る / 進む', '戻る / 進む']] },
    2: { title: 'マウス モード2 — ホイールとクリック', note: '', rows: [
      ['ホイール ↑', '好評'], ['ホイール ↓', '見送り'], ['左クリック', '超好評'], ['右クリック', '前の画像に戻る'],
      ['ホイールを押す', '見送り'], ['サイドボタン 戻る', 'アーカイブ'], ['サイドボタン 進む', '進む']] },
    3: { title: 'マウス モード3 — フリック（試作）', note: '右クリックでフリックが始まり、もう一度右クリック（または Esc）で終わります。ボタンを押さずに、動かす勢いで評価します。タッチパッドは一本指で。フリック中はマウスの矢印が消えます。', rows: [
      ['右へはじく', '好評'], ['左へはじく', '見送り'], ['上へはじく', '超好評'], ['下へはじく', 'アーカイブ'],
      ['右クリック', 'フリックの開始 / 終了'], ['左クリック', '前の画像に戻る'], ['ホイール ↑ / ↓', '戻る / 進む']] },
  },
  keys: {
    1: { title: 'キーボード A — これまでの配置', note: '', rows: [
      ['<kbd>→</kbd>', '好評'], ['<kbd>←</kbd>', '見送り'], ['<kbd>↑</kbd>', '超好評'],
      ['<kbd>↓</kbd>', '前の画像に戻る'], ['<kbd>Enter</kbd> / <kbd>Delete</kbd>', 'アーカイブ']] },
    2: { title: 'キーボード B — 矢印キー', note: '', rows: [
      ['<kbd>→</kbd>', '好評'], ['<kbd>→</kbd> 長押し', '超好評'], ['<kbd>←</kbd>', '見送り'], ['<kbd>↓</kbd>', '前の画像に戻る'],
      ['<kbd>↑</kbd> / <kbd>Delete</kbd>', 'アーカイブ']] },
    3: { title: 'キーボード C — W / A / D（ゲームと同じ）', note: '日本語入力がオンでも使えます。', rows: [
      ['<kbd>D</kbd>', '好評'], ['<kbd>A</kbd>', '見送り'], ['<kbd>W</kbd>', '超好評'], ['<kbd>左 Shift</kbd>', '前の画像に戻る'],
      ['<kbd>E</kbd> / <kbd>Delete</kbd>', 'アーカイブ']] },
  },
  common: [['<kbd>,</kbd> <kbd>.</kbd>', '作品のページをめくる'], ['<kbd>E</kbd>', 'エクスプローラーで表示（C 以外）'], ['<kbd>H</kbd>', '操作一覧']],
};
const KEY_ACTIONS = ['likeHold', 'like', 'super', 'pass', 'archive', 'back'];
const MOUSE_ACTIONS_BUTTON = ['grab', 'flick', 'like', 'super', 'pass', 'archive', 'back', 'fwd', 'none'];
const MOUSE_ACTIONS_OTHER = ['like', 'super', 'pass', 'archive', 'back', 'fwd', 'none'];

// The custom layouts' rows, built from what is saved (or from B / mode 1 until something is).
function customKeyRows() {
  const byCmd = {};
  for (const [code, cmd] of Object.entries(customKeymap())) (byCmd[cmd] = byCmd[cmd] || []).push(code);
  return KEY_ACTIONS.filter((c) => byCmd[c]).map((c) => [byCmd[c].map(kbd).join(' '), CMD_LABEL[c]]);
}
function customMouseRows() {
  const m = customMouse();
  return MOUSE_INPUTS.filter((k) => m[k] !== 'none').map((k) => [INPUT_LABEL[k], CMD_LABEL[m[k]] || m[k]]);
}
function controlSets() {
  return {
    mouse: { ...CONTROLS.mouse, 4: { title: 'マウス — 自分で割り当てる', note: '', rows: customMouseRows(), custom: !!(A.st && A.st.customMouse) } },
    keys: { ...CONTROLS.keys, 4: { title: 'キーボード D — 自分で割り当てる', note: '', rows: customKeyRows(), custom: !!(A.st && A.st.customKeys) } },
  };
}

function controlsHtml() {
  const sets = controlSets();
  const dl = (rows) => `<dl>${rows.map(([k, v]) => `<dt>${k}</dt><dd>${v}</dd>`).join('')}</dl>`;
  const panel = (kind, id, c, on) => {
    const edit = id === '4' ? `<button class="btn ghost bind-edit" data-edit="${kind}">${c.custom ? '編集' : '割り当てを作る'}</button>` : '';
    const empty = id === '4' && !c.custom ? '<p class="hint">まだ作っていません。「割り当てを作る」から好きなキーやボタンを決められます。</p>' : '';
    return `<section class="ctl${on ? ' current' : ''}" data-kind="${kind}" data-id="${id}" tabindex="-1" title="クリックで選ぶ">
      <h3>${on ? '● ' : '○ '}${c.title}${on ? '（使用中）' : ''}</h3>${c.note ? `<p class="hint">${c.note}</p>` : ''}${empty || dl(c.rows)}${edit}</section>`;
  };
  const mouse = Object.entries(sets.mouse).map(([id, c]) => panel('mouse', id, c, Number(id) === A.mouse)).join('');
  const keys = Object.entries(sets.keys).map(([id, c]) => panel('keys', id, c, Number(id) === A.keys)).join('');
  return `<h4 class="ctl-h">マウス</h4><div class="keys">${mouse}</div>
    <h4 class="ctl-h">キーボード</h4><div class="keys">${keys}</div>
    <p class="hint ctl-common">どの配置でも: ${CONTROLS.common.map(([k, v]) => `${k} ${v}`).join(' · ')}</p>`;
}

function updateModeUI() {
  document.body.classList.toggle('nograb', !Object.values(mouseMap()).includes('grab'));
  $('#stControls').innerHTML = controlsHtml();
  if (!$('#helpModal').classList.contains('hidden')) $('#helpBody').innerHTML = controlsHtml();
}

function setControl(kind, id) {
  id = Number(id);
  const set = controlSets()[kind][id];
  if (id === 4 && !set.custom) { openBindings(kind); return; }  // nothing to use yet: make it first
  if (kind === 'mouse') A.mouse = id; else A.keys = id;
  if (kind === 'mouse' && id !== 3) flickOff();
  A.pending[kind] = true;  // ignore server echoes of the old value until the change is stored
  updateModeUI();
  postJ('/api/config?' + qs(kind === 'mouse' ? { mouse_mode: id } : { key_mode: id }))
    .catch(() => {}).finally(() => { A.pending[kind] = false; });
  toast(esc(set.title));
}

function toggleHelp() {
  const m = $('#helpModal');
  if (m.classList.contains('hidden')) { flickOff(); $('#helpBody').innerHTML = controlsHtml(); m.classList.remove('hidden'); } else m.classList.add('hidden');
}

const MODALS = ['#scopeModal', '#helpModal', '#bindModal', '#remoteModal'];
const modalOpen = () => MODALS.some((m) => !$(m).classList.contains('hidden'));
function closeModals() {
  Bind.capture = null;
  MODALS.forEach((m) => $(m).classList.add('hidden'));
}

// ---- the owner's own bindings (keyboard layout D, mouse mode 4)
// Keys are stored by position (e.code), so they keep working with the Japanese IME on. H and Esc
// stay reserved; a key can do one thing only (assigning it again moves it).

const Bind = { kind: 'keys', keys: {}, mouse: {}, capture: null };

function openBindings(kind) {
  flickOff();
  Bind.kind = kind;
  Bind.capture = null;
  const keys = {};
  for (const [code, cmd] of Object.entries(A.st && A.st.customKeys ? customKeymap() : KEYMAPS[A.keys] || KEYMAPS[2])) (keys[cmd] = keys[cmd] || []).push(code);
  Bind.keys = keys;
  Bind.mouse = { ...(A.st && A.st.customMouse ? customMouse() : MOUSE_PRESETS[A.mouse] || MOUSE_PRESETS[1]) };
  $('#helpModal').classList.add('hidden');
  $('#bindMsg').textContent = '';
  renderBindings();
  $('#bindModal').classList.remove('hidden');
}

function renderBindings() {
  const k = Bind.kind === 'keys';
  $('#bindTitle').textContent = k ? 'キーボード D — 自分で割り当てる' : 'マウス — 自分で割り当てる';
  let html;
  if (k) {
    const base = [1, 2, 3].map((i) => `<button class="btn ghost" data-base="${i}">${'ABC'[i - 1]} をもとにする</button>`).join(' ');
    const rows = KEY_ACTIONS.map((cmd) => {
      const codes = Bind.keys[cmd] || [];
      const slots = [0, 1, 2].map((i) => {
        const code = codes[i];
        const cap = Bind.capture && Bind.capture.cmd === cmd && Bind.capture.i === i;
        if (!code && i > codes.length) return '';
        return `<span class="keyslot${cap ? ' capturing' : ''}"><button class="btn" data-cmd="${cmd}" data-i="${i}">${cap ? 'キーを押してください…' : code ? esc(keyName(code)) : '＋ キーを追加'}</button>${code && !cap ? `<button class="btn ghost x" data-clear="${cmd}" data-i="${i}" title="外す">×</button>` : ''}</span>`;
      }).join('');
      return `<tr><th>${CMD_LABEL[cmd]}</th><td>${slots}</td></tr>`;
    }).join('');
    html = `<p class="hint">ボタンを押してから、割り当てたいキーを押してください。同じキーは 1 つの操作にだけ使えます（別の操作に割り当てると移ります）。
      「好評（長押しで超好評）」のキーは、短く押すと好評、長く押すと超好評です。<kbd>H</kbd>（操作一覧）と <kbd>Esc</kbd> は変えられません。</p>
      <div class="row">${base}</div><table class="bindtab">${rows}</table>`;
  } else {
    const base = [1, 2, 3].map((i) => `<button class="btn ghost" data-base="${i}">モード${i} をもとにする</button>`).join(' ');
    const rows = MOUSE_INPUTS.map((inp) => {
      const opts = (['left', 'right', 'middle'].includes(inp) ? MOUSE_ACTIONS_BUTTON : MOUSE_ACTIONS_OTHER)
        .map((c) => `<option value="${c}"${Bind.mouse[inp] === c ? ' selected' : ''}>${CMD_LABEL[c]}</option>`).join('');
      return `<tr><th>${INPUT_LABEL[inp]}</th><td><select data-input="${inp}">${opts}</select></td></tr>`;
    }).join('');
    html = `<p class="hint">「つかんで投げる」は、そのボタンを押したまま画像を投げます（右 = 好評、左 = 見送り、上 = 超好評、下 = アーカイブ）。
      「フリックの開始 / 終了」は、クリックしてからボタンを押さずに動かす勢いで評価します。</p>
      <div class="row">${base}</div><table class="bindtab">${rows}</table>`;
  }
  $('#bindBody').innerHTML = html;
}

function bindKeyCaptured(e) {
  if (!Bind.capture) return;
  e.preventDefault();
  e.stopImmediatePropagation();
  const code = e.code;
  if (!code || e.repeat) return;
  if (code === 'Escape') { Bind.capture = null; renderBindings(); return; }
  if (RESERVED_KEYS.has(code)) { $('#bindMsg').textContent = `${keyName(code)} は操作一覧のキーなので使えません`; return; }
  const { cmd, i } = Bind.capture;
  let moved = '';
  for (const [c, codes] of Object.entries(Bind.keys)) {
    const j = codes.indexOf(code);
    if (j >= 0 && !(c === cmd && j === i)) { codes.splice(j, 1); moved = CMD_LABEL[c]; }
  }
  const list = (Bind.keys[cmd] = Bind.keys[cmd] || []);
  if (i < list.length) list[i] = code; else list.push(code);
  Bind.capture = null;
  $('#bindMsg').textContent = moved ? `${keyName(code)} を「${moved}」から移しました` : '';
  if (COMMON_KEYS[code]) $('#bindMsg').textContent += `（${keyName(code)} のもとの機能は、この配置では使えなくなります）`;
  renderBindings();
}

async function saveBindings() {
  const k = Bind.kind === 'keys';
  let value;
  if (k) {
    const likes = (Bind.keys.like || []).length + (Bind.keys.likeHold || []).length;
    if (!likes || !(Bind.keys.pass || []).length) { $('#bindMsg').textContent = '好評と見送りのキーは、少なくとも 1 つずつ決めてください'; return; }
    value = KEY_ACTIONS.filter((c) => (Bind.keys[c] || []).length).map((c) => `${c}:${Bind.keys[c].join(',')}`).join(';');
  } else {
    value = MOUSE_INPUTS.map((inp) => `${inp}:${Bind.mouse[inp] || 'none'}`).join(';');
  }
  try {
    const r = await postJ('/api/config?' + qs(k ? { custom_keys: value, key_mode: 4 } : { custom_mouse: value, mouse_mode: 4 }));
    if (!r.ok) { $('#bindMsg').textContent = r.error || '保存できませんでした'; return; }
  } catch { $('#bindMsg').textContent = '保存できませんでした'; return; }
  if (k) A.keys = 4; else A.mouse = 4;
  closeModals();
  toast(k ? 'キーボード D（自分の割り当て）を使います' : 'マウス（自分の割り当て）を使います');
}

// ---------------------------------------------------------------- scope picker

let scopeCb = null;
let scopeSel = new Set();

async function openScope(title, current, cb) {
  await loadFolders();
  scopeCb = cb;
  scopeSel = new Set(current);
  $('#mdTitle').textContent = title;
  $('#mdSearch').value = '';
  const kids = new Map();
  for (const f of A.folders) {
    if (f.depth === 0) continue;
    const p = parentOf(f.path);
    if (!kids.has(p)) kids.set(p, []);
    kids.get(p).push(f);
  }
  for (const list of kids.values()) list.sort((a, b) => a.name.localeCompare(b.name, 'ja'));
  const rows = [];
  // With several photo folders each one is a top entry (its own check box) with its folders below.
  const multi = multiRoot();
  const row = (f, token, indent, top) => {
    rows.push(`<label class="frow${top ? ' top' : ''}" style="padding-left:${14 + indent * 20}px" data-s="${esc(f.name.toLowerCase())}">
        <input type="checkbox" data-p="${esc(token)}"${scopeSel.has(token) ? ' checked' : ''}>
        <span>${esc(f.name)}</span>
        <span class="cnt">${num(f.total)}枚 · 好評 ${num(f.liked)}${f.rank && !top ? ` · ${f.rank}位` : ''}</span></label>`);
  };
  const walk = (p) => {
    for (const f of kids.get(p) || []) {
      row(f, f.path, f.depth - 1 + (multi ? 1 : 0), false);
      walk(f.path);
    }
  };
  if (multi) {
    for (const t of A.folders.filter((f) => f.depth === 0)) {
      row(t, t.path === '' ? FIRST_ROOT : t.path, 0, true);
      walk(t.path);
    }
  } else {
    walk('');
  }
  $('#mdList').innerHTML = rows.join('') || '<p class="hint" style="padding:0 14px">フォルダがありません</p>';
  updateScopeCount();
  $('#scopeModal').classList.remove('hidden');
  $('#mdSearch').focus();
}

function updateScopeCount() {
  $('#mdCount').textContent = scopeSel.size ? `${scopeSel.size}個のフォルダを選択中` : '未選択 = すべて';
}

// ---------------------------------------------------------------- ranking

function renderRanking() {
  loadFolders().then(() => {
    const maxDepth = A.folders.reduce((m, f) => Math.max(m, f.depth), 0);
    const sel = $('#rkDepth');
    const want = store.get('rkDepth', A.st ? A.st.artistDepth : 1);
    sel.innerHTML = '';
    for (let d = 1; d <= Math.max(1, maxDepth); d++) sel.add(new Option(d === (A.st ? A.st.artistDepth : 1) ? `${d}（標準）` : String(d), d));
    sel.value = Math.min(want, Math.max(1, maxDepth));
    $('#rkSort').value = store.get('rkSort', 'score');
    $('#rkSince').value = String(A.sinceDays);
    drawRanking();
    if (!$('#rkStats').classList.contains('hidden')) renderStats();
  }).catch(() => {});
}

// Swipes per day (last 30 days) as stacked bars, plus totals.
async function renderStats() {
  const box = $('#rkStats');
  let r;
  try { r = await getJ('/api/stats?days=30'); } catch { box.textContent = '統計を読み込めませんでした'; return; }
  const days = r.days;
  const tot = (d) => d.like + d.super + d.pass + d.hold + (d.archive || 0);
  const max = Math.max(1, ...days.map(tot));
  const W = 720, H = 120, bw = W / days.length;
  const bars = days.map((d, i) => {
    let y = H;
    const seg = (v, cls) => {
      if (!v) return '';
      const h = (v / max) * (H - 14);
      y -= h;
      return `<rect class="${cls}" x="${(i * bw + 1).toFixed(1)}" y="${y.toFixed(1)}" width="${(bw - 2).toFixed(1)}" height="${h.toFixed(1)}"><title>${d.day}: ${tot(d)}枚</title></rect>`;
    };
    return seg(d.pass, 'b-pass') + seg(d.like, 'b-like') + seg(d.super, 'b-super') + seg(d.hold, 'b-hold') + seg(d.archive || 0, 'b-archive');
  }).join('');
  const week = days.slice(-7).reduce((s, d) => s + tot(d), 0);
  const likeRate = r.rated ? Math.round((100 * r.liked) / r.rated) : 0;
  box.innerHTML = `<div class="stats-head">
      <span>評価済み <b>${num(r.rated)}</b>枚</span><span>好評率 <b>${likeRate}%</b></span>
      <span>超好評 <b>${num(r.supers)}</b>枚</span><span>この7日間 <b>${num(week)}</b>回スワイプ</span>
      <span class="legend"><i class="b-super"></i>超好評 <i class="b-like"></i>好評 <i class="b-pass"></i>見送り ${days.some((d) => d.hold) ? '<i class="b-hold"></i>保留（以前の評価） ' : ''}<i class="b-archive"></i>アーカイブ</span>
    </div>
    <svg viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" class="stats-chart" role="img" aria-label="1日ごとのスワイプ数（30日）">${bars}</svg>
    <div class="stats-axis"><span>${days[0].day}</span><span>今日</span></div>`;
}

const rkSelected = new Set();

function drawRanking() {
  const depth = Number($('#rkDepth').value) || 1;
  const sort = $('#rkSort').value;
  const q = $('#rkSearch').value.trim().toLowerCase();
  // at the artist depth: every artist, including a photo folder read without subfolders
  const ad = A.st ? A.st.artistDepth : 1;
  const rows = A.folders.filter((f) => (depth === ad ? f.artist : f.depth === depth) && (!q || f.name.toLowerCase().includes(q)));
  const rate = (f) => (f.rated ? f.liked / f.rated : -1);
  // rank change vs the comparison point; newly ranked folders count as a big rise
  const moved = (f) => (!f.rank ? -1e9 : !f.rank0 ? 1e6 : f.rank0 - f.rank);
  const by = {
    rise: (a, b) => moved(b) - moved(a) || (a.rank || 1e9) - (b.rank || 1e9),
    fall: (a, b) => (a.rank && a.rank0 ? a.rank0 - a.rank : 1e9) - (b.rank && b.rank0 ? b.rank0 - b.rank : 1e9) || (a.rank || 1e9) - (b.rank || 1e9),
    score: (a, b) => (a.rank || 1e9) - (b.rank || 1e9) || a.name.localeCompare(b.name, 'ja'),
    rate: (a, b) => rate(b) - rate(a) || b.rated - a.rated,
    liked: (a, b) => b.liked - a.liked || b.score - a.score,
    rated: (a, b) => b.rated - a.rated,
    total: (a, b) => b.total - a.total,
    name: (a, b) => a.name.localeCompare(b.name, 'ja'),
  }[sort];
  rows.sort(by);
  const html = rows.map((f) => {
    let delta = '';
    if (f.rank && f.rank0 && f.rank !== f.rank0) {
      delta = f.rank < f.rank0 ? `<span class="delta up">▲${f.rank0 - f.rank}</span>` : `<span class="delta down">▼${f.rank - f.rank0}</span>`;
    } else if (f.rank && !f.rank0) delta = '<span class="delta new">NEW</span>';
    // with several photo folders: which one it is in (nothing for a photo folder's own row)
    const parent = f.depth === 0 ? '' : placeOf(parentOf(f.path));
    const was = f.rank0 ? `比較時点: ${f.rank0}位 · スコア ${Number(f.score0).toFixed(1)} · 評価 ${num(f.rated0)}枚` : '比較時点では未評価';
    return `<tr data-p="${esc(f.path)}" class="${f.rated ? '' : 'unrated'}">
      <td class="c-sel"><input type="checkbox"${rkSelected.has(f.path) ? ' checked' : ''}></td>
      <td class="c-rank" title="${esc(was)}">${f.rank || '—'}${delta}</td>
      <td><div class="nm">${esc(f.name)}</div>${parent ? `<div class="sub">${esc(parent)}</div>` : ''}</td>
      <td class="c-score"><div class="scorebar"><div class="track"><div class="fill" style="width:${f.rated ? f.score : 0}%"></div></div><span>${f.rated ? f.score.toFixed(1) : '—'}</span></div></td>
      <td class="c-num">${num(f.liked)}</td><td class="c-num">${num(f.rated)}</td><td class="c-num opt">${num(f.total)}</td>
      <td class="c-num opt">${f.rated ? Math.round((100 * f.liked) / f.rated) + '%' : '—'}</td><td class="c-num opt">${num(f.supers)}</td>
      <td class="c-act"><button class="btn" data-act="swipe">スワイプ</button> <button class="btn" data-act="gallery">好評を見る</button></td>
    </tr>`;
  }).join('');
  $('#rkTable tbody').innerHTML = html || '<tr><td colspan="10" class="muted" style="padding:20px">フォルダがありません</td></tr>';
  updateRkSel();
}

function updateRkSel() {
  $('#rkSel').textContent = rkSelected.size ? `${rkSelected.size}個選択中` : '';
  $('#rkSwipe').disabled = $('#rkGallery').disabled = !rkSelected.size;
}

// ---------------------------------------------------------------- gallery

const Ga = {
  status: store.get('gaStatus', 'liked'),
  sort: store.get('gaSort', 'score'),
  view: store.get('gaView', 'images'),
  folderSort: store.get('gaFolderSort', 'rank'),
  scope: store.get('gaScope', []),
  size: store.get('gaSize', typeof window.ChoosenAndroid !== 'undefined' || innerWidth <= 760 ? 120 : 200),  // smaller rows on a phone  // row height of the image view (the slider at the top right)
  thumbS: 0,
  ratio: new Map(),                // fingerprint -> width / height, learned from the thumbnail
  seed: 0,
  items: [],
  total: 0,
  loading: false,
  done: false,
  loaded: false,
  dirty: false,  // something was archived / restored from the lightbox
  req: 0,
};

function galleryControls() {
  $$('#gaStatus button').forEach((b) => b.classList.toggle('on', b.dataset.v === Ga.status));
  $$('#gaView button').forEach((b) => b.classList.toggle('on', b.dataset.v === Ga.view));
  $('#gaSort').value = Ga.sort;
  $('#gaFolderSort').value = Ga.folderSort;
  $('#gaSortWrap').classList.toggle('hidden', Ga.view !== 'images');
  $('#gaFolderSortWrap').classList.toggle('hidden', Ga.view !== 'folders');
  $('#gaScope').textContent = scopeLabel(Ga.scope);
  $('#gaScope').classList.toggle('active', Ga.scope.length > 0);
  store.set('gaStatus', Ga.status);
  store.set('gaSort', Ga.sort);
  store.set('gaView', Ga.view);
  store.set('gaFolderSort', Ga.folderSort);
  store.set('gaScope', Ga.scope);
}

// Thumbnail size for the row height (a landscape picture is wider than it is tall).
const gaThumbS = () => [256, 384, 512, 768, 1024].find((b) => b >= Ga.size * 1.6 * (window.devicePixelRatio || 1)) || 1024;
// While dragging only the height changes; on release, sharper thumbnails are loaded if needed.
function applyGallerySize(final) {
  $('#grid').style.setProperty('--rowh', Ga.size + 'px');
  if (!Ga.thumbS) Ga.thumbS = gaThumbS();
  if (!final) return;
  store.set('gaSize', Ga.size);
  const s = gaThumbS();
  if (s === Ga.thumbS) return;
  Ga.thumbS = s;
  for (const im of $$('#grid .cell > img')) {
    const it = Ga.items[Number(im.parentElement.dataset.i)];
    if (it) im.src = thumbUrl(it, s);
  }
}

function galleryReload() {
  Ga.loaded = true;
  Ga.ver = A.st ? A.st.version : 0;
  Ga.items = [];
  Ga.done = false;
  Ga.loading = false;
  Ga.req++;
  $('#grid').innerHTML = '';
  $('#grid').classList.toggle('folders', Ga.view === 'folders' && Ga.status !== 'archived');
  $('#grid').classList.toggle('natural', !(Ga.view === 'folders' && Ga.status !== 'archived'));
  $('#gaSizeWrap').classList.toggle('hidden', Ga.view === 'folders' && Ga.status !== 'archived');
  applyGallerySize(false);
  $('#gridWrap').scrollTop = 0;
  galleryControls();
  if (Ga.view === 'folders' && Ga.status !== 'archived') return galleryFolders();
  return galleryMore();
}

async function galleryMore() {
  if (Ga.loading || Ga.done || (Ga.view === 'folders' && Ga.status !== 'archived')) return;
  Ga.loading = true;
  const req = Ga.req;
  $('#gridEnd').textContent = '読み込み中…';
  try {
    const r = await getJ('/api/items?' + qs({ filter: Ga.status, sort: Ga.sort, scope: Ga.scope.join('|'), offset: Ga.items.length, limit: 150, seed: Ga.seed }));
    if (req !== Ga.req) return;
    const start = Ga.items.length;
    Ga.items.push(...r.items);
    Ga.total = r.total;
    const frag = document.createDocumentFragment();
    r.items.forEach((it, k) => {
      const d = document.createElement('div');
      d.className = 'cell';
      d.dataset.i = start + k;
      d.title = `${placeOf(it.dir)} › ${it.name}`;
      d.innerHTML = `<img loading="lazy" decoding="async" src="${thumbUrl(it, Ga.thumbS)}" alt="">` +
        (it.rated ? `<span class="badge ${it.last}">${marks(it, 3)}${fmtPts(it.p)}</span>` : '') +
        (pagesOf(it) ? `<span class="npages">${it.pages.length}p</span>` : '');
      // the cell takes the picture's own shape once its thumbnail is known
      const known = Ga.ratio.get(it.fp);
      if (known) d.style.setProperty('--r', known.toFixed(3));
      const im = d.firstChild;
      im.addEventListener('load', () => {
        const r = im.naturalWidth / im.naturalHeight;
        if (r > 0 && isFinite(r)) { Ga.ratio.set(it.fp, r); d.style.setProperty('--r', r.toFixed(3)); }
      });
      frag.appendChild(d);
    });
    $('#grid').appendChild(frag);
    Ga.done = Ga.items.length >= r.total || !r.items.length;
    $('#gaCount').textContent = `${num(r.total)}枚`;
    $('#gridEnd').textContent = Ga.done ? (r.total ? '' : '該当する画像がありません') : '';
  } catch {
    $('#gridEnd').textContent = '読み込めませんでした';
  } finally {
    if (req === Ga.req) Ga.loading = false;
  }
  // keep filling while the end of the grid is still on screen (only while the gallery is visible;
  // a hidden element reports a zero rectangle and would load everything)
  if (!Ga.done && req === Ga.req && A.tab === 'gallery') {
    const end = $('#gridEnd').getBoundingClientRect(), wrap = $('#gridWrap').getBoundingClientRect();
    if (wrap.height > 0 && end.top < wrap.bottom + 400) await galleryMore();
  }
}

// Folder view: one tile per artist, its best matching image as cover, in ranking order.
async function galleryFolders() {
  const req = Ga.req;
  $('#gridEnd').textContent = '読み込み中…';
  try {
    const r = await getJ('/api/folderCards?' + qs({ filter: Ga.status, scope: Ga.scope.join('|'), sort: Ga.folderSort }));
    if (req !== Ga.req) return;
    Ga.done = true;
    // a photo folder's own tile holds only the images directly in it
    $('#grid').innerHTML = r.folders.map((f) => `<div class="ftile" data-p="${esc(f.depth === 0 ? f.path + '/' : f.path)}" title="${esc(f.depth === 0 ? topName(f.path) + ' の直下' : placeOf(f.path))}">
        <img loading="lazy" decoding="async" src="${thumbUrl(f.cover)}" alt="">
        <div class="fcap"><div class="fname">${f.rank ? `<span class="frank">${f.rank}位</span>` : ''}${esc(f.name)}</div>
        <div class="fsub">該当 ${num(f.count)} · 好評 ${num(f.liked)}/${num(f.rated)} · 全 ${num(f.total)}</div></div></div>`).join('');
    $('#gaCount').textContent = `${num(r.total)}フォルダ`;
    $('#gridEnd').textContent = r.total ? '' : '該当するフォルダがありません';
  } catch {
    $('#gridEnd').textContent = '読み込めませんでした';
  }
}

// ---- albums (saved gallery conditions; they store folder paths, never copies of photos)

let albums = [];
async function loadAlbums() {
  try { albums = await getJ('/api/albums'); } catch { albums = []; }
  if (!Array.isArray(albums)) albums = [];
  renderAlbums();
}
function renderAlbums(selected = '') {
  const s = $('#gaAlbums');
  s.innerHTML = '<option value="">保存した条件…</option>' + albums.map((a) => `<option${a.name === selected ? ' selected' : ''}>${esc(a.name)}</option>`).join('');
}
function saveAlbums() { return postJ('/api/albums', JSON.stringify(albums)).catch(() => toast('保存できませんでした')); }

// ---- lightbox + slideshow

const Lb = { i: 0, page: 0, playing: false, timer: 0, idle: 0 };
function lbItem() {
  const it = Ga.items[Lb.i];
  const ps = it && pagesOf(it);
  return { it, shown: ps ? ps[Math.min(Lb.page || 0, ps.length - 1)] : it };
}
function lbOpen(i) {
  Lb.i = i;
  Lb.page = 0;
  $('#lightbox').classList.remove('hidden');
  lbShow();
}
function lbPlay(on) {
  Lb.playing = on;
  clearTimeout(Lb.timer);
  $('#lbPlay').textContent = on ? '❚❚ 一時停止' : '▶ 再生';
  $('#lightbox').classList.toggle('playing', on);
  lbWake();
  if (on) lbTick();
}
// While playing, the controls and cursor hide after 2 s without mouse movement.
function lbWake() {
  const lb = $('#lightbox');
  lb.classList.remove('idle');
  clearTimeout(Lb.idle);
  if (Lb.playing) Lb.idle = setTimeout(() => lb.classList.add('idle'), 2000);
}
function lbTick() {
  clearTimeout(Lb.timer);
  if (!Lb.playing) return;
  Lb.timer = setTimeout(() => {
    const ps = pagesOf(Ga.items[Lb.i]);
    if (ps && (Lb.page || 0) + 1 < ps.length) lbPage(1);  // a work plays through its pages first
    else if (Lb.i + 1 < Ga.items.length) lbStep(1);
    else if (Ga.done) { Lb.i = 0; Lb.page = 0; lbShow(); }  // loop from the start
    lbTick();
  }, Number($('#lbSpeed').value) * 1000);
}
async function startSlideshow(shuffle) {
  if (Ga.view !== 'images') { Ga.view = 'images'; await galleryReload(); }
  if (shuffle) {
    Ga.sort = 'random';
    Ga.seed = Math.floor(Math.random() * 1e9) + 1;  // a new order every time
    await galleryReload();
  }
  if (!Ga.items.length) { toast('スライドショーにする画像がありません'); return; }
  lbOpen(0);
  try { await $('#lightbox').requestFullscreen(); } catch { /* not allowed: stay windowed */ }
  lbPlay(true);
}
function lbShow() {
  const { it, shown } = lbItem();
  if (!it) return;
  const ps = pagesOf(it);
  Lb.page = Math.min(Lb.page || 0, ps ? ps.length - 1 : 0);
  $('#lbImg').src = imgUrl(shown);
  const st = it.archived ? 'アーカイブ済み' : it.rated ? `${LABEL[it.last]} · ${marks(it)}${fmtPts(it.p)}点 · ${it.n}回評価` : '未評価';
  const pg = ps ? ` · ${Lb.page + 1}/${ps.length}ページ（↑↓）` : '';
  $('#lbCap').innerHTML = `<b>${esc(placeOf(it.dir))}</b> › ${esc(shown.name)} · ${st}${pg} · ${Lb.i + 1} / ${num(Ga.total)}`;
  $('#lbArchive').textContent = it.archived ? '元に戻す (R)' : 'アーカイブ (Delete)';
  const nx = ps && ps[Lb.page + 1] ? ps[Lb.page + 1] : Ga.items[Lb.i + 1];
  if (nx) new Image().src = imgUrl(nx);
  if (Lb.i > Ga.items.length - 20) galleryMore();
}
function lbPage(d) {
  const ps = pagesOf(Ga.items[Lb.i]);
  if (!ps) return;
  Lb.page = Math.max(0, Math.min(ps.length - 1, (Lb.page || 0) + d));
  lbShow();
}
function lbClose() {
  lbPlay(false);
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
  $('#lightbox').classList.add('hidden');
  $('#lbImg').removeAttribute('src');
  if (Ga.dirty) { Ga.dirty = false; galleryReload(); }
}
function lbStep(d) {
  const n = Lb.i + d;
  if (n < 0 || n >= Ga.items.length) return;
  Lb.i = n;
  Lb.page = 0;
  lbShow();
}
// Archive (or restore) the whole card shown in the lightbox.
function lbArchive(restore) {
  const { it } = lbItem();
  if (!it || (restore && !it.archived) || (!restore && it.archived)) return;
  Outbox.push({ id: it.id, fp: it.fp, a: restore ? 'R' : 'A', cid: newCid(), replace: '' });
  it.archived = !restore;
  Ga.dirty = true;
  lbShow();
}
function lbKey(e) {
  if (e.key === 'Escape') lbClose();
  else if (e.key === 'ArrowRight') { lbStep(1); lbTick(); }
  else if (e.key === 'ArrowLeft') { lbStep(-1); lbTick(); }
  else if (e.key === 'ArrowDown' || e.key === '.' || e.key === 'PageDown') { lbPage(1); lbTick(); }
  else if (e.key === 'ArrowUp' || e.key === ',' || e.key === 'PageUp') { lbPage(-1); lbTick(); }
  else if (e.key === ' ') lbPlay(!Lb.playing);
  else if (e.code === 'KeyS') startSlideshow(true);
  else if (e.code === 'KeyE') reveal(lbItem().shown);
  else if (e.key === 'Delete') lbArchive(false);
  else if (e.code === 'KeyR') lbArchive(true);
  else return;
  e.preventDefault();
}
// Clicks on the picture: left third = previous, right third = next, middle third = pause / play.
// A double-click in the middle closes. (Only the middle: two quick clicks on "next" would
// otherwise count as a double-click and close the slideshow.)
const lbZone = (e) => (e.target.closest('.lb-bar, .lb-nav') ? '' : e.clientX < innerWidth / 3 ? 'prev' : e.clientX > (innerWidth * 2) / 3 ? 'next' : 'mid');
function lbClick(e) {
  const z = lbZone(e);
  if (!z) return;
  if (z === 'prev') { lbStep(-1); lbTick(); } else if (z === 'next') { lbStep(1); lbTick(); } else { lbPlay(!Lb.playing); lbFlash(Lb.playing ? '▶' : '❚❚'); }
}
function lbDblClick(e) { if (lbZone(e) === 'mid') lbClose(); }
function lbFlash(sym) {
  const f = $('#lbFlash');
  f.textContent = sym;
  f.classList.remove('show');
  void f.offsetWidth;  // restart the fade
  f.classList.add('show');
}
let lbWheelLock = 0;
function lbWheel(e) {
  e.preventDefault();
  if (Math.abs(e.deltaY) < 1 || e.ctrlKey) return;
  const now = performance.now();
  const locked = now < lbWheelLock;
  lbWheelLock = now + 180;  // one step per turn of the wheel or swipe of the touchpad
  if (locked) return;
  lbStep(e.deltaY > 0 ? 1 : -1);
  lbTick();
}

// ---------------------------------------------------------------- settings

// A photo folder's subfolder option. depth: subfolder levels read (-1 = all, 0 = none). The check
// box turns subfolders on or off; advanced mode adds the maximum depth (empty = no limit). A
// change that would hide images first says how many; their ratings stay.
function subOption(depth, pathOf) {
  const maxD = (A.st && A.st.maxSubfolderDepth) || 64;
  const label = document.createElement('label');
  label.className = 'sub';
  const cb = document.createElement('input');
  cb.type = 'checkbox';
  cb.checked = depth !== 0;
  label.append(cb, 'サブフォルダも読む');
  const lim = document.createElement('label');
  lim.className = 'sub adv-only';
  const n = document.createElement('input');
  n.type = 'number';
  n.min = '1';
  n.max = String(maxD);
  n.placeholder = '制限なし';
  n.className = 'stMaxDepth';
  n.value = depth > 0 ? String(depth) : '';
  n.disabled = !cb.checked;
  lim.append('最大の深さ', n);
  const note = document.createElement('p');
  note.className = 'hint';
  const current = () => {
    if (!cb.checked) return 0;
    const v = Math.round(Number(n.value));
    return n.value.trim() === '' || !(v >= 1) ? -1 : Math.min(v, maxD);
  };
  const changed = async () => {
    n.disabled = !cb.checked;
    updateDepthNote();
    note.textContent = '';
    const d = current();
    if (d < 0) return;
    const r = await getJ('/api/hiddenCount?' + qs({ root: pathOf(), max_depth: d })).catch(() => null);
    if (r && r.hidden && current() === d) {
      note.textContent = `保存すると、${d === 0 ? 'サブフォルダ' : `${d} 階層より深いフォルダ`}の ${num(r.hidden)}枚が表示されなくなります（評価は残り、戻すと戻ります）。` +
        (d === 0 ? 'このフォルダは 1 つのフォルダとして順位がつきます。' : '');
    }
  };
  cb.addEventListener('change', changed);
  n.addEventListener('change', changed);
  return { label, lim, note, depth: current, name: () => pathOf().split(/[\\/]/).filter(Boolean).pop() || pathOf() };
}

// The artist depth never goes below what a photo folder reads: say where it is limited.
function updateDepthNote() {
  const ad = Number($('#stDepth').value) || 1;
  const subs = [A.rootSub, ...$$('#stMoreRoots .rootItem').map((el) => el.sub)].filter(Boolean);
  const lines = subs.filter((s) => s.depth() >= 0 && s.depth() < ad).map((s) => {
    const d = s.depth();
    return `「${s.name()}」は ${d === 0 ? 'サブフォルダを読まない' : `${d} 階層までしか読まない`}ため、そのフォルダでは ${d === 0 ? 'フォルダそのもの' : `${d} 階層目`}で順位をつけます。`;
  });
  $('#stDepthNote').textContent = lines.join(' ');
}

// One row per further photo folder: its path, its subfolder option and a remove button. Saved with
// the "save" button.
function addRootRow(path, depth = -1) {
  const item = document.createElement('div');
  item.className = 'rootItem';
  const row = document.createElement('div');
  row.className = 'row';
  const input = document.createElement('input');
  input.type = 'text';
  input.spellcheck = false;
  input.className = 'stMore';
  input.value = path;
  const sub = subOption(depth, () => input.value.trim());
  const del = document.createElement('button');
  del.className = 'btn';
  del.textContent = '✕';
  del.title = 'このフォルダを外す（評価は残ります）';
  del.addEventListener('click', () => { item.remove(); updateDepthNote(); });
  row.append(input, sub.label, sub.lim, del);
  item.append(row, sub.note);
  item.sub = sub;
  $('#stMoreRoots').append(item);
}

// "スマホでスワイプ": a phone on the same Wi-Fi scans the QR code shown here and then swipes this
// PC's library; its ratings count in the PC's ranking. The settings show whether it is on and how
// many phones are paired.
async function refreshRemote() {
  const r = await getJ('/api/remote').catch(() => null);
  const on = !!(r && r.on), devices = r ? r.devices : 0;
  $('#stRemoteOff').classList.toggle('hidden', !on);
  $('#stRemoteForget').classList.toggle('hidden', !devices);
  $('#stRemoteState').textContent = [on ? 'オン（ウィンドウを閉じても動き続けます）' : '', devices ? `登録済みのスマホ ${devices} 台` : ''].filter(Boolean).join(' · ');
}

// The QR code from the core (rows of '1'/'0') as an SVG: black on white whatever the theme, with
// the 4-module quiet zone that scanners need.
function qrSvg(rows) {
  const n = rows.length, size = n + 8;
  let d = '';
  rows.forEach((row, y) => {
    for (let x = 0; x < n; x++) {
      if (row[x] !== '1') continue;
      let w = 1;
      while (row[x + w] === '1') w++;
      d += `M${x + 4} ${y + 4}h${w}v1h-${w}z`;
      x += w - 1;
    }
  });
  return `<svg viewBox="0 0 ${size} ${size}" shape-rendering="crispEdges" role="img" aria-label="QR コード"><rect width="${size}" height="${size}" fill="#fff"/><path d="${d}" fill="#000"/></svg>`;
}

// Pressing "スマホでスワイプ": switch phone access on, show a new code as a QR code, and close by
// itself once a phone has paired with it.
const Remote = { timer: 0, paired: 0 };
async function startRemote() {
  clearTimeout(Remote.timer);
  const r = await getJ('/api/remote').catch(() => null);
  if (!r) { toast('Choosen に接続できません'); return; }
  if (!r.on && !(await postJ('/api/config?' + qs({ remote_access: 1 })).then((x) => x.ok).catch(() => false))) {
    toast('スマホ用の接続を開けませんでした');
    return;
  }
  Remote.paired = r.paired;
  await postJ('/api/remote/pair').catch(() => null);
  flickOff();
  $('#remoteQr').replaceChildren();
  $('#remoteQr').dataset.code = '';
  $('#remoteModal').classList.remove('hidden');
  pollRemote();
}
async function pollRemote() {
  clearTimeout(Remote.timer);
  if ($('#remoteModal').classList.contains('hidden')) { refreshRemote(); return; }
  const r = await getJ('/api/remote').catch(() => null);
  if (r && r.paired > Remote.paired) {
    Remote.paired = r.paired;
    showRemote(null, 'つながりました。スマホでスワイプできます');
    Remote.timer = setTimeout(() => { $('#remoteModal').classList.add('hidden'); refreshRemote(); }, 1500);
    return;
  }
  if (r) showRemote(r);
  Remote.timer = setTimeout(pollRemote, 1000);
}
function showRemote(r, done) {
  const qr = $('#remoteQr'), msg = $('#remoteMsg');
  const live = !!(r && !r.error && r.code && r.qr && r.qr.length);
  qr.classList.toggle('expired', !live);
  $('#remoteAgain').classList.toggle('hidden', !r || live || !!r.error);
  $('#remoteCode').textContent = live ? r.code : '';
  $('#remoteUrl').textContent = live ? '読み取れないとき: スマホのブラウザで ' + r.urls[0] + ' を開いて番号を入力' : '';
  if (done) { qr.classList.remove('expired'); msg.textContent = done; return; }
  if (r.error) { msg.textContent = r.error; return; }
  if (!r.listening) { msg.textContent = '準備中…'; return; }
  if (!r.urls.length) { msg.textContent = 'この PC のネットワークアドレスが見つかりません（Wi-Fi に接続していますか）'; return; }
  if (!live) { msg.textContent = 'QR コードの期限が切れました'; return; }
  if (qr.dataset.code !== r.code) { qr.innerHTML = qrSvg(r.qr); qr.dataset.code = r.code; }
  msg.textContent = `スマホのカメラで読み取ってください（あと ${Math.floor(r.codeSeconds / 60)}:${String(r.codeSeconds % 60).padStart(2, '0')}）`;
}

// The core keeps paths with \ inside; the Android app shows them the way Android writes them.
const shownPath = (p) => (ANDROID ? (p || '').replace(/\\/g, '/') : p);

function fillSettings() {
  const st = A.st;
  if (!st) return;
  refreshRemote();
  const depths = st.maxDepths || [];
  $('#stRoot').value = shownPath(st.root);
  A.rootSub = subOption(depths[0] ?? -1, () => $('#stRoot').value.trim());
  $('#stRootSubSlot').replaceChildren(A.rootSub.label, A.rootSub.lim);
  $('#stRootNote').replaceChildren(A.rootSub.note);
  $('#stMoreRoots').replaceChildren();
  (st.moreRoots || []).forEach((p, i) => addRootRow(shownPath(p), depths[i + 1] ?? -1));
  $('#stDepth').value = st.artistDepth;
  updateDepthNote();
  $('#stBackup').value = st.backupDir || '';
  const shares = [0, 0.05, 0.067, 0.1, 0.2, 0.35, 0.5, 1];
  const near = shares.reduce((a, b) => (Math.abs(b - st.reviewShare) < Math.abs(a - st.reviewShare) ? b : a), 0.1);
  $('#stShare').value = String(near);
  $('#stAdvanced').checked = !!st.advanced;
  $$('#stThrowMode button').forEach((b) => b.classList.toggle('on', (b.dataset.v === '1') === !!st.throwSlow));
  const snap = (v, list, d) => String(list.includes(v) ? v : d);
  $('#stThrow').value = snap(st.throwDelayMs, [0, 200, 300, 400, 500], 300);
  $('#stLong').value = snap(st.longPressMs, [200, 300, 400, 500], 400);
  $('#stOver').checked = !!st.overPoints;
  $('#stGroup').checked = !!st.groupWorks;
  $('#stCopies').textContent = st.copies
    ? `中身がまったく同じコピー: ${num(st.copies)}枚（ファイル全体を比べて確認済み。1 枚として数え、二度は表示しません）。名前ではなく中身で判定しています。`
    : '';
  $('#stGroupInfo').textContent = `今: ${num(st.items)}枚。まとめると ${num(st.groupedCount)}枚のカードになります（1 作品 = 1 枚、1 回のスワイプで作品全体を評価）。`;
  $('#stPerf').textContent = Perf.summary();
  updateModeUI();
  renderSettingsNotice();
}

function renderSettingsNotice() {
  const st = A.st;
  let n = $('#stNotice');
  if (!st || st.hasRoot) { if (n) n.remove(); }
  else if (!n) {
    n = document.createElement('div');
    n.id = 'stNotice';
    n.className = 'notice';
    n.innerHTML = 'はじめに、整理したい写真フォルダを選んで「保存」を押してください。フォルダの読み込みは裏側で進み、見つかった画像からすぐにスワイプできます。';
    $('.settings').prepend(n);
  }
  if (st && st.scanError) $('#stMsg').textContent = st.scanError;
  if (st && st.warning) $('#stWarn').textContent = st.warning;
  $('#stWarn').classList.toggle('hidden', !(st && st.warning));
}

// Timing settings apply at once; the rest waits for the "save" button.
function saveQuick(params) {
  postJ('/api/config?' + qs(params)).then(() => toast('設定を変えました')).catch(() => toast('保存できませんでした'));
}

async function saveSettings() {
  const unquote = (v) => v.trim().replace(/^"|"$/g, '');
  const root = unquote($('#stRoot').value);
  const rows = $$('#stMoreRoots .rootItem')
    .map((el) => ({ path: unquote(el.querySelector('.stMore').value), depth: el.sub.depth() }))
    .filter((x) => x.path);
  const more = rows.map((x) => x.path);
  // one subfolder limit per folder: the first, then the further ones
  const maxDepths = [A.rootSub ? A.rootSub.depth() : -1, ...rows.map((x) => x.depth)];
  const depth = Number($('#stDepth').value) || 1;
  const backup = $('#stBackup').value.trim().replace(/^"|"$/g, '');
  const group = $('#stGroup').checked ? 1 : 0;
  $('#stMsg').textContent = '保存中…';
  try {
    // only what this button saves; the timing and display settings save themselves when changed,
    // and the folder depth only when it is on screen (advanced mode)
    const r = await postJ('/api/config?' + qs({
      root, group_works: group, artist_depth: A.st && A.st.advanced ? depth : undefined,
    }) + '&more_roots=' + encodeURIComponent(more.join('|')) + '&max_depths=' + maxDepths.join(',') +
      '&backup_dir=' + encodeURIComponent(backup));
    if (!r.ok) { $('#stMsg').textContent = r.error; return; }
    $('#stMsg').textContent = '保存しました';
    if (isPhone()) toast('保存しました');  // the message line is far below on a phone
    if (isPhone() && A.st && !A.st.hasRoot && root) setTimeout(() => showTab('swipe'), 600);  // first folder: start swiping
    store.set('rkDepth', depth);
    await loadFolders(true);
    Ga.loaded = false;
    resetScope(Sw.scope.filter((p) => scopeKnown(p)));
  } catch {
    $('#stMsg').textContent = '保存できませんでした';
  }
}

// ---------------------------------------------------------------- wiring

function bind() {
  $$('#tabs button').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));
  document.addEventListener('click', (e) => { const b = e.target.closest('button'); if (b) b.blur(); });
  document.addEventListener('keydown', onKey);
  document.addEventListener('keyup', onKeyUp);
  window.addEventListener('blur', () => pressEnd('', true));  // a held key is lost when the window loses focus
  window.addEventListener('focus', () => { A.focusAt = performance.now(); });
  document.addEventListener('contextmenu', (e) => { if (!e.target.matches('input, textarea')) e.preventDefault(); });
  // keep the mouse's side buttons from navigating the window
  for (const t of ['mouseup', 'mousedown', 'auxclick']) {
    document.addEventListener(t, (e) => { if (e.button === 3 || e.button === 4 || (t === 'mousedown' && e.button === 1)) e.preventDefault(); });
  }

  const stage = $('#stage');
  stage.addEventListener('pointerdown', onPointerDown);
  stage.addEventListener('pointermove', onPointerMove);
  stage.addEventListener('pointerup', onPointerUp);
  stage.addEventListener('pointercancel', onPointerUp);
  stage.addEventListener('wheel', onWheel, { passive: false });

  $('#swScope').addEventListener('click', () => openScope('スワイプする範囲', Sw.scope, (s) => resetScope(s)));
  $('#swFilter').addEventListener('change', (e) => { e.target.blur(); resetScope(Sw.scope, e.target.value); });
  $('#helpClose').addEventListener('click', toggleHelp);
  const pickControl = (e) => {
    const ed = e.target.closest('[data-edit]');
    if (ed) { openBindings(ed.dataset.edit); return; }
    const p = e.target.closest('section.ctl');
    if (p) setControl(p.dataset.kind, p.dataset.id);
  };
  $('#helpBody').addEventListener('click', pickControl);
  $('#stControls').addEventListener('click', pickControl);

  // the previous image's rating: click to go back to that image
  $('#lastRate').addEventListener('click', () => {
    const x = Last.e;
    const k = x ? Sw.hist.indexOf(x.h) : -1;
    if (k < 0 || A.tab !== 'swipe') return;
    commitThrow();
    Sw.hpos = k;
    renderSwipe();
  });

  // flick (mouse mode 3)
  document.addEventListener('mousemove', onFlickMove);
  document.addEventListener('pointerlockchange', () => { if (!document.pointerLockElement) flickOff(); });

  // own bindings
  window.addEventListener('keydown', bindKeyCaptured, true);  // before everything else while waiting for a key
  $('#bindBody').addEventListener('click', (e) => {
    const b = e.target.closest('[data-base]');
    if (b) {
      const i = Number(b.dataset.base);
      if (Bind.kind === 'keys') {
        Bind.keys = {};
        for (const [code, cmd] of Object.entries(KEYMAPS[i])) (Bind.keys[cmd] = Bind.keys[cmd] || []).push(code);
      } else Bind.mouse = { ...MOUSE_PRESETS[i] };
      Bind.capture = null;
      renderBindings();
      return;
    }
    const s = e.target.closest('[data-cmd]');
    if (s) { Bind.capture = { cmd: s.dataset.cmd, i: Number(s.dataset.i) }; $('#bindMsg').textContent = ''; renderBindings(); return; }
    const c = e.target.closest('[data-clear]');
    if (c) { (Bind.keys[c.dataset.clear] || []).splice(Number(c.dataset.i), 1); renderBindings(); }
  });
  $('#bindBody').addEventListener('change', (e) => {
    const inp = e.target.dataset && e.target.dataset.input;
    if (inp) Bind.mouse[inp] = e.target.value;
  });
  $('#bindSave').addEventListener('click', saveBindings);
  $('#bindCancel').addEventListener('click', closeModals);
  $('#bindClose').addEventListener('click', closeModals);

  $('#pageStrip').addEventListener('pointerdown', (e) => e.stopPropagation());  // not a swipe
  $('#pageStrip').addEventListener('click', (e) => {
    const k = e.target.dataset && e.target.dataset.k;
    const c = cur();
    if (k == null || !c || Sw.throw) return;
    Sw.page.set(c.fp, Number(k));
    flipPage(0);
  });
  $('#pageStrip').addEventListener('wheel', (e) => { e.stopPropagation(); }, { passive: true });

  // scope modal
  $('#mdList').addEventListener('change', (e) => {
    const p = e.target.dataset.p;
    if (p == null) return;
    e.target.checked ? scopeSel.add(p) : scopeSel.delete(p);
    updateScopeCount();
  });
  $('#mdSearch').addEventListener('input', () => {
    const q = $('#mdSearch').value.trim().toLowerCase();
    $$('#mdList .frow').forEach((r) => r.classList.toggle('hidden', !!q && !r.dataset.s.includes(q)));
  });
  $('#mdOk').addEventListener('click', () => { closeModals(); scopeCb && scopeCb([...scopeSel]); });
  $('#mdAll').addEventListener('click', () => { closeModals(); scopeCb && scopeCb([]); });
  $('#mdCancel').addEventListener('click', closeModals);
  $$('.modal').forEach((m) => m.addEventListener('pointerdown', (e) => { if (e.target === m) closeModals(); }));

  // ranking
  $('#rkDepth').addEventListener('change', () => { store.set('rkDepth', Number($('#rkDepth').value)); rkSelected.clear(); drawRanking(); });
  $('#rkSort').addEventListener('change', () => { store.set('rkSort', $('#rkSort').value); drawRanking(); });
  $('#rkSince').addEventListener('change', () => {
    A.sinceDays = Number($('#rkSince').value);
    store.set('rkSince', A.sinceDays);
    loadFolders(true).then(drawRanking).catch(() => {});
  });
  $('#rkStatsBtn').addEventListener('click', () => {
    const box = $('#rkStats');
    box.classList.toggle('hidden');
    $('#rkStatsBtn').classList.toggle('active', !box.classList.contains('hidden'));
    if (!box.classList.contains('hidden')) renderStats();
  });
  $('#rkSearch').addEventListener('input', drawRanking);
  $('#rkTable').addEventListener('change', (e) => {
    const tr = e.target.closest('tr[data-p]');
    if (!tr) return;
    e.target.checked ? rkSelected.add(tr.dataset.p) : rkSelected.delete(tr.dataset.p);
    updateRkSel();
  });
  $('#rkTable').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-act]');
    if (!b) return;
    const p = b.closest('tr').dataset.p;
    if (b.dataset.act === 'swipe') { resetScope([p]); showTab('swipe'); } else { Ga.scope = [p]; Ga.status = 'liked'; Ga.view = 'images'; galleryReload(); showTab('gallery'); }
  });
  $('#rkSwipe').addEventListener('click', () => { resetScope([...rkSelected]); showTab('swipe'); });
  $('#rkGallery').addEventListener('click', () => { Ga.scope = [...rkSelected]; Ga.view = 'images'; galleryReload(); showTab('gallery'); });

  // gallery
  $('#gaStatus').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-v]');
    if (!b) return;
    Ga.status = b.dataset.v;
    $('#gaAlbums').value = '';
    galleryReload();
  });
  $('#gaView').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-v]');
    if (!b) return;
    Ga.view = b.dataset.v;
    galleryReload();
  });
  $('#gaSort').addEventListener('change', (e) => { e.target.blur(); Ga.sort = e.target.value; galleryReload(); });
  $('#gaSize').value = Ga.size;
  $('#gaSize').addEventListener('input', (e) => { Ga.size = Number(e.target.value); applyGallerySize(false); });
  $('#gaSize').addEventListener('change', (e) => { e.target.blur(); Ga.size = Number(e.target.value); applyGallerySize(true); galleryMore(); });
  $('#gaFolderSort').addEventListener('change', (e) => { e.target.blur(); Ga.folderSort = e.target.value; galleryReload(); });
  $('#gaScope').addEventListener('click', () => openScope('ギャラリーの範囲', Ga.scope, (s) => { Ga.scope = s; galleryReload(); }));
  $('#gaAlbums').addEventListener('change', (e) => {
    e.target.blur();
    const a = albums.find((x) => x.name === e.target.value);
    if (!a) return;
    const kept = a.scope.filter((p) => scopeKnown(p));
    // never let a saved "only these folders" silently turn into "everything"
    if (a.scope.length && kept.length < a.scope.length) {
      toast(kept.length ? `保存したフォルダのうち ${a.scope.length - kept.length}個が見つかりません` : '保存したフォルダが見つかりません（名前の変更や移動？）');
    }
    Ga.status = a.status; Ga.sort = a.sort; Ga.view = a.view || 'images';
    Ga.scope = kept.length || !a.scope.length ? kept : a.scope;
    galleryReload();
  });
  $('#gaSave').addEventListener('click', () => {
    const name = (prompt('この条件に名前を付けて保存', '') || '').trim();
    if (!name) return;
    albums = albums.filter((x) => x.name !== name);
    albums.push({ name, status: Ga.status, sort: Ga.sort, view: Ga.view, scope: Ga.scope });
    saveAlbums();
    renderAlbums(name);
  });
  $('#gaDel').addEventListener('click', () => {
    const name = $('#gaAlbums').value;
    if (!name || !confirm(`保存した条件「${name}」を削除しますか？（写真は削除されません）`)) return;
    albums = albums.filter((x) => x.name !== name);
    saveAlbums();
    renderAlbums();
  });
  // "Swipe with these conditions": swipe exactly this gallery selection (curating what is already rated)
  $('#gaSwipe').addEventListener('click', () => {
    const f = { liked: 'liked', super: 'super', passed: 'passed', unrated: 'fresh', all: '', archived: null }[Ga.status];
    if (f === null) { toast('アーカイブした画像はスワイプに出ません（拡大表示の「元に戻す」で戻せます）'); return; }
    resetScope(Ga.scope, f);
    showTab('swipe');
  });
  $('#grid').addEventListener('click', (e) => {
    const t = e.target.closest('.ftile');
    if (t) {  // a folder tile: show that artist's images
      Ga.scope = [t.dataset.p];
      Ga.view = 'images';
      galleryReload();
      return;
    }
    const c = e.target.closest('.cell');
    if (c) lbOpen(Number(c.dataset.i));
  });
  new IntersectionObserver((es) => { if (es.some((x) => x.isIntersecting)) galleryMore(); }, { root: $('#gridWrap'), rootMargin: '600px' }).observe($('#gridEnd'));

  // lightbox
  $('#lbPrev').addEventListener('click', () => { lbStep(-1); lbTick(); });
  $('#lbNext').addEventListener('click', () => { lbStep(1); lbTick(); });
  $('#lightbox').addEventListener('click', lbClick);
  $('#lightbox').addEventListener('dblclick', lbDblClick);
  $('#lbClose').addEventListener('click', lbClose);
  $('#lbReveal').addEventListener('click', () => reveal(lbItem().shown));
  $('#lbArchive').addEventListener('click', () => { const { it } = lbItem(); if (it) lbArchive(!!it.archived); });
  $('#lbPlay').addEventListener('click', () => lbPlay(!Lb.playing));
  $('#lbShuffle').addEventListener('click', () => startSlideshow(true));
  $('#lbSpeed').addEventListener('change', (e) => { store.set('lbSpeed', e.target.value); e.target.blur(); lbTick(); });
  $('#lbSpeed').value = store.get('lbSpeed', '5');
  $('#lightbox').addEventListener('wheel', lbWheel, { passive: false });
  $('#lightbox').addEventListener('mousemove', lbWake);
  $('#gaShow').addEventListener('click', () => startSlideshow(false));
  document.addEventListener('fullscreenchange', () => {
    if (!document.fullscreenElement && Lb.playing) lbPlay(false);  // Esc in fullscreen pauses
  });

  // settings
  $('#stSave').addEventListener('click', saveSettings);
  $('#stAdvanced').addEventListener('change', (e) => {
    document.body.classList.toggle('advanced', e.target.checked);
    saveQuick({ advanced_mode: e.target.checked ? 1 : 0 });
  });
  $('#stThrowMode').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-v]');
    if (!b) return;
    $$('#stThrowMode button').forEach((x) => x.classList.toggle('on', x === b));
    saveQuick({ throw_slow: b.dataset.v });
  });
  $('#stOver').addEventListener('change', (e) => saveQuick({ over_points: e.target.checked ? 1 : 0 }));
  $('#stThrow').addEventListener('change', (e) => { e.target.blur(); saveQuick({ throw_delay_ms: e.target.value }); });
  $('#stLong').addEventListener('change', (e) => { e.target.blur(); saveQuick({ long_press_ms: e.target.value }); });
  $('#stShare').addEventListener('change', (e) => { e.target.blur(); saveQuick({ review_share: e.target.value }); resetScope(Sw.scope); });
  // On a phone the folder is saved as soon as it is picked (the save button is far below).
  const pickedOnPhone = (r) => {
    if (r && r.path && isPhone()) saveSettings();
  };
  $('#stPick').addEventListener('click', async () => {
    const path = await browseFolder();
    if (path) $('#stRoot').value = path;
    pickedOnPhone({ path });
  });
  $('#stSaveFolders').addEventListener('click', () => saveSettings());
  // touch buttons under the card; they must not start a drag of the card behind them
  $('#swButtons').addEventListener('pointerdown', (e) => e.stopPropagation());
  $('#swButtons').addEventListener('click', (e) => {
    const b = e.target.closest('button[data-cmd]');
    if (b) runCommand(b.dataset.cmd);
  });
  $('#stDepth').addEventListener('input', updateDepthNote);
  $('#stRemoteStart').addEventListener('click', startRemote);
  $('#remoteAgain').addEventListener('click', startRemote);
  $('#remoteClose').addEventListener('click', () => { $('#remoteModal').classList.add('hidden'); pollRemote(); });
  $('#stRemoteOff').addEventListener('click', async () => {
    await postJ('/api/config?' + qs({ remote_access: 0 })).catch(() => toast('保存できませんでした'));
    refreshRemote();
  });
  $('#stRemoteForget').addEventListener('click', () => {
    if (confirm('登録したスマホをすべて解除します。もう一度使うときは登録し直してください。')) postJ('/api/remote/forget').then(refreshRemote).catch(() => {});
  });
  $('#stAddRoot').addEventListener('click', async () => {
    const path = await browseFolder();
    if (path) addRootRow(path);
    pickedOnPhone({ path });
  });
  $('#stPickBackup').addEventListener('click', async () => {
    const r = await postJ('/api/pickfolder').catch(() => null);
    if (r && r.path) $('#stBackup').value = r.path;
  });
  $('#stRescan').addEventListener('click', () => postJ('/api/rescan').then(() => { $('#stMsg').textContent = '再スキャンを開始しました'; }));
  // Closing the window while a card is still in the air: land it, so the outbox keeps the rating.
  window.addEventListener('pagehide', () => { commitThrow(); Perf.flush(); });
  // Android: open Choosen V3 from now on (the app remembers the choice)
  $('#stV3').addEventListener('click', () => {
    window.ChoosenAndroid.setUi('v3');
    location.href = '/v3/';
  });
  $('#stQuit').addEventListener('click', async () => {
    commitThrow();
    if (Outbox.q.length) await Outbox.pump();
    Perf.flush();
    await postJ('/api/quit').catch(() => {});
    document.body.innerHTML = '<div class="empty"><div><div class="big">Choosen を終了しました</div>このウィンドウは閉じて構いません。</div></div>';
    setTimeout(() => window.close(), 300);
  });
}

// The Android app's own folder browser: every folder can be chosen, including the top of the storage.
// Resolves with the chosen path, or "" when cancelled. On Windows the system's folder dialog is used.
function browseFolder() {
  if (!ANDROID) return postJ('/api/pickfolder').then((r) => (r && r.path) || '').catch(() => '');
  return new Promise((resolve) => {
    const m = $('#dirModal');
    let cur = '';
    const parents = [];
    const show = async (path) => {
      const r = await getJ('/api/dirs?' + qs({ path })).catch(() => ({ path, dirs: [] }));
      cur = r.path;
      $('#dirPath').textContent = cur || '場所を選んでください';
      $('#dirUse').disabled = !cur;
      $('#dirUp').disabled = !parents.length;
      $('#dirList').replaceChildren(...r.dirs.map((d) => {
        const b = document.createElement('button');
        b.className = 'dir-row';
        b.textContent = '📁 ' + d.name;
        b.onclick = () => { parents.push(cur); show(d.path); };
        return b;
      }));
      if (!r.dirs.length) $('#dirList').innerHTML = '<p class="hint" style="padding:0 14px">サブフォルダはありません。このフォルダを使えます。</p>';
    };
    const done = (path) => { m.classList.add('hidden'); resolve(path); };
    $('#dirUp').onclick = () => show(parents.pop() ?? '');
    $('#dirCancel').onclick = () => done('');
    $('#dirUse').onclick = () => done(cur);
    m.classList.remove('hidden');
    show('');
  });
}

// Phone layout: the Android app, or a narrow window (e.g. a phone using the PC library).
const isPhone = () => ANDROID || innerWidth <= 760;
function applyPhone() { document.body.classList.toggle('phone', isPhone()); }

async function boot() {
  await pairFromLink();  // before the first request, so it already carries the phone's token
  document.body.classList.toggle('android', ANDROID);  // the app hides what only the PC can do
  applyPhone();
  addEventListener('resize', applyPhone);
  if (ANDROID) $('#stRoot').placeholder = '例: /storage/emulated/0/Pictures';
  bind();
  loadKeyLayout();
  Outbox.adopt();
  $('#swScope').textContent = scopeLabel(Sw.scope);
  $('#swFilter').value = Sw.filter;
  try { onState(await getJ('/api/state')); } catch { if ($('#pairModal').classList.contains('hidden')) $('#offline').classList.remove('hidden'); }
  updateModeUI();
  connect();
  let startTab = A.st && !A.st.hasRoot ? 'settings' : store.get('tab', 'swipe');
  if (A.st && A.st.remote && startTab === 'settings') startTab = 'swipe';
  showTab(startTab);
  refill();
  Outbox.pump();  // ratings left unsent by a previous window go out first
  loadFolders().then(() => {
    $('#swScope').textContent = scopeLabel(Sw.scope);
    $('#swScope').classList.toggle('active', Sw.scope.length > 0);
    renderInfo();
  }).catch(() => {});
  loadAlbums();
}

boot();
