<script lang="ts">
  import { t } from './lib/i18n';
  // The swipe screen: a card stack on ambient light in the photo's colours. Hot-path rule: input is
  // never blocked by animation — a thrown card flies on as a "ghost" while the next card is already
  // live. Per-frame values are written straight to the DOM, not through Svelte state.
  import { onMount, tick, untrack } from 'svelte';
  import { fade } from 'svelte/transition';
  import { getJ, postJ, imgUrl, thumbUrl, placeOf, fmtPts, IOS, type Card, type State } from './lib/api';
  import { newCid, type Saved } from './lib/outbox';
  import { outbox } from './lib/ratings';
  import { Spring, Tracker, dragRotation, lean, release, throwVelocity, LETTER, type Verdict } from './lib/physics';
  import { commandFor, HOLD_MS } from './lib/keys';
  import { mouseMap, buttonInput, Flick, RATING_CMDS, type MouseCmd } from './lib/mouse';
  import { Ambient, paletteOf } from './lib/ambient';
  import { COLORS, type Intensity } from './lib/burst';
  import { svg } from './ui/icons';
  import ScopePicker from './ScopePicker.svelte';
  import FolderSheet from './FolderSheet.svelte';
  import { photoFolders, readOff, scopeOfOn } from './lib/folders';
  import { fx, setFx } from './lib/fx.svelte';
  import { prefs, toggleInfo } from './lib/prefs.svelte';
  import { Taps, TAP_SLOP, axisOf, scrollRelease, pairOf, pinchZoom, NO_ZOOM, type Pair, type Zoom } from './lib/gesture';

  let {
    st,
    active = true,
    onbuttons,
  }: {
    st: State;
    active?: boolean;
    onbuttons?: (shown: boolean) => void; // the app hides its bar with the card buttons on a phone
  } = $props();

  interface Past {
    card: Card;
    verdict: Verdict;
    cid: string;
  }
  interface Last {
    verdict: Verdict;
    prev: Verdict | null; // the image's earlier verdict (re-rating or correction)
    corrected: boolean;
    before: number | null;
    after: number | null;
  }
  interface Ghost {
    key: number;
    card: Card;
    verdict: Verdict;
    x: number;
    y: number;
    r: number;
    vx: number;
    vy: number;
    zoomed: number; // the scale its picture was drawn at (a quiet moment brings it closer)
  }

  const VERDICT_OF: Record<string, Verdict> = { L: 'like', S: 'super', D: 'pass', A: 'archive' };
  const ICON_OF: Record<Verdict, 'heart' | 'star' | 'x' | 'archive'> = { like: 'heart', super: 'star', pass: 'x', archive: 'archive' };
  const LABEL: Record<Verdict, string> = { like: t('好評'), super: t('超好評'), pass: t('見送り'), archive: t('アーカイブ') };
  const VERDICTS: Verdict[] = ['like', 'pass', 'super', 'archive'];

  // raw state: these arrays are replaced, never mutated, and per-frame code must not go through proxies
  let queue: Card[] = $state.raw([]);
  let history: Past[] = $state.raw([]);
  let redo: number | null = $state(null);
  let last: Last | null = $state.raw(null);
  let ghosts: Ghost[] = $state.raw([]);
  let exhausted = $state(false);
  let photoReady = $state(false);
  // the card buttons (▼/▲): this device's own choice; until there is one, the shared setting
  let buttons: boolean | null = $state(readButtons());
  function readButtons(): boolean | null {
    try {
      const v = localStorage.getItem('choosen3.buttons');
      return v === null ? null : v === '1';
    } catch {
      return null;
    }
  }
  const FX_NEXT: Record<Intensity, Intensity> = { normal: 'calm', calm: 'off', off: 'normal' };
  const FX_LABEL: Record<Intensity, string> = { normal: t('エフェクト: フル'), calm: t('エフェクト: ライト'), off: t('エフェクト: オフ') };
  const cycleFx = () => setFx(FX_NEXT[fx.level]);

  const CURATE = new Set(['liked', 'likeOnly', 'super', 'passed']);
  let remaining: number | null = $state(null);
  // 範囲 (folders) and 対象 (which images); a change starts the cards over, the history stays. The
  // folder button opens the photo folders with オン / オフ (kept per device); サブフォルダで選ぶ there
  // sets a finer range (custom) until a folder is switched again.
  // svelte-ignore state_referenced_locally
  const onFolders = scopeOfOn(photoFolders(st), readOff());
  let scope: string[] = $state.raw(onFolders.tokens);
  let scopeLabel = $state(onFolders.label);
  let custom = $state('');
  let filter = $state('');
  let sheet = $state(false);
  let addingFolder = $state(false);
  let picking = $state(false);
  function pickFiner(tokens: string[], label: string) {
    if (tokens.length) {
      custom = label;
      choose({ scope: tokens, label });
    } else {
      custom = ''; // すべて: back to the folders that are on
      const s = scopeOfOn(photoFolders(st), readOff());
      choose({ scope: s.tokens, label: s.label });
    }
  }
  let targetMenu = $state(false);
  let gen = 0;
  const targets = $derived.by(() => {
    const share = st.reviewShare;
    const mixed = share >= 0.999 ? t('再評価だけ') : share <= 0.001 ? t('未評価（なくなったら再評価）') : t('未評価 ＋ 再評価（{0}枚に1枚）', Math.round(1 / share));
    return [
      { v: '', menu: mixed, chip: share >= 0.999 ? t('再評価だけ') : t('未評価＋再評価') },
      { v: 'fresh', menu: t('未評価だけ'), chip: t('未評価だけ') },
      { v: 'liked', menu: t('好評・超好評を厳選'), chip: t('好評・超好評') },
      { v: 'super', menu: t('超好評を厳選'), chip: t('超好評') },
      { v: 'passed', menu: t('見送りを見直す'), chip: t('見送り') },
    ];
  });
  const target = $derived(targets.find((t) => t.v === filter) ?? targets[0]);
  function choose(next: { scope?: string[]; label?: string; filter?: string }) {
    if (next.scope) {
      scope = next.scope;
      scopeLabel = next.label ?? t('すべて');
    }
    if (next.filter !== undefined) filter = next.filter;
    picking = false;
    targetMenu = false;
    gen++;
    redo = null;
    queue = [];
    exhausted = false;
    photoReady = false;
    refill();
  }


  // nothing to show yet (the folder is still being read, or new ratings changed what is due): ask again
  let triedAt = -1;
  $effect(() => {
    if (exhausted && st.version !== triedAt) {
      triedAt = st.version;
      untrack(refill);
    }
  });

  const current = $derived(redo !== null ? history[redo]?.card ?? null : queue[0] ?? null);
  const behind = $derived(redo !== null ? queue.slice(0, 2) : queue.slice(1, 3));
  const showButtons = $derived(buttons ?? st.swipeButtons !== false);
  $effect(() => onbuttons?.(showButtons));
  // pictures straight in the photo folder: its own name, not a generic word
  const rootName = $derived((st.root ?? '').split(/[\\/]/).filter(Boolean).pop() ?? '');

  let stage: HTMLDivElement;
  let deck: HTMLDivElement = $state()!; // the card stack or the scroll strip, whichever layout is shown
  let topEl: HTMLDivElement | undefined = $state();
  let ambientCanvas: HTMLCanvasElement;
  let ambient: Ambient;


  // ---- cards: prefetch and decode ahead, so a card is never blank
  const decoded = new Map<number, HTMLImageElement>();
  let loading = false;
  let ghostKey = 0;

  async function refill() {
    if (loading) return;
    loading = true;
    const g = gen;
    try {
      const r = await getJ<{ items: Card[]; counts?: { due: number; fresh: number } }>('/api/next', {
        n: 8 - queue.length,
        // not the cards on their way to the server either: unsaved, they would come straight back
        exclude: [...queue.map((c) => c.id), ...outbox.pendingIds].join(','),
        recent: history.slice(-5).map((h) => h.card.id).join(','),
        ...(scope.length ? { scope: scope.join('|') } : {}),
        ...(filter ? { filter } : {}),
      });
      if (g !== gen) return; // the range or target changed meanwhile: finally asks again
      // pictures left in a chosen range (the re-swipe targets go round, so they have no end)
      remaining = scope.length && r.counts && !CURATE.has(filter) ? (filter === 'fresh' ? r.counts.fresh : r.counts.fresh + r.counts.due) : null;
      const have = new Set(queue.map((c) => c.id));
      queue = [...queue, ...r.items.filter((c) => !have.has(c.id))];
      exhausted = queue.length === 0;
      queue.slice(0, 6).forEach(preload);
    } catch {
      setTimeout(refill, 1500); // the core is still loading
    } finally {
      loading = false;
      if (g !== gen) refill();
    }
  }

  function preload(c: Card) {
    if (decoded.has(c.id)) return;
    const img = new Image();
    img.src = imgUrl(c);
    img
      .decode()
      .then(() => {
        decoded.set(c.id, img);
        while (decoded.size > 12) decoded.delete(decoded.keys().next().value!);
      })
      .catch(() => {});
  }

  // ---- the top card's pose (not Svelte state: written per frame)
  let dx = 0, dy = 0, rot = 0;
  let drag: { id: number; x0: number; y0: number; grabY: number; touch: boolean } | null = null;
  const tracker = new Tracker();
  const spring = new Spring();
  let springRaf = 0;

  function pose() {
    if (!topEl) return;
    topEl.style.transform = `translate3d(${dx}px, ${dy}px, 0) rotate(${rot}deg)`;
    const r = deck.getBoundingClientRect();
    const l = lean(dx, dy, r.width, r.height);
    stage.style.setProperty('--amt', String(l.amount));
    stage.dataset.lean = l.verdict ?? '';
    topEl.style.setProperty('--lean-color', l.verdict ? COLORS[l.verdict] : 'transparent');
  }

  function stopSpring() {
    cancelAnimationFrame(springRaf);
    springRaf = 0;
  }

  /** Back to the middle with a slight wobble (or in from the side, after "back"). */
  function springBack() {
    stopSpring();
    spring.x = dx;
    spring.y = dy;
    spring.r = rot;
    let t0 = performance.now();
    const step = (now: number) => {
      const moving = spring.step(now - t0);
      t0 = now;
      dx = spring.x;
      dy = spring.y;
      rot = spring.r;
      pose();
      springRaf = moving ? requestAnimationFrame(step) : 0;
    };
    springRaf = requestAnimationFrame(step);
  }

  // ---- mouse modes: a button either grabs the card (mode 1) or runs a command, on the card only
  let focusAt = -1000; // the click that brings the window forward does nothing else
  /** A touch that starts at the very left or right edge (~3 mm): the phone's back gesture, not ours. */
  const EDGE = 20;
  const atEdge = (e: PointerEvent) => e.pointerType === 'touch' && (e.clientX < EDGE || e.clientX > innerWidth - EDGE);

  function onPointerDown(e: PointerEvent) {
    if ((e.target as Element).closest('button') || atEdge(e)) return;
    if (fingerDown(e, topEl)) return;
    if (panDown(e)) return;
    if (e.pointerType === 'mouse') {
      const input = buttonInput(e.button);
      const cmd: MouseCmd = input ? mouseMap(st.mouseMode, st.customMouse)[input] : 'none';
      if (cmd !== 'grab') {
        e.preventDefault();
        if (cmd !== 'none' && e.timeStamp - focusAt > 250) run(cmd);
        return;
      }
      if (flick.on) return;
    }
    if (!topEl) return;
    stopSpring();
    const r = topEl!.getBoundingClientRect();
    drag = { id: e.pointerId, x0: e.clientX - dx, y0: e.clientY - dy, grabY: (e.clientY - r.top) / r.height, touch: e.pointerType !== 'mouse' };
    startTap(e);
    tracker.reset();
    tracker.add(e.clientX, e.clientY, e.timeStamp);
    capture(topEl!, e.pointerId);
    topEl!.classList.add('held');
  }

  function onPointerMove(e: PointerEvent) {
    if (fingerMove(e) || panMove(e)) return;
    if (topEl) {
      const r = topEl.getBoundingClientRect();
      topEl.style.setProperty('--gx', `${((e.clientX - r.left) / r.width) * 100}%`);
      topEl.style.setProperty('--gy', `${((e.clientY - r.top) / r.height) * 100}%`);
    }
    if (!drag || e.pointerId !== drag.id) return;
    moveTap(e);
    dx = e.clientX - drag.x0;
    dy = e.clientY - drag.y0;
    rot = dragRotation(dx, deck.clientWidth, drag.grabY);
    tracker.add(e.clientX, e.clientY, e.timeStamp);
    pose();
  }

  function onPointerUp(e: PointerEvent) {
    if (fingerUp(e) || panUp(e)) return;
    if (!drag || e.pointerId !== drag.id) return;
    finishDrag(e.timeStamp);
  }

  function cancelDrag() {
    if (!drag) return;
    try {
      topEl?.releasePointerCapture(drag.id);
    } catch {
      /* already released */
    }
    drag = null;
    topEl?.classList.remove('held');
  }

  function finishDrag(now: number) {
    if (!drag) return;
    cancelDrag();
    if (maxMove <= TAP_SLOP) {
      springBack();
      const n = taps.tap(down.x, down.y, now);
      if (n === 2 && current) {
        pop('like', down.x, down.y);
        const card = current;
        clearTimeout(cardPending?.timer);
        cardPending = { card, timer: window.setTimeout(() => {
          cardPending = null;
          taps.reset();
          if (current === card) throwCard('like');
        }, 320) };
      } else if (n === 3 && cardPending) {
        clearTimeout(cardPending.timer);
        cardPending = null;
        taps.reset();
        pop('super', down.x, down.y);
        throwCard('super');
      }
      return;
    }
    const { vx, vy } = tracker.velocity(now);
    const v = release(dx, dy, vx, vy, deck.clientWidth, deck.clientHeight);
    if (v) throwCard(v, vx, vy);
    else springBack();
  }

  /** Keeps a pointer's moves on this element; a pointer that is already gone just is not captured. */
  function capture(el: HTMLElement, id: number) {
    try {
      el.setPointerCapture(id);
    } catch {
      /* not an active pointer any more */
    }
  }

  // ---- zoom: two fingers zoom the picture (1x to 5x) and it stays zoomed; one finger then moves it
  // around (no swipe, no scroll), a double tap brings it back to its size. A new picture starts
  // unzoomed. Until every finger of a pinch is up, nothing is thrown, scrolled or tapped.
  const fingers = new Map<number, { x: number; y: number }>();
  let pinch: { start: Pair; z0: Zoom; el: HTMLElement; left: number; top: number } | null = null;
  let pinched = false;
  let zoom: Zoom = NO_ZOOM;
  let zoomEl: HTMLElement | null = null;
  let pan: { id: number; x0: number; y0: number; z0: Zoom } | null = null;
  const isZoomed = () => zoom.s > 1.01 && !!zoomEl?.isConnected;
  function setZoom(el: HTMLElement, z: Zoom, animate: boolean) {
    el.classList.toggle('zooming', !animate); // no transition while the fingers drive it
    el.style.setProperty('--zs', String(z.s));
    el.style.setProperty('--zx', `${z.tx}px`);
    el.style.setProperty('--zy', `${z.ty}px`);
  }
  function resetZoom(animate = true) {
    if (zoomEl) setZoom(zoomEl, NO_ZOOM, animate);
    zoom = NO_ZOOM;
    zoomEl = null;
    pan = null;
  }
  // a new picture on screen starts at its own size
  $effect(() => {
    void current?.id;
    untrack(() => resetZoom(false));
  });
  /** One finger on a zoomed picture moves it; a double tap there resets the zoom. True if it did. */
  function panDown(e: PointerEvent): boolean {
    if (!isZoomed()) return false;
    pan = { id: e.pointerId, x0: e.clientX, y0: e.clientY, z0: zoom };
    startTap(e);
    capture(zoomEl!, e.pointerId);
    return true;
  }
  function panMove(e: PointerEvent): boolean {
    if (!pan || e.pointerId !== pan.id) return false;
    moveTap(e);
    zoom = { s: pan.z0.s, tx: pan.z0.tx + e.clientX - pan.x0, ty: pan.z0.ty + e.clientY - pan.y0 };
    setZoom(zoomEl!, zoom, false);
    return true;
  }
  function panUp(e: PointerEvent): boolean {
    if (!pan || e.pointerId !== pan.id) return false;
    pan = null;
    zoomEl?.classList.remove('zooming');
    if (maxMove <= TAP_SLOP && taps.tap(e.clientX, e.clientY, e.timeStamp) === 2) {
      taps.reset();
      resetZoom();
    }
    return true;
  }
  /** A touch went down: true when it starts a pinch (the second finger). */
  function fingerDown(e: PointerEvent, el: HTMLElement | null | undefined): boolean {
    if (e.pointerType !== 'touch') return false;
    if (e.isPrimary) forgetFingers(); // the first finger of a new touch: no other finger is down
    fingers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (fingers.size !== 2 || !el) return pinched;
    if (drag) {
      cancelDrag();
      springBack();
    }
    if (feed) {
      feed = null;
      void glide(0, 200);
    }
    taps.reset();
    pan = null;
    const [a, b] = [...fingers.values()];
    const r = el.getBoundingClientRect();
    const start = pairOf({ x: a.x - r.left, y: a.y - r.top }, { x: b.x - r.left, y: b.y - r.top });
    let z0 = zoomEl === el ? zoom : NO_ZOOM;
    // the card layout's quiet moment may still be on its way back: the pinch takes the picture at
    // the size it is drawn (its layers go to 1 at once and the zoom takes that scale over)
    const layers = el.querySelector<HTMLElement>(':scope > .layers');
    const sl = layers ? parseFloat(getComputedStyle(layers).scale) || 1 : 1;
    if (Math.abs(sl - 1) > 0.001) {
      const cx = r.width / 2;
      const cy = r.height / 2;
      z0 = { s: z0.s * sl, tx: cx + sl * (z0.tx - cx), ty: cy + sl * (z0.ty - cy) };
      zoom = z0;
      zoomEl = el;
      setZoom(el, z0, false);
    }
    pinch = { start, z0, el, left: r.left, top: r.top };
    pinched = true;
    capture(el, e.pointerId);
    return true;
  }
  /** A touch moved: true when it belongs to a pinch (then it must not drag). */
  function fingerMove(e: PointerEvent): boolean {
    if (!fingers.has(e.pointerId)) return false;
    fingers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pinch && fingers.size >= 2) {
      const [a, b] = [...fingers.values()];
      const p = pinch;
      zoom = pinchZoom(p.z0, p.start, pairOf({ x: a.x - p.left, y: a.y - p.top }, { x: b.x - p.left, y: b.y - p.top }));
      zoomEl = p.el;
      setZoom(p.el, zoom, false);
    }
    return pinched;
  }
  /** A touch left: true when it was part of a pinch. */
  function fingerUp(e: PointerEvent): boolean {
    const was = pinched;
    fingers.delete(e.pointerId);
    if (pinch && fingers.size < 2) endPinch();
    if (!fingers.size) pinched = false;
    return was;
  }
  function endPinch() {
    if (!pinch) return;
    pinch.el.classList.remove('zooming');
    if (zoom.s < 1.05) resetZoom(); // hardly zoomed: back to its size (a CSS transition)
    pinch = null;
  }
  function forgetFingers() {
    fingers.clear();
    endPinch();
    pinched = false;
  }
  // any finger that leaves, wherever it is (the page's own handlers may never see it)
  function anyUp(e: PointerEvent) {
    if (!fingers.has(e.pointerId)) return;
    fingers.delete(e.pointerId);
    if (pinch && fingers.size < 2) endPinch();
    if (!fingers.size) setTimeout(() => !fingers.size && (pinched = false)); // after this event's own handlers
  }

  // ---- taps: double tap = 好評 (both layouts), triple tap = 超好評 (scroll layout)
  const taps = new Taps();
  let down = { x: 0, y: 0 };
  let maxMove = 0;
  function startTap(e: PointerEvent) {
    down = { x: e.clientX, y: e.clientY };
    maxMove = 0;
  }
  function moveTap(e: PointerEvent) {
    maxMove = Math.max(maxMove, Math.hypot(e.clientX - down.x, e.clientY - down.y));
  }

  // a big ♥ / ★ where the finger was
  interface Pop {
    key: number;
    v: Verdict;
    x: number;
    y: number;
  }
  let pops: Pop[] = $state.raw([]);
  let popKey = 0;
  function pop(v: Verdict, x: number, y: number) {
    const r = stage.getBoundingClientRect();
    const p = { key: ++popKey, v, x: x - r.left, y: y - r.top };
    pops = [...pops, p];
    setTimeout(() => (pops = pops.filter((q) => q !== p)), 800);
  }

  // the verdict just given: its button keeps its colour a moment (ライン shows its colour only then)
  let lit: Verdict | null = $state(null);
  let litTimer = 0;
  function light(v: Verdict) {
    lit = v;
    clearTimeout(litTimer);
    litTimer = window.setTimeout(() => (lit = null), 380);
  }

  /** A verdict from a button, a key or the mouse: thrown as a card, or scrolled on. */
  function decide(v: Verdict) {
    if (scroll) feedCommit(v);
    else throwCard(v);
  }

  // ---- throwing and rating
  let cardPending: { card: Card; timer: number } | null = null; // a double tap's like, waiting for a third tap
  function throwCard(v: Verdict, vx = 0, vy = 0) {
    const card = current;
    if (!card) return;
    if (cardPending) {
      clearTimeout(cardPending.timer); // thrown another way meanwhile: the waiting like is dropped
      cardPending = null;
    }
    stopSpring();
    const vel = throwVelocity(v, vx, vy);
    const layers = topEl?.querySelector<HTMLElement>(':scope > .layers');
    const zoomed = layers ? parseFloat(getComputedStyle(layers).scale) || 1 : 1; // after a quiet moment
    ghosts = [...ghosts, { key: ++ghostKey, card, verdict: v, x: dx, y: dy, r: rot, zoomed, ...vel }];
    sweepFlash(v);
    light(v);
    navigator.vibrate?.(8);
    dx = dy = rot = 0;
    photoReady = decoded.has(nextCardAfter(card)?.id ?? -1);
    rate(card, v);
    pose();
  }

  function nextCardAfter(card: Card): Card | undefined {
    if (redo !== null) return redo + 1 < history.length ? history[redo + 1].card : queue[0];
    return queue[0] === card ? queue[1] : queue[0];
  }

  function rate(card: Card, v: Verdict) {
    const replace = redo !== null ? history[redo] : null;
    const cid = newCid();
    const earlier = card.rated && card.last ? VERDICT_OF[card.last] ?? null : null;
    const info: Last = {
      verdict: v,
      prev: replace ? replace.verdict : v !== 'archive' ? earlier : null,
      corrected: !!replace,
      before: !replace && card.rated ? card.p : null,
      after: null,
    };
    last = info;
    if (replace) {
      // a correction keeps its place, and the next picture is the one seen after it (as in
      // Version 1): going back and forth always follows the order the pictures were seen in
      const k = history.indexOf(replace);
      history = history.map((h) => (h === replace ? { card, verdict: v, cid } : h));
      redo = k >= 0 && k + 1 < history.length ? k + 1 : null;
    } else {
      queue = queue.slice(1);
      history = [...history.slice(-399), { card, verdict: v, cid }];
      if (remaining !== null) remaining = Math.max(0, remaining - 1);
    }
    if (queue.length < 4) refill();
    queue.slice(0, 6).forEach(preload);
    outbox.push({ id: card.id, fp: card.fp, a: LETTER[v], cid, replace: replace?.cid ?? '' }).then((saved) => {
      if (saved?.item && last === info) last = { ...info, after: saved.item.p };
    });
  }

  /** The previous card comes back along the path it left by. */
  async function back() {
    if (scroll) return feedBack();
    if (!history.length) return;
    const idx = redo === null ? history.length - 1 : redo - 1;
    if (idx < 0) return;
    const w = deck.clientWidth, h = deck.clientHeight;
    const from: Record<Verdict, [number, number, number]> = {
      like: [w * 0.9, 0, 12],
      pass: [-w * 0.9, 0, -12],
      super: [0, -h * 0.8, 0],
      archive: [0, h * 0.8, 0],
    };
    stopSpring();
    const card = history[idx].card;
    ghosts = ghosts.filter((g) => g.card !== card); // it is coming back: no second copy flying away
    redo = idx;
    photoReady = decoded.has(card.id);
    [dx, dy, rot] = from[history[idx].verdict];
    await tick(); // the returning card's element exists now: place it before it is painted
    pose();
    springBack();
  }

  /** The phone's back button: closes a menu, else the previous picture; false when there is none. */
  export function systemBack(): boolean {
    if (picking || targetMenu || sheet) {
      picking = targetMenu = sheet = false;
      return true;
    }
    if (!history.length || redo === 0) return false;
    back();
    return true;
  }

  function forward() {
    if (redo === null) return;
    redo = redo + 1 < history.length ? redo + 1 : null;
  }

  // ---- the scroll layout: one picture after another, like short-video apps. Up = next (見送り
  // unless it was rated; a picture seen again just moves on), down = back. The previous, current
  // and next pictures are one strip (--fy moves it), so the next one is already there.
  const scroll = $derived(prefs.layout === 'scroll');
  const prevCard = $derived(redo !== null ? (history[redo - 1]?.card ?? null) : (history.at(-1)?.card ?? null));
  const nextCard = $derived(redo !== null ? (history[redo + 1]?.card ?? queue[0] ?? null) : (queue[1] ?? null));
  const slides = $derived.by(() => {
    const out: { card: Card; k: number }[] = [];
    const seen = new Set<number>();
    for (const [card, k] of [[current, 0], [prevCard, -1], [nextCard, 1]] as [Card | null, number][]) {
      if (card && !seen.has(card.id)) out.push({ card, k }), seen.add(card.id);
    }
    return out;
  });
  let feed: { id: number; y0: number; axis: 'x' | 'y' | null } | null = null;
  let fy = 0;
  let feedBusy = false;
  let pending: { v: Verdict; timer: number } | null = null; // a tapped verdict, moving on in a moment

  function feedPose() {
    deck?.style.setProperty('--fy', `${fy}px`);
  }
  /** Eases the strip to fy in ms; resolves even in a hidden window (no frames there). */
  function glide(to: number, ms: number, inOut = false): Promise<void> {
    const from = fy, t0 = performance.now();
    return new Promise((done) => {
      let over = false;
      const end = () => {
        if (over) return;
        over = true;
        fy = to;
        feedPose();
        done();
      };
      const step = (now: number) => {
        if (over) return;
        const k = Math.min(1, (now - t0) / ms);
        const e = inOut ? (k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2) : 1 - Math.pow(1 - k, 3);
        fy = from + (to - from) * e;
        feedPose();
        if (k < 1) requestAnimationFrame(step);
        else end();
      };
      requestAnimationFrame(step);
      setTimeout(end, ms + 250);
    });
  }
  /** Moves the strip one picture (1 = on, -1 = back), then makes the change and settles. */
  async function feedGo(dir: 1 | -1, change: () => void, ms = 230) {
    feedBusy = true;
    await glide(-dir * deck.clientHeight, ms, ms > 230);
    change();
    await tick();
    fy = 0;
    feedPose();
    feedBusy = false;
  }
  /** Rates the current picture and scrolls on; with a wait, a further tap may still change it. */
  function feedCommit(v: Verdict, wait = 0, released = false) {
    const card = current;
    if (!card || feedBusy) return;
    if (pending) clearTimeout(pending.timer);
    const go = () => {
      pending = null;
      if (v === 'like' || v === 'super') edgeFlash(v); // 見送り moves on quietly
      light(v);
      navigator.vibrate?.(8);
      void feedGo(1, () => rate(card, v), released ? 230 : 320);
    };
    if (wait) pending = { v, timer: window.setTimeout(go, wait) };
    else go();
  }
  /** The verdict of the picture on screen when it is a seen one (after back, or rated in place). */
  const shownVerdict = $derived(redo !== null ? (history[redo]?.verdict ?? null) : null);
  /** Auto next off: rates the picture at once and keeps it on screen (a correction stays too). */
  // the last rating made in place, so the lit button can take it back
  let stayed: { card: Card; prev: Verdict | null } | null = null;
  function rateStay(v: Verdict) {
    const card = current;
    if (!card || feedBusy || shownVerdict === v) return;
    const k = redo;
    const prev = k !== null ? (stayed?.card === card ? stayed.prev : history[k].verdict) : null;
    rate(card, v);
    redo = k ?? history.length - 1;
    stayed = { card, prev };
    edgeFlash(v);
    light(v);
    navigator.vibrate?.(8);
  }
  function cancelStay() {
    const card = current;
    if (!card || feedBusy || redo === null) return;
    const k = redo;
    const prev = stayed?.card === card ? stayed.prev : null;
    if (prev) {
      rate(card, prev); // a seen picture: back to the verdict it had
      redo = k;
    } else {
      // this visit's rating is withdrawn: a new picture is unrated again (and next), a picture rated
      // before this visit has its earlier rating again
      const entry = history[k];
      outbox.push({ id: card.id, fp: card.fp, a: 'X', cid: newCid(), replace: entry.cid });
      history = history.filter((_, i) => i !== k);
      queue = [card, ...queue];
      redo = null;
      last = null;
      if (remaining !== null) remaining++;
    }
    stayed = null;
    navigator.vibrate?.(8);
  }
  // card model: the verdict's colour sweeps in from the side the card left by (演出 decides how much)
  let sweep: { key: number; v: Verdict } | null = $state.raw(null); // raw: compared by identity below
  let sweepKey = 0;
  function sweepFlash(v: Verdict) {
    if (fx.level === 'off') return;
    const s = { key: ++sweepKey, v };
    sweep = s;
    setTimeout(() => sweep === s && (sweep = null), 700);
  }
  // the verdict's colour lights up the edge of the picture for a moment (演出 decides how much)
  let edge: { key: number; v: Verdict } | null = $state.raw(null);
  let edgeKey = 0;
  function edgeFlash(v: Verdict) {
    if (fx.level === 'off') return;
    const e = { key: ++edgeKey, v };
    edge = e;
    setTimeout(() => edge === e && (edge = null), 700);
  }
  /** ♥ / ★ / ✕ in the scroll model: rate and move on, or (auto next off) ♥ / ★ rate in place. */
  function feedTap(v: Verdict) {
    if (shownVerdict === v && (v === 'like' || v === 'super')) cancelStay(); // a lit ♥ / ★ again: taken back
    else if (prefs.autoNext || v === 'pass' || v === 'archive') feedCommit(v);
    else if (shownVerdict === v) cancelStay(); // pressed again: taken back
    else rateStay(v);
  }
  function feedOn(released = false) {
    if (redo !== null) void feedGo(1, forward, released ? 230 : 320); // a seen picture: just onward
    else feedCommit('pass', 0, released);
  }
  function feedBack() {
    if (feedBusy) return;
    const idx = redo === null ? history.length - 1 : redo - 1;
    if (idx < 0) return void glide(0, 200);
    void feedGo(-1, () => (redo = idx));
  }
  function feedDown(e: PointerEvent) {
    wake();
    if ((e.target as Element).closest('button') || feedBusy || !current || atEdge(e)) return;
    if (fingerDown(e, deck.querySelector<HTMLElement>('.slide.cur'))) return;
    if (panDown(e)) return;
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    feed = { id: e.pointerId, y0: e.clientY, axis: null };
    tracker.reset();
    tracker.add(e.clientX, e.clientY, e.timeStamp);
    startTap(e);
    capture(deck, e.pointerId);
  }
  function feedMove(e: PointerEvent) {
    if (fingerMove(e) || panMove(e)) return;
    if (!feed || e.pointerId !== feed.id) return;
    moveTap(e);
    tracker.add(e.clientX, e.clientY, e.timeStamp);
    if (pending) return; // a tapped picture is on its way: no scrolling meanwhile
    feed.axis ??= axisOf(e.clientX - down.x, e.clientY - down.y);
    if (feed.axis === 'y') fy = e.clientY - feed.y0; // the strip only ever moves up and down
    feedPose();
  }
  function feedUp(e: PointerEvent) {
    if (fingerUp(e) || panUp(e)) return;
    if (!feed || e.pointerId !== feed.id) return;
    const f = feed;
    feed = null;
    if (maxMove <= TAP_SLOP) {
      const n = taps.tap(e.clientX, e.clientY, e.timeStamp);
      if (n === 2) {
        pop('like', e.clientX, e.clientY);
        if (prefs.autoNext) feedCommit('like', 380);
        else rateStay('like');
      } else if (n === 3 && (pending || !prefs.autoNext)) {
        taps.reset();
        pop('super', e.clientX, e.clientY);
        if (prefs.autoNext) feedCommit('super', 380);
        else rateStay('super');
      }
      return;
    }
    if (pending) return;
    const { vy } = tracker.velocity(e.timeStamp);
    if (f.axis === 'y') {
      const r = scrollRelease(e.clientY - f.y0, vy, deck.clientHeight);
      if (r === 'next') return feedOn(true);
      if (r === 'back' && prevCard) return feedBack();
    }
    void glide(0, 220);
  }
  // the 再評価 pill shows for a moment after a re-rating, then steps aside
  let pillOn = $state(false);
  let pillTimer = 0;
  $effect(() => {
    const show = !!last?.prev;
    untrack(() => {
      clearTimeout(pillTimer);
      pillOn = show;
      if (show) pillTimer = window.setTimeout(() => (pillOn = false), 2000);
    });
  });
  // ---- a few quiet seconds (no touch, key or mouse move): only the picture stays and slowly comes
  // closer; any of them brings everything back. The scroll layout always; the card layout once its
  // buttons are hidden (on a phone the app's tab bar goes with them). Never while a card or a slide
  // is held, a picture is zoomed by hand, a menu is open or flick mode is on.
  const IDLE_MS = 2500;
  let idle = $state(false);
  let idleTimer = 0;
  let lastInput = 0;
  const quietHere = () => scroll || !showButtons;
  const calm = () =>
    !!current && !drag && !feed && !pending && !cardPending && !pinched && !isZoomed() && !flick.on && !picking && !targetMenu && !sheet;
  function wake() {
    lastInput = performance.now();
    idle = false;
    clearTimeout(idleTimer);
    if (active && quietHere()) idleTimer = window.setTimeout(checkIdle, IDLE_MS);
  }
  function checkIdle() {
    if (!active || !quietHere()) return;
    const left = lastInput + IDLE_MS - performance.now();
    if (left > 16) idleTimer = window.setTimeout(checkIdle, left); // a mouse moved meanwhile: the rest of the wait
    else if (calm()) idle = true;
    else wake(); // busy: look again in a while
  }
  /** A pointer move or let go: puts the quiet off (no timer per move), or ends it. */
  function stir() {
    lastInput = performance.now();
    if (idle) wake();
  }
  $effect(() => {
    // shown again, a menu, sheet or flick mode opening or closing, the buttons shown or hidden, the
    // other layout, the card layout's next card (a throw, undo, the phone's back): the wait starts anew
    void [picking, targetMenu, sheet, flick.on, showButtons, scroll, scroll ? 0 : current?.id];
    if (!active) {
      idle = false;
      return;
    }
    untrack(wake);
    return () => clearTimeout(idleTimer);
  });

  /** Marks a picture ready once it has loaded (or at once if it is in the cache already). */
  function loaded(img: HTMLImageElement) {
    const on = () => img.classList.add('ready');
    if (img.complete && img.naturalWidth) on();
    else img.addEventListener('load', on, { once: true });
  }

  // ---- a thrown card flies on by itself (time-based), then is removed
  function fly(node: HTMLElement, g: Ghost) {
    let t0 = performance.now();
    let age = 0;
    let raf = requestAnimationFrame(function step(now) {
      const dt = Math.min(now - t0, 32);
      t0 = now;
      age += dt;
      const speed = 0.75 + Math.min(1, age / 200) * 0.5; // eases in, then keeps going
      g.x += g.vx * dt * speed;
      g.y += g.vy * dt * speed;
      g.r += (g.vx / 40) * dt * 0.6 * speed;
      const k = Math.min(1, age / 280);
      const scale = g.verdict === 'archive' ? 1 - 0.75 * k : 1;
      node.style.transform = `translate3d(${g.x}px, ${g.y}px, 0) rotate(${g.r}deg) scale(${scale})`;
      node.style.opacity = String(1 - Math.max(0, (age - 110) / 190));
      if (age < 300) raf = requestAnimationFrame(step);
      else ghosts = ghosts.filter((x) => x !== g);
    });
    // a hidden or minimised window gets no frames: the ghost still goes
    const done = setTimeout(() => (ghosts = ghosts.filter((x) => x !== g)), 460);
    return {
      destroy: () => {
        cancelAnimationFrame(raf);
        clearTimeout(done);
      },
    };
  }

  // ---- keyboard (layouts A/B/C) and the wheel (mode 1: up = back, down = forward)
  let holdTimer = 0;
  function onKeyDown(e: KeyboardEvent) {
    if (!active) return;
    wake();
    if (picking || targetMenu || sheet) {
      if (e.code === 'Escape') (picking = false), (targetMenu = false), (sheet = false);
      return;
    }
    if (e.code === 'Escape' && flick.on) {
      flickOff();
      return;
    }
    const cmd = commandFor(st.keyMode, e.code);
    if (!cmd) return;
    e.preventDefault();
    if (cmd === 'likeHold') {
      if (!e.repeat && !holdTimer) holdTimer = window.setTimeout(() => ((holdTimer = 0), decide('super')), HOLD_MS);
      return;
    }
    if (e.repeat && cmd !== 'back') return;
    if (cmd === 'back') back();
    else decide(cmd);
  }
  function onKeyUp(e: KeyboardEvent) {
    if (!active) return;
    if (commandFor(st.keyMode, e.code) === 'likeHold' && holdTimer) {
      clearTimeout(holdTimer);
      holdTimer = 0;
      decide('like');
    }
  }
  // The wheel: going back / forward may repeat while it turns; a rating is one per flick of the
  // wheel (the lock ends only after the wheel has been still a moment).
  let wheelLock = 0;
  function onWheel(e: WheelEvent) {
    e.preventDefault();
    if (Math.abs(e.deltaY) < 1 || e.ctrlKey) return;
    if (scroll) {
      // the scroll layout: the wheel scrolls (down = next, up = back), one picture per turn
      if (e.timeStamp < wheelLock) return;
      wheelLock = e.timeStamp + 260;
      if (e.deltaY > 0) feedOn();
      else feedBack();
      return;
    }
    const cmd = mouseMap(st.mouseMode, st.customMouse)[e.deltaY < 0 ? 'wheelUp' : 'wheelDown'];
    if (cmd === 'none' || cmd === 'grab') return;
    if (!RATING_CMDS.has(cmd)) {
      if (e.timeStamp < wheelLock) return;
      wheelLock = e.timeStamp + 150;
      run(cmd);
      return;
    }
    const locked = e.timeStamp < wheelLock;
    wheelLock = e.timeStamp + 220;
    if (!locked) run(cmd);
  }

  function run(cmd: MouseCmd) {
    if (cmd === 'back') back();
    else if (cmd === 'fwd') forward();
    else if (cmd === 'flick') toggleFlick();
    else if (cmd === 'like' || cmd === 'super' || cmd === 'pass' || cmd === 'archive') decide(cmd);
  }

  // ---- flick (mode 3): right click starts and ends it (Esc ends it too). The pointer is locked to
  // the deck, so it never runs into a screen edge; the card leans with the leaking movement and is
  // thrown like a dragged one.
  const flick = $state({ on: false });
  const flickMove = new Flick();
  let flickRaf = 0;
  function toggleFlick() {
    if (flick.on) flickOff();
    else {
      flick.on = true;
      flickMove.reset(performance.now());
      try {
        const p = deck.requestPointerLock() as unknown as Promise<void> | undefined;
        p?.catch?.(() => {}); // without the lock flicks still work, the pointer just reaches the edge
      } catch {
        /* same */
      }
    }
  }
  function flickOff() {
    if (!flick.on) return;
    flick.on = false;
    cancelAnimationFrame(flickRaf);
    flickRaf = 0;
    if (document.pointerLockElement) document.exitPointerLock();
    springBack();
  }
  function onFlickMove(e: MouseEvent) {
    if (!flick.on || !current) return;
    const now = performance.now();
    const hit = flickMove.move(e.movementX, e.movementY, now);
    if (hit) {
      const v = Math.abs(hit.vx) >= Math.abs(hit.vy) ? (hit.vx > 0 ? 'like' : 'pass') : hit.vy < 0 ? 'super' : 'archive';
      throwCard(v, hit.vx, hit.vy);
      return;
    }
    flickLean();
  }
  // the card follows the leaking movement and settles back as it leaks away
  function flickLean() {
    cancelAnimationFrame(flickRaf);
    flickMove.leak(performance.now());
    dx = flickMove.x * 1.6;
    dy = flickMove.y * 1.6;
    rot = flickMove.x * 0.05;
    pose();
    flickRaf = flick.on && Math.abs(flickMove.x) + Math.abs(flickMove.y) > 0.5 ? requestAnimationFrame(flickLean) : 0;
  }

  function toggleButtons() {
    buttons = !showButtons;
    try {
      localStorage.setItem('choosen3.buttons', buttons ? '1' : '0');
    } catch {
      /* private mode: for this visit only */
    }
  }

  // another screen: flicking ends (the pointer is free again)
  $effect(() => {
    if (!active && flick.on) flickOff();
  });

  // The light follows the current photo.
  $effect(() => {
    const c = current;
    if (!c || !ambient) return;
    paletteOf(thumbUrl(c, 64))
      .then((p) => current === c && ambient.setColors(p))
      .catch(() => {});
  });

  onMount(() => {
    const onFocus = () => (focusAt = performance.now());
    const noNav = (e: MouseEvent) => {
      if (e.button === 3 || e.button === 4 || (e.type === 'mousedown' && e.button === 1)) e.preventDefault();
    };
    const lockChange = () => {
      if (!document.pointerLockElement && flick.on) flickOff();
    };
    addEventListener('focus', onFocus);
    addEventListener('pointerup', anyUp, true);
    addEventListener('pointercancel', anyUp, true);
    for (const t of ['mousedown', 'mouseup', 'auxclick'] as const) document.addEventListener(t, noNav);
    document.addEventListener('pointerlockchange', lockChange);
    document.addEventListener('mousemove', onFlickMove);
    const unhook = () => {
      removeEventListener('focus', onFocus);
      removeEventListener('pointerup', anyUp, true);
      removeEventListener('pointercancel', anyUp, true);
      for (const t of ['mousedown', 'mouseup', 'auxclick'] as const) document.removeEventListener(t, noNav);
      document.removeEventListener('pointerlockchange', lockChange);
      document.removeEventListener('mousemove', onFlickMove);
    };
    ambient = new Ambient(ambientCanvas);
    refill();
    void outbox.pump();
    return () => {
      unhook();
      ambient.stop();
    };
  });

  const tags = (c: Card) => [c.rated ? t('{0}回目', c.n + 1) : t('新規'), ...(c.rated ? [fmtPts(c.p, st.overPoints)] : [])];
</script>

<svelte:window onkeydown={onKeyDown} onkeyup={onKeyUp} />

<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="stage" class:flicking={flick.on} class:idle data-fx={fx.level} bind:this={stage} onpointerdowncapture={wake} onpointermove={stir} onpointerupcapture={stir} onpointercancelcapture={stir} oncontextmenu={(e) => e.preventDefault()}>
  <canvas class="ambient" bind:this={ambientCanvas}></canvas>

  <header>
    <button class="pill" class:active={scope.length > 0} class:only={!scope.length} class:veiled={!prefs.info} title={t('写真フォルダ: {0}', scopeLabel)} onclick={() => ((addingFolder = false), (sheet = true))}>
      {#if scope.length}<span class="ic">{@html svg('folder', 15)}</span><span class="txt">{scopeLabel}</span>
        {#if remaining !== null}<span class="left num" title={t('残り {0} 枚', remaining)}>{remaining}</span>{/if}
      {:else}<span class="ic">{@html svg('folderPlus', 18)}</span>{/if}
    </button>
    <div class="menu-wrap" class:veiled={!prefs.info}>
      <button class="pill" class:active={filter !== ''} title={t('対象')} onclick={() => (targetMenu = !targetMenu)}>
        <span class="ic">{@html svg('filter', 15)}</span><span class="txt">{target.chip}</span>
      </button>
      {#if targetMenu}
        <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
        <div class="scrim" onclick={() => (targetMenu = false)}></div>
        <div class="menu">
          {#each targets as t (t.v)}
            <button class:on={t.v === filter} onclick={() => choose({ filter: t.v })}>
              <span class="ic">{#if t.v === filter}{@html svg('check', 16)}{/if}</span>{t.menu}
            </button>
          {/each}
        </div>
      {/if}
    </div>
    {#if last?.prev && pillOn}
      <button transition:fade={{ duration: 220 }} class="pill history" title={t('{0} {1} — 押すと前の画像へ', last.corrected ? t('訂正') : t('再評価'), LABEL[last.verdict])} onclick={back}>
        {#if last.prev}
          <span class="ic" style:color={COLORS[last.prev]}>{@html svg(ICON_OF[last.prev], 16)}</span>
          <span class="arrow">›</span>
        {/if}
        <span class="ic" style:color={COLORS[last.verdict]}>{@html svg(ICON_OF[last.verdict], 16)}</span>
        {#if last.after !== null && last.verdict !== 'archive'}
          <span class="num pts">{last.before !== null && Math.round(last.before) !== Math.round(last.after) ? `${fmtPts(last.before, st.overPoints)}→` : ''}{fmtPts(last.after, st.overPoints)}</span>
        {/if}
      </button>
    {/if}
    {#if redo !== null}<span class="redo">{@html svg('undo', 16)}</span>{/if}
    {#if flick.on}<span class="flick-badge" title={t('右クリックか Esc で終了')}>{@html svg('sparkle', 14)}{t('フリック')}</span>{/if}
    <button class="fx" class:dim={fx.level !== 'normal'} title={FX_LABEL[fx.level]} onclick={cycleFx}>{@html svg('sparkle', 18)}</button>
  </header>

  {#if scroll}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="deck feed" bind:this={deck} onpointerdown={feedDown} onpointermove={feedMove} onpointerup={feedUp} onpointercancel={feedUp} onwheel={onWheel}>
    {#each slides as s (s.card.id)}
      <div class="card slide" class:cur={s.k === 0} style:--k={s.k}>
        <img class="fill" src={thumbUrl(s.card, 64)} alt="" />
        <img class="lqip" src={thumbUrl(s.card, 320)} alt="" />
        <img class="photo" src={imgUrl(s.card)} alt="" use:loaded draggable="false" />
        {#if s.k === 0}
          <div class="badge">
            {#each VERDICTS as v}
              <span class="b {v}" style:color={COLORS[v]}>{@html svg(ICON_OF[v], 96)}</span>
            {/each}
          </div>
          <div class="meta" class:low={!showButtons} class:off={!prefs.info}>
            <div class="place">{placeOf(s.card.dir) || rootName || t('写真フォルダ')}</div>
            <div class="name">{s.card.name}</div>
            <div class="tags">{#each tags(s.card) as t}<span class="num">{t}</span>{/each}</div>
          </div>
        {/if}
      </div>
    {:else}
      {#if exhausted}
        <div class="done">
          {#if st.scanning}<span class="loading"></span><span>{t('写真を読み込んでいます…')}</span>
          {:else}{@html svg('heart', 40)}<span>{t('今は評価する画像がありません')}</span>
            {#if !st.remote && !IOS}<button class="btn add-folder" onclick={() => ((addingFolder = true), (sheet = true))}>{@html svg('folderPlus', 18)}{t('写真フォルダを追加')}</button>{/if}
          {/if}
        </div>
      {/if}
    {/each}
    {#if edge}
      {#key edge.key}<div class="edge" style:--c={COLORS[edge.v]}></div>{/key}
    {/if}
    {#if current}
      <div class="buttons" class:hidden={!showButtons} data-style={prefs.buttons} data-lit={lit}>
        <button class="rb small" title={t('前の画像に戻る')} disabled={!history.length} onclick={back}>{@html svg('undo', 25)}</button>
        <button class="rb pass" title={t('見送り')} onclick={() => feedTap('pass')}>{@html svg('x', 28)}</button>
        <button class="rb super mid" class:marked={shownVerdict === 'super'} title={t('超好評')} onclick={() => feedTap('super')}>{@html svg('star', 26)}</button>
        <button class="rb like" class:marked={shownVerdict === 'like'} title={t('好評')} onclick={() => feedTap('like')}>{@html svg('heart', 28)}</button>
      </div>
      <!-- the picture's mark sits behind its own button: seen once the buttons are hidden; a tap
           takes the rating back -->
      <div class="buttons marks" class:shown={!showButtons && !!shownVerdict} aria-hidden={showButtons}>
        {#each ['back', 'pass', 'super', 'like'] as slot (slot)}
          <span class="rb slot" class:small={slot === 'back'} class:mid={slot === 'super'}>
            {#if shownVerdict && slot === (shownVerdict === 'archive' ? 'pass' : shownVerdict)}
              <button
                class="mark"
                style:color={COLORS[shownVerdict]}
                title={t('{0}を取り消す', LABEL[shownVerdict])}
                tabindex={showButtons ? -1 : 0}
                onclick={cancelStay}>{@html svg(ICON_OF[shownVerdict], 30)}</button
              >
            {/if}
          </span>
        {/each}
      </div>
      <button class="chev" class:low={!showButtons} title={showButtons ? t('ボタンを隠す') : t('ボタンを表示')} onclick={toggleButtons}>
        {@html svg(showButtons ? 'down' : 'up', 18)}
      </button>
      <button class="eye" title={prefs.info ? t('フォルダ名とファイル名を隠す') : t('フォルダ名とファイル名を表示')} onclick={toggleInfo}>
        {@html svg(prefs.info ? 'eye' : 'eyeOff', 18)}
      </button>
    {/if}
  </div>
  {:else}
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div class="deck" bind:this={deck} onpointerdown={onPointerDown} onwheel={onWheel}>
    {#each behind as c, i (c.id)}
      <div class="card behind" style:--i={i + 1}>
        <img class="fill" src={thumbUrl(c, 128)} alt="" />
      </div>
    {/each}

    {#if current}
      {#key current.id}
        <!-- svelte-ignore a11y_no_static_element_interactions -->
        <div
          class="card top"
          bind:this={topEl}
          onpointermove={onPointerMove}
          onpointerup={onPointerUp}
          onpointercancel={onPointerUp}
        >
          <!-- the picture's layers: they come slowly closer in a quiet moment, inside the card (the card
               keeps its drag, the pictures their pinch zoom) -->
          <div class="layers">
            <img class="fill" src={thumbUrl(current, 64)} alt="" />
            <img class="lqip" src={thumbUrl(current, 320)} alt="" />
            <img class="photo" class:ready={photoReady} src={imgUrl(current)} alt="" onload={() => (photoReady = true)} draggable="false" />
          </div>
          <div class="glare"></div>
          <div class="rim"></div>
          <div class="badge">
            {#each VERDICTS as v}
              <span class="b {v}" style:color={COLORS[v]}>{@html svg(ICON_OF[v], 96)}</span>
            {/each}
          </div>
          <div class="meta" class:low={!showButtons} class:off={!prefs.info}>
            <div class="place">{placeOf(current.dir) || rootName || t('写真フォルダ')}</div>
            <div class="name">{current.name}</div>
            <div class="tags">{#each tags(current) as t}<span class="num">{t}</span>{/each}</div>
          </div>
        </div>
      {/key}
      <div class="buttons fixed" class:hidden={!showButtons} data-style={prefs.buttons} data-lit={lit}>
        <button class="rb small" title={t('前の画像に戻る')} disabled={!history.length} onclick={back}>{@html svg('undo', 25)}</button>
        <button class="rb pass" title={t('見送り')} onclick={() => decide('pass')}>{@html svg('x', 28)}</button>
        <button class="rb super mid" title={t('超好評')} onclick={() => decide('super')}>{@html svg('star', 26)}</button>
        <button class="rb like" title={t('好評')} onclick={() => decide('like')}>{@html svg('heart', 28)}</button>
      </div>
      <button class="chev fixed" class:low={!showButtons} title={showButtons ? t('ボタンを隠す') : t('ボタンを表示')} onclick={toggleButtons}>
        {@html svg(showButtons ? 'down' : 'up', 18)}
      </button>
      <button class="eye fixed" title={prefs.info ? t('フォルダ名とファイル名を隠す') : t('フォルダ名とファイル名を表示')} onclick={toggleInfo}>
        {@html svg(prefs.info ? 'eye' : 'eyeOff', 18)}
      </button>
      {#if !showButtons}
        <button class="miniback fixed" title={t('前の画像に戻る')} disabled={!history.length} onclick={back}>{@html svg('undo', 21)}</button>
      {/if}
    {:else if exhausted}
      <div class="done">
        {#if st.scanning}<span class="loading"></span><span>{t('写真を読み込んでいます…')}</span>
        {:else}{@html svg('heart', 40)}<span>{t('今は評価する画像がありません')}</span>
          {#if !st.remote && !IOS}<button class="btn add-folder" onclick={() => ((addingFolder = true), (sheet = true))}>{@html svg('folderPlus', 18)}{t('写真フォルダを追加')}</button>{/if}
        {/if}
      </div>
    {/if}

    {#each ghosts as g (g.key)}
      <div class="card ghost" use:fly={g}>
        <div class="layers" style:scale={g.zoomed !== 1 ? g.zoomed : null}>
          <img class="fill" src={thumbUrl(g.card, 64)} alt="" />
          <img class="photo ready" src={imgUrl(g.card)} alt="" />
        </div>
      </div>
    {/each}
  </div>
  {/if}

  {#if sweep}
    {#key sweep.key}<div class="sweep {sweep.v}" style:--c={COLORS[sweep.v]}></div>{/key}
  {/if}
  {#each pops as p (p.key)}
    <span class="pop" style:left={`${p.x}px`} style:top={`${p.y}px`} style:color={COLORS[p.v]}>{@html svg(ICON_OF[p.v], 120)}</span>
  {/each}
</div>

{#if sheet}
  <FolderSheet
    {st}
    {custom}
    startAdding={addingFolder}
    onapply={(tokens, label) => ((custom = ''), choose({ scope: tokens, label }))}
    onsub={() => ((sheet = false), (picking = true))}
    onclose={() => ((sheet = false), (addingFolder = false))}
  />
{/if}
{#if picking}
  <ScopePicker current={custom ? scope : []} onpick={pickFiner} onclose={() => (picking = false)} />
{/if}

<style>
  .stage {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
    --amt: 0;
  }
  .ambient {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    z-index: 0;
  }
  header {
    position: relative;
    z-index: 2;
    height: 52px;
    display: flex;
    align-items: center;
    justify-content: center;
    gap: 8px;
    padding: 0 52px; /* room for the effects switch */
  }
  @media (max-width: 759px) {
    /* the picture runs up to the top of the screen (under the hidden status bar, like a video
       app); the top line floats over it, below the camera (--safe-top from the Android app) */
    header {
      position: absolute;
      top: 0;
      left: 0;
      right: 0;
      z-index: 3;
      height: auto;
      padding: calc(var(--safe-top, env(safe-area-inset-top, 0px)) + 8px) 44px 8px 10px;
      justify-content: flex-start;
      pointer-events: none;
    }
    header > * {
      pointer-events: auto;
    }
    .fx {
      top: calc(var(--safe-top, env(safe-area-inset-top, 0px)) + 10px);
    }
    .pill .txt {
      max-width: 96px;
    }
    .stage .deck {
      margin: 0; /* edge to edge (the base rule below would win otherwise) */
    }
    .card.top,
    .card.behind,
    .card.ghost {
      border-radius: 0; /* no gap with the light behind it showing through */
    }
  }
  .pill.only {
    padding: 6px 10px;
  }
  /* over a photo (or its dark glass) the ink stays white in both looks; the menu takes the page's */
  header,
  .card,
  .buttons {
    color: #f2f3f5;
  }
  .menu {
    color: var(--text);
  }
  /* the scroll layout: a strip of full pictures (previous above, next below) */
  .feed {
    overflow: hidden;
    touch-action: none;
    border-radius: var(--radius-card);
  }
  .card.slide {
    border-radius: 0;
    box-shadow: none;
    transform: translate3d(0, calc(var(--k) * 100% + var(--fy, 0px)), 0);
  }
  .meta.off {
    opacity: 0;
  }
  /* pinch zoom: the picture scales around the point between the fingers, then springs back */
  .card .photo,
  .card .lqip {
    transform: translate(var(--zx, 0px), var(--zy, 0px)) scale(var(--zs, 1));
    transform-origin: 0 0;
    transition:
      opacity 160ms linear,
      transform 280ms var(--ease-out);
  }
  .card:global(.zooming) .photo,
  .card:global(.zooming) .lqip {
    transition: opacity 160ms linear;
  }
  .card:global(.zooming) {
    z-index: 4;
    overflow: visible;
  }
  .edge {
    position: absolute;
    inset: 0;
    z-index: 3;
    pointer-events: none;
    box-shadow:
      inset 0 0 0 3px var(--c),
      inset 0 0 46px color-mix(in srgb, var(--c) 70%, transparent);
    animation: edgeflash 700ms ease-out forwards;
  }
  .stage[data-fx='calm'] .edge {
    box-shadow:
      inset 0 0 0 2px var(--c),
      inset 0 0 22px color-mix(in srgb, var(--c) 45%, transparent);
  }
  @keyframes edgeflash {
    0% {
      opacity: 0;
    }
    20% {
      opacity: 1;
    }
    100% {
      opacity: 0;
    }
  }
  .pill .left {
    padding: 1px 7px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.12);
    font-size: 11.5px;
  }
  /* the picture's mark (auto next off, or a seen picture), behind its button; a tap takes it back */
  .marks {
    z-index: 1;
    opacity: 0;
    pointer-events: none;
    transition: opacity 200ms linear;
  }
  .marks.shown {
    opacity: 1;
  }
  .marks.shown .mark {
    pointer-events: auto;
  }
  .marks .slot {
    border: none;
    background: none;
    backdrop-filter: none;
    cursor: default;
  }
  .mark {
    display: grid;
    place-items: center;
    width: 46px;
    height: 46px;
    padding: 0;
    border: none;
    border-radius: 50%;
    background: rgba(0, 0, 0, 0.35);
    cursor: pointer;
    filter: drop-shadow(0 0 10px currentColor);
    animation: markin 260ms var(--ease-out);
  }
  @keyframes markin {
    from {
      scale: 0.4;
      opacity: 0;
    }
  }
  .rb.like.marked {
    background: color-mix(in srgb, var(--like) 45%, rgba(0, 0, 0, 0.35));
  }
  .rb.super.marked {
    background: color-mix(in srgb, var(--super) 45%, rgba(0, 0, 0, 0.35));
  }
  /* a few quiet seconds: only the picture */
  header,
  .buttons,
  .chev,
  .eye,
  .meta,
  .mark,
  .miniback,
  .card.top::after {
    transition: opacity 400ms;
  }
  .stage.idle header,
  .stage.idle .buttons,
  .stage.idle .chev,
  .stage.idle .eye,
  .stage.idle .meta,
  .stage.idle .mark,
  .stage.idle .miniback,
  .stage.idle .card.top::after {
    opacity: 0;
    pointer-events: none;
  }
  /* on a phone the header's own buttons take touches (header > *): not while they are invisible */
  .stage.idle header > * {
    pointer-events: none;
  }
  .eye {
    position: absolute;
    right: 10px;
    bottom: 6px;
    width: 32px;
    height: 28px;
    border: none;
    display: grid;
    place-items: center;
    background: transparent;
    color: rgba(255, 255, 255, 0.6);
    filter: drop-shadow(0 1px 3px rgba(0, 0, 0, 0.6));
    cursor: pointer;
    z-index: 2;
  }
  /* the card model's controls sit above the cards and stay put while a card moves */
  .buttons.fixed,
  .chev.fixed,
  .eye.fixed {
    z-index: 7;
  }
  @media (max-width: 759px) {
    .feed {
      border-radius: 0;
    }
  }
  /* the ♥ / ★ of a double tap or a long press, where the finger was */
  .pop {
    position: absolute;
    z-index: 6;
    translate: -50% -50%;
    pointer-events: none;
    animation: popin 800ms var(--ease-out) forwards;
  }
  .stage[data-fx='normal'] .pop {
    filter: drop-shadow(0 0 24px currentColor);
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
    32% {
      scale: 0.95;
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
  .pill {
    display: flex;
    align-items: center;
    gap: 6px;
    padding: 6px 12px;
    border-radius: 999px;
    border: 1px solid rgba(255, 255, 255, 0.12);
    background: rgba(20, 22, 26, 0.55);
    backdrop-filter: blur(12px);
    cursor: pointer;
    font-size: 13px;
    animation: pop 220ms var(--ease-out);
  }
  .pill .ic {
    display: flex;
  }
  .pill .txt {
    max-width: 180px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .pill.active {
    border-color: color-mix(in srgb, var(--accent) 55%, transparent);
    color: #dfe5ff;
  }
  .menu-wrap {
    position: relative;
  }
  .scrim {
    position: fixed;
    inset: 0;
    z-index: 20;
  }
  .menu {
    position: absolute;
    top: calc(100% + 8px);
    left: 50%;
    translate: -50% 0;
    z-index: 21;
    min-width: 250px;
    padding: 6px;
    border-radius: 16px;
    background: var(--menu-bg);
    backdrop-filter: blur(16px);
    border: 1px solid rgba(255, 255, 255, 0.1);
    box-shadow: 0 18px 50px rgba(0, 0, 0, 0.5);
    animation: drop 180ms var(--ease-out);
  }
  @keyframes drop {
    from {
      opacity: 0;
      translate: -50% -6px;
    }
  }
  .menu button {
    width: 100%;
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 9px 12px;
    border: none;
    border-radius: 10px;
    background: transparent;
    font-size: 13.5px;
    text-align: left;
    white-space: nowrap;
    cursor: pointer;
  }
  .menu button:hover {
    background: rgba(255, 255, 255, 0.07);
  }
  .menu .ic {
    width: 16px;
    display: flex;
    color: var(--accent-ink);
  }
  .pill .arrow {
    color: var(--muted);
  }
  .pill .pts {
    color: var(--text);
    font-weight: 600;
  }
  .redo {
    display: flex;
    color: var(--super);
  }
  .flick-badge {
    display: flex;
    align-items: center;
    gap: 5px;
    padding: 4px 10px;
    border-radius: 999px;
    font-size: 12px;
    color: var(--super);
    background: rgba(20, 22, 26, 0.6);
    border: 1px solid rgba(244, 183, 58, 0.4);
    animation: pop 220ms var(--ease-out);
  }
  .flicking .top {
    cursor: none;
  }
  .fx {
    position: absolute;
    right: 14px;
    top: 12px;
    width: 30px;
    height: 30px;
    border-radius: 50%;
    border: none;
    display: grid;
    place-items: center;
    background: transparent;
    color: rgba(255, 255, 255, 0.6);
    cursor: pointer;
  }
  .fx.dim {
    color: rgba(255, 255, 255, 0.25);
  }
  @media (min-width: 760px) {
    :global([data-theme='light']) .fx {
      color: var(--muted);
    }
  }
  /* 演出: 標準 = light behind the card, glow on the card and the badge; 控えめ = dim light, no
     glow; なし = a plain background and no light or glow at all */
  /* a thrown card: light sweeps in from the side it left by */
  .sweep {
    position: absolute;
    inset: 0;
    z-index: 5;
    pointer-events: none;
    animation: sweepin 700ms ease-out forwards;
  }
  .sweep.like {
    background: linear-gradient(to left, color-mix(in srgb, var(--c) 50%, transparent), transparent 42%);
  }
  .sweep.pass {
    background: linear-gradient(to right, color-mix(in srgb, var(--c) 50%, transparent), transparent 42%);
  }
  .sweep.super {
    background: linear-gradient(to bottom, color-mix(in srgb, var(--c) 50%, transparent), transparent 40%);
  }
  .sweep.archive {
    background: linear-gradient(to top, color-mix(in srgb, var(--c) 45%, transparent), transparent 40%);
  }
  .stage[data-fx='calm'] .sweep {
    opacity: 0.5;
  }
  @keyframes sweepin {
    0% {
      opacity: 0;
    }
    25% {
      opacity: 1;
    }
    100% {
      opacity: 0;
    }
  }
  /* scroll model, a few quiet seconds: the picture slowly comes closer (from its middle); a touch
     brings it back in a moment */
  .card.slide.cur {
    transition: scale 1.2s var(--ease-out);
  }
  .stage.idle .card.slide.cur {
    scale: 1.1;
    transition: scale 45s cubic-bezier(0.25, 0.1, 0.25, 1);
  }
  .stage[data-fx='off'].idle .card.slide.cur {
    scale: 1;
  }
  /* the card layout: the picture's layers come closer inside the card, from its middle */
  .layers {
    position: absolute;
    inset: 0;
    pointer-events: none;
    transition: scale 1.2s var(--ease-out);
  }
  .stage.idle .card.top .layers {
    scale: 1.1;
    transition: scale 45s cubic-bezier(0.25, 0.1, 0.25, 1);
  }
  .stage[data-fx='off'].idle .card.top .layers {
    scale: 1;
  }
  /* a pinch takes the picture as it is, without the way back still under way */
  .card:global(.zooming) .layers {
    transition: none;
  }
  /* reduced motion makes transitions instant: no jump closer at all */
  @media (prefers-reduced-motion: reduce) {
    .stage.idle .card.slide.cur,
    .stage.idle .card.top .layers {
      scale: 1;
    }
  }
  /* hidden with the eye: the range and target (the eye shows them again) */
  .veiled {
    opacity: 0;
    pointer-events: none;
  }
  /* a re-rating with its points: in the top row on a PC; on a phone just below it, in the middle,
     where it never covers the range and target buttons */
  @media (max-width: 759px) {
    .pill.history {
      position: absolute;
      left: 50%;
      top: calc(100% - 2px);
      translate: -50% 0;
    }
  }
  .stage[data-fx='calm'] .ambient {
    opacity: 0.35;
  }
  .stage[data-fx='off'] .ambient {
    display: none;
  }
  .stage[data-fx='calm'] :global(.rim),
  .stage[data-fx='off'] :global(.rim),
  .stage[data-fx='off'] :global(.glare) {
    display: none;
  }
  .stage[data-fx='normal'] :global(.badge .b) {
    filter: drop-shadow(0 0 22px currentColor) drop-shadow(0 0 6px currentColor);
  }
  .stage[data-fx='off'] :global(.badge .b) {
    filter: none;
  }
  @keyframes pop {
    from {
      transform: scale(0.6);
      opacity: 0;
    }
  }

  .deck {
    position: relative;
    z-index: 1;
    flex: 1;
    margin: 4px clamp(10px, 4vw, 48px) clamp(12px, 3vh, 28px);
  }
  .card {
    position: absolute;
    inset: 0;
    isolation: isolate;
    border-radius: var(--radius-card);
    overflow: hidden;
    background: #101114;
    box-shadow:
      0 2px 6px rgba(0, 0, 0, 0.35),
      0 24px 60px rgba(0, 0, 0, 0.45);
  }
  .card img {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    pointer-events: none;
  }
  .fill {
    object-fit: cover;
    filter: blur(13px) saturate(1.2);
    transform: scale(1.15);
    opacity: 0.85;
  }
  .lqip,
  .photo {
    object-fit: contain;
  }
  .lqip {
    filter: blur(6px);
  }
  .photo {
    opacity: 0;
    transition: opacity 160ms linear;
  }
  .photo.ready {
    opacity: 1;
  }

  .behind {
    transform: translateY(calc(var(--i) * 14px)) scale(calc(1 - var(--i) * 0.05));
    filter: brightness(calc(0.75 - var(--i) * 0.15));
    transition: transform 180ms var(--ease-out);
  }
  .behind .fill {
    filter: none;
    transform: none;
    opacity: 1;
  }

  .top {
    touch-action: none;
    cursor: grab;
    will-change: transform;
    animation: rise 300ms var(--ease-out);
  }
  .top:global(.held) {
    cursor: grabbing;
    box-shadow:
      0 6px 16px rgba(0, 0, 0, 0.35),
      0 40px 90px rgba(0, 0, 0, 0.55);
  }
  /* scale/translate, not transform: a drag that starts during the rise writes transform at once */
  @keyframes rise {
    from {
      translate: 0 10px;
      scale: 0.96;
      opacity: 0.55;
    }
  }
  .ghost {
    z-index: 3;
    pointer-events: none;
    will-change: transform, opacity;
  }

  .glare {
    position: absolute;
    inset: 0;
    background: radial-gradient(circle at var(--gx, 50%) var(--gy, 30%), rgba(255, 255, 255, 0.16), transparent 45%);
    mix-blend-mode: soft-light;
    opacity: calc(0.4 + var(--amt) * 0.6);
    pointer-events: none;
    z-index: 1;
  }
  .rim {
    position: absolute;
    inset: 0;
    border-radius: inherit;
    box-shadow: inset 0 0 0 3px var(--lean-color, transparent), inset 0 0 40px var(--lean-color, transparent);
    opacity: var(--amt);
    pointer-events: none;
    z-index: 1;
  }
  .badge {
    position: absolute;
    inset: 0;
    display: grid;
    place-items: center;
    pointer-events: none;
    z-index: 1;
  }
  .badge .b {
    grid-area: 1 / 1;
    opacity: 0;
    transform: scale(0.6);
    filter: drop-shadow(0 6px 24px rgba(0, 0, 0, 0.5));
  }
  .stage:global([data-lean='like']) .badge .like,
  .stage:global([data-lean='pass']) .badge .pass,
  .stage:global([data-lean='super']) .badge .super,
  .stage:global([data-lean='archive']) .badge .archive {
    opacity: var(--amt);
    transform: scale(calc(0.6 + var(--amt) * 0.5));
  }

  .meta {
    position: absolute;
    left: 22px;
    right: 22px;
    bottom: 146px; /* above the zigzag of buttons (its high ones reach about 134 px) */
    transition:
      bottom 200ms var(--ease-out),
      opacity 400ms;
    text-shadow: 0 1px 12px rgba(0, 0, 0, 0.8);
    pointer-events: none;
    z-index: 1;
  }
  .meta.low {
    bottom: 40px;
  }
  .place {
    font-size: 20px;
    font-weight: 800;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .name {
    font-size: 13px;
    color: rgba(255, 255, 255, 0.75);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .tags {
    display: flex;
    gap: 6px;
    margin-top: 6px;
  }
  .tags span {
    padding: 2px 9px;
    border-radius: 999px;
    background: rgba(255, 255, 255, 0.14);
    font-size: 12px;
  }
  .card.top::after {
    content: '';
    position: absolute;
    left: 0;
    right: 0;
    bottom: 0;
    height: 230px;
    background: linear-gradient(transparent, rgba(0, 0, 0, 0.75));
    pointer-events: none;
    z-index: 0;
  }

  .buttons {
    position: absolute;
    left: 0;
    right: 0;
    bottom: 36px;
    display: flex;
    justify-content: center;
    align-items: center;
    gap: clamp(14px, 4vw, 34px);
    z-index: 2;
    transition:
      transform 200ms var(--ease-out),
      opacity 200ms linear;
  }
  .buttons.hidden {
    transform: translateY(120px);
    opacity: 0;
    pointer-events: none;
  }
  /* a zigzag, as the owner drew it: ↩ low, ✕ high, ★ low, ♥ high (the mark row behind follows) */
  .buttons > .rb:nth-child(2),
  .buttons > .rb:nth-child(4) {
    translate: 0 -36px;
  }
  .rb {
    width: 62px;
    height: 62px;
    border-radius: 50%;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.35);
    border: 2px solid currentColor;
    backdrop-filter: blur(8px);
    cursor: pointer;
    transition: transform 120ms var(--ease-out);
  }
  .rb:active {
    transform: scale(0.9);
  }
  .rb.small {
    width: 52px;
    height: 52px;
    color: rgba(255, 255, 255, 0.75);
  }
  .rb.small:disabled {
    opacity: 0.3;
    cursor: default;
  }
  .rb.mid {
    width: 54px;
    height: 54px;
  }
  .rb.pass {
    color: var(--pass);
  }
  .rb.super {
    color: var(--super);
  }
  .rb.like {
    color: var(--like);
  }
  .stage:global([data-lean='like']) .rb.like,
  .stage:global([data-lean='pass']) .rb.pass,
  .stage:global([data-lean='super']) .rb.super {
    transform: scale(calc(1 + var(--amt) * 0.25));
    background: color-mix(in srgb, currentColor calc(var(--amt) * 35%), rgba(0, 0, 0, 0.35));
  }
  .rb.like {
    --c: var(--like);
  }
  .rb.super {
    --c: var(--super);
  }
  .rb.pass {
    --c: var(--pass);
  }
  .rb.small {
    --c: #8c919b;
  }
  /* ライン: a thin white ring, see-through, a white sign; the colour comes the moment it is pressed */
  .buttons[data-style='line'] .rb {
    border: 1.5px solid rgba(255, 255, 255, 0.92);
    color: #fff;
    background: rgba(0, 0, 0, 0.1);
    backdrop-filter: none;
    box-shadow:
      0 0 0 0.5px rgba(0, 0, 0, 0.25),
      inset 0 0 0 0.5px rgba(0, 0, 0, 0.2);
    filter: drop-shadow(0 1px 2px rgba(0, 0, 0, 0.45));
    transition:
      transform 120ms var(--ease-out),
      background-color 90ms linear,
      border-color 90ms linear;
  }
  .buttons[data-style='line'] .rb:not(.small):active,
  .buttons[data-style='line'][data-lit='like'] .rb.like,
  .buttons[data-style='line'][data-lit='pass'] .rb.pass,
  .buttons[data-style='line'][data-lit='super'] .rb.super,
  .buttons[data-style='line'] .rb.marked {
    background: var(--c);
    border-color: var(--c);
    transition: none;
  }
  .buttons[data-style='line'] .rb.small:active {
    background: rgba(255, 255, 255, 0.28);
  }
  .stage:global([data-lean='like']) .buttons[data-style='line'] .rb.like,
  .stage:global([data-lean='pass']) .buttons[data-style='line'] .rb.pass,
  .stage:global([data-lean='super']) .buttons[data-style='line'] .rb.super {
    background: color-mix(in srgb, var(--c) calc(var(--amt) * 100%), rgba(0, 0, 0, 0.1));
    border-color: color-mix(in srgb, var(--c) calc(var(--amt) * 100%), rgba(255, 255, 255, 0.92));
  }
  /* グラス: frosted glass with the sign in its colour */
  .buttons[data-style='glass'] .rb {
    border: 1px solid rgba(255, 255, 255, 0.35);
    color: var(--c);
    background:
      linear-gradient(180deg, rgba(255, 255, 255, 0.28), rgba(255, 255, 255, 0.06) 55%),
      rgba(255, 255, 255, 0.08);
    backdrop-filter: blur(16px) saturate(1.4);
    box-shadow:
      inset 0 1px 0 rgba(255, 255, 255, 0.45),
      0 8px 22px rgba(0, 0, 0, 0.3);
  }
  .buttons[data-style='glass'] .rb.small {
    color: rgba(255, 255, 255, 0.85);
  }
  .miniback {
    position: absolute;
    left: calc(50% - 60px);
    bottom: 32px;
    width: 44px;
    height: 44px;
    border-radius: 50%;
    border: none;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.35);
    color: rgba(255, 255, 255, 0.8);
    backdrop-filter: blur(8px);
    cursor: pointer;
    z-index: 7;
    animation: rise 300ms var(--ease-out);
  }
  .miniback:disabled {
    opacity: 0.3;
  }
  .chev {
    position: absolute;
    left: 50%;
    bottom: 8px;
    transform: translateX(-50%);
    width: 28px;
    height: 24px;
    border-radius: 12px;
    border: none;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.35);
    color: rgba(255, 255, 255, 0.75);
    cursor: pointer;
    z-index: 2;
  }
  .loading {
    width: 34px;
    height: 34px;
    border-radius: 50%;
    border: 3px solid rgba(255, 255, 255, 0.12);
    border-top-color: var(--like);
    animation: turn 800ms linear infinite;
  }
  @keyframes turn {
    to {
      rotate: 360deg;
    }
  }
  .done {
    position: absolute;
    inset: 0;
    display: grid;
    place-content: center;
    justify-items: center;
    gap: 10px;
    color: var(--muted);
  }
  /* short screens (small phones, landscape): smaller buttons, a flatter zigzag, the name lower */
  @media (max-height: 680px) {
    .rb {
      width: 52px;
      height: 52px;
    }
    .rb.mid {
      width: 46px;
      height: 46px;
    }
    .rb.small {
      width: 44px;
      height: 44px;
    }
    .buttons > .rb:nth-child(2),
    .buttons > .rb:nth-child(4) {
      translate: 0 -24px;
    }
    .meta {
      bottom: 118px;
    }
  }
</style>
