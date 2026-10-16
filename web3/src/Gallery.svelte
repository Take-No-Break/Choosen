<script lang="ts">
  import { t } from './lib/i18n';
  // The gallery: images of one kind as a grid, or one tile per folder (フォルダ別); a tile opens the
  // full view. Pages of 120 load as the grid scrolls; tiles off screen skip layout and paint.
  // A range (範囲), a sort order and ♥ / ★ switches; two fingers (or Ctrl + wheel) set the tile size,
  // kept per device (the tiles glide to their new places); ▶ plays the pictures story style,
  // スライド shows them sideways by hand (Story.svelte). The round button at the bottom left shows
  // the pictures in their own shapes (rows of one height; shapes beyond 9:16 or 16:9 are cut a
  // little), also kept per device.
  import { flushSync } from 'svelte';
  import { getJ, postJ, thumbUrl, placeOf, fmtPts, IOS, PHONE_APP, type Card, type State } from './lib/api';
  import { svg } from './ui/icons';
  import { squareGrid, shapesGrid, type Grid } from './lib/vgrid';
  import { galleryMinTile, galleryTilePx, gallerySizeOf, GALLERY_MAX_TILE, PHONE_GALLERY_COLUMNS, SQUARE_TILE_SCALE } from './lib/gallery-size';
  import { inBand, meets, nearest, keepAt, invert, still } from './lib/flip';
  import Viewer from './Viewer.svelte';
  import ScopePicker from './ScopePicker.svelte';
  import Story from './Story.svelte';
  import SortSheet from './SortSheet.svelte';
  import { longpress } from './lib/longpress';

  let {
    st,
    active,
    request = null,
  }: {
    st: State;
    active: boolean;
    request?: { path: string; name: string; n: number } | null;
  } = $props();

  interface FolderCard {
    path: string;
    name: string;
    depth: number;
    rank: number;
    count: number;
    cover: { id: number; fp: string };
  }

  // the likes are the ♥ and ★ switches at the top right (both = 好評 and 超好評); these are the rest,
  // in the ☰ menu at the top left (with フォルダ別), next to the range button (フォルダ＋)
  const FILTERS: [string, string][] = [
    ['picks', t('サブギャラリー')],
    ['passed', t('見送り')],
    ['unrated', t('未評価')],
    ['all', t('すべて')],
    ['archived', t('アーカイブ')],
  ];

  const SORTS: [string, string][] = [
    ['score', t('画像スコア順')],
    ['folder', t('フォルダ順位順')],
    ['recent', t('最近評価した順')],
    ['name', t('名前順')],
    ['random', t('ランダム')],
  ];
  // the tile size: 0 = small (many per row) ... 100 = large
  let size = $state(readSize());
  function readSize() {
    try {
      const v = Number(localStorage.getItem('choosen3.tileSize'));
      return Number.isFinite(v) && localStorage.getItem('choosen3.tileSize') !== null ? Math.min(100, Math.max(0, v)) : 40;
    } catch {
      return 40;
    }
  }
  $effect(() => {
    try {
      localStorage.setItem('choosen3.tileSize', String(size));
    } catch {
      /* private mode */
    }
  });
  const GAP = 3;
  let gridW = $state(0);
  const tileMin = $derived(galleryMinTile(gridW, PHONE_APP, GAP));
  const tilePx = $derived(galleryTilePx(size, tileMin));
  // a gallery so wide that its smallest tile is already the largest: the saved size stays as it is
  const sizeOf = (px: number) => (tileMin >= GALLERY_MAX_TILE ? size : gallerySizeOf(px, tileMin));
  const maxColumns = PHONE_APP ? PHONE_GALLERY_COLUMNS : Infinity;

  // A new tile size without a jump (FLIP, lib/flip.ts): each tile on screen glides from where it
  // was drawn to its new place, the picture at (x, y) stays at that height (the scroll follows it),
  // and tiles that only now come into view fade in. Only the tiles on screen move (a binary search
  // finds them), and a size changed again mid-way starts from where the tiles are drawn then.
  const FLIP_MS = 300;
  const FLIP_MAX = 500; // more tiles than this on screen (tiny tiles on a big monitor): no animation
  let flips: Animation[] = [];
  const calm = () => typeof matchMedia === 'function' && matchMedia('(prefers-reduced-motion: reduce)').matches;
  function resize(scroller: HTMLElement, next: number, x: number, y: number, scaled: HTMLElement | null = null) {
    const grid = scroller.querySelector<HTMLElement>('.grid');
    if (!grid || (next === size && !scaled)) {
      size = next;
      return;
    }
    const tiles = Array.from(grid.children) as HTMLElement[];
    const box = (i: number) => tiles[i].getBoundingClientRect();
    const view = scroller.getBoundingClientRect();
    // First: where each tile is drawn now; a tile still gliding is caught where it is
    const was = new Map<HTMLElement, { r: DOMRect; o: number }>();
    for (const a of flips) {
      const t = (a.effect as KeyframeEffect | null)?.target as HTMLElement | null;
      if (t?.isConnected && !was.has(t)) was.set(t, { r: t.getBoundingClientRect(), o: Number(getComputedStyle(t).opacity) });
    }
    for (const a of flips) a.cancel();
    flips = [];
    const before = inBand(tiles.length, box, view.top, view.bottom);
    if (before) for (let i = before[0]; i <= before[1]; i++) if (!was.has(tiles[i])) was.set(tiles[i], { r: box(i), o: 1 });
    const shown = [...was].filter(([, w]) => meets(w.r, view));
    const anchor = shown[nearest(shown.map(([, w]) => w.r), x, y)];
    const anchorAt = anchor ? Number(anchor[0].dataset.i) : NaN;
    // Last: the new size, laid out at once
    size = next;
    if (scaled) scaled.style.transform = scaled.style.transformOrigin = '';
    flushSync();
    if (anchor && geo && Number.isFinite(anchorAt) && anchorAt < geo.count) {
      // the whole list's grid: the anchor's new place comes from the numbers, then the tiles
      // around the new scroll position are drawn
      const g = grid.getBoundingClientRect();
      const p = geo.at(anchorAt);
      scroller.scrollTop += keepAt(anchor[1].r, { left: g.left + p.x, top: g.top + p.y, width: p.w, height: p.h }, y);
      scrollY = scroller.scrollTop;
      flushSync();
    } else if (anchor?.[0].isConnected) scroller.scrollTop += keepAt(anchor[1].r, anchor[0].getBoundingClientRect(), y);
    if (calm()) return;
    const now = Array.from(grid.children) as HTMLElement[];
    const after = inBand(now.length, (i) => now[i].getBoundingClientRect(), view.top, view.bottom);
    const moving = new Set(shown.map(([t]) => t).filter((t) => t.isConnected));
    if (after) for (let i = after[0]; i <= after[1]; i++) moving.add(now[i]);
    if (moving.size > FLIP_MAX) return;
    // Invert and play: from where it was drawn (or faded out) to its place
    const timing: KeyframeAnimationOptions = { duration: FLIP_MS, easing: 'cubic-bezier(0.2, 0.8, 0.2, 1)' };
    for (const t of moving) {
      const w = was.get(t);
      const to = t.getBoundingClientRect();
      if (!w) flips.push(t.animate([{ opacity: 0 }, { opacity: 1 }], timing));
      else if (!still(w.r, to) || w.o < 1)
        flips.push(
          t.animate(
            [
              { transformOrigin: '0 0', transform: invert(w.r, to), opacity: w.o },
              { transformOrigin: '0 0', transform: 'none', opacity: 1 },
            ],
            timing,
          ),
        );
    }
  }

  // two fingers on the grid make the tiles bigger or smaller, as in a phone's photo app: the grid
  // follows the fingers with a scale, and the tiles glide into the new columns when they lift
  let pinch: { d0: number; px0: number; grid: HTMLElement; k: number; x: number; y: number } | null = null;
  const gap = (t: TouchList) => Math.hypot(t[0].clientX - t[1].clientX, t[0].clientY - t[1].clientY);
  const mid = (t: TouchList): [number, number] => [(t[0].clientX + t[1].clientX) / 2, (t[0].clientY + t[1].clientY) / 2];
  function zoomTiles(node: HTMLElement) {
    const start = (e: TouchEvent) => {
      if (e.touches.length !== 2) return;
      const grid = node.querySelector<HTMLElement>('.grid');
      if (!grid) return;
      const r = grid.getBoundingClientRect();
      const [x, y] = mid(e.touches);
      grid.style.transformOrigin = `${x - r.left}px ${y - r.top}px`;
      grid.style.transition = 'none';
      pinch = { d0: gap(e.touches), px0: tilePx, grid, k: 1, x, y };
    };
    const move = (e: TouchEvent) => {
      if (!pinch || e.touches.length !== 2) return;
      e.preventDefault(); // not a scroll
      // follow the fingers smoothly (only a scale), within the sizes the tiles can take
      pinch.k = Math.min(GALLERY_MAX_TILE / pinch.px0, Math.max(tileMin / pinch.px0, gap(e.touches) / Math.max(1, pinch.d0)));
      [pinch.x, pinch.y] = mid(e.touches);
      pinch.grid.style.transform = `scale(${pinch.k})`;
    };
    const end = (e: TouchEvent) => {
      if (!pinch || e.touches.length >= 2) return;
      const p = pinch;
      pinch = null;
      resize(node, sizeOf(p.px0 * p.k), p.x, p.y, p.grid); // the real size (columns settle)
    };
    // Ctrl + wheel (and a touchpad's pinch, which arrives as one): the steps of one frame add up
    let wheelK = 1;
    let wheelAt = [0, 0];
    let frame = 0;
    const wheel = (e: WheelEvent) => {
      if (!e.ctrlKey) return;
      e.preventDefault();
      if (!e.deltaY) return;
      wheelK *= e.deltaY < 0 ? 1.1 : 1 / 1.1;
      wheelAt = [e.clientX, e.clientY];
      frame ||= requestAnimationFrame(() => {
        frame = 0;
        const k = wheelK;
        wheelK = 1;
        if (!pinch) resize(node, sizeOf(tilePx * k), wheelAt[0], wheelAt[1]);
      });
    };
    node.addEventListener('touchstart', start, { passive: true });
    node.addEventListener('touchmove', move, { passive: false });
    node.addEventListener('touchend', end);
    node.addEventListener('touchcancel', end);
    node.addEventListener('wheel', wheel, { passive: false });
    return {
      destroy() {
        node.removeEventListener('touchstart', start);
        node.removeEventListener('touchmove', move);
        node.removeEventListener('touchend', end);
        node.removeEventListener('touchcancel', end);
        node.removeEventListener('wheel', wheel);
        cancelAnimationFrame(frame);
        for (const a of flips) a.cancel();
        flips = [];
      },
    };
  }
  let playing: 'play' | 'browse' | null = $state(null);

  // ---- the pictures' own shapes: measured from their thumbnails (which keep the shape), kept
  // within 9:16 .. 16:9
  let shapes = $state(readShapes());
  function readShapes() {
    try {
      return localStorage.getItem('choosen3.shapes') === '1';
    } catch {
      return false;
    }
  }
  function toggleShapes() {
    shapes = !shapes;
    try {
      localStorage.setItem('choosen3.shapes', shapes ? '1' : '0');
    } catch {
      /* private mode */
    }
  }
  const ars = new Map<number, number>();
  let ratioTick = $state(0); // the shapes layout follows new measurements once a frame
  let tickFrame = 0;
  const clampAr = (r: number) => Math.min(16 / 9, Math.max(9 / 16, r));
  function measure(c: Card, e: Event) {
    const im = e.currentTarget as HTMLImageElement;
    if (!im.naturalWidth || !im.naturalHeight || ars.has(c.id)) return;
    ars.set(c.id, clampAr(im.naturalWidth / im.naturalHeight));
    if (shapes) tickFrame ||= requestAnimationFrame(() => ((tickFrame = 0), ratioTick++));
  }
  // rows of about the tile size high: pictures join a row until it is full, then the row is sized
  // to fill the width exactly; the last row keeps the plain height (a picture not measured yet
  // counts as a square, and only rows from it on move when it is)
  let filter = $state('liked');
  let sort = $state('score');
  let seed = $state(Math.floor(Math.random() * 1e9));
  let range: string[] = $state.raw([]);
  let rangeLabel = $state(t('すべて'));
  let menu: 'sort' | 'kind' | null = $state(null);
  let picking = $state(false);
  let byFolder = $state(false);
  let scope: { path: string; name: string } | null = $state(null); // a folder opened from フォルダ別
  // The whole list at once: one slot per picture (empty until its page is loaded), so the scroll is
  // as long as the list from the start; pages of 120 load wherever the scroll stops, and only the
  // tiles near the screen are drawn (lib/vgrid.ts).
  const PAGE = 120;
  let items: (Card | undefined)[] = $state.raw([]);
  let folders: FolderCard[] = $state.raw([]);
  let total = $state(0);
  let loading = $state(false);
  let req = 0;
  let pages = new Set<number>(); // pages of this list loaded or on their way
  let viewing: number | null = $state(null);
  let shownVersion = -1;
  let scrollerEl: HTMLElement | undefined = $state();
  let scrollY = $state(0);
  let viewH = $state(0);

  const folderView = $derived(byFolder && !scope && filter !== 'archived' && filter !== 'picks');
  // where every tile goes (square, or the pictures' own shapes; a picture not measured yet is a square)
  const geo: Grid | null = $derived.by(() => {
    if (folderView || gridW <= 0) return null;
    if (!shapes) return squareGrid(items.length, gridW, Math.round(tilePx * SQUARE_TILE_SCALE), GAP, maxColumns);
    void ratioTick;
    const r = new Array<number>(items.length);
    for (let i = 0; i < items.length; i++) r[i] = ars.get(items[i]?.id ?? -1) ?? 1;
    return shapesGrid(r, gridW, tilePx * 0.8, GAP, maxColumns);
  });
  // the tiles drawn: the screen and one screen above and below it
  const drawn = $derived.by(() => {
    const b = geo?.band(scrollY - 2 - viewH, scrollY - 2 + 2 * viewH);
    if (!b) return [];
    const out: number[] = [];
    for (let i = b[0]; i <= b[1]; i++) out.push(i);
    return out;
  });
  // the pages under the screen load as soon as the scroll rests a moment
  let needTimer = 0;
  $effect(() => {
    const b = geo?.band(scrollY - 2, scrollY - 2 + Math.max(1, viewH));
    if (!b) return;
    clearTimeout(needTimer);
    needTimer = window.setTimeout(() => need(b[0], b[1]), 80);
    return () => clearTimeout(needTimer);
  });

  async function reload() {
    req++;
    items = [];
    folders = [];
    total = 0;
    pages = new Set();
    shownVersion = st.version;
    if (scrollerEl) scrollerEl.scrollTop = 0;
    scrollY = 0;
    if (folderView) await loadFolders();
    else {
      loading = true;
      await loadPage(0);
      loading = false;
    }
  }

  /** The pages around pictures first..last (a little ahead of them). */
  function need(first: number, last = first) {
    if (filter === 'picks') return void loadPage(0);
    const hi = Math.min(Math.max(0, total - 1), last + 40);
    for (let p = Math.floor(Math.max(0, first - 20) / PAGE); p <= Math.floor(hi / PAGE); p++) void loadPage(p);
  }

  async function loadPage(p: number) {
    if (pages.has(p)) return;
    pages.add(p);
    const my = req;
    try {
      if (filter === 'picks') {
        // the sub gallery: the pictures liked while playing, newest first, all at once
        const r = await getJ<{ items: Card[] }>('/api/picks');
        if (my !== req) return;
        total = r.items.length;
        items = r.items;
        return;
      }
      const r = await getJ<{ total: number; items: Card[] }>('/api/items', {
        filter,
        sort,
        seed,
        offset: p * PAGE,
        limit: PAGE,
        scope: scope ? scope.path : range.join('|'),
      });
      if (my !== req) return;
      const next = items.slice(0, r.total);
      next.length = r.total;
      r.items.forEach((c, k) => (next[p * PAGE + k] = c));
      total = r.total;
      items = next;
    } catch {
      if (my === req) pages.delete(p); // not ready yet: asked again when the scroll moves
    }
  }

  async function loadFolders() {
    const my = req;
    try {
      const r = await getJ<{ folders: FolderCard[] }>('/api/folderCards', { filter, sort: 'rank', scope: range.join('|') });
      if (my === req) folders = r.folders;
    } catch {
      /* not ready yet */
    }
  }

  // a folder opened from the ranking: all its images
  let handled = 0;
  $effect(() => {
    if (request && request.n !== handled) {
      handled = request.n;
      filter = 'all';
      scope = { path: request.path, name: request.name };
      reload();
    }
  });

  // reload when shown after ratings changed (cheap: one page)
  $effect(() => {
    if (active && st.version !== shownVersion && viewing === null) reload();
  });

  function choose(f: string) {
    filter = f;
    scope = null;
    reload();
  }

  // ♥ = 好評, ★ = 超好評, both = both; turning the last one off shows everything. In a folder
  // opened from フォルダ別 or the ranking they filter that folder.
  // the ☰ button names the kind shown unless it is the ♥ / ★ switches'
  const kindLabel = $derived(FILTERS.find(([f]) => f === filter && f !== 'all')?.[1] ?? '');
  const heartOn = $derived(filter === 'liked' || filter === 'likeOnly');
  const starOn = $derived(filter === 'liked' || filter === 'super');
  function toggleKind(heart: boolean) {
    const h = heart ? !heartOn : heartOn;
    const s = heart ? starOn : !starOn;
    filter = h && s ? 'liked' : h ? 'likeOnly' : s ? 'super' : 'all';
    reload();
  }

  function setRange(tokens: string[], label: string) {
    range = tokens;
    rangeLabel = label;
    picking = false;
    scope = null;
    reload();
  }
  function setSort(s: string) {
    sort = s;
    if (s === 'random') seed = Math.floor(Math.random() * 1e9);
    menu = null;
    reload();
  }

  let resetAsk: { images: number; ratings: number } | null = $state(null);
  let resetBusy = $state(false);
  async function askReset() {
    if (!scope) return;
    resetAsk = await getJ<{ images: number; ratings: number }>('/api/reset', { scope: scope.path }).catch(() => null);
  }
  async function doReset() {
    if (!scope) return;
    resetBusy = true;
    await postJ('/api/reset', { scope: scope.path }).catch(() => null);
    resetBusy = false;
    resetAsk = null;
    reload();
  }

  function openFolder(f: FolderCard) {
    scope = { path: f.depth === 0 ? (f.path ? f.path + '/' : '/') : f.path, name: f.name };
    reload();
  }

  function onScroll(e: Event) {
    scrollY = (e.currentTarget as HTMLElement).scrollTop;
  }

  // ---- selecting pictures: a long press starts it, then a tap adds or removes one. The chosen ones
  // go into the サブギャラリー or into subfolders by their verdict (SortSheet).
  let picked = $state(new Map<number, Card>());
  let sorting = $state(false);
  let selMsg = $state('');
  function pickCard(c: Card) {
    const next = new Map(picked);
    if (next.has(c.id)) next.delete(c.id);
    else next.set(c.id, c);
    picked = next;
  }
  const clearPicked = () => ((picked = new Map()), (selMsg = ''));

  // A long press selects a picture; keeping the finger down and moving it then selects every picture
  // between that one and the one under the finger (as in phone photo apps). Near the top or bottom
  // edge the grid scrolls by itself; the finger does not scroll it meanwhile.
  let finger = { id: -1, x: 0, y: 0 };
  let sweep: { anchor: number; base: Map<number, Card>; last: number } | null = null;
  let sweepFrame = 0;
  function startSweep(i: number, c: Card) {
    if (!picked.has(c.id)) pickCard(c);
    sweep = { anchor: i, base: new Map(picked), last: i };
    sweepFrame ||= requestAnimationFrame(sweepStep);
  }
  function sweepTo(x: number, y: number) {
    if (!sweep) return;
    const at = Number((document.elementFromPoint(x, y) as Element | null)?.closest<HTMLElement>('[data-i]')?.dataset.i);
    if (!Number.isFinite(at) || at === sweep.last) return;
    sweep.last = at;
    const next = new Map(sweep.base);
    for (let k = Math.min(at, sweep.anchor); k <= Math.max(at, sweep.anchor); k++) {
      const c = items[k];
      if (c) next.set(c.id, c);
    }
    picked = next;
  }
  function sweepStep() {
    sweepFrame = 0;
    if (!sweep || !scrollerEl) return;
    const r = scrollerEl.getBoundingClientRect();
    const edge = 72;
    const v = finger.y > r.bottom - edge ? (finger.y - (r.bottom - edge)) / edge : finger.y < r.top + edge ? -((r.top + edge - finger.y) / edge) : 0;
    if (v) {
      scrollerEl.scrollTop += Math.max(-1, Math.min(1, v)) * 16;
      sweepTo(finger.x, finger.y);
    }
    sweepFrame = requestAnimationFrame(sweepStep);
  }
  function onGridDown(e: PointerEvent) {
    finger = { id: e.pointerId, x: e.clientX, y: e.clientY };
  }
  function onGridMove(e: PointerEvent) {
    if (e.pointerId !== finger.id) return;
    finger.x = e.clientX;
    finger.y = e.clientY;
    sweepTo(e.clientX, e.clientY);
  }
  function onGridUp(e: PointerEvent) {
    if (e.pointerId !== finger.id) return;
    finger.id = -1;
    sweep = null;
    cancelAnimationFrame(sweepFrame);
    sweepFrame = 0;
  }
  // while sweeping, a finger moving on the grid selects instead of scrolling
  $effect(() => {
    const el = scrollerEl;
    if (!el) return;
    const hold = (e: TouchEvent) => sweep && e.cancelable && e.preventDefault();
    el.addEventListener('touchmove', hold, { passive: false });
    return () => el.removeEventListener('touchmove', hold);
  });
  async function addToPicks() {
    const cards = [...picked.values()];
    for (const c of cards) await postJ('/api/picks', { fp: c.fp, add: 1 }).catch(() => {});
    selMsg = t('{0} 枚をサブギャラリーに追加しました', cards.length);
    setTimeout(clearPicked, 1400);
  }
  // a work of several pages moves as a whole
  const pickedIds = $derived([...picked.values()].flatMap((c) => [c.id, ...(c.pages ?? []).map((p) => p.id)]));

  const likedBefore = (c: Card) => (c.nL ?? 0) + (c.nS ?? 0) > 0;
  const badge = (c: Card) => (c.last === 'S' ? 'star' : c.last !== 'D' ? 'heart' : likedBefore(c) ? 'heartHalf' : 'x');
  const color = (c: Card) => (c.last === 'S' ? 'var(--super)' : c.last !== 'D' || likedBefore(c) ? 'var(--like)' : 'var(--pass)');
</script>

<section class="gallery">
  <div class="bar">
    {#if scope}
      <button class="back" title={t('戻る')} onclick={() => ((scope = null), reload())}>{@html svg('undo', 18)}</button>
      <span class="title" title={scope.name}>{scope.name}</span>
      {#if !st.remote}<button class="tool" title={t('このフォルダの評価をリセット')} onclick={askReset}>{@html svg('reset', 18)}</button>{/if}
    {:else}
      <div class="lead">
        <button class="chip icon" class:on={range.length > 0} title={t('範囲（フォルダ）: {0}', rangeLabel)} onclick={() => (picking = true)}>
          {#if range.length}{@html svg('folder', 15)}<span class="lbl">{rangeLabel}</span>{:else}{@html svg('folderPlus', 18)}{/if}
        </button>
        <button class="chip icon" class:on={menu === 'kind' || !!kindLabel || folderView} title={t('種類')} onclick={() => (menu = menu === 'kind' ? null : 'kind')}>
          {@html svg('filter', 16)}{#if kindLabel}<span class="lbl">{kindLabel}</span>{/if}{#if folderView}<span class="ic">{@html svg('folder', 14)}</span>{/if}
        </button>
        {#if menu === 'kind'}
          <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
          <div class="scrim" onclick={() => (menu = null)}></div>
          <div class="menu left">
            {#each FILTERS as [f, label] (f)}
              <button class="item" class:on={filter === f} onclick={() => ((menu = null), choose(f))}>
                <span class="ck">{#if filter === f}{@html svg('check', 16)}{/if}</span>{label}
              </button>
            {/each}
            <div class="msep"></div>
            <button
              class="item"
              disabled={filter === 'archived' || filter === 'picks'}
              onclick={() => ((menu = null), (byFolder = !byFolder), reload())}
            >
              <span class="ck">{#if folderView}{@html svg('check', 16)}{/if}</span>{t('フォルダ別')}
            </button>
          </div>
        {/if}
      </div>
    {/if}
    <span class="count num">{folderView ? folders.length : total}</span>
    <div class="tools">
      {#if !folderView}
        <button class="tool" class:on={menu === 'sort'} title={t('並び順: {0}', SORTS.find(([s]) => s === sort)?.[1])} onclick={() => (menu = menu === 'sort' ? null : 'sort')}>{@html svg('sort', 18)}</button>
      {/if}
      <button class="tool kind like" class:on={heartOn} title={t('好評')} onclick={() => toggleKind(true)}>{@html svg('heart', 18)}</button>
      <button class="tool kind super" class:on={starOn} title={t('超好評')} onclick={() => toggleKind(false)}>{@html svg('star', 18)}</button>
      <button class="tool" title={t('スライド（横にめくる）')} disabled={folderView || !total} onclick={() => (playing = 'browse')}>{@html svg('slides', 18)}</button>
      <button class="tool" title={t('プレイ')} disabled={filter === 'archived'} onclick={() => (playing = 'play')}>{@html svg('play', 18)}</button>
      {#if menu === 'sort'}
        <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
        <div class="scrim" onclick={() => (menu = null)}></div>
        <div class="menu">
          {#if menu === 'sort'}
            {#each SORTS as [s, label] (s)}
              <button class="item" class:on={s === sort} onclick={() => setSort(s)}>
                <span class="ck">{#if s === sort}{@html svg('check', 16)}{/if}</span>{label}
              </button>
            {/each}
          {/if}
        </div>
      {/if}
    </div>
  </div>

  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    class="scroller"
    style:--tile={`${Math.round(tilePx * SQUARE_TILE_SCALE)}px`}
    bind:this={scrollerEl}
    bind:clientHeight={viewH}
    onscroll={onScroll}
    onpointerdown={onGridDown}
    onpointermove={onGridMove}
    onpointerup={onGridUp}
    onpointercancel={onGridUp}
    use:zoomTiles
  >
    {#if folderView}
      <div class="grid folders">
        {#each folders as f (f.path)}
          <button class="tile ftile" onclick={() => openFolder(f)}>
            <img src={thumbUrl(f.cover, 320)} alt="" loading="lazy" />
            <span class="fname">{f.name}</span>
            <span class="fcount num">{f.count}</span>
            {#if f.rank > 0 && f.rank <= 3}<span class="medal m{f.rank} num">{f.rank}</span>{/if}
          </button>
        {/each}
      </div>
    {:else}
      <div class="grid whole" bind:clientWidth={gridW} style:height={geo ? `${geo.height}px` : null}>
        {#if geo}
          {#each drawn as i (items[i]?.id ?? -1 - i)}
            {@const c = items[i]}
            {@const p = geo.at(i)}
            {#if c}
              <button
                class="tile"
                data-i={i}
                style:left={`${p.x}px`}
                style:top={`${p.y}px`}
                style:width={`${p.w}px`}
                style:height={`${p.h}px`}
                class:sel={picked.has(c.id)}
                use:longpress={{ onlong: () => startSweep(i, c) }}
                onclick={() => (picked.size ? pickCard(c) : (viewing = i))}
                title={placeOf(c.dir)}
              >
                <img src={thumbUrl(c, 320)} alt="" decoding="async" onload={(e) => measure(c, e)} />
                {#if c.rated}
                  <span class="pts num" style:color={color(c)}>{@html svg(badge(c), 12)}<b>{fmtPts(c.p, st.overPoints)}</b></span>
                {/if}
                {#if picked.size}<span class="tick" class:on={picked.has(c.id)}>{#if picked.has(c.id)}{@html svg('check', 14)}{/if}</span>{/if}
              </button>
            {:else}
              <div class="tile ghost" data-i={i} style:left={`${p.x}px`} style:top={`${p.y}px`} style:width={`${p.w}px`} style:height={`${p.h}px`}></div>
            {/if}
          {/each}
        {/if}
      </div>
      {#if !items.length && !loading}<div class="empty">{@html svg('heart', 36)}</div>{/if}
    {/if}
  </div>
  {#if picked.size}
    <div class="selbar">
      <button class="x" title={t('選択をやめる')} onclick={clearPicked}>{@html svg('x', 18)}</button>
      <span class="num" class:msg={!!selMsg}>{selMsg || t('{0} 枚', picked.size)}</span>
      <button class="act ghost" title={t('サブギャラリーに追加')} onclick={addToPicks}>{@html svg('bookmark', 16)}</button>
      {#if !st.remote && !IOS}<button class="act" onclick={() => (sorting = true)}>{@html svg('folderPlus', 17)}<span class="lbl">{t('サブフォルダに分ける§button')}</span></button>{/if}
    </div>
  {:else if !folderView}
    <button class="shapes-sw" class:on={shapes} title={shapes ? t('正方形で表示') : t('元の形で表示')} onclick={toggleShapes}>{@html svg('wavy', 20)}</button>
  {/if}
</section>

{#if resetAsk && scope}
  <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
  <div class="modal" onclick={() => (resetAsk = null)}>
    <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
    <div class="ask" onclick={(e) => e.stopPropagation()}>
      <b>{t('「{0}」の評価をリセット', scope.name)}</b>
      {#if resetAsk.images}
        <p>{t('{0} 枚（{1} 件の評価）が未評価に戻り、ランキングから外れます。アーカイブはそのままです。元に戻せません。', resetAsk.images, resetAsk.ratings)}</p>
        <div class="row">
          <button class="btn" onclick={() => (resetAsk = null)}>{t('やめる')}</button>
          <button class="btn danger" disabled={resetBusy} onclick={doReset}>{t('リセット')}</button>
        </div>
      {:else}
        <p>{t('評価された画像はありません。')}</p>
        <div class="row"><button class="btn" onclick={() => (resetAsk = null)}>{t('閉じる')}</button></div>
      {/if}
    </div>
  </div>
{/if}

{#if playing}
  <Story
    {filter}
    scope={scope ? scope.path : range.join('|')}
    browse={playing === 'browse'}
    {sort}
    {seed}
    onclose={() => (playing = null)}
  />
{/if}

{#if sorting}
  <SortSheet ids={pickedIds} label={t('{0} 枚の写真', picked.size)} onclose={() => ((sorting = false), clearPicked())} />
{/if}

{#if picking}
  <ScopePicker current={range} onpick={setRange} onclose={() => (picking = false)} />
{/if}

{#if viewing !== null}
  <Viewer {items} start={viewing} onclose={() => (viewing = null)} onneed={(i) => need(i)} />
{/if}

<style>
  .gallery {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
  }
  .bar {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 10px 10px 6px 12px;
  }
  .lead {
    position: relative;
    display: flex;
    gap: 6px;
    flex: 1;
    min-width: 0;
  }
  .lead .chip {
    flex: 0 1 auto; /* the names give way on a narrow phone (the range's first, then the kind's) ... */
    min-width: 42px; /* ... but never the icon */
    overflow: hidden;
  }
  .lead .chip:first-child {
    flex-shrink: 9999; /* all of the range's name goes before any of the kind's */
  }
  .lead .chip :global(svg) {
    flex: none;
  }
  .chip .ic {
    display: flex;
    margin-left: 2px;
  }
  .menu.left {
    left: 0;
    right: auto;
    width: 230px;
  }
  .msep {
    height: 1px;
    margin: 5px 8px;
    background: var(--line);
  }
  .item:disabled {
    opacity: 0.35;
    cursor: default;
  }
  .item.on {
    font-weight: 700;
  }
  .chip {
    flex: none;
    padding: 6px 14px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: transparent;
    color: var(--muted);
    cursor: pointer;
    font-size: 13px;
    transition: all 150ms var(--ease-out);
  }
  .chip.on {
    background: var(--surface-2);
    color: var(--text);
    border-color: var(--strong-line);
  }
  .chip.icon {
    display: flex;
    align-items: center;
    gap: 5px;
    padding: 6px 12px;
  }
  .chip .lbl {
    max-width: 120px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .tools {
    position: relative;
    flex: none;
    display: flex;
    gap: 2px;
  }
  /* a phone: the tools spread over the room the left group leaves (no hole in the middle), up to an
     even spacing; the count stays just before them, and the kind's name keeps its room first */
  @media (max-width: 759px) {
    .lead {
      flex: 0 1 auto;
    }
    .lead ~ .count {
      margin-left: auto;
    }
    .lead ~ .tools {
      gap: 12px; /* fixed: switching ♥ / ★ or a kind's name never moves them */
    }
  }
  .tool {
    width: 34px;
    height: 34px;
    border-radius: 50%;
    border: none;
    display: grid;
    place-items: center;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
  }
  .tool:hover,
  .tool.on {
    background: var(--surface-2);
    color: var(--text);
  }
  .tool:disabled {
    opacity: 0.3;
  }
  .tool.kind.like.on {
    color: var(--like);
    background: color-mix(in srgb, var(--like) 18%, transparent);
    box-shadow: 0 0 14px color-mix(in srgb, var(--like) 45%, transparent);
  }
  .tool.kind.super.on {
    color: var(--super);
    background: color-mix(in srgb, var(--super) 18%, transparent);
    box-shadow: 0 0 14px color-mix(in srgb, var(--super) 45%, transparent);
  }
  .modal {
    position: fixed;
    inset: 0;
    z-index: 40;
    display: grid;
    place-items: center;
    padding: 16px;
    background: rgba(0, 0, 0, 0.6);
    backdrop-filter: blur(6px);
  }
  .ask {
    width: min(420px, 100%);
    display: flex;
    flex-direction: column;
    gap: 12px;
    padding: 20px;
    border-radius: 20px;
    background: var(--surface);
    border: 1px solid var(--line);
  }
  .ask p {
    margin: 0;
    color: var(--muted);
    font-size: 14px;
    line-height: 1.6;
  }
  .ask .row {
    display: flex;
    justify-content: flex-end;
    gap: 8px;
  }
  .btn {
    padding: 9px 18px;
    border-radius: 999px;
    border: 1px solid var(--line);
    background: transparent;
    cursor: pointer;
  }
  .btn.danger {
    background: var(--pass);
    border-color: transparent;
    color: #fff;
    font-weight: 700;
  }
  .btn:disabled {
    opacity: 0.5;
  }
  .scrim {
    position: fixed;
    inset: 0;
    z-index: 20;
  }
  .menu {
    position: absolute;
    right: 0;
    top: calc(100% + 6px);
    z-index: 21;
    width: 290px;
    max-height: 60vh;
    overflow-y: auto;
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
      translate: 0 -6px;
    }
  }
  .item {
    flex: 1;
    width: 100%;
    min-width: 0;
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
    overflow: hidden;
    text-overflow: ellipsis;
    cursor: pointer;
  }
  .item:hover {
    background: rgba(255, 255, 255, 0.07);
  }
  .ck {
    flex: none;
    width: 16px;
    display: flex;
    color: var(--accent-ink);
  }
  .chip:disabled {
    opacity: 0.3;
  }
  .back {
    display: grid;
    place-items: center;
    width: 34px;
    height: 34px;
    border-radius: 50%;
    border: none;
    background: var(--surface-2);
    cursor: pointer;
  }
  .title {
    flex: 1;
    min-width: 0; /* a long folder name ends in … rather than pushing the tools off */
    font-weight: 700;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .count {
    color: var(--muted);
    font-size: 13px;
  }
  .scroller {
    flex: 1;
    overflow-x: hidden; /* tiles gliding in from the side (size change) draw no scrollbar */
    overflow-y: auto;
    touch-action: pan-y; /* one finger scrolls; two fingers size the tiles (not the page) */
    padding: 2px 3px 16px;
  }
  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(min(var(--tile, 140px), 100%), 1fr));
    gap: 3px;
  }
  .tile {
    position: relative;
    aspect-ratio: 1;
    border: none;
    padding: 0;
    border-radius: 6px;
    overflow: hidden;
    background: var(--surface);
    cursor: pointer;
    content-visibility: auto;
    contain-intrinsic-size: 200px;
    transition: transform 160ms var(--ease-out);
  }
  .tile:hover {
    transform: scale(1.02);
  }
  .tile img {
    width: 100%;
    height: 100%;
    object-fit: cover;
    display: block;
  }
  /* the whole list's grid (lib/vgrid.ts): as tall as all its tiles, the ones near the screen placed
     one by one (square, or the pictures' own shapes in rows edge to edge) */
  .grid.whole {
    display: block;
    position: relative;
  }
  .grid.whole .tile {
    position: absolute;
    aspect-ratio: auto;
    content-visibility: visible;
  }
  .tile.ghost {
    cursor: default; /* its page is on its way */
  }
  .shapes-sw {
    position: absolute;
    left: 14px;
    bottom: 14px;
    z-index: 5;
    width: 40px;
    height: 40px;
    padding: 0;
    border-radius: 50%;
    border: 1px solid rgba(255, 255, 255, 0.22);
    display: grid;
    place-items: center;
    background: rgba(16, 20, 26, 0.42);
    backdrop-filter: blur(10px);
    color: rgba(255, 255, 255, 0.88);
    cursor: pointer;
    box-shadow: 0 4px 14px rgba(0, 0, 0, 0.3);
    transition:
      color 160ms,
      background-color 160ms,
      transform 120ms var(--ease-out);
  }
  .shapes-sw:active {
    transform: scale(0.9);
  }
  .shapes-sw.on {
    color: var(--accent);
    background: rgba(16, 20, 26, 0.6);
    border-color: color-mix(in srgb, var(--accent) 55%, transparent);
  }
  .pts {
    position: absolute;
    right: 6px;
    bottom: 5px;
    display: flex;
    align-items: center;
    gap: 3px;
    font-size: 12.5px;
    filter: drop-shadow(0 1px 2px rgba(0, 0, 0, 0.85)) drop-shadow(0 0 6px rgba(0, 0, 0, 0.45));
  }
  .pts b {
    color: #f2f3f5; /* on its dark badge in both looks */
    font-weight: 600;
  }
  .folders .ftile {
    aspect-ratio: 0.8;
  }
  .ftile::after {
    content: '';
    position: absolute;
    inset: 40% 0 0;
    background: linear-gradient(transparent, rgba(0, 0, 0, 0.8));
  }
  .fname {
    position: absolute;
    left: 10px;
    right: 44px;
    bottom: 9px;
    z-index: 1;
    font-weight: 700;
    font-size: 13px;
    text-align: left;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .fcount {
    position: absolute;
    right: 10px;
    bottom: 9px;
    z-index: 1;
    font-size: 12px;
    color: rgba(255, 255, 255, 0.75);
  }
  .medal {
    position: absolute;
    left: 8px;
    top: 8px;
    width: 28px;
    height: 28px;
    border-radius: 50%;
    display: grid;
    place-items: center;
    font-weight: 900;
    color: #15171b;
    z-index: 1;
  }
  .m1 {
    background: #f4c542;
  }
  .m2 {
    background: #c9ced6;
  }
  .m3 {
    background: #d2905a;
  }
  .empty {
    display: grid;
    place-items: center;
    height: 50vh;
    color: var(--line);
  }
  /* selecting pictures */
  .tile.sel {
    outline: 3px solid var(--accent);
    outline-offset: -3px;
  }
  .tile.sel img {
    opacity: 0.75;
  }
  .tick {
    position: absolute;
    left: 6px;
    top: 6px;
    width: 22px;
    height: 22px;
    border-radius: 50%;
    border: 1.5px solid rgba(255, 255, 255, 0.9);
    background: rgba(0, 0, 0, 0.3);
    display: grid;
    place-items: center;
    color: #fff;
  }
  .tick.on {
    background: var(--accent);
    border-color: var(--accent);
  }
  .selbar {
    position: absolute;
    left: 10px;
    right: 10px;
    bottom: 12px;
    z-index: 6;
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 8px 8px 6px;
    border-radius: 999px;
    background: var(--menu-bg);
    border: 1px solid var(--line);
    backdrop-filter: blur(14px);
    box-shadow: 0 10px 30px rgba(0, 0, 0, 0.35);
    animation: selin 200ms var(--ease-out);
  }
  @keyframes selin {
    from {
      opacity: 0;
      translate: 0 12px;
    }
  }
  .selbar .num {
    flex: 1 0 auto;
    min-width: 0;
    font-size: 13.5px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .selbar .num.msg {
    flex: 1 1 0; /* a message may shorten; the count keeps its width */
  }
  .selbar .x {
    width: 34px;
    height: 34px;
    border: none;
    border-radius: 50%;
    display: grid;
    place-items: center;
    background: transparent;
    color: var(--muted);
    cursor: pointer;
  }
  .selbar .act {
    display: flex;
    align-items: center;
    min-width: 0;
    gap: 6px;
    padding: 9px 14px;
    border: none;
    border-radius: 999px;
    background: var(--accent);
    color: var(--on-accent);
    font-weight: 700;
    white-space: nowrap;
    cursor: pointer;
  }
  .selbar .act :global(svg) {
    flex-shrink: 0;
  }
  .selbar .act .lbl {
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .selbar .act.ghost {
    padding: 9px 11px;
    background: var(--surface-2);
    color: var(--text);
  }
</style>
