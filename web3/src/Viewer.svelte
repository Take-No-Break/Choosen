<script lang="ts">
  import { t } from './lib/i18n';
  // Full view of a gallery image: the whole picture over a blurred fill, with a soft glow of its own
  // colours around its edges, into which its own edge fades; ← → keys or a swipe sideways to the
  // neighbours, a swipe up or down (or Esc, ✕) to close. A touch shows the buttons and, beside 秒数,
  // the picture's rating history with its points; a few quiet seconds hide them again. Tapping the
  // history changes the picture's latest rating (未評価: gives it one); a double tap on the picture
  // adds a like each time, so its points go up. Pictures next to it are decoded ahead. bare (a picture opened from スライド or play): only the picture, a tap
  // closes it.
  // Slideshow: ▶ or Space; the next picture comes a set time after the current one has loaded, with a
  // slow zoom (the glow zooms with it) and a cross-fade; the controls hide while the mouse rests, the
  // screen stays on.
  import { onMount, untrack } from 'svelte';
  import { getJ, imgUrl, thumbUrl, placeOf, type Card } from './lib/api';
  import { newCid } from './lib/outbox';
  import { outbox } from './lib/ratings';
  import { feather } from './lib/feather';
  import { svg } from './ui/icons';
  import { pairOf, pinchZoom, NO_ZOOM, Taps, type Pair, type Zoom } from './lib/gesture';
  import { NO_PAGE, pagesOf, pageOf, ratedOf, shownOf, stepPage, tapSide } from './lib/pages';
  import { PageKeyHold, PageTaps } from './lib/taps';

  // items: the gallery's whole list, a slot empty until its page is loaded (onneed(i) asks for it)
  let {
    items,
    start,
    onclose,
    onneed,
    bare = false,
  }: { items: (Card | undefined)[]; start: number; onclose: () => void; onneed: (i: number) => void; bare?: boolean } = $props();

  // svelte-ignore state_referenced_locally
  let index = $state(start); // where it opens; then it moves on its own
  let ready = $state(false);
  const card = $derived(items[index]);
  // a work of several pages: the page on screen (Shift, ↓ / ↑, . / ,, a tap or the badge turns it; a
  // rating goes to the whole work, or with grouping off to the page on screen)
  let flipAt = $state.raw(NO_PAGE);
  const pages = $derived(pagesOf(card));
  const page = $derived(pageOf(card, flipAt));
  const subject = $derived(card && ratedOf(card, page)); // what the ratings and the history are of
  const pageTaps = new PageTaps();
  const pageHold = new PageKeyHold();
  // a held page key runs through the work and stops at its end
  const heldTurn = (d: 1 | -1) => (first: boolean) => {
    if (!pages || (!first && (page + d < 0 || page + d >= pages.length))) return false;
    flip(d);
    return true;
  };
  function flip(d: number) {
    if (!card || !pages) return;
    flipAt = { fp: card.fp, k: stepPage(page, pages.length, d) };
    pageTaps.turned(performance.now());
    setZoom(NO_ZOOM, false);
  }

  function go(d: number) {
    const n = index + d;
    if (n < 0 || n >= items.length) return;
    pageTaps.cancel();
    index = n;
    ready = false;
    if (!items[n] || !items[Math.min(items.length - 1, n + 10)]) onneed(n);
  }

  // ---- slideshow
  const SECONDS = [3, 5, 8, 12];
  let playing = $state(false);
  let seconds = $state(readSeconds());
  let idle = $state(false);
  let lock: { release: () => Promise<void> } | null = null;
  let idleTimer = 0;

  function readSeconds() {
    try {
      const v = Number(localStorage.getItem('choosen3.slideSeconds'));
      return SECONDS.includes(v) ? v : 5;
    } catch {
      return 5;
    }
  }
  function nextSeconds() {
    seconds = SECONDS[(SECONDS.indexOf(seconds) + 1) % SECONDS.length];
    try {
      localStorage.setItem('choosen3.slideSeconds', String(seconds));
    } catch {
      /* private mode */
    }
  }
  async function play(on: boolean) {
    playing = on;
    wake();
    const nav = navigator as Navigator & { wakeLock?: { request: (t: 'screen') => Promise<typeof lock> } };
    try {
      if (on) lock = (await nav.wakeLock?.request('screen')) ?? null;
      else {
        await lock?.release();
        lock = null;
      }
    } catch {
      lock = null; // not allowed here: the screen may dim, the show goes on
    }
  }
  // the next picture: a set time after this one is on screen; after the last, back to the first
  $effect(() => {
    if (!playing || !ready) return;
    const t = setTimeout(() => {
      if (index < items.length - 1) go(1);
      else if (index > 0) (index = 0), (ready = false); // a single picture just stays on screen
    }, seconds * 1000);
    return () => clearTimeout(t);
  });
  function wake() {
    idle = false;
    clearTimeout(idleTimer);
    idleTimer = window.setTimeout(quiet, playing ? 2200 : 3500); // the choices close with the buttons
  }
  /** A few quiet seconds: the buttons step aside, unless a mouse rests on the rating choices (or they have the focus). */
  function quiet() {
    if (editing && viewer?.querySelector('.edit:hover, .history:hover, .edit:focus-within')) return void wake();
    idle = true;
    editing = false;
  }

  // ---- the picture's ratings, oldest first, with the points after each (beside 秒数)
  interface Rated {
    a: string;
    ms: number;
    p: number;
    ref: string; // how /api/rate's replace names it: its rating id, or "tick-<n>" for an old one
  }
  let history = $state.raw<Rated[]>([]);
  let historyReady = $state(false); // the choices open only once the history is known
  let editing = $state(false);
  let hearts = $state(0); // a double tap's heart (its key replays the pop)
  let pending = $state(0); // this picture's ratings on their way (the outbox keeps and retries them)
  let historySeq = 0; // only the newest history answer counts
  function loadHistory(c: Card) {
    const seq = ++historySeq;
    getJ<{ events: Rated[] }>('/api/history', { id: c.id, fp: c.fp })
      .then((r) => {
        if (c !== subject || seq !== historySeq) return;
        history = r.events;
        // a correction on its way: the history still shows what it replaces, so it waits for the next
        historyReady = !outbox.pendingFor(c.fp).some((x) => x.replace);
      })
      .catch(() => {});
  }
  $effect(() => {
    const c = subject;
    history = [];
    historyReady = false;
    editing = false;
    chosen = -1;
    pending = c ? outbox.pendingFor(c.fp).length : 0; // also ratings made before it was left
    if (!c || bare) return;
    loadHistory(c);
  });
  // a rating of this picture saved (or queued, from here or the swipe screen): count and history follow
  onMount(() =>
    outbox.watch(() => {
      const c = subject;
      if (!c || bare) return;
      const now = outbox.pendingFor(c.fp).length;
      if (now < pending) loadHistory(c);
      pending = now;
    }),
  );
  /**
   * Rates the picture through the page's outbox: kept until saved, sent in order, retried. replace:
   * in place of its newest rating ("latest": the core takes the newest one when it applies the
   * request, so quick changes replace one another, also after leaving the picture or a change made
   * on another device).
   */
  function rate(a: 'L' | 'S' | 'D' | 'A', replace: boolean) {
    const c = subject;
    if (!c || (replace && !historyReady)) return;
    // the newest one: named "latest" (the core picks it as it applies the request, so quick changes
    // replace one another); an earlier one: by its own ref (its rating id, or tick-<n> for an old one)
    const older = replace && chosen >= 0 && chosen < history.length - 1 ? history[chosen].ref : '';
    if (replace) (historyReady = false), (editing = false); // until the history shows this correction
    void outbox.push({ id: c.id, fp: c.fp, a, cid: newCid(), replace: !replace ? '' : older || 'latest' });
  }
  let chosen = $state(-1); // the history entry the choices change (-1: the newest, or none yet)
  /** A mouse wheel scrolls the history row sideways (its older entries), when it is longer than its room. */
  function historyWheel(e: WheelEvent) {
    const row = e.currentTarget as HTMLElement;
    if (row.scrollWidth <= row.clientWidth || Math.abs(e.deltaX) > Math.abs(e.deltaY)) return;
    row.scrollLeft += e.deltaY;
    e.preventDefault();
  }
  /** The history row scrolls sideways; it shows its newest end, also when a rating is added. */
  function toEnd(node: HTMLElement, _n: number) {
    const go = () => (node.scrollLeft = node.scrollWidth);
    go();
    return { update: go }; // after the row has its new entries
  }
  function pick(k: number) {
    wake();
    if (editing && chosen === k) return void (editing = false);
    chosen = k;
    editing = true;
  }
  const CHOICES: ['L' | 'S' | 'D' | 'A', string, string][] = [
    ['L', 'heart', 'var(--like)'],
    ['S', 'star', 'var(--super)'],
    ['D', 'x', 'var(--pass)'],
    ['A', 'archive', 'var(--archive, #8c919b)'],
  ];
  const CHOICE_LABEL: Record<string, string> = { L: t('好評'), S: t('超好評'), D: t('見送り'), A: t('アーカイブ') };
  const SIGN: Record<string, [string, string]> = {
    L: ['heart', 'var(--like)'],
    S: ['star', 'var(--super)'],
    D: ['x', 'var(--pass)'],
    A: ['archive', 'var(--archive, #8c919b)'],
  };

  $effect(() => {
    for (const k of [1, 2, -1]) {
      const c = items[index + k];
      if (!c) continue;
      new Image().src = imgUrl(c);
      new Image().src = thumbUrl(c, 320); // its glow, there together with the picture
    }
    if (pages) new Image().src = imgUrl(pages[stepPage(page, pages.length, 1)]); // the next page
  });

  function onKey(e: KeyboardEvent) {
    pageHold.saw(e.code, e.shiftKey);
    const plain = !e.ctrlKey && !e.altKey && !e.metaKey; // Alt+Shift, Win+Shift+S ... are not ours
    if (e.key === 'Escape' && editing) editing = false; // the choices first, then the view
    else if (e.key === 'Escape') onclose();
    else if (e.key === 'ArrowRight') go(1);
    else if (e.key === 'ArrowLeft') go(-1);
    else if (pages && plain && (e.code === 'ShiftLeft' || e.code === 'ShiftRight' || e.key === 'ArrowDown' || e.code === 'Period' || e.key === 'PageDown')) {
      if (!e.repeat) pageHold.press(e.code, heldTurn(1));
    } else if (pages && plain && (e.key === 'ArrowUp' || e.code === 'Comma' || e.key === 'PageUp')) {
      if (!e.repeat) pageHold.press(e.code, heldTurn(-1));
    } else if (e.code === 'Space') void play(!playing);
    else return;
    e.preventDefault();
    e.stopPropagation();
  }

  // A swipe sideways goes to the neighbour. Two fingers zoom (1x to 5x) and it stays zoomed; one
  // finger then moves the picture, a double tap brings it back to its size. A new picture starts at
  // its size.
  let sx = 0;
  let viewer: HTMLDivElement;
  const fingers = new Map<number, { x: number; y: number }>();
  let pinch: { start: Pair; z0: Zoom } | null = null;
  let pinched = false;
  let zoom: Zoom = NO_ZOOM;
  let pan: { id: number; x0: number; y0: number; z0: Zoom } | null = null;
  let moved = 0;
  let sy = 0;
  let dropY = $state(0); // px the picture follows a finger up or down (to close)
  const taps = new Taps();
  function setZoom(z: Zoom, animate: boolean) {
    zoom = z;
    viewer.classList.toggle('zooming', !animate);
    viewer.style.setProperty('--zs', String(z.s));
    viewer.style.setProperty('--zx', `${z.tx}px`);
    viewer.style.setProperty('--zy', `${z.ty}px`);
  }
  $effect(() => {
    void index;
    untrack(() => {
      flipAt = NO_PAGE;
      pageTaps.reset();
      pageHold.release();
      if (viewer) setZoom(NO_ZOOM, false);
    });
  });
  function down(e: PointerEvent) {
    wake();
    pageTaps.cancel(); // another touch: the waiting tap was not a lone one
    sx = e.clientX;
    sy = e.clientY;
    moved = 0;
    if (e.pointerType !== 'touch') return;
    if (e.isPrimary) fingers.clear(); // a new touch: no other finger is down
    fingers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (fingers.size === 2) {
      const [a, b] = [...fingers.values()];
      pinch = { start: pairOf(a, b), z0: zoom };
      pinched = true;
      pan = null;
      taps.reset();
    } else if (zoom.s > 1.01) {
      pan = { id: e.pointerId, x0: e.clientX, y0: e.clientY, z0: zoom };
    }
  }
  function move(e: PointerEvent) {
    wake();
    moved = Math.max(moved, Math.abs(e.clientX - sx));
    if (!fingers.has(e.pointerId)) return;
    fingers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pinch && fingers.size >= 2) {
      const [a, b] = [...fingers.values()];
      setZoom(pinchZoom(pinch.z0, pinch.start, pairOf(a, b)), false);
    } else if (pan && e.pointerId === pan.id) {
      setZoom({ s: pan.z0.s, tx: pan.z0.tx + e.clientX - pan.x0, ty: pan.z0.ty + e.clientY - pan.y0 }, false);
    } else if (!pinched && fingers.size === 1 && zoom.s <= 1.01) {
      const dy = e.clientY - sy;
      if (dropY || (Math.abs(dy) > 12 && Math.abs(dy) > Math.abs(e.clientX - sx))) dropY = dy;
    }
  }
  function up(e: PointerEvent) {
    fingers.delete(e.pointerId);
    if (pinch && fingers.size < 2) {
      pinch = null;
      viewer.classList.remove('zooming');
      if (zoom.s < 1.05) setZoom(NO_ZOOM, true);
    }
    if (pinched) {
      if (!fingers.size) pinched = false;
      return;
    }
    if (dropY) {
      const far = Math.abs(dropY) > 90;
      dropY = 0;
      if (far && e.type !== 'pointercancel') onclose(); // swiped up or down: closed
      return;
    }
    // a button's own tap, or a tap on the controls around them (the choices, the history, the
    // names), is not the picture's; nor is a mouse's right or middle button
    if ((e.target as Element | null)?.closest?.('button, .show, .edit, .info, .history')) return;
    if (e.pointerType === 'mouse' && e.button !== 0) return;
    const isTap = moved < 10 && Math.abs(e.clientY - sy) < 10;
    const lone = isTap && e.pointerType !== 'mouse' && !!pages && !bare && zoom.s <= 1.01 && e.type !== 'pointercancel';
    const side = tapSide(e.clientX, 0, innerWidth); // the right half: the next page, the left half: back
    if (lone && pageTaps.turning(e.timeStamp)) return void (taps.reset(), flip(side));
    const n = isTap ? taps.tap(e.clientX, e.clientY, e.timeStamp) : 0;
    if (n === 1 && lone) {
      const c = card;
      pageTaps.later(() => card === c && (flip(side), true));
    } else if (n >= 2) pageTaps.cancel();
    const tapped = n === 2;
    if (bare && n === 1 && zoom.s <= 1.01 && e.type !== 'pointercancel') return void onclose(); // a tap: back
    if (pan) {
      pan = null;
      viewer.classList.remove('zooming');
      if (tapped) setZoom(NO_ZOOM, true); // a double tap: back to its size
      return;
    }
    if (tapped && !bare && zoom.s <= 1.01) {
      taps.reset(); // the next two taps are a double tap of their own
      hearts++; // a double tap on the picture: one more like
      rate('L', false);
      return;
    }
    const d = e.clientX - sx;
    if (Math.abs(d) > 60) go(d < 0 ? 1 : -1);
  }
  onMount(() => {
    const keyUp = (e: KeyboardEvent) => {
      pageHold.release(e.code);
      // a Shift whose keydown the Japanese IME took (its keyup still comes): one page on
      if (pageHold.missed(e.code, e.shiftKey) && e.code.startsWith('Shift') && pages && !editing && !e.ctrlKey && !e.altKey && !e.metaKey) flip(1);
    };
    const blur = () => pageHold.release();
    addEventListener('keydown', onKey, true); // before the swipe screen's keys
    addEventListener('keyup', keyUp, true);
    addEventListener('blur', blur);
    return () => {
      removeEventListener('keydown', onKey, true);
      removeEventListener('keyup', keyUp, true);
      removeEventListener('blur', blur);
      pageHold.release();
      clearTimeout(idleTimer);
      void lock?.release();
    };
  });
</script>

<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
  class="viewer"
  bind:this={viewer}
  class:playing
  class:idle
  class:bare
  class:dropping={dropY !== 0}
  style:--dy={`${dropY}px`}
  style:--drop={Math.min(1, Math.abs(dropY) / 260)}
  style:--show={`${seconds + 1.5}s`}
  onpointermove={move}
  onpointerdown={down}
  onpointerup={up}
  onpointercancel={up}
>
  {#if card}
    {@const pic = shownOf(card, page)}
    {#key card.id}
      <img class="fill" src={thumbUrl(pic, 64)} alt="" />
      <div class="waves"></div>
      <img class="glow" class:ready class:kb={playing} src={thumbUrl(pic, 320)} alt="" />
      <img class="lqip feather" class:ready src={thumbUrl(pic, 320)} alt="" use:feather />
      <img class="photo feather" class:ready class:kb={playing} src={imgUrl(pic)} alt="" onload={() => (ready = true)} draggable="false" use:feather />
    {/key}
    <div class="info">
      <div class="place">{placeOf(card.dir)}</div>
      <div class="name">{pic.name}</div>
    </div>
    {#if pages}
      <!-- which page of the work is on screen; a tap (or click) turns to the next one -->
      <button class="pg num" title={t('次のページ（Shift / タップ）')} onclick={() => flip(1)}>{@html svg('layers', 13)}{page + 1} / {pages.length}</button>
    {/if}
  {/if}
  <div class="edge-shade" aria-hidden="true"></div>
  <button class="close" title={t('閉じる')} onclick={onclose}>{@html svg('x', 22)}</button>
  <div class="show">
    {#if !bare && card}
      <!-- each rating is its own button: a tap opens the choices for that one (the newest by default) -->
      <div class="history num" class:open={editing} class:pending={pending > 0} role="group" aria-label={t('評価を変える')} use:toEnd={history.length} onwheel={historyWheel}>
        {#each history as h, at (at)}
          {@const sign = SIGN[h.a] ?? SIGN.D}
          <button class="ev" class:chosen={editing && chosen === at} style:color={sign[1]} disabled={!historyReady} title={t('この評価を変える')} onclick={() => pick(at)}
            >{@html svg(sign[0] as 'heart', 13)}{#if h.a === 'L' || h.a === 'S'}<b>{h.p}</b>{/if}</button
          >
        {:else}
          <button class="ev none" disabled={!historyReady} title={t('評価を変える')} onclick={() => pick(-1)}>{t('未評価')}</button>
        {/each}
        {#if pending}<span class="saving" title={t('保存中…')}>…</span>{/if}
      </div>
    {/if}
    <button class="secs num" title={t('1枚の表示時間')} onclick={nextSeconds}>{seconds}{t('秒')}</button>
    <button class="playbtn" title={playing ? t('止める（Space）') : t('スライドショー（Space）')} onclick={() => play(!playing)}>
      {@html svg(playing ? 'pause' : 'play', 20)}
    </button>
  </div>
  {#if editing}
    <div class="edit" role="group" aria-label={t('評価を変える')}>
      {#each CHOICES as [a, icon, color] (a)}
        <button class="choice" style:color title={CHOICE_LABEL[a]} onclick={() => ((editing = false), rate(a, true))}>{@html svg(icon as 'heart', 22)}</button>
      {/each}
    </div>
  {/if}
  {#key hearts}
    {#if hearts}<span class="heart-pop">{@html svg('heart', 96)}</span>{/if}
  {/key}
  <span class="pos num">{index + 1} / {items.length}</span>
</div>

<style>
  .viewer {
    position: fixed;
    inset: 0;
    z-index: 20;
    background: #000;
    touch-action: none; /* two fingers zoom the picture, not the page */
    animation: fade 160ms var(--ease-out);
  }
  @keyframes fade {
    from {
      opacity: 0;
      scale: 0.97;
    }
  }
  img {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    pointer-events: none;
  }
  .fill {
    object-fit: cover;
    filter: blur(18px) brightness(0.7);
    transform: scale(1.2);
  }
  /* the glow, the placeholder and the picture move as one (pinch zoom, the slideshow's zoom) */
  .glow,
  .lqip,
  .photo {
    object-fit: contain;
    transform: translate(var(--zx, 0px), calc(var(--zy, 0px) + var(--dy, 0px))) scale(var(--zs, 1));
    transform-origin: 0 0;
  }
  /* a wide, lively blur of the picture behind it: only its rim shows, as a glow. It comes with the
     picture, so nothing blurry shows before the picture does */
  .glow {
    filter: blur(14px) saturate(1.3) brightness(1.1);
  }
  .lqip {
    filter: blur(6px);
    /* steps aside once the picture is fully in: one glow, the same still or playing */
    transition:
      opacity 200ms linear 160ms,
      transform 280ms var(--ease-out);
  }
  .lqip.ready {
    opacity: 0;
  }
  .glow,
  .photo {
    opacity: 0;
    transition:
      opacity 160ms linear,
      transform 280ms var(--ease-out);
  }
  .glow.ready,
  .photo.ready {
    opacity: 1;
  }
  .viewer:global(.zooming) .glow,
  .viewer:global(.zooming) .photo,
  .viewer:global(.zooming) .lqip {
    transition: opacity 160ms linear;
  }
  .playing .photo {
    transition-duration: 700ms;
  }
  .playing .glow {
    transition:
      opacity 300ms linear 450ms,
      transform 700ms var(--ease-out);
  }
  .viewer.playing:global(.zooming) .glow {
    transition: opacity 160ms linear;
  }
  .playing .lqip {
    opacity: 0; /* a cross-fade from black-blur, not a blurry flash */
  }
  .glow.kb,
  .photo.kb {
    transform-origin: 50% 50%;
    animation: kb var(--show) linear both;
  }
  @keyframes kb {
    from {
      scale: 1;
    }
    to {
      scale: 1.06;
    }
  }
  /* a work's page badge, top left (the position counter is elsewhere): it turns to the next page */
  .pg {
    position: absolute;
    top: calc(var(--safe-top, env(safe-area-inset-top, 0px)) + 14px);
    left: 14px;
    z-index: 2;
    display: inline-flex;
    align-items: center;
    gap: 5px;
    min-height: 34px;
    padding: 4px 12px;
    border: none;
    border-radius: 999px;
    background: rgba(0, 0, 0, 0.45);
    color: #fff;
    font-size: 13px;
    font-weight: 700;
    cursor: pointer;
    transition: opacity 400ms;
  }
  .viewer.idle .pg {
    opacity: 0;
  }
  /* Screen shading stays while controls hide, independent of picture zoom. */
  .edge-shade {
    position: absolute;
    inset: 0;
    z-index: 1;
    pointer-events: none;
    background: linear-gradient(to bottom, rgba(0, 0, 0, 0.5), transparent 18%, transparent 82%, rgba(0, 0, 0, 0.5));
  }
  .viewer > button,
  .show,
  .edit,
  .info,
  .pos {
    z-index: 2;
    transition: opacity 400ms;
  }
  .viewer.idle {
    cursor: none;
  }
  .viewer.idle > button,
  .viewer.idle .show,
  .viewer.idle .edit,
  .viewer.idle .info,
  .viewer.idle .pos {
    opacity: 0;
  }
  .show {
    position: absolute;
    top: calc(var(--safe-top, 0px) + 14px);
    right: 68px;
    display: flex;
    gap: 6px;
  }
  .show button {
    position: static;
  }
  .secs {
    width: auto !important;
    padding: 0 12px;
    border-radius: 999px !important;
    font-size: 13px;
  }
  .info {
    position: absolute;
    left: 18px;
    bottom: 16px;
    text-shadow: 0 1px 10px #000;
  }
  .place {
    font-weight: 800;
  }
  .name {
    font-size: 12px;
    color: rgba(255, 255, 255, 0.7);
  }
  button {
    position: absolute;
    border: none;
    border-radius: 50%;
    width: 44px;
    height: 44px;
    display: grid;
    place-items: center;
    background: rgba(0, 0, 0, 0.45);
    color: #fff;
    cursor: pointer;
  }
  .close {
    top: calc(var(--safe-top, 0px) + 14px);
    right: 14px;
  }
  .pos {
    position: absolute;
    right: 18px;
    bottom: 18px;
    font-size: 12px;
    color: rgba(255, 255, 255, 0.7);
  }
  /* a swipe up or down: the picture follows the finger, the view fades, far enough it closes */
  .viewer.dropping {
    background: rgba(0, 0, 0, calc(1 - var(--drop) * 0.7));
  }
  .viewer.dropping .fill {
    opacity: calc(1 - var(--drop));
  }
  .viewer.dropping .photo,
  .viewer.dropping .lqip,
  .viewer.dropping .glow {
    transition: none;
  }
  /* the rating history beside 秒数; a tap opens the choices below it */
  .history {
    display: flex;
    align-items: center;
    gap: 6px;
    width: auto !important;
    padding: 0 12px;
    height: 44px;
    border-radius: 999px !important;
    background: rgba(0, 0, 0, 0.45);
    font-size: 12px;
    white-space: nowrap;
  }
  .history {
    max-width: calc(100vw - 190px); /* beside 秒数 and ▶ (and ✕): longer histories scroll */
    overflow-x: auto;
    scrollbar-width: none;
  }
  .history::-webkit-scrollbar {
    display: none;
  }
  .history .ev,
  .history .saving {
    flex: none;
  }
  .history.open {
    box-shadow: inset 0 0 0 1.5px rgba(255, 255, 255, 0.55);
  }
  /* a rating in the history: tap it to change it */
  .history .ev {
    position: static;
    width: auto;
    height: 32px;
    padding: 0 4px;
    border-radius: 999px;
    background: transparent;
    display: flex;
    align-items: center;
    gap: 2px;
    font: inherit;
    color: inherit;
  }
  .history .ev.chosen {
    background: rgba(255, 255, 255, 0.18);
    box-shadow: inset 0 0 0 1.5px rgba(255, 255, 255, 0.6);
  }
  .history .none {
    color: rgba(255, 255, 255, 0.75);
  }
  /* a rating on its way (the outbox keeps it and retries until it is saved) */
  .history .saving {
    color: rgba(255, 255, 255, 0.75);
    animation: saving 1s ease-in-out infinite alternate;
  }
  @keyframes saving {
    from {
      opacity: 0.35;
    }
  }
  .edit {
    position: absolute;
    top: calc(var(--safe-top, 0px) + 66px);
    right: 68px;
    display: flex;
    gap: 8px;
    padding: 6px;
    border-radius: 999px;
    background: rgba(0, 0, 0, 0.55);
    animation: fade 140ms var(--ease-out);
  }
  .edit .choice {
    position: static;
  }
  /* a double tap's like */
  .heart-pop {
    position: absolute;
    left: 50%;
    top: 50%;
    translate: -50% -50%;
    color: var(--like);
    filter: drop-shadow(0 4px 18px rgba(0, 0, 0, 0.45));
    pointer-events: none;
    animation: heart 700ms var(--ease-out) both;
  }
  @keyframes heart {
    0% {
      opacity: 0;
      scale: 0.4;
    }
    25% {
      opacity: 1;
      scale: 1.15;
    }
    60% {
      opacity: 1;
      scale: 1;
    }
    100% {
      opacity: 0;
      scale: 1.05;
    }
  }
  .ev {
    display: flex;
    align-items: center;
    gap: 2px;
  }
  .ev b {
    color: #f2f3f5;
    font-weight: 600;
  }
  /* bare: only the picture (opened from スライド or play) */
  .viewer.bare > button,
  .viewer.bare .show,
  .viewer.bare .edit,
  .viewer.bare .info,
  .viewer.bare .pos {
    display: none;
  }
</style>
