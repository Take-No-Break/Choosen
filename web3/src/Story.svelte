<script lang="ts">
  import { t } from './lib/i18n';
  // The gallery's full-screen views, over the same glowing light as the swipe screen. The pictures
  // reach the screen's sides; the light shows above and below them.
  //   play (▶): the pictures shuffled, story style. One white bar fills once a round; a single
  //     picture changes at its end, with two or four each stays for at least the selected seconds
  //     and changes once a round at its own moment, and
  //     a パズル page changes as a whole. Tap or swipe right / left for the next or the previous set;
  //     a tap in the middle pauses or goes on (no mark shows); hold to pause (the bar and buttons step
  //     aside). Two fingers spread for fewer, larger pictures (4 → 2 → 1) or pinch for more, as the
  //     gallery sizes its tiles (Ctrl + wheel on a PC). The screen stays on.
  //   browse (スライド): the gallery's pictures in its own order. モード スクロール (the default): one
  //     endless sideways strip that scrolls freely like the gallery's grid; ページ: a set at a time
  //     that follows the finger and slides in. A tap shows or hides the buttons and a round ▶ in the
  //     middle (shown, they step aside by themselves after a few seconds). ▶: the strip flows left by
  //     itself (a finger on it pauses it), or the sets turn every few seconds; the screen stays on
  //     meanwhile.
  // In both, a double tap on a picture = 好評 and a triple tap = 超好評; either way it also goes into
  // the サブギャラリー. A long press (0.9 s) shows that picture alone. One, two or four at a time
  // (スライド sideways: up to ten, in one row or two), or
  // パズル: pages of one to four in the arrangement their shapes suit (lib/puzzle.ts). Each picture is
  // shown whole (or filled) with rounded corners; whole pictures share one blurred background across
  // the screen. With
  // two or more on screen, ↓ saves them as one picture (a collage) on this device. ⚙ (a tap anywhere
  // else closes it) also sets the seconds and play's pictures. Kept per device.
  import { onMount, tick, untrack } from 'svelte';
  import { getJ, postJ, imgUrl, thumbUrl, saveImage, ANDROID, IOS, type Card } from './lib/api';
  import { newCid } from './lib/outbox';
  import { Ambient, paletteOf } from './lib/ambient';
  import { collage, collageName, fitRect } from './lib/collage';
  import { storyTiming } from './lib/story-timing';
  import { puzzlePages, slantPage, roundClip, type Cell, type Page } from './lib/puzzle';
  import { TAP_SLOP, pairOf, pinchCount, pinchScale } from './lib/gesture';
  import { fx } from './lib/fx.svelte';
  import { TapSeries } from './lib/taps';
  import { Drift } from './lib/drift';
  import { keepAwake } from './lib/awake';
  import { svg } from './ui/icons';
  import Viewer from './Viewer.svelte';
  import StoryBackground from './StoryBackground.svelte';

  import { Handover } from './lib/handover';

  let {
    filter,
    scope,
    onclose,
    browse = false,
    sort = 'score',
    seed = 0,
  }: {
    filter: string;
    scope: string;
    onclose: () => void;
    browse?: boolean; // スライド: by hand, in the gallery's order
    sort?: string;
    seed?: number;
  } = $props();

  interface Opts {
    seconds: number; // play: minimum picture time (a staggered round is a little longer)
    count: number; // 1, 2, 4; スライド sideways also 6, 8, 10
    rowsSide: 1 | 2; // スライド sideways, with 6 or more: one row, or two
    puzzle: boolean; // パズル pages instead of `count` at a time (the count stays for going back)
    fill: boolean; // filled (cropped) instead of whole
    kind: string; // play: '' = as in the gallery; liked, likeOnly, super, picks (the sub gallery)
    speed: number; // browse スクロール: 1..5, an index into SPEEDS (+1)
  }
  const SECONDS = [3, 5, 8, 12];
  // スライド スクロール's 速度: how fast the strip flows, in px per second
  const SPEEDS = [24, 45, 75, 120, 190];
  const KINDS: [string, string][] = [
    ['', t('ギャラリーと同じ')],
    ['likeOnly', t('♥ 好評')],
    ['super', t('★ 超好評')],
    ['liked', t('♥★ 両方')],
    ['picks', t('サブギャラリー')],
  ];
  // svelte-ignore state_referenced_locally
  const KEY = browse ? 'choosen3.browse' : 'choosen3.story';
  function readOpts(): Opts {
    const d: Opts = { seconds: 5, count: 1, rowsSide: 2, puzzle: false, fill: false, kind: '', speed: 3 };
    try {
      const o = JSON.parse(localStorage.getItem(KEY) ?? '{}');
      return {
        seconds: SECONDS.includes(o.seconds) ? o.seconds : d.seconds,
        count: [1, 2, 4, 6, 8, 10].includes(o.count) ? o.count : d.count,
        rowsSide: o.rowsSide === 1 ? 1 : 2,
        puzzle: o.puzzle === true, // options saved before パズル have none
        fill: !!o.fill,
        kind: KINDS.some(([k]) => k === o.kind) ? o.kind : d.kind,
        speed: [1, 2, 3, 4, 5].includes(o.speed) ? o.speed : d.speed,
      };
    } catch {
      return d;
    }
  }
  let opts = $state(readOpts());
  function save() {
    try {
      localStorage.setItem(KEY, JSON.stringify(opts));
    } catch {
      /* private mode */
    }
  }

  // ---- the pictures: play shuffles them (an even mix, whatever order they came in); browse keeps
  // the gallery's order and loads it a page at a time
  let items: Card[] = $state.raw([]);
  let loaded = $state(false);
  let total = $state(0);
  let at = $state(0); // play: the next picture to come (パズル: the first on screen); browse: the first on screen
  function shuffle<T>(a: T[]): T[] {
    for (let i = a.length - 1; i > 0; i--) {
      const j = Math.floor(Math.random() * (i + 1));
      [a[i], a[j]] = [a[j], a[i]];
    }
    return a;
  }
  async function load() {
    loaded = false;
    clearAll();
    pages = [];
    seek = null;
    measured = 0;
    if (browse) {
      items = [];
      total = 0;
      await page();
    } else {
      const kind = opts.kind || filter;
      const r =
        kind === 'picks'
          ? await getJ<{ items: Card[] }>('/api/picks').catch(() => ({ items: [] as Card[] }))
          : await getJ<{ items: Card[] }>('/api/items', {
              filter: kind,
              sort: 'random',
              seed: Math.floor(Math.random() * 1e9),
              offset: 0,
              limit: 300,
              scope,
            }).catch(() => ({ items: [] as Card[] }));
      items = shuffle([...r.items]);
      total = items.length;
    }
    at = 0;
    loaded = true;
    show();
  }
  // one load at a time: the strip asks on every scroll near its end
  let paging: Promise<void> | null = null;
  function page(): Promise<void> {
    return (paging ??= more()
      .then(refresh)
      .finally(() => (paging = null)));
  }
  async function more() {
    if (filter === 'picks') {
      if (items.length) return;
      const r = await getJ<{ items: Card[] }>('/api/picks').catch(() => ({ items: [] as Card[] }));
      items = r.items;
      total = items.length;
      return;
    }
    if (items.length && items.length >= total) return;
    const r = await getJ<{ total: number; items: Card[] }>('/api/items', {
      filter,
      sort,
      seed,
      offset: items.length,
      limit: 120,
      scope,
    }).catch(() => null);
    if (!r) return;
    total = r.total;
    items = [...items, ...r.items];
  }

  // ---- where the pictures go: the cells of a set, or the strip (browse, スクロール)
  const GAP = 3;
  const strip = $derived(browse); // スライド: always the endless strip
  const puzzle = $derived(opts.puzzle);
  let stripEl: HTMLElement | undefined = $state();
  let stripW = $state(0);
  let stripH = $state(0);
  let cellsEl: HTMLElement | undefined = $state();
  let cellsW = $state(0);
  let cellsH = $state(0);
  const sideways = $derived(stripW > stripH);
  // play: one, two or four; スライド sideways also six, eight or ten (as a phone in portrait: up to four)
  const count = $derived(browse && sideways ? opts.count : Math.min(4, opts.count));
  const rows = $derived(count === 1 ? 1 : count > 4 ? opts.rowsSide : count === 4 ? 2 : sideways ? 1 : 2);
  const cols = $derived(Math.max(1, Math.ceil(count / rows)));
  const cellW = $derived(Math.max(1, (stripW - GAP * (cols - 1)) / cols));
  const boxW = $derived(strip ? stripW : cellsW); // the area a パズル page fills
  const padY = $derived(Math.round(stripH * 0.04)); // the strip's room above and below its pictures
  const boxH = $derived(strip ? stripH - 2 * padY : cellsH);

  // ---- パズル: pages laid out in order from the first picture, as the pictures' shapes become known.
  // A shape comes from the picture's small thumbnail (it keeps the picture's shape), measured a few at
  // a time ahead of what is on screen and kept by card id.
  const AHEAD = 16; // pictures measured ahead of the first one on screen
  const AT_ONCE = 4; // thumbnails asked for at a time: the browser's few connections stay free for the pictures
  const shapes = new Map<number, number>(); // card id -> width / height
  let measured = 0; // the items before this one have been asked for
  let measuring = 0;
  let shapeFrame = 0;
  let pages = $state.raw<Page[]>([]);
  let pi = $state(0); // the page on screen
  let seek = $state<number | null>(null); // a picture to bring on screen once its page is laid out
  const laidOut = $derived(!!pages.length && pages[pages.length - 1].start + pages[pages.length - 1].count >= total);
  /** Lays out the pages that can be decided now, after the ones there are. */
  function layMore() {
    if (!opts.puzzle || !(boxW > 0 && boxH > 0) || !items.length) return;
    const last = pages[pages.length - 1];
    const from = last ? last.start + last.count : 0;
    if (from >= items.length) return;
    const ratios = items.map((c) => shapes.get(c.id) ?? 0);
    // slanted cuts between the pictures, like manga panels (the page's sides stay straight)
    const more = puzzlePages(ratios, { w: boxW, h: boxH }, GAP, !browse || items.length >= total, from).map((p) => ({ ...p, cells: slantPage(p, GAP) }));
    if (more.length) pages = [...pages, ...more];
  }
  /** The laid-out page with picture i on it, or -1. */
  function pageOf(i: number): number {
    let lo = 0;
    let hi = pages.length - 1;
    while (lo <= hi) {
      const m = (lo + hi) >> 1;
      if (i < pages[m].start) hi = m - 1;
      else if (i >= pages[m].start + pages[m].count) lo = m + 1;
      else return m;
    }
    return -1;
  }
  /** Measures the shapes needed next, a few at a time; asks for more of the list when they run out. */
  function measure() {
    if (!opts.puzzle) return;
    const need = Math.max(seek ?? 0, at) + AHEAD;
    if (browse && need > items.length && items.length < total) void page();
    for (const end = Math.min(items.length, need); measuring < AT_ONCE && measured < end; ) {
      const c = items[measured++];
      if (shapes.has(c.id)) continue;
      measuring++;
      const img = new Image();
      img.onload = img.onerror = () => {
        shapes.set(c.id, img.naturalWidth > 0 && img.naturalHeight > 0 ? img.naturalWidth / img.naturalHeight : 3 / 2);
        measuring--;
        shapeFrame ||= requestAnimationFrame(() => {
          shapeFrame = 0;
          refresh();
        });
      };
      img.src = thumbUrl(c, 64);
    }
  }
  /** New shapes or pictures: lays out what it can and brings on the page a picture waits for. */
  function refresh() {
    if (!opts.puzzle) return;
    layMore();
    if (seek !== null && pageOf(seek) >= 0) {
      if (strip) {
        seek = null;
        void toStrip();
      } else show(); // shows the page with `at` (= seek)
    }
    measure();
  }
  /** パズル came on or the area changed size (a turned phone): pages anew, the first picture kept on screen. */
  function relayout() {
    const keep = seek ?? at;
    pages = [];
    seek = null;
    if (!opts.puzzle || !items.length) return;
    at = keep;
    if (strip) {
      seek = keep;
      refresh();
    } else show();
  }
  $effect(() => {
    void [boxW, boxH, puzzle];
    untrack(relayout);
  });

  // ---- the cells on screen
  interface Slot {
    card: Card;
    key: number;
    box?: Cell; // パズル: its place on the page
  }
  let slots = $state.raw<Slot[]>([]);
  // play: the cells that are being replaced, each at its place (pos), kept until every new cell has
  // its pictures in (a page turn would otherwise show the new cells' blurred colours a moment late)
  let leaving = $state.raw<(Slot & { pos: number; gone?: boolean })[]>([]);
  let readyBackground = $state.raw<Card | undefined>();
  const handover = new Handover<Slot>((l) => (leaving = l));
  function letGo(old: Slot[], at?: number[]) {
    if (!browse) handover.letGo(old, at);
  }
  /** A cell has all its pictures (or waited long enough): once every current one has, the old go. */
  function cellReady(node: HTMLElement, key: number) {
    let done = false;
    const imgs = [...node.querySelectorAll('img')];
    const pic = node.querySelector<HTMLImageElement>('img.pic');
    const shown = () => !pic || (pic.complete && pic.naturalWidth > 0);
    // let in before its photo is there (slow, or failed): its own blurred copy stands in until the
    // photo comes, so the page never shows an empty cell over the new picture's background
    const photoIn = () => node.classList.remove('stand-in');
    pic?.addEventListener('load', photoIn);
    const mark = () => {
      if (done) return;
      done = true;
      node.classList.toggle('stand-in', !shown());
      node.classList.add('ready');
      if (slots[0]?.key === key) readyBackground = slots[0].card;
      handover.markReady(key, slots);
    };
    const check = () => imgs.every((i) => i.complete) && mark();
    imgs.forEach((i) => i.addEventListener('load', check, { once: true }));
    imgs.forEach((i) => i.addEventListener('error', check, { once: true }));
    const late = setTimeout(mark, 1500); // a picture that never comes does not hold the page
    check();
    return { destroy: () => (clearTimeout(late), pic?.removeEventListener('load', photoIn), handover.forget(key), pins.delete(key)) };
  }
  let keySeq = 0;
  const nextCard = () => items[at++ % items.length];
  const ahead = (from: number, n: number) => {
    for (let i = 0; i < n && items.length; i++) {
      const c = items[(((from + i) % items.length) + items.length) % items.length];
      // decoded ahead, so it is there the moment it is shown, with its blurred copy
      for (const src of [imgUrl(c), thumbUrl(c, 64)]) {
        const im = new Image();
        im.src = src;
        im.decode?.().catch(() => {});
      }
    }
  };
  /**
   * A fresh set: play's next pictures (and a new round), or browse's from `at`; パズル: the page with
   * `at`. keep (play): these come first, the next pictures after them.
   */
  function show(keep: Card[] = []) {
    clearAll();
    if (!items.length) return void (slots = []);
    if (opts.puzzle) return showPage();
    const n = Math.min(count, items.length);
    if (browse) {
      slots = items.slice(at, at + n).map((card) => ({ card, key: ++keySeq })); // the strip shows them all
      return;
    }
    letGo(slots);
    slots = Array.from({ length: n }, (_, k) => ({ card: keep[k] ?? nextCard(), key: ++keySeq }));
    startRound();
    ahead(at, n);
  }
  function showPage() {
    layMore();
    const k = pageOf(at);
    if (k < 0) {
      seek = at; // it comes once its pictures' shapes are known (a spinner meanwhile)
      slots = [];
      measure();
      return;
    }
    seek = null;
    pi = k;
    const p = pages[k];
    at = p.start;
    letGo(slots);
    slots = p.cells.map((c) => ({ card: items[c.i], key: ++keySeq, box: c }));
    measure();
    ahead(p.start + p.count, 4);
    if (!browse) startRound();
  }

  // ---- play's clocks (and スライド's ▶ in ページ): held, or the ⚙ open, every clock waits and goes
  // on with the time it had left
  const timers = new Map<string, { t: number; start: number; left: number; fn: () => void }>();
  function arm(key: string, v: { t: number; start: number; left: number; fn: () => void }) {
    v.start = performance.now();
    v.t = window.setTimeout(() => {
      timers.delete(key);
      v.fn();
    }, Math.max(0, v.left));
  }
  function later(key: string, ms: number, fn: () => void) {
    clearTimeout(timers.get(key)?.t);
    const v = { t: 0, start: performance.now(), left: ms, fn };
    timers.set(key, v);
    if (!still) arm(key, v);
  }
  function cancel(key: string) {
    clearTimeout(timers.get(key)?.t);
    timers.delete(key);
  }
  function clearAll() {
    timers.forEach((v) => clearTimeout(v.t));
    timers.clear();
  }
  let held = $state(false);
  let showOpts = $state(false);
  let single: Card | null = $state(null); // a picture shown alone (a long press)
  let paused = $state(false); // play: a tap in the middle stops the clock, another goes on (no mark shows)
  const still = $derived(held || showOpts || paused || single !== null);
  $effect(() => {
    if (still) {
      for (const v of timers.values()) {
        clearTimeout(v.t);
        v.left -= performance.now() - v.start;
      }
    } else {
      for (const [k, v] of timers) {
        clearTimeout(v.t);
        arm(k, v);
      }
    }
  });
  // The selected time is a minimum for each picture; a multi-picture round leaves extra room
  // for staggered changes, so neither the first change nor a later replacement comes early.
  let round = $state(0);
  const timing = $derived(storyTiming(opts.seconds, slots.length > 1 && !opts.puzzle));
  const roundMs = $derived(timing.round);
  function startRound() {
    round++;
    later('round', roundMs, endRound);
    if (slots.length > 1 && !opts.puzzle) slots.forEach((_, i) => later('cell' + i, timing.minimum + Math.random() * timing.spread, () => change(i)));
  }
  function endRound() {
    if (opts.puzzle) {
      if (!turn(1)) later('round', 400, endRound); // the next page is not laid out yet
      return;
    }
    if (slots.length === 1) change(0);
    startRound();
  }
  function change(i: number) {
    const s = { card: nextCard(), key: ++keySeq };
    if (slots[i]) letGo([slots[i]], [i]);
    slots = slots.map((x, k) => (k === i ? s : x));
    ahead(at, 1);
  }

  // ---- next / previous set (true if it moved)
  async function go(d: 1 | -1): Promise<boolean> {
    if (!items.length) return false;
    if (opts.puzzle) return turn(d);
    const n = slots.length || 1;
    if (d < 0) at = (((at - 2 * n) % items.length) + items.length) % items.length;
    show();
    return true;
  }
  /** パズル: the next or the previous page; play goes round. False if there is none (yet). */
  function turn(d: 1 | -1): boolean {
    const p = pages[pi];
    if (!p || seek !== null) return false;
    let to: number;
    if (d > 0) {
      to = p.start + p.count;
      if (to >= total) {
        if (browse) return false;
        to = 0;
      }
    } else if (pi > 0) to = pages[pi - 1].start;
    else if (!browse && laidOut) to = pages[pages.length - 1].start;
    else return false;
    if (!browse && pageOf(to) < 0) {
      measure(); // play keeps its page until the next one is laid out
      return false;
    }
    at = to;
    show();
    return true;
  }

  // ---- スライド's buttons: a tap shows or hides them, with the round ▶ in the middle; shown, they
  // step aside by themselves after a few seconds without a touch (never while ⚙ is open)
  let ui = $state(true);
  let uiTimer = 0;
  function showUi(ms = 3000) {
    ui = true;
    clearTimeout(uiTimer);
    uiTimer = window.setTimeout(() => (showOpts ? showUi(ms) : (ui = false)), ms);
  }
  function hideUi() {
    ui = false;
    clearTimeout(uiTimer);
  }

  // ---- ▶ in スライド: the strip flows left by itself, a column (パズル: a page) every `seconds`; in
  // ページ the sets turn every `seconds`. It stops at the very end.
  let auto = $state(false);
  function toggleAuto() {
    auto = !auto;
    if (!auto) {
      settleStrip();
      return showUi();
    }
    snapOn = false; // flowing: no snapping
    if (stripEl && stripEl.scrollLeft >= flowMax() - 1 && allIn()) stripEl.scrollLeft = 0; // at the end: from the start
    showUi(1200);
  }
  // the strip snaps to a picture (a column, or a パズル page) when it is moved by hand; while it flows
  // by itself it does not, and once it stops it glides to the nearest picture first
  let snapOn = $state(true);
  let snapTimer = 0;
  function settleStrip() {
    if (!strip || !stripEl) return;
    const step = (opts.puzzle ? stripW : cellW) + GAP;
    const to = Math.min(stripEl.scrollWidth - stripEl.clientWidth, Math.round(stripEl.scrollLeft / step) * step);
    stripEl.scrollTo({ left: to, behavior: 'smooth' });
    clearTimeout(snapTimer);
    snapTimer = window.setTimeout(() => (snapOn = !auto), 450);
  }
  let fingers = 0; // touches on the strip (they go on through its native scrolling)
  let pressing = false; // a pointer is down on the strip
  let drift: Drift | null = null;
  /** How far the strip may flow: its end, or with パズル the last page laid out so far. */
  function flowMax(): number {
    if (!stripEl) return 0;
    const end = stripEl.scrollWidth - stripEl.clientWidth;
    return opts.puzzle && !laidOut ? Math.min(end, Math.max(0, pages.length - 1) * (stripW + GAP)) : end;
  }
  /** Nothing more comes after the strip's end. */
  const allIn = () => (opts.puzzle ? laidOut : items.length >= total);
  $effect(() => {
    if (!auto || !strip) return;
    const d = (drift = new Drift());
    let raf = requestAnimationFrame(function frame(now) {
      raf = requestAnimationFrame(frame);
      if (!stripEl || seek !== null) return;
      const to = d.frame(now, stripEl.scrollLeft, flowMax(), SPEEDS[opts.speed - 1], fingers > 0 || pressing || showOpts || !!single);
      if (to !== null) stripEl.scrollLeft = to;
      else if (d.ended && allIn()) {
        auto = false; // the very end
        settleStrip();
        showUi();
      }
    });
    return () => {
      cancelAnimationFrame(raf);
      drift = null;
    };
  });
  // the screen stays on while playing, and in スライド while ▶ runs
  $effect(() => {
    if (browse && !auto) return;
    return keepAwake();
  });

  // ---- touch: play has hold = pause and a swipe = next / previous (the strip scrolls by itself).
  // Taps are counted below.
  let pointer = -1;
  let down = { x: 0, y: 0, t: 0 };
  let holdTimer = 0;
  // a press this long is no tap: in play it is a hold (it pauses)
  const HOLD_MS = 260;
  // a press well longer than a hold: that picture alone, as large as it goes (play and スライド)
  const LONG_MS = 900;
  let longTimer = 0;
  let longDone = false;
  function longPress(x: number, y: number) {
    clearTimeout(longTimer);
    longDone = false;
    longTimer = window.setTimeout(() => {
      const card = cardAt(x, y);
      if (!card) return;
      longDone = true;
      held = false;
      single = card;
      navigator.vibrate?.(10);
    }, LONG_MS);
  }
  let swallow = false; // a touch that closed ⚙: its other fingers do nothing either
  function onDown(e: PointerEvent) {
    const t = e.target as Element;
    const touch = !browse && e.pointerType === 'touch';
    if (e.isPrimary) swallow = false;
    if (swallow) return;
    if (touch && e.isPrimary) {
      // a new touch: no other finger is down (one whose lift the page never saw is forgotten)
      if (pinched) endPinch(true);
      pts.clear();
      pointer = -1;
    }
    if (t.closest('.single')) return; // the picture shown alone has its own touch
    if (browse && t.closest('.top, .opts')) showUi(); // the buttons are in use: they stay
    if (showOpts && !t.closest('.opts')) {
      if (!t.closest('button')) {
        showOpts = false; // a tap anywhere else closes ⚙ (and does nothing more)
        if (e.pointerType === 'touch') swallow = true;
        if (browse) showUi();
      }
      return;
    }
    if (t.closest('button, .opts, .strip') || e.button > 0) return; // the strip scrolls by itself
    if (touch && (pointer >= 0 || pinched)) return void addFinger(e); // another finger: a pinch
    if (pointer >= 0) return;
    pointer = e.pointerId;
    down = { x: e.clientX, y: e.clientY, t: e.timeStamp };
    if (touch) pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
    try {
      (e.currentTarget as Element).setPointerCapture(e.pointerId);
    } catch {
      /* a synthetic pointer */
    }
    if (!browse) holdTimer = window.setTimeout(() => (held = true), HOLD_MS);
    longPress(e.clientX, e.clientY);
  }
  function onMove(e: PointerEvent) {
    if (pts.has(e.pointerId)) pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pinched) return void followPinch();
    if (e.pointerId !== pointer) return;
    const dx = e.clientX - down.x, dy = e.clientY - down.y;
    if (Math.hypot(dx, dy) > 12) {
      clearTimeout(longTimer); // moving: not a long press
      clearTimeout(holdTimer); // a swipe, not a hold
    }
  }
  function onUp(e: PointerEvent) {
    pts.delete(e.pointerId);
    if (pinched) {
      if (e.pointerId === pointer) pointer = -1;
      if (e.type === 'pointercancel') pinchCancelled = true; // the system took a finger: the pinch counts for nothing
      if (pts.size >= 2) startPinch(); // a finger left: the next pair goes on from the scale there is
      else if (!pts.size) endPinch(pinchCancelled);
      return; // until every finger is up: no tap, hold, long press or page turn
    }
    if (e.pointerId !== pointer) return;
    pointer = -1;
    clearTimeout(holdTimer);
    clearTimeout(longTimer);
    if (longDone) {
      longDone = false; // the long press opened the picture alone: nothing more
      return;
    }
    const dx = e.clientX - down.x, dy = e.clientY - down.y;
    if (held) {
      held = false; // a hold ends: it goes on
      return;
    }
    if (!browse && e.type !== 'pointercancel' && Math.abs(dx) > 50 && Math.abs(dx) > Math.abs(dy)) return void go(dx < 0 ? 1 : -1);
    if (e.type === 'pointercancel' || Math.hypot(dx, dy) > 12) return;
    taps.tap(e.clientX, e.clientY, e.timeStamp);
  }

  // ---- two fingers (play): spread = fewer, larger pictures (4 → 2 → 1), pinch = more (1 → 2 → 4), as
  // the gallery sizes its tiles: the set follows the fingers and the new count comes when they lift
  // (past 1.8x two steps). The pictures on screen stay (the one under the fingers first when fewer).
  // パズル gives way to the count. Ctrl + wheel does a step on a PC.
  const pts = new Map<number, { x: number; y: number }>(); // touches on the pictures (play)
  let pinch: { d0: number; k: number; card?: Card } | null = null;
  let pinched = false; // from the second finger until the last one is up
  let pinchCancelled = false; // a finger of this pinch was cancelled
  function addFinger(e: PointerEvent) {
    if (!slots.length || (!pts.size && !pinched)) return; // nothing to scale, or the first finger is not ours
    pts.set(e.pointerId, { x: e.clientX, y: e.clientY });
    try {
      (e.currentTarget as Element).setPointerCapture(e.pointerId);
    } catch {
      /* a synthetic pointer */
    }
    if (pts.size === 2) startPinch();
  }
  function startPinch() {
    const [a, b] = [...pts.values()];
    const p = pairOf(a, b);
    const k = pinch?.k ?? 1;
    pinch = { d0: Math.max(1, p.d) / k, k, card: pinch ? pinch.card : cardAt(p.cx, p.cy) };
    if (pinched) return; // another pair: the scale goes on from where it is
    pinched = true;
    pinchCancelled = false;
    clearTimeout(holdTimer);
    clearTimeout(longTimer);
    taps.interrupt(); // a waiting tap does not turn the page under the fingers
    held = true; // the clocks wait and the buttons step aside, as while held
    const el = cellsEl;
    if (!el) return;
    const r = el.getBoundingClientRect();
    el.style.transition = 'none';
    el.style.willChange = 'scale';
    el.style.transformOrigin = `${p.cx - r.left}px ${p.cy - r.top}px`;
  }
  function followPinch() {
    if (!pinch || pts.size < 2) return;
    const [a, b] = [...pts.values()];
    pinch.k = pinchScale(pairOf(a, b).d / pinch.d0, stepFrom(), opts.puzzle);
    if (cellsEl) cellsEl.style.scale = String(pinch.k);
  }
  function endPinch(cancelled: boolean) {
    const p = pinch;
    pinch = null;
    pinched = false;
    held = false;
    let next = !p || cancelled ? null : pinchCount(stepFrom(), p.k, opts.puzzle);
    if (next === count && !opts.puzzle) next = null;
    // a step: the outgoing pictures are pinned where they are drawn, still scaled (pinCells)
    if (next !== null) applyCount(next, p?.card);
    const el = cellsEl;
    if (el) {
      // no step: back to its size (at once with 演出 なし)
      el.style.transition = next === null && el.style.scale && fx.level !== 'off' ? 'scale 180ms var(--ease-out)' : 'none';
      el.style.scale = '';
      el.style.willChange = '';
    }
  }
  /** Play: what a pinch steps from: the setting (パズル: the pictures on its page). */
  const stepFrom = () => (opts.puzzle ? slots.length : count);
  /** Play: `next` at a time (パズル gives way); the pictures on screen stay, the one under the fingers first. */
  function applyCount(next: number, anchor?: Card) {
    const cards = slots.map((s) => s.card);
    const keep = anchor && next < cards.length ? [anchor, ...cards.filter((c) => c !== anchor)] : cards;
    const pg = opts.puzzle ? pages[pi] : undefined;
    if (pg && items.length) at = (pg.start + pg.count) % items.length; // `at` = the next to come again
    setOpts({ count: next, puzzle: false }, keep.slice(0, next));
  }
  let wheelSum = 0;
  let wheelAt = -1e9;
  let wheelDone = false;
  function onWheel(e: WheelEvent) {
    if (browse || !e.ctrlKey || single) return;
    e.preventDefault(); // not the window's zoom
    if (e.timeStamp - wheelAt > 300) ((wheelSum = 0), (wheelDone = false)); // a new turn of the wheel
    wheelAt = e.timeStamp;
    if (wheelDone) return;
    wheelSum += e.deltaY;
    if (Math.abs(wheelSum) < 40 || !slots.length) return;
    wheelDone = true; // one step a turn (it ends once the wheel is still for 300 ms)
    const next = pinchCount(stepFrom(), wheelSum < 0 ? 1.3 : 0.75, opts.puzzle);
    if (next !== null && (next !== count || opts.puzzle)) applyCount(next);
  }
  // play: before a new arrangement (pictures at a time, パズル) the outgoing cells keep the place they
  // are drawn at, so they do not jump into the new grid while the new cells come in
  const pins = new Map<number, Cell>();
  let pinFrom: DOMRect | null = null; // where .cells was when the pins were taken
  function pinCells() {
    if (browse || !cellsEl) return;
    const all = [...slots, ...leaving];
    const els = [...cellsEl.querySelectorAll<HTMLElement>(':scope > .cell')];
    const rects = els.map((el) => el.getBoundingClientRect()); // as drawn: a pinch's scale included
    if (cellsEl.style.scale) {
      cellsEl.style.transition = 'none';
      cellsEl.style.scale = '';
    }
    const o = (pinFrom = cellsEl.getBoundingClientRect());
    els.forEach((el, k) => {
      const s = all.find((x) => String(x.key) === el.dataset.key);
      if (!s) return;
      const r = rects[k];
      const f = s.box?.w ? r.width / s.box.w : 1; // a slanted パズル shape grows with its cell
      const clip = s.box?.clip && f !== 1 ? s.box.clip.replace(/(-?[\d.]+)px/g, (_, v) => +(v * f).toFixed(1) + 'px') : s.box?.clip;
      pins.set(s.key, { i: -1, x: r.left - o.left, y: r.top - o.top, w: r.width, h: r.height, clip });
    });
  }
  /** After a new arrangement is in: the pins move with .cells if it moved, so the pictures stay put. */
  async function settlePins() {
    await tick();
    const from = pinFrom;
    pinFrom = null;
    if (!from || !cellsEl || !pins.size) return;
    const o = cellsEl.getBoundingClientRect();
    const dx = o.left - from.left;
    const dy = o.top - from.top;
    if (Math.abs(dx) < 0.5 && Math.abs(dy) < 0.5) return;
    for (const [k, b] of pins) pins.set(k, { ...b, x: b.x - dx, y: b.y - dy });
    leaving = [...leaving]; // drawn again at the corrected places
  }

  // ---- taps that were neither a drag nor a scroll: one = play's next (right two thirds) or previous
  // set, スライド's buttons in or out; two = 好評, three = 超好評 (the ♥ shows at once, then waits a
  // moment for a third)
  let liking: { card: Card; pop: Pop } | null = null;
  const taps = new TapSeries({
    single(x) {
      if (!browse) {
        if (x < innerWidth / 3) void go(-1);
        else if (x > (innerWidth * 2) / 3) void go(1);
        else paused = !paused; // the middle: the clock stops, or goes on (no mark either way)
      }
      else toggleAuto(); // スライド: anywhere on the pictures = ▶ / ❚❚ (no button shows)
    },
    double(x, y) {
      const card = cardAt(x, y);
      liking = card ? { card, pop: pop(x, y, false) } : null;
      if (card) navigator.vibrate?.(8);
    },
    like() {
      if (liking) void give(liking.card, 'L');
      liking = null;
    },
    triple(x, y) {
      const card = liking?.card ?? cardAt(x, y);
      if (liking) unpop(liking.pop);
      liking = null;
      if (!card) return;
      pop(x, y, true);
      navigator.vibrate?.([8, 60, 8]);
      void give(card, 'S');
    },
  });
  function cardAt(x: number, y: number): Card | undefined {
    const id = (document.elementFromPoint(x, y) as Element | null)?.closest<HTMLElement>('.cell')?.dataset.id;
    return id === undefined ? undefined : items.find((c) => String(c.id) === id);
  }
  interface Pop {
    key: number;
    x: number;
    y: number;
    star: boolean;
  }
  let pops = $state.raw<Pop[]>([]);
  function pop(x: number, y: number, star: boolean): Pop {
    const p = { key: ++keySeq, x, y, star };
    pops = [...pops, p];
    setTimeout(() => unpop(p), 800);
    return p;
  }
  const unpop = (p: Pop) => (pops = pops.filter((q) => q !== p));
  // 好評 / 超好評 as on the swipe screen, and into the サブギャラリー: two taps make it 好評 (a 超好評
  // goes back to 好評), three make it 超好評. The card keeps its latest letter, so the same taps
  // again do not rate it again.
  let marks = $state.raw(new Map<string, 'L' | 'S'>()); // fp -> what it was given here
  async function give(c: Card, a: 'L' | 'S') {
    marks = new Map(marks).set(c.fp, a);
    const was = c.last;
    const rate = was !== a;
    if (rate) c.last = a;
    await postJ('/api/picks', { fp: c.fp, add: 1 }).catch(() => {});
    if (rate) await postJ('/api/rate', { id: c.id, fp: c.fp, a, cid: newCid() }).catch(() => c.last === a && (c.last = was));
  }

  // ---- the endless strip (browse, スクロール): columns of one or two pictures, or パズル pages as wide
  // as the view, loaded page by page. It scrolls natively; its taps are read without getting in the way.
  let movedAt = -1e9; // the last time something other than ▶ scrolled it
  let pressAt: { id: number; x: number; y: number; left: number; t: number } | null = null;
  function onStrip() {
    if (!stripEl) return;
    const left = stripEl.scrollLeft;
    if (!drift?.owns(left)) {
      movedAt = performance.now();
      if (pressAt && Math.abs(left - pressAt.left) > TAP_SLOP) clearTimeout(longTimer); // a scroll, not a long press
    }
    if (opts.puzzle) {
      if (seek === null && pages.length) {
        pi = Math.min(pages.length - 1, Math.round(left / (stripW + GAP)));
        at = pages[pi].start;
      }
      measure();
    } else {
      const col = Math.round(left / (cellW + GAP));
      if (col * rows !== at) at = Math.min(Math.max(0, total - 1), col * rows);
    }
    if (left + stripEl.clientWidth > stripEl.scrollWidth - stripEl.clientWidth * 1.5) void page();
  }
  function onStripWheel(e: WheelEvent) {
    if (!stripEl || Math.abs(e.deltaX) > Math.abs(e.deltaY)) return;
    stripEl.scrollLeft += e.deltaY; // a mouse wheel scrolls it sideways
    e.preventDefault();
  }
  function onStripDown(e: PointerEvent) {
    pressAt = null;
    if (stripPinch) return;
    if (showOpts || !stripEl || e.button > 0) return; // a tap with ⚙ open only closes it (the view's own handler)
    pressing = true;
    if (e.timeStamp - movedAt < 150) return; // it stops a glide: not a tap
    pressAt = { id: e.pointerId, x: e.clientX, y: e.clientY, left: stripEl.scrollLeft, t: e.timeStamp };
    longPress(e.clientX, e.clientY);
  }
  function onStripUp(e: PointerEvent) {
    pressing = false;
    clearTimeout(longTimer);
    const p = pressAt;
    pressAt = null;
    if (stripPinch) return; // no tap or long press after a two-finger count change
    if (longDone) return void (longDone = false); // it opened the picture alone
    if (!p || p.id !== e.pointerId || e.type === 'pointercancel' || !stripEl) return; // cancelled: the strip took it as a scroll
    if (Math.hypot(e.clientX - p.x, e.clientY - p.y) > TAP_SLOP || Math.abs(stripEl.scrollLeft - p.left) > TAP_SLOP) return;
    if (e.timeStamp - p.t > HOLD_MS) return; // held, then let go before it opened: no tap (▶ stays as it was)
    taps.tap(e.clientX, e.clientY, e.timeStamp);
  }
  let stripPinch: { d0: number; k: number; ids: string; from: number; puzzle: boolean; choices: number[]; anchor: number; first: number; cancelled: boolean } | null = null;
  function stripTouches(node: HTMLElement) {
    // Native one-finger scrolling stays enabled; a count pinch alone prevents it.
    const types = ['touchstart', 'touchmove', 'touchend', 'touchcancel'] as const;
    for (const type of types) node.addEventListener(type, onTouches, { passive: false });
    return { destroy() { for (const type of types) node.removeEventListener(type, onTouches); } };
  }
  function onTouches(e: TouchEvent) {
    // A finger on the toolbar is not a second finger on the pictures.
    const touches = Array.from(e.touches).filter((t) => stripEl?.contains(t.target as Node));
    fingers = touches.length;
    if (stripPinch && e.type === 'touchcancel') stripPinch.cancelled = true;
    // a pinch starting while the strip already pans or glides natively cannot stop it: not a pinch
    if (!stripPinch && (e.type === 'touchstart' || e.type === 'touchmove') && fingers >= 2 && e.cancelable && !showOpts && !swallow && !single && stripEl) {
      const [a, b] = touches;
      const p = pairOf({ x: a.clientX, y: a.clientY }, { x: b.clientX, y: b.clientY });
      const card = cardAt(p.cx, p.cy);
      stripPinch = {
        d0: Math.max(1, p.d), k: 1, ids: `${a.identifier},${b.identifier}`,
        from: opts.puzzle ? (pages[pi]?.count ?? count) : count,
        puzzle: opts.puzzle, choices: sideways ? [1, 2, 4, 6, 8, 10] : [1, 2, 4],
        anchor: card ? items.indexOf(card) : at, first: at, cancelled: false,
      };
      clearTimeout(longTimer);
      clearTimeout(snapTimer);
      pressAt = null;
      pressing = true; // auto-flow waits until the last finger lifts
      snapOn = false;
      taps.interrupt();
      const r = stripEl.getBoundingClientRect();
      stripEl.style.transformOrigin = `${p.cx - r.left}px ${p.cy - r.top}px`;
      stripEl.style.transition = 'none';
      stripEl.style.willChange = 'scale'; // one layer scaled, not every picture drawn again per move
    }
    if (!stripPinch) return; // a single finger still scrolls natively
    if (e.cancelable) e.preventDefault();
    else if (e.type === 'touchmove') stripPinch.cancelled = true; // the browser took the gesture over
    if (fingers >= 2) {
      const [a, b] = touches;
      const p = pairOf({ x: a.clientX, y: a.clientY }, { x: b.clientX, y: b.clientY });
      const ids = `${a.identifier},${b.identifier}`;
      if (ids !== stripPinch.ids) {
        stripPinch.d0 = Math.max(1, p.d) / stripPinch.k;
        stripPinch.ids = ids;
      }
      stripPinch.k = pinchScale(p.d / stripPinch.d0, stripPinch.from, stripPinch.puzzle, stripPinch.choices);
      if (stripEl) stripEl.style.scale = String(stripPinch.k);
    }
    if (fingers) return;
    const p = stripPinch;
    stripPinch = null;
    pressing = false;
    if (stripEl) {
      stripEl.style.scale = '';
      stripEl.style.willChange = '';
    }
    const next = p.cancelled ? null : pinchCount(p.from, p.k, p.puzzle, p.choices);
    if (next !== null && (next !== count || opts.puzzle)) {
      // fewer: the picture under the fingers comes first; more: the ones on screen stay on screen
      at = Math.max(0, next < p.from ? p.anchor : p.first);
      setOpts({ count: next, puzzle: false });
      if (!auto) void tick().then(() => settleStrip());
    } else if (!auto) settleStrip();
  }
  const stepStrip = (d: 1 | -1) =>
    stripEl?.scrollBy({ left: d * (opts.puzzle ? stripW + GAP : (cellW + GAP) * cols), behavior: 'smooth' });
  /** Scrolls the strip to the column (パズル: the page) with `at` on it. */
  async function toStrip() {
    await tick();
    if (stripEl) stripEl.scrollLeft = opts.puzzle ? Math.max(0, pageOf(at)) * (stripW + GAP) : Math.floor(at / rows) * (cellW + GAP);
  }
  // a turned phone or a resized window: the strip keeps its first column on screen (パズル: relayout)
  $effect(() => {
    void [stripW, stripH, rows, cols];
    if (strip && !puzzle) untrack(() => void toStrip());
  });
  // filled pictures start their slow zoom when they come into view
  function seen(node: HTMLElement) {
    const io = new IntersectionObserver(
      (es) => {
        if (es.some((x) => x.isIntersecting)) {
          node.classList.add('seen');
          io.disconnect();
        }
      },
      { threshold: 0.25 },
    );
    io.observe(node);
    return { destroy: () => io.disconnect() };
  }
  const shownCard = $derived(strip ? items[at] : slots[0]?.card);
  // スライド: the background follows the pictures on screen once the strip settles (or every 1.5 s
  // while it moves), not every column that passes
  let stripBackground = $state.raw<Card | undefined>();
  let bgTimer = 0;
  let bgAt = -1e9;
  $effect(() => {
    const c = strip ? items[at] : undefined;
    untrack(() => {
      clearTimeout(bgTimer);
      if (!c) return void (stripBackground = undefined);
      if (!stripBackground) return void ((stripBackground = c), (bgAt = performance.now()));
      const wait = Math.max(300, 1500 - (performance.now() - bgAt));
      bgTimer = window.setTimeout(() => ((stripBackground = c), (bgAt = performance.now())), wait);
    });
  });
  const backgroundCard = $derived(strip ? stripBackground : readyBackground);

  // Rounded corners on the photo itself: whole (contain), the picture is sized to its own
  // rectangle inside the cell, so the corners round the photo and not its letterbox; filled, the
  // frame is rounded. The cell's box stays as it is for scrolling, hit testing and the collage.
  // One observer for every cell; client sizes ignore a live pinch scale.
  const cornerOf = (w: number, h: number) => Math.round(Math.min(14, Math.min(w, h) * 0.06));
  const fits = new Map<Element, () => void>();
  let fitObserver: ResizeObserver | null = null;
  function fitPhoto(node: HTMLImageElement, fill: boolean) {
    const frame = node.parentElement!;
    let cover = fill;
    const update = () => {
      const w = frame.clientWidth, h = frame.clientHeight;
      if (!w || !h || !node.naturalWidth || !node.naturalHeight) return;
      const [x, y, pw, ph] = cover ? [0, 0, w, h] : fitRect(node.naturalWidth, node.naturalHeight, w, h, false);
      node.style.left = x + 'px'; node.style.top = y + 'px';
      node.style.width = pw + 'px'; node.style.height = ph + 'px';
      frame.style.setProperty('--round', cornerOf(pw, ph) + 'px');
    };
    fitObserver ??= new ResizeObserver((entries) => entries.forEach((e) => fits.get(e.target)?.()));
    fits.set(frame, update);
    fitObserver.observe(frame);
    node.addEventListener('load', update);
    update();
    return {
      update(fill: boolean) { cover = fill; update(); },
      destroy() {
        fitObserver?.unobserve(frame);
        fits.delete(frame);
        node.removeEventListener('load', update);
      },
    };
  }
  /** A slanted パズル cell's clip with rounded corners. */
  const roundedClip = (c: Cell | undefined) => roundClip(c?.clip, c ? cornerOf(c.w, c.h) : 0) ?? null;

  // ---- the collage: the pictures on screen as one picture on this device
  let saving = $state(false);
  let toast = $state('');
  function say(t: string) {
    toast = t;
    setTimeout(() => toast === t && (toast = ''), 2400);
  }
  async function saveCollage() {
    if (saving) return;
    saving = true;
    let onScreen = [...((strip ? stripEl : cellsEl)?.querySelectorAll<HTMLElement>('.cell') ?? [])]; // a set: all of it
    if (strip && opts.puzzle) onScreen = [...(stripEl?.querySelectorAll('.pg')[pi]?.querySelectorAll<HTMLElement>('.cell') ?? [])]; // the page on screen, whole
    else if (strip) {
      const box = stripEl?.getBoundingClientRect();
      onScreen = onScreen.filter((el) => {
        const r = el.getBoundingClientRect();
        return box && r.left >= box.left - 2 && r.right <= box.right + 2;
      });
    }
    const blob = await collage(onScreen, opts.fill).catch(() => null);
    const ok = blob ? await saveImage(blob, collageName()).catch(() => false) : false;
    saving = false;
    navigator.vibrate?.(8);
    say(!blob ? t('画像の読み込み中です。もう一度どうぞ') : ok ? (ANDROID ? t('「ピクチャ › Choosen」に保存しました') : IOS ? t('写真に保存しました') : t('保存しました')) : t('保存できませんでした'));
  }

  function onKey(e: KeyboardEvent) {
    if (single) return; // the picture shown alone takes the keys (Esc closes it)
    if (e.key === 'Escape') {
      if (showOpts) showOpts = false;
      else onclose();
    } else if (e.key === 'ArrowRight') void (strip ? stepStrip(1) : go(1));
    else if (e.key === 'ArrowLeft') void (strip ? stepStrip(-1) : go(-1));
    else if (e.code === 'Space') {
      if (browse) toggleAuto();
      else held = !held;
    } else return;
    e.preventDefault();
    e.stopPropagation();
  }

  // ---- the light around the pictures, from the first one's colours (as on the swipe screen)
  let lightCanvas: HTMLCanvasElement;
  let light: Ambient | null = null;
  // the shared background (全体) covers the light: it rests then, and takes no colours
  const covered = $derived(!opts.fill && !!backgroundCard);
  $effect(() => {
    light?.hold(covered);
  });
  $effect(() => {
    const c = shownCard;
    if (!c || !light || covered) return;
    paletteOf(thumbUrl(c, 64))
      .then((p) => light?.setColors(p))
      .catch(() => {});
  });

  onMount(() => {
    light = new Ambient(lightCanvas);
    light.hold(covered);
    void load();
    if (browse) showUi();
    const up = () => (pressing = false); // also when the pointer comes up off the strip
    addEventListener('keydown', onKey, true);
    addEventListener('pointerup', up, true);
    addEventListener('pointercancel', up, true);
    return () => {
      removeEventListener('keydown', onKey, true);
      removeEventListener('pointerup', up, true);
      removeEventListener('pointercancel', up, true);
      clearAll();
      taps.cancel();
      clearTimeout(holdTimer);
      clearTimeout(longTimer); // a long press under way must not open a picture after the view closed
      clearTimeout(bgTimer);
      fitObserver?.disconnect();
      clearTimeout(uiTimer);
      cancelAnimationFrame(shapeFrame);
      light?.dispose();
    };
  });

  function setOpts(patch: Partial<Opts>, keep: Card[] = []) {
    const was = opts;
    opts = { ...opts, ...patch };
    save();
    if ('kind' in patch) return void load();
    const turned = opts.puzzle !== was.puzzle;
    const pin = !browse && (('count' in patch && opts.count !== was.count) || turned);
    if (pin) {
      pinCells(); // play: the outgoing cells stay where they are
      void settlePins();
    }
    // play: `at` is the next picture to come, with パズル the first on screen
    if (turned && opts.puzzle && !browse && items.length) at = (((at - slots.length) % items.length) + items.length) % items.length;
    if (turned && opts.puzzle) return; // relayout() lays the pages out and shows them
    if ('count' in patch || turned || ('seconds' in patch && !browse)) {
      show(keep);
      if (strip) void toStrip();
    }
  }
  function tapFill() {
    setOpts({ fill: true });
  }
  const n = $derived(strip ? (opts.puzzle ? (pages[pi]?.count ?? 0) : Math.min(count, total)) : slots.length);
  const pos = $derived(
    !total
      ? '0 / 0'
      : browse || opts.puzzle
        ? `${n > 1 ? `${at + 1}–${Math.min(at + n, total)}` : at + 1} / ${total}`
        : `${((at - 1) % total) + 1} / ${total}`,
  );
</script>

{#snippet body(card: Card, lazy: boolean)}

  <!-- filled: the frame's own edges are the soft ones (the picture zooms past its own) -->
  <div class="frame">
    <img class="blur" src={thumbUrl(card, 64)} alt="" loading={lazy ? 'lazy' : 'eager'} />
    <img class="pic" src={imgUrl(card)} alt="" draggable="false" loading={lazy ? 'lazy' : 'eager'} use:fitPhoto={opts.fill} />
  </div>
  {@render mine(card)}
{/snippet}

{#snippet mine(card: Card)}
  {@const m = marks.get(card.fp)}
  {#if m}<span class="mine" class:sup={m === 'S'}>{@html svg(m === 'S' ? 'star' : 'heart', 16)}</span>{/if}
{/snippet}

<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
  class="viewer story"
  class:browse
  class:fill={opts.fill}
  class:held
  class:bare={browse && !ui}
  onpointerdown={onDown}
  onpointermove={onMove}
  onpointerup={onUp}
  onpointercancel={onUp}
  onwheel={onWheel}
  oncontextmenu={(e) => e.preventDefault()}
>
  <canvas class="light" bind:this={lightCanvas}></canvas>
  {#if !opts.fill && backgroundCard}<StoryBackground src={thumbUrl(backgroundCard, 64)} />{/if}
  {#if strip}
    <div
      class="strip"
      class:pz={opts.puzzle}
      class:snap={snapOn && !auto}
      style:padding-block={padY + 'px'}
      bind:this={stripEl}
      bind:clientWidth={stripW}
      bind:clientHeight={stripH}
      onscroll={onStrip}
      onwheel={onStripWheel}
      onpointerdown={onStripDown}
      onpointerup={onStripUp}
      onpointercancel={onStripUp}
      use:stripTouches
      style:--cw={cellW + 'px'}
      style:--rows={rows}
      style:--pw={stripW + 'px'}
    >
      {#if opts.puzzle}
        {#each pages as p (p.start)}
          <div class="pg">
            {#each p.cells as c (c.i)}
              {@const card = items[c.i]}
              <div class="cell" data-id={card.id} use:seen style:left={c.x + 'px'} style:top={c.y + 'px'} style:width={c.w + 'px'} style:height={c.h + 'px'} style:clip-path={roundedClip(c)}>
                {@render body(card, true)}
              </div>
            {/each}
          </div>
        {/each}
        {#if total && !laidOut}<div class="pg"><span class="wait"></span></div>{/if}
      {:else}
        {#each items as c (c.id)}
          <div class="cell" data-id={c.id} use:seen>
            {@render body(c, true)}
          </div>
        {/each}
      {/if}
    </div>
  {:else}
    <div
      class="cells {opts.puzzle ? 'pz' : `c${n}`}"
      bind:this={cellsEl}
      bind:clientWidth={cellsW}
      bind:clientHeight={cellsH}
    >
      {#each [...leaving.map((s) => ({ s, pos: s.pos, out: true })), ...slots.map((s, pos) => ({ s, pos, out: false }))] as { s, pos, out } (s.key)}
        {@const box = out ? (pins.get(s.key) ?? s.box) : s.box}
        <div
          class="cell"
          class:out
          class:gone={out && !!('gone' in s && s.gone)}
          class:pinned={out && !!box}
          use:cellReady={s.key}
          data-key={s.key}
          data-id={out ? null : s.card.id}
          style:--r={n === 4 ? Math.floor(pos / 2) + 1 : pos + 1}
          style:--c={n === 4 ? (pos % 2) + 1 : pos + 1}
          style:--ms={roundMs * (n > 1 && !opts.puzzle ? 1.3 : 1) + 'ms'}
          style:left={box ? box.x + 'px' : null}
          style:top={box ? box.y + 'px' : null}
          style:width={box ? box.w + 'px' : null}
          style:height={box ? box.h + 'px' : null}
          style:clip-path={roundedClip(box)}
        >
          {@render body(s.card, false)}
        </div>
      {/each}
    </div>
    {#if opts.puzzle && seek !== null && total}<span class="wait"></span>{/if}
  {/if}
  {#if loaded && !total}<p class="none">{t('表示する画像がありません')}</p>{/if}
  {#each pops as p (p.key)}
    <span class="pop" class:star={p.star} style:left={p.x + 'px'} style:top={p.y + 'px'}>{@html svg(p.star ? 'star' : 'heart', 110)}</span>
  {/each}

  <div class="edge-shade" aria-hidden="true"></div>
  <div class="top">
    {#if !browse && n}
      <div class="bar">
        {#key round}<span class="fill-bar" class:hold={still} style:--d={roundMs + 'ms'}></span>{/key}
      </div>
    {/if}
    <div class="row">
      <span class="pos num">{pos}</span>
      {#if n > 1}
        <button title={t('コラージュを保存（この端末に）')} disabled={saving} onclick={saveCollage}>{@html svg('download', 20)}</button>
      {/if}
      <button title={t('設定')} onclick={() => (showOpts = !showOpts)}>{@html svg('gear', 20)}</button>
      <button title={t('閉じる')} onclick={onclose}>{@html svg('x', 22)}</button>
    </div>
  </div>

  {#if showOpts}
    <div class="opts">
      {#if strip}
        <div class="line">
          <b>{t('速度')}</b>
          <span class="seg">{#each [1, 2, 3, 4, 5] as v (v)}<button class:on={opts.speed === v} onclick={() => setOpts({ speed: v })}>{v}</button>{/each}</span>
        </div>
      {:else}
        <div class="line">
          <b>{t('秒数')}</b>
          <span class="seg">{#each SECONDS as s (s)}<button class:on={opts.seconds === s} onclick={() => setOpts({ seconds: s })}>{s}</button>{/each}</span>
        </div>
      {/if}
      <div class="line">
        <b>{t('同時表示')}</b>
        <span class="seg">
          {#each browse && sideways ? [1, 2, 4, 6, 8, 10] : [1, 2, 4] as k (k)}<button class:on={!opts.puzzle && count === k} onclick={() => setOpts({ count: k, puzzle: false })}>{k}</button>{/each}
          <button class:on={opts.puzzle} onclick={() => setOpts({ puzzle: true })}>{t('パズル')}</button>
        </span>
      </div>
      {#if browse && sideways && !opts.puzzle && count > 4}
        <div class="line">
          <b>{t('並び')}</b>
          <span class="seg">
            <button class:on={opts.rowsSide === 1} onclick={() => setOpts({ rowsSide: 1 })}>{t('横一列')}</button>
            <button class:on={opts.rowsSide === 2} onclick={() => setOpts({ rowsSide: 2 })}>{t('2段')}</button>
          </span>
        </div>
      {/if}
      <div class="line">
        <b>{t('表示')}</b>
        <span class="seg">
          <button class:on={!opts.fill} onclick={() => setOpts({ fill: false })}>{t('全体')}</button>
          <button class:on={opts.fill} onclick={tapFill}>{t('拡大')}</button>
        </span>
      </div>
      {#if !browse}
        <div class="line">
          <b>{t('種類')}</b>
          <span class="seg wrap">{#each KINDS as [k, label] (k)}<button class:on={opts.kind === k} onclick={() => setOpts({ kind: k })}>{label}</button>{/each}</span>
        </div>
      {/if}
    </div>
  {/if}
  {#if single}
    <div class="single">
      <Viewer items={[single]} start={0} bare onclose={() => (single = null)} onneed={() => {}} />
    </div>
  {/if}
  {#if toast}<div class="toast">{toast}</div>{/if}
</div>

<style>
  .story {
    position: fixed;
    inset: 0;
    z-index: 25;
    background: #000;
    color: #f2f3f5;
    touch-action: none;
    user-select: none;
  }
  .light {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
  }
  /* the pictures reach the screen's sides; the light shows above and below them */
  .cells {
    position: absolute;
    inset: 3% 0;
    display: grid;
    gap: 3px;
  }
  .cells.c1 {
    grid-template: 1fr / 1fr;
    inset: 4% 0;
  }
  .cells.c2 {
    grid-template: 1fr 1fr / 1fr;
  }
  .cells.c4 {
    grid-template: 1fr 1fr / 1fr 1fr;
  }
  @media (orientation: landscape) {
    .cells.c2 {
      grid-template: 1fr / 1fr 1fr;
    }
  }
  /* each cell at its own place, so an outgoing cell and the one replacing it overlap */
  .cells.c2 > .cell {
    grid-row: var(--r);
    grid-column: 1;
  }
  @media (orientation: landscape) {
    .cells.c2 > .cell {
      grid-row: 1;
      grid-column: var(--c);
    }
  }
  .cells.c4 > .cell {
    grid-row: var(--r);
    grid-column: var(--c);
  }
  .cells.c1 > .cell {
    grid-area: 1 / 1;
  }
  /* play: a new cell shows once its pictures are in, over the one it replaces */
  .cells > .cell:not(.out) {
    z-index: 1;
  }
  .cells > .cell:not(:global(.ready)) {
    opacity: 0;
    animation: none;
  }
  .cells > .cell.out {
    z-index: 0;
    pointer-events: none;
    animation: none;
  }
  /* play, after a new arrangement: the outgoing cells stay where they were drawn */
  .cells > .cell.out.pinned {
    position: absolute;
    grid-area: auto;
  }
  /* パズル: each cell where the page's layout puts it */
  .cells.pz {
    display: block;
  }
  .pz .cell {
    position: absolute;
  }
  /* the endless strip: columns of one or two pictures, scrolling freely sideways */
  .strip {
    position: absolute;
    inset: 0; /* its own padding (padY) keeps the room above and below */
    display: grid;
    grid-auto-flow: column;
    grid-template-rows: repeat(var(--rows, 1), 1fr);
    grid-auto-columns: var(--cw);
    gap: 3px;
    overflow-x: auto;
    overflow-y: hidden;
    overscroll-behavior: contain;
    touch-action: pan-x;
    scrollbar-width: none;
  }
  /* moved by hand, it comes to rest on a picture (a column, or a パズル page) */
  .strip.snap {
    scroll-snap-type: x mandatory;
  }
  .strip.snap > .cell,
  .strip.snap > .pg {
    scroll-snap-align: start;
  }
  .strip::-webkit-scrollbar {
    display: none;
  }
  /* パズル: pages as wide as the view, side by side */
  .strip.pz {
    display: flex;
  }
  .pg {
    flex: none;
    position: relative;
    width: var(--pw);
    height: 100%;
  }
  .strip .cell {
    animation: none;
  }
  /* square corners: cells meet the screen's sides and each other; the frame inside clips the
     picture and its blurred copy */
  .cell {
    position: relative;
    animation: storyin 600ms ease-out;
  }
  .frame {
    position: absolute;
    inset: 0;
    z-index: 1;
    overflow: hidden;
  }
  @keyframes storyin {
    from {
      opacity: 0;
      scale: 1.03;
    }
  }
  .cell img {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    pointer-events: none;
  }
  /* Filled pictures keep their backing while loading; whole pictures share the viewport background.
     Keep the thumbnail for readiness and the existing collage export. */
  .cell .blur {
    object-fit: cover;
    filter: blur(18px) brightness(0.7);
    transform: scale(1.2);
  }
  .story:not(.fill) .cell .blur { visibility: hidden; }
  .story:not(.fill) .cell:global(.stand-in) .blur { visibility: visible; }
  .cells > .cell.out.gone {
    opacity: 0;
    transition: opacity 600ms ease-out;
  }
  /* rounded corners, no soft edges: whole, the picture (sized to its own rectangle by fitPhoto);
     filled, the frame that crops it */
  .pic {
    object-fit: contain;
    border-radius: var(--round, 12px);
  }
  .fill .frame {
    border-radius: var(--round, 12px);
  }
  .fill .pic {
    object-fit: cover;
    border-radius: 0;
  }
  .mine {
    position: absolute;
    z-index: 2;
    right: 10px;
    bottom: 10px;
    color: var(--like);
    filter: drop-shadow(0 0 8px var(--like));
  }
  .mine.sup {
    color: var(--super);
    filter: drop-shadow(0 0 8px var(--super));
  }
  /* waiting for a パズル page: its pictures' shapes are being measured */
  .wait {
    position: absolute;
    left: 50%;
    top: 50%;
    width: 28px;
    height: 28px;
    translate: -50% -50%;
    border-radius: 50%;
    border: 2px solid rgba(255, 255, 255, 0.16);
    border-top-color: rgba(255, 255, 255, 0.65);
    animation: spin 900ms linear infinite;
    pointer-events: none;
  }
  @keyframes spin {
    to {
      rotate: 1turn;
    }
  }
  .pop {
    position: absolute;
    z-index: 6;
    translate: -50% -50%;
    color: var(--like);
    pointer-events: none;
    filter: drop-shadow(0 0 22px var(--like));
    animation: popin 800ms ease-out forwards;
  }
  .pop.star {
    color: var(--super);
    filter: drop-shadow(0 0 22px var(--super));
  }
  @keyframes popin {
    0% {
      scale: 0.2;
      opacity: 0;
    }
    18% {
      scale: 1.15;
      opacity: 1;
    }
    70% {
      scale: 1;
      opacity: 1;
    }
    100% {
      scale: 1.3;
      opacity: 0;
      translate: -50% -90%;
    }
  }
  .edge-shade {
    position: absolute;
    inset: 0;
    z-index: 5;
    pointer-events: none;
    background: linear-gradient(to bottom, rgba(0, 0, 0, 0.5), transparent 18%, transparent 82%, rgba(0, 0, 0, 0.5));
  }
  .top {
    position: absolute;
    z-index: 10; /* above the pictures */
    left: 0;
    right: 0;
    top: 0;
    padding: calc(var(--safe-top, 0px) + 10px) 12px 0;
    background: linear-gradient(rgba(0, 0, 0, 0.45), transparent);
    transition: opacity 250ms;
    pointer-events: none; /* a tap beside the buttons is a tap on the pictures */
  }
  .held .top,
  .bare .top {
    opacity: 0; /* held: only the pictures; スライド: until a tap */
  }
  .bare .row button {
    pointer-events: none;
  }
  .bar {
    height: 3px;
    border-radius: 2px;
    background: rgba(255, 255, 255, 0.25);
    overflow: hidden;
  }
  .fill-bar {
    display: block;
    height: 100%;
    background: #fff;
    transform-origin: left;
    animation: grow var(--d) linear forwards;
  }
  .fill-bar.hold {
    animation-play-state: paused;
  }
  @keyframes grow {
    from {
      transform: scaleX(0);
    }
    to {
      transform: scaleX(1);
    }
  }
  .row {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 6px 0;
  }
  .pos {
    flex: 1;
    font-size: 12px;
    color: rgba(255, 255, 255, 0.75);
  }
  .row button {
    width: 36px;
    height: 36px;
    border: none;
    border-radius: 50%;
    display: grid;
    place-items: center;
    background: transparent;
    color: #fff;
    cursor: pointer;
    filter: drop-shadow(0 1px 3px rgba(0, 0, 0, 0.6));
    pointer-events: auto;
  }
  .row button:disabled {
    opacity: 0.4;
  }
  .none {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
    margin: 0;
    color: rgba(255, 255, 255, 0.7);
  }
  .opts {
    position: absolute;
    z-index: 11;
    left: 12px;
    right: 12px;
    top: calc(var(--safe-top, 0px) + 64px);
    display: flex;
    flex-direction: column;
    gap: 10px;
    padding: 14px;
    border-radius: 18px;
    background: rgba(20, 22, 26, 0.88);
    backdrop-filter: blur(14px);
    animation: storyin 200ms ease-out;
  }
  .line {
    display: flex;
    align-items: center;
    gap: 12px;
  }
  .line b {
    flex: none;
    width: 64px;
    font-size: 13px;
  }
  .seg {
    flex: 1;
    display: flex;
    gap: 6px;
  }
  .seg.wrap {
    flex-wrap: wrap;
  }
  .seg button {
    flex: 1;
    padding: 7px 10px;
    border-radius: 999px;
    border: 1px solid rgba(255, 255, 255, 0.18);
    background: transparent;
    color: rgba(255, 255, 255, 0.75);
    font-size: 13px;
    white-space: nowrap;
    cursor: pointer;
  }
  .seg button.on {
    background: #fff;
    color: #15171b;
    border-color: transparent;
  }
  .toast {
    position: absolute;
    z-index: 12;
    left: 50%;
    bottom: calc(28px + env(safe-area-inset-bottom, 0px));
    translate: -50% 0;
    max-width: calc(100% - 32px);
    padding: 9px 16px;
    border-radius: 999px;
    background: rgba(20, 22, 26, 0.85);
    backdrop-filter: blur(10px);
    font-size: 13px;
    white-space: nowrap;
    animation: toastin 200ms ease-out;
    pointer-events: none;
  }
  @keyframes toastin {
    from {
      opacity: 0;
    }
  }
</style>
